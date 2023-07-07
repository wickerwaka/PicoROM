use anyhow::{anyhow, Result};
use clap::{Parser, Subcommand};
use indicatif;
use indicatif::ProgressBar;
use indicatif::ProgressStyle;
use std::fs;
use std::iter;
use std::path::{Path, PathBuf};
use std::time::Duration;
use clap_num::maybe_hex;

mod enumerate;
mod pico_link;
mod rom_size;

use crate::enumerate::*;
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

    Comms {
        name: String,
        #[arg(value_parser=maybe_hex::<u32>)]
        addr: u32
    }
}

fn main() -> Result<()> {
    let args = Cli::parse();

    match args.command {
        Commands::List => {
            let found = enumerate_picos()?;
            for k in found.keys() {
                println!("  {}", k);
            }
        }
        Commands::Rename { current, new } => {
            let mut pico = find_pico(&current)?;
            pico.set_ident(&new)?;
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
        },
        Commands::Comms { name, addr } => {
            let mut pico = find_pico(&name)?;
            pico.send(pico_link::ReqPacket::CommsStart(addr))?;
            pico.send(pico_link::ReqPacket::CommsData("HELLO WORLD.  ".to_owned().into_bytes()))?;

            pico.recv_forever()?;
        }
    }

    Ok(())
}