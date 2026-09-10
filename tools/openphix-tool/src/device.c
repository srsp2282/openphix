#include "device.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"
#include "protocol.h"

static int fail(struct obd_device *d, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d->err, sizeof d->err, fmt, ap);
    va_end(ap);
    return -1;
}

static void trace(struct obd_device *d, const char *dir, const uint8_t *p, size_t n)
{
    if (d->verbose < 2)
        return;
    fprintf(stderr, "%s ", dir);
    if (n == 0)
        fputs("(timeout)", stderr);
    for (size_t i = 0; i < n; i++)
        fprintf(stderr, "%02x", p[i]);
    fputc('\n', stderr);
}

void obd_device_init(struct obd_device *d, struct obd_transport *t)
{
    memset(d, 0, sizeof *d);
    d->t = t;
}

int obd_exchange(struct obd_device *d, uint8_t cmd, const uint8_t *payload, size_t plen,
                 uint8_t resp[16], unsigned ack_timeout, unsigned write_timeout)
{
    uint8_t pkt[OBD_CMD_SIZE];
    int n;
    if (obd_build_command(pkt, cmd, payload, plen) < 0)
        return fail(d, "payload too long for command 0x%02x", cmd);
    trace(d, "->", pkt, sizeof pkt);
    n = d->t->ops->write_cmd(d->t, pkt, sizeof pkt, write_timeout ? write_timeout : OBD_CMD_WRITE_TIMEOUT);
    if (n != OBD_CMD_SIZE)
        return fail(d, "short write of command 0x%02x (%d)", cmd, n);
    memset(resp, 0, OBD_CMD_SIZE);
    n = d->t->ops->read_cmd(d->t, resp, OBD_CMD_SIZE, ack_timeout ? ack_timeout : obd_ack_timeout_for(cmd));
    if (n < 0)
        return fail(d, "USB read failed after command 0x%02x (%d)", cmd, n);
    trace(d, "<-", resp, (size_t)n);
    return n;
}

int obd_command(struct obd_device *d, uint8_t cmd, const uint8_t *payload, size_t plen,
                uint8_t resp[16], unsigned ack_timeout, unsigned write_timeout)
{
    int n = obd_exchange(d, cmd, payload, plen, resp, ack_timeout, write_timeout);
    if (n < 0)
        return n;
    if (n == 0)
        return fail(d, "no reply to command 0x%02x", cmd);
    if (!obd_is_ack(resp, (size_t)n, cmd))
        return fail(d, "command 0x%02x rejected, reply byte 2 = 0x%02x", cmd, resp[2]);
    return 0;
}

int obd_get_version(struct obd_device *d, char *out, size_t outlen)
{
    uint8_t resp[OBD_CMD_SIZE];
    int n = obd_exchange(d, OBD_CMD_GET_VERSION, NULL, 0, resp, 0, 0);
    if (n < 0)
        return n;
    if (n == 0 || !obd_is_ack(resp, (size_t)n, OBD_CMD_GET_VERSION))
        return 0;
    obd_parse_version(resp, out, outlen);
    return 1;
}

int obd_get_flash_size(struct obd_device *d, uint32_t *size, bool probe)
{
    uint8_t resp[OBD_CMD_SIZE];
    uint8_t tmp[OBD_BLOCK_SIZE];
    int n = obd_exchange(d, OBD_CMD_GET_FLASH_SIZE, NULL, 0, resp, 0, 0);
    if (n < 0)
        return n;
    if (n > 0 && obd_is_ack(resp, (size_t)n, OBD_CMD_GET_FLASH_SIZE)) {
        *size = obd_parse_flash_size(resp);
        return 1;
    }
    if (!probe)
        return 0;
    for (size_t i = 0; i < 4; i++) {
        uint32_t cand = obd_flash_size_candidates[i];
        if (obd_read_flash(d, cand - OBD_BLOCK_SIZE, tmp, OBD_BLOCK_SIZE, OBD_BULK_PROBE_TIMEOUT) == 0) {
            *size = cand;
            return 1;
        }
    }
    d->err[0] = 0;
    return 0;
}

