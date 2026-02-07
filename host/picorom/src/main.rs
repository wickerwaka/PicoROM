use anyhow::Result;
use clap::{Parser, Subcommand};
use std::path::PathBuf;

mod commands;
mod embedded_firmware;
mod firmware;
mod rom_size;
mod uf2;

use crate::rom_size::RomSize;

#[derive(Debug, Parser)]
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
        /// Emulate a specific ROM size. If not specified, uses the device's current rom_size.
        #[arg(value_enum, ignore_case = true)]
        size: Option<RomSize>,
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

    /// Upload firmware to a PicoROM device
    Firmware {
        /// PicoROM device name (optional if only one device connected)
        name: Option<String>,
        /// Path to firmware file (.uf2 or .bin) - if omitted, select from embedded firmware
        #[arg(short = 'f', long = "file")]
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
        Commands::List => commands::list::run()?,
        Commands::Identify { name } => commands::identify::run(&name)?,
        Commands::Commit { name } => commands::commit::run(&name)?,
        Commands::Rename { current, new } => commands::rename::run(&current, &new)?,
        Commands::NameSwap { first, second } => commands::rename::run_swap(&first, &second)?,
        Commands::Upload {
            name,
            source,
            size,
            store,
        } => commands::upload::run(&name, &source, size, store)?,
        Commands::Download { name, dest } => commands::download::run(&name, &dest)?,
        Commands::Reset { name, level } => commands::reset::run(&name, &level)?,
        Commands::Get { name, param } => commands::parameter::run_get(&name, param.as_deref())?,
        Commands::Set { name, param, value } => {
            commands::parameter::run_set(&name, &param, &value)?
        }
        Commands::Firmware {
            name,
            firmware,
            yes,
            no_reboot,
        } => commands::firmware::run(name.as_deref(), firmware.as_deref(), yes, no_reboot)?,
    }

    Ok(())
}
