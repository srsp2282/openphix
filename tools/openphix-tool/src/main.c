/* openphix-tool: command line updater for Autophix DM100/DM300 scan tools. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crypto.h"
#include "device.h"
#include "fake.h"
#include "package.h"
#include "platform.h"
#include "protocol.h"
#include "transport.h"

#define VERSION "0.1.0"

struct opts {
    int verbose;
    bool strict;
    int vid, pid;
    const char *simulate;
    const char *cmd;
    int argc;
    char **argv;
};

struct progress_state {
    int last;
    int tty;
    double t0;
};

static void progress(size_t done, size_t total, void *ctx)
{
    struct progress_state *s = ctx;
    int pct = total ? (int)((unsigned long long)done * 100 / total) : 100;
    char line[96];
    if (pct == s->last)
        return;
    if (!s->tty && pct % 10 != 0 && pct != 100)
        return;
    s->last = pct;
    snprintf(line, sizeof line, "  %3d%%  (%zu / %zu bytes, %.0f s)", pct, done, total, obd_now() - s->t0);
    if (s->tty) {
        fprintf(stderr, "\r%s%s", line, pct >= 100 ? "\n" : "");
    } else {
        fprintf(stderr, "%s\n", line);
    }
    fflush(stderr);
}

static void progress_init(struct progress_state *s)
{
    s->last = -1;
    s->tty = obd_isatty(stderr);
    s->t0 = obd_now();
}

static void status(const char *msg, void *ctx)
{
    (void)ctx;
    printf("%s\n", msg);
    fflush(stdout);
}

static void usage(FILE *f)
{
    fputs("usage: openphix-tool [global options] <command> [args]\n"
          "\n"
          "Open updater for Autophix DM100/DM300 based VAG scan tools\n"
          "(Autophix 9610, Ancel VD700, OBD2 SCANZ FST32, Biltema 15-1375 ...).\n"
          "\n"
          "global options:\n"
          "  -v, -vv            progress details / packet traces\n"
          "  --vid N --pid N    USB ids (default: any known tool)\n"
          "  --strict           fail on mismatches the vendor tool ignores\n"
          "  --simulate HW      built-in simulated device: DM100, DM300 or DM100HC\n"
          "  --version, --help\n"
          "\n"
          "commands:\n"
          "  list                         list connected scan tools\n"
          "  info                         hardware revision, bootloader version, flash size\n"
          "  package <dir> [--notes]      describe an unzipped update package\n"
          "  update <dir> [-y] [--mcu-only] [--data-only] [--skip-erase] [--force-erase]\n"
          "  feedback [-o Feedback.bin]   download the recorded bus log\n"
          "  dtc-review [-o file]         dump stored DTC sessions (vendor 'Review & Print')\n"
          "  read-flash -o file [-a addr] [-l len] [--decrypt]\n"
          "  decrypt <in> <out> [-a addr] deobfuscate an ExtFlashDat.bin or flash dump\n"
          "  encrypt <in> <out> [-a addr] the same transform in the other direction\n",
          f);
}

static struct obd_transport *open_transport(const struct opts *o)
{
    char err[256] = "";
    struct obd_transport *t;
    if (o->simulate) {
        t = obd_fake_open(o->simulate, 0, NULL);
        if (!t)
            fprintf(stderr, "unknown simulated hardware: %s\n", o->simulate);
        return t;
    }
    t = obd_usb_open(o->vid, o->pid, err, sizeof err);
    if (!t)
        fprintf(stderr, "USB error: %s\n", err);
    return t;
}

static int parse_int(const char *s, long *out)
{
    char *end;
    long v = strtol(s, &end, 0);
    if (*s == 0 || *end != 0)
        return -1;
    *out = v;
    return 0;
}

static const char *arg_value(struct opts *o, const char *name, const char *dflt)
{
    for (int i = 0; i < o->argc; i++)
        if (strcmp(o->argv[i], name) == 0 && i + 1 < o->argc)
            return o->argv[i + 1];
    return dflt;
}

static bool arg_flag(struct opts *o, const char *name)
{
    for (int i = 0; i < o->argc; i++)
        if (strcmp(o->argv[i], name) == 0)
            return true;
    return false;
}

static int cmd_list(struct opts *o)
{
    char err[256] = "";
    int n;
    if (o->simulate) {
        printf("(simulated) %s\n", o->simulate);
        return 0;
    }
    n = obd_usb_list(o->vid, o->pid, err, sizeof err);
    if (n < 0) {
        fprintf(stderr, "USB error: %s\n", err);
        return 3;
    }
    if (n == 0) {
        puts("no scan tool found");
        return 1;
    }
    return 0;
}

static int cmd_info(struct opts *o)
{
    struct obd_transport *t = open_transport(o);
    struct obd_device d;
    char version[16];
    uint32_t size = 0;
    int r, rc = 0;
    if (!t)
        return 3;
    obd_device_init(&d, t);
    d.strict = o->strict;
    d.verbose = o->verbose;
    printf("USB id:             %04x:%04x (%s)\n", t->vid, t->pid, obd_hardware_name(t->vid, t->pid));
    printf("update path:        %s (vendor tool logic)\n", obd_is_dm100_path(t->pid) ? "DM100" : "DM300");
    r = obd_get_version(&d, version, sizeof version);
    if (r < 0) {
        fprintf(stderr, "protocol error: %s\n", d.err);
        rc = 4;
        goto out;
    }
    printf("bootloader version: %s\n", r ? version : "not supported");
    r = obd_get_flash_size(&d, &size, true);
    if (r < 0) {
        fprintf(stderr, "protocol error: %s\n", d.err);
        rc = 4;
        goto out;
    }
    if (r) {
        uint8_t blk[OBD_BLOCK_SIZE];
        printf("external flash:     %u bytes (%u MiB)\n", size, size >> 20);
        if (obd_read_flash(&d, size - OBD_FEEDBACK_AREA_OFFSET, blk, sizeof blk, 0) == 0)
            printf("feedback area:      %s\n",
                   (blk[0] & blk[1] & blk[2] & blk[3]) == 0xFF ? "empty" : "contains data");
    } else {
        puts("external flash:     unknown");
    }
out:
    obd_transport_close(t);
    return rc;
}

static int cmd_package(struct opts *o)
{
    struct obd_package p;
    char path[OBD_PATH_MAX];
    if (o->argc < 1) {
        fputs("usage: package <dir> [--notes]\n", stderr);
        return 2;
    }
    if (obd_package_open(&p, o->argv[0]) < 0) {
        fprintf(stderr, "error: no bin/ directory found under %s (unzip the package first)\n", o->argv[0]);
        return 2;
    }
    obd_package_print_summary(&p, stdout);
    if (arg_flag(o, "--notes") && obd_package_notes(&p, path, sizeof path)) {
        size_t len;
        uint8_t *text = obd_read_file(path, &len);
        if (text) {
            putchar('\n');
            fwrite(text, 1, len, stdout);
            putchar('\n');
            free(text);
        }
    }
    return 0;
}

static int cmd_update(struct opts *o)
{
    struct obd_package p;
    struct obd_transport *t;
    struct obd_device d;
    struct obd_update_opts uo = {0};
    struct progress_state ps;
    char mcu_path[OBD_PATH_MAX], ext_path[OBD_PATH_MAX], erase_path[OBD_PATH_MAX];
    uint8_t *mcu = NULL, *ext = NULL, *erase = NULL;
    size_t mcu_len = 0, ext_len = 0, erase_len = 0;
    bool have_mcu, have_ext, have_erase;
    double elapsed = 0;
    int rc = 0;

    if (o->argc < 1) {
        fputs("usage: update <dir> [-y] [--mcu-only] [--data-only] [--skip-erase] [--force-erase]\n", stderr);
        return 2;
    }
    if (obd_package_open(&p, o->argv[0]) < 0) {
        fprintf(stderr, "error: no bin/ directory found under %s (unzip the package first)\n", o->argv[0]);
        return 2;
    }
    t = open_transport(o);
    if (!t)
        return 3;
    uo.dm100_path = obd_is_dm100_path(t->pid);
    uo.skip_erase = arg_flag(o, "--skip-erase");
    uo.force_erase = arg_flag(o, "--force-erase");
    printf("device: %04x:%04x (%s), update path: %s\n", t->vid, t->pid, obd_hardware_name(t->vid, t->pid),
           uo.dm100_path ? "DM100" : "DM300");
    have_mcu = obd_package_mcu(&p, uo.dm100_path, mcu_path, sizeof mcu_path);
    have_ext = obd_package_ext(&p, ext_path, sizeof ext_path);
    have_erase = obd_package_erase(&p, erase_path, sizeof erase_path);
    if (arg_flag(o, "--data-only"))
        have_mcu = false;
    if (arg_flag(o, "--mcu-only"))
        have_ext = false;
    if (!have_mcu && !have_ext) {
        fputs("error: package has neither an MCU image nor ExtFlashDat.bin\n", stderr);
        rc = 2;
        goto out;
    }
    if (have_mcu && !(mcu = obd_read_file(mcu_path, &mcu_len))) {
        fprintf(stderr, "error: cannot read %s\n", mcu_path);
        rc = 2;
        goto out;
    }
    if (have_ext && !(ext = obd_read_file(ext_path, &ext_len))) {
        fprintf(stderr, "error: cannot read %s\n", ext_path);
        rc = 2;
        goto out;
    }
    if (have_erase && !(erase = obd_read_file(erase_path, &erase_len))) {
        fprintf(stderr, "error: cannot read %s\n", erase_path);
        rc = 2;
        goto out;
    }
    if (mcu)
        printf("MCU image: %s (%zu bytes)\n", mcu_path, mcu_len);
    if (ext)
        printf("external flash image: %s (%zu bytes)\n", ext_path, ext_len);
    if (erase)
        printf("erase image: %s (%zu bytes)\n", erase_path, erase_len);
    if (!arg_flag(o, "-y") && !arg_flag(o, "--yes")) {
        char answer[16] = "";
        puts("\nThis rewrites the firmware of the tool. Do not unplug it during the update.");
        fputs("Continue? [y/N] ", stdout);
        fflush(stdout);
        if (!fgets(answer, sizeof answer, stdin) || (answer[0] != 'y' && answer[0] != 'Y')) {
            puts("aborted");
            rc = 1;
            goto out;
        }
    }
    obd_device_init(&d, t);
    d.strict = o->strict;
    d.verbose = o->verbose;
    progress_init(&ps);
    if (obd_run_update(&d, mcu, mcu_len, ext, ext_len, erase, erase_len, &uo, progress, status, &ps, &elapsed) < 0) {
        fprintf(stderr, "protocol error: %s\n", d.err);
        rc = 4;
        goto out;
    }
    printf("update successful, it took %.0f s\n", elapsed);
out:
    free(mcu);
    free(ext);
    free(erase);
    obd_transport_close(t);
    return rc;
}

static int get_size_or_fail(struct obd_device *d, uint32_t *size)
{
    int r = obd_get_flash_size(d, size, true);
    if (r < 0) {
        fprintf(stderr, "protocol error: %s\n", d->err);
        return 4;
    }
    if (r == 0) {
        fputs("error: cannot determine flash size\n", stderr);
        return 4;
    }
    return 0;
}

static int write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f || fwrite(data, 1, len, f) != len) {
        if (f)
            fclose(f);
        fprintf(stderr, "error: cannot write %s\n", path);
        return -1;
    }
    fclose(f);
    return 0;
}

static int cmd_feedback(struct opts *o)
{
    const char *out = arg_value(o, "-o", "Feedback.bin");
    struct obd_transport *t = open_transport(o);
    struct obd_device d;
    struct progress_state ps;
    uint32_t size;
    uint8_t *buf;
    int r, rc;
    if (!t)
        return 3;
    obd_device_init(&d, t);
    d.strict = o->strict;
    d.verbose = o->verbose;
    rc = get_size_or_fail(&d, &size);
    if (rc)
        goto out;
    buf = malloc(OBD_FEEDBACK_BLOCKS * OBD_BLOCK_SIZE);
    if (!buf) {
        rc = 2;
        goto out;
    }
    progress_init(&ps);
    r = obd_read_feedback(&d, size, buf, progress, &ps);
    if (r < 0) {
        fprintf(stderr, "protocol error: %s\n", d.err);
        rc = 4;
    } else if (r == 0) {
        puts("no feedback data on the device (run Tool Setup > Feedback and a diagnostic session first)");
        rc = 1;
    } else if (write_file(out, buf, OBD_FEEDBACK_BLOCKS * OBD_BLOCK_SIZE) == 0) {
        printf("wrote %u bytes to %s\n", OBD_FEEDBACK_BLOCKS * OBD_BLOCK_SIZE, out);
    } else {
        rc = 2;
    }
    free(buf);
out:
    obd_transport_close(t);
    return rc;
}

static int cmd_dtc_review(struct opts *o)
{
    const char *out = arg_value(o, "-o", NULL);
    struct obd_transport *t = open_transport(o);
    struct obd_device d;
    struct progress_state ps;
    struct obd_dtc_record *recs = NULL;
    size_t count = 0;
    uint32_t size;
    int rc;
    if (!t)
        return 3;
    obd_device_init(&d, t);
    d.strict = o->strict;
    d.verbose = o->verbose;
    rc = get_size_or_fail(&d, &size);
    if (rc)
        goto out;
    progress_init(&ps);
    if (obd_read_dtc_records(&d, size, &recs, &count, progress, &ps) < 0) {
        fprintf(stderr, "protocol error: %s\n", d.err[0] ? d.err : "out of memory");
        rc = 4;
        goto out;
    }
    if (count == 0) {
        puts("no stored DTC records on the device");
        rc = 1;
    } else if (out) {
        FILE *f = fopen(out, "wb");
        if (!f) {
            fprintf(stderr, "error: cannot write %s\n", out);
            rc = 2;
        } else {
            obd_render_dtc_report(f, recs, count);
            fclose(f);
            printf("wrote %zu records to %s\n", count, out);
        }
    } else {
        obd_render_dtc_report(stdout, recs, count);
    }
    obd_free_dtc_records(recs, count);
out:
    obd_transport_close(t);
    return rc;
}

/* decrypt / encrypt the data image. Only the data image at the bottom of the
 * flash is obfuscated; the feedback log, DTC records and settings near the
 * top are stored in plain form and must not be passed through this. */
