add_executable(faultycat
    main.c
)

# Link the HAL + drivers + USB composite.
target_link_libraries(faultycat PRIVATE
    hal_rp2040
    driver_ui_leds
    driver_ui_buttons
    driver_target_monitor
    driver_scanner_io
    driver_ext_trigger
    driver_crowbar_mosfet
    driver_hv_charger
    driver_emfi_pulse
    service_emfi
    host_proto_emfi
    usb_composite
)

# tusb_config.h must be visible to the tinyusb sources that pico-sdk
# compiles directly into THIS target. `usb_composite` marks its
# include/ PUBLIC but pico-sdk's tinyusb_device INTERFACE adds its
# sources before that propagation completes — so we also attach the
# path to the faultycat target explicitly.
target_include_directories(faultycat PRIVATE
    ${CMAKE_SOURCE_DIR}/usb/include
)

# F3 turns stdio_usb OFF: our own composite owns the USB stack now.
# The diag that used printf over the pico-sdk default CDC goes silent
# in this commit and gets restored on CDC2 (scanner) in F3-4.
pico_enable_stdio_usb(faultycat  0)
pico_enable_stdio_uart(faultycat 0)

# Generates .uf2, .hex, .bin, map/lst/dis alongside the ELF.
pico_add_extra_outputs(faultycat)
