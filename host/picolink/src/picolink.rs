//! PicoLink - Communication with PicoROM devices in application mode

use anyhow::{anyhow, Result};
use nusb::transfer::{Bulk, In, Out};
use nusb::{Endpoint, Interface, MaybeFuture};
use std::collections::HashMap;
use std::time::{Duration, Instant};

use num_derive::FromPrimitive;
use num_traits::FromPrimitive;

use crate::new_in_buffer;

// Protocol constants
const MAX_PKT_PAYLOAD: usize = 36;
const MAX_DATA_PAYLOAD: usize = MAX_PKT_PAYLOAD - 4; // 32 bytes (offset takes 4)

// USB constants for PicoROM application
const VID: u16 = 0x2E8A;
const PID: u16 = 0x000A;
const INTERFACE_NUM: u8 = 0;
const EP_OUT: u8 = 0x01;
const EP_IN: u8 = 0x81;

#[repr(u8)]
#[derive(FromPrimitive, Debug)]
enum PacketKind {
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
    Write(u32, Vec<u8>), // (offset, data)
    Read(u32, u8),       // (offset, size)
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
            ReqPacket::Write(offset, data) => {
                let mut payload = offset.to_le_bytes().to_vec();
                payload.extend(data);
                (PacketKind::Write, payload)
            }
            ReqPacket::Read(offset, size) => {
                let mut payload = offset.to_le_bytes().to_vec();
                payload.push(size);
                (PacketKind::Read, payload)
            }
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

        if payload.len() > MAX_PKT_PAYLOAD {
            return Err(anyhow!("{:?} request packet payload too large", self));
        }

        let mut data = Vec::with_capacity(MAX_PKT_PAYLOAD + 2);
        data.push(kind as u8);
        data.push(payload.len() as u8);
        data.extend(payload);
        Ok(data)
    }
}

#[derive(Clone, Debug)]
pub enum RespPacket {
    ReadData(u32, Vec<u8>), // (offset, data)
    CommitDone,
    CommsData(Vec<u8>),
    Parameter(String),
    ParameterError,

    Error(String, u32, u32),
    Debug(String, u32, u32),
}

pub struct PicoLink {
    #[allow(dead_code)]
    interface: Interface,
    ep_out: Endpoint<Bulk, Out>,
    ep_in: Endpoint<Bulk, In>,
    debug: bool,
    pub device_id: String,
}

struct RawPacket {
    kind: PacketKind,
    size: usize,
    payload: [u8; MAX_PKT_PAYLOAD],
}

impl PicoLink {
    pub fn open(device_id: &str, debug: bool) -> Result<PicoLink> {
        let devices = nusb::list_devices().wait()?;

        let device_info = devices
            .filter(|d| d.vendor_id() == VID && d.product_id() == PID)
            .find(|d| {
                // Match by device_id portion of serial number or bus:addr
                if let Some(serial) = d.serial_number() {
                    let (id, _) = parse_serial_string(serial);
                    if id == device_id {
                        return true;
                    }
                }
                let bus_addr = format!("{}:{}", d.bus_id(), d.device_address());
                bus_addr == device_id
            })
            .ok_or_else(|| anyhow!("Device '{}' not found", device_id))?;

        let device = device_info.open().wait()?;
        let interface = device.claim_interface(INTERFACE_NUM).wait()?;

        let ep_out = interface.endpoint::<Bulk, Out>(EP_OUT)?;
        let mut ep_in = interface.endpoint::<Bulk, In>(EP_IN)?;

        // Pre-submit a buffer for receiving
        ep_in.submit(new_in_buffer(64));

        Ok(PicoLink {
            interface,
            ep_out,
            ep_in,
            debug,
            device_id: device_id.to_string(),
        })
    }

    pub fn send(&mut self, packet: ReqPacket) -> Result<()> {
        self.recv_flush()?;

        let data = packet.encode()?;

        self.ep_out.submit(data.into());
        let completion = self
            .ep_out
            .wait_next_complete(Duration::from_secs(1))
            .ok_or_else(|| anyhow!("USB send timeout"))?;

        completion
            .status
            .map_err(|e| anyhow!("USB send error: {:?}", e))?;

        Ok(())
    }

    /// Receive a raw packet
    /// Err on port error or packet formatting
    /// None if data not received before deadline
    fn recv_raw(&mut self, deadline: Instant) -> Result<Option<RawPacket>> {
        loop {
            let timeout = deadline.saturating_duration_since(Instant::now());
            if timeout.is_zero() {
                return Ok(None);
            }

            // Check if we have pending data
            if let Some(completion) = self.ep_in.wait_next_complete(timeout) {
                // Re-submit a buffer for the next receive
                self.ep_in.submit(new_in_buffer(64));

                completion
                    .status
                    .map_err(|e| anyhow!("USB receive error: {:?}", e))?;

                let data = &completion.buffer[..completion.actual_len];
                if data.len() < 2 {
                    return Err(anyhow!("Packet too small: {} bytes", data.len()));
                }

                let size = data[1] as usize;
                if size > MAX_PKT_PAYLOAD {
                    return Err(anyhow!("Packet payload too large: {}", size));
                }

                if data.len() < 2 + size {
                    return Err(anyhow!(
                        "Packet truncated: expected {} bytes, got {}",
                        2 + size,
                        data.len()
                    ));
                }

                let kind: Option<PacketKind> = FromPrimitive::from_u8(data[0]);
                if let Some(kind) = kind {
                    let mut payload = [0u8; MAX_PKT_PAYLOAD];
                    payload[..size].copy_from_slice(&data[2..2 + size]);
                    return Ok(Some(RawPacket {
                        kind,
                        size,
                        payload,
                    }));
                } else {
                    return Err(anyhow!("Unknown packet kind: 0x{:x}", data[0]));
                }
            } else {
                return Ok(None);
            }
        }
    }

