#include "GameplayHooks.hpp"

#include "Addresses.hpp"
#include "AudioCue.hpp"
#include "Controls.hpp"
#include "D2Game.hpp"
#include "EventLog.hpp"
#include "FrontendHooks.hpp"
#include "GameplayQueries.hpp"
#include "Hooking.hpp"
#include "LevelMap.hpp"
#include "Localization.hpp"
#include "Logging.hpp"
#include "MenuAccess.hpp"
#include "Panels.hpp"
#include "Pathfinder.hpp"
#include "QuestLog.hpp"
#include "ScreenReader.hpp"
#include "SkillTree.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <cwctype>
#include <deque>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace d2access {

namespace {

using game::Point;

// ---------------------------------------------------------------------------
// Tracker categories, mirroring Diablo Access (controls/tracker.cpp).
// ---------------------------------------------------------------------------

enum class Category : std::uint8_t {
    Items,
    Chests,
    Doors,
    Shrines,
    Objects,
    Breakables,
    Monsters,
    Npcs,
    Players,
    Exits,
    Waypoints,
    Portals,
    Count,
};

constexpr size_t CategoryCount = static_cast<size_t>(Category::Count);

constexpr std::array<Category, CategoryCount> AllCategories = {
    Category::Items,    Category::Chests,  Category::Doors,   Category::Shrines,
    Category::Objects,  Category::Breakables, Category::Monsters, Category::Npcs,
    Category::Players,  Category::Exits,   Category::Waypoints, Category::Portals,
};

const wchar_t *CategoryLabel(Category category)
{
    switch (category)
    {
    case Category::Items:
        return Tr(L"przedmioty", L"items");
    case Category::Chests:
        return Tr(L"skrzynie", L"chests");
    case Category::Doors:
        return Tr(L"drzwi", L"doors");
    case Category::Shrines:
        return Tr(L"kapliczki", L"shrines");
    case Category::Objects:
        return Tr(L"obiekty", L"objects");
    case Category::Breakables:
        return Tr(L"obiekty do zniszczenia", L"breakable objects");
    case Category::Monsters:
        return Tr(L"potwory", L"monsters");
    case Category::Npcs:
        return Tr(L"NPC", L"NPCs");
    case Category::Players:
        return Tr(L"gracze", L"players");
    case Category::Exits:
        return Tr(L"wyjścia", L"exits");
    case Category::Waypoints:
        return Tr(L"waypointy", L"waypoints");
    case Category::Portals:
        return Tr(L"portale", L"portals");
    case Category::Count:
        break;
    }
    return L"";
}

const wchar_t *NoTargetsMessage(Category category)
{
    switch (category)
    {
    case Category::Items:
        return Tr(L"Nie znaleziono przedmiotów.", L"No items found.");
    case Category::Chests:
        return Tr(L"Nie znaleziono skrzyń.", L"No chests found.");
    case Category::Doors:
        return Tr(L"Nie znaleziono drzwi.", L"No doors found.");
    case Category::Shrines:
        return Tr(L"Nie znaleziono kapliczek.", L"No shrines found.");
    case Category::Objects:
        return Tr(L"Nie znaleziono obiektów.", L"No objects found.");
    case Category::Breakables:
        return Tr(L"Nie znaleziono obiektów do zniszczenia.", L"No breakable objects found.");
    case Category::Monsters:
        return Tr(L"Nie znaleziono potworów.", L"No monsters found.");
    case Category::Npcs:
        return Tr(L"Nie znaleziono NPC.", L"No NPCs found.");
    case Category::Players:
        return Tr(L"Nie znaleziono graczy.", L"No players found.");
    case Category::Exits:
        return Tr(L"Nie znaleziono wyjść.", L"No exits found.");
    case Category::Waypoints:
        return Tr(L"Nie znaleziono waypointów.", L"No waypoints found.");
    case Category::Portals:
        return Tr(L"Nie znaleziono portali.", L"No portals found.");
    case Category::Count:
        break;
    }
    return Tr(L"Nie znaleziono celów.", L"No targets found.");
}

const wchar_t *InRangeMessage(Category category)
{
    switch (category)
    {
    case Category::Items:
        return Tr(L"Przedmiot w zasięgu.", L"Item in range.");
    case Category::Chests:
        return Tr(L"Skrzynia w zasięgu.", L"Chest in range.");
    case Category::Doors:
        return Tr(L"Drzwi w zasięgu.", L"Door in range.");
    case Category::Shrines:
        return Tr(L"Kapliczka w zasięgu.", L"Shrine in range.");
    case Category::Objects:
        return Tr(L"Obiekt w zasięgu.", L"Object in range.");
    case Category::Breakables:
        return Tr(L"Obiekt do zniszczenia w zasięgu.", L"Breakable object in range.");
    case Category::Monsters:
        return Tr(L"Potwór w zasięgu.", L"Monster in range.");
    case Category::Npcs:
        return Tr(L"NPC w zasięgu.", L"NPC in range.");
    case Category::Players:
        return Tr(L"Gracz w zasięgu.", L"Player in range.");
    case Category::Exits:
        return Tr(L"Wyjście w zasięgu.", L"Exit in range.");
    case Category::Waypoints:
        return Tr(L"Waypoint w zasięgu.", L"Waypoint in range.");
    case Category::Portals:
        return Tr(L"Portal w zasięgu.", L"Portal in range.");
    case Category::Count:
        break;
    }
    return Tr(L"Cel w zasięgu.", L"Target in range.");
}

CueId CueForCategory(Category category)
{
    switch (category)
    {
    case Category::Monsters:
        return CueId::Monster;
    case Category::Items:
        return CueId::Item;
    case Category::Chests:
        return CueId::Chest;
    case Category::Doors:
        return CueId::Door;
    case Category::Exits:
        return CueId::Stairs;
    default:
        return CueId::InteractionPossible;
    }
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------

constexpr bool RunWhenMoving = true;
constexpr DWORD64 AutoWalkTickMs = 50;
constexpr DWORD64 AutoWalkReplanMs = 1200;
constexpr DWORD64 AutoWalkMinResendMs = 150;
constexpr DWORD64 AutoWalkIdleResendMs = 350;
constexpr DWORD64 AutoWalkForcedResendMs = 1000;
constexpr DWORD64 AutoWalkStuckMs = 3000;
constexpr int AutoWalkMaxFailures = 4;
constexpr size_t AutoWalkLookAhead = 20;
// One arrow press moves exactly one step (one tile) and the Home path speech
// counts the same steps, like Diablo Access and Pokemon Access.
constexpr int ManualStepSubtiles = 5;
constexpr int ManualStepArriveDistance = 1;
constexpr DWORD64 ManualStepTimeoutMs = 900;
constexpr DWORD64 WallAnnounceIntervalMs = 700;
constexpr size_t ManualStepQueueLimit = 8;
constexpr DWORD64 LiveCollisionRefreshMs = 400;
constexpr DWORD64 CueTickMs = 300;
// Proximity audio from Diablo Access (utils/proximity_audio.cpp and
// engine/sound_defs.hpp). One Diablo tile is one step here (5 subtiles).
constexpr int CueTileSubtiles = 5;
constexpr int MaxCueDistanceTiles = 12;
constexpr int InteractCueDistance = 3;
constexpr size_t MaxCueEmitters = 3;
constexpr std::uint32_t MinCueIntervalMs = 250;
constexpr std::uint32_t MaxCueIntervalMs = 1000;
constexpr std::uint32_t MinMonsterCueIntervalMs = 100;
constexpr std::uint32_t MaxMonsterCueIntervalMs = 1000;
constexpr int CuePanMin = -6400;
constexpr int CuePanMax = 6400;
constexpr int CueAttenuationMin = -6400;
constexpr int ItemTypePotion = 9;
constexpr int ItemTypeScroll = 22;
constexpr int ItemTypeWeapon = 45;
constexpr int ItemTypeArmor = 50;
constexpr int UiInventoryPanel = 1;
constexpr int DropAnnounceDistance = 20;
constexpr int PresetLiveMatchDistance = 5;
constexpr const wchar_t *GameplayVersion = L"1.2 beta";

constexpr int NpcMenuOffset_SelectedIndex = 0x44;
constexpr int NpcMenuOffset_SelectableCount = 0x4C;
constexpr int NpcMenuOffset_ItemCount = 0x50;
constexpr int NpcMenuOffset_ItemText = 0x68;
constexpr int NpcMenuItemStride = 0x110;
constexpr int NpcMenuItemOffset_Selectable = 0x174;
// Dialogs (.\UI\dialog.cpp, created by 0x4A63A0) keep one action function per
// entry. Enter in the game (0x4A5E80) calls the function of the entry at +0x44
// and, when it returns 1, 0x440D00. The mod does the same, because the dialog
// itself only reacts to the mouse.
constexpr int NpcMenuItemOffset_Action = 0x170;
constexpr uintptr_t Game_DialogItemActivated = 0x440D00;
constexpr int NpcMenuMaxItems = 10;
constexpr int NpcMenuMaxTextChars = 119;
constexpr uintptr_t Global_NpcInteractionUnitClassId = 0x7B736D;

constexpr std::array<uintptr_t, 8> NpcDialogGlobals = {
    va::Global_NpcMenu, 0x7B73AB, 0x7B73A7, 0x7B73AF, 0x7B73B3, 0x7B73BB, 0x7B73A3, 0x7B73B7,
};

// ---------------------------------------------------------------------------
// Static game data
// ---------------------------------------------------------------------------

const wchar_t *LookupNpcName(std::uint32_t classId)
{
    switch (classId)
    {
    case 146:
    case 244:
    case 245:
    case 246:
    case 265:
    case 520:
        return L"Deckard Cain";
    case 147:
        return L"Gheed";
    case 148:
        return L"Akara";
    case 150:
        return L"Kashya";
    case 152:
        return Tr(L"Łotrzyca", L"Rogue");
    case 154:
        return L"Charsi";
    case 155:
    case 175:
        return L"Warriv";
    case 176:
        return L"Atma";
    case 177:
        return L"Drognan";
    case 178:
        return L"Fara";
    case 187:
        return L"Greiz";
    case 188:
        return L"Elzix";
    case 189:
        return L"Geglash";
    case 190:
        return L"Jerhyn";
    case 191:
        return L"Lysander";
    case 201:
        return L"Meshif";
    default:
        return nullptr;
    }
}

bool IsIgnoredCritter(std::uint32_t classId)
{
    return classId == 149 || classId == 151 || classId == 157 || classId == 158 ||
           classId == 159 || classId == 179;
}

bool IsTownLevel(int levelId)
{
    return levelId == 1 || levelId == 40 || levelId == 75 || levelId == 103 || levelId == 109;
}

const wchar_t *FallbackLevelName(int levelId)
{
    static constexpr std::array<const wchar_t *, 137> Names = {
        nullptr, L"Rogue Encampment", L"Blood Moor", L"Cold Plains", L"Stony Field",
        L"Dark Wood", L"Black Marsh", L"Tamoe Highland", L"Den of Evil", L"Cave level 1",
        L"Underground Passage level 1", L"Hole level 1", L"Pit level 1", L"Cave level 2",
        L"Underground Passage level 2", L"Hole level 2", L"Pit level 2", L"Burial Grounds",
        L"Crypt", L"Mausoleum", L"Forgotten Tower", L"Tower Cellar level 1",
        L"Tower Cellar level 2", L"Tower Cellar level 3", L"Tower Cellar level 4",
        L"Tower Cellar level 5", L"Monastery Gate", L"Outer Cloister", L"Barracks",
        L"Jail level 1", L"Jail level 2", L"Jail level 3", L"Inner Cloister", L"Cathedral",
        L"Catacombs level 1", L"Catacombs level 2", L"Catacombs level 3", L"Catacombs level 4",
        L"Tristram", L"Moo Moo Farm", L"Lut Gholein", L"Rocky Waste", L"Dry Hills",
        L"Far Oasis", L"Lost City", L"Valley of Snakes", L"Canyon of the Magi",
        L"Sewers level 1", L"Sewers level 2", L"Sewers level 3", L"Harem level 1",
        L"Harem level 2", L"Palace Cellar level 1", L"Palace Cellar level 2",
        L"Palace Cellar level 3", L"Stony Tomb level 1", L"Halls of the Dead level 1",
        L"Halls of the Dead level 2", L"Claw Viper Temple level 1", L"Stony Tomb level 2",
        L"Halls of the Dead level 3", L"Claw Viper Temple level 2", L"Maggot Lair level 1",
        L"Maggot Lair level 2", L"Maggot Lair level 3", L"Ancient Tunnels", L"Tal Rasha's Tomb",
        L"Tal Rasha's Tomb", L"Tal Rasha's Tomb", L"Tal Rasha's Tomb", L"Tal Rasha's Tomb",
        L"Tal Rasha's Tomb", L"Tal Rasha's Tomb", L"Duriel's Lair", L"Arcane Sanctuary",
        L"Kurast Docks", L"Spider Forest", L"Great Marsh", L"Flayer Jungle", L"Lower Kurast",
        L"Kurast Bazaar", L"Upper Kurast", L"Kurast Causeway", L"Travincal", L"Spider Cave",
        L"Spider Cavern", L"Swampy Pit level 1", L"Swampy Pit level 2",
        L"Flayer Dungeon level 1", L"Flayer Dungeon level 2", L"Swampy Pit level 3",
        L"Flayer Dungeon level 3", L"Kurast Sewers level 1", L"Kurast Sewers level 2",
        L"Ruined Temple", L"Disused Fane", L"Forgotten Reliquary", L"Forgotten Temple",
        L"Ruined Fane", L"Disused Reliquary", L"Durance of Hate level 1",
        L"Durance of Hate level 2", L"Durance of Hate level 3", L"The Pandemonium Fortress",
        L"Outer Steppes", L"Plains of Despair", L"City of the Damned", L"River of Flame",
        L"Chaos Sanctuary", L"Harrogath", L"Bloody Foothills", L"Frigid Highlands",
        L"Arreat Plateau", L"Crystalline Passage", L"Frozen River", L"Glacial Trail",
        L"Drifter Cavern", L"Frozen Tundra", L"The Ancients' Way", L"Icy Cellar",
        L"Arreat Summit", L"Nihlathak's Temple", L"Halls of Anguish", L"Halls of Pain",
        L"Halls of Vaught", L"Abaddon", L"Pit of Acheron", L"Infernal Pit",
        L"Worldstone Keep level 1", L"Worldstone Keep level 2", L"Worldstone Keep level 3",
        L"Throne of Destruction", L"The Worldstone Chamber", L"Matron's Den",
        L"Forgotten Sands", L"Furnace of Pain", L"Uber Tristram",
    };
    if (levelId <= 0 || levelId >= static_cast<int>(Names.size()))
        return nullptr;
    return Names[static_cast<size_t>(levelId)];
}

// The Polish string tables prefix names with grammatical gender tags such as
// "[fs]" or "[ms]"; the game strips them before drawing, so do the same.
std::wstring StripGrammarTags(const std::wstring &text)
{
    std::wstring out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == L'[')
        {
            const size_t end = text.find(L']', i);
            if (end != std::wstring::npos && end - i >= 2 && end - i <= 4 &&
                std::all_of(text.begin() + static_cast<std::ptrdiff_t>(i + 1),
                            text.begin() + static_cast<std::ptrdiff_t>(end),
                            [](wchar_t ch) { return ch >= L'a' && ch <= L'z'; }))
            {
                i = end;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

std::wstring NormalizeUiText(std::wstring text)
{
    text = StripGrammarTags(text);
    std::wstring normalized;
    bool previousSpace = true;
    for (size_t i = 0; i < text.size(); ++i)
    {
        wchar_t ch = text[i];
        // D2 colour codes: "ÿc" followed by one character.
        if (ch == 0x00FF && i + 2 < text.size() && text[i + 1] == L'c')
        {
            i += 2;
            continue;
        }
        if (ch == L'\r' || ch == L'\n' || ch == L'\t' || ch == L' ')
        {
            if (!previousSpace)
                normalized.push_back(L' ');
            previousSpace = true;
            continue;
        }
        if (ch < 32)
            continue;
        normalized.push_back(ch);
        previousSpace = false;
    }
    if (!normalized.empty() && normalized.back() == L' ')
        normalized.pop_back();
    return normalized;
}

std::wstring LevelDisplayName(int levelId)
{
    static std::array<std::wstring, 256> cache;
    if (levelId <= 0 || levelId >= 256)
        return Tr(L"nieznany obszar", L"unknown area");

    std::wstring &cached = cache[static_cast<size_t>(levelId)];
    if (cached.empty())
    {
        cached = NormalizeUiText(game::LevelTxtName(levelId));
        if (cached.empty())
        {
            const wchar_t *fallback = FallbackLevelName(levelId);
            cached = fallback != nullptr ? fallback : TrS(L"obszar ", L"area ") + std::to_wstring(levelId);
        }
    }
    return cached;
}

bool ContainsInsensitive(const std::wstring &text, const wchar_t *needle)
{
    std::wstring lower = text;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return lower.find(needle) != std::wstring::npos;
}

// ---------------------------------------------------------------------------
// Targets
// ---------------------------------------------------------------------------

enum class TargetSource : std::uint8_t {
    Unit,
    Preset,
    Exit,
};

struct Target {
    TargetSource source = TargetSource::Unit;
    Category category = Category::Objects;
    std::uint64_t key = 0;
    uintptr_t unit = 0;
    std::uint32_t unitType = 0;
    std::uint32_t unitId = 0;
    std::uint32_t classId = 0;
    std::uint32_t mode = 0;
    Point position;
    Point beyond;
    int destinationLevel = 0;
    bool warpTile = false;
    int distance = 0;
    std::wstring name;
};

std::uint64_t UnitKey(std::uint32_t unitType, std::uint32_t unitId)
{
    return (std::uint64_t{unitType} << 32) | unitId;
}

std::uint64_t PresetKey(std::uint32_t unitType, std::uint32_t classId, Point position)
{
    return 0x8000000000000000ull | (std::uint64_t{unitType & 0x7u} << 60) |
           (std::uint64_t{classId & 0xFFFu} << 40) |
           (std::uint64_t{static_cast<std::uint32_t>(position.x) & 0xFFFFFu} << 20) |
           std::uint64_t{static_cast<std::uint32_t>(position.y) & 0xFFFFFu};
}

std::uint64_t ExitKey(int destinationLevel, Point position)
{
    return 0x4000000000000000ull |
           (std::uint64_t{static_cast<std::uint32_t>(destinationLevel) & 0xFFu} << 40) |
           (std::uint64_t{static_cast<std::uint32_t>(position.x) & 0xFFFFFu} << 20) |
           std::uint64_t{static_cast<std::uint32_t>(position.y) & 0xFFFFFu};
}

std::wstring ExitName(int destinationLevel)
{
    return TrS(L"Przejście do ", L"Passage to ") + LevelDisplayName(destinationLevel);
}

// Classifies an object by its objects.txt record.
std::optional<Category> ClassifyObject(std::uint32_t classId, std::uint32_t mode)
{
    const uintptr_t txt = game::ObjectTxt(classId);
    if (txt == 0)
        return std::nullopt;

    std::uint8_t subClass = 0;
    std::uint8_t isDoor = 0;
    std::uint8_t selectable = 0;
    std::uint8_t operateFn = 0;
    game::Read(txt + game::off::ObjectTxtSubClass, subClass);
    game::Read(txt + game::off::ObjectTxtIsDoor, isDoor);
    game::Read(txt + game::off::ObjectTxtSelectable + std::min<std::uint32_t>(mode, 7), selectable);
    game::Read(txt + 0x1B3, operateFn);

    // Only IsDoor marks real doors in 1.14b; SubClass 0x80 is also set on beds,
    // which open like caskets (operateFn 1).
    if (isDoor != 0)
        return Category::Doors;
    if ((subClass & game::ObjectSubClassWaypoint) != 0)
        return Category::Waypoints;
    if ((subClass & (game::ObjectSubClassTownPortal | game::ObjectSubClassPortal)) != 0)
        return Category::Portals;
    if (selectable == 0)
        return std::nullopt;
    if ((subClass & (game::ObjectSubClassShrine | game::ObjectSubClassWell)) != 0 || operateFn == 2 ||
        operateFn == 22)
        return mode == 0 ? std::optional<Category>(Category::Shrines) : std::nullopt;
    // Operate functions from D2MOO ObjMode.h: 3 urn/basket/jar, 5 barrel,
    // 7 exploding barrel, 68 evil urn.
    if (operateFn == 3 || operateFn == 5 || operateFn == 7 || operateFn == 68)
        return mode == 0 ? std::optional<Category>(Category::Breakables) : std::nullopt;
    // 1 casket, 4 chest, 14 corpse, 30 exploding chest, 51 jungle stash.
    if ((subClass & game::ObjectSubClassChest) != 0 || operateFn == 1 || operateFn == 4 || operateFn == 14 ||
        operateFn == 30 || operateFn == 51)
        return mode == 0 ? std::optional<Category>(Category::Chests) : std::nullopt;
    return Category::Objects;
}

std::wstring ObjectTxtName(std::uint32_t classId)
{
    const uintptr_t txt = game::ObjectTxt(classId);
    if (txt == 0)
        return {};
    std::wstring name = NormalizeUiText(game::ReadWideString(txt + 0x40, 64));
    if (name.empty())
        name = NormalizeUiText(game::ReadAnsiString(txt, 64));
    return name;
}

struct UsedObject {
    std::uint32_t classId = 0;
    Point position;
};

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

struct KeyEvent {
    DWORD virtualKey = 0;
    bool ctrl = false;
    bool shift = false;
};

struct TrackerState {
    Category category = Category::Items;
    int levelId = 0;
    std::array<std::uint64_t, CategoryCount> locked{};
};

struct AutoWalkState {
    bool active = false;
    std::uint64_t key = 0;
    Category category = Category::Items;
    TargetSource source = TargetSource::Unit;
    std::uint32_t classId = 0;
    Point lastKnownPosition;
    bool interactOnArrival = false;
    std::vector<Point> path;
    size_t index = 0;
    DWORD64 lastTick = 0;
    DWORD64 lastPlan = 0;
    DWORD64 lastSend = 0;
    DWORD64 lastProgress = 0;
    Point lastSent;
    bool sent = false;
    int bestDistance = INT_MAX;
    int failures = 0;
};

struct ManualMoveState {
    bool stepping = false;   // walking to `target`
    Point target;            // destination of the last step
    DWORD64 lastSend = 0;
    DWORD64 lastWall = 0;
    std::deque<Point> taps;  // single key presses still to walk (dx, dy)
};

struct PathPlan {
    bool found = false;
    bool doorBlocked = false;
    Point door;
    std::vector<Point> cells;
};

struct NpcMenuSnapshot {
    uintptr_t menu = 0;
    int mode = 0;
    int npcClassId = -1;
    int selectedIndex = -1;
    int itemCount = 0;
    std::wstring selectedLabel;
};

std::atomic<bool> g_gameActive = false;
std::array<std::atomic<bool>, 256> g_keyHeld{};
std::mutex g_keyMutex;
std::deque<KeyEvent> g_keyQueue;

InlineHook g_frameHook;
InlineHook g_setCurrentPlayerHook;
using GameFrameCallback_t = int(__stdcall *)(int);
using SetCurrentPlayerUnit_t = int(__thiscall *)(void *);
GameFrameCallback_t g_realGameFrame = nullptr;
SetCurrentPlayerUnit_t g_realSetCurrentPlayerUnit = nullptr;

// Everything below is only touched from the game thread.
LevelMap g_levelMap;
TrackerState g_tracker;
AutoWalkState g_autoWalk;
ManualMoveState g_manual;
std::vector<UsedObject> g_usedObjects;
std::unordered_set<std::uint32_t> g_knownGroundItems;
bool g_groundItemsSeeded = false;
DWORD64 g_lastLiveRefresh = 0;
DWORD64 g_lastCueTick = 0;
std::uint64_t g_lastInteractCueKey = 0;
uintptr_t g_lastNpcMenu = 0;
int g_lastNpcMenuSelected = -2;
std::wstring g_lastNpcMenuLabel;
bool g_dialogWasOpen = false;
bool g_tickExceptionLogged = false;
bool g_playerDead = false;
bool g_lifeChecked = false;

void Say(const std::wstring &text, bool interrupt = true)
{
    LogLine(L"Gameplay: " + text);
    Speak(text, interrupt);
}

// ---------------------------------------------------------------------------
// NPC menus and dialogs
// ---------------------------------------------------------------------------

const wchar_t *NpcMenuModeName(int mode)
{
    switch (mode)
    {
    case 1:
        return Tr(L"rozmowa", L"talk");
    case 2:
        return Tr(L"usługi", L"services");
    case 3:
        return Tr(L"handel", L"trade");
    case 4:
        return Tr(L"identyfikacja", L"identify");
    case 5:
        return Tr(L"najemnik", L"hire");
    default:
        return Tr(L"menu", L"menu");
    }
}

bool ReadGlobalInt(uintptr_t va, int &out)
{
    return game::Read(game::Absolute(va), out);
}

// Entries without an action function (headings) cannot be chosen.
bool NpcMenuItemSelectable(uintptr_t menu, int index)
{
    int value = 0;
    return game::Read(menu + static_cast<uintptr_t>(NpcMenuItemOffset_Selectable +
                                                    NpcMenuItemStride * index),
                      value) &&
           value != 0;
}

bool ReadNpcMenuSnapshotFromPointer(uintptr_t menu, NpcMenuSnapshot &snapshot)
{
    snapshot = NpcMenuSnapshot{};
    std::uint8_t probe = 0;
    if (menu == 0 || !game::Read(menu + 0xB0F, probe))
        return false;

    snapshot.menu = menu;
    if (!game::Read(menu + NpcMenuOffset_ItemCount, snapshot.itemCount) || snapshot.itemCount <= 0 ||
        snapshot.itemCount > NpcMenuMaxItems)
        return false;

    ReadGlobalInt(va::Global_NpcMenuMode, snapshot.mode);
    ReadGlobalInt(va::Global_NpcMenuUnitClassId, snapshot.npcClassId);
    if (snapshot.npcClassId <= 0)
        ReadGlobalInt(Global_NpcInteractionUnitClassId, snapshot.npcClassId);
    game::Read(menu + NpcMenuOffset_SelectedIndex, snapshot.selectedIndex);

    std::vector<std::wstring> labels;
    for (int index = 0; index < snapshot.itemCount; ++index)
    {
        const uintptr_t textAddress =
            menu + NpcMenuOffset_ItemText + static_cast<uintptr_t>(index * NpcMenuItemStride);
        labels.push_back(NormalizeUiText(game::ReadWideString(textAddress, NpcMenuMaxTextChars)));
    }

    const auto selectable = [menu](int index) { return NpcMenuItemSelectable(menu, index); };

    const auto validSelection = [&]() {
        return snapshot.selectedIndex >= 0 && snapshot.selectedIndex < snapshot.itemCount &&
               !labels[static_cast<size_t>(snapshot.selectedIndex)].empty();
    };

    if (!validSelection())
    {
        for (int index = 0; index < snapshot.itemCount; ++index)
        {
            if (selectable(index) && !labels[static_cast<size_t>(index)].empty())
            {
                snapshot.selectedIndex = index;
                break;
            }
        }
    }
    if (!validSelection())
    {
        for (int index = 0; index < snapshot.itemCount; ++index)
        {
            if (!labels[static_cast<size_t>(index)].empty())
            {
                snapshot.selectedIndex = index;
                break;
            }
        }
    }

    if (snapshot.selectedIndex >= 0 && snapshot.selectedIndex < snapshot.itemCount)
        snapshot.selectedLabel = labels[static_cast<size_t>(snapshot.selectedIndex)];
    if (snapshot.selectedLabel.empty())
        snapshot.selectedLabel = TrS(L"opcja ", L"option ") + std::to_wstring(snapshot.selectedIndex + 1);
    return true;
}

bool ReadNpcMenuSnapshot(NpcMenuSnapshot &snapshot)
{
    for (uintptr_t globalVa : NpcDialogGlobals)
    {
        const uintptr_t menu = game::ReadPtr(game::Absolute(globalVa));
        if (menu != 0 && ReadNpcMenuSnapshotFromPointer(menu, snapshot))
            return true;
    }
    snapshot = NpcMenuSnapshot{};
    return false;
}

std::atomic<bool> g_npcMenuOpenForKeys = false;

// Moves the highlight to the previous or next choosable entry. The next poll
// reads the new entry out.
bool MoveNpcMenuSelection(int delta)
{
    NpcMenuSnapshot menu;
    if (!ReadNpcMenuSnapshot(menu) || menu.itemCount <= 0)
        return false;

    int index = menu.selectedIndex;
    for (int step = 0; step < menu.itemCount; ++step)
    {
        index += delta;
        if (index < 0)
            index = menu.itemCount - 1;
        else if (index >= menu.itemCount)
            index = 0;
        if (NpcMenuItemSelectable(menu.menu, index))
            return game::Write(menu.menu + NpcMenuOffset_SelectedIndex, index);
    }
    return false;
}

using NpcMenuAction_t = int(__cdecl *)();

int CallNpcMenuAction(uintptr_t action)
{
    __try
    {
        return reinterpret_cast<NpcMenuAction_t>(action)();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

void CallDialogItemActivated()
{
    __try
    {
        reinterpret_cast<void(__cdecl *)()>(game::Absolute(Game_DialogItemActivated))();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
}

bool ActivateNpcMenuSelection()
{
    NpcMenuSnapshot menu;
    if (!ReadNpcMenuSnapshot(menu) || menu.selectedIndex < 0 || menu.selectedIndex >= menu.itemCount ||
        !NpcMenuItemSelectable(menu.menu, menu.selectedIndex))
        return false;

    const uintptr_t action = game::ReadPtr(menu.menu + NpcMenuItemOffset_Action +
                                           static_cast<uintptr_t>(NpcMenuItemStride * menu.selectedIndex));
    if (action == 0)
        return false;

    // The choice usually replaces or closes the menu; let the next poll announce
    // whatever comes up.
    g_lastNpcMenu = 0;
    g_lastNpcMenuSelected = -2;
    g_lastNpcMenuLabel.clear();
    if (CallNpcMenuAction(action) == 1)
        CallDialogItemActivated();
    return true;
}

// The NPC menu only reacts to the mouse, so the mod drives it from the keyboard.
bool HandleNpcMenuKey(DWORD virtualKey)
{
    if (!g_npcMenuOpenForKeys.load())
        return false;

    switch (virtualKey)
    {
    case VK_UP:
    case VK_NUMPAD8:
        return MoveNpcMenuSelection(-1);
    case VK_DOWN:
    case VK_NUMPAD2:
        return MoveNpcMenuSelection(1);
    case VK_RETURN:
        return ActivateNpcMenuSelection();
    default:
        return false;
    }
}

bool IsGameplayDialogActiveInternal()
{
    NpcMenuSnapshot menu;
    if (ReadNpcMenuSnapshot(menu))
        return true;
    int dialogActive = 0;
    return ReadGlobalInt(va::Global_DialogActive, dialogActive) && dialogActive != 0;
}

void PollNpcDialogAccessibility()
{
    NpcMenuSnapshot menu;
    if (ReadNpcMenuSnapshot(menu))
    {
        const bool opened = menu.menu != g_lastNpcMenu;
        if (opened || menu.selectedIndex != g_lastNpcMenuSelected || menu.selectedLabel != g_lastNpcMenuLabel)
        {
            std::wstringstream stream;
            if (opened)
            {
                stream << Tr(L"Menu NPC", L"NPC menu");
                if (const wchar_t *npcName = LookupNpcName(static_cast<std::uint32_t>(menu.npcClassId)))
                    stream << L": " << npcName;
                stream << Tr(L". Tryb ", L". Mode ") << NpcMenuModeName(menu.mode) << L". ";
            }
            else
            {
                stream << Tr(L"Opcja NPC. ", L"NPC option. ");
            }
            stream << menu.selectedLabel << L". " << (menu.selectedIndex + 1) << Tr(L" z ", L" of ")
                   << menu.itemCount << L".";
            if (opened)
                stream << Tr(L" Strzałki w górę i w dół zmieniają opcję, Enter wybiera, Escape zamyka.",
                             L" Up and down arrows change the option, Enter selects, Escape closes.");
            Say(stream.str());
        }
        g_npcMenuOpenForKeys = true;
        g_lastNpcMenu = menu.menu;
        g_lastNpcMenuSelected = menu.selectedIndex;
        g_lastNpcMenuLabel = menu.selectedLabel;
    }
    else
    {
        g_npcMenuOpenForKeys = false;
        if (g_lastNpcMenu != 0)
        {
            g_lastNpcMenu = 0;
            g_lastNpcMenuSelected = -2;
            g_lastNpcMenuLabel.clear();
        }
    }

    int dialogActive = 0;
    const bool dialogOpen = ReadGlobalInt(va::Global_DialogActive, dialogActive) && dialogActive != 0 &&
                            g_lastNpcMenu == 0;
    if (dialogOpen && !g_dialogWasOpen)
        Say(Tr(L"Okno dialogowe otwarte. Enter przechodzi dalej lub aktywuje wybór, Escape zamyka.",
               L"Dialog open. Enter continues or activates the choice, Escape closes."));
    g_dialogWasOpen = dialogOpen;
}

// ---------------------------------------------------------------------------
// World collection
// ---------------------------------------------------------------------------

bool IsPlayerIdle(const game::UnitInfo &player)
{
    return player.mode == game::PlayerModeNeutral || player.mode == game::PlayerModeTownNeutral;
}

// Client pet roster (RosterPets.cpp, head at 0x7B1BFC): records of 0x34 bytes
// with the pet unit id at +0x08 and the next record at +0x30. Mercenaries and
// summons are listed there and must not show up as monsters.
bool IsRosterPet(std::uint32_t unitId)
{
    constexpr uintptr_t PetRosterHead = 0x7B1BFC;
    uintptr_t record = game::ReadPtr(game::Absolute(PetRosterHead));
    for (int i = 0; i < 256 && record != 0; ++i)
    {
        std::uint32_t petId = 0;
        if (game::Read(record + 0x08, petId) && petId == unitId)
            return true;
        const uintptr_t next = game::ReadPtr(record + 0x30);
        if (next == record)
            break;
        record = next;
    }
    return false;
}

constexpr std::uint32_t ItemClassGold = 523;

std::wstring GroundItemName(uintptr_t unit, std::uint32_t classId)
{
    // The item name table abbreviates gold to "Złt.".
    if (classId == ItemClassGold)
        return TrS(L"Złoto", L"Gold");
    std::wstring name = NormalizeUiText(game::UnitName(unit));
    return name.empty() ? TrS(L"Przedmiot", L"Item") : name;
}

bool BuildUnitTarget(const game::UnitInfo &player, uintptr_t unit, bool withNamesForAll,
                     std::optional<Category> nameFilter, Target &target)
{
    game::UnitInfo info;
    if (!game::ReadUnitInfo(unit, info) || info.unit == player.unit)
        return false;

    target = Target{};
    target.source = TargetSource::Unit;
    target.unit = unit;
    target.unitType = info.type;
    target.unitId = info.unitId;
    target.classId = info.classId;
    target.mode = info.mode;
    target.position = info.position;
    target.beyond = info.position;
    target.distance = game::ChebyshevDistance(player.position, info.position);
    target.key = UnitKey(info.type, info.unitId);

    // UnitName calls into the game, so names are only built for the category
    // that is being listed.
    const auto wantName = [&](Category category) {
        return withNamesForAll && (!nameFilter || *nameFilter == category);
    };

    switch (static_cast<game::UnitType>(info.type))
    {
    case game::UnitType::Player:
    {
        // A dead player unit is a corpse; the equipment stays on it.
        const bool corpse = info.mode == game::PlayerModeDeath || info.mode == game::PlayerModeDead;
        target.category = Category::Players;
        if (wantName(target.category))
            target.name = corpse ? TrS(L"Zwłoki: ", L"Corpse: ") + game::PlayerName(unit) : game::PlayerName(unit);
        break;
    }
    case game::UnitType::Monster:
    {
        if (info.mode == 0 || info.mode == 12 || IsIgnoredCritter(info.classId) || IsRosterPet(info.unitId))
            return false;
        const wchar_t *npcName = LookupNpcName(info.classId);
        const bool npc = npcName != nullptr || IsTownLevel(g_levelMap.LevelId());
        target.category = npc ? Category::Npcs : Category::Monsters;
        if (wantName(target.category))
        {
            target.name = NormalizeUiText(game::UnitName(unit));
            // Town rogues carry the placeholder monstats name "not used - tell Ken".
            if (target.name.empty() || ContainsInsensitive(target.name, L"not used"))
                target.name = npcName != nullptr ? npcName : Tr(L"Potwór", L"Monster");
        }
        break;
    }
    case game::UnitType::Object:
    {
        const std::optional<Category> category = ClassifyObject(info.classId, info.mode);
        if (!category)
            return false;
        target.category = *category;
        if (wantName(target.category))
        {
            target.name = NormalizeUiText(game::UnitName(unit));
            if (target.name.empty())
                target.name = ObjectTxtName(info.classId);
            if (target.category == Category::Doors && !ContainsInsensitive(target.name, L"zamkn") &&
                !ContainsInsensitive(target.name, L"otwar") && !ContainsInsensitive(target.name, L"open") &&
                !ContainsInsensitive(target.name, L"close"))
                target.name += info.mode == 0 ? Tr(L", zamknięte", L", closed") : Tr(L", otwarte", L", open");
        }
        break;
    }
    case game::UnitType::Item:
        if (info.mode != 3 && info.mode != 5)
            return false;
        target.category = Category::Items;
        if (wantName(target.category))
            target.name = GroundItemName(unit, info.classId);
        break;
    case game::UnitType::Tile:
    {
        const int destination = game::WarpDestinationLevel(game::Room1Room2(info.room1), info.classId);
        if (destination <= 0 || destination == g_levelMap.LevelId())
            return false;
        target.category = Category::Exits;
        target.destinationLevel = destination;
        target.warpTile = true;
        if (wantName(target.category))
            target.name = ExitName(destination);
        break;
    }
    default:
        return false;
    }

    return true;
}

void CollectLiveTargets(const game::UnitInfo &player, bool withNames, std::vector<Target> &out,
                        std::optional<Category> nameFilter = std::nullopt)
{
    const uintptr_t act = game::ClientAct();
    const int levelId = g_levelMap.LevelId();
    std::unordered_set<uintptr_t> seen;

    uintptr_t room1 = game::ActFirstRoom1(act);
    for (int r = 0; r < 4096 && room1 != 0; ++r, room1 = game::Room1Next(room1))
    {
        if (levelId != 0 && game::Room1LevelId(room1) != levelId)
            continue;

        uintptr_t unit = game::Room1FirstUnit(room1);
        for (int i = 0; i < 1024 && unit != 0; ++i)
        {
            if (!seen.insert(unit).second)
                break;
            Target target;
            if (BuildUnitTarget(player, unit, withNames, nameFilter, target))
                out.push_back(std::move(target));
            unit = game::ReadPtr(unit + game::off::UnitRoomNext);
        }
    }

    // Remember opened chests and used shrines so their presets stay hidden once
    // the room unloads again.
    uintptr_t room = game::ActFirstRoom1(act);
    for (int r = 0; r < 4096 && room != 0; ++r, room = game::Room1Next(room))
    {
        if (levelId != 0 && game::Room1LevelId(room) != levelId)
            continue;
        uintptr_t unit = game::Room1FirstUnit(room);
        for (int i = 0; i < 1024 && unit != 0; ++i)
        {
            game::UnitInfo info;
            if (game::ReadUnitInfo(unit, info) && info.type == static_cast<std::uint32_t>(game::UnitType::Object) &&
                info.mode != 0)
            {
                const std::optional<Category> closedCategory = ClassifyObject(info.classId, 0);
                if (closedCategory && (*closedCategory == Category::Chests || *closedCategory == Category::Shrines ||
                                       *closedCategory == Category::Breakables))
                {
                    const bool known = std::any_of(g_usedObjects.begin(), g_usedObjects.end(),
                                                   [&info](const UsedObject &used) {
                                                       return used.classId == info.classId &&
                                                              used.position == info.position;
                                                   });
                    if (!known)
                        g_usedObjects.push_back(UsedObject{info.classId, info.position});
                }
            }
            const uintptr_t next = game::ReadPtr(unit + game::off::UnitRoomNext);
            if (next == unit)
                break;
            unit = next;
        }
    }
}

void CollectStaticTargets(const game::UnitInfo &player, bool withNames, const std::vector<Target> &live,
                          std::vector<Target> &out, std::optional<Category> filter)
{
    const auto liveObjectNear = [&live](std::uint32_t classId, Point position) {
        return std::any_of(live.begin(), live.end(), [&](const Target &t) {
            return t.unitType == static_cast<std::uint32_t>(game::UnitType::Object) && t.classId == classId &&
                   game::ChebyshevDistance(t.position, position) <= PresetLiveMatchDistance;
        });
    };
    const auto usedObjectNear = [](std::uint32_t classId, Point position) {
        return std::any_of(g_usedObjects.begin(), g_usedObjects.end(), [&](const UsedObject &used) {
            return used.classId == classId &&
                   game::ChebyshevDistance(used.position, position) <= PresetLiveMatchDistance;
        });
    };

    for (const LevelPreset &preset : g_levelMap.Presets())
    {
        if (preset.unitType != static_cast<std::uint32_t>(game::UnitType::Object))
            continue;
        const std::optional<Category> category = ClassifyObject(preset.classId, 0);
        if (!category || (filter && *filter != *category) || liveObjectNear(preset.classId, preset.position) ||
            usedObjectNear(preset.classId, preset.position))
            continue;

        Target target;
        target.source = TargetSource::Preset;
        target.category = *category;
        target.unitType = preset.unitType;
        target.classId = preset.classId;
        target.position = preset.position;
        target.beyond = preset.position;
        target.distance = game::ChebyshevDistance(player.position, preset.position);
        target.key = PresetKey(preset.unitType, preset.classId, preset.position);
        if (withNames)
        {
            target.name = ObjectTxtName(preset.classId);
            if (target.name.empty())
                target.name = Tr(L"Obiekt", L"Object");
        }
        out.push_back(std::move(target));
    }

    for (const LevelExit &exit : g_levelMap.Exits())
    {
        if (filter && *filter != Category::Exits)
            break;
        const bool coveredByLiveTile = std::any_of(live.begin(), live.end(), [&exit](const Target &t) {
            return t.category == Category::Exits && t.destinationLevel == exit.destinationLevel &&
                   game::ChebyshevDistance(t.position, exit.position) <= 15;
        });
        if (coveredByLiveTile)
            continue;

        Target target;
        target.source = TargetSource::Exit;
        target.category = Category::Exits;
        target.position = exit.position;
        target.beyond = exit.beyond;
        target.destinationLevel = exit.destinationLevel;
        target.warpTile = exit.isWarpTile;
        target.distance = game::ChebyshevDistance(player.position, exit.position);
        target.key = ExitKey(exit.destinationLevel, exit.position);
        if (withNames)
            target.name = ExitName(exit.destinationLevel);
        out.push_back(std::move(target));
    }
}

std::vector<Target> CollectAllTargets(const game::UnitInfo &player, bool withNames,
                                      std::optional<Category> filter = std::nullopt)
{
    std::vector<Target> targets;
    CollectLiveTargets(player, withNames, targets, filter);
    std::vector<Target> statics;
    CollectStaticTargets(player, withNames, targets, statics, filter);
    targets.insert(targets.end(), std::make_move_iterator(statics.begin()), std::make_move_iterator(statics.end()));
    return targets;
}

std::vector<Target> CollectCategory(const game::UnitInfo &player, Category category, bool withNames = true)
{
    std::vector<Target> all = CollectAllTargets(player, withNames, category);
    std::vector<Target> result;
    for (Target &target : all)
    {
        if (target.category == category)
            result.push_back(std::move(target));
    }
    std::sort(result.begin(), result.end(), [](const Target &a, const Target &b) {
        if (a.distance != b.distance)
            return a.distance < b.distance;
        return a.key < b.key;
    });
    return result;
}

std::wstring DecoratedName(const Target &target, const std::vector<Target> &candidates)
{
    int total = 0;
    int ordinal = 0;
    for (const Target &candidate : candidates)
    {
        if (candidate.name != target.name)
            continue;
        ++total;
        if (candidate.key == target.key)
            ordinal = total;
    }
    if (total <= 1 || ordinal == 0)
        return target.name;
    return target.name + L" " + std::to_wstring(ordinal);
}

// Resolves the locked target of a category, or locks the nearest one.
bool ResolveTrackerTarget(const game::UnitInfo &player, Category category, Target &target,
                          std::vector<Target> &candidates)
{
    candidates = CollectCategory(player, category);
    if (candidates.empty())
        return false;

    std::uint64_t &locked = g_tracker.locked[static_cast<size_t>(category)];
    const auto it = std::find_if(candidates.begin(), candidates.end(),
                                 [locked](const Target &t) { return t.key == locked; });
    target = it != candidates.end() ? *it : candidates.front();
    locked = target.key;
    return true;
}

// ---------------------------------------------------------------------------
// Path planning
// ---------------------------------------------------------------------------

int ArrivalRadius(const Target &target)
{
    switch (target.category)
    {
    case Category::Items:
    case Category::Portals:
        return 1;
    case Category::Npcs:
        return 3;
    case Category::Exits:
        return target.warpTile ? 2 : 1;
    default:
        return 2;
    }
}

void RefreshLiveCollision(bool force)
{
    const DWORD64 now = GetTickCount64();
    if (!force && now - g_lastLiveRefresh < LiveCollisionRefreshMs)
        return;
    g_lastLiveRefresh = now;
    g_levelMap.RefreshLiveCollision(game::ClientAct());
}

// keyboardStyle: prefer a north/south/east/west path for speech (Diablo Access).
// Auto-walk only needs any path, so it skips that extra search.
PathPlan PlanPath(Point start, const Target &target, bool keyboardStyle)
{
    PathPlan plan;
    PathOptions options;
    options.goalRadius = ArrivalRadius(target);

    const auto search = [&](const PathOptions &searchOptions) {
        return keyboardStyle ? FindKeyboardPath(g_levelMap, start, target.position, searchOptions)
                             : FindPath(g_levelMap, start, target.position, searchOptions);
    };

    PathResult result = search(options);
    if (result.found)
    {
        plan.found = true;
        plan.cells = std::move(result.cells);
        return plan;
    }

    // Like Diablo Access: look again through closed doors and stop in front of
    // the first one. Falls back to the closest reachable cell.
    PathOptions ignoreDoors = options;
    ignoreDoors.blockMask = static_cast<std::uint16_t>(options.blockMask & ~game::CollideDoor);
    ignoreDoors.closestIfUnreachable = true;
    result = search(ignoreDoors);
    for (size_t i = 0; i < result.cells.size(); ++i)
    {
        const Point cell = result.cells[i];
        if ((g_levelMap.MaskAt(cell.x, cell.y) & game::CollideDoor) != 0)
        {
            plan.doorBlocked = true;
            plan.door = cell;
            result.cells.resize(i > 2 ? i - 2 : 0);
            break;
        }
    }
    plan.found = result.found;
    plan.cells = std::move(result.cells);
    return plan;
}

// ---------------------------------------------------------------------------
// Auto-walk (Diablo Access UpdateAutoWalkTracker)
// ---------------------------------------------------------------------------

void StopAutoWalk()
{
    g_autoWalk = AutoWalkState{};
}

bool FindAutoWalkTarget(const game::UnitInfo &player, Target &target)
{
    const std::vector<Target> candidates = CollectCategory(player, g_autoWalk.category, false);
    const auto it = std::find_if(candidates.begin(), candidates.end(),
                                 [](const Target &t) { return t.key == g_autoWalk.key; });
    if (it != candidates.end())
    {
        target = *it;
        return true;
    }

    // A preset becomes a live unit once its room loads, and a map exit may be
    // replaced by the live warp tile. Follow the target across that change.
    if (g_autoWalk.source != TargetSource::Unit)
    {
        for (const Target &candidate : candidates)
        {
            const bool sameThing = g_autoWalk.source == TargetSource::Exit
                                       ? candidate.category == Category::Exits
                                       : candidate.classId == g_autoWalk.classId;
            if (sameThing &&
                game::ChebyshevDistance(candidate.position, g_autoWalk.lastKnownPosition) <= PresetLiveMatchDistance * 3)
            {
                target = candidate;
                g_autoWalk.key = candidate.key;
                g_autoWalk.source = candidate.source;
                return true;
            }
        }
    }
    return false;
}

void InteractWithTarget(const game::UnitInfo &player, const Target &target, bool allowWalk);

void UpdateAutoWalk(const game::UnitInfo &player)
{
    if (!g_autoWalk.active)
        return;

    const DWORD64 now = GetTickCount64();
    if (now - g_autoWalk.lastTick < AutoWalkTickMs)
        return;
    g_autoWalk.lastTick = now;

    if (IsGameplayDialogActiveInternal() || player.mode == game::PlayerModeDeath ||
        player.mode == game::PlayerModeDead)
    {
        StopAutoWalk();
        return;
    }

    Target target;
    if (!FindAutoWalkTarget(player, target))
    {
        StopAutoWalk();
        Say(Tr(L"Cel zniknął.", L"The target is gone."));
        return;
    }
    g_autoWalk.lastKnownPosition = target.position;

    const int distance = game::ChebyshevDistance(player.position, target.position);
    if (distance <= ArrivalRadius(target))
    {
        // Stepping onto stairs changes the level in Diablo; in Diablo II the warp
        // tile has to be used, so auto-walk does that on arrival.
        const bool interact = g_autoWalk.interactOnArrival || (target.category == Category::Exits && target.warpTile);
        StopAutoWalk();
        if (target.category == Category::Exits && !target.warpTile)
        {
            game::SendMoveTo(target.beyond, RunWhenMoving);
            Say(InRangeMessage(target.category));
            return;
        }
        PlayCue(CueForCategory(target.category));
        Say(InRangeMessage(target.category));
        if (interact)
            InteractWithTarget(player, target, false);
        return;
    }

    if (distance < g_autoWalk.bestDistance)
    {
        g_autoWalk.bestDistance = distance;
        g_autoWalk.lastProgress = now;
    }
    else if (now - g_autoWalk.lastProgress > AutoWalkStuckMs)
    {
        g_autoWalk.lastProgress = now;
        g_autoWalk.path.clear();
        if (++g_autoWalk.failures >= AutoWalkMaxFailures)
        {
            StopAutoWalk();
            Say(Tr(L"Nie mogę dojść do celu.", L"Cannot reach the target."));
            return;
        }
        LogLine(L"Auto walk: no progress, replanning.");
    }

    const auto nearestPathCell = [&player](const std::vector<Point> &path, size_t from, int &nearestDistance) {
        size_t nearest = from;
        nearestDistance = INT_MAX;
        const size_t window = std::min(path.size(), from + 40);
        for (size_t i = from; i < window; ++i)
        {
            const int d = game::ChebyshevDistance(player.position, path[i]);
            if (d < nearestDistance)
            {
                nearestDistance = d;
                nearest = i;
            }
        }
        return nearest;
    };

    int offPathDistance = 0;
    const bool offPath = !g_autoWalk.path.empty() &&
                         (nearestPathCell(g_autoWalk.path, g_autoWalk.index, offPathDistance), offPathDistance > 6);
    if (g_autoWalk.path.empty() || offPath || now - g_autoWalk.lastPlan >= AutoWalkReplanMs)
    {
        RefreshLiveCollision(false);
        PathPlan plan = PlanPath(player.position, target, false);
        g_autoWalk.lastPlan = now;

        if (plan.doorBlocked && game::ChebyshevDistance(player.position, plan.door) <= 3)
        {
            StopAutoWalk();
            Say(Tr(L"Drzwi blokują drogę. Otwórz je i spróbuj ponownie.", L"A door blocks the way. Open it and try again."));
            return;
        }
        if (plan.cells.empty())
        {
            StopAutoWalk();
            Say(g_levelMap.Ready()
                    ? Tr(L"Nie mogę znaleźć ścieżki do celu.", L"Cannot find a path to the target.")
                    : Tr(L"Nie mogę znaleźć ścieżki do celu. Mapa poziomu jest jeszcze wczytywana.",
                         L"Cannot find a path to the target. The level map is still loading."));
            return;
        }

        g_autoWalk.path = std::move(plan.cells);
        g_autoWalk.index = 0;
        std::wstringstream stream;
        stream << L"Auto walk plan. Player X " << player.position.x << L" Y " << player.position.y
               << L", target X " << target.position.x << L" Y " << target.position.y << L", steps "
               << g_autoWalk.path.size() << (plan.found ? L"" : L" (closest reachable)")
               << (plan.doorBlocked ? L" (door ahead)" : L"") << L".";
        LogLine(stream.str());
        PushEvent("autowalk", stream.str());
    }

    std::vector<Point> &path = g_autoWalk.path;
    int nearestDistance = INT_MAX;
    const size_t nearest = nearestPathCell(path, g_autoWalk.index, nearestDistance);
    if (nearestDistance <= 2)
        g_autoWalk.index = nearest + 1;

    if (g_autoWalk.index >= path.size())
    {
        // End of a partial path (door ahead or closest reachable cell): replan.
        path.clear();
        return;
    }

    const size_t waypointIndex = FurthestStraightIndex(g_levelMap, player.position, path, g_autoWalk.index,
                                                       AutoWalkLookAhead, game::CollideMaskPlayerPath);
    const Point waypoint = path[waypointIndex];
    const DWORD64 sinceSend = now - g_autoWalk.lastSend;
    const bool reachedLastWaypoint =
        g_autoWalk.sent && game::ChebyshevDistance(player.position, g_autoWalk.lastSent) <= 3;
    // Re-sending the same destination restarts the server path, so only send a
    // new waypoint, or repeat one when the character stopped.
    const bool shouldSend = !g_autoWalk.sent || (waypoint != g_autoWalk.lastSent && reachedLastWaypoint) ||
                            (IsPlayerIdle(player) && sinceSend >= AutoWalkIdleResendMs) ||
                            (waypoint != g_autoWalk.lastSent && sinceSend >= AutoWalkForcedResendMs);

    if (shouldSend && sinceSend >= AutoWalkMinResendMs && game::SendMoveTo(waypoint, RunWhenMoving))
    {
        g_autoWalk.sent = true;
        g_autoWalk.lastSent = waypoint;
        g_autoWalk.lastSend = now;
    }
}

void StartAutoWalkTo(const game::UnitInfo &player, const Target &target, bool interactOnArrival)
{
    StopAutoWalk();
    g_autoWalk.active = true;
    g_autoWalk.key = target.key;
    g_autoWalk.category = target.category;
    g_autoWalk.source = target.source;
    g_autoWalk.classId = target.classId;
    g_autoWalk.lastKnownPosition = target.position;
    g_autoWalk.interactOnArrival = interactOnArrival;
    g_autoWalk.lastProgress = GetTickCount64();
    g_autoWalk.bestDistance = game::ChebyshevDistance(player.position, target.position);
    RefreshLiveCollision(true);
    UpdateAutoWalk(player);
}

void AutoWalkKeyPressed(const game::UnitInfo &player)
{
    if (g_autoWalk.active)
    {
        StopAutoWalk();
        Say(Tr(L"Marsz anulowany.", L"Walk cancelled."));
        return;
    }

    Target target;
    std::vector<Target> candidates;
    if (!ResolveTrackerTarget(player, g_tracker.category, target, candidates))
    {
        Say(NoTargetsMessage(g_tracker.category));
        return;
    }

    Say(TrS(L"Idę do: ", L"Walking to: ") + DecoratedName(target, candidates));
    StartAutoWalkTo(player, target, false);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------

void InteractWithTarget(const game::UnitInfo &player, const Target &target, bool allowWalk)
{
    const int distance = game::ChebyshevDistance(player.position, target.position);

    if (target.source != TargetSource::Unit)
    {
        if (target.category == Category::Exits && !target.warpTile)
        {
            if (allowWalk)
                StartAutoWalkTo(player, target, false);
            return;
        }
        // Presets and map exits have no unit yet: walk there and interact with
        // the unit that loads on arrival.
        std::vector<Target> live;
        CollectLiveTargets(player, false, live);
        const auto it = std::find_if(live.begin(), live.end(), [&target](const Target &t) {
            const bool same = target.category == Category::Exits ? t.category == Category::Exits
                                                                 : t.classId == target.classId;
            return same && game::ChebyshevDistance(t.position, target.position) <= PresetLiveMatchDistance * 3;
        });
        if (it != live.end())
        {
            InteractWithTarget(player, *it, allowWalk);
            return;
        }
        if (allowWalk)
            StartAutoWalkTo(player, target, true);
        return;
    }

    switch (target.category)
    {
    case Category::Items:
        if (distance <= 2)
            game::SendPickupItem(target.unitId);
        else if (allowWalk)
            StartAutoWalkTo(player, target, true);
        return;
    case Category::Monsters:
        game::SendUnitPacket(0x06, target.unitType, target.unitId);
        return;
    case Category::Players:
        if (target.mode == game::PlayerModeDeath || target.mode == game::PlayerModeDead)
        {
            // Clicking a corpse (packet 0x13) takes the equipment back.
            if (distance <= 3)
                game::SendUnitPacket(0x13, target.unitType, target.unitId);
            else if (allowWalk)
                StartAutoWalkTo(player, target, true);
            return;
        }
        if (allowWalk)
            StartAutoWalkTo(player, target, false);
        return;
    default:
        game::SendUnitPacket(0x13, target.unitType, target.unitId);
        return;
    }
}

void InteractKeyPressed(const game::UnitInfo &player)
{
    StopAutoWalk();

    Target target;
    std::vector<Target> candidates;
    bool found = ResolveTrackerTarget(player, g_tracker.category, target, candidates) &&
                 g_tracker.category != Category::Monsters;
    if (!found)
    {
        std::vector<Target> all = CollectAllTargets(player, true);
        int bestDistance = INT_MAX;
        for (Target &candidate : all)
        {
            if (candidate.category == Category::Monsters || candidate.category == Category::Players)
                continue;
            if (candidate.distance < bestDistance)
            {
                bestDistance = candidate.distance;
                target = candidate;
                found = true;
            }
        }
    }

    if (!found)
    {
        Say(Tr(L"Brak celu do interakcji.", L"Nothing to interact with."));
        return;
    }

    PlayCue(CueForCategory(target.category));
    Say(TrS(L"Interakcja: ", L"Interacting: ") + target.name);
    InteractWithTarget(player, target, true);
}

// ---------------------------------------------------------------------------
// Combat skills
// ---------------------------------------------------------------------------

// The attack key sends a left click (packet 0x06), which the game answers with
// the skill in the left hand. So choosing a skill means putting it there, and
// the attack key then uses it.
std::vector<int> KnownSkillIds(uintptr_t playerUnit)
{
    std::vector<int> ids;
    std::uint32_t classId = 0;
    if (!game::Read(playerUnit + game::off::UnitClassId, classId))
        return ids;

    for (int id : game::ClassSkillIds(classId))
    {
        game::SkillDefinition skill;
        if (!game::ReadSkillDefinition(id, skill) || !skill.selectable)
            continue;
        if (game::PlayerSkillPoints(playerUnit, id) + game::SkillBonusLevels(playerUnit, skill) <= 0)
            continue;
        ids.push_back(id);
    }
    return ids;
}

std::wstring SkillLabel(uintptr_t playerUnit, int skillId)
{
    game::SkillDefinition skill;
    if (!game::ReadSkillDefinition(skillId, skill))
        return TrS(L"Umiejętność ", L"Skill ") + std::to_wstring(skillId);

    std::wstring name = panels::CleanText(game::StringById(skill.nameStringId));
    if (name.empty())
        name = TrS(L"Umiejętność ", L"Skill ") + std::to_wstring(skillId);
    const int level = game::PlayerSkillPoints(playerUnit, skillId) + game::SkillBonusLevels(playerUnit, skill);
    return name + TrS(L", poziom ", L", level ") + std::to_wstring(level);
}

void SelectCombatSkill(const game::UnitInfo &player, int delta)
{
    const std::vector<int> ids = KnownSkillIds(player.unit);
    if (ids.empty())
    {
        Say(Tr(L"Nie masz jeszcze umiejętności do wyboru. Dodaj punkt w drzewku pod klawiszem T.",
               L"You have no skills to choose yet. Spend a point in the skill tree under T."));
        return;
    }

    const int count = static_cast<int>(ids.size());
    const int current = game::SelectedSkillId(player.unit, true);
    const auto found = std::find(ids.begin(), ids.end(), current);
    int index = found != ids.end() ? static_cast<int>(std::distance(ids.begin(), found)) + delta
                                   : (delta < 0 ? count - 1 : 0);
    index = ((index % count) + count) % count;

    const int skillId = ids[static_cast<size_t>(index)];
    if (!game::SendSelectSkill(skillId, true))
    {
        Say(Tr(L"Nie można teraz zmienić umiejętności.", L"The skill cannot be changed now."));
        return;
    }

    PlayCue(CueId::Item);
    Say(SkillLabel(player.unit, skillId) + L". " + std::to_wstring(index + 1) + TrS(L" z ", L" of ") +
        std::to_wstring(count) + Tr(L". F używa tej umiejętności.", L". F uses this skill."));
}

void SelectNormalAttack(const game::UnitInfo &player)
{
    (void)player;
    game::SendSelectSkill(0, true);
    Say(Tr(L"Zwykły atak. F atakuje bronią.", L"Normal attack. F attacks with the weapon."));
}

void AttackKeyPressed(const game::UnitInfo &player)
{
    StopAutoWalk();

    Target target;
    std::vector<Target> candidates;
    if (!ResolveTrackerTarget(player, Category::Monsters, target, candidates))
    {
        Say(Tr(L"Nie widzę potwora do ataku.", L"No monster to attack."));
        return;
    }
    if (g_tracker.category != Category::Monsters)
        target = candidates.front();

    PlayCue(CueId::Monster);
    game::SendUnitPacket(0x06, target.unitType, target.unitId);
    Say(TrS(L"Atakuję: ", L"Attacking: ") + DecoratedName(target, candidates));
}

// ---------------------------------------------------------------------------
// Tracker keys (Diablo Access TrackerPageUp/PageDown/Home)
// ---------------------------------------------------------------------------

void SelectTrackerTargetRelative(const game::UnitInfo &player, int delta)
{
    StopAutoWalk();
    const Category category = g_tracker.category;
    const std::vector<Target> candidates = CollectCategory(player, category);
    std::uint64_t &locked = g_tracker.locked[static_cast<size_t>(category)];

    if (candidates.empty())
    {
        locked = 0;
        Say(NoTargetsMessage(category));
        return;
    }

    if (candidates.size() == 1)
    {
        locked = candidates.front().key;
        Say(candidates.front().name);
        return;
    }

    const auto it = std::find_if(candidates.begin(), candidates.end(),
                                 [locked](const Target &t) { return t.key == locked; });
    size_t index = 0;
    if (it == candidates.end())
    {
        index = delta > 0 ? 0 : candidates.size() - 1;
    }
    else
    {
        const size_t current = static_cast<size_t>(it - candidates.begin());
        index = delta > 0 ? (current + 1) % candidates.size()
                          : (current + candidates.size() - 1) % candidates.size();
    }

    const Target &target = candidates[index];
    locked = target.key;
    Say(DecoratedName(target, candidates));

    std::wstringstream stream;
    stream << L"Selected " << target.name << L" key 0x" << std::hex << target.key << std::dec << L" at X "
           << target.position.x << L" Y " << target.position.y << L", distance " << target.distance << L".";
    LogLine(stream.str());
}

void SelectTrackerCategoryRelative(const game::UnitInfo &player, int delta)
{
    StopAutoWalk();
    const auto it = std::find(AllCategories.begin(), AllCategories.end(), g_tracker.category);
    const int count = static_cast<int>(AllCategories.size());
    int index = it == AllCategories.end() ? 0 : static_cast<int>(it - AllCategories.begin());
    index = ((index + delta) % count + count) % count;
    g_tracker.category = AllCategories[static_cast<size_t>(index)];

    std::wstring message = CategoryLabel(g_tracker.category);
    if (CollectCategory(player, g_tracker.category, false).empty())
        message += std::wstring(L". ") + NoTargetsMessage(g_tracker.category);
    Say(message);
}

// Where the next arrow press starts: the end of the step in progress, so the
// spoken steps stay on the same grid as the key presses.
Point ManualStepOrigin(const game::UnitInfo &player)
{
    if (g_manual.lastSend != 0 && game::ChebyshevDistance(player.position, g_manual.target) <= ManualStepSubtiles)
        return g_manual.target;
    return player.position;
}

void NavigateKeyPressed(const game::UnitInfo &player, bool clearTarget)
{
    StopAutoWalk();
    if (clearTarget)
    {
        g_tracker.locked[static_cast<size_t>(g_tracker.category)] = 0;
        Say(Tr(L"Wyczyszczono cel.", L"Target cleared."));
        return;
    }

    Target target;
    std::vector<Target> candidates;
    if (!ResolveTrackerTarget(player, g_tracker.category, target, candidates))
    {
        Say(NoTargetsMessage(g_tracker.category));
        return;
    }

    RefreshLiveCollision(true);
    // Count in arrow presses from where the next press starts.
    const Point origin = ManualStepOrigin(player);
    const int reach = std::max(ArrivalRadius(target), ManualStepSubtiles - 1);
    const StepPathResult path =
        FindStepPath(g_levelMap, origin, target.position, ManualStepSubtiles, reach, game::CollideMaskPlayerPath);

    std::wstring message = DecoratedName(target, candidates) + L". ";
    if (path.found)
    {
        message += DescribePath(origin, path.nodes) + L".";
        if (path.throughDoor)
            message += Tr(L" Po drodze są zamknięte drzwi, otwórz je klawiszem E.",
                          L" A closed door is on the way, open it with E.");
    }
    else
    {
        message += Tr(L"Nie mogę znaleźć drogi do celu.", L"Cannot find a path to the target.");
        if (!g_levelMap.Ready())
            message += Tr(L" Mapa poziomu jest jeszcze wczytywana.", L" The level map is still loading.");
        if (!path.nodes.empty())
            message += TrS(L" Najbliżej celu dojdziesz tak: ", L" The closest you can get: ") +
                       DescribePath(origin, path.nodes) + L".";
    }
    Say(message);
}

// ---------------------------------------------------------------------------
// Other speech keys
// ---------------------------------------------------------------------------

void SpeakHelp()
{
    Say(Tr(L"Sterowanie jak w Diablo Access. Strzałki ruszają postać, dwie strzałki naraz to ruch po skosie. "
           L"Page Down i Page Up wybierają następny i poprzedni cel. Control plus Page Down lub Page Up zmienia kategorię. "
           L"Home czyta drogę do celu. Shift plus Home idzie automatycznie do celu, ponowne naciśnięcie zatrzymuje. "
           L"Control plus Home czyści wybrany cel. E wykonuje interakcję z celem, F atakuje potwora. "
           L"S wybiera następną umiejętność bojową, Shift plus S poprzednią, Control plus S wraca do zwykłego "
           L"ataku. Wybrana umiejętność trafia na lewą rękę, więc F atakuje właśnie nią. "
           L"Z czyta procent życia, Shift plus Z procent many, X procent doświadczenia do następnego poziomu. "
           L"C otwiera kartę postaci, I ekwipunek. W panelu strzałki wybierają pole, Enter podnosi lub odkłada "
           L"przedmiot albo dodaje punkt atrybutu, Shift plus Enter używa przedmiotu, broń i zbroję od razu "
           L"zakłada, albo rozdaje wszystkie punkty, "
           L"Spacja czyta opis przedmiotu. Aby założyć przedmiot, podnieś go Enterem, przejdź strzałką w górę do "
           L"założonych przedmiotów, wybierz miejsce i naciśnij Enter. "
           L"Tab przełącza między kartą a ekwipunkiem. "
           L"T otwiera drzewko umiejętności: strzałki w górę i w dół przechodzą po drzewku, w prawo rozwija zakładkę, "
           L"w lewo zwija, Enter dodaje punkt umiejętności, Spacja czyta opis. "
           L"Q otwiera dziennik zadań: strzałki w górę i w dół wybierają zadanie, w lewo i w prawo zmieniają akt, "
           L"Spacja czyta zadanie ponownie, Q zamyka dziennik. "
           L"W rozmowie z postacią niezależną strzałki w górę i w dół wybierają odpowiedź, Enter ją zatwierdza, "
           L"Escape zamyka rozmowę. "
           L"Escape otwiera menu gry: strzałki wybierają, Enter zatwierdza, w lewo i w prawo zmieniają ustawienie. "
           L"W konfiguracji sterowania strzałki w górę i w dół wybierają akcję, w lewo i w prawo klawisz główny "
           L"albo zapasowy, Enter przypisuje nowy klawisz, Tab przechodzi do przycisków. "
           L"G albo F2 skanuje poziom. K czyta współrzędne, L czyta lokację, F1 pomoc.",
           L"Controls follow Diablo Access. Arrows move the character, two arrows together move diagonally. "
           L"Page Down and Page Up select the next and previous target. Control plus Page Down or Page Up changes "
           L"the category. Home reads the path to the target. Shift plus Home walks to the target, press again to "
           L"stop. Control plus Home clears the target. E interacts with the target, F attacks a monster. "
           L"S chooses the next combat skill, Shift plus S the previous one, Control plus S goes back to the "
           L"normal attack. The chosen skill goes into the left hand, so F attacks with it. "
           L"Z reads life percentage, Shift plus Z mana percentage, X experience missing to the next level. "
           L"C opens the character sheet, I the inventory. In a panel arrows select a field, Enter picks up or "
           L"places an item or adds a stat point, Shift plus Enter uses an item, equips weapons and armor, or "
           L"spends all points, "
           L"Space reads the item description. To equip an item, pick it up with Enter, go up to the equipped "
           L"items, choose the slot and press Enter. "
           L"Tab switches between the sheet and the inventory. "
           L"T opens the skill tree: up and down arrows move through the tree, right expands a tab, left collapses "
           L"it, Enter adds a skill point, Space reads the description. "
           L"Q opens the quest log: up and down arrows choose a quest, left and right change the act, "
           L"Space reads the quest again, Q closes the log. "
           L"In a conversation with an NPC up and down arrows choose the answer, Enter selects it, Escape closes "
           L"the conversation. "
           L"Escape opens the game menu: arrows select, Enter confirms, left and right change a setting. "
           L"In the controls configuration up and down arrows choose an action, left and right the primary or "
           L"secondary key, Enter assigns a new key, Tab moves to the buttons. "
           L"G or F2 scans the level. K reads coordinates, L reads the location, F1 help."));
}

void SpeakAreaScan(const game::UnitInfo &player)
{
    std::array<size_t, CategoryCount> counts{};
    for (const Target &target : CollectAllTargets(player, false))
        ++counts[static_cast<size_t>(target.category)];

    std::wstringstream stream;
    stream << TrS(L"Lokacja: ", L"Location: ") << LevelDisplayName(g_levelMap.LevelId()) << L".";
    for (Category category : AllCategories)
    {
        const size_t count = counts[static_cast<size_t>(category)];
        if (count > 0)
            stream << L" " << CategoryLabel(category) << L" " << count << L".";
    }
    if (!g_levelMap.Ready())
        stream << Tr(L" Mapa poziomu jest jeszcze wczytywana.", L" The level map is still loading.");
    Say(stream.str());
}

void SpeakCoordinates(const game::UnitInfo &player)
{
    std::wstringstream stream;
    stream << L"X " << player.position.x << L", Y " << player.position.y << Tr(L". Poziom ", L". Level ")
           << g_levelMap.LevelId() << L".";
    Say(stream.str());
}

void SpeakLocation()
{
    std::wstringstream stream;
    stream << TrS(L"Lokacja: ", L"Location: ") << LevelDisplayName(g_levelMap.LevelId()) << L".";
    if (!g_levelMap.Ready())
        stream << Tr(L" Mapa poziomu jest jeszcze wczytywana.", L" The level map is still loading.");
    Say(stream.str());
}

// ---------------------------------------------------------------------------
// Manual movement
// ---------------------------------------------------------------------------

bool KeyHeld(DWORD virtualKey)
{
    return virtualKey < g_keyHeld.size() && g_keyHeld[virtualKey].load();
}

void HeldDirection(int &dx, int &dy)
{
    dx = 0;
    dy = 0;
    if (KeyHeld(VK_RIGHT) || KeyHeld(VK_NUMPAD6) || KeyHeld(VK_NUMPAD9) || KeyHeld(VK_NUMPAD3))
        ++dx;
    if (KeyHeld(VK_LEFT) || KeyHeld(VK_NUMPAD4) || KeyHeld(VK_NUMPAD7) || KeyHeld(VK_NUMPAD1))
        --dx;
    if (KeyHeld(VK_DOWN) || KeyHeld(VK_NUMPAD2) || KeyHeld(VK_NUMPAD1) || KeyHeld(VK_NUMPAD3))
        ++dy;
    if (KeyHeld(VK_UP) || KeyHeld(VK_NUMPAD8) || KeyHeld(VK_NUMPAD7) || KeyHeld(VK_NUMPAD9))
        --dy;
}

bool KeyDirection(DWORD virtualKey, int &dx, int &dy)
{
    dx = 0;
    dy = 0;
    switch (virtualKey)
    {
    case VK_UP:
    case VK_NUMPAD8:
        dy = -1;
        return true;
    case VK_DOWN:
    case VK_NUMPAD2:
        dy = 1;
        return true;
    case VK_LEFT:
    case VK_NUMPAD4:
        dx = -1;
        return true;
    case VK_RIGHT:
    case VK_NUMPAD6:
        dx = 1;
        return true;
    case VK_NUMPAD7:
        dx = -1;
        dy = -1;
        return true;
    case VK_NUMPAD9:
        dx = 1;
        dy = -1;
        return true;
    case VK_NUMPAD1:
        dx = -1;
        dy = 1;
        return true;
    case VK_NUMPAD3:
        dx = 1;
        dy = 1;
        return true;
    default:
        return false;
    }
}

void UpdateManualMovement(const game::UnitInfo &player)
{
    if (panels::IsAnyPanelFocused() || skilltree::IsSkillTreeFocused() || gamemenu::IsGameMenuOpen())
    {
        g_manual = ManualMoveState{};
        return;
    }

    const DWORD64 now = GetTickCount64();
    if (g_manual.stepping)
    {
        // One step at a time: the next press or the held key continues once
        // the character reached the previous step.
        const bool arrived = game::ChebyshevDistance(player.position, g_manual.target) <= ManualStepArriveDistance;
        if (!arrived && now - g_manual.lastSend < ManualStepTimeoutMs)
            return;
        g_manual.stepping = false;
    }

    int dx = 0;
    int dy = 0;
    if (!g_manual.taps.empty())
    {
        dx = g_manual.taps.front().x;
        dy = g_manual.taps.front().y;
        g_manual.taps.pop_front();
    }
    else
    {
        HeldDirection(dx, dy);
    }
    if (dx == 0 && dy == 0)
        return;

    if (g_autoWalk.active)
        StopAutoWalk();
    RefreshLiveCollision(false);

    const Point origin = ManualStepOrigin(player);
    const Point destination{origin.x + dx * ManualStepSubtiles, origin.y + dy * ManualStepSubtiles};
    const bool unknown =
        !g_levelMap.HasCollision() || g_levelMap.MaskAt(destination.x, destination.y) == game::CollideUnknown;
    if (!unknown && !CanTakeStep(g_levelMap, origin, destination, game::CollideMaskPlayerPath))
    {
        // No sliding along walls: a blocked press does not move, so the step
        // count of the path description stays valid.
        g_manual.taps.clear();
        if (now - g_manual.lastWall >= WallAnnounceIntervalMs)
        {
            g_manual.lastWall = now;
            Say(Tr(L"Ściana.", L"Wall."));
        }
        return;
    }

    if (game::SendMoveTo(destination, RunWhenMoving))
    {
        g_manual.stepping = true;
        g_manual.target = destination;
        g_manual.lastSend = now;
    }
}

// ---------------------------------------------------------------------------
// Proximity cues
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Proximity audio (Diablo Access utils/proximity_audio.cpp)
// ---------------------------------------------------------------------------

std::uint32_t MakeEmitterId(std::uint32_t type, std::uint32_t id)
{
    return (type << 24) | (id & 0x00FFFFFF);
}

// DevilutionX Point::ApproxDistance.
int ApproxTileDistance(int dx, int dy)
{
    dx = std::abs(dx);
    dy = std::abs(dy);
    const int minimum = std::min(dx, dy);
    const int maximum = std::max(dx, dy);
    int approx = maximum * 1007 + minimum * 441;
    if (maximum < (minimum * 16))
        approx -= maximum * 40;
    return (approx + 512) / 1024;
}

int SubtilesToTiles(int subtiles)
{
    const int half = CueTileSubtiles / 2;
    return subtiles >= 0 ? (subtiles + half) / CueTileSubtiles : -((-subtiles + half) / CueTileSubtiles);
}

int CueTileDistance(Point from, Point to)
{
    return ApproxTileDistance(SubtilesToTiles(to.x - from.x), SubtilesToTiles(to.y - from.y));
}

std::uint32_t IntervalMsForDistance(int distance, int maxDistance, std::uint32_t minIntervalMs,
                                    std::uint32_t maxIntervalMs)
{
    if (maxDistance <= 0)
        return minIntervalMs;
    const float t = std::clamp(static_cast<float>(distance) / static_cast<float>(maxDistance), 0.0F, 1.0F);
    const float closeness = 1.0F - t;
    const float interval =
        static_cast<float>(maxIntervalMs) - closeness * static_cast<float>(maxIntervalMs - minIntervalMs);
    return static_cast<std::uint32_t>(std::lround(interval));
}

// DevilutionX CalculateSoundPosition: pan follows the screen x offset of the
// isometric grid, volume drops 64 per tile.
bool CueSoundPosition(Point player, Point sound, int &logVolume, int &logPan)
{
    const int dx = SubtilesToTiles(sound.x - player.x);
    const int dy = SubtilesToTiles(sound.y - player.y);
    logPan = std::clamp((dx - dy) * 256, CuePanMin, CuePanMax);
    const int volume = ApproxTileDistance(dx, dy) * -64;
    if (volume <= CueAttenuationMin)
        return false;
    logVolume = volume;
    return true;
}

std::optional<CueId> ItemCueSound(const Target &target)
{
    if (target.classId == ItemClassGold)
        return CueId::Gold;
    if (game::ItemIsType(target.unit, ItemTypeWeapon))
        return CueId::Weapon;
    if (game::ItemIsType(target.unit, ItemTypeArmor))
        return CueId::Armor;
    if (game::ItemIsType(target.unit, ItemTypePotion))
        return CueId::Potion;
    if (game::ItemIsType(target.unit, ItemTypeScroll))
        return CueId::Scroll;
    return std::nullopt;
}

std::wstring InteractCueName(const Target &target)
{
    if (target.category == Category::Items)
        return GroundItemName(target.unit, target.classId);
    std::wstring name = NormalizeUiText(game::UnitName(target.unit));
    if (name.empty())
        name = ObjectTxtName(target.classId);
    if (target.category == Category::Doors && !ContainsInsensitive(name, L"zamkn") &&
        !ContainsInsensitive(name, L"otwar") && !ContainsInsensitive(name, L"open") &&
        !ContainsInsensitive(name, L"close"))
        name += target.mode == 0 ? Tr(L", zamknięte", L", closed") : Tr(L", otwarte", L", open");
    return name;
}

// Items first, then chests and doors next to the character: speak the name and
// play the one-shot interaction cue once per target.
bool UpdateInteractCue(const game::UnitInfo &player, const std::vector<Target> &live)
{
    const Target *best = nullptr;
    for (int pass = 0; pass < 2 && best == nullptr; ++pass)
    {
        for (const Target &target : live)
        {
            const bool wanted = pass == 0 ? target.category == Category::Items
                                          : (target.category == Category::Chests || target.category == Category::Doors);
            if (!wanted || target.distance > InteractCueDistance)
                continue;
            if (best == nullptr || target.distance < best->distance)
                best = &target;
        }
    }

    if (best == nullptr)
    {
        g_lastInteractCueKey = 0;
        return false;
    }
    if (best->key == g_lastInteractCueKey)
        return false;
    g_lastInteractCueKey = best->key;

    const std::wstring name = InteractCueName(*best);
    if (!name.empty())
        Say(name);
    int logVolume = 0;
    int logPan = 0;
    if (CueSoundPosition(player.position, best->position, logVolume, logPan))
        PlayOneShot(CueId::InteractionPossible, logVolume, logPan, true);
    return true;
}

void UpdateProximityAudio(const game::UnitInfo &player, const std::vector<Target> &live)
{
    if (g_playerDead || gamemenu::IsGameMenuOpen())
        return;
    if (game::IsUiPanelOpen(UiInventoryPanel))
    {
        UpdateEmitters({});
        return;
    }
    if (UpdateInteractCue(player, live))
        return;

    struct Candidate {
        std::uint32_t emitterId;
        CueId sound;
        Point position;
        int distance;
        std::uint32_t intervalMs;
    };
    std::array<std::optional<Candidate>, MaxCueEmitters> best{};
    const auto consider = [&best](const Candidate &candidate) {
        for (size_t i = 0; i < best.size(); ++i)
        {
            if (!best[i] || candidate.distance < best[i]->distance ||
                (candidate.distance == best[i]->distance && candidate.emitterId < best[i]->emitterId))
            {
                for (size_t j = best.size() - 1; j > i; --j)
                    best[j] = best[j - 1];
                best[i] = candidate;
                return;
            }
        }
    };

    // Items everywhere; chests, doors, exits and monsters only outside town.
    const bool town = IsTownLevel(g_levelMap.LevelId());
    for (const Target &target : live)
    {
        std::optional<CueId> sound;
        std::uint32_t type = 0;
        bool monster = false;
        switch (target.category)
        {
        case Category::Items:
            sound = ItemCueSound(target);
            type = 1;
            break;
        case Category::Chests:
            if (!town)
                sound = CueId::Chest;
            type = 2;
            break;
        case Category::Doors:
            if (!town)
                sound = CueId::Door;
            type = 2;
            break;
        case Category::Monsters:
            if (!town)
                sound = CueId::Monster;
            type = 3;
            monster = true;
            break;
        case Category::Exits:
            if (!town)
                sound = CueId::Stairs;
            type = 4;
            break;
        default:
            break;
        }
        if (!sound)
            continue;

        const int distance = CueTileDistance(player.position, target.position);
        if (distance > MaxCueDistanceTiles)
            continue;
        const std::uint32_t interval =
            monster ? IntervalMsForDistance(distance, MaxCueDistanceTiles, MinMonsterCueIntervalMs, MaxMonsterCueIntervalMs)
                    : IntervalMsForDistance(distance, MaxCueDistanceTiles, MinCueIntervalMs, MaxCueIntervalMs);
        consider(Candidate{MakeEmitterId(type, target.unitId), *sound, target.position, distance, interval});
    }

    if (!town)
    {
        const std::vector<LevelExit> &exits = g_levelMap.Exits();
        for (size_t i = 0; i < exits.size(); ++i)
        {
            const LevelExit &exit = exits[i];
            const bool coveredByLiveTile = std::any_of(live.begin(), live.end(), [&exit](const Target &t) {
                return t.category == Category::Exits && t.destinationLevel == exit.destinationLevel &&
                       game::ChebyshevDistance(t.position, exit.position) <= 15;
            });
            if (coveredByLiveTile)
                continue;
            const int distance = CueTileDistance(player.position, exit.position);
            if (distance > MaxCueDistanceTiles)
                continue;
            consider(Candidate{MakeEmitterId(5, static_cast<std::uint32_t>(i)), CueId::Stairs, exit.position, distance,
                               IntervalMsForDistance(distance, MaxCueDistanceTiles, MinCueIntervalMs, MaxCueIntervalMs)});
        }
    }

    std::vector<EmitterRequest> requests;
    for (const std::optional<Candidate> &entry : best)
    {
        if (!entry)
            continue;
        EmitterRequest request;
        request.emitterId = entry->emitterId;
        request.sound = entry->sound;
        request.intervalMs = entry->intervalMs;
        if (!CueSoundPosition(player.position, entry->position, request.logVolume, request.logPan))
            continue;
        requests.push_back(request);
    }
    UpdateEmitters(requests);
}

void UpdateCues(const game::UnitInfo &player)
{
    std::vector<Target> live;
    CollectLiveTargets(player, false, live);

    const DWORD64 now = GetTickCount64();
    if (now - g_lastCueTick >= CueTickMs)
    {
        g_lastCueTick = now;

        // Announce loot that appears nearby (chests, barrels, monster drops) so it
        // is clear the items are on the ground and listed under "przedmioty".
        std::unordered_set<std::uint32_t> groundItems;
        std::wstring dropped;
        for (const Target &target : live)
        {
            if (target.category != Category::Items)
                continue;
            groundItems.insert(target.unitId);
            if (g_groundItemsSeeded && !g_knownGroundItems.contains(target.unitId) &&
                target.distance <= DropAnnounceDistance)
            {
                if (!dropped.empty())
                    dropped += L", ";
                dropped += GroundItemName(target.unit, target.classId);
            }
        }
        g_knownGroundItems = std::move(groundItems);
        g_groundItemsSeeded = true;
        if (!dropped.empty())
            Say(TrS(L"Na ziemi: ", L"On the ground: ") + dropped + L".", false);
    }

    UpdateProximityAudio(player, live);
}

// ---------------------------------------------------------------------------
// Game thread driver
// ---------------------------------------------------------------------------

// Death: D2 saves the character with 0 life at the moment of death, keeps the
// equipment on the corpse (which stays in town across reloads) and respawns the
// character after Escape. Only the next save stores the restored life.
void UpdateDeathState(const game::UnitInfo &player)
{
    const bool dead = player.mode == game::PlayerModeDeath || player.mode == game::PlayerModeDead;
    if (dead && !g_playerDead)
    {
        StopAutoWalk();
        PushEvent("death", L"player died");
        Say(Tr(L"Postać zginęła i gra zapisała ją z zerowym życiem. Założone przedmioty zostały przy zwłokach. "
               L"Naciśnij Escape, aby odrodzić się w mieście. Później wychodź przez menu gry, Zapisz i wyjdź, żeby "
               L"zapisało się pełne życie.",
               L"Your character died and the game saved it with zero life. The equipped items stay on the corpse. "
               L"Press Escape to respawn in town. Leave later through the game menu, Save and exit, so full life "
               L"is saved."));
    }
    else if (!dead && g_playerDead)
    {
        PushEvent("death", L"player respawned");
        Say(Tr(L"Postać odrodziła się. Zwłoki z ekwipunkiem są w kategorii gracze: wybierz je i naciśnij E, aby "
               L"odzyskać przedmioty.",
               L"Your character respawned. The corpse with your equipment is in the players category: select it and "
               L"press E to take the items back."));
    }
    g_playerDead = dead;

    if (!dead && !g_lifeChecked)
    {
        g_lifeChecked = true;
        const int maxLife = game::UnitStat(player.unit, game::stat::MaxHitPoints) >> 8;
        if (maxLife > 0 && (game::UnitStat(player.unit, game::stat::HitPoints) >> 8) <= 0)
        {
            Say(Tr(L"Uwaga: postać ma zero punktów życia i zginie od jednego ciosu. Porozmawiaj z uzdrowicielem w "
                   L"mieście, w pierwszym akcie to Akara z kategorii NPC, a przywróci pełne życie.",
                   L"Warning: your character has zero life and dies from a single hit. Talk to the healer in town, "
                   L"Akara in act one in the NPC category, to restore full life."),
                false);
        }
    }
}

void ResetGameplayState()
{
    g_playerDead = false;
    g_lifeChecked = false;
    panels::ResetPanels();
    skilltree::ResetSkillTree();
    questlog::ResetQuestLog();
    controls::ResetControls();
    gamemenu::ResetGameMenu();
    g_levelMap.Reset();
    g_tracker = TrackerState{};
    StopAutoWalk();
    g_manual = ManualMoveState{};
    g_usedObjects.clear();
    g_knownGroundItems.clear();
    g_groundItemsSeeded = false;
    g_lastLiveRefresh = 0;
    g_lastCueTick = 0;
    g_lastInteractCueKey = 0;
    StopEmitters();
    g_lastNpcMenu = 0;
    g_lastNpcMenuSelected = -2;
    g_lastNpcMenuLabel.clear();
    g_dialogWasOpen = false;
    {
        std::scoped_lock lock(g_keyMutex);
        g_keyQueue.clear();
    }
    for (auto &held : g_keyHeld)
        held = false;
}

void OnLevelChanged()
{
    const int levelId = g_levelMap.LevelId();
    const bool walkingToExit = g_autoWalk.active && g_autoWalk.category == Category::Exits;
    StopAutoWalk();
    g_tracker.levelId = levelId;
    g_tracker.locked.fill(0);
    g_usedObjects.clear();
    g_knownGroundItems.clear();
    g_groundItemsSeeded = false;
    g_lastInteractCueKey = 0;

    std::wstringstream log;
    log << L"Level changed to " << levelId << L" (" << LevelDisplayName(levelId) << L")"
        << (walkingToExit ? L" while walking to an exit" : L"") << L".";
    LogLine(log.str());
    PushEvent("level", L"entered level " + std::to_wstring(levelId) + L" " + LevelDisplayName(levelId));
    Say(TrS(L"Lokacja: ", L"Location: ") + LevelDisplayName(levelId) + L".");
}

void ProcessKeyEvent(const game::UnitInfo &player, const KeyEvent &event)
{
    // The game menu takes every key while it is open; the skill tree and then
    // the character sheet or inventory take arrows, Enter, Space and Tab.
    if (gamemenu::IsGameMenuOpen())
    {
        gamemenu::HandleGameMenuKey(event.virtualKey, event.ctrl, event.shift);
        return;
    }
    if (controls::HandleControlsKey(event.virtualKey, event.ctrl, event.shift))
        return;
    if (questlog::HandleQuestLogKey(event.virtualKey, event.ctrl, event.shift))
        return;
    // The NPC menu takes the arrows, so they no longer walk the character while
    // a choice is waiting.
    if (HandleNpcMenuKey(event.virtualKey))
        return;
    if (skilltree::HandleSkillTreeKey(player.unit, event.virtualKey, event.ctrl, event.shift))
        return;
    if (panels::HandlePanelKey(player.unit, player.position,
                               panels::PanelKey{event.virtualKey, event.ctrl, event.shift}))
        return;

    int dx = 0;
    int dy = 0;
    if (KeyDirection(event.virtualKey, dx, dy))
    {
        StopAutoWalk();
        if (g_manual.taps.size() < ManualStepQueueLimit)
            g_manual.taps.push_back(Point{dx, dy});
        return;
    }

    switch (event.virtualKey)
    {
    case VK_F1:
        SpeakHelp();
        break;
    case VK_F2:
    case 'G':
        SpeakAreaScan(player);
        break;
    case 'Z':
        panels::SpeakHealth(player.unit, event.shift);
        break;
    case 'X':
        panels::SpeakExperience(player.unit);
        break;
    case 'K':
        SpeakCoordinates(player);
        break;
    case 'L':
        SpeakLocation();
        break;
    case 'Q':
        questlog::ToggleQuestLog();
        break;
    case VK_PRIOR:
        if (event.ctrl)
            SelectTrackerCategoryRelative(player, -1);
        else
            SelectTrackerTargetRelative(player, -1);
        break;
    case VK_NEXT:
        if (event.ctrl)
            SelectTrackerCategoryRelative(player, +1);
        else
            SelectTrackerTargetRelative(player, +1);
        break;
    case VK_HOME:
    case 'H':
    case VK_CLEAR:
    case VK_NUMPAD5:
        if (event.shift)
            AutoWalkKeyPressed(player);
        else
            NavigateKeyPressed(player, event.ctrl);
        break;
    case 'E':
        InteractKeyPressed(player);
        break;
    case 'S':
        if (event.ctrl)
            SelectNormalAttack(player);
        else
            SelectCombatSkill(player, event.shift ? -1 : +1);
        break;
    case 'F':
        AttackKeyPressed(player);
        break;
    default:
        break;
    }
}

// Queries from the MCP server, executed on the game thread.
struct GameJob {
    std::function<nlohmann::json()> query;
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    bool cancelled = false;
    nlohmann::json result;
};

std::mutex g_jobMutex;
std::deque<std::shared_ptr<GameJob>> g_jobs;

void RunPendingJobs()
{
    std::deque<std::shared_ptr<GameJob>> jobs;
    {
        std::scoped_lock lock(g_jobMutex);
        jobs.swap(g_jobs);
    }
    for (const std::shared_ptr<GameJob> &job : jobs)
    {
        {
            std::scoped_lock lock(job->mutex);
            if (job->cancelled)
                continue;
        }
        nlohmann::json value;
        try
        {
            value = job->query();
        }
        catch (const std::exception &e)
        {
            value = nlohmann::json{{"error", e.what()}};
        }
        {
            std::scoped_lock lock(job->mutex);
            job->result = std::move(value);
            job->done = true;
        }
        job->cv.notify_all();
    }
}

void GameTick()
{
    const uintptr_t playerUnit = game::PlayerUnit();
    if (playerUnit == 0)
        return;

    if (!g_gameActive.load())
        OnGameEntered();

    game::UnitInfo player;
    if (!game::ReadUnitInfo(playerUnit, player) || player.room1 == 0)
        return;

    if (g_levelMap.Update(game::ClientAct(), player.room1))
        OnLevelChanged();
    UpdateDeathState(player);

    std::deque<KeyEvent> events;
    {
        std::scoped_lock lock(g_keyMutex);
        events.swap(g_keyQueue);
    }
    for (const KeyEvent &event : events)
        ProcessKeyEvent(player, event);

    RunPendingJobs();
    gamemenu::UpdateGameMenu();
    controls::UpdateControls();
    questlog::UpdateQuestLog();
    skilltree::UpdateSkillTree(player.unit);
    panels::UpdatePanels(player.unit, player.position);
    PollNpcDialogAccessibility();
    UpdateManualMovement(player);
    UpdateAutoWalk(player);
    UpdateCues(player);
}

void ReportTickException()
{
    if (g_tickExceptionLogged)
        return;
    g_tickExceptionLogged = true;
    LogLine(L"Gameplay tick raised a structured exception; the tick was skipped.");
}

void SafeGameTick()
{
    __try
    {
        GameTick();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        ReportTickException();
    }
}

int __stdcall Hook_GameFrameCallback(int frame)
{
    const int result = g_realGameFrame != nullptr ? g_realGameFrame(frame) : 0;
    SafeGameTick();
    return result;
}

int __fastcall Hook_SetCurrentPlayerUnit(void *unit, void *)
{
    const int result = g_realSetCurrentPlayerUnit != nullptr ? g_realSetCurrentPlayerUnit(unit) : 0;
    if (unit != nullptr)
        OnGameEntered();
    return result;
}

} // namespace

bool InitializeGameplayHooks()
{
    if (InstallInlineHook(reinterpret_cast<void *>(game::Absolute(game::va::GameFrameCallback)),
                          reinterpret_cast<void *>(&Hook_GameFrameCallback), 5, g_frameHook))
    {
        g_realGameFrame = reinterpret_cast<GameFrameCallback_t>(g_frameHook.trampoline);
        LogLine(L"Gameplay frame hook installed.");
    }
    else
    {
        LogLine(L"Failed to install gameplay frame hook.");
    }

    if (InstallInlineHook(reinterpret_cast<void *>(AbsoluteAddress(va::Game_SetCurrentPlayerUnit)),
                          reinterpret_cast<void *>(&Hook_SetCurrentPlayerUnit), 7, g_setCurrentPlayerHook))
    {
        g_realSetCurrentPlayerUnit = reinterpret_cast<SetCurrentPlayerUnit_t>(g_setCurrentPlayerHook.trampoline);
        LogLine(L"Gameplay player-unit hook installed.");
    }
    else
    {
        LogLine(L"Failed to install gameplay player-unit hook.");
    }

    LogLine(L"Gameplay accessibility initialized.");
    return true;
}

void ShutdownGameplayHooks()
{
    g_gameActive = false;
    RemoveInlineHook(g_frameHook);
    RemoveInlineHook(g_setCurrentPlayerHook);
    g_realGameFrame = nullptr;
    g_realSetCurrentPlayerUnit = nullptr;
}

void OnGameEntered()
{
    if (g_gameActive.exchange(true))
        return;

    ResetGameplayState();
    PushEvent("game", L"entered");
    NotifyFrontendGameplayReady();
    std::wstringstream stream;
    stream << Tr(L"Gra aktywna. Wersja ", L"Game active. Version ") << GameplayVersion
           << Tr(L". Strzałki ruch, Page Down cel, Control Page Down kategoria, Home droga, Shift Home marsz do celu, "
                 L"E interakcja, F atak, F1 pomoc.",
                 L". Arrows move, Page Down target, Control Page Down category, Home path, Shift Home walk to target, "
                 L"E interact, F attack, F1 help.");
    Say(stream.str());
}

void OnGameLeft()
{
    if (g_gameActive.exchange(false))
        PushEvent("game", L"left");
    ResetGameplayState();
}

bool IsGameplayDialogActive()
{
    if (!g_gameActive.load())
        return false;
    return IsGameplayDialogActiveInternal();
}

bool IsNpcMenuOpenForKeys()
{
    return g_gameActive.load() && g_npcMenuOpenForKeys.load();
}

void NotifyGameplayVirtualKeyState(DWORD virtualKey, bool isDown)
{
    if (virtualKey < g_keyHeld.size())
        g_keyHeld[virtualKey] = isDown;
}

bool HandleGameplayVirtualKey(DWORD virtualKey)
{
    // Runs on the keyboard hook thread: queue the key for the game thread, which
    // is the only place where game memory and packets are touched.
    KeyEvent event;
    event.virtualKey = virtualKey;
    event.ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    event.shift = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    {
        std::scoped_lock lock(g_keyMutex);
        if (g_keyQueue.size() >= 64)
            g_keyQueue.pop_front();
        g_keyQueue.push_back(event);
    }
    return true;
}

// ---------------------------------------------------------------------------
// MCP queries (GameplayQueries.hpp)
// ---------------------------------------------------------------------------

namespace {

using json = nlohmann::json;

constexpr std::array<const char *, CategoryCount> CategoryIds = {
    "items", "chests", "doors", "shrines", "objects", "breakables",
    "monsters", "npcs", "players", "exits", "waypoints", "portals",
};

const char *CategoryId(Category category)
{
    return CategoryIds[static_cast<size_t>(category)];
}

std::optional<Category> ParseCategory(const std::string &name)
{
    for (size_t i = 0; i < CategoryIds.size(); ++i)
    {
        if (name == CategoryIds[i])
            return static_cast<Category>(i);
    }
    return std::nullopt;
}

std::string KeyHex(std::uint64_t key)
{
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%016llx", static_cast<unsigned long long>(key));
    return buffer;
}

json PointJson(Point point)
{
    return json{{"x", point.x}, {"y", point.y}};
}

json TargetJson(const Target &target, const std::vector<Target> &candidates)
{
    const char *source = target.source == TargetSource::Unit ? "unit" : (target.source == TargetSource::Preset ? "preset" : "exit");
    json value{{"key", KeyHex(target.key)},
               {"name", Utf8FromWide(DecoratedName(target, candidates))},
               {"category", CategoryId(target.category)},
               {"source", source},
               {"x", target.position.x},
               {"y", target.position.y},
               {"distance", target.distance},
               {"unit_type", target.unitType},
               {"unit_id", target.unitId},
               {"class_id", target.classId},
               {"mode", target.mode}};
    if (target.category == Category::Exits)
    {
        value["destination_level"] = target.destinationLevel;
        value["destination_name"] = Utf8FromWide(LevelDisplayName(target.destinationLevel));
        value["warp_tile"] = target.warpTile;
    }
    return value;
}

bool ReadPlayerInfo(game::UnitInfo &player)
{
    return game::ReadUnitInfo(game::PlayerUnit(), player);
}

} // namespace

bool IsInGame()
{
    return g_gameActive.load() && game::PlayerUnit() != 0;
}

std::optional<nlohmann::json> QueryGameThread(std::function<nlohmann::json()> query, DWORD timeoutMs)
{
    auto job = std::make_shared<GameJob>();
    job->query = std::move(query);
    {
        std::scoped_lock lock(g_jobMutex);
        g_jobs.push_back(job);
    }

    std::unique_lock lock(job->mutex);
    if (!job->cv.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&job] { return job->done; }))
    {
        job->cancelled = true;
        return std::nullopt;
    }
    return job->result;
}

std::vector<std::string> TargetCategoryNames()
{
    return std::vector<std::string>(CategoryIds.begin(), CategoryIds.end());
}

nlohmann::json QueryStatus()
{
    game::UnitInfo player;
    if (!ReadPlayerInfo(player))
        return json{{"in_game", true}, {"error", "Nie można odczytać jednostki gracza."}};

    const std::uint64_t lockedKey = g_tracker.locked[static_cast<size_t>(g_tracker.category)];
    json autoWalk{{"active", g_autoWalk.active}};
    if (g_autoWalk.active)
    {
        autoWalk["target_key"] = KeyHex(g_autoWalk.key);
        autoWalk["category"] = CategoryId(g_autoWalk.category);
        autoWalk["path_steps"] = g_autoWalk.path.size();
        autoWalk["path_index"] = g_autoWalk.index;
        autoWalk["last_sent"] = g_autoWalk.sent ? PointJson(g_autoWalk.lastSent) : json(nullptr);
        autoWalk["best_distance"] = g_autoWalk.bestDistance;
        autoWalk["failures"] = g_autoWalk.failures;
    }

    return json{
        {"in_game", true},
        {"player",
         {{"x", player.position.x},
          {"y", player.position.y},
          {"mode", player.mode},
          {"idle", IsPlayerIdle(player)},
          {"unit_id", player.unitId},
          {"name", Utf8FromWide(game::PlayerName(player.unit))}}},
        {"level",
         {{"id", g_levelMap.LevelId()},
          {"name", Utf8FromWide(LevelDisplayName(g_levelMap.LevelId()))},
          {"map_ready", g_levelMap.Ready()},
          {"rooms", g_levelMap.RoomCount()},
          {"rooms_with_collision", g_levelMap.RoomsWithCollision()}}},
        {"tracker", {{"category", CategoryId(g_tracker.category)}, {"locked_key", lockedKey != 0 ? json(KeyHex(lockedKey)) : json(nullptr)}}},
        {"auto_walk", std::move(autoWalk)},
        {"dialog_active", IsGameplayDialogActiveInternal()},
        {"last_event_id", LastEventId()},
    };
}

nlohmann::json QueryTargets(const std::string &category)
{
    const std::optional<Category> parsed = ParseCategory(category);
    if (!parsed)
        return json{{"error", "Nieznana kategoria: " + category}};

    game::UnitInfo player;
    if (!ReadPlayerInfo(player))
        return json{{"error", "Nie można odczytać jednostki gracza."}};

    const std::vector<Target> candidates = CollectCategory(player, *parsed);
    json list = json::array();
    for (const Target &target : candidates)
        list.push_back(TargetJson(target, candidates));
    return json{{"category", category},
                {"count", candidates.size()},
                {"player", PointJson(player.position)},
                {"map_ready", g_levelMap.Ready()},
                {"targets", std::move(list)}};
}

nlohmann::json QueryLevel()
{
    std::array<size_t, 6> presetsByType{};
    for (const LevelPreset &preset : g_levelMap.Presets())
        ++presetsByType[std::min<size_t>(preset.unitType, 5)];

    json exits = json::array();
    for (const LevelExit &exit : g_levelMap.Exits())
    {
        exits.push_back(json{{"destination_level", exit.destinationLevel},
                             {"destination_name", Utf8FromWide(LevelDisplayName(exit.destinationLevel))},
                             {"position", PointJson(exit.position)},
                             {"beyond", PointJson(exit.beyond)},
                             {"warp_tile", exit.isWarpTile}});
    }

    const game::Rect &bounds = g_levelMap.Bounds();
    return json{{"id", g_levelMap.LevelId()},
                {"name", Utf8FromWide(LevelDisplayName(g_levelMap.LevelId()))},
                {"map_ready", g_levelMap.Ready()},
                {"bounds", {{"x", bounds.x}, {"y", bounds.y}, {"w", bounds.w}, {"h", bounds.h}}},
                {"rooms", g_levelMap.RoomCount()},
                {"rooms_with_collision", g_levelMap.RoomsWithCollision()},
                {"presets",
                 {{"monsters", presetsByType[1]}, {"objects", presetsByType[2]}, {"tiles", presetsByType[5]}}},
                {"exits", std::move(exits)}};
}

nlohmann::json QueryPath(std::optional<int> x, std::optional<int> y, const std::string &targetKey)
{
    game::UnitInfo player;
    if (!ReadPlayerInfo(player))
        return json{{"error", "Nie można odczytać jednostki gracza."}};

    Target target;
    if (!targetKey.empty())
    {
        std::uint64_t key = 0;
        try
        {
            key = std::stoull(targetKey, nullptr, 16);
        }
        catch (...)
        {
            return json{{"error", "Nieprawidłowy target_key."}};
        }
        const std::vector<Target> all = CollectAllTargets(player, true);
        const auto it = std::find_if(all.begin(), all.end(), [key](const Target &t) { return t.key == key; });
        if (it == all.end())
            return json{{"error", "Nie znaleziono celu o tym kluczu."}};
        target = *it;
    }
    else if (x && y)
    {
        target.category = Category::Items;
        target.position = Point{*x, *y};
        target.name = L"punkt";
    }
    else
    {
        return json{{"error", "Podaj target_key albo x i y."}};
    }

    RefreshLiveCollision(true);
    PathPlan plan = PlanPath(player.position, target, true);

    json cells = json::array();
    for (size_t i = 0; i < plan.cells.size() && i < 400; ++i)
        cells.push_back(json::array({plan.cells[i].x, plan.cells[i].y}));

    return json{{"from", PointJson(player.position)},
                {"to", PointJson(target.position)},
                {"target", Utf8FromWide(target.name)},
                {"found", plan.found},
                {"door_blocked", plan.doorBlocked},
                {"door", plan.doorBlocked ? PointJson(plan.door) : json(nullptr)},
                {"steps", plan.cells.size()},
                {"description", Utf8FromWide(DescribePath(player.position, plan.cells))},
                {"map_ready", g_levelMap.Ready()},
                {"cells", std::move(cells)}};
}

nlohmann::json QueryCollision(std::optional<int> x, std::optional<int> y, int radius)
{
    game::UnitInfo player;
    if (!ReadPlayerInfo(player))
        return json{{"error", "Nie można odczytać jednostki gracza."}};

    RefreshLiveCollision(true);
    const Point centre = x && y ? Point{*x, *y} : player.position;
    json rows = json::array();
    for (int row = centre.y - radius; row <= centre.y + radius; ++row)
    {
        std::string line;
        for (int column = centre.x - radius; column <= centre.x + radius; ++column)
        {
            const std::uint16_t mask = g_levelMap.MaskAt(column, row);
            char ch = '.';
            if (column == player.position.x && row == player.position.y)
                ch = '@';
            else if (mask == game::CollideUnknown)
                ch = '?';
            else if ((mask & game::CollideDoor) != 0)
                ch = 'D';
            else if ((mask & game::CollideMaskPlayerPath) != 0)
                ch = '#';
            else if (!g_levelMap.IsStandable(column, row, game::CollideMaskPlayerPath))
                ch = ',';
            line.push_back(ch);
        }
        rows.push_back(std::move(line));
    }

    return json{{"centre", PointJson(centre)},
                {"player", PointJson(player.position)},
                {"top_left", PointJson(Point{centre.x - radius, centre.y - radius})},
                {"legend", "@ postać, . można stanąć, , wąsko, # blokada, D drzwi, ? nieznane"},
                {"rows", std::move(rows)}};
}

nlohmann::json QueryUnits(int radius)
{
    game::UnitInfo player;
    if (!ReadPlayerInfo(player))
        return json{{"error", "Nie można odczytać jednostki gracza."}};

    static constexpr std::array<const char *, 6> TypeNames = {"player", "monster", "object", "missile", "item", "tile"};
    std::vector<std::pair<int, json>> units;
    std::unordered_set<uintptr_t> seen;

    uintptr_t room1 = game::ActFirstRoom1(game::ClientAct());
    for (int r = 0; r < 4096 && room1 != 0; ++r, room1 = game::Room1Next(room1))
    {
        const int roomLevel = game::Room1LevelId(room1);
        uintptr_t unit = game::Room1FirstUnit(room1);
        for (int i = 0; i < 1024 && unit != 0; ++i)
        {
            if (!seen.insert(unit).second)
                break;
            game::UnitInfo info;
            if (game::ReadUnitInfo(unit, info))
            {
                const int distance = game::ChebyshevDistance(player.position, info.position);
                if (distance <= radius)
                {
                    Target target;
                    const bool tracked = BuildUnitTarget(player, unit, true, std::nullopt, target);
                    std::wstring name = tracked ? target.name : game::UnitName(unit);
                    units.emplace_back(distance,
                                       json{{"type", TypeNames[std::min<size_t>(info.type, 5)]},
                                            {"class_id", info.classId},
                                            {"unit_id", info.unitId},
                                            {"mode", info.mode},
                                            {"x", info.position.x},
                                            {"y", info.position.y},
                                            {"distance", distance},
                                            {"room_level", roomLevel},
                                            {"name", Utf8FromWide(NormalizeUiText(name))},
                                            {"tracker_category", tracked ? json(CategoryId(target.category)) : json(nullptr)},
                                            {"is_self", info.unit == player.unit}});
                }
            }
            const uintptr_t next = game::ReadPtr(unit + game::off::UnitRoomNext);
            if (next == unit)
                break;
            unit = next;
        }
    }

    std::sort(units.begin(), units.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    json list = json::array();
    for (auto &entry : units)
        list.push_back(std::move(entry.second));
    return json{{"player", PointJson(player.position)}, {"radius", radius}, {"count", list.size()}, {"units", std::move(list)}};
}

bool IsGameplayKey(DWORD virtualKey)
{
    return IsGameplayKeyCaptured(virtualKey);
}

bool IsGameplayKeyCaptured(DWORD virtualKey)
{
    // While the controls screen waits for the new key, every key belongs to the
    // game, otherwise the key could never be assigned.
    if (controls::IsWaitingForKeyBinding())
        return false;

    if (virtualKey == VK_F1 || virtualKey == VK_F2 || virtualKey == VK_PRIOR || virtualKey == VK_NEXT ||
        virtualKey == VK_HOME || virtualKey == VK_UP || virtualKey == VK_DOWN || virtualKey == VK_LEFT ||
        virtualKey == VK_RIGHT || virtualKey == 'E' || virtualKey == 'F' || virtualKey == 'G' ||
        virtualKey == 'H' || virtualKey == 'K' || virtualKey == 'L' || virtualKey == 'Q' || virtualKey == 'S' ||
        virtualKey == 'Z' ||
        virtualKey == 'X' || virtualKey == VK_CLEAR || (virtualKey >= VK_NUMPAD1 && virtualKey <= VK_NUMPAD9))
        return true;

    // Enter, Space, Tab and End stay with the game unless one of the accessible
    // panels or menus is open.
    if (virtualKey == VK_RETURN || virtualKey == VK_SPACE || virtualKey == VK_TAB || virtualKey == VK_END)
        return g_gameActive.load() && (panels::IsPanelOpenForKeys() || skilltree::IsSkillTreeOpenForKeys() ||
                                       gamemenu::IsGameMenuOpenForKeys() || questlog::IsQuestLogOpenForKeys() ||
                                       controls::IsControlsScreenOpenForKeys() || IsNpcMenuOpenForKeys());
    return false;
}

void InjectGameplayKey(DWORD virtualKey, bool ctrl, bool shift)
{
    KeyEvent event;
    event.virtualKey = virtualKey;
    event.ctrl = ctrl;
    event.shift = shift;
    std::scoped_lock lock(g_keyMutex);
    if (g_keyQueue.size() >= 64)
        g_keyQueue.pop_front();
    g_keyQueue.push_back(event);
}

void SetInjectedKeyHeld(DWORD virtualKey, bool held)
{
    NotifyGameplayVirtualKeyState(virtualKey, held);
}

} // namespace d2access
