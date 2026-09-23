# openphix-tool

Cross-platform command line updater for Autophix DM100/DM300 based VAG
scan tools (Autophix 9610, Ancel VD700, OBD2 SCANZ FST32, Biltema 15-1375
and other rebrands). It replaces the Windows-only vendor `Update.exe` and
speaks the same USB protocol, documented in `docs/update-protocol.md` at the
repository root. Written in C99, it builds to a single small binary with
libusb-1.0 as its only dependency and runs on Linux, macOS and Windows.

Commands:

- `list`: connected scan tools.
- `info`: hardware revision (DM100 / DM300 / DM100HC), bootloader version,
  external flash size, whether the feedback area holds data.
- `package <dir> [--notes]`: describe an unzipped update package offline.
- `update <dir> [-y] [--mcu-only] [--data-only] [--skip-erase] [--force-erase]`:
  flash a package with the vendor's sequence, including the Erase.bin step
  for DM300 units with an old bootloader.
- `feedback [-o Feedback.bin]`: download the 128 KiB Feedback support/debug trace.
- `dtc-review [-o file]`: dump stored DTC sessions (vendor "Review & Print").
- `read-flash -o file [-a addr] [-l len] [--decrypt]`: dump external flash,
  optionally removing the data image obfuscation as it is read.
- `decrypt <in> <out> [-a addr]` and `encrypt <in> <out> [-a addr]`: remove
  or apply the `ExtFlashDat.bin` cipher; `-a` gives the position of the
  input inside the image when you are working on a slice.

Global options go before the command: `-v` / `-vv` (progress details,
packet traces), `--strict` (turn the mismatches the vendor tool ignores into
errors), `--vid N --pid N`, and `--simulate DM100|DM300|DM100HC` to run any
command against the built-in simulated device.

## Build and install

Requirements: a C99 compiler, libusb-1.0 with headers, and CMake 3.13 or
newer if you use the CMake build.

```
# Linux (Debian/Ubuntu): apt install build-essential cmake libusb-1.0-0-dev
# macOS:                 brew install cmake libusb
# Windows (MSYS2/MinGW): pacman -S mingw-w64-ucrt-x86_64-{gcc,cmake,libusb}
# Windows (MSVC):        vcpkg install libusb, then pass -DCMAKE_TOOLCHAIN_FILE=...
```

With the plain Makefile:

```
make
make test
sudo make install
```

Or with CMake, which does the same thing:

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
sudo cmake --install build
```

Installing puts the binary in `/usr/local/bin`. On Linux it also drops
`99-openphix.rules` into `/etc/udev/rules.d` and reloads udev, so the tool
can reach the device without root. Replug the tool afterwards for the new
permissions to apply. `sudo make uninstall` removes both again.

Both builds honour the usual overrides. For the Makefile these are `PREFIX`,
`BINDIR`, `UDEVDIR`, `DESTDIR` and `INSTALL_UDEV`; for CMake they are
`CMAKE_INSTALL_PREFIX`, `OPENPHIX_UDEV_DIR` and `DESTDIR`. A user install
needs no root, but skip the udev rule because that part does:

```
make install PREFIX=$HOME/.local INSTALL_UDEV=0
```

Without the rule the tool still works, it just has to be run with `sudo`.

On Windows keep the vendor's WinUSB driver (run `driver\install driver.bat`
from any update package once); libusb talks to WinUSB devices directly. On
macOS nothing else is needed and the udev step is skipped automatically.

## Use

```
openphix-tool list
openphix-tool info
unzip 9610_EN_DE_ES_V1.61.zip
openphix-tool package 9610_EN_DE_ES_V1.61 --notes
openphix-tool update 9610_EN_DE_ES_V1.61
openphix-tool feedback -o Feedback.bin
openphix-tool dtc-review
openphix-tool read-flash -o flash.bin
openphix-tool decrypt bin/ExtFlashDat.bin ExtFlashDat.plain
openphix-tool --simulate DM300 update 9610_EN_DE_ES_V1.61 -y
```

The package argument is the unzipped directory (the one containing `bin/`,
or its parent when the zip extracted into a single sub directory).

## Image obfuscation

`ExtFlashDat.bin`, the 20 MB data image that holds the fonts, translations,
fault code text and vehicle databases, is passed through a byte-wise
rotate-and-XOR cipher that repeats every 4864 bytes. The key table and the
rotation rule have been recovered and are built into this tool
(`src/crypto.c`), so `decrypt` turns a package image or a flash dump into
the real data image, a plain file container. It works for every image seen
so far, from both vendors and all language packs. `tools/hixtool` at the
repository root takes it from there and unpacks the files inside.

`McuCode.bin`, the microcontroller firmware, uses an unrelated 16 byte block
cipher in ECB mode whose key sits in the device bootloader. This tool cannot
decrypt it, and the USB protocol has no way to read the microcontroller's
internal flash. See `docs/update-protocol.md`.

## Update rules, copied from the vendor tool

- USB product id 0x5265 means DM100 and gets `bin/DM100/McuCode.bin`.
  Every other product id (DM300 0x5750, DM100HC 0x4605) takes the DM300
  path and gets `bin/DM300/McuCode.bin`. `bin/McuCode.bin` is the fallback.
- On the DM300 path the bootloader version (command 0x07) is compared with
  the string "1.26". Versions not newer than that receive `Erase.bin` as an
  MCU image first, followed by a 5 second pause. The packages in this
  repository contain no Erase.bin, so on such a unit the tool stops unless
  you pass `--skip-erase`.
- MCU image first, then `ExtFlashDat.bin`, then the finish command, whose
  reply the vendor tool ignores.
- There is no resume. Do not unplug the tool during the update.

## Layout

```
src/protocol.[ch]       packet formats, checksums, constants
src/transport.h         byte-pipe interface shared by USB and simulator
src/usb_transport.c     libusb-1.0 implementation
src/fake.[ch]           simulated device (validates checksums and block order)
src/device.[ch]         queries, image upload, feedback and DTC readout, update flow
src/package.[ch]        locating files in an update package
src/crypto.[ch]         data image cipher (recovered key table and rotation)
src/platform.h          sleep / tty / clock for POSIX and Windows
src/main.c              command line interface
tests/                  unit tests (no hardware or libusb needed)
```

Exit codes: 0 success, 1 nothing to do (no device, no data, aborted),
2 usage or file error, 3 USB error, 4 protocol error.

## Status

The read-only commands (`info`, `read-flash`, `feedback`, `dtc-review`)
have been verified on a real DM100 (0483:5265, firmware 1.56): a full
32 MiB flash dump takes about 4 minutes. The upload side (`update`) is
verified against the vendor binaries by static analysis and against the
simulator (also under AddressSanitizer), but has not yet been run against
a physical tool. Real devices answer with the header bytes swapped
(`AA 55`), which the tool accepts.

The `decrypt` key is verified against seven independent images (five
Autophix language packs, the OBD2 SCANZ package and a real DM100 flash
dump) and round-trips byte for byte.
