# openphix firmware

The open replacement firmware for the DM100 / DM300 family of VAG scan
tools. It is written from scratch in C99 against the public standards and
against the decoded data image (`docs/data-image-format.md`); no vendor
code is copied. The vendor's own MCU firmware has never been read (its key
is still in the bootloader), so this is a clean-room application that
consumes the vendor's data image, not a port of the vendor's code.

## Layout

```
include/openphix/   public headers of the portable core
core/               the portable core (no hardware access, only the HAL)
  res.c             external flash access, image cipher, hix container
  gfx.c             320x240 RGB565 framebuffer and primitives
  app.c             application entry, developer tools
ports/sim/          PC simulator (Linux, optional X11 window)
tests/              unit tests, run against the flash dump in dumps/
```

Further modules land as their file formats are decoded: fonts and text,
string tables, menu tree, module database, command scripts and the
expression interpreter, the protocol stacks and the UI.

## Build and run the simulator

```
cmake -S . -B build
cmake --build build
ctest --test-dir build
./build/openphix-sim ../dumps/biltema-15-1375-dm100/extflash.bin
```

The simulator takes a raw flash dump or an `ExtFlashDat.bin` (both are
encrypted; the core decrypts on the fly, exactly as the device does) and
opens a window scaled 2x. Keys: arrows, Return (OK), Escape (ESC), `i`
(I/M), `d` (READ DTC), `s` (screenshot to `screenshot.ppm`), `q` (quit).

Headless runs for tests and scripts:

```
./build/openphix-sim image.bin --keys ok,down,down,ok --shot out.ppm
./build/openphix-sim image.bin --tool ls
```

`--tool` runs one of the developer tools built into the core, which print
decoded resources so they can be compared with `tools/hixtool`.

## Hardware ports

None yet. The HAL in `include/openphix/hal.h` is the whole contract a port
has to fulfil: display flush, keys, time, beeper, external flash, supply
voltage, K-line and CAN. The microcontroller has not been identified (see
the roadmap in the top-level README); a port is added under `ports/<mcu>/`
once it is.
