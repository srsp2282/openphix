/* Wire format of the Autophix DM100/DM300 bootloader protocol.
 * See docs/update-protocol.md in the repository root. */
#ifndef OPENPHIX_PROTOCOL_H
#define OPENPHIX_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define OBD_CMD_SIZE 16
#define OBD_BLOCK_SIZE 0x1000u
#define OBD_DATA_PACKET_SIZE 0x1008u
#define OBD_ACK_FLAG 0x80

enum obd_cmd {
    OBD_CMD_BLOCK = 0x01,          /* announce one 4 KiB data block (length, index) */
    OBD_CMD_FINISH = 0x02,         /* end of update, device applies / reboots */
    OBD_CMD_BEGIN_MCU = 0x03,      /* start MCU code upload (block count) */
    OBD_CMD_BEGIN_DATA = 0x04,     /* start external flash data upload (block count) */
    OBD_CMD_READ_FLASH = 0x06,     /* read external flash (address, length) */
    OBD_CMD_GET_VERSION = 0x07,    /* bootloader version string */
    OBD_CMD_UNKNOWN_0A = 0x0A,     /* only in the vendor's timeout table (120 s) */
    OBD_CMD_GET_FLASH_SIZE = 0x0B  /* external flash size in bytes */
};

/* Timeouts in milliseconds, copied from the vendor tool. */
#define OBD_CMD_WRITE_TIMEOUT 1000
#define OBD_CMD_WRITE_TIMEOUT_UPLOAD 10000
#define OBD_ACK_TIMEOUT_DEFAULT 1000
#define OBD_ACK_TIMEOUT_UPLOAD 20000
#define OBD_ACK_TIMEOUT_BEGIN_MCU 20000
#define OBD_ACK_TIMEOUT_0A 120000
#define OBD_BULK_TIMEOUT 10000
#define OBD_BULK_PROBE_TIMEOUT 2000

/* Layout of the external flash, relative to its end. */
#define OBD_FEEDBACK_AREA_OFFSET 0x50000u
#define OBD_FEEDBACK_BLOCKS 32u
#define OBD_DTC_AREA_OFFSET 0x30000u
#define OBD_DTC_BLOCKS 30u
#define OBD_DTC_RECORD_MAGIC "AUTOPHIX"

#define OBD_PID_DM100 0x5265

struct obd_usb_id {
    uint16_t vid;
    uint16_t pid;
    const char *name;
};
extern const struct obd_usb_id obd_usb_ids[];
extern const size_t obd_usb_id_count;
extern const uint32_t obd_flash_size_candidates[4];

const char *obd_hardware_name(uint16_t vid, uint16_t pid);
/* Look up the USB id of a hardware name (DM100, DM300, DM100HC). */
bool obd_hardware_id(const char *name, uint16_t *vid, uint16_t *pid);
/* Vendor rule: idProduct 0x5265 is the DM100 path, everything else DM300. */
static inline bool obd_is_dm100_path(uint16_t pid) { return pid == OBD_PID_DM100; }

unsigned obd_ack_timeout_for(uint8_t cmd);

uint8_t obd_checksum8(const uint8_t *data, size_t len);
/* 55 AA <cmd> <payload> ... <sum8>. Returns -1 when the payload is too long. */
int obd_build_command(uint8_t out[OBD_CMD_SIZE], uint8_t cmd, const uint8_t *payload, size_t len);
bool obd_is_ack(const uint8_t *resp, size_t len, uint8_t cmd);
uint32_t obd_block_count(size_t len);
int obd_build_begin(uint8_t out[OBD_CMD_SIZE], uint8_t cmd, size_t image_len);
int obd_build_block_header(uint8_t out[OBD_CMD_SIZE], size_t block_len, unsigned index);
void obd_build_finish(uint8_t out[OBD_CMD_SIZE]);
/* CMD_READ_FLASH: little-endian 32 bit address, little-endian 16 bit length. */
int obd_build_read_flash(uint8_t out[OBD_CMD_SIZE], uint32_t address, size_t len);
/* 55 AA 55 AA + 4096 bytes (zero padded) + big-endian sum32 of the first 0x1004. */
int obd_build_data_packet(uint8_t out[OBD_DATA_PACKET_SIZE], const uint8_t *chunk, size_t len);
bool obd_verify_data_packet(const uint8_t *pkt, size_t len);
/* Copy the C string at offset 3 of a version reply. Returns its length. */
size_t obd_parse_version(const uint8_t resp[OBD_CMD_SIZE], char *out, size_t outlen);
uint32_t obd_parse_flash_size(const uint8_t resp[OBD_CMD_SIZE]);
/* Vendor rule: only bootloaders strictly newer than "1.26" (strcmp) skip Erase.bin. */
bool obd_erase_required(const char *version);
/* "P0143", "U0100" ... from the 16 bit DTC encoding. out must hold 6 bytes. */
void obd_format_dtc(uint16_t code, char out[6]);

#endif
