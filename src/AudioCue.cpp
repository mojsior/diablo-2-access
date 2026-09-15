#include "AudioCue.hpp"

#include "Logging.hpp"

#include <Windows.h>
#include <Shlwapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <xaudio2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "xaudio2.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")

namespace d2access {
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

constexpr UINT32 OutputRate = 44100;
constexpr UINT32 OutputChannels = 2;
constexpr UINT32 MaxMasterChannels = 32;
constexpr size_t CueCount = static_cast<size_t>(CueId::Count);
constexpr size_t MaxEmitters = 3;
constexpr size_t CueVoiceCount = 4;

// DevilutionX utils/soundsample.cpp and engine/sound_defs.hpp.
constexpr float LogBase = 10.0F;
constexpr float VolumeScale = 3321.9281F;
constexpr float MillibelMin = -10000.0F;
constexpr float StereoSeparation = 6000.0F;
constexpr int AttenuationMin = -6400;

enum class CommandType {
    Emitter,
    StopEmitter,
    OneShot,
    Cue,
};

struct Command {
    CommandType type = CommandType::Cue;
    size_t slot = 0;
    CueId cue = CueId::InteractionPossible;
    int logVolume = 0;
    int logPan = 0;
    bool stopEmitters = false;
};

struct EmitterSlot {
    bool active = false;
    std::uint32_t emitterId = 0;
    CueId sound = CueId::Count;
    DWORD64 lastPlayMs = 0;
};

std::mutex g_commandMutex;
std::condition_variable g_commandCv;
std::deque<Command> g_commands;
std::thread g_audioThread;
std::atomic<bool> g_audioRunning = false;

// Emitter scheduling runs on the game thread, playback on the audio thread.
std::mutex g_emitterMutex;
std::array<EmitterSlot, MaxEmitters> g_emitterSlots{};

// Audio thread only.
IXAudio2 *g_xaudio = nullptr;
IXAudio2MasteringVoice *g_master = nullptr;
UINT32 g_masterChannels = OutputChannels;
std::array<std::vector<std::int16_t>, CueCount> g_sounds;
std::array<IXAudio2SourceVoice *, MaxEmitters> g_emitterVoices{};
IXAudio2SourceVoice *g_oneShotVoice = nullptr;
std::array<IXAudio2SourceVoice *, CueVoiceCount> g_cueVoices{};
size_t g_nextCueVoice = 0;

std::wstring AudioDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);
    return std::wstring(modulePath) + L"\\audio\\";
}

// Same sounds as Diablo Access (proximity_audio.cpp EnsureNavigationSoundsLoaded);
// Media Foundation has no Ogg decoder, so WAV and MP3 are used.
std::vector<const wchar_t *> CandidateFiles(CueId cue)
{
    switch (cue)
    {
    case CueId::InteractionPossible:
        return {L"interactispossible.wav", L"interactionispossible.wav", L"interactispossible.mp3",
                L"interactionispossible.mp3"};
    case CueId::Monster:
        return {L"monster.wav", L"monster.mp3"};
    case CueId::Chest:
        return {L"chest.wav", L"chest.mp3"};
    case CueId::Door:
        return {L"door.wav", L"Door.wav", L"door.mp3"};
    case CueId::Item:
        return {L"player_pickedup_item.wav", L"player_pickedup_item.mp3"};
    case CueId::Stairs:
        return {L"stairs.wav", L"Stairs.wav", L"stairs.mp3"};
    case CueId::Footstep:
        return {L"walk1.wav"};
    case CueId::Weapon:
        return {L"weapon.wav", L"weapon.mp3"};
    case CueId::Armor:
        return {L"armor.wav", L"armor.mp3"};
    case CueId::Gold:
        return {L"coin.wav", L"coin.mp3"};
    case CueId::Potion:
        return {L"potion.wav", L"Potion.wav", L"potion.mp3"};
    case CueId::Scroll:
        return {L"scroll.wav", L"Scroll.wav", L"scroll.mp3"};
    case CueId::Count:
        break;
    }
    return {};
}

