add_library(service_jtag STATIC
    jtag_core.c
)

target_include_directories(service_jtag
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_jtag
    PUBLIC
        hal_rp2040
        board_v2_header
)
