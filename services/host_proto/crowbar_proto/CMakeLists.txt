add_library(host_proto_crowbar STATIC
    crowbar_proto.c
)
target_include_directories(host_proto_crowbar
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(host_proto_crowbar
    PUBLIC
        service_crowbar
)
