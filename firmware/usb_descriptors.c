/*
 * USB Descriptors for PicoROM
 * Vendor bulk endpoints with MS OS 2.0 descriptors for driverless WinUSB
 */

#include <string.h>

#include "tusb.h"
#include "pico/unique_id.h"
#include "pico/usb_reset_interface.h"

#include "flash_name.h"

//--------------------------------------------------------------------
// Device Descriptor
//--------------------------------------------------------------------

#define USBD_VID          0x2E8A  // Raspberry Pi
#define USBD_PID          0x000A  // Pico SDK CDC for RP2040 (reused for compatibility)
#define USBD_BCD_DEVICE   0x0100

// USB 2.1 required for BOS descriptor (MS OS 2.0)
#define USBD_BCD_USB      0x0210

static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = USBD_BCD_USB,
    .bDeviceClass       = 0x00,  // Defined at interface level
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USBD_VID,
    .idProduct          = USBD_PID,
    .bcdDevice          = USBD_BCD_DEVICE,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

//--------------------------------------------------------------------
// Configuration Descriptor
//--------------------------------------------------------------------

enum {
    ITF_NUM_VENDOR = 0,
    ITF_NUM_RESET  = 1,
    ITF_NUM_TOTAL
};

#define EPNUM_VENDOR_OUT  0x01
#define EPNUM_VENDOR_IN   0x81
#define VENDOR_EP_SIZE    64

// Reset interface descriptor macro (no endpoints)
#define TUD_RESET_INTERFACE_DESCRIPTOR(_itfnum, _stridx) \
    9, TUSB_DESC_INTERFACE, _itfnum, 0, 0, \
    TUSB_CLASS_VENDOR_SPECIFIC, RESET_INTERFACE_SUBCLASS, RESET_INTERFACE_PROTOCOL, _stridx

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN + 9)

static const uint8_t desc_configuration[] = {
    // Configuration descriptor
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),

    // Interface 0: Vendor bulk endpoints
    TUD_VENDOR_DESCRIPTOR(ITF_NUM_VENDOR, 4, EPNUM_VENDOR_OUT, EPNUM_VENDOR_IN, VENDOR_EP_SIZE),

    // Interface 1: Reset interface (no endpoints)
    TUD_RESET_INTERFACE_DESCRIPTOR(ITF_NUM_RESET, 5),
};

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

//--------------------------------------------------------------------
// MS OS 2.0 Descriptor
//--------------------------------------------------------------------

#define VENDOR_REQUEST_MICROSOFT 1

// Total length: header(10) + config subset(8) + 2x(function subset(8) + compat id(20) + registry prop(132))
// = 10 + 8 + 2*(8 + 20 + 132) = 10 + 8 + 320 = 338 bytes
#define MS_OS_20_SUBSET_LEN       (8 + 20 + 132)
#define MS_OS_20_DESC_LEN         (10 + 8 + MS_OS_20_SUBSET_LEN + MS_OS_20_SUBSET_LEN)

//--------------------------------------------------------------------
// BOS Descriptor (for MS OS 2.0)
//--------------------------------------------------------------------

#define BOS_TOTAL_LEN     (TUD_BOS_DESC_LEN + TUD_BOS_MICROSOFT_OS_DESC_LEN)

static const uint8_t desc_bos[] = {
    // BOS descriptor header
    TUD_BOS_DESCRIPTOR(BOS_TOTAL_LEN, 1),

    // Microsoft OS 2.0 descriptor platform capability
    TUD_BOS_MS_OS_20_DESCRIPTOR(MS_OS_20_DESC_LEN, VENDOR_REQUEST_MICROSOFT),
};

const uint8_t *tud_descriptor_bos_cb(void) {
    return desc_bos;
}

