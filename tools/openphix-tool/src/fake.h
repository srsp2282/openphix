/* In-process simulation of a scan tool, for tests and --simulate runs. */
#ifndef OPENPHIX_FAKE_H
#define OPENPHIX_FAKE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "transport.h"

#define OBD_FAKE_MAX_IMAGES 8
#define OBD_FAKE_MAX_LOG 8192

struct obd_fake_cmd {
    uint8_t cmd;
    uint8_t params[12];
};

struct obd_fake {
    uint32_t flash_size;
    char version[12];
    bool supports_size_cmd;
    bool supports_version_cmd;
    uint8_t *flash;               /* flash_size bytes, 0xFF filled, allocated lazily */
    uint8_t *mcu_images[OBD_FAKE_MAX_IMAGES];
    size_t mcu_image_len[OBD_FAKE_MAX_IMAGES];
    size_t mcu_image_count;
    uint8_t *ext_image;
    size_t ext_image_len;
    unsigned finished;
    struct obd_fake_cmd log[OBD_FAKE_MAX_LOG];
    size_t log_count;
    /* internal state */
    uint8_t reply[16];
    size_t reply_len;
    int upload_kind;
    uint8_t *upload_buf;
    size_t upload_len, upload_cap;
    int pending_block_len, pending_block_index;
    int64_t pending_read_addr;
    int pending_read_len;
    const char *error;            /* set when the host violates the protocol */
};

/* hardware: DM100, DM300 or DM100HC. Returns NULL for an unknown name. */
struct obd_transport *obd_fake_open(const char *hardware, uint32_t flash_size, const char *version);
struct obd_fake *obd_fake_state(struct obd_transport *t);
int obd_fake_preload_flash(struct obd_transport *t, uint32_t address, const uint8_t *data, size_t len);
size_t obd_fake_count_cmd(struct obd_transport *t, uint8_t cmd);

#endif
