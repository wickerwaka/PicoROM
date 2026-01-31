/*
 * Debug Logging Interface for PicoROM
 * Custom TinyUSB class driver with bulk IN endpoint for debug output
 */

#include "debug.h"
#include "tusb.h"
#include "device/usbd_pvt.h"
#include "pico/printf.h"
#include <stdarg.h>

#define DEBUG_FIFO_SIZE 16

// Control request to enable/disable debug output
#define DEBUG_REQUEST_SET_ENABLED 0x01

static uint8_t debug_itf_num;
static uint8_t debug_ep_in;
static uint8_t debug_fifo[DEBUG_FIFO_SIZE];
static uint8_t debug_fifo_count;
static bool debug_mounted;
static bool debug_enabled;
static bool debug_xfer_in_progress;
static void (*debug_enabled_cb)(void);

// Flush FIFO to USB endpoint
static void debug_flush(void) {
    if (!debug_enabled || debug_fifo_count == 0 || debug_xfer_in_progress) return;

    if (usbd_edpt_claim(0, debug_ep_in)) {
        debug_xfer_in_progress = true;
        usbd_edpt_xfer(0, debug_ep_in, debug_fifo, debug_fifo_count);
        debug_fifo_count = 0;
    }
}

// Character output callback for vfctprintf
static void debug_putc(char c, void *arg) {
    (void)arg;
    if (!debug_enabled) return;

    debug_fifo[debug_fifo_count++] = (uint8_t)c;
    if (debug_fifo_count >= DEBUG_FIFO_SIZE) {
        debug_flush();
    }
}

void dbg_print(const char *fmt, ...) {
    if (!debug_enabled) return;

    va_list va;
    va_start(va, fmt);
    vfctprintf(debug_putc, NULL, fmt, va);
    va_end(va);

    debug_flush();
}

void dbg_set_enabled_cb(void (*cb)(void)) {
    debug_enabled_cb = cb;
}

bool dbg_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, void const *request) {
    tusb_control_request_t const *req = (tusb_control_request_t const *)request;

    // Only handle SETUP stage - let caller handle other stages
    if (stage != CONTROL_STAGE_SETUP) return false;

    // Only handle requests for our interface
    if (!debug_mounted || req->wIndex != debug_itf_num) return false;

    if (req->bRequest == DEBUG_REQUEST_SET_ENABLED) {
        debug_enabled = (req->wValue != 0);

        // Clear FIFO when disabling
        if (!debug_enabled) {
            debug_fifo_count = 0;
        }

        // Call callback when enabled
        if (debug_enabled && debug_enabled_cb) {
            debug_enabled_cb();
        }

        return tud_control_status(rhport, req);
    }

    return false;
}

//--------------------------------------------------------------------
// TinyUSB Class Driver Implementation
//--------------------------------------------------------------------

void debugd_init(void) {
    debug_mounted = false;
    debug_enabled = false;
    debug_xfer_in_progress = false;
    debug_fifo_count = 0;
}

void debugd_reset(uint8_t rhport) {
    (void)rhport;
    debug_mounted = false;
    debug_enabled = false;
    debug_xfer_in_progress = false;
    debug_fifo_count = 0;
}

uint16_t debugd_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
    // Only claim vendor class interfaces with protocol 0xDB (debug interface)
    TU_VERIFY(TUSB_CLASS_VENDOR_SPECIFIC == itf_desc->bInterfaceClass &&
              0xDB == itf_desc->bInterfaceProtocol, 0);

    // Must have exactly 1 endpoint
    TU_VERIFY(itf_desc->bNumEndpoints == 1, 0);

    uint16_t const drv_len = sizeof(tusb_desc_interface_t) + sizeof(tusb_desc_endpoint_t);
    TU_VERIFY(max_len >= drv_len, 0);

    debug_itf_num = itf_desc->bInterfaceNumber;

    // Parse endpoint descriptor
    uint8_t const *p_desc = (uint8_t const *)itf_desc;
    p_desc += sizeof(tusb_desc_interface_t);

    tusb_desc_endpoint_t const *ep_desc = (tusb_desc_endpoint_t const *)p_desc;

    // Must be bulk IN endpoint
    TU_VERIFY(ep_desc->bDescriptorType == TUSB_DESC_ENDPOINT, 0);
    TU_VERIFY(tu_edpt_dir(ep_desc->bEndpointAddress) == TUSB_DIR_IN, 0);

    // Open endpoint
    TU_VERIFY(usbd_edpt_open(rhport, ep_desc), 0);
    debug_ep_in = ep_desc->bEndpointAddress;

    debug_mounted = true;

    return drv_len;
}

bool debugd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    (void)rhport;
    (void)stage;
    (void)request;
    // Vendor control requests are handled via dbg_vendor_control_xfer_cb
    return false;
}

bool debugd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void)result;
    (void)xferred_bytes;

    if (ep_addr == debug_ep_in) {
        usbd_edpt_release(rhport, debug_ep_in);
        debug_xfer_in_progress = false;

        // Flush any data that accumulated during the transfer
        if (debug_fifo_count > 0) {
            debug_flush();
        }
    }
    return true;
}
