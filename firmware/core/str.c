#include <stdio.h>
#include <string.h>

#include "openphix/str.h"

static struct str_table tables[STR_MAX_LANGS];
static int table_count;
static int current = -1, english = -1;

int str_init(const struct res *image)
{
    struct res dir;
    char name[65];
    table_count = 0;
    current = english = -1;
    if (res_open(image, "STRING.BIN", &dir) != 0)
        return 0;
    for (unsigned i = 0; i < res_count(&dir) && table_count < STR_MAX_LANGS; i++) {
        struct str_table *t = &tables[table_count];
        if (res_entry(&dir, i, name, sizeof name, &t->file) != 0)
            break;
        /* string_NN_xx.hix */
        if (strncmp(name, "string_", 7) != 0 || strlen(name) < 16)
            continue;
        t->number = (uint8_t)((name[7] - '0') * 10 + (name[8] - '0'));
        t->code[0] = name[10];
        t->code[1] = name[11];
        t->code[2] = 0;
        t->count = res_u32(&t->file, 0);
        if (t->number == 3)
            english = table_count;
        table_count++;
    }
    current = english >= 0 ? english : (table_count ? 0 : -1);
    return table_count;
}

int str_language_count(void) { return table_count; }
const struct str_table *str_language(int index)
{
    return index >= 0 && index < table_count ? &tables[index] : NULL;
}
int str_select(int index)
{
    if (index < 0 || index >= table_count)
        return -1;
    current = index;
    return 0;
}
int str_selected(void) { return current; }

uint32_t str_find(const struct str_table *t, uint32_t id)
{
    uint32_t lo = 0, hi = t->count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t k = res_u32(&t->file, 4 + mid * 8);
        if (k == id)
            return res_u32(&t->file, 4 + mid * 8 + 4);
        if (k < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return 0;
}

static int fetch(const struct str_table *t, uint32_t off, char *buf, size_t cap)
{
    /* the declared u16 length may overstate; copy up to the NUL */
    size_t n = 0;
    uint16_t declared = res_u16(&t->file, off);
    uint32_t p = off + 2;
    if (cap == 0)
        return -1;
    while (n + 1 < cap && n < declared) {
        uint8_t c = res_u8(&t->file, p + (uint32_t)n);
        if (c == 0)
            break;
        buf[n++] = (char)c;
    }
    buf[n] = 0;
    return (int)n;
}

int str_get(uint32_t id, char *buf, size_t cap)
{
    int order[2] = { current, english };
    for (int k = 0; k < 2; k++) {
        int i = order[k];
        uint32_t off;
        if (i < 0 || (k == 1 && i == current))
            continue;
        off = str_find(&tables[i], id);
        if (off)
            return fetch(&tables[i], off, buf, cap);
    }
    if (cap)
        buf[0] = 0;
    return -1;
}

const char *str_get_or_id(uint32_t id, char *buf, size_t cap)
{
    if (str_get(id, buf, cap) < 0)
        snprintf(buf, cap, "[%08x]", (unsigned)id);
    return buf;
}
