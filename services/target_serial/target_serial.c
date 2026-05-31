#include "target_serial.h"

#include "swd_bus_lock.h"
#include "target_serial_pio.h"

// RP2040 default boot clock; the PIO UART programs run at 8 cycles per
// bit, so SM clock target = baud * 8. Matches the sysclk assumption in
// swd_phy.c (SWD_SYSCLK_KHZ = 125000).
#define TS_SYSCLK_HZ      125000000u
#define TS_CYCLES_PER_BIT 8u

static target_serial_state_t s_state = TARGET_SERIAL_DISABLED;
static uint8_t s_tx_gp               = TARGET_SERIAL_TX_GP_DEFAULT;
static uint8_t s_rx_gp               = TARGET_SERIAL_RX_GP_DEFAULT;
static uint32_t s_baud               = TARGET_SERIAL_BAUD_DEFAULT;

uint32_t target_serial_baud_to_divider(uint32_t baud) {
    if (baud == 0u)
        return 0xFFFFu;
    uint32_t denom = baud * TS_CYCLES_PER_BIT;
    uint32_t div   = (TS_SYSCLK_HZ + denom / 2u) / denom; // round to nearest
    if (div == 0u)
        div = 1u;
    if (div > 0xFFFFu)
        div = 0xFFFFu;
    return div;
}

void target_serial_init(void) {
    s_state = TARGET_SERIAL_DISABLED;
    s_tx_gp = TARGET_SERIAL_TX_GP_DEFAULT;
    s_rx_gp = TARGET_SERIAL_RX_GP_DEFAULT;
    s_baud  = TARGET_SERIAL_BAUD_DEFAULT;
}

bool target_serial_enable(uint8_t tx_gp, uint8_t rx_gp, uint32_t baud) {
    if (s_state == TARGET_SERIAL_ENABLED)
        return false;
    if (tx_gp >= BOARD_SCANNER_CHANNEL_COUNT || rx_gp >= BOARD_SCANNER_CHANNEL_COUNT)
        return false;
    if (tx_gp == rx_gp)
        return false;
    if (baud == 0u)
        return false;
    if (!swd_bus_acquire(SWD_BUS_OWNER_SERIAL, SWD_BUS_TIMEOUT_NONE))
        return false;
    if (!target_serial_pio_init(tx_gp, rx_gp, target_serial_baud_to_divider(baud))) {
        swd_bus_release(SWD_BUS_OWNER_SERIAL);
        return false;
    }
    s_tx_gp = tx_gp;
    s_rx_gp = rx_gp;
    s_baud  = baud;
    s_state = TARGET_SERIAL_ENABLED;
    return true;
}

bool target_serial_set_baud(uint32_t baud) {
    if (s_state != TARGET_SERIAL_ENABLED || baud == 0u)
        return false;
    target_serial_pio_set_divider(target_serial_baud_to_divider(baud));
    s_baud = baud;
    return true;
}

void target_serial_disable(void) {
    if (s_state != TARGET_SERIAL_ENABLED)
        return;
    target_serial_pio_deinit();
    swd_bus_release(SWD_BUS_OWNER_SERIAL);
    s_state = TARGET_SERIAL_DISABLED;
}

void target_serial_get_status(target_serial_status_t* out) {
    if (out == NULL)
        return;
    out->state = s_state;
    out->tx_gp = s_tx_gp;
    out->rx_gp = s_rx_gp;
    out->baud  = s_baud;
}

bool target_serial_tx_byte(uint8_t b) {
    if (s_state != TARGET_SERIAL_ENABLED)
        return false;
    return target_serial_pio_try_put(b);
}

size_t target_serial_rx_drain(uint8_t* buf, size_t cap) {
    if (s_state != TARGET_SERIAL_ENABLED || buf == NULL || cap == 0u)
        return 0u;
    // Bounded by `cap` (caller passes a fixed-size stack buffer), so a
    // flood on the RX line can never spin this unbounded and starve
    // tud_task() / the magic-baud BOOTSEL path.
    size_t n = 0u;
    while (n < cap) {
        uint8_t b;
        if (!target_serial_pio_try_get(&b))
            break;
        buf[n++] = b;
    }
    return n;
}
