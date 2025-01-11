use anyhow::{anyhow, Result};
use serialport::SerialPort;
use std::collections::HashMap;
use std::fs::File;
use std::io::{BufRead, BufReader, BufWriter, Read, Write};
use std::path::PathBuf;
use std::{thread::sleep, time::Duration, time::Instant};

use dirs::cache_dir;
use num_derive::FromPrimitive;
use num_traits::FromPrimitive;

#[repr(u8)]
#[derive(FromPrimitive, Debug)]
enum PacketKind {
    PointerSet = 3,
    PointerGet = 4,
    PointerCur = 5,
    Write = 6,
    Read = 7,
    ReadData = 8,

    CommitFlash = 12,
    CommitDone = 13,

    ParameterSet = 20,
    ParameterGet = 21,
    Parameter = 22,
    ParameterError = 23,
    ParameterQuery = 24,

    CommsStart = 80,
    CommsEnd = 81,
    CommsData = 82,

    Identify = 0xf8,
    Bootsel = 0xf9,
    Error = 0xfe,
    Debug = 0xff,
}

#[derive(Clone, Debug)]
pub enum ResetLevel {
    High,
    Low,
    Z,
}

#[derive(Clone, Debug)]
pub enum ReqPacket {
    PointerSet(u32),
    PointerGet,
    Write(Vec<u8>),
    Read,
    CommitFlash,
    CommsStart(u32),
    CommsEnd,
    CommsData(Vec<u8>),
    Identify,
    Bootsel,
    ParameterQuery(Option<String>),
    ParameterGet(String),
    ParameterSet(String, String),
}

fn zstring(s: String) -> Vec<u8> {
    let mut v = s.as_bytes().to_vec();
    v.push(0u8);
    v
}

impl ReqPacket {
    fn encode(self) -> Result<Vec<u8>> {
        let (kind, payload) = match self.clone() {
            ReqPacket::PointerSet(offset) => {
                (PacketKind::PointerSet, offset.to_le_bytes().to_vec())
            }
            ReqPacket::PointerGet => (PacketKind::PointerGet, vec![]),
            ReqPacket::Write(data) => (PacketKind::Write, data),
            ReqPacket::Read => (PacketKind::Read, vec![]),
            ReqPacket::CommitFlash => (PacketKind::CommitFlash, vec![]),
            ReqPacket::CommsStart(addr) => (PacketKind::CommsStart, addr.to_le_bytes().to_vec()),
            ReqPacket::CommsEnd => (PacketKind::CommsEnd, vec![]),
            ReqPacket::CommsData(data) => (PacketKind::CommsData, data),
            ReqPacket::Identify => (PacketKind::Identify, vec![]),
            ReqPacket::Bootsel => (PacketKind::Bootsel, vec![]),
            ReqPacket::ParameterQuery(None) => (PacketKind::ParameterQuery, vec![]),
            ReqPacket::ParameterQuery(Some(x)) => (PacketKind::ParameterQuery, zstring(x)),
            ReqPacket::ParameterGet(param) => (PacketKind::ParameterGet, zstring(param)),
            ReqPacket::ParameterSet(param, value) => (
                PacketKind::ParameterSet,
                zstring(format!("{},{}", param, value)),
            ),
        };

        if payload.len() > 30 {
            return Err(anyhow!("{:?} request packet payload too large", self));
        }

        let mut data = Vec::with_capacity(32);
        data.push(kind as u8);
        data.push(payload.len() as u8);
        data.extend(payload);
        Ok(data)
    }
}

#[derive(Clone, Debug)]
pub enum RespPacket {
    PointerCur(u32),
    ReadData(Vec<u8>),
    CommitDone,
    CommsData(Vec<u8>),
    Parameter(String),
    ParameterError,

    Error(String, u32, u32),
    Debug(String, u32, u32),
}

pub struct PicoLink {
    port: Box<dyn SerialPort>,
    debug: bool,
    pub path: String,
}

struct RawPacket {
    kind: PacketKind,
    size: usize,
    payload: [u8; 30],
}

impl PicoLink {
    pub fn open(port_path: &str, debug: bool) -> Result<PicoLink> {
        let mut port = serialport::new(port_path, 9600)
            .timeout(std::time::Duration::from_millis(500))
            .open()?;

        let expected = "PicoROM Hello".as_bytes();
        let mut preamble = Vec::new();

        port.write_data_terminal_ready(true)?;

        while preamble.len() < expected.len() && !preamble.ends_with(&expected) {
            let mut buf = [0u8];
            port.read_exact(&mut buf)?;
            preamble.push(buf[0]);
        }

        Ok(PicoLink {
            port,
            debug,
            path: port_path.to_string(),
        })
    }

