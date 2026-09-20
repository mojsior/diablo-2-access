#include "ScreenReader.hpp"

#include "EventLog.hpp"
#include "Logging.hpp"

#include <Windows.h>
#include <sapi.h>

#include <Shlwapi.h>

#include <prism.h>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")

namespace d2access {
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

// Prism is loaded by hand rather than linked, so a missing or broken prism32.dll
// leaves the mod running and talking through SAPI instead of failing to load.
using PrismConfigInitFn = PrismConfig(PRISM_CALL *)(void);
using PrismInitFn = PrismContext *(PRISM_CALL *)(PrismConfig *);
using PrismShutdownFn = void(PRISM_CALL *)(PrismContext *);
using PrismCreateBestFn = PrismBackend *(PRISM_CALL *)(PrismContext *);
using PrismBackendFreeFn = void(PRISM_CALL *)(PrismBackend *);
using PrismBackendNameFn = const char *(PRISM_CALL *)(PrismBackend *);
using PrismBackendOutputFn = PrismError(PRISM_CALL *)(PrismBackend *, const char *, bool);
using PrismBackendSpeakFn = PrismError(PRISM_CALL *)(PrismBackend *, const char *, bool);
using PrismErrorMessageFn = const char *(PRISM_CALL *)(PrismError);

struct PrismApi {
    HMODULE module = nullptr;
    PrismConfigInitFn config_init = nullptr;
    PrismInitFn init = nullptr;
    PrismShutdownFn shutdown = nullptr;
    PrismCreateBestFn create_best = nullptr;
    PrismBackendFreeFn backend_free = nullptr;
    PrismBackendNameFn backend_name = nullptr;
    PrismBackendOutputFn backend_output = nullptr;
    PrismBackendSpeakFn backend_speak = nullptr;
    PrismErrorMessageFn error_message = nullptr;
};

struct SpeechRequest {
    std::wstring text;
    bool interrupt = true;
};

std::mutex g_mutex;
std::condition_variable g_cv;
std::queue<SpeechRequest> g_queue;
std::thread g_worker;
std::atomic<bool> g_running = false;

PrismApi g_prism;
PrismContext *g_prismContext = nullptr;
PrismBackend *g_prismBackend = nullptr;
bool g_prismOutputBroken = false;
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

std::string ToUtf8(std::wstring_view text)
{
    if (text.empty())
        return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};
    std::string utf8(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size,
                        nullptr, nullptr);
    return utf8;
}

std::wstring PrismErrorText(PrismError error);

std::wstring ToWide(const char *text)
{
    if (text == nullptr || *text == '\0')
        return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (size <= 1)
        return {};
    std::wstring wide(static_cast<size_t>(size - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), size);
    return wide;
}

std::wstring PrismErrorText(PrismError error)
{
    if (g_prism.error_message != nullptr)
        return ToWide(g_prism.error_message(error));
    return std::to_wstring(static_cast<int>(error));
}

bool LoadPrismFrom(const std::wstring &candidate)
{
    if (candidate.empty() || !std::filesystem::exists(candidate))
        return false;

    HMODULE module = LoadLibraryW(candidate.c_str());
    if (module == nullptr)
        return false;

    PrismApi api;
    api.module = module;
    api.config_init = reinterpret_cast<PrismConfigInitFn>(GetProcAddress(module, "prism_config_init"));
    api.init = reinterpret_cast<PrismInitFn>(GetProcAddress(module, "prism_init"));
    api.shutdown = reinterpret_cast<PrismShutdownFn>(GetProcAddress(module, "prism_shutdown"));
    api.create_best =
        reinterpret_cast<PrismCreateBestFn>(GetProcAddress(module, "prism_registry_create_best"));
    api.backend_free =
        reinterpret_cast<PrismBackendFreeFn>(GetProcAddress(module, "prism_backend_free"));
    api.backend_name =
        reinterpret_cast<PrismBackendNameFn>(GetProcAddress(module, "prism_backend_name"));
    api.backend_output =
        reinterpret_cast<PrismBackendOutputFn>(GetProcAddress(module, "prism_backend_output"));
    api.backend_speak =
        reinterpret_cast<PrismBackendSpeakFn>(GetProcAddress(module, "prism_backend_speak"));
    api.error_message =
        reinterpret_cast<PrismErrorMessageFn>(GetProcAddress(module, "prism_error_string"));

    if (api.config_init == nullptr || api.init == nullptr || api.shutdown == nullptr ||
        api.create_best == nullptr || api.backend_free == nullptr ||
        (api.backend_output == nullptr && api.backend_speak == nullptr))
    {
        FreeLibrary(module);
        return false;
    }

    g_prism = api;
    LogLine(L"Prism loaded: " + candidate);
    return true;
}

