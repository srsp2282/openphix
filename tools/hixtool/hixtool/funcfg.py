"""FUNCFG.BIN: the per control module function configuration
(docs/data-image-format.md, section 15).

    record := u16 variant, u8 kind, u8 module, u32 proto_ptr, u32 slot[6], u8 n, u32 extra[n]
    slots: 0 connect, 1 keep alive, 2 version info, 3 read DTC, 4 erase DTC, 5 datastream;
    extras: 05 basic settings, 07 adaptation, 08 access authorization.

Only the UDS forms of the slot chunks are decoded here (kinds 0x30 to 0x34,
0x70, 0x80); the K-line and TP2.0 forms are hex dumped.
"""
import struct

from . import container

u8 = lambda d, p: d[p]
u16 = lambda d, p: struct.unpack_from("<H", d, p)[0]
u32 = lambda d, p: struct.unpack_from("<I", d, p)[0]

KIND_NAMES = {0x01: "K-line KWP2000", 0x02: "TP2.0", 0x04: "KWP1281", 0x05: "KWP2000 fast init",
              0x06: "TP1.6", 0x30: "UDS 11 bit", 0x31: "UDS variant", 0x32: "UDS 29 bit",
              0x33: "UDS", 0x34: "UDS", 0x70: "Crafter LT3", 0x80: "Crafter LT3 (2)"}


def records(d):
    p, out = 0, []
    while p + 33 <= len(d):
        variant, kind, module, proto = struct.unpack_from("<HBBI", d, p)
        slots = list(struct.unpack_from("<6I", d, p + 8))
        n = d[p + 32]
        extra = list(struct.unpack_from("<%dI" % n, d, p + 33)) if n else []
        rec = {"offset": p, "variant": variant, "kind": kind, "module": module, "proto": proto,
               "slots": slots, "extra": extra}
        p += 33 + 4 * n
        if kind == 0xFF:
            rec["table_end"] = slots[0]
            out.append(rec)
            break
        out.append(rec)
    return out


def proto(d, p):
    if not p:
        return None
    kind, flags = d[p], d[p + 1]
    nids = d[p + 15]
    ids = [u32(d, p + 16 + 4 * k) for k in range(nids)]
    q = p + 16 + 4 * nids + 1
    timing = list(struct.unpack_from("<6H", d, q))
    dec = [(i >> 3) if kind == 3 else ((i & 0xFFFF) >> 5) for i in ids]
    return {"proto": kind, "flags": flags, "ids": dec, "timing": timing, "end": q + 12}


def cmd_list(d, p):
    n = d[p]
    items = [struct.unpack_from("<IHB", d, p + 1 + 7 * k) for k in range(n)]
    return items, p + 1 + 7 * n


def node_header(d, p):
    """-> (variants list of (cond_ptr, body_offset))"""
    if d[p] == 0xFF:
        nvar = u16(d, p + 1)
        q = p + 3 + 4 * nvar + 4 + 2
        ptrs = [u32(d, q + 4 * k) for k in range(nvar)]
        return [(u32(d, np + 3), np + 7) for np in ptrs]
    return [(u32(d, p + 3), p + 7)]


def slot0(d, p):
    n = u32(d, p)
    q, steps = p + 8, []
    for _ in range(n):
        seq, exp = u32(d, q), u32(d, q + 4)
        cmds, q = cmd_list(d, q + 10)
        steps.append({"seq": seq, "exp": exp, "cmds": cmds})
    return steps, q


def slot1(d, p):
    n = u16(d, p + 4)
    q, steps = p + 6, []
    for _ in range(n):
        seq, exp = u32(d, q), u32(d, q + 4)
        cmds, q = cmd_list(d, q + 8)
        steps.append({"seq": seq, "exp": exp, "cmds": cmds})
    return steps, q


def version_body(d, p):
    fmt = d[p + 19]
    n = u16(d, p + 20)
    q, steps = p + 22, []
    for _ in range(n):
        seq, label = u16(d, q), u32(d, q + 2)
        if fmt == 3:
            steps.append({"seq": seq, "label": label, "ptr": u32(d, q + 10)})
            q += 14
        else:
            exp = u32(d, q + 9)
            cmds, q = cmd_list(d, q + 13)
            steps.append({"seq": seq, "label": label, "exp": exp, "cmds": cmds})
    return steps, q + 2


def dtc_body(d, p):
    if d[p] != 2 or d[p + 14] != 6:
        return None
    n = u16(d, p + 27)
    q, entries = p + 29, []
    for _ in range(n):
        seq, exp_status, module = u16(d, q), u32(d, q + 6), u16(d, q + 12)
        cmds, q = cmd_list(d, q + 16)
        entries.append({"seq": seq, "exp_status": exp_status, "module": module, "cmds": cmds})
    m = u16(d, q + 1)
    refs = [u32(d, q + 3 + 4 * k) for k in range(m)]
    return {"ref": u32(d, p + 6), "entries": entries, "dtc_refs": refs, "end": q + 3 + 4 * m}


