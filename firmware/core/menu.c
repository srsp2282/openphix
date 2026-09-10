#include "openphix/menu.h"

static struct res file;

int menu_init(const struct res *image)
{
    return res_open(image, "Menu.BIN", &file);
}

int menu_node(uint32_t offset, uint32_t *title, uint32_t *count)
{
    uint32_t n;
    if (offset + 8 > file.size)
        return -1;
    n = res_u32(&file, offset + 4);
    if (offset + 8 + 20u * n > file.size)
        return -1;
    if (title)
        *title = res_u32(&file, offset);
    if (count)
        *count = n;
    return 0;
}

int menu_entry(uint32_t offset, uint32_t index, struct menu_entry *e)
{
    uint32_t n, p;
    if (menu_node(offset, NULL, &n) != 0 || index >= n)
        return -1;
    p = offset + 8 + 20u * index;
    e->caption = res_u32(&file, p);
    e->action = res_u32(&file, p + 4);
    e->param = res_u32(&file, p + 8);
    if (e->action == 0 && e->param)
        e->kind = MENU_SUBMENU;
    else if (e->action & 0x80000000u)
        e->kind = MENU_SPFUNC;
    else if ((e->caption >> 16) == 0x0102)
        e->kind = MENU_MODULE;
    else if (e->action == 0x19FF || (e->action >= 0xF000 && e->action <= 0xF0FF))
        e->kind = MENU_SCAN;
    else
        e->kind = MENU_OTHER;
    return 0;
}
