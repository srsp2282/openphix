/* Framebuffer graphics: a 320x240 RGB565 buffer in RAM plus primitives.
 * Text rendering lives in font.h. */
#ifndef OPENPHIX_GFX_H
#define OPENPHIX_GFX_H

#include <stdint.h>

#include "openphix/hal.h"
#include "openphix/res.h"

#define GFX_W HAL_LCD_W
#define GFX_H HAL_LCD_H

#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define GFX_BLACK 0x0000
#define GFX_WHITE 0xFFFF

extern uint16_t gfx_fb[GFX_W * GFX_H];

void gfx_clear(uint16_t color);
void gfx_fill(int x, int y, int w, int h, uint16_t color);
void gfx_rect(int x, int y, int w, int h, uint16_t color);
void gfx_hline(int x, int y, int w, uint16_t color);
void gfx_vline(int x, int y, int h, uint16_t color);
void gfx_pixel(int x, int y, uint16_t color);

/* Copy a raw RGB565 image (w*h*2 bytes, little-endian) from a resource to
 * (x, y). Rows are read one at a time, so no large buffer is needed. */
void gfx_blit_res(const struct res *img, int x, int y, int w, int h);
/* Full screen 320x240 image (the BGSTART and LGSETUP files). */
void gfx_blit_screen(const struct res *img);

/* Push the framebuffer to the display. */
void gfx_flush(void);

#endif
