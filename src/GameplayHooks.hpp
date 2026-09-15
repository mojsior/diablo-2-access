#pragma once

#include <Windows.h>

namespace d2access {

bool InitializeGameplayHooks();
void ShutdownGameplayHooks();
void OnGameEntered();
void OnGameLeft();
bool IsGameplayDialogActive();
void NotifyGameplayVirtualKeyState(DWORD virtualKey, bool isDown);
bool HandleGameplayVirtualKey(DWORD virtualKey);
// Keys the mod takes over in game. Enter, Space and Tab are only taken while the
// character or inventory panel is open, so chat still works otherwise.
bool IsGameplayKeyCaptured(DWORD virtualKey);

} // namespace d2access
