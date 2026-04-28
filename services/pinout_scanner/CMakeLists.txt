add_library(service_pinout_scanner STATIC
    pinout_scanner.c
)

target_include_directories(service_pinout_scanner
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_pinout_scanner
    PUBLIC
        hal_rp2040
        board_v2_header
        service_jtag
        service_swd
)
