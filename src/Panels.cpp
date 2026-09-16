#include "Panels.hpp"

#include "AudioCue.hpp"
#include "Localization.hpp"
#include "Logging.hpp"
#include "ScreenReader.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <string>
#include <vector>

namespace d2access::panels {

namespace {

constexpr int UiInventory = 1;
constexpr int UiCharacter = 2;
constexpr int GridBody = 0;
constexpr int GridBelt = 1;
constexpr int GridInventory = 2;
constexpr int BeltColumns = 4;
constexpr int BeltSlots = 16;
constexpr std::uint32_t ItemClassGold = 523;
constexpr DWORD64 StatSendIntervalMs = 260;
constexpr DWORD64 StatAnnounceDelayMs = 450;
constexpr DWORD64 ItemActionTimeoutMs = 1500;

// Item descriptions: the game builds the full hover text (0x47BC70) only for
// the item under its mouse cursor and copies it to 0x874978 (0x4F5B90). The
// panel layout it hit-tests against is cached by 0x4710E0.
constexpr uintptr_t VaHoverText = 0x874978;
constexpr size_t HoverTextChars = 0x400;
constexpr uintptr_t VaHoveredItem = 0x7B3234;
constexpr uintptr_t VaLayoutReady = 0x7B321C;
constexpr uintptr_t VaInventoryGrid = 0x7B31C8; // grid info, 6 dwords
// Body location rects (left, right, top, bottom, size) in inventory.txt
// column order: right arm, torso, left arm, head, neck, right hand, left hand,
// belt, feet, gloves.
constexpr std::array<uintptr_t, 10> VaBodyRects = {
    0x7B32E8, 0x7B32D4, 0x7B32FC, 0x7B32AC, 0x7B32C0, 0x7B3310, 0x7B3324, 0x7B3338, 0x7B334C, 0x7B3360,
};
constexpr DWORD64 DescriptionTimeoutMs = 800;

enum class Focus : std::uint8_t {
    None,
    Character,
    Inventory,
};

enum class InventoryArea : std::uint8_t {
    Equipment,
    Grid,
    Belt,
};

enum class CharField : std::uint8_t {
    NameAndClass,
    Level,
    Experience,
    NextLevel,
    Strength,
    Dexterity,
    Vitality,
    Energy,
    StatPoints,
    SkillPoints,
    Gold,
    Defense,
    Life,
    Mana,
    Stamina,
    FireResist,
    ColdResist,
    LightningResist,
    PoisonResist,
    Count,
};

constexpr size_t CharFieldCount = static_cast<size_t>(CharField::Count);

struct EquipmentSlot {
    int bodyLocation;
    const wchar_t *polish;
    const wchar_t *english;
};

constexpr std::array<EquipmentSlot, 10> EquipmentSlots = {
    EquipmentSlot{game::bodyloc::Head, L"Hełm", L"Helm"},
    EquipmentSlot{game::bodyloc::Neck, L"Amulet", L"Amulet"},
    EquipmentSlot{game::bodyloc::Torso, L"Zbroja", L"Armor"},
    EquipmentSlot{game::bodyloc::RightHand, L"Prawa ręka", L"Right hand"},
    EquipmentSlot{game::bodyloc::LeftHand, L"Lewa ręka", L"Left hand"},
    EquipmentSlot{game::bodyloc::RightRing, L"Prawy pierścień", L"Right ring"},
    EquipmentSlot{game::bodyloc::LeftRing, L"Lewy pierścień", L"Left ring"},
    EquipmentSlot{game::bodyloc::Belt, L"Pas", L"Belt"},
    EquipmentSlot{game::bodyloc::Feet, L"Buty", L"Boots"},
    EquipmentSlot{game::bodyloc::Gloves, L"Rękawice", L"Gloves"},
};

enum class ItemAction : std::uint8_t {
    None,
    Move,
    Use,
    EquipPick,  // waiting for the item to reach the cursor
    EquipPlace, // waiting for the game to put it on the body
};

struct State {
    Focus focus = Focus::None;
    bool inventoryWasOpen = false;
    bool characterWasOpen = false;

    size_t charField = 0;

    InventoryArea area = InventoryArea::Grid;
    int gridColumn = 0;
    int gridRow = 0;
    size_t equipmentIndex = 0;
    int beltSlot = 0;

    // Shift+Enter equipping: where the item came from and where it goes.
    int equipBodyLocation = 0;
    std::uint32_t equipItemId = 0;
    int equipColumn = 0;
    int equipRow = 0;

    int pendingStat = -1;
    int pendingStatCount = 0;
    DWORD64 lastStatSend = 0;
    bool announceStat = false;

    ItemAction itemAction = ItemAction::None;
    std::uint32_t cursorItemBefore = 0;
    uintptr_t usedItem = 0;
    std::wstring usedItemName;
    DWORD64 itemActionTick = 0;

