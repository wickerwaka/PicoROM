use anyhow::{anyhow, Result};
use clap::{Parser, Subcommand};
use indicatif::{ProgressBar, ProgressStyle};
use std::fs;
use std::io::{self, Write};
use std::iter;
use std::path::{Path, PathBuf};
use std::thread::sleep;
use std::time::Duration;

use picolink::{
    enumerate_picos, find_pico, get_device_location, wait_for_bootloader_at_location,
    PicobootConnection, FLASH_SECTOR_SIZE,
};

mod embedded_firmware;
mod firmware;
mod rom_size;
mod uf2;

use crate::firmware::{upload_firmware, ProgressKind};
use crate::rom_size::*;
use crate::uf2::Uf2File;

fn read_file(name: &Path, rom_size: RomSize) -> Result<Vec<u8>> {
    let mut data = fs::read(name)?;
    if data.len() > rom_size.bytes() {
        return Err(anyhow!(
            "{:?} larger ({}) than rom size ({})",
            name,
            data.len(),
            rom_size.bytes()
        ));
    }

    let diff = rom_size.bytes() - data.len();
    data.extend(iter::repeat(0u8).take(diff));

    Ok(data)
}

#[derive(Debug, Parser)] // requires `derive` feature
#[command(name = "picorom")]
#[command(about = "PicoROM controller", long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Debug, Subcommand)]
enum Commands {
    /// Return a list of currently connected PicoROM devices.
    List,

    /// Flash the activity LED on a specific PicoRom
    Identify {
        /// PicoROM device name.
        name: String,
    },

    /// Commit the current ROM image to flash memory
    Commit {
        /// PicoROM device name.
        name: String,
    },

    /// Change the name of a PicoROM device.
    Rename {
        /// Current name.
        current: String,
        /// New name to rename it to.
        new: String,
    },

    /// Swap the names of two PicoROM devices.
    NameSwap {
        /// First device name.
        first: String,
        /// Second device name.
        second: String,
    },

    /// Upload a ROM image to a PicoROM
    Upload {
        /// PicoROM device name.
        name: String,
        /// Path of file to upload.
        source: PathBuf,
        /// Emulate a specific ROM size.
        #[arg(value_enum, ignore_case=true, default_value_t=RomSize::MBit(2))]
        size: RomSize,
        /// Store the uploaded image in flash memory also.
        #[arg(short, long, default_value_t = false)]
        store: bool,
    },

    /// Download the current ROM image from a PicoROM to a file
    Download {
        /// PicoROM device name.
        name: String,
        /// Destination file path.
        dest: PathBuf,
    },

    /// Set the level of the reset pin
    Reset {
        /// PicoROM device name.
        name: String,

        /// Reset level
        #[arg(value_parser = clap::builder::PossibleValuesParser::new(["high", "low", "z"]))]
        level: String,
    },

    /// Get the value of a parameter
    Get {
        /// PicoROM device name.
        name: String,

        /// Parameter name
        param: Option<String>,
    },

    /// Set a parameter to a new value
    Set {
        /// PicoROM device name.
        name: String,

        /// Parameter name
        param: String,

        /// Parameter value
        value: String,
    },

    /// Reboot the device into USB mode
    USBBoot { name: String },

    /// Upload firmware to a PicoROM device
    Firmware {
        /// PicoROM device name
        name: String,
        /// Path to firmware file (.uf2 or .bin) - if omitted, select from embedded firmware
        firmware: Option<PathBuf>,
        /// Skip confirmation prompt
        #[arg(short = 'y', long)]
        yes: bool,
        /// Don't reboot after flashing
        #[arg(long)]
        no_reboot: bool,
    },
}

