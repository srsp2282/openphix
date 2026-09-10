#include "fake.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "protocol.h"

static void set_reply(struct obd_fake *f, uint8_t cmd, const uint8_t *payload, size_t len)
{
    memset(f->reply, 0, sizeof f->reply);
    f->reply[0] = 0x55;
    f->reply[1] = 0xAA;
    f->reply[2] = (uint8_t)(cmd + OBD_ACK_FLAG);
    if (payload && len)
        memcpy(f->reply + 3, payload, len > 12 ? 12 : len);
    f->reply[15] = obd_checksum8(f->reply, 15);
    f->reply_len = 16;
}

static int ensure_flash(struct obd_fake *f)
{
    if (!f->flash) {
        f->flash = malloc(f->flash_size);
        if (!f->flash)
            return -1;
        memset(f->flash, 0xFF, f->flash_size);
    }
    return 0;
}

static void commit_upload(struct obd_fake *f)
{
    if (f->upload_kind == OBD_CMD_BEGIN_MCU && f->mcu_image_count < OBD_FAKE_MAX_IMAGES) {
        uint8_t *copy = malloc(f->upload_len ? f->upload_len : 1);
        if (copy) {
            memcpy(copy, f->upload_buf, f->upload_len);
            f->mcu_images[f->mcu_image_count] = copy;
            f->mcu_image_len[f->mcu_image_count] = f->upload_len;
            f->mcu_image_count++;
        }
    } else if (f->upload_kind == OBD_CMD_BEGIN_DATA) {
        uint8_t *copy = malloc(f->upload_len ? f->upload_len : 1);
        if (copy) {
            memcpy(copy, f->upload_buf, f->upload_len);
            free(f->ext_image);
            f->ext_image = copy;
            f->ext_image_len = f->upload_len;
        }
    }
    f->upload_kind = 0;
    f->upload_len = 0;
}

static int fake_write_cmd(struct obd_transport *t, const uint8_t *data, size_t len, unsigned timeout)
{
    struct obd_fake *f = t->priv;
    uint8_t cmd;
    (void)timeout;
    f->reply_len = 0;
    if (len != OBD_CMD_SIZE || data[0] != 0x55 || data[1] != 0xAA ||
        data[15] != obd_checksum8(data, 15)) {
        f->error = "malformed command packet";
        return (int)len;
    }
    cmd = data[2];
    if (f->log_count < OBD_FAKE_MAX_LOG) {
        f->log[f->log_count].cmd = cmd;
        memcpy(f->log[f->log_count].params, data + 3, 12);
        f->log_count++;
    }
    switch (cmd) {
    case OBD_CMD_GET_VERSION:
        if (f->supports_version_cmd)
            set_reply(f, cmd, (const uint8_t *)f->version, strlen(f->version));
        break;
    case OBD_CMD_GET_FLASH_SIZE:
        if (f->supports_size_cmd) {
            uint8_t p[4] = {(uint8_t)f->flash_size, (uint8_t)(f->flash_size >> 8),
                            (uint8_t)(f->flash_size >> 16), (uint8_t)(f->flash_size >> 24)};
            set_reply(f, cmd, p, 4);
        }
        break;
    case OBD_CMD_BEGIN_MCU:
    case OBD_CMD_BEGIN_DATA:
        commit_upload(f);
        f->upload_kind = cmd;
        set_reply(f, cmd, NULL, 0);
        break;
    case OBD_CMD_BLOCK:
        if (!f->upload_kind) {
            f->error = "CMD_BLOCK without a begin command";
            break;
        }
        f->pending_block_len = (data[3] << 8) | data[4];
        f->pending_block_index = (data[5] << 8) | data[6];
        set_reply(f, cmd, NULL, 0);
        break;
    case OBD_CMD_READ_FLASH: {
        uint32_t addr = (uint32_t)data[3] | ((uint32_t)data[4] << 8) | ((uint32_t)data[5] << 16) |
                        ((uint32_t)data[6] << 24);
        int n = data[7] | (data[8] << 8);
        if ((uint64_t)addr + (uint64_t)n > f->flash_size || n <= 0)
            break; /* no reply, like a device that ignores bad addresses */
        f->pending_read_addr = addr;
        f->pending_read_len = n;
        set_reply(f, cmd, NULL, 0);
        break;
    }
    case OBD_CMD_FINISH:
        f->finished++;
        commit_upload(f);
        set_reply(f, cmd, NULL, 0);
        break;
    default:
        break;
    }
    return (int)len;
}

