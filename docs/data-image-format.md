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

## 9. The control module database (`SYSSCAN.BIN`)

`SYSSCAN.BIN` drives "System Scan" and "System Selection": which addresses
to probe, how to identify the module found there, and which data set (one
ECU variant's configuration in FUNCFG.BIN, DsTransID.BIN and ComboSpFunc.BIN)
applies. Every byte of the file is accounted for except an 11 byte stub at
0x1223.

### 9.1 The data set key

A data set is identified by a u32 written as two u16: `sub` (an index from
1, or 0x1001 and up, or 0xFFFF for "default") and `X` = (u8 family, u8
module index). Families: 0x30 (K-line and TP2.0 style), 0x32 (UDS), rarely
0x31, 0x33, 0x34; the module index is the low byte of the diagnostic
address or 0xFF. `(0x0001, 0x0130)` is engine data set 1 of the 0x30 family;
`(0x13D5, 0x0132)` engine data set 0x13D5 of the UDS family. This key is the
value of the EV name tables below, the record key of FUNCFG.BIN, the key of
the ComboSpFunc function table and (the `sub` part) the group id of the
DsTransID measuring block texts.

### 9.2 Region map (dump)

| Offset | Size | Content |
|---|---|---|
| 0x0000000 | 12 | header: u32 12, u32 0, u32 offset of the module index |
| 0x000000C | 130 | generic scan procedure index |
| 0x000008E | 437 | generic scan records (21 of 38 or 57 bytes) |
| 0x0000243 | 4055 | script nodes of the generic records |
| 0x000122E | 1327 | 2 character code table (94 VIN model codes: 8K, 8P, 4L, FV, 3W, ...) |
| 0x0001991 | 32841 | 3 character code table (2345 rows, each pointing at an address list) |
| 0x000D0D0 | 131432 | 382 address lists |
| 0x002D238 | 7369 | module index (554 entries) |
| 0x002EF01 | to end | 509 scan records with scripts, four EV name tables, one 312 KB zero fill |

### 9.3 Generic scan procedures

The index at offset 12 holds `{u32 id, u8 a, u8 n, u32 record[n]}` entries
terminated by `FF FF FF FF`. Ids 0xF001 and up are referenced from Menu.BIN,
so they are the procedures behind the scan menu items. 0xF001 and 0xF002
run six and five protocol passes (one 38 byte record with a script each);
0xF003 to 0xF009 are one 57 byte record each pointing at a fixed address
list (for example 1 2 3 9 15 17 46, or 94 95). The V1.61 package adds
0xF00A and 0xF00B.

### 9.4 Scan record

Every record referenced from an index starts with `08 00 00 00`:

```
r+0   u32  8
r+4   u32  uid, monotonically increasing through the file (a generator handle)
r+13  u8   flags: 0x00, 0x30 or 0x81
r+17  u8[14] parameters, non zero only for flags 0x81
r+31  u8   2 when a script follows
r+34  u32  script offset, always r+60
r+37  u8   item count
r+38  items of 7 bytes: { u16 key, u8 addr, u8 type, u16 y, u8 z }
      tail entries of 10 bytes: { u16 n, u32 ref, u32 v }
      u16 0xFFFF
      (script records) nodes, EV name tables, padding up to the next record
```

The item type tells the identification method: 0x56 followed by four 0x01
items (KWP1281 style, tail v = 0x87), 0x02 items with the module address
(tail types 0x02 and 0x06, v = 0x71 and 0x70), 0x30/0x31/0x32 items with
(offset 3, length 0x1C) and (offset 7, length 3), which look like substring
descriptors for the part number match, 0x70 items on the UDS only 0x9x
modules. Tail entries carry either a file offset (generic procedures: the
address list to scan) or a data set key. Records with tail family 0x30 to
0x34 enumerate the data sets that exist for the module (module 3: sub 1 to
0x93 plus the 0xFFFF default; module 2: 147 plus default). The small v
values (0x87, 0x71, 0x70, 0x74, 0x73, 6) are constant per type and most
likely Cmd.BIN ids of the connect sequence for that protocol; unverified.

### 9.5 Code tables and EV name tables

Tables of "type 2" have an 11 byte header `{u8 2, u32 count, u16 width, u16
sub, u16 X}` and rows `{u32 id, u32 value, u16 len, char name[len]}`.

- The 2 character table (94 rows) lists VAG model codes as they appear in
  VIN positions 7 and 8; the value is always 1 ("known").
- The 3 character table (2345 rows, 0CJ to ZXN) maps a code to the offset of
  an address list; scan pass 5 of 0xF001 looks a vehicle up here to choose
  which addresses to probe.
- The EV name tables map identification strings to data set keys: engine
  address 1 has 5581 rows (family 0x30) and 29 (UDS), engine 2 (address
  0x11) 5546 and 7. A name such as `EV_ECM00TDI01104L907309AC001` is
  vendor built: type, engine kind (TDI, TFS, CPI, MPI, CSI), the VAG part
  number (04L907309AC) and a variant. The row value is the data set key.

Address lists are `{u16 count, count x {u32 address, u16 name_idx, 02 01 08
01 02 02}}`. One list declares 43 entries but holds 27 (same in both
images).

### 9.6 Module index

`{u32 address, u8 n, n x {u8 kind = 2, u32 record}}` entries terminated by
`FF FF FF FF`. Address classes: 0x0001 to 0x00E0 plain VAG addresses (210);
0x0601 to 0x0768 sub addresses (54); 0x800B and 0x8104 to 0x8122 extended
addresses (13); 0x19FF "gateway, all" (the installation list scan);
0x6000xxxx a second view of a plain address with a subset of its records
(247, System Selection versus System Scan); 0x8000xxxx groups referenced
from Menu.BIN for the special function menus (0x80000016 = the records of
module 0x96 for "Battery Coding").

### 9.7 How identification works

1. The tool connects to an address with one of the module's records (the
   tail type selects KWP1281, KWP2000, TP2.0 or UDS).
