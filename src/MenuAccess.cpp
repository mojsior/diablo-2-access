#include "MenuAccess.hpp"

#include "D2Game.hpp"
#include "Localization.hpp"
#include "Logging.hpp"
#include "ScreenReader.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cstring>
#include <string>
#include <vector>

// The option menu activation routine (0x46AF90) passes esi, the window message
// the click handler received, to the entry callbacks in edx. "Save and exit"
// (0x46CCC0) sends WM_CLOSE to the HWND in its first field.
static uintptr_t g_menuActivateFn = 0;

static __declspec(naked) void __stdcall CallMenuActivate(void *)
{
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov esi, [esp + 20]
        call dword ptr [g_menuActivateFn]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 4
    }
}

namespace d2access::gamemenu {

namespace {

// Game.exe 1.14b option menus (0x46B570 loads them, 0x46BDB0 draws the current
// one). Header: +0 entry count, +4 row height. Entries are 0x550 bytes.
constexpr int UiGameMenu = 9;
constexpr uintptr_t VaSelectedEntry = 0x7B2F78;
constexpr uintptr_t VaMenuHeader = 0x7B2F7C;
constexpr uintptr_t VaMenuEntries = 0x7B2F80;
constexpr uintptr_t VaActivateSelected = 0x46AF90;

constexpr size_t EntrySize = 0x550;
constexpr size_t EntryType = 0;           // -1 title, 0 button, 1 choice, 2 slider
constexpr size_t EntryExpansionOnly = 4;
constexpr size_t EntryName = 12;          // image name, e.g. "SoundOptions"
constexpr size_t EntryEnabledFn = 272;
constexpr size_t EntryChangedFn = 276;
constexpr size_t EntryChoiceCount = 288;
constexpr size_t EntryValue = 292;
constexpr size_t EntryChoiceNames = 300; // char[260] per choice
constexpr size_t ChoiceNameSize = 260;
constexpr int MaxEntries = 16;

constexpr int TypeTitle = -1;
constexpr int TypeButton = 0;
constexpr int TypeChoice = 1;
constexpr int TypeSlider = 2;

struct Entry {
    int index = 0;
    uintptr_t address = 0;
    int type = TypeButton;
    std::string name;
    int choiceCount = 0;
    int value = 0;
    bool selectable = false;
};

struct State {
    bool wasOpen = false;
    uintptr_t header = 0;
    int selected = INT_MIN;
    int value = INT_MIN;
};

State g_state;
std::atomic<bool> g_openForKeys = false;

void Say(const std::wstring &text)
{
    LogLine(L"GameMenu: " + text);
    Speak(text, true);
}

using EntryFn_t = int(__fastcall *)(void *, void *);

// Stand-in for the window message: +0 HWND, the rest zero.
std::array<std::uint32_t, 16> g_windowMessage{};

BOOL CALLBACK FindGameWindowProc(HWND window, LPARAM result)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr)
        return TRUE;
    *reinterpret_cast<HWND *>(result) = window;
    return FALSE;
}

void *WindowMessage()
{
    HWND window = nullptr;
    EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&window));
    g_windowMessage.fill(0);
    g_windowMessage[0] = static_cast<std::uint32_t>(reinterpret_cast<uintptr_t>(window));
    return g_windowMessage.data();
}

