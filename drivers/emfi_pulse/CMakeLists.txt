add_library(driver_emfi_pulse STATIC
    emfi_pulse.c
)

target_include_directories(driver_emfi_pulse
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_emfi_pulse
    PUBLIC
        hal_rp2040
        board_v2_header
)
