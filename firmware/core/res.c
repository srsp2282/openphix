#include <string.h>

#include "openphix/hal.h"
#include "openphix/res.h"

/* ---- cipher (docs/data-image-format.md, section 1) --------------------- */

#define PERIOD 4864u

static const uint8_t sbox[256] = {
    0xdf, 0x56, 0x25, 0x4e, 0x87, 0x75, 0xcf, 0x85, 0x05, 0xb7, 0x9f, 0x4c,
    0xf5, 0x47, 0x5e, 0x4a, 0x87, 0x02, 0x74, 0x1c, 0xe9, 0xf0, 0x46, 0xcf,
    0xe0, 0x2b, 0x81, 0x58, 0x99, 0x1a, 0x8b, 0xef, 0x78, 0xc0, 0xef, 0x4d,
    0x1c, 0xb5, 0xd5, 0x51, 0x65, 0xfb, 0xf9, 0xe0, 0xf6, 0x0a, 0x0f, 0x4a,
    0xd0, 0x2a, 0x70, 0xf2, 0xc6, 0xe3, 0xfe, 0xe3, 0xdf, 0x8b, 0xa2, 0xa7,
    0xf3, 0xd4, 0xd7, 0x75, 0xc5, 0x40, 0x9f, 0x41, 0x8a, 0xd4, 0x74, 0x47,
    0x56, 0x34, 0xd4, 0xae, 0xca, 0xd4, 0x7b, 0x97, 0xff, 0x00, 0x48, 0x2b,
    0x3d, 0xa1, 0x48, 0x85, 0xff, 0xa1, 0xc2, 0xae, 0x88, 0x87, 0x6c, 0xc1,
    0x12, 0x85, 0x1d, 0xb3, 0xae, 0x8c, 0xd5, 0x34, 0x81, 0x7e, 0xec, 0xf7,
    0xa1, 0x1b, 0xc8, 0x17, 0x67, 0xbc, 0xf5, 0x6a, 0x87, 0xeb, 0xc7, 0xa4,
    0xb1, 0x12, 0x19, 0x24, 0x5c, 0xff, 0xa9, 0x87, 0x23, 0xbd, 0x5c, 0x6a,
    0xea, 0x1d, 0x61, 0x32, 0xaa, 0xa9, 0x21, 0xcd, 0x6b, 0x05, 0xb6, 0x84,
    0x96, 0x97, 0x6b, 0x4c, 0x42, 0x31, 0xa9, 0x25, 0xa3, 0x7a, 0xdc, 0x72,
    0x2f, 0xf5, 0x4a, 0xb4, 0x45, 0x7d, 0x80, 0x87, 0x19, 0xcb, 0x6c, 0xc3,
    0xf1, 0x84, 0x75, 0xae, 0x0d, 0x0a, 0x88, 0x32, 0x28, 0x50, 0x78, 0xab,
    0xab, 0x0a, 0xce, 0xeb, 0x5c, 0x0f, 0x2d, 0xc7, 0xcf, 0x8a, 0x5b, 0x7b,
    0x3b, 0x0e, 0x9d, 0x0a, 0xe8, 0x8d, 0xd4, 0x88, 0x82, 0x94, 0x03, 0x26,
    0x56, 0x54, 0x90, 0x99, 0x58, 0xb5, 0xd6, 0x88, 0xfd, 0xa4, 0x6b, 0xb3,
    0xcd, 0xbf, 0xc9, 0xad, 0x77, 0x67, 0x27, 0xb2, 0x16, 0xbc, 0x2e, 0xbc,
    0x5f, 0x87, 0x5d, 0xf3, 0x7e, 0x16, 0x5b, 0x2d, 0x01, 0x1f, 0x36, 0x63,
    0x63, 0xf4, 0x87, 0x06, 0xc2, 0x0e, 0xa3, 0xc9, 0xff, 0xa1, 0x7f, 0xb2,
    0x6d, 0x95, 0x8c, 0xef,
};

