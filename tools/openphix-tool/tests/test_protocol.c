#include <string.h>

#include "../src/protocol.h"
#include "test.h"

int main(void)
{
    uint8_t pkt[OBD_DATA_PACKET_SIZE];
    uint8_t chunk[OBD_BLOCK_SIZE];
    char buf[16];
    unsigned long sum = 0;

    /* command layout */
    CHECK_EQ(obd_build_command(pkt, 0x07, NULL, 0), 0);
    CHECK_EQ(pkt[0], 0x55);
    CHECK_EQ(pkt[1], 0xAA);
    CHECK_EQ(pkt[2], 0x07);
    CHECK_EQ(pkt[15], (0x55 + 0xAA + 0x07) & 0xFF);
    CHECK_EQ(obd_build_command(pkt, 0x07, chunk, 14), -1);

    /* begin / block */
    CHECK_EQ(obd_block_count(0), 0);
    CHECK_EQ(obd_block_count(1), 1);
    CHECK_EQ(obd_block_count(0x1000), 1);
    CHECK_EQ(obd_block_count(0x1001), 2);
    CHECK_EQ(obd_build_begin(pkt, OBD_CMD_BEGIN_MCU, 281728), 0);
    CHECK(memcmp(pkt + 2, "\x03\x00\x45", 3) == 0);
    CHECK_EQ(obd_build_block_header(pkt, 0x1000, 0x0102), 0);
    CHECK(memcmp(pkt + 2, "\x01\x10\x00\x01\x02", 5) == 0);
    CHECK_EQ(obd_build_block_header(pkt, 0x80, 3), 0);
    CHECK(memcmp(pkt + 2, "\x01\x00\x80\x00\x03", 5) == 0);
    CHECK_EQ(obd_build_block_header(pkt, 0x1001, 0), -1);

    /* read flash */
    CHECK_EQ(obd_build_read_flash(pkt, 0x01FB0000u, 0x1000), 0);
    CHECK(memcmp(pkt + 2, "\x06\x00\x00\xfb\x01\x00\x10", 7) == 0);

    /* data packet */
    for (size_t i = 0; i < sizeof chunk; i++)
        chunk[i] = (uint8_t)i;
    CHECK_EQ(obd_build_data_packet(pkt, chunk, sizeof chunk), 0);
    CHECK(memcmp(pkt, "\x55\xaa\x55\xaa", 4) == 0);
    CHECK(memcmp(pkt + 4, chunk, sizeof chunk) == 0);
    for (size_t i = 0; i < sizeof chunk; i++)
        sum += chunk[i];
    sum += 0x55 + 0xAA + 0x55 + 0xAA;
    CHECK_EQ(pkt[0x1004], (sum >> 24) & 0xFF);
    CHECK_EQ(pkt[0x1007], sum & 0xFF);
    CHECK(obd_verify_data_packet(pkt, sizeof pkt));
    pkt[100] ^= 1;
    CHECK(!obd_verify_data_packet(pkt, sizeof pkt));
    pkt[100] ^= 1;
    pkt[0] = 0xAA; pkt[1] = 0x55; pkt[2] = 0xAA; pkt[3] = 0x55;   /* device byte order */
    CHECK(obd_verify_data_packet(pkt, sizeof pkt));
    pkt[0] = 0x55;
    CHECK(!obd_verify_data_packet(pkt, sizeof pkt));
    CHECK_EQ(obd_build_data_packet(pkt, (const uint8_t *)"abc", 3), 0);
    CHECK(memcmp(pkt + 4, "abc\0\0", 5) == 0);

    /* ack and parsers */
    {
        uint8_t resp[16] = {0x55, 0xAA, 0x87, '1', '.', '3', '0', 0};
        uint8_t size[16] = {0x55, 0xAA, 0x8B, 0x00, 0x00, 0x00, 0x02};
        CHECK(obd_is_ack(resp, 16, 0x07));
        CHECK(!obd_is_ack(resp, 16, 0x06));
        CHECK(!obd_is_ack(resp, 15, 0x07));
        CHECK_EQ(obd_parse_version(resp, buf, sizeof buf), 4);
        CHECK(strcmp(buf, "1.30") == 0);
        CHECK_EQ(obd_parse_flash_size(size), 0x2000000);
    }

    /* erase rule */
    CHECK(obd_erase_required("1.26"));
    CHECK(obd_erase_required("1.20"));
    CHECK(!obd_erase_required("1.27"));
    CHECK(!obd_erase_required("1.30"));

    /* dtc formatting */
    obd_format_dtc(0x0143, buf);
    CHECK(strcmp(buf, "P0143") == 0);
    obd_format_dtc(0x4123, buf);
    CHECK(strcmp(buf, "C0123") == 0);
    obd_format_dtc(0x8123, buf);
    CHECK(strcmp(buf, "B0123") == 0);
    obd_format_dtc(0xC100, buf);
    CHECK(strcmp(buf, "U0100") == 0);

    /* ids */
    CHECK(strcmp(obd_hardware_name(0x0483, 0x5750), "DM300") == 0);
    CHECK(strcmp(obd_hardware_name(0x1234, 0x0001), "unknown") == 0);
    CHECK(obd_is_dm100_path(0x5265));
    CHECK(!obd_is_dm100_path(0x4605));
    CHECK_EQ(obd_ack_timeout_for(OBD_CMD_BEGIN_MCU), 20000);
    CHECK_EQ(obd_ack_timeout_for(OBD_CMD_UNKNOWN_0A), 120000);
    CHECK_EQ(obd_ack_timeout_for(OBD_CMD_READ_FLASH), 1000);

    TEST_MAIN_END();
}
