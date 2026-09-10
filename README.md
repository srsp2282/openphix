# openphix

An open firmware (and, eventually, open hardware) project for the family of
handheld VAG (VW / Audi / Skoda / Seat) OBD2 scan tools that is sold under many
different brand names but is, in reality, one product from one Chinese ODM.

The goal is a free, auditable, hackable replacement firmware that runs on the
existing hardware, followed by a fully open reader that does not depend on the
original manufacturer at all.

## Status

Research phase, but no longer paper-only. What works today:

- The vendor's USB update protocol is decompiled and documented, and
  `openphix-tool` reimplements it for Linux, macOS and Windows.
- Its read-only commands are verified against a real DM100, including a full
  32 MiB dump of the external flash.
- The data image cipher is broken. `openphix-tool decrypt` turns any
  `ExtFlashDat.bin` or flash dump into the real data image.

What does not exist yet: any replacement firmware, and the key to the MCU
image, which needs a hardware dump of the microcontroller. See
[Roadmap](#roadmap) and [How to help](#how-to-help).

## The device family

Four brands were studied. Their manuals are the same document with a different
logo, the menus and screenshots are identical down to the sample VIN and the
sample fault code, and the update packages share the same layout, drivers and
update tool.

| Brand / model | Region | Sources in `vendor-files/` | Notes |
|---|---|---|---|
| Autophix 9610 | Global | Manual V2.2 (EN/FR/DE/ES/IT), five V1.61 update packages | The ODM's own branding. Update tool is named "ES910 Update Tool" and carries Autophix copyright strings. |
| Ancel VD700 | Global | Manual (EN) | Sold by OBDSPACE Technology, Shenzhen. The PDF's internal title is "7610 English manual", the ODM's earlier model number. |
| OBD2 SCANZ FST32 | Australia | Update/feedback guide, V1.60 update package | The user manual PDF in the folder is an empty file (0 bytes) and needs to be re-downloaded. The update tool still contains "(C) Autophix" and a stray Ancel support address. |
| Biltema 15-1375 | Nordics | Manual (SV/NO/FI/DA) | Nordic hardware-store rebrand. Same menus, same screens, Nordic UI languages. |

Evidence that these are one platform:

- Identical user interface: same eight-icon main menu, same module and
  function numbering, same wording of every prompt.
- Identical hardware description in every manual: 2.8" 320x240 colour LCD,
  8 to 18 V supply, 0 to 60 C operating range, same ten-button layout.
- Identical update package layout (`bin/DM100/McuCode.bin`,
  `bin/DM300/McuCode.bin`, `bin/ExtFlashDat.bin`, `driver/`, `Update.exe`).
- Byte-identical Windows driver files (`.inf`, `.cat`, WDF co-installer)
  between the Autophix and OBD2 SCANZ packages.
- `Update.exe` in every package is the same MFC application, "ES910 Update
  Tool" version 1.2, with only the branding strings and support e-mail
  changed.

## Hardware (what the manuals and update packages reveal)

Nobody has opened one up for this project yet, so the following comes from the
documentation and from static inspection of the update packages.

### Enclosure and user interface

- 2.8 inch colour LCD, 320 x 240 pixels, backlit.
- Eight keys: OK, ESC, four arrow keys, and two shortcut keys labelled I/M
  (emissions readiness quick check) and READ DTC (quick fault code read).
- Captive OBD2 cable with a 16-pin J1962 plug.
- USB port for updates and feedback upload (the tool is powered from the car
  when plugged into the DLC).
- Two selectable "skins" (Sky Gray, Gem Blue) and an optional key beeper.

### Electrical

- Operating voltage 8 to 18 V, supplied from pin 16 of the DLC.
- Operating temperature 0 to 60 C, storage -20 to 70 C.

### Electronics

The Windows drivers and update packages identify three hardware revisions,
selected by USB identity:

| Revision | USB VID:PID | Driver date | Firmware image |
|---|---|---|---|
| DM100 | 0483:5265 | 2017-01-05 | `bin/DM100/McuCode.bin` |
| DM300 | 0483:5750 | 2016-11-11 | `bin/DM300/McuCode.bin` |
| DM100HC | 2E88:4605 | 2021-10-18 | not shipped separately, presumably shares DM100 code |

Vendor ID 0x0483 belongs to STMicroelectronics, and 0x5750 is the product ID
ST uses in its own USB examples, which strongly suggests an STM32
microcontroller. The MCU image is around 280 KB, so an STM32 with 512 KB or
1 MB of flash is likely. This still needs to be confirmed by opening a unit.

The device enumerates as a vendor-specific WinUSB device (interface GUID
`F70242C7-FB25-443B-9E7E-A4260F373982`), not as a serial port. The update
tool talks to it with plain bulk transfers through `WinUsb_*` calls.

A large external flash holds the data image (`ExtFlashDat.bin`, 18 to 23 MB
depending on the language pack), so expect a 32 MB SPI NOR flash next to the
MCU. This image carries the fonts, translations, fault code text, vehicle
system databases and the special function scripts. The MCU image only changes
size slightly between language packs, so the language data is not in it.

### Firmware images

Both images ship with an entropy of about 7.95 bits per byte and no readable
strings, but they are protected in completely different ways.

**`ExtFlashDat.bin` is solved.** It is the data image XORed with a keystream
that repeats every 4864 bytes, built from one 256 byte table repeated 19
times with copy `b` rotated left by `b` bytes. The key was recovered from
unused regions of the image whose plaintext is all zero, and it decrypts
every image we have: five Autophix language packs, the OBD2 SCANZ package
and the flash dump of a real DM100. Decryption drops the entropy to 5.87 and
brings out the German, Spanish and English resource text. `openphix-tool
decrypt` does it. The strings inside are still packed record by record, so
the container format is the next piece of work.

**`McuCode.bin` is not solved.** It is a genuine 16 byte block cipher in ECB
mode: identical ciphertext blocks repeat only where the plaintext repeats,
two builds of the same image differ in every byte, and no repeating
keystream of any period up to 30000 fits. Every `McuCode.bin` seen so far
(DM100 and DM300, Autophix and OBD2 SCANZ) ends in the same repeated padding
block, so they share one key, but that key lives in the device bootloader in
the microcontroller's internal flash, which the USB protocol cannot read.
Recovering it means reading the microcontroller over SWD, which
`docs/mcu-key-extraction.md` describes step by step.

The update tool (`Update.exe`) has been decompiled. It is a debug build of
an MFC application called `DM100UpdateCustom`, it performs no brand, serial
or language check, and its USB protocol is fully documented in
[docs/update-protocol.md](docs/update-protocol.md). An open, cross-platform
replacement lives in [tools/openphix-tool](tools/openphix-tool).

Versions seen:

| Package | Software | Library | Date |
|---|---|---|---|
| Ancel VD700 (screenshot in manual) | 1.16.000 | 1.00.000 | 2019 |
| OBD2 SCANZ FST32 | 1.60.000 | 1.36.000 | 2026-01 |
| Autophix 9610 | 1.61.000 | 1.36.000 | 2026-07 |

The "library" is the vehicle protocol / function database and the "software"
is the application; both are updated together.

## Vehicle coverage and protocols

From the manuals:

- Brands: VW, Audi, Skoda, Seat, Bentley, Lamborghini and Bugatti, model
  year 2000 and newer, plus generic OBD2/EOBD on any car.
- VAG diagnostic transports and protocols: KWP1281, KWP2000, TP1.6 (TP16),
  TP2.0 (TP20) and UDS.
- Generic OBD2 protocols: SAE J1850 VPW, SAE J1850 PWM, ISO 9141, ISO 14230
  (KWP2000) and ISO 15765 (CAN).

The Autophix V1.61 changelog confirms the firmware implements these on the
device, not through a PC: it lists fixes to data offsets in the TP16 stack,
handling of UDS negative responses 0x7F 0x21 (busy) and 0x7F 0x23, new UDS
basic settings for DSG fluid service, and DPF regeneration over KWP2000.

## Feature set (functional specification for the open firmware)

This is the complete feature list of the stock firmware, taken from the
manuals. Anything that claims parity with the original device should cover
the same ground.

### Main menu

| Icon | Function |
|---|---|
| OBDII | Generic OBD2/EOBD diagnostics |
| For VW | VAG-specific diagnostics (below) |
| Oil reset | Service interval reset shortcut |
| EPB reset | Electronic parking brake, pad replacement |
| BAT Check | Read battery voltage |
| BMS reset | Battery registration after battery replacement |
| ETCS reset | Electronic throttle control adaptation |
| Tool Setup | Device settings |

### For VW

- Vehicle Scan
  - System Scan: probe every control module and show the fault count per
    module (for example `0017-Dash Board 4`, `0019-Gateway 5`,
    `0061-Battery Regulation 1`).
  - Manual Select: pick a module from the full supported list.
- System Selection
  - Common System: a short list of the most used modules (0001 Engine,
    0002 Transmission, 0003 Brakes, 0008 Air Conditioning, 0009 Central
    Electrics, 0015 Airbag, 0016 Steering Column Electronics, and so on).
  - All Systems: every known VAG address.
- Crafter (LT3) 2006-2017: separate vehicle entry for the LT3 platform.
- Special Functions: Service Reset (flexible and fixed intervals, oil
  quality, mileage and time channels), Throttle Learning, EPB Replace Brake
  Pads, Diesel Engine Special Functions (DPF regeneration, DEF reset),
  Steering Angle Learning, Tire Pressure Reset (TPMS), Injector Adaptation.

Per control module, the function menu is:

| Nr | Function | What it does |
|---|---|---|
| 01 | Version Information | Protocol type, VW/Audi part number, VIN, system description, workshop code, coding |
| 02 | Read Fault Codes | List DTCs with text |
| 03 | Erase Fault Codes | Clear DTCs, then prompt to cycle ignition and re-read |
| 04 | Read Datastream | Measuring blocks by channel number (0 to 255), live values |
| 05 | Basic Setting | Basic settings by channel number |
| 07 | Adaptation | Adaptation by channel number, or from a named list of channels (for example tank characteristic, service reminder, consumption display, language) |
| 08 | Access Authorization | Security login with a five digit code |

Battery registration walks through gateway or battery regulation modules
(0019, 0016, 008C), shows the old capacity and manufacturer, then lets the
user pick a new capacity (68 to 115 Ah) and manufacturer code (Moll, VARTA,
JCI/JCB, Exide, JFF/Boading, Banner, EPN, and more), and writes them.

EPB service walks through Retraction Brake Pump, pad replacement and Release
Brake Pump with confirmation prompts between each step.

### OBDII

Read Codes (stored and pending, generic and manufacturer specific), Erase
Codes, I/M Readiness (since DTCs cleared, this drive cycle, with per-monitor
status), Data Stream (all PIDs or a selection), Evap System Test, Vehicle
Information (VIN, CID, CVN).

### Tool Setup

Language (ten UI languages per firmware build, chosen by the brand: the
Biltema build's list starts with English, Swedish, Finnish, Norwegian,
Danish, German and French; the Autophix build's list starts with English,
German, Spanish, French, Russian, Portuguese and Finnish), Beeper, Instructions at startup, Unit of
Measure (metric or imperial), Skin Style, Feedback, Device Information
(software version, library version, serial number).

### Feedback and update

- Feedback: the device records all bus traffic of the next diagnostic session
  into internal storage. Back on the PC, the update tool downloads it as
  `Feedback.bin` (128 KB in the manual's screenshot) for the user to e-mail
  to the vendor. For this project, that recorder is a ready-made bus logger
  and a good source of real traces.
- Update: Windows 7/8/10 only. `install driver.bat` registers the three
  WinUSB `.inf` files with `pnputil`, then `Update.exe` writes `McuCode.bin`
  and `ExtFlashDat.bin` from the `bin/` folder to the device over USB.
  `tools/openphix-tool` does the same from Linux, macOS or Windows.
- Biltema does not offer updates at all. Its manual has no update chapter,
  its product page has no download, and Biltema has stated that it did not
  buy the manufacturer's update service. Since the hardware is identical,
  the Autophix packages should flash a Biltema unit, at the cost of the
  Nordic UI languages. This has not been tried yet.

## Repository layout

```
vendor-files/
  autophix-9610/        manual (EN/FR/DE/ES/IT) and five V1.61 language-pack updates
  ancel-vd700/          manual (EN)
  obd2-scanz-vw/        update/feedback guide, V1.60 update package, (empty) user manual
  biltema-15-1375/      manual (SV/NO/FI/DA)
docs/
  update-protocol.md    USB update protocol, image format, flash layout
  mcu-key-extraction.md how to pull the MCU image key off the hardware
  prior-art.md          related projects, protection bypasses, reusable protocol code
tools/
  openphix-tool/        cross-platform CLI updater (C99, libusb-1.0)
  keysearch/            finds the MCU image key in a microcontroller flash dump
dumps/                  flash dumps from real units, git-ignored
```

The vendor's `Update.exe` binaries are not kept here. They were decompiled to
produce `docs/update-protocol.md`, and that write-up is what the project
needs going forward.

Future directories, once work starts:

```
firmware/    the open firmware
hardware/    schematics and PCB for the open reader
```

## Roadmap

1. Teardown. Photograph the board, identify the MCU, the flash, the CAN and
   K-line transceivers, the LCD controller and any debug pads (SWD).
2. Understand the stock update protocol. Done by decompilation, see
   `docs/update-protocol.md` and `tools/openphix-tool`. Still open: a USB
   capture against a real device to confirm it, and the cipher key.
3. Get code running. Either through the vendor bootloader (if the image
   format can be produced) or through SWD with a full backup of the original
   flash first.
4. Board support: LCD, keypad, backlight, beeper, USB, SPI flash, CAN,
   K-line, power measurement.
5. Protocol stacks: ISO 15765 / UDS first, then KWP2000 on K-line and CAN
   (TP2.0), then KWP1281 and TP1.6 for older cars.
6. Feature parity with the list above, starting with generic OBD2, then VAG
   fault codes and measuring blocks, then the service functions.
7. Open hardware: a new board that is drop-in for the same enclosure and
   cable, then a fully open design.

## Dumping a device

The USB protocol has a flash-read command, so the external flash of any
unit can be saved without changing anything on it. `tools/openphix-tool`
does this with read-only commands only:

```
cd tools/openphix-tool
make && sudo make install        # also installs the udev rule, so no sudo below

openphix-tool info                       # ids, version, flash size
openphix-tool read-flash -o extflash.bin # whole 32 MiB, about 4 minutes
openphix-tool feedback -o Feedback.bin   # the recorded bus log
openphix-tool decrypt extflash.bin plain.bin   # remove the XOR keystream
```

This was done on a DM100 in September 2026; the results are
described in `docs/update-protocol.md`. Dumps go under `dumps/`, which is
git-ignored because the content is the vendor's. The MCU's internal flash
(the actual firmware code) cannot be read over USB; that needs SWD access
to the microcontroller.

## Prior art

No public project covers this device family, and neither image format has
been opened before. The nearest neighbour is a replacement firmware for the
Autophix OM127, a simpler reader from the same vendor, which overwrote the
stock firmware rather than extracting it. There is mature public tooling for
STM32 read-out protection bypasses, and several good KWP1281 implementations
worth reusing. See [docs/prior-art.md](docs/prior-art.md).

## How to help

- Send photos of the PCB (both sides, high resolution) of any of these
  devices, and the exact brand and hardware revision (DM100, DM300 or
  DM100HC, visible from the USB PID in Windows Device Manager).
- Provide USB captures (Wireshark with USBPcap) of a full stock update and of
  a feedback download.
- Provide a working copy of the OBD2 SCANZ FST32 user manual, since the one
  in this repository is empty.
- Report other brand names this hardware is sold under. Any tool with the
  same eight-icon main menu, the same `0017-Dash Board` screenshots, and an
  update package with `DM100` and `DM300` folders belongs to this family.

## Legal

The manuals and update packages in `vendor-files/` are copyrighted by their
respective vendors and are kept here for reference and interoperability
research only. The open firmware will be written from scratch against public
standards (ISO 15765, ISO 14230, ISO 9141, SAE J1979 and the published VAG
transport protocols) and against observed device behaviour. No vendor code
is copied. Do not contribute decrypted or disassembled vendor firmware to
this repository.

Diagnostic tools can change vehicle settings. Anything you flash or run on a
car is at your own risk.