    bool describing = false;
    uintptr_t describeItem = 0;
    DWORD64 describeTick = 0;
    std::int32_t savedMouseX = 0;
    std::int32_t savedMouseY = 0;
};

State g_state;
std::atomic<bool> g_panelOpenForKeys = false;

void Say(const std::wstring &text)
{
    LogLine(L"Panels: " + text);
    Speak(text, true);
}

// ---------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------

std::wstring CleanGameText(const std::wstring &text, const wchar_t *separator = L", ")
{
    // Drop colour codes ("ÿc" + one char) and grammar tags ("[fs]"), and join
    // the lines of multi-line names. The game stores the top line last.
    std::vector<std::wstring> lines{std::wstring()};
    for (size_t i = 0; i < text.size(); ++i)
    {
        const wchar_t ch = text[i];
        if (ch == 0x00FF && i + 2 < text.size() && text[i + 1] == L'c')
        {
            i += 2;
            continue;
        }
        if (ch == L'[')
        {
            const size_t end = text.find(L']', i);
            if (end != std::wstring::npos && end - i >= 2 && end - i <= 4 &&
                std::all_of(text.begin() + static_cast<std::ptrdiff_t>(i + 1),
                            text.begin() + static_cast<std::ptrdiff_t>(end),
                            [](wchar_t c) { return c >= L'a' && c <= L'z'; }))
            {
                i = end;
                continue;
            }
        }
        if (ch == L'\n' || ch == L'\r')
        {
            if (!lines.back().empty())
                lines.emplace_back();
            continue;
        }
        if (ch >= 32)
            lines.back().push_back(ch);
    }

    std::wstring joined;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it)
    {
        std::wstring line = *it;
        while (!line.empty() && line.back() == L' ')
            line.pop_back();
        while (!line.empty() && line.front() == L' ')
            line.erase(line.begin());
        if (line.empty())
            continue;
        if (!joined.empty())
            joined += separator;
        joined += line;
    }
    return joined;
}

std::wstring ItemLabel(uintptr_t item)
{
    std::uint32_t classId = 0;
    game::Read(item + game::off::UnitClassId, classId);
    if (classId == ItemClassGold)
        return TrS(L"Złoto: ", L"Gold: ") + std::to_wstring(game::UnitStat(item, game::stat::Gold));

    std::wstring name = CleanGameText(game::ItemFullName(item));
    if (name.empty())
        name = CleanGameText(game::UnitName(item));
    return name.empty() ? TrS(L"przedmiot", L"item") : name;
}

const wchar_t *ClassName(std::uint32_t classId)
{
    static constexpr std::array<const wchar_t *, 7> Polish = {
        L"Amazonka", L"Czarodziejka", L"Nekromanta", L"Paladyn", L"Barbarzyńca", L"Druid", L"Zabójczyni",
    };
    static constexpr std::array<const wchar_t *, 7> English = {
        L"Amazon", L"Sorceress", L"Necromancer", L"Paladin", L"Barbarian", L"Druid", L"Assassin",
    };
    return classId < Polish.size() ? Tr(Polish[classId], English[classId]) : Tr(L"nieznana klasa", L"unknown class");
}

int Percent(long long current, long long maximum)
{
    if (maximum <= 0)
        return 0;
    const long long clamped = std::max<long long>(current, 0);
    return static_cast<int>(std::clamp<long long>((clamped * 100 + maximum / 2) / maximum, 0, 100));
}

// ---------------------------------------------------------------------------
// Character sheet
// ---------------------------------------------------------------------------

int AttributeStatForField(CharField field)
{
    switch (field)
    {
    case CharField::Strength:
        return game::stat::Strength;
    case CharField::Dexterity:
        return game::stat::Dexterity;
    case CharField::Vitality:
        return game::stat::Vitality;
    case CharField::Energy:
        return game::stat::Energy;
    default:
        return -1;
    }
}

std::wstring AttributeText(uintptr_t player, const wchar_t *label, int statId)
{
    return std::wstring(label) + Tr(L": bazowa ", L": base ") + std::to_wstring(game::UnitBaseStat(player, statId)) +
           Tr(L", obecna ", L", current ") + std::to_wstring(game::UnitStat(player, statId));
}

std::wstring ResistText(uintptr_t player, const wchar_t *label, int statId, int maxStatId)
{
    static constexpr std::array<int, 3> Penalty = {0, 40, 100};
    const int value = game::UnitStat(player, statId) - Penalty[static_cast<size_t>(game::Difficulty())];
    const int cap = 75 + game::UnitStat(player, maxStatId);
    return std::wstring(label) + L": " + std::to_wstring(std::min(value, cap)) + L"%";
}

std::wstring PoolText(uintptr_t player, const wchar_t *label, int statId, int maxStatId)
{
    return std::wstring(label) + L": " + std::to_wstring(game::UnitStat(player, statId) >> 8) + Tr(L" z ", L" of ") +
           std::to_wstring(game::UnitStat(player, maxStatId) >> 8);
}

std::wstring CharFieldText(uintptr_t player, CharField field)
{
    std::uint32_t classId = 0;
    game::Read(player + game::off::UnitClassId, classId);
    const int level = game::UnitStat(player, game::stat::Level);

    switch (field)
    {
    case CharField::NameAndClass:
        return game::PlayerName(player) + L", " + ClassName(classId);
    case CharField::Level:
        return TrS(L"Poziom: ", L"Level: ") + std::to_wstring(level);
    case CharField::Experience:
        return TrS(L"Doświadczenie: ", L"Experience: ") +
               std::to_wstring(static_cast<std::uint32_t>(game::UnitStat(player, game::stat::Experience)));
    case CharField::NextLevel:
    {
        const long long next = game::ExperienceForLevel(classId, level + 1);
        if (level >= 99 || next < 0)
            return Tr(L"Następny poziom: maksymalny poziom osiągnięty", L"Next level: maximum level reached");
        return TrS(L"Następny poziom: ", L"Next level: ") + std::to_wstring(next);
    }
    case CharField::Strength:
        return AttributeText(player, Tr(L"Siła", L"Strength"), game::stat::Strength);
    case CharField::Dexterity:
        return AttributeText(player, Tr(L"Zręczność", L"Dexterity"), game::stat::Dexterity);
    case CharField::Vitality:
        return AttributeText(player, Tr(L"Żywotność", L"Vitality"), game::stat::Vitality);
    case CharField::Energy:
        return AttributeText(player, Tr(L"Energia", L"Energy"), game::stat::Energy);
    case CharField::StatPoints:
        return TrS(L"Punkty atrybutów do rozdania: ", L"Stat points to spend: ") +
               std::to_wstring(game::UnitStat(player, game::stat::StatPoints));
    case CharField::SkillPoints:
        return TrS(L"Punkty umiejętności: ", L"Skill points: ") +
               std::to_wstring(game::UnitStat(player, game::stat::SkillPoints));
    case CharField::Gold:
        return TrS(L"Złoto: ", L"Gold: ") + std::to_wstring(game::UnitStat(player, game::stat::Gold)) +
               Tr(L", w skrytce: ", L", in stash: ") + std::to_wstring(game::UnitStat(player, game::stat::GoldBank));
    case CharField::Defense:
        return TrS(L"Obrona: ", L"Defense: ") + std::to_wstring(game::UnitStat(player, game::stat::Defense) +
                                                                game::UnitStat(player, game::stat::Dexterity) / 4);
    case CharField::Life:
        return PoolText(player, Tr(L"Życie", L"Life"), game::stat::HitPoints, game::stat::MaxHitPoints);
    case CharField::Mana:
        return PoolText(player, Tr(L"Mana", L"Mana"), game::stat::Mana, game::stat::MaxMana);
    case CharField::Stamina:
        return PoolText(player, Tr(L"Wytrzymałość", L"Stamina"), game::stat::Stamina, game::stat::MaxStamina);
    case CharField::FireResist:
        return ResistText(player, Tr(L"Odporność na ogień", L"Fire resistance"), game::stat::FireResist,
                          game::stat::MaxFireResist);
    case CharField::ColdResist:
        return ResistText(player, Tr(L"Odporność na zimno", L"Cold resistance"), game::stat::ColdResist,
                          game::stat::MaxColdResist);
    case CharField::LightningResist:
        return ResistText(player, Tr(L"Odporność na błyskawice", L"Lightning resistance"),
                          game::stat::LightningResist, game::stat::MaxLightningResist);
    case CharField::PoisonResist:
        return ResistText(player, Tr(L"Odporność na truciznę", L"Poison resistance"), game::stat::PoisonResist,
                          game::stat::MaxPoisonResist);
    case CharField::Count:
        break;
    }
    return {};
}

std::wstring CurrentCharFieldText(uintptr_t player)
{
    return CharFieldText(player, static_cast<CharField>(g_state.charField));
}

void OpenCharacterSheet(uintptr_t player)
{
    // Like Diablo Access: start on the first attribute when points are waiting.
    g_state.charField = game::UnitStat(player, game::stat::StatPoints) > 0
                            ? static_cast<size_t>(CharField::Strength)
                            : static_cast<size_t>(CharField::NameAndClass);
    Say(TrS(L"Karta postaci. ", L"Character sheet. ") +CurrentCharFieldText(player));
}

void CharacterMove(uintptr_t player, int delta)
{
    const int count = static_cast<int>(CharFieldCount);
    int index = static_cast<int>(g_state.charField) + delta;
    index = ((index % count) + count) % count;
    g_state.charField = static_cast<size_t>(index);
    Say(CurrentCharFieldText(player));
}

void CharacterActivate(uintptr_t player, bool allPoints)
{
    const int statId = AttributeStatForField(static_cast<CharField>(g_state.charField));
    if (statId < 0)
    {
        Say(CurrentCharFieldText(player));
        return;
    }
    const int points = game::UnitStat(player, game::stat::StatPoints);
    if (points <= 0)
    {
        Say(Tr(L"Brak punktów do rozdania.", L"No stat points to spend."));
        return;
    }
    // Identical packets sent within 200 ms are dropped by the client, so the
    // points are queued and sent one by one.
    g_state.pendingStat = statId;
    g_state.pendingStatCount = allPoints ? points : 1;
    g_state.lastStatSend = 0;
}

void ProcessStatQueue(uintptr_t player)
{
    const DWORD64 now = GetTickCount64();
    if (g_state.pendingStatCount > 0 && now - g_state.lastStatSend >= StatSendIntervalMs)
    {
        if (game::UnitStat(player, game::stat::StatPoints) <= 0)
        {
            g_state.pendingStatCount = 0;
        }
        else if (game::SendAddStatPoint(g_state.pendingStat))
        {
            --g_state.pendingStatCount;
            g_state.lastStatSend = now;
            g_state.announceStat = true;
        }
        else
        {
            g_state.pendingStatCount = 0;
        }
    }

    if (g_state.pendingStatCount == 0 && g_state.announceStat && now - g_state.lastStatSend >= StatAnnounceDelayMs)
    {
        g_state.announceStat = false;
        Say(CurrentCharFieldText(player) + Tr(L". Punkty do rozdania: ", L". Stat points left: ") +
            std::to_wstring(game::UnitStat(player, game::stat::StatPoints)) + L".");
    }
}

// ---------------------------------------------------------------------------
// Inventory
// ---------------------------------------------------------------------------

struct GridView {
    int width = 0;
    int height = 0;
    std::vector<uintptr_t> cells;

