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
static const uint8_t text_plain[16] = {
    0x47, 0x61, 0x74, 0x65, 0x77, 0x61, 0x79, 0x5f,
    0x60, 0x62, 0x40, 0x9a, 0xca, 0xe6, 0xe6, 0xc2 };

static void test_key_schedule(void)
{
    /* keystream is 19 copies of the S table, copy b rotated left by b */
    for (unsigned b = 0; b < OBD_EXT_KEY_ROTATIONS; b++)
        for (unsigned t = 0; t < 256; t++)
            CHECK_EQ(obd_ext_key_byte((uint64_t)b * 256 + t), obd_ext_sbox[(t + b) & 0xFF]);
    /* and it repeats every 4864 bytes */
    for (uint64_t o = 0; o < 3000; o += 7)
        CHECK_EQ(obd_ext_key_byte(o), obd_ext_key_byte(o + OBD_EXT_KEY_PERIOD));
    CHECK_EQ(obd_ext_key_byte(0), 0xdf);
    CHECK_EQ(obd_ext_key_byte(255), 0xef);
    CHECK_EQ(obd_ext_key_byte(256), 0x56);
    CHECK_EQ(obd_ext_key_byte(4863), 0x02);
    CHECK_EQ(obd_ext_key_byte(4864), 0xdf);
    CHECK_EQ(obd_ext_key_byte(100000), 0x75);
    CHECK_EQ(obd_ext_key_byte(0xafe271), 0x23);
}

static void test_known_vectors(void)
{
    uint8_t buf[16];
    memcpy(buf, filler_cipher, sizeof buf);
    obd_ext_crypt(buf, sizeof buf, 0x4c0000);
    for (size_t i = 0; i < sizeof buf; i++)
        CHECK_EQ(buf[i], 0);

    memcpy(buf, text_cipher, sizeof buf);
    obd_ext_crypt(buf, sizeof buf, 0xafe271);
    CHECK(memcmp(buf, text_plain, sizeof buf) == 0);
    CHECK(memcmp(buf, "Gateway_", 8) == 0);

    /* the transform is its own inverse */
    obd_ext_crypt(buf, sizeof buf, 0xafe271);
    CHECK(memcmp(buf, text_cipher, sizeof buf) == 0);
}

static void test_chunking(void)
{
    /* crypting in pieces must equal crypting in one go, at any offset */
    uint8_t a[1000], b[1000];
    for (size_t i = 0; i < sizeof a; i++)
        a[i] = b[i] = (uint8_t)(i * 31 + 7);
    uint64_t base = 4864 * 3 + 251;
    obd_ext_crypt(a, sizeof a, base);
    size_t done = 0;
    while (done < sizeof b) {
        size_t n = (done % 97) + 1;
        if (done + n > sizeof b) n = sizeof b - done;
        obd_ext_crypt(b + done, n, base + done);
        done += n;
    }
    CHECK(memcmp(a, b, sizeof a) == 0);
    CHECK_EQ(obd_ext_zero_fraction((const uint8_t *)"\0\0\0\1", 4), 0.75);
}

int main(void)
{
    test_key_schedule();
    test_known_vectors();
    test_chunking();
    TEST_MAIN_END();
}
