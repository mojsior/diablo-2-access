#include "FrontendHooks.hpp"

#include "Addresses.hpp"
#include "AudioCue.hpp"
#include "D2Game.hpp"
#include "GameplayHooks.hpp"
#include "Hooking.hpp"
#include "Localization.hpp"
#include "Logging.hpp"
#include "Panels.hpp"
#include "ScreenReader.hpp"

#include <Windows.h>

#include <array>
#include <atomic>
#include <bitset>
#include <cstring>
#include <deque>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>

namespace d2access {
extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {

enum class FrontendScreen {
    Unknown,
    MainMenu,
    CharacterSelect,
    CharacterCreate,
    InGame,
};

using FE_MainMenu_Show_t = int(__cdecl *)();
using FE_CharacterCreate_Show_t = int(__cdecl *)();
using FE_CharacterCreate_HandleClassSelection_t = int(__stdcall *)(void **);
using FE_CharacterCreate_ValidateName_t = int(__cdecl *)();
using FE_CharacterCreate_SaveExists_t = int(__cdecl *)();
using FE_CharacterCreate_OnCreateButton_t = int(__stdcall *)(int);
using FE_CharacterCreate_Commit_t = int(__cdecl *)();
using FE_CharacterSelect_Show_t = int(__cdecl *)();
using FE_CharacterSelect_Update_t = int(__cdecl *)();
using FE_CharacterSelect_CreateNew_t = int(__stdcall *)(int);
using FE_MainMenu_Action_t = int(__stdcall *)(int);
using UI_ControlIsEnabled_t = int(__thiscall *)(void *);
using UI_ControlSetFlag_t = int(__fastcall *)(void *, int);
using UI_FocusControl_t = int(__stdcall *)(void *);
using GetKeyState_t = SHORT(WINAPI *)(int);
using GetAsyncKeyState_t = SHORT(WINAPI *)(int);
using DispatchMessageA_t = LRESULT(WINAPI *)(const MSG *);
using TranslateAcceleratorA_t = int(WINAPI *)(HWND, HACCEL, LPMSG);

InlineHook g_mainMenuHook;
InlineHook g_characterCreateShowHook;
InlineHook g_classSelectionHook;
InlineHook g_validateNameHook;
InlineHook g_createButtonHook;
InlineHook g_commitHook;
InlineHook g_characterSelectShowHook;
InlineHook g_characterSelectUpdateHook;

FE_MainMenu_Show_t g_realMainMenuShow = nullptr;
FE_CharacterCreate_Show_t g_realCharacterCreateShow = nullptr;
FE_CharacterCreate_HandleClassSelection_t g_realHandleClassSelection = nullptr;
FE_CharacterCreate_ValidateName_t g_realValidateName = nullptr;
FE_CharacterCreate_OnCreateButton_t g_realOnCreateButton = nullptr;
FE_CharacterCreate_Commit_t g_realCommit = nullptr;
FE_CharacterSelect_Show_t g_realCharacterSelectShow = nullptr;
FE_CharacterSelect_Update_t g_realCharacterSelectUpdate = nullptr;

std::atomic<FrontendScreen> g_currentScreen = FrontendScreen::Unknown;
std::atomic<int> g_lastSpokenClass = -1;
std::atomic<int> g_accessibilityClassIndex = 0;
std::atomic<int> g_lastSpokenCharacterSelectIndex = -1;
std::atomic<bool> g_createEnabled = false;
std::atomic<bool> g_keyboardHookRunning = false;
std::thread g_keyboardHookThread;
HHOOK g_keyboardHook = nullptr;
DWORD g_keyboardHookThreadId = 0;
std::bitset<256> g_keyDownState;
std::mutex g_keyDownMutex;
int g_mainMenuIndex = 0;
GetKeyState_t g_realGetKeyState = nullptr;
GetAsyncKeyState_t g_realGetAsyncKeyState = nullptr;
DispatchMessageA_t g_realDispatchMessageA = nullptr;
TranslateAcceleratorA_t g_realTranslateAcceleratorA = nullptr;
std::atomic<bool> g_mainMenuActionInProgress = false;
std::atomic<bool> g_deleteConfirmPending = false;
int g_pendingDeleteIndex = -1;
std::wstring g_pendingDeleteName;
std::mutex g_postedKeyMutex;
std::deque<std::pair<FrontendScreen, DWORD>> g_postedKeys;

constexpr std::array<uintptr_t, 5> ClassicClassButtonGlobals = {
    va::Global_ClassButtonAmazon,
    va::Global_ClassButtonSorceress,
    va::Global_ClassButtonNecromancer,
    va::Global_ClassButtonPaladin,
    va::Global_ClassButtonBarbarian,
};

constexpr std::array<const wchar_t *, 5> ClassicClassNames = {
    L"Amazon",
    L"Sorceress",
    L"Necromancer",
    L"Paladin",
    L"Barbarian",
};

// Class names by save/class id (Amazon, Sorceress, Necromancer, Paladin,
// Barbarian, Druid, Assassin).
const wchar_t *ClassDisplayName(int classId)
{
    static constexpr std::array<const wchar_t *, 7> Polish = {
        L"Amazonka", L"Czarodziejka", L"Nekromanta", L"Paladyn", L"Barbarzyńca", L"Druid", L"Zabójczyni",
    };
    static constexpr std::array<const wchar_t *, 7> English = {
        L"Amazon", L"Sorceress", L"Necromancer", L"Paladin", L"Barbarian", L"Druid", L"Assassin",
    };
    if (classId < 0 || classId >= static_cast<int>(Polish.size()))
        return Tr(L"nieznana klasa", L"Unknown class");
    return Tr(Polish[static_cast<size_t>(classId)], English[static_cast<size_t>(classId)]);
}

const wchar_t *FrontendScreenName(FrontendScreen screen)
{
    switch (screen)
    {
    case FrontendScreen::MainMenu:
        return L"MainMenu";
    case FrontendScreen::CharacterSelect:
        return L"CharacterSelect";
    case FrontendScreen::CharacterCreate:
        return L"CharacterCreate";
    case FrontendScreen::InGame:
        return L"InGame";
    default:
        return L"Unknown";
    }
}

bool IsDiagnosticVirtualKey(DWORD virtualKey)
{
    return virtualKey == VK_F1 || virtualKey == VK_PRIOR || virtualKey == VK_NEXT ||
           virtualKey == VK_HOME || virtualKey == VK_UP || virtualKey == VK_DOWN ||
           virtualKey == VK_LEFT || virtualKey == VK_RIGHT || virtualKey == 'E' ||
           virtualKey == 'F' || virtualKey == 'G' || virtualKey == 'H' || virtualKey == 'K' ||
           virtualKey == 'L' || virtualKey == VK_CLEAR ||
           (virtualKey >= VK_NUMPAD1 && virtualKey <= VK_NUMPAD9);
}

bool IsInGameAccessVirtualKey(DWORD virtualKey)
{
    return virtualKey == VK_F1 || virtualKey == VK_PRIOR || virtualKey == VK_NEXT ||
           virtualKey == VK_HOME || virtualKey == VK_UP || virtualKey == VK_DOWN ||
           virtualKey == VK_LEFT || virtualKey == VK_RIGHT || virtualKey == 'E' ||
           virtualKey == 'F' || virtualKey == 'G' || virtualKey == 'H' || virtualKey == 'K' ||
           virtualKey == 'L' || virtualKey == VK_CLEAR ||
           (virtualKey >= VK_NUMPAD1 && virtualKey <= VK_NUMPAD9);
}

DWORD TranslateLowLevelInGameVirtualKey(DWORD virtualKey, DWORD flags)
{
    const bool extended = (flags & LLKHF_EXTENDED) != 0;
    if (extended)
        return virtualKey;

    switch (virtualKey)
    {
    case VK_END:
        return VK_NUMPAD1;
    case VK_DOWN:
        return VK_NUMPAD2;
    case VK_NEXT:
        return VK_NUMPAD3;
    case VK_LEFT:
        return VK_NUMPAD4;
    case VK_CLEAR:
        return VK_NUMPAD5;
    case VK_RIGHT:
        return VK_NUMPAD6;
    case VK_HOME:
        return VK_NUMPAD7;
    case VK_UP:
        return VK_NUMPAD8;
    case VK_PRIOR:
        return VK_NUMPAD9;
    default:
        return virtualKey;
    }
}

constexpr size_t CharacterSelectNodeOffset_Name = 0x000;
constexpr size_t CharacterSelectNodeOffset_ClassId = 0x320;
constexpr size_t CharacterSelectNodeOffset_Level = 0x322;
constexpr size_t CharacterSelectNodeOffset_Flags = 0x324;
constexpr size_t CharacterSelectNodeOffset_Next = 0x34C;

constexpr std::array<size_t, 5> ClassicClassStateOffsets = {
    129, // Amazon
    131, // Sorceress
    132, // Necromancer
    130, // Paladin
    133, // Barbarian
};

struct MainMenuEntry {
    int stringId;
    uintptr_t callback;
    const wchar_t *polish;
    const wchar_t *english;
    bool needsFullInstall;
};

// The buttons FE_MainMenu_Show builds, top to bottom. Labels come from the
// game's string table so they match the installed language.
constexpr std::array<MainMenuEntry, 7> MainMenuEntries = {{
    {5106, va::FE_MainMenu_OpenSinglePlayer, L"Gra jednoosobowa", L"Single Player", false},
    {5107, va::FE_MainMenu_OpenBattleNet, L"Battle.net", L"Battle.net", true},
    {0, va::FE_MainMenu_OpenGateway, L"Brama Battle.net", L"Battle.net gateway", true},
    {5108, va::FE_MainMenu_OpenOtherMultiplayer, L"Inne tryby wieloosobowe", L"Other Multiplayer", true},
    {5110, va::FE_MainMenu_OpenCredits, L"Twórcy", L"Credits", false},
    {5111, va::FE_MainMenu_OpenCinematics, L"Filmy", L"Cinematics", false},
    {5109, va::FE_MainMenu_ExitGame, L"Wyjdź z Diablo II", L"Exit Diablo II", false},
}};

HWND GetForegroundGameWindow()
{
    HWND foreground = GetForegroundWindow();
    if (foreground == nullptr)
        return nullptr;

    DWORD pid = 0;
    GetWindowThreadProcessId(foreground, &pid);
    return pid == GetCurrentProcessId() ? foreground : nullptr;
}

bool HasGameFocus()
{
    return GetForegroundGameWindow() != nullptr;
}

bool MessageBelongsToGameWindow(const MSG &message)
{
    if (message.hwnd == nullptr)
        return HasGameFocus();

    DWORD pid = 0;
    GetWindowThreadProcessId(message.hwnd, &pid);
    return pid == GetCurrentProcessId();
}

template <typename T>
T ReadAbsolute(uintptr_t va)
{
    return *reinterpret_cast<T *>(AbsoluteAddress(va));
}

template <typename T>
void WriteAbsolute(uintptr_t va, T value)
{
    *reinterpret_cast<T *>(AbsoluteAddress(va)) = value;
}

bool IsReadableMemory(uintptr_t address, size_t size)
{
    if (address == 0 || size == 0)
        return false;

    MEMORY_BASIC_INFORMATION info = {};
    if (VirtualQuery(reinterpret_cast<const void *>(address), &info, sizeof(info)) == 0)
        return false;

    if (info.State != MEM_COMMIT)
        return false;

    if ((info.Protect & PAGE_GUARD) != 0 || (info.Protect & PAGE_NOACCESS) != 0)
        return false;

    const uintptr_t regionStart = reinterpret_cast<uintptr_t>(info.BaseAddress);
    const uintptr_t regionEnd = regionStart + info.RegionSize;
    return address >= regionStart && address + size <= regionEnd;
}

template <typename T>
bool TryRead(uintptr_t address, T &out)
{
    static_assert(std::is_trivially_copyable_v<T>);
    if (!IsReadableMemory(address, sizeof(T)))
        return false;

    std::memcpy(&out, reinterpret_cast<const void *>(address), sizeof(T));
    return true;
}

template <typename T>
bool TryReadField(uintptr_t base, size_t offset, T &out)
{
    return TryRead(base + offset, out);
}

std::wstring WidenAnsi(const char *text, int length)
{
    if (text == nullptr || length <= 0)
        return {};

    const int needed = MultiByteToWideChar(CP_ACP, 0, text, length, nullptr, 0);
    if (needed <= 0)
        return {};

    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, length, wide.data(), needed);
    return wide;
}

std::wstring ReadFixedAnsiString(uintptr_t address, size_t maxChars)
{
    if (maxChars == 0 || maxChars > 255 || !IsReadableMemory(address, maxChars))
        return {};

    char buffer[256] = {};
    std::memcpy(buffer, reinterpret_cast<const void *>(address), maxChars);
    buffer[maxChars] = 0;

    size_t length = 0;
    while (length < maxChars && buffer[length] != 0)
        ++length;

    return WidenAnsi(buffer, static_cast<int>(length));
}

bool IsCreateButtonEnabled()
{
    auto control = ReadAbsolute<void *>(va::Global_CreateButton);
    if (control == nullptr)
        return false;

    auto fn = reinterpret_cast<UI_ControlIsEnabled_t>(AbsoluteAddress(va::UI_ControlIsEnabled));
    return fn(control) != 0;
}

void SetControlEnabled(void *control, bool enabled)
{
    if (control == nullptr)
        return;

    auto fn = reinterpret_cast<UI_ControlSetFlag_t>(AbsoluteAddress(va::UI_ControlSetEnabled));
    fn(control, enabled ? 1 : 0);
}

void FocusControl(void *control)
{
    if (control == nullptr)
        return;

    auto fn = reinterpret_cast<UI_FocusControl_t>(AbsoluteAddress(va::UI_FocusControl));
    fn(control);
}

int NormalizeClassIndex(int rawIndex)
{
    if (rawIndex < 0 || rawIndex >= static_cast<int>(ClassicClassNames.size()))
        return -1;
    return rawIndex;
}

void SpeakCurrentClassIfNeeded(bool interrupt)
{
    int selectedClass = NormalizeClassIndex(g_accessibilityClassIndex.load());
    if (selectedClass < 0)
        return;

    int previous = g_lastSpokenClass.exchange(selectedClass);
    if (previous == selectedClass)
        return;

    LogLine(std::wstring(L"CharacterCreate class: ") +
            ClassicClassNames[static_cast<size_t>(selectedClass)]);
    Speak(ClassDisplayName(selectedClass), interrupt);
}

const wchar_t *CharacterClassNameFromSave(std::uint8_t classId)
{
    return ClassDisplayName(classId);
}

uintptr_t CharacterSelectNodeAtIndex(int selectedIndex)
{
    if (selectedIndex < 0)
        return 0;

    uintptr_t node = ReadAbsolute<uintptr_t>(va::Global_CharacterSelectHead);
    for (int index = 0; index < selectedIndex && node != 0; ++index)
    {
        uintptr_t next = 0;
        if (!TryReadField(node, CharacterSelectNodeOffset_Next, next))
            return 0;
        node = next;
    }
    return node;
}

std::wstring DescribeSelectedCharacter()
{
    const int count = ReadAbsolute<int>(va::Global_CharacterSelectCount);
    if (count <= 0)
        return Tr(L"Brak zapisanych postaci. Naciśnij N, aby stworzyć nową postać.",
                  L"No saved characters. Press N to create a new character.");

    const int selectedIndex = ReadAbsolute<int>(va::Global_CharacterSelectSelectedIndex);
    uintptr_t node = CharacterSelectNodeAtIndex(selectedIndex);
    if (node == 0)
        return Tr(L"Wybór postaci. Naciśnij N, aby stworzyć nową postać.",
                  L"Character selection. Press N to create a new character.");

    std::uint8_t classId = 0;
    std::uint16_t level = 0;
    std::uint16_t flags = 0;
    TryReadField(node, CharacterSelectNodeOffset_ClassId, classId);
    TryReadField(node, CharacterSelectNodeOffset_Level, level);
    TryReadField(node, CharacterSelectNodeOffset_Flags, flags);

    std::wstring name = ReadFixedAnsiString(node + CharacterSelectNodeOffset_Name, 31);
    if (name.empty())
        name = Tr(L"Postać bez imienia", L"Unnamed character");

    std::wstringstream stream;
    stream << name << L", " << CharacterClassNameFromSave(classId);
    if (level > 0)
        stream << Tr(L", poziom ", L", level ") << level;
    if ((flags & 0x0004) != 0)
        stream << Tr(L", hardcore", L", hardcore");
    stream << Tr(L". Postać ", L". Character ") << (selectedIndex + 1) << Tr(L" z ", L" of ") << count
           << Tr(L". Enter rozpoczyna grę. N tworzy nową postać, Delete usuwa postać.",
                 L". Press Enter to play. Press N to create a new character, Delete to delete it.");
    return stream.str();
}

void SpeakSelectedCharacter(bool force)
{
    const int selectedIndex = ReadAbsolute<int>(va::Global_CharacterSelectSelectedIndex);
    if (!force && g_lastSpokenCharacterSelectIndex.exchange(selectedIndex) == selectedIndex)
        return;

    if (force)
        g_lastSpokenCharacterSelectIndex = selectedIndex;

    std::wstring text = DescribeSelectedCharacter();
    LogLine(L"CharacterSelect selected: " + text);
    Speak(text, true);
}

void ApplyNativeCharacterClassSelection(int index)
{
    if (index < 0 || index >= static_cast<int>(ClassicClassButtonGlobals.size()))
        return;

    WriteAbsolute<int>(va::Global_SelectedClassIndex, index);

    auto characterState = ReadAbsolute<unsigned char *>(va::Global_CharacterCreateState);
    if (characterState != nullptr)
    {
        characterState[490] = static_cast<unsigned char>(index);
        for (size_t offset = 129; offset <= 135; ++offset)
            characterState[offset] = 0;
        characterState[ClassicClassStateOffsets[static_cast<size_t>(index)]] = 1;
    }

    void *nameField = ReadAbsolute<void *>(va::Global_NameField);
    void *createButton = ReadAbsolute<void *>(va::Global_CreateButton);
    SetControlEnabled(nameField, true);
    SetControlEnabled(createButton, true);
    FocusControl(nameField);

    if (g_realValidateName != nullptr)
        g_realValidateName();
    g_createEnabled = IsCreateButtonEnabled();
}

void SelectCharacterClass(int index)
{
    if (index < 0 || index >= static_cast<int>(ClassicClassButtonGlobals.size()))
        return;

    g_accessibilityClassIndex = index;
    ApplyNativeCharacterClassSelection(index);
    SpeakCurrentClassIfNeeded(true);
}

void SelectRelativeCharacterClass(int delta)
{
    int current = NormalizeClassIndex(g_accessibilityClassIndex.load());
    if (current < 0)
        current = 0;

    current += delta;
    if (current < 0)
        current = static_cast<int>(ClassicClassButtonGlobals.size()) - 1;
    else if (current >= static_cast<int>(ClassicClassButtonGlobals.size()))
        current = 0;

    SelectCharacterClass(current);
}

bool RefreshCreateButtonState()
{
    if (g_realValidateName != nullptr)
        g_realValidateName();

    const bool enabled = IsCreateButtonEnabled();
    g_createEnabled = enabled;
    LogLine(enabled ? L"CharacterCreate create button: enabled"
                    : L"CharacterCreate create button: disabled");
    return enabled;
}

bool DoesEnteredCharacterSaveExist()
{
    const int mode = ReadAbsolute<int>(va::Global_FrontendCreateMode);
    if (mode == 1)
        return false;

    auto fn = reinterpret_cast<FE_CharacterCreate_SaveExists_t>(
        AbsoluteAddress(va::FE_CharacterCreate_SaveExists));
    return fn() == 1;
}

bool ConfirmCharacterCreate()
{
    LogLine(L"CharacterCreate key: CONFIRM");
    if (!RefreshCreateButtonState())
    {
        LogLine(L"CharacterCreate create blocked: name is not valid yet");
        Speak(Tr(L"Najpierw wpisz poprawne imię postaci.", L"Type a valid character name first."), true);
        return true;
    }

    if (DoesEnteredCharacterSaveExist())
    {
        LogLine(L"CharacterCreate create blocked: save file already exists");
        Speak(Tr(L"Postać o tym imieniu już istnieje. Wpisz inne imię albo naciśnij Escape i wczytaj istniejącą "
                 L"postać z wyboru postaci.",
                 L"Character name already exists. Type a different name, or press Escape and load the existing "
                 L"character from character selection."),
              true);
        FocusControl(ReadAbsolute<void *>(va::Global_NameField));
        return true;
    }

    if (g_realOnCreateButton == nullptr)
    {
        LogLine(L"CharacterCreate create button: native callback is missing");
        Speak(Tr(L"Nie można stworzyć postaci.", L"Cannot create character. Native callback is not available."),
              true);
        return true;
    }

    LogLine(L"CharacterCreate create button: invoking native callback");
    Speak(Tr(L"Tworzę postać.", L"Creating character."), true);
    g_currentScreen = FrontendScreen::Unknown;
    const int result = g_realOnCreateButton(0);
    LogLine(std::wstring(L"CharacterCreate create button callback returned: ") +
            std::to_wstring(result));
    return true;
}

bool IsMainMenuEntryAvailable(const MainMenuEntry &entry)
{
    if (!entry.needsFullInstall)
        return true;
    using IsSpawn_t = int(__cdecl *)();
    return reinterpret_cast<IsSpawn_t>(AbsoluteAddress(va::FE_IsSpawnInstall))() == 0;
}

std::wstring MainMenuEntryLabel(const MainMenuEntry &entry)
{
    std::wstring label = entry.stringId != 0 ? panels::CleanText(game::StringById(entry.stringId)) : std::wstring();
    return label.empty() ? TrS(entry.polish, entry.english) : label;
}

std::wstring MainMenuEntryText(int index)
{
    const MainMenuEntry &entry = MainMenuEntries[static_cast<size_t>(index)];
    std::wstringstream stream;
    stream << MainMenuEntryLabel(entry);
    if (!IsMainMenuEntryAvailable(entry))
        stream << Tr(L", niedostępne", L", unavailable");
    stream << L", " << (index + 1) << Tr(L" z ", L" of ") << MainMenuEntries.size();
    return stream.str();
}

void MoveMainMenuSelection(int delta)
{
    const int count = static_cast<int>(MainMenuEntries.size());
    g_mainMenuIndex = ((g_mainMenuIndex + delta) % count + count) % count;
    const std::wstring text = MainMenuEntryText(g_mainMenuIndex);
    LogLine(L"MainMenu selected: " + text);
    Speak(text, true);
}

bool ActivateMainMenuEntry(int index, const wchar_t *source)
{
    const MainMenuEntry &entry = MainMenuEntries[static_cast<size_t>(index)];
    const std::wstring label = MainMenuEntryLabel(entry);
    if (!IsMainMenuEntryAvailable(entry))
    {
        Speak(label + Tr(L": niedostępne w tej instalacji gry.", L": not available in this installation."), true);
        return true;
    }
    if (g_mainMenuActionInProgress.exchange(true))
    {
        LogLine(std::wstring(L"MainMenu key: ") + source + L" ignored, transition already in progress.");
        return true;
    }

    LogLine(std::wstring(L"MainMenu key: ") + source + L" " + label);
    Speak(TrS(L"Otwieram: ", L"Opening: ") + label, true);
    g_currentScreen = FrontendScreen::Unknown;
    {
        std::scoped_lock lock(g_keyDownMutex);
        g_keyDownState.reset();
    }
    {
        std::scoped_lock lock(g_postedKeyMutex);
        g_postedKeys.clear();
    }
    reinterpret_cast<FE_MainMenu_Action_t>(AbsoluteAddress(entry.callback))(0);
    return true;
}

bool OpenNewCharacterFromCharacterSelect()
{
    LogLine(L"CharacterSelect key: New Character");
    Speak(Tr(L"Tworzę nową postać.", L"Creating new character."), true);
    g_currentScreen = FrontendScreen::Unknown;
    {
        std::scoped_lock lock(g_keyDownMutex);
        g_keyDownState.reset();
    }
    {
        std::scoped_lock lock(g_postedKeyMutex);
        g_postedKeys.clear();
    }
    reinterpret_cast<FE_CharacterSelect_CreateNew_t>(
        AbsoluteAddress(va::FE_CharacterSelect_CreateNew))(0);
    return true;
}

bool HandleMainMenuVirtualKey(DWORD virtualKey)
{
    switch (virtualKey)
    {
    case VK_UP:
        MoveMainMenuSelection(-1);
        return true;
    case VK_DOWN:
        MoveMainMenuSelection(1);
        return true;
    case '1':
        return ActivateMainMenuEntry(0, L"1");
    case VK_RETURN:
        return ActivateMainMenuEntry(g_mainMenuIndex, L"ENTER");
    case VK_SPACE:
        return ActivateMainMenuEntry(g_mainMenuIndex, L"SPACE");
    default:
        return false;
    }
}

bool RequestDeleteSelectedCharacter()
{
    const int count = ReadAbsolute<int>(va::Global_CharacterSelectCount);
    const int index = ReadAbsolute<int>(va::Global_CharacterSelectSelectedIndex);
    const uintptr_t node = CharacterSelectNodeAtIndex(index);
    std::uint8_t classId = 0xFF;
    if (node != 0)
        TryReadField(node, CharacterSelectNodeOffset_ClassId, classId);
    // The "new character" slot has no class and cannot be deleted.
    if (count <= 0 || node == 0 || classId > 6)
    {
        Speak(Tr(L"Najpierw wybierz postać, którą chcesz usunąć.", L"First select the character you want to delete."),
              true);
        return true;
    }

    g_pendingDeleteName = ReadFixedAnsiString(node + CharacterSelectNodeOffset_Name, 31);
    g_pendingDeleteIndex = index;
    g_deleteConfirmPending = true;
    LogLine(L"CharacterSelect: delete requested for " + g_pendingDeleteName);
    Speak(TrS(L"Usunąć postać ", L"Delete character ") + g_pendingDeleteName +
              Tr(L"? Tego nie można cofnąć. Enter usuwa, Escape anuluje.",
                 L"? This cannot be undone. Press Enter to delete, Escape to cancel."),
          true);
    return true;
}

bool ConfirmDeleteCharacter()
{
    g_deleteConfirmPending = false;
    if (ReadAbsolute<int>(va::Global_CharacterSelectSelectedIndex) != g_pendingDeleteIndex)
    {
        Speak(Tr(L"Wybrana postać się zmieniła. Usuwanie anulowane.", L"The selection changed. Deletion cancelled."),
              true);
        return true;
    }

    const int before = ReadAbsolute<int>(va::Global_CharacterSelectCount);
    LogLine(L"CharacterSelect: deleting " + g_pendingDeleteName);
    {
        std::scoped_lock lock(g_keyDownMutex);
        g_keyDownState.reset();
    }
    reinterpret_cast<FE_MainMenu_Action_t>(AbsoluteAddress(va::FE_CharacterSelect_DeleteConfirmed))(0);

    const int after = ReadAbsolute<int>(va::Global_CharacterSelectCount);
    if (after < before)
        Speak(TrS(L"Usunięto postać ", L"Deleted character ") + g_pendingDeleteName + L". " +
                  DescribeSelectedCharacter(),
              true);
    else
        Speak(Tr(L"Nie udało się usunąć postaci.", L"Could not delete the character."), true);
    return true;
}

bool HandleCharacterSelectVirtualKey(DWORD virtualKey)
{
    switch (virtualKey)
    {
    case 'N':
        g_deleteConfirmPending = false;
        return OpenNewCharacterFromCharacterSelect();
    case VK_DELETE:
        return RequestDeleteSelectedCharacter();
    case VK_RETURN:
        return g_deleteConfirmPending.load() ? ConfirmDeleteCharacter() : false;
    case VK_ESCAPE:
        if (!g_deleteConfirmPending.exchange(false))
            return false;
        Speak(Tr(L"Anulowano usuwanie postaci.", L"Deletion cancelled."), true);
        return true;
    default:
        return false;
    }
}

bool IsCharacterSelectKey(DWORD virtualKey)
{
    return virtualKey == 'N' || virtualKey == VK_DELETE ||
           ((virtualKey == VK_RETURN || virtualKey == VK_ESCAPE) && g_deleteConfirmPending.load());
}

bool HandleCharacterCreateVirtualKey(DWORD virtualKey)
{
    switch (virtualKey)
    {
    case VK_UP:
    case VK_LEFT:
        SelectRelativeCharacterClass(-1);
        LogLine(virtualKey == VK_UP ? L"CharacterCreate key: UP" : L"CharacterCreate key: LEFT");
        return true;
    case VK_DOWN:
    case VK_RIGHT:
        SelectRelativeCharacterClass(1);
        LogLine(virtualKey == VK_DOWN ? L"CharacterCreate key: DOWN"
                                      : L"CharacterCreate key: RIGHT");
        return true;
    case '1':
        SelectCharacterClass(0);
        LogLine(L"CharacterCreate key: 1");
        return true;
    case '2':
        SelectCharacterClass(1);
        LogLine(L"CharacterCreate key: 2");
        return true;
    case '3':
        SelectCharacterClass(2);
        LogLine(L"CharacterCreate key: 3");
        return true;
    case '4':
        SelectCharacterClass(3);
        LogLine(L"CharacterCreate key: 4");
        return true;
    case '5':
        SelectCharacterClass(4);
        LogLine(L"CharacterCreate key: 5");
        return true;
    case VK_RETURN:
    case VK_SPACE:
        return ConfirmCharacterCreate();
    default:
        return false;
    }
}

bool ShouldCaptureVirtualKey(FrontendScreen screen, DWORD virtualKey)
{
    switch (screen)
    {
    case FrontendScreen::MainMenu:
        return virtualKey == VK_UP || virtualKey == VK_DOWN || virtualKey == VK_RETURN ||
               virtualKey == VK_SPACE || virtualKey == '1';
    case FrontendScreen::CharacterSelect:
        return IsCharacterSelectKey(virtualKey);
    case FrontendScreen::CharacterCreate:
        return virtualKey == VK_LEFT || virtualKey == VK_RIGHT || virtualKey == VK_UP ||
               virtualKey == VK_DOWN || virtualKey == VK_RETURN || virtualKey == VK_SPACE ||
               virtualKey == '1' || virtualKey == '2' || virtualKey == '3' ||
               virtualKey == '4' || virtualKey == '5';
    case FrontendScreen::InGame:
        if (IsGameplayDialogActive() && (virtualKey == 'E' || virtualKey == 'F'))
            return false;

        return IsGameplayKeyCaptured(virtualKey);
    default:
        return false;
    }
}

bool ShouldLowLevelCaptureVirtualKey(FrontendScreen screen, DWORD virtualKey)
{
    if ((virtualKey == VK_RETURN || virtualKey == VK_SPACE) && screen != FrontendScreen::InGame)
        return false;

    switch (screen)
    {
    case FrontendScreen::MainMenu:
        return virtualKey == VK_UP || virtualKey == VK_DOWN;
    case FrontendScreen::CharacterSelect:
        return IsCharacterSelectKey(virtualKey);
    case FrontendScreen::CharacterCreate:
        return virtualKey == VK_LEFT || virtualKey == VK_RIGHT || virtualKey == VK_UP ||
               virtualKey == VK_DOWN || virtualKey == '1' || virtualKey == '2' ||
               virtualKey == '3' || virtualKey == '4' || virtualKey == '5';
    case FrontendScreen::InGame:
        if (IsGameplayDialogActive() && (virtualKey == 'E' || virtualKey == 'F'))
            return false;

        return IsGameplayKeyCaptured(virtualKey);
    default:
        return false;
    }
}

bool IsKeyMessage(UINT message)
{
    return message == WM_KEYDOWN || message == WM_SYSKEYDOWN || message == WM_KEYUP ||
           message == WM_SYSKEYUP;
}

bool DispatchCapturedVirtualKey(FrontendScreen screen, DWORD virtualKey)
{
    switch (screen)
    {
    case FrontendScreen::MainMenu:
        return HandleMainMenuVirtualKey(virtualKey);
    case FrontendScreen::CharacterSelect:
        return HandleCharacterSelectVirtualKey(virtualKey);
    case FrontendScreen::CharacterCreate:
        return HandleCharacterCreateVirtualKey(virtualKey);
    case FrontendScreen::InGame:
        return HandleGameplayVirtualKey(virtualKey);
    default:
        return false;
    }
}

bool PostCapturedVirtualKey(FrontendScreen screen, DWORD virtualKey)
{
    HWND window = GetForegroundGameWindow();
    if (window == nullptr)
        return false;

    {
        std::scoped_lock lock(g_postedKeyMutex);
        if (g_postedKeys.size() >= 32)
            g_postedKeys.pop_front();
        g_postedKeys.emplace_back(screen, virtualKey);
    }

    return PostMessageW(window, WM_NULL, 0, 0) != FALSE;
}

bool ConsumePostedFrontendKeyMessage(const MSG &message)
{
    if (message.message != WM_NULL || !MessageBelongsToGameWindow(message))
        return false;

    FrontendScreen screen = FrontendScreen::Unknown;
    DWORD virtualKey = 0;
    {
        std::scoped_lock lock(g_postedKeyMutex);
        if (g_postedKeys.empty())
            return false;

        screen = g_postedKeys.front().first;
        virtualKey = g_postedKeys.front().second;
        g_postedKeys.pop_front();
    }

    if (screen != g_currentScreen.load())
    {
        LogLine(L"Posted frontend key ignored, screen changed.");
        return true;
    }

    if (!ShouldCaptureVirtualKey(screen, virtualKey))
        return true;

    DispatchCapturedVirtualKey(screen, virtualKey);
    return true;
}

bool ConsumeFrontendKeyMessage(const MSG &message)
{
    if (!IsKeyMessage(message.message) || !MessageBelongsToGameWindow(message))
        return false;

    const DWORD virtualKey = static_cast<DWORD>(message.wParam);
    const FrontendScreen screen = g_currentScreen.load();
    if (!ShouldCaptureVirtualKey(screen, virtualKey))
        return false;

    const bool keyDown = (message.message == WM_KEYDOWN || message.message == WM_SYSKEYDOWN);
    const bool keyUp = (message.message == WM_KEYUP || message.message == WM_SYSKEYUP);

    if (screen == FrontendScreen::InGame && (keyDown || keyUp))
        NotifyGameplayVirtualKeyState(virtualKey, keyDown);

    if (virtualKey < 256)
    {
        std::scoped_lock lock(g_keyDownMutex);
        if (keyDown)
        {
            if (g_keyDownState.test(virtualKey))
                return true;
            g_keyDownState.set(virtualKey);
        }
        else if (keyUp)
        {
            g_keyDownState.reset(virtualKey);
            return true;
        }
    }

    if (keyDown)
    {
        if (screen == FrontendScreen::CharacterCreate &&
            (virtualKey == VK_RETURN || virtualKey == VK_SPACE))
        {
            if (!PostCapturedVirtualKey(screen, virtualKey))
                DispatchCapturedVirtualKey(screen, virtualKey);
            return true;
        }

        DispatchCapturedVirtualKey(screen, virtualKey);
    }

    return true;
}

bool ShouldSuppressNativeKeyState(int virtualKey)
{
    if (!HasGameFocus())
        return false;
    return ShouldCaptureVirtualKey(g_currentScreen.load(), static_cast<DWORD>(virtualKey));
}

SHORT WINAPI Hook_GetKeyState(int virtualKey)
{
    if (ShouldSuppressNativeKeyState(virtualKey))
        return 0;
    return g_realGetKeyState != nullptr ? g_realGetKeyState(virtualKey) : 0;
}

SHORT WINAPI Hook_GetAsyncKeyState(int virtualKey)
{
    if (ShouldSuppressNativeKeyState(virtualKey))
        return 0;
    return g_realGetAsyncKeyState != nullptr ? g_realGetAsyncKeyState(virtualKey) : 0;
}

LRESULT WINAPI Hook_DispatchMessageA(const MSG *message)
{
    if (message != nullptr)
    {
        if (ConsumePostedFrontendKeyMessage(*message))
            return 0;
        if (ConsumeFrontendKeyMessage(*message))
            return 0;
    }

    return g_realDispatchMessageA != nullptr ? g_realDispatchMessageA(message) : 0;
}

int WINAPI Hook_TranslateAcceleratorA(HWND window, HACCEL acceleratorTable, LPMSG message)
{
    if (message != nullptr)
    {
        if (ConsumePostedFrontendKeyMessage(*message))
            return 1;
        if (ConsumeFrontendKeyMessage(*message))
            return 1;
    }

    return g_realTranslateAcceleratorA != nullptr
               ? g_realTranslateAcceleratorA(window, acceleratorTable, message)
               : 0;
}

bool PatchPointer(uintptr_t va, void *replacement, void **originalOut)
{
    auto *slot = reinterpret_cast<void **>(AbsoluteAddress(va));
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &oldProtect))
        return false;

    *originalOut = *slot;
    *slot = replacement;
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *));
    VirtualProtect(slot, sizeof(void *), oldProtect, &oldProtect);
    return true;
}

