/* Obfuscation of the external flash data image (ExtFlashDat.bin).
 *
 * Every byte is bit-rotated and XORed with a byte from a 256 byte table S.
 * Both the rotation amount and the table index come from one "virtual
 * position" v that repeats every 4864 bytes (19 blocks of 256):
 *
 *     j = offset mod 4864
 *     v = (j & 0xFF) + (j >> 8)
 *     cipher = rotl(plain, (v >> 3) & 7) ^ S[v & 0xFF]
 *     plain  = rotr(cipher ^ S[v & 0xFF], (v >> 3) & 7)
 *
 * The XOR table was recovered from regions of the image whose plaintext is
 * all zero (rotating zero changes nothing, so those regions expose S
 * directly). The rotation was found afterwards from the statistics of the
 * text-heavy parts of the image: for every one of the 4864 positions, one
 * rotation makes far more bytes land in printable ASCII than any other, and
 * the winning amounts follow the formula above exactly. Verified on the
 * five Autophix 9610 V1.61 language packs, the OBD2 SCANZ FST32 V1.60
 * package and a flash dump of a real DM100: the file directory at the start
 * of the image and every string inside it come out clean. See
 * docs/data-image-format.md.
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

/* XOR table byte for a given byte offset into the data image. */
uint8_t obd_ext_key_byte(uint64_t offset);

/* Left rotation amount (0..7) for a given byte offset into the data image. */
unsigned obd_ext_rotation(uint64_t offset);

/* Decrypt or encrypt len bytes in place; offset is the position of buf[0]
 * in the image. The two are not the same operation: the rotation direction
 * differs. */
void obd_ext_decrypt(uint8_t *buf, size_t len, uint64_t offset);
void obd_ext_encrypt(uint8_t *buf, size_t len, uint64_t offset);

/* Fraction of zero bytes, used to tell an encrypted image (about 0.4%) from
 * a decrypted one (about 16%). */
double obd_ext_zero_fraction(const uint8_t *buf, size_t len);

#endif
