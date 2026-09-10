#include "package.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <windows.h>
#define OBD_SEP '\\'
#else
#include <dirent.h>
#define OBD_SEP '/'
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & S_IFMT) == S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & S_IFMT) == S_IFREG)
#endif

bool obd_file_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

bool obd_dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static void join(char *out, size_t outlen, const char *a, const char *b)
{
    size_t n = strlen(a);
    if (n && (a[n - 1] == '/' || a[n - 1] == '\\'))
        snprintf(out, outlen, "%s%s", a, b);
    else
        snprintf(out, outlen, "%s%c%s", a, OBD_SEP, b);
}

static bool has_bin(const char *dir)
{
    char tmp[OBD_PATH_MAX];
    join(tmp, sizeof tmp, dir, "bin");
    return obd_dir_exists(tmp);
}

/* First sub directory of path that itself contains bin/ (zip files extract
 * into a single directory named after the archive). */
static bool find_child_with_bin(const char *path, char *out, size_t outlen)
{
    bool found = false;
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    char pattern[OBD_PATH_MAX];
    HANDLE h;
    join(pattern, sizeof pattern, path, "*");
    h = FindFirstFileA(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.')
            continue;
        join(out, outlen, path, fd.cFileName);
        if (has_bin(out)) {
            found = true;
            break;
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR *d = opendir(path);
    struct dirent *e;
    if (!d)
        return false;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.')
            continue;
        join(out, outlen, path, e->d_name);
        if (obd_dir_exists(out) && has_bin(out)) {
            found = true;
            break;
        }
    }
    closedir(d);
#endif
    if (!found)
        out[0] = 0;
    return found;
}

int obd_package_open(struct obd_package *p, const char *path)
{
    char child[OBD_PATH_MAX];
    if (!obd_dir_exists(path))
        return -1;
    if (has_bin(path)) {
        snprintf(p->root, sizeof p->root, "%s", path);
        return 0;
    }
    if (find_child_with_bin(path, child, sizeof child)) {
        snprintf(p->root, sizeof p->root, "%s", child);
        return 0;
    }
    return -1;
}

static bool first_existing(const struct obd_package *p, const char *const *names, size_t n,
                           char *out, size_t outlen)
{
    for (size_t i = 0; i < n; i++) {
        char rel[OBD_PATH_MAX];
        snprintf(rel, sizeof rel, "%s", names[i]);
        for (char *c = rel; *c; c++)
            if (*c == '/')
                *c = OBD_SEP;
        join(out, outlen, p->root, rel);
        if (obd_file_exists(out))
            return true;
    }
    out[0] = 0;
    return false;
}

bool obd_package_mcu(const struct obd_package *p, bool dm100_path, char *out, size_t outlen)
{
    static const char *const dm100[] = {"bin/DM100/McuCode.bin", "bin/McuCode.bin"};
    static const char *const dm300[] = {"bin/DM300/McuCode.bin", "bin/McuCode.bin"};
    return first_existing(p, dm100_path ? dm100 : dm300, 2, out, outlen);
}

bool obd_package_erase(const struct obd_package *p, char *out, size_t outlen)
{
    static const char *const names[] = {"bin/DM300/Erase.bin", "bin/Erase.bin"};
    return first_existing(p, names, 2, out, outlen);
}

bool obd_package_ext(const struct obd_package *p, char *out, size_t outlen)
{
    static const char *const names[] = {"bin/ExtFlashDat.bin"};
    return first_existing(p, names, 1, out, outlen);
}

bool obd_package_notes(const struct obd_package *p, char *out, size_t outlen)
{
    static const char *const names[] = {"Update Instructions.txt", "Update Instructions.TXT"};
    return first_existing(p, names, 2, out, outlen);
}

void obd_package_print_versions(const struct obd_package *p, FILE *out)
{
    static const char *const keys[] = {"Software Versions", "Library Version", "Language"};
    char path[OBD_PATH_MAX];
    size_t len;
    uint8_t *text;
    char *line, *save;
    if (!obd_package_notes(p, path, sizeof path))
        return;
    text = obd_read_file(path, &len);
    if (!text)
        return;
    text = realloc(text, len + 1);
    if (!text)
        return;
    text[len] = 0;
    for (line = (char *)text; line && *line; line = save) {
        char *nl = strpbrk(line, "\r\n");
        if (nl) {
            *nl = 0;
            save = nl + 1;
            while (*save == '\r' || *save == '\n')
                save++;
        } else {
            save = NULL;
        }
        for (size_t k = 0; k < 3; k++) {
            size_t kl = strlen(keys[k]);
            if (strncmp(line, keys[k], kl) == 0) {
                const char *v = line + kl;
                while (*v == ' ' || *v == '\t' || *v == ':' || (unsigned char)*v == 0xEF)
                    v++;
                /* skip a UTF-8 full-width colon if present */
                if ((unsigned char)v[0] == 0xBC && (unsigned char)v[1] == 0x9A)
                    v += 2;
                while (*v == ' ')
                    v++;
                fprintf(out, "%s: %s\n", keys[k], v);
            }
        }
    }
    free(text);
}

static void print_file(FILE *out, const char *label, bool ok, const char *path)
{
    if (!ok) {
        fprintf(out, "%s: missing\n", label);
    } else {
        size_t len = 0;
        uint8_t *d = obd_read_file(path, &len);
        fprintf(out, "%s: %s (%zu bytes)\n", label, path, len);
        free(d);
    }
}

void obd_package_print_summary(const struct obd_package *p, FILE *out)
{
    char path[OBD_PATH_MAX];
    fprintf(out, "package root: %s\n", p->root);
    obd_package_print_versions(p, out);
    print_file(out, "MCU image (DM100)", obd_package_mcu(p, true, path, sizeof path), path);
    print_file(out, "MCU image (DM300)", obd_package_mcu(p, false, path, sizeof path), path);
    print_file(out, "Erase image", obd_package_erase(p, path, sizeof path), path);
    print_file(out, "External flash image", obd_package_ext(p, path, sizeof path), path);
}

uint8_t *obd_read_file(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    long size;
    uint8_t *buf;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = malloc((size_t)size ? (size_t)size : 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *len = (size_t)size;
    return buf;
}
