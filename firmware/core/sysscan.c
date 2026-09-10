#include <string.h>

#include "openphix/sysscan.h"

static struct res file;
static uint32_t module_index_ptr, module_count;

#define MAX_EV_TABLES 8
static uint32_t ev_tables[MAX_EV_TABLES];
static int ev_table_count;

/* Find the "type 4" reference nodes (04 00 00 00, u32 id, u32 table,
 * 00000000, ffffffff) whose table's first row name starts with "EV_". They
 * sit at any byte alignment inside the script records, so the file is
 * scanned in RAM sized chunks once at init. */
static void find_ev_tables(void)
{
    uint8_t chunk[1024 + 20];
    uint32_t p = module_index_ptr;
    ev_table_count = 0;
    while (p + 20 <= file.size && ev_table_count < MAX_EV_TABLES) {
        uint32_t n = file.size - p;
        if (n > sizeof chunk)
            n = sizeof chunk;
        if (res_read(&file, p, chunk, n) != 0)
            return;
        for (uint32_t i = 0; i + 20 <= n; i++) {
            uint32_t tab;
            uint8_t head[24];
            if (chunk[i] != 4 || chunk[i + 1] || chunk[i + 2] || chunk[i + 3])
                continue;
            if (chunk[i + 12] || chunk[i + 13] || chunk[i + 14] || chunk[i + 15])
                continue;
            if (chunk[i + 16] != 0xFF || chunk[i + 17] != 0xFF || chunk[i + 18] != 0xFF || chunk[i + 19] != 0xFF)
                continue;
            tab = (uint32_t)chunk[i + 8] | ((uint32_t)chunk[i + 9] << 8) |
                  ((uint32_t)chunk[i + 10] << 16) | ((uint32_t)chunk[i + 11] << 24);
            if (tab + 24 > file.size || res_read(&file, tab, head, 24) != 0)
                continue;
            if (head[0] == 2 && memcmp(head + 21, "EV_", 3) == 0 && ev_table_count < MAX_EV_TABLES)
                ev_tables[ev_table_count++] = tab;
        }
        if (n < sizeof chunk)
            break;
        p += 1024;
    }
}

static void count_modules(void)
{
    uint32_t p = module_index_ptr;
    module_count = 0;
    while (p + 5 <= file.size && res_u32(&file, p) != 0xFFFFFFFFu) {
        uint8_t n = res_u8(&file, p + 4);
        p += 5 + 5u * n;
        module_count++;
    }
}

int sysscan_init(const struct res *image)
{
    if (res_open(image, "SYSSCAN.BIN", &file) != 0)
        return -1;
    module_index_ptr = res_u32(&file, 8);
    if (module_index_ptr >= file.size)
        return -1;
    count_modules();
    find_ev_tables();
    return 0;
}

uint32_t sysscan_module_count(void) { return module_count; }

int sysscan_module(uint32_t index, struct sysscan_module *m)
{
    uint32_t p = module_index_ptr;
    for (uint32_t i = 0; i < module_count; i++) {
        uint8_t n = res_u8(&file, p + 4);
        if (i == index) {
            m->address = res_u32(&file, p);
            m->count = n < SYSSCAN_MAX_RECORDS ? n : SYSSCAN_MAX_RECORDS;
            for (uint8_t k = 0; k < m->count; k++)
                m->record[k] = res_u32(&file, p + 5 + 5u * k + 1);
            return 0;
        }
        p += 5 + 5u * n;
    }
    return -1;
}

int sysscan_find_module(uint32_t address, struct sysscan_module *m)
{
    for (uint32_t i = 0; i < module_count; i++)
        if (sysscan_module(i, m) == 0 && m->address == address)
            return 0;
    return -1;
}

int sysscan_record(uint32_t offset, struct sysscan_record *r)
{
    uint32_t p;
    if (offset + 38 > file.size || res_u32(&file, offset) != 8)
        return -1;
    memset(r, 0, sizeof *r);
    r->offset = offset;
    r->flags = res_u8(&file, offset + 13);
    r->has_script = res_u8(&file, offset + 31) == 2;
    r->item_count = r->has_script ? 0 : res_u8(&file, offset + 37);
    if (r->item_count > 6)
        r->item_count = 6;
    for (uint8_t i = 0; i < r->item_count; i++) {
        uint32_t q = offset + 38 + 7u * i;
        r->item[i].key = res_u16(&file, q);
        r->item[i].addr = res_u8(&file, q + 2);
        r->item[i].type = res_u8(&file, q + 3);
        r->item[i].y = res_u16(&file, q + 4);
        r->item[i].z = res_u8(&file, q + 6);
    }
    p = offset + 38 + 7u * r->item_count;
    r->tail_offset = p;
    while (p + 2 <= file.size && res_u16(&file, p) != 0xFFFF) {
        if (res_u16(&file, p) != r->tail_count + 1u)
            break;
        r->tail_count++;
        p += 10;
    }
    r->end = p + 2;
    return 0;
}

int sysscan_tail(const struct sysscan_record *r, uint16_t k, struct sysscan_tail *t)
{
    uint32_t p = r->tail_offset + 10u * k;
    if (k >= r->tail_count)
        return -1;
    t->sub = res_u16(&file, p + 2);
    t->x = res_u16(&file, p + 4);
    t->ref = res_u32(&file, p + 2);
    t->v = res_u32(&file, p + 6);
    return 0;
}

int sysscan_procedure(uint32_t id, uint32_t *records, int cap)
{
    uint32_t p = 12;
    while (p + 6 <= file.size) {
        uint32_t k = res_u32(&file, p);
        uint8_t n;
        if (k == 0xFFFFFFFFu)
            break;
        n = res_u8(&file, p + 5);
        if (k == id) {
            int got = 0;
            for (uint8_t i = 0; i < n && got < cap; i++)
                records[got++] = res_u32(&file, p + 6 + 4u * i);
            return got;
        }
        p += 6 + 4u * n;
    }
    return -1;
}

/* Type 2 table rows: { u32 id, u16 sub, u16 X, u16 len, char name[len] } */
static int table_lookup(uint32_t tab, const char *name, uint16_t *sub, uint16_t *x)
{
    uint32_t cnt = res_u32(&file, tab + 1), q = tab + 11;
    char buf[48];
    size_t want = strlen(name);
    for (uint32_t i = 0; i < cnt && q + 10 <= file.size; i++) {
        uint16_t len = res_u16(&file, q + 8);
        if (len > sizeof buf)
            return -1;
        if (len == want + 1 || len > want) {
            res_read(&file, q + 10, buf, len);
            buf[len - 1] = 0;
            if (strcmp(buf, name) == 0) {
                *sub = res_u16(&file, q + 4);
                *x = res_u16(&file, q + 6);
                return 0;
            }
        }
        q += 10 + len;
    }
    return -1;
}

int sysscan_ev_lookup(const char *name, uint16_t *sub, uint16_t *x)
{
    for (int i = 0; i < ev_table_count; i++)
        if (table_lookup(ev_tables[i], name, sub, x) == 0)
            return 0;
    return -1;
}

uint32_t sysscan_module_name_id(uint32_t address)
{
    return 0x01020000u | (address & 0xFFFF);
}