int obd_read_flash(struct obd_device *d, uint32_t address, uint8_t *buf, size_t len, unsigned bulk_timeout)
{
    uint8_t cmd[OBD_CMD_SIZE], resp[OBD_CMD_SIZE], pkt[OBD_DATA_PACKET_SIZE];
    int n;
    if (obd_build_read_flash(cmd, address, len) < 0)
        return fail(d, "bad flash read length %zu", len);
    if (obd_command(d, OBD_CMD_READ_FLASH, cmd + 3, 6, resp, 0, 0) < 0)
        return -1;
    n = d->t->ops->read_bulk(d->t, pkt, sizeof pkt, bulk_timeout ? bulk_timeout : OBD_BULK_TIMEOUT);
    if (d->verbose >= 2 && n > 0) {
        size_t shown = n < 16 ? (size_t)n : 16;
        fprintf(stderr, "<= bulk %d bytes, head ", n);
        for (size_t i = 0; i < shown; i++)
            fprintf(stderr, "%02x", pkt[i]);
        if (n == (int)sizeof pkt)
            fprintf(stderr, " tail %02x%02x%02x%02x", pkt[n - 4], pkt[n - 3], pkt[n - 2], pkt[n - 1]);
        fputc('\n', stderr);
    }
    if (n != (int)sizeof pkt)
        return fail(d, "short flash read at 0x%x (%d bytes)", address, n);
    if (!obd_verify_data_packet(pkt, sizeof pkt)) {
        if (d->strict)
            return fail(d, "bad header or checksum in flash data at 0x%x", address);
        if (d->verbose)
            fprintf(stderr, "warning: bad header or checksum in flash data at 0x%x\n", address);
    }
    memcpy(buf, pkt + 4, len);
    return 0;
}

int obd_read_flash_range(struct obd_device *d, uint32_t address, uint8_t *buf, size_t len,
                         obd_progress_fn progress, void *ctx)
{
    size_t done = 0;
    while (done < len) {
        size_t n = len - done < OBD_BLOCK_SIZE ? len - done : OBD_BLOCK_SIZE;
        if (obd_read_flash(d, address + (uint32_t)done, buf + done, n, 0) < 0)
            return -1;
        done += n;
        if (progress)
            progress(done, len, ctx);
    }
    return 0;
}

int obd_write_image(struct obd_device *d, uint8_t kind, const uint8_t *data, size_t len,
                    obd_progress_fn progress, void *ctx)
{
    uint8_t cmd[OBD_CMD_SIZE], resp[OBD_CMD_SIZE];
    uint8_t *pkt;
    uint32_t nblocks;
    if (kind != OBD_CMD_BEGIN_MCU && kind != OBD_CMD_BEGIN_DATA)
        return fail(d, "bad image kind 0x%02x", kind);
    if (obd_build_begin(cmd, kind, len) < 0)
        return fail(d, "image too large (%zu bytes)", len);
    nblocks = obd_block_count(len);
    if (obd_command(d, kind, cmd + 3, 2, resp, OBD_ACK_TIMEOUT_UPLOAD, OBD_CMD_WRITE_TIMEOUT_UPLOAD) < 0)
        return -1;
    pkt = malloc(OBD_DATA_PACKET_SIZE);
    if (!pkt)
        return fail(d, "out of memory");
    for (uint32_t i = 0; i < nblocks; i++) {
        size_t off = (size_t)i * OBD_BLOCK_SIZE;
        size_t n = len - off < OBD_BLOCK_SIZE ? len - off : OBD_BLOCK_SIZE;
        int r;
        obd_build_block_header(cmd, n, i);
        if (obd_command(d, OBD_CMD_BLOCK, cmd + 3, 4, resp, OBD_ACK_TIMEOUT_UPLOAD, OBD_CMD_WRITE_TIMEOUT_UPLOAD) < 0)
            goto err;
        obd_build_data_packet(pkt, data + off, n);
        r = d->t->ops->write_bulk(d->t, pkt, OBD_DATA_PACKET_SIZE, OBD_BULK_TIMEOUT);
        if (r != (int)OBD_DATA_PACKET_SIZE) {
            fail(d, "short bulk write in block %u (%d)", i, r);
            goto err;
        }
        r = d->t->ops->read_cmd(d->t, resp, OBD_CMD_SIZE, OBD_BULK_TIMEOUT);
        trace(d, "<-", resp, r > 0 ? (size_t)r : 0);
        if (r <= 0) {
            fail(d, "no reply after data block %u", i);
            goto err;
        }
        if (!obd_is_ack(resp, (size_t)r, OBD_CMD_BLOCK)) {
            if (d->strict) {
                fail(d, "unexpected reply after data block %u: 0x%02x", i, resp[2]);
                goto err;
            }
            if (d->verbose)
                fprintf(stderr, "warning: unexpected reply after data block %u: 0x%02x\n", i, resp[2]);
        }
        if (progress)
            progress(off + n, len, ctx);
    }
    free(pkt);
    return 0;
err:
    free(pkt);
    return -1;
}

