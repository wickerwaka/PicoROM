# Raspberry Pi Pico W and Pico2 W FOTA Bootloader

This bootloader allows you to perform secure `Firmware Over The Air (FOTA)`
OTA updates with the Raspberry Pi Pico W and Pico2 W boards. It contains all
required linker scripts that will adapt your application to the new application
memory layout.

The memory layout is as follows:

```
+-------------------------------------------+  <-- __FLASH_START (0x10000000)
|              Bootloader (36k)             |
+-------------------------------------------+  <-- __FLASH_INFO_APP_HEADER
|             App Header (4 bytes)          |
+-------------------------------------------+  <-- __FLASH_INFO_DOWNLOAD_HEADER
|         Download Header (4 bytes)         |
+-------------------------------------------+  <-- __FLASH_INFO_IS_DOWNLOAD_SLOT_VALID
|      Is Download Slot Valid (4 bytes)     |
+-------------------------------------------+  <-- __FLASH_INFO_IS_FIRMWARE_SWAPPED
|       Is Firmware Swapped (4 bytes)       |
+-------------------------------------------+  <-- __FLASH_INFO_IS_AFTER_ROLLBACK
|        Is After Rollback (4 bytes)        |
+-------------------------------------------+  <-- __FLASH_INFO_SHOULD_ROLLBACK
|         Should Rollback (4 bytes)         |
+-------------------------------------------+
|            Padding (4072 bytes)           |
+-------------------------------------------+  <-- __FLASH_APP_START
|           Flash Application Slot          |
+-------------------------------------------+  <-- __FLASH_DOWNLOAD_SLOT_START
|            Flash Download Slot            |
+-------------------------------------------+
|   Filesystem Block (FILESYSTEM_SIZE)      |
|              (Optional)                   |
+-------------------------------------------+  <-- __FLASH_END (FLASH_SIZE)
```
## Basic usage