fn main() -> Result<()> {
    let args = Cli::parse();

    match args.command {
        Commands::List => {
            let found = enumerate_picos()?;
            if found.len() > 0 {
                println!("Available PicoROMs:");
                for (k, v) in found.iter() {
                    println!("  {:16} [{}]", k, v.device_id);
                }
            } else {
                println!("No PicoROMs found.");
            }
        }
        Commands::Identify { name } => {
            let mut pico = find_pico(&name)?;
            pico.identify()?;
            println!("Requested identification from '{}'", name);
        }
        Commands::Commit { name } => {
            let mut pico = find_pico(&name)?;
            let spinner = ProgressBar::new_spinner()
                .with_prefix("Storing to Flash")
                .with_style(
                    ProgressStyle::with_template("{prefix:.bold} {spinner} {msg}")
                        .unwrap()
                        .tick_chars(r"\|/--"),
                );
            spinner.enable_steady_tick(Duration::from_millis(250));
            pico.commit_rom()?;
            spinner.finish_with_message("Done.");
        }
        Commands::Rename { current, new } => {
            let mut pico = find_pico(&current)?;
            pico.set_ident(&new)?;
            println!("Renamed '{}' to '{}'", current, new);
        }
        Commands::NameSwap { first, second } => {
            let mut pico_a = find_pico(&first)?;
            let mut pico_b = find_pico(&second)?;
            pico_a.set_ident(&second)?;
            pico_b.set_ident(&first)?;
            println!("Renamed '{}' to '{}'", first, second);
            println!("Renamed '{}' to '{}'", second, first);
        }
        Commands::Upload {
            name,
            source,
            size,
            store,
        } => {
            let mut pico = find_pico(&name)?;
            let data = read_file(source.as_path(), size)?;
            let progress = ProgressBar::new(data.len() as u64)
                .with_prefix("Uploading ROM")
                .with_style(
                    ProgressStyle::with_template("{prefix:.bold} [{wide_bar:.cyan/blue}] {msg:10}")
                        .unwrap()
                        .progress_chars("#>-"),
                );
            pico.upload(&data, size.mask(), |x| progress.inc(x as u64))?;
            progress.finish_with_message("Done.");
            if let Some(filename) = source.file_name() {
                pico.set_parameter("rom_name", filename.to_string_lossy().as_ref())?;
            }
            if store {
                let spinner = ProgressBar::new_spinner()
                    .with_prefix("Storing to Flash")
                    .with_style(
                        ProgressStyle::with_template("{prefix:.bold} {spinner} {msg}")
                            .unwrap()
                            .tick_chars(r"\|/--"),
                    );
                spinner.enable_steady_tick(Duration::from_millis(250));
                pico.commit_rom()?;
                spinner.finish_with_message("Done.");
            }
        }
        Commands::Download { name, dest } => {
            let mut pico = find_pico(&name)?;

            // Get addr_mask to determine ROM size
            let mask_str = pico.get_parameter("addr_mask")?;
            let mask_str = mask_str.trim().strip_prefix("0x").unwrap_or(&mask_str);
            let addr_mask = u32::from_str_radix(mask_str, 16)?;
            let size = (addr_mask + 1) as usize;

            let progress = ProgressBar::new(size as u64)
                .with_prefix("Downloading ROM")
                .with_style(
                    ProgressStyle::with_template("{prefix:.bold} [{wide_bar:.cyan/blue}] {msg:10}")
                        .unwrap()
                        .progress_chars("#>-"),
                );

            let data = pico.download(size, |x| progress.inc(x as u64))?;
            progress.finish_with_message("Done.");

            fs::write(&dest, &data)?;
            println!("Downloaded {} bytes to {:?}", data.len(), dest);
        }
        Commands::Reset { name, level } => {
            let mut pico = find_pico(&name)?;
            pico.set_parameter("reset", &level)?;
            println!("Setting '{}' reset pin to: {}", name, level);
        }
        Commands::Get { name, param } => {
            let mut pico = find_pico(&name)?;
            if let Some(param) = param {
                let value = pico.get_parameter(&param)?;
                println!("{}={}", param, value);
            } else {
                let params = pico.get_parameters()?;
                for p in params {
                    let value = pico.get_parameter(&p)?;
                    println!("{}={}", p, value);
                }
            }
        }
        Commands::Set { name, param, value } => {
            let mut pico = find_pico(&name)?;
            let newvalue = pico.set_parameter(&param, &value)?;
            println!("{}={}", param, newvalue);
        }

        Commands::USBBoot { name } => {
            let mut pico = find_pico(&name)?;
            println!("Requesting USB boot");
            pico.usb_boot()?;
        }

        Commands::Firmware {
            name,
            firmware,
            yes,
            no_reboot,
        } => {
            // Parse firmware file based on extension, or select from embedded firmware
            let (uf2, firmware_label) = if let Some(firmware_path) = firmware {
                let extension = firmware_path
                    .extension()
                    .and_then(|e| e.to_str())
                    .map(|e| e.to_lowercase());

                let uf2 = match extension.as_deref() {
                    Some("uf2") => Uf2File::parse(&firmware_path)?,
                    Some("bin") => Uf2File::parse_bin(&firmware_path)?,
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
                print!("\nFlash firmware to '{}'? [y/N] ", name);
                io::stdout().flush()?;

                let mut input = String::new();
                io::stdin().read_line(&mut input)?;

                if !input.trim().eq_ignore_ascii_case("y") {
                    println!("Aborted.");
                    return Ok(());
                }
            }

            // Try to find device and send to bootloader, or connect to existing bootloader
            let mut conn = match find_pico(&name) {
                Ok(mut pico) => {
                    // Capture USB port location before rebooting
                    // This allows us to identify the same physical device in bootloader mode
                    let (bus_id, port_chain) = get_device_location(&name)?;

                    println!("\nSending '{}' to bootloader...", name);
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

                    let conn = wait_for_bootloader_at_location(
                        &bus_id,
                        &port_chain,
                        Duration::from_secs(10),
                    )?;
                    spinner.finish_with_message("Connected");
                    conn
                }
                Err(_) => {
                    // Device not found - check if there's already a bootloader device
                    println!("\nDevice '{}' not found, checking for bootloader...", name);
                    match PicobootConnection::open(None) {
                        Ok(conn) => {
                            println!("Found bootloader device: {}", conn.device_id);
                            conn
                        }
                        Err(_) => {
                            return Err(anyhow!(
                                "Device '{}' not found and no bootloader device available",
                                name
                            ));
                        }
                    }
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

                // Try to reconnect
                let deadline = std::time::Instant::now() + Duration::from_secs(10);
                loop {
                    if let Ok(_pico) = find_pico(&name) {
                        spinner.finish_with_message("Device online");
                        break;
                    }
                    if std::time::Instant::now() >= deadline {
                        spinner.finish_with_message("Timeout (device may still boot)");
                        break;
                    }
                    sleep(Duration::from_millis(200));
                }

                println!("\nFirmware update complete!");
            } else {
                println!("\nFirmware written. Device left in bootloader mode.");
            }
        }
    }

    Ok(())
}
