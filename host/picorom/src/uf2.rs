//! Firmware file format parsing for RP2040
//!
//! Supports two formats:
//! - UF2 (USB Flashing Format): 512-byte blocks with headers and metadata
//! - BIN (raw binary): Direct flash image loaded at 0x10000000

use anyhow::{anyhow, Result};
use std::collections::BTreeMap;
use std::fs;
use std::path::Path;

/// Base address for RP2040 flash
const FLASH_BASE: u32 = 0x10000000;

/// UF2 block size is always 512 bytes
const UF2_BLOCK_SIZE: usize = 512;

/// UF2 magic numbers
const UF2_MAGIC_START0: u32 = 0x0A324655; // "UF2\n"
const UF2_MAGIC_START1: u32 = 0x9E5D5157;
const UF2_MAGIC_END: u32 = 0x0AB16F30;

/// RP2040 family ID
const RP2040_FAMILY_ID: u32 = 0xE48BFF56;

/// UF2 flags
const UF2_FLAG_FAMILY_ID_PRESENT: u32 = 0x00002000;

/// Parsed UF2 file containing flash data
pub struct Uf2File {
    /// Map of target address to payload data
    pub blocks: BTreeMap<u32, Vec<u8>>,
    /// Total number of blocks in the file
    pub block_count: u32,
    /// Family ID if present
    #[allow(dead_code)]
    pub family_id: Option<u32>,
}

impl Uf2File {
    /// Parse a UF2 file from disk
    pub fn parse(path: &Path) -> Result<Uf2File> {
        let data = fs::read(path)?;
        Self::parse_bytes(&data)
    }

    /// Parse a raw binary file from disk
    /// The binary is loaded at FLASH_BASE (0x10000000)
    pub fn parse_bin(path: &Path) -> Result<Uf2File> {
        let data = fs::read(path)?;
        Self::parse_bin_bytes(&data)
    }

    /// Parse raw binary data into flash blocks
    /// Splits the data into 256-byte chunks (flash page size) for efficient writing
    pub fn parse_bin_bytes(data: &[u8]) -> Result<Uf2File> {
        if data.is_empty() {
            return Err(anyhow!("Binary file is empty"));
        }

        // Split into 256-byte blocks (flash page size) for efficient writing
        const BLOCK_SIZE: usize = 256;
        let mut blocks = BTreeMap::new();
        let mut addr = FLASH_BASE;

        for chunk in data.chunks(BLOCK_SIZE) {
            blocks.insert(addr, chunk.to_vec());
            addr += chunk.len() as u32;
        }

        let block_count = blocks.len() as u32;

        Ok(Uf2File {
            blocks,
            block_count,
            family_id: Some(RP2040_FAMILY_ID),
        })
    }

    /// Parse UF2 data from bytes
    pub fn parse_bytes(data: &[u8]) -> Result<Uf2File> {
        if data.len() % UF2_BLOCK_SIZE != 0 {
            return Err(anyhow!(
                "UF2 file size ({}) is not a multiple of block size ({})",
                data.len(),
                UF2_BLOCK_SIZE
            ));
        }

        let num_blocks = data.len() / UF2_BLOCK_SIZE;
        if num_blocks == 0 {
            return Err(anyhow!("UF2 file is empty"));
        }

        let mut blocks = BTreeMap::new();
        let mut family_id = None;
        let mut expected_total = None;

        for (i, block_data) in data.chunks(UF2_BLOCK_SIZE).enumerate() {
            let block = parse_block(block_data, i)?;

            // Validate magic numbers
            if block.magic_start0 != UF2_MAGIC_START0
                || block.magic_start1 != UF2_MAGIC_START1
                || block.magic_end != UF2_MAGIC_END
            {
                return Err(anyhow!("Block {} has invalid magic numbers", i));
            }

            // Check family ID
            if block.flags & UF2_FLAG_FAMILY_ID_PRESENT != 0 {
                if block.file_size != RP2040_FAMILY_ID {
                    return Err(anyhow!(
                        "Block {} has unsupported family ID 0x{:08X} (expected RP2040: 0x{:08X})",
                        i,
                        block.file_size,
                        RP2040_FAMILY_ID
                    ));
                }
                family_id = Some(block.file_size);
            }

            // Validate block numbering
            if block.block_no != i as u32 {
                return Err(anyhow!(
                    "Block {} has incorrect block number {}",
                    i,
                    block.block_no
                ));
            }

            match expected_total {
                None => expected_total = Some(block.num_blocks),
                Some(expected) if block.num_blocks != expected => {
                    return Err(anyhow!(
                        "Block {} claims {} total blocks, but previous blocks claimed {}",
                        i,
                        block.num_blocks,
                        expected
                    ));
                }
                _ => {}
            }

            // Validate payload size
            if block.payload_size > 476 {
                return Err(anyhow!(
                    "Block {} has invalid payload size {}",
                    i,
                    block.payload_size
                ));
            }

            // Store the block's payload
            let payload = block_data[32..32 + block.payload_size as usize].to_vec();
            blocks.insert(block.target_addr, payload);
        }

        let block_count = expected_total.unwrap_or(num_blocks as u32);

        Ok(Uf2File {
            blocks,
            block_count,
            family_id,
        })
    }

