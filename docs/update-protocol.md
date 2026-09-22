# USB update protocol of the Autophix DM100 / DM300 scan tools

This documents the protocol between the vendor's Windows update tool
(`Update.exe`, internally "ES910 Update Tool" or "AD410 Update Tool", a
debug build of the Visual Studio project `DM100UpdateCustom`) and the
scan tool. It was derived by decompiling the Autophix 9610 (2022) and
OBD2 SCANZ FST32 (2024) update tools; both contain identical protocol code.
The reference implementation is `tools/openphix-tool`.

## USB level

| Item | Value |
|---|---|
| Vendor / product ids | 0483:5265 (DM100), 0483:5750 (DM300), 2E88:4605 (DM100HC) |
| Driver on Windows | WinUSB, device interface GUID `F70242C7-FB25-443B-9E7E-A4260F373982` |
| Interface | one interface, alternate setting 0, exactly four endpoints |
| Endpoints | bulk IN, bulk OUT, interrupt IN, interrupt OUT (addresses discovered at run time) |
| Device descriptor | requested once to read `idProduct`; 0x5265 selects the DM100 update path, anything else the DM300 path |

Commands go out on the interrupt OUT endpoint, replies come back on the
interrupt IN endpoint. Large data (firmware blocks, flash dumps) uses the
bulk endpoints. The vendor tool sets `PIPE_TRANSFER_TIMEOUT` per transfer;
the values are listed below.

## Packets

### Command packet (16 bytes, interrupt OUT)

```
offset  0   1   2     3 .. 14      15
        55  AA  cmd   parameters   sum8 of bytes 0..14
```

The reply is also 16 bytes. Byte 2 of a positive reply is `cmd + 0x80`.
The vendor tool checks nothing else in a reply (not the header, not the
checksum).

### Data packet (0x1008 bytes, bulk)

```
offset  0..3          4 .. 0x1003             0x1004 .. 0x1007
        55 AA 55 AA   4096 bytes of payload   sum32 of bytes 0..0x1003, big-endian
```

Payload shorter than 4096 bytes is zero padded. The same layout is used in
both directions.

## Commands

| cmd | name | parameters | reply | notes |
|---|---|---|---|---|
| 0x01 | block header | [3..4] length (BE16, 1..0x1000), [5..6] block index (BE16) | 0x81 | followed by one data packet on bulk OUT, then a 16 byte reply is read (not checked) |
| 0x02 | finish | none | 0x82 | sent after the last image; reply ignored |
| 0x03 | begin MCU image | [3..4] block count (BE16) | 0x83 | also used for `Erase.bin` |
| 0x04 | begin external flash image | [3..4] block count (BE16) | 0x84 | |
| 0x06 | read external flash | [3..6] address (LE32), [7..8] length (LE16, 0x1000) | 0x86 | followed by one data packet on bulk IN |
| 0x07 | get version | none | 0x87, C string at [3..] | e.g. `1.30`; compared with `strcmp` against `"1.26"` |
| 0x0A | unknown | | | only appears in the vendor tool's timeout table (120 s), never sent |
| 0x0B | get flash size | none | 0x8B, LE32 at [3..6] | bytes; older firmware does not support it |

Timeouts (ms): command write 1000 (10000 during uploads); reply read 1000 by
default, 20000 for 0x03 and during uploads, 120000 for 0x0A; bulk transfers
10000 (2000 while probing the flash size).

## Update sequence

```
idProduct == 0x5265  ->  DM100 path, image bin/DM100/McuCode.bin
otherwise            ->  DM300 path, image bin/DM300/McuCode.bin
(bin/McuCode.bin is the fallback name in both cases)

DM300 path only:
    0x07 get version
    if no reply, or version <= "1.26" (C string compare):
        upload bin/DM300/Erase.bin (or bin/Erase.bin) as an MCU image (0x03 ...)
        0x02 finish
        sleep 5 s

upload McuCode.bin:      0x03 <blocks>, then per block: 0x01 <len,idx>, data packet, reply
upload ExtFlashDat.bin:  0x04 <blocks>, then per block: 0x01 <len,idx>, data packet, reply
0x02 finish
```

