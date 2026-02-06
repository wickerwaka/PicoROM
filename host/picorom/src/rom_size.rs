use clap::{builder::PossibleValue, ValueEnum};

#[derive(Clone, Debug, Copy)]
pub enum RomSize {
    MBit(usize),
    KBit(usize),
}

impl RomSize {
    /// Parse a ROM size from a byte count (as returned by device rom_size parameter)
    pub fn from_bytes(bytes: usize) -> Option<Self> {
        let bits = bytes * 8;
        if bits >= 1024 * 1024 && bits % (1024 * 1024) == 0 {
            Some(RomSize::MBit(bits / (1024 * 1024)))
        } else if bits >= 1024 && bits % 1024 == 0 {
            Some(RomSize::KBit(bits / 1024))
        } else {
            None
        }
    }

    /// Parse a ROM size from a hex string like "0x00010000"
    pub fn from_hex_bytes(s: &str) -> Option<Self> {
        let s = s.trim().strip_prefix("0x").unwrap_or(s.trim());
        let bytes = usize::from_str_radix(s, 16).ok()?;
        Self::from_bytes(bytes)
    }
}

impl RomSize {
    pub fn bytes(&self) -> usize {
        match *self {
            RomSize::MBit(x) => x * 128 * 1024,
            RomSize::KBit(x) => x * 128,
        }
    }

    pub fn mask(&self) -> u32 {
        (self.bytes() as u32) - 1
    }
}

impl ValueEnum for RomSize {
    fn value_variants<'a>() -> &'a [Self] {
        &[
            RomSize::MBit(2),
            RomSize::MBit(1),
            RomSize::KBit(512),
            RomSize::KBit(256),
            RomSize::KBit(128),
            RomSize::KBit(64),
            RomSize::KBit(32),
            RomSize::KBit(16),
            RomSize::KBit(8),
        ]
    }

    fn to_possible_value(&self) -> Option<clap::builder::PossibleValue> {
        match self {
            RomSize::MBit(x) => Some(PossibleValue::new(format!("{}MBit", x))),
            RomSize::KBit(x) => Some(PossibleValue::new(format!("{}KBit", x))),
        }
    }
}