void RestorePointer(uintptr_t va, void *original)
{
    if (original == nullptr)
        return;

    auto *slot = reinterpret_cast<void **>(AbsoluteAddress(va));
    DWORD oldProtect = 0;
    if (!VirtualProtect(slot, sizeof(void *), PAGE_READWRITE, &oldProtect))
        return;

    *slot = original;
    FlushInstructionCache(GetCurrentProcess(), slot, sizeof(void *));
    VirtualProtect(slot, sizeof(void *), oldProtect, &oldProtect);
}

bool InstallInputStateHooks()
{
    bool ok = true;
    ok = PatchPointer(va::IAT_GetKeyState, reinterpret_cast<void *>(&Hook_GetKeyState),
                      reinterpret_cast<void **>(&g_realGetKeyState)) && ok;
    ok = PatchPointer(va::IAT_GetAsyncKeyState, reinterpret_cast<void *>(&Hook_GetAsyncKeyState),
                      reinterpret_cast<void **>(&g_realGetAsyncKeyState)) && ok;
    ok = PatchPointer(va::IAT_DispatchMessageA, reinterpret_cast<void *>(&Hook_DispatchMessageA),
                      reinterpret_cast<void **>(&g_realDispatchMessageA)) && ok;
    ok = PatchPointer(va::IAT_TranslateAcceleratorA,
                      reinterpret_cast<void *>(&Hook_TranslateAcceleratorA),
                      reinterpret_cast<void **>(&g_realTranslateAcceleratorA)) &&
         ok;
    LogLine(ok ? L"Native input hooks installed." : L"Failed to install native input hooks.");
    return ok;
}

