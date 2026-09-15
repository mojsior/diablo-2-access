#include "Pathfinder.hpp"

#include "Localization.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <functional>
#include <queue>
#include <utility>

namespace d2access {

namespace {

struct Step {
    int dx;
    int dy;
};

// Axis directions first so equal-cost ties keep straight lines.
constexpr std::array<Step, 8> Steps = {
    Step{0, -1}, Step{0, 1}, Step{1, 0}, Step{-1, 0},
    Step{1, -1}, Step{-1, -1}, Step{1, 1}, Step{-1, 1},
};

constexpr std::uint8_t NoParent = 0xFF;
constexpr int StraightCost = 10;
constexpr int DiagonalCost = 14;
constexpr int TurnPenalty = 3;

struct SearchBuffers {
    std::vector<std::int32_t> g;
    std::vector<std::uint32_t> stamp;
    std::vector<std::uint8_t> parent;
    std::vector<std::uint8_t> closed;
    std::uint32_t generation = 0;

    void Prepare(size_t cells)
    {
        if (g.size() != cells)
        {
            g.assign(cells, 0);
            stamp.assign(cells, 0);
            parent.assign(cells, NoParent);
            closed.assign(cells, 0);
            generation = 0;
        }
        ++generation;
        if (generation == 0)
        {
            std::fill(stamp.begin(), stamp.end(), 0);
            generation = 1;
        }
    }
};

SearchBuffers &Buffers()
{
    static SearchBuffers buffers;
    return buffers;
}

int Heuristic(game::Point a, game::Point b, bool axisOnly)
{
    const int dx = std::abs(a.x - b.x);
    const int dy = std::abs(a.y - b.y);
    if (axisOnly)
        return (dx + dy) * StraightCost;
    return std::max(dx, dy) * StraightCost + std::min(dx, dy) * (DiagonalCost - StraightCost);
}

} // namespace

PathResult FindPath(const LevelMap &map, game::Point start, game::Point goal, const PathOptions &options)
{
    PathResult result;
    result.end = start;

    const game::Rect &bounds = map.Bounds();
    if (!map.HasCollision() || !bounds.Contains(start.x, start.y))
        return result;

    const auto indexOf = [&bounds](int x, int y) {
        return static_cast<size_t>(y - bounds.y) * static_cast<size_t>(bounds.w) +
               static_cast<size_t>(x - bounds.x);
    };

    int goalRadius = options.goalRadius;
    if (goalRadius == 0 && !map.IsStandable(goal.x, goal.y, options.blockMask))
        goalRadius = 1;

    const auto isGoal = [&](game::Point p) {
        if (goalRadius == 0)
            return p == goal;
        return game::ChebyshevDistance(p, goal) <= goalRadius &&
               (p == start || map.IsStandable(p.x, p.y, options.blockMask));
    };

    if (isGoal(start))
    {
        result.found = true;
        return result;
    }

    SearchBuffers &buffers = Buffers();
    buffers.Prepare(static_cast<size_t>(bounds.w) * static_cast<size_t>(bounds.h));
    const std::uint32_t generation = buffers.generation;

    using Entry = std::pair<int, size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;

    const size_t startIndex = indexOf(start.x, start.y);
    buffers.g[startIndex] = 0;
    buffers.stamp[startIndex] = generation;
    buffers.parent[startIndex] = NoParent;
    buffers.closed[startIndex] = 0;
    open.emplace(Heuristic(start, goal, options.axisOnly), startIndex);

    const size_t stepCount = options.axisOnly ? 4 : Steps.size();
    size_t goalIndex = SIZE_MAX;
    size_t closestIndex = startIndex;
    int closestDistance = game::ChebyshevDistance(start, goal);
    int closestCost = 0;
    int expansions = 0;

    while (!open.empty() && expansions < options.maxExpansions)
    {
        const auto [f, index] = open.top();
        open.pop();
        if (buffers.closed[index] != 0 && buffers.stamp[index] == generation)
            continue;
        buffers.closed[index] = 1;
        ++expansions;

        const game::Point current{static_cast<int>(index % static_cast<size_t>(bounds.w)) + bounds.x,
                                  static_cast<int>(index / static_cast<size_t>(bounds.w)) + bounds.y};
        const int currentG = buffers.g[index];

        if (isGoal(current))
        {
            goalIndex = index;
            break;
        }

        const int distance = game::ChebyshevDistance(current, goal);
        if (distance < closestDistance || (distance == closestDistance && currentG < closestCost))
        {
            closestDistance = distance;
            closestCost = currentG;
            closestIndex = index;
        }

        const std::uint8_t parentStep = buffers.parent[index];
        for (size_t s = 0; s < stepCount; ++s)
        {
            const Step step = Steps[s];
            const game::Point next{current.x + step.dx, current.y + step.dy};
            if (!bounds.Contains(next.x, next.y))
                continue;

            const bool nextIsGoalCell = next == goal;
            if (!map.IsStandable(next.x, next.y, options.blockMask) && !nextIsGoalCell)
                continue;

            const bool diagonal = step.dx != 0 && step.dy != 0;
            if (diagonal && (!map.IsStandable(current.x + step.dx, current.y, options.blockMask) ||
                             !map.IsStandable(current.x, current.y + step.dy, options.blockMask)))
                continue;

            int cost = diagonal ? DiagonalCost : StraightCost;
            if (parentStep != NoParent && parentStep != s)
                cost += TurnPenalty;

            const size_t nextIndex = indexOf(next.x, next.y);
            const int newG = currentG + cost;
            if (buffers.stamp[nextIndex] == generation)
            {
                if (buffers.closed[nextIndex] != 0 || newG >= buffers.g[nextIndex])
                    continue;
            }
            else
            {
                buffers.stamp[nextIndex] = generation;
                buffers.closed[nextIndex] = 0;
            }

            buffers.g[nextIndex] = newG;
            buffers.parent[nextIndex] = static_cast<std::uint8_t>(s);
            open.emplace(newG + Heuristic(next, goal, options.axisOnly), nextIndex);
        }
    }

    size_t endIndex = goalIndex;
    if (endIndex == SIZE_MAX)
    {
        if (!options.closestIfUnreachable)
            return result;
        endIndex = closestIndex;
    }
    else
    {
        result.found = true;
    }

    std::vector<game::Point> reversed;
    size_t index = endIndex;
    while (index != startIndex)
    {
        const std::uint8_t s = buffers.parent[index];
        if (s == NoParent || buffers.stamp[index] != generation)
            return PathResult{false, start, {}};
        const game::Point p{static_cast<int>(index % static_cast<size_t>(bounds.w)) + bounds.x,
                            static_cast<int>(index / static_cast<size_t>(bounds.w)) + bounds.y};
        reversed.push_back(p);
        index = indexOf(p.x - Steps[s].dx, p.y - Steps[s].dy);
        if (reversed.size() > static_cast<size_t>(bounds.w) * static_cast<size_t>(bounds.h))
            return PathResult{false, start, {}};
    }

    result.cells.assign(reversed.rbegin(), reversed.rend());
    result.end = result.cells.empty() ? start : result.cells.back();
    return result;
}

PathResult FindKeyboardPath(const LevelMap &map, game::Point start, game::Point goal, PathOptions options)
{
    PathOptions axis = options;
    axis.axisOnly = true;
    axis.closestIfUnreachable = false;
    PathResult result = FindPath(map, start, goal, axis);
    if (result.found)
        return result;

    options.axisOnly = false;
    return FindPath(map, start, goal, options);
}

bool HasWalkableLine(const LevelMap &map, game::Point from, game::Point to, std::uint16_t blockMask)
{
    int x = from.x;
    int y = from.y;
    const int dx = std::abs(to.x - from.x);
    const int dy = std::abs(to.y - from.y);
    const int sx = from.x < to.x ? 1 : -1;
    const int sy = from.y < to.y ? 1 : -1;
    int error = dx - dy;

    while (x != to.x || y != to.y)
    {
        const int doubled = error * 2;
        int nextX = x;
        int nextY = y;
        if (doubled > -dy)
        {
            error -= dy;
            nextX += sx;
        }
        if (doubled < dx)
        {
            error += dx;
            nextY += sy;
        }

        if (!map.IsStandable(nextX, nextY, blockMask))
            return false;
        if (nextX != x && nextY != y &&
            (!map.IsStandable(nextX, y, blockMask) || !map.IsStandable(x, nextY, blockMask)))
            return false;

        x = nextX;
        y = nextY;
    }
    return true;
}

bool CanTakeStep(const LevelMap &map, game::Point from, game::Point to, std::uint16_t blockMask)
{
    return map.IsStandable(to.x, to.y, blockMask) && HasWalkableLine(map, from, to, blockMask);
}

namespace {

struct LatticeSearch {
    bool found = false;
    std::vector<game::Point> nodes;
};

LatticeSearch SearchLattice(const LevelMap &map, game::Point start, game::Point goal, int step, int reach,
                            std::uint16_t blockMask, bool axisOnly)
{
    LatticeSearch search;
    const game::Rect &bounds = map.Bounds();
    const int iMin = -((start.x - bounds.x) / step);
    const int iMax = (bounds.x + bounds.w - 1 - start.x) / step;
    const int jMin = -((start.y - bounds.y) / step);
    const int jMax = (bounds.y + bounds.h - 1 - start.y) / step;
    const int width = iMax - iMin + 1;
    const int height = jMax - jMin + 1;
    if (width <= 0 || height <= 0)
        return search;

    const size_t cells = static_cast<size_t>(width) * static_cast<size_t>(height);
    const auto indexOf = [&](int i, int j) {
        return static_cast<size_t>(j - jMin) * static_cast<size_t>(width) + static_cast<size_t>(i - iMin);
    };
    const auto pointOf = [&](int i, int j) { return game::Point{start.x + i * step, start.y + j * step}; };
    const auto heuristic = [&](game::Point p) {
        return std::max(0, game::ChebyshevDistance(p, goal) - reach) / step * StraightCost;
    };

    std::vector<int> g(cells, INT_MAX);
    std::vector<std::uint8_t> parent(cells, NoParent);
    std::vector<std::uint8_t> closed(cells, 0);
    using Entry = std::pair<int, size_t>;
    std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;

    const size_t startIndex = indexOf(0, 0);
    g[startIndex] = 0;
    open.emplace(heuristic(start), startIndex);

    size_t goalIndex = SIZE_MAX;
    size_t closestIndex = startIndex;
    int closestDistance = game::ChebyshevDistance(start, goal);
    int closestCost = 0;
    const size_t stepCount = axisOnly ? 4 : Steps.size();

    while (!open.empty())
    {
        const size_t index = open.top().second;
        open.pop();
        if (closed[index] != 0)
            continue;
        closed[index] = 1;

        const int ci = static_cast<int>(index % static_cast<size_t>(width)) + iMin;
        const int cj = static_cast<int>(index / static_cast<size_t>(width)) + jMin;
        const game::Point current = pointOf(ci, cj);
        const int distance = game::ChebyshevDistance(current, goal);
        if (distance <= reach)
        {
            goalIndex = index;
            break;
        }
        if (distance < closestDistance || (distance == closestDistance && g[index] < closestCost))
        {
            closestDistance = distance;
            closestCost = g[index];
            closestIndex = index;
        }

        for (size_t s = 0; s < stepCount; ++s)
        {
            const int ni = ci + Steps[s].dx;
            const int nj = cj + Steps[s].dy;
            if (ni < iMin || ni > iMax || nj < jMin || nj > jMax)
                continue;
            const size_t nextIndex = indexOf(ni, nj);
            if (closed[nextIndex] != 0)
                continue;
            const game::Point next = pointOf(ni, nj);
            if (!CanTakeStep(map, current, next, blockMask))
                continue;

            int cost = (Steps[s].dx != 0 && Steps[s].dy != 0) ? DiagonalCost : StraightCost;
            if (parent[index] != NoParent && parent[index] != s)
                cost += TurnPenalty;
            const int newG = g[index] + cost;
            if (newG >= g[nextIndex])
                continue;
            g[nextIndex] = newG;
            parent[nextIndex] = static_cast<std::uint8_t>(s);
            open.emplace(newG + heuristic(next), nextIndex);
        }
    }

    search.found = goalIndex != SIZE_MAX;
    size_t index = search.found ? goalIndex : closestIndex;
    std::vector<game::Point> reversed;
    while (index != startIndex)
    {
        const std::uint8_t s = parent[index];
        if (s == NoParent)
            return LatticeSearch{};
        const int i = static_cast<int>(index % static_cast<size_t>(width)) + iMin;
        const int j = static_cast<int>(index / static_cast<size_t>(width)) + jMin;
        reversed.push_back(pointOf(i, j));
        index = indexOf(i - Steps[s].dx, j - Steps[s].dy);
    }
    search.nodes.assign(reversed.rbegin(), reversed.rend());
    return search;
}

} // namespace

StepPathResult FindStepPath(const LevelMap &map, game::Point start, game::Point goal, int step, int reach,
                            std::uint16_t blockMask)
{
    StepPathResult result;
    if (!map.HasCollision() || step <= 0 || !map.Bounds().Contains(start.x, start.y))
        return result;
    if (game::ChebyshevDistance(start, goal) <= reach)
    {
        result.found = true;
        return result;
    }

    const auto withoutDoors = static_cast<std::uint16_t>(blockMask & ~game::CollideDoor);
    struct Attempt {
        std::uint16_t mask;
        bool axisOnly;
        bool throughDoor;
    };
    const std::array<Attempt, 4> attempts = {{
        {blockMask, true, false},
        {blockMask, false, false},
        {withoutDoors, true, true},
        {withoutDoors, false, true},
    }};

    std::vector<game::Point> closest;
    for (const Attempt &attempt : attempts)
    {
        LatticeSearch search = SearchLattice(map, start, goal, step, reach, attempt.mask, attempt.axisOnly);
        if (search.found)
        {
            result.found = true;
            result.throughDoor = attempt.throughDoor;
            result.nodes = std::move(search.nodes);
            return result;
        }
        if (!attempt.axisOnly && !attempt.throughDoor)
            closest = std::move(search.nodes);
    }
    result.nodes = std::move(closest);
    return result;
}

size_t FurthestStraightIndex(const LevelMap &map, game::Point from, const std::vector<game::Point> &path,
                             size_t startIndex, size_t maxAhead, std::uint16_t blockMask)
{
    if (path.empty())
        return 0;
    startIndex = std::min(startIndex, path.size() - 1);
    const size_t last = std::min(path.size() - 1, startIndex + maxAhead);
    for (size_t i = last; i > startIndex; --i)
    {
        if (HasWalkableLine(map, from, path[i], blockMask))
            return i;
    }
    return startIndex;
}

const wchar_t *DirectionName(int dx, int dy)
{
    const int sx = dx > 0 ? 1 : (dx < 0 ? -1 : 0);
    const int sy = dy > 0 ? 1 : (dy < 0 ? -1 : 0);
    if (sx == 0 && sy < 0)
        return Tr(L"północ", L"north");
    if (sx == 0 && sy > 0)
        return Tr(L"południe", L"south");
    if (sx > 0 && sy == 0)
        return Tr(L"wschód", L"east");
    if (sx < 0 && sy == 0)
        return Tr(L"zachód", L"west");
    if (sx > 0 && sy < 0)
        return Tr(L"północny wschód", L"north east");
    if (sx < 0 && sy < 0)
        return Tr(L"północny zachód", L"north west");
    if (sx > 0 && sy > 0)
        return Tr(L"południowy wschód", L"south east");
    if (sx < 0 && sy > 0)
        return Tr(L"południowy zachód", L"south west");
    return Tr(L"tutaj", L"here");
}

std::wstring DescribePath(game::Point start, const std::vector<game::Point> &path)
{
    if (path.empty())
        return Tr(L"tutaj", L"here");

    std::wstring message;
    game::Point previous = start;
    int runDx = 0;
    int runDy = 0;
    int runLength = 0;

    const auto flush = [&]() {
        if (runLength == 0)
            return;
        if (!message.empty())
            message += L", ";
        message += DirectionName(runDx, runDy);
        message += L" ";
        message += std::to_wstring(runLength);
    };

    for (const game::Point &cell : path)
    {
        const int dx = cell.x - previous.x;
        const int dy = cell.y - previous.y;
        if (runLength > 0 && dx == runDx && dy == runDy)
        {
            ++runLength;
        }
        else
        {
            flush();
            runDx = dx;
            runDy = dy;
            runLength = 1;
        }
        previous = cell;
    }
    flush();
    return message.empty() ? TrS(L"tutaj", L"here") : message;
}

} // namespace d2access
