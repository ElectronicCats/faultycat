add_library(driver_ui_leds STATIC
    ui_leds.c
)

target_include_directories(driver_ui_leds
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_ui_leds
    PUBLIC
        hal_rp2040
        board_v2_header
)
