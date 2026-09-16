#include "Logging.hpp"

#include <Windows.h>

#include <Shlwapi.h>

#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Version.lib")

namespace {

std::wstring GetModuleDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);
    return modulePath;
}

std::wstring InstallPathFromRegistry(HKEY root, DWORD flags)
{
    wchar_t buffer[MAX_PATH] = {};
    DWORD size = sizeof(buffer);
    if (RegGetValueW(root, L"SOFTWARE\\Blizzard Entertainment\\Diablo II", L"InstallPath", RRF_RT_REG_SZ | flags,
                     nullptr, buffer, &size) != ERROR_SUCCESS)
        return {};
    return buffer;
}

// D2AccessSetup installs the mod into the game folder, so the Game.exe next to
// the launcher comes first, then the path the Blizzard installer registered.
std::wstring GetDefaultGamePath()
{
    const std::filesystem::path local = std::filesystem::path(GetModuleDirectory()) / L"Game.exe";
    if (std::filesystem::exists(local))
        return local.wstring();

    for (const std::wstring &folder : {InstallPathFromRegistry(HKEY_LOCAL_MACHINE, RRF_SUBKEY_WOW6432KEY),
                                       InstallPathFromRegistry(HKEY_CURRENT_USER, 0)})
    {
        if (folder.empty())
            continue;
        const std::filesystem::path candidate = std::filesystem::path(folder) / L"Game.exe";
        if (std::filesystem::exists(candidate))
            return candidate.wstring();
    }
    return L"C:\\Program Files (x86)\\Diablo II\\Game.exe";
}

// Every address the mod hooks comes from Game.exe 1.14b (file version
// 1.14.1.68). Another build would be hooked in the wrong places and would crash
// the game, so the launcher says so instead of starting it. When the version
// cannot be read at all, the launcher lets the player try anyway.
bool IsSupportedGameVersion(const std::wstring &gamePath, std::wstring &versionText)
{
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(gamePath.c_str(), &ignored);
    if (size == 0)
        return true;

    std::vector<std::byte> buffer(size);
    VS_FIXEDFILEINFO *info = nullptr;
    UINT length = 0;
    if (!GetFileVersionInfoW(gamePath.c_str(), 0, size, buffer.data()) ||
        !VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void **>(&info), &length) || info == nullptr)
        return true;

    const WORD major = HIWORD(info->dwFileVersionMS);
    const WORD minor = LOWORD(info->dwFileVersionMS);
    const WORD build = HIWORD(info->dwFileVersionLS);
    const WORD revision = LOWORD(info->dwFileVersionLS);
    versionText = std::to_wstring(major) + L"." + std::to_wstring(minor) + L"." + std::to_wstring(build) + L"." +
                  std::to_wstring(revision);
    return major == 1 && minor == 14 && build == 1 && revision == 68;
}

bool InjectDll(HANDLE processHandle, const std::wstring &dllPath)
{
    const size_t bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    void *remoteBuffer =
        VirtualAllocEx(processHandle, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (remoteBuffer == nullptr)
        return false;

    bool success = false;
    if (WriteProcessMemory(processHandle, remoteBuffer, dllPath.c_str(), bytes, nullptr))
    {
        HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        auto loadLibraryW = reinterpret_cast<LPTHREAD_START_ROUTINE>(
            GetProcAddress(kernel32, "LoadLibraryW"));
        HANDLE remoteThread =
            CreateRemoteThread(processHandle, nullptr, 0, loadLibraryW, remoteBuffer, 0, nullptr);
        if (remoteThread != nullptr)
        {
            WaitForSingleObject(remoteThread, INFINITE);
            CloseHandle(remoteThread);
            success = true;
        }
    }

    VirtualFreeEx(processHandle, remoteBuffer, 0, MEM_RELEASE);
    return success;
}

} // namespace

int wmain(int argc, wchar_t **argv)
{
    d2access::InitializeLogging(L"D2AccessLauncher.log");

    std::wstring gamePath = argc > 1 ? argv[1] : GetDefaultGamePath();
    if (!std::filesystem::exists(gamePath))
    {
        MessageBoxW(nullptr, (L"Nie znaleziono Game.exe / Game.exe not found: " + gamePath).c_str(),
                    L"D2AccessLauncher", MB_ICONERROR);
        return 1;
    }

    std::wstring versionText;
    if (!IsSupportedGameVersion(gamePath, versionText))
    {
        d2access::LogLine(L"Unsupported game version: " + versionText);
        MessageBoxW(nullptr,
                    (L"Ta wersja gry nie jest obsługiwana: " + versionText +
                     L"\nMod działa z Diablo II 1.14b (1.14.1.68). Nie aktualizuj gry opcją BATTLE.NET "
                     L"w menu głównym.\n\nThis game version is not supported: " +
                     versionText +
                     L"\nThe mod works with Diablo II 1.14b (1.14.1.68). Do not update the game through "
                     L"BATTLE.NET in the main menu.")
                        .c_str(),
                    L"D2AccessLauncher", MB_ICONERROR);
        return 1;
    }

    std::wstring dllPath = GetModuleDirectory() + L"\\D2AccessHook.dll";
    if (!std::filesystem::exists(dllPath))
    {
        MessageBoxW(nullptr, (L"Nie znaleziono D2AccessHook.dll / D2AccessHook.dll not found: " + dllPath).c_str(),
                    L"D2AccessLauncher", MB_ICONERROR);
        return 1;
    }

    STARTUPINFOW startupInfo = {};
    PROCESS_INFORMATION processInfo = {};
    startupInfo.cb = sizeof(startupInfo);

    std::wstring commandLine = L"\"" + gamePath + L"\"";
    std::wstring currentDirectory = std::filesystem::path(gamePath).parent_path().wstring();

    if (!CreateProcessW(gamePath.c_str(), commandLine.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED, nullptr, currentDirectory.c_str(),
                        &startupInfo, &processInfo))
    {
        MessageBoxW(nullptr, L"Nie udało się uruchomić Game.exe. / Could not start Game.exe.",
                    L"D2AccessLauncher", MB_ICONERROR);
        return 1;
    }

    if (!InjectDll(processInfo.hProcess, dllPath))
    {
        TerminateProcess(processInfo.hProcess, 1);
        CloseHandle(processInfo.hThread);
        CloseHandle(processInfo.hProcess);
        MessageBoxW(nullptr, L"Nie udało się wstrzyknąć D2AccessHook.dll. / Could not inject D2AccessHook.dll.",
                    L"D2AccessLauncher", MB_ICONERROR);
        return 1;
    }

    ResumeThread(processInfo.hThread);

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return 0;
}
