/* keysearch: recover the MCU image key from a microcontroller flash dump.
 *
 * The MCU images (McuCode.bin) in the vendor update packages are encrypted
 * with a 128-bit-block cipher in ECB mode. The key is not in the update
 * packages; it lives in the device bootloader, in the microcontroller's
 * internal flash. Once that flash has been dumped over SWD, this tool finds
 * the key inside it.
 *
 * How it works: several 16-byte ciphertext blocks repeat in every MCU image
 * ever seen, from both hardware revisions and both vendors. Those are the
 * encryptions of constant plaintext, almost certainly all-zero and all-0xFF
 * runs. So a candidate key is correct if encrypting (or decrypting) one of
 * those constants reproduces one of the known blocks. Every 16, 24 and 32
 * byte window of the dump is tried as a candidate, for several ciphers, in
 * both directions. On a 1 MB dump that is a few seconds of work.
 *
 * Build:  make          (needs libssl-dev)
 * Search: ./keysearch search mcu-flash.bin
 * Verify: ./keysearch verify AES-128-ECB <hexkey> path/to/McuCode.bin
 */
#include <openssl/evp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 16-byte ciphertext blocks that repeat inside the vendor MCU images.
 * The first is present in all twelve images examined, 152 times in total. */
static const char *const known_hex[] = {
    "20f9b5485e0e567b546552f60c46df30", "b6b9f9cac0686308bc4c3a46c3158406",
    "c7c03372728f37ae699330af67069de5", "f830d3f27c941a5cf541c3f41b3f073f",
    "eec3f4e865179dd0932eef7915ca2b73", "6051030177dac9d2d1a9fe6924def5f6",
    "35ef81d7bf20d8e48eefe1b9b8ff7652", "0cd158e3ebcae6c87fbc29726541c201",
};
#define NKNOWN (sizeof known_hex / sizeof known_hex[0])
static uint8_t known[NKNOWN][16];

/* Ciphers with a 128-bit block that a vendor might plausibly use. */
static const struct { const char *name; int keylen; } ciphers[] = {
    {"AES-128-ECB", 16}, {"AES-192-ECB", 24}, {"AES-256-ECB", 32},
    {"SM4-ECB", 16}, {"CAMELLIA-128-ECB", 16}, {"CAMELLIA-256-ECB", 32},
    {"SEED-ECB", 16}, {"ARIA-128-ECB", 16},
};
#define NCIPHERS (sizeof ciphers / sizeof ciphers[0])

static const uint8_t plains[2][16] = {
    {0}, {255,255,255,255,255,255,255,255,255,255,255,255,255,255,255,255}
};

static void load_known(void)
{
    for (size_t i = 0; i < NKNOWN; i++)
        for (int j = 0; j < 16; j++) {
            unsigned v;
            sscanf(known_hex[i] + 2 * j, "%2x", &v);
            known[i][j] = (uint8_t)v;
        }
}

static int match_known(const uint8_t *b)
{
    for (size_t i = 0; i < NKNOWN; i++)
        if (memcmp(b, known[i], 16) == 0)
            return (int)i;
    return -1;
}

static uint8_t *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long n;
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); fprintf(stderr, "%s is empty\n", path); return NULL; }
    buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "cannot read %s\n", path); free(buf); fclose(f); return NULL;
    }
    fclose(f);
    *len = (size_t)n;
    return buf;
}

static void print_hex(const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) printf("%02x", b[i]);
}

static int cmd_search(const char *path)
{
    size_t len;
    uint8_t *buf = slurp(path, &len);
    uint8_t out[64];
    int ol, hits = 0;
    if (!buf) return 2;
    printf("searching %s (%zu bytes) for the MCU image key\n", path, len);
    for (size_t ci = 0; ci < NCIPHERS; ci++) {
        const EVP_CIPHER *c = EVP_get_cipherbyname(ciphers[ci].name);
        int kl = ciphers[ci].keylen;
        EVP_CIPHER_CTX *e, *d;
        if (!c) { printf("  %-18s not supported by this OpenSSL, skipped\n", ciphers[ci].name); continue; }
        if (len < (size_t)kl) continue;
        e = EVP_CIPHER_CTX_new(); d = EVP_CIPHER_CTX_new();
        EVP_EncryptInit_ex(e, c, NULL, NULL, NULL); EVP_CIPHER_CTX_set_padding(e, 0);
        EVP_DecryptInit_ex(d, c, NULL, NULL, NULL); EVP_CIPHER_CTX_set_padding(d, 0);
        for (size_t off = 0; off + (size_t)kl <= len; off++) {
            EVP_EncryptInit_ex(e, NULL, NULL, buf + off, NULL);
            EVP_DecryptInit_ex(d, NULL, NULL, buf + off, NULL);
            for (int p = 0; p < 2; p++) {
                int t;
                EVP_EncryptUpdate(e, out, &ol, plains[p], 16);
                if ((t = match_known(out)) >= 0) {
                    printf("\n  *** KEY FOUND ***\n  cipher %s, image = encrypt(plaintext)\n",
                           ciphers[ci].name);
                    printf("  key at dump offset 0x%zx: ", off); print_hex(buf + off, kl);
                    printf("\n  constant plaintext 0x%02x maps to known block %d\n", plains[p][0], t);
                    hits++;
                }
                EVP_DecryptUpdate(d, out, &ol, plains[p], 16);
                if ((t = match_known(out)) >= 0) {
                    printf("\n  *** KEY FOUND ***\n  cipher %s, image = decrypt(plaintext)\n",
                           ciphers[ci].name);
                    printf("  key at dump offset 0x%zx: ", off); print_hex(buf + off, kl);
                    printf("\n  constant plaintext 0x%02x maps to known block %d\n", plains[p][0], t);
                    hits++;
                }
            }
        }
        EVP_CIPHER_CTX_free(e); EVP_CIPHER_CTX_free(d);
        printf("  %-18s %zu windows, %d hits\n", ciphers[ci].name, len - kl + 1, hits);
    }
    free(buf);
    if (!hits) {
        printf("\nno key found. The key may be split, derived at run time, held in a\n"
               "hardware key register, or the cipher may not be one of the eight tried.\n"
               "Locating the crypto routine in the bootloader by hand is the next step.\n");
        return 1;
    }
    return 0;
}

