#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Layout of the client-side world in Game.exe 1.14b (1.14.1.68), recovered with
// IDA. Offsets match the 1.13c structure layout, not the 1.10 one used by D2MOO.
// See docs/ida_notes.md for the function each offset was verified against.
namespace d2access::game {

enum class UnitType : std::uint32_t {
    Player = 0,
    Monster = 1,
    Object = 2,
    Missile = 3,
    Item = 4,
    Tile = 5,
};

namespace va {
constexpr uintptr_t CurrentPlayerUnit = 0x79D0B0;
constexpr uintptr_t ClientAct = 0x796C74;
constexpr uintptr_t GameFrameCallback = 0x43C280;
constexpr uintptr_t SendPacket = 0x465C40;
constexpr uintptr_t AddRoomData = 0x616360;
constexpr uintptr_t RemoveRoomData = 0x6163B0;
constexpr uintptr_t WarpDestinationLevel = 0x66AD50;
constexpr uintptr_t GetUnitName = 0x451E40;
constexpr uintptr_t GetObjectsTxtRecord = 0x63E7D0;
constexpr uintptr_t GetLevelsTxtRecord = 0x61A0B0;
} // namespace va

namespace off {
constexpr size_t UnitType = 0x00;
constexpr size_t UnitClassId = 0x04;
constexpr size_t UnitId = 0x0C;
constexpr size_t UnitMode = 0x10;
constexpr size_t UnitData = 0x14;
constexpr size_t UnitPath = 0x2C;
constexpr size_t UnitFlags = 0xC4;
constexpr size_t UnitFlagsEx = 0xC8;
constexpr size_t UnitRoomNext = 0xE8;

constexpr size_t DynamicPathX = 0x02;
constexpr size_t DynamicPathY = 0x06;
constexpr size_t DynamicPathRoom = 0x1C;
constexpr size_t StaticPathRoom = 0x00;
constexpr size_t StaticPathX = 0x0C;
constexpr size_t StaticPathY = 0x10;

constexpr size_t ActFirstRoom1 = 0x10;
constexpr size_t ActDrlgMisc = 0x48;

constexpr size_t Room1Adjacent = 0x00;
constexpr size_t Room1Room2 = 0x10;
constexpr size_t Room1Collision = 0x20;
constexpr size_t Room1AdjacentCount = 0x24;
constexpr size_t Room1SubtileX = 0x4C;
constexpr size_t Room1FirstUnit = 0x74;
constexpr size_t Room1Next = 0x7C;

constexpr size_t CollisionMask = 0x20;

constexpr size_t Room2Adjacent = 0x08;
constexpr size_t Room2Next = 0x24;
constexpr size_t Room2AdjacentCount = 0x2C;
constexpr size_t Room2Room1 = 0x30;
constexpr size_t Room2TileX = 0x34;
constexpr size_t Room2RoomTiles = 0x4C;
constexpr size_t Room2Level = 0x58;
constexpr size_t Room2Presets = 0x5C;

constexpr size_t PresetClassId = 0x04;
constexpr size_t PresetX = 0x08;
constexpr size_t PresetNext = 0x0C;
constexpr size_t PresetUnitType = 0x14;
constexpr size_t PresetY = 0x18;

constexpr size_t LevelFirstRoom2 = 0x10;
constexpr size_t LevelTileX = 0x1C;
constexpr size_t LevelId = 0x1D0;

constexpr size_t PlayerDataName = 0x00;
constexpr size_t MonsterDataName = 0x2C;

constexpr size_t ObjectTxtSelectable = 0xC4;
constexpr size_t ObjectTxtIsDoor = 0x13A;
constexpr size_t ObjectTxtSubClass = 0x167;
} // namespace off

// objects.txt SubClass bits.
constexpr std::uint8_t ObjectSubClassShrine = 0x01;
constexpr std::uint8_t ObjectSubClassObelisk = 0x02;
constexpr std::uint8_t ObjectSubClassTownPortal = 0x04;
constexpr std::uint8_t ObjectSubClassChest = 0x08;
constexpr std::uint8_t ObjectSubClassPortal = 0x10;
constexpr std::uint8_t ObjectSubClassWell = 0x20;
constexpr std::uint8_t ObjectSubClassWaypoint = 0x40;
constexpr std::uint8_t ObjectSubClassDoor = 0x80;

// Collision mask bits (D2C_CollisionMaskFlags).
constexpr std::uint16_t CollideWall = 0x0001;
constexpr std::uint16_t CollideNoPlayer = 0x0008;
constexpr std::uint16_t CollideBlank = 0x0020;
constexpr std::uint16_t CollidePlayer = 0x0080;
constexpr std::uint16_t CollideMonster = 0x0100;
constexpr std::uint16_t CollideObject = 0x0400;
constexpr std::uint16_t CollideDoor = 0x0800;
constexpr std::uint16_t CollideNoPath = 0x1000;
constexpr std::uint16_t CollideUnknown = 0xFFFF;
// CollideNoPath is left out: units mark their own centre cell with 0x1080
// (player + no-path), which would block the path search at its start cell.
constexpr std::uint16_t CollideMaskPlayerPath =
    CollideWall | CollideNoPlayer | CollideBlank | CollideObject | CollideDoor;

// Player animation modes.
constexpr std::uint32_t PlayerModeDeath = 0;
constexpr std::uint32_t PlayerModeNeutral = 1;
constexpr std::uint32_t PlayerModeTownNeutral = 5;
constexpr std::uint32_t PlayerModeDead = 17;

struct Point {
    int x = 0;
    int y = 0;

