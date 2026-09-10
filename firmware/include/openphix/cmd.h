/* Diagnostic command records of Cmd.BIN (docs/data-image-format.md,
 * section 8). This module only decodes records; sending them is the job of
 * the protocol stacks. */
#ifndef OPENPHIX_CMD_H
#define OPENPHIX_CMD_H

#include <stdint.h>

#include "openphix/res.h"

enum cmd_kind {
    CMD_KEEPALIVE = 0x01,
    CMD_KLINE_RAW = 0x02,
    CMD_RAW_OTHER = 0x08,
    CMD_SEND = 0x10,
    CMD_KLINE_RAW_C4 = 0x25,
    CMD_SLOW_INIT = 0x30,
    CMD_KWP1281_BLOCK = 0x40,
    CMD_NUMBER = 0x60
};

enum cmd_link {
    LINK_CAN = 0x00,
    LINK_TP20_SETUP = 0x01,
    LINK_TP20_SETUP2 = 0x02,
    LINK_TP20_DATA = 0x05,
    LINK_KLINE_KWP2000 = 0x06,
    LINK_KLINE_KWP1281 = 0x07,
    LINK_CAN_B = 0x50,
    LINK_CAN_C = 0x52
};

struct cmd {
    uint32_t id;
    struct res rec;          /* the whole record */
    uint8_t len_byte, kind;
    uint8_t h[5];
    uint32_t payload;        /* offset of the payload inside rec */
    uint32_t payload_len;
};

struct cmd_packet {
    uint8_t dlc;             /* CAN data length (low nibble) */
    uint8_t flags;           /* high nibble of the dlc byte */
    int has_id;
    uint16_t can_id;         /* 11 bit id */
    uint8_t data[8];
    uint8_t len;             /* bytes in data */
};

int cmd_init(const struct res *image);
int cmd_get(uint32_t id, struct cmd *c);

/* Iterate packets of a kind 0x10 record (or a keepalive with packets).
 * `pos` starts at 0 and is advanced; returns 1 on a packet, 0 at the end. */
int cmd_next_packet(const struct cmd *c, uint32_t *pos, struct cmd_packet *pk);

/* Reassemble a chunked K-line message (link 0x06 / 0x07) into buf. Returns
 * the length, or -1 when the record is not chunked. */
int cmd_kline_message(const struct cmd *c, uint8_t *buf, int cap);

/* Keepalive period in ms, slow init address, number value. */
uint16_t cmd_period_ms(const struct cmd *c);
uint32_t cmd_number(const struct cmd *c);

#endif