Block count is `ceil(size / 4096)`. Every block except the last is
announced with length 0x1000; the last one with the remaining length, but
the data packet is always 0x1008 bytes. The vendor tool aborts on a missing
or wrong reply to 0x03, 0x04 and 0x01, and does not look at the reply after
the data packet. There is no resume and no verification read-back.

The DM100HC (2E88:4605) takes the DM300 path in both vendor tools examined,
so it receives `bin/DM300/McuCode.bin`.

## Images

`McuCode.bin` (about 280 KB) and `ExtFlashDat.bin` (18 to 23 MB) are sent
as-is; the update tool contains no cipher code. The two images use
different schemes:

- `McuCode.bin` is encrypted with a 16 byte block cipher in ECB mode.
  Identical 16 byte ciphertext blocks repeat where the plaintext repeats
  (the padding at the end is the same block twice, and the same block
  appears in every DM100 and DM300 image across brands, so they share one
  key). Two builds of the same image differ in every byte from offset 0
  on, as expected when the vector table changes slightly and the cipher
  spreads the change over the whole block. The bootloader decrypts it into
  the MCU's internal flash; there is no read command for internal flash.
- `ExtFlashDat.bin` is written to the external flash byte for byte and is
  obfuscated with a byte-wise rotate-and-XOR cipher, which has been
  recovered. See the next section.

## The data image cipher (solved)

`ExtFlashDat.bin` is the plaintext data image passed through a byte-wise
cipher that repeats every 4864 bytes: each byte is rotated left by 0 to 7
bits and XORed with a byte from a 256 byte table `S`. For a byte at offset
`i` in the image,

```
j      = i mod 4864
v      = (j & 0xFF) + (j >> 8)
cipher = rotl8(plain, (v >> 3) & 7) ^ S[v & 0xFF]
plain  = rotr8(cipher ^ S[v & 0xFF], (v >> 3) & 7)
```

`S` begins `df 56 25 4e 87 75 cf 85 05 b7 9f 4c ...` and is listed in full in
`tools/openphix-tool/src/crypto.c`. It holds 158 distinct byte values, so it
is a fixed table rather than the output of a counter or a simple generator.

How it was recovered, in two steps:

1. Parts of the image are unused and their plaintext is all zero. Rotating
   zero changes nothing, so the ciphertext there is `S` itself, laid out as
   19 copies of the table, copy `b` rotated by `b` positions. Those runs are
   visible because they are exactly periodic with 4864 bytes; the longest is
   about 300 KB, which covers every position 60 times over.
2. Removing the XOR alone left a plaintext with readable fragments every
   64 bytes and garbage in between. A statistical scan over the whole image
   (for each of the 4864 positions, which bit rotation most often turns the
   byte into printable ASCII) gave a clear winner at every position, and the
   winners follow the formula above exactly: the rotation steps up every
   8 bytes, and the boundary shifts by one byte per 256 byte block, the
   same shift the XOR table index has.

Checks that it is right and complete:

| Check | Result |
|---|---|
| Entropy of a package image | 7.93 bits/byte before, 6.10 after |
| Zero bytes | 0.4% before, 16% after |
| Printable ASCII | 59% of all bytes after decryption |
| File directory at offset 0 | 34 clean file names with monotonically increasing offsets |
| Images it decrypts | five Autophix 9610 V1.61 language packs, OBD2 SCANZ FST32 V1.60, and a DM100 flash dump |

The same cipher applies to every image seen, across two vendors, two
firmware versions and six language packs, so the key is baked into the
product line rather than derived per build or per device. The plaintext is
a simple file container; its layout and the formats of the files inside are
described in [data-image-format.md](data-image-format.md).

`tools/openphix-tool` implements this as `decrypt` and `encrypt` and as
`read-flash --decrypt`.

## External flash layout (relative to the end of flash)

