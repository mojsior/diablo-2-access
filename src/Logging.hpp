#pragma once

#include <Windows.h>

#include <string>

namespace d2access {

void InitializeLogging(const wchar_t *logFileName);
void ShutdownLogging();
void LogLine(const std::wstring &message);
void LogLineUtf8(const std::string &message);

} // namespace d2access