    [[nodiscard]] uintptr_t At(int column, int row) const
    {
        if (column < 0 || row < 0 || column >= width || row >= height)
            return 0;
        return cells[static_cast<size_t>(row * width + column)];
    }
};

// Inventory+0x14 holds grids of 0x10 bytes: +8 width, +9 height, +0xC cell
// array with one item pointer per cell. Grid 0 is the body (indexed by body
// location), 1 the belt, 2 the inventory (10 x 4). Verified in game.
bool ReadGrid(uintptr_t inventory, int index, GridView &grid)
{
    grid = GridView{};
    const uintptr_t grids = game::ReadPtr(inventory + 0x14);
    std::int32_t count = 0;
    if (grids == 0 || !game::Read(inventory + 0x18, count) || index >= count)
        return false;

    const uintptr_t entry = grids + static_cast<uintptr_t>(index) * 0x10;
    std::uint8_t width = 0;
    std::uint8_t height = 0;
    if (!game::Read(entry + 8, width) || !game::Read(entry + 9, height) || width == 0 || height == 0)
        return false;
    const uintptr_t cells = game::ReadPtr(entry + 0x0C);
    grid.width = width;
    grid.height = height;
    grid.cells.assign(static_cast<size_t>(width) * height, 0);
    if (cells == 0)
        return true;
    for (size_t i = 0; i < grid.cells.size(); ++i)
        grid.cells[i] = game::ReadPtr(cells + i * 4);
    return true;
}

std::wstring InventoryCellText(uintptr_t player)
{
    const uintptr_t inventory = game::UnitInventory(player);
    GridView grid;

    switch (g_state.area)
    {
    case InventoryArea::Grid:
    {
        ReadGrid(inventory, GridInventory, grid);
        const uintptr_t item = grid.At(g_state.gridColumn, g_state.gridRow);
        const std::wstring position = TrS(L"wiersz ", L"row ") + std::to_wstring(g_state.gridRow + 1) +
                                      Tr(L", kolumna ", L", column ") + std::to_wstring(g_state.gridColumn + 1) + L": ";
        return position + (item != 0 ? ItemLabel(item) : TrS(L"pusto", L"empty"));
    }
    case InventoryArea::Equipment:
    {
        const EquipmentSlot &slot = EquipmentSlots[g_state.equipmentIndex];
        ReadGrid(inventory, GridBody, grid);
        const uintptr_t item = grid.At(slot.bodyLocation, 0);
        return TrS(slot.polish, slot.english) + L": " + (item != 0 ? ItemLabel(item) : TrS(L"pusto", L"empty"));
    }
    case InventoryArea::Belt:
    {
        ReadGrid(inventory, GridBelt, grid);
        const uintptr_t item = grid.At(g_state.beltSlot, 0);
        return TrS(L"Pas, rząd ", L"Belt, row ") + std::to_wstring(g_state.beltSlot / BeltColumns + 1) +
               Tr(L", miejsce ", L", slot ") + std::to_wstring(g_state.beltSlot % BeltColumns + 1) + L": " +
               (item != 0 ? ItemLabel(item) : TrS(L"pusto", L"empty"));
    }
    }
    return {};
}

void SpeakInventoryCell(uintptr_t player, const std::wstring &prefix = {})
{
    Say(prefix + InventoryCellText(player));
}

void InventoryMoveGrid(uintptr_t player, int dx, int dy)
{
    GridView grid;
    if (!ReadGrid(game::UnitInventory(player), GridInventory, grid))
    {
        Say(Tr(L"Nie mogę odczytać ekwipunku.", L"Cannot read the inventory."));
        return;
    }

    // Step over the rest of a multi-cell item, as Diablo Access does.
    const uintptr_t current = grid.At(g_state.gridColumn, g_state.gridRow);
    int column = g_state.gridColumn;
    int row = g_state.gridRow;
    do
    {
        column += dx;
        row += dy;
    } while (current != 0 && column >= 0 && row >= 0 && column < grid.width && row < grid.height &&
             grid.At(column, row) == current);

    if (row < 0)
    {
        g_state.area = InventoryArea::Equipment;
        SpeakInventoryCell(player);
        return;
    }
    if (row >= grid.height)
    {
        g_state.area = InventoryArea::Belt;
        g_state.beltSlot = std::min(g_state.gridColumn, BeltColumns - 1);
        SpeakInventoryCell(player);
        return;
    }
    if (column < 0 || column >= grid.width)
    {
        SpeakInventoryCell(player);
        return;
    }
    g_state.gridColumn = column;
    g_state.gridRow = row;
    SpeakInventoryCell(player);
}

void InventoryMove(uintptr_t player, int dx, int dy)
{
    switch (g_state.area)
    {
    case InventoryArea::Grid:
        InventoryMoveGrid(player, dx, dy);
        return;
    case InventoryArea::Equipment:
    {
        if (dy > 0)
        {
            g_state.area = InventoryArea::Grid;
            g_state.gridRow = 0;
        }
        else if (dx != 0)
        {
            const int count = static_cast<int>(EquipmentSlots.size());
            int index = static_cast<int>(g_state.equipmentIndex) + dx;
            g_state.equipmentIndex = static_cast<size_t>(((index % count) + count) % count);
        }
        SpeakInventoryCell(player);
        return;
    }
    case InventoryArea::Belt:
    {
        int column = g_state.beltSlot % BeltColumns;
        int row = g_state.beltSlot / BeltColumns;
        if (dy < 0 && row == 0)
        {
            GridView grid;
            ReadGrid(game::UnitInventory(player), GridInventory, grid);
            g_state.area = InventoryArea::Grid;
            g_state.gridRow = std::max(0, grid.height - 1);
            g_state.gridColumn = column;
            SpeakInventoryCell(player);
            return;
        }
        column = std::clamp(column + dx, 0, BeltColumns - 1);
        row = std::clamp(row + dy, 0, BeltSlots / BeltColumns - 1);
        g_state.beltSlot = row * BeltColumns + column;
        SpeakInventoryCell(player);
        return;
    }
    }
}

std::uint32_t ItemId(uintptr_t item)
{
    std::uint32_t id = 0;
    if (item != 0)
        game::Read(item + game::off::UnitId, id);
    return id;
}

void StartItemAction(ItemAction action, uintptr_t cursorItem, uintptr_t usedItem)
{
    g_state.itemAction = action;
    g_state.cursorItemBefore = ItemId(cursorItem);
    g_state.usedItem = usedItem;
    g_state.usedItemName = usedItem != 0 ? ItemLabel(usedItem) : std::wstring();
    g_state.itemActionTick = GetTickCount64();
}

void InventoryActivate(uintptr_t player, game::Point playerPosition, bool use)
{
    const uintptr_t inventory = game::UnitInventory(player);
    const uintptr_t cursor = game::InventoryCursorItem(inventory);
    const std::uint32_t cursorId = ItemId(cursor);
    GridView grid;

    switch (g_state.area)
    {
    case InventoryArea::Grid:
    {
        ReadGrid(inventory, GridInventory, grid);
        const uintptr_t item = grid.At(g_state.gridColumn, g_state.gridRow);
        if (use)
        {
            if (item == 0)
            {
                Say(Tr(L"Pusto.", L"Empty."));
                return;
            }
            // Weapons and armor go the way the panel does it by hand: onto the
            // cursor first, then into the slot their type belongs to. The game
            // only drinks or reads items through the "use" packet.
            int primary = 0;
            int secondary = 0;
            if (game::ItemBodyLocations(item, primary, secondary))
            {
                if (cursor != 0)
                {
                    Say(Tr(L"Najpierw odłóż trzymany przedmiot.", L"Put down the held item first."));
                    return;
                }

                GridView body;
                ReadGrid(inventory, GridBody, body);
                int slot = primary != 0 ? primary : secondary;
                if (primary != 0 && secondary != 0 && body.At(primary, 0) != 0 && body.At(secondary, 0) == 0)
                    slot = secondary;
                if (slot == 0)
                {
                    Say(Tr(L"Nie można tego zrobić.", L"Cannot do that."));
                    return;
                }

                g_state.equipBodyLocation = slot;
                g_state.equipItemId = ItemId(item);
                g_state.equipColumn = g_state.gridColumn;
                g_state.equipRow = g_state.gridRow;
                game::SendPickItemFromStorage(ItemId(item));
                StartItemAction(ItemAction::EquipPick, cursor, item);
                return;
            }
            game::SendUseStorageItem(ItemId(item), playerPosition);
            StartItemAction(ItemAction::Use, cursor, item);
            return;
        }
        if (cursor != 0)
            game::SendPlaceItemInStorage(cursorId, g_state.gridColumn, g_state.gridRow, game::ItemPageInventory);
        else if (item != 0)
            game::SendPickItemFromStorage(ItemId(item));
        else
        {
            Say(Tr(L"Pusto.", L"Empty."));
            return;
        }
        StartItemAction(ItemAction::Move, cursor, 0);
        return;
    }
    case InventoryArea::Equipment:
    {
        const EquipmentSlot &slot = EquipmentSlots[g_state.equipmentIndex];
        ReadGrid(inventory, GridBody, grid);
        const uintptr_t item = grid.At(slot.bodyLocation, 0);
        if (use)
        {
            SpeakInventoryCell(player);
            return;
        }
        if (cursor != 0 && item != 0)
            game::SendSwapEquippedItem(cursorId, slot.bodyLocation);
        else if (cursor != 0)
            game::SendEquipCursorItem(cursorId, slot.bodyLocation);
        else if (item != 0)
            game::SendPickEquippedItem(slot.bodyLocation);
        else
        {
            Say(Tr(L"Pusto.", L"Empty."));
            return;
        }
        StartItemAction(ItemAction::Move, cursor, 0);
        return;
    }
    case InventoryArea::Belt:
    {
        ReadGrid(inventory, GridBelt, grid);
        const uintptr_t item = grid.At(g_state.beltSlot, 0);
        if (use)
        {
            if (item == 0)
            {
                Say(Tr(L"Pusto.", L"Empty."));
                return;
            }
            game::SendUseBeltItem(ItemId(item));
            StartItemAction(ItemAction::Use, cursor, item);
            return;
        }
        if (cursor != 0)
            game::SendPlaceItemInBelt(cursorId, g_state.beltSlot);
        else if (item != 0)
            game::SendPickBeltItem(ItemId(item));
        else
        {
            Say(Tr(L"Pusto.", L"Empty."));
            return;
        }
        StartItemAction(ItemAction::Move, cursor, 0);
        return;
    }
    }
}

bool ItemStillInInventory(uintptr_t inventory, uintptr_t item)
{
    for (const game::ItemEntry &entry : game::InventoryItems(inventory))
    {
        if (entry.unit == item)
            return true;
    }
    return false;
}

void ProcessItemAction(uintptr_t player)
{
    if (g_state.itemAction == ItemAction::None)
        return;

    const uintptr_t inventory = game::UnitInventory(player);
    const uintptr_t cursor = game::InventoryCursorItem(inventory);
    const std::uint32_t cursorId = ItemId(cursor);
    const bool timedOut = GetTickCount64() - g_state.itemActionTick > ItemActionTimeoutMs;

    if (g_state.itemAction == ItemAction::EquipPick)
    {
        if (cursor != 0 && cursorId == g_state.equipItemId)
        {
            game::SendEquipCursorItem(g_state.equipItemId, g_state.equipBodyLocation);
            g_state.itemAction = ItemAction::EquipPlace;
            g_state.itemActionTick = GetTickCount64();
        }
        else if (timedOut)
        {
            g_state.itemAction = ItemAction::None;
            Say(Tr(L"Nie można tego zrobić.", L"Cannot do that."));
        }
        return;
    }

    if (g_state.itemAction == ItemAction::EquipPlace)
    {
        game::ItemEntry worn;
        if (g_state.usedItem != 0 && game::ReadItemEntry(g_state.usedItem, worn) && worn.bodyLocation != 0)
        {
            g_state.itemAction = ItemAction::None;
            PlayCue(CueId::Item);
            Say(TrS(L"Założono: ", L"Equipped: ") + g_state.usedItemName + L".");
        }
        else if (timedOut)
        {
            g_state.itemAction = ItemAction::None;
            // The character does not meet the requirements, so the item stayed
            // on the cursor; put it back where it came from.
            if (cursor != 0 && cursorId == g_state.equipItemId)
                game::SendPlaceItemInStorage(g_state.equipItemId, g_state.equipColumn, g_state.equipRow,
                                             game::ItemPageInventory);
            Say(Tr(L"Nie można tego zrobić.", L"Cannot do that."));
        }
        return;
    }

    if (g_state.itemAction == ItemAction::Use)
    {
        // Packet 0x20 is what a right click sends: a potion is drunk, a weapon
        // or a piece of armor is equipped. Equipped items stay in the item list
        // but get a body location, so both outcomes can be told apart.
        game::ItemEntry entry;
        if (g_state.usedItem != 0 && game::ReadItemEntry(g_state.usedItem, entry) && entry.bodyLocation != 0)
        {
            g_state.itemAction = ItemAction::None;
            PlayCue(CueId::Item);
            Say(TrS(L"Założono: ", L"Equipped: ") + g_state.usedItemName + L".");
        }
        else if (!ItemStillInInventory(inventory, g_state.usedItem))
        {
            g_state.itemAction = ItemAction::None;
            PlayCue(CueId::Item);
            Say(TrS(L"Użyto: ", L"Used: ") + g_state.usedItemName + L".");
        }
        else if (timedOut)
        {
            g_state.itemAction = ItemAction::None;
            Say(Tr(L"Nie można tego zrobić.", L"Cannot do that."));
        }
        return;
    }

    if (cursorId != g_state.cursorItemBefore)
    {
        g_state.itemAction = ItemAction::None;
        PlayCue(CueId::Item);
        if (cursor != 0)
            Say(TrS(L"Trzymasz: ", L"Holding: ") + ItemLabel(cursor) +
                Tr(L". Wybierz pole i naciśnij Enter, aby odłożyć albo założyć.",
                   L". Choose a slot and press Enter to place or equip it."));
        else
            SpeakInventoryCell(player, g_state.area == InventoryArea::Equipment ? Tr(L"Założono. ", L"Equipped. ")
                                                                                 : Tr(L"Odłożono. ", L"Placed. "));
    }
    else if (timedOut)
    {
        g_state.itemAction = ItemAction::None;
        Say(Tr(L"Nie można tego zrobić.", L"Cannot do that."));
    }
}

// ---------------------------------------------------------------------------
// Item descriptions
// ---------------------------------------------------------------------------

int BodyRectIndex(int bodyLocation)
{
    switch (bodyLocation)
    {
    case game::bodyloc::RightHand:
        return 0;
    case game::bodyloc::Torso:
        return 1;
    case game::bodyloc::LeftHand:
        return 2;
    case game::bodyloc::Head:
        return 3;
    case game::bodyloc::Neck:
        return 4;
    case game::bodyloc::RightRing:
        return 5;
    case game::bodyloc::LeftRing:
        return 6;
    case game::bodyloc::Belt:
        return 7;
    case game::bodyloc::Feet:
        return 8;
    case game::bodyloc::Gloves:
        return 9;
    default:
        return -1;
    }
}

// Item in the selected slot and the screen point the game hit-tests for it.
bool SelectedSlotScreenPoint(uintptr_t player, uintptr_t &item, std::int32_t &x, std::int32_t &y)
{
    item = 0;
    std::int32_t ready = 0;
    const bool layout = game::Read(game::Absolute(VaLayoutReady), ready) && ready != 0;
    const uintptr_t inventory = game::UnitInventory(player);
    GridView grid;

    switch (g_state.area)
    {
    case InventoryArea::Grid:
    {
        ReadGrid(inventory, GridInventory, grid);
        item = grid.At(g_state.gridColumn, g_state.gridRow);
        std::array<std::int32_t, 6> info{};
        if (!layout || !game::ReadBytes(game::Absolute(VaInventoryGrid), info.data(), sizeof(info)))
            return false;

        int columns = 0, rows = 0, left = 0, right = 0, top = 0, bottom = 0, boxWidth = 0, boxHeight = 0;
        if (info[0] > 0 && info[0] < 64 && info[1] > 0 && info[1] < 64)
        {
            // Separate dwords: columns, rows, left, right, top, bottom.
            columns = info[0];
            rows = info[1];
            left = info[2];
            right = info[3];
            top = info[4];
            bottom = info[5];
        }
        else
        {
            // Packed: bytes columns/rows, left, right, top, bottom, bytes box size.
            columns = info[0] & 0xFF;
            rows = (info[0] >> 8) & 0xFF;
            left = info[1];
            right = info[2];
            top = info[3];
            bottom = info[4];
            boxWidth = info[5] & 0xFF;
            boxHeight = (info[5] >> 8) & 0xFF;
        }
        if (columns <= 0 || rows <= 0 || right <= left || bottom <= top)
            return false;
        if (boxWidth <= 0)
            boxWidth = (right - left) / columns;
        if (boxHeight <= 0)
            boxHeight = (bottom - top) / rows;
        x = left + g_state.gridColumn * boxWidth + boxWidth / 2;
        y = top + g_state.gridRow * boxHeight + boxHeight / 2;
        return true;
    }
    case InventoryArea::Equipment:
    {
        const EquipmentSlot &slot = EquipmentSlots[g_state.equipmentIndex];
        ReadGrid(inventory, GridBody, grid);
        item = grid.At(slot.bodyLocation, 0);
        const int index = BodyRectIndex(slot.bodyLocation);
        std::array<std::int32_t, 5> rect{};
        if (!layout || index < 0 ||
            !game::ReadBytes(game::Absolute(VaBodyRects[static_cast<size_t>(index)]), rect.data(), sizeof(rect)) ||
            rect[1] <= rect[0] || rect[3] <= rect[2])
            return false;
        x = (rect[0] + rect[1]) / 2;
        y = (rect[2] + rect[3]) / 2;
        return true;
    }
    case InventoryArea::Belt:
    {
        ReadGrid(inventory, GridBelt, grid);
        item = grid.At(g_state.beltSlot, 0);
        return false;
    }
    }
    return false;
}

void FinishDescription()
{
    if (!g_state.describing)
        return;
    game::PostGameMouseMove(g_state.savedMouseX, g_state.savedMouseY);
    g_state.describing = false;
}

void StartDescription(uintptr_t player)
{
    FinishDescription();

    uintptr_t item = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    const bool located = SelectedSlotScreenPoint(player, item, x, y);
    if (item == 0)
    {
        Say(Tr(L"Pusto.", L"Empty."));
        return;
    }
    if (!located || game::InventoryCursorItem(game::UnitInventory(player)) != 0)
    {
        Say(ItemLabel(item));
        return;
    }

    game::ReadGameMouse(g_state.savedMouseX, g_state.savedMouseY);
    const wchar_t terminator = 0;
    game::Write(game::Absolute(VaHoverText), terminator);
    if (!game::PostGameMouseMove(x, y))
    {
        Say(ItemLabel(item));
        return;
    }
    g_state.describing = true;
    g_state.describeItem = item;
    g_state.describeTick = GetTickCount64();
    LogLine(L"Panels: describing item at screen " + std::to_wstring(x) + L", " + std::to_wstring(y));
}

void ProcessDescription(bool inventoryOpen)
{
    if (!g_state.describing)
        return;
    if (!inventoryOpen)
    {
        FinishDescription();
        return;
    }

    const uintptr_t item = g_state.describeItem;
    const uintptr_t hovered = game::ReadPtr(game::Absolute(VaHoveredItem));
    const std::wstring text = game::ReadWideString(game::Absolute(VaHoverText), HoverTextChars);
    if (hovered == item && !text.empty())
    {
        FinishDescription();
        Say(CleanGameText(text, L". "));
        return;
    }
    if (GetTickCount64() - g_state.describeTick > DescriptionTimeoutMs)
    {
        FinishDescription();
        LogLine(L"Panels: description timed out, hovered item " + std::to_wstring(hovered));
        Say(ItemLabel(item) + Tr(L". Nie udało się odczytać opisu.", L". Could not read the description."));
    }
}

bool KeyToDirection(DWORD virtualKey, int &dx, int &dy)
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
    default:
        return false;
    }
}

} // namespace

