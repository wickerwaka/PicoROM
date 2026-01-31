/*
 * Debug Logging Interface for PicoROM
 * USB bulk endpoint for debug output
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void dbg_print(const char *fmt, ...);
void dbg_set_enabled_cb(void (*cb)(void));

// Called from tud_vendor_control_xfer_cb to handle debug control requests
// Returns true if the request was handled
bool dbg_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, void const *request);

#ifdef __cplusplus
}
#endif
