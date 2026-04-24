add_library(driver_hv_charger STATIC
    hv_charger.c
)

target_include_directories(driver_hv_charger
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_hv_charger
    PUBLIC
        hal_rp2040
        board_v2_header
)
