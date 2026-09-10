/* Minimal HAL for unit tests: a file-backed flash, no display, no keys.
 * Tests call hal_stub_load(path) first. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openphix/hal.h"

static uint8_t *flash;
static uint32_t flash_size;

int hal_stub_load(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f)
        return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    flash_size = (uint32_t)n;
    flash = malloc(flash_size);
    if (fread(flash, 1, flash_size, f) != flash_size)
        return -1;
    fclose(f);
    return 0;
}

void hal_lcd_flush(const uint16_t *fb) { (void)fb; }
void hal_lcd_backlight(int on) { (void)on; }
enum hal_key hal_key_wait(uint32_t t) { (void)t; return HAL_KEY_QUIT; }
uint32_t hal_millis(void) { return 0; }
void hal_delay_ms(uint32_t ms) { (void)ms; }
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
uint32_t hal_supply_mv(void) { return 12000; }
int hal_bus_open(enum hal_bus b, uint32_t r) { (void)b; (void)r; return -1; }
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