int obd_finish(struct obd_device *d)
{
    uint8_t resp[OBD_CMD_SIZE];
    int n = obd_exchange(d, OBD_CMD_FINISH, NULL, 0, resp, OBD_ACK_TIMEOUT_UPLOAD, OBD_CMD_WRITE_TIMEOUT_UPLOAD);
    return n < 0 ? n : 0; /* the vendor tool ignores the reply */
}

int obd_read_feedback(struct obd_device *d, uint32_t flash_size, uint8_t *buf, obd_progress_fn progress, void *ctx)
{
    if (flash_size < OBD_FEEDBACK_AREA_OFFSET)
        return fail(d, "flash too small");
    if (obd_read_flash_range(d, flash_size - OBD_FEEDBACK_AREA_OFFSET, buf,
                             OBD_FEEDBACK_BLOCKS * OBD_BLOCK_SIZE, progress, ctx) < 0)
        return -1;
    if (buf[0] == 0xFF && buf[1] == 0xFF && buf[2] == 0xFF && buf[3] == 0xFF)
        return 0;
    return 1;
}

static int cmp_key(const void *a, const void *b)
{
    const struct obd_dtc_record *x = a, *y = b;
    if (x->key != y->key)
        return x->key < y->key ? -1 : 1;
    return x->sequence < y->sequence ? -1 : (x->sequence > y->sequence);
}

int obd_parse_dtc_records(const uint8_t *raw, size_t len, struct obd_dtc_record **out, size_t *count)
{
    size_t nrec = len / OBD_BLOCK_SIZE, n = 0;
    struct obd_dtc_record *recs = calloc(nrec ? nrec : 1, sizeof *recs);
    if (!recs)
        return -1;
    for (size_t i = 0; i < nrec; i++) {
        const uint8_t *rec = raw + i * OBD_BLOCK_SIZE;
        struct obd_dtc_record *r;
        size_t ndtc;
        if (memcmp(rec, OBD_DTC_RECORD_MAGIC, 8) != 0)
            continue;
        r = &recs[n];
        r->sequence = (unsigned)i;
        r->key = (uint32_t)rec[9] | ((uint32_t)rec[10] << 8) | ((uint32_t)rec[11] << 16) | ((uint32_t)rec[12] << 24);
        memcpy(r->vin, rec + 13, 17);
        r->vin[17] = 0;
        ndtc = rec[0x1E];
        r->dtcs = calloc(ndtc ? ndtc : 1, sizeof *r->dtcs);
        if (!r->dtcs)
            goto oom;
        for (size_t j = 0; j < ndtc; j++) {
            const uint8_t *e = rec + 0x20 + j * 8;
            size_t off = (((size_t)e[1] << 8) | e[0]) & 0xFFF;
            size_t tlen = (size_t)((e[3] * 256 + e[2] - 1) & 0xFFFF);
            uint16_t code = (uint16_t)((e[4] << 8) | e[5]);
            struct obd_dtc *dt = &r->dtcs[r->ndtcs];
            if (off + tlen > OBD_BLOCK_SIZE)
                tlen = off < OBD_BLOCK_SIZE ? OBD_BLOCK_SIZE - off : 0;
            obd_format_dtc(code, dt->code);
            dt->text = malloc(tlen + 1);
            if (!dt->text)
                goto oom;
            memcpy(dt->text, rec + off, tlen);
            dt->text[tlen] = 0;
            r->ndtcs++;
        }
        n++;
    }
    qsort(recs, n, sizeof *recs, cmp_key);
    *out = recs;
    *count = n;
    return 0;
oom:
    obd_free_dtc_records(recs, n + 1);
    return -1;
}

