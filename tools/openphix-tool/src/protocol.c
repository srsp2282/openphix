#include "protocol.h"

#include <stdio.h>
#include <string.h>

const struct obd_usb_id obd_usb_ids[] = {
    {0x0483, 0x5265, "DM100"},
    {0x0483, 0x5750, "DM300"},
    {0x2E88, 0x4605, "DM100HC"},
};
const size_t obd_usb_id_count = sizeof(obd_usb_ids) / sizeof(obd_usb_ids[0]);
const uint32_t obd_flash_size_candidates[4] = {0x2000000u, 0x1000000u, 0x800000u, 0x400000u};

const char *obd_hardware_name(uint16_t vid, uint16_t pid)
{
    for (size_t i = 0; i < obd_usb_id_count; i++)
        if (obd_usb_ids[i].vid == vid && obd_usb_ids[i].pid == pid)
            return obd_usb_ids[i].name;
    return "unknown";
}

bool obd_hardware_id(const char *name, uint16_t *vid, uint16_t *pid)
{
    for (size_t i = 0; i < obd_usb_id_count; i++) {
        if (strcmp(obd_usb_ids[i].name, name) == 0) {
            *vid = obd_usb_ids[i].vid;
            *pid = obd_usb_ids[i].pid;
            return true;
        }
    }
    return false;
}

unsigned obd_ack_timeout_for(uint8_t cmd)
{
    if (cmd == OBD_CMD_BEGIN_MCU)
        return OBD_ACK_TIMEOUT_BEGIN_MCU;
    if (cmd == OBD_CMD_UNKNOWN_0A)
        return OBD_ACK_TIMEOUT_0A;
    return OBD_ACK_TIMEOUT_DEFAULT;
}

uint8_t obd_checksum8(const uint8_t *data, size_t len)
{
    unsigned sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += data[i];
    return (uint8_t)sum;
}

int obd_build_command(uint8_t out[OBD_CMD_SIZE], uint8_t cmd, const uint8_t *payload, size_t len)
{
    if (len > OBD_CMD_SIZE - 3)
        return -1;
    memset(out, 0, OBD_CMD_SIZE);
    out[0] = 0x55;
    out[1] = 0xAA;
    out[2] = cmd;
    if (len)
        memcpy(out + 3, payload, len);
    out[15] = obd_checksum8(out, 15);
    return 0;
}

bool obd_is_ack(const uint8_t *resp, size_t len, uint8_t cmd)
{
    return len == OBD_CMD_SIZE && resp[2] == (uint8_t)(cmd + OBD_ACK_FLAG);
}

uint32_t obd_block_count(size_t len)
{
    return (uint32_t)((len + OBD_BLOCK_SIZE - 1) / OBD_BLOCK_SIZE);
}

int obd_build_begin(uint8_t out[OBD_CMD_SIZE], uint8_t cmd, size_t image_len)
{
    uint32_t n = obd_block_count(image_len);
    uint8_t p[2];
    if (n > 0xFFFF)
        return -1;
    p[0] = (uint8_t)(n >> 8);
    p[1] = (uint8_t)n;
    return obd_build_command(out, cmd, p, 2);
}

int obd_build_block_header(uint8_t out[OBD_CMD_SIZE], size_t block_len, unsigned index)
{
    uint8_t p[4];
    if (block_len == 0 || block_len > OBD_BLOCK_SIZE || index > 0xFFFF)
        return -1;
    p[0] = (uint8_t)(block_len >> 8);
    p[1] = (uint8_t)block_len;
    p[2] = (uint8_t)(index >> 8);
    p[3] = (uint8_t)index;
    return obd_build_command(out, OBD_CMD_BLOCK, p, 4);
}

void obd_build_finish(uint8_t out[OBD_CMD_SIZE])
{
    obd_build_command(out, OBD_CMD_FINISH, NULL, 0);
}

