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
)

# F0/F1 kept stdio off to avoid depending on tinyusb. F2 turns USB-CDC
# stdio on for the diag loop (temporary — the F3 USB composite
# descriptor will replace this single-CDC pico-sdk default).
pico_enable_stdio_usb(faultycat  1)
pico_enable_stdio_uart(faultycat 0)

# Generates .uf2, .hex, .bin, map/lst/dis alongside the ELF.
pico_add_extra_outputs(faultycat)
