# tests/hal_fake/ — native-side fake of the HAL, used only in host-tests.
#
# Each fake implements the same headers exposed by hal/include/hal/*.h
# but stores call history in inspectable globals so tests can assert
# "init was called for pin N with dir OUT" etc. See per-module header
# comments for the available probes.

# Native-side fakes of the HAL. Real sources land as the corresponding
# hal/include/hal/*.h gets an implementation.

add_library(hal_fake STATIC
    gpio_fake.c
    time_fake.c
    adc_fake.c
    pwm_fake.c
    pio_fake.c
)

target_include_directories(hal_fake
    PUBLIC
        ${CMAKE_SOURCE_DIR}/hal/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)
