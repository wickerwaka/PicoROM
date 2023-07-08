use anyhow::{anyhow, Result};
use std::collections::HashMap;

use crate::pico_link::*;

/// Find all USB serial ports matching the PicoROM VID:PID
fn enumerate_ports() -> Result<Vec<String>> {
    let mut ports = Vec::new();
    let all_ports = serialport::available_ports()?;

    for p in all_ports.iter() {
        match &p.port_type {
            serialport::SerialPortType::UsbPort(info) => {
                if info.vid == 0x2e8a && info.pid == 0x000a {
                    ports.push(p.port_name.clone());
                }
            }
            _ => {}
        }
    }

    Ok(ports)
}

pub fn enumerate_picos() -> Result<HashMap<String, PicoLink>> {
    let mut found = HashMap::new();
    for p in enumerate_ports()?.iter() {
        let link = PicoLink::open(p);
        if let Ok(mut link) = link {
            if let Ok(ident) = link.get_ident() {
                found.insert(ident, link);
            }
        }
    }

    Ok(found)
}

pub fn find_pico(name: &str) -> Result<PicoLink> {
    let mut found = enumerate_picos()?;

    if let Some(pico) = found.remove(name) {
        Ok(pico)
    } else {
        Err(anyhow!("PicoROM '{}' not found.", name))
    }
}
