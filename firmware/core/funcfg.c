#include <string.h>

#include "openphix/funcfg.h"

static struct res file;
static uint32_t table_end;

int funcfg_init(const struct res *image)
{
    uint32_t p = 0;
    if (res_open(image, "FUNCFG.BIN", &file) != 0)
        return -1;
    /* the table ends at the terminator record (kind 0xFF), whose slot 0
     * holds the table end */
    table_end = 0;
    while (p + 33 <= file.size) {
        uint8_t kind = res_u8(&file, p + 2), n = res_u8(&file, p + 32);
        if (kind == 0xFF) {
            table_end = res_u32(&file, p + 8);
            break;
        }
        p += 33 + 4u * n;
    }
    return table_end ? 0 : -1;
}

static void read_record(uint32_t p, struct funcfg_record *r)
{
    uint8_t n = res_u8(&file, p + 32);
    memset(r, 0, sizeof *r);
    r->offset = p;
    r->variant = res_u16(&file, p);
    r->kind = res_u8(&file, p + 2);
    r->module = res_u8(&file, p + 3);
    r->proto = res_u32(&file, p + 4);
    for (int i = 0; i < 6; i++)
        r->slot[i] = res_u32(&file, p + 8 + 4u * i);
    for (int i = 0; i < 3 && i < n; i++)
        r->slot[6 + i] = res_u32(&file, p + 33 + 4u * i);
}

int funcfg_find(uint8_t kind, uint8_t module, uint16_t variant, struct funcfg_record *r)
{
    uint32_t p = 0;
    while (p + 33 <= table_end) {
        uint8_t n = res_u8(&file, p + 32);
        if (res_u8(&file, p + 2) == kind && res_u8(&file, p + 3) == module &&
            res_u16(&file, p) == variant) {
            read_record(p, r);
            return 0;
        }
        p += 33 + 4u * n;
    }
    return -1;
}

int funcfg_kinds_for_module(uint8_t module, uint8_t *kinds, int cap)
{
    uint32_t p = 0;
    int n = 0;
    while (p + 33 <= table_end) {
        uint8_t cnt = res_u8(&file, p + 32);
        if (res_u8(&file, p + 3) == module) {
            uint8_t k = res_u8(&file, p + 2);
            int seen = 0;
            for (int i = 0; i < n; i++)
                if (kinds[i] == k)
                    seen = 1;
            if (!seen && n < cap)
                kinds[n++] = k;
        }
        p += 33 + 4u * cnt;
    }
    return n;
}

int funcfg_proto(const struct funcfg_record *r, struct funcfg_proto *p)
{
    uint32_t q = r->proto;
    if (!q || q + 33 > file.size)
        return -1;
    memset(p, 0, sizeof *p);
    p->kind = res_u8(&file, q);
    p->flags = res_u8(&file, q + 1);
    p->nids = res_u8(&file, q + 15);
    if (p->nids > 2)
        p->nids = 2;
    for (int i = 0; i < p->nids; i++) {
        uint32_t raw = res_u32(&file, q + 16 + 4u * i);
        p->id[i] = p->kind == 3 ? raw >> 3 : (raw & 0xFFFF) >> 5;
    }
    q += 16 + 4u * res_u8(&file, q + 15) + 1;
    for (int i = 0; i < 6; i++)
        p->timing[i] = res_u16(&file, q + 2u * i);
    return 0;
}

/* cmd list: u8 count, count x { u32 cmd, u16 arg, u8 flag }. Returns the
 * offset after the list. */
static uint32_t read_cmds(uint32_t p, struct funcfg_step *s)
{
    uint8_t n = res_u8(&file, p);
    s->ncmd = 0;
    for (uint8_t i = 0; i < n; i++) {
        uint32_t q = p + 1 + 7u * i;
        if (s->ncmd < FUNCFG_MAX_CMDS) {
            s->cmd[s->ncmd].cmd = res_u32(&file, q);
            s->cmd[s->ncmd].arg = res_u16(&file, q + 4);
            s->cmd[s->ncmd].flag = res_u8(&file, q + 6);
            s->ncmd++;
        }
    }
    return p + 1 + 7u * n;
}

int funcfg_connect_step(const struct funcfg_record *r, int k, struct funcfg_step *s)
{
    uint32_t p = r->slot[SLOT_CONNECT], n, q;
    if (!p)
        return -1;
    n = res_u32(&file, p);
    if ((uint32_t)k >= n)
        return -1;
    q = p + 8;
    for (int i = 0; i <= k; i++) {
        memset(s, 0, sizeof *s);
        s->seq = res_u32(&file, q);
        s->exp = res_u32(&file, q + 4);
        q = read_cmds(q + 10, s);
    }
    return 0;
}

int funcfg_keepalive_step(const struct funcfg_record *r, int k, struct funcfg_step *s)
{
    uint32_t p = r->slot[SLOT_KEEPALIVE], n, q;
    if (!p)
        return -1;
    n = res_u16(&file, p + 4);
    if ((uint32_t)k >= n)
        return -1;
    q = p + 6;
    for (int i = 0; i <= k; i++) {
        memset(s, 0, sizeof *s);
        s->seq = res_u32(&file, q);
        s->exp = res_u32(&file, q + 4);
        q = read_cmds(q + 8, s);
    }
    return 0;
}