    /// Calculate the flash sectors that need to be erased
    /// Returns a list of (start_addr, size) tuples, sorted by address
    pub fn sectors_to_erase(&self, sector_size: u32) -> Vec<(u32, u32)> {
        if self.blocks.is_empty() {
            return vec![];
        }

        // Get all addresses that need to be written
        let mut sector_starts: Vec<u32> = self
            .blocks
            .iter()
            .flat_map(|(&addr, data)| {
                // Calculate all sectors touched by this block
                let start_sector = (addr / sector_size) * sector_size;
                let end_addr = addr + data.len() as u32;
                let end_sector = ((end_addr + sector_size - 1) / sector_size) * sector_size;

                (start_sector..end_sector)
                    .step_by(sector_size as usize)
                    .collect::<Vec<_>>()
            })
            .collect();

        sector_starts.sort();
        sector_starts.dedup();

        // Merge contiguous sectors
        let mut result = vec![];
        if sector_starts.is_empty() {
            return result;
        }

        let mut current_start = sector_starts[0];
        let mut current_size = sector_size;

        for &addr in sector_starts.iter().skip(1) {
            if addr == current_start + current_size {
                // Contiguous - extend
                current_size += sector_size;
            } else {
                // Gap - push current region and start new one
                result.push((current_start, current_size));
                current_start = addr;
                current_size = sector_size;
            }
        }
        result.push((current_start, current_size));

        result
    }

    /// Get total payload bytes
    pub fn total_bytes(&self) -> usize {
        self.blocks.values().map(|v| v.len()).sum()
    }

    /// Get the range of addresses covered
    pub fn address_range(&self) -> Option<(u32, u32)> {
        if self.blocks.is_empty() {
            return None;
        }

        let min_addr = *self.blocks.keys().next().unwrap();
        let (max_addr, max_data) = self.blocks.iter().last().unwrap();
        let end_addr = max_addr + max_data.len() as u32;

        Some((min_addr, end_addr))
    }
}

/// Parsed UF2 block header
struct Uf2Block {
    magic_start0: u32,
    magic_start1: u32,
    flags: u32,
    target_addr: u32,
    payload_size: u32,
    block_no: u32,
    num_blocks: u32,
    file_size: u32, // or family ID if flag set
    magic_end: u32,
}

fn parse_block(data: &[u8], _block_idx: usize) -> Result<Uf2Block> {
    if data.len() < UF2_BLOCK_SIZE {
        return Err(anyhow!("Block data too short"));
    }

    Ok(Uf2Block {
        magic_start0: u32::from_le_bytes(data[0..4].try_into().unwrap()),
        magic_start1: u32::from_le_bytes(data[4..8].try_into().unwrap()),
        flags: u32::from_le_bytes(data[8..12].try_into().unwrap()),
        target_addr: u32::from_le_bytes(data[12..16].try_into().unwrap()),
        payload_size: u32::from_le_bytes(data[16..20].try_into().unwrap()),
        block_no: u32::from_le_bytes(data[20..24].try_into().unwrap()),
        num_blocks: u32::from_le_bytes(data[24..28].try_into().unwrap()),
        file_size: u32::from_le_bytes(data[28..32].try_into().unwrap()),
        magic_end: u32::from_le_bytes(data[508..512].try_into().unwrap()),
    })
}
