#pragma once

#include <Windows.h>

// In-game Escape menu and its option sub-menus (sound, video, automap).
// Everything except IsGameMenuOpenForKeys runs on the game thread.
namespace d2access::gamemenu {

void ResetGameMenu();

// Once per game tick: announces the menu when it opens or changes, and the
// selected entry when the mouse moves the highlight.
void UpdateGameMenu();

bool IsGameMenuOpen();
// Safe from the keyboard hook thread.
bool IsGameMenuOpenForKeys();

// Up/Down select, Enter activates, Left/Right change options and sliders.
// Returns true when the key was consumed.
bool HandleGameMenuKey(DWORD virtualKey, bool ctrl, bool shift);

} // namespace d2access::gamemenu
