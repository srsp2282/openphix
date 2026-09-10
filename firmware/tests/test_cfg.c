#include <string.h>

#include "openphix/funcfg.h"
#include "openphix/menu.h"
#include "openphix/res.h"
#include "openphix/str.h"
#include "test.h"

int main(int argc, char **argv)
{
    struct res image;
    struct menu_entry e;
    struct funcfg_record r;
    struct funcfg_proto p;
    struct funcfg_step s;
    uint32_t title, n, name, conv;
    uint16_t id;
    uint8_t kinds[16];
    char buf[96];
    if (argc < 2 || hal_stub_load(argv[1]) != 0)
        return 1;
    CHECK_EQ(res_init(&image), 0);
    str_init(&image);
    CHECK_EQ(menu_init(&image), 0);
    CHECK_EQ(funcfg_init(&image), 0);

    /* menu tree of the Biltema dump */
    CHECK_EQ(menu_node(0, &title, &n), 0);
    CHECK_EQ(title, 0x10b); CHECK_EQ(n, 2);
    str_get(title, buf, sizeof buf); CHECK(strcmp(buf, "Vehicle Selection") == 0);
    CHECK_EQ(menu_entry(0, 0, &e), 0);
    CHECK_EQ(e.kind, MENU_SUBMENU); CHECK_EQ(e.param, 0x30);
    CHECK_EQ(menu_node(0x30, &title, &n), 0); CHECK_EQ(n, 3);
    CHECK_EQ(menu_entry(0x30, 0, &e), 0);
    CHECK_EQ(e.kind, MENU_SCAN); CHECK_EQ(e.param, 0xf001);
    CHECK_EQ(menu_entry(0x30, 2, &e), 0);
    CHECK_EQ(e.kind, MENU_SUBMENU); CHECK_EQ(e.param, 0xd4);
    CHECK_EQ(menu_entry(0xd4, 0, &e), 0);
    CHECK_EQ(e.kind, MENU_SPFUNC); CHECK_EQ(e.action & 0x7fffffff, 1); CHECK_EQ(e.param, 0x10000);
    CHECK_EQ(menu_node(0x2c8, &title, &n), 0); CHECK_EQ(n, 252);
    CHECK_EQ(menu_entry(0x2c8, 0, &e), 0);
    CHECK_EQ(e.kind, MENU_MODULE); CHECK_EQ(e.action, 1); CHECK_EQ(e.caption, 0x01020001);
    CHECK_EQ(menu_entry(0x2c8, 252, &e), -1);
    CHECK_EQ(menu_node(0x1680, &title, &n), 0); CHECK_EQ(n, 14);

    /* FUNCFG records */
    CHECK_EQ(funcfg_find(FK_KLINE_KWP2000, 1, 0, &r), 0);
    CHECK_EQ(r.offset, 0);
    CHECK_EQ(r.slot[SLOT_CONNECT], 0x92cd8);
    CHECK_EQ(r.slot[SLOT_BASIC_SETTINGS], 0x27a537);
    CHECK_EQ(r.proto, 0);
    CHECK_EQ(funcfg_find(FK_UDS, 1, 0, &r), 0);
    CHECK_EQ(funcfg_proto(&r, &p), 0);
    CHECK_EQ(p.kind, 2); CHECK_EQ(p.nids, 2); CHECK_EQ(p.id[0], 0x7e0); CHECK_EQ(p.id[1], 0x7e8);
    CHECK_EQ(p.timing[0], 3000); CHECK_EQ(p.timing[2], 200);
    CHECK_EQ(funcfg_find(FK_UDS_29BIT, 1, 0xffff, &r), 0);
    CHECK_EQ(funcfg_proto(&r, &p), 0);
    CHECK_EQ(p.kind, 3); CHECK_EQ(p.id[0], 0x17fc0076); CHECK_EQ(p.id[1], 0x17fe0076);
    CHECK_EQ(funcfg_find(FK_UDS, 1, 5582, &r), -1);       /* only in the package */
    CHECK_EQ(funcfg_find(FK_UDS, 1, 5581, &r), 0);
    n = funcfg_kinds_for_module(1, kinds, 16);
    CHECK(n >= 6);

    CHECK_EQ(funcfg_find(FK_UDS, 1, 0, &r), 0);
    CHECK_EQ(funcfg_connect_step(&r, 0, &s), 0);
    CHECK_EQ(s.ncmd, 1); CHECK_EQ(s.cmd[0].cmd, 0x30000004);
    CHECK_EQ(funcfg_connect_step(&r, 1, &s), -1);
    CHECK_EQ(funcfg_keepalive_step(&r, 0, &s), 0);
    CHECK_EQ(s.cmd[0].cmd, 0x30000005);
    CHECK_EQ(funcfg_keepalive_step(&r, 1, &s), 0);
    CHECK_EQ(s.cmd[0].cmd, 0x30000006);
    CHECK_EQ(funcfg_version_step(&r, 1, &s), 0);
    str_get(s.label, buf, sizeof buf); CHECK(strcmp(buf, "VIN: ") == 0);
    CHECK_EQ(s.exp, 0x93); CHECK_EQ(s.cmd[0].cmd, 0x30000007);
    CHECK_EQ(funcfg_version_step(&r, 7, &s), -1);
    CHECK_EQ(funcfg_dtc_entry(&r, 0, &s), 0);
    CHECK_EQ(s.cmd[0].cmd, 0x30000017); CHECK_EQ(s.exp, 0x9a);
    CHECK_EQ(funcfg_dtc_entry(&r, 2, &s), 0);
    CHECK_EQ(s.cmd[0].cmd, 0x30000019);
    CHECK_EQ(funcfg_dtc_entry(&r, 3, &s), -1);
    CHECK_EQ(funcfg_channel_count(&r), 301);
    CHECK_EQ(funcfg_channel(&r, 0, &id, &name, &s, &conv), 0);
    CHECK_EQ(id, 0x973); CHECK_EQ(name, 0x020205ea); CHECK_EQ(s.cmd[0].cmd, 0x3022f403); CHECK_EQ(conv, 0x01000313);
    CHECK_EQ(funcfg_channel(&r, 301, &id, &name, &s, &conv), -1);
    CHECK_EQ(funcfg_items(&r, SLOT_BASIC_SETTINGS, &title, &n), -1);   /* variant 0 has no extras */
    CHECK_EQ(funcfg_find(FK_UDS, 1, 0xffff, &r), 0);
    CHECK_EQ(r.slot[SLOT_ADAPTATION], 0);
    CHECK_EQ(funcfg_items(&r, SLOT_BASIC_SETTINGS, &title, &n), 0);
    CHECK_EQ(title, 0x01080001); CHECK_EQ(n, 1);
    CHECK_EQ(funcfg_item(&r, SLOT_BASIC_SETTINGS, 0, &name), 0);
    CHECK_EQ(name, 0x01081004);
    str_get(name, buf, sizeof buf); CHECK(strcmp(buf, "Throttle Learning") == 0);

    /* a TP2.0 record: multi variant version chunk, datastream not decoded */
    CHECK_EQ(funcfg_find(FK_TP20, 1, 0, &r), 0);
    CHECK_EQ(funcfg_version_step(&r, 0, &s), 0);
    CHECK_EQ(funcfg_channel_count(&r), -1);
    TEST_MAIN_END();
}
