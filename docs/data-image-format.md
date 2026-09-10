# The data image (`ExtFlashDat.bin`) format

`ExtFlashDat.bin` is the image the vendor writes to the external 32 MiB SPI
flash of the DM100 / DM300 scan tools. It holds everything that is not code:
the diagnostic database, the string tables of every UI language, the bitmap
fonts and the screens. This document describes it byte by byte, as decoded
from a flash dump of a Biltema 15-1375 (DM100, older firmware) and from the
Autophix 9610 V1.61 update packages. `tools/hixtool` is the reference
implementation of everything here, and `firmware/` consumes the same
structures on the device side.

Conventions: all integers are little-endian unless said otherwise. Offsets
inside a file are relative to the start of that file. "hix" is the vendor's
own extension for its containers (`string_03_en.hix`), and is used here as
the name of the container format.

## 1. Cipher

Every byte of the image is bit-rotated and XORed with a byte from a fixed
256 byte table `S`. Both the rotation and the table index come from one
virtual position `v` that repeats every 4864 bytes (19 blocks of 256):

```
j      = offset mod 4864
v      = (j & 0xFF) + (j >> 8)
cipher = rotl8(plain, (v >> 3) & 7) ^ S[v & 0xFF]
plain  = rotr8(cipher ^ S[v & 0xFF], (v >> 3) & 7)
```

`S` is listed in `tools/openphix-tool/src/crypto.c` and
`tools/hixtool/hixtool/cipher.py`. The table was recovered from zero-filled
regions (rotation does not change a zero byte, so those regions expose `S`
directly). The rotation was then found statistically: for each of the 4864
positions, exactly one rotation amount makes far more bytes land in
printable ASCII than any other, and the winners follow the formula above
without exception. The rotation steps up by one every eight bytes; the
boundary moves by one byte per 256 byte block, exactly like the table index.

The application on the device removes the cipher when it reads a resource.
Areas the application writes itself (feedback log, DTC review records,
settings near the top of the flash) are not encrypted.

## 2. The hix container

The plaintext image, and several files inside it, are containers with this
layout:

```
u8    count
count times:
    u16   name_len        length of the file name including its NUL
    char  name[name_len]  ASCII, NUL terminated
    u32   offset          from the start of the container
file data, back to back, in directory order
```

There are no sizes, no checksums and no alignment: a file ends where the
next one starts, and the last one runs to the end of the container. The
directory of the DM100 dump is 34 entries and ends at 0x2CA, which is also
the offset of the first file.

Files that are themselves containers: `STRING.BIN` (one `string_NN_xx.hix`
per UI language), `IMAGE00.bin` and `IMAGE01.bin` (three 320x240 screens
each), `DsTransID.BIN` and `UDSDTCTransID.BIN` (per data set tables).

## 3. File inventory

| File | Size (dump) | Content |
|---|---|---|
| Cmd.BIN | 163253 | diagnostic command definitions, referenced by id |
| ComboSpFunc.BIN | 272114 | special function procedures (EPB, battery, oil reset, ...) |
| DsTransID.BIN | 562425 | datastream (measuring block) text and unit ids |
| FUNCFG.BIN | 2983680 | per control module function configuration |
| Exp.BIN | 516691 | expressions in a C-like language for value conversion |
| Menu.BIN | 6636 | menu tree |
| SpFun.BIN | 106 | special function list |
| SYSSCAN.BIN | 1331222 | control module database (addresses, identification, data set lookup) |
| UDSDTCTransID.BIN | 758359 | UDS fault code text ids per module type |
| SpFuncDetecList.BIN | (package only) | detection list for special functions |
| STRING.BIN | 11555524 | string tables, one hix per language |
| POL_WIN_8x16.bin, POL_WIN_16x16.bin, POL_WIN_24x24.bin | 4096, 8192, 18432 | bitmap fonts, Windows-1250 (Polish) |
| RUSS_ISO_8x16.BIN, RUSS_ISO_16x16.BIN, RUSS_ISO_24x24.BIN | 4096, 8192, 18432 | bitmap fonts, Cyrillic |
| WESTISO_16X16.bin, WESTISO_20X20.BIN, WESTISO_20X32.BIN, WESTISO_24X24.bin | 8192, 15360, 24576, 18432 | bitmap fonts, Western European |
| SCN24_CP936.BIN, SCN16_CP936.BIN | 588832, 261712 | Chinese fonts, "MFL" format |
| Readyness.bin | 260 | I/M readiness icons |
| BGMENU.BIN | 17920 | all zero in every image seen |
| BGSTART_autophix.BIN, BGSTART_GRAY.BIN, BGSTART_BLUE.BIN | 153600 each | start screens, 320x240 RGB565 |
| LGMAKER.BIN | 21060 | logo |
| LGSETUP_GRAY.BIN, LGSETUP_BLUE.BIN | 153600 each | setup screens |
| ArrowKey.bin | 2560 | arrow key icons |
| LanguageLib.BIN | 9 | count and ids of the installed UI languages |
| IMAGE00.bin, IMAGE01.bin | 460874 each | screens for the special functions, one set per skin |

