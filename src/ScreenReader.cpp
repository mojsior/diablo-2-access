#include "ScreenReader.hpp"

#include "EventLog.hpp"
#include "Logging.hpp"

#include <Windows.h>
#include <sapi.h>

#include <Shlwapi.h>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")

namespace d2access {
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

using NvdaSpeakFn = int(WINAPI *)(const wchar_t *);
using NvdaCancelFn = int(WINAPI *)(void);

struct SpeechRequest {
    std::wstring text;
    bool interrupt = true;
};

std::mutex g_mutex;
std::condition_variable g_cv;
std::queue<SpeechRequest> g_queue;
std::thread g_worker;
std::atomic<bool> g_running = false;

HMODULE g_nvdaModule = nullptr;
NvdaSpeakFn g_nvdaSpeak = nullptr;
NvdaCancelFn g_nvdaCancel = nullptr;
ISpVoice *g_voice = nullptr;

std::wstring GetModuleDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);
    return modulePath;
}

std::wstring GetEnvironmentString(const wchar_t *name)
{
    wchar_t buffer[MAX_PATH] = {};
    DWORD length = GetEnvironmentVariableW(name, buffer, MAX_PATH);
    if (length == 0 || length >= MAX_PATH)
        return {};
    return buffer;
}

bool TryLoadNvdaModulePath(const std::wstring &candidate)
{
    if (candidate.empty() || !std::filesystem::exists(candidate))
        return false;

    HMODULE module = LoadLibraryW(candidate.c_str());
    if (module == nullptr)
        return false;

    auto speak = reinterpret_cast<NvdaSpeakFn>(
        GetProcAddress(module, "nvdaController_speakText"));
    auto cancel = reinterpret_cast<NvdaCancelFn>(
        GetProcAddress(module, "nvdaController_cancelSpeech"));
    if (speak != nullptr)
    {
        g_nvdaModule = module;
        g_nvdaSpeak = speak;
        g_nvdaCancel = cancel;
        LogLine(L"Screen reader backend initialized: NVDA (" + candidate + L")");
        return true;
    }

    FreeLibrary(module);
    return false;
}

bool TryLoadNvdaFromBaseDir(const std::wstring &dir)
{
    if (dir.empty() || !std::filesystem::exists(dir))
        return false;

    const std::vector<std::wstring> candidates = {
        dir + L"\\nvdaControllerClient32.dll",
        dir + L"\\nvdaControllerClient.dll",
    };

    for (const auto &candidate : candidates)
    {
        if (TryLoadNvdaModulePath(candidate))
            return true;
    }

    return false;
}

void InitializeBackend()
{
    const auto moduleDir = GetModuleDirectory();
    const auto buildDir = std::filesystem::path(moduleDir).parent_path();
    const auto repoDir = buildDir.parent_path();
    const auto sourceRootDir = repoDir.parent_path();
    const auto userProfile = GetEnvironmentString(L"USERPROFILE");

    const std::vector<std::wstring> searchDirs = {
        moduleDir,
        buildDir.wstring(),
        (repoDir / L"third_party" / L"nvda").wstring(),
        (sourceRootDir / L"third_party" / L"nvda").wstring(),
        userProfile.empty() ? std::wstring() : (std::filesystem::path(userProfile) / L"Downloads").wstring(),
        L"C:\\Users\\mojsior\\Downloads",
        L"C:\\Program Files\\NVDA",
        L"C:\\Program Files (x86)\\NVDA",
    };

    for (const auto &dir : searchDirs)
    {
        if (TryLoadNvdaFromBaseDir(dir))
            return;
    }

    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    {
        if (SUCCEEDED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice,
                                       reinterpret_cast<void **>(&g_voice))))
        {
            LogLine(L"Screen reader backend initialized: SAPI");
            return;
        }
        CoUninitialize();
    }

    LogLine(L"Screen reader backend not available.");
}

void ShutdownBackend()
{
    if (g_voice != nullptr)
    {
        g_voice->Release();
        g_voice = nullptr;
        CoUninitialize();
    }

    if (g_nvdaModule != nullptr)
    {
        FreeLibrary(g_nvdaModule);
        g_nvdaModule = nullptr;
        g_nvdaSpeak = nullptr;
        g_nvdaCancel = nullptr;
    }
}

void SpeakInternal(const SpeechRequest &request)
{
    if (request.text.empty())
        return;

    if (g_nvdaSpeak != nullptr)
    {
        if (request.interrupt && g_nvdaCancel != nullptr)
            g_nvdaCancel();
        g_nvdaSpeak(request.text.c_str());
        return;
    }

    if (g_voice != nullptr)
    {
        g_voice->Speak(request.text.c_str(),
                       request.interrupt ? SPF_ASYNC | SPF_PURGEBEFORESPEAK : SPF_ASYNC,
                       nullptr);
    }
}

void WorkerMain()
{
    InitializeBackend();

    while (g_running)
    {
        SpeechRequest request;
        {
            std::unique_lock lock(g_mutex);
            g_cv.wait(lock, [] { return !g_running || !g_queue.empty(); });
            if (!g_running && g_queue.empty())
                break;

            request = std::move(g_queue.front());
            g_queue.pop();
        }

        SpeakInternal(request);
    }

    ShutdownBackend();
}

} // namespace

void InitializeScreenReader()
{
    if (g_running.exchange(true))
        return;

    g_worker = std::thread(WorkerMain);
}

void ShutdownScreenReader()
{
    if (!g_running.exchange(false))
        return;

    g_cv.notify_all();
    if (g_worker.joinable())
        g_worker.join();
}

void Speak(std::wstring_view text, bool interrupt)
{
    if (!text.empty())
        PushEvent("speech", text);
    if (!g_running || text.empty())
        return;

    {
        std::scoped_lock lock(g_mutex);
        if (interrupt)
        {
            std::queue<SpeechRequest> empty;
            std::swap(g_queue, empty);
        }
        g_queue.push(SpeechRequest { std::wstring(text), interrupt });
    }

    g_cv.notify_one();
}

} // namespace d2access
