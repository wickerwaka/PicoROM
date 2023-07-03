use anyhow::{anyhow, Result};
use clap::{Parser, Subcommand};
use std::fs;
use std::iter;
use std::path::{Path, PathBuf};

mod enumerate;
mod pico_link;
mod rom_size;

use crate::enumerate::*;
use crate::rom_size::*;

fn read_file(name: &Path, size: RomSize) -> Result<Vec<u8>> {
    let mut data = fs::read(name)?;
    if data.len() > size.bytes() {
        return Err(anyhow!(
            "{:?} larger ({}) than rom size ({})",
            name,
            data.len(),
            size.bytes()
        ));
    }

    let diff = size.bytes() - data.len();
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
    List,
    Rename {
        #[arg(value_name = "CURRENT_NAME")]
        original: String,
        #[arg(value_name = "NEW_NAME")]
        new: String,
    },
    Upload {
        #[arg(value_name = "NAME")]
        name: String,
        #[arg(value_name = "FILE")]
        source: PathBuf,
    },
}

fn main() -> Result<()> {
    let args = Cli::parse();

    match args.command {
        Commands::List => {
            let found = enumerate_picos()?;
            for k in found.keys() {
                println!("* {}", k);
            }
        }
        Commands::Rename { original, new } => {
            let mut pico = find_pico(&original)?;
            pico.set_ident(&new)?;
        }
        Commands::Upload { name, source } => {
            let mut pico = find_pico(&name)?;
            let data = read_file(source.as_path(), RomSize::MBit(2))?;
            pico.upload(&data)?;
        }
    }

    Ok(())
}
