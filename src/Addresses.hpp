#pragma once

#include <Windows.h>

#include <cstdint>

namespace d2access {

constexpr uintptr_t GameImageBase = 0x400000;

constexpr uintptr_t RvaFromVa(uintptr_t va)
{
    return va - GameImageBase;
}

inline uintptr_t AbsoluteAddress(uintptr_t va)
{
    const auto moduleBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    return moduleBase + RvaFromVa(va);
}

namespace va {
constexpr uintptr_t FE_MainMenu_Show = 0x685E80;
constexpr uintptr_t FE_CharacterCreate_Show = 0x687D10;
constexpr uintptr_t FE_CharacterCreate_HandleClassSelection = 0x6863C0;
constexpr uintptr_t FE_CharacterCreate_ValidateName = 0x682EF0;
constexpr uintptr_t FE_CharacterCreate_SaveExists = 0x684800;
constexpr uintptr_t FE_CharacterCreate_OnCreateButton = 0x689180;
constexpr uintptr_t FE_CharacterCreate_Commit = 0x688D40;
constexpr uintptr_t FE_CharacterSelect_Show = 0x68D7A0;
constexpr uintptr_t FE_CharacterSelect_Update = 0x68B930;
constexpr uintptr_t FE_CharacterSelect_CreateNew = 0x688D20;
// "Yes" in the delete character dialog (control 216, opened by 0x68C420):
// deletes the selected character and rebuilds the selection screen.
constexpr uintptr_t FE_CharacterSelect_DeleteConfirmed = 0x68DDD0;
constexpr uintptr_t FE_MainMenu_OpenSinglePlayer = 0x688450;
constexpr uintptr_t FE_MainMenu_OpenBattleNet = 0x6884B0;
// Main menu button callbacks, stdcall(int), from the control table at 0x741D48
// (48-byte records: +0x18 string id, +0x20 callback).
constexpr uintptr_t FE_MainMenu_OpenGateway = 0x686DA0;
constexpr uintptr_t FE_MainMenu_OpenOtherMultiplayer = 0x6834F0;
constexpr uintptr_t FE_MainMenu_OpenCredits = 0x683B70;
constexpr uintptr_t FE_MainMenu_OpenCinematics = 0x683EB0;
constexpr uintptr_t FE_MainMenu_ExitGame = 0x6844C0;
// True when d2char.mpq is missing (spawn install): multiplayer buttons are disabled.
constexpr uintptr_t FE_IsSpawnInstall = 0x4EB520;
constexpr uintptr_t UI_ControlSetActive = 0x4EA000;
constexpr uintptr_t UI_ControlIsEnabled = 0x4EA020;
constexpr uintptr_t UI_ControlSetEnabled = 0x4EA050;
constexpr uintptr_t UI_FocusControl = 0x4E9B00;

constexpr uintptr_t Global_SelectedClassIndex = 0x745BBC;
constexpr uintptr_t Global_CreateButton = 0x97F628;
constexpr uintptr_t Global_NameField = 0x97F644;
constexpr uintptr_t Global_CharacterCreateState = 0x97F8CC;
constexpr uintptr_t Global_FrontendResult = 0x97F8E0;
constexpr uintptr_t Global_FrontendCreateMode = 0x97F8E4;
constexpr uintptr_t Global_CharacterSelectSelectedIndex = 0x745BC4;
constexpr uintptr_t Global_CharacterSelectVisibleCount = 0x745BD0;
constexpr uintptr_t Global_CharacterSelectHead = 0x9800B4;
constexpr uintptr_t Global_CharacterSelectCount = 0x9800BC;

constexpr uintptr_t Global_ClassButtonBarbarian = 0x97F864;
constexpr uintptr_t Global_ClassButtonNecromancer = 0x97F868;
constexpr uintptr_t Global_ClassButtonAmazon = 0x97F86C;
constexpr uintptr_t Global_ClassButtonSorceress = 0x97F870;
constexpr uintptr_t Global_ClassButtonPaladin = 0x97F874;

constexpr uintptr_t Global_CurrentPlayerUnit = 0x79D0B0;
constexpr uintptr_t Global_NpcInteractionActive = 0x7B7369;
constexpr uintptr_t Global_NpcMenuMode = 0x7B7371;
constexpr uintptr_t Global_NpcMenu = 0x7B739F;
constexpr uintptr_t Global_NpcMenuUnitClassId = 0x7B7421;
constexpr uintptr_t Global_DialogActive = 0x7B7498;

constexpr uintptr_t Game_SetCurrentPlayerUnit = 0x451180;
constexpr uintptr_t Game_RenderUnit = 0x56C380;

constexpr uintptr_t IAT_GetKeyState = 0x6CD3CC;
constexpr uintptr_t IAT_GetAsyncKeyState = 0x6CD3D0;
constexpr uintptr_t IAT_DispatchMessageA = 0x6CD3E8;
constexpr uintptr_t IAT_PeekMessageA = 0x6CD3F0;
constexpr uintptr_t IAT_GetMessageA = 0x6CD3F4;
constexpr uintptr_t IAT_TranslateAcceleratorA = 0x6CD420;
} // namespace va

} // namespace d2access
