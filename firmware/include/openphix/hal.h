/* Hardware abstraction layer of the openphix firmware.
 *
 * Everything the portable core needs from the board is declared here and
 * implemented once per port (ports/sim for the PC simulator, ports/<mcu>
 * for real hardware once the microcontroller is known). The core never
 * touches a peripheral directly.
 */
#ifndef OPENPHIX_HAL_H
#define OPENPHIX_HAL_H

#include <stddef.h>
#include <stdint.h>

/* Display: 320x240, 16 bit RGB565, landscape. The core draws into its own
 * framebuffer and hands complete frames to the port. */
#define HAL_LCD_W 320
#define HAL_LCD_H 240

void hal_lcd_flush(const uint16_t *fb);
void hal_lcd_backlight(int on);

/* Keys. The stock unit has OK, ESC, four arrows and two shortcuts. */
enum hal_key {
    HAL_KEY_NONE = 0,
    HAL_KEY_OK,
    HAL_KEY_ESC,
    HAL_KEY_UP,
    HAL_KEY_DOWN,
    HAL_KEY_LEFT,
    HAL_KEY_RIGHT,
    HAL_KEY_IM,        /* I/M readiness shortcut */
    HAL_KEY_DTC,       /* READ DTC shortcut */
    HAL_KEY_QUIT       /* simulator only: window closed */
};

/* Returns the next key press, or HAL_KEY_NONE if none is pending within
 * timeout_ms (0 = poll, HAL_WAIT_FOREVER = block). */
#define HAL_WAIT_FOREVER 0xFFFFFFFFu
enum hal_key hal_key_wait(uint32_t timeout_ms);

/* Time */
uint32_t hal_millis(void);
void hal_delay_ms(uint32_t ms);

/* Beeper */
void hal_beep(uint32_t ms);

/* External data flash (the 32 MiB SPI NOR holding the data image). The core
 * reads through this; on the simulator it is a file. */
uint32_t hal_flash_size(void);
int hal_flash_read(uint32_t addr, void *buf, size_t len);   /* 0 on success */
int hal_flash_write(uint32_t addr, const void *buf, size_t len); /* erase+program, 0 on success */

/* Battery / supply voltage in millivolts, from DLC pin 16. */
uint32_t hal_supply_mv(void);

/* Diagnostic bus. One raw interface for now; the protocol stacks sit on
 * top of it in the core. */
enum hal_bus {
    HAL_BUS_NONE = 0,
    HAL_BUS_KLINE,      /* ISO 9141 / ISO 14230 / KWP1281 K-line, 10400 baud by default */
    HAL_BUS_CAN         /* ISO 15765 / TP2.0 on CAN */
};

int hal_bus_open(enum hal_bus bus, uint32_t bitrate);
void hal_bus_close(void);
/* K-line: send/receive raw bytes with a timeout. CAN: frames of up to 8 bytes
 * with an 11 or 29 bit id. */
int hal_kline_write(const uint8_t *data, size_t len);
int hal_kline_read(uint8_t *data, size_t len, uint32_t timeout_ms); /* bytes read */
int hal_kline_init_5baud(uint8_t address);     /* ISO 9141-2 / KWP1281 slow init */
int hal_kline_init_fast(void);                 /* ISO 14230 fast init (25 ms low, 25 ms high) */
int hal_can_send(uint32_t id, const uint8_t *data, size_t len);
int hal_can_recv(uint32_t *id, uint8_t *data, size_t *len, uint32_t timeout_ms);

/* Logging (simulator: stderr; device: optional UART). */
void hal_log(const char *fmt, ...);

#endif