int obd_build_read_flash(uint8_t out[OBD_CMD_SIZE], uint32_t address, size_t len)
{
    uint8_t p[6];
    if (len == 0 || len > OBD_BLOCK_SIZE)
        return -1;
    p[0] = (uint8_t)address;            /* little-endian address */
    p[1] = (uint8_t)(address >> 8);
    p[2] = (uint8_t)(address >> 16);
    p[3] = (uint8_t)(address >> 24);
    p[4] = (uint8_t)len;                /* little-endian length (verified on a DM100) */
    p[5] = (uint8_t)(len >> 8);
    return obd_build_command(out, OBD_CMD_READ_FLASH, p, 6);
}

int obd_build_data_packet(uint8_t out[OBD_DATA_PACKET_SIZE], const uint8_t *chunk, size_t len)
{
    uint32_t sum = 0;
    if (len > OBD_BLOCK_SIZE)
        return -1;
    memset(out, 0, OBD_DATA_PACKET_SIZE);
    out[0] = 0x55;
    out[1] = 0xAA;
    out[2] = 0x55;
    out[3] = 0xAA;
    if (len)
        memcpy(out + 4, chunk, len);
    for (size_t i = 0; i < OBD_DATA_PACKET_SIZE - 4; i++)
        sum += out[i];
    out[OBD_DATA_PACKET_SIZE - 4] = (uint8_t)(sum >> 24);
    out[OBD_DATA_PACKET_SIZE - 3] = (uint8_t)(sum >> 16);
    out[OBD_DATA_PACKET_SIZE - 2] = (uint8_t)(sum >> 8);
    out[OBD_DATA_PACKET_SIZE - 1] = (uint8_t)sum;
    return 0;
}

bool obd_verify_data_packet(const uint8_t *pkt, size_t len)
{
    uint32_t sum = 0, stored;
    if (len != OBD_DATA_PACKET_SIZE)
        return false;
    /* The host sends 55 AA 55 AA; a real DM100 answers with AA 55 AA 55. */
    if (!((pkt[0] == 0x55 && pkt[1] == 0xAA && pkt[2] == 0x55 && pkt[3] == 0xAA) ||
          (pkt[0] == 0xAA && pkt[1] == 0x55 && pkt[2] == 0xAA && pkt[3] == 0x55)))
        return false;
    for (size_t i = 0; i < OBD_DATA_PACKET_SIZE - 4; i++)
        sum += pkt[i];
    stored = ((uint32_t)pkt[OBD_DATA_PACKET_SIZE - 4] << 24) |
             ((uint32_t)pkt[OBD_DATA_PACKET_SIZE - 3] << 16) |
             ((uint32_t)pkt[OBD_DATA_PACKET_SIZE - 2] << 8) |
             (uint32_t)pkt[OBD_DATA_PACKET_SIZE - 1];
    return sum == stored;
}

size_t obd_parse_version(const uint8_t resp[OBD_CMD_SIZE], char *out, size_t outlen)
{
    size_t n = 0;
    if (outlen == 0)
        return 0;
    while (n < OBD_CMD_SIZE - 3 && n < outlen - 1 && resp[3 + n] != 0) {
        out[n] = (char)resp[3 + n];
        n++;
    }
    out[n] = 0;
    return n;
}

uint32_t obd_parse_flash_size(const uint8_t resp[OBD_CMD_SIZE])
{
    return (uint32_t)resp[3] | ((uint32_t)resp[4] << 8) | ((uint32_t)resp[5] << 16) |
           ((uint32_t)resp[6] << 24);
}

bool obd_erase_required(const char *version)
{
    return !(strcmp(version, "1.26") > 0);
}

void obd_format_dtc(uint16_t code, char out[6])
{
    static const char letters[4] = {'P', 'C', 'B', 'U'};
    out[0] = letters[(code >> 14) & 3];
    snprintf(out + 1, 5, "%04X", code & 0x3FFF);
}
