#include "QuestLog.hpp"

#include "Localization.hpp"
#include "Logging.hpp"
#include "Panels.hpp"
#include "ScreenReader.hpp"

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace d2access::questlog {

namespace {

// Quest log (.\UI\QuestLog.cpp). 0x492360 opens and closes it exactly like the
// game's own key does, 0x7B58D8 holds its state (2 = open). While it is open the
// game rebuilds up to six entries of 618 bytes at 0x7B59E9 for the act tab in
// 0x7B6895: +0 shown flag, +1 quest id, +5 title string id, +7 the status text
// the panel prints and +613 the quest state. The entries of a new tab only
// appear after the game has drawn the panel once, so announcements are delayed
// by a few ticks.
constexpr uintptr_t VaToggleQuestLog = 0x492360;
constexpr uintptr_t VaLoadQuestTab = 0x4914F0; // __stdcall(tab, resetCache)
constexpr uintptr_t VaQuestLogState = 0x7B58D8;
constexpr uintptr_t VaQuestEntries = 0x7B59E9;
constexpr uintptr_t VaQuestActTab = 0x7B6895;

constexpr int QuestEntryStride = 618;
constexpr int MaxQuestEntries = 6;
constexpr int EntryOffset_QuestId = 1;
constexpr int EntryOffset_TitleString = 5;
constexpr int EntryOffset_Text = 7;
constexpr int EntryOffset_State = 613;
constexpr int MaxQuestTextChars = 299;
constexpr int ActCount = 5;
constexpr int AnnounceDelayTicks = 3;
constexpr int AnnounceRetries = 12;
// Entries that hold no quest keep this string id.
constexpr int EmptyTitleString = 3724;
// The player's quest record (0x4A16D0 returns it) and the flag reader 0x65ADD0.
constexpr uintptr_t VaQuestRecord = 0x7B7383;
constexpr uintptr_t Game_QuestFlag = 0x65ADD0;

struct QuestEntry {
    int questId = 0;
    int state = 0;
    std::wstring title;
    std::wstring text;
};

struct State {
    bool wasOpen = false;
    int selected = 0;
    int pendingTicks = 0;
    int pendingRetries = 0;
    std::wstring pendingPrefix;
};

State g_state;
std::atomic<bool> g_openForKeys = false;

void Say(const std::wstring &text)
{
    LogLine(L"QuestLog: " + text);
    Speak(text, true);
}

using ToggleQuestLog_t = int(__fastcall *)(void *, void *);
using LoadQuestTab_t = int(__stdcall *)(int, int);

void CallToggle()
{
    __try
    {
        reinterpret_cast<ToggleQuestLog_t>(game::Absolute(VaToggleQuestLog))(nullptr, nullptr);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void CallLoadTab(int tab)
{
    __try
    {
        reinterpret_cast<LoadQuestTab_t>(game::Absolute(VaLoadQuestTab))(tab, 0);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

int ActTab()
{
    int tab = 0;
    game::Read(game::Absolute(VaQuestActTab), tab);
    return tab;
}

using QuestFlag_t = int(__stdcall *)(void *, int, int);

int CallQuestFlag(uintptr_t record, int questId, int flag)
{
    __try
    {
        return reinterpret_cast<QuestFlag_t>(game::Absolute(Game_QuestFlag))(
            reinterpret_cast<void *>(record), questId, flag);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

// Act one is always there; every other tab opens with the quest that ends the
// act before it, and act five needs the expansion.
bool ActUnlocked(int tab)
{
    static const int TabQuest[ActCount] = {0, 7, 15, 23, 26};
    if (tab <= 0)
        return true;
    if (tab >= ActCount || (tab == ActCount - 1 && !game::IsExpansion()))
        return false;

    const uintptr_t record = game::ReadPtr(game::Absolute(VaQuestRecord));
    return record != 0 && CallQuestFlag(record, TabQuest[tab], 0) != 0;
}

std::vector<QuestEntry> ReadEntries()
{
    std::vector<QuestEntry> entries;
    for (int index = 0; index < MaxQuestEntries; ++index)
    {
        const uintptr_t record =
            game::Absolute(VaQuestEntries) + static_cast<uintptr_t>(index * QuestEntryStride);
        std::uint8_t shown = 0;
        if (!game::Read(record, shown) || shown == 0)
            continue;

        QuestEntry entry;
        std::uint16_t titleString = 0;
        std::uint8_t state = 0;
        game::Read(record + EntryOffset_QuestId, entry.questId);
        game::Read(record + EntryOffset_TitleString, titleString);
        game::Read(record + EntryOffset_State, state);
        if (titleString == EmptyTitleString)
            continue;
        entry.state = state;
        entry.title = panels::CleanText(game::StringById(titleString));
        entry.text = panels::CleanText(game::ReadWideString(record + EntryOffset_Text, MaxQuestTextChars));
        if (entry.title.empty() && entry.text.empty())
            continue;
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::wstring ActName(int tab)
{
    return TrS(L"Akt ", L"Act ") + std::to_wstring(tab + 1);
}

std::wstring EntryText(const std::vector<QuestEntry> &entries, int index)
{
    if (entries.empty())
        return TrS(L"Brak zadań w tym akcie.", L"No quests in this act.");
    if (index < 0 || index >= static_cast<int>(entries.size()))
        return {};

    const QuestEntry &entry = entries[static_cast<size_t>(index)];
    std::wstring text = entry.title;
    if (!entry.text.empty())
    {
        if (!text.empty())
            text += L". ";
        text += entry.text;
    }
    text += L". " + std::to_wstring(index + 1) + TrS(L" z ", L" of ") + std::to_wstring(entries.size()) + L".";
    return text;
}

void ClampSelection(const std::vector<QuestEntry> &entries)
{
    if (g_state.selected >= static_cast<int>(entries.size()))
        g_state.selected = entries.empty() ? 0 : static_cast<int>(entries.size()) - 1;
    if (g_state.selected < 0)
        g_state.selected = 0;
}

void SpeakSelected(const std::wstring &prefix)
{
    const std::vector<QuestEntry> entries = ReadEntries();
    ClampSelection(entries);
    Say(prefix + EntryText(entries, g_state.selected));
}

// The entries of a tab that was just opened or switched are only filled in when
// the game draws the panel again.
void ScheduleAnnouncement(const std::wstring &prefix)
{
    g_state.pendingPrefix = prefix;
    g_state.pendingTicks = AnnounceDelayTicks;
    g_state.pendingRetries = AnnounceRetries;
}

bool MoveSelection(int delta)
{
    const std::vector<QuestEntry> entries = ReadEntries();
    if (entries.empty())
    {
        Say(EntryText(entries, 0));
        return true;
    }

    const int count = static_cast<int>(entries.size());
    int index = g_state.selected + delta;
    if (index < 0)
        index = count - 1;
    else if (index >= count)
        index = 0;
    g_state.selected = index;
    Say(EntryText(entries, index));
    return true;
}

bool MoveAct(int delta)
{
    const int lastAct = game::IsExpansion() ? ActCount - 1 : ActCount - 2;
    const int current = ActTab();
    int tab = current;
    for (int step = 0; step < ActCount; ++step)
    {
        tab += delta;
        if (tab < 0)
            tab = lastAct;
        else if (tab > lastAct)
            tab = 0;
        if (ActUnlocked(tab))
            break;
    }
    if (!ActUnlocked(tab) || tab == current)
    {
        Say(Tr(L"To jedyny dostępny akt.", L"This is the only act you can open."));
        return true;
    }

    if (!game::Write(game::Absolute(VaQuestActTab), tab))
        return false;
    CallLoadTab(tab);
    g_state.selected = 0;
    ScheduleAnnouncement(ActName(tab) + L". ");
    return true;
}

} // namespace

void ResetQuestLog()
{
    g_state = State{};
    g_openForKeys = false;
}

bool IsQuestLogOpen()
{
    // Only 2 means the panel is up; 1 is a state the game passes through, and
    // treating it as open stole the arrows from the skill tree.
    constexpr int QuestLogOpen = 2;
    int state = 0;
    return game::Read(game::Absolute(VaQuestLogState), state) && state == QuestLogOpen;
}

bool IsQuestLogOpenForKeys()
{
    return g_openForKeys.load();
}

void ToggleQuestLog()
{
    const bool wasOpen = IsQuestLogOpen();
    CallToggle();
    if (wasOpen && !IsQuestLogOpen())
        Say(Tr(L"Dziennik zadań zamknięty.", L"Quest log closed."));
}

void UpdateQuestLog()
{
    const bool open = IsQuestLogOpen();
    g_openForKeys = open;

    if (open && !g_state.wasOpen)
    {
        g_state.selected = 0;
        ScheduleAnnouncement(TrS(L"Dziennik zadań. ", L"Quest log. ") + ActName(ActTab()) + L". " +
                             TrS(L"Strzałki w górę i w dół wybierają zadanie, w lewo i w prawo zmieniają akt. ",
                                 L"Up and down arrows choose a quest, left and right change the act. "));
    }

    if (!open)
    {
        g_state.pendingTicks = 0;
        g_state.pendingPrefix.clear();
    }
    else if (g_state.pendingTicks > 0 && --g_state.pendingTicks == 0)
    {
        // The entries of a freshly opened tab only appear once the game has
        // drawn the panel, so wait for them instead of reporting an empty act.
        if (ReadEntries().empty() && g_state.pendingRetries > 0)
        {
            --g_state.pendingRetries;
            g_state.pendingTicks = AnnounceDelayTicks;
        }
        else
        {
            SpeakSelected(g_state.pendingPrefix);
            g_state.pendingPrefix.clear();
        }
    }

    g_state.wasOpen = open;
}

bool HandleQuestLogKey(DWORD virtualKey, bool ctrl, bool shift)
{
    (void)ctrl;
    (void)shift;
    if (!IsQuestLogOpen())
        return false;

    switch (virtualKey)
    {
    case VK_UP:
    case VK_NUMPAD8:
        return MoveSelection(-1);
    case VK_DOWN:
    case VK_NUMPAD2:
        return MoveSelection(1);
    case VK_LEFT:
    case VK_NUMPAD4:
        return MoveAct(-1);
    case VK_RIGHT:
    case VK_NUMPAD6:
        return MoveAct(1);
    case VK_SPACE:
    case VK_RETURN:
        SpeakSelected({});
        return true;
    case 'Q':
        ToggleQuestLog();
        return true;
    default:
        return false;
    }
}

} // namespace d2access::questlog