/* Decrypt an McuCode.bin with a candidate key and report whether the result
 * looks like a Cortex-M image: a stack pointer into SRAM and a reset vector
 * into flash with the Thumb bit set. */
static int cmd_verify(const char *cipher_name, const char *keyhex, const char *image)
{
    const EVP_CIPHER *c = EVP_get_cipherbyname(cipher_name);
    size_t klen = strlen(keyhex) / 2, len;
    uint8_t key[32], *buf, *out;
    EVP_CIPHER_CTX *ctx;
    int ol, ok = 0;
    uint32_t sp, reset;
    if (!c) { fprintf(stderr, "unknown cipher %s\n", cipher_name); return 2; }
    if (klen == 0 || klen > sizeof key) { fprintf(stderr, "bad key length\n"); return 2; }
    for (size_t i = 0; i < klen; i++) {
        unsigned v;
        if (sscanf(keyhex + 2 * i, "%2x", &v) != 1) { fprintf(stderr, "bad key hex\n"); return 2; }
        key[i] = (uint8_t)v;
    }
    if (!(buf = slurp(image, &len))) return 2;
    len -= len % 16;
    out = malloc(len + 16);
    ctx = EVP_CIPHER_CTX_new();
    EVP_DecryptInit_ex(ctx, c, NULL, key, NULL);
    EVP_CIPHER_CTX_set_padding(ctx, 0);
    EVP_DecryptUpdate(ctx, out, &ol, buf, (int)len);
    EVP_CIPHER_CTX_free(ctx);
    sp = (uint32_t)out[0] | ((uint32_t)out[1] << 8) | ((uint32_t)out[2] << 16) | ((uint32_t)out[3] << 24);
    reset = (uint32_t)out[4] | ((uint32_t)out[5] << 8) | ((uint32_t)out[6] << 16) | ((uint32_t)out[7] << 24);
    printf("first 32 bytes decrypted: "); print_hex(out, 32); printf("\n");
    printf("initial stack pointer 0x%08x, reset vector 0x%08x\n", sp, reset);
    if (sp >= 0x20000000 && sp <= 0x20050000) { printf("  stack pointer points into SRAM, good\n"); ok++; }
    if ((reset & 0xFFF00000) == 0x08000000 && (reset & 1)) { printf("  reset vector points into flash with the Thumb bit, good\n"); ok++; }
    {
        size_t zeros = 0;
        for (size_t i = 0; i < len; i++) if (out[i] == 0) zeros++;
        printf("  zero bytes %.2f%% (encrypted input was about 0.4%%)\n", 100.0 * (double)zeros / (double)len);
        if ((double)zeros / (double)len > 0.03) ok++;
    }
    {
        char path[512];
        snprintf(path, sizeof path, "%s.plain", image);
        FILE *f = fopen(path, "wb");
        if (f) { fwrite(out, 1, len, f); fclose(f); printf("wrote %s\n", path); }
    }
    printf(ok >= 2 ? "\nlooks like a real Cortex-M image: the key is right\n"
                   : "\ndoes not look like firmware: wrong key or wrong cipher\n");
    free(buf); free(out);
    return ok >= 2 ? 0 : 1;
}

int main(int argc, char **argv)
{
    load_known();
    if (argc >= 3 && strcmp(argv[1], "search") == 0)
        return cmd_search(argv[2]);
    if (argc >= 5 && strcmp(argv[1], "verify") == 0)
        return cmd_verify(argv[2], argv[3], argv[4]);
    fprintf(stderr,
        "usage:\n"
        "  keysearch search <mcu-flash-dump.bin>\n"
        "  keysearch verify <cipher> <hexkey> <McuCode.bin>\n"
        "\n"
        "ciphers: AES-128-ECB AES-192-ECB AES-256-ECB SM4-ECB\n"
        "         CAMELLIA-128-ECB CAMELLIA-256-ECB SEED-ECB ARIA-128-ECB\n");
    return 2;
}
