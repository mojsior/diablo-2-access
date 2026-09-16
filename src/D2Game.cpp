#include "D2Game.hpp"

#include "Addresses.hpp"

#include <array>
#include <cstring>

// CL_SendPacket (0x465C40) takes the packet in ebx and its length in edi.
static uintptr_t g_d2SendPacketFn = 0;

static __declspec(naked) int __stdcall CallGameSendPacket(const unsigned char *, int)
{
    __asm {
        push ebp
        push ebx
        push esi
        push edi
        mov ebx, [esp + 20]
        mov edi, [esp + 24]
        call dword ptr [g_d2SendPacketFn]
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret 8
    }
}

namespace d2access::game {

namespace {

constexpr uintptr_t ObjectsTxtBase = 0x97F418;
constexpr uintptr_t ObjectsTxtCount = 0x97F41C;
constexpr size_t ObjectsTxtRecordSize = 0x1C0;
constexpr uintptr_t StringByKey = 0x520BC0;
constexpr size_t LevelsTxtNameKey = 0xF5;
constexpr int MaxListWalk = 4096;

bool SehMemcpy(void *dst, const void *src, size_t size)
{
    __try
    {
        std::memcpy(dst, src, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

using RoomData_t = void(__stdcall *)(void *, int, int, int, void *);

bool SehRoomData(uintptr_t fn, uintptr_t act, int levelId, int x, int y, uintptr_t room1)
{
    __try
    {
        reinterpret_cast<RoomData_t>(fn)(reinterpret_cast<void *>(act), levelId, x, y,
                                         reinterpret_cast<void *>(room1));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

using UnitName_t = const wchar_t *(__thiscall *)(void *);

const wchar_t *SehUnitName(uintptr_t fn, uintptr_t unit)
{
    __try
    {
        return reinterpret_cast<UnitName_t>(fn)(reinterpret_cast<void *>(unit));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

using LevelsTxt_t = void *(__stdcall *)(int);
using StringByKey_t = const wchar_t *(__thiscall *)(const char *);

const wchar_t *SehLevelName(uintptr_t levelsFn, uintptr_t stringFn, int levelId)
{
    __try
    {
        const void *record = reinterpret_cast<LevelsTxt_t>(levelsFn)(levelId);
        if (record == nullptr)
            return nullptr;
        const char *key = static_cast<const char *>(record) + LevelsTxtNameKey;
        if (key[0] == '\0')
            return nullptr;
        return reinterpret_cast<StringByKey_t>(stringFn)(key);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

int SehSendPacket(const std::uint8_t *data, int size)
{
    __try
    {
        return CallGameSendPacket(data, size);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool IsPlausiblePointer(uintptr_t value)
{
    return value >= 0x10000 && value < 0x80000000u;
}

} // namespace

uintptr_t Absolute(uintptr_t va)
{
    return AbsoluteAddress(va);
}

bool ReadBytes(uintptr_t address, void *out, size_t size)
{
    if (!IsPlausiblePointer(address) || out == nullptr || size == 0)
        return false;
    return SehMemcpy(out, reinterpret_cast<const void *>(address), size);
}

uintptr_t ReadPtr(uintptr_t address)
{
    std::uint32_t value = 0;
    if (!Read(address, value) || !IsPlausiblePointer(value))
        return 0;
    return value;
}

std::wstring ReadWideString(uintptr_t address, size_t maxChars)
{
    std::wstring text;
    for (size_t i = 0; i < maxChars; ++i)
    {
        std::uint16_t ch = 0;
        if (!Read(address + i * 2, ch) || ch == 0)
            break;
        text.push_back(static_cast<wchar_t>(ch));
    }
    return text;
}

std::wstring ReadAnsiString(uintptr_t address, size_t maxChars)
{
    std::wstring text;
    for (size_t i = 0; i < maxChars; ++i)
    {
        std::uint8_t ch = 0;
        if (!Read(address + i, ch) || ch == 0)
            break;
        text.push_back(static_cast<wchar_t>(ch));
    }
    return text;
}

uintptr_t PlayerUnit()
{
    return ReadPtr(Absolute(va::CurrentPlayerUnit));
}

uintptr_t ClientAct()
{
    return ReadPtr(Absolute(va::ClientAct));
}

bool ReadUnitInfo(uintptr_t unit, UnitInfo &info)
{
    info = UnitInfo{};
    if (!IsPlausiblePointer(unit))
        return false;

    struct Header {
        std::uint32_t type;
        std::uint32_t classId;
        std::uint32_t memoryPool;
        std::uint32_t unitId;
        std::uint32_t mode;
    } header{};
    if (!Read(unit, header) || header.type > static_cast<std::uint32_t>(UnitType::Tile))
        return false;

    info.unit = unit;
    info.type = header.type;
    info.classId = header.classId;
    info.unitId = header.unitId;
    info.mode = header.mode;
    Read(unit + off::UnitFlags, info.flags);
    Read(unit + off::UnitFlagsEx, info.flagsEx);

    const uintptr_t path = ReadPtr(unit + off::UnitPath);
    if (path == 0)
        return false;

    // UNITS_GetRoom: objects, items and tiles use the static path layout.
    const bool staticPath = header.type == static_cast<std::uint32_t>(UnitType::Object) ||
                            header.type == static_cast<std::uint32_t>(UnitType::Item) ||
                            header.type == static_cast<std::uint32_t>(UnitType::Tile);
    if (staticPath)
    {
        std::int32_t x = 0;
        std::int32_t y = 0;
        info.room1 = ReadPtr(path + off::StaticPathRoom);
        if (!Read(path + off::StaticPathX, x) || !Read(path + off::StaticPathY, y))
            return false;
        info.position = Point{x, y};
    }
    else
    {
        std::uint16_t x = 0;
        std::uint16_t y = 0;
        info.room1 = ReadPtr(path + off::DynamicPathRoom);
        if (!Read(path + off::DynamicPathX, x) || !Read(path + off::DynamicPathY, y))
            return false;
        info.position = Point{x, y};
    }

    return info.position.x > 0 && info.position.y > 0;
}

uintptr_t Room1Room2(uintptr_t room1)
{
    return room1 != 0 ? ReadPtr(room1 + off::Room1Room2) : 0;
}

uintptr_t Room1Next(uintptr_t room1)
{
    return room1 != 0 ? ReadPtr(room1 + off::Room1Next) : 0;
}

uintptr_t Room1FirstUnit(uintptr_t room1)
{
    return room1 != 0 ? ReadPtr(room1 + off::Room1FirstUnit) : 0;
}

bool Room1Bounds(uintptr_t room1, Rect &subtiles)
{
    std::array<std::int32_t, 4> values{};
    if (room1 == 0 || !Read(room1 + off::Room1SubtileX, values))
        return false;
    subtiles = Rect{values[0], values[1], values[2], values[3]};
    return subtiles.w > 0 && subtiles.h > 0;
}

int Room1LevelId(uintptr_t room1)
{
    return Room2LevelId(Room1Room2(room1));
}

uintptr_t Room2Level(uintptr_t room2)
{
    return room2 != 0 ? ReadPtr(room2 + off::Room2Level) : 0;
}

uintptr_t Room2Next(uintptr_t room2)
{
    return room2 != 0 ? ReadPtr(room2 + off::Room2Next) : 0;
}

bool Room2TileRect(uintptr_t room2, Rect &tiles)
{
    std::array<std::int32_t, 4> values{};
    if (room2 == 0 || !Read(room2 + off::Room2TileX, values))
        return false;
    tiles = Rect{values[0], values[1], values[2], values[3]};
    return tiles.w > 0 && tiles.h > 0;
}

std::vector<uintptr_t> Room2Adjacent(uintptr_t room2)
{
    std::vector<uintptr_t> result;
    const uintptr_t list = ReadPtr(room2 + off::Room2Adjacent);
    std::int32_t count = 0;
    if (list == 0 || !Read(room2 + off::Room2AdjacentCount, count) || count <= 0 || count > 64)
        return result;

    result.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        const uintptr_t adjacent = ReadPtr(list + static_cast<uintptr_t>(i) * 4);
        if (adjacent != 0 && adjacent != room2)
            result.push_back(adjacent);
    }
    return result;
}

int Room2LevelId(uintptr_t room2)
{
    return LevelIdOf(Room2Level(room2));
}

uintptr_t LevelFirstRoom2(uintptr_t level)
{
    return level != 0 ? ReadPtr(level + off::LevelFirstRoom2) : 0;
}

bool LevelTileRect(uintptr_t level, Rect &tiles)
{
    std::array<std::int32_t, 4> values{};
    if (level == 0 || !Read(level + off::LevelTileX, values))
        return false;
    tiles = Rect{values[0], values[1], values[2], values[3]};
    return tiles.w > 0 && tiles.h > 0;
}

int LevelIdOf(uintptr_t level)
{
    std::int32_t id = 0;
    if (level == 0 || !Read(level + off::LevelId, id) || id <= 0 || id >= 256)
        return 0;
    return id;
}

uintptr_t ActFirstRoom1(uintptr_t act)
{
    return act != 0 ? ReadPtr(act + off::ActFirstRoom1) : 0;
}

bool Room1Collision(uintptr_t room1, CollisionGrid &grid)
{
    const uintptr_t collision = ReadPtr(room1 + off::Room1Collision);
    if (collision == 0)
        return false;

    std::array<std::int32_t, 4> values{};
    if (!Read(collision, values))
        return false;

    grid.rect = Rect{values[0], values[1], values[2], values[3]};
    grid.mask = ReadPtr(collision + off::CollisionMask);
    return grid.mask != 0 && grid.rect.w > 0 && grid.rect.h > 0 && grid.rect.w <= 1024 &&
           grid.rect.h <= 1024;
}

bool AddRoomData(uintptr_t act, int levelId, int tileX, int tileY, uintptr_t hintRoom1)
{
    if (act == 0 || levelId <= 0)
        return false;
    return SehRoomData(Absolute(va::AddRoomData), act, levelId, tileX, tileY, hintRoom1);
}

bool RemoveRoomData(uintptr_t act, int levelId, int tileX, int tileY, uintptr_t hintRoom1)
{
    if (act == 0 || levelId <= 0)
        return false;
    return SehRoomData(Absolute(va::RemoveRoomData), act, levelId, tileX, tileY, hintRoom1);
}

int WarpDestinationLevel(uintptr_t room2, std::uint32_t tileClassId)
{
    // DRLGROOM_GetWarpDestinationLevel (0x66AD50) calls exit() when no room tile
    // matches, so the lookup is done by walking Room2+0x4C directly.
    int onlyDestination = 0;
    int destinations = 0;
    uintptr_t tile = ReadPtr(room2 + off::Room2RoomTiles);
    for (int i = 0; i < 64 && tile != 0; ++i)
    {
        const uintptr_t destinationRoom2 = ReadPtr(tile);
        const int destination = Room2LevelId(destinationRoom2);
        const uintptr_t warp = ReadPtr(tile + 0x10);
        std::uint32_t warpId = 0;
        if (destination > 0 && warp != 0 && Read(warp, warpId) && warpId == tileClassId)
            return destination;
        if (destination > 0 && destination != onlyDestination)
        {
            onlyDestination = destination;
            ++destinations;
        }
        tile = ReadPtr(tile + 0x04);
    }
    return destinations == 1 ? onlyDestination : 0;
}

std::wstring UnitName(uintptr_t unit)
{
    std::uint32_t type = 0;
    if (!Read(unit, type))
        return {};
    if (type == static_cast<std::uint32_t>(UnitType::Player))
        return PlayerName(unit);
    if (type != static_cast<std::uint32_t>(UnitType::Monster) &&
        type != static_cast<std::uint32_t>(UnitType::Object) &&
        type != static_cast<std::uint32_t>(UnitType::Item))
        return {};

    const wchar_t *name = SehUnitName(Absolute(va::GetUnitName), unit);
    if (name == nullptr)
        return {};
    return ReadWideString(reinterpret_cast<uintptr_t>(name), 96);
}

std::wstring PlayerName(uintptr_t unit)
{
    const uintptr_t data = ReadPtr(unit + off::UnitData);
    return data != 0 ? ReadAnsiString(data + off::PlayerDataName, 16) : std::wstring();
}

uintptr_t ObjectTxt(std::uint32_t classId)
{
    const uintptr_t base = ReadPtr(Absolute(ObjectsTxtBase));
    std::int32_t count = 0;
    if (base == 0 || !Read(Absolute(ObjectsTxtCount), count) || classId >= static_cast<std::uint32_t>(count))
        return 0;
    return base + classId * ObjectsTxtRecordSize;
}

std::wstring LevelTxtName(int levelId)
{
    if (levelId <= 0 || levelId >= 256)
        return {};
    const wchar_t *name =
        SehLevelName(Absolute(va::GetLevelsTxtRecord), Absolute(StringByKey), levelId);
    if (name == nullptr)
        return {};
    return ReadWideString(reinterpret_cast<uintptr_t>(name), 64);
}

bool SendPacket(const std::uint8_t *data, int size)
{
    if (data == nullptr || size <= 0 || size >= 512)
        return false;
    g_d2SendPacketFn = Absolute(va::SendPacket);
    return SehSendPacket(data, size) != 0;
}

bool SendMoveTo(Point destination, bool run)
{
    if (destination.x <= 0 || destination.y <= 0 || destination.x > 0xFFFF || destination.y > 0xFFFF)
        return false;

    const std::array<std::uint8_t, 5> packet = {
        static_cast<std::uint8_t>(run ? 0x03 : 0x01),
        static_cast<std::uint8_t>(destination.x & 0xFF),
        static_cast<std::uint8_t>((destination.x >> 8) & 0xFF),
        static_cast<std::uint8_t>(destination.y & 0xFF),
        static_cast<std::uint8_t>((destination.y >> 8) & 0xFF),
    };
    return SendPacket(packet.data(), static_cast<int>(packet.size()));
}

bool SendUnitPacket(std::uint8_t packetId, std::uint32_t unitType, std::uint32_t unitId)
{
    std::array<std::uint8_t, 9> packet{};
    packet[0] = packetId;
    std::memcpy(packet.data() + 1, &unitType, 4);
    std::memcpy(packet.data() + 5, &unitId, 4);
    return SendPacket(packet.data(), static_cast<int>(packet.size()));
}

namespace {

using UnitStat_t = int(__stdcall *)(void *, int, int);
using Difficulty_t = std::uint8_t(__stdcall *)();
using ItemName_t = void(__fastcall *)(void *, wchar_t *, int);

constexpr uintptr_t VaUnitStat = 0x621C90;
constexpr uintptr_t VaStatListBaseStat = 0x621B70;
constexpr uintptr_t VaDifficulty = 0x43ADF0;
constexpr uintptr_t VaItemName = 0x479E40;
constexpr uintptr_t VaExperienceTable = 0x97E850;
constexpr uintptr_t VaUiVars = 0x798E00;
constexpr int UiVarCount = 38;

int SehStat(uintptr_t fn, uintptr_t target, int statId, int layer = 0)
{
    __try
    {
        return reinterpret_cast<UnitStat_t>(fn)(reinterpret_cast<void *>(target), statId, layer);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

int SehDifficulty(uintptr_t fn)
{
    __try
    {
        return reinterpret_cast<Difficulty_t>(fn)();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

bool SehItemName(uintptr_t fn, uintptr_t item, wchar_t *buffer, int size)
{
    __try
    {
        reinterpret_cast<ItemName_t>(fn)(reinterpret_cast<void *>(item), buffer, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

void PutU16(std::vector<std::uint8_t> &packet, std::uint32_t value)
{
    packet.push_back(static_cast<std::uint8_t>(value & 0xFF));
    packet.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
}

void PutU32(std::vector<std::uint8_t> &packet, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        packet.push_back(static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF));
}

bool SendDwords(std::uint8_t id, std::initializer_list<std::uint32_t> values)
{
    std::vector<std::uint8_t> packet{id};
    for (std::uint32_t value : values)
        PutU32(packet, value);
    return SendPacket(packet.data(), static_cast<int>(packet.size()));
}

} // namespace

int UnitStat(uintptr_t unit, int statId)
{
    if (unit == 0)
        return 0;
    return SehStat(Absolute(VaUnitStat), unit, statId);
}

int UnitStatLayer(uintptr_t unit, int statId, int layer)
{
    if (unit == 0)
        return 0;
    return SehStat(Absolute(VaUnitStat), unit, statId, layer);
}

int UnitBaseStat(uintptr_t unit, int statId)
{
    const uintptr_t statList = ReadPtr(unit + 0x5C);
    if (statList == 0)
        return 0;
    return SehStat(Absolute(VaStatListBaseStat), statList, statId);
}

int Difficulty()
{
    const int difficulty = SehDifficulty(Absolute(VaDifficulty));
    return difficulty >= 0 && difficulty <= 2 ? difficulty : 0;
}

long long ExperienceForLevel(std::uint32_t classId, int level)
{
    // experience.txt: 32 byte rows, one dword per class (Amazon .. Assassin)
    // followed by ExpRatio. Row 0 is MaxLvl; row n is the experience needed to
    // reach level n (row 2 = 500), verified in game.
    const uintptr_t table = ReadPtr(Absolute(VaExperienceTable));
    if (table == 0 || classId > 6 || level < 1 || level > 99)
        return -1;
    std::uint32_t value = 0;
    if (!Read(table + static_cast<uintptr_t>(level) * 32 + classId * 4, value))
        return -1;
    return value;
}

uintptr_t UnitInventory(uintptr_t unit)
{
    const uintptr_t inventory = ReadPtr(unit + 0x60);
    std::uint32_t signature = 0;
    return inventory != 0 && Read(inventory, signature) && signature == 0x01020304 ? inventory : 0;
}

uintptr_t InventoryCursorItem(uintptr_t inventory)
{
    return inventory != 0 ? ReadPtr(inventory + 0x20) : 0;
}

bool ReadItemEntry(uintptr_t item, ItemEntry &entry)
{
    entry = ItemEntry{};
    std::uint32_t type = 0;
    if (!Read(item, type) || type != static_cast<std::uint32_t>(UnitType::Item))
        return false;
    const uintptr_t data = ReadPtr(item + off::UnitData);
    const uintptr_t path = ReadPtr(item + off::UnitPath);
    if (data == 0)
        return false;

    entry.unit = item;
    Read(item + off::UnitClassId, entry.classId);
    Read(item + off::UnitId, entry.unitId);
    Read(data + 0x44, entry.bodyLocation);
    Read(data + 0x45, entry.page);
    Read(data + 0x69, entry.node);
    if (path != 0)
    {
        std::int32_t x = 0;
        std::int32_t y = 0;
        Read(path + off::StaticPathX, x);
        Read(path + off::StaticPathY, y);
        entry.x = x;
        entry.y = y;
    }
    return true;
}

std::vector<ItemEntry> InventoryItems(uintptr_t inventory)
{
    std::vector<ItemEntry> items;
    uintptr_t item = inventory != 0 ? ReadPtr(inventory + 0x0C) : 0;
    for (int i = 0; i < 512 && item != 0; ++i)
    {
        ItemEntry entry;
        if (ReadItemEntry(item, entry))
            items.push_back(entry);
        const uintptr_t data = ReadPtr(item + off::UnitData);
        const uintptr_t next = data != 0 ? ReadPtr(data + 0x64) : 0;
        if (next == item)
            break;
        item = next;
    }
    return items;
}

std::wstring ItemFullName(uintptr_t item)
{
    std::array<wchar_t, 256> buffer{};
    if (item == 0 || !SehItemName(Absolute(VaItemName), item, buffer.data(), static_cast<int>(buffer.size())))
        return {};
    buffer.back() = L'\0';
    return std::wstring(buffer.data());
}

bool IsUiPanelOpen(int index)
{
    std::uint32_t value = 0;
    if (index < 0 || index >= UiVarCount)
        return false;
    return Read(Absolute(VaUiVars) + static_cast<uintptr_t>(index) * 4, value) && value != 0;
}

bool SendPickItemFromStorage(std::uint32_t itemId)
{
    return SendDwords(0x19, {itemId});
}

bool SendPlaceItemInStorage(std::uint32_t itemId, int x, int y, int page)
{
    return SendDwords(0x18, {itemId, static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y),
                             static_cast<std::uint32_t>(page)});
}

bool SendEquipCursorItem(std::uint32_t itemId, int bodyLocation)
{
    return SendDwords(0x1A, {itemId, static_cast<std::uint32_t>(bodyLocation)});
}

bool SendSwapEquippedItem(std::uint32_t itemId, int bodyLocation)
{
    return SendDwords(0x1D, {itemId, static_cast<std::uint32_t>(bodyLocation)});
}

bool SendPickEquippedItem(int bodyLocation)
{
    std::vector<std::uint8_t> packet{0x1C};
    PutU16(packet, static_cast<std::uint32_t>(bodyLocation));
    return SendPacket(packet.data(), static_cast<int>(packet.size()));
}

bool SendUseStorageItem(std::uint32_t itemId, Point playerPosition)
{
    return SendDwords(0x20, {itemId, static_cast<std::uint32_t>(playerPosition.x),
                             static_cast<std::uint32_t>(playerPosition.y)});
}

bool SendUseBeltItem(std::uint32_t itemId)
{
    return SendDwords(0x26, {itemId, 0, 0});
}

bool SendPickBeltItem(std::uint32_t itemId)
{
    return SendDwords(0x24, {itemId});
}

bool SendPlaceItemInBelt(std::uint32_t itemId, int slot)
{
    return SendDwords(0x23, {itemId, static_cast<std::uint32_t>(slot)});
}

bool SendDropCursorItem(std::uint32_t itemId)
{
    return SendDwords(0x17, {itemId});
}

bool SendAddStatPoint(int statId)
{
    std::vector<std::uint8_t> packet{0x3A};
    PutU16(packet, static_cast<std::uint32_t>(statId));
    return SendPacket(packet.data(), static_cast<int>(packet.size()));
}

namespace {

using StringById_t = const wchar_t *(__fastcall *)(int);
using IsExpansion_t = int(__cdecl *)();

constexpr uintptr_t VaStringById = 0x5207C0;
constexpr uintptr_t VaIsExpansion = 0x406290;
constexpr uintptr_t VaDataTables = 0x73D020;
constexpr size_t DataTablesSkillDesc = 0xB8C;
constexpr size_t DataTablesSkillDescCount = 0xB94;
constexpr size_t DataTablesSkills = 0xB98;
constexpr size_t DataTablesSkillsCount = 0xBA0;
constexpr size_t DataTablesClassSkillCounts = 0xBA4;
constexpr size_t DataTablesHighestClassSkillCount = 0xBA8;
constexpr size_t DataTablesClassSkillList = 0xBAC;
constexpr size_t SkillsTxtRecordSize = 0x23C;
constexpr size_t SkillDescRecordSize = 0x120;
constexpr size_t UnitSkills = 0xA8;

const wchar_t *SehStringById(uintptr_t fn, int stringId)
{
    __try
    {
        return reinterpret_cast<StringById_t>(fn)(stringId);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
}

int SehIsExpansion(uintptr_t fn)
{
    __try
    {
        return reinterpret_cast<IsExpansion_t>(fn)();
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

uintptr_t DataTables()
{
    return ReadPtr(Absolute(VaDataTables));
}

uintptr_t SkillsTxtRecord(int skillId)
{
    const uintptr_t tables = DataTables();
    std::int32_t count = 0;
    if (tables == 0 || skillId < 0 || !Read(tables + DataTablesSkillsCount, count) || skillId >= count)
        return 0;
    const uintptr_t base = ReadPtr(tables + DataTablesSkills);
    return base != 0 ? base + static_cast<uintptr_t>(skillId) * SkillsTxtRecordSize : 0;
}

uintptr_t SkillDescRecord(int descId)
{
    const uintptr_t tables = DataTables();
    std::int32_t count = 0;
    if (tables == 0 || descId < 0 || !Read(tables + DataTablesSkillDescCount, count) || descId >= count)
        return 0;
    const uintptr_t base = ReadPtr(tables + DataTablesSkillDesc);
    return base != 0 ? base + static_cast<uintptr_t>(descId) * SkillDescRecordSize : 0;
}

} // namespace

std::wstring StringById(int stringId)
{
    if (stringId <= 0 || stringId > 0xFFFF)
        return {};
    const wchar_t *text = SehStringById(Absolute(VaStringById), stringId);
    return text != nullptr ? ReadWideString(reinterpret_cast<uintptr_t>(text), 256) : std::wstring();
}

bool IsExpansion()
{
    return SehIsExpansion(Absolute(VaIsExpansion)) != 0;
}

bool ReadSkillDefinition(int skillId, SkillDefinition &skill)
{
    skill = SkillDefinition{};
    const uintptr_t txt = SkillsTxtRecord(skillId);
    std::int16_t id = -1;
    if (txt == 0 || !Read(txt, id) || id != skillId)
        return false;

    std::int8_t classId = -1;
    std::uint16_t requiredLevel = 0;
    std::array<std::int16_t, 3> requiredSkills{-1, -1, -1};
    std::uint16_t descId = 0;
    std::uint8_t elementType = 0;
    Read(txt + 0x0C, classId);
    Read(txt + 0x174, requiredLevel);
    Read(txt + 0x17E, requiredSkills);
    Read(txt + 0x194, descId);
    Read(txt + 0x1DC, elementType);

    // skills.txt +0x04 holds the flag word with passive, aura and the rest;
    // 0x499B30 refuses to select a skill when bit 0x10 is set.
    std::uint16_t flags = 0;
    Read(txt + 0x04, flags);
    skill.selectable = (flags & 0x10) == 0;

    skill.id = skillId;
    skill.classId = classId;
    skill.requiredLevel = requiredLevel;
    for (size_t i = 0; i < requiredSkills.size(); ++i)
        skill.requiredSkills[i] = requiredSkills[i];
    skill.elementType = elementType;

    const uintptr_t desc = SkillDescRecord(descId);
    if (desc != 0)
    {
        std::uint8_t page = 0;
        std::uint8_t row = 0;
        std::uint8_t column = 0;
        std::uint16_t nameId = 0;
        std::uint16_t shortId = 0;
        Read(desc + 0x02, page);
        Read(desc + 0x03, row);
        Read(desc + 0x04, column);
        Read(desc + 0x08, nameId);
        Read(desc + 0x0A, shortId);
        skill.page = page;
        skill.row = row;
        skill.column = column;
        skill.nameStringId = nameId;
        skill.shortStringId = shortId;
    }
    return true;
}

std::vector<int> ClassSkillIds(std::uint32_t classId)
{
    std::vector<int> ids;
    const uintptr_t tables = DataTables();
    if (tables == 0 || classId > 6)
        return ids;
    const uintptr_t counts = ReadPtr(tables + DataTablesClassSkillCounts);
    const uintptr_t list = ReadPtr(tables + DataTablesClassSkillList);
    std::int32_t highest = 0;
    std::int32_t count = 0;
    if (counts == 0 || list == 0 || !Read(tables + DataTablesHighestClassSkillCount, highest) ||
        !Read(counts + classId * 4, count) || highest <= 0 || count <= 0 || count > highest)
        return ids;
    for (std::int32_t i = 0; i < count; ++i)
    {
        std::int16_t id = -1;
        if (Read(list + (static_cast<uintptr_t>(highest) * classId + static_cast<uintptr_t>(i)) * 2, id) && id >= 0)
            ids.push_back(id);
    }
    return ids;
}

int PlayerSkillPoints(uintptr_t unit, int skillId)
{
    const uintptr_t skills = unit != 0 ? ReadPtr(unit + UnitSkills) : 0;
    uintptr_t skill = skills != 0 ? ReadPtr(skills + 0x04) : 0;
    for (int i = 0; i < 512 && skill != 0; ++i)
    {
        const uintptr_t txt = ReadPtr(skill);
        std::int16_t id = -1;
        if (txt != 0 && Read(txt, id) && id == skillId)
        {
            std::int32_t level = 0;
            Read(skill + 0x28, level);
            return level;
        }
        const uintptr_t next = ReadPtr(skill + 0x04);
        if (next == skill)
            break;
        skill = next;
    }
    return 0;
}

int SkillBonusLevels(uintptr_t unit, const SkillDefinition &skill)
{
    constexpr int StatAllSkills = 127;
    constexpr int StatClassSkills = 83;
    constexpr int StatTabSkills = 188;
    constexpr int StatNonClassSkill = 97;
    constexpr int StatElementalSkills = 126;
    constexpr int StatSingleSkill = 107;

    std::uint32_t unitType = 0;
    std::uint32_t classId = 0;
    if (unit == 0 || skill.id < 0 || !Read(unit + off::UnitType, unitType) || !Read(unit + off::UnitClassId, classId))
        return 0;

    int bonus = UnitStatLayer(unit, StatAllSkills, 0);
    if (unitType == static_cast<std::uint32_t>(UnitType::Player) && skill.classId == static_cast<int>(classId))
    {
        bonus += UnitStatLayer(unit, StatClassSkills, static_cast<int>(classId));
        if (skill.page > 0)
            bonus += UnitStatLayer(unit, StatTabSkills, skill.page + 8 * static_cast<int>(classId) - 1);
    }
    bonus += std::min(UnitStatLayer(unit, StatNonClassSkill, skill.id), 3);
    if (skill.elementType != 0)
        bonus += UnitStatLayer(unit, StatElementalSkills, skill.elementType);
    return bonus + UnitStatLayer(unit, StatSingleSkill, skill.id);
}

namespace {

constexpr uintptr_t VaMouseX = 0x79D0F0;
constexpr uintptr_t VaMouseY = 0x79D0EC;
constexpr uintptr_t VaScreenWidth = 0x70CD40;
constexpr uintptr_t VaScreenHeight = 0x70CD44;

BOOL CALLBACK FindGameWindowProc(HWND window, LPARAM result)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(window) || GetWindow(window, GW_OWNER) != nullptr)
        return TRUE;
    *reinterpret_cast<HWND *>(result) = window;
    return FALSE;
}

} // namespace

HWND GameWindow()
{
    HWND window = nullptr;
    EnumWindows(FindGameWindowProc, reinterpret_cast<LPARAM>(&window));
    return window;
}

bool ReadGameMouse(std::int32_t &x, std::int32_t &y)
{
    return Read(Absolute(VaMouseX), x) && Read(Absolute(VaMouseY), y);
}

bool PostGameMouseMove(std::int32_t x, std::int32_t y)
{
    const HWND window = GameWindow();
    if (window == nullptr)
        return false;

    std::int32_t width = 0;
    std::int32_t height = 0;
    RECT client{};
    if (Read(Absolute(VaScreenWidth), width) && Read(Absolute(VaScreenHeight), height) && width > 0 && height > 0 &&
        GetClientRect(window, &client))
    {
        const LONG clientWidth = client.right - client.left;
        const LONG clientHeight = client.bottom - client.top;
        if (clientWidth > 0 && clientHeight > 0 && (clientWidth != width || clientHeight != height))
        {
            x = x * clientWidth / width;
            y = y * clientHeight / height;
        }
    }
    return PostMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(x, y)) != FALSE;
}

bool WriteBytes(uintptr_t address, const void *data, size_t size)
{
    if (address == 0 || data == nullptr || size == 0)
        return false;
    return SehMemcpy(reinterpret_cast<void *>(address), data, size);
}

namespace {

using ItemIsType_t = int(__stdcall *)(void *, int);
constexpr uintptr_t VaItemIsType = 0x626320;

int SehItemIsType(uintptr_t fn, uintptr_t item, int itemType)
{
    __try
    {
        return reinterpret_cast<ItemIsType_t>(fn)(reinterpret_cast<void *>(item), itemType);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return 0;
    }
}

} // namespace

bool ItemIsType(uintptr_t item, int itemType)
{
    return item != 0 && SehItemIsType(Absolute(VaItemIsType), item, itemType) != 0;
}

bool ItemBodyLocations(uintptr_t item, int &primary, int &secondary)
{
    // items.txt (record 424 bytes, table at 0x97EA04, count at 0x97EA00) points
    // at its itemtypes.txt row with a word at +286 ("type"); that row (228
    // bytes, table at 0x97E7D0, count at 0x97E7D4) holds bodyloc1 at +10 and
    // bodyloc2 at +11. 0xFF means the type cannot be worn.
    constexpr uintptr_t VaItemsTable = 0x97EA04;
    constexpr uintptr_t VaItemsCount = 0x97EA00;
    constexpr uintptr_t VaItemTypesTable = 0x97E7D0;
    constexpr uintptr_t VaItemTypesCount = 0x97E7D4;
    constexpr uintptr_t ItemsRecordSize = 424;
    constexpr uintptr_t ItemTypesRecordSize = 228;

    primary = 0;
    secondary = 0;

    std::uint32_t classId = 0;
    std::int32_t itemCount = 0;
    const uintptr_t itemTable = ReadPtr(Absolute(VaItemsTable));
    if (item == 0 || !Read(item + off::UnitClassId, classId) || itemTable == 0 ||
        !Read(Absolute(VaItemsCount), itemCount) || itemCount <= 0 ||
        classId >= static_cast<std::uint32_t>(itemCount))
        return false;

    std::uint16_t typeIndex = 0;
    constexpr uintptr_t ItemsOffset_Type = 286;
    if (!Read(itemTable + static_cast<uintptr_t>(classId) * ItemsRecordSize + ItemsOffset_Type, typeIndex))
        return false;

    std::int32_t typeCount = 0;
    const uintptr_t typeTable = ReadPtr(Absolute(VaItemTypesTable));
    if (typeTable == 0 || !Read(Absolute(VaItemTypesCount), typeCount) || typeCount <= 0 ||
        typeIndex >= static_cast<std::uint16_t>(typeCount))
        return false;

    std::uint8_t first = 0xFF;
    std::uint8_t second = 0xFF;
    const uintptr_t typeRecord = typeTable + static_cast<uintptr_t>(typeIndex) * ItemTypesRecordSize;
    Read(typeRecord + 10, first);
    Read(typeRecord + 11, second);
    primary = first == 0xFF ? 0 : first;
    secondary = second == 0xFF ? 0 : second;
    return primary != 0 || secondary != 0;
}

bool SendAddSkillPoint(int skillId)
{
    std::vector<std::uint8_t> packet{0x3B};
    PutU16(packet, static_cast<std::uint32_t>(skillId));
    return SendPacket(packet.data(), static_cast<int>(packet.size()));
}

bool SendSelectSkill(int skillId, bool leftHand)
{
    // 0x465FA0(0x3C, skill | 0x80000000 for the left hand, item or -1).
    constexpr std::uint32_t LeftHandFlag = 0x80000000;
    constexpr std::uint32_t NoItem = 0xFFFFFFFF;
    if (skillId < 0)
        return false;
    const std::uint32_t hand = static_cast<std::uint32_t>(skillId) | (leftHand ? LeftHandFlag : 0);
    return SendDwords(0x3C, {hand, NoItem});
}

int SelectedSkillId(uintptr_t unit, bool leftHand)
{
    constexpr uintptr_t SkillListLeft = 0x08;
    constexpr uintptr_t SkillListRight = 0x0C;
    const uintptr_t skills = unit != 0 ? ReadPtr(unit + UnitSkills) : 0;
    const uintptr_t skill = skills != 0 ? ReadPtr(skills + (leftHand ? SkillListLeft : SkillListRight)) : 0;
    if (skill == 0)
        return 0;

    const uintptr_t txt = ReadPtr(skill);
    std::int16_t id = -1;
    return txt != 0 && Read(txt, id) ? id : -1;
}

bool SendPickupItem(std::uint32_t unitId)
{
    std::array<std::uint8_t, 13> packet{};
    const std::uint32_t itemType = static_cast<std::uint32_t>(UnitType::Item);
    const std::uint32_t action = 0;
    packet[0] = 0x16;
    std::memcpy(packet.data() + 1, &itemType, 4);
    std::memcpy(packet.data() + 5, &unitId, 4);
    std::memcpy(packet.data() + 9, &action, 4);
    return SendPacket(packet.data(), static_cast<int>(packet.size()));
}

} // namespace d2access::game
