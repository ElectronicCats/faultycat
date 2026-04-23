add_executable(faultycat
    main.c
)

# Link the HAL only. hal_rp2040 transitively pulls pico_stdlib and the
# hardware_* libs it wraps — main.c never sees pico-sdk symbols.
target_link_libraries(faultycat PRIVATE
    hal_rp2040
)

# F0 kept stdio off so we didn't depend on tinyusb; same policy in F1.
# F3 brings up the real USB composite.
pico_enable_stdio_usb(faultycat  0)
pico_enable_stdio_uart(faultycat 0)

# Generates .uf2, .hex, .bin, map/lst/dis alongside the ELF.
pico_add_extra_outputs(faultycat)
