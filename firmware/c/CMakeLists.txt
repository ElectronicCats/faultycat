# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.1.0)
set(toolchainVersion 13_3_Rel1)
set(picotoolVersion 2.1.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)
# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)
project(faultycat)

pico_sdk_init()

# Add Nanopb support
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/faultier/nanopb/extra)
find_package(Nanopb REQUIRED)

add_executable(faultycat)

# Generate PIO headers
pico_generate_pio_header(faultycat ${CMAKE_CURRENT_LIST_DIR}/trigger_basic.pio)

# Add additional PIO headers from faultier
pico_generate_pio_header(faultycat ${CMAKE_CURRENT_LIST_DIR}/faultier/faultier/pio/spi.pio)
pico_generate_pio_header(faultycat ${CMAKE_CURRENT_LIST_DIR}/faultier/faultier/pio/i2c.pio)
pico_generate_pio_header(faultycat ${CMAKE_CURRENT_LIST_DIR}/faultier/faultier/pio/uart_rx.pio)
pico_generate_pio_header(faultycat ${CMAKE_CURRENT_LIST_DIR}/faultier/faultier/pio/uart_tx.pio)
pico_generate_pio_header(faultycat ${CMAKE_CURRENT_LIST_DIR}/faultier/swd/probe.pio)

target_sources(faultycat PRIVATE
        main.c
        # faultier/main.c
        picoemp.c
        faultier_test.c
        serial/serial.c
        serial/serial_utils.c
        glitcher/glitcher.c
        # Add faultier sources
        # faultier/faultier/pio/pio_spi.c
        # faultier/faultier/pio/pio_i2c.c
        # faultier/faultier/faultier.c
        # faultier/faultier/pdnd.c
        # faultier/faultier/pdnd_spi.c
        # faultier/faultier/pdnd_display.c
        # faultier/faultier/display/FreeSans5pt7b.c
        # faultier/faultier/display/FreeSans9pt7b.c
        # faultier/faultier/display/font_renderer.c
        # faultier/faultier/display/ssd1306.c
        # faultier/proto/faultier.pb.c
        # faultier/bootup.c
        # faultier/bootup2.c
        faultier/glitcher/ft_pio.c
        faultier/glitcher/power_cycler.c
        faultier/glitcher/trigger_compiler.c
        faultier/glitcher/delay_compiler.c
        faultier/glitcher/glitch_compiler.c
        # faultier/protocol.c
        # faultier/usb_descriptors.c
        # faultier/swd/tamarin_probe.c
        # faultier/swd/swdchecker.c
        )

# Add include directory for tusb_config.h
target_include_directories(faultycat PRIVATE ${CMAKE_CURRENT_LIST_DIR})
pico_set_binary_type(faultycat copy_to_ram)

target_link_libraries(faultycat PRIVATE 
        pico_stdlib
        hardware_pwm
        hardware_pio
        pico_multicore
        # tinyusb_device
        # tinyusb_board
        # hardware_i2c
        hardware_adc
        hardware_dma
        nanopb
        )

target_include_directories(faultycat PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/glitcher
        ${CMAKE_CURRENT_LIST_DIR}/serial
        # From faultier repo
        ${CMAKE_CURRENT_LIST_DIR}/faultier
        ${CMAKE_CURRENT_LIST_DIR}/faultier/faultier
        ${CMAKE_CURRENT_LIST_DIR}/faultier/proto
        ${CMAKE_CURRENT_LIST_DIR}/faultier/glitcher)

pico_add_extra_outputs(faultycat)

# enable usb output, disable uart output
pico_enable_stdio_usb(faultycat 1)
pico_enable_stdio_uart(faultycat 0)
