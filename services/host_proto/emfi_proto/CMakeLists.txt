add_library(host_proto_emfi STATIC
    emfi_proto.c
)
target_include_directories(host_proto_emfi
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(host_proto_emfi
    PUBLIC
        service_emfi
)