    friend bool operator==(const Point &a, const Point &b) { return a.x == b.x && a.y == b.y; }
    friend bool operator!=(const Point &a, const Point &b) { return !(a == b); }
};

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    [[nodiscard]] bool Contains(int px, int py) const
    {
        return px >= x && py >= y && px < x + w && py < y + h;
    }
};

inline int ChebyshevDistance(Point a, Point b)
{
    const int dx = a.x > b.x ? a.x - b.x : b.x - a.x;
    const int dy = a.y > b.y ? a.y - b.y : b.y - a.y;
    return dx > dy ? dx : dy;
}

uintptr_t Absolute(uintptr_t va);

// Memory access. All helpers are SEH guarded and return false/0 on bad pointers.
bool ReadBytes(uintptr_t address, void *out, size_t size);

template <typename T>
bool Read(uintptr_t address, T &out)
{
    return ReadBytes(address, &out, sizeof(T));
}

bool WriteBytes(uintptr_t address, const void *data, size_t size);

template <typename T>
bool Write(uintptr_t address, const T &value)
{
    return WriteBytes(address, &value, sizeof(T));
}

uintptr_t ReadPtr(uintptr_t address);
std::wstring ReadWideString(uintptr_t address, size_t maxChars);
std::wstring ReadAnsiString(uintptr_t address, size_t maxChars);

uintptr_t PlayerUnit();
uintptr_t ClientAct();

struct UnitInfo {
    uintptr_t unit = 0;
    std::uint32_t type = 0;
    std::uint32_t classId = 0;
    std::uint32_t unitId = 0;
    std::uint32_t mode = 0;
    std::uint32_t flags = 0;
    std::uint32_t flagsEx = 0;
    uintptr_t room1 = 0;
    Point position;
};

bool ReadUnitInfo(uintptr_t unit, UnitInfo &info);

uintptr_t Room1Room2(uintptr_t room1);
uintptr_t Room1Next(uintptr_t room1);
uintptr_t Room1FirstUnit(uintptr_t room1);
bool Room1Bounds(uintptr_t room1, Rect &subtiles);
int Room1LevelId(uintptr_t room1);

uintptr_t Room2Level(uintptr_t room2);
uintptr_t Room2Next(uintptr_t room2);
bool Room2TileRect(uintptr_t room2, Rect &tiles);
std::vector<uintptr_t> Room2Adjacent(uintptr_t room2);
int Room2LevelId(uintptr_t room2);

