/*
 * TinyUSB configuration for PicoROM
 * Custom vendor bulk endpoints for libusb/WinUSB access
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

//--------------------------------------------------------------------
// Common Configuration
//--------------------------------------------------------------------

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS           OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif

// Enable Device stack
#define CFG_TUD_ENABLED       1
#define CFG_TUSB_RHPORT0_MODE OPT_MODE_DEVICE

// Default is max speed that hardware controller could support with on-chip PHY
#define CFG_TUD_MAX_SPEED     OPT_MODE_DEFAULT_SPEED

#ifndef CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_SECTION
#endif

#ifndef CFG_TUSB_MEM_ALIGN
#define CFG_TUSB_MEM_ALIGN    __attribute__((aligned(4)))
#endif

//--------------------------------------------------------------------
// Device Configuration
//--------------------------------------------------------------------

#ifndef CFG_TUD_ENDPOINT0_SIZE
#define CFG_TUD_ENDPOINT0_SIZE 64
#endif

//--------------------------------------------------------------------
// Class Configuration
//--------------------------------------------------------------------

// Disable CDC - we use vendor bulk instead
#define CFG_TUD_CDC           0

// Enable Vendor class for bulk endpoints
#define CFG_TUD_VENDOR        1

// Disable unused classes
#define CFG_TUD_MSC           0
#define CFG_TUD_HID           0
#define CFG_TUD_MIDI          0
#define CFG_TUD_AUDIO         0

// Vendor FIFO size of TX and RX
// Set to 0 for direct endpoint access (no internal buffering)
#define CFG_TUD_VENDOR_RX_BUFSIZE 0
#define CFG_TUD_VENDOR_TX_BUFSIZE 64

// Vendor endpoint buffer size
#define CFG_TUD_VENDOR_EP_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
