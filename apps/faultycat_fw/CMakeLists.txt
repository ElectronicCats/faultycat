add_executable(faultycat
    main.c
)

# Link the HAL only. hal_rp2040 transitively pulls pico_stdlib and the
# hardware_* libs it wraps — main.c never sees pico-sdk symbols.
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
    usb_composite
)

# F3 turns stdio_usb OFF: our own composite owns the USB stack now.
# The diag that used printf over the pico-sdk default CDC goes silent
# in this commit and gets restored on CDC2 (scanner) in F3-4.
pico_enable_stdio_usb(faultycat  0)
pico_enable_stdio_uart(faultycat 0)

# Generates .uf2, .hex, .bin, map/lst/dis alongside the ELF.
pico_add_extra_outputs(faultycat)
