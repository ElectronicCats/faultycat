#pragma once

// drivers/voltage_mux — NOT IMPLEMENTED on FaultyCat HW v2.x.
//
// The plan §3 reserves this slot for a voltage multiplexer IC, but
// the v2.2 PCB has no such component (confirmed by the maintainer on
// 2026-04-23). The two crowbar paths (LP on GP16, HP on GP17) are
// selected by software through drivers/crowbar_mosfet, not by an
// analog mux.
//
// If a future hardware revision adds a mux IC, this header + a
// corresponding crowbar_mosfet_set_path-like API will land. Until
// then, any `#include "voltage_mux.h"` is a bug and the `#error`
// below catches it at build time.

#error "drivers/voltage_mux is a stub — FaultyCat HW v2.x has no voltage mux. Do not include. See drivers/voltage_mux/README.md."