    pub fn send(&mut self, packet: ReqPacket) -> Result<()> {
        self.recv_flush()?;

        let data = packet.encode()?;

        //println!(">>> {} {} {:?}", data[0], data[1], &data[2..]);

        self.port.write_all(&data)?;
        Ok(())
    }

    /// Receive a raw packet
    /// Err on port error or packet formatting
    /// None if data not received before deadline
    fn recv_raw(&mut self, deadline: Instant) -> Result<Option<RawPacket>> {
        let port = &mut self.port;

        while port.bytes_to_read()? < 2 {
            if Instant::now() > deadline {
                return Ok(None);
            }
            sleep(Duration::from_micros(10));
        }

        let mut data = [0u8; 32];
        port.read_exact(&mut data[0..2])?;
        let size = data[1] as usize;

        if size > 30 {
            return Err(anyhow!("Packet payload too large: {}", size));
        }

        while port.bytes_to_read()? < size as u32 {
            sleep(Duration::from_micros(10));
        }

        port.read_exact(&mut data[2..2 + size])?;

        let kind: Option<PacketKind> = FromPrimitive::from_u8(data[0]);
        if let Some(kind) = kind {
            Ok(Some(RawPacket {
                kind,
                size,
                payload: data[2..].try_into().unwrap(),
            }))
        } else {
            Err(anyhow!("Unknown packet kind: 0x{:x}", data[0]))
        }
    }

    pub fn recv(&mut self, deadline: Instant) -> Result<Option<RespPacket>> {
        let pkt = self.recv_raw(deadline)?;

        if pkt.is_none() {
            return Ok(None);
        }

        let pkt = pkt.unwrap();
        let payload = &pkt.payload[0..pkt.size];

        //println!("<<< {:?} {} {:?}", pkt.kind, pkt.size, payload);

        match pkt.kind {
            PacketKind::Debug => {
                if payload.len() >= 8 {
                    let v0 = u32::from_le_bytes(payload[0..4].try_into()?);
                    let v1 = u32::from_le_bytes(payload[4..8].try_into()?);
                    let msg = String::from_utf8_lossy(&payload[8..]);
                    Ok(Some(RespPacket::Debug(msg.to_string(), v0, v1)))
                } else {
                    Err(anyhow!("Debug payload is too small: {}", payload.len()))
                }
            }
            PacketKind::Error => {
                if payload.len() >= 8 {
                    let v0 = u32::from_le_bytes(payload[0..4].try_into()?);
                    let v1 = u32::from_le_bytes(payload[4..8].try_into()?);
                    let msg = String::from_utf8_lossy(&payload[8..]);
                    Ok(Some(RespPacket::Error(msg.to_string(), v0, v1)))
                } else {
                    Err(anyhow!("Error payload is too small: {}", payload.len()))
                }
            }
            PacketKind::PointerCur => {
                let arr = payload.try_into().unwrap_or_default();
                Ok(Some(RespPacket::PointerCur(u32::from_le_bytes(arr))))
            }
            PacketKind::ReadData => Ok(Some(RespPacket::ReadData(payload.to_vec()))),
            PacketKind::CommitDone => Ok(Some(RespPacket::CommitDone)),
            PacketKind::CommsData => Ok(Some(RespPacket::CommsData(payload.to_vec()))),
            PacketKind::ParameterError => Ok(Some(RespPacket::ParameterError)),
            PacketKind::Parameter => Ok(Some(RespPacket::Parameter(
                String::from_utf8_lossy(&payload).to_string(),
            ))),

            x => Err(anyhow::format_err!("Unexpected packet kind: {:?}", x)),
        }
    }

    fn recv_flush(&mut self) -> Result<()> {
        let deadline = Instant::now();

        while let Some(pkt) = self.recv(deadline)? {
            match pkt {
                RespPacket::Debug(msg, v0, v1) => {
                    if self.debug {
                        eprintln!("DEBUG: '{}' [0x{:x}, 0x{:x}]", msg, v0, v1);
                    }
                }
                RespPacket::Error(msg, v0, v1) => {
                    if self.debug {
                        eprintln!("ERROR: '{}' [0x{:x}, 0x{:x}]", msg, v0, v1);
                    }
                }
                _ => {}
            }
        }

        Ok(())
    }

