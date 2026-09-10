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

## 7. Expressions (`Exp.BIN`)

### 7.1 Layout

```
u32   count                          3535 in the dump, 3543 in V1.61
count x { u32 id, u32 offset }       sorted by id
records: u16 len (including the NUL), text, NUL
```

Records follow each other in index order; one unreferenced record sits
after id 0x02080053 in both images (a deleted entry whose bytes were kept).
Ids are `group:index` (u16:u16) and are referenced as u32 from FUNCFG.BIN,
ComboSpFunc.BIN, SYSSCAN.BIN, DsTransID.BIN and Menu.BIN.

| Group | Count | Content |
|---|---|---|
| 0x0000 | 155 | generic helpers: response checks, VIN and part number formatting |
| 0x0100 | 890 | measuring value conversions (`Y=0.39215687*x;`) |
| 0x0101 | 72 | byte to string id enumerations (`else if` chains) |
| 0x0110 | 69 | multi byte conversions with "not available" checks |
| 0x02MM | 1 to 147 per module | ECU identification by part number, MM = VAG address |
| 0x0400 | 579 | fault code translation hooks (`TRANSID`) |
| 0xEE00 | 232 | special functions, battery test limits (`RANGE`) |

### 7.2 The language

Every record is one or more statements that assign the result `Y`:

```
Y=1;
if(X1==0x54) Y=1; else Y=0;
if(x1==0xFF&&x2==0xFF) Y=string(0x02,0x01,0x00,0x0A);else Y=1.0/32768*(x1*256+x2);
if(0 == strcmp([EV_Brake1ABS90BOSCH003],ASCII(X5,X6,...,X33))&&(X1==22)) Y=1; else Y=0;
Y=TRANSID(ID(0x00000038),ID(0x00000039),ID(0x04001001));
Y=RANGE(2000,boundless);
```

- Variables: `X1`..`X41` (case mixed freely, `x1` is the same), the
  response payload bytes; bare `x`, the single input byte; `x.bitN` and
  `xK.bitN`, bit access; `Y` or `y`, the result.
- Literals: hex `0x..`, decimal integers, decimal floats (`0.39215687`),
  no negative literals. Two records write `FF` without the `0x`.
- Operators: `== != < > <= >= && || & | >> + - * / %`, parentheses. Not
  used anywhere: `<<`, `^`, `~`, `!`, `?:`, braces, double quotes.
- Statements: `Y=expr;`, `if(cond) stmt; else stmt;`, `else if` chains
  (the longest record is 2204 characters). A few records follow a chain
  with an unconditional assignment, which then always wins.
- String literals are written in square brackets: `[EV_ClimaTronicT5A01]`,
  `[%02X %02X]`.
- Functions: `string(a,b,c,d)` yields the string id `a<<24|b<<16|c<<8|d`
  (3026 of 3109 uses resolve in the English table; literal constants such
  as `Y=0x01040002;` are string ids too); `strcmp([lit], ASCII(...))`, always
  compared with 0; `ASCII(bytes...)` builds text from bytes; `SPRINTF([fmt],
  args...)` and `CHARCONVERT` with `%c`, `%X`, `%02X`; `HEX(bytes...)`;
  `INT(expr)`; `ID(n)` evaluates another record; `TRANSID(ID(a),ID(b),ID(c))`
  translates a fault code (a = the raw code, b and c string id bases);
  `RANGE(lo, hi)` or `RANGE(lo, boundless)` gives limits for a value.
- Seven records have unbalanced parentheses (0x01000001, 0x01000305,
  0x01000307, 0x01000308, 0x01000363, 0x01000364, 0x01000365). An
  interpreter has to tolerate that or those conversions fail.

The open firmware implements this language in `firmware/core/exp.c`; the
test suite parses and evaluates every record of the dump.

## 8. Diagnostic commands (`Cmd.BIN`)

### 8.1 Layout

```
u32   count                          6399 in the dump, 6408 in V1.61
count x { u32 id, u32 offset }
records:
      u8  len          bytes that follow; wrong in 171 records, use the index
      u8  kind
      u8  h1..h5       header, meaning depends on kind
      payload
```

| Kind | Count | Meaning | Header and payload |
|---|---|---|---|
| 0x10 | 5751 | send | h1 link, h2 = 1 when packets carry a CAN id, h3 packet count, h4 = 0xFF when the payload is completed at run time, h5 = 2 when the second packet is an ISO-TP flow control; packets follow |
| 0x01 | 363 | keepalive | h1 payload format (1 raw K-line, 2 CAN packets, 3 ISO 9141, 4 KWP1281 block, 5 K-line chunks, 0x11 KWP1281 chunk), h2 flags, h4h5 = period in ms (800 for tester present) |
| 0x02, 0x25 | 87 | raw ISO 14230 message | `fmt target source [len] data... checksum`, target, source and checksum are 0x00 placeholders |
| 0x40 | 39 | raw KWP1281 block | `len 00 title data... 03`, the 0x00 is the block counter placeholder |
| 0x30 | 86 | 5 baud init | h3 = ECU address, h4h5 = 5; group 0x5500 has one per VAG address |
| 0x60 | 15 | number | h2..h5 u32 big-endian, microsecond delays and timeouts (8000000, 500000, 95000) |
| 0x08 | 58 | raw bytes | ISO 9141-2 headers `68 6a f1`, ISO 14230 address modes, and an unidentified K-line protocol |

### 8.2 Packets of kind 0x10

