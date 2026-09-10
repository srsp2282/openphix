#include <string.h>

#include "openphix/cmd.h"
#include "openphix/table.h"

static struct res cmd_file;

int cmd_init(const struct res *image)
{
    return res_open(image, "Cmd.BIN", &cmd_file);
}

int cmd_get(uint32_t id, struct cmd *c)
{
    uint8_t hdr[7];
    if (table_find(&cmd_file, id, &c->rec) != 0 || c->rec.size < 7)
        return -1;
    if (res_read(&c->rec, 0, hdr, 7) != 0)
        return -1;
    c->id = id;
    c->len_byte = hdr[0];
    c->kind = hdr[1];
    memcpy(c->h, hdr + 2, 5);
    c->payload = 7;
    c->payload_len = c->rec.size - 7;
    /* keepalives carrying K-line chunks have two extra bytes first */
    if (c->kind == CMD_KEEPALIVE && c->h[0] == 0x05 && c->payload_len >= 2) {
        c->payload += 2;
        c->payload_len -= 2;
    }
    return 0;
}

int cmd_next_packet(const struct cmd *c, uint32_t *pos, struct cmd_packet *pk)
{
    uint8_t blk[40];
    uint8_t blen;
    uint32_t p = c->payload + *pos;
    if (*pos >= c->payload_len)
        return 0;
    blen = res_u8(&c->rec, p);
    if (blen == 0 || blen > sizeof blk || p + 1 + blen > c->rec.size)
        return 0;
    if (res_read(&c->rec, p + 1, blk, blen) != 0)
        return 0;
    *pos += 1 + blen;
    memset(pk, 0, sizeof *pk);
    pk->dlc = blk[0] & 0x0F;
    pk->flags = blk[0] & 0xF0;
    {
        const uint8_t *body = blk + 1;
        int n = blen - 1;
        if (n == pk->dlc + 2) {
            pk->has_id = 1;
            pk->can_id = (uint16_t)(((body[0] << 8) | body[1]) >> 5);
            body += 2;
            n -= 2;
        }
        if (n > 8)
            n = 8;
        pk->len = (uint8_t)n;
        memcpy(pk->data, body, (size_t)n);
    }
    return 1;
}

int cmd_kline_message(const struct cmd *c, uint8_t *buf, int cap)
{
    uint32_t pos = 0;
    struct cmd_packet pk;
    int n = 0;
    if (c->kind != CMD_SEND && c->kind != CMD_KEEPALIVE)
        return -1;
    if (c->kind == CMD_SEND && c->h[0] != LINK_KLINE_KWP2000 && c->h[0] != LINK_KLINE_KWP1281)
        return -1;
    if (c->kind == CMD_KEEPALIVE && c->h[0] != 0x05 && c->h[0] != 0x11)
        return -1;
    while (cmd_next_packet(c, &pos, &pk)) {
        int op = pk.len ? pk.data[0] >> 4 : 0;
        if (pk.len < 1 || (op != 2 && op != 3))
            continue;
        for (int i = 1; i < pk.len; i++)
            if (n < cap)
                buf[n++] = pk.data[i];
    }
    return n;
}

uint16_t cmd_period_ms(const struct cmd *c)
{
    return (uint16_t)((c->h[3] << 8) | c->h[4]);
}

uint32_t cmd_number(const struct cmd *c)
{
    return ((uint32_t)c->h[1] << 24) | ((uint32_t)c->h[2] << 16) | ((uint32_t)c->h[3] << 8) | c->h[4];
}