def datastream_body(d, p):
    if d[p] != 0x15:
        return None
    table = u32(d, p + 20)
    n = u16(d, p + 24)
    ids = [u16(d, p + 26 + 2 * k) for k in range(n)]
    return {"table": table, "channels": ids, "end": p + 26 + 2 * n + 2}


def channel_table(d, p):
    n = u16(d, p)
    q, out = p + 2, []
    for _ in range(n):
        cid, name = u16(d, q), u32(d, q + 2)
        read, q = cmd_list(d, q + 14)
        exp = u32(d, q + 2)
        write, q = cmd_list(d, q + 6)
        out.append({"id": cid, "name": name, "read": read, "exp": exp, "write": write})
    return out


def cmd_funcfg(a):
    from .__main__ import load_plain
    from . import strings
    image = load_plain(a.image)
    blob = next(b for e, b in container.files(image) if e.name == "FUNCFG.BIN")
    en = None
    try:
        en = strings.load(next(b for e, b in container.files(image) if e.name == "STRING.BIN"))["string_03_en.hix"]
    except (StopIteration, KeyError):
        pass
    S = (lambda i: (en.get(i) or "") if en else "")
    recs = records(blob)
    if a.module is None:
        from collections import Counter
        c = Counter(r["kind"] for r in recs)
        print("%d records" % len(recs))
        for k, n in sorted(c.items()):
            mods = sorted(set(r["module"] for r in recs if r["kind"] == k))
            print("  kind %02x %-20s %5d records, %d modules" % (k, KIND_NAMES.get(k, "?"), n, len(mods)))
        return
    sel = [r for r in recs if r["module"] == a.module and (a.kind is None or r["kind"] == a.kind)
           and (a.variant is None or r["variant"] == a.variant)]
    if a.variant is None and a.kind is not None:
        sel = sel[:1]
    for r in sel:
        print("record @0x%x variant %d kind %02x module %02x proto @0x%x" % (r["offset"], r["variant"], r["kind"], r["module"], r["proto"]))
        pr = proto(blob, r["proto"])
        if pr:
            print("  proto %d ids %s timing %s" % (pr["proto"], " ".join("0x%x" % i for i in pr["ids"]), pr["timing"]))
        names = ["connect", "keepalive", "version", "read dtc", "erase dtc", "datastream"]
        for i, s in enumerate(r["slots"]):
            if not s:
                continue
            print("  slot %d %-10s @0x%x" % (i, names[i], s))
            if i == 0:
                steps, _ = slot0(blob, s)
            elif i == 1:
                steps, _ = slot1(blob, s)
            else:
                steps = None
            if steps is not None:
                for st in steps:
                    print("      step %d exp 0x%x cmds %s" % (st["seq"], st["exp"], " ".join("%08x" % c[0] for c in st["cmds"])))
                continue
            for cond, body in node_header(blob, s):
                if cond:
                    print("      condition exp 0x%x cmd %s" % (u32(blob, cond + 3), " ".join("%08x" % c[0] for c in cmd_list(blob, cond + 7)[0])))
                if i == 2:
                    for st in version_body(blob, body)[0]:
                        print("      %-30s exp 0x%-6x cmds %s" % (S(st["label"])[:30], st.get("exp", 0), " ".join("%08x" % c[0] for c in st.get("cmds", []))))
                elif i == 3:
                    b = dtc_body(blob, body)
                    if b:
                        for e in b["entries"]:
                            print("      read %s status exp 0x%x" % (" ".join("%08x" % c[0] for c in e["cmds"]), e["exp_status"]))
                        print("      dtc refs %s" % " ".join("%08x" % x for x in b["dtc_refs"]))
                    else:
                        print("      (not decoded: %s)" % blob[body:body + 24].hex(" "))
                elif i == 5:
                    b = datastream_body(blob, body)
                    if b:
                        tab = {c["id"]: c for c in channel_table(blob, b["table"])}
                        print("      %d channels from table @0x%x" % (len(b["channels"]), b["table"]))
                        for cid in b["channels"][:a.limit]:
                            c = tab.get(cid)
                            if c:
                                print("      %04x %-36s read %s exp 0x%x" % (cid, S(c["name"])[:36], " ".join("%08x" % x[0] for x in c["read"]), c["exp"]))
                    else:
                        print("      (not decoded: %s)" % blob[body:body + 24].hex(" "))
        for i, s in enumerate(r["extra"]):
            if s:
                print("  extra %d %s @0x%x  %s (%d items)" % (i, ["basic settings", "adaptation", "login"][i], s,
                                                            S(u32(blob, s + 7)), u32(blob, s + 22)))


def register(sub):
    s = sub.add_parser("funcfg", help="function configuration per module (FUNCFG.BIN)")
    s.add_argument("image")
    s.add_argument("-m", "--module", type=lambda x: int(x, 0), help="module address")
    s.add_argument("-k", "--kind", type=lambda x: int(x, 0), help="protocol kind (0x30 UDS, 2 TP2.0, ...)")
    s.add_argument("-v", "--variant", type=lambda x: int(x, 0), help="variant index")
    s.add_argument("-n", "--limit", type=int, default=12, help="channels to list")
    s.set_defaults(fn=cmd_funcfg)
