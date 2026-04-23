# hal/ — portable HAL consumed by drivers, services, and apps.
# Built only in firmware mode. The test-host fake in tests/hal_fake/
# substitutes this library; tests must NOT link this one.

add_library(hal_rp2040 STATIC
    src/rp2040/gpio.c
    src/rp2040/time.c
)

target_include_directories(hal_rp2040
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(hal_rp2040
    PUBLIC
        pico_stdlib
        hardware_gpio
)
