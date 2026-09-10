#include <string.h>

#include "openphix/res.h"
#include "test.h"

int main(int argc, char **argv)
{
    struct res image, f, dir, sub;
    char name[65];
    uint8_t buf[16];

    /* cipher vectors (same as tools/openphix-tool/tests/test_crypto.c) */
    static const uint8_t text_cipher[16] = {
        0x64, 0xdc, 0x28, 0x0f, 0x9d, 0x7c, 0x18, 0x6d,
        0xca, 0xcb, 0x61, 0x57, 0xa1, 0xe3, 0x50, 0x46 };
    memcpy(buf, text_cipher, 16);
    res_cipher_decrypt(buf, 16, 0xafe271);
    CHECK(memcmp(buf, "Gateway_01 Messa", 16) == 0);
    CHECK_EQ(res_cipher_decrypt_byte(0xfc, 0), 0x23);

    if (argc < 2 || hal_stub_load(argv[1]) != 0) {
        fprintf(stderr, "cannot load flash image %s\n", argc > 1 ? argv[1] : "(none)");
        return 1;
    }
    CHECK_EQ(res_init(&image), 0);
    CHECK_EQ(res_count(&image), 34);
    CHECK_EQ(res_entry(&image, 0, name, sizeof name, &f), 0);
    CHECK(strcmp(name, "Cmd.BIN") == 0);
    CHECK_EQ(f.base, 0x2ca);
    CHECK_EQ(f.size, 163253);
    CHECK_EQ(res_open(&image, "menu.bin", &f), 0);
    CHECK_EQ(f.size, 6636);
    CHECK_EQ(res_u32(&f, 0), 0x10b);
    CHECK_EQ(res_open(&image, "LanguageLib.BIN", &f), 0);
    CHECK_EQ(f.size, 9);
    CHECK_EQ(res_u8(&f, 0), 8);
    CHECK_EQ(res_u8(&f, 8), 0x0e);
    CHECK_EQ(res_u8(&f, 9), 0);            /* out of range reads as 0 */
    CHECK_EQ(res_read(&f, 8, buf, 2), -1);

    /* nested container */
    CHECK_EQ(res_open(&image, "STRING.BIN", &dir), 0);
    CHECK_EQ(res_count(&dir), 5);
    CHECK_EQ(res_open(&dir, "string_16_sv.hix", &sub), 0);
    CHECK_EQ(sub.size, 2466646);
    CHECK_EQ(res_entry(&dir, 4, name, sizeof name, &sub), 0);
    CHECK(strcmp(name, "string_17_no.hix") == 0);
    CHECK_EQ(sub.size, 97331);
    CHECK_EQ(res_entry(&dir, 5, name, sizeof name, &sub), -1);
    CHECK_EQ(res_open(&image, "nope.bin", &f), -1);
    TEST_MAIN_END();
}
