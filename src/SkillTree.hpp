#pragma once

#include "D2Game.hpp"

#include <Windows.h>

#include <cstdint>

// Skill tree panel (T) presented as a tree view: the three class tabs are the
// top-level nodes and each one expands to its skills. Everything except
// IsSkillTreeOpenForKeys runs on the game thread.
namespace d2access::skilltree {

void ResetSkillTree();

// Once per game tick: announces the panel when it opens and sends queued
// skill points.
void UpdateSkillTree(uintptr_t playerUnit);

bool IsSkillTreeFocused();
// Safe from the keyboard hook thread.
bool IsSkillTreeOpenForKeys();

// Returns true when the skill tree consumed the key.
bool HandleSkillTreeKey(uintptr_t playerUnit, DWORD virtualKey, bool ctrl, bool shift);

} // namespace d2access::skilltree