static const uint8_t desc_ms_os_20[] = {
    // Microsoft OS 2.0 descriptor set header
    U16_TO_U8S_LE(0x000A),                          // wLength
    U16_TO_U8S_LE(MS_OS_20_SET_HEADER_DESCRIPTOR),  // wDescriptorType
    U32_TO_U8S_LE(0x06030000),                      // dwWindowsVersion (Windows 8.1+)
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN),               // wTotalLength

    // Configuration subset header
    U16_TO_U8S_LE(0x0008),                                  // wLength
    U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_CONFIGURATION),    // wDescriptorType
    0x00,                                                   // bConfigurationValue
    0x00,                                                   // bReserved
    U16_TO_U8S_LE(MS_OS_20_DESC_LEN - 10),                  // wTotalLength (rest of descriptor)

    //--------------------------------------------------------------------------
    // Function subset for Interface 0 (Vendor bulk)
    //--------------------------------------------------------------------------
    U16_TO_U8S_LE(0x0008),                              // wLength
    U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),     // wDescriptorType
    ITF_NUM_VENDOR,                                     // bFirstInterface
    0x00,                                               // bReserved
    U16_TO_U8S_LE(MS_OS_20_SUBSET_LEN),                 // wSubsetLength

    // Compatible ID descriptor
    U16_TO_U8S_LE(0x0014),                              // wLength
    U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),       // wDescriptorType
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,           // CompatibleID
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // SubCompatibleID

    // Registry property descriptor: DeviceInterfaceGUID
    U16_TO_U8S_LE(0x0084),                              // wLength (132 bytes)
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),       // wDescriptorType
    U16_TO_U8S_LE(0x0007),                              // wPropertyDataType (REG_MULTI_SZ)
    U16_TO_U8S_LE(0x002A),                              // wPropertyNameLength (42 bytes)
    // PropertyName: "DeviceInterfaceGUIDs\0" in UTF-16LE
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00,
    'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00,
    'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00,
    'D', 0x00, 's', 0x00, 0x00, 0x00,
    U16_TO_U8S_LE(0x0050),                              // wPropertyDataLength (80 bytes)
    // PropertyData: "{DEADBEEF-1234-5678-9ABC-PICOROM00001}\0\0" in UTF-16LE
    // GUID for PicoROM vendor interface: {E0E0E0E1-BEEF-CAFE-PICO-ROM000000001}
    // Using a more standard GUID format: {e0e0e0e1-1234-5678-9abc-def012345678}
    '{', 0x00, 'e', 0x00, '0', 0x00, 'e', 0x00, '0', 0x00, 'e', 0x00,
    '0', 0x00, 'e', 0x00, '1', 0x00, '-', 0x00,
    '1', 0x00, '2', 0x00, '3', 0x00, '4', 0x00, '-', 0x00,
    '5', 0x00, '6', 0x00, '7', 0x00, '8', 0x00, '-', 0x00,
    '9', 0x00, 'a', 0x00, 'b', 0x00, 'c', 0x00, '-', 0x00,
    'd', 0x00, 'e', 0x00, 'f', 0x00, '0', 0x00, '1', 0x00, '2', 0x00,
    '3', 0x00, '4', 0x00, '5', 0x00, '6', 0x00, '7', 0x00, '8', 0x00,
    '}', 0x00, 0x00, 0x00, 0x00, 0x00,

    //--------------------------------------------------------------------------
    // Function subset for Interface 1 (Reset)
    //--------------------------------------------------------------------------
    U16_TO_U8S_LE(0x0008),                              // wLength
    U16_TO_U8S_LE(MS_OS_20_SUBSET_HEADER_FUNCTION),     // wDescriptorType
    ITF_NUM_RESET,                                      // bFirstInterface
    0x00,                                               // bReserved
    U16_TO_U8S_LE(MS_OS_20_SUBSET_LEN),                 // wSubsetLength

    // Compatible ID descriptor
    U16_TO_U8S_LE(0x0014),                              // wLength
    U16_TO_U8S_LE(MS_OS_20_FEATURE_COMPATBLE_ID),       // wDescriptorType
    'W', 'I', 'N', 'U', 'S', 'B', 0x00, 0x00,           // CompatibleID
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,     // SubCompatibleID

    // Registry property descriptor: DeviceInterfaceGUID
    U16_TO_U8S_LE(0x0084),                              // wLength (132 bytes)
    U16_TO_U8S_LE(MS_OS_20_FEATURE_REG_PROPERTY),       // wDescriptorType
    U16_TO_U8S_LE(0x0007),                              // wPropertyDataType (REG_MULTI_SZ)
    U16_TO_U8S_LE(0x002A),                              // wPropertyNameLength (42 bytes)
    // PropertyName: "DeviceInterfaceGUIDs\0" in UTF-16LE
    'D', 0x00, 'e', 0x00, 'v', 0x00, 'i', 0x00, 'c', 0x00, 'e', 0x00,
    'I', 0x00, 'n', 0x00, 't', 0x00, 'e', 0x00, 'r', 0x00, 'f', 0x00,
    'a', 0x00, 'c', 0x00, 'e', 0x00, 'G', 0x00, 'U', 0x00, 'I', 0x00,
    'D', 0x00, 's', 0x00, 0x00, 0x00,
    U16_TO_U8S_LE(0x0050),                              // wPropertyDataLength (80 bytes)
    // PropertyData: GUID for reset interface (different from vendor)
    // {e0e0e0e2-1234-5678-9abc-def012345678}
    '{', 0x00, 'e', 0x00, '0', 0x00, 'e', 0x00, '0', 0x00, 'e', 0x00,
    '0', 0x00, 'e', 0x00, '2', 0x00, '-', 0x00,
    '1', 0x00, '2', 0x00, '3', 0x00, '4', 0x00, '-', 0x00,
    '5', 0x00, '6', 0x00, '7', 0x00, '8', 0x00, '-', 0x00,
    '9', 0x00, 'a', 0x00, 'b', 0x00, 'c', 0x00, '-', 0x00,
    'd', 0x00, 'e', 0x00, 'f', 0x00, '0', 0x00, '1', 0x00, '2', 0x00,
    '3', 0x00, '4', 0x00, '5', 0x00, '6', 0x00, '7', 0x00, '8', 0x00,
    '}', 0x00, 0x00, 0x00, 0x00, 0x00,

};