// Decodes a WAV or MP3 file to 44.1 kHz, 16-bit stereo.
bool DecodeFile(const std::wstring &path, std::vector<std::int16_t> &out)
{
    out.clear();
    IMFSourceReader *reader = nullptr;
    if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader)))
        return false;

    const auto firstAudio = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    reader->SetStreamSelection(firstAudio, TRUE);

    bool ok = false;
    IMFMediaType *requested = nullptr;
    if (SUCCEEDED(MFCreateMediaType(&requested)))
    {
        requested->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        requested->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
        requested->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        ok = SUCCEEDED(reader->SetCurrentMediaType(firstAudio, nullptr, requested));
        requested->Release();
    }

    UINT32 channels = 0;
    UINT32 rate = 0;
    UINT32 bits = 0;
    if (ok)
    {
        IMFMediaType *actual = nullptr;
        ok = SUCCEEDED(reader->GetCurrentMediaType(firstAudio, &actual));
        if (ok)
        {
            channels = MFGetAttributeUINT32(actual, MF_MT_AUDIO_NUM_CHANNELS, 0);
            rate = MFGetAttributeUINT32(actual, MF_MT_AUDIO_SAMPLES_PER_SECOND, 0);
            bits = MFGetAttributeUINT32(actual, MF_MT_AUDIO_BITS_PER_SAMPLE, 0);
            actual->Release();
        }
    }
    ok = ok && channels > 0 && rate > 0 && bits == 16;

    std::vector<std::int16_t> pcm;
    while (ok)
    {
        DWORD flags = 0;
        IMFSample *sample = nullptr;
        if (FAILED(reader->ReadSample(firstAudio, 0, nullptr, &flags, nullptr, &sample)))
        {
            ok = false;
            break;
        }
        if (sample != nullptr)
        {
            IMFMediaBuffer *buffer = nullptr;
            if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer)))
            {
                BYTE *data = nullptr;
                DWORD length = 0;
                if (SUCCEEDED(buffer->Lock(&data, nullptr, &length)))
                {
                    const size_t count = length / sizeof(std::int16_t);
                    const size_t previous = pcm.size();
                    pcm.resize(previous + count);
                    std::memcpy(pcm.data() + previous, data, count * sizeof(std::int16_t));
                    buffer->Unlock();
                }
                buffer->Release();
            }
            sample->Release();
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0)
            break;
    }
    reader->Release();

    const size_t frames = channels > 0 ? pcm.size() / channels : 0;
    if (!ok || frames == 0)
        return false;

    const size_t outFrames = static_cast<size_t>(static_cast<std::uint64_t>(frames) * OutputRate / rate);
    out.resize(outFrames * OutputChannels);
    for (size_t i = 0; i < outFrames; ++i)
    {
        const double position = static_cast<double>(i) * rate / OutputRate;
        const size_t index = std::min(static_cast<size_t>(position), frames - 1);
        const size_t next = std::min(index + 1, frames - 1);
        const double fraction = position - static_cast<double>(index);
        for (UINT32 c = 0; c < OutputChannels; ++c)
        {
            const UINT32 source = channels == 1 ? 0 : c;
            const double a = pcm[index * channels + source];
            const double b = pcm[next * channels + source];
            out[i * OutputChannels + c] = static_cast<std::int16_t>(std::lround(a + (b - a) * fraction));
        }
    }
    return true;
}

void LoadSounds()
{
    const std::wstring directory = AudioDirectory();
    size_t loadedCount = 0;
    for (size_t i = 0; i < CueCount; ++i)
    {
        bool loaded = false;
        for (const wchar_t *name : CandidateFiles(static_cast<CueId>(i)))
        {
            const std::wstring path = directory + name;
            if (PathFileExistsW(path.c_str()) && DecodeFile(path, g_sounds[i]))
            {
                loaded = true;
                break;
            }
        }
        if (loaded)
            ++loadedCount;
        else if (static_cast<CueId>(i) != CueId::Footstep)
            LogLine(L"Audio cue missing or not decodable: sound " + std::to_wstring(i));
    }
    LogLine(L"Audio cues: loaded " + std::to_wstring(loadedCount) + L" sounds.");
}

// DevilutionX VolumeLogToLinear(logVolume, ATTENUATION_MIN, 0).
float VolumeLogToLinear(int logVolume)
{
    const float millibel = static_cast<float>(logVolume) * MillibelMin / static_cast<float>(AttenuationMin);
    return std::pow(LogBase, millibel / VolumeScale);
}

// DevilutionX PanLogToLinear.
float PanLogToLinear(int logPan)
{
    if (logPan == 0)
        return 0.0F;
    const float factor = std::pow(LogBase, static_cast<float>(-std::abs(logPan)) / StereoSeparation);
    return std::copysign(1.0F - factor, static_cast<float>(logPan));
}

IXAudio2SourceVoice *CreateVoice()
{
    WAVEFORMATEX format{};
    format.wFormatTag = WAVE_FORMAT_PCM;
    format.nChannels = OutputChannels;
    format.nSamplesPerSec = OutputRate;
    format.wBitsPerSample = 16;
    format.nBlockAlign = static_cast<WORD>(format.nChannels * format.wBitsPerSample / 8);
    format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
    IXAudio2SourceVoice *voice = nullptr;
    if (FAILED(g_xaudio->CreateSourceVoice(&voice, &format)))
        return nullptr;
    return voice;
}

void StopVoice(IXAudio2SourceVoice *voice)
{
    if (voice == nullptr)
        return;
    voice->Stop(0);
    voice->FlushSourceBuffers();
}

