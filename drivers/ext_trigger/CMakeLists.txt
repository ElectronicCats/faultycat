add_library(driver_ext_trigger STATIC
    ext_trigger.c
)

target_include_directories(driver_ext_trigger
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(driver_ext_trigger
    PUBLIC
        hal_rp2040
        board_v2_header
)
