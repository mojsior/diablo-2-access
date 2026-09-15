#include "Logging.hpp"

#include "EventLog.hpp"

#include <Shlwapi.h>

#include <mutex>
#include <sstream>
#include <string>

#pragma comment(lib, "Shlwapi.lib")

namespace d2access {
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

std::mutex g_logMutex;
HANDLE g_logFile = INVALID_HANDLE_VALUE;

std::wstring GetModuleDirectory()
{
    wchar_t modulePath[MAX_PATH] = {};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
    PathRemoveFileSpecW(modulePath);
    return modulePath;
}

std::wstring GetLogPath(const wchar_t *logFileName)
{
    std::wstring path = GetModuleDirectory();
    path += L"\\";
    path += logFileName;
    return path;
}

std::string WideToUtf8(const std::wstring &text)
{
    if (text.empty())
        return {};

    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(),
                                           static_cast<int>(text.size()), nullptr,
                                           0, nullptr, nullptr);
    if (length <= 0)
        return {};

    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        out.data(), length, nullptr, nullptr);
    return out;
}

} // namespace

void InitializeLogging(const wchar_t *logFileName)
{
    std::scoped_lock lock(g_logMutex);
    if (g_logFile != INVALID_HANDLE_VALUE)
        return;

    const std::wstring path = GetLogPath(logFileName);
    g_logFile = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
}

void ShutdownLogging()
{
    std::scoped_lock lock(g_logMutex);
    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
}

void LogLine(const std::wstring &message)
{
    PushEvent("log", message);
    std::scoped_lock lock(g_logMutex);

    OutputDebugStringW((message + L"\n").c_str());
    if (g_logFile == INVALID_HANDLE_VALUE)
        return;

    const std::string line = WideToUtf8(message + L"\r\n");
    if (line.empty())
        return;

    DWORD written = 0;
    WriteFile(g_logFile, line.data(), static_cast<DWORD>(line.size()), &written,
              nullptr);
    FlushFileBuffers(g_logFile);
}

void LogLineUtf8(const std::string &message)
{
    int length = MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, nullptr, 0);
    if (length <= 0)
        return;

    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, message.c_str(), -1, wide.data(), length);
    if (!wide.empty() && wide.back() == L'\0')
        wide.pop_back();
    LogLine(wide);
}

} // namespace d2access