The sections below describe each format.

## 20. The feedback bus log (outside the data image)

The 128 KiB "Feedback" area at flash end minus 0x50000 (saved by
`openphix-tool feedback`) is written by the application. It starts with
`AUTOPHIX` and five zero bytes, then holds records back to back until the
first erased word (`FF FF FF FF`):

```
u32   length, counting the length field itself
bytes payload, length - 4 bytes
```

The payload is encrypted with the image cipher of section 1, keyed by the
payload's own offset inside the area (not the flash address). The records
seen so far are text:

```
"Str:" id(2 hex digits) " " len(4 hex digits) string NUL junk
```

`len` counts the string and its NUL; one uninitialised byte follows. The
Biltema unit's log holds only power-on entries, in pairs: `Str id=00
"01.58.000"` (the software version, which the manuals call "Software
Version" in Device Information) and `Str id=00 "AUTOPHIX"`. Records with
bus traffic (the reason the log exists) have not been captured yet; the
manual says the device records the next diagnostic session after Feedback
is enabled in Tool Setup. `hixtool feedback` decodes the file.

## 4. String tables (`STRING.BIN`)

`STRING.BIN` is a hix container with one member per UI language, named
`string_NN_xx.hix` (`NN` a language number, `xx` the ISO code): the Biltema
dump has 03_en, 15_da, 16_sv, 11_fi and 17_no; the Autophix EN/DE/ES
package has 03_en, 06_de, 05_es and eight stub languages (04_fr, 08_it,
07_ru, 10_pt, 11_fi, 15_da, 16_sv, 17_no) of about 90 strings each, just
enough for the main menu and the language menu (string 0x27 in them says
that the language only works for the menu display).

### 4.1 `string_NN_xx.hix`

```
u32   count
count x { u32 id, u32 offset }     sorted by id, offsets ascending
records, back to back, to the end of the file:
      u16  declared_len
      char text[]                  single byte code page
      u8   NUL
