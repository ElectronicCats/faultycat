add_library(service_buspirate_compat STATIC
    buspirate_compat.c
)

target_include_directories(service_buspirate_compat
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_buspirate_compat
    PUBLIC
        hal_rp2040
)
