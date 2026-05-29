add_library(driver_target_monitor STATIC
    target_monitor.c
)

target_include_directories(driver_target_monitor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_target_monitor
    PUBLIC
        hal_rp2040
        board_v2_header
)