// Restarts the sound on the voice with the Aulib stereo mix: the side away from
// the pan loses volume, as in Aulib::Stream mixing.
void PlayOnVoice(IXAudio2SourceVoice *voice, CueId cue, int logVolume, int logPan)
{
    const size_t index = static_cast<size_t>(cue);
    if (voice == nullptr || index >= CueCount || g_sounds[index].empty())
        return;

    const float volume = VolumeLogToLinear(logVolume);
    const float pan = PanLogToLinear(logPan);
    float left = volume;
    float right = volume;
    if (pan < 0.0F)
        right *= 1.0F + pan;
    else if (pan > 0.0F)
        left *= 1.0F - pan;

    StopVoice(voice);
    XAUDIO2_VOICE_STATE state{};
    for (int i = 0; i < 20; ++i)
    {
        voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (state.BuffersQueued == 0)
            break;
        Sleep(1);
    }

    const std::vector<std::int16_t> &samples = g_sounds[index];
    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(samples.size() * sizeof(std::int16_t));
    buffer.pAudioData = reinterpret_cast<const BYTE *>(samples.data());
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    if (FAILED(voice->SubmitSourceBuffer(&buffer)))
        return;

    std::array<float, OutputChannels * MaxMasterChannels> matrix{};
    if (g_masterChannels >= 2)
    {
        matrix[0] = left;                   // source left  -> front left
        matrix[OutputChannels + 1] = right; // source right -> front right
    }
    else
    {
        matrix[0] = left * 0.5F;
        matrix[1] = right * 0.5F;
    }
    voice->SetOutputMatrix(nullptr, OutputChannels, g_masterChannels, matrix.data());
    voice->Start(0);
}

void ExecuteCommand(const Command &command)
{
    if (g_xaudio == nullptr)
        return;
    switch (command.type)
    {
    case CommandType::Emitter:
        PlayOnVoice(g_emitterVoices[command.slot], command.cue, command.logVolume, command.logPan);
        break;
    case CommandType::StopEmitter:
        StopVoice(g_emitterVoices[command.slot]);
        break;
    case CommandType::OneShot:
        if (command.stopEmitters)
        {
            for (IXAudio2SourceVoice *voice : g_emitterVoices)
                StopVoice(voice);
        }
        PlayOnVoice(g_oneShotVoice, command.cue, command.logVolume, command.logPan);
        break;
    case CommandType::Cue:
        PlayOnVoice(g_cueVoices[g_nextCueVoice], command.cue, 0, 0);
        g_nextCueVoice = (g_nextCueVoice + 1) % g_cueVoices.size();
        break;
    }
}

bool StartEngine()
{
    if (FAILED(XAudio2Create(&g_xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR)))
    {
        g_xaudio = nullptr;
        return false;
    }
    if (FAILED(g_xaudio->CreateMasteringVoice(&g_master)))
    {
        g_xaudio->Release();
        g_xaudio = nullptr;
        return false;
    }
    XAUDIO2_VOICE_DETAILS details{};
    g_master->GetVoiceDetails(&details);
    g_masterChannels = std::clamp<UINT32>(details.InputChannels, 1, MaxMasterChannels);

    for (IXAudio2SourceVoice *&voice : g_emitterVoices)
        voice = CreateVoice();
    g_oneShotVoice = CreateVoice();
    for (IXAudio2SourceVoice *&voice : g_cueVoices)
        voice = CreateVoice();
    return true;
}

void StopEngine()
{
    const auto destroy = [](IXAudio2SourceVoice *&voice) {
        if (voice != nullptr)
            voice->DestroyVoice();
        voice = nullptr;
    };
    for (IXAudio2SourceVoice *&voice : g_emitterVoices)
        destroy(voice);
    destroy(g_oneShotVoice);
    for (IXAudio2SourceVoice *&voice : g_cueVoices)
        destroy(voice);
    if (g_master != nullptr)
        g_master->DestroyVoice();
    g_master = nullptr;
    if (g_xaudio != nullptr)
        g_xaudio->Release();
    g_xaudio = nullptr;
}

void AudioWorkerMain()
{
    const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool mediaFoundation = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    if (mediaFoundation)
        LoadSounds();
    else
        LogLine(L"Audio cues: Media Foundation is not available.");
    if (!StartEngine())
        LogLine(L"Audio cues: XAudio2 could not be started.");
    else
        LogLine(L"Audio cues: XAudio2 ready.");

    for (;;)
    {
        Command command;
        {
            std::unique_lock lock(g_commandMutex);
            g_commandCv.wait(lock, [] { return !g_audioRunning || !g_commands.empty(); });
            if (!g_audioRunning)
                break;
            command = g_commands.front();
            g_commands.pop_front();
        }
        ExecuteCommand(command);
    }

    StopEngine();
    if (mediaFoundation)
        MFShutdown();
    if (SUCCEEDED(com))
        CoUninitialize();
}

