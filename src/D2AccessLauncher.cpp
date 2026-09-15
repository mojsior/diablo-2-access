#include "Logging.hpp"

#include <Windows.h>

#include <Shlwapi.h>

#include <filesystem>
#include <string>

#pragma comment(lib, "Shlwapi.lib")

namespace {

std::wstring GetModuleDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);
    return modulePath;
}

std::wstring GetDefaultGamePath()
{
    return L"C:\\Program Files (x86)\\Diablo II\\Game.exe";
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