void RemoveInputStateHooks()
{
    RestorePointer(va::IAT_TranslateAcceleratorA,
                   reinterpret_cast<void *>(g_realTranslateAcceleratorA));
    RestorePointer(va::IAT_DispatchMessageA, reinterpret_cast<void *>(g_realDispatchMessageA));
    RestorePointer(va::IAT_GetAsyncKeyState, reinterpret_cast<void *>(g_realGetAsyncKeyState));
    RestorePointer(va::IAT_GetKeyState, reinterpret_cast<void *>(g_realGetKeyState));
    g_realTranslateAcceleratorA = nullptr;
    g_realDispatchMessageA = nullptr;
    g_realGetAsyncKeyState = nullptr;
    g_realGetKeyState = nullptr;
}

LRESULT CALLBACK LowLevelKeyboardProc(int code, WPARAM wParam, LPARAM lParam)
{
    if (code < 0)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    const auto *info = reinterpret_cast<KBDLLHOOKSTRUCT *>(lParam);
    if (info == nullptr)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    const DWORD rawVirtualKey = info->vkCode;
    const bool keyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool keyUp = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    const FrontendScreen screen = g_currentScreen.load();
    const DWORD virtualKey = screen == FrontendScreen::InGame
                                 ? TranslateLowLevelInGameVirtualKey(rawVirtualKey,
                                                                     info->flags)
                                 : rawVirtualKey;
    const bool hasFocus = HasGameFocus();
    const bool dialogActive = screen == FrontendScreen::InGame && IsGameplayDialogActive();
    const bool captureKey = hasFocus && ShouldLowLevelCaptureVirtualKey(screen, virtualKey);

    if (keyDown && (IsDiagnosticVirtualKey(virtualKey) ||
                    IsDiagnosticVirtualKey(rawVirtualKey)))
    {
        std::wstringstream stream;
        stream << L"Low-level key vk 0x" << std::hex << rawVirtualKey
               << L", mapped 0x" << virtualKey << std::dec << L", scan 0x"
               << std::hex << info->scanCode << std::dec << L", extended "
               << ((info->flags & LLKHF_EXTENDED) != 0 ? L"yes" : L"no")
               << L". Screen " << FrontendScreenName(screen) << L". Focus "
               << (hasFocus ? L"yes" : L"no") << L". Dialog "
               << (dialogActive ? L"yes" : L"no") << L". Capture "
               << (captureKey ? L"yes" : L"no") << L".";
        LogLine(stream.str());
    }

    if (!captureKey)
        return CallNextHookEx(g_keyboardHook, code, wParam, lParam);

    if (virtualKey < 256)
    {
        std::scoped_lock lock(g_keyDownMutex);
        if (keyDown)
        {
            if (g_keyDownState.test(virtualKey))
                return 1;
            g_keyDownState.set(virtualKey);
        }
        else if (keyUp)
        {
            g_keyDownState.reset(virtualKey);
            if (screen == FrontendScreen::InGame)
                NotifyGameplayVirtualKeyState(virtualKey, false);
            return 1;
        }
    }

    if (!keyDown)
        return 1;

    if (screen == FrontendScreen::InGame)
    {
        NotifyGameplayVirtualKeyState(virtualKey, true);
        DispatchCapturedVirtualKey(screen, virtualKey);
        return 1;
    }

    if (!PostCapturedVirtualKey(screen, virtualKey))
        LogLine(L"Low-level captured key dropped: failed to post frontend message.");
    return 1;
}

