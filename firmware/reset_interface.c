/*
 * Reset Interface Driver for PicoROM
 * Enables BOOTSEL mode via USB control transfer (compatible with picotool)
 */

#include "tusb.h"
#include "device/usbd_pvt.h"
#include "pico/bootrom.h"
#include "pico/usb_reset_interface.h"

static uint8_t itf_num;

//--------------------------------------------------------------------
// USB Class Driver Implementation
//--------------------------------------------------------------------

static void resetd_init(void) {
}

static void resetd_reset(uint8_t rhport) {
    (void)rhport;
    itf_num = 0;
}

static uint16_t resetd_open(uint8_t rhport, tusb_desc_interface_t const *itf_desc, uint16_t max_len) {
    (void)rhport;

    // Verify this is our reset interface
    TU_VERIFY(TUSB_CLASS_VENDOR_SPECIFIC == itf_desc->bInterfaceClass &&
              RESET_INTERFACE_SUBCLASS == itf_desc->bInterfaceSubClass &&
              RESET_INTERFACE_PROTOCOL == itf_desc->bInterfaceProtocol, 0);

    uint16_t const drv_len = sizeof(tusb_desc_interface_t);
    TU_VERIFY(max_len >= drv_len, 0);

    itf_num = itf_desc->bInterfaceNumber;
    return drv_len;
}

static bool resetd_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    (void)rhport;

    // Only handle requests for our interface
    if (request->wIndex != itf_num) return false;

    // Only handle SETUP stage
    if (stage != CONTROL_STAGE_SETUP) return true;

    switch (request->bRequest) {
        case RESET_REQUEST_BOOTSEL:
            // Reset to BOOTSEL mode (USB mass storage bootloader)
            // wValue can specify GPIO for activity LED and disable mask
            rom_reset_usb_boot(request->wValue & 0x7f, 0);
            // Does not return
            break;

        case RESET_REQUEST_FLASH:
            // Reset to flash boot (normal reboot)
            // Use watchdog to trigger reset
            *((volatile uint32_t*)(PPB_BASE + 0x0ED0C)) = 0x5FA0004;
            // Does not return
            break;

        default:
            return false;
    }

    return true;
}

static bool resetd_xfer_cb(uint8_t rhport, uint8_t ep_addr, xfer_result_t result, uint32_t xferred_bytes) {
    (void)rhport;
    (void)ep_addr;
    (void)result;
    (void)xferred_bytes;
    return true;
}

//--------------------------------------------------------------------
// USB Class Driver Registration
//--------------------------------------------------------------------

static const usbd_class_driver_t _app_drivers[] = {
    // Reset driver
    {
#if CFG_TUSB_DEBUG >= 2
        .name             = "RESET",
#endif
        .init             = resetd_init,
        .reset            = resetd_reset,
        .open             = resetd_open,
        .control_xfer_cb  = resetd_control_xfer_cb,
        .xfer_cb          = resetd_xfer_cb,
        .sof              = NULL,
    },
};

// TinyUSB callback to register custom USB class drivers
usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return _app_drivers;
}
