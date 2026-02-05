//! PICOBOOT - Communication with RP2040 devices in bootloader mode

use anyhow::{anyhow, Result};
use nusb::transfer::{Bulk, In, Out};
use nusb::{Endpoint, Interface, MaybeFuture};
use std::thread::sleep;
use std::time::{Duration, Instant};

use crate::new_in_buffer;

// USB constants for PICOBOOT bootloader
const PICOBOOT_VID: u16 = 0x2E8A;
const PICOBOOT_PID_RP2040: u16 = 0x0003;
const PICOBOOT_MAGIC: u32 = 0x431FD10B;

// Flash constants
pub const FLASH_SECTOR_SIZE: u32 = 4096;
pub const FLASH_PAGE_SIZE: u32 = 256;

/// PICOBOOT command IDs
#[repr(u8)]
#[derive(Debug, Clone, Copy)]
#[allow(dead_code)]
enum PicobootCmd {
    ExclusiveAccess = 0x01,
    Reboot = 0x02,
    FlashErase = 0x03,
    Read = 0x84, // MSB=1 means IN direction
    Write = 0x05,
    ExitXip = 0x06,
    EnterCmdXip = 0x07,
    Exec = 0x08,
}

/// Connection to a device in PICOBOOT bootloader mode
pub struct PicobootConnection {
    device: nusb::Device,
    #[allow(dead_code)]
    interface: Interface,
    ep_out: Endpoint<Bulk, Out>,
    ep_in: Endpoint<Bulk, In>,
    token: u32,
    pub device_id: String,
}

impl PicobootConnection {
    /// Open a connection to a device in bootloader mode
    pub fn open(device_id: Option<&str>) -> Result<PicobootConnection> {
        let devices = nusb::list_devices().wait()?;

        let device_info = devices
            .filter(|d| d.vendor_id() == PICOBOOT_VID && d.product_id() == PICOBOOT_PID_RP2040)
            .find(|d| {
                if let Some(target_id) = device_id {
                    // Try to match by serial number
                    if let Some(serial) = d.serial_number() {
                        return serial == target_id;
                    }
                    // Or by bus:addr
                    let bus_addr = format!("{}:{}", d.bus_id(), d.device_address());
                    return bus_addr == target_id;
                }
                true // Accept any bootloader device if no ID specified
            })
            .ok_or_else(|| {
                if let Some(id) = device_id {
                    anyhow!("Bootloader device '{}' not found", id)
                } else {
                    anyhow!("No bootloader device found")
                }
            })?;

        Self::open_device_info(device_info)
    }

    /// Open a connection to a bootloader device at a specific USB port location
    /// This matches by bus_id and port_chain, which remain stable across device reboots
    pub fn open_at_location(bus_id: &str, port_chain: &[u8]) -> Result<PicobootConnection> {
        let devices = nusb::list_devices().wait()?;

        let device_info = devices
            .filter(|d| d.vendor_id() == PICOBOOT_VID && d.product_id() == PICOBOOT_PID_RP2040)
            .find(|d| d.bus_id() == bus_id && d.port_chain() == port_chain)
            .ok_or_else(|| {
                anyhow!(
                    "No bootloader device found at bus {} port {:?}",
                    bus_id,
                    port_chain
                )
            })?;

        Self::open_device_info(device_info)
    }

    /// Internal helper to open a connection from DeviceInfo
    fn open_device_info(device_info: nusb::DeviceInfo) -> Result<PicobootConnection> {
        let actual_device_id = device_info
            .serial_number()
            .map(|s| s.to_string())
            .unwrap_or_else(|| {
                format!("{}:{}", device_info.bus_id(), device_info.device_address())
            });

        let device = device_info.open().wait()?;

        // Find the vendor-specific interface (class 0xFF)
        let config = device.active_configuration()?;
        let mut vendor_interface = None;
        let mut ep_out_addr = 0u8;
        let mut ep_in_addr = 0u8;

        for iface in config.interfaces() {
            for alt in iface.alt_settings() {
                if alt.class() == 0xFF {
                    // Vendor-specific
                    vendor_interface = Some(iface.interface_number());
                    for ep in alt.endpoints() {
                        match ep.direction() {
                            nusb::transfer::Direction::Out => ep_out_addr = ep.address(),
                            nusb::transfer::Direction::In => ep_in_addr = ep.address(),
                        }
                    }
                    break;
                }
            }
        }

        let interface_num =
            vendor_interface.ok_or_else(|| anyhow!("No vendor-specific interface found"))?;

        if ep_out_addr == 0 || ep_in_addr == 0 {
            return Err(anyhow!("Could not find bulk endpoints"));
        }

        // Try to detach kernel driver from our interface
        let _ = device.detach_kernel_driver(interface_num);

        let interface = device.claim_interface(interface_num).wait()?;

        let ep_out = interface.endpoint::<Bulk, Out>(ep_out_addr)?;
        let ep_in = interface.endpoint::<Bulk, In>(ep_in_addr)?;

        Ok(PicobootConnection {
            device,
            interface,
            ep_out,
            ep_in,
            token: 1,
            device_id: actual_device_id,
        })
    }

