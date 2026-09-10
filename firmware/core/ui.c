#include <stdio.h>
#include <string.h>

#include "openphix/dtc.h"
#include "openphix/exp.h"
#include "openphix/font.h"
#include "openphix/funcfg.h"
#include "openphix/gfx.h"
#include "openphix/hal.h"
#include "openphix/menu.h"
#include "openphix/res.h"
#include "openphix/str.h"
#include "openphix/sysscan.h"
#include "openphix/ui.h"

extern struct res g_image;

const struct ui_skin ui_skin_gray = {
    RGB565(0x22, 0x28, 0x38), GFX_WHITE, RGB565(0x14, 0x18, 0x24), GFX_WHITE,
    RGB565(0xF5, 0x9A, 0x23), GFX_BLACK, RGB565(0x50, 0x58, 0x70),
    "BGSTART_GRAY.BIN", "LGSETUP_GRAY.BIN"
};
const struct ui_skin ui_skin_blue = {
    GFX_WHITE, GFX_BLACK, RGB565(0x1E, 0x64, 0xC8), GFX_WHITE,
    RGB565(0x1E, 0x64, 0xC8), GFX_WHITE, RGB565(0xC0, 0xC8, 0xD8),
    "BGSTART_BLUE.BIN", "LGSETUP_BLUE.BIN"
};
const struct ui_skin *ui_skin = &ui_skin_gray;

#define TITLE_H 28
#define ROW_H 24
#define ROWS ((GFX_H - TITLE_H) / ROW_H)

/* String ids of the built in menus (docs/data-image-format.md, 13). */
#define STR_OBDII 0x1017Cu
#define STR_FOR_VW 0x10268u
#define STR_OIL_RESET 0x728u
#define STR_EPB_RESET 0x729u
#define STR_BAT_CHECK 0x102ADu
#define STR_BMS_RESET 0x72Au
#define STR_ETC_RESET 0x72Bu
#define STR_TOOL_SETUP 0x1025Au
#define STR_SELECT_FUNCTION 0x5u
#define STR_FN_VERSION 0x6u
#define STR_FN_READ_DTC 0x7u
#define STR_FN_ERASE_DTC 0x8u
#define STR_FN_DATASTREAM 0x9u
#define STR_READ_CODES 0x101C8u
#define STR_ERASE_CODES 0x1009Au
#define STR_IM_READINESS 0x100D0u
#define STR_DATA_STREAM 0x10059u
#define STR_VEHICLE_INFO 0x1026Au
#define STR_LANGUAGE 0x10111u
#define STR_BEEPER 0x1002Bu
#define STR_UNIT 0x1025Cu
#define STR_SKIN 0x20u
#define STR_DEVICE_INFO 0x10259u
#define STR_INSTRUCTIONS 0x01FF21B5u

static const char *S(uint32_t id, char *buf, unsigned cap)
{
    return str_get_or_id(id, buf, cap);
}

void ui_title(const char *title)
{
    gfx_fill(0, 0, GFX_W, TITLE_H, ui_skin->title_bg);
    font_text_clipped(font_get(FONT_16X16), title, 6, (TITLE_H - 16) / 2, GFX_W - 12,
                      ui_skin->title_fg, FONT_TRANSPARENT);
    gfx_hline(0, TITLE_H - 1, GFX_W, ui_skin->line);
}

void ui_text_wrapped(const char *text, int x, int y, int w, int line_h, uint16_t fg)
{
    const int y_max = GFX_H - 24;   /* keep clear of the [OK] line */
    const struct font *f = font_get(FONT_16X16);
    char line[64];
    while (*text && y + line_h <= y_max) {
        int n = 0, last_space = -1;
        while (text[n] && text[n] != '\n' && n < (int)sizeof line - 1 &&
               font_text_width(f, text, n + 1) <= w) {
            if (text[n] == ' ')
                last_space = n;
            n++;
        }
        if (text[n] && text[n] != '\n' && last_space > 0)
            n = last_space;
        memcpy(line, text, (size_t)n);
        line[n] = 0;
        font_text(f, line, x, y, fg, FONT_TRANSPARENT);
        text += n;
        while (*text == ' ' || *text == '\n')
            text++;
        y += line_h;
    }
}

