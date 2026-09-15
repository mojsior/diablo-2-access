#include "AudioCue.hpp"
#include "FrontendHooks.hpp"
#include "GameplayHooks.hpp"
#include "Localization.hpp"
#include "Logging.hpp"
#include "McpServer.hpp"
#include "ScreenReader.hpp"

#include <Windows.h>

namespace d2access {
namespace {

DWORD WINAPI InitializeRuntime(LPVOID)
{
    InitializeLogging(L"D2AccessHook.log");
    LogLine(L"D2AccessHook initializing.");
    InitializeLocalization();

    InitializeAudioCue();
    InitializeScreenReader();
    InitializeGameplayHooks();

    if (!InitializeFrontendHooks())
        LogLine(L"Failed to install frontend hooks.");

    StartMcpServer();
    return 0;
}

} // namespace
} // namespace d2access

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    switch (reason)
    {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(module);
        CreateThread(nullptr, 0, d2access::InitializeRuntime, nullptr, 0, nullptr);
        break;
    case DLL_PROCESS_DETACH:
        d2access::StopMcpServer();
        d2access::ShutdownGameplayHooks();
        d2access::ShutdownFrontendHooks();
        d2access::ShutdownScreenReader();
        d2access::ShutdownAudioCue();
        d2access::ShutdownLogging();
        break;
    default:
        break;
    }

    return TRUE;
}
