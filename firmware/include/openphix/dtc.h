/* Fault code and measuring block text lookup (docs/data-image-format.md,
 * sections 4.3, 10 and 11). */
#ifndef OPENPHIX_DTC_H
#define OPENPHIX_DTC_H

#include <stddef.h>
#include <stdint.h>

#include "openphix/res.h"

int dtc_init(const struct res *image);

/* Generic OBDII: 16 bit code (P0xxx = 0x0xxx, C = 0x4xxx, B = 0x8xxx,
 * U = 0xCxxx) to its text. Returns the string id (0 when none). */
uint32_t dtc_obd_string(uint16_t code);
/* Format a 16 bit code as "P0123". */
const char *dtc_format(uint16_t code, char *buf, size_t cap);

/* VAG 5 digit code (KWP1281 / KWP2000) to its text id. */
uint32_t dtc_vag_string(uint16_t number);
/* VAG UDS 3 byte code (DTC high, DTC low, fault type) to its text id. */
uint32_t dtc_uds_string(uint16_t code, uint8_t fault_type);

/* UDSDTCTransID.BIN: per module type ("EV_ECM") the key to string id
 * table. Returns the string id or 0. */
uint32_t dtc_uds_translate(const char *module_type, uint32_t key);

/* DsTransID.BIN: measuring block text of (data set group, block, field),
 * the KWP1281 0x25 value text n, and the default field name for a data
 * type byte. All return a string id or 0. */
uint32_t ds_block_text(uint16_t group, uint8_t block, uint8_t field);
uint32_t ds_value_text(uint16_t n);
uint32_t ds_default_name(uint8_t data_type);

#endif
