add_library(driver_ui_buttons STATIC
    ui_buttons.c
)

target_include_directories(driver_ui_buttons
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_ui_buttons
    PUBLIC
        hal_rp2040
        board_v2_header
)