2. The identification response gives the part number (KWP1281 id string,
   KWP2000 `1A 9B`, UDS DID F187/F19E). For engines the name is looked up
   in the EV name table, which yields the data set key. For other modules
   the record with tail family 0x30 to 0x34 lists the existing data sets.
3. The key selects the FUNCFG.BIN record, the DsTransID text group, the
   ComboSpFunc function table row and, through the module type name (string
   family 0x0100), the UDSDTCTransID section.

Open: the semantics of the script nodes (u32 type 0x1E, 0x01, 0x04, 0x28
with child pointers; type 4 nodes reference the code and EV tables), the
uid field, and the meaning of the 0x06xx, 0x8xxx and 0xC0xx addresses.

## 10. Measuring block texts (`DsTransID.BIN`)

A hix container with four sections, each `u32 count` then `count x {u32
key, u32 string_id}`, identical in both images:

| Section | Records | Key | Value |
|---|---|---|---|
| TEXT | 68150 | `group << 16 \| block << 8 \| field`; group = the data set `sub` (0x1001 to 0x142A and 0x1B01 to 0x1C44, 868 groups), block = measuring block 1 to 0xE6, field 1 to 4 | string in family 0x0203, for example 0x10010101 to 0x02030D94 "Engine Speed-(Discrete)". Each group starts with a header record (block 0, field 0) to 0x12000000 |
| TEXT_25H | 1630 | n | 0x02050000 + n + 1: value texts of KWP1281 0x25 responses ("ADP is OK") |
| TEXT_DEFNAME_02H, TEXT_DEFNAME_E7H | 256 each | KWP1281 data type byte | 0x0205xxxx default field name (2 "RPM", 3 "Load", 0x82 "Duty cycle"); the two tables are identical |

## 11. UDS fault code texts (`UDSDTCTransID.BIN`)

A hix container with 140 sections named by module type (`EV_ACC`, `EV_ECM`,
`EV_ESP`, `LT3_UDS`, ...); the same names live in string family 0x0100.
Each section is `u32 count` then sorted `{u32 key, u32 string_id}`. Values
are string ids of family 0xA1 whose low 24 bits encode the DTC (section
4.3). For the UDS only module types the key is the raw 24 bit DTC and most
rows are identity mappings; for the big types (EV_ECM 22159 rows, EV_ESP
7467, EV_SteerAssis 6023) the keys are small dense indices into the type's
DTC list held in FUNCFG.BIN. The LT3 sections use 0xA2 and 0xA3 ids.

## 12. Special function scripts (`ComboSpFunc.BIN`)

