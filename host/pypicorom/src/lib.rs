use pyo3::{prelude::*};
use picolink::*;

#[pyclass]
struct PicoROM {
    link: PicoLink,
    read_buffer: Vec<u8>
}

#[pymethods]
impl PicoROM {
    fn read(&mut self, size: Option<isize>) -> PyResult<Option<Vec<u8>>> {
        let new_data = self.link.poll_comms(None)?;
        self.read_buffer.extend_from_slice(&new_data);
        

        if self.read_buffer.len() == 0 {
            return Ok(None);
        }

        let end = match size {
            None | Some(-1) => {
                self.read_buffer.len()
            },
            Some(x) => {
                self.read_buffer.len().min(x as usize)
            }
        };

        Ok(Some(self.read_buffer.drain(0..end).collect()))
    }

    fn write(&mut self, data: Vec<u8>) -> PyResult<usize> {
        let len = data.len();
        let new_data = self.link.poll_comms(Some(data))?;
        self.read_buffer.extend_from_slice(&new_data);
        Ok(len)
    }
}

/// Formats the sum of two numbers as string.
#[pyfunction]
fn enumerate() -> PyResult<Vec<String>> {
    let picos = enumerate_picos()?;
    Ok(Vec::from_iter(picos.keys().cloned()))
}


#[pyfunction]
fn open(name: &str, addr: u32) -> PyResult<PicoROM> {
    let mut pico = find_pico(name)?;
    pico.send(ReqPacket::CommsStart(addr))?;
    Ok(PicoROM { link: pico, read_buffer: Vec::new() } )
}

/// A Python module implemented in Rust.
#[pymodule]
fn pypicorom(_py: Python, m: &PyModule) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(enumerate, m)?)?;
    m.add_function(wrap_pyfunction!(open, m)?)?;
    m.add_class::<PicoROM>()?;
    Ok(())
}