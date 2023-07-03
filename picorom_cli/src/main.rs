use anyhow::{anyhow, Context, Result};
use clap::{Args, Parser, Subcommand, ValueEnum};
use serialport::{SerialPort, SerialPortBuilder, SerialPortType};
use std::{cell::RefCell, collections::HashMap, thread::sleep, time::Duration, time::Instant, path::{PathBuf, Path}, fs, iter::repeat};

use num_derive::FromPrimitive;
use num_traits::FromPrimitive;

#[repr(u8)]
#[derive(FromPrimitive)]
enum PacketKind {
    IdentReq = 0,
    IdentResp = 1,
    IdentSet = 2,
    
    PointerSet = 3,
    PointerGet = 4,
    PointerCur = 5,
    Write = 6,
    Read = 7,
    ReadData = 8,

    SizeSet = 9,
    SizeGet = 10,
    SizeCur = 11,

    CommitFlash = 12,
    
    Debug = 0xff,
}

enum ReqPacket {
    Ident,
    IdentSet(String),
    PointerSet(u32),
    PointerGet,
    Write(Vec<u8>),
    Read,
    SizeSet(u32),
    SizeGet,
    CommitFlash,
}

enum RespPacket {
    Ident(String),
    PointerCur(u32),
    ReadData(Vec<u8>),
    SizeCur(u32),

    Debug
}

struct PacketLink {
    port: RefCell<Box<dyn SerialPort>>,
}

impl PacketLink {
    fn open(port_path: &str) -> Result<PacketLink> {
        let mut port = serialport::new(port_path, 9600)
            .timeout(std::time::Duration::from_millis(1000))
            .open()?;

        let expected = "PicoROM Hello".as_bytes();
        let mut preamble = Vec::new();

        while preamble.len() < expected.len() && !preamble.ends_with(&expected) {
            let mut buf = [0u8];
            port.read_exact(&mut buf)?;
            preamble.push(buf[0]);
        }

        Ok(PacketLink {
            port: RefCell::new(port),
        })
    }

    fn send(&self, packet: ReqPacket) -> Result<()> {
        while let Some(_) = self.recv(Instant::now())? { }

        let (kind, mut payload) = match packet {
            ReqPacket::Ident => (PacketKind::IdentReq, vec![]),
            ReqPacket::IdentSet(name) => (PacketKind::IdentSet, name.as_bytes().to_vec()),
            ReqPacket::PointerSet(offset) => (PacketKind::PointerSet, offset.to_le_bytes().to_vec()),
            ReqPacket::PointerGet => (PacketKind::PointerGet, vec![]),
            ReqPacket::Write(data) => (PacketKind::Write, data),
            ReqPacket::Read => (PacketKind::Read, vec![]),
            ReqPacket::SizeSet(size) => (PacketKind::SizeSet, size.to_le_bytes().to_vec()),
            ReqPacket::SizeGet => (PacketKind::SizeGet, vec![]),
            ReqPacket::CommitFlash => (PacketKind::CommitFlash, vec![]),
        };

        let mut data = Vec::with_capacity(32);
        data.push(kind as u8);
        payload.truncate(30);
        data.push(payload.len() as u8);
        data.extend(payload);

        println!(">>> {} {} {:?}", data[0], data[1], &data[2..]);

        self.port.borrow_mut().write_all(&data)?;
        Ok(())
    }

    fn recv(&self, deadline: Instant) -> Result<Option<RespPacket>> {
        let mut port = self.port.borrow_mut();

        while port.bytes_to_read()? < 2 {
            if Instant::now() > deadline {
                return Ok(None);
            }
            sleep(Duration::from_micros(10));
        }

        let mut kind_and_size = [0u8, 0u8];
        port.read_exact(&mut kind_and_size)?;
        let size = kind_and_size[1] as usize;
        let kind = kind_and_size[0];
        let mut payload = vec![0u8; size];

        while port.bytes_to_read()? < size as u32 {
            if Instant::now() > deadline {
                return Ok(None);
            }
            sleep(Duration::from_micros(10));
        }

        port.read_exact(&mut payload)?;

        println!("<<< {} {} {:?}", kind, size, payload);

        match FromPrimitive::from_u8(kind) {
            Some(PacketKind::IdentResp) => Ok(Some(RespPacket::Ident(
                String::from_utf8_lossy(&payload).to_string(),
            ))),
            Some(PacketKind::Debug) => Ok(Some(RespPacket::Debug)),
            Some(PacketKind::PointerCur) => {
                let arr = payload.try_into().unwrap_or_default();
                Ok(Some(RespPacket::PointerCur(u32::from_le_bytes(arr))))
            },
            Some(PacketKind::ReadData) => Ok(Some(RespPacket::ReadData(payload))),
            Some(PacketKind::SizeCur) => {
                let arr = payload.try_into().unwrap_or_default();
                Ok(Some(RespPacket::SizeCur(u32::from_le_bytes(arr))))
            },
            Some(x) => Err(anyhow::format_err!("Unexpected packet kind: {}", kind)),
            None => Err(anyhow::format_err!("Unknown packet kind: {}", kind)),
        }
    }