void KeyboardHookThreadMain()
{
    g_keyboardHookThreadId = GetCurrentThreadId();
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc,
                                       reinterpret_cast<HMODULE>(&__ImageBase), 0);
    if (g_keyboardHook == nullptr)
    {
        LogLine(L"Failed to install keyboard hook.");
        return;
    }
    LogLine(L"Keyboard hook installed.");

    MSG msg = {};
    while (g_keyboardHookRunning.load() && GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_keyboardHook != nullptr)
    {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
}

int __cdecl Hook_FE_MainMenu_Show()
{
    int result = g_realMainMenuShow();
    OnGameLeft();
    g_currentScreen = FrontendScreen::MainMenu;
    g_lastSpokenClass = -1;
    g_lastSpokenCharacterSelectIndex = -1;
    g_createEnabled = false;
    g_mainMenuActionInProgress = false;
    g_mainMenuIndex = 0;
    {
        std::scoped_lock lock(g_keyDownMutex);
        g_keyDownState.reset();
    }
    {
        std::scoped_lock lock(g_postedKeyMutex);
        g_postedKeys.clear();
    }
    Speak(TrS(L"Menu główne. Strzałki w górę i w dół wybierają opcję, Enter zatwierdza. ",
              L"Main menu. Up and down arrows choose an option, Enter confirms. ") +
              MainMenuEntryText(0),
          true);
    LogLine(L"Frontend screen: MainMenu");
    return result;
}

