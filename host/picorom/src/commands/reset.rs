use anyhow::Result;
use picolink::find_pico;

pub fn run(name: &str, level: &str) -> Result<()> {
    let mut pico = find_pico(name)?;
    pico.set_parameter("reset", level)?;
    println!("Setting '{}' reset pin to: {}", name, level);
    Ok(())
}