std::wstring CleanText(const std::wstring &text)
{
    return CleanGameText(text);
}

void ResetPanels()
{
    g_state = State{};
    g_panelOpenForKeys = false;
}

void UpdatePanels(uintptr_t playerUnit, game::Point playerPosition)
{
    (void)playerPosition;
    const bool inventoryOpen = game::IsUiPanelOpen(UiInventory);
    const bool characterOpen = game::IsUiPanelOpen(UiCharacter);
    g_panelOpenForKeys = inventoryOpen || characterOpen;

    if (characterOpen && !g_state.characterWasOpen)
    {
        g_state.focus = Focus::Character;
        OpenCharacterSheet(playerUnit);
    }
    if (inventoryOpen && !g_state.inventoryWasOpen)
    {
        g_state.focus = Focus::Inventory;
        SpeakInventoryCell(playerUnit, Tr(L"Ekwipunek. ", L"Inventory. "));
    }
    if (!inventoryOpen && g_state.focus == Focus::Inventory)
        g_state.focus = characterOpen ? Focus::Character : Focus::None;
    if (!characterOpen && g_state.focus == Focus::Character)
        g_state.focus = inventoryOpen ? Focus::Inventory : Focus::None;

    g_state.inventoryWasOpen = inventoryOpen;
    g_state.characterWasOpen = characterOpen;

    ProcessStatQueue(playerUnit);
    ProcessItemAction(playerUnit);
    ProcessDescription(inventoryOpen);
}