    pub fn recv_forever(&mut self) -> Result<()> {
        loop {
            self.recv_flush()?;
            sleep(Duration::from_millis(1));
        }
    }

    pub fn recv_until_with_timeout<T, F>(&mut self, f: F, timeout: Duration) -> Result<T>
    where
        F: Fn(RespPacket) -> Option<T>,
    {
        let deadline = Instant::now() + timeout;

        while let Some(pkt) = self.recv(deadline)? {
            match pkt {
                RespPacket::Debug(msg, v0, v1) => {
                    if self.debug {
                        eprintln!("DEBUG: '{}' [0x{:x}, 0x{:x}]", msg, v0, v1);
                    }
                }
                RespPacket::Error(msg, v0, v1) => {
                    if self.debug {
                        eprintln!("ERROR: '{}' [0x{:x}, 0x{:x}]", msg, v0, v1);
                    }
                }
                x => {
                    let res = f(x);
                    if res.is_some() {
                        return Ok(res.unwrap());
                    }
                }
            }
        }

        Err(anyhow!("timeout"))
    }

    pub fn recv_until<T, F>(&mut self, f: F) -> Result<T>
    where
        F: Fn(RespPacket) -> Option<T>,
    {
        self.recv_until_with_timeout(f, Duration::from_millis(100))
    }

    pub fn get_ident(&mut self) -> Result<String> {
        self.get_parameter("name")
    }

    pub fn set_ident(&mut self, name: &str) -> Result<()> {
        let name_check = self.set_parameter("name", name)?;
        if name != name_check {
            Err(anyhow!(
                "Rename failed. Expected name '{}' but PicoROM returned '{}'",
                name,
                name_check
            ))
        } else {
            Ok(())
        }
    }

    pub fn get_parameter(&mut self, name: &str) -> Result<String> {
        self.send(ReqPacket::ParameterGet(name.to_string()))?;
        self.recv_until(|pkt| match pkt {
            RespPacket::Parameter(x) => Some(Ok(x)),
            RespPacket::ParameterError => Some(Err(anyhow!("Could not get parameter '{}'", name))),
            _ => None,
        })?
    }

    pub fn get_parameters(&mut self) -> Result<Vec<String>> {
        let mut prev = None;

        let mut parameters = Vec::new();

        loop {
            self.send(ReqPacket::ParameterQuery(prev))?;
            let parameter = self.recv_until(|pkt| match pkt {
                RespPacket::Parameter(x) => Some(Ok(x)),
                RespPacket::ParameterError => Some(Err(anyhow!("Could not get parameters"))),
                _ => None,
            })?;
            let parameter = parameter?;
            if parameter.len() > 0 {
                prev = Some(parameter.clone());
                parameters.push(parameter);
            } else {
                return Ok(parameters);
            }
        }
    }

    pub fn set_parameter(&mut self, name: &str, value: &str) -> Result<String> {
        self.send(ReqPacket::ParameterSet(name.to_string(), value.to_string()))?;
        self.recv_until(|pkt| match pkt {
            RespPacket::Parameter(x) => Some(Ok(x)),
            RespPacket::ParameterError => Some(Err(anyhow!("Could not set parameter '{}'", name))),
            _ => None,
        })?
    }

    pub fn upload<F>(&mut self, data: &[u8], addr_mask: u32, f: F) -> Result<()>
    where
        F: Fn(usize),
    {
        self.send(ReqPacket::PointerSet(0))?;

        for chunk in data.chunks(30) {
            f(chunk.len());
            self.send(ReqPacket::Write(chunk.to_vec()))?;
        }

        self.send(ReqPacket::PointerGet)?;

        let cur = self.recv_until(|x| match x {
            RespPacket::PointerCur(x) => Some(x),
            _ => None,
        })?;

        if cur != data.len() as u32 {
            return Err(anyhow!("Upload did not complete."));
        }

        self.set_parameter("addr_mask", &format!("0x{:x}", addr_mask))?;

        Ok(())
    }

