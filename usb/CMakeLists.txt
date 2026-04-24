# usb/ — FaultyCat v3 composite USB device (TinyUSB-based).

add_library(usb_composite STATIC
    src/usb_composite.c
    src/usb_descriptors.c
)

target_include_directories(usb_composite
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# TinyUSB picks up tusb_config.h from the include path. We point
# TinyUSB at ours by exposing `usb/include/` before its default.
target_link_libraries(usb_composite
    PUBLIC
        tinyusb_device
    PRIVATE
        pico_unique_id
        pico_bootrom
)