```
u32   count                          181 in the dump, 257 in V1.61
count x { u8 variant, u8 kind, u16 function, u32 offset }
function table: rows { u16 sub, u16 X, u8 n, n x { u8 variant, u8 kind, u16 function } }
                4983 rows, then FF FF FF FF
records
```

`kind` uses the same family codes as the SYSSCAN tail types (1, 2, 4, 5, 6
for the protocol attempt types, 0x30 and 0x32 for the data set families),
so a function has one script per protocol. Function numbers are the
special function menu items: 1 service reset (its records use string
0x01083001), 2 throttle learning, 3 EPB (0x0108202F "Replace Brake Pads"),
5 injector adaptation, 6 DPF, and so on. The function table says which
functions a data set supports: `(1, 0x0130)` engine data set 1 supports
`(1, 0x30, 2)`.

A record starts with a flag byte and the title string id (family 0x0108),
then an entry count and entries of `{u32 string_id, u32 pointer}`, an
8 byte trailer with the variant number and a function group byte (0xF0
service, 0xF1 EPB, 0xF4 injector adaptation). The steps behind the pointers
are nodes in the same style as the SYSSCAN scripts: string ids (titles,
prompts, results), child pointer lists, length prefixed screen image names
(`ToolRetractPump_P1.BIN`, `ReleaeEPB_P1.BIN`, `BatInfo_P1.BIN`) followed by
a u32 10000000 or 20000000 (10 s or 20 s), and `{u8 tag, u32}` operands.
Across all records: 10594 string ids, 6797 pointers, 31 image names. The
step grammar itself (which bytes are bus commands, expected responses and
branches) is open and needs either a bus capture of a special function or
the MCU firmware.

## 13. The menu tree (`Menu.BIN`)

```
node  := u32 title_string_id, u32 count, count x entry      (8 + 20 * count bytes)
entry := u32 caption_string_id, u32 action, u32 param, u32 0, u32 0
```

The file is a flat sequence of nodes, node 0 at offset 0 is the root, and
sub menus are addressed by file offset. Entry kinds, decided by `action`:

| action | Entry |
|---|---|
| 0, param != 0 | sub menu at offset `param` |
| bit 31 set | special function: `action & 0x7FFFFFFF` is the type (1 service reset, 2 throttle, 3 EPB, 4 injector, 5 DPF adaptation, 6 DPF regeneration, 8/0xA/0xB steering angle via module 03/16/44, 9 TPMS, 0xE fuel pump, 0x11 SRS, 0x15 ABS bleeding, 0x16 Crafter service reset, 0x1C NOx regeneration, 0x20 OPF), `param` the selector whose byte 2 is the function index and byte 3 the variant (the same keys as ComboSpFunc.BIN and SpFuncDetecList.BIN) |
| caption in family 0x0102 | control module, `action` = its diagnostic address |
| 0x19FF | "Vehicle Scan", `param` = SYSSCAN procedure id (0xF001); the Crafter branch puts the id 0xF003 in `action` instead |

