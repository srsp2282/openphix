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
  table.c           the indexed table layout shared by several files
  str.c             string tables, current language with English fall back
  font.c            bitmap fonts and proportional text drawing
  gfx.c             320x240 RGB565 framebuffer and primitives
  exp.c             interpreter for the Exp.BIN expression language
  cmd.c             Cmd.BIN command records (CAN frames, K-line chunks)
  dtc.c             fault code and measuring block text lookup
  sysscan.c         the control module database (SYSSCAN.BIN)
  menu.c            the For VW menu tree (Menu.BIN)
  funcfg.c          per module function configuration (FUNCFG.BIN)
  ui.c              list widget and the screens
  app.c             boot, main loop, developer tools
ports/sim/          PC simulator (Linux, optional X11 window)
tests/              unit tests, run against the flash dump in dumps/
```

What runs today: boot from the encrypted flash image, the eight icon main
menu, the For VW tree with System Selection down to the per module
function menu (built from the module's FUNCFG record), the special
function lists, OBDII and Tool Setup menus, language switching, both
skins, battery voltage. Every function that needs the vehicle bus ends in
the stock "Device unable to communicate" screen, because the protocol
stacks (ISO 15765 / UDS, TP2.0, KWP2000, KWP1281) and the hardware port do
not exist yet. The data behind them (connect sequences, command bytes,
response expressions, text ids) is already reachable through the core
modules; see `docs/data-image-format.md`.

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
decoded resources so they can be compared with `tools/hixtool`: `ls`,
`str <id>...`, `text [string]`, `exp <id> [bytes]`, `cmd <id>`,
`modules`, `dtc <code>...`.

## Hardware ports

None yet. The HAL in `include/openphix/hal.h` is the whole contract a port
has to fulfil: display flush, keys, time, beeper, external flash, supply
voltage, K-line and CAN. The microcontroller has not been identified (see
the roadmap in the top-level README); a port is added under `ports/<mcu>/`
once it is.
