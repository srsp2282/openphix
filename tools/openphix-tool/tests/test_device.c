#include <stdlib.h>
#include <string.h>

#include "../src/device.h"
#include "../src/fake.h"
#include "../src/protocol.h"
#include "test.h"

static uint8_t *image(size_t n, unsigned seed)
{
    uint8_t *b = malloc(n);
    for (size_t i = 0; i < n; i++)
        b[i] = (uint8_t)((i * seed + (i >> 8)) & 0xFF);
    return b;
}

static void no_status(const char *m, void *c) { (void)m; (void)c; }

static void test_queries(void)
{
    struct obd_transport *t = obd_fake_open("DM100", 0x1000000, "1.30");
    struct obd_device d;
    char v[16];
    uint32_t size = 0;
    obd_device_init(&d, t);
    CHECK_EQ(obd_get_version(&d, v, sizeof v), 1);
    CHECK(strcmp(v, "1.30") == 0);
    CHECK_EQ(obd_get_flash_size(&d, &size, true), 1);
    CHECK_EQ(size, 0x1000000);
    CHECK(strcmp(obd_hardware_name(t->vid, t->pid), "DM100") == 0);
    obd_transport_close(t);
}

static void test_flash_size_probe(void)
{
    struct obd_transport *t = obd_fake_open("DM300", 0x800000, "1.30");
    struct obd_device d;
    uint32_t size = 0;
    obd_fake_state(t)->supports_size_cmd = false;
    obd_device_init(&d, t);
    CHECK_EQ(obd_get_flash_size(&d, &size, true), 1);
    CHECK_EQ(size, 0x800000);
    obd_transport_close(t);
}

static void test_upload_roundtrip(void)
{
    struct obd_transport *t = obd_fake_open("DM100", 0, NULL);
    struct obd_fake *f = obd_fake_state(t);
    struct obd_device d;
    uint8_t *mcu = image(281728, 1), *ext = image(0x2345, 7);
    size_t nblk = 0, last_mcu = 0;
    obd_device_init(&d, t);
    d.strict = true;
    CHECK_EQ(obd_write_image(&d, OBD_CMD_BEGIN_MCU, mcu, 281728, NULL, NULL), 0);
    CHECK_EQ(obd_write_image(&d, OBD_CMD_BEGIN_DATA, ext, 0x2345, NULL, NULL), 0);
    CHECK_EQ(obd_finish(&d), 0);
    CHECK(f->error == NULL);
    CHECK_EQ(f->mcu_image_count, 1);
    CHECK_EQ(f->mcu_image_len[0], 281728);
    CHECK(memcmp(f->mcu_images[0], mcu, 281728) == 0);
    CHECK_EQ(f->ext_image_len, 0x2345);
    CHECK(memcmp(f->ext_image, ext, 0x2345) == 0);
    for (size_t i = 0; i < f->log_count; i++) {
        if (f->log[i].cmd == OBD_CMD_BLOCK) {
            if (nblk == 68)
                last_mcu = i;
            nblk++;
        }
    }
    CHECK_EQ(nblk, 69 + 3);
    CHECK_EQ((f->log[last_mcu].params[0] << 8) | f->log[last_mcu].params[1], 3200);
    CHECK_EQ((f->log[last_mcu].params[2] << 8) | f->log[last_mcu].params[3], 68);
    free(mcu);
    free(ext);
    obd_transport_close(t);
}

static void test_update_paths(void)
{
    struct obd_update_opts o = {0};
    struct obd_device d;
    struct obd_transport *t;
    uint8_t *mcu = image(5000, 1), *ext = image(9000, 3), *erase = image(100, 5);

    /* DM100 never asks for the version and never erases */
    t = obd_fake_open("DM100", 0, "1.20");
    obd_device_init(&d, t);
    d.strict = true;
    o.dm100_path = true;
    CHECK_EQ(obd_run_update(&d, mcu, 5000, ext, 9000, erase, 100, &o, NULL, no_status, NULL, NULL), 0);
    CHECK_EQ(obd_fake_state(t)->mcu_image_count, 1);
    CHECK_EQ(obd_fake_state(t)->finished, 1);
    CHECK_EQ(obd_fake_count_cmd(t, OBD_CMD_GET_VERSION), 0);
    obd_transport_close(t);

    /* DM300 with a new bootloader skips the erase */
    t = obd_fake_open("DM300", 0, "1.30");
    obd_device_init(&d, t);
    d.strict = true;
    o.dm100_path = false;
    CHECK_EQ(obd_run_update(&d, mcu, 5000, ext, 9000, erase, 100, &o, NULL, no_status, NULL, NULL), 0);
    CHECK_EQ(obd_fake_state(t)->mcu_image_count, 1);
    CHECK_EQ(obd_fake_state(t)->finished, 1);
    CHECK_EQ(obd_fake_count_cmd(t, OBD_CMD_GET_VERSION), 1);
    obd_transport_close(t);

    /* DM300 with an old bootloader and no Erase.bin fails before touching flash */
    t = obd_fake_open("DM300", 0, "1.26");
    obd_device_init(&d, t);
    CHECK_EQ(obd_run_update(&d, mcu, 5000, NULL, 0, NULL, 0, &o, NULL, no_status, NULL, NULL), -1);
    CHECK_EQ(obd_fake_state(t)->mcu_image_count, 0);
    CHECK(strstr(d.err, "Erase.bin") != NULL);
    obd_transport_close(t);

    /* --skip-erase bypasses the rule */
    t = obd_fake_open("DM300", 0, "1.26");
    obd_device_init(&d, t);
    o.skip_erase = true;
    CHECK_EQ(obd_run_update(&d, mcu, 5000, NULL, 0, NULL, 0, &o, NULL, no_status, NULL, NULL), 0);
    CHECK_EQ(obd_fake_state(t)->mcu_image_count, 1);
    obd_transport_close(t);
    o.skip_erase = false;

    /* data-only update on a DM300 never asks for the version */
    t = obd_fake_open("DM300", 0, "1.26");
    obd_device_init(&d, t);
    CHECK_EQ(obd_run_update(&d, NULL, 0, ext, 9000, NULL, 0, &o, NULL, no_status, NULL, NULL), 0);
    CHECK_EQ(obd_fake_count_cmd(t, OBD_CMD_GET_VERSION), 0);
    CHECK_EQ(obd_fake_state(t)->ext_image_len, 9000);
    obd_transport_close(t);

    free(mcu);
    free(ext);
    free(erase);
}

