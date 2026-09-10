# keysearch

Recovers the `McuCode.bin` key from a dump of the scan tool's
microcontroller flash, and verifies a candidate key by decrypting a real
vendor image.

The MCU images are encrypted with a 128-bit-block cipher in ECB mode. The key
is not in the update packages: about 300 million candidates drawn from the
vendor tools, the decrypted data image, brand strings and the well-known
cipher test vectors were tried against eight cipher families with no hit. The
key is in the device bootloader, so this tool operates on a flash dump taken
over SWD. See `docs/mcu-key-extraction.md` for how to get that dump.

How the search works: several 16-byte ciphertext blocks repeat in every
vendor MCU image, across both hardware revisions and both vendors. They are
the encryptions of constant plaintext, so a candidate key is correct when
encrypting an all-zero or all-0xFF block reproduces one of them. Every 16, 24
and 32-byte window of the dump is tried, for each cipher, in both directions.

```
make
./keysearch search mcu-flash.bin
./keysearch verify AES-128-ECB <hexkey> path/to/McuCode.bin
```

Ciphers tried: AES-128/192/256, SM4, Camellia-128/256, SEED and ARIA-128.
Needs `libssl-dev`.

`verify` writes the decrypted image alongside the input as `<name>.plain` and
reports whether it looks like a Cortex-M binary.
