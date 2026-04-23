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
    VERSION      3.0.0
    LANGUAGES    C CXX ASM
    DESCRIPTION  "FaultyCat v3 firmware (rewrite, runs on HW v2.x)"
)

# -----------------------------------------------------------------------------
# Firmware wire-up — after project().
# -----------------------------------------------------------------------------
if(FAULTYCAT_BUILD_FIRMWARE)
    pico_sdk_init()
    add_subdirectory(apps/faultycat_fw)
endif()

# -----------------------------------------------------------------------------
# Host-test mode — placeholder. Real test harnesses are added in F1.
# -----------------------------------------------------------------------------
if(FAULTYCAT_BUILD_TESTS)
    enable_testing()
    message(STATUS
        "FaultyCat: host-test mode active. No tests wired yet — F1 will "
        "add the HAL fakes + unit tests under tests/.")
endif()

# -----------------------------------------------------------------------------
# third_party/faultier  — REFERENCE ONLY, NEVER BUILT.
#
# Upstream has no LICENSE file. See LICENSES/NOTICE-faultier.md. It stays
# in the tree so humans can read the code while we reimplement the
# glitcher architecture from scratch in services/glitch_engine/crowbar/.
# -----------------------------------------------------------------------------
# (deliberately no add_subdirectory)
