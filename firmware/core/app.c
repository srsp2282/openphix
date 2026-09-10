#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openphix/app.h"
#include "openphix/cmd.h"
#include "openphix/dtc.h"
#include "openphix/sysscan.h"
#include "openphix/ui.h"
#include "openphix/exp.h"
#include "openphix/font.h"
#include "openphix/menu.h"
#include "openphix/funcfg.h"
#include "openphix/str.h"
#include "openphix/gfx.h"
#include "openphix/hal.h"
#include "openphix/res.h"

struct res g_image;

static int tool_ls(int argc, char **argv)
{
    struct res dir = g_image, f;
    char name[65];
    (void)argc; (void)argv;
    for (unsigned i = 0; res_entry(&dir, i, name, sizeof name, &f) == 0; i++)
        printf("%-28s 0x%08x %10u\n", name, f.base, f.size);
    return 0;
}

/* str <id> [<id>...]: print string ids in every language */
static int tool_str(int argc, char **argv)
{
    char buf[256];
    for (int i = 0; i < argc; i++) {
        uint32_t id = (uint32_t)strtoul(argv[i], NULL, 0);
        for (int l = 0; l < str_language_count(); l++) {
            str_select(l);
            printf("0x%08x %s: %s\n", (unsigned)id, str_language(l)->code,
                   str_get(id, buf, sizeof buf) < 0 ? "(missing)" : buf);
        }
    }
    return 0;
}

/* text <string>: render a string in every font and flush (for --shot) */
static int tool_text(int argc, char **argv)
{
    static const enum font_id ids[] = { FONT_8X16, FONT_16X16, FONT_20X20, FONT_24X24, FONT_20X32 };
    int y = 4;
    const char *s = argc > 0 ? argv[0] : "The quick brown fox 0123 \xc4\xe4\xf6\xe5\xdf\x01\x02\x07\x08";
    if (font_load(&g_image, "WESTISO") != 0)
        return 1;
    gfx_clear(GFX_WHITE);
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; i++) {
        const struct font *f = font_get(ids[i]);
        font_text(f, s, 4, y, GFX_BLACK, FONT_TRANSPARENT);
        y += f->height + 4;
    }
    gfx_flush();
    return 0;
}

/* exp <id> [hex bytes...]: evaluate an expression against input bytes */
static int tool_exp(int argc, char **argv)
{
    struct exp_ctx ctx;
    struct exp_value v;
    uint8_t x[64];
    char buf[256];
    int n = 0;
    if (argc < 1)
        return 2;
    for (int i = 1; i < argc && n < 64; i++)
        x[n++] = (uint8_t)strtoul(argv[i], NULL, 16);
    memset(&ctx, 0, sizeof ctx);
    ctx.in.x = x;
    ctx.in.n = n;
    if (exp_source((uint32_t)strtoul(argv[0], NULL, 0), buf, sizeof buf) >= 0)
        printf("source: %s%s\n", buf, strlen(buf) >= sizeof buf - 1 ? "..." : "");
    if (exp_eval(&ctx, (uint32_t)strtoul(argv[0], NULL, 0), &v) != 0) {
        printf("no result\n");
        return 1;
    }
    printf("Y = %s%s\n", exp_format(&v, buf, sizeof buf), ctx.error ? "  (parse error tolerated)" : "");
    if (ctx.range.valid)
        printf("range %g .. %s\n", ctx.range.lo, ctx.range.boundless ? "boundless" : "");
    return 0;
}

/* cmd <id>: decode a command record */
static int tool_cmd(int argc, char **argv)
{
    struct cmd c;
    struct cmd_packet pk;
    uint32_t pos = 0;
    uint8_t msg[64];
    int n;
    if (argc < 1 || cmd_get((uint32_t)strtoul(argv[0], NULL, 0), &c) != 0)
        return 2;
    printf("%08x kind=%02x h=%02x %02x %02x %02x %02x len=%u\n", (unsigned)c.id, c.kind,
           c.h[0], c.h[1], c.h[2], c.h[3], c.h[4], (unsigned)c.payload_len);
    if (c.kind == CMD_SEND || (c.kind == CMD_KEEPALIVE && (c.h[0] == 2 || c.h[0] == 5 || c.h[0] == 0x11))) {
        while (cmd_next_packet(&c, &pos, &pk)) {
            printf("  packet dlc=%u", pk.dlc);
            if (pk.has_id)
                printf(" id=0x%03x", pk.can_id);
            printf(" [");
            for (int i = 0; i < pk.len; i++)
                printf("%s%02x", i ? " " : "", pk.data[i]);
            printf("]\n");
        }
        if ((n = cmd_kline_message(&c, msg, sizeof msg)) > 0) {
            printf("  message [");
            for (int i = 0; i < n; i++)
                printf("%s%02x", i ? " " : "", msg[i]);
            printf("]\n");
        }
    }
    if (c.kind == CMD_KEEPALIVE)
        printf("  period %u ms\n", cmd_period_ms(&c));
    if (c.kind == CMD_NUMBER)
        printf("  value %u\n", (unsigned)cmd_number(&c));
    return 0;
}

