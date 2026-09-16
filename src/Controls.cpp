#include "Controls.hpp"

#include "Localization.hpp"
#include "Logging.hpp"
#include "Panels.hpp"
#include "ScreenReader.hpp"

#include <atomic>
#include <cstdint>
#include <string>

namespace d2access::controls {

namespace {

// Configure controls screen (drawn by 0x493600, UI var 11). 0x71F78C points at
// the list of bindable actions, 51 rows without the expansion and 62 with it
// (0x7B68B4): +0 the action id, +4 the string id of its name. Rows whose action
// is 57 are headings. 0x7B68F0 is the highlighted row, 0x7B68EC the first drawn
// row, 0x71F7E0 the chosen column (1 primary key, 0 secondary) and 0x7B68F8 is
// set while the game waits for the new key. 0x457340 gives the name of the key
// bound to an action, 0x494090 starts an assignment. The three buttons at the
// bottom live at 0x71F790 (stride 26: +0 name string id, +2 the function), with
// 0x7B68F4 as the chosen one.
constexpr int UiControls = 11;
constexpr uintptr_t VaRowTable = 0x71F78C;
constexpr uintptr_t VaRowCount = 0x7B68B4;
constexpr uintptr_t VaFirstDrawnRow = 0x7B68EC;
constexpr uintptr_t VaSelectedRow = 0x7B68F0;
constexpr uintptr_t VaSelectedColumn = 0x71F7E0;
constexpr uintptr_t VaWaitingForKey = 0x7B68F8;
constexpr uintptr_t VaSelectedButton = 0x7B68F4;
constexpr uintptr_t VaButtons = 0x71F790;
constexpr uintptr_t Game_BeginKeyAssignment = 0x494090;
constexpr uintptr_t Game_KeyNameForAction = 0x457340;

constexpr int RowStride = 10;
constexpr int RowOffset_NameString = 4;
constexpr int HeadingAction = 57;
constexpr int VisibleRows = 15;
constexpr int ButtonStride = 26;
constexpr int ButtonOffset_Action = 2;
constexpr int ButtonCount = 3;
constexpr int ColumnPrimary = 1;
constexpr int ColumnSecondary = 0;
constexpr size_t MaxKeyNameChars = 64;

struct Row {
    int action = 0;
    int nameString = 0;
};

struct State {
    bool wasOpen = false;
    bool onButtons = false;
    bool wasWaitingForKey = false;
};

State g_state;
std::atomic<bool> g_openForKeys = false;
std::atomic<bool> g_waitingForKey = false;

void Say(const std::wstring &text)
{
    LogLine(L"Controls: " + text);
    Speak(text, true);
}

int ReadInt(uintptr_t va)
{
    int value = 0;
    game::Read(game::Absolute(va), value);
    return value;
}

int RowCount()
{
    const int count = ReadInt(VaRowCount);
    return count > 0 && count < 256 ? count : 0;
}

bool ReadRow(int index, Row &row)
{
    const uintptr_t table = game::ReadPtr(game::Absolute(VaRowTable));
    if (table == 0 || index < 0 || index >= RowCount())
        return false;

    const uintptr_t record = table + static_cast<uintptr_t>(index * RowStride);
    std::uint16_t nameString = 0;
    if (!game::Read(record, row.action) || !game::Read(record + RowOffset_NameString, nameString))
        return false;
    row.nameString = nameString;
    return true;
}

using KeyNameForAction_t = const wchar_t *(__fastcall *)(int, int);
using ButtonAction_t = void(__cdecl *)();

const wchar_t *CallKeyNameForAction(int action, int column)
{
    __try
    {
        return reinterpret_cast<KeyNameForAction_t>(game::Absolute(Game_KeyNameForAction))(action, column);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

void CallBeginKeyAssignment()
{
    __try
    {
        reinterpret_cast<void(__cdecl *)()>(game::Absolute(Game_BeginKeyAssignment))();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

void CallButtonAction(uintptr_t action)
{
    __try
    {
        reinterpret_cast<ButtonAction_t>(action)();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

std::wstring KeyName(int action, int column)
{
    const wchar_t *text = CallKeyNameForAction(action, column);
    if (text == nullptr)
        return {};
    return panels::CleanText(game::ReadWideString(reinterpret_cast<uintptr_t>(text), MaxKeyNameChars));
}

std::wstring ColumnName(int column)
{
    return column == ColumnPrimary ? TrS(L"klawisz główny", L"primary key")
                                   : TrS(L"klawisz zapasowy", L"secondary key");
}

// Keeps the highlighted row inside the drawn part of the list.
void ScrollToRow(int index)
{
    int first = ReadInt(VaFirstDrawnRow);
    if (index < first)
        first = index;
    else if (index >= first + VisibleRows)
        first = index - VisibleRows + 1;
    if (first < 0)
        first = 0;
    game::Write(game::Absolute(VaFirstDrawnRow), first);
}

std::wstring RowText(int index)
{
    Row row;
    if (!ReadRow(index, row))
        return {};

    const std::wstring name = panels::CleanText(game::StringById(row.nameString));
    if (row.action == HeadingAction)
        return name + Tr(L", nagłówek.", L", heading.");

    const int column = ReadInt(VaSelectedColumn);
    std::wstring text = name + L": " + ColumnName(column) + L" " + KeyName(row.action, column);
    const int other = column == ColumnPrimary ? ColumnSecondary : ColumnPrimary;
    text += L", " + ColumnName(other) + L" " + KeyName(row.action, other);
    text += L". " + std::to_wstring(index + 1) + TrS(L" z ", L" of ") + std::to_wstring(RowCount()) + L".";
    return text;
}

bool ReadButton(int index, int &nameString, uintptr_t &action)
{
    if (index < 0 || index >= ButtonCount)
        return false;

    const uintptr_t record = game::Absolute(VaButtons) + static_cast<uintptr_t>(index * ButtonStride);
    std::uint16_t name = 0;
    if (!game::Read(record, name))
        return false;
    nameString = name;
    action = game::ReadPtr(record + ButtonOffset_Action);
    return true;
}

std::wstring ButtonText(int index)
{
    int nameString = 0;
    uintptr_t action = 0;
    if (!ReadButton(index, nameString, action))
        return {};
    return panels::CleanText(game::StringById(nameString)) + Tr(L", przycisk. ", L", button. ") +
           std::to_wstring(index + 1) + TrS(L" z ", L" of ") + std::to_wstring(ButtonCount) + L".";
}

void SpeakSelection(const std::wstring &prefix)
{
    if (g_state.onButtons)
        Say(prefix + ButtonText(ReadInt(VaSelectedButton)));
    else
        Say(prefix + RowText(ReadInt(VaSelectedRow)));
}

bool MoveRow(int delta)
{
    const int count = RowCount();
    if (count <= 0)
        return false;

    int index = ReadInt(VaSelectedRow);
    for (int step = 0; step < count; ++step)
    {
        index += delta;
        if (index < 0)
            index = count - 1;
        else if (index >= count)
            index = 0;

        Row row;
        if (!ReadRow(index, row) || row.action == HeadingAction)
            continue;
        if (!game::Write(game::Absolute(VaSelectedRow), index))
            return false;
        ScrollToRow(index);
        Say(RowText(index));
        return true;
    }
    return false;
}

bool MoveButton(int delta)
{
    int index = ReadInt(VaSelectedButton) + delta;
    if (index < 0)
        index = ButtonCount - 1;
    else if (index >= ButtonCount)
        index = 0;
    if (!game::Write(game::Absolute(VaSelectedButton), index))
        return false;
    Say(ButtonText(index));
    return true;
}

bool SwitchColumn(int column)
{
    if (!game::Write(game::Absolute(VaSelectedColumn), column))
        return false;
    Say(RowText(ReadInt(VaSelectedRow)));
    return true;
}

bool Activate()
{
    if (g_state.onButtons)
    {
        int nameString = 0;
        uintptr_t action = 0;
        if (!ReadButton(ReadInt(VaSelectedButton), nameString, action) || action == 0)
            return false;
        CallButtonAction(action);
        return true;
    }

    Row row;
    if (!ReadRow(ReadInt(VaSelectedRow), row) || row.action == HeadingAction)
        return false;

    Say(Tr(L"Naciśnij nowy klawisz. Escape anuluje.", L"Press the new key. Escape cancels."));
    CallBeginKeyAssignment();
    return true;
}

bool SwitchArea()
{
    g_state.onButtons = !g_state.onButtons;
    // The game starts with no button chosen (-1), which would read as nothing.
    if (g_state.onButtons)
    {
        const int button = ReadInt(VaSelectedButton);
        if (button < 0 || button >= ButtonCount)
            game::Write(game::Absolute(VaSelectedButton), 0);
    }
    SpeakSelection(g_state.onButtons ? Tr(L"Przyciski. ", L"Buttons. ") : Tr(L"Lista akcji. ", L"Action list. "));
    return true;
}

} // namespace

void ResetControls()
{
    g_state = State{};
    g_openForKeys = false;
    g_waitingForKey = false;
}

bool IsControlsScreenOpen()
{
    return game::IsUiPanelOpen(UiControls);
}

bool IsControlsScreenOpenForKeys()
{
    return g_openForKeys.load();
}

bool IsWaitingForKeyBinding()
{
    return g_waitingForKey.load();
}

void UpdateControls()
{
    const bool open = IsControlsScreenOpen();
    g_openForKeys = open;
    if (!open)
    {
        if (g_state.wasOpen)
            g_state = State{};
        g_waitingForKey = false;
        g_state.wasOpen = false;
        return;
    }

    const bool waiting = ReadInt(VaWaitingForKey) != 0;
    g_waitingForKey = waiting;

    if (!g_state.wasOpen)
    {
        g_state.onButtons = false;
        SpeakSelection(Tr(L"Konfiguracja sterowania. Strzałki w górę i w dół wybierają akcję, w lewo i w prawo "
                          L"klawisz główny albo zapasowy, Enter przypisuje nowy klawisz, Tab przechodzi do "
                          L"przycisków. ",
                          L"Configure controls. Up and down arrows choose an action, left and right the primary or "
                          L"secondary key, Enter assigns a new key, Tab moves to the buttons. "));
    }
    else if (g_state.wasWaitingForKey && !waiting)
    {
        SpeakSelection(Tr(L"Zapisano. ", L"Saved. "));
    }

    g_state.wasWaitingForKey = waiting;
    g_state.wasOpen = true;
}

bool HandleControlsKey(DWORD virtualKey, bool ctrl, bool shift)
{
    (void)ctrl;
    (void)shift;
    // While the game waits for the new key every key belongs to it.
    if (!IsControlsScreenOpen() || IsWaitingForKeyBinding())
        return false;

    switch (virtualKey)
    {
    case VK_UP:
    case VK_NUMPAD8:
        return g_state.onButtons ? MoveButton(-1) : MoveRow(-1);
    case VK_DOWN:
    case VK_NUMPAD2:
        return g_state.onButtons ? MoveButton(1) : MoveRow(1);
    case VK_LEFT:
    case VK_NUMPAD4:
        return g_state.onButtons ? MoveButton(-1) : SwitchColumn(ColumnPrimary);
    case VK_RIGHT:
    case VK_NUMPAD6:
        return g_state.onButtons ? MoveButton(1) : SwitchColumn(ColumnSecondary);
    case VK_TAB:
        return SwitchArea();
    case VK_RETURN:
        return Activate();
    case VK_SPACE:
        SpeakSelection({});
        return true;
    default:
        return false;
    }
}

} // namespace d2access::controls