int SehEntryFn(uintptr_t fn, uintptr_t entry)
{
    __try
    {
        return reinterpret_cast<EntryFn_t>(fn)(reinterpret_cast<void *>(entry), WindowMessage());
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool SehActivate(uintptr_t fn)
{
    __try
    {
        g_menuActivateFn = fn;
        CallMenuActivate(WindowMessage());
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool SehWriteInt(uintptr_t address, int value)
{
    __try
    {
        std::memcpy(reinterpret_cast<void *>(address), &value, sizeof(value));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

std::vector<Entry> ReadEntries(uintptr_t &headerOut)
{
    std::vector<Entry> entries;
    headerOut = game::ReadPtr(game::Absolute(VaMenuHeader));
    const uintptr_t base = game::ReadPtr(game::Absolute(VaMenuEntries));
    std::int32_t count = 0;
    if (headerOut == 0 || base == 0 || !game::Read(headerOut, count) || count <= 0 || count > MaxEntries)
        return entries;

    const bool expansion = game::IsExpansion();
    for (int i = 0; i < count; ++i)
    {
        Entry entry;
        entry.index = i;
        entry.address = base + static_cast<uintptr_t>(i) * EntrySize;
        std::int32_t expansionOnly = 0;
        game::Read(entry.address + EntryType, entry.type);
        game::Read(entry.address + EntryExpansionOnly, expansionOnly);
        game::Read(entry.address + EntryChoiceCount, entry.choiceCount);
        game::Read(entry.address + EntryValue, entry.value);

        std::array<char, 64> name{};
        game::ReadBytes(entry.address + EntryName, name.data(), name.size() - 1);
        entry.name = name.data();

        const bool visible = expansionOnly == 0 || expansion;
        entry.selectable = visible && entry.type != TypeTitle;
        if (entry.selectable)
        {
            const uintptr_t enabledFn = game::ReadPtr(entry.address + EntryEnabledFn);
            if (enabledFn != 0 && SehEntryFn(enabledFn, entry.address) == 0)
                entry.selectable = false;
        }
        entries.push_back(std::move(entry));
    }
    return entries;
}

struct Label {
    const char *name;
    const wchar_t *polish;
    const wchar_t *english;
};

constexpr std::array<Label, 40> Labels = {{
    {"Options", L"Opcje", L"Options"},
    {"Exit", L"Zapisz i wyjdź z gry", L"Save and exit game"},
    {"ReturnToGame", L"Powrót do gry", L"Return to game"},
    {"SoundOptions", L"Opcje dźwięku", L"Sound options"},
    {"VideoOptions", L"Opcje obrazu", L"Video options"},
    {"AutoMapOptions", L"Opcje automapy", L"Automap options"},
    {"CfgOptions", L"Konfiguracja sterowania", L"Configure controls"},
    {"Previous", L"Poprzednie menu", L"Previous menu"},
    {"SPrevious", L"Poprzednie menu", L"Previous menu"},
    {"Sound", L"Głośność dźwięku", L"Sound volume"},
    {"Music", L"Głośność muzyki", L"Music volume"},
    {"3DSound", L"Dźwięk 3D", L"3D sound"},
    {"EAX", L"Efekty otoczenia EAX", L"EAX environmental effects"},
    {"3DBias", L"Balans dźwięku 3D", L"3D bias"},
    {"NPCSpeech", L"Mowa postaci niezależnych", L"NPC speech"},
    {"Resolution", L"Rozdzielczość", L"Resolution"},
    {"LightQuality", L"Jakość oświetlenia", L"Lighting quality"},
    {"BlendShadow", L"Mieszane cienie", L"Blended shadows"},
    {"Perspective", L"Perspektywa", L"Perspective"},
    {"Gamma", L"Gamma", L"Gamma"},
    {"Contrast", L"Kontrast", L"Contrast"},
    {"AutoMapMode", L"Rozmiar automapy", L"Automap size"},
    {"AutoMapFade", L"Zanikanie automapy", L"Fade automap"},
    {"AutoMapCenter", L"Centrowanie na postaci", L"Center when cleared"},
    {"AutoMapParty", L"Pokaż drużynę", L"Show party"},
    {"AutoMapPartyNames", L"Pokaż imiona drużyny", L"Show party names"},
    {"SmallOff", L"wyłączone", L"off"},
    {"SmallOn", L"włączone", L"on"},
    {"SmallNo", L"nie", L"no"},
    {"SmallYes", L"tak", L"yes"},
    {"Low", L"niska", L"low"},
    {"Medium", L"średnia", L"medium"},
    {"High", L"wysoka", L"high"},
    {"AudioOnly", L"tylko dźwięk", L"audio only"},
    {"TextOnly", L"tylko tekst", L"text only"},
    {"AudioText", L"dźwięk i tekst", L"audio and text"},
    {"Full", L"pełna", L"full screen"},
    {"Mini", L"mała", L"mini"},
    {"Center", L"środek", L"center"},
    {"Everything", L"wszystko", L"everything"},
}};

std::wstring Translate(const std::string &name)
{
    for (const Label &label : Labels)
    {
        if (_stricmp(label.name, name.c_str()) == 0)
            return Tr(label.polish, label.english);
    }
    if (_stricmp(name.c_str(), "Auto") == 0)
        return TrS(L"automatycznie", L"auto");
    return std::wstring(name.begin(), name.end());
}

std::wstring ValueText(const Entry &entry)
{
    if (entry.type == TypeChoice && entry.value >= 0 && entry.value < entry.choiceCount && entry.choiceCount <= 8)
    {
        std::array<char, 64> choice{};
        game::ReadBytes(entry.address + EntryChoiceNames + static_cast<uintptr_t>(entry.value) * ChoiceNameSize,
                        choice.data(), choice.size() - 1);
        return Translate(choice.data());
    }
    if (entry.type == TypeSlider && entry.choiceCount > 1)
    {
        const int percent = std::clamp(entry.value * 100 / (entry.choiceCount - 1), 0, 100);
        return std::to_wstring(percent) + L"%";
    }
    return {};
}

std::wstring EntryText(const std::vector<Entry> &entries, const Entry &entry)
{
    std::wstring text = Translate(entry.name);
    const std::wstring value = ValueText(entry);
    if (!value.empty())
        text += L": " + value;
    if (entry.type == TypeChoice)
        text += Tr(L", wybór", L", choice");
    else if (entry.type == TypeSlider)
        text += Tr(L", suwak", L", slider");

    int position = 0;
    int count = 0;
    for (const Entry &other : entries)
    {
        if (!other.selectable)
            continue;
        ++count;
        if (other.index == entry.index)
            position = count;
    }
    if (count > 0 && position > 0)
        text += L", " + std::to_wstring(position) + Tr(L" z ", L" of ") + std::to_wstring(count);
    return text;
}

std::wstring MenuTitle(const std::vector<Entry> &entries)
{
    if (!entries.empty() && entries.front().type == TypeTitle)
        return Translate(entries.front().name);
    // The options list has no title entry; the Escape menu starts with "Options".
    if (!entries.empty() && _stricmp(entries.front().name.c_str(), "SoundOptions") == 0)
        return TrS(L"Opcje", L"Options");
    return TrS(L"Menu gry", L"Game menu");
}

int SelectedIndex()
{
    std::int32_t selected = -1;
    game::Read(game::Absolute(VaSelectedEntry), selected);
    return selected;
}

const Entry *FindEntry(const std::vector<Entry> &entries, int index)
{
    if (index < 0 || index >= static_cast<int>(entries.size()))
        return nullptr;
    return &entries[static_cast<size_t>(index)];
}

void RememberSelection(const std::vector<Entry> &entries, int selected)
{
    g_state.selected = selected;
    const Entry *entry = FindEntry(entries, selected);
    g_state.value = entry != nullptr ? entry->value : INT_MIN;
}

void MoveSelection(int delta)
{
    uintptr_t header = 0;
    std::vector<Entry> entries = ReadEntries(header);
    std::vector<int> selectable;
    for (const Entry &entry : entries)
    {
        if (entry.selectable)
            selectable.push_back(entry.index);
    }
    if (selectable.empty())
        return;

    const int current = SelectedIndex();
    const auto it = std::find(selectable.begin(), selectable.end(), current);
    int position = 0;
    if (it == selectable.end())
        position = delta > 0 ? 0 : static_cast<int>(selectable.size()) - 1;
    else
    {
        const int count = static_cast<int>(selectable.size());
        position = (static_cast<int>(it - selectable.begin()) + delta + count) % count;
    }

    const int next = selectable[static_cast<size_t>(position)];
    SehWriteInt(game::Absolute(VaSelectedEntry), next);
    RememberSelection(entries, next);
    Say(EntryText(entries, entries[static_cast<size_t>(next)]));
}

void ChangeValue(int delta)
{
    uintptr_t header = 0;
    const std::vector<Entry> entries = ReadEntries(header);
    const Entry *entry = FindEntry(entries, SelectedIndex());
    if (entry == nullptr || !entry->selectable)
        return;
    if (entry->type != TypeChoice && entry->type != TypeSlider)
    {
        Say(EntryText(entries, *entry));
        return;
    }
    if (entry->choiceCount <= 1)
        return;

    int value = entry->value;
    if (entry->type == TypeChoice)
    {
        value = (value + delta + entry->choiceCount) % entry->choiceCount;
    }
    else
    {
        const int step = std::max(1, (entry->choiceCount - 1) / 20);
        value = std::clamp(value + delta * step, 0, entry->choiceCount - 1);
    }
    if (value == entry->value)
    {
        // Already at the end of a slider: repeat the value.
        Say(ValueText(*entry));
        return;
    }
    SehWriteInt(entry->address + EntryValue, value);
    const uintptr_t changedFn = game::ReadPtr(entry->address + EntryChangedFn);
    if (changedFn != 0)
        SehEntryFn(changedFn, entry->address);
    // The poll in UpdateGameMenu announces the value the game kept.
}

void Activate()
{
    uintptr_t header = 0;
    const std::vector<Entry> entries = ReadEntries(header);
    const Entry *entry = FindEntry(entries, SelectedIndex());
    if (entry == nullptr || !entry->selectable)
    {
        Say(Tr(L"Nic nie jest zaznaczone.", L"Nothing is selected."));
        return;
    }
    if (entry->type == TypeSlider)
    {
        Say(EntryText(entries, *entry) + Tr(L". Strzałki w lewo i w prawo zmieniają wartość.",
                                             L". Left and right arrows change the value."));
        return;
    }
    LogLine(L"GameMenu: activating " + std::wstring(entry->name.begin(), entry->name.end()));
    SehActivate(game::Absolute(VaActivateSelected));
}

} // namespace

void ResetGameMenu()
{
    g_state = State{};
    g_openForKeys = false;
}

void UpdateGameMenu()
{
    uintptr_t header = 0;
    std::vector<Entry> entries;
    const bool open = game::IsUiPanelOpen(UiGameMenu);
    if (open)
        entries = ReadEntries(header);
    const bool usable = open && !entries.empty();
    g_openForKeys = usable;

    if (!usable)
    {
        g_state = State{};
        return;
    }

    int selected = SelectedIndex();
    const Entry *entry = FindEntry(entries, selected);
    if (!g_state.wasOpen || header != g_state.header)
    {
        if (entry == nullptr || !entry->selectable)
        {
            for (const Entry &candidate : entries)
            {
                if (candidate.selectable)
                {
                    selected = candidate.index;
                    SehWriteInt(game::Absolute(VaSelectedEntry), selected);
                    break;
                }
            }
            entry = FindEntry(entries, selected);
        }
        std::wstring text = MenuTitle(entries) + L". ";
        if (!g_state.wasOpen)
            text += Tr(L"Strzałki w górę i w dół wybierają, Enter zatwierdza, strzałki w lewo i w prawo zmieniają "
                       L"ustawienie, Escape wraca. ",
                       L"Up and down arrows select, Enter confirms, left and right arrows change a setting, "
                       L"Escape goes back. ");
        if (entry != nullptr)
            text += EntryText(entries, *entry);
        Say(text);
    }
    else if (selected != g_state.selected)
    {
        if (entry != nullptr && entry->selectable)
            Say(EntryText(entries, *entry));
    }
    else if (entry != nullptr && entry->value != g_state.value)
    {
        const std::wstring value = ValueText(*entry);
        if (!value.empty())
            Say(value);
    }

    g_state.wasOpen = true;
    g_state.header = header;
    RememberSelection(entries, selected);
}

bool IsGameMenuOpen()
{
    return g_state.wasOpen;
}

bool IsGameMenuOpenForKeys()
{
    return g_openForKeys.load();
}

bool HandleGameMenuKey(DWORD virtualKey, bool ctrl, bool shift)
{
    (void)ctrl;
    (void)shift;
    if (!g_state.wasOpen)
        return false;

    switch (virtualKey)
    {
    case VK_UP:
    case VK_NUMPAD8:
        MoveSelection(-1);
        break;
    case VK_DOWN:
    case VK_NUMPAD2:
        MoveSelection(1);
        break;
    case VK_LEFT:
    case VK_NUMPAD4:
        ChangeValue(-1);
        break;
    case VK_RIGHT:
    case VK_NUMPAD6:
        ChangeValue(1);
        break;
    case VK_RETURN:
    case VK_SPACE:
        Activate();
        break;
    default:
        break;
    }
    // While the menu is open no gameplay key should move or act.
    return true;
}

} // namespace d2access::gamemenu
