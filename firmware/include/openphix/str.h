/* String tables (docs/data-image-format.md, section 4). Strings are
 * addressed by a u32 id; the current language is searched first, English
 * second. Fetched strings are copied into a caller buffer, so nothing is
 * cached in RAM. */
#ifndef OPENPHIX_STR_H
#define OPENPHIX_STR_H

#include <stddef.h>
#include <stdint.h>

#include "openphix/res.h"

struct str_table {
    struct res file;      /* the string_NN_xx.hix */
    uint32_t count;
    uint8_t number;       /* NN */
    char code[4];         /* "en" */
};

#define STR_MAX_LANGS 16

/* Enumerate the languages in STRING.BIN. Returns the count. */
int str_init(const struct res *image);
int str_language_count(void);
const struct str_table *str_language(int index);
/* Select the current language by index into the list; English (number 3)
 * is always the fall back. Returns 0 on success. */
int str_select(int index);
int str_selected(void);

/* Copy the text of `id` into buf (cap bytes, always NUL terminated).
 * Returns the length, or -1 and an empty buffer when the id is unknown
 * in every table. */
int str_get(uint32_t id, char *buf, size_t cap);
/* Same, but returns a printable fallback ("[id]") instead of failing. */
const char *str_get_or_id(uint32_t id, char *buf, size_t cap);

/* Lookup in one table only. Returns the record offset or 0. */
uint32_t str_find(const struct str_table *t, uint32_t id);

#endif
