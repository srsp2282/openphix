"""The "Feedback" bus log (the 128 KiB area at flash end - 0x50000, saved by
`openphix-tool feedback` as Feedback.bin).

    0x00  "AUTOPHIX" + 5 zero bytes
    0x0D  records, back to back, until 0xFF 0xFF 0xFF 0xFF:
            u32   length, counting itself
            bytes payload (length - 4), encoded according to the selected
                  Feedback profile and keyed by the payload's own offset
                  inside the Feedback area

Payload text seen so far has the shape

    "Str:" id2hex " " len4hex string NUL junk

The AD410 profile additionally decodes captured Ask:/Ans: bus traffic.
"""
import struct

from . import cipher


MAGIC = b"AUTOPHIX\0\0\0\0\0"


def _decryptor(profile):
    """Return the payload decoder for a Feedback profile."""
    if profile == "legacy":
        return cipher.decrypt

    if profile == "ad410":
        from . import feedback_ad410
        return feedback_ad410.decrypt

    raise ValueError("unknown Feedback profile: %s" % profile)


def records(data, profile="legacy"):
    """Yield (offset, plaintext payload) for every stored record."""
    if not data.startswith(b"AUTOPHIX"):
        return

    decrypt = _decryptor(profile)

    p = len(MAGIC)

    while p + 4 <= len(data):
        if data[p:p + 4] == b"\xff\xff\xff\xff":
            return

        (length,) = struct.unpack_from("<I", data, p)

        if length < 4 or p + length > len(data):
            return

        yield p, decrypt(
            data[p + 4:p + length],
            p + 4,
        )

        p += length


def _logical_event_size(payload, offset):
    """Return (size, error) for one logical Feedback event.

    Ask:/Ans: use a binary extent field.  Str: uses its own ASCII length
    field.  Unknown tags cannot be safely resynchronised inside a stored
    payload.
    """
    remaining = len(payload) - offset

    if remaining < 4:
        return None, "truncated logical-event tag"

    tag = payload[offset:offset + 4]

    if tag in (b"Ask:", b"Ans:"):
        if remaining < 10:
            return None, "truncated bus-event header"

        extent = struct.unpack_from("<H", payload, offset + 4)[0]

        # The extent includes the four-byte tick field and the event body.
        if extent < 4:
            return None, "invalid bus-event extent"

        size = 6 + extent

        if size > remaining:
            return None, "bus-event extent exceeds stored payload"

        return size, None

    if tag == b"Str:":
        if remaining < 11:
            return None, "truncated Str header"

        try:
            int(payload[offset + 4:offset + 6], 16)
            text_length = int(payload[offset + 7:offset + 11], 16)
        except ValueError:
            return None, "invalid Str ASCII-hex field"

        # Layout:
        #   "Str:" + id[2] + separator + text_length[4]
        #   + text_with_nul[text_length] + trailing_byte
        size = 12 + text_length

        if size > remaining:
            return None, "Str length exceeds stored payload"

        # Observed AD410 Str records are inconsistent about whether the
        # terminating NUL is included in text_length.  The declared length
        # still gives the correct logical-event boundary, so do not require
        # the NUL at a particular position here.
        return size, None

    return None, "unknown logical-event tag"


def logical_events(payload):
    """Yield logical events contained in one decoded stored payload.

    Each result is:

        (offset, tag, raw_event, error)

    Valid events have error=None.

    On an unknown or malformed event, the remaining bytes are yielded as
    one event and parsing stops.  We deliberately do not scan for magic
    strings to resynchronise inside a stored payload.
    """
    offset = 0

    while offset < len(payload):
        size, error = _logical_event_size(payload, offset)

        if size is None:
            raw = payload[offset:]
            tag = raw[:4]
            yield offset, tag, raw, error
            return

        raw = payload[offset:offset + size]
        tag = raw[:4]

        yield offset, tag, raw, None

        offset += size


