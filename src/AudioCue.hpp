#pragma once

#include <cstdint>
#include <vector>

namespace d2access {

enum class CueId {
    InteractionPossible,
    Monster,
    Chest,
    Door,
    Item,
    Stairs,
    Footstep,
    Weapon,
    Armor,
    Gold,
    Potion,
    Scroll,
    Count,
};

void InitializeAudioCue();
void ShutdownAudioCue();

// Centred cue at full volume.
void PlayCue(CueId cue);

// Proximity emitters as in Diablo Access (engine/sound_pool.cpp): at most three
// emitters, each restarted every `intervalMs`, a new one plays at once. Volume
// and pan use the DevilutionX units: logVolume in [-6400, 0], logPan in
// [-6400, 6400]. Call from the game thread.
struct EmitterRequest {
    std::uint32_t emitterId = 0;
    CueId sound = CueId::Monster;
    int logVolume = 0;
    int logPan = 0;
    std::uint32_t intervalMs = 0;
};

void UpdateEmitters(const std::vector<EmitterRequest> &emitters);
void StopEmitters();
// One-shot navigation cue, not counted in the emitter limit.
void PlayOneShot(CueId sound, int logVolume, int logPan, bool stopEmitters);

} // namespace d2access
