# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.2.0)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.2.0-a4)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
cmake_minimum_required(VERSION 3.20)

# -----------------------------------------------------------------------------
# FaultyCat v3 firmware — top-level build file.
#
# Two build modes, gated by options so a single tree can produce:
#   * firmware UF2 for the RP2040 on FaultyCat v2.x (FAULTYCAT_BUILD_FIRMWARE)
#   * host-side unit tests for the HAL fakes (FAULTYCAT_BUILD_TESTS)
# Use a CMakePreset (fw-debug / fw-release / host-tests) to pick one.
# -----------------------------------------------------------------------------

option(FAULTYCAT_BUILD_FIRMWARE "Build the RP2040 firmware UF2"   ON)
option(FAULTYCAT_BUILD_TESTS    "Build the host-side unit tests" OFF)

if(FAULTYCAT_BUILD_FIRMWARE AND FAULTYCAT_BUILD_TESTS)
    message(FATAL_ERROR
        "FAULTYCAT_BUILD_FIRMWARE and FAULTYCAT_BUILD_TESTS are mutually "
        "exclusive — cross-compile vs. native toolchain. Pick one preset.")
endif()

# -----------------------------------------------------------------------------
# Firmware mode — must import pico-sdk BEFORE project().
# -----------------------------------------------------------------------------
if(FAULTYCAT_BUILD_FIRMWARE)
    set(PICO_SDK_PATH "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pico-sdk"
        CACHE PATH "Path to the vendored pico-sdk" FORCE)
    set(PICO_BOARD pico CACHE STRING "Target pico board (RP2040 = pico)")
    # pico-sdk's importer must run before project(); it injects toolchain bits.
    include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()

project(faultycat
    # 4-segment version (MAJOR.MINOR.PATCH.TWEAK). Bumped in-place by
    # the release workflow's `sed` step on every `vX.Y.Z.W` tag push;
    # mirrored to host/faultycmd-py/pyproject.toml and
    # host/faultycmd-py/src/faultycmd/__init__.py. The workflow rejects
    # tags that don't match the 4-segment shape.
    VERSION      3.0.0.5
    LANGUAGES    C CXX ASM
    DESCRIPTION  "FaultyCat v3 firmware (rewrite, runs on HW v2.x)"
)

# -----------------------------------------------------------------------------
# Generated firmware_version.h — single point where the version
# propagates from CMake's PROJECT_VERSION_* (filled from the
# `project(... VERSION ...)` line above, which the release workflow
# `sed`s on every `vX.Y.Z.W` tag) into C code. Exposed as an
# INTERFACE library so any target that needs the macros just links
# `firmware_version` and `#include <firmware_version.h>`. Configured
# for both firmware and host-test builds so the Unity tests can
# sanity-check the PING reply against the expected version too.
# -----------------------------------------------------------------------------
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/firmware_version.h.in
    ${CMAKE_BINARY_DIR}/generated/firmware_version.h
    @ONLY
)
add_library(firmware_version INTERFACE)
target_include_directories(firmware_version INTERFACE
    ${CMAKE_BINARY_DIR}/generated
)

# -----------------------------------------------------------------------------
# Firmware wire-up — after project().
# -----------------------------------------------------------------------------
if(FAULTYCAT_BUILD_FIRMWARE)
    pico_sdk_init()
    add_subdirectory(hal)
    add_subdirectory(drivers)
    add_subdirectory(services)
    add_subdirectory(usb)
    add_subdirectory(apps/faultycat_fw)
endif()

# -----------------------------------------------------------------------------
# Host-test mode — placeholder. Real test harnesses are added in F1.
# -----------------------------------------------------------------------------
if(FAULTYCAT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
