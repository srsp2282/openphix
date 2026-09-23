"""The "Feedback" support/debug trace (the 128 KiB area at flash end - 0x50000,
saved by
`openphix-tool feedback` as Feedback.bin).

    0x00  13-byte header beginning with "AUTOPHIX"
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


HEADER_PREFIX = b"AUTOPHIX"
HEADER_SIZE = 13


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
    if not data.startswith(HEADER_PREFIX):
        return

    decrypt = _decryptor(profile)

    p = HEADER_SIZE

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


def parse_isotp_frame(data):
    """Parse the ISO-TP PCI information from one CAN data field.

    This handles the classic-CAN forms observed in AD410 captures:
    Single Frame, First Frame, Consecutive Frame, and Flow Control.

    Padding bytes are retained where useful but are not interpreted.
    """
    data = bytes(data)

    if not data:
        raise ValueError("empty CAN data field")

    frame_type = data[0] >> 4

    if frame_type == 0:
        length = data[0] & 0x0f

        if length > len(data) - 1:
            raise ValueError(
                "ISO-TP Single Frame length exceeds CAN data"
            )

        return {
            "type": "single",
            "declared_length": length,
            "payload": data[1:1 + length],
            "padding": data[1 + length:],
            "raw": data,
        }

    if frame_type == 1:
        if len(data) < 2:
            raise ValueError("truncated ISO-TP First Frame")

        length = (
            ((data[0] & 0x0f) << 8)
            | data[1]
        )

        return {
            "type": "first",
            "declared_length": length,
            "payload": data[2:],
            "raw": data,
        }

    if frame_type == 2:
        return {
            "type": "consecutive",
            "sequence": data[0] & 0x0f,
            "payload": data[1:],
            "raw": data,
        }

    if frame_type == 3:
        if len(data) < 3:
            raise ValueError("truncated ISO-TP Flow Control frame")

        return {
            "type": "flow_control",
            "flow_status": data[0] & 0x0f,
            "block_size": data[1],
            "st_min": data[2],
            "padding": data[3:],
            "raw": data,
        }

    raise ValueError(
        "unsupported ISO-TP PCI type 0x%x" % frame_type
    )


def reassemble_isotp_frames(frames):
    """Reassemble one ordered ISO-TP message from CAN frame dictionaries.

    `frames` uses the dictionaries produced by parse_bus_event() for Ans
    frames.  All frames in one message must have the same CAN ID.

    The result records completeness instead of assuming that every capture
    contains all Consecutive Frames.

    This function only reassembles frames already known to belong to one
    direction/message.  It does not infer wire chronology between stored
    Ask:/Ans: events.
    """
    if not frames:
        raise ValueError("cannot reassemble an empty frame sequence")

    first_can_id = frames[0].get("can_id")
    first = parse_isotp_frame(frames[0]["data"])

    if first["type"] == "single":
        return {
            "can_id": first_can_id,
            "declared_length": first["declared_length"],
            "payload": first["payload"],
            "complete": True,
            "frames_used": 1,
            "errors": [],
        }

    if first["type"] != "first":
        raise ValueError(
            "ISO-TP message must begin with Single or First Frame"
        )

    declared_length = first["declared_length"]
    payload = bytearray(first["payload"])
    frames_used = 1
    expected_sequence = 1
    errors = []

    for frame in frames[1:]:
        if len(payload) >= declared_length:
            break

        can_id = frame.get("can_id")

        if (
            first_can_id is not None
            and can_id is not None
            and can_id != first_can_id
        ):
            errors.append(
                "CAN ID changed from 0x%x to 0x%x"
                % (first_can_id, can_id)
            )
            break

        parsed = parse_isotp_frame(frame["data"])

        if parsed["type"] != "consecutive":
            errors.append(
                "expected Consecutive Frame, got %s"
                % parsed["type"]
            )
            break

        if parsed["sequence"] != expected_sequence:
            errors.append(
                "expected sequence 0x%x, got 0x%x"
                % (
                    expected_sequence,
                    parsed["sequence"],
                )
            )
            break

        payload.extend(parsed["payload"])
        frames_used += 1
        expected_sequence = (
            expected_sequence + 1
        ) & 0x0f

    complete = len(payload) >= declared_length

    return {
        "can_id": first_can_id,
        "declared_length": declared_length,
        "payload": bytes(payload[:declared_length]),
        "complete": complete,
        "frames_used": frames_used,
        "errors": errors,
    }


def classify_ask_event(event):
    """Classify one parsed Ask event by its ISO-TP PCI.

    Returns:
        "diagnostic"    observed Single Frame diagnostic request
        "flow_control"  ISO-TP Flow Control frame
        other ISO-TP frame type strings when encountered

    No transmit CAN ID is inferred here because the observed Ask record
    format does not contain one.
    """
    if event.get("type") != "ask":
        raise ValueError("event is not an Ask")

    parsed = parse_isotp_frame(event["can_data"])

    if parsed["type"] == "single":
        return "diagnostic"

    if parsed["type"] == "flow_control":
        return "flow_control"

    return parsed["type"]


def _classify_ask_or_none(event):
    """Return an Ask classification, or None for an unknown PCI variant."""
    try:
        return classify_ask_event(event)
    except ValueError:
        return None


def parsed_bus_events(data, profile="ad410"):
    """Return parsed Ask:/Ans: events in stored/logical order.

    Each item contains storage location metadata, parsed event data, and a
    counter epoch.  A decrease in the raw tick begins a new epoch.

    This is deliberately a stored/logical sequence, not a reconstructed
    physical wire trace.
    """
    out = []

    epoch = 0
    previous_tick = None
    bus_index = 0

    for stored_index, (stored_offset, payload) in enumerate(
        records(data, profile=profile)
    ):
        for logical_offset, tag, raw, boundary_error in logical_events(payload):
            if boundary_error:
                raise ValueError(
                    "malformed logical event at stored record %d + %d: %s"
                    % (
                        stored_index,
                        logical_offset,
                        boundary_error,
                    )
                )

            event = parse_logical_event(raw)

            if event["type"] not in ("ask", "ans"):
                continue

            tick = event["tick"]

            if previous_tick is not None and tick < previous_tick:
                epoch += 1

            out.append({
                "index": bus_index,
                "stored_index": stored_index,
                "stored_offset": stored_offset,
                "logical_offset": logical_offset,
                "epoch": epoch,
                "event": event,
            })

            bus_index += 1
            previous_tick = tick

    return out


def build_diagnostic_transactions(bus_events):
    """Pair diagnostic requests with receive batches conservatively.

    Accepted stored/logical patterns are only:

        diagnostic Ask -> Ans

    and the one observed ISO-TP form:

        diagnostic Ask -> Flow Control Ask -> Ans

    No searching, nearest-neighbor matching, or cross-epoch pairing is
    performed.

    The Flow Control bridge describes Feedback storage/logical ordering.
    It must not be interpreted as literal physical wire chronology: the
    ECU First Frame necessarily preceded the tester's Flow Control on the
    actual bus even when the logger later stores the RX frames together.

    Returns a dictionary containing:
        transactions
        orphan_answers
        auxiliary_asks
    """
    transactions = []
    used_answers = set()
    used_auxiliary = set()

    for i, item in enumerate(bus_events):
        event = item["event"]

        if event["type"] != "ask":
            continue

        kind = _classify_ask_or_none(event)

        if kind != "diagnostic":
            continue

        if i + 1 >= len(bus_events):
            continue

        nxt = bus_events[i + 1]

        if nxt["epoch"] != item["epoch"]:
            continue

        response = None
        auxiliary = []
        pairing = None

        if nxt["event"]["type"] == "ans":
            response = nxt
            pairing = "immediate"

        elif (
            nxt["event"]["type"] == "ask"
            and _classify_ask_or_none(nxt["event"]) == "flow_control"
            and i + 2 < len(bus_events)
        ):
            candidate = bus_events[i + 2]

            if (
                candidate["epoch"] == item["epoch"]
                and candidate["event"]["type"] == "ans"
            ):
                auxiliary = [nxt]
                response = candidate
                pairing = "flow_control_bridge"
                used_auxiliary.add(nxt["index"])

        if response is None:
            continue

        used_answers.add(response["index"])

        transactions.append({
            "request": item,
            "auxiliary": auxiliary,
            "response": response,
            "pairing": pairing,
            "tick_delta": (
                response["event"]["tick"]
                - item["event"]["tick"]
            ),
            "empty_receive_batch": (
                response["event"]["frame_count"] == 0
            ),
        })

    orphan_answers = [
        item
        for item in bus_events
        if (
            item["event"]["type"] == "ans"
            and item["index"] not in used_answers
        )
    ]

    auxiliary_asks = [
        item
        for item in bus_events
        if (
            item["event"]["type"] == "ask"
            and _classify_ask_or_none(item["event"]) == "flow_control"
            and item["index"] not in used_auxiliary
        )
    ]

    return {
        "transactions": transactions,
        "orphan_answers": orphan_answers,
        "auxiliary_asks": auxiliary_asks,
    }


def describe_ad410_event(event):
    """Readable one-line form of a parsed AD410 logical event."""
    kind = event["type"]

    if kind == "str":
        return "Str id=%02x %r" % (
            event["id"],
            event["text"],
        )

    if kind == "ask":
        return "Ask tick=%d data=%s" % (
            event["tick"],
            event["can_data"].hex(" "),
        )

    if kind == "ans":
        if not event["frames"]:
            return "Ans tick=%d frames=0" % event["tick"]

        frames = []

        for frame in event["frames"]:
            frames.append(
                "0x%03x[%d]=%s"
                % (
                    frame["can_id"],
                    frame["dlc"],
                    frame["data"].hex(" "),
                )
            )

        return "Ans tick=%d frames=%d %s" % (
            event["tick"],
            event["frame_count"],
            " | ".join(frames),
        )

    return "Unknown %s" % event["raw"].hex(" ")


def _cmd_feedback_ad410(a, data):
    """Display AD410 Feedback stored records as logical events."""
    from collections import Counter

    stored_count = 0
    logical_count = 0
    string_counts = Counter()

    for stored_off, payload in records(data, a.profile):
        stored_count += 1

        for logical_off, tag, raw, boundary_error in logical_events(payload):
            logical_count += 1

            location = "0x%05x" % stored_off

            if logical_off:
                location += "+0x%x" % logical_off

            if boundary_error:
                print(
                    "%s malformed: %s: %s"
                    % (
                        location,
                        boundary_error,
                        raw.hex(" "),
                    )
                )
                continue

            try:
                event = parse_logical_event(raw)
            except ValueError as exc:
                print(
                    "%s malformed: %s: %s"
                    % (
                        location,
                        exc,
                        raw.hex(" "),
                    )
                )
                continue

            text = describe_ad410_event(event)

            if event["type"] == "str" and not a.verbose:
                string_counts[text] += 1
                continue

            print("%s %s" % (location, text))

    if not a.verbose:
        for text, count in string_counts.most_common():
            print("%6d x %s" % (count, text))

    print(
        "%d stored records, %d logical events"
        % (stored_count, logical_count)
    )


def cmd_feedback(a):
    data = open(a.file, "rb").read()

    if a.profile == "ad410":
        return _cmd_feedback_ad410(a, data)

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
        help="decode a Feedback.bin support/debug trace",
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
