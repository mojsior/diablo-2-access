#pragma once

#include "D2Game.hpp"

#include <Windows.h>

#include <cstdint>

// Quest log (Q). The game draws it as a page of icons, so the mod reads the
// entries the game builds for the selected act and walks them with the arrows.
// Everything except IsQuestLogOpenForKeys runs on the game thread.
namespace d2access::questlog {

void ResetQuestLog();

// Once per game tick: announces the quest log when it opens.
void UpdateQuestLog();

bool IsQuestLogOpen();
// Safe from the keyboard hook thread.
bool IsQuestLogOpenForKeys();

// Opens or closes the quest log, like the game's own key does.
void ToggleQuestLog();

// Returns true when the quest log consumed the key.
bool HandleQuestLogKey(DWORD virtualKey, bool ctrl, bool shift);

} // namespace d2access::questlog
