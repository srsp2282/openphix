/* PC simulator port. The data image (a raw flash dump or an ExtFlashDat.bin,
 * encrypted) is loaded into memory as the external flash. Keys come from an
 * X11 window when one can be opened, or from a scripted key list on the
 * command line for headless test runs. Screenshots are written as PPM.
 *
 *   openphix-sim <image> [--keys "ok,down,esc"] [--shot out.ppm] [--headless]
 *                        [--tool <name> ...]
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "openphix/hal.h"
#include "openphix/app.h"

static uint8_t *flash;
static uint32_t flash_size;
static const char *script_keys[256];
static int script_n, script_i;
static const char *shot_path;
static int headless;
static uint16_t last_frame[HAL_LCD_W * HAL_LCD_H];

/* ---- X11 -------------------------------------------------------------- */
#ifdef OPENPHIX_SIM_X11
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
static Display *dpy;
static Window win;
static GC gc;
static XImage *ximg;
static uint32_t *xpix;
#define SCALE 2

static int x11_open(void)
{
    dpy = XOpenDisplay(NULL);
    if (!dpy)
        return -1;
    int scr = DefaultScreen(dpy);
    win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, HAL_LCD_W * SCALE,
                              HAL_LCD_H * SCALE, 0, 0, 0);
    XStoreName(dpy, win, "openphix simulator");
    XSelectInput(dpy, win, KeyPressMask | ExposureMask | StructureNotifyMask);
    XMapWindow(dpy, win);
    gc = DefaultGC(dpy, scr);
    xpix = malloc((size_t)HAL_LCD_W * SCALE * HAL_LCD_H * SCALE * 4);
    ximg = XCreateImage(dpy, DefaultVisual(dpy, scr), 24, ZPixmap, 0, (char *)xpix,
                        HAL_LCD_W * SCALE, HAL_LCD_H * SCALE, 32, 0);
    Atom wm_delete = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(dpy, win, &wm_delete, 1);
    return 0;
}

static void x11_draw(const uint16_t *fb)
{
    for (int y = 0; y < HAL_LCD_H * SCALE; y++)
        for (int x = 0; x < HAL_LCD_W * SCALE; x++) {
            uint16_t p = fb[(y / SCALE) * HAL_LCD_W + x / SCALE];
            uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
            xpix[y * HAL_LCD_W * SCALE + x] =
                ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
        }
    XPutImage(dpy, win, gc, ximg, 0, 0, 0, 0, HAL_LCD_W * SCALE, HAL_LCD_H * SCALE);
    XFlush(dpy);
}

static enum hal_key x11_key(uint32_t timeout_ms)
{
    uint32_t start = hal_millis();
    for (;;) {
        while (XPending(dpy)) {
            XEvent ev;
            XNextEvent(dpy, &ev);
            if (ev.type == Expose)
                x11_draw(last_frame);
            else if (ev.type == ClientMessage)
                return HAL_KEY_QUIT;
            else if (ev.type == KeyPress) {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                switch (ks) {
                case XK_Return: case XK_KP_Enter: return HAL_KEY_OK;
                case XK_Escape: case XK_BackSpace: return HAL_KEY_ESC;
                case XK_Up: return HAL_KEY_UP;
                case XK_Down: return HAL_KEY_DOWN;
                case XK_Left: return HAL_KEY_LEFT;
                case XK_Right: return HAL_KEY_RIGHT;
                case XK_i: return HAL_KEY_IM;
                case XK_d: return HAL_KEY_DTC;
                case XK_q: return HAL_KEY_QUIT;
                case XK_s: {
                    FILE *f = fopen("screenshot.ppm", "wb");
                    if (f) {
                        fprintf(f, "P6 %d %d 255\n", HAL_LCD_W, HAL_LCD_H);
                        for (int i = 0; i < HAL_LCD_W * HAL_LCD_H; i++) {
                            uint16_t p = last_frame[i];
                            uint8_t rgb[3] = { (uint8_t)((p >> 8) & 0xF8), (uint8_t)((p >> 3) & 0xFC), (uint8_t)((p << 3) & 0xF8) };
                            fwrite(rgb, 1, 3, f);
                        }
                        fclose(f);
                        fprintf(stderr, "wrote screenshot.ppm\n");
                    }
                    break;
                }
                default: break;
                }
            }
        }
        if (timeout_ms != HAL_WAIT_FOREVER && hal_millis() - start >= timeout_ms)
            return HAL_KEY_NONE;
        usleep(5000);
    }
}
#endif

/* ---- HAL -------------------------------------------------------------- */

