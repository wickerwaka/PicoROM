use std::env;
use std::fs;
use std::path::PathBuf;

fn main() {
    println!("cargo::rustc-check-cfg=cfg(has_embedded_firmware)");
    println!("cargo:rerun-if-env-changed=EMBED_FIRMWARE");

    if let Ok(firmware_path) = env::var("EMBED_FIRMWARE") {
        let firmware_path = PathBuf::from(&firmware_path);

        if !firmware_path.exists() {
            panic!(
                "EMBED_FIRMWARE points to non-existent file: {}",
                firmware_path.display()
            );
        }

        let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
        let dest_path = out_dir.join("firmware-bundle.zip");

        fs::copy(&firmware_path, &dest_path).expect("Failed to copy firmware bundle");

        println!("cargo:rustc-cfg=has_embedded_firmware");
        println!("cargo:rerun-if-changed={}", firmware_path.display());
    }
}
