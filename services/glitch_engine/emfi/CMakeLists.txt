add_library(service_emfi STATIC
    emfi_pio.c
    emfi_capture.c
)

target_include_directories(service_emfi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_emfi
    PUBLIC
        hal_rp2040
        board_v2_header
        driver_emfi_pulse
)