    /// Build a PICOBOOT command packet
    fn build_cmd(&mut self, cmd_id: PicobootCmd, args: &[u8], transfer_len: u32) -> [u8; 32] {
        let mut cmd = [0u8; 32];
        cmd[0..4].copy_from_slice(&PICOBOOT_MAGIC.to_le_bytes());
        cmd[4..8].copy_from_slice(&self.token.to_le_bytes());
        cmd[8] = cmd_id as u8;
        cmd[9] = args.len() as u8;
        // bytes 10-11 reserved (0)
        cmd[12..16].copy_from_slice(&transfer_len.to_le_bytes());
        if !args.is_empty() {
            let copy_len = args.len().min(16);
            cmd[16..16 + copy_len].copy_from_slice(&args[..copy_len]);
        }
        self.token += 1;
        cmd
    }

    /// Send a command (header only) and wait for completion
    /// Does NOT read ACK - caller must handle data phase and ACK as needed
    fn send_cmd_header(
        &mut self,
        cmd_id: PicobootCmd,
        args: &[u8],
        transfer_len: u32,
    ) -> Result<()> {
        let cmd = self.build_cmd(cmd_id, args, transfer_len);

        // Send command via bulk OUT
        self.ep_out.submit(cmd.to_vec().into());
        let completion = self
            .ep_out
            .wait_next_complete(Duration::from_millis(1000))
            .ok_or_else(|| anyhow!("PICOBOOT command send timeout"))?;
        completion
            .status
            .map_err(|e| anyhow!("PICOBOOT send error: {:?}", e))?;

        Ok(())
    }

    /// Read ACK from IN endpoint
    fn read_ack(&mut self) -> Result<()> {
        self.ep_in.submit(new_in_buffer(64));
        let ack_completion = self
            .ep_in
            .wait_next_complete(Duration::from_millis(1000))
            .ok_or_else(|| anyhow!("PICOBOOT ACK timeout"))?;
        ack_completion
            .status
            .map_err(|e| anyhow!("PICOBOOT ACK error: {:?}", e))?;
        Ok(())
    }

    /// Send a command without data phase
    fn send_cmd(&mut self, cmd_id: PicobootCmd, args: &[u8]) -> Result<()> {
        self.send_cmd_header(cmd_id, args, 0)?;
        // For OUT direction commands (not Read), we need to read an ACK from IN endpoint
        // The MSB of cmd_id indicates direction: 0x80 = IN, 0x00 = OUT
        if (cmd_id as u8) & 0x80 == 0 {
            self.read_ack()?;
        }
        Ok(())
    }

    /// Get status via control transfer
    fn get_status(&self) -> Result<u32> {
        // Control transfer: bmRequestType=0xC1 (device-to-host, vendor, interface)
        // bRequest=0x42
        let control = nusb::transfer::ControlIn {
            control_type: nusb::transfer::ControlType::Vendor,
            recipient: nusb::transfer::Recipient::Interface,
            request: 0x42,
            value: 0,
            index: self.interface.interface_number() as u16,
            length: 16,
        };

        let status_buf = self
            .device
            .control_in(control, Duration::from_secs(5))
            .wait()
            .map_err(|e| anyhow!("Status control transfer failed: {:?}", e))?;

        if status_buf.len() < 8 {
            return Err(anyhow!(
                "Status response too short: {} bytes",
                status_buf.len()
            ));
        }

        let status_code = u32::from_le_bytes(status_buf[4..8].try_into().unwrap());
        Ok(status_code)
    }

    /// Request exclusive access to the device
    pub fn exclusive_access(&mut self) -> Result<()> {
        // args[0] = 1 for exclusive, 0 to release
        self.send_cmd(PicobootCmd::ExclusiveAccess, &[1])?;
        let status = self.get_status()?;
        if status != 0 {
            return Err(anyhow!("Exclusive access failed with status: {}", status));
        }
        Ok(())
    }

    /// Exit XIP (execute-in-place) mode before flash operations
    pub fn exit_xip(&mut self) -> Result<()> {
        self.send_cmd(PicobootCmd::ExitXip, &[])?;
        let status = self.get_status()?;
        if status != 0 {
            return Err(anyhow!("Exit XIP failed with status: {}", status));
        }
        Ok(())
    }