static int fake_read_cmd(struct obd_transport *t, uint8_t *buf, size_t len, unsigned timeout)
{
    struct obd_fake *f = t->priv;
    size_t n = f->reply_len < len ? f->reply_len : len;
    (void)timeout;
    if (n)
        memcpy(buf, f->reply, n);
    f->reply_len = 0;
    return (int)n;
}

static int fake_write_bulk(struct obd_transport *t, const uint8_t *data, size_t len, unsigned timeout)
{
    struct obd_fake *f = t->priv;
    size_t need;
    (void)timeout;
    if (f->pending_block_len < 0) {
        f->error = "bulk data without CMD_BLOCK";
        return -1;
    }
    if (!obd_verify_data_packet(data, len)) {
        f->error = "bad data packet checksum";
        return -1;
    }
    if ((size_t)f->pending_block_index != f->upload_len / OBD_BLOCK_SIZE) {
        f->error = "block index out of order";
        return -1;
    }
    need = f->upload_len + (size_t)f->pending_block_len;
    if (need > f->upload_cap) {
        size_t cap = f->upload_cap ? f->upload_cap * 2 : 0x10000;
        while (cap < need)
            cap *= 2;
        uint8_t *nb = realloc(f->upload_buf, cap);
        if (!nb)
            return -1;
        f->upload_buf = nb;
        f->upload_cap = cap;
    }
    memcpy(f->upload_buf + f->upload_len, data + 4, (size_t)f->pending_block_len);
    f->upload_len = need;
    f->pending_block_len = -1;
    set_reply(f, OBD_CMD_BLOCK, NULL, 0);
    return (int)len;
}

static int fake_read_bulk(struct obd_transport *t, uint8_t *buf, size_t len, unsigned timeout)
{
    struct obd_fake *f = t->priv;
    uint8_t pkt[OBD_DATA_PACKET_SIZE];
    (void)timeout;
    if (f->pending_read_addr < 0)
        return 0;
    if (ensure_flash(f) < 0)
        return -1;
    obd_build_data_packet(pkt, f->flash + f->pending_read_addr, (size_t)f->pending_read_len);
    f->pending_read_addr = -1;
    if (len > sizeof pkt)
        len = sizeof pkt;
    memcpy(buf, pkt, len);
    return (int)len;
}

static void fake_close(struct obd_transport *t)
{
    struct obd_fake *f = t->priv;
    if (f) {
        for (size_t i = 0; i < f->mcu_image_count; i++)
            free(f->mcu_images[i]);
        free(f->ext_image);
        free(f->upload_buf);
        free(f->flash);
        free(f);
    }
    free(t);
}

static const struct obd_transport_ops fake_ops = {
    fake_write_cmd, fake_read_cmd, fake_write_bulk, fake_read_bulk, fake_close,
};

struct obd_transport *obd_fake_open(const char *hardware, uint32_t flash_size, const char *version)
{
    struct obd_transport *t;
    struct obd_fake *f;
    uint16_t vid, pid;
    if (!obd_hardware_id(hardware, &vid, &pid))
        return NULL;
    t = calloc(1, sizeof *t);
    f = calloc(1, sizeof *f);
    if (!t || !f) {
        free(t);
        free(f);
        return NULL;
    }
    f->flash_size = flash_size ? flash_size : 0x2000000u;
    snprintf(f->version, sizeof f->version, "%s", version ? version : "1.30");
    f->supports_size_cmd = true;
    f->supports_version_cmd = true;
    f->pending_block_len = -1;
    f->pending_read_addr = -1;
    t->ops = &fake_ops;
    t->vid = vid;
    t->pid = pid;
    t->priv = f;
    return t;
}

struct obd_fake *obd_fake_state(struct obd_transport *t)
{
    return t->ops == &fake_ops ? (struct obd_fake *)t->priv : NULL;
}

int obd_fake_preload_flash(struct obd_transport *t, uint32_t address, const uint8_t *data, size_t len)
{
    struct obd_fake *f = obd_fake_state(t);
    if (!f || (uint64_t)address + len > f->flash_size || ensure_flash(f) < 0)
        return -1;
    memcpy(f->flash + address, data, len);
    return 0;
}

size_t obd_fake_count_cmd(struct obd_transport *t, uint8_t cmd)
{
    struct obd_fake *f = obd_fake_state(t);
    size_t n = 0;
    if (!f)
        return 0;
    for (size_t i = 0; i < f->log_count; i++)
        if (f->log[i].cmd == cmd)
            n++;
    return n;
}
