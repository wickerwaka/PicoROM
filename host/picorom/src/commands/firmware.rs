use anyhow::{anyhow, Result};
use indicatif::{ProgressBar, ProgressStyle};
use std::io::{self, Write};
use std::path::Path;
use std::thread::sleep;
use std::time::Duration;

use picolink::{
    enumerate_all_devices, find_pico, get_device_location, reboot_to_bootloader,
    wait_for_bootloader_at_location, wait_for_device_at_location, DetectedDevice, DeviceMode,
    PicobootConnection, FLASH_SECTOR_SIZE,
};

use crate::embedded_firmware;
use crate::firmware::{upload_firmware, ProgressKind};
use crate::uf2::Uf2File;

pub fn run(
    name: Option<&str>,
    firmware_path: Option<&Path>,
    yes: bool,
    no_reboot: bool,
) -> Result<()> {
    // Resolve target device - either by name or auto-detect
    let target_device: DetectedDevice = if let Some(device_name) = name {
        // Explicit device name provided - find it
        match find_pico(device_name) {
            Ok(_) => {
                let (bus_id, port_chain) = get_device_location(device_name)?;
                DetectedDevice {
                    mode: DeviceMode::Application,
                    display_name: device_name.to_string(),
                    device_id: device_name.to_string(),
                    bus_id,
                    port_chain,
                }
            }
            Err(_) => {
                // Check if there's a bootloader device
                match PicobootConnection::open(None) {
                    Ok(conn) => {
                        // Try to find a bootloader matching by name pattern or accept any
                        let all_devices = enumerate_all_devices()?;
                        let bootloaders: Vec<_> = all_devices
                            .into_iter()
                            .filter(|d| matches!(d.mode, DeviceMode::Bootloader))
                            .collect();

                        if bootloaders.is_empty() {
                            return Err(anyhow!(
                                "Device '{}' not found and no bootloader device available",
                                device_name
                            ));
                        }

                        // Use the first bootloader found
                        println!(
                            "Device '{}' not found, using bootloader: {}",
                            device_name, conn.device_id
                        );
                        drop(conn); // Release connection, we'll reconnect later
                        bootloaders.into_iter().next().unwrap()
                    }
                    Err(_) => {
                        return Err(anyhow!(
                            "Device '{}' not found and no bootloader device available",
                            device_name
                        ));
                    }
                }
            }
        }
    } else {
        // Auto-detect: enumerate all devices
        let all_devices = enumerate_all_devices()?;

        match all_devices.len() {
            0 => {
                return Err(anyhow!(
                    "No PicoROM devices found.\n\
                     Connect a device or hold BOOTSEL while connecting for bootloader mode."
                ));
            }
            1 => {
                let device = all_devices.into_iter().next().unwrap();
                let mode_str = match device.mode {
                    DeviceMode::Application => "application mode",
                    DeviceMode::Bootloader => "bootloader mode",
                    DeviceMode::Resettable => "resettable",
                };
                println!("Auto-detected: {} ({})", device.display_name, mode_str);
                device
            }
            _ => {
                eprintln!(
                    "Error: Found {} devices. Please specify which device to flash:",
                    all_devices.len()
                );
                for device in &all_devices {
                    let mode_str = match device.mode {
                        DeviceMode::Application => "application mode",
                        DeviceMode::Bootloader => "bootloader mode",
                        DeviceMode::Resettable => "resettable",
                    };
                    eprintln!("  {} ({})", device.display_name, mode_str);
                }
                eprintln!();
                eprintln!("Usage: picorom firmware <name>");
                return Err(anyhow!("Multiple devices found"));
            }
        }
    };

    // Parse firmware file based on extension, or select from embedded firmware
    let (uf2, firmware_label) = if let Some(firmware_path) = firmware_path {
        let extension = firmware_path
            .extension()
            .and_then(|e| e.to_str())
            .map(|e| e.to_lowercase());

        let uf2 = match extension.as_deref() {
            Some("uf2") => Uf2File::parse(firmware_path)?,
            Some("bin") => Uf2File::parse_bin(firmware_path)?,
            Some(ext) => return Err(anyhow!("Unsupported firmware format: .{}", ext)),
            None => return Err(anyhow!("Firmware file has no extension")),
        };
        (uf2, format!("{:?}", firmware_path))
    } else {
        // Select from embedded firmware
        let firmwares = embedded_firmware::read_embedded_firmware()?;
        if firmwares.is_empty() {
            return Err(anyhow!("No embedded firmware and no file specified"));
        }

        let items: Vec<&str> = firmwares.iter().map(|f| f.display_name.as_str()).collect();

        let selection = dialoguer::Select::new()
            .with_prompt("Select firmware version")
            .items(&items)
            .default(0)
            .interact()?;

        let selected = &firmwares[selection];
        let label = selected.display_name.clone();
        let uf2 = Uf2File::parse_bin_bytes(&selected.data)?;
        (uf2, label)
    };

    let (start_addr, end_addr) = uf2
        .address_range()
        .ok_or_else(|| anyhow!("Firmware file contains no data"))?;

    // Show summary
    println!("Firmware: {}", firmware_label);
    println!(
        "  Blocks: {}, Total size: {} bytes",
        uf2.block_count,
        uf2.total_bytes()
    );
    println!("  Address range: 0x{:08X} - 0x{:08X}", start_addr, end_addr);

    let sectors = uf2.sectors_to_erase(FLASH_SECTOR_SIZE);
    let total_erase: u32 = sectors.iter().map(|(_, s)| s).sum();
    println!(
        "  Sectors to erase: {} ({} bytes)",
        sectors.len(),
        total_erase
    );

    // Confirmation prompt
    if !yes {
        print!(
            "\nFlash firmware to '{}'? [y/N] ",
            target_device.display_name
        );
        io::stdout().flush()?;

        let mut input = String::new();
        io::stdin().read_line(&mut input)?;

        if !input.trim().eq_ignore_ascii_case("y") {
            println!("Aborted.");
            return Ok(());
        }
    }

    // Connect to device based on its mode
    let (mut conn, bus_id, port_chain) = match target_device.mode {
        DeviceMode::Application => {
            // Device is in application mode - need to reboot to bootloader
            let mut pico = find_pico(&target_device.display_name)?;
            let bus_id = target_device.bus_id.clone();
            let port_chain = target_device.port_chain.clone();

            println!(
                "\nSending '{}' to bootloader...",
                target_device.display_name
            );
            pico.usb_boot()?;

            // Wait for device to disconnect
            sleep(Duration::from_millis(500));

            // Wait for bootloader to appear at the same USB port location
            let spinner = ProgressBar::new_spinner()
                .with_prefix("Waiting for bootloader")
                .with_style(
                    ProgressStyle::with_template("{prefix:.bold} {spinner} {msg}")
                        .unwrap()
                        .tick_chars(r"\|/--"),
                );
            spinner.enable_steady_tick(Duration::from_millis(100));

            let conn =
                wait_for_bootloader_at_location(&bus_id, &port_chain, Duration::from_secs(10))?;
            spinner.finish_with_message("Connected");
            (conn, bus_id, port_chain)
        }
        DeviceMode::Bootloader => {
            // Device is already in bootloader mode - connect directly
            println!("\nConnecting to bootloader...");
            let conn = PicobootConnection::open_at_location(
                &target_device.bus_id,
                &target_device.port_chain,
            )?;
            (
                conn,
                target_device.bus_id.clone(),
                target_device.port_chain.clone(),
            )
        }
        DeviceMode::Resettable => {
            // RP2040 device with reset interface - reboot to bootloader
            let bus_id = target_device.bus_id.clone();
            let port_chain = target_device.port_chain.clone();

            println!(
                "\nSending '{}' to bootloader...",
                target_device.display_name
            );
            reboot_to_bootloader(&bus_id, &port_chain)?;

            // Wait for device to disconnect
            sleep(Duration::from_millis(500));

            // Wait for bootloader to appear at the same USB port location
            let spinner = ProgressBar::new_spinner()
                .with_prefix("Waiting for bootloader")
                .with_style(
                    ProgressStyle::with_template("{prefix:.bold} {spinner} {msg}")
                        .unwrap()
                        .tick_chars(r"\|/--"),
                );
            spinner.enable_steady_tick(Duration::from_millis(100));

            let conn =
                wait_for_bootloader_at_location(&bus_id, &port_chain, Duration::from_secs(10))?;
            spinner.finish_with_message("Connected");
            (conn, bus_id, port_chain)
        }
    };

    // Erase progress bar
    let erase_progress = ProgressBar::new(total_erase as u64)
        .with_prefix("Erasing flash")
        .with_style(
            ProgressStyle::with_template(
                "{prefix:.bold} [{wide_bar:.yellow/red}] {bytes}/{total_bytes}",
            )
            .unwrap()
            .progress_chars("#>-"),
        );

    // Write progress bar
    let write_progress = ProgressBar::new(uf2.total_bytes() as u64)
        .with_prefix("Writing flash")
        .with_style(
            ProgressStyle::with_template(
                "{prefix:.bold} [{wide_bar:.cyan/blue}] {bytes}/{total_bytes}",
            )
            .unwrap()
            .progress_chars("#>-"),
        );

    // Upload firmware
    upload_firmware(&uf2, &mut conn, |kind, current, _total| match kind {
        ProgressKind::Erase => erase_progress.set_position(current),
        ProgressKind::Write => write_progress.set_position(current),
    })?;

    erase_progress.finish();
    write_progress.finish();

    if !no_reboot {
        println!("Rebooting device...");
        conn.reboot(500)?;

        // Wait for device to come back
        sleep(Duration::from_millis(1000));

        let spinner = ProgressBar::new_spinner()
            .with_prefix("Waiting for device")
            .with_style(
                ProgressStyle::with_template("{prefix:.bold} {spinner} {msg}")
                    .unwrap()
                    .tick_chars(r"\|/--"),
            );
        spinner.enable_steady_tick(Duration::from_millis(100));

        // Wait for device at the same USB location
        match wait_for_device_at_location(&bus_id, &port_chain, Duration::from_secs(10)) {
            Ok(_) => spinner.finish_with_message("Device online"),
            Err(_) => spinner.finish_with_message("Timeout (device may still boot)"),
        }

        println!("\nFirmware update complete!");
    } else {
        println!("\nFirmware written. Device left in bootloader mode.");
    }

    Ok(())
}
