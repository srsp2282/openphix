import hashlib
import struct
import unittest

from hixtool import feedback
from hixtool import feedback_ad410


# These are raw stored records copied from a controlled AD410 capture.
# The first four bytes are the stored-record u32le length field.
#
# None of these fixtures contains a VIN or other identifying vehicle data.

FIXTURES = {
    "empty_ans": {
        "stored_offset": 0x00433,
        "raw": bytes.fromhex(
            "0f 00 00 00 "
            "6a 4f 92 84 8d 8b 9b 66 6c 91 6e"
        ),
        "plain": bytes.fromhex(
            "41 6e 73 3a 05 00 00 00 ad fb 00"
        ),
    },

    "two_frame_ans": {
        "stored_offset": 0x040A2,
        "raw": bytes.fromhex(
            "27 00 00 00 "
            "13 e0 49 3f f6 d4 6d 58 f8 00 c9 df "
            "3e b3 97 02 38 39 a5 ea 1b 52 6f 97 "
            "2a e3 a6 df f9 97 64 77 2d 21 02"
        ),
        "plain": bytes.fromhex(
            "41 6e 73 3a 1d 00 00 00 f3 5b "
            "02 "
            "0b 08 07 e8 06 41 00 be 3e a8 13 aa "
            "0b 08 07 e9 06 41 00 80 00 00 01 aa"
        ),
    },

    "rpm_zero_ask": {
        "stored_offset": 0x066CA,
        "raw": bytes.fromhex(
            "16 00 00 00 "
            "7b 64 aa e5 18 e2 53 6a a7 c8 0a b9 "
            "d5 49 74 da 03 6f"
        ),
        "plain": bytes.fromhex(
            "41 73 6b 3a 0c 00 00 00 45 c9 "
            "02 01 0c 00 00 00 00 00"
        ),
    },

    "rpm_zero_ans": {
        "stored_offset": 0x066E0,
        "raw": bytes.fromhex(
            "1b 00 00 00 "
            "c2 dd 4b 81 96 b8 94 02 73 77 1f 21 "
            "45 08 42 0b 50 8b 53 e5 08 c1 3d"
        ),
        "plain": bytes.fromhex(
            "41 6e 73 3a 11 00 00 00 45 ce "
            "01 "
            "0b 08 07 e8 04 41 0c 00 00 aa aa aa"
        ),
    },
}


def ask(tick, data):
    data = bytes(data)
    assert len(data) == 8

    return (
        b"Ask:"
        + struct.pack("<H", 12)
        + tick.to_bytes(4, "big")
        + data
    )


def ans(tick, frames):
    body = bytearray([len(frames)])

    for can_id, data in frames:
        data = bytes(data)
        dlc = len(data)

        body.append(3 + dlc)
        body.append(dlc)
        body.extend(can_id.to_bytes(2, "big"))
        body.extend(data)

    extent = 4 + len(body)

    return (
        b"Ans:"
        + struct.pack("<H", extent)
        + tick.to_bytes(4, "big")
        + bytes(body)
    )


class FeedbackAd410CipherTests(unittest.TestCase):

    def test_key_size_and_hash(self):
        self.assertEqual(
            len(feedback_ad410.FEEDBACK_POST_ROTATION_KEY),
            256,
        )

        self.assertEqual(
            hashlib.sha256(
                feedback_ad410.FEEDBACK_POST_ROTATION_KEY
            ).hexdigest(),
            feedback_ad410.FEEDBACK_POST_ROTATION_KEY_SHA256,
        )

        self.assertEqual(
            feedback_ad410.FEEDBACK_POST_ROTATION_KEY_SHA256,
            "b64f5505aa9f324b35763784fa66f33ce"
            "9b048afe13640706c1638e39b7571c4",
        )

    def test_real_raw_fixtures_decrypt(self):
        for name, fixture in FIXTURES.items():
            with self.subTest(name=name):
                raw = fixture["raw"]

                stored_length = struct.unpack_from("<I", raw, 0)[0]

                self.assertEqual(
                    stored_length,
                    len(raw),
                )

                # Cipher coordinates are Feedback-area-relative and begin
                # after the four-byte stored-record length.
                payload_offset = fixture["stored_offset"] + 4

                plain = feedback_ad410.decrypt(
                    raw[4:],
                    payload_offset,
                )

                self.assertEqual(
                    plain,
                    fixture["plain"],
                )


