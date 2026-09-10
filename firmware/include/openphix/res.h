/* Resource access: the data image in external flash.
 *
 * The image is stored encrypted, exactly as the vendor writes it, and is
 * decrypted on the fly as it is read (docs/data-image-format.md, section
 * 1). On top of that sits the hix container (section 2), possibly nested.
 *
 * A resource is described by a (base, size) window into the flash; reads
 * are always relative to a window so that nested containers and the
 * per-file parsers never need absolute addresses.
 */
#ifndef OPENPHIX_RES_H
#define OPENPHIX_RES_H

#include <stddef.h>
#include <stdint.h>

struct res {
    uint32_t base;   /* absolute flash address of byte 0 */
    uint32_t size;
};

/* Whole data image. Call once at start; returns 0 if the directory at
 * offset 0 is valid. */
int res_init(struct res *image);

/* Read `len` bytes at `off` inside `r`, decrypting. Returns 0 on success,
 * -1 when the range leaves the window. */
int res_read(const struct res *r, uint32_t off, void *buf, size_t len);

/* Convenience readers. Out-of-range reads return 0. */
uint8_t res_u8(const struct res *r, uint32_t off);
uint16_t res_u16(const struct res *r, uint32_t off);
uint32_t res_u32(const struct res *r, uint32_t off);

/* Look a file up by name in the container `dir`. Returns 0 and fills `out`
 * on success. Names compare case-insensitively because the image mixes
 * `.BIN` and `.bin`. */
int res_open(const struct res *dir, const char *name, struct res *out);

/* Enumerate a container: index 0..count-1. Name is copied into `name`
 * (name_cap bytes). Returns 0 on success, -1 past the end. */
int res_entry(const struct res *dir, unsigned index, char *name, size_t name_cap,
              struct res *out);
unsigned res_count(const struct res *dir);

/* A sub-window (no copy, no check beyond clamping). */
struct res res_slice(const struct res *r, uint32_t off, uint32_t size);

/* The cipher itself, exposed for tools and tests. */
uint8_t res_cipher_decrypt_byte(uint8_t c, uint32_t image_offset);
void res_cipher_decrypt(uint8_t *buf, size_t len, uint32_t image_offset);

#endif
