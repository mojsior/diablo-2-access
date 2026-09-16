#pragma once

#include "D2Game.hpp"

#include <Windows.h>

// "Configure controls" screen of the in-game options menu. The game draws it as
// a picture and reacts only to the mouse, so the mod walks its rows itself.
// Everything except IsControlsScreenOpenForKeys and IsWaitingForKeyBinding runs
// on the game thread.
namespace d2access::controls {

void ResetControls();

// Once per game tick: announces the screen when it opens and the result of a
// key assignment.
void UpdateControls();

bool IsControlsScreenOpen();
// Safe from the keyboard hook thread.
bool IsControlsScreenOpenForKeys();
// While the game waits for the new key, every key belongs to the game.
bool IsWaitingForKeyBinding();

// Returns true when the screen consumed the key.
bool HandleControlsKey(DWORD virtualKey, bool ctrl, bool shift);

} // namespace d2access::controls