static int cmd_crypt(struct opts *o, int decrypting)
{
    long addr = 0;
    uint8_t *data;
    size_t len;
    double before, after;
    const char *in, *out;

    if (o->argc < 2 || o->argv[0][0] == '-' || o->argv[1][0] == '-') {
        fprintf(stderr, "usage: %s <input> <output> [-a offset-in-image]\n",
                decrypting ? "decrypt" : "encrypt");
        return 2;
    }
    in = o->argv[0];
    out = o->argv[1];
    if (parse_int(arg_value(o, "-a", "0"), &addr) < 0 || addr < 0) {
        fputs("error: bad -a offset\n", stderr);
        return 2;
    }
    data = obd_read_file(in, &len);
    if (!data) {
        fprintf(stderr, "error: cannot read %s\n", in);
        return 2;
    }
    before = obd_ext_zero_fraction(data, len);
    if (decrypting)
        obd_ext_decrypt(data, len, (uint64_t)addr);
    else
        obd_ext_encrypt(data, len, (uint64_t)addr);
    after = obd_ext_zero_fraction(data, len);
    if (write_file(out, data, len) < 0) {
        free(data);
        return 2;
    }
    free(data);
    printf("wrote %zu bytes to %s (zero bytes %.2f%% -> %.2f%%)\n", len, out,
           100.0 * before, 100.0 * after);
    if (decrypting && after < 0.05)
        fputs("warning: the result does not look like a decrypted image. Check that the\n"
              "         input is an ExtFlashDat.bin or a flash dump starting at offset 0,\n"
              "         and that -a matches its position in the image.\n", stderr);
    return 0;
}

