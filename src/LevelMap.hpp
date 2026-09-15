#pragma once

#include "D2Game.hpp"

#include <cstdint>
#include <vector>

namespace d2access {

struct LevelPreset {
    std::uint32_t unitType = 0;
    std::uint32_t classId = 0;
    game::Point position;
};

struct LevelExit {
    int destinationLevel = 0;
    game::Point position; // standable cell inside the current level
    game::Point beyond;   // a point past the level border, walked to on arrival
    bool isWarpTile = false;
};

// Whole-level knowledge for the current level: collision for every room (rooms
// that are not loaded are activated briefly with AddRoomData, the way D2BS builds
// its collision map), preset units and the transitions to other levels.
// Everything here must run on the game thread.
class LevelMap {
public:
    void Reset();

    // Call once per frame. Returns true when the player entered another level.
    bool Update(uintptr_t act, uintptr_t playerRoom1);

    // Copies collision from every loaded room of this level (doors, objects).
    void RefreshLiveCollision(uintptr_t act);

    [[nodiscard]] int LevelId() const { return levelId_; }
    [[nodiscard]] bool Ready() const { return ready_; }
    [[nodiscard]] bool HasCollision() const { return !mask_.empty(); }
    [[nodiscard]] const game::Rect &Bounds() const { return bounds_; }
    [[nodiscard]] const std::vector<LevelPreset> &Presets() const { return presets_; }
    [[nodiscard]] const std::vector<LevelExit> &Exits() const { return exits_; }
    [[nodiscard]] size_t RoomCount() const { return room2s_.size(); }
    [[nodiscard]] size_t RoomsWithCollision() const { return roomsWithCollision_; }

    [[nodiscard]] std::uint16_t MaskAt(int x, int y) const;
    [[nodiscard]] bool IsBlocked(int x, int y, std::uint16_t blockMask) const
    {
        return (MaskAt(x, y) & blockMask) != 0;
    }
    // A player occupies a cross of five subtiles (COLLISION_CheckMaskWithPattern).
    [[nodiscard]] bool IsStandable(int x, int y, std::uint16_t blockMask) const;
    [[nodiscard]] bool FindNearestStandable(game::Point around, int radius, std::uint16_t blockMask,
                                            game::Point &result) const;

private:
    struct ExitCandidate {
        LevelExit exit;
    };

    void BeginLevel(uintptr_t level, int levelId, uintptr_t act);
    void ProcessRoom2(uintptr_t act, uintptr_t room2, uintptr_t hintRoom1);
    void CopyCollision(uintptr_t room1);
    void ReadPresets(uintptr_t room2, const game::Rect &tiles, std::vector<int> &warpTileLevels);
    void ReadAdjacentExits(uintptr_t act, uintptr_t room2, const game::Rect &tiles, uintptr_t hintRoom1);
    void ReadRoomTileExits(uintptr_t room2, const game::Rect &tiles, const std::vector<int> &warpTileLevels);
    void FinalizeExits();
    uintptr_t FindRoom1(uintptr_t act, uintptr_t room2) const;

    uintptr_t act_ = 0;
    uintptr_t level_ = 0;
    int levelId_ = 0;
    bool ready_ = false;
    game::Rect bounds_;
    std::vector<std::uint16_t> mask_;
    std::vector<uintptr_t> room2s_;
    size_t scanIndex_ = 0;
    size_t roomsWithCollision_ = 0;
    std::vector<LevelPreset> presets_;
    std::vector<ExitCandidate> candidates_;
    std::vector<LevelExit> exits_;
};

} // namespace d2access