int ui_list(const char *title, int count, ui_item_fn item, void *ctx, int start)
{
    const struct font *f = font_get(FONT_16X16);
    int sel = start < count ? start : 0, top = 0;
    char buf[80];
    for (;;) {
        enum hal_key k;
        if (sel < top)
            top = sel;
        if (sel >= top + ROWS)
            top = sel - ROWS + 1;
        gfx_clear(ui_skin->bg);
        ui_title(title);
        for (int i = 0; i < ROWS && top + i < count; i++) {
            int y = TITLE_H + i * ROW_H;
            int is_sel = top + i == sel;
            if (is_sel)
                gfx_fill(0, y, GFX_W, ROW_H, ui_skin->sel_bg);
            item(ctx, top + i, buf, sizeof buf);
            font_text_clipped(f, buf, 8, y + (ROW_H - 16) / 2, GFX_W - 16,
                              is_sel ? ui_skin->sel_fg : ui_skin->fg, FONT_TRANSPARENT);
        }
        if (count > ROWS) {
            /* scroll bar */
            int h = (GFX_H - TITLE_H) * ROWS / count, y = TITLE_H + (GFX_H - TITLE_H) * top / count;
            gfx_fill(GFX_W - 4, TITLE_H, 4, GFX_H - TITLE_H, ui_skin->line);
            gfx_fill(GFX_W - 4, y, 4, h < 8 ? 8 : h, ui_skin->fg);
        }
        if (count == 0)
            font_text(f, "-", 8, TITLE_H + 4, ui_skin->fg, FONT_TRANSPARENT);
        gfx_flush();
        k = hal_key_wait(HAL_WAIT_FOREVER);
        switch (k) {
        case HAL_KEY_UP: if (count) sel = (sel + count - 1) % count; break;
        case HAL_KEY_DOWN: if (count) sel = (sel + 1) % count; break;
        case HAL_KEY_LEFT: sel = sel >= ROWS ? sel - ROWS : 0; break;
        case HAL_KEY_RIGHT: sel = sel + ROWS < count ? sel + ROWS : (count ? count - 1 : 0); break;
        case HAL_KEY_OK: if (count) return sel; break;
        case HAL_KEY_ESC: return -1;
        case HAL_KEY_QUIT: return -2;
        default: break;
        }
    }
}

int ui_message(const char *title, const char *text)
{
    enum hal_key k;
    gfx_clear(ui_skin->bg);
    ui_title(title);
    ui_text_wrapped(text, 8, TITLE_H + 8, GFX_W - 16, 18, ui_skin->fg);
    font_text_centred(font_get(FONT_16X16), "[OK]", 0, GFX_W, GFX_H - 20, ui_skin->fg, FONT_TRANSPARENT);
    gfx_flush();
    do {
        k = hal_key_wait(HAL_WAIT_FOREVER);
    } while (k != HAL_KEY_OK && k != HAL_KEY_ESC && k != HAL_KEY_QUIT);
    return k;
}

/* ---- screens ------------------------------------------------------------ */

/* Until the protocol stacks exist every bus function ends here, with the
 * stock "Device unable to communicate with vehicle" text (string 0xB) under
 * the "Communication Error" title (0x1004A). */
static int no_vehicle(const char *title)
{
    static char buf[512];
    char t[64];
    (void)title;
    str_get_or_id(0xBu, buf, sizeof buf);
    return ui_message(str_get_or_id(0x1004Au, t, sizeof t), buf) == HAL_KEY_QUIT ? -2 : -1;
}

/* function menu of a control module, from FUNCFG */
struct fn_item { uint32_t caption; enum funcfg_slot slot; };

static void fn_item_text(void *ctx, int i, char *buf, unsigned cap)
{
    const struct fn_item *items = ctx;
    str_get_or_id(items[i].caption, buf, cap);
}

