# Prior art

A survey of GitHub for existing work on these scan tools, on the vendor's
firmware protection, and on the vehicle protocols the open firmware will
need. Searched September 2026.

## Nobody has done this device family

There is no public work on the Autophix 9610, Ancel VD700, OBD2 SCANZ FST32
or Biltema 15-1375, and none on the file formats. Searches for `ExtFlashDat`,
`McuCode`, `obd2scanz`, `vd700`, `autophix 9610`, `ancel AD310` and
`biltema obd` return nothing relevant. As far as the public record goes, the
data image keystream in this repository is the first time either of these
images has been opened.

## The one closely related project

[darkspr1te/OM127-reboot](https://github.com/darkspr1te/OM127-reboot) is a
replacement firmware for the Autophix OM127, a simpler code reader from the
same vendor, also sold as the Ancel AD310. Started 2018, still touched in
2025. Its notes are useful to us:

- The OM127 is an **STM32F103RB** with a **TJA1050** CAN transceiver and a
  transistor and op-amp K-line driver. So Autophix builds on STM32 parts, and
  at least historically on the STM32F1 family.
- The repository carries an ST-Link to SWD wiring diagram and board photos,
  which is a good guide to where these vendors put their debug pads.
- It confirms the rebadging pattern we documented: one ODM design sold as
  Autophix and as Ancel.
- Importantly, they did **not** extract or decrypt the vendor firmware. Their
  README warns that installing their build erases the OEM bootloader. They
  took the overwrite route, not the extraction route, so the vendor's
  encryption was never broken there either.

Note the OM127 is not our hardware. An STM32F103RB holds 128 KB of flash,
and our MCU image alone is about 275 KB, so the DM100 uses a larger part.

## Read-out protection bypasses, if we need them

If the microcontroller turns out to be an STM32F1 at protection level 1,
there is mature public tooling. This matters because the F1 family has 512 KB
parts (F103xE) that would fit our image, so it is a live possibility.

| Project | Notes |
|---|---|
| [CTXz/stm32f1-picopwner](https://github.com/CTXz/stm32f1-picopwner) | 322 stars, actively maintained. Runs the attack from a Raspberry Pi Pico, so the hardware cost is trivial. |
| [JohannesObermaier/f103-analysis](https://github.com/JohannesObermaier/f103-analysis) | The original implementation from the USENIX WOOT 2020 paper by Obermaier, Schink and Moczek. |
| [jerinsunny/stm32_vglitch](https://github.com/jerinsunny/stm32_vglitch) | Voltage glitching approach. |
| [raphui/rdp_bypass](https://github.com/raphui/rdp_bypass) | Another level 1 bypass. |

The same author as the OM127 project keeps forks of several of these, which
suggests they are the practical tools people actually reach for.

## VAG protocol implementations worth reusing

The open firmware has to speak the same protocols the stock firmware does.
These are all Arduino or embedded C and directly relevant.

| Project | Stars | Covers |
|---|---|---|
| [ibanezgomez/FISBlocks](https://github.com/ibanezgomez/FISBlocks) | 137 | KWP1281 over K-line |
| [mnaberez/vwradio](https://github.com/mnaberez/vwradio) | 123 | Reverse engineering of VW head units, good protocol notes |
| [domnulvlad/KLineKWP1281Lib](https://github.com/domnulvlad/KLineKWP1281Lib) | 77 | KWP1281 library for Arduino, ESP and Pico |
| [RXTX4816/OBDisplay-Uno](https://github.com/RXTX4816/OBDisplay-Uno) | 43 | KWP1281 measuring blocks on a display |
| [domnulvlad/KLineKWP1281Lib_ESP32](https://github.com/domnulvlad/KLineKWP1281Lib_ESP32) | 25 | ESP32 port of the above |
| [domnulvlad/KWP1281-VWTP2.0_converter_ESP32](https://github.com/domnulvlad/KWP1281-VWTP2.0_converter_ESP32) | 9 | Bridges KWP1281 and TP2.0, the two transports our device uses |

Nothing public covers TP1.6, and UDS is well served by generic automotive
libraries, so those two are where original work will be needed.
