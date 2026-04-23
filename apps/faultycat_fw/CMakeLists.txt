add_executable(faultycat
    main.c
)

target_link_libraries(faultycat PRIVATE
    pico_stdlib
)

# F0 keeps stdio off: the blink doesn't need printf, and avoiding USB
# stdio means we don't depend on pico-sdk's tinyusb submodule at this
# stage. F3 adds the real USB composite (4× CDC + vendor + HID) once
# the nested tinyusb submodule is populated by tools/bootstrap.sh.
pico_enable_stdio_usb(faultycat  0)
pico_enable_stdio_uart(faultycat 0)

# Generates .uf2, .hex, .bin, map/lst/dis alongside the ELF.
pico_add_extra_outputs(faultycat)