static void test_feedback(void)
{
    struct obd_transport *t = obd_fake_open("DM100", 0x2000000, NULL);
    struct obd_device d;
    uint8_t *buf = malloc(0x20000), *payload = image(0x20000, 11);
    obd_device_init(&d, t);
    CHECK_EQ(obd_read_feedback(&d, 0x2000000, buf, NULL, NULL), 0);
    CHECK_EQ(obd_fake_preload_flash(t, 0x2000000 - OBD_FEEDBACK_AREA_OFFSET, payload, 0x20000), 0);
    CHECK_EQ(obd_read_feedback(&d, 0x2000000, buf, NULL, NULL), 1);
    CHECK(memcmp(buf, payload, 0x20000) == 0);
    free(buf);
    free(payload);
    obd_transport_close(t);
}

static void put_entry(uint8_t *rec, int j, unsigned off, const char *txt, uint16_t code)
{
    uint8_t *e = rec + 0x20 + j * 8;
    size_t n = strlen(txt) + 1;
    e[0] = off & 0xFF;
    e[1] = off >> 8;
    e[2] = n & 0xFF;
    e[3] = (uint8_t)(n >> 8);
    e[4] = code >> 8;
    e[5] = code & 0xFF;
    memcpy(rec + off, txt, strlen(txt));
}

static void test_dtc_records(void)
{
    uint8_t *raw = malloc(3 * OBD_BLOCK_SIZE);
    uint8_t *rec = raw, *rec2 = raw + 2 * OBD_BLOCK_SIZE;
    struct obd_dtc_record *recs = NULL;
    size_t n = 0;
    const char *t1 = "O2 Sensor Circuit Low Voltage Bank 1 Sensor 3", *t2 = "Drive train data bus";
    memset(raw, 0xFF, 3 * OBD_BLOCK_SIZE);
    memcpy(rec, "AUTOPHIX", 8);
    rec[9] = 7; rec[10] = rec[11] = rec[12] = 0;
    memcpy(rec + 13, "WVWZZZ1KZ8W123456", 17);
    rec[0x1E] = 2;
    put_entry(rec, 0, 0x100, t1, 0x0143);
    put_entry(rec, 1, 0x100 + (unsigned)strlen(t1), t2, 0xC100);
    memcpy(rec2, rec, OBD_BLOCK_SIZE);
    rec2[9] = 3;
    rec2[0x1E] = 1;
    CHECK_EQ(obd_parse_dtc_records(raw, 3 * OBD_BLOCK_SIZE, &recs, &n), 0);
    CHECK_EQ(n, 2);
    CHECK_EQ(recs[0].sequence, 2);   /* sorted by key: 3 before 7 */
    CHECK_EQ(recs[1].sequence, 0);
    CHECK(strcmp(recs[1].vin, "WVWZZZ1KZ8W123456") == 0);
    CHECK_EQ(recs[1].ndtcs, 2);
    CHECK(strcmp(recs[1].dtcs[0].code, "P0143") == 0);
    CHECK(strcmp(recs[1].dtcs[0].text, t1) == 0);
    CHECK(strcmp(recs[1].dtcs[1].code, "U0100") == 0);
    CHECK(strcmp(recs[1].dtcs[1].text, t2) == 0);
    {
        char out[1024];
        FILE *f = tmpfile();
        size_t len;
        obd_render_dtc_report(f, recs, n);
        rewind(f);
        len = fread(out, 1, sizeof out - 1, f);
        out[len] = 0;
        fclose(f);
        CHECK(strstr(out, "02\tVIN: WVWZZZ1KZ8W123456") != NULL);
        CHECK(strstr(out, "\tP0143 O2 Sensor") != NULL);
    }
    obd_free_dtc_records(recs, n);
    {
        struct obd_transport *t = obd_fake_open("DM100", 0x2000000, NULL);
        struct obd_device d;
        obd_device_init(&d, t);
        obd_fake_preload_flash(t, 0x2000000 - OBD_DTC_AREA_OFFSET, rec, OBD_BLOCK_SIZE);
        CHECK_EQ(obd_read_dtc_records(&d, 0x2000000, &recs, &n, NULL, NULL), 0);
        CHECK_EQ(n, 1);
        obd_free_dtc_records(recs, n);
        obd_transport_close(t);
    }
    free(raw);
}

int main(void)
{
    test_queries();
    test_flash_size_probe();
    test_upload_roundtrip();
    test_update_paths();
    test_feedback();
    test_dtc_records();
    TEST_MAIN_END();
}