class FeedbackLogicalEventTests(unittest.TestCase):

    def test_rpm_zero_ask(self):
        event = feedback.parse_bus_event(
            FIXTURES["rpm_zero_ask"]["plain"]
        )

        self.assertEqual(event["type"], "ask")
        self.assertEqual(event["tick"], 0x45C9)
        self.assertEqual(
            event["can_data"],
            bytes.fromhex("02 01 0c 00 00 00 00 00"),
        )

    def test_rpm_zero_ans(self):
        event = feedback.parse_bus_event(
            FIXTURES["rpm_zero_ans"]["plain"]
        )

        self.assertEqual(event["type"], "ans")
        self.assertEqual(event["tick"], 0x45CE)
        self.assertEqual(event["frame_count"], 1)

        frame = event["frames"][0]

        self.assertEqual(frame["can_id"], 0x7E8)
        self.assertEqual(frame["dlc"], 8)
        self.assertEqual(
            frame["data"],
            bytes.fromhex("04 41 0c 00 00 aa aa aa"),
        )

    def test_empty_ans(self):
        event = feedback.parse_bus_event(
            FIXTURES["empty_ans"]["plain"]
        )

        self.assertEqual(event["type"], "ans")
        self.assertEqual(event["frame_count"], 0)
        self.assertEqual(event["frames"], [])

    def test_two_frame_ans(self):
        event = feedback.parse_bus_event(
            FIXTURES["two_frame_ans"]["plain"]
        )

        self.assertEqual(event["frame_count"], 2)

        self.assertEqual(
            [f["can_id"] for f in event["frames"]],
            [0x7E8, 0x7E9],
        )

        self.assertEqual(
            event["frames"][0]["data"],
            bytes.fromhex("06 41 00 be 3e a8 13 aa"),
        )

        self.assertEqual(
            event["frames"][1]["data"],
            bytes.fromhex("06 41 00 80 00 00 01 aa"),
        )

    def test_synthetic_ans_counts_zero_through_three(self):
        frame = (
            0x7E8,
            bytes.fromhex("03 41 04 00 aa aa aa aa"),
        )

        expected_lengths = {
            0: 11,
            1: 23,
            2: 35,
            3: 47,
        }

        for count in range(4):
            with self.subTest(count=count):
                raw = ans(
                    1234,
                    [frame] * count,
                )

                event = feedback.parse_bus_event(raw)

                self.assertEqual(
                    event["frame_count"],
                    count,
                )

                self.assertEqual(
                    len(raw),
                    expected_lengths[count],
                )

    def test_concatenated_ask_events(self):
        first = ask(
            0x00018AEE,
            bytes.fromhex("02 09 02 00 00 00 00 00"),
        )

        second = ask(
            0x00018AFE,
            bytes.fromhex("30 00 00 00 00 00 00 00"),
        )

        events = list(
            feedback.logical_events(first + second)
        )

        self.assertEqual(len(events), 2)

        off0, tag0, raw0, err0 = events[0]
        off1, tag1, raw1, err1 = events[1]

        self.assertEqual(off0, 0)
        self.assertEqual(off1, 18)

        self.assertEqual(tag0, b"Ask:")
        self.assertEqual(tag1, b"Ask:")

        self.assertIsNone(err0)
        self.assertIsNone(err1)

        self.assertEqual(raw0, first)
        self.assertEqual(raw1, second)

    def test_tick_is_u32_big_endian(self):
        before = ask(
            0x0000FFE1,
            bytes.fromhex("02 01 00 00 00 00 00 00"),
        )

        after = ask(
            0x00010037,
            bytes.fromhex("02 01 00 00 00 00 00 00"),
        )

        a = feedback.parse_bus_event(before)
        b = feedback.parse_bus_event(after)

        self.assertEqual(a["tick"], 65505)
        self.assertEqual(b["tick"], 65591)
        self.assertEqual(
            b["tick"] - a["tick"],
            86,
        )

    def test_malformed_ans_entry_length(self):
        raw = bytearray(
            ans(
                100,
                [
                    (
                        0x7E8,
                        bytes.fromhex(
                            "03 41 04 00 aa aa aa aa"
                        ),
                    ),
                ],
            )
        )

        # First entry length sits at byte 11.
        # Valid is 11 for DLC=8; make it inconsistent.
        raw[11] = 10

        with self.assertRaisesRegex(
            ValueError,
            "entry length",
        ):
            feedback.parse_bus_event(bytes(raw))

    def test_truncated_bus_event(self):
        raw = ask(
            100,
            bytes.fromhex("02 01 0c 00 00 00 00 00"),
        )

        events = list(
            feedback.logical_events(raw[:-1])
        )

        self.assertEqual(len(events), 1)
        self.assertIsNotNone(events[0][3])
        self.assertIn(
            "extent exceeds",
            events[0][3],
        )

    def test_unknown_tag_is_preserved(self):
        raw = b"Foo:" + b"\x01\x02\x03"

        event = feedback.parse_logical_event(raw)

        self.assertEqual(event["type"], "unknown")
        self.assertEqual(event["tag"], b"Foo:")
        self.assertEqual(event["raw"], raw)


