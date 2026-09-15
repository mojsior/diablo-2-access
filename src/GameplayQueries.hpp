#pragma once

#include <Windows.h>

#include <nlohmann/json.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

// Read-only views of the gameplay state for the MCP server. The Query*
// functions touch game memory and must run on the game thread, so callers go
// through QueryGameThread.
namespace d2access {

bool IsInGame();
std::optional<nlohmann::json> QueryGameThread(std::function<nlohmann::json()> query, DWORD timeoutMs);

nlohmann::json QueryStatus();
nlohmann::json QueryTargets(const std::string &category);
nlohmann::json QueryLevel();
nlohmann::json QueryPath(std::optional<int> x, std::optional<int> y, const std::string &targetKey);
nlohmann::json QueryCollision(std::optional<int> x, std::optional<int> y, int radius);
nlohmann::json QueryUnits(int radius);
std::vector<std::string> TargetCategoryNames();

// Safe from any thread.
bool IsGameplayKey(DWORD virtualKey);
void InjectGameplayKey(DWORD virtualKey, bool ctrl, bool shift);
void SetInjectedKeyHeld(DWORD virtualKey, bool held);

} // namespace d2access