bool LoadPrism()
{
    const auto moduleDir = GetModuleDirectory();
    const auto buildDir = std::filesystem::path(moduleDir).parent_path();
    const auto repoDir = buildDir.parent_path();
    const auto sourceRootDir = repoDir.parent_path();
    const auto userProfile = GetEnvironmentString(L"USERPROFILE");

    const std::vector<std::wstring> searchDirs = {
        moduleDir,
        buildDir.wstring(),
        (repoDir / L"third_party" / L"prism").wstring(),
        (sourceRootDir / L"third_party" / L"prism").wstring(),
        userProfile.empty() ? std::wstring()
                            : (std::filesystem::path(userProfile) / L"Downloads").wstring(),
        // D2AccessSetup.exe extracts its embedded copy here.
        (std::filesystem::temp_directory_path() / L"D2AccessSetup").wstring(),
    };

    for (const auto &dir : searchDirs)
    {
        if (dir.empty())
            continue;
        if (LoadPrismFrom(dir + L"\\prism32.dll") || LoadPrismFrom(dir + L"\\prism.dll"))
            return true;
    }

    return false;
}

// The chosen backend is the highest priority one that initialized, so a running
// screen reader wins over plain text to speech.
bool CreatePrismBackend()
{
    if (g_prismContext == nullptr)
        return false;
    if (g_prismBackend != nullptr)
        return true;

    g_prismBackend = g_prism.create_best(g_prismContext);
    if (g_prismBackend == nullptr)
        return false;

    std::wstring name = L"unknown";
    if (g_prism.backend_name != nullptr)
        name = ToWide(g_prism.backend_name(g_prismBackend));
    LogLine(L"Screen reader backend initialized: Prism (" + name + L")");
    return true;
}

void DropPrismBackend()
{
    if (g_prismBackend != nullptr)
    {
        g_prism.backend_free(g_prismBackend);
        g_prismBackend = nullptr;
    }
}

bool InitializeSapi()
{
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)))
    {
        if (SUCCEEDED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_ISpVoice,
                                       reinterpret_cast<void **>(&g_voice))))
        {
            LogLine(L"Screen reader backend initialized: SAPI");
            return true;
        }
        CoUninitialize();
    }
    return false;
}

void InitializeBackend()
{
    if (LoadPrism())
    {
        PrismConfig config = g_prism.config_init();
        g_prismContext = g_prism.init(&config);
        if (g_prismContext != nullptr && CreatePrismBackend())
            return;
        LogLine(L"Prism has no usable backend, falling back to SAPI.");
        if (g_prismContext != nullptr)
        {
            g_prism.shutdown(g_prismContext);
            g_prismContext = nullptr;
        }
    }
    else
    {
        LogLine(L"Prism not found, falling back to SAPI.");
    }

    if (!InitializeSapi())
        LogLine(L"Screen reader backend not available.");
}

void ShutdownBackend()
{
    DropPrismBackend();

    if (g_prismContext != nullptr)
    {
        g_prism.shutdown(g_prismContext);
        g_prismContext = nullptr;
    }

    if (g_prism.module != nullptr)
    {
        FreeLibrary(g_prism.module);
        g_prism = PrismApi{};
    }

    if (g_voice != nullptr)
    {
        g_voice->Release();
        g_voice = nullptr;
        CoUninitialize();
    }
}

void SpeakInternal(const SpeechRequest &request)
{
    if (request.text.empty())
        return;

    if (g_prismContext != nullptr)
    {
        // A screen reader that started or stopped after the game changes which
        // backend is the right one, so a failed line drops the backend and the
        // next one picks again.
        if (!CreatePrismBackend())
            return;

        const std::string utf8 = ToUtf8(request.text);
        // Output is speech plus braille, which not every backend can do even when
        // it says it can: ZDSR answers with an internal error while still speaking.
        // Plain speech is what the mod really needs, so it is the fallback.
        PrismError error = PRISM_ERROR_NOT_IMPLEMENTED;
        if (g_prism.backend_output != nullptr && !g_prismOutputBroken)
        {
            error = g_prism.backend_output(g_prismBackend, utf8.c_str(), request.interrupt);
            if (error == PRISM_OK)
                return;
            g_prismOutputBroken = true;
            LogLine(L"Prism output failed (" + PrismErrorText(error) + L"), speaking only.");
        }
        if (g_prism.backend_speak != nullptr)
        {
            error = g_prism.backend_speak(g_prismBackend, utf8.c_str(), request.interrupt);
            if (error == PRISM_OK)
                return;
        }

        LogLine(L"Prism speech failed: " + PrismErrorText(error));
        DropPrismBackend();
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
