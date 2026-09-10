/* Menu.BIN: the "For VW" menu tree (docs/data-image-format.md, section 13). */
#ifndef OPENPHIX_MENU_H
#define OPENPHIX_MENU_H

#include <stdint.h>

#include "openphix/res.h"

enum menu_kind {
    MENU_SUBMENU,      /* param = offset of the sub menu node */
    MENU_MODULE,       /* action = diagnostic address */
    MENU_SCAN,         /* procedure id (param, or action for the Crafter branch) */
    MENU_SPFUNC,       /* action & 0x7fffffff = type, param = selector */
    MENU_OTHER
};

struct menu_entry {
    uint32_t caption;  /* string id */
    uint32_t action, param;
    enum menu_kind kind;
};

int menu_init(const struct res *image);
/* Node at `offset` (0 = root): title string id and entry count. */
int menu_node(uint32_t offset, uint32_t *title, uint32_t *count);
int menu_entry(uint32_t offset, uint32_t index, struct menu_entry *e);

#endif
