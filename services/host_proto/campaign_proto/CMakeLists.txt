add_library(host_proto_campaign STATIC
    campaign_proto.c
)

target_include_directories(host_proto_campaign
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(host_proto_campaign
    PUBLIC
        hal_rp2040
        service_campaign_manager
)
