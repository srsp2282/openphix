#include <stdlib.h>
#include <string.h>

#include "../src/crypto.h"
#include "test.h"

/* Real ciphertext taken from bin/ExtFlashDat.bin of the Autophix 9610
 * V1.61 EN/DE/ES package. */
static const uint8_t filler_cipher[16] = {  /* image offset 0x4c0000, all-zero plaintext */
    0xdf, 0x56, 0x25, 0x4e, 0x87, 0x75, 0xcf, 0x85,
    0x05, 0xb7, 0x9f, 0x4c, 0xf5, 0x47, 0x5e, 0x4a };
static const uint8_t text_cipher[16] = {    /* image offset 0xafe271 */
    0x64, 0xdc, 0x28, 0x0f, 0x9d, 0x7c, 0x18, 0x6d,
    0xca, 0xcb, 0x61, 0x57, 0xa1, 0xe3, 0x50, 0x46 };
static const char text_plain[16] = "Gateway_01 Messa";
/* Start of the image: the file directory. Offset 0 has rotation 0, offset
 * 0x100 sits in block 1 where the rotation boundary is shifted by one. */
static const uint8_t dir_cipher[16] = {     /* image offset 0 */
    0xfc, 0x5e, 0x25, 0x0d, 0xea, 0x11, 0xe1, 0xc7,
    0x97, 0x2b, 0x9f, 0x85, 0xf1, 0x47, 0x5e, 0x6a };
static const uint8_t dir_plain[16] = {
    0x23, 0x08, 0x00, 'C', 'm', 'd', '.', 'B', 'I', 'N', 0x00,
    0xe4, 0x02, 0x00, 0x00, 0x10 };
static const uint8_t dir2_cipher[16] = {    /* image offset 0x100 */
    0x09, 0x72, 0x07, 0xc9, 0x2a, 0xfd, 0xb1, 0xf5,
    0xd3, 0xf7, 0x10, 0x31, 0x95, 0x82, 0x4a, 0xfd };
static const uint8_t dir2_plain[16] = {
    '_', 'W', 'I', 'N', '_', '2', '4', 'x', '2', '4', '.', 'b', 'i', 'n', 0x00, 0x9e };

static void test_key_schedule(void)
{
    /* the XOR table is 19 copies of S, copy b rotated left by b positions */
    for (unsigned b = 0; b < OBD_EXT_KEY_ROTATIONS; b++)
        for (unsigned t = 0; t < 256; t++)
            CHECK_EQ(obd_ext_key_byte((uint64_t)b * 256 + t), obd_ext_sbox[(t + b) & 0xFF]);
    /* and both key and rotation repeat every 4864 bytes */
    for (uint64_t o = 0; o < 3000; o += 7) {
        CHECK_EQ(obd_ext_key_byte(o), obd_ext_key_byte(o + OBD_EXT_KEY_PERIOD));
        CHECK_EQ(obd_ext_rotation(o), obd_ext_rotation(o + OBD_EXT_KEY_PERIOD));
    }
    CHECK_EQ(obd_ext_key_byte(0), 0xdf);
    CHECK_EQ(obd_ext_key_byte(255), 0xef);
    CHECK_EQ(obd_ext_key_byte(256), 0x56);
    CHECK_EQ(obd_ext_key_byte(4863), 0x02);
    CHECK_EQ(obd_ext_key_byte(4864), 0xdf);
    CHECK_EQ(obd_ext_key_byte(100000), 0x75);
    CHECK_EQ(obd_ext_key_byte(0xafe271), 0x23);

    /* rotation: (v >> 3) & 7 with v = (j & 0xFF) + (j >> 8) */
    for (unsigned j = 0; j < 256; j++)
        CHECK_EQ(obd_ext_rotation(j), (j >> 3) & 7);
    CHECK_EQ(obd_ext_rotation(0x106), 0);   /* block 1: boundary moves by one */
    CHECK_EQ(obd_ext_rotation(0x107), 1);
    CHECK_EQ(obd_ext_rotation(0x205), 0);   /* block 2: by two */
    CHECK_EQ(obd_ext_rotation(0x206), 1);
    CHECK_EQ(obd_ext_rotation(0x12ff), 2);  /* block 18: v = 0xff + 18 = 0x111 */
    CHECK_EQ(obd_ext_rotation(0xafe271), 0);
}

static void test_known_vectors(void)
{
    uint8_t buf[16];
    memcpy(buf, filler_cipher, sizeof buf);
    obd_ext_decrypt(buf, sizeof buf, 0x4c0000);
    for (size_t i = 0; i < sizeof buf; i++)
        CHECK_EQ(buf[i], 0);

    memcpy(buf, text_cipher, sizeof buf);
    obd_ext_decrypt(buf, sizeof buf, 0xafe271);
    CHECK(memcmp(buf, text_plain, sizeof buf) == 0);

    memcpy(buf, dir_cipher, sizeof buf);
    obd_ext_decrypt(buf, sizeof buf, 0);
    CHECK(memcmp(buf, dir_plain, sizeof buf) == 0);

    memcpy(buf, dir2_cipher, sizeof buf);
    obd_ext_decrypt(buf, sizeof buf, 0x100);
    CHECK(memcmp(buf, dir2_plain, sizeof buf) == 0);

    /* encrypt is the exact inverse */
    obd_ext_encrypt(buf, sizeof buf, 0x100);
    CHECK(memcmp(buf, dir2_cipher, sizeof buf) == 0);
    memcpy(buf, text_plain, sizeof buf);
    obd_ext_encrypt(buf, sizeof buf, 0xafe271);
    CHECK(memcmp(buf, text_cipher, sizeof buf) == 0);
}

static void test_chunking(void)
{
    /* crypting in pieces must equal crypting in one go, at any offset, and
     * a round trip must give the input back */
    uint8_t a[1000], b[1000], orig[1000];
    for (size_t i = 0; i < sizeof a; i++)
        a[i] = b[i] = orig[i] = (uint8_t)(i * 31 + 7);
    uint64_t base = 4864 * 3 + 251;
    obd_ext_encrypt(a, sizeof a, base);
    size_t done = 0;
    while (done < sizeof b) {
        size_t n = (done % 97) + 1;
        if (done + n > sizeof b) n = sizeof b - done;
        obd_ext_encrypt(b + done, n, base + done);
        done += n;
    }
    CHECK(memcmp(a, b, sizeof a) == 0);
    obd_ext_decrypt(a, sizeof a, base);
    CHECK(memcmp(a, orig, sizeof a) == 0);
    CHECK_EQ(obd_ext_zero_fraction((const uint8_t *)"\0\0\0\1", 4), 0.75);
}

int main(void)
{
    test_key_schedule();
    test_known_vectors();
    test_chunking();
    TEST_MAIN_END();
}
