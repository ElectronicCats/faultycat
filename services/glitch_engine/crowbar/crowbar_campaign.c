#include "crowbar_campaign.h"

#include <string.h>

// F5-1 skeleton — API-only stub. F5-3 lands the real state machine,
// the path management (set_path(NONE) before each fire), and the
// PIO+driver composition.

static crowbar_status_t s_status;

bool crowbar_campaign_init(void) {
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = CROWBAR_STATE_IDLE;
    return true;
}

bool crowbar_campaign_configure(const crowbar_config_t *cfg) {
    (void)cfg;
    return false;
}

bool crowbar_campaign_arm(void) {
    return false;
}

bool crowbar_campaign_fire(uint32_t trigger_timeout_ms) {
    (void)trigger_timeout_ms;
    return false;
}

void crowbar_campaign_disarm(void) {
}

void crowbar_campaign_tick(void) {
}

void crowbar_campaign_get_status(crowbar_status_t *out) {
    if (!out) return;
    *out = s_status;
}