void obd_free_dtc_records(struct obd_dtc_record *recs, size_t count)
{
    if (!recs)
        return;
    for (size_t i = 0; i < count; i++) {
        for (size_t j = 0; j < recs[i].ndtcs; j++)
            free(recs[i].dtcs[j].text);
        free(recs[i].dtcs);
    }
    free(recs);
}

void obd_render_dtc_report(FILE *out, const struct obd_dtc_record *recs, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        fprintf(out, "%02u\tVIN: %s\r\n\r\n\tDTCNUMBER: %02u\r\n\r\n", (unsigned)(i + 1), recs[i].vin,
                (unsigned)recs[i].ndtcs);
        for (size_t j = 0; j < recs[i].ndtcs; j++)
            fprintf(out, "\t%s %s\r\n\r\n", recs[i].dtcs[j].code, recs[i].dtcs[j].text);
    }
}

int obd_read_dtc_records(struct obd_device *d, uint32_t flash_size, struct obd_dtc_record **out,
                         size_t *count, obd_progress_fn progress, void *ctx)
{
    uint8_t *raw;
    int r;
    if (flash_size < OBD_DTC_AREA_OFFSET)
        return fail(d, "flash too small");
    raw = malloc(OBD_DTC_BLOCKS * OBD_BLOCK_SIZE);
    if (!raw)
        return fail(d, "out of memory");
    r = obd_read_flash_range(d, flash_size - OBD_DTC_AREA_OFFSET, raw, OBD_DTC_BLOCKS * OBD_BLOCK_SIZE, progress, ctx);
    if (r == 0)
        r = obd_parse_dtc_records(raw, OBD_DTC_BLOCKS * OBD_BLOCK_SIZE, out, count);
    free(raw);
    return r;
}

static void say(obd_status_fn status, void *ctx, const char *msg)
{
    if (status)
        status(msg, ctx);
}

int obd_run_update(struct obd_device *d, const uint8_t *mcu, size_t mcu_len,
                   const uint8_t *ext, size_t ext_len, const uint8_t *erase, size_t erase_len,
                   const struct obd_update_opts *opts, obd_progress_fn progress, obd_status_fn status,
                   void *ctx, double *elapsed)
{
    double t0 = obd_now();
    char msg[200];

    if (!opts->dm100_path && mcu && !opts->skip_erase) {
        char version[16];
        int have = obd_get_version(d, version, sizeof version);
        bool needed;
        if (have < 0)
            return -1;
        needed = have ? obd_erase_required(version) : true;
        snprintf(msg, sizeof msg, "bootloader version: %s", have ? version : "unknown");
        say(status, ctx, msg);
        if (needed || opts->force_erase) {
            if (!erase)
                return fail(d, "this device needs Erase.bin before the MCU image but the package has none; "
                               "use --skip-erase to override at your own risk");
            snprintf(msg, sizeof msg, "sending Erase.bin (%zu bytes)", erase_len);
            say(status, ctx, msg);
            if (obd_write_image(d, OBD_CMD_BEGIN_MCU, erase, erase_len, progress, ctx) < 0)
                return -1;
            if (obd_finish(d) < 0)
                return -1;
            say(status, ctx, "waiting 5 s for the device to erase");
            obd_sleep_ms(5000);
        }
    }
    if (mcu) {
        snprintf(msg, sizeof msg, "sending MCU code (%zu bytes)", mcu_len);
        say(status, ctx, msg);
        if (obd_write_image(d, OBD_CMD_BEGIN_MCU, mcu, mcu_len, progress, ctx) < 0)
            return -1;
    }
    if (ext) {
        snprintf(msg, sizeof msg, "sending external flash data (%zu bytes)", ext_len);
        say(status, ctx, msg);
        if (obd_write_image(d, OBD_CMD_BEGIN_DATA, ext, ext_len, progress, ctx) < 0)
            return -1;
    }
    say(status, ctx, "finishing");
    if (obd_finish(d) < 0)
        return -1;
    if (elapsed)
        *elapsed = obd_now() - t0;
    return 0;
}
