add_library(service_swd STATIC
    swd_phy.c
    swd_dp.c
    swd_mem.c
)

target_include_directories(service_swd
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_swd
    PUBLIC
        hal_rp2040
        board_v2_header
)
