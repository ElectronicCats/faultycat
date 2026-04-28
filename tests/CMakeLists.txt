# tests/ — host-side unit tests.
#
# Reached only under the host-tests preset (native toolchain, no pico).
# The FAULTYCAT_BUILD_TESTS guard in the root CMakeLists.txt gates us.

# Unity lives vendored as a submodule and exposes the `unity` target.
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/Unity Unity_build)

# hal_fake/ provides the native-side implementation of the HAL headers
# plus inspection hooks the tests read.
add_subdirectory(hal_fake)

# Small helper: each test_<name>.c becomes a ctest case linked against
# Unity and the native HAL fake.
function(faultycat_add_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# Driver tests compile the driver source into the test binary so it
# links against hal_fake (not hal_rp2040). The driver source lives at
# drivers/<driver_name>/<driver_name>.c and its public header at
# drivers/<driver_name>/<driver_name>.h.
function(faultycat_add_driver_test name driver_name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/drivers/${driver_name}/${driver_name}.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/drivers/${driver_name}
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

function(faultycat_add_service_emfi_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/drivers/emfi_pulse/emfi_pulse.c
        ${CMAKE_SOURCE_DIR}/drivers/hv_charger/hv_charger.c
        ${CMAKE_SOURCE_DIR}/drivers/ext_trigger/ext_trigger.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_pio.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_capture.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi/emfi_campaign.c
        ${CMAKE_SOURCE_DIR}/services/host_proto/emfi_proto/emfi_proto.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/drivers/emfi_pulse
        ${CMAKE_SOURCE_DIR}/drivers/hv_charger
        ${CMAKE_SOURCE_DIR}/drivers/ext_trigger
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/emfi
        ${CMAKE_SOURCE_DIR}/services/host_proto/emfi_proto
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# F1 — HAL unit tests
faultycat_add_test(test_gpio)
faultycat_add_test(test_time)

# F4-1 — hal/pio unit tests
faultycat_add_test(test_hal_pio)

# F4-2 — hal/dma unit tests
faultycat_add_test(test_hal_dma)

# F2a — driver unit tests
faultycat_add_driver_test(test_ui_leds        ui_leds)
faultycat_add_driver_test(test_ui_buttons     ui_buttons)
faultycat_add_driver_test(test_target_monitor target_monitor)
faultycat_add_driver_test(test_scanner_io     scanner_io)
faultycat_add_driver_test(test_ext_trigger    ext_trigger)

# F2b — driver unit tests (HV domain)
faultycat_add_driver_test(test_crowbar_mosfet crowbar_mosfet)
faultycat_add_driver_test(test_hv_charger     hv_charger)
faultycat_add_driver_test(test_emfi_pulse     emfi_pulse)

# F4-3 — emfi_pio service unit tests
faultycat_add_service_emfi_test(test_emfi_pio)
# F4-4 — emfi_capture service unit tests
faultycat_add_service_emfi_test(test_emfi_capture)
# F4-5 — emfi_campaign state machine
faultycat_add_service_emfi_test(test_emfi_campaign)
# F4-6 — emfi_proto host framing
faultycat_add_service_emfi_test(test_emfi_proto)

# Crowbar test helper: links the crowbar service + its drivers and
# the host_proto/crowbar_proto layer. Mirrors the EMFI helper above.
function(faultycat_add_service_crowbar_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/drivers/crowbar_mosfet/crowbar_mosfet.c
        ${CMAKE_SOURCE_DIR}/drivers/ext_trigger/ext_trigger.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/crowbar/crowbar_pio.c
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/crowbar/crowbar_campaign.c
        ${CMAKE_SOURCE_DIR}/services/host_proto/crowbar_proto/crowbar_proto.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/drivers/crowbar_mosfet
        ${CMAKE_SOURCE_DIR}/drivers/ext_trigger
        ${CMAKE_SOURCE_DIR}/services/glitch_engine/crowbar
        ${CMAKE_SOURCE_DIR}/services/host_proto/crowbar_proto
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# F5-2 — crowbar_pio service unit tests
faultycat_add_service_crowbar_test(test_crowbar_pio)
# F5-3 — crowbar_campaign state machine
faultycat_add_service_crowbar_test(test_crowbar_campaign)
# F5-4 — crowbar_proto host framing
faultycat_add_service_crowbar_test(test_crowbar_proto)

# SWD test helper — links swd_core sources for unit tests.
function(faultycat_add_service_swd_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/services/swd_core/swd_phy.c
        ${CMAKE_SOURCE_DIR}/services/swd_core/swd_dp.c
        ${CMAKE_SOURCE_DIR}/services/swd_core/swd_mem.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/services/swd_core
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# F6-2 — swd_phy unit tests
faultycat_add_service_swd_test(test_swd_phy)
# F6-3 — swd_dp wire protocol
faultycat_add_service_swd_test(test_swd_dp)
# F6-4 — swd_mem MEM-AP read32/write32
faultycat_add_service_swd_test(test_swd_mem)

# JTAG test helper — links jtag_core sources for unit tests.
function(faultycat_add_service_jtag_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/services/jtag_core/jtag_core.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/services/jtag_core
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# F8-1 — jtag_core TAP + IDCODE
faultycat_add_service_jtag_test(test_jtag_core)

# Pinout scanner test helper — links pinout_scanner.c only (the
# permutation iterator is the unit under test; pinout_scan_jtag /
# pinout_scan_swd end-to-end runs are deferred to physical smoke).
function(faultycat_add_service_pinout_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/services/pinout_scanner/pinout_scanner.c
        ${CMAKE_SOURCE_DIR}/services/jtag_core/jtag_core.c
        ${CMAKE_SOURCE_DIR}/services/swd_core/swd_phy.c
        ${CMAKE_SOURCE_DIR}/services/swd_core/swd_dp.c
        ${CMAKE_SOURCE_DIR}/services/swd_core/swd_mem.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/services/pinout_scanner
        ${CMAKE_SOURCE_DIR}/services/jtag_core
        ${CMAKE_SOURCE_DIR}/services/swd_core
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# F8-2 — pinout_scanner permutation iterator
faultycat_add_service_pinout_test(test_pinout_scanner)

# F8-4 — buspirate_compat (BusPirate v1 BBIO + OpenOCD JTAG)
function(faultycat_add_service_buspirate_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/services/buspirate_compat/buspirate_compat.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/services/buspirate_compat
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

faultycat_add_service_buspirate_test(test_buspirate_compat)

# F8-5 — flashrom_serprog (Serial Flasher Protocol v1)
function(faultycat_add_service_serprog_test name)
    add_executable(${name}
        ${name}.c
        ${CMAKE_SOURCE_DIR}/services/flashrom_serprog/flashrom_serprog.c
    )
    target_link_libraries(${name} PRIVATE unity hal_fake)
    target_include_directories(${name} PRIVATE
        ${CMAKE_SOURCE_DIR}/drivers/include
        ${CMAKE_SOURCE_DIR}/services/flashrom_serprog
    )
    add_test(NAME ${name} COMMAND ${name})
endfunction()

faultycat_add_service_serprog_test(test_flashrom_serprog)
