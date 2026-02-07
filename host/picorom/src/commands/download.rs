use anyhow::Result;
use indicatif::{ProgressBar, ProgressStyle};
use picolink::find_pico;
use std::fs;
use std::path::Path;

pub fn run(name: &str, dest: &Path) -> Result<()> {
    let mut pico = find_pico(name)?;

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

    fs::write(dest, &data)?;
    println!("Downloaded {} bytes to {:?}", data.len(), dest);
    Ok(())
}
