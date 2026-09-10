"""Cmd.BIN: the diagnostic command table. Index as in table.py. Record:

    u8  len       bytes that follow (wrong in ~170 records, use the index)
    u8  kind
    u8  h1..h5    header, meaning depends on kind
    payload

Kinds: 0x10 send (packet list), 0x01 keepalive (h4h5 = period ms),
0x02/0x25 raw ISO 14230 message with placeholders, 0x40 raw KWP1281 block,
0x30 5 baud init (h3 = address), 0x60 u32be number, 0x08 raw bytes of
other protocols. See docs/data-image-format.md section 8.
"""
import struct

from . import table

KIND_NAMES = {
    0x01: "keepalive", 0x02: "kline_raw", 0x08: "raw_other", 0x10: "send",
    0x25: "kline_raw_c4", 0x30: "slow_init", 0x40: "kwp1281_block", 0x60: "number",
}

LINK_NAMES = {
    0x00: "can", 0x50: "can_b", 0x52: "can_c", 0x01: "tp20_setup", 0x02: "tp20_setup2",
    0x05: "tp20_data", 0x06: "kline_kwp2000", 0x07: "kline_kwp1281",
}

TP20_OPCODE = {0x0: "more,ack", 0x1: "last,ack", 0x2: "more,noack", 0x3: "last,noack",
               0x9: "nack", 0xa: "ctrl", 0xb: "ack"}


def _split_blocks(buf):
    blocks, p = [], 0
    while p < len(buf):
        ln = buf[p]
        if ln == 0 or p + 1 + ln > len(buf):
            return blocks, buf[p:]
        blocks.append(buf[p + 1:p + 1 + ln])
        p += 1 + ln
    return blocks, b""


def _decode_packet(blk, link):
    dlc_byte = blk[0]
    pk = {"dlc_byte": dlc_byte, "dlc": dlc_byte & 0x0F, "dlc_flags": dlc_byte & 0xF0}
    body = blk[1:]
    if len(body) == pk["dlc"] + 2 and len(body) >= 2:
        (raw_id,) = struct.unpack_from(">H", body, 0)
        pk["can_id"] = raw_id >> 5
        body = body[2:]
    pk["data"] = body
    if link in (0x05, 0x06, 0x07, 0x11) and body:
        pk["opcode"] = body[0]
        pk["chunk"] = body[1:]
    return pk


def kwp2000_reassemble(packets):
    msg = b"".join(p["chunk"] for p in packets if "chunk" in p and (p["opcode"] >> 4) in (2, 3))
    if len(msg) < 2:
        return msg, None
    return msg, (sum(msg[:-1]) & 0xFF) == msg[-1]


def parse_record(rid, raw):
    kind = raw[1]
    h = raw[2:7]
    pl = raw[7:]
    rec = {"id": rid, "len_byte": raw[0], "size": len(raw), "kind": kind,
           "kind_name": KIND_NAMES.get(kind, "unknown"), "h": h, "payload": pl}
    if kind == 0x10:
        rec["link"] = h[0]
        rec["link_name"] = LINK_NAMES.get(h[0], "unknown")
        rec["n_packets"] = h[2]
        blocks, rest = _split_blocks(pl)
        rec["packets"] = [_decode_packet(b, h[0]) for b in blocks]
        rec["rest"] = rest
        if h[0] == 0x06:
            rec["kwp_message"], rec["kwp_checksum_ok"] = kwp2000_reassemble(rec["packets"])
    elif kind == 0x01:
        rec["period_ms"] = (h[3] << 8) | h[4]
        rec["link"] = h[0]
        body = pl[2:] if h[0] == 0x05 else pl
        if h[0] in (0x02, 0x05, 0x11):
            blocks, rest = _split_blocks(body)
            rec["packets"] = [_decode_packet(b, 0x06 if h[0] in (0x05, 0x11) else 0) for b in blocks] if rest == b"" else []
            if rest:
                rec["rest"] = body
            elif h[0] == 0x05:
                rec["kwp_message"], rec["kwp_checksum_ok"] = kwp2000_reassemble(rec["packets"])
        else:
            rec["message"] = body
    elif kind in (0x02, 0x25):
        rec["message"] = pl
    elif kind == 0x40:
        rec["block"] = pl
        if len(pl) >= 3:
            rec["title"] = pl[2]
    elif kind == 0x30:
        rec["ecu_address"] = h[2]
        rec["baud"] = (h[3] << 8) | h[4]
    elif kind == 0x60:
        (rec["value"],) = struct.unpack_from(">I", h, 1)
    else:
        rec["message"] = pl
    return rec


def load(data):
    out = []
    for rid, off, end in table.records(data):
        rec = parse_record(rid, data[off:end])
        rec["offset"] = off
        rec["len_consistent"] = data[off] + 1 == end - off
        out.append(rec)
    return out


def hx(b):
    return b.hex(" ")


def describe(rec):
    parts = ["%08x kind=%02x %-13s h=[%s]" % (rec["id"], rec["kind"], rec["kind_name"], hx(rec["h"]))]
    k = rec["kind"]
    if "packets" in rec and rec["packets"]:
        if k == 0x10:
            parts.append("link=%02x(%s) n=%d" % (rec["link"], rec["link_name"], rec["n_packets"]))
        else:
            parts.append("period=%dms" % rec["period_ms"])
        for pk in rec["packets"]:
            s = "dlc=%d" % pk["dlc"]
            if pk["dlc_flags"]:
                s += "|%02x" % pk["dlc_flags"]
            if "can_id" in pk:
                s += " id=0x%03x" % pk["can_id"]
            if "opcode" in pk:
                s += " op=%02x(%s) [%s]" % (pk["opcode"], TP20_OPCODE.get(pk["opcode"] >> 4, "?"), hx(pk["chunk"]))
            else:
                s += " [%s]" % hx(pk["data"])
            parts.append("{" + s + "}")
        if rec.get("rest"):
            parts.append("REST=%s" % hx(rec["rest"]))
        if rec.get("kwp_message"):
            parts.append("msg=%s cs=%s" % (hx(rec["kwp_message"]), rec["kwp_checksum_ok"]))
    elif k == 0x01:
        parts.append("period=%dms msg=%s" % (rec["period_ms"], hx(rec.get("message", rec.get("rest", b"")))))
    elif k == 0x30:
        parts.append("slow init ecu=0x%02x baud=%d" % (rec["ecu_address"], rec["baud"]))
    elif k == 0x60:
        parts.append("value=%d" % rec["value"])
    else:
        parts.append("raw=[%s]" % hx(rec["payload"]))
    return " ".join(parts)


def cmd_cmd(a):
    from .__main__ import load_plain
    from . import container
    image = load_plain(a.image)
    blob = next(b for e, b in container.files(image) if e.name == "Cmd.BIN")
    for rec in load(blob):
        if a.id is not None and rec["id"] != a.id:
            continue
        if a.group is not None and rec["id"] >> 16 != a.group:
            continue
        print(describe(rec))


def register(sub):
    s = sub.add_parser("cmd", help="dump the diagnostic command table")
    s.add_argument("image")
    s.add_argument("-g", "--group", type=lambda x: int(x, 0), help="only this id group (id >> 16)")
    s.add_argument("-i", "--id", type=lambda x: int(x, 0), help="print one record")
    s.set_defaults(fn=cmd_cmd)