static int cmd_read_flash(struct opts *o)
{
    const char *out = arg_value(o, "-o", NULL);
    long addr = 0, len = -1;
    struct obd_transport *t;
    struct obd_device d;
    struct progress_state ps;
    uint8_t *buf;
    int rc = 0;
    if (!out) {
        fputs("usage: read-flash -o file [-a addr] [-l len]\n", stderr);
        return 2;
    }
    if (parse_int(arg_value(o, "-a", "0"), &addr) < 0 || addr < 0 ||
        parse_int(arg_value(o, "-l", "-1"), &len) < 0) {
        fputs("error: bad address or length\n", stderr);
        return 2;
    }
    t = open_transport(o);
    if (!t)
        return 3;
    obd_device_init(&d, t);
    d.strict = o->strict;
    d.verbose = o->verbose;
    if (len < 0) {
        uint32_t size;
        rc = get_size_or_fail(&d, &size);
        if (rc)
            goto out;
        if ((uint32_t)addr >= size) {
            fputs("error: address beyond end of flash\n", stderr);
            rc = 2;
            goto out;
        }
        len = (long)(size - (uint32_t)addr);
    }
    buf = malloc((size_t)len ? (size_t)len : 1);
    if (!buf) {
        rc = 2;
        goto out;
    }
    progress_init(&ps);
    if (obd_read_flash_range(&d, (uint32_t)addr, buf, (size_t)len, progress, &ps) < 0) {
        fprintf(stderr, "protocol error: %s\n", d.err);
        rc = 4;
    } else {
        if (arg_flag(o, "--decrypt"))
            obd_ext_decrypt(buf, (size_t)len, (uint64_t)addr);
        if (write_file(out, buf, (size_t)len) == 0)
            printf("wrote %ld bytes from 0x%lx to %s%s\n", len, addr, out,
                   arg_flag(o, "--decrypt") ? " (decrypted)" : "");
        else
            rc = 2;
    }
    free(buf);
out:
    obd_transport_close(t);
    return rc;
}

