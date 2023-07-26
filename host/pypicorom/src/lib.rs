use std::thread::sleep;
use std::time::{Duration, Instant};

use picolink::*;
use pyo3::create_exception;
use pyo3::exceptions::PyException;
use pyo3::prelude::*;

create_exception!(
    pypicorom,
    CommsStateError,
    PyException,
    "Invalid comms setup"
);

create_exception!(
    pypicorom,
    CommsTimeoutError,
    PyException,
    "Communication timeout"
);

/// A PicoROM connection.
#[pyclass]
struct PicoROM {
    link: PicoLink,
    read_buffer: Vec<u8>,
    comms_active: bool,
}

impl PicoROM {
    fn comms_inactive(&self) -> PyResult<()> {
        if self.comms_active {
            return Err(CommsStateError::new_err("Comms active."));
        }
        Ok(())
    }

    fn comms_active(&self) -> PyResult<()> {
        if !self.comms_active {
            return Err(CommsStateError::new_err("Comms not active."));
        }
        Ok(())
    }
}

#[pymethods]
impl PicoROM {
    /// Get the identifying name
    fn get_name(&mut self) -> PyResult<String> {
        self.comms_inactive()?;

        Ok(self.link.get_ident()?)
    }

    /// Set the identifying name
    fn set_name(&mut self, name: String) -> PyResult<()> {
        self.comms_inactive()?;

        Ok(self.link.set_ident(&name)?)
    }

    /// Commit the current ROM data to flash memory
    fn commit(&mut self) -> PyResult<()> {
        self.comms_inactive()?;

        Ok(self.link.commit_rom()?)
    }

    /// Ask PicoROM to identify itself
    fn identify(&mut self) -> PyResult<()> {
        self.comms_inactive()?;

        Ok(self.link.identify()?)
    }

    /// Upload ROM data
    #[pyo3(signature = (data, mask=0x3ffff), text_signature = "(data, mask=0x3ffff, /)")]
    fn upload(&mut self, data: &[u8], mask: u32) -> PyResult<()> {
        self.comms_inactive()?;

        self.link.upload(data, mask, |_| {})?;

        Ok(())
    }

    /// Update to a specific address
    fn upload_to(&mut self, addr: u32, data: &[u8]) -> PyResult<()> {
        self.comms_inactive()?;

        self.link.upload_to(addr, data, |_| {})?;

        Ok(())
    }

    /// Start two-way communications
    fn start_comms(&mut self, addr: u32) -> PyResult<()> {
        self.comms_inactive()?;

        self.link.send(ReqPacket::CommsStart(addr))?;
        self.comms_active = true;
        self.read_buffer.clear();
        Ok(())
    }

    /// End two-way communications
    fn end_comms(&mut self) -> PyResult<()> {
        self.comms_active()?;

        self.link.send(ReqPacket::CommsEnd)?;
        self.comms_active = false;
        self.read_buffer.clear();
        Ok(())
    }

    /// Read from the communication channel
    #[pyo3(signature = (size=-1), text_signature = "(size=-1, /)")]
    fn read(&mut self, size: i32) -> PyResult<Option<Vec<u8>>> {
        self.comms_active()?;

        let new_data = self.link.poll_comms(None)?;
        self.read_buffer.extend_from_slice(&new_data);

        if self.read_buffer.len() == 0 {
            return Ok(None);
        }

        let end = if size == -1 {
            self.read_buffer.len()
        } else {
            self.read_buffer.len().min(size as usize)
        };

        Ok(Some(self.read_buffer.drain(0..end).collect()))
    }

    /// Read an exact amount with an optional timeout
    fn read_exact(
        &mut self,
        size: usize,
        timeout: Option<f32>,
        py: Python<'_>,
    ) -> PyResult<Vec<u8>> {
        self.comms_active()?;

        let end = timeout.map(|x| Instant::now() + Duration::from_secs_f32(x));

        loop {
            let new_data = self.link.poll_comms(None)?;
            self.read_buffer.extend_from_slice(&new_data);

            if self.read_buffer.len() < size {
                if let Some(end) = end {
                    if Instant::now() >= end {
                        return Err(CommsTimeoutError::new_err("read_all timeout"));
                    }
                }
                py.check_signals()?;
                sleep(Duration::from_micros(10));
            } else {
                return Ok(self.read_buffer.drain(0..size).collect());
            }
        }
    }

    /// Write to the communication channel
    fn write(&mut self, data: Vec<u8>) -> PyResult<usize> {
        self.comms_active()?;

        let len = data.len();
        let new_data = self.link.poll_comms(Some(data))?;
        self.read_buffer.extend_from_slice(&new_data);
        Ok(len)
    }
}

/// Enumerate all available PicoROMs
#[pyfunction]
fn enumerate() -> PyResult<Vec<String>> {
    let picos = enumerate_picos()?;
    Ok(Vec::from_iter(picos.keys().cloned()))
}

/// Open a connection to the named PicoROM.
#[pyfunction]
fn open(name: &str) -> PyResult<PicoROM> {
    let pico = find_pico(name)?;
    Ok(PicoROM {
        link: pico,
        read_buffer: Vec::new(),
        comms_active: false,
    })
}

/// Python module for communicating with PicoROMs.
#[pymodule]
fn pypicorom(py: Python, m: &PyModule) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(enumerate, m)?)?;
    m.add_function(wrap_pyfunction!(open, m)?)?;
    m.add_class::<PicoROM>()?;
    m.add("CommsStateError", py.get_type::<CommsStateError>())?;
    m.add("CommsTimeoutError", py.get_type::<CommsTimeoutError>())?;
    Ok(())
}
