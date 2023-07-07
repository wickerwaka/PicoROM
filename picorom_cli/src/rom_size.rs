use clap::{builder::PossibleValue, ValueEnum};

#[derive(Clone, Debug, Copy)]
pub enum RomSize {
    MBit(usize),
    KBit(usize),
}

impl RomSize {
    pub fn bytes(&self) -> usize {
        match *self {
            RomSize::MBit(x) => x * 128 * 1024,
            RomSize::KBit(x) => x * 128,
        }
    }

    pub fn mask(&self) -> u32 {
        ( self.bytes() as u32 ) - 1
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
