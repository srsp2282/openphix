/* The control module database SYSSCAN.BIN (docs/data-image-format.md,
 * section 9): module index, scan records, data set keys and the part
 * number (EV name) tables. */
#ifndef OPENPHIX_SYSSCAN_H
#define OPENPHIX_SYSSCAN_H

#include <stdint.h>

#include "openphix/res.h"

#define SYSSCAN_MAX_RECORDS 8

struct sysscan_module {
    uint32_t address;        /* 0x0001..0x00E0 plain, 0x6000xxxx view, 0x8000xxxx menu group */
    uint8_t count;
    uint32_t record[SYSSCAN_MAX_RECORDS];   /* file offsets */
};

struct sysscan_item {
    uint16_t key;
    uint8_t addr, type;
    uint16_t y;
    uint8_t z;
};

struct sysscan_tail {
    uint16_t sub, x;         /* data set key (sub, X) or a file offset in ref */
    uint32_t ref, v;
};

struct sysscan_record {
    uint32_t offset, end;
    uint8_t flags;
    int has_script;
    uint8_t item_count;
    struct sysscan_item item[6];
    uint32_t tail_offset;    /* first tail entry */
    uint16_t tail_count;
};

int sysscan_init(const struct res *image);
uint32_t sysscan_module_count(void);
/* Walk the module index: index 0..count-1. Returns 0 on success. */
int sysscan_module(uint32_t index, struct sysscan_module *m);
int sysscan_find_module(uint32_t address, struct sysscan_module *m);
int sysscan_record(uint32_t offset, struct sysscan_record *r);
int sysscan_tail(const struct sysscan_record *r, uint16_t k, struct sysscan_tail *t);

/* Generic scan procedures (menu ids 0xF001..): the list of record offsets
 * to try. Returns the count, or -1 when the id is unknown. */
int sysscan_procedure(uint32_t id, uint32_t *records, int cap);

/* Look an identification string (an EV name) up in the EV tables of a
 * module record. Fills (sub, X) and returns 0 when found. */
int sysscan_ev_lookup(const char *name, uint16_t *sub, uint16_t *x);

/* The string id of a module's display name ("0001-Engine Control Module 1"). */
uint32_t sysscan_module_name_id(uint32_t address);

#endif
