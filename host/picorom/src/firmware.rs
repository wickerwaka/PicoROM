//! Firmware upload orchestration for PicoROM devices

use anyhow::Result;
use picolink::{PicobootConnection, FLASH_PAGE_SIZE, FLASH_SECTOR_SIZE};

use crate::uf2::Uf2File;

/// Progress update kind
#[derive(Debug, Clone, Copy)]
pub enum ProgressKind {
    Erase,
    Write,
}

/// Upload firmware to a device in bootloader mode
///
/// The progress callback receives (kind, current_bytes, total_bytes)
pub fn upload_firmware<F>(
    uf2: &Uf2File,
    conn: &mut PicobootConnection,
    mut progress: F,
) -> Result<()>
where
    F: FnMut(ProgressKind, u64, u64),
{
    // Request exclusive access
    conn.exclusive_access()?;

    // Exit XIP mode before flash operations
    conn.exit_xip()?;

    // Calculate what needs to be erased
    let sectors = uf2.sectors_to_erase(FLASH_SECTOR_SIZE);
    let total_erase_bytes: u64 = sectors.iter().map(|(_, size)| *size as u64).sum();

    // Erase required sectors
    let mut erased_bytes: u64 = 0;
    for (addr, size) in &sectors {
        conn.flash_erase(*addr, *size)?;
        erased_bytes += *size as u64;
        progress(ProgressKind::Erase, erased_bytes, total_erase_bytes);
    }

    // Calculate total write bytes
    let total_write_bytes: u64 = uf2.blocks.values().map(|v| v.len() as u64).sum();

    // Write data - blocks are already sorted by address in BTreeMap
    let mut written_bytes: u64 = 0;
    for (&addr, data) in &uf2.blocks {
        // PICOBOOT write requires page-aligned addresses
        // UF2 blocks are typically 256 bytes at 256-byte aligned addresses
        // Write in page-sized chunks if data is larger
        for (chunk_idx, chunk) in data.chunks(FLASH_PAGE_SIZE as usize).enumerate() {
            let chunk_addr = addr + (chunk_idx as u32 * FLASH_PAGE_SIZE);

            // Pad to page size if needed for final chunk
            let write_data = if chunk.len() < FLASH_PAGE_SIZE as usize {
                let mut padded = chunk.to_vec();
                padded.resize(FLASH_PAGE_SIZE as usize, 0xFF);
                padded
            } else {
                chunk.to_vec()
            };

            conn.flash_write(chunk_addr, &write_data)?;
        }

        written_bytes += data.len() as u64;
        progress(ProgressKind::Write, written_bytes, total_write_bytes);
    }

    Ok(())
}