    fn get_ident(&self) -> Result<String> {
        self.send(ReqPacket::Ident)?;
        let deadline = Instant::now() + Duration::from_millis(500);
        while let Some(pkt) = self.recv(deadline)? {
            if let RespPacket::Ident(ident) = pkt {
                return Ok(ident);
            }
        }
        Err(anyhow!("timeout"))
    }

    fn set_ident(&self, name: &str) -> Result<()> {
        self.send(ReqPacket::IdentSet(name.to_string()))?;
        let name_check = self.get_ident()?;
        if name != name_check {
            Err(anyhow!("Rename failed. Expected name '{}' but PicoROM returned '{}'", name, name_check))
        } else {
            Ok(())
        }
    }

    fn upload(&self, data: &[u8], size: RomSize) -> Result<()> {
        self.send(ReqPacket::SizeSet(size.bytes() as u32))?;
        self.send(ReqPacket::PointerSet(0))?;

        for chunk in data.chunks(30) {
            self.send(ReqPacket::Write(chunk.to_vec()))?;
        }

        self.send(ReqPacket::PointerGet)?;

        let deadline = Instant::now() + Duration::from_millis(500);
        loop {
            match self.recv(deadline)? {
                Some(RespPacket::PointerCur(cur)) => {
                    if cur != size.bytes() as u32 {
                        return Err(anyhow!("Upload did not complete."));
                    } else {
                        break;
                    }    
                },
                Some(x) => {},
                None => {
                    return Err(anyhow!("Timeout waiting for upload to complete."));
                }
            }
        }

        Ok(())
    }
}

/// Find all USB serial ports matching the PicoROM VID:PID
fn enumerate_ports() -> Result<Vec<String>> {
    let mut ports = Vec::new();
    let all_ports = serialport::available_ports()?;

    for p in all_ports.iter() {
        match &p.port_type {
            SerialPortType::UsbPort(info) => {
                if info.vid == 0x2e8a && info.pid == 0x000a {
                    ports.push(p.port_name.clone());
                }
            }
            _ => {}
        }
    }

    Ok(ports)
}

fn enumerate_picos() -> Result<HashMap<String, PacketLink>> {
    let mut found = HashMap::new();
    for p in enumerate_ports()?.iter() {
        let link = PacketLink::open(p);
        if let Ok(link) = link {
            if let Ok(ident) = link.get_ident() {
                found.insert(ident, link);
            }
        }
    }

    Ok(found)
}

fn find_pico(name: &str) -> Result<Option<PacketLink>> {
    let mut found = enumerate_picos()?;

    Ok(found.remove(name))
}


enum RomSize {
    MBit(usize),
    KBit(usize),
    Bits(usize),
    Bytes(usize)
}

impl RomSize {
    fn bytes(&self) -> usize {
        match *self {
            RomSize::MBit(x) => x * 128 * 1024,
            RomSize::KBit(x) => x * 128,
            RomSize::Bits(x) => 1 << x,
            RomSize::Bytes(x) => x.next_power_of_two()
        }
    }

    fn mask(&self) -> u32 {
        (self.bytes() - 1) as u32
    }
}

fn read_file(name: &Path, size: RomSize) -> Result<Vec<u8>> {
    let mut data = fs::read(name)?;
    if data.len() > size.bytes() {
        return Err(anyhow!("{:?} larger ({}) than rom size ({})", name, data.len(), size.bytes()));
    }

    let diff = size.bytes() - data.len();
    data.extend(repeat(0u8).take(diff));

    Ok(data)
}

#[derive(Debug, Parser)] // requires `derive` feature
#[command(name = "picorom")]
#[command(about = "PicoROM controller", long_about = None)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Debug, Subcommand)]
enum Commands {
    List,
    Rename {
        #[arg(value_name = "CURRENT_NAME")]
        original: String,
        #[arg(value_name = "NEW_NAME")]
        new: String,
    },
    Upload {
        #[arg(value_name = "NAME")]
        name: String,
        #[arg(value_name = "FILE")]
        source: PathBuf,
    }
}

fn main() -> Result<()> {
    let args = Cli::parse();

    match args.command {
        Commands::List => {
            let found = enumerate_picos()?;
            for k in found.keys() {
                println!("* {}", k);
            }
        }
        Commands::Rename { original, new } => {
            if let Some(pico) = find_pico(&original)? {
                pico.set_ident(&new)?;
            } else {
                println!( "PicoROM named '{}' not found.", original );
            }
        }
        Commands::Upload { name, source } => {
            if let Some(pico) = find_pico(&name)? {
                let data = read_file(source.as_path(), RomSize::MBit(2))?;
                pico.upload(&data, RomSize::MBit(2))?;
            } else {
                println!( "PicoROM named '{}' not found.", name );
            }            

        }

    }

    Ok(())
}