int __cdecl Hook_FE_CharacterCreate_Show()
{
    int result = g_realCharacterCreateShow();
    OnGameLeft();
    g_currentScreen = FrontendScreen::CharacterCreate;
    g_lastSpokenCharacterSelectIndex = -1;
    g_accessibilityClassIndex = 0;
    g_lastSpokenClass = 0;
    g_mainMenuActionInProgress = false;
    g_createEnabled = IsCreateButtonEnabled();
    {
        std::scoped_lock lock(g_keyDownMutex);
        g_keyDownState.reset();
    }
    {
        std::scoped_lock lock(g_postedKeyMutex);
        g_postedKeys.clear();
    }
    ApplyNativeCharacterClassSelection(0);
    Speak(TrS(L"Tworzenie postaci. Obecna klasa: ", L"Character creation. Current class: ") + ClassDisplayName(0) +
              Tr(L". Strzałki zmieniają klasę, klawisze od 1 do 5 wybierają ją bezpośrednio. Wpisz imię. Enter "
                 L"tworzy postać.",
                 L". Arrows change class, keys 1 to 5 select a class directly. Type a name. Enter creates the "
                 L"character."),
          true);
    LogLine(L"Frontend screen: CharacterCreate");
    return result;
}

int __cdecl Hook_FE_CharacterSelect_Show()
{
    int result = g_realCharacterSelectShow();
    OnGameLeft();
    g_currentScreen = FrontendScreen::CharacterSelect;
    g_lastSpokenCharacterSelectIndex = -1;
    g_mainMenuActionInProgress = false;
    g_deleteConfirmPending = false;
    {
        std::scoped_lock lock(g_keyDownMutex);
        g_keyDownState.reset();
    }
    {
        std::scoped_lock lock(g_postedKeyMutex);
        g_postedKeys.clear();
    }
    SpeakSelectedCharacter(true);
    LogLine(L"Frontend screen: CharacterSelect");
    return result;
}