**Basic usage can be found
[here](https://github.com/JZimnol/pico_fota_example).**

## Features

`pico_fota_bootloader` supports the following features:

- **SHA256 calculation**- application binary FOTA image is appended with a
  SHA256 value

  - as a result, the `<app_name>_fota_image.bin` binary file will be appended
    with 256 bytes from which last 32 bytes will contain SHA256 of the image

  - after downloading a binary file, the user can use the
    `pfb_firmware_sha256_check` function to check if the calculated SHA256
    matches the expected one

  - this option can be disabled using `-DPFB_WITH_SHA256_HASHING=OFF` CMake
    option

  - see the [example](#your_projectmainc) for more information

- **image encryption** - application binary FOTA image is encrypted using AES
  ECB algorithm

  - encryption/decryption key should be set using `-DPFB_AES_KEY=<value>` CMake
    option

      - both the bootloader and the future FOTA images should be compiled using
        the same key value, otherwise the bootloader won't be able to properly
        decrypt the FOTA image

  - as a result, the `<app_name>_fota_image_encrypted.bin` file will be created

    - if `PFB_WITH_SHA256_HASHING` has been enabled, a SHA256 will also be
      encrypted with the firmware image

  - this option can be disabled using `-DPFB_WITH_IMAGE_ENCRYPTION=OFF` CMake
    option

- **rollback mechanism** - if the freshly downloaded firmware won't be committed
  before the very next reboot, the bootloader will perform the rollback (the
  firmware will be swapped back to the previous working version)

- **basic debug logging** - enabled by default, can be turned off using
  `-DPFB_WITH_BOOTLOADER_LOGS=OFF` CMake option

  - debug logs can be redirected from USB to UART using
    `-DPFB_REDIRECT_BOOTLOADER_LOGS_TO_UART=ON` CMake option

- **filesystem block** - block at the end of the flash, reserved for a file system.
  0 by default, can be set with
    `-DPFB_RESERVED_FILESYSTEM_SIZE_KB=<value in KB>` CMake flag.

## Prerequisites

- `pico-sdk` version `>= 2.2.0`

- `Python 3` with the following packages: `argparse`, `hashlib`, `os`,
  `Crypto.Cipher`

  - required for the SHA256 calculation and AES ECB image encryption

# Example

## File structure

Assume the following file structure:
```
your_project/
├── CMakeLists.txt
├── main.c
├── deps/
|   └── pico_fota_bootloader/
│       ├── CMakeLists.txt
│       ├── include/
│       │   └── pico_fota_bootloader/
│       │       └── core.h
│       ├── linker_common/
│       │   ├── app2040.ld
│       │   ├── app2350.ld
│       │   └── ...
│       └── src/
|           ├── bootloader_app.c
│           ├── pico_fota_bootloader.c
│           └── ...
└── pico_sdk_import.cmake
```

The following files should have the following contents:

### your_project/CMakeLists.txt

```cmake
cmake_minimum_required(VERSION 3.13)

# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)
...
pico_sdk_init()

add_subdirectory(deps/pico_fota_bootloader)

add_executable(your_app
               main.c)
target_link_libraries(your_app
                      pico_stdlib
                      pico_fota_bootloader_lib)
pfb_compile_with_bootloader(your_app)
pico_add_extra_outputs(your_app)
# rest of the file if needed...
```

### your_project/main.c
```c
#include <pico_fota_bootloader/core.h>
...
int main() {
    ...
    if (pfb_is_after_firmware_update()) {
        // handle new firmare info if needed
    }
    if (pfb_is_after_rollback()) {
        // handle performed rollback if needed
    }
    ...

    // commit the new firmware - otherwise after the next reboot the rollback
    // will be performed
    pfb_firmware_commit();
    ...

    // initialize download slot before writing into it
    pfb_initialize_download_slot();
    ...

    // acquire the data (e.g. from the web) and write it into the download slot
    // using chunks of N*256 bytes
    for (int i = 0; i < size; i++) {
        if (pfb_write_to_flash_aligned_256_bytes(src, offset_bytes, len_bytes)) {
            // handle error if needed
            while (1);
        }
    }
    ...

    size_t firmware_size = offset_bytes;
    if (pfb_firmware_sha256_check(firmware_size)) {
        // handle the SHA256 error/mismatch if needed
        while (1);
    }

    // once the binary file has been successfully downloaded, mark the download
    // slot as valid - the firmware will be swapped after a reboot
    pfb_mark_download_slot_as_valid();
    ...

    // when you're ready - reboot and perform the upgrade
    pfb_perform_update();

    /* code unreachable */
}
```

## Compiling and running

### Compiling

Create the build directory and build the project within it.

```shell
# these commands may vary depending on the OS
mkdir build/
cd build
cmake -DPFB_AES_KEY="<your_key_value>" .. && make -j
```

You should have output similar to:

```
build/
├── deps/
|   └── pico_fota_bootloader
│       ├── CMakeFiles
│       ├── cmake_install.cmake
│       ├── libpico_fota_bootloader_lib.a
│       ├── Makefile
│       ├── pico_fota_bootloader.bin
│       ├── pico_fota_bootloader.dis
│       ├── pico_fota_bootloader.elf
│       ├── pico_fota_bootloader.elf.map
│       ├── pico_fota_bootloader.hex
│       └── pico_fota_bootloader.uf2
└── your_app
    ├── CMakeFiles
    ├── cmake_install.cmake
    ├── Makefile
    ├── your_app.bin
    ├── your_app.dis
    ├── your_app.elf
    ├── your_app.elf.map
    ├── your_app_fota_image.bin
    ├── your_app_fota_image_encrypted.bin
    ├── your_app.hex
    └── your_app.uf2
```

### Running

Set Pico W to the BOOTSEL state (by powering it up with the `BOOTSEL` button
pressed) and copy the `pico_fota_bootloader.uf2` file into it. Right now the
Pico W is flashed with the bootloader but does not have proper application in
the application FLASH memory slot. Then, set Pico W to the BOOTSEL state again
(if it is not already in that state) and copy the `your_app.uf2` file. The board
should reboot and start `your_app` application.

**NOTE:** you can also flash the board with `PicoProbe` using files
`pico_fota_bootloader.elf` and `your_app.elf`.

**NOTE:** you can also look at the serial output logs to monitor the
application state.

### Performing the firmware update (OTA)

To perform a firmware update (over the air), a
`your_app_fota_image_encrypted.bin` file (or `your_app_fota_image.bin` file in
case of setting the `-DPFB_WITH_IMAGE_ENCRYPTION=OFF` CMake option) should be
sent to or downloaded by the Pico W. Note that while rebuilding the
application, the linker scripts' contents should not be changed or should be
changed carefully to maintain the memory layout backward compatibility.