Menu.BIN holds only the "For VW" branch. The eight icon main menu, the
OBDII branch and Tool Setup are built into the firmware (their captions
are strings 0x1017C "OBDII", 0x10268 "For VW", 0x728 to 0x72B "Oil reset",
"EPB reset", "BMS reset", "ETC reset", 0x102AD "BAT Check", 0x1025A "Tool
Setup"). The Biltema tree (12 nodes, 6636 bytes):

```
[0x0000 "Vehicle Selection"]
  "All Other Vehicles" -> 0x30
    [0x0030 "Select Menu"]
      "Vehicle Scan"        scan procedure 0xF001
      "System Selection"    -> 0x74: "Common System" (14 modules), "All Systems" (252 modules)
      "Special Functions"   -> 0xD4: Service Reset, Throttle Learning, EPB Replace Brake Pads,
                               Diesel Engine Special Function (5 entries), Steering Angle Learning
                               (3 modules), TPMS, Injector Adaptation, Battery Registration
                               (modules 0019, 0061, 008C), SRS Reset (0015, 8118), ABS Pump Bleeding
  "Only for Crafter(LT3) 2006-2017" -> 0xA4: Vehicle Scan (0xF003), Service Reset (LT3)
[0x17A0 "Select System"]   unreferenced: the 29 Crafter LT3 modules (0091 ... 00D3)
```

The V1.61 package adds a "Select Brand" root (VW, Audi, Skoda, Seat, Bentley,
Lamborghini, "Buaggti" [sic], VWN) whose brand nodes share one System
Selection node but carry their own Special Functions node with different
selector variants, plus two OPF entries for VW.

## 14. Small files

- `LanguageLib.BIN`: `u8 count, u8 id[count]` = 8, then 1 3 4 5 6 8 12 14.
  Identical in the Biltema dump and the Autophix package although their
  string tables differ, so it does not describe the installed languages.
  How these ids relate to the `string_NN` numbers is open.
- `SpFun.BIN` (106 bytes): `u8 1, u32 5, u32 0x23, u8 5, u8 2, u32 0x3C`,
  20 zero bytes, at 0x23 a list `u8 5` and six u32 0x00010001 to
  0x00010006 (special function selectors, function 1 variants 1 to 6), at
  0x3C `u8 0, u32 0, u32 0x45`, and at 0x45 a 37 byte protocol record
  identical to the FUNCFG one for the default UDS module (0x7E0 / 0x7E8).
- `SpFuncDetecList.BIN` (package only, 202 bytes): `u32 0x18 ("Special
  Functions"), u32 count = 11, count x { u32 module, u8 2, u8 n, u32
  selector[n] }`: per module, the special function selectors that the
  "detect special functions" scan tries (engine: 0x00020000, 0x00040000,
  0x00050000, 0x00060000, 0x000E0000, 0x00190000, 0x001C0000; ABS: EPB,
  steering angle, TPMS, bleeding; gateway, battery regulation, airbag ...).

## 15. Function configuration (`FUNCFG.BIN`)

The per control module function configuration: 13621 records (13634 in
V1.61) followed by pools of chunks. All pointers are absolute file offsets.

### 15.1 Record table (offset 0)

```
record := u16 variant, u8 kind, u8 module, u32 proto_ptr, u32 slot[6], u8 n, u32 extra[n]
          (n is 0 or 3)
```

The table ends with a terminator record (kind 0xFF, module 0xFF) whose
slot 0 holds the table end (0x904B9). `kind` is the protocol family and is
also the top byte of every Cmd.BIN id the record uses:

| kind | Records | Protocol |
|---|---|---|
| 0x01 | 90 | K-line KWP2000 (commands 0x0100xxxx: `18 00 ff 00`, `1a 9b`, `31 b8`) |
| 0x02 | 53 | CAN TP2.0 with KWP2000 (per module command sets 0x02MM00xx) |
| 0x04 | 85 | K-line KWP1281 (block titles `06 03` End Output, `09 03` ACK) |
| 0x05 | 85 | K-line KWP2000 with fast init (`81 .. 3e`) |
| 0x06 | 52 | CAN TP1.6 (TP2.0 style transport with KWP1281 blocks) |
| 0x30 | 12796 | CAN UDS, 11 bit ids (0x7E0/0x7E8 engine, 0x7E1/0x7E9 gearbox) |
| 0x31 | 11 | UDS variant with its own protocol records |
| 0x32 | 413 | CAN UDS, 29 bit ids 0x17FC00xx / 0x17FE00xx |
| 0x33 to 0x36 | 1 each | UDS variants for module 0xFF |
| 0x70, 0x80 | 23, 8 | Crafter LT3 modules |

`variant` numbers the ECU variants of a (kind, module) pair: kind 0x30
module 01 has 5583 records (0 to 5581 and the 0xFFFF fall back); the
package adds 13 engine variants, the only table difference. The slots:

| Slot | Function |
|---|---|
| 0 | connect / start session |
| 1 | keep alive and disconnect |
| 2 | 01 Version Information |
| 3 | 02 Read Fault Codes |
| 4 | 03 Erase Fault Codes |
| 5 | 04 Read Datastream (0 = not available) |
| extra 0, 1, 2 | 05 Basic Settings, 07 Adaptation, 08 Access Authorization |

### 15.2 Protocol records

```
u8 proto (2 = 11 bit ids, 3 = 29 bit ids), u8 flags, u8 0, u32 id0, u32 0, 03 06 0e 00,
u8 nids, u32 id[nids], u8 0, u16 timing[6]
```

CAN ids are stored in STM32 bxCAN register form: 11 bit id << 5 in the low
u16 (0xFC00 = 0x7E0, 0xFD00 = 0x7E8), 29 bit id << 3 in the u32 (0xBFE003B0
= 0x17FC0076). The timing is 3000 5 200 15 20 10 for most modules (t0 200
or 800 for some), presumably timeouts in ms and retry counts. K-line kinds
have no protocol record.

### 15.3 Primitives

```
cmd item := u32 cmd_id (Cmd.BIN), u16 arg (0xFFFF = none), u8 flag (mostly the expected reply length)
cmd list := u8 count, count x cmd item
node     := u8 0, u16 0, u32 cond_ptr, body                    (single variant chunk)
multi    := u8 0xFF, u16 nvar, u32 0x702[nvar], u32 flags, u16 0, u32 node_ptr[nvar], nodes
cond     := u8 0, u16 0, u32 exp_id, cmd list                   (send, evaluate, use the node if 1)
```

Slots 2 to 5 and the item lists start with the 7 byte node header; slots
0 and 1 do not.

### 15.4 The chunk grammars (UDS forms, verified on every chunk of both images)

```
connect   := u32 n, u32 0, n x { u32 seq, u32 exp_id, u16 0, cmd list }
keepalive := u32 0, u16 n, n x { u32 seq, u32 exp_id, cmd list }
version   := node, u8 a, u16 c, u32 z[4], u8 fmt, u16 n, n x step, u16 0
             step (fmt 1) := u16 seq, u32 label_string_id, u8 0, u16 0, u32 exp_id, cmd list
             step (fmt 3) := u16 seq, u32 label_string_id, u8 0, u8 2, u16 0x100, u32 ptr
read dtc  := node, u8 2, u8 x, u16 0xFFFD, u16 y, u32 ref, u32 0, u8 6, u8 0[12],
             u16 n, n x { u16 seq, u32 0x704, u32 exp_status, u16 0, u16 module, u16 0x400, cmd list },
             u8 0, u16 m, u32 dtc_ref[m]
erase dtc := node, 42 byte template, u32 exp_id, cmd list, 10 byte tail
datastream:= node, u8 0x15, u16 1, u32 1, u8 0[11], u16 1, u32 channel_table, u16 n, u16 channel_id[n], u16 0
channel table := u16 count, count x { u16 channel_id, u32 name_string_id, u32 1, u32 4,
                 cmd list read, u16 0, u32 conversion_exp_id, cmd list write }
item list := node, u32 title, u8 0, u16 0, u32 0, u32 title, u32 n, n x { u32 caption, u32 screen, u32 submenu, u32 0, u32 0 }
```

Engine module, kind 0x30, variant 0: connect sends command 0x30000004
(session setup with tester present), keep alive 0x30000005 (`3e 80`) and
0x30000006 (`10 01`, back to the default session), version information has
seven steps ("Protocol Type: ", "VIN: " through `22 f1 90` with expression
0x93 taking 17 characters, "ASAM ODX File Identifier: ", "ASAM ODX File
Version: ", "VW/Audi Part Number: ", "System Description: ", "Serve
Station Code: "), read DTC sends `19 02 04`, `19 02 08` and `19 02 20` and
decodes the status byte with expression 0x9A into "Active/static" or
"Passive/Sporadic", erase sends `14 ff ff ff` and checks for 0x44, and the
datastream lists 301 channels out of a shared table of 2745 (channel
0x973: name 0x020205EA, read `22 f4 03`, conversion 0x01000313). There
are only 19 channel tables (9097 entries) shared by 832 datastream chunks.

The K-line and TP2.0 forms of the DTC and datastream chunks (57 chunks of
each) are not decoded. The basic settings, adaptation and login item lists
point at screen scripts (3238 screens, decoded only to their header: caption,
protocol group, procedure pointer, prompt string ids) and at procedures in
the keep alive layout. A 417 KB region of per module part number tables
(`{u32 seq, u16 0, u32 id 0x000D1xxx, char[13]}` plus 24 zero bytes per
entry) is reached only from the K-line datastream chunks. What maps an
identified ECU to its `variant` index for the UDS kinds is not in this
file; SYSSCAN.BIN's data set keys (section 9.1) are the candidate.

The open firmware reads records, protocol records, connect and keep alive
steps, version information, UDS fault code entries, datastream channels
and the item lists in `firmware/core/funcfg.c`.

## 16. The feedback bus log (outside the data image)

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
