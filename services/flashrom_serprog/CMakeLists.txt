add_library(service_flashrom_serprog STATIC
    flashrom_serprog.c
)

target_include_directories(service_flashrom_serprog
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_flashrom_serprog
    PUBLIC
        hal_rp2040
)