int ui_module_menu(uint8_t address, uint32_t caption)
{
    struct funcfg_record r;
    struct fn_item items[8];
    uint8_t kinds[16];
    int nk = funcfg_kinds_for_module(address, kinds, 16), n = 0, sel = 0;
    char title[64];
    uint32_t t, cnt;
    /* the record of the first kind that has one, fall back variant first */
    int have = 0;
    for (int i = 0; i < nk && !have; i++)
        if (funcfg_find(kinds[i], address, 0xFFFF, &r) == 0 || funcfg_find(kinds[i], address, 0, &r) == 0)
            have = 1;
    S(caption, title, sizeof title);
    if (!have)
        return ui_message(title, "No function configuration for this module.") == HAL_KEY_QUIT ? -2 : -1;
    if (r.slot[SLOT_VERSION]) { items[n].caption = STR_FN_VERSION; items[n++].slot = SLOT_VERSION; }
    if (r.slot[SLOT_READ_DTC]) { items[n].caption = STR_FN_READ_DTC; items[n++].slot = SLOT_READ_DTC; }
    if (r.slot[SLOT_ERASE_DTC]) { items[n].caption = STR_FN_ERASE_DTC; items[n++].slot = SLOT_ERASE_DTC; }
    if (r.slot[SLOT_DATASTREAM]) { items[n].caption = STR_FN_DATASTREAM; items[n++].slot = SLOT_DATASTREAM; }
    for (enum funcfg_slot s = SLOT_BASIC_SETTINGS; s < SLOT_COUNT; s++)
        if (funcfg_items(&r, s, &t, &cnt) == 0) { items[n].caption = t; items[n++].slot = s; }
    for (;;) {
        int rc = ui_list(title, n, fn_item_text, items, sel);
        if (rc < 0)
            return rc;
        sel = rc;
        /* nothing on the bus yet: every function ends in the stock prompt */
        S(items[sel].caption, title, sizeof title);
        rc = no_vehicle(title);
        if (rc == -2)
            return -2;
        S(caption, title, sizeof title);
    }
}

/* Menu.BIN tree */
struct vw_ctx { uint32_t node; };

static void vw_item_text(void *ctx, int i, char *buf, unsigned cap)
{
    struct menu_entry e;
    if (menu_entry(((struct vw_ctx *)ctx)->node, (uint32_t)i, &e) == 0)
        str_get_or_id(e.caption, buf, cap);
    else
        buf[0] = 0;
}

int ui_vw_menu(uint32_t node)
{
    struct vw_ctx ctx = { node };
    uint32_t title_id, count;
    char title[64];
    int sel = 0;
    if (menu_node(node, &title_id, &count) != 0)
        return -1;
    S(title_id, title, sizeof title);
    for (;;) {
        struct menu_entry e;
        int rc = ui_list(title, (int)count, vw_item_text, &ctx, sel);
        if (rc < 0)
            return rc;
        sel = rc;
        if (menu_entry(node, (uint32_t)sel, &e) != 0)
            continue;
        switch (e.kind) {
        case MENU_SUBMENU: rc = ui_vw_menu(e.param); break;
        case MENU_MODULE: rc = ui_module_menu((uint8_t)e.action, e.caption); break;
        default: {
            char t[64];
            rc = no_vehicle(S(e.caption, t, sizeof t));
            break;
        }
        }
        if (rc == -2)
            return -2;
    }
}

/* OBDII */
static const uint32_t obd_items[] = { STR_READ_CODES, STR_ERASE_CODES, STR_IM_READINESS, STR_DATA_STREAM, STR_VEHICLE_INFO };

static void id_item_text(void *ctx, int i, char *buf, unsigned cap)
{
    str_get_or_id(((const uint32_t *)ctx)[i], buf, cap);
}

int ui_obd_menu(void)
{
    char title[64];
    int sel = 0;
    S(STR_OBDII, title, sizeof title);
    for (;;) {
        int rc = ui_list(title, 5, id_item_text, (void *)obd_items, sel);
        char t[64];
        if (rc < 0)
            return rc;
        sel = rc;
        if (no_vehicle(S(obd_items[sel], t, sizeof t)) == -2)
            return -2;
    }
}

/* Tool setup */
static void lang_item_text(void *ctx, int i, char *buf, unsigned cap)
{
    const struct str_table *t = str_language(i);
    int cur = str_selected();
    (void)ctx;
    /* 0x10078 is the language's own name in each table */
    str_select(i);
    str_get_or_id(0x10078u, buf, cap);
    str_select(cur);
    if (!t)
        buf[0] = 0;
}

int ui_device_info(void)
{
    char text[200], lib[32], sw[32];
    str_get_or_id(0x1026Eu, lib, sizeof lib);       /* library version string */
    snprintf(sw, sizeof sw, "openphix 0.1");
    snprintf(text, sizeof text, "Software: %s\nLibrary: %s\nFlash: %u MiB\nSupply: %u.%02u V",
             sw, lib, (unsigned)(hal_flash_size() >> 20), (unsigned)(hal_supply_mv() / 1000),
             (unsigned)(hal_supply_mv() % 1000 / 10));
    {
        char title[64];
        return ui_message(S(STR_DEVICE_INFO, title, sizeof title), text) == HAL_KEY_QUIT ? -2 : -1;
    }
}

