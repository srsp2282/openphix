#include <stdio.h>
#include <string.h>

#include "openphix/dtc.h"
#include "openphix/str.h"

static struct res ds_file, uds_file;
static struct res ds_text, ds_25h, ds_defname;

/* Binary search in a `u32 count, then sorted (u32 key, u32 value)` table. */
static uint32_t pair_lookup(const struct res *t, uint32_t key)
{
    uint32_t lo = 0, hi = res_u32(t, 0);
    if (4 + hi * 8 > t->size)
        return 0;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t k = res_u32(t, 4 + mid * 8);
        if (k == key)
            return res_u32(t, 4 + mid * 8 + 4);
        if (k < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

int dtc_init(const struct res *image)
{
    int rc = 0;
    if (res_open(image, "DsTransID.BIN", &ds_file) == 0) {
        res_open(&ds_file, "TEXT", &ds_text);
        res_open(&ds_file, "TEXT_25H", &ds_25h);
        res_open(&ds_file, "TEXT_DEFNAME_02H", &ds_defname);
    } else {
        rc = -1;
    }
    if (res_open(image, "UDSDTCTransID.BIN", &uds_file) != 0)
        rc = -1;
    return rc;
}

static uint32_t exists(uint32_t id)
{
    char buf[2];
    return str_get(id, buf, sizeof buf) >= 0 ? id : 0;
}

uint32_t dtc_obd_string(uint16_t code)
{
    return exists(0xFF000000u | code);
}

const char *dtc_format(uint16_t code, char *buf, size_t cap)
{
    static const char letters[4] = { 'P', 'C', 'B', 'U' };
    snprintf(buf, cap, "%c%04X", letters[code >> 14], code & 0x3FFF);
    return buf;
}

uint32_t dtc_vag_string(uint16_t number)
{
    return exists(0xA0000000u | number);
}

uint32_t dtc_uds_string(uint16_t code, uint8_t fault_type)
{
    uint32_t id = 0xA1000000u | ((uint32_t)code << 8) | fault_type;
    if (exists(id))
        return id;
    return exists(0xA1000000u | ((uint32_t)code << 8));    /* without the type */
}

uint32_t dtc_uds_translate(const char *module_type, uint32_t key)
{
    struct res sec;
    if (uds_file.size == 0 || res_open(&uds_file, module_type, &sec) != 0)
        return 0;
    return pair_lookup(&sec, key);
}

uint32_t ds_block_text(uint16_t group, uint8_t block, uint8_t field)
{
    return ds_text.size ? pair_lookup(&ds_text, ((uint32_t)group << 16) | ((uint32_t)block << 8) | field) : 0;
}

uint32_t ds_value_text(uint16_t n)
{
    return ds_25h.size ? pair_lookup(&ds_25h, n) : 0;
}

uint32_t ds_default_name(uint8_t data_type)
{
    return ds_defname.size ? pair_lookup(&ds_defname, data_type) : 0;
}