def parse_bus_event(payload):
    """Parse one validated Ask:/Ans: logical event.

    Returns a dictionary.  Raises ValueError for malformed or unsupported
    bus-event variants.
    """
    if len(payload) < 10:
        raise ValueError("bus event shorter than common header")

    tag = payload[:4]

    if tag not in (b"Ask:", b"Ans:"):
        raise ValueError("not a bus event")

    extent = struct.unpack_from("<H", payload, 4)[0]
    expected_size = 6 + extent

    if expected_size != len(payload):
        raise ValueError(
            "bus-event size mismatch: extent=%d gives %d bytes, got %d"
            % (extent, expected_size, len(payload))
        )

    tick = int.from_bytes(payload[6:10], "big")

    out = {
        "tag": tag,
        "extent": extent,
        "tick": tick,
        "raw": payload,
    }

    if tag == b"Ask:":
        if extent != 12 or len(payload) != 18:
            raise ValueError(
                "unsupported Ask variant: extent=%d length=%d"
                % (extent, len(payload))
            )

        out.update({
            "type": "ask",
            "can_data": payload[10:18],
        })

        return out

    # Ans:
    frame_count = payload[10]
    cursor = 11
    frames = []

    for index in range(frame_count):
        if cursor >= len(payload):
            raise ValueError(
                "Ans frame %d missing entry length" % index
            )

        entry_length = payload[cursor]
        entry_end = cursor + 1 + entry_length

        if entry_length < 3:
            raise ValueError(
                "Ans frame %d has invalid entry length %d"
                % (index, entry_length)
            )

        if entry_end > len(payload):
            raise ValueError(
                "Ans frame %d overruns logical event" % index
            )

        dlc = payload[cursor + 1]
        can_id = int.from_bytes(
            payload[cursor + 2:cursor + 4],
            "big",
        )

        expected_entry_length = 3 + dlc

        if entry_length != expected_entry_length:
            raise ValueError(
                "Ans frame %d entry length %d != 3 + DLC %d"
                % (index, entry_length, dlc)
            )

        data_start = cursor + 4
        data_end = data_start + dlc

        if data_end != entry_end:
            raise ValueError(
                "Ans frame %d DLC does not consume entry" % index
            )

        frames.append({
            "index": index,
            "entry_length": entry_length,
            "dlc": dlc,
            "can_id": can_id,
            "data": payload[data_start:data_end],
        })

        cursor = entry_end

    if cursor != len(payload):
        raise ValueError(
            "Ans has %d surplus byte(s)"
            % (len(payload) - cursor)
        )

    out.update({
        "type": "ans",
        "frame_count": frame_count,
        "frames": frames,
    })

    return out


def parse_str_event(payload):
    """Parse one Str: logical event.

    AD410 captures contain two observed NUL conventions:
      * declared text length includes the NUL, followed by one trailing byte
      * declared text length excludes the NUL, with the following byte being
        the NUL terminator

    Preserve which form was observed rather than assigning meaning to the
    trailing byte.
    """
    if not payload.startswith(b"Str:") or len(payload) < 12:
        raise ValueError("not a complete Str event")

    try:
        string_id = int(payload[4:6], 16)
        text_length = int(payload[7:11], 16)
    except ValueError as exc:
        raise ValueError("invalid Str ASCII-hex field") from exc

    expected_size = 12 + text_length

    if len(payload) != expected_size:
        raise ValueError(
            "Str size mismatch: declared=%d expected=%d got=%d"
            % (text_length, expected_size, len(payload))
        )

    declared = payload[11:11 + text_length]
    following = payload[11 + text_length:]

    nul_in_declared_length = bool(declared and declared[-1] == 0)

    if nul_in_declared_length:
        text_raw = declared[:-1]
        trailing_byte = following[0]
    elif following == b"\x00":
        text_raw = declared
        trailing_byte = None
    else:
        text_raw = declared
        trailing_byte = following[0] if following else None

    return {
        "type": "str",
        "tag": b"Str:",
        "id": string_id,
        "declared_text_length": text_length,
        "text_raw": text_raw,
        "text": text_raw.decode("latin-1", "replace"),
        "nul_in_declared_length": nul_in_declared_length,
        "trailing_byte": trailing_byte,
        "raw": payload,
    }


def parse_logical_event(payload):
    """Parse one already-delimited logical Feedback event."""
    if payload.startswith((b"Ask:", b"Ans:")):
        return parse_bus_event(payload)

    if payload.startswith(b"Str:"):
        return parse_str_event(payload)

    return {
        "type": "unknown",
        "tag": payload[:4],
        "raw": payload,
    }


def describe(payload):
    """Best effort text form of one payload."""
    if payload.startswith(b"Str:") and len(payload) >= 11:
        try:
            rid = int(payload[4:6], 16)
            n = int(payload[7:11], 16)
        except ValueError:
            return repr(payload)

        text = (
            payload[11:11 + n]
            .split(b"\0")[0]
            .decode("latin-1", "replace")
        )

        return "Str id=%02x %r" % (rid, text)

    return payload.hex(" ")


def cmd_feedback(a):
    data = open(a.file, "rb").read()
    n = 0

    for off, payload in records(data, a.profile):
        n += 1

        if a.verbose or not payload.startswith(b"Str:"):
            print("0x%05x %s" % (off, describe(payload)))

    if not a.verbose:
        from collections import Counter

        c = Counter(
            describe(payload)
            for _, payload in records(data, a.profile)
        )

        for text, cnt in c.most_common():
            print("%6d x %s" % (cnt, text))

    print("%d records" % n)


def register(sub):
    s = sub.add_parser(
        "feedback",
        help="decode a Feedback.bin bus log",
    )

    s.add_argument("file")

    s.add_argument(
        "--profile",
        choices=("legacy", "ad410"),
        default="legacy",
        help="Feedback payload profile (default: legacy)",
    )

    s.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="every record in order",
    )

    s.set_defaults(fn=cmd_feedback)
