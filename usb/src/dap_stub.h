#pragma once

#include <stddef.h>
#include <stdint.h>

// CMSIS-DAP stub — absolute minimum DAP_Info responder so hosts like
// OpenOCD, pyOCD, and probe-rs can enumerate the Vendor IF and tell
// us "this is a CMSIS-DAP v2 probe". F7 replaces this with a port of
// debugprobe's real DAP engine (SWD via PIO, full command set).
//
// Scope: responds to `DAP_Info` subcodes for vendor/product/serial/
// firmware/capabilities/packet-size/packet-count. Everything else
// replies with `DAP_ERROR` so the host backs off cleanly instead of
// spinning on a non-implemented command.
//
// Packet layout (CMSIS-DAP v2):
//   req[0]  = command ID
//   req[1+] = payload
//   resp[0] = echo of command ID
//   resp[1+] = length + payload (command-specific)

// Handle a single CMSIS-DAP request packet. `req_len` is the exact
// number of bytes in the packet; `resp` must be at least 64 bytes.
// Returns the number of bytes to send back.
size_t dap_stub_handle(const uint8_t *req, size_t req_len,
                       uint8_t *resp, size_t resp_capacity);
