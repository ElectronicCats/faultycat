add_library(service_campaign_manager STATIC
    campaign_manager.c
)

target_include_directories(service_campaign_manager
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(service_campaign_manager
    PUBLIC
        hal_rp2040
)
