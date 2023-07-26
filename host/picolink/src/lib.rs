use anyhow::{anyhow, Result};
use serialport::SerialPort;
use std::collections::HashMap;
use std::{thread::sleep, time::Duration, time::Instant};

use num_derive::FromPrimitive;
use num_traits::FromPrimitive;

#[repr(u8)]
#[derive(FromPrimitive, Debug)]
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

    MaskSet = 9,
    MaskGet = 10,
    MaskCur = 11,

    CommitFlash = 12,
    CommitDone = 13,

    CommsStart = 80,
    CommsEnd = 81,
    CommsData = 82,

    Identify = 0xf8,
    Error = 0xfe,
    Debug = 0xff,
}

#[derive(Clone, Debug)]
pub enum ReqPacket {
    Ident,
    IdentSet(String),
    PointerSet(u32),
    PointerGet,
    MaskSet(u32),
    MaskGet,
    Write(Vec<u8>),
    Read,
    CommitFlash,
    CommsStart(u32),
    CommsEnd,
    CommsData(Vec<u8>),
    Identify,
}

impl ReqPacket {
    fn encode(self) -> Result<Vec<u8>> {
        let (kind, payload) = match self.clone() {
            ReqPacket::Ident => (PacketKind::IdentReq, vec![]),
            ReqPacket::IdentSet(name) => (PacketKind::IdentSet, name.as_bytes().to_vec()),
            ReqPacket::PointerSet(offset) => {
                (PacketKind::PointerSet, offset.to_le_bytes().to_vec())
            }
            ReqPacket::PointerGet => (PacketKind::PointerGet, vec![]),
            ReqPacket::MaskSet(mask) => (PacketKind::MaskSet, mask.to_le_bytes().to_vec()),
            ReqPacket::MaskGet => (PacketKind::MaskGet, vec![]),
            ReqPacket::Write(data) => (PacketKind::Write, data),
            ReqPacket::Read => (PacketKind::Read, vec![]),
            ReqPacket::CommitFlash => (PacketKind::CommitFlash, vec![]),
            ReqPacket::CommsStart(addr) => (PacketKind::CommsStart, addr.to_le_bytes().to_vec()),
            ReqPacket::CommsEnd => (PacketKind::CommsEnd, vec![]),
            ReqPacket::CommsData(data) => (PacketKind::CommsData, data),
            ReqPacket::Identify => (PacketKind::Identify, vec![]),
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
    Ident(String),
    PointerCur(u32),
    ReadData(Vec<u8>),
    CommitDone,
    CommsData(Vec<u8>),

    Error(String, u32, u32),
    Debug(String, u32, u32),
}

pub struct PicoLink {
    port: Box<dyn SerialPort>,
    debug: bool,
}

struct RawPacket {
    kind: PacketKind,
    size: usize,
    payload: [u8; 30],
}

impl PicoLink {
    pub fn open(port_path: &str, debug: bool) -> Result<PicoLink> {
        let mut port = serialport::new(port_path, 9600)
            .timeout(std::time::Duration::from_millis(1000))
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
            port: port,
            debug: debug,
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
            PacketKind::IdentResp => Ok(Some(RespPacket::Ident(
                String::from_utf8_lossy(&payload).to_string(),
            ))),
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
        self.send(ReqPacket::Ident)?;
        self.recv_until(|pkt| match pkt {
            RespPacket::Ident(x) => Some(x),
            _ => None,
        })
    }

    pub fn set_ident(&mut self, name: &str) -> Result<()> {
        self.send(ReqPacket::IdentSet(name.to_string()))?;
        let name_check = self.get_ident()?;
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

        self.send(ReqPacket::MaskSet(addr_mask))?;

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

pub fn enumerate_picos() -> Result<HashMap<String, PicoLink>> {
    let mut found = HashMap::new();
    for p in enumerate_ports()?.iter() {
        let link = PicoLink::open(p, false);
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
