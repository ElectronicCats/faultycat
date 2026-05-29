add_library(service_crowbar STATIC
    crowbar_pio.c
    crowbar_campaign.c
)

target_include_directories(service_crowbar
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_crowbar
    PUBLIC
        hal_rp2040
        board_v2_header
        driver_crowbar_mosfet
        driver_ext_trigger
)
