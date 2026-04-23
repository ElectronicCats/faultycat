# tests/hal_fake/ — native-side fake of the HAL, used only in host-tests.
#
# Each fake implements the same headers exposed by hal/include/hal/*.h
# but stores call history in inspectable globals so tests can assert
# "init was called for pin N with dir OUT" etc. See per-module header
# comments for the available probes.

# F1-1 scaffolding: interface target until a real fake source lands.
# F1-2 converts this to `add_library(hal_fake STATIC ...)` once
# gpio_fake.c and time_fake.c exist.
add_library(hal_fake INTERFACE)

target_include_directories(hal_fake
    INTERFACE
        ${CMAKE_SOURCE_DIR}/hal/include
)
