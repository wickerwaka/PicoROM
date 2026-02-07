use anyhow::Result;
use picolink::find_pico;

pub fn run(current: &str, new: &str) -> Result<()> {
    let mut pico = find_pico(current)?;
    pico.set_ident(new)?;
    println!("Renamed '{}' to '{}'", current, new);
    Ok(())
}

pub fn run_swap(first: &str, second: &str) -> Result<()> {
    let mut pico_a = find_pico(first)?;
    let mut pico_b = find_pico(second)?;
    pico_a.set_ident(second)?;
    pico_b.set_ident(first)?;
    println!("Renamed '{}' to '{}'", first, second);
    println!("Renamed '{}' to '{}'", second, first);
    Ok(())
}
