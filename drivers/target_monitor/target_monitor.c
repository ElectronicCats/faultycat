#include "target_monitor.h"

#include "board_v2.h"
#include "hal/adc.h"

void target_monitor_init(void) {
    hal_adc_init();
    hal_adc_channel_enable(BOARD_TARGET_ADC_CHANNEL);
}

uint16_t target_monitor_read_raw(void) {
    return hal_adc_read(BOARD_TARGET_ADC_CHANNEL);
}
