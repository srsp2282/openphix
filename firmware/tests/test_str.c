#include <string.h>

#include "openphix/font.h"
#include "openphix/res.h"
#include "openphix/str.h"
#include "test.h"

int main(int argc, char **argv)
{
    struct res image;
    char buf[128];
    if (argc < 2 || hal_stub_load(argv[1]) != 0)
        return 1;
    CHECK_EQ(res_init(&image), 0);
    CHECK_EQ(str_init(&image), 5);
    CHECK(strcmp(str_language(0)->code, "en") == 0);
    CHECK_EQ(str_language(0)->number, 3);
    CHECK_EQ(str_language(0)->count, 68581);
    CHECK(strcmp(str_language(2)->code, "sv") == 0);
    CHECK_EQ(str_selected(), 0);

    CHECK_EQ(str_get(1, buf, sizeof buf), 13);
    CHECK(strcmp(buf, "Select System") == 0);
    CHECK_EQ(str_get(0x1020001, buf, sizeof buf), 28);
    CHECK(strcmp(buf, "0001-Engine Control Module 1") == 0);
    CHECK_EQ(str_get(0xff1c1783, buf, sizeof buf), 36);       /* last id */
    CHECK(strncmp(buf, "P1783 4th-3rd", 13) == 0);
    CHECK_EQ(str_get(0xa1000100, buf, sizeof buf), 54);
    CHECK_EQ(str_get(0x12345678, buf, sizeof buf), -1);
    CHECK_EQ(buf[0], 0);
    CHECK(strcmp(str_get_or_id(0x12345678, buf, sizeof buf), "[12345678]") == 0);
    /* small buffer: truncated but terminated */
    CHECK_EQ(str_get(1, buf, 4), 3);
    CHECK(strcmp(buf, "Sel") == 0);

    /* Swedish with English fallback */
    CHECK_EQ(str_select(2), 0);
    str_get(0x1020001, buf, sizeof buf);
    CHECK(strcmp(buf, "0001-Motorelektronik") == 0);
    str_get(0x3010001, buf, sizeof buf);   /* units exist only in English */
    CHECK(buf[0] != 0);
    CHECK_EQ(str_select(9), -1);

    /* fonts */
    CHECK_EQ(font_load(&image, "WESTISO"), 0);
    CHECK_EQ(font_get(FONT_16X16)->glyph_bytes, 32);
    CHECK_EQ(font_get(FONT_24X24)->glyph_bytes, 72);
    CHECK_EQ(font_get(FONT_20X32)->height, 32);
    CHECK_EQ(font_load(&image, "RUSS_ISO"), 0);
    CHECK_EQ(font_get(FONT_20X20)->width, 20);   /* borrowed from WESTISO */
    TEST_MAIN_END();
}