int __cdecl Hook_FE_CharacterSelect_Update()
{
    const int result = g_realCharacterSelectUpdate();
    if (g_currentScreen.load() == FrontendScreen::CharacterSelect)
        SpeakSelectedCharacter(false);
    return result;
}

int __stdcall Hook_FE_CharacterCreate_HandleClassSelection(void **buttonRef)
{
    int result = g_realHandleClassSelection(buttonRef);
    int nativeClass = NormalizeClassIndex(ReadAbsolute<int>(va::Global_SelectedClassIndex));
    if (nativeClass >= 0)
        g_accessibilityClassIndex = nativeClass;
    SpeakCurrentClassIfNeeded(true);
    return result;
}

int __cdecl Hook_FE_CharacterCreate_ValidateName()
{
    int result = g_realValidateName();
    bool enabled = IsCreateButtonEnabled();
    g_createEnabled = enabled;
    return result;
}

int __stdcall Hook_FE_CharacterCreate_OnCreateButton(int arg0)
{
    LogLine(std::wstring(L"CharacterCreate native create callback: arg=") +
            std::to_wstring(arg0));
    Speak(Tr(L"Tworzę postać.", L"Creating character."), true);
    g_currentScreen = FrontendScreen::Unknown;
    const int result = g_realOnCreateButton(arg0);
    LogLine(std::wstring(L"CharacterCreate native create callback returned: ") +
            std::to_wstring(result));
    return result;
}