class FeedbackStringTests(unittest.TestCase):

    def test_str_length_includes_nul(self):
        # text_length = 4: "ABC" + NUL
        raw = (
            b"Str:"
            b"02 "
            b"0004"
            b"ABC\x00"
            b"\x7f"
        )

        event = feedback.parse_str_event(raw)

        self.assertEqual(event["id"], 0x02)
        self.assertEqual(event["text"], "ABC")
        self.assertTrue(
            event["nul_in_declared_length"]
        )
        self.assertEqual(
            event["trailing_byte"],
            0x7F,
        )

    def test_str_length_excludes_nul(self):
        # Observed ECU-label convention:
        # declared length covers text only; following byte is NUL.
        text = b"ECU 1 (#e8)"

        raw = (
            b"Str:"
            b"02 "
            + ("%04X" % len(text)).encode("ascii")
            + text
            + b"\x00"
        )

        event = feedback.parse_str_event(raw)

        self.assertEqual(event["id"], 0x02)
        self.assertEqual(
            event["text"],
            "ECU 1 (#e8)",
        )
        self.assertFalse(
            event["nul_in_declared_length"]
        )
        self.assertIsNone(
            event["trailing_byte"]
        )


class FeedbackDescriptionTests(unittest.TestCase):

    def test_ad410_ask_description(self):
        event = feedback.parse_bus_event(
            FIXTURES["rpm_zero_ask"]["plain"]
        )

        text = feedback.describe_ad410_event(event)

        self.assertIn("Ask tick=17865", text)
        self.assertIn(
            "02 01 0c 00 00 00 00 00",
            text,
        )

    def test_ad410_ans_description(self):
        event = feedback.parse_bus_event(
            FIXTURES["rpm_zero_ans"]["plain"]
        )

        text = feedback.describe_ad410_event(event)

        self.assertIn("Ans tick=17870", text)
        self.assertIn("0x7e8[8]", text)
        self.assertIn(
            "04 41 0c 00 00 aa aa aa",
            text,
        )


class FeedbackIsoTpTests(unittest.TestCase):

    def test_single_frame(self):
        parsed = feedback.parse_isotp_frame(
            bytes.fromhex(
                "04 41 0c 00 00 aa aa aa"
            )
        )

        self.assertEqual(parsed["type"], "single")
        self.assertEqual(parsed["declared_length"], 4)
        self.assertEqual(
            parsed["payload"],
            bytes.fromhex("41 0c 00 00"),
        )
        self.assertEqual(
            parsed["padding"],
            bytes.fromhex("aa aa aa"),
        )

    def test_flow_control(self):
        parsed = feedback.parse_isotp_frame(
            bytes.fromhex(
                "30 00 00 00 00 00 00 00"
            )
        )

        self.assertEqual(
            parsed["type"],
            "flow_control",
        )
        self.assertEqual(parsed["flow_status"], 0)
        self.assertEqual(parsed["block_size"], 0)
        self.assertEqual(parsed["st_min"], 0)

    def test_synthetic_vin_reassembly(self):
        # Synthetic Mode 09 PID 02 response:
        #
        #   49 02 01 + VIN "1HGCM82633A004352"
        #
        # This is intentionally not taken from the user's vehicle.
        frames = [
            {
                "can_id": 0x7E8,
                "data": bytes.fromhex(
                    "10 14 49 02 01 31 48 47"
                ),
            },
            {
                "can_id": 0x7E8,
                "data": bytes.fromhex(
                    "21 43 4d 38 32 36 33 33"
                ),
            },
            {
                "can_id": 0x7E8,
                "data": bytes.fromhex(
                    "22 41 30 30 34 33 35 32"
                ),
            },
        ]

        result = feedback.reassemble_isotp_frames(
            frames
        )

        expected = (
            bytes.fromhex("49 02 01")
            + b"1HGCM82633A004352"
        )

        self.assertEqual(result["can_id"], 0x7E8)
        self.assertEqual(
            result["declared_length"],
            20,
        )
        self.assertTrue(result["complete"])
        self.assertEqual(result["frames_used"], 3)
        self.assertEqual(result["errors"], [])
        self.assertEqual(
            result["payload"],
            expected,
        )

    def test_incomplete_first_frame(self):
        frames = [
            {
                "can_id": 0x7E8,
                "data": bytes.fromhex(
                    "10 14 49 02 01 31 48 47"
                ),
            },
        ]

        result = feedback.reassemble_isotp_frames(
            frames
        )

        self.assertFalse(result["complete"])
        self.assertEqual(
            result["declared_length"],
            20,
        )
        self.assertEqual(result["frames_used"], 1)

    def test_bad_consecutive_sequence(self):
        frames = [
            {
                "can_id": 0x7E8,
                "data": bytes.fromhex(
                    "10 14 49 02 01 31 48 47"
                ),
            },
            {
                "can_id": 0x7E8,
                "data": bytes.fromhex(
                    "22 43 4d 38 32 36 33 33"
                ),
            },
        ]

        result = feedback.reassemble_isotp_frames(
            frames
        )

        self.assertFalse(result["complete"])
        self.assertEqual(result["frames_used"], 1)
        self.assertEqual(len(result["errors"]), 1)
        self.assertIn(
            "expected sequence",
            result["errors"][0],
        )


if __name__ == "__main__":
    unittest.main()
