use anyhow::Result;
use indicatif::{ProgressBar, ProgressStyle};
use picolink::find_pico;
use std::time::Duration;

pub fn run(name: &str) -> Result<()> {
    let mut pico = find_pico(name)?;
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
    Ok(())
}