int __cdecl Hook_FE_CharacterCreate_Commit()
{
    const int mode = ReadAbsolute<int>(va::Global_FrontendCreateMode);
    LogLine(std::wstring(L"CharacterCreate commit hook: mode=") + std::to_wstring(mode));
    g_currentScreen = FrontendScreen::Unknown;
    const int result = g_realCommit();
    const int frontendResult = ReadAbsolute<int>(va::Global_FrontendResult);
    LogLine(std::wstring(L"CharacterCreate commit hook returned: ") +
            std::to_wstring(result) + L", frontend result=" +
            std::to_wstring(frontendResult));
    return result;
}

bool InstallHook(uintptr_t va, void *detour, size_t patchSize, InlineHook &hook, void **trampolineOut)
{
    void *target = reinterpret_cast<void *>(AbsoluteAddress(va));
    if (!InstallInlineHook(target, detour, patchSize, hook))
        return false;

    *trampolineOut = hook.trampoline;
    return true;
}

} // namespace

bool InitializeFrontendHooks()
{
    InstallInputStateHooks();

    if (!InstallHook(va::FE_MainMenu_Show, reinterpret_cast<void *>(&Hook_FE_MainMenu_Show), 5,
                     g_mainMenuHook, reinterpret_cast<void **>(&g_realMainMenuShow)))
        return false;

    if (!InstallHook(va::FE_CharacterCreate_Show,
                     reinterpret_cast<void *>(&Hook_FE_CharacterCreate_Show), 6,
                     g_characterCreateShowHook,
                     reinterpret_cast<void **>(&g_realCharacterCreateShow)))
        return false;

    if (!InstallHook(va::FE_CharacterCreate_ValidateName,
                     reinterpret_cast<void *>(&Hook_FE_CharacterCreate_ValidateName), 6,
                     g_validateNameHook,
                     reinterpret_cast<void **>(&g_realValidateName)))
        return false;

    if (!InstallHook(va::FE_CharacterCreate_OnCreateButton,
                     reinterpret_cast<void *>(&Hook_FE_CharacterCreate_OnCreateButton), 6,
                     g_createButtonHook,
                     reinterpret_cast<void **>(&g_realOnCreateButton)))
        return false;

    if (!InstallHook(va::FE_CharacterCreate_Commit,
                     reinterpret_cast<void *>(&Hook_FE_CharacterCreate_Commit), 6,
                     g_commitHook,
                     reinterpret_cast<void **>(&g_realCommit)))
        return false;

    if (!InstallHook(va::FE_CharacterSelect_Show,
                     reinterpret_cast<void *>(&Hook_FE_CharacterSelect_Show), 5,
                     g_characterSelectShowHook,
                     reinterpret_cast<void **>(&g_realCharacterSelectShow)))
        return false;

    if (!InstallHook(va::FE_CharacterSelect_Update,
                     reinterpret_cast<void *>(&Hook_FE_CharacterSelect_Update), 6,
                     g_characterSelectUpdateHook,
                     reinterpret_cast<void **>(&g_realCharacterSelectUpdate)))
        return false;

    g_keyboardHookRunning = true;
    g_keyboardHookThread = std::thread(KeyboardHookThreadMain);
    LogLine(L"Frontend hooks installed.");
    return true;
}

void ShutdownFrontendHooks()
{
    g_keyboardHookRunning = false;
    if (g_keyboardHookThreadId != 0)
        PostThreadMessageW(g_keyboardHookThreadId, WM_QUIT, 0, 0);
    if (g_keyboardHookThread.joinable())
        g_keyboardHookThread.join();

    RemoveInlineHook(g_commitHook);
    RemoveInlineHook(g_characterSelectUpdateHook);
    RemoveInlineHook(g_characterSelectShowHook);
    RemoveInlineHook(g_createButtonHook);
    RemoveInlineHook(g_validateNameHook);
    RemoveInlineHook(g_classSelectionHook);
    RemoveInlineHook(g_characterCreateShowHook);
    RemoveInlineHook(g_mainMenuHook);
    RemoveInputStateHooks();
}

void NotifyFrontendGameplayReady()
{
    g_currentScreen = FrontendScreen::InGame;
    g_lastSpokenCharacterSelectIndex = -1;
    {
        std::scoped_lock lock(g_keyDownMutex);
        g_keyDownState.reset();
    }
    {
        std::scoped_lock lock(g_postedKeyMutex);
        g_postedKeys.clear();
    }
    LogLine(L"Frontend screen: InGame");
}

} // namespace d2access
