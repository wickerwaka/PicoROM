//! PicoLink - USB communication library for PicoROM devices
//!
//! This crate provides two main modules:
//! - `picolink` - Communication with PicoROM devices in application mode
//! - `picoboot` - Communication with RP2040 devices in PICOBOOT bootloader mode

use nusb::transfer::Buffer;

mod picoboot;
mod picolink;

// Re-export picolink types
pub use picolink::{
    enumerate_all_devices, enumerate_picos, find_pico, get_device_location, reboot_to_bootloader,
    wait_for_device_at_location, DetectedDevice, DeviceMode, PicoLink, ReqPacket, ResetLevel,
    RespPacket,
};

// Re-export picoboot types
pub use picoboot::{
    enumerate_bootloaders, wait_for_bootloader, wait_for_bootloader_at_location,
    PicobootConnection, FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE,
};

/// Create an IN buffer for receiving data
fn new_in_buffer(size: usize) -> Buffer {
    let mut buf = Buffer::new(size);
    buf.set_requested_len(size);
    buf
}
