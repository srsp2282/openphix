/* User interface: the list widget and the screens of the application. */
#ifndef OPENPHIX_UI_H
#define OPENPHIX_UI_H

#include <stdint.h>

/* Colours of the "Sky Gray" skin (dark) and "Gem Blue" skin (light). */
struct ui_skin {
    uint16_t bg, fg, title_bg, title_fg, sel_bg, sel_fg, line;
    const char *start, *menu;    /* BGSTART_* and LGSETUP_* file names */
};

extern const struct ui_skin ui_skin_gray, ui_skin_blue;
extern const struct ui_skin *ui_skin;

/* A list screen. Items are produced on demand through the callback so a
 * 252 entry module list needs no RAM. Returns the chosen index, or -1 on
 * ESC, or -2 on quit. */
typedef void (*ui_item_fn)(void *ctx, int index, char *buf, unsigned cap);
int ui_list(const char *title, int count, ui_item_fn item, void *ctx, int start);

/* A message screen with a title and up to a few lines; waits for a key.
 * Returns the key. */
int ui_message(const char *title, const char *text);

/* Draw helpers shared by screens. */
void ui_title(const char *title);
void ui_text_wrapped(const char *text, int x, int y, int w, int line_h, uint16_t fg);

/* Screens */
int ui_main_menu(void);
int ui_vw_menu(uint32_t node);
int ui_module_menu(uint8_t address, uint32_t caption);
int ui_obd_menu(void);
int ui_setup_menu(void);
int ui_battery_check(void);
int ui_device_info(void);

#endif
