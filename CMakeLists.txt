cmake_minimum_required(VERSION 3.15)
project(SuperAdjustmentStudio VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# submodules
#   LExSDKv2
message(STATUS "Adding build target: thirdparty/LExSDKv2")
# disabling LE1 and LE3 for now, might reintroduce after
set(LESDK_WITH_LE1 OFF)
set(LESDK_WITH_LE3 OFF)

# Patch the generated SDK in the submodule working tree: parms structs the
# generator packed tighter than the game's real layouts (ProcessEvent then
# writes past them - /RTC stack corruption in Debug, wrong values in
# Release), and the "FunctionFlags |= ~0x400" generator typo (should be
# "&= ~0x400": clear FUNC_Native, run bytecode, restore after the call).
# See patches/LExSDKv2/enumerate.py.
set(LExSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/LExSDKv2")
set(LExSDK_PATCH "${CMAKE_CURRENT_SOURCE_DIR}/patches/LExSDKv2/fix-sdk-layout.patch")
execute_process(
    COMMAND git -C "${LExSDK_DIR}" apply --check "${LExSDK_PATCH}"
    RESULT_VARIABLE LExSDK_PATCH_FORWARD
    OUTPUT_QUIET ERROR_QUIET
)
if (LExSDK_PATCH_FORWARD EQUAL 0)
    execute_process(
        COMMAND git -C "${LExSDK_DIR}" apply "${LExSDK_PATCH}"
        RESULT_VARIABLE LExSDK_PATCH_APPLY
    )
    if (NOT LExSDK_PATCH_APPLY EQUAL 0)
        message(FATAL_ERROR "Applying patches/LExSDKv2/fix-sdk-layout.patch failed.")
    endif()
    message(STATUS "Applied LExSDKv2 patch: fix-sdk-layout.patch")
else()
    execute_process(
        COMMAND git -C "${LExSDK_DIR}" apply --check --reverse "${LExSDK_PATCH}"
        RESULT_VARIABLE LExSDK_PATCH_REVERSE
        OUTPUT_QUIET ERROR_QUIET
    )
    if (LExSDK_PATCH_REVERSE EQUAL 0)
        message(STATUS "LExSDKv2 patch already applied")
    else()
        message(FATAL_ERROR "patches/LExSDKv2/fix-sdk-layout.patch neither applies nor is applied - the submodule changed. Regenerate: python patches/LExSDKv2/enumerate.py --apply && git -C thirdparty/LExSDKv2 diff --output=../../patches/LExSDKv2/fix-sdk-layout.patch")
    endif()
endif()

add_subdirectory(thirdparty/LExSDKv2)

message(STATUS "Adding build target: thirdparty/spdlog")
set(SPDLOG_BUILD_EXAMPLE OFF)
set(SPDLOG_BUILD_TESTS OFF)
set(SPDLOG_BUILD_BENCH OFF)
set(SPDLOG_INSTALL OFF)
add_subdirectory(thirdparty/spdlog)

#   imgui
message(STATUS "Adding build target: thirdparty/imgui")
# explicitly list the source files
file(GLOB IMGUI_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/imgui.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/imgui_draw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/imgui_tables.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/imgui_widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/misc/cpp/imgui_stdlib.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_dx11.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/backends/imgui_impl_win32.cpp
)
add_library(imgui STATIC ${IMGUI_SOURCES})
target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui 
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui/backends
)
target_compile_definitions(imgui PUBLIC IMGUI_IMPL_OPENGL_LOADER_GLAD=0)

#   kiero (D3D11 swapchain hooking, bundles MinHook for the actual detour)
message(STATUS "Adding build target: thirdparty/kiero")

# kiero.h hardcodes these flags to 0, and it #defines them unconditionally so
# they can't be overridden on the command line. Patch the header at configure
# time instead of relying on a manual submodule edit (idempotent: only writes
# when the flags are not already enabled, so `git submodule update` followed by
# a reconfigure re-applies it automatically).
set(KIERO_H_PATH "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/kiero/kiero.h")
file(READ "${KIERO_H_PATH}" KIERO_H_CONTENT)
if (NOT KIERO_H_CONTENT MATCHES "KIERO_INCLUDE_D3D11[ \t]+1")
    string(REPLACE "#define KIERO_INCLUDE_D3D11  0" "#define KIERO_INCLUDE_D3D11  1" KIERO_H_CONTENT "${KIERO_H_CONTENT}")
    string(REPLACE "#define KIERO_USE_MINHOOK    0" "#define KIERO_USE_MINHOOK    1" KIERO_H_CONTENT "${KIERO_H_CONTENT}")
    file(WRITE "${KIERO_H_PATH}" "${KIERO_H_CONTENT}")
    message(STATUS "kiero.h: enabled D3D11 + MinHook flags")
else()
    message(STATUS "kiero.h: D3D11 + MinHook flags already enabled")
endif()

add_library(kiero STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/kiero/kiero.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/kiero/minhook/src/buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/kiero/minhook/src/hook.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/kiero/minhook/src/trampoline.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/kiero/minhook/src/hde/hde32.c
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/kiero/minhook/src/hde/hde64.c
)
target_include_directories(kiero PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/kiero
)

# SAS targets
file(GLOB SAS_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/**/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/resources/*.rc
)
message(STATUS "Adding build target: SuperAdjustmentStudio")
add_library(SuperAdjustmentStudio SHARED ${SAS_SOURCES})

target_include_directories(SuperAdjustmentStudio PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui_helpers
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/LExSDKv2/Src
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/LExSDKv2/Src/LESDK
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/IconFontCppHeaders
)

# link dependencies

if (TARGET LESDK-LE1)
    target_link_libraries(SuperAdjustmentStudio PRIVATE LESDK-LE1)
elseif (TARGET LESDK-LE2)
    target_link_libraries(SuperAdjustmentStudio PRIVATE LESDK-LE2)
elseif (TARGET LESDK-LE3)
    target_link_libraries(SuperAdjustmentStudio PRIVATE LESDK-LE3)
else()
    message(FATAL_ERROR "No LESDK target found. Please check the LExSDKv2 submodule.")
endif()

if (TARGET spdlog)
    target_link_libraries(SuperAdjustmentStudio PRIVATE spdlog)
else()
    message(FATAL_ERROR "No spdlog target found. Please check the spdlog submodule.")
endif()

if (TARGET imgui)
    target_link_libraries(SuperAdjustmentStudio PRIVATE imgui)
else()
    message(FATAL_ERROR "No imgui target found. Please check the imgui submodule.")
endif()

if (TARGET kiero)
    target_link_libraries(SuperAdjustmentStudio PRIVATE kiero)
else()
    message(FATAL_ERROR "No kiero target found. Please check the kiero submodule.")
endif()


if (WIN32)
# LE uses DirectX 11, so we need to link against the appropriate libraries
# d3d11/dxgi for DX11 render backend and swapchain hook
# d3dcompiler for the raw-D3D11 overlay gizmo shader compilation
target_link_libraries(SuperAdjustmentStudio PRIVATE d3d11 dxgi d3dcompiler)
target_compile_definitions(SuperAdjustmentStudio PRIVATE "WIN32_LEAN_AND_MEAN")
endif()

set_target_properties(SuperAdjustmentStudio PROPERTIES
PREFIX "SAS_"
OUTPUT_NAME "SuperAdjustmentStudio"
)

# ensure SAS is compiled with C++20 standard
target_compile_features(SuperAdjustmentStudio PUBLIC cxx_std_20)

# LE2 target 
target_compile_definitions(SuperAdjustmentStudio PRIVATE SDK_TARGET_LE2)

if (MSVC)
    # ensure SAS is using MSVC conformance flags when MSVC is used
    target_compile_options(SuperAdjustmentStudio PRIVATE /std:c++20 /permissive /Zc:__cplusplus /wd4191 /wd4005 /EHa)
    # force to use the LESDK precompiled headers when MSVC is used
    target_precompile_headers(SuperAdjustmentStudio PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/LExSDKv2/Src/LESDK/_Global.pch.hpp")

    # parallel compilation, at least 8 processes regardless of detected core count
    include(ProcessorCount)
    ProcessorCount(SAS_CORE_COUNT)
    if (SAS_CORE_COUNT LESS 8)
        set(SAS_CORE_COUNT 8)
    endif()
    target_compile_options(SuperAdjustmentStudio PRIVATE /MP${SAS_CORE_COUNT})
endif()

target_compile_definitions(SuperAdjustmentStudio PRIVATE $<$<CONFIG:Debug>:SAS_DEBUG>)
target_compile_options(SuperAdjustmentStudio PRIVATE $<$<CONFIG:Debug>:/Zi /Od>)
target_link_options(SuperAdjustmentStudio PRIVATE $<$<CONFIG:Debug>:/DEBUG>)

install(TARGETS SuperAdjustmentStudio DESTINATION bin)