    pub fn recv(&mut self, deadline: Instant) -> Result<Option<RespPacket>> {
        let pkt = self.recv_raw(deadline)?;

        if pkt.is_none() {
            return Ok(None);
        }

        let pkt = pkt.unwrap();
        let payload = &pkt.payload[0..pkt.size];

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
            PacketKind::ReadData => {
                if payload.len() < 4 {
                    return Err(anyhow!("ReadData payload too small: {}", payload.len()));
                }
                let offset = u32::from_le_bytes(payload[0..4].try_into()?);
                let data = payload[4..].to_vec();
                Ok(Some(RespPacket::ReadData(offset, data)))
            }
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
            std::thread::sleep(Duration::from_millis(1));
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
        let mut offset: u32 = 0;
        for chunk in data.chunks(MAX_DATA_PAYLOAD) {
            f(chunk.len());
            self.send(ReqPacket::Write(offset, chunk.to_vec()))?;
            offset += chunk.len() as u32;
        }

        self.set_parameter("addr_mask", &format!("0x{:x}", addr_mask))?;

        Ok(())
    }

    pub fn upload_to<F>(&mut self, addr: u32, data: &[u8], f: F) -> Result<()>
    where
        F: Fn(usize),
    {
        let mut offset = addr;
        for chunk in data.chunks(MAX_DATA_PAYLOAD) {
            f(chunk.len());
            self.send(ReqPacket::Write(offset, chunk.to_vec()))?;
            offset += chunk.len() as u32;
        }

        Ok(())
    }

    pub fn download<F>(&mut self, size: usize, f: F) -> Result<Vec<u8>>
    where
        F: Fn(usize),
    {
        let mut data = Vec::with_capacity(size);
        let mut offset: u32 = 0;

        while data.len() < size {
            let remaining = size - data.len();
            let chunk_size = remaining.min(MAX_DATA_PAYLOAD) as u8;

            self.send(ReqPacket::Read(offset, chunk_size))?;
            let (resp_offset, chunk) = self.recv_until(|pkt| match pkt {
                RespPacket::ReadData(off, bytes) => Some((off, bytes)),
                _ => None,
            })?;

            if resp_offset != offset {
                return Err(anyhow!(
                    "Read offset mismatch: expected {}, got {}",
                    offset,
                    resp_offset
                ));
            }
            if chunk.is_empty() {
                break;
            }

            f(chunk.len());
            data.extend_from_slice(&chunk);
            offset += chunk.len() as u32;
        }

        data.truncate(size);
        Ok(data)
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
            for chunk in outgoing.chunks(MAX_PKT_PAYLOAD) {
                while let Some(pkt) = self.recv(Instant::now())? {
                    match pkt {
                        RespPacket::CommsData(data) => {
                            incoming.extend_from_slice(&data);
                        }
                        _ => {}
                    }
                }
                let pkt = ReqPacket::CommsData(chunk.to_vec()).encode()?;
                self.ep_out.submit(pkt.into());
                let completion = self
                    .ep_out
                    .wait_next_complete(Duration::from_secs(1))
                    .ok_or_else(|| anyhow!("USB send timeout"))?;
                completion
                    .status
                    .map_err(|e| anyhow!("USB send error: {:?}", e))?;
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

/// Find all USB devices matching the PicoROM VID:PID
fn enumerate_devices() -> Result<Vec<nusb::DeviceInfo>> {
    let devices: Vec<_> = nusb::list_devices()
        .wait()?
        .filter(|d| d.vendor_id() == VID && d.product_id() == PID)
        .collect();
    Ok(devices)
}

/// Parse the USB serial string which may contain a device name.
/// Format: "device_id:device_name" or just "device_id" for old firmware.
/// Returns (device_id, Option<device_name>)
fn parse_serial_string(serial: &str) -> (String, Option<String>) {
    if let Some((device_id, name)) = serial.split_once(':') {
        (device_id.to_string(), Some(name.to_string()))
    } else {
        // Backward compatibility: old firmware without name
        (serial.to_string(), None)
    }
}

pub fn enumerate_picos() -> Result<HashMap<String, PicoLink>> {
    let mut found = HashMap::new();
    for device_info in enumerate_devices()?.iter() {
        if let Some(serial) = device_info.serial_number() {
            let (device_id, name) = parse_serial_string(serial);
            if let Some(name) = name {
                if let Ok(link) = PicoLink::open(&device_id, false) {
                    found.insert(name, link);
                }
            }
        }
    }
    Ok(found)
}

pub fn find_pico(name: &str) -> Result<PicoLink> {
    for device_info in enumerate_devices()?.iter() {
        if let Some(serial) = device_info.serial_number() {
            let (device_id, device_name) = parse_serial_string(serial);
            if device_name.as_deref() == Some(name) {
                return PicoLink::open(&device_id, false);
            }
        }
    }
    Err(anyhow!("PicoROM '{}' not found.", name))
}

/// Get the USB port location (bus_id, port_chain) for a device by name
/// Returns (bus_id, port_chain) which uniquely identifies the physical USB port
pub fn get_device_location(name: &str) -> Result<(String, Vec<u8>)> {
    for device_info in enumerate_devices()?.iter() {
        if let Some(serial) = device_info.serial_number() {
            let (_device_id, device_name) = parse_serial_string(serial);
            if device_name.as_deref() == Some(name) {
                return Ok((
                    device_info.bus_id().to_string(),
                    device_info.port_chain().to_vec(),
                ));
            }
        }
    }
    Err(anyhow!("PicoROM '{}' not found.", name))
}
