#include "LevelMap.hpp"

#include "Logging.hpp"

#include <algorithm>
#include <sstream>

namespace d2access {

namespace {

constexpr int SubtilesPerTile = 5;
constexpr size_t MaxLevelCells = 24u * 1024u * 1024u;
constexpr size_t MaxRoom2PerLevel = 4096;
constexpr int MaxRoom1Walk = 4096;
constexpr double ScanBudgetMs = 6.0;
constexpr int ExitMergeDistance = 60;
constexpr std::uint16_t SnapMask = game::CollideMaskPlayerPath;

double NowMs()
{
    static LARGE_INTEGER frequency = [] {
        LARGE_INTEGER value{};
        QueryPerformanceFrequency(&value);
        return value;
    }();
    LARGE_INTEGER counter{};
    QueryPerformanceCounter(&counter);
    return static_cast<double>(counter.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
}

std::uint16_t GridCell(const game::CollisionGrid &grid, int x, int y)
{
    std::uint16_t value = game::CollideUnknown;
    if (!grid.rect.Contains(x, y))
        return value;
    const uintptr_t index = static_cast<uintptr_t>((y - grid.rect.y) * grid.rect.w + (x - grid.rect.x));
    game::Read(grid.mask + index * 2, value);
    return value;
}

} // namespace

void LevelMap::Reset()
{
    *this = LevelMap{};
}

bool LevelMap::Update(uintptr_t act, uintptr_t playerRoom1)
{
    const uintptr_t level = game::Room2Level(game::Room1Room2(playerRoom1));
    const int levelId = game::LevelIdOf(level);
    if (levelId <= 0 || act == 0)
        return false;

    if (levelId != levelId_ || level != level_ || act != act_)
    {
        BeginLevel(level, levelId, act);
        return true;
    }

    if (ready_)
        return false;

    const double start = NowMs();
    while (scanIndex_ < room2s_.size())
    {
        ProcessRoom2(act, room2s_[scanIndex_], playerRoom1);
        ++scanIndex_;
        if (NowMs() - start >= ScanBudgetMs)
            break;
    }

    if (scanIndex_ >= room2s_.size())
    {
        RefreshLiveCollision(act);
        FinalizeExits();
        ready_ = true;

        std::wstringstream stream;
        stream << L"Level map ready. Level " << levelId_ << L", rooms " << room2s_.size()
               << L", rooms with collision " << roomsWithCollision_ << L", presets "
               << presets_.size() << L", exits " << exits_.size() << L", bounds X "
               << bounds_.x << L" Y " << bounds_.y << L" W " << bounds_.w << L" H " << bounds_.h
               << L".";
        LogLine(stream.str());
        for (const LevelExit &exit : exits_)
        {
            std::wstringstream line;
            line << L"  exit to level " << exit.destinationLevel << L" at X " << exit.position.x
                 << L" Y " << exit.position.y << (exit.isWarpTile ? L" (warp tile)" : L" (border)");
            LogLine(line.str());
        }
    }

    return false;
}

void LevelMap::BeginLevel(uintptr_t level, int levelId, uintptr_t act)
{
    Reset();
    act_ = act;
    level_ = level;
    levelId_ = levelId;

    game::Rect tiles;
    if (game::LevelTileRect(level, tiles))
    {
        bounds_ = game::Rect{tiles.x * SubtilesPerTile, tiles.y * SubtilesPerTile,
                             tiles.w * SubtilesPerTile, tiles.h * SubtilesPerTile};
        const size_t cells = static_cast<size_t>(bounds_.w) * static_cast<size_t>(bounds_.h);
        if (cells > 0 && cells <= MaxLevelCells)
            mask_.assign(cells, game::CollideUnknown);
    }

    for (uintptr_t room2 = game::LevelFirstRoom2(level);
         room2 != 0 && room2s_.size() < MaxRoom2PerLevel; room2 = game::Room2Next(room2))
    {
        if (std::find(room2s_.begin(), room2s_.end(), room2) != room2s_.end())
            break;
        room2s_.push_back(room2);
    }

    std::wstringstream stream;
    stream << L"Level map started. Level " << levelId << L", rooms " << room2s_.size()
           << L", collision grid " << (mask_.empty() ? L"unavailable" : L"allocated") << L".";
    LogLine(stream.str());
}

uintptr_t LevelMap::FindRoom1(uintptr_t act, uintptr_t room2) const
{
    const uintptr_t candidate = game::ReadPtr(room2 + game::off::Room2Room1);
    if (candidate != 0 && game::Room1Room2(candidate) == room2)
        return candidate;

    uintptr_t room1 = game::ActFirstRoom1(act);
    for (int i = 0; i < MaxRoom1Walk && room1 != 0; ++i)
    {
        if (game::Room1Room2(room1) == room2)
            return room1;
        room1 = game::Room1Next(room1);
    }
    return 0;
}

void LevelMap::ProcessRoom2(uintptr_t act, uintptr_t room2, uintptr_t hintRoom1)
{
    game::Rect tiles;
    if (!game::Room2TileRect(room2, tiles))
        return;

    uintptr_t room1 = FindRoom1(act, room2);
    bool added = false;
    if (room1 == 0 && hintRoom1 != 0 &&
        game::AddRoomData(act, levelId_, tiles.x, tiles.y, hintRoom1))
    {
        added = true;
        room1 = FindRoom1(act, room2);
    }

    if (room1 != 0)
    {
        CopyCollision(room1);
        ++roomsWithCollision_;
    }

    std::vector<int> warpTileLevels;
    ReadPresets(room2, tiles, warpTileLevels);
    ReadAdjacentExits(act, room2, tiles, hintRoom1);
    ReadRoomTileExits(room2, tiles, warpTileLevels);

    if (added)
        game::RemoveRoomData(act, levelId_, tiles.x, tiles.y, hintRoom1);
}

void LevelMap::CopyCollision(uintptr_t room1)
{
    if (mask_.empty())
        return;

    game::CollisionGrid grid;
    if (!game::Room1Collision(room1, grid))
        return;

    const int x0 = std::max(grid.rect.x, bounds_.x);
    const int x1 = std::min(grid.rect.x + grid.rect.w, bounds_.x + bounds_.w);
    const int y0 = std::max(grid.rect.y, bounds_.y);
    const int y1 = std::min(grid.rect.y + grid.rect.h, bounds_.y + bounds_.h);
    if (x0 >= x1 || y0 >= y1)
        return;

    std::vector<std::uint16_t> row(static_cast<size_t>(grid.rect.w));
    for (int y = y0; y < y1; ++y)
    {
        const uintptr_t rowAddress = grid.mask + static_cast<uintptr_t>(y - grid.rect.y) *
                                                     static_cast<uintptr_t>(grid.rect.w) * 2;
        if (!game::ReadBytes(rowAddress, row.data(), row.size() * 2))
            return;

        const size_t destRow = static_cast<size_t>(y - bounds_.y) * static_cast<size_t>(bounds_.w);
        for (int x = x0; x < x1; ++x)
            mask_[destRow + static_cast<size_t>(x - bounds_.x)] = row[static_cast<size_t>(x - grid.rect.x)];
    }
}

void LevelMap::ReadPresets(uintptr_t room2, const game::Rect &tiles, std::vector<int> &warpTileLevels)
{
    uintptr_t preset = game::ReadPtr(room2 + game::off::Room2Presets);
    for (int i = 0; i < 512 && preset != 0; ++i)
    {
        std::uint32_t classId = 0;
        std::int32_t x = 0;
        std::uint32_t unitType = 0;
        std::int32_t y = 0;
        if (game::Read(preset + game::off::PresetClassId, classId) &&
            game::Read(preset + game::off::PresetX, x) &&
            game::Read(preset + game::off::PresetUnitType, unitType) &&
            game::Read(preset + game::off::PresetY, y) && unitType <= 5)
        {
            const game::Point position{tiles.x * SubtilesPerTile + x, tiles.y * SubtilesPerTile + y};
            presets_.push_back(LevelPreset{unitType, classId, position});

            if (unitType == static_cast<std::uint32_t>(game::UnitType::Tile))
            {
                const int destination = game::WarpDestinationLevel(room2, classId);
                if (destination > 0 && destination != levelId_)
                {
                    LevelExit exit;
                    exit.destinationLevel = destination;
                    exit.position = position;
                    exit.beyond = position;
                    exit.isWarpTile = true;
                    candidates_.push_back(ExitCandidate{exit});
                    warpTileLevels.push_back(destination);
                }
            }
        }

        const uintptr_t next = game::ReadPtr(preset + game::off::PresetNext);
        if (next == preset)
            break;
        preset = next;
    }
}

void LevelMap::ReadAdjacentExits(uintptr_t act, uintptr_t room2, const game::Rect &tiles, uintptr_t hintRoom1)
{
    for (uintptr_t adjacent : game::Room2Adjacent(room2))
    {
        const int destination = game::Room2LevelId(adjacent);
        game::Rect other;
        if (destination <= 0 || destination == levelId_ || !game::Room2TileRect(adjacent, other))
            continue;

        const int overlapX0 = std::max(tiles.x, other.x) * SubtilesPerTile;
        const int overlapX1 = std::min(tiles.x + tiles.w, other.x + other.w) * SubtilesPerTile;
        const int overlapY0 = std::max(tiles.y, other.y) * SubtilesPerTile;
        const int overlapY1 = std::min(tiles.y + tiles.h, other.y + other.h) * SubtilesPerTile;

        // `edge` is the first subtile outside the current room; (dx, dy) points
        // outward; [from, to) is the shared stretch of the border.
        int dx = 0;
        int dy = 0;
        int edge = 0;
        int from = 0;
        int to = 0;
        if (other.x == tiles.x + tiles.w && overlapY1 > overlapY0)
        {
            dx = 1;
            edge = (tiles.x + tiles.w) * SubtilesPerTile;
            from = overlapY0;
            to = overlapY1;
        }
        else if (other.x + other.w == tiles.x && overlapY1 > overlapY0)
        {
            dx = -1;
            edge = tiles.x * SubtilesPerTile - 1;
            from = overlapY0;
            to = overlapY1;
        }
        else if (other.y == tiles.y + tiles.h && overlapX1 > overlapX0)
        {
            dy = 1;
            edge = (tiles.y + tiles.h) * SubtilesPerTile;
            from = overlapX0;
            to = overlapX1;
        }
        else if (other.y + other.h == tiles.y && overlapX1 > overlapX0)
        {
            dy = -1;
            edge = tiles.y * SubtilesPerTile - 1;
            from = overlapX0;
            to = overlapX1;
        }
        else
        {
            continue;
        }

        // depth 0 is the first cell past the border, negative depths are inside.
        const auto cellAt = [dx, dy, edge](int along, int depth) {
            return dx != 0 ? game::Point{edge + dx * depth, along} : game::Point{along, edge + dy * depth};
        };

        // Borders such as the Rogue Encampment palisade are only passable in a
        // gap, so the neighbouring level's collision is read too.
        uintptr_t neighbourRoom1 = FindRoom1(act, adjacent);
        bool added = false;
        if (neighbourRoom1 == 0 && hintRoom1 != 0 &&
            game::AddRoomData(act, destination, other.x, other.y, hintRoom1))
        {
            added = true;
            neighbourRoom1 = FindRoom1(act, adjacent);
        }
        game::CollisionGrid neighbourGrid;
        const bool haveNeighbour = neighbourRoom1 != 0 && game::Room1Collision(neighbourRoom1, neighbourGrid);

        const auto passable = [&](int along) {
            for (int depth = -4; depth <= -1; ++depth)
            {
                const game::Point p = cellAt(along, depth);
                if ((MaskAt(p.x, p.y) & SnapMask) != 0)
                    return false;
            }
            if (!haveNeighbour)
                return true;
            for (int depth = 0; depth <= 4; ++depth)
            {
                const game::Point p = cellAt(along, depth);
                if ((GridCell(neighbourGrid, p.x, p.y) & SnapMask) != 0)
                    return false;
            }
            return true;
        };

        int runStart = -1;
        for (int along = from; along <= to; ++along)
        {
            const bool open = along < to && passable(along);
            if (open && runStart < 0)
                runStart = along;
            if (!open && runStart >= 0)
            {
                if (along - runStart >= 3)
                {
                    const int middle = (runStart + along - 1) / 2;
                    LevelExit exit;
                    exit.destinationLevel = destination;
                    exit.position = cellAt(middle, -3);
                    exit.beyond = cellAt(middle, 8);
                    candidates_.push_back(ExitCandidate{exit});
                }
                runStart = -1;
            }
        }

        if (added)
            game::RemoveRoomData(act, destination, other.x, other.y, hintRoom1);
    }
}

void LevelMap::ReadRoomTileExits(uintptr_t room2, const game::Rect &tiles,
                                 const std::vector<int> &warpTileLevels)
{
    uintptr_t tile = game::ReadPtr(room2 + game::off::Room2RoomTiles);
    for (int i = 0; i < 64 && tile != 0; ++i)
    {
        const uintptr_t destinationRoom2 = game::ReadPtr(tile);
        const int destination = game::Room2LevelId(destinationRoom2);
        const bool alreadyKnown =
            std::find(warpTileLevels.begin(), warpTileLevels.end(), destination) != warpTileLevels.end();
        game::Rect destinationTiles;
        // Room tiles that link spatially adjacent rooms are plain borders, which
        // ReadAdjacentExits already handles. The rest are entrances without a
        // tile preset, so the room centre is the best available position.
        const bool adjacent = game::Room2TileRect(destinationRoom2, destinationTiles) &&
                              (destinationTiles.x == tiles.x + tiles.w ||
                               destinationTiles.x + destinationTiles.w == tiles.x ||
                               destinationTiles.y == tiles.y + tiles.h ||
                               destinationTiles.y + destinationTiles.h == tiles.y);
        if (destination > 0 && destination != levelId_ && !alreadyKnown && !adjacent)
        {
            LevelExit exit;
            exit.destinationLevel = destination;
            exit.position = game::Point{tiles.x * SubtilesPerTile + tiles.w * SubtilesPerTile / 2,
                                        tiles.y * SubtilesPerTile + tiles.h * SubtilesPerTile / 2};
            exit.beyond = exit.position;
            exit.isWarpTile = true;
            candidates_.push_back(ExitCandidate{exit});
        }

        const uintptr_t next = game::ReadPtr(tile + 0x04);
        if (next == tile)
            break;
        tile = next;
    }
}

void LevelMap::FinalizeExits()
{
    exits_.clear();
    std::vector<bool> used(candidates_.size(), false);

    for (size_t i = 0; i < candidates_.size(); ++i)
    {
        if (used[i])
            continue;
        used[i] = true;

        const LevelExit &seed = candidates_[i].exit;
        std::vector<const LevelExit *> members{&seed};
        long long sumX = seed.position.x;
        long long sumY = seed.position.y;

        for (size_t j = i + 1; j < candidates_.size(); ++j)
        {
            const LevelExit &other = candidates_[j].exit;
            if (used[j] || other.destinationLevel != seed.destinationLevel ||
                other.isWarpTile != seed.isWarpTile ||
                game::ChebyshevDistance(other.position, seed.position) > ExitMergeDistance)
                continue;
            used[j] = true;
            sumX += other.position.x;
            sumY += other.position.y;
            members.push_back(&other);
        }

        // Keep a real gap position: the average of separate gaps can land on a wall.
        const game::Point centre{static_cast<int>(sumX / static_cast<long long>(members.size())),
                                 static_cast<int>(sumY / static_cast<long long>(members.size()))};
        const LevelExit *best = *std::min_element(members.begin(), members.end(),
                                                  [&centre](const LevelExit *a, const LevelExit *b) {
                                                      return game::ChebyshevDistance(a->position, centre) <
                                                             game::ChebyshevDistance(b->position, centre);
                                                  });

        LevelExit merged = *best;
        game::Point snapped;
        if (FindNearestStandable(merged.position, merged.isWarpTile ? 10 : 6, SnapMask, snapped))
        {
            merged.beyond = game::Point{merged.beyond.x + snapped.x - merged.position.x,
                                        merged.beyond.y + snapped.y - merged.position.y};
            merged.position = snapped;
        }
        if (merged.isWarpTile)
            merged.beyond = merged.position;

        exits_.push_back(merged);
    }
}

void LevelMap::RefreshLiveCollision(uintptr_t act)
{
    if (mask_.empty())
        return;

    uintptr_t room1 = game::ActFirstRoom1(act);
    for (int i = 0; i < MaxRoom1Walk && room1 != 0; ++i)
    {
        if (game::Room1Room2(room1) != 0 && game::Room1LevelId(room1) == levelId_)
            CopyCollision(room1);
        room1 = game::Room1Next(room1);
    }
}

std::uint16_t LevelMap::MaskAt(int x, int y) const
{
    if (mask_.empty() || !bounds_.Contains(x, y))
        return game::CollideUnknown;
    return mask_[static_cast<size_t>(y - bounds_.y) * static_cast<size_t>(bounds_.w) +
                 static_cast<size_t>(x - bounds_.x)];
}

bool LevelMap::IsStandable(int x, int y, std::uint16_t blockMask) const
{
    return !IsBlocked(x, y, blockMask) && !IsBlocked(x - 1, y, blockMask) &&
           !IsBlocked(x + 1, y, blockMask) && !IsBlocked(x, y - 1, blockMask) &&
           !IsBlocked(x, y + 1, blockMask);
}

bool LevelMap::FindNearestStandable(game::Point around, int radius, std::uint16_t blockMask,
                                    game::Point &result) const
{
    if (IsStandable(around.x, around.y, blockMask))
    {
        result = around;
        return true;
    }

    for (int r = 1; r <= radius; ++r)
    {
        bool found = false;
        int bestScore = 0;
        for (int dy = -r; dy <= r; ++dy)
        {
            for (int dx = -r; dx <= r; ++dx)
            {
                if (std::abs(dx) != r && std::abs(dy) != r)
                    continue;
                if (!IsStandable(around.x + dx, around.y + dy, blockMask))
                    continue;
                const int score = dx * dx + dy * dy;
                if (!found || score < bestScore)
                {
                    found = true;
                    bestScore = score;
                    result = game::Point{around.x + dx, around.y + dy};
                }
            }
        }
        if (found)
            return true;
    }
    return false;
}

} // namespace d2access
