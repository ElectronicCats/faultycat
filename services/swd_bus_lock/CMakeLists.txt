add_library(service_swd_bus_lock STATIC
    swd_bus_lock.c
)

target_include_directories(service_swd_bus_lock
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_swd_bus_lock
    PUBLIC
        hal_rp2040
)
