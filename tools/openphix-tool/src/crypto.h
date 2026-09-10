/* Obfuscation of the external flash data image (ExtFlashDat.bin).
 *
 * The image is XORed with a repeating 4864 byte keystream. The keystream is
 * built from a single 256 byte table: the keystream is 19 consecutive copies
 * of that table, copy number b rotated left by b bytes. In other words
 *
 *     j = offset mod 4864
 *     key(offset) = S[((j & 0xFF) + (j >> 8)) & 0xFF]
 *
 * The keystream was recovered from regions of the image whose plaintext is
 * all zero, and verified byte for byte against seven independent images:
 * five Autophix 9610 V1.61 language packs, the OBD2 SCANZ FST32 V1.60
 * package, and a flash dump of a real DM100. See docs/update-protocol.md.
 *
 * The MCU image (McuCode.bin) uses a different, unrelated scheme (a 16 byte
 * block cipher in ECB mode) whose key lives in the device bootloader and is
 * not recoverable from the update packages.
 */
#ifndef OPENPHIX_CRYPTO_H
#define OPENPHIX_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

#define OBD_EXT_KEY_PERIOD 4864u
#define OBD_EXT_KEY_ROTATIONS 19u

/* The 256 byte table the keystream is built from. */
extern const uint8_t obd_ext_sbox[256];

/* Keystream byte for a given byte offset into the data image. */
uint8_t obd_ext_key_byte(uint64_t offset);

/* XOR len bytes in place. The transform is its own inverse, so this both
 * decrypts and encrypts; offset is the position of buf[0] in the image. */
void obd_ext_crypt(uint8_t *buf, size_t len, uint64_t offset);

/* Fraction of zero bytes, used to tell an encrypted image (about 0.4%) from
 * a decrypted one (about 16%). */
double obd_ext_zero_fraction(const uint8_t *buf, size_t len);

#endif
