#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "openphix/app.h"
#include "openphix/font.h"
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

int app_tool(int argc, char **argv)
{
    if (res_init(&g_image) != 0) {
        hal_log("no data image in flash");
        return 1;
    }
    str_init(&g_image);
    if (argc < 1) {
        hal_log("tools: ls, str <id>..., text [string]");
        return 2;
    }
    if (strcmp(argv[0], "ls") == 0)
        return tool_ls(argc - 1, argv + 1);
    if (strcmp(argv[0], "str") == 0)
        return tool_str(argc - 1, argv + 1);
    if (strcmp(argv[0], "text") == 0)
        return tool_text(argc - 1, argv + 1);
    hal_log("unknown tool %s", argv[0]);
    return 2;
}

int app_main(void)
{
    struct res img;
    if (res_init(&g_image) != 0) {
        gfx_clear(GFX_BLACK);
        gfx_flush();
        hal_log("no data image in flash");
        hal_key_wait(HAL_WAIT_FOREVER);
        return 1;
    }
    if (res_open(&g_image, "BGSTART_GRAY.BIN", &img) == 0)
        gfx_blit_screen(&img);
    gfx_flush();
    while (hal_key_wait(HAL_WAIT_FOREVER) != HAL_KEY_QUIT)
        ;
    return 0;
}