int ui_setup_menu(void)
{
    static const uint32_t items[] = { STR_LANGUAGE, STR_BEEPER, STR_UNIT, STR_SKIN, STR_DEVICE_INFO };
    char title[64];
    int sel = 0;
    S(STR_TOOL_SETUP, title, sizeof title);
    for (;;) {
        int rc = ui_list(title, 5, id_item_text, (void *)items, sel), sub;
        char t[64];
        if (rc < 0)
            return rc;
        sel = rc;
        switch (sel) {
        case 0:
            sub = ui_list(S(STR_LANGUAGE, t, sizeof t), str_language_count(), lang_item_text, NULL, str_selected());
            if (sub >= 0)
                str_select(sub);
            break;
        case 3:
            ui_skin = ui_skin == &ui_skin_gray ? &ui_skin_blue : &ui_skin_gray;
            sub = -1;
            break;
        case 4:
            sub = ui_device_info();
            break;
        default:
            sub = ui_message(S(items[sel], t, sizeof t), "Not implemented yet.") == HAL_KEY_QUIT ? -2 : -1;
            break;
        }
        if (sub == -2)
            return -2;
        S(STR_TOOL_SETUP, title, sizeof title);
    }
}

int ui_battery_check(void)
{
    char title[64], text[64];
    uint32_t mv = hal_supply_mv();
    snprintf(text, sizeof text, "%u.%02u V", (unsigned)(mv / 1000), (unsigned)(mv % 1000 / 10));
    gfx_clear(ui_skin->bg);
    ui_title(S(STR_BAT_CHECK, title, sizeof title));
    font_text_centred(font_get(FONT_20X32), text, 0, GFX_W, 100, ui_skin->fg, FONT_TRANSPARENT);
    gfx_flush();
    for (;;) {
        enum hal_key k = hal_key_wait(1000);
        if (k == HAL_KEY_ESC || k == HAL_KEY_OK)
            return -1;
        if (k == HAL_KEY_QUIT)
            return -2;
    }
}

/* Main menu: the 3x3 icon grid of LGSETUP_*.BIN with a caption under the
 * selected icon. */
static const uint32_t main_captions[8] = {
    STR_OBDII, STR_FOR_VW, STR_OIL_RESET, STR_EPB_RESET, STR_BAT_CHECK,
    STR_BMS_RESET, STR_ETC_RESET, STR_TOOL_SETUP
};

int ui_main_menu(void)
{
    struct res img;
    int sel = 1;
    for (;;) {
        enum hal_key k;
        int col = sel % 3, row = sel / 3, cx = col * 107, cy = row * 80;
        char cap[48];
        if (res_open(&g_image, ui_skin->menu, &img) == 0)
            gfx_blit_screen(&img);
        else
            gfx_clear(ui_skin->bg);
        gfx_rect(cx + 2, cy + 2, 103, 76, ui_skin->sel_bg);
        gfx_rect(cx + 3, cy + 3, 101, 74, ui_skin->sel_bg);
        S(main_captions[sel], cap, sizeof cap);
        gfx_fill(cx, cy + 62, 107, 16, ui_skin->sel_bg);
        font_text_centred(font_get(FONT_8X16), cap, cx, 107, cy + 62, ui_skin->sel_fg, FONT_TRANSPARENT);
        gfx_flush();
        k = hal_key_wait(HAL_WAIT_FOREVER);
        switch (k) {
        case HAL_KEY_LEFT: sel = (sel + 7) % 8; break;
        case HAL_KEY_RIGHT: sel = (sel + 1) % 8; break;
        case HAL_KEY_UP: sel = sel >= 3 ? sel - 3 : sel; break;
        case HAL_KEY_DOWN: sel = sel + 3 < 8 ? sel + 3 : sel; break;
        case HAL_KEY_QUIT: return -2;
        case HAL_KEY_IM: if (no_vehicle("I/M") == -2) return -2; break;
        case HAL_KEY_DTC: if (no_vehicle("READ DTC") == -2) return -2; break;
        case HAL_KEY_OK: {
            int rc = -1;
            char t[48];
            switch (sel) {
            case 0: rc = ui_obd_menu(); break;
            case 1: rc = ui_vw_menu(0); break;
            case 4: rc = ui_battery_check(); break;
            case 7: rc = ui_setup_menu(); break;
            default: rc = no_vehicle(S(main_captions[sel], t, sizeof t)); break;
            }
            if (rc == -2)
                return -2;
            break;
        }
        default: break;
        }
    }
}