void Enqueue(const Command &command)
{
    if (!g_audioRunning)
        return;
    {
        std::scoped_lock lock(g_commandMutex);
        if (g_commands.size() >= 64)
            g_commands.pop_front();
        g_commands.push_back(command);
    }
    g_commandCv.notify_one();
}

} // namespace

void InitializeAudioCue()
{
    if (!g_audioRunning.exchange(true))
        g_audioThread = std::thread(AudioWorkerMain);
}

void ShutdownAudioCue()
{
    if (!g_audioRunning.exchange(false))
        return;
    g_commandCv.notify_all();
    if (g_audioThread.joinable())
        g_audioThread.join();
}

void PlayCue(CueId cue)
{
    Command command;
    command.type = CommandType::Cue;
    command.cue = cue;
    Enqueue(command);
}

// Same bookkeeping as DevilutionX SoundPool::UpdateEmitters.
void UpdateEmitters(const std::vector<EmitterRequest> &emitters)
{
    const DWORD64 now = GetTickCount64();
    std::vector<Command> commands;
    {
        std::scoped_lock lock(g_emitterMutex);
        const auto isRequested = [&emitters](std::uint32_t emitterId) {
            return std::any_of(emitters.begin(), emitters.end(),
                               [emitterId](const EmitterRequest &request) { return request.emitterId == emitterId; });
        };

        for (size_t i = 0; i < g_emitterSlots.size(); ++i)
        {
            EmitterSlot &slot = g_emitterSlots[i];
            if (!slot.active || isRequested(slot.emitterId))
                continue;
            slot = EmitterSlot{};
            Command stop;
            stop.type = CommandType::StopEmitter;
            stop.slot = i;
            commands.push_back(stop);
        }

        for (size_t r = 0; r < emitters.size() && r < MaxEmitters; ++r)
        {
            const EmitterRequest &request = emitters[r];
            size_t slotIndex = MaxEmitters;
            for (size_t i = 0; i < g_emitterSlots.size(); ++i)
            {
                if (g_emitterSlots[i].active && g_emitterSlots[i].emitterId == request.emitterId)
                {
                    slotIndex = i;
                    break;
                }
            }

            bool isNew = false;
            if (slotIndex == MaxEmitters)
            {
                for (size_t i = 0; i < g_emitterSlots.size(); ++i)
                {
                    if (!g_emitterSlots[i].active)
                    {
                        g_emitterSlots[i] = EmitterSlot{true, request.emitterId, request.sound, now};
                        slotIndex = i;
                        isNew = true;
                        LogLine(L"Audio emitter started: id " + std::to_wstring(request.emitterId) + L", sound " +
                                std::to_wstring(static_cast<int>(request.sound)) + L", volume " +
                                std::to_wstring(request.logVolume) + L", pan " + std::to_wstring(request.logPan) +
                                L", interval " + std::to_wstring(request.intervalMs) + L" ms");
                        break;
                    }
                }
            }
            if (slotIndex == MaxEmitters)
                continue;

            EmitterSlot &slot = g_emitterSlots[slotIndex];
            slot.sound = request.sound;
            const bool shouldPlay = isNew || (request.intervalMs != 0 && now - slot.lastPlayMs >= request.intervalMs);
            if (!shouldPlay)
                continue;

            slot.lastPlayMs = now;
            Command play;
            play.type = CommandType::Emitter;
            play.slot = slotIndex;
            play.cue = request.sound;
            play.logVolume = request.logVolume;
            play.logPan = request.logPan;
            commands.push_back(play);
        }
    }
    for (const Command &command : commands)
        Enqueue(command);
}

void StopEmitters()
{
    std::vector<Command> commands;
    {
        std::scoped_lock lock(g_emitterMutex);
        for (size_t i = 0; i < g_emitterSlots.size(); ++i)
        {
            if (!g_emitterSlots[i].active)
                continue;
            g_emitterSlots[i] = EmitterSlot{};
            Command stop;
            stop.type = CommandType::StopEmitter;
            stop.slot = i;
            commands.push_back(stop);
        }
    }
    for (const Command &command : commands)
        Enqueue(command);
}

void PlayOneShot(CueId sound, int logVolume, int logPan, bool stopEmitters)
{
    if (stopEmitters)
    {
        std::scoped_lock lock(g_emitterMutex);
        g_emitterSlots.fill(EmitterSlot{});
    }
    Command command;
    command.type = CommandType::OneShot;
    command.cue = sound;
    command.logVolume = logVolume;
    command.logPan = logPan;
    command.stopEmitters = stopEmitters;
    Enqueue(command);
}

} // namespace d2access
