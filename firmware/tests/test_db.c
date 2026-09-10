#include <string.h>

#include "openphix/dtc.h"
#include "openphix/res.h"
#include "openphix/str.h"
#include "openphix/sysscan.h"
#include "test.h"

int main(int argc, char **argv)
{
    struct res image;
    char buf[128];
    struct sysscan_module m;
    struct sysscan_record r;
    struct sysscan_tail t;
    uint32_t recs[8];
    uint16_t sub, x;
    if (argc < 2 || hal_stub_load(argv[1]) != 0)
        return 1;
    CHECK_EQ(res_init(&image), 0);
    str_init(&image);
    CHECK_EQ(dtc_init(&image), 0);
    CHECK_EQ(sysscan_init(&image), 0);

    /* DTC texts through the string id encodings */
    CHECK_EQ(dtc_obd_string(0x0001), 0xff000001);
    str_get(0xff000001, buf, sizeof buf);
    CHECK(strncmp(buf, "P0001", 5) == 0);
    CHECK(strcmp(dtc_format(0x0001, buf, sizeof buf), "P0001") == 0);
    CHECK(strcmp(dtc_format(0xd000, buf, sizeof buf), "U1000") == 0);
    CHECK(strcmp(dtc_format(0x9000, buf, sizeof buf), "B1000") == 0);
    CHECK_EQ(dtc_obd_string(0x3fff), 0);
    CHECK_EQ(dtc_vag_string(1), 0xa0000001);
    CHECK_EQ(dtc_uds_string(0x0001, 0x00), 0xa1000100);
    str_get(0xa1000100, buf, sizeof buf);
    CHECK(strncmp(buf, "P000100", 7) == 0);
    CHECK_EQ(dtc_uds_string(0x9000, 0x11), 0xa1900011);

    /* UDSDTCTransID: identity rows and the big EV_ECM section */
    CHECK_EQ(dtc_uds_translate("EV_AirCondiCompr", 0x908001), 0xa1908001);
    CHECK_EQ(dtc_uds_translate("EV_ACC", 0), 0xa1a00ff0);
    CHECK_EQ(dtc_uds_translate("EV_ACC", 1), 0xa1c04600);
    CHECK(dtc_uds_translate("EV_ECM", 0x3a) != 0);
    CHECK_EQ(dtc_uds_translate("EV_NOPE", 1), 0);

    /* DsTransID */
    CHECK_EQ(ds_block_text(0x1001, 1, 1), 0x02030d94);
    str_get(0x02030d94, buf, sizeof buf);
    CHECK(strncmp(buf, "Engine Speed", 12) == 0);
    CHECK_EQ(ds_block_text(0x1001, 1, 2), 0x020308c0);
    CHECK_EQ(ds_block_text(0x1c44, 5, 2), 0x020326c9);
    CHECK_EQ(ds_block_text(0x1001, 0, 0), 0x12000000);
    CHECK_EQ(ds_block_text(0x0fff, 1, 1), 0);
    CHECK_EQ(ds_value_text(0), 0x02050001);
    CHECK_EQ(ds_default_name(2), 0x02050000 | (ds_default_name(2) & 0xffff));
    CHECK(str_get(ds_default_name(2), buf, sizeof buf) > 0);

    /* SYSSCAN */
    CHECK_EQ(sysscan_module_count(), 554);
    CHECK_EQ(sysscan_module(0, &m), 0);
    CHECK_EQ(m.address, 1);
    CHECK_EQ(m.count, 5);
    CHECK_EQ(m.record[0], 0x30119);
    CHECK_EQ(m.record[4], 0xb24f9);
    CHECK_EQ(sysscan_find_module(3, &m), 0);
    CHECK_EQ(m.count, 4);
    CHECK_EQ(sysscan_record(m.record[2], &r), 0);       /* 0xb2a26: 148 data sets */
    CHECK_EQ(r.offset, 0xb2a26);
    CHECK_EQ(r.flags, 0x30);
    CHECK_EQ(r.item_count, 3);
    CHECK_EQ(r.item[0].type, 0x30);
    CHECK_EQ(r.tail_count, 148);
    CHECK_EQ(sysscan_tail(&r, 0, &t), 0);
    CHECK_EQ(t.sub, 1); CHECK_EQ(t.x, 0x0330);
    CHECK_EQ(sysscan_tail(&r, 147, &t), 0);
    CHECK_EQ(t.sub, 0xffff); CHECK_EQ(t.v, 1);
    CHECK_EQ(sysscan_tail(&r, 148, &t), -1);
    CHECK_EQ(sysscan_record(0x30119, &r), 0);
    CHECK_EQ(r.item_count, 5);
    CHECK_EQ(r.item[0].type, 0x56);
    CHECK_EQ(r.tail_count, 1);
    sysscan_tail(&r, 0, &t);
    CHECK_EQ(t.v, 0x87);
    CHECK_EQ(sysscan_record(0x301bf, &r), 0);
    CHECK(r.has_script);
    CHECK_EQ(sysscan_record(0x12345, &r), -1);
    CHECK_EQ(sysscan_procedure(0xf001, recs, 8), 6);
    CHECK_EQ(recs[0], 0x269);
    CHECK_EQ(sysscan_procedure(0xf003, recs, 8), 1);
    CHECK_EQ(recs[0], 0x8e);
    CHECK_EQ(sysscan_procedure(0xf0ff, recs, 8), -1);
    CHECK_EQ(sysscan_ev_lookup("EV_ECM00CPI01104C907309AF001", &sub, &x), 0);
    CHECK_EQ(sub, 1); CHECK_EQ(x, 0x0130);
    CHECK_EQ(sysscan_ev_lookup("EV_NOTHING", &sub, &x), -1);
    str_get(sysscan_module_name_id(0x19), buf, sizeof buf);
    CHECK(strcmp(buf, "0019-Gateway") == 0);
    TEST_MAIN_END();
}
