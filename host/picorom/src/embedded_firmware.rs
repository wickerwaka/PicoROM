//! Embedded firmware extraction from appended zip file

use anyhow::{anyhow, Result};
use std::io::{Cursor, Read};
use zip::ZipArchive;

pub struct EmbeddedFirmware {
    pub display_name: String,
    pub data: Vec<u8>,
}

/// Parse filename like "PicoROM-2MBit_100ns-1_7.bin" into display name
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

/// Find the start of zip data by looking for PK\x03\x04 (local file header) signature
fn find_zip_start(data: &[u8]) -> Option<usize> {
    // ZIP local file header signature: PK\x03\x04
    let signature: [u8; 4] = [0x50, 0x4b, 0x03, 0x04];

    // Search from the end since the zip is appended
    // We look for the last occurrence of the signature
    for i in (0..data.len().saturating_sub(4)).rev() {
        if data[i..i + 4] == signature {
            // Verify this looks like a valid zip by checking we can parse it
            if let Ok(_) = ZipArchive::new(Cursor::new(&data[i..])) {
                return Some(i);
            }
        }
    }
    None
}

/// Read embedded firmware zip from the executable.
/// The zip is appended to the end of the executable.
pub fn read_embedded_firmware() -> Result<Vec<EmbeddedFirmware>> {
    let exe_path = std::env::current_exe()?;

    // Resolve symlinks to get the actual file with appended zip data
    let exe_path = exe_path.canonicalize().unwrap_or(exe_path);

    let data = std::fs::read(&exe_path)?;

    // Find where the zip data starts
    let zip_start = find_zip_start(&data).ok_or_else(|| anyhow!("No embedded firmware found"))?;

    // Parse the zip data
    let zip_data = &data[zip_start..];
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

    // Sort by display name
    firmwares.sort_by(|a, b| a.display_name.cmp(&b.display_name));

    Ok(firmwares)
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