    pub fn upload_to<F>(&mut self, addr: u32, data: &[u8], f: F) -> Result<()>
    where
        F: Fn(usize),
    {
        self.send(ReqPacket::PointerSet(addr))?;

        for chunk in data.chunks(30) {
            f(chunk.len());
            self.send(ReqPacket::Write(chunk.to_vec()))?;
        }

        self.send(ReqPacket::PointerGet)?;

        let cur = self.recv_until(|x| match x {
            RespPacket::PointerCur(x) => Some(x),
            _ => None,
        })?;

        if (cur - addr) != data.len() as u32 {
            return Err(anyhow!("Upload did not complete."));
        }

        Ok(())
    }

    pub fn commit_rom(&mut self) -> Result<()> {
        self.send(ReqPacket::CommitFlash)?;

        self.recv_until_with_timeout(
            |x| match x {
                RespPacket::CommitDone => Some(()),
                _ => None,
            },
            Duration::from_secs(5),
        )
    }

    pub fn identify(&mut self) -> Result<()> {
        self.send(ReqPacket::Identify)?;
        Ok(())
    }

    pub fn usb_boot(&mut self) -> Result<()> {
        self.send(ReqPacket::Bootsel)?;
        Ok(())
    }

    pub fn reset(&mut self, level: ResetLevel) -> Result<()> {
        let rst = match level {
            ResetLevel::Low => "low",
            ResetLevel::High => "high",
            ResetLevel::Z => "z",
        };
        self.set_parameter("reset", rst)?;
        Ok(())
    }

    pub fn poll_comms(&mut self, outgoing: Option<Vec<u8>>) -> Result<Vec<u8>> {
        let mut incoming = Vec::new();
        if let Some(outgoing) = outgoing {
            for chunk in outgoing.chunks(30) {
                while let Some(pkt) = self.recv(Instant::now())? {
                    match pkt {
                        RespPacket::CommsData(data) => {
                            incoming.extend_from_slice(&data);
                        }
                        _ => {}
                    }
                }
                let pkt = ReqPacket::CommsData(chunk.to_vec()).encode()?;
                self.port.write_all(&pkt)?;
            }
        }
        while let Some(pkt) = self.recv(Instant::now())? {
            match pkt {
                RespPacket::CommsData(data) => {
                    incoming.extend_from_slice(&data);
                }
                _ => {}
            }
        }

        Ok(incoming)
    }
}

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

fn get_cache_path() -> Option<PathBuf> {
    cache_dir().map(|x| x.join("picorom_enum"))
}

fn write_cache_file(entries: HashMap<String, String>) -> Result<()> {
    if let Some(cache_path) = get_cache_path() {
        let fs = File::create(cache_path)?;
        let mut writer = BufWriter::new(fs);
        for (ident, path) in entries.iter() {
            writeln!(writer, "{},{}", path, ident)?;
        }
    }

    Ok(())
}

fn read_cache_file() -> Result<HashMap<String, String>> {
    let mut entries = HashMap::new();

    if let Some(cache_path) = get_cache_path() {
        let fs = File::open(cache_path)?;
        let reader = BufReader::new(fs);
        for line in reader.lines() {
            let line = line?;
            if let Some((path, ident)) = line.split_once(',') {
                entries.insert(ident.to_string(), path.to_string());
            }
        }
    }

    Ok(entries)
}

pub fn enumerate_picos() -> Result<HashMap<String, PicoLink>> {
    let mut cache_data = HashMap::new();
    let mut found = HashMap::new();
    for p in enumerate_ports()?.iter() {
        let link = PicoLink::open(p, false);
        if let Ok(mut link) = link {
            if let Ok(ident) = link.get_parameter("name") {
                cache_data.insert(ident.clone(), p.to_string());
                found.insert(ident, link);
            }
        }
    }

    write_cache_file(cache_data).unwrap(); // don't care if it fails

    Ok(found)
}

pub fn find_pico(name: &str) -> Result<PicoLink> {
    // Check cache first
    let cached_paths = read_cache_file().unwrap_or_default();
    if let Some(path) = cached_paths.get(name) {
        if let Ok(mut link) = PicoLink::open(path, false) {
            if let Ok(ident) = link.get_parameter("name") {
                if ident == name {
                    println!("Found in cache");
                    return Ok(link);
                }
            }
        }
    }

    // If it wasn't found in the cache then do a full enumeration
    let mut found = enumerate_picos()?;

    if let Some(pico) = found.remove(name) {
        Ok(pico)
    } else {
        Err(anyhow!("PicoROM '{}' not found.", name))
    }
}