/* The chunks of slots 2..5 are either one node (u8 f, u16 x, u32 cond,
 * body) or a multi variant header. This returns the body of variant 0. */
static uint32_t chunk_body(uint32_t p)
{
    if (res_u8(&file, p) == 0xFF) {
        uint16_t nvar = res_u16(&file, p + 1);
        uint32_t q = p + 3 + 4u * nvar + 6;
        p = res_u32(&file, q);
    }
    return p + 7;
}

int funcfg_version_step(const struct funcfg_record *r, int k, struct funcfg_step *s)
{
    uint32_t p = r->slot[SLOT_VERSION], b, n, q;
    uint8_t fmt;
    if (!p)
        return -1;
    b = chunk_body(p);
    fmt = res_u8(&file, b + 19);
    n = res_u16(&file, b + 20);
    if ((uint32_t)k >= n)
        return -1;
    q = b + 22;
    for (int i = 0; i <= k; i++) {
        memset(s, 0, sizeof *s);
        s->seq = res_u16(&file, q);
        s->label = res_u32(&file, q + 2);
        if (fmt == 3) {
            s->exp = 0;
            q += 14;
        } else {
            s->exp = res_u32(&file, q + 9);
            q = read_cmds(q + 13, s);
        }
    }
    return 0;
}

int funcfg_dtc_entry(const struct funcfg_record *r, int k, struct funcfg_step *s)
{
    uint32_t p = r->slot[SLOT_READ_DTC], b, n, q;
    if (!p)
        return -1;
    b = chunk_body(p);
    if (res_u8(&file, b) != 2 || res_u8(&file, b + 14) != 6)
        return -1;                     /* K-line / TP2.0 form, not decoded */
    n = res_u16(&file, b + 27);
    if ((uint32_t)k >= n)
        return -1;
    q = b + 29;
    for (int i = 0; i <= k; i++) {
        memset(s, 0, sizeof *s);
        s->seq = res_u16(&file, q);
        s->exp = res_u32(&file, q + 6);
        q = read_cmds(q + 16, s);
    }
    return 0;
}

static int datastream_body(const struct funcfg_record *r, uint32_t *table, uint32_t *ids, uint32_t *n)
{
    uint32_t p = r->slot[SLOT_DATASTREAM], b;
    if (!p)
        return -1;
    b = chunk_body(p);
    if (res_u8(&file, b) != 0x15)
        return -1;
    *table = res_u32(&file, b + 20);
    *n = res_u16(&file, b + 24);
    *ids = b + 26;
    return 0;
}

int funcfg_channel_count(const struct funcfg_record *r)
{
    uint32_t t, ids, n;
    return datastream_body(r, &t, &ids, &n) == 0 ? (int)n : -1;
}

int funcfg_channel(const struct funcfg_record *r, int k, uint16_t *id, uint32_t *name,
                   struct funcfg_step *read, uint32_t *conv_exp)
{
    uint32_t t, ids, n, cnt, q;
    uint16_t want;
    if (datastream_body(r, &t, &ids, &n) != 0 || (uint32_t)k >= n)
        return -1;
    want = res_u16(&file, ids + 2u * k);
    *id = want;
    /* the table is sorted by channel id; a linear walk is enough here */
    cnt = res_u16(&file, t);
    q = t + 2;
    for (uint32_t i = 0; i < cnt; i++) {
        uint16_t cid = res_u16(&file, q);
        struct funcfg_step tmp;
        uint32_t after;
        if (cid == want) {
            *name = res_u32(&file, q + 2);
            memset(read, 0, sizeof *read);
            after = read_cmds(q + 14, read);
            *conv_exp = res_u32(&file, after + 2);
            return 0;
        }
        after = read_cmds(q + 14, &tmp);
        after = read_cmds(after + 6, &tmp);
        q = after;
    }
    return -1;
}

int funcfg_items(const struct funcfg_record *r, enum funcfg_slot slot, uint32_t *title, uint32_t *count)
{
    uint32_t p = slot < SLOT_COUNT ? r->slot[slot] : 0;
    if (!p || slot < SLOT_BASIC_SETTINGS)
        return -1;
    /* 7 byte node header, u32 title, u8 0, u16 0, u32 0, u32 title, u32 n */
    p += 7;
    *title = res_u32(&file, p);
    *count = res_u32(&file, p + 15);
    return 0;
}

int funcfg_item(const struct funcfg_record *r, enum funcfg_slot slot, uint32_t k, uint32_t *caption)
{
    uint32_t title, count, p;
    if (funcfg_items(r, slot, &title, &count) != 0 || k >= count)
        return -1;
    p = r->slot[slot] + 7 + 19 + 20u * k;
    *caption = res_u32(&file, p);
    return 0;
}