uint8_t res_cipher_decrypt_byte(uint8_t c, uint32_t image_offset)
{
    unsigned j = image_offset % PERIOD;
    unsigned v = (j & 0xFFu) + (j >> 8);
    unsigned r = (v >> 3) & 7u;
    uint8_t x = c ^ sbox[v & 0xFFu];
    return r ? (uint8_t)((x >> r) | (x << (8 - r))) : x;
}

void res_cipher_decrypt(uint8_t *buf, size_t len, uint32_t image_offset)
{
    unsigned j = image_offset % PERIOD;
    for (size_t i = 0; i < len; i++) {
        unsigned v = (j & 0xFFu) + (j >> 8);
        unsigned r = (v >> 3) & 7u;
        uint8_t x = buf[i] ^ sbox[v & 0xFFu];
        buf[i] = r ? (uint8_t)((x >> r) | (x << (8 - r))) : x;
        if (++j == PERIOD)
            j = 0;
    }
}

/* ---- windows ----------------------------------------------------------- */

int res_read(const struct res *r, uint32_t off, void *buf, size_t len)
{
    if (off > r->size || len > r->size - off)
        return -1;
    if (hal_flash_read(r->base + off, buf, len) != 0)
        return -1;
    res_cipher_decrypt((uint8_t *)buf, len, r->base + off);
    return 0;
}

uint8_t res_u8(const struct res *r, uint32_t off)
{
    uint8_t b;
    return res_read(r, off, &b, 1) == 0 ? b : 0;
}

uint16_t res_u16(const struct res *r, uint32_t off)
{
    uint8_t b[2];
    if (res_read(r, off, b, 2) != 0)
        return 0;
    return (uint16_t)(b[0] | (b[1] << 8));
}

uint32_t res_u32(const struct res *r, uint32_t off)
{
    uint8_t b[4];
    if (res_read(r, off, b, 4) != 0)
        return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
           ((uint32_t)b[3] << 24);
}

struct res res_slice(const struct res *r, uint32_t off, uint32_t size)
{
    struct res s;
    if (off > r->size)
        off = r->size;
    if (size > r->size - off)
        size = r->size - off;
    s.base = r->base + off;
    s.size = size;
    return s;
}

/* ---- hix container (section 2) ---------------------------------------- */

static int ieq(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = *a, cb = *b;
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb)
            return 0;
    }
    return *a == *b;
}

unsigned res_count(const struct res *dir)
{
    return dir->size ? res_u8(dir, 0) : 0;
}

/* Walk the directory to entry `index`; fills name/out and the offset of
 * the following entry's data (for the size). */
int res_entry(const struct res *dir, unsigned index, char *name, size_t name_cap,
              struct res *out)
{
    unsigned count = res_count(dir);
    uint32_t p = 1, off = 0, next = dir->size;
    if (index >= count)
        return -1;
    for (unsigned k = 0; k <= index + 1 && k < count; k++) {
        uint16_t len = res_u16(dir, p);
        uint32_t o;
        if (len == 0 || len > 64 || p + 2 + len + 4 > dir->size)
            return -1;
        if (k == index && name) {
            size_t n = len < name_cap ? len : name_cap;
            res_read(dir, p + 2, name, n);
            name[name_cap - 1] = 0;
            if (n < name_cap)
                name[n] = 0;
        }
        o = res_u32(dir, p + 2 + len);
        if (k == index)
            off = o;
        else if (k == index + 1)
            next = o;
        p += 2 + len + 4;
    }
    if (off > dir->size || next < off || next > dir->size)
        return -1;
    if (out) {
        out->base = dir->base + off;
        out->size = next - off;
    }
    return 0;
}

int res_open(const struct res *dir, const char *name, struct res *out)
{
    char n[65];
    unsigned count = res_count(dir);
    for (unsigned i = 0; i < count; i++) {
        if (res_entry(dir, i, n, sizeof n, out) != 0)
            return -1;
        if (ieq(n, name))
            return 0;
    }
    return -1;
}

int res_init(struct res *image)
{
    struct res first;
    image->base = 0;
    image->size = hal_flash_size();
    if (res_count(image) == 0 || res_entry(image, 0, NULL, 0, &first) != 0)
        return -1;
    /* the image ends where the last file ends; the directory has no total
     * size, so use the flash size as the outer bound */
    return 0;
}
