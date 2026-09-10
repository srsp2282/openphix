/* High level operations on a connected scan tool. */
#ifndef OPENPHIX_DEVICE_H
#define OPENPHIX_DEVICE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "transport.h"

typedef void (*obd_progress_fn)(size_t done, size_t total, void *ctx);
typedef void (*obd_status_fn)(const char *msg, void *ctx);

struct obd_device {
    struct obd_transport *t;
    bool strict;        /* fail on mismatches the vendor tool ignores */
    int verbose;        /* 2: trace every packet to stderr */
    char err[256];      /* last error message */
};

void obd_device_init(struct obd_device *d, struct obd_transport *t);

/* Send one command, receive the 16 byte reply. Returns bytes received
 * (0 on timeout) or a negative error. ack_timeout 0 = vendor default. */
int obd_exchange(struct obd_device *d, uint8_t cmd, const uint8_t *payload, size_t plen,
                 uint8_t resp[16], unsigned ack_timeout, unsigned write_timeout);
/* obd_exchange that insists on an acknowledgement. 0 on success. */
int obd_command(struct obd_device *d, uint8_t cmd, const uint8_t *payload, size_t plen,
                uint8_t resp[16], unsigned ack_timeout, unsigned write_timeout);

/* 1: version copied to out; 0: command unsupported; <0: error. */
int obd_get_version(struct obd_device *d, char *out, size_t outlen);
/* 1: size known; 0: unknown; <0: error. probe = try the vendor's fallback reads. */
int obd_get_flash_size(struct obd_device *d, uint32_t *size, bool probe);
int obd_read_flash(struct obd_device *d, uint32_t address, uint8_t *buf, size_t len, unsigned bulk_timeout);
int obd_read_flash_range(struct obd_device *d, uint32_t address, uint8_t *buf, size_t len,
                         obd_progress_fn progress, void *ctx);
/* kind: OBD_CMD_BEGIN_MCU or OBD_CMD_BEGIN_DATA. */
int obd_write_image(struct obd_device *d, uint8_t kind, const uint8_t *data, size_t len,
                    obd_progress_fn progress, void *ctx);
int obd_finish(struct obd_device *d);

/* 1: feedback data copied (0x20000 bytes); 0: area empty; <0: error. */
int obd_read_feedback(struct obd_device *d, uint32_t flash_size, uint8_t *buf, obd_progress_fn progress, void *ctx);

struct obd_dtc {
    char code[6];
    char *text;
};
struct obd_dtc_record {
    unsigned sequence;
    uint32_t key;
    char vin[18];
    size_t ndtcs;
    struct obd_dtc *dtcs;
};
int obd_parse_dtc_records(const uint8_t *raw, size_t len, struct obd_dtc_record **out, size_t *count);
void obd_free_dtc_records(struct obd_dtc_record *recs, size_t count);
void obd_render_dtc_report(FILE *out, const struct obd_dtc_record *recs, size_t count);
int obd_read_dtc_records(struct obd_device *d, uint32_t flash_size, struct obd_dtc_record **out,
                         size_t *count, obd_progress_fn progress, void *ctx);

struct obd_update_opts {
    bool skip_erase;
    bool force_erase;
    bool dm100_path;    /* from obd_is_dm100_path(pid) */
};
/* Full update mirroring the vendor tool. Images may be NULL to skip them.
 * Returns 0 on success; elapsed seconds in *elapsed when non-NULL. */
int obd_run_update(struct obd_device *d, const uint8_t *mcu, size_t mcu_len,
                   const uint8_t *ext, size_t ext_len, const uint8_t *erase, size_t erase_len,
                   const struct obd_update_opts *opts, obd_progress_fn progress, obd_status_fn status,
                   void *ctx, double *elapsed);

#endif
