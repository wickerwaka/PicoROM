use anyhow::{anyhow, Result};
use indicatif::{ProgressBar, ProgressStyle};
use picolink::find_pico;
use std::fs;
use std::path::Path;
use std::time::Duration;

use crate::rom_size::RomSize;

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
    data.extend(std::iter::repeat_n(0u8, diff));

    Ok(data)
}

pub fn run(name: &str, source: &Path, size: Option<RomSize>, store: bool) -> Result<()> {
    let mut pico = find_pico(name)?;

    // Use provided size or read from device
    let size = match size {
        Some(s) => s,
        None => {
            let rom_size_str = pico.get_parameter("rom_size")?;
            RomSize::from_hex_bytes(&rom_size_str)
                .ok_or_else(|| anyhow!("Invalid rom_size from device: {}", rom_size_str))?
        }
    };

    let data = read_file(source, size)?;
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
    Ok(())
}
