#include <string.h>

#include "openphix/gfx.h"

uint16_t gfx_fb[GFX_W * GFX_H];

void gfx_clear(uint16_t color)
{
    for (int i = 0; i < GFX_W * GFX_H; i++)
        gfx_fb[i] = color;
}

void gfx_pixel(int x, int y, uint16_t color)
{
    if (x >= 0 && y >= 0 && x < GFX_W && y < GFX_H)
        gfx_fb[y * GFX_W + x] = color;
}

void gfx_fill(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > GFX_W) w = GFX_W - x;
    if (y + h > GFX_H) h = GFX_H - y;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            gfx_fb[(y + j) * GFX_W + x + i] = color;
}

void gfx_hline(int x, int y, int w, uint16_t color) { gfx_fill(x, y, w, 1, color); }
void gfx_vline(int x, int y, int h, uint16_t color) { gfx_fill(x, y, 1, h, color); }

void gfx_rect(int x, int y, int w, int h, uint16_t color)
{
    gfx_hline(x, y, w, color);
    gfx_hline(x, y + h - 1, w, color);
    gfx_vline(x, y, h, color);
    gfx_vline(x + w - 1, y, h, color);
}

void gfx_blit_res(const struct res *img, int x, int y, int w, int h)
{
    uint8_t row[GFX_W * 2];
    if (w > GFX_W)
        w = GFX_W;
    for (int j = 0; j < h; j++) {
        int yy = y + j;
        if (res_read(img, (uint32_t)(j * w * 2), row, (size_t)w * 2) != 0)
            return;
        if (yy < 0 || yy >= GFX_H)
            continue;
        for (int i = 0; i < w; i++) {
            int xx = x + i;
            if (xx >= 0 && xx < GFX_W)
                gfx_fb[yy * GFX_W + xx] = (uint16_t)(row[2 * i] | (row[2 * i + 1] << 8));
        }
    }
}

void gfx_blit_screen(const struct res *img)
{
    gfx_blit_res(img, 0, 0, GFX_W, GFX_H);
}

void gfx_flush(void)
{
    hal_lcd_flush(gfx_fb);
}
