/* Locate the files of a vendor update package (an unzipped download). */
#ifndef OPENPHIX_PACKAGE_H
#define OPENPHIX_PACKAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define OBD_PATH_MAX 1024

struct obd_package {
    char root[OBD_PATH_MAX];
};

/* Accepts the directory containing bin/, or its parent when the zip was
 * extracted into a single sub directory. Returns 0 on success. */
int obd_package_open(struct obd_package *p, const char *path);
/* The following return true and fill out when the file exists. */
bool obd_package_mcu(const struct obd_package *p, bool dm100_path, char *out, size_t outlen);
bool obd_package_erase(const struct obd_package *p, char *out, size_t outlen);
bool obd_package_ext(const struct obd_package *p, char *out, size_t outlen);
bool obd_package_notes(const struct obd_package *p, char *out, size_t outlen);
/* Print "Software Versions", "Library Version", "Language" lines from the notes. */
void obd_package_print_versions(const struct obd_package *p, FILE *out);
void obd_package_print_summary(const struct obd_package *p, FILE *out);

/* Read a whole file. Caller frees. NULL on error. */
uint8_t *obd_read_file(const char *path, size_t *len);
bool obd_file_exists(const char *path);
bool obd_dir_exists(const char *path);

#endif
