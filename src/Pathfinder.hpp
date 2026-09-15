#pragma once

#include "LevelMap.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace d2access {

struct PathOptions {
    std::uint16_t blockMask = game::CollideMaskPlayerPath;
    // Diablo Access first looks for a path made of north/south/east/west moves
    // only, because that is what the arrow keys produce.
    bool axisOnly = false;
    // The goal counts as reached within this Chebyshev distance. Objects such as
    // chests block their own cell, so they are approached from a neighbour.
    int goalRadius = 0;
    // When the goal cannot be reached, return the path to the closest cell.
    bool closestIfUnreachable = false;
    int maxExpansions = 350000;
};

struct PathResult {
    bool found = false;
    game::Point end;                 // last cell of the path
    std::vector<game::Point> cells;  // every step, the start cell excluded
};

PathResult FindPath(const LevelMap &map, game::Point start, game::Point goal, const PathOptions &options);

// Diablo Access tries axis-only first and falls back to eight directions.
PathResult FindKeyboardPath(const LevelMap &map, game::Point start, game::Point goal, PathOptions options);

bool HasWalkableLine(const LevelMap &map, game::Point from, game::Point to, std::uint16_t blockMask);

// Keyboard steps: one arrow press moves the character by exactly `step`
// subtiles. As in Diablo Access and Pokemon Access, one spoken unit in the
// path description is one key press.
bool CanTakeStep(const LevelMap &map, game::Point from, game::Point to, std::uint16_t blockMask);

struct StepPathResult {
    bool found = false;              // the goal is within `reach` of the last node
    bool throughDoor = false;        // the path needs a closed door opened
    std::vector<game::Point> nodes;  // position after each step (start excluded);
                                     // the closest reachable part when not found
};

// Path on the grid start + (i, j) * step: straight steps first, then diagonal
// ones (two arrows), then the same through closed doors.
StepPathResult FindStepPath(const LevelMap &map, game::Point start, game::Point goal, int step, int reach,
                            std::uint16_t blockMask);

// Index of the furthest cell in path[startIndex, startIndex + maxAhead] that can
// be reached in a straight line from `from`. Sending the move command there lets
// the server walk without detours.
size_t FurthestStraightIndex(const LevelMap &map, game::Point from, const std::vector<game::Point> &path,
                             size_t startIndex, size_t maxAhead, std::uint16_t blockMask);

const wchar_t *DirectionName(int dx, int dy);

// "północ 3, wschód 2" (Diablo Access AppendKeyboardWalkPathForSpeech format):
// every entry of `path` counts as one unit, so step paths are counted in steps.
std::wstring DescribePath(game::Point start, const std::vector<game::Point> &path);

} // namespace d2access
