# dead_or_alive_4 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(dead_or_alive_4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(DEADORALIVE4_SOURCES
    src/main.cpp
    src/doa4_app_hooks.cpp
    src/doa4_debug_hooks.cpp
    src/doa4_diag_overlay.cpp
    src/doa4_config.cpp
    src/doa4_menu.cpp
    src/doa4_unlocks.cpp
    src/doa4_tweaks.cpp
    src/doa4_backup.cpp
    src/doa4_extras.cpp
    src/doa4_cvars.cpp
    src/doa4_graphics.cpp
    src/doa4_photo.cpp
    src/doa4_photo_win.cpp
    src/doa4_photo_service.cpp
    src/doa4_photo_notice.cpp
    src/doa4_photo_watch.cpp
    src/doa4_photo_hook.cpp
    src/doa4_xdvdfs.cpp
    src/doa4_gamefiles.cpp
    src/doa4_setup_flow.cpp
    src/doa4_setup_win.cpp
    src/doa4_setup_dialog.cpp
)

if(WIN32)
    # The window, taskbar, Explorer and Task Manager icon (src/dead_or_alive_4.rc).
    enable_language(RC)
    add_executable(dead_or_alive_4 WIN32 ${DEADORALIVE4_SOURCES} src/dead_or_alive_4.rc)
else()
    add_executable(dead_or_alive_4 ${DEADORALIVE4_SOURCES})
endif()

rexglue_setup_target(dead_or_alive_4)

# Local build fix: the ReXGlue SDK at v0.8.0 does not pass ImGui's headers on to the
# game target (src/doa4_diag_overlay.cpp includes <imgui.h>); ImGui itself is
# already compiled into the SDK's UI library.
if(TARGET imgui)
    target_include_directories(dead_or_alive_4 PRIVATE $<TARGET_PROPERTY:imgui,INTERFACE_INCLUDE_DIRECTORIES>)
endif()
# Local build fix: with REXSDK_DIR (the SDK built from source alongside the game), the
# SDK's generated headers (rex/version.h) sit in its build folder, which the game
# target's own SDK sources (ui/rex_app.cpp) do not search.
if(REXSDK_DIR)
    target_include_directories(dead_or_alive_4 PRIVATE ${CMAKE_BINARY_DIR}/rexglue-sdk/include)
endif()
