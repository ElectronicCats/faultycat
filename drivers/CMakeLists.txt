# drivers/ — FaultyCat HW v2.x driver layer.
# Built only in firmware mode. Host tests compile driver sources
# against hal_fake via a per-driver CMake helper in tests/.

# Shared pin map — every driver includes board_v2.h via this
# interface target; tests do the same via an equivalent target there.
add_library(board_v2_header INTERFACE)
target_include_directories(board_v2_header
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Register drivers. Add new drivers here as their commits land.
add_subdirectory(ui_leds)
add_subdirectory(ui_buttons)
