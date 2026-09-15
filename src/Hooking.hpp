#pragma once

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace d2access {

struct InlineHook {
    void *target = nullptr;
    void *detour = nullptr;
    void *trampoline = nullptr;
    std::array<std::uint8_t, 32> originalBytes = {};
    size_t patchSize = 0;
    bool installed = false;
};

bool InstallInlineHook(void *target, void *detour, size_t patchSize, InlineHook &hook);
void RemoveInlineHook(InlineHook &hook);

} // namespace d2access