uintptr_t LevelFirstRoom2(uintptr_t level);
bool LevelTileRect(uintptr_t level, Rect &tiles);
int LevelIdOf(uintptr_t level);

uintptr_t ActFirstRoom1(uintptr_t act);

struct CollisionGrid {
    Rect rect;
    uintptr_t mask = 0;
};

bool Room1Collision(uintptr_t room1, CollisionGrid &grid);

// Game functions. Call these only from the game thread.
bool AddRoomData(uintptr_t act, int levelId, int tileX, int tileY, uintptr_t hintRoom1);
bool RemoveRoomData(uintptr_t act, int levelId, int tileX, int tileY, uintptr_t hintRoom1);
int WarpDestinationLevel(uintptr_t room2, std::uint32_t tileClassId);
std::wstring UnitName(uintptr_t unit);
std::wstring PlayerName(uintptr_t unit);
uintptr_t ObjectTxt(std::uint32_t classId);
std::wstring LevelTxtName(int levelId);

bool SendPacket(const std::uint8_t *data, int size);
bool SendMoveTo(Point destination, bool run);
bool SendUnitPacket(std::uint8_t packetId, std::uint32_t unitType, std::uint32_t unitId);
bool SendPickupItem(std::uint32_t unitId);

// ---------------------------------------------------------------------------
// Stats, experience and difficulty
// ---------------------------------------------------------------------------

namespace stat {
constexpr int Strength = 0;
constexpr int Energy = 1;
constexpr int Dexterity = 2;
constexpr int Vitality = 3;
constexpr int StatPoints = 4;
constexpr int SkillPoints = 5;
constexpr int HitPoints = 6;
constexpr int MaxHitPoints = 7;
constexpr int Mana = 8;
constexpr int MaxMana = 9;
constexpr int Stamina = 10;
constexpr int MaxStamina = 11;
constexpr int Level = 12;
constexpr int Experience = 13;
constexpr int Gold = 14;
constexpr int GoldBank = 15;
constexpr int Defense = 31;
constexpr int DamageResist = 36;
constexpr int MagicResist = 37;
constexpr int FireResist = 39;
constexpr int MaxFireResist = 40;
constexpr int LightningResist = 41;
constexpr int MaxLightningResist = 42;
constexpr int ColdResist = 43;
constexpr int MaxColdResist = 44;
constexpr int PoisonResist = 45;
constexpr int MaxPoisonResist = 46;
constexpr int MagicFind = 80;
} // namespace stat

// Total value including items and skills (STATLIST_GetUnitStat, 0x621C90).
int UnitStat(uintptr_t unit, int statId);
// Same, for stats that use a layer (skill id, class id, skill tab).
int UnitStatLayer(uintptr_t unit, int statId, int layer);
// Value without item bonuses (0x621B70 on the unit's stat list).
int UnitBaseStat(uintptr_t unit, int statId);
// 0 normal, 1 nightmare, 2 hell (0x43ADF0).
int Difficulty();
// Experience needed to reach `level` for a character class, or -1.
long long ExperienceForLevel(std::uint32_t classId, int level);

// ---------------------------------------------------------------------------
// Inventory
// ---------------------------------------------------------------------------

namespace bodyloc {
constexpr int Head = 1;
constexpr int Neck = 2;
constexpr int Torso = 3;
constexpr int RightHand = 4;
constexpr int LeftHand = 5;
constexpr int RightRing = 6;
constexpr int LeftRing = 7;
constexpr int Belt = 8;
constexpr int Feet = 9;
constexpr int Gloves = 10;
} // namespace bodyloc

enum class ItemNode : std::uint8_t {
    Storage = 1, // inventory grid, stash or cube (see page)
    Belt = 2,
    Equipped = 3,
};

constexpr std::uint8_t ItemPageInventory = 0;
constexpr std::uint8_t ItemPageCube = 3;
constexpr std::uint8_t ItemPageStash = 4;

