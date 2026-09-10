/* Indexed tables (docs/data-image-format.md): u32 count, then count pairs
 * of (u32 id, u32 offset) sorted by id, then the records. Shared by
 * Exp.BIN, Cmd.BIN and others. */
#ifndef OPENPHIX_TABLE_H
#define OPENPHIX_TABLE_H

#include <stdint.h>

#include "openphix/res.h"

/* Binary search for `id`. On success returns 0 and sets *rec to the
 * record window (from its offset to the next record's offset or the end
 * of the file). */
int table_find(const struct res *file, uint32_t id, struct res *rec);
uint32_t table_count(const struct res *file);
/* Record number k (0-based): id and window. Returns 0 on success. */
int table_entry(const struct res *file, uint32_t k, uint32_t *id, struct res *rec);

#endif
