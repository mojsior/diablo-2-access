cmake_minimum_required(VERSION 3.21)

project(D2AccessOriginal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(D2AccessHook SHARED
    src/AudioCue.cpp
    src/D2AccessHook.cpp
    src/D2Game.cpp
    src/EventLog.cpp
    src/FrontendHooks.cpp
    src/GameplayHooks.cpp
    src/Hooking.cpp
    src/LevelMap.cpp
    src/Localization.cpp
    src/Logging.cpp
    src/McpServer.cpp
    src/MenuAccess.cpp
    src/Panels.cpp
    src/SkillTree.cpp
    src/Pathfinder.cpp
    src/ScreenReader.cpp
)

if(MSVC)
    target_compile_options(D2AccessHook PRIVATE /utf-8 /bigobj)
endif()

target_compile_definitions(D2AccessHook PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_include_directories(D2AccessHook PRIVATE src third_party)
target_link_libraries(D2AccessHook PRIVATE winmm ole32 sapi shlwapi ws2_32)

set_target_properties(D2AccessHook PROPERTIES
    OUTPUT_NAME "D2AccessHook"
)

add_executable(D2AccessLauncher
    src/D2AccessLauncher.cpp
    src/EventLog.cpp
    src/Logging.cpp
)

target_compile_definitions(D2AccessLauncher PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_include_directories(D2AccessLauncher PRIVATE src)
target_link_libraries(D2AccessLauncher PRIVATE shlwapi)

# Accessible installer for classic Diablo II and the mod. The NVDA controller
# client is embedded so the assistant can be downloaded as a single file.
set(NVDA_CLIENT_DLL "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvda/nvdaControllerClient32.dll")
configure_file(src/D2AccessSetup.rc.in "${CMAKE_CURRENT_BINARY_DIR}/D2AccessSetup.rc" @ONLY)

add_executable(D2AccessSetup
    src/D2AccessSetup.cpp
    src/EventLog.cpp
    src/Logging.cpp
    src/ScreenReader.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/D2AccessSetup.rc"
)

if(MSVC)
    target_compile_options(D2AccessSetup PRIVATE /utf-8)
    target_link_options(D2AccessSetup PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
endif()

target_compile_definitions(D2AccessSetup PRIVATE
    NOMINMAX
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
)

target_include_directories(D2AccessSetup PRIVATE src third_party)
target_link_libraries(D2AccessSetup PRIVATE shlwapi ole32 sapi comdlg32 winhttp)

add_custom_command(TARGET D2AccessHook POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:D2AccessHook>/audio"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio"
            "$<TARGET_FILE_DIR:D2AccessHook>/audio"
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvda/nvdaControllerClient32.dll")
    add_custom_command(TARGET D2AccessHook POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nvda/nvdaControllerClient32.dll"
                "$<TARGET_FILE_DIR:D2AccessHook>/nvdaControllerClient32.dll"
    )
endif()
