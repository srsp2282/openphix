#include "openphix/table.h"

uint32_t table_count(const struct res *file)
{
    uint32_t n = res_u32(file, 0);
    return 4 + n * 8 <= file->size ? n : 0;
}

int table_entry(const struct res *file, uint32_t k, uint32_t *id, struct res *rec)
{
    uint32_t n = table_count(file), off, next;
    if (k >= n)
        return -1;
    if (id)
        *id = res_u32(file, 4 + k * 8);
    off = res_u32(file, 4 + k * 8 + 4);
    next = k + 1 < n ? res_u32(file, 4 + (k + 1) * 8 + 4) : file->size;
    if (off > file->size || next < off || next > file->size)
        return -1;
    if (rec)
        *rec = res_slice(file, off, next - off);
    return 0;
}

int table_find(const struct res *file, uint32_t id, struct res *rec)
{
    uint32_t lo = 0, hi = table_count(file);
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t k = res_u32(file, 4 + mid * 8);
        if (k == id)
            return table_entry(file, mid, NULL, rec);
        if (k < id)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}