| Offset from end | Size | Content |
|---|---|---|
| -0x50000 | 0x20000 (32 blocks) | "Feedback" bus log. Empty when the first four bytes are 0xFF. Saved as `Feedback.bin` |
| -0x30000 | 0x1E000 (30 blocks) | DTC review records, one per 4 KiB block |

Flash size comes from 0x0B, or, when unsupported, by attempting a 0x06 read
at `size - 0x1000` for size in 32 MiB, 16 MiB, 8 MiB, 4 MiB and taking the
first one that is acknowledged.

### DTC review record (4 KiB)

```
0x00  "AUTOPHIX"                 magic
0x09  LE32                       sort key (records are sorted ascending)
0x0D  17 bytes                   VIN
0x1E  u8                         number of DTC entries
0x20  entries of 8 bytes:
        [0..1]  text offset within the record (& 0xFFF)
        [2..3]  text length + 1
        [4..5]  DTC code, big-endian: bits 15..14 select P/C/B/U, bits 13..0 are printed as %04X
```

The vendor's "Review & Print" writes them as tab indented text with the VIN
and the DTC count per record.

## Observations on a real DM100 (September 2026)

The unit measured here is a Biltema 15-1375, the Nordic rebrand that gets no
updates from its seller at all. It is ordinary DM100 hardware underneath.

- USB descriptor: manufacturer "Autophix", product "Automotive Diagnostic
  Device", serial "Autophix DM", one configuration, 100 mA. Endpoints:
  0x01/0x81 interrupt, 16 bytes, interval 1 ms; 0x02/0x82 bulk, 64 bytes.
- Every reply from the device starts with `AA 55` (command replies) or
  `AA 55 AA 55` (bulk data packets), the reverse of what the host sends.
  The checksums are computed over the bytes as sent, so they verify.
- Command 0x07 is answered on a DM100 too (the vendor tool only sends it on
  the DM300 path). The unit reported `1.56`.
- Command 0x0B reported 0x2000000 (32 MiB) of external flash.
- The length field of 0x06 is little-endian, like the address. Sending
  `10 00` (big-endian 0x1000) makes the device return 16 bytes of data and
  zero padding in an otherwise normal 0x1008 byte packet.
- A full 0x06 read of the 32 MiB takes about 4 minutes and gave this
  layout on a unit whose data image is older than the V1.60 package (only
  a few percent of the bytes match V1.60 beyond the first megabyte):

  | Range | Content |
  |---|---|
  | 0x0000000 .. 0x13E4F99 | data image (`ExtFlashDat.bin` content, about 20.8 MB) |
  | 0x13E4F9A .. 0x1FAFFFF | erased (0xFF) |
  | 0x1FB0000 .. 0x1FCB127 | feedback log: `AUTOPHIX` header, then back-to-back length-prefixed stored records (LE32 total length; mostly 26 bytes on this unit), followed by erased 0xFF; `Feedback.bin` is exactly this 128 KiB area |
  | 0x1FD0000 .. 0x1FEDFFF | DTC review area, erased on this unit |
  | 0x1FF0000 | settings block, `03 00 01 00 01 00` then 0xFF (probably language index, beeper, startup instructions) |

- Application-written areas are not uniform. DTC review records and
  settings observed so far are plain. Feedback stored-record payloads are
  position encoded: the older format uses the image cipher, while the tested
  AD410 uses the same rotation schedule with a different fixed 256-byte XOR
  table. See section 16 of `data-image-format.md`. The data image itself is
  stored exactly as in the package file, so it is the application, not the
  bootloader, that removes the image cipher when it reads a resource.

## Host side details worth knowing

- The vendor tool reads `LanguageConfig.ini` (`Language` followed by four
  hex digits) from its own directory to pick the UI language; only 0x804
  (Chinese) and 0x409 (English) are supported.
- The Feedback flow opens `note.txt` in Notepad with the e-mail instructions.
- Nothing in the tool checks the device's serial number, brand or the
  language of the package.