TU_VERIFY_STATIC(sizeof(desc_ms_os_20) == MS_OS_20_DESC_LEN, "Incorrect MS OS 2.0 descriptor size");

//--------------------------------------------------------------------
// String Descriptors
//--------------------------------------------------------------------

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER,
    STRID_PRODUCT,
    STRID_SERIAL,
    STRID_VENDOR_ITF,
    STRID_RESET_ITF,
};

static const char *const string_desc_arr[] = {
    [STRID_LANGID]       = (const char[]){0x09, 0x04},  // English (US)
    [STRID_MANUFACTURER] = "PicoROM",
    [STRID_PRODUCT]      = "PicoROM",
    [STRID_SERIAL]       = NULL,  // Handled specially in callback
    [STRID_VENDOR_ITF]   = "PicoROM Data",
    [STRID_RESET_ITF]    = "Reset",
};

static uint16_t _desc_str[32 + 1];

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t chr_count;

    switch (index) {
        case STRID_LANGID:
            memcpy(&_desc_str[1], string_desc_arr[0], 2);
            chr_count = 1;
            break;

        case STRID_SERIAL: {
            // Build "device_id:device_name" on stack
            char serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1 + CONFIG_NAME_LEN];
            pico_get_unique_board_id_string(serial_str, PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1);
            serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2] = ':';
            const char *name = flash_get_device_name();
            strncpy(&serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1], name, CONFIG_NAME_LEN - 1);
            serial_str[sizeof(serial_str) - 1] = '\0';

            chr_count = strlen(serial_str);
            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = serial_str[i];
            }
            break;
        }

        default:
            if (index >= sizeof(string_desc_arr) / sizeof(string_desc_arr[0])) {
                return NULL;
            }

            const char *str = string_desc_arr[index];
            if (!str) return NULL;

            chr_count = strlen(str);
            size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
            if (chr_count > max_count) chr_count = max_count;

            // Convert ASCII to UTF-16
            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = str[i];
            }
            break;
    }

    // First byte is length (including header), second byte is string type
    _desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

    return _desc_str;
}

//--------------------------------------------------------------------
// Vendor Control Transfer Callback (for MS OS 2.0 descriptor request)
//--------------------------------------------------------------------

bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const *request) {
    if (stage == CONTROL_STAGE_SETUP) {
        // Handle MS OS 2.0 descriptor request
        if (request->bRequest == VENDOR_REQUEST_MICROSOFT && request->wIndex == 7) {
            return tud_control_xfer(rhport, request, (void *)desc_ms_os_20, sizeof(desc_ms_os_20));
        }

        return false;  // Stall unknown requests
    }

    // For non-SETUP stages, return true to complete the transfer
    return true;
}
