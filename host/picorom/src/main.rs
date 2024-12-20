use anyhow::{anyhow, Result};
use clap::{Parser, Subcommand};
use indicatif;
use indicatif::ProgressBar;
use indicatif::ProgressStyle;
use std::fs;
use std::iter;
use std::path::{Path, PathBuf};
use std::time::Duration;

use picolink::*;

mod rom_size;
use crate::rom_size::*;

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

    Ok(data.repeat(RomSize::MBit(2).bytes() / rom_size.bytes()))
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
        value: String
    },
}

fn main() -> Result<()> {
    let args = Cli::parse();

    match args.command {
        Commands::List => {
            let found = enumerate_picos()?;
            if found.len() > 0 {
                println!("Available PicoROMs:");
                for k in found.keys() {
                    println!("  {}", k);
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
        Commands::Reset { name, level } => {
            let mut pico = find_pico(&name)?;
            pico.set_parameter("reset", &level)?;
            println!("Setting '{}' reset pin to: {}", name, level);
        },
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
        },
        Commands::Set { name, param, value } => {
            let mut pico = find_pico(&name)?;
            let newvalue = pico.set_parameter(&param, &value)?;
            println!("{}={}", param, newvalue);
        },
    }

    Ok(())
}
