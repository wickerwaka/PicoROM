use anyhow::Result;
use picolink::find_pico;

pub fn run(name: &str) -> Result<()> {
    let mut pico = find_pico(name)?;
    pico.identify()?;
    println!("Requested identification from '{}'", name);
    Ok(())
}
