use anyhow::Result;
use picolink::find_pico;

pub fn run_get(name: &str, param: Option<&str>) -> Result<()> {
    let mut pico = find_pico(name)?;
    if let Some(param) = param {
        let value = pico.get_parameter(param)?;
        println!("{}={}", param, value);
    } else {
        let params = pico.get_parameters()?;
        for p in params {
            let value = pico.get_parameter(&p)?;
            println!("{}={}", p, value);
        }
    }
    Ok(())
}

pub fn run_set(name: &str, param: &str, value: &str) -> Result<()> {
    let mut pico = find_pico(name)?;
    let newvalue = pico.set_parameter(param, value)?;
    println!("{}={}", param, newvalue);
    Ok(())
}
