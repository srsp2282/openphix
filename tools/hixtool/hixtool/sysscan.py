"""SYSSCAN.BIN: the control module database (docs/data-image-format.md,
section 9). Decoded: header, generic scan procedure index, module index,
scan records (items, tail entries, EV name tables), the type 2 code tables
and the address lists. Script node semantics are not decoded.
"""
import struct

u16 = lambda d, p: struct.unpack_from("<H", d, p)[0]
u32 = lambda d, p: struct.unpack_from("<I", d, p)[0]

ADDR_LIST_TAIL = b"\x02\x01\x08\x01\x02\x02"


def generic_index(d):
    p, out = 12, []
    while u32(d, p) != 0xFFFFFFFF:
        a, n = d[p + 4], d[p + 5]
        out.append({"id": u32(d, p), "a": a, "offsets": [u32(d, p + 6 + 4 * k) for k in range(n)]})
        p += 6 + 4 * n
    return out, p + 4


def module_index(d, p):
    out = []
    while u32(d, p) != 0xFFFFFFFF:
        addr, n = u32(d, p), d[p + 4]
        p += 5
        ents = [(d[p + 5 * k], u32(d, p + 5 * k + 1)) for k in range(n)]
        p += 5 * n
        out.append({"address": addr, "records": [o for _, o in ents]})
    return out, p + 4


def type2_table(d, p):
    if d[p] != 2:
        raise ValueError("no type 2 table at 0x%x" % p)
    cnt, width, sub, x = u32(d, p + 1), u16(d, p + 5), u16(d, p + 7), u16(d, p + 9)
    q, rows = p + 11, []
    for _ in range(cnt):
        rid, s, xx, ln = struct.unpack_from("<IHHH", d, q)
        rows.append({"id": rid, "sub": s, "X": xx, "value": u32(d, q + 4),
                     "name": d[q + 10:q + 10 + ln].rstrip(b"\0").decode("latin-1")})
        q += 10 + ln
    return {"offset": p, "count": cnt, "width": width, "sub": sub, "X": x, "rows": rows, "end": q}


def address_list(d, p, lim):
    n, q, ents = u16(d, p), p + 2, []
    while len(ents) < n and q + 12 <= lim and d[q + 6:q + 12] == ADDR_LIST_TAIL:
        ents.append(u32(d, q))
        q += 12
    if not ents:
        raise ValueError("no address list at 0x%x" % p)
    return {"offset": p, "declared": n, "addresses": ents, "end": q}


def record(d, r, end):
    rec = {"offset": r, "end": end, "uid": u32(d, r + 4), "flags": d[r + 13],
           "has_script": d[r + 31] == 2, "script": u32(d, r + 34) if d[r + 31] == 2 else 0}
    cnt = 0 if rec["has_script"] else d[r + 37]
    rec["items"] = [dict(zip(("key", "addr", "type", "y", "z"), struct.unpack_from("<HBBHB", d, r + 38 + 7 * i)))
                    for i in range(cnt)]
    p, tail, n = r + 38 + 7 * cnt, [], 1
    while p + 2 <= end and u16(d, p) != 0xFFFF:
        nn, sub, x, v = struct.unpack_from("<HHHI", d, p)
        if nn != n:
            break
        tail.append({"sub": sub, "X": x, "v": v})
        p += 10
        n += 1
    rec["tail"] = tail
    rec["ev_tables"] = []
    if rec["has_script"]:
        q = p
        while True:
            q = d.find(b"EV_", q, end)
            if q < 0:
                break
            hp = q - 21
            if hp >= p and d[hp] == 2 and u16(d, hp + 19) == u16(d, hp + 5) + 1:
                t = type2_table(d, hp)
                rec["ev_tables"].append(t)
                q = t["end"]
            else:
                q += 3
    return rec