```

The index ends exactly where the first record starts and the last record
ends exactly at the end of the file. `declared_len` is not a reliable byte
count: in 622 of the 68581 English records it is larger than the real
length (by 1 per embedded line feed and by 3 per glyph code, the build tool
counted the escape sequence of its source text). The real extent of a
record is the distance to the next record's offset, minus the two length
bytes, minus the NUL. A reader that copies up to the NUL is safe.

Encoding: ISO-8859-1 for every Latin language (en, de, es, fr, it, pt, fi,
da, sv, no), ISO-8859-5 for Russian, matching the font files of section 5.
Bytes below 0x20 are glyph codes drawn from the font: 0x01 up arrow, 0x02
down, 0x03 left, 0x04 right, 0x07 "°C", 0x08 "°F", 0x09 "°", 0x0A line
break, 0x11..0x14 arrow variants that exist only in WESTISO_20X20.

### 4.2 Ids and groups

Every other database file stores the full u32 id (section 4.4). The high
16 bits act as a group. Strings per group in the English table of the dump:

| Group | Count | Content |
|---|---|---|
| 0x0000 | 152 | core UI: 0x0001..0x0044 menus and prompts, 0x0101.. vehicle selection, 0x0728..0x072b "Oil reset", "EPB reset", "BMS reset", "ETC reset", 0x1001..0x1044 on-screen keyboard keys |
| 0x0001 | 701 | generic OBDII (PID names, monitors, readiness), tool setup (0x10078 = name of the current language) |
| 0x0010 | 9 | special function error texts |
| 0x00e0 | 136 | tool setup, voltmeter, battery test |
| 0x00f0, 0x00f1 | 56 | professional mode UI and function names |
| 0x0100 | 146 | ECU family names `EV_ECM`, `EV_TCM`, ...; index = family number used by Cmd.BIN and UDSDTCTransID.BIN |
| 0x0102 | 326 | VAG module names, index = diagnostic address: 0x1020001 "0001-Engine Control Module 1", 0x1020019 "0019-Gateway" |
| 0x0104 | 103 | DTC status (0x1040001..4 "Passive/Sporadic", "Active/static") and fault type suffixes (0x1041000 + type) |
| 0x0108 | 1827 | special functions (basic settings, adaptation, login, service reset) |
| 0x0109 | 641 | adaptation and basic setting channel names "CH:000 ..." |
| 0x01ff | 7 | late additions |
| 0x0200 | 20 | ECU info labels ("VW/Audi Part Number: ") and protocol names ("STCAN_UDS", "KWP2000", "KWP1281") |
| 0x0201 | 939 | datastream value enumerations ("Yes", "No", "Stand-by") |
| 0x0202 | 1913 | UDS datastream names with unit ("Fuel level [L]") |
| 0x0203 | 10964 | KWP1281/KWP2000 measuring block field texts |
| 0x0205 | 1640 | measuring block status texts ("Idling", "ADP is OK") |
| 0x0301 | 176 | units ("%", "Hz", "U/min", "mV") |
| 0xa000 | 7702 | VAG 5 digit DTC texts, index = the number (section 4.3) |
| 0xa100..0xa1ff | 23971 | VAG UDS 3 byte DTC texts |
| 0xa200..0xa211 | 2483 | VAG KWP2000 2 byte DTC texts with variants |
| 0xa300 | 869 | running list of DTC texts |
| 0xff00 | 8377 | generic OBDII DTC texts, index = 16 bit DTC |
| 0xff01..0xff1c | 5967 | manufacturer specific P1xxx texts, one group per manufacturer |

The V1.61 package adds 290 ids (0x0302 units in brackets, 0x0110 battery
part numbers, more 0x0108 and 0x0202) and changes 72 texts (module names
gained abbreviations, "0001-ECM Engine Control Module 1"; 0x1026e, the
library version, became "01.36.000"). Nothing was removed.

### 4.3 DTC id encodings

DTC numbers are packed into the id itself, so a fault code found on the bus
maps to a string without a lookup table. `DDDD` below is the 16 bit SAE
code (top two bits select P/C/B/U, then the 14 bit number, so P0001 =
0x0001, C0001 = 0x4001, B1000 = 0x9000, U1000 = 0xD000).

| Id | Meaning | Verified |
|---|---|---|
| 0xA000 iiii | VAG 5 digit code, iiii = the decimal number. Below 16384 the text starts with the number; 16384..32767 the text starts with an SAE style code, n = i - 16384, letter = "PCBU"[n / 4096], digit = (n mod 4096) / 1024, then n mod 1024 in decimal, then "00" | all 2901 |
| 0xA1 DDDD TT | VAG UDS 3 byte DTC, TT = fault type byte. Text "P000100 Fuel Volume Regulator..." | all 23971 |
| 0xA2 VV DDDD | VAG KWP2000 2 byte DTC, VV = variant 0..0x11 for the same code in another module family | 2476 of 2483 |
| 0xFF00 DDDD | generic OBDII. 0xFF000001 "P0001 ..." | all 8377 |
| 0xFF MM DDDD | manufacturer specific, MM = 0x01..0x1C selects a make (0x01 Ford, 0x04 Mercedes, 0x17 Toyota; the full mapping is open) | |

### 4.4 Which files use which strings

Counting u32 values that are valid English ids (ids above 0xFFFF only):

| File | Hits | Main groups |
|---|---|---|
| FUNCFG.BIN | 91181 | 0x0001, 0x0108, 0x0202, 0x0100 |
| UDSDTCTransID.BIN | 108422 | 0xA1xxxxxx, 0xA000 |
| DsTransID.BIN | 89725 | 0x0203, 0x0202, 0x0205, 0x0201 |
| SYSSCAN.BIN | 37671 | 0x0202, 0x0102, 0x0108 |
| ComboSpFunc.BIN | 12501 | 0x0108, 0x0001, 0xFF00 |
| Cmd.BIN | 7207 | 0x0001, 0x0100 |
| Menu.BIN | 350 | 0x0102, 0x0108 |

Each language file has its own sorted index, so the natural lookup is a
binary search by id in the current language with a fall back to English
when the id is missing (the Nordic tables have about 40000 of the 68581
English strings: no measuring block texts, no units, no generic OBDII DTC
texts; Norwegian has only the 1899 menu and special function strings).

## 5. Fonts

### 5.1 Raw 256 glyph fonts

`POL_WIN_*`, `RUSS_ISO_*` and `WESTISO_*` have no header: 256 glyphs, glyph
code = byte value in the font's code page, each glyph `ceil(width / 8)`
bytes per row, rows top to bottom, most significant bit = leftmost pixel,
1 = ink. File size = 256 x glyph size in every case (16, 32, 72, 60 and 96
bytes for 8x16, 16x16, 24x24, 20x20 and 20x32).

| Family | Code page | Proof glyphs |
|---|---|---|
| WESTISO | ISO-8859-1 | 0xC4 Ä, 0xE4 ä, 0xF6 ö, 0xE5 å, 0xDF ß, 0xE6 æ, 0xF8 ø |
| RUSS_ISO | ISO-8859-5 | 0xB0..0xB3 А Б В Г, 0xC0 Р (under CP1251 it would be А) |
| POL_WIN | Windows-1250 | 0xA3 Ł, 0xB9 ą, 0xEA ę, 0xBF ż, 0x9C ś |

Latin glyphs in the 16x16 and 24x24 fonts are narrow designs left-aligned
in the cell (a typical letter uses 10 of 16 columns); the 8x16 fonts fill
the cell. Codes 0x01..0x04 are arrows, 0x07 "°C", 0x08 "°F", 0x09 "°" in
all families; WESTISO_20X20 adds 0x05, 0x06, 0x0B..0x17 (bell, speaker,
triangles, filled arrows). WESTISO_20X32 has only bold digits and letters
(the large value font). 0x20, 0x7F and 0x80..0x9F are empty.

### 5.2 MFL fonts (`SCN16_CP936.BIN`, `SCN24_CP936.BIN`)

```
0x00  "MFL" 0x10
0x04  u32  total file size, header included (261712 / 588832)
0x08  u8 0, u8 glyph size (0x10 / 0x18), u8 2, u8 0
0x0C  u32 0
0x10  8178 glyphs, same bit layout as 5.1, 32 or 72 bytes each
```

8178 = 87 rows x 94 cells, the GB2312 matrix in the classic HZK layout:
glyph index = (b1 - 0xA1) * 94 + (b2 - 0xA1). No index table and no ASCII
part (ASCII comes from the raw fonts). Verified with 中 (D6D0), 国 (B9FA),
± (A1C0), ℃ (A1E6) and 啊 (B0A1). No Chinese string table exists in either
image, so these fonts are unused ballast on the European builds.

## 6. Images

| File | Format | Content |
|---|---|---|
| BGSTART_autophix.BIN | 320x240 RGB565 little-endian, no header | Autophix splash |
| BGSTART_GRAY.BIN, BGSTART_BLUE.BIN | 320x240 RGB565 | splash per skin (Sky Gray = dark navy, Gem Blue = white). The only image files that differ between the Biltema dump and the Autophix package |
| LGSETUP_GRAY.BIN, LGSETUP_BLUE.BIN | 320x240 RGB565 | the eight icon main menu per skin |
| IMAGE00.bin, IMAGE01.bin | hix container of 320x240 RGB565 screens | help screens of the special functions, light and dark skin: BatInfo_P1, ReleaeEPB_P1, ToolRetractPump_P1; the package adds BatInfo_P2..P4 and OpenEngineHood_P1 |
| LGMAKER.BIN | 117x90 RGB565 | "vehicle diagnosis" button (car with magnifier) |
| ArrowKey.bin | 5 x 64x64, 1 bpp, 8 bytes per row, bit 0 = leftmost pixel | up, down, right, left, OK icons |
| Readyness.bin | 32x20 then 3 x 24x20, 1 bpp, bit 0 = leftmost pixel | readiness header icon, "not available", "complete", "not complete" |
| BGMENU.BIN | 17920 zero bytes | unused |

Byte order was proven by rendering: little-endian gives the logos and icon
grids, big-endian gives noise. `hixtool images` exports all of them. One
quirk: the dump's IMAGE01.bin/ToolRetractPump_P1.BIN is 153601 bytes, one
stray byte after the image.
