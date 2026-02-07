use anyhow::Result;
use picolink::enumerate_picos;

pub fn run() -> Result<()> {
    let found = enumerate_picos()?;
    if !found.is_empty() {
        println!("Available PicoROMs:");
        for (k, v) in found.iter() {
            println!("  {:16} [{}]", k, v.device_id);
        }
    } else {
        println!("No PicoROMs found.");
    }
    Ok(())
}