/* modules: list the module index with names and data set counts */
static int tool_modules(int argc, char **argv)
{
    struct sysscan_module m;
    struct sysscan_record r;
    struct sysscan_tail t;
    char buf[64];
    (void)argc; (void)argv;
    for (uint32_t i = 0; i < sysscan_module_count(); i++) {
        int sets = 0;
        if (sysscan_module(i, &m) != 0)
            continue;
        for (uint8_t k = 0; k < m.count; k++) {
            if (sysscan_record(m.record[k], &r) != 0)
                continue;
            for (uint16_t j = 0; j < r.tail_count; j++)
                if (sysscan_tail(&r, j, &t) == 0 && (t.x & 0xFF) >= 0x30 && (t.x & 0xFF) <= 0x34)
                    sets++;
        }
        buf[0] = 0;
        if (m.address < 0x10000)
            str_get(sysscan_module_name_id(m.address), buf, sizeof buf);
        printf("%08x records=%u sets=%-4d %s\n", (unsigned)m.address, m.count, sets, buf);
    }
    return 0;
}

/* dtc <P0123|uds hex> : fault code text */
static int tool_dtc(int argc, char **argv)
{
    char buf[160];
    for (int i = 0; i < argc; i++) {
        const char *s = argv[i];
        uint32_t id = 0;
        if (strchr("PCBU", s[0]) && strlen(s) == 5) {
            uint16_t code = (uint16_t)(((uint16_t)(strchr("PCBU", s[0]) - "PCBU") << 14) | strtoul(s + 1, NULL, 16));
            id = dtc_obd_string(code);
        } else {
            uint32_t n = (uint32_t)strtoul(s, NULL, 0);
            id = n > 0xFFFF ? dtc_uds_string((uint16_t)(n >> 8), (uint8_t)n) : dtc_vag_string((uint16_t)n);
        }
        printf("%s: %s\n", s, id ? str_get_or_id(id, buf, sizeof buf) : "(no text)");
    }
    return 0;
}

int app_tool(int argc, char **argv)
{
    if (res_init(&g_image) != 0) {
        hal_log("no data image in flash");
        return 1;
    }
    str_init(&g_image);
    exp_init(&g_image);
    cmd_init(&g_image);
    dtc_init(&g_image);
    sysscan_init(&g_image);
    if (argc < 1) {
        hal_log("tools: ls, str <id>..., text [string], exp <id> [bytes], cmd <id>, modules, dtc <code>...");
        return 2;
    }
    if (strcmp(argv[0], "ls") == 0)
        return tool_ls(argc - 1, argv + 1);
    if (strcmp(argv[0], "str") == 0)
        return tool_str(argc - 1, argv + 1);
    if (strcmp(argv[0], "text") == 0)
        return tool_text(argc - 1, argv + 1);
    if (strcmp(argv[0], "exp") == 0)
        return tool_exp(argc - 1, argv + 1);
    if (strcmp(argv[0], "cmd") == 0)
        return tool_cmd(argc - 1, argv + 1);
    if (strcmp(argv[0], "modules") == 0)
        return tool_modules(argc - 1, argv + 1);
    if (strcmp(argv[0], "dtc") == 0)
        return tool_dtc(argc - 1, argv + 1);
    hal_log("unknown tool %s", argv[0]);
    return 2;
}

static void boot(void)
{
    struct res img;
    str_init(&g_image);
    exp_init(&g_image);
    cmd_init(&g_image);
    dtc_init(&g_image);
    sysscan_init(&g_image);
    menu_init(&g_image);
    funcfg_init(&g_image);
    font_load(&g_image, "WESTISO");
    if (res_open(&g_image, ui_skin->start, &img) == 0) {
        gfx_blit_screen(&img);
        gfx_flush();
        hal_delay_ms(1500);
    }
}

int app_main(void)
{
    if (res_init(&g_image) != 0) {
        gfx_clear(GFX_BLACK);
        font_text(font_get(FONT_16X16), "No data image in flash", 8, 8, GFX_WHITE, FONT_TRANSPARENT);
        gfx_flush();
        hal_log("no data image in flash");
        hal_key_wait(HAL_WAIT_FOREVER);
        return 1;
    }
    boot();
    ui_main_menu();
    return 0;
}