    /// Erase flash sectors
    /// addr and size must be sector-aligned (4KB)
    pub fn flash_erase(&mut self, addr: u32, size: u32) -> Result<()> {
        if addr % FLASH_SECTOR_SIZE != 0 || size % FLASH_SECTOR_SIZE != 0 {
            return Err(anyhow!(
                "Flash erase address and size must be {}-byte aligned",
                FLASH_SECTOR_SIZE
            ));
        }

        let mut args = [0u8; 8];
        args[0..4].copy_from_slice(&addr.to_le_bytes());
        args[4..8].copy_from_slice(&size.to_le_bytes());

        self.send_cmd(PicobootCmd::FlashErase, &args)?;

        // Erase can take time - poll status
        loop {
            sleep(Duration::from_millis(10));
            let status = self.get_status()?;
            if status == 0 {
                break;
            }
            // Check for actual error vs in-progress
            // In PICOBOOT, a non-zero status during erase might indicate in-progress
            // We need to check bInProgress field at offset 9
        }

        Ok(())
    }

    /// Write data to flash
    /// addr must be page-aligned (256 bytes) and data length must be a multiple of page size
    pub fn flash_write(&mut self, addr: u32, data: &[u8]) -> Result<()> {
        if addr % FLASH_PAGE_SIZE != 0 {
            return Err(anyhow!(
                "Flash write address must be {}-byte aligned",
                FLASH_PAGE_SIZE
            ));
        }

        // Build command with address AND size in args (range_cmd format)
        let mut args = [0u8; 8];
        args[0..4].copy_from_slice(&addr.to_le_bytes()); // dAddr
        args[4..8].copy_from_slice(&(data.len() as u32).to_le_bytes()); // dSize

        // Send command header with transfer length
        self.send_cmd_header(PicobootCmd::Write, &args, data.len() as u32)?;

        // Small delay to let device prepare for data
        sleep(Duration::from_millis(1));

        // Send data via bulk OUT
        self.ep_out.submit(data.to_vec().into());
        let completion = self
            .ep_out
            .wait_next_complete(Duration::from_secs(5))
            .ok_or_else(|| anyhow!("PICOBOOT data send timeout"))?;
        completion
            .status
            .map_err(|e| anyhow!("PICOBOOT data send error: {:?}", e))?;

        // Read ACK after data phase
        self.read_ack()?;

        // Get status to confirm write completed
        let status = self.get_status()?;
        if status != 0 {
            return Err(anyhow!("Flash write failed with status: {}", status));
        }

        Ok(())
    }

    /// Reboot the device
    pub fn reboot(&mut self, delay_ms: u32) -> Result<()> {
        // args: u32 pc (0 = default), u32 sp (0 = default), u32 delay_ms
        let mut args = [0u8; 12];
        args[0..4].copy_from_slice(&0u32.to_le_bytes()); // pc = 0 (use flash boot)
        args[4..8].copy_from_slice(&0u32.to_le_bytes()); // sp = 0
        args[8..12].copy_from_slice(&delay_ms.to_le_bytes());

        self.send_cmd(PicobootCmd::Reboot, &args)?;
        // Don't wait for status - device is rebooting
        Ok(())
    }
}

/// Find all devices in PICOBOOT bootloader mode
pub fn enumerate_bootloaders() -> Result<Vec<nusb::DeviceInfo>> {
    let devices: Vec<_> = nusb::list_devices()
        .wait()?
        .filter(|d| d.vendor_id() == PICOBOOT_VID && d.product_id() == PICOBOOT_PID_RP2040)
        .collect();
    Ok(devices)
}

/// Wait for a bootloader device to appear
pub fn wait_for_bootloader(
    device_id: Option<&str>,
    timeout: Duration,
) -> Result<PicobootConnection> {
    let deadline = Instant::now() + timeout;

    loop {
        if let Ok(conn) = PicobootConnection::open(device_id) {
            return Ok(conn);
        }

        if Instant::now() >= deadline {
            return Err(anyhow!("Timeout waiting for bootloader device"));
        }

        sleep(Duration::from_millis(100));
    }
}

/// Wait for a bootloader device at a specific USB port location
/// This is more reliable than matching by serial number because the bootloader
/// uses a different serial number than the application firmware
pub fn wait_for_bootloader_at_location(
    bus_id: &str,
    port_chain: &[u8],
    timeout: Duration,
) -> Result<PicobootConnection> {
    let deadline = Instant::now() + timeout;

    loop {
        if let Ok(conn) = PicobootConnection::open_at_location(bus_id, port_chain) {
            return Ok(conn);
        }

        if Instant::now() >= deadline {
            return Err(anyhow!(
                "Timeout waiting for bootloader device at {}:{:?}",
                bus_id,
                port_chain
            ));
        }

        sleep(Duration::from_millis(100));
    }
}
