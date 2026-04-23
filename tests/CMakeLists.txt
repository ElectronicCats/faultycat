# tests/ — host-side unit tests.
#
# Reached only under the host-tests preset (native toolchain, no pico).
# The FAULTYCAT_BUILD_TESTS guard in the root CMakeLists.txt gates us.

# Unity lives vendored as a submodule and exposes the `unity` target.
add_subdirectory(${CMAKE_SOURCE_DIR}/third_party/Unity Unity_build)

# hal_fake/ provides the native-side implementation of the HAL headers
# plus inspection hooks the tests read.
add_subdirectory(hal_fake)

# Test targets are added here as HAL modules land. F1 populates this
# with test_gpio + test_time once hal/gpio.h and hal/time.h exist.
