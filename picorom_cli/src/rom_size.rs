pub enum RomSize {
    MBit(usize),
    KBit(usize),
    Bits(usize),
    Bytes(usize),
}

impl RomSize {
    pub fn bytes(&self) -> usize {
        match *self {
            RomSize::MBit(x) => x * 128 * 1024,
            RomSize::KBit(x) => x * 128,
            RomSize::Bits(x) => 1 << x,
            RomSize::Bytes(x) => x.next_power_of_two(),
        }
    }

    pub fn mask(&self) -> u32 {
        (self.bytes() - 1) as u32
    }
}
