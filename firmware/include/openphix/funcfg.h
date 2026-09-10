/* FUNCFG.BIN: what each control module supports and how to talk to it
 * (docs/data-image-format.md, section 15). One record per (protocol kind,
 * module, ECU variant); the record points at chunks for connect, keep
 * alive, version information, fault codes, erase and datastream, and at
 * item lists for basic settings, adaptation and login. */
#ifndef OPENPHIX_FUNCFG_H
#define OPENPHIX_FUNCFG_H

#include <stdint.h>

#include "openphix/res.h"

enum funcfg_kind {
    FK_KLINE_KWP2000 = 0x01,
    FK_TP20 = 0x02,
    FK_KWP1281 = 0x04,
    FK_KWP2000_FAST = 0x05,
    FK_TP16 = 0x06,
    FK_UDS = 0x30,
    FK_UDS_VARIANT = 0x31,
    FK_UDS_29BIT = 0x32,
    FK_CRAFTER = 0x70,
    FK_CRAFTER2 = 0x80
};

enum funcfg_slot {
    SLOT_CONNECT, SLOT_KEEPALIVE, SLOT_VERSION, SLOT_READ_DTC, SLOT_ERASE_DTC, SLOT_DATASTREAM,
    SLOT_BASIC_SETTINGS, SLOT_ADAPTATION, SLOT_LOGIN, SLOT_COUNT
};

struct funcfg_record {
    uint32_t offset;
    uint16_t variant;
    uint8_t kind, module;
    uint32_t proto;
    uint32_t slot[SLOT_COUNT];   /* 0 = not available */
};

struct funcfg_proto {
    uint8_t kind;        /* 2 = 11 bit ids, 3 = 29 bit ids */
    uint8_t flags;
    uint8_t nids;
    uint32_t id[2];      /* request id, response id (decoded from bxCAN form) */
    uint16_t timing[6];
};

struct funcfg_cmd {
    uint32_t cmd;        /* Cmd.BIN id */
    uint16_t arg;
    uint8_t flag;        /* mostly the expected reply length */
};

#define FUNCFG_MAX_CMDS 8

struct funcfg_step {
    uint32_t seq, exp;   /* Exp.BIN id evaluated on the reply */
    uint32_t label;      /* string id (version info) */
    uint8_t ncmd;
    struct funcfg_cmd cmd[FUNCFG_MAX_CMDS];
};

int funcfg_init(const struct res *image);
/* Look a record up. variant 0xFFFF is the fallback the vendor stores for
 * most (kind, module) pairs. Returns 0 on success. */
int funcfg_find(uint8_t kind, uint8_t module, uint16_t variant, struct funcfg_record *r);
/* The kinds that have any record for a module, as a bit set of the low
 * byte values (bit 0x30 -> UDS and so on), plus the count. */
int funcfg_kinds_for_module(uint8_t module, uint8_t *kinds, int cap);
int funcfg_proto(const struct funcfg_record *r, struct funcfg_proto *p);

/* Connect (slot 0) and keep alive (slot 1) step lists: step k, or -1. */
int funcfg_connect_step(const struct funcfg_record *r, int k, struct funcfg_step *s);
int funcfg_keepalive_step(const struct funcfg_record *r, int k, struct funcfg_step *s);
/* Version information steps (variant 0 of the chunk). */
int funcfg_version_step(const struct funcfg_record *r, int k, struct funcfg_step *s);
/* Read DTC entries of the UDS form: the command and the status expression. */
int funcfg_dtc_entry(const struct funcfg_record *r, int k, struct funcfg_step *s);
/* Datastream channels (UDS form): channel k -> id; then the table entry. */
int funcfg_channel_count(const struct funcfg_record *r);
int funcfg_channel(const struct funcfg_record *r, int k, uint16_t *id, uint32_t *name,
                   struct funcfg_step *read, uint32_t *conv_exp);
/* Item lists of the extra slots: title string id and item count; item k
 * caption. */
int funcfg_items(const struct funcfg_record *r, enum funcfg_slot slot, uint32_t *title, uint32_t *count);
int funcfg_item(const struct funcfg_record *r, enum funcfg_slot slot, uint32_t k, uint32_t *caption);

#endif