int main(int argc, char **argv)
{
    struct opts o = {0, false, -1, -1, NULL, NULL, 0, NULL};
    int i = 1;
    for (; i < argc; i++) {
        const char *a = argv[i];
        long v;
        if (strcmp(a, "-v") == 0) {
            o.verbose += 1;
        } else if (strcmp(a, "-vv") == 0) {
            o.verbose += 2;
        } else if (strcmp(a, "--strict") == 0) {
            o.strict = true;
        } else if (strcmp(a, "--simulate") == 0 && i + 1 < argc) {
            o.simulate = argv[++i];
        } else if (strcmp(a, "--vid") == 0 && i + 1 < argc) {
            if (parse_int(argv[++i], &v) < 0) {
                fputs("error: bad --vid\n", stderr);
                return 2;
            }
            o.vid = (int)v;
        } else if (strcmp(a, "--pid") == 0 && i + 1 < argc) {
            if (parse_int(argv[++i], &v) < 0) {
                fputs("error: bad --pid\n", stderr);
                return 2;
            }
            o.pid = (int)v;
        } else if (strcmp(a, "--version") == 0) {
            puts("openphix-tool " VERSION);
            return 0;
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage(stdout);
            return 0;
        } else if (a[0] == '-') {
            fprintf(stderr, "unknown option %s\n", a);
            usage(stderr);
            return 2;
        } else {
            break;
        }
    }
    if (i >= argc) {
        usage(stderr);
        return 2;
    }
    if ((o.vid < 0) != (o.pid < 0)) {
        fputs("error: --vid and --pid must be given together\n", stderr);
        return 2;
    }
    o.cmd = argv[i];
    o.argc = argc - i - 1;
    o.argv = argv + i + 1;
    if (strcmp(o.cmd, "list") == 0)
        return cmd_list(&o);
    if (strcmp(o.cmd, "info") == 0)
        return cmd_info(&o);
    if (strcmp(o.cmd, "package") == 0)
        return cmd_package(&o);
    if (strcmp(o.cmd, "update") == 0)
        return cmd_update(&o);
    if (strcmp(o.cmd, "feedback") == 0)
        return cmd_feedback(&o);
    if (strcmp(o.cmd, "dtc-review") == 0)
        return cmd_dtc_review(&o);
    if (strcmp(o.cmd, "read-flash") == 0)
        return cmd_read_flash(&o);
    if (strcmp(o.cmd, "decrypt") == 0)
        return cmd_crypt(&o, 1);
    if (strcmp(o.cmd, "encrypt") == 0)
        return cmd_crypt(&o, 0);
    fprintf(stderr, "unknown command %s\n", o.cmd);
    usage(stderr);
    return 2;
}
