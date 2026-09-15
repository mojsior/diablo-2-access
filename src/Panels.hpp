#pragma once

#include "D2Game.hpp"

#include <Windows.h>

#include <cstdint>
#include <string>

// Character sheet and inventory accessibility, modelled on Diablo Access
// (panels/charpanel.cpp, controls/plrctrls.cpp, accessibility_keys.cpp).
// Everything except IsPanelOpenForKeys runs on the game thread.
namespace d2access::panels {

struct PanelKey {
    DWORD virtualKey = 0;
    bool ctrl = false;
    bool shift = false;
};

// Removes colour codes and grammar tags from game text and joins its lines.
std::wstring CleanText(const std::wstring &text);

void ResetPanels();

// Once per game tick: announces panels that open, sends queued stat points and
// reports the result of item moves.
void UpdatePanels(uintptr_t playerUnit, game::Point playerPosition);

bool IsAnyPanelFocused();
// Safe from the keyboard hook thread.
bool IsPanelOpenForKeys();

// Returns true when a focused panel consumed the key.
bool HandlePanelKey(uintptr_t playerUnit, game::Point playerPosition, const PanelKey &key);

// Z: health percentage, Shift+Z: mana percentage.
void SpeakHealth(uintptr_t playerUnit, bool mana);
// X: percentage of experience still missing to the next level.
void SpeakExperience(uintptr_t playerUnit);

} // namespace d2access::panels
