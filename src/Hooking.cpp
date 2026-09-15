#include "Hooking.hpp"

#include <cstring>

namespace d2access {

namespace {

void RelocateCopiedRelativeBranches(std::uint8_t *gateway, const std::uint8_t *target,
                                    size_t patchSize)
{
    for (size_t offset = 0; offset + 5 <= patchSize; ++offset)
    {
        const std::uint8_t opcode = gateway[offset];
        if (opcode != 0xE8 && opcode != 0xE9)
            continue;

        const auto oldRel = *reinterpret_cast<std::int32_t *>(gateway + offset + 1);
        const auto originalNext =
            reinterpret_cast<std::uintptr_t>(target + offset + 5);
        const auto originalDestination = originalNext + oldRel;
        const auto gatewayNext =
            reinterpret_cast<std::uintptr_t>(gateway + offset + 5);

        *reinterpret_cast<std::int32_t *>(gateway + offset + 1) =
            static_cast<std::int32_t>(originalDestination - gatewayNext);
    }
}

} // namespace

bool InstallInlineHook(void *target, void *detour, size_t patchSize, InlineHook &hook)
{
    if (patchSize < 5 || patchSize > hook.originalBytes.size() || target == nullptr ||
        detour == nullptr)
        return false;

    auto *gateway = static_cast<std::uint8_t *>(
        VirtualAlloc(nullptr, patchSize + 5, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (gateway == nullptr)
        return false;

    std::memcpy(gateway, target, patchSize);
    std::memcpy(hook.originalBytes.data(), target, patchSize);
    RelocateCopiedRelativeBranches(gateway, static_cast<const std::uint8_t *>(target), patchSize);
    gateway[patchSize] = 0xE9;

    const auto gatewaySrc = reinterpret_cast<std::uintptr_t>(gateway + patchSize);
    const auto returnDst = reinterpret_cast<std::uintptr_t>(target) + patchSize;
    *reinterpret_cast<std::int32_t *>(gateway + patchSize + 1) =
        static_cast<std::int32_t>(returnDst - (gatewaySrc + 5));

    DWORD oldProtect = 0;
    if (!VirtualProtect(target, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        VirtualFree(gateway, 0, MEM_RELEASE);
        return false;
    }

    auto *targetBytes = static_cast<std::uint8_t *>(target);
    targetBytes[0] = 0xE9;
    *reinterpret_cast<std::int32_t *>(targetBytes + 1) =
        static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(detour) -
                                  (reinterpret_cast<std::uintptr_t>(target) + 5));

    for (size_t i = 5; i < patchSize; ++i)
        targetBytes[i] = 0x90;

    FlushInstructionCache(GetCurrentProcess(), target, patchSize);
    VirtualProtect(target, patchSize, oldProtect, &oldProtect);

    hook.target = target;
    hook.detour = detour;
    hook.trampoline = gateway;
    hook.patchSize = patchSize;
    hook.installed = true;
    return true;
}

void RemoveInlineHook(InlineHook &hook)
{
    if (!hook.installed || hook.target == nullptr || hook.trampoline == nullptr)
        return;

    DWORD oldProtect = 0;
    if (VirtualProtect(hook.target, hook.patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        std::memcpy(hook.target, hook.originalBytes.data(), hook.patchSize);
        FlushInstructionCache(GetCurrentProcess(), hook.target, hook.patchSize);
        VirtualProtect(hook.target, hook.patchSize, oldProtect, &oldProtect);
    }

    VirtualFree(hook.trampoline, 0, MEM_RELEASE);
    hook = {};
}

} // namespace d2access
