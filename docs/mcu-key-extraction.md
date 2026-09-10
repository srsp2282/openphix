# Getting the MCU image key off the hardware

The data image (`ExtFlashDat.bin`) is already solved: it is a repeating XOR
keystream and `openphix-tool decrypt` handles it. The MCU image
(`McuCode.bin`) is a different matter. It uses a 128-bit-block cipher in ECB
mode whose key is not in the update packages, not in the Windows update tool
and not derivable from the ciphertext. Roughly 300 million candidate keys
were tried against eight cipher families with no result, so the only place
left is the device itself.

The key lives in the bootloader, in the microcontroller's internal flash.
That flash is not reachable over USB: the update protocol can read the
external SPI flash but has no command for internal flash. It is reachable
over SWD, the ARM debug port, if the chip's readout protection allows it.

The prize is bigger than the key. A successful dump gives you the bootloader
and the currently running application, both in plaintext. That is the actual
firmware, which is what the project needs to understand the protocol stacks
and the resource format. The key is the bonus that makes it possible to keep
shipping updates through the vendor's own USB path.

Because the key is identical across DM100, DM300 and DM100HC, across Autophix
and OBD2 SCANZ, and across every firmware version seen since 2019, one
successful extraction unlocks the entire product line permanently.

## What you need

- An SWD probe. An ST-Link V2 clone costs a few units of currency and works
  with both tools below. A J-Link, a CMSIS-DAP probe, or a Raspberry Pi Pico
  flashed with the `debugprobe` firmware all work equally well.
- Jumper wires, and ideally a fine-tipped multimeter for continuity testing.
- `openocd` and `stlink-tools`, both already installed on this machine.

## Step 1: identify the microcontroller

Open the case and photograph the board on both sides. Read the marking on
the largest chip next to the OBD connector. Everything we know so far says
STM32 or an STM32-compatible part: the USB vendor ID is ST's, the DM300's
product ID sits inside ST's own USB example range, and the device runs at USB
full speed.

You do not strictly need the marking. Once the probe is attached, the chip
identifies itself:

```
st-info --probe
```

That prints the chip ID, the flash size and the SRAM size, which pins down
the exact family and settles the question for the repository.

## Step 2: find the SWD pads

You need four signals: `SWDIO`, `SWCLK`, `GND` and, optionally, `NRST`. On
boards like this they are usually a row of unpopulated pads or test points
near the microcontroller.

On every STM32, `SWDIO` is pin `PA13` and `SWCLK` is pin `PA14`. Put the
multimeter in continuity mode, put one probe on those pins of the chip, and
find which pad they connect to. Take `GND` from the USB shield or any large
ground pour.

## Step 3: connect

Power the tool from its own USB port and connect only `SWDIO`, `SWCLK` and
`GND` from the probe. Do not connect the probe's 3.3 V line to a board that
is already powered. Never apply the OBD connector's 12 V while the probe is
attached.

## Step 4: read the readout protection level

This is the decision point and it is a read-only operation.

```
st-flash --area=option read
```

Or with OpenOCD, reading the option bytes directly (`stm32f4x.cfg` here;
substitute the config for the family you found in step 1):

```
openocd -f interface/stlink.cfg -c "transport select hla_swd" \
        -f target/stm32f4x.cfg \
        -c "init; reset halt; mdw 0x40023C14; shutdown"
```

The readout protection byte tells you which case you are in.

| Level | Symptom | What you can do |
|---|---|---|
| 0 | debugger attaches, flash reads fine | dump everything, go to step 5 |
| 1 | debugger attaches, flash reads fail or return zeros | see below |
| 2 | debugger does not attach at all | not practical |

The quickest check of all is simply to try reading one word of flash. If
`mdw 0x08000000 4` returns plausible values, you are at level 0.

Consumer devices very often ship at level 0 because nobody changed the
default. This vendor left the update path unsigned, chose ECB, and protected
the data image with a repeating XOR, so the odds here are good.

## Step 5: dump the internal flash

Size the dump to the flash the chip reported in step 1, commonly 512 KiB
(`0x80000`) or 1 MiB (`0x100000`).

```
st-flash read mcu-flash.bin 0x08000000 0x100000
```

Or with OpenOCD:

```
openocd -f interface/stlink.cfg -c "transport select hla_swd" \
        -f target/stm32f4x.cfg \
        -c "init; reset halt; dump_image mcu-flash.bin 0x08000000 0x100000; shutdown"
```

Take the dump twice and compare the two files. If they differ, the read is
unreliable and the wiring or clock speed needs attention.

## Step 6: find the key in the dump

`tools/keysearch` does this. Several 16-byte ciphertext blocks repeat in
every vendor MCU image, and they are the encryptions of constant plaintext.
A candidate key is correct when encrypting an all-zero or all-0xFF block
reproduces one of them. The tool tries every 16, 24 and 32-byte window of
the dump against eight ciphers in both directions.

```
cd tools/keysearch && make
./keysearch search ../../mcu-flash.bin
```

On a 1 MiB dump this takes a few seconds. A hit prints the cipher, the key
and the offset in the dump where it was stored.

## Step 7: verify

Confirm the key by decrypting a real vendor image and checking that the
result is a Cortex-M binary, meaning an initial stack pointer into SRAM and a
reset vector into flash with the Thumb bit set.

```
./keysearch verify AES-128-ECB <hexkey> \
    ../../vendor-files/autophix-9610/…/bin/DM100/McuCode.bin
```

It writes the decrypted image next to the input as `McuCode.bin.plain`.

If `search` finds nothing, the key is not stored as a contiguous run of
bytes. It may be assembled at run time, held in a hardware key register, or
the cipher may be outside the eight tried. At that point the job becomes
locating the crypto routine in the bootloader by hand: look for the AES
tables, or for writes to the chip's cryptographic peripheral, and read back
what gets loaded as the key.

## If protection is at level 1

Flash reads are blocked. Your options, in order of sanity:

- Lowering protection back to level 0 triggers a mass erase. That wipes the
  bootloader and the key with it, so it gains you nothing here.
- Published research covers protection bypasses on several STM32 families,
  using debug-interface timing or voltage glitching. These need extra
  equipment and a willingness to lose the board.
- Buying a second unit to experiment on is usually cheaper than the time
  spent on the alternatives.

Whatever you try, do it on a spare unit and not on the one you actually use.

## Rules that keep the device alive

- Never run a mass erase, `unlock`, or any `flash write`, `flash erase` or
  `program` command. The bootloader is not recoverable: it is the only thing
  that can accept a USB update, and no copy of it exists anywhere.
- Do not change option bytes at all.
- Do not connect probe power to an already-powered board.
- Reading is safe. Everything in steps 4 to 5 only reads.
