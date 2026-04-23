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
    add_test(NAME ${name} COMMAND ${name})
endfunction()

# F1 — HAL unit tests
faultycat_add_test(test_gpio)
faultycat_add_test(test_time)
