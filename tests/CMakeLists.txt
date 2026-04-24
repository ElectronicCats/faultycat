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

# F1 — HAL unit tests
faultycat_add_test(test_gpio)
faultycat_add_test(test_time)

# F4-1 — hal/pio unit tests
faultycat_add_test(test_hal_pio)

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
