//! Embedded firmware extraction

#[cfg(not(has_embedded_firmware))]
use anyhow::anyhow;
use anyhow::Result;
#[cfg(has_embedded_firmware)]
use std::io::{Cursor, Read};
#[cfg(has_embedded_firmware)]
use zip::ZipArchive;

pub struct EmbeddedFirmware {
    pub display_name: String,
    pub data: Vec<u8>,
}

/// Parse filename like "PicoROM-2MBit_100ns-1_7.bin" into display name
#[cfg(any(has_embedded_firmware, test))]
fn parse_firmware_name(filename: &str) -> String {
    let name = filename.strip_suffix(".bin").unwrap_or(filename);
    let parts: Vec<&str> = name.split('-').collect();

    if parts.len() >= 3 && parts[0] == "PicoROM" {
        let variant = parts[1..parts.len() - 1].join("-");
        let version = parts.last().unwrap().replace('_', ".");
        format!("{} v{}", variant, version)
    } else {
        filename.to_string()
    }
}

#[cfg(has_embedded_firmware)]
fn extract_firmware_from_zip(zip_data: &[u8]) -> Result<Vec<EmbeddedFirmware>> {
    let mut archive = ZipArchive::new(Cursor::new(zip_data))?;
    let mut firmwares = Vec::new();

    for i in 0..archive.len() {
        let mut entry = archive.by_index(i)?;
        let name = entry.name().to_string();

        if name.ends_with(".bin") {
            let mut data = Vec::new();
            entry.read_to_end(&mut data)?;

            firmwares.push(EmbeddedFirmware {
                display_name: parse_firmware_name(&name),
                data,
            });
        }
    }

    firmwares.sort_by(|a, b| a.display_name.cmp(&b.display_name));
    Ok(firmwares)
}

#[cfg(has_embedded_firmware)]
pub fn read_embedded_firmware() -> Result<Vec<EmbeddedFirmware>> {
    static FIRMWARE_ZIP: &[u8] = include_bytes!(concat!(env!("OUT_DIR"), "/firmware-bundle.zip"));
    extract_firmware_from_zip(FIRMWARE_ZIP)
}

#[cfg(not(has_embedded_firmware))]
pub fn read_embedded_firmware() -> Result<Vec<EmbeddedFirmware>> {
    Err(anyhow!(
        "No embedded firmware (build with EMBED_FIRMWARE=<path>)"
    ))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_firmware_name() {
        assert_eq!(parse_firmware_name("PicoROM-2MBit-1_7.bin"), "2MBit v1.7");
        assert_eq!(
            parse_firmware_name("PicoROM-2MBit_100ns-1_7.bin"),
            "2MBit_100ns v1.7"
        );
        assert_eq!(
            parse_firmware_name("PicoROM-32P_2MBit-1_7_3.bin"),
            "32P_2MBit v1.7.3"
        );
        assert_eq!(parse_firmware_name("other.bin"), "other.bin");
    }
}