static void write_ppm(const char *path, const uint16_t *fb)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6 %d %d 255\n", HAL_LCD_W, HAL_LCD_H);
    for (int i = 0; i < HAL_LCD_W * HAL_LCD_H; i++) {
        uint16_t p = fb[i];
        uint8_t rgb[3] = { (uint8_t)((p >> 8) & 0xF8), (uint8_t)((p >> 3) & 0xFC), (uint8_t)((p << 3) & 0xF8) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

void hal_lcd_flush(const uint16_t *fb)
{
    memcpy(last_frame, fb, sizeof last_frame);
#ifdef OPENPHIX_SIM_X11
    if (!headless)
        x11_draw(fb);
#endif
}

void hal_lcd_backlight(int on) { (void)on; }

static enum hal_key parse_key(const char *s)
{
    static const struct { const char *n; enum hal_key k; } map[] = {
        { "ok", HAL_KEY_OK }, { "esc", HAL_KEY_ESC }, { "up", HAL_KEY_UP },
        { "down", HAL_KEY_DOWN }, { "left", HAL_KEY_LEFT }, { "right", HAL_KEY_RIGHT },
        { "im", HAL_KEY_IM }, { "dtc", HAL_KEY_DTC }, { "quit", HAL_KEY_QUIT },
    };
    for (size_t i = 0; i < sizeof map / sizeof map[0]; i++)
        if (strcmp(map[i].n, s) == 0)
            return map[i].k;
    return HAL_KEY_NONE;
}

enum hal_key hal_key_wait(uint32_t timeout_ms)
{
    if (headless) {
        if (script_i < script_n)
            return parse_key(script_keys[script_i++]);
        return HAL_KEY_QUIT;     /* script exhausted: end the run */
    }
#ifdef OPENPHIX_SIM_X11
    return x11_key(timeout_ms);
#else
    (void)timeout_ms;
    return HAL_KEY_QUIT;
#endif
}

uint32_t hal_millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

void hal_delay_ms(uint32_t ms)
{
    if (!headless)
        usleep(ms * 1000);
}

void hal_beep(uint32_t ms) { (void)ms; }

uint32_t hal_flash_size(void) { return flash_size; }

int hal_flash_read(uint32_t addr, void *buf, size_t len)
{
    if (addr > flash_size || len > flash_size - addr)
        return -1;
    memcpy(buf, flash + addr, len);
    return 0;
}

int hal_flash_write(uint32_t addr, const void *buf, size_t len)
{
    if (addr > flash_size || len > flash_size - addr)
        return -1;
    memcpy(flash + addr, buf, len);
    return 0;
}

uint32_t hal_supply_mv(void) { return 12600; }

int hal_bus_open(enum hal_bus bus, uint32_t bitrate) { (void)bus; (void)bitrate; return -1; }
void hal_bus_close(void) {}
int hal_kline_write(const uint8_t *d, size_t n) { (void)d; (void)n; return -1; }
int hal_kline_read(uint8_t *d, size_t n, uint32_t t) { (void)d; (void)n; (void)t; return 0; }
int hal_kline_init_5baud(uint8_t a) { (void)a; return -1; }
int hal_kline_init_fast(void) { return -1; }
int hal_can_send(uint32_t id, const uint8_t *d, size_t n) { (void)id; (void)d; (void)n; return -1; }
int hal_can_recv(uint32_t *id, uint8_t *d, size_t *n, uint32_t t) { (void)id; (void)d; (void)n; (void)t; return -1; }

void hal_log(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
}

/* ---- main ------------------------------------------------------------- */

static int load_flash(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* a 32 MiB flash regardless of the file size, erased (0xFF) beyond it */
    flash_size = 32u << 20;
    if ((uint32_t)n > flash_size)
        flash_size = (uint32_t)n;
    flash = malloc(flash_size);
    memset(flash, 0xFF, flash_size);
    if (fread(flash, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        return -1;
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv)
{
    const char *image = NULL;
    int tool_argc = 0;
    char **tool_argv = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--keys") == 0 && i + 1 < argc) {
            char *s = strdup(argv[++i]);
            for (char *t = strtok(s, ","); t && script_n < 256; t = strtok(NULL, ","))
                script_keys[script_n++] = t;
            headless = 1;
        } else if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot_path = argv[++i];
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = 1;
        } else if (strcmp(argv[i], "--tool") == 0) {
            tool_argc = argc - i - 1;
            tool_argv = argv + i + 1;
            headless = 1;
            break;
        } else if (!image) {
            image = argv[i];
        } else {
            fprintf(stderr, "unexpected argument %s\n", argv[i]);
            return 2;
        }
    }
    if (!image) {
        fprintf(stderr, "usage: openphix-sim <image> [--keys k,k,...] [--shot out.ppm] [--headless]\n"
                        "                    [--tool <name> [args]]\n");
        return 2;
    }
    if (load_flash(image) != 0) {
        fprintf(stderr, "cannot load %s\n", image);
        return 1;
    }
#ifdef OPENPHIX_SIM_X11
    if (!headless && x11_open() != 0) {
        fprintf(stderr, "no X display, running headless\n");
        headless = 1;
    }
#endif
    int rc = tool_argv ? app_tool(tool_argc, tool_argv) : app_main();
    if (shot_path)
        write_ppm(shot_path, last_frame);
    return rc;
}