class SysScan:
    def __init__(self, d):
        self.d = d
        self.module_index_ptr = u32(d, 8)
        self.generic, gen_end = generic_index(d)
        self.modules, self.module_index_end = module_index(d, self.module_index_ptr)
        offs = set(o for g in self.generic for o in g["offsets"])
        offs |= set(o for m in self.modules for o in m["records"])
        p = gen_end
        while d[p:p + 4] == b"\x08\x00\x00\x00" and d[p + 8:p + 13] == b"\0" * 5:
            offs.add(p)
            p += 38 if d[p + 31] == 2 else 38 + 7 * d[p + 37] + 12
        self.generic_records_end = p
        offs = sorted(offs)
        self.records = {}
        for i, r in enumerate(offs):
            e = offs[i + 1] if i + 1 < len(offs) else len(d)
            if r < self.module_index_ptr < e:
                e = self.module_index_ptr
            self.records[r] = record(d, r, e)
        # code tables referenced from type 4 nodes: 04 00 00 00 id ptr 0 ffffffff
        self.code_tables = {}
        q = self.generic_records_end
        while True:
            q = d.find(b"\x04\x00\x00\x00", q, self.module_index_ptr)
            if q < 0:
                break
            ptr = u32(d, q + 8)
            if u32(d, q + 12) == 0 and u32(d, q + 16) == 0xFFFFFFFF and ptr < len(d) and d[ptr] == 2:
                try:
                    t = type2_table(d, ptr)
                    t["ref_id"] = u32(d, q + 4)
                    self.code_tables[ptr] = t
                except (ValueError, struct.error):
                    pass
            q += 4
        self.address_lists = {}
        vals = sorted(v["value"] for t in self.code_tables.values() for v in t["rows"]
                      if 0x1000 < v["value"] < self.module_index_ptr)
        if vals:
            p = vals[0]
            while p < self.module_index_ptr:
                try:
                    L = address_list(d, p, self.module_index_ptr)
                except (ValueError, struct.error):
                    break
                self.address_lists[p] = L
                p = L["end"]

    def module(self, address):
        for m in self.modules:
            if m["address"] == address:
                return m
        return None

    def data_sets(self, address):
        """(sub, X) keys listed by the module's family records."""
        m = self.module(address)
        out = []
        for o in m["records"] if m else []:
            for t in self.records[o]["tail"]:
                if 0x30 <= (t["X"] & 0xFF) <= 0x34:
                    out.append((t["sub"], t["X"]))
        return out

    def ev_lookup(self, name):
        for rec in self.records.values():
            for t in rec["ev_tables"]:
                for row in t["rows"]:
                    if row["name"] == name:
                        return row["sub"], row["X"]
        return None


def cmd_modules(a):
    from .__main__ import load_plain
    from . import container, strings
    image = load_plain(a.image)
    blob = next(b for e, b in container.files(image) if e.name == "SYSSCAN.BIN")
    s = SysScan(blob)
    en = None
    try:
        en = strings.load(next(b for e, b in container.files(image) if e.name == "STRING.BIN"))["string_03_en.hix"]
    except (StopIteration, KeyError):
        pass
    if a.ev:
        for rec in s.records.values():
            for t in rec["ev_tables"]:
                print("## table @0x%x X=%04x rows=%d" % (t["offset"], t["X"], t["count"]))
                for r in t["rows"]:
                    print("%5d %-32s (%04x,%04x)" % (r["id"], r["name"], r["sub"], r["X"]))
        return
    if a.codes:
        for off, t in sorted(s.code_tables.items()):
            print("## table @0x%x ref_id=0x%x rows=%d width=%d" % (off, t["ref_id"], t["count"], t["width"]))
            for r in t["rows"]:
                print("%-4s -> 0x%x" % (r["name"], r["value"]))
        return
    print("generic procedures: " + " ".join("%04x(%d)" % (g["id"], len(g["offsets"])) for g in s.generic))
    print("code tables: %d, address lists: %d, module index entries: %d" %
          (len(s.code_tables), len(s.address_lists), len(s.modules)))
    for m in s.modules:
        addr = m["address"]
        if a.address is not None and addr != a.address:
            continue
        name = en.get(0x01020000 | (addr & 0xFFFF)) if en and addr < 0x10000 else None
        sets = s.data_sets(addr)
        recs = []
        for o in m["records"]:
            r = s.records[o]
            desc = "@%x" % o
            if r["items"]:
                desc += " items[%s]" % ",".join("%02x" % i["type"] for i in r["items"])
            if r["tail"]:
                desc += " tail[%s x%d]" % (",".join(sorted(set("%02x" % (t["X"] & 0xFF) for t in r["tail"]))), len(r["tail"]))
            if r["has_script"]:
                desc += " script"
            for t in r["ev_tables"]:
                desc += " EV(%d)" % t["count"]
            recs.append(desc)
        print("%08x %-40s sets=%-4d %s" % (addr, (name or "")[:40], len(sets), " | ".join(recs)))


def register(sub):
    s = sub.add_parser("modules", help="dump the control module database (SYSSCAN.BIN)")
    s.add_argument("image")
    s.add_argument("-a", "--address", type=lambda x: int(x, 0), help="only this module index address")
    s.add_argument("--ev", action="store_true", help="dump the EV name tables (part number to data set)")
    s.add_argument("--codes", action="store_true", help="dump the VIN model code tables")
    s.set_defaults(fn=cmd_modules)