bool IsAnyPanelFocused()
{
    return g_state.focus != Focus::None;
}

bool IsPanelOpenForKeys()
{
    return g_panelOpenForKeys.load();
}

bool HandlePanelKey(uintptr_t playerUnit, game::Point playerPosition, const PanelKey &key)
{
    if (g_state.focus == Focus::None)
        return false;

    if (key.virtualKey == VK_TAB)
    {
        const bool inventoryOpen = game::IsUiPanelOpen(UiInventory);
        const bool characterOpen = game::IsUiPanelOpen(UiCharacter);
        if (g_state.focus == Focus::Inventory && characterOpen)
        {
            g_state.focus = Focus::Character;
            Say(TrS(L"Karta postaci. ", L"Character sheet. ") +CurrentCharFieldText(playerUnit));
        }
        else if (g_state.focus == Focus::Character && inventoryOpen)
        {
            g_state.focus = Focus::Inventory;
            SpeakInventoryCell(playerUnit, Tr(L"Ekwipunek. ", L"Inventory. "));
        }
        return true;
    }

    int dx = 0;
    int dy = 0;
    const bool direction = KeyToDirection(key.virtualKey, dx, dy);
    const bool activate = key.virtualKey == VK_RETURN;

    // Space reads: the selected field on the sheet, the item description in
    // the inventory.
    if (key.virtualKey == VK_SPACE)
    {
        if (g_state.focus == Focus::Character)
            Say(CurrentCharFieldText(playerUnit));
        else
            StartDescription(playerUnit);
        return true;
    }

    if (g_state.focus == Focus::Character)
    {
        if (direction)
        {
            if (dy != 0)
                CharacterMove(playerUnit, dy);
            else
                Say(CurrentCharFieldText(playerUnit));
            return true;
        }
        if (activate)
        {
            CharacterActivate(playerUnit, key.shift);
            return true;
        }
        return false;
    }

    if (direction)
    {
        InventoryMove(playerUnit, dx, dy);
        return true;
    }
    if (activate)
    {
        InventoryActivate(playerUnit, playerPosition, key.shift);
        return true;
    }
    return false;
}

void SpeakHealth(uintptr_t playerUnit, bool mana)
{
    const int current = game::UnitStat(playerUnit, mana ? game::stat::Mana : game::stat::HitPoints);
    const int maximum = game::UnitStat(playerUnit, mana ? game::stat::MaxMana : game::stat::MaxHitPoints);
    if (maximum <= 0)
        return;
    Say(std::to_wstring(Percent(current, maximum)) + L"%");
}

void SpeakExperience(uintptr_t playerUnit)
{
    std::uint32_t classId = 0;
    game::Read(playerUnit + game::off::UnitClassId, classId);
    const int level = game::UnitStat(playerUnit, game::stat::Level);
    if (level >= 99)
    {
        Say(Tr(L"Maksymalny poziom.", L"Maximum level."));
        return;
    }

    const long long previous = game::ExperienceForLevel(classId, level);
    const long long next = game::ExperienceForLevel(classId, level + 1);
    if (previous < 0 || next <= previous)
        return;
    const long long experience =
        std::clamp<long long>(static_cast<std::uint32_t>(game::UnitStat(playerUnit, game::stat::Experience)), previous, next);
    Say(std::to_wstring(Percent(next - experience, next - previous)) + L"%");
}

} // namespace d2access::panels
