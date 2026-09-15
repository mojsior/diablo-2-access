#include "EventLog.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cwctype>
#include <deque>
#include <mutex>

namespace d2access {

namespace {

constexpr size_t MaxEvents = 5000;

std::mutex g_eventMutex;
std::condition_variable g_eventCv;
std::deque<GameEvent> g_events;
std::uint64_t g_nextEventId = 1;

std::wstring LowerWide(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return text;
}

bool Matches(const GameEvent &event, std::string_view type, const std::wstring &containsLower)
{
    if (!type.empty() && event.type != type)
        return false;
    if (containsLower.empty())
        return true;
    return LowerWide(WideFromUtf8(event.text)).find(containsLower) != std::wstring::npos;
}

} // namespace

std::string Utf8FromWide(std::wstring_view text)
{
    if (text.empty())
        return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0,
                                           nullptr, nullptr);
    if (length <= 0)
        return {};
    std::string out(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length, nullptr,
                        nullptr);
    return out;
}

std::wstring WideFromUtf8(std::string_view text)
{
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring out(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), length);
    return out;
}

void PushEvent(std::string_view type, std::wstring_view text)
{
    {
        std::scoped_lock lock(g_eventMutex);
        GameEvent event;
        event.id = g_nextEventId++;
        event.tickMs = GetTickCount64();
        event.type = std::string(type);
        event.text = Utf8FromWide(text);
        g_events.push_back(std::move(event));
        if (g_events.size() > MaxEvents)
            g_events.pop_front();
    }
    g_eventCv.notify_all();
}

std::vector<GameEvent> GetEvents(std::uint64_t afterId, size_t limit, std::string_view type)
{
    std::vector<GameEvent> result;
    std::scoped_lock lock(g_eventMutex);
    for (const GameEvent &event : g_events)
    {
        if (event.id <= afterId || (!type.empty() && event.type != type))
            continue;
        result.push_back(event);
        if (result.size() >= limit)
            break;
    }
    return result;
}

std::uint64_t LastEventId()
{
    std::scoped_lock lock(g_eventMutex);
    return g_nextEventId - 1;
}

bool WaitForEvent(std::uint64_t afterId, std::string_view type, std::string_view contains, DWORD timeoutMs,
                  GameEvent &found)
{
    const std::wstring containsLower = LowerWide(WideFromUtf8(contains));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);

    std::unique_lock lock(g_eventMutex);
    for (;;)
    {
        for (const GameEvent &event : g_events)
        {
            if (event.id > afterId && Matches(event, type, containsLower))
            {
                found = event;
                return true;
            }
        }
        if (!g_events.empty())
            afterId = std::max(afterId, g_events.back().id);
        if (g_eventCv.wait_until(lock, deadline) == std::cv_status::timeout)
            return false;
    }
}

} // namespace d2access
