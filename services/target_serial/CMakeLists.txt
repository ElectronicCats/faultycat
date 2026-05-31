add_library(service_target_serial STATIC
    target_serial.c
    target_serial_pio.c
)

target_include_directories(service_target_serial
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_target_serial
    PUBLIC
        hal_rp2040
        board_v2_header
        service_swd_bus_lock
)
