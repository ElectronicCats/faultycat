add_library(driver_scanner_io STATIC
    scanner_io.c
)

target_include_directories(driver_scanner_io
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_scanner_io
    PUBLIC
        hal_rp2040
        board_v2_header
)
