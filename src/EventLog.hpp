#pragma once

#include <Windows.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace d2access {

// In-memory event history for the MCP server: speech, log lines and gameplay
// events. Thread safe.
struct GameEvent {
    std::uint64_t id = 0;
    std::uint64_t tickMs = 0;
    std::string type;
    std::string text;
};

std::string Utf8FromWide(std::wstring_view text);
std::wstring WideFromUtf8(std::string_view text);

void PushEvent(std::string_view type, std::wstring_view text);
std::vector<GameEvent> GetEvents(std::uint64_t afterId, size_t limit, std::string_view type);
std::uint64_t LastEventId();

// Waits for the first event newer than afterId whose type matches (empty = any)
// and whose text contains `contains` (case-insensitive, empty = any).
bool WaitForEvent(std::uint64_t afterId, std::string_view type, std::string_view contains, DWORD timeoutMs,
                  GameEvent &found);

} // namespace d2access
