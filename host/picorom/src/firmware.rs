use anyhow::Result;
use flate2;
use rawzip;
use std::fs;

#[derive(Debug)]
pub struct EmbeddedFirmware {
    pub config: String,
    pub version: String,
    wayfinder: rawzip::ZipArchiveEntryWayfinder,
}

impl EmbeddedFirmware {
    pub fn extract(&self) -> Result<Vec<u8>> {
        let exe = open_exe()?;
        let mut buffer = vec![0u8; rawzip::RECOMMENDED_BUFFER_SIZE];
        let archive = rawzip::ZipArchive::from_file(exe, &mut buffer)?;

        let entry = archive.get_entry(self.wayfinder)?;
        let mut data = Vec::new();
        let decompressor = flate2::read::DeflateDecoder::new(entry.reader());
        let mut reader = entry.verifying_reader(decompressor);
        std::io::copy(&mut reader, &mut data)?;

        let mut crc = flate2::Crc::new();
        crc.update(&data);
        let digest = crc.sum().to_le_bytes();

        let pad_len = 256 - digest.len();

        let padding = vec![0u8; pad_len];
        data.extend_from_slice(&padding);
        data.extend_from_slice(digest.as_slice());

        Ok(data)
    }
}

fn open_exe() -> Result<fs::File> {
    Ok(fs::OpenOptions::new()
        .read(true)
        .open(std::env::current_exe()?)?)
}

pub fn enumerate_firmware() -> Result<Vec<EmbeddedFirmware>> {
    let exe = open_exe()?;
    let mut buffer = vec![0u8; rawzip::RECOMMENDED_BUFFER_SIZE];
    let archive = rawzip::ZipArchive::from_file(exe, &mut buffer)?;
    let mut entries = archive.entries(&mut buffer);

    let mut res = Vec::new();
    while let Some(entry) = entries.next_entry()? {
        let name = entry.file_path().try_normalize()?.as_ref().to_string();
        if let Some(inner) = name
            .strip_prefix("PicoROM-")
            .and_then(|x| x.strip_suffix(".bin"))
        {
            let v: Vec<&str> = inner.splitn(2, "-").collect();
            if v.len() == 2 {
                let config = v[0].to_string();
                let version = v[1].replace("_", ".");
                res.push(EmbeddedFirmware {
                    config,
                    version,
                    wayfinder: entry.wayfinder(),
                });
            }
        }
    }

    Ok(res)
}