struct ItemEntry {
    uintptr_t unit = 0;
    std::uint32_t unitId = 0;
    std::uint32_t classId = 0;
    std::uint8_t node = 0;
    std::uint8_t page = 0xFF;
    std::uint8_t bodyLocation = 0;
    int x = 0; // grid column, or belt slot
    int y = 0; // grid row
};

uintptr_t UnitInventory(uintptr_t unit);
uintptr_t InventoryCursorItem(uintptr_t inventory);
std::vector<ItemEntry> InventoryItems(uintptr_t inventory);
bool ReadItemEntry(uintptr_t item, ItemEntry &entry);
// Full coloured-name text as shown in the hover box (0x479E40).
std::wstring ItemFullName(uintptr_t item);
// ITEMS_CheckItemType (0x626320) with itemtypes.txt rows, e.g. 9 potion,
// 22 scroll, 45 weapon, 50 armor (shields included).
bool ItemIsType(uintptr_t item, int itemType);
// Body locations an item may be worn in, from itemtypes.txt. False for items
// that cannot be worn at all, such as potions and scrolls.
bool ItemBodyLocations(uintptr_t item, int &primary, int &secondary);

// UI panel flags (D2CLIENT UI vars at 0x798E00).
bool IsUiPanelOpen(int index);

// Top-level window of the game process.
HWND GameWindow();
// Game mouse position (0x79D0F0 / 0x79D0EC), in game screen coordinates.
bool ReadGameMouse(std::int32_t &x, std::int32_t &y);
// Posts WM_MOUSEMOVE for a point in game screen coordinates. The game hit-tests
// panels (e.g. the hovered inventory item, 0x475100) only in its mouse-move
// handler, so writing the position alone is not enough.
bool PostGameMouseMove(std::int32_t x, std::int32_t y);

bool SendPickItemFromStorage(std::uint32_t itemId);
bool SendPlaceItemInStorage(std::uint32_t itemId, int x, int y, int page);
bool SendEquipCursorItem(std::uint32_t itemId, int bodyLocation);
bool SendSwapEquippedItem(std::uint32_t itemId, int bodyLocation);
bool SendPickEquippedItem(int bodyLocation);
bool SendUseStorageItem(std::uint32_t itemId, Point playerPosition);
bool SendUseBeltItem(std::uint32_t itemId);
bool SendPickBeltItem(std::uint32_t itemId);
bool SendPlaceItemInBelt(std::uint32_t itemId, int slot);
bool SendDropCursorItem(std::uint32_t itemId);
bool SendAddStatPoint(int statId);

// ---------------------------------------------------------------------------
// Strings, expansion flag and skills
// ---------------------------------------------------------------------------

// Localised text by string table id (STRTABLE_GetStringById, 0x5207C0).
std::wstring StringById(int stringId);
// True when Lord of Destruction is running (0x406290).
bool IsExpansion();

// skills.txt (record 0x23C) joined with skilldesc.txt (record 0x120).
struct SkillDefinition {
    int id = -1;
    int classId = -1;
    int page = 0;   // skill tree tab, 1..3
    int row = 0;    // 1..6
    int column = 0; // 1..3
    int requiredLevel = 0;
    std::array<int, 3> requiredSkills{-1, -1, -1};
    int nameStringId = 0;
    int shortStringId = 0;
    int elementType = 0;
};

bool ReadSkillDefinition(int skillId, SkillDefinition &skill);
// Skills of a character class in skills.txt order.
std::vector<int> ClassSkillIds(std::uint32_t classId);
// Points spent in a skill (0 when the skill was never learned).
int PlayerSkillPoints(uintptr_t unit, int skillId);
// Levels added by items (+skills, +class, +tab, oskills), as in 0x641B10.
int SkillBonusLevels(uintptr_t unit, const SkillDefinition &skill);
// Packet 0x3B, sent by the skill tree click handler (0x499F70).
bool SendAddSkillPoint(int skillId);

} // namespace d2access::game
