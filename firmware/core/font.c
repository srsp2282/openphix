#include <string.h>

#include "openphix/font.h"
#include "openphix/gfx.h"

static struct font fonts[FONT_COUNT];

static int load_raw(const struct res *image, const char *name, unsigned w, unsigned h,
                    struct font *f)
{
    struct res r;
    if (res_open(image, name, &r) != 0)
        return -1;
    f->data = r;
    f->width = (uint8_t)w;
    f->height = (uint8_t)h;
    f->row_bytes = (uint8_t)((w + 7) / 8);
    f->glyph_bytes = (uint16_t)(f->row_bytes * h);
    f->count = 256;
    /* the 16 and 24 pixel fonts are narrow designs in a wide cell; the
     * stock UI draws them tighter than the cell width */
    f->pitch = (uint8_t)(w == 8 ? 8 : w == 16 ? 12 : w == 20 ? 16 : w == 24 ? 16 : w);
    if (f->height == 32)
        f->pitch = 20;
    if (r.size != (uint32_t)f->glyph_bytes * 256)
        return -1;
    return 0;
}

int font_load(const struct res *image, const char *family)
{
    char name[40];
    int west = strcmp(family, "WESTISO") == 0;
    memset(fonts, 0, sizeof fonts);
    /* every family has 8x16 (except WESTISO, which borrows POL_WIN's ASCII
     * half), 16x16 and 24x24; only WESTISO has 20x20 and 20x32 */
    strcpy(name, west ? "POL_WIN_8x16.bin" : family);
    if (!west)
        strcat(name, "_8x16.BIN");
    if (load_raw(image, name, 8, 16, &fonts[FONT_8X16]) != 0)
        return -1;
    strcpy(name, family);
    strcat(name, west ? "_16X16.bin" : "_16x16.BIN");
    if (load_raw(image, name, 16, 16, &fonts[FONT_16X16]) != 0)
        return -1;
    strcpy(name, family);
    strcat(name, west ? "_24X24.bin" : "_24x24.BIN");
    if (load_raw(image, name, 24, 24, &fonts[FONT_24X24]) != 0)
        return -1;
    if (load_raw(image, "WESTISO_20X20.BIN", 20, 20, &fonts[FONT_20X20]) != 0)
        fonts[FONT_20X20] = fonts[FONT_16X16];
    if (load_raw(image, "WESTISO_20X32.BIN", 20, 32, &fonts[FONT_20X32]) != 0)
        fonts[FONT_20X32] = fonts[FONT_24X24];
    return 0;
}

const struct font *font_get(enum font_id id)
{
    return &fonts[id < FONT_COUNT ? id : FONT_16X16];
}

int font_glyph(const struct font *f, unsigned code, int x, int y, uint16_t fg, uint32_t bg)
{
    uint8_t g[96];
    if (!f->glyph_bytes || code >= f->count || f->glyph_bytes > sizeof g)
        return x + f->pitch;
    if (res_read(&f->data, (uint32_t)code * f->glyph_bytes, g, f->glyph_bytes) != 0)
        return x + f->pitch;
    for (int row = 0; row < f->height; row++) {
        const uint8_t *bits = g + row * f->row_bytes;
        for (int col = 0; col < f->width; col++) {
            int ink = (bits[col >> 3] >> (7 - (col & 7))) & 1;
            if (ink)
                gfx_pixel(x + col, y + row, fg);
            else if (bg != FONT_TRANSPARENT && col < f->pitch)
                gfx_pixel(x + col, y + row, (uint16_t)bg);
        }
    }
    return x + f->pitch;
}

int font_text(const struct font *f, const char *s, int x, int y, uint16_t fg, uint32_t bg)
{
    for (; *s; s++)
        x = font_glyph(f, (uint8_t)*s, x, y, fg, bg);
    return x;
}

int font_text_width(const struct font *f, const char *s, int n)
{
    int len = n > 0 ? n : (int)strlen(s);
    return len * f->pitch;
}

void font_text_centred(const struct font *f, const char *s, int x, int w, int y, uint16_t fg, uint32_t bg)
{
    int tw = font_text_width(f, s, 0);
    font_text(f, s, x + (w - tw) / 2, y, fg, bg);
}

int font_text_clipped(const struct font *f, const char *s, int x, int y, int w, uint16_t fg, uint32_t bg)
{
    int n = (int)strlen(s);
    int fit = w / f->pitch;
    if (n <= fit)
        return font_text(f, s, x, y, fg, bg);
    if (fit < 3)
        return x;
    for (int i = 0; i < fit - 2; i++)
        x = font_glyph(f, (uint8_t)s[i], x, y, fg, bg);
    x = font_glyph(f, '.', x, y, fg, bg);
    return font_glyph(f, '.', x, y, fg, bg);
}
