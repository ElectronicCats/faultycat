add_library(driver_crowbar_mosfet STATIC
    crowbar_mosfet.c
)

target_include_directories(driver_crowbar_mosfet
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_crowbar_mosfet
    PUBLIC
        hal_rp2040
        board_v2_header
)
