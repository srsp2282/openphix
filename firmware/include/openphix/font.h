/* Bitmap fonts from the data image (docs/data-image-format.md, section 5)
 * and text drawing. Text is in the single byte code page of the current
 * language; glyph codes below 0x20 draw the arrows and degree symbols. */
#ifndef OPENPHIX_FONT_H
#define OPENPHIX_FONT_H

#include <stdint.h>

#include "openphix/res.h"

struct font {
    struct res data;      /* glyph bitmaps */
    uint8_t width, height;
    uint8_t row_bytes;    /* ceil(width / 8) */
    uint8_t pitch;        /* advance per character when drawing */
    uint16_t glyph_bytes;
    uint16_t count;       /* 256, or 8178 for the GB2312 fonts */
};

enum font_id {
    FONT_8X16,    /* narrow, fixed pitch */
    FONT_16X16,   /* the normal UI font */
    FONT_20X20,
    FONT_24X24,   /* titles */
    FONT_20X32,   /* large values */
    FONT_COUNT
};

/* Load the font set for a code page family: "WESTISO", "RUSS_ISO" or
 * "POL_WIN". Sizes that the family lacks fall back to the nearest one.
 * Returns 0 on success. */
int font_load(const struct res *image, const char *family);
const struct font *font_get(enum font_id id);

/* Draw one glyph / a string at (x, y) with foreground fg. If bg is
 * FONT_TRANSPARENT the background is left alone. Returns the x after the
 * last glyph. */
#define FONT_TRANSPARENT 0xFFFFFFFFu
int font_glyph(const struct font *f, unsigned code, int x, int y, uint16_t fg, uint32_t bg);
int font_text(const struct font *f, const char *s, int x, int y, uint16_t fg, uint32_t bg);
/* Width in pixels of a string (or its first n bytes if n > 0). */
int font_text_width(const struct font *f, const char *s, int n);
/* Draw text centred in [x, x + w). */
void font_text_centred(const struct font *f, const char *s, int x, int w, int y, uint16_t fg, uint32_t bg);
/* Draw text clipped to w pixels, with ".." when it does not fit. */
int font_text_clipped(const struct font *f, const char *s, int x, int y, int w, uint16_t fg, uint32_t bg);

#endif