```
u8   blen        bytes that follow in this packet
u8   dlc         low nibble = CAN data length; bit 7 set in the 0x32xx groups
u16  can_id      big-endian, only when blen == dlc + 3; STM32 bxCAN format, StdId = value >> 5
u8   data[dlc]
```

The link byte h1 says how to read the data:

| h1 | Count | Content |
|---|---|---|
| 0x00, 0x50, 0x52 | 3621 | raw CAN frames with the ISO-TP PCI inside (padding 0x00, 0x55 or 0xFF). The same UDS command set exists three times (groups 0x3000 / 0x3100 / 0x3200 and 0x3022 / 0x3222) under the three values, so they select a physical CAN setting that the file does not name |
| 0x05 | 108 | TP2.0 data packet: opcode (1N last packet, 2N more follow), u16 big-endian length, KWP2000 data |
| 0x06 | 2795 | K-line KWP2000: the complete message (format, target, source, service, data, checksum) split into chunks of at most 7 bytes, each prefixed with a TP2.0 style opcode (0x30 single or last, 0x20 first, 0x21 second, 0x31/0x32 last with sequence). Reassembled, 2123 of 2144 messages have a correct checksum; the 21 failures are generator bugs (a stale 0xD8 byte in the tester present records of most modules, module 0xE9 with checksums copied from another module) |
| 0x07 | 15 | KWP1281 block with a 0x00 counter placeholder, same chunk prefixes |
| 0x01, 0x02 | 226 | TP2.0 channel setup (`07 01 c0 00 10 00 03 01` to 0x200) and parameter frames (`a0 0f 8a ff 32 ff`) |

CAN ids seen: 0x7DF and 0x7E0 (OBD), 0x700 (functional tester present),
0x200 (TP2.0 broadcast), per module TP2.0 channel ids, 0x440/0x441 (a non
VAG UDS set in group 0x7000). The bxCAN encoding was checked: 0xFBE0 >> 5 =
0x7DF, 0xFC00 >> 5 = 0x7E0, 0x4000 >> 5 = 0x200.

Worked examples:

```
00000001  10 | 00 00 01 00 00 | 09 08 02 10 03 00 00 00 00 00
          one CAN frame, 02 10 03 = UDS DiagnosticSessionControl extended, pad 00
00000004  10 | 50 00 02 00 02 | 09 08 03 22 f1 90 55 55 55 55 | 09 08 30 00 00 55 55 55 55 55
          ReadDataByIdentifier F190 (VIN), then the flow control frame, pad 55
30220102  10 | 50 00 01 00 00 | 09 08 03 22 01 02 55 55 55 55
          group 0x3022 = "UDS 22 <DID>", the DID is the low 16 bits of the id
02010005  10 | 06 00 01 00 00 | 08 07 30 82 10 f1 10 89 1c
          K-line StartDiagnosticSession 0x89 to target 0x10 from 0xF1, checksum 0x1C
02011026  10 | 06 00 03 00 00 | 09 08 20 8c 10 f1 31 bb 01 03 | 09 08 21 00 .. | 04 03 32 00 7d
          three chunks = 8c 10 f1 31 bb 01 03 00 00 00 00 00 00 00 00 7d (StartRoutine 0xBB)
06000005  10 | 07 00 01 01 00 | 07 06 30 04 00 29 51 03
          KWP1281 block: len 4, counter placeholder, title 0x29 group reading, group 0x51
00000003  01 | 02 c0 00 03 20 | 0b 08 e0 00 02 3e 80 55 55 55 55 55
          every 800 ms: 02 3e 80 TesterPresent to CAN id 0x700
```

### 8.3 Per module command sets

Group 0x02MM (MM = VAG diagnostic address, 53 modules) holds the same 44
commands per module: TP2.0 connect and parameters, StartDiagnosticSession
`10 89`, ReadEcuIdentification `1a 9b/90/91/86`, ReadDataByLocalId `22
f187/f190/f197/f1a5/f1df/f191`, ReadDTC `18 00 ff 00` and `18 02 ff 00`,
ClearDTC `14 ff 00`, `21 01`, routines `31 b8/b9/ba`, SecurityAccess `27
03/04`, tester present. The KWP2000 physical target per VAG address, read
from those records (VAG:target):

```
01:10 02:1a 07:63 08:98 0d:b0 13:29 15:58 16:30 19:40 1c:69 23:2a 25:c0 26:a0
32:18 34:38 37:62 38:af 46:a2 48:a8 57:82 64:20 67:83 68:a6 6d:b2 6e:68
76:60 83:28 85:c4 86:a5 89:a4 91:11 92:1b 97:61 98:99 a2:1c a7:64 a8:9a
ad:92 b5:c1 b6:a1 bc:6b c4:31 c7:81 d5:70 d6:80 dc:6c e5:3a e6:a3 e9:41
ec:6a f1:43 f7:91 f8:a7
```

Generic sets: 0x0100 (94, CAN), 0x0400 (KWP1281 blocks), 0x0500 (K-line
raw), 0x0600 (KWP1281 chunked), 0x5500 (slow init per module), 0x5600
(TP2.0 connect per module), 0x7000 and 0x8000 (a non VAG UDS set), 0xE0xx,
0xE122, 0xE200, 0xE320 (special functions: `2e` writes, `31 01 03 a0/a1`
routines, security access), 0xFF00 (delays).

Open: what the three CAN link values and the dlc bit 7 select physically,
the keepalive flag bits, and the protocols behind kind 0x08. These need a
bus capture or the MCU firmware.
