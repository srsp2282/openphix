"""ComboSpFunc.BIN: special function scripts (docs/data-image-format.md,
section 12). The index and the function table are decoded; the step
grammar of the records is not, so records are shown as a token stream.
"""
import struct

from . import container

u16 = lambda d, p: struct.unpack_from("<H", d, p)[0]
u32 = lambda d, p: struct.unpack_from("<I", d, p)[0]


def parse(d):
    n = u32(d, 0)
    index = [dict(zip(("variant", "kind", "function", "offset"), struct.unpack_from("<BBHI", d, 4 + 8 * i)))
             for i in range(n)]
    p, ftab = 4 + 8 * n, []
    while p + 5 <= len(d):
        sub, x, cnt = u16(d, p), u16(d, p + 2), d[p + 4]
        if cnt == 0 or cnt > 64 or p + 5 + 4 * cnt > len(d):
            break
        ents = [struct.unpack_from("<BBH", d, p + 5 + 4 * k) for k in range(cnt)]
        if any(e[0] == 0 or e[0] > 40 for e in ents):
            break
        ftab.append({"sub": sub, "X": x, "functions": ents})
        p += 5 + 4 * cnt
    offs = sorted(set(e["offset"] for e in index))
    ends = {offs[i]: (offs[i + 1] if i + 1 < len(offs) else len(d)) for i in range(len(offs))}
    return {"index": index, "ftab": ftab, "ftab_end": p, "rec_start": offs[0], "rec_ends": ends}


def tokens(d, o, e, rec_start, families):
    out, q = [], o
    while q < e:
        v = u32(d, q) if q + 4 <= e else 0
        if rec_start <= v < len(d) and v != 0:
            out.append(("ptr", v))
            q += 4
            continue
        if v > 0xFFFF and (v >> 16) in families:
            out.append(("str", v))
            q += 4
            continue
        ln = u16(d, q) if q + 2 <= e else 0
        if 3 <= ln <= 64 and q + 2 + ln <= e and d[q + 1 + ln] == 0 and all(32 <= c < 127 for c in d[q + 2:q + 1 + ln]):
            out.append(("name", d[q + 2:q + 1 + ln].decode("latin-1")))
            q += 2 + ln
            continue
        out.append(("b", d[q]))
        q += 1
    return out


def cmd_spfunc(a):
    from .__main__ import load_plain
    from . import strings
    image = load_plain(a.image)
    blob = next(b for e, b in container.files(image) if e.name == "ComboSpFunc.BIN")
    c = parse(blob)
    en = None
    try:
        en = strings.load(next(b for e, b in container.files(image) if e.name == "STRING.BIN"))["string_03_en.hix"]
    except (StopIteration, KeyError):
        pass
    fams = set(sid >> 16 for sid, _ in en.index) if en else set()
    if a.function is None:
        print("%d scripts, %d function table rows" % (len(c["index"]), len(c["ftab"])))
        for e in c["index"]:
            title = en.get(u32(blob, e["offset"] + 1)) if en else None
            print("function %3d variant %d kind %02x @0x%06x %6d bytes  %s" %
                  (e["function"], e["variant"], e["kind"], e["offset"],
                   c["rec_ends"][e["offset"]] - e["offset"], title or ""))
        return
    for e in c["index"]:
        if e["function"] != a.function:
            continue
        o, end = e["offset"], c["rec_ends"][e["offset"]]
        print("## function %d variant %d kind %02x @0x%x" % (e["function"], e["variant"], e["kind"], o))
        line = []
        for kind, v in tokens(blob, o, end, c["rec_start"], fams):
            if kind == "ptr":
                line.append("[ptr %x]" % v)
            elif kind == "str":
                t = en.get(v) if en else None
                line.append("[str %08x%s]" % (v, (" " + repr(t[:40])) if t else ""))
            elif kind == "name":
                line.append("[image %s]" % v)
            else:
                line.append("%02x" % v)
        print(" ".join(line))


def register(sub):
    s = sub.add_parser("spfunc", help="special function scripts (ComboSpFunc.BIN)")
    s.add_argument("image")
    s.add_argument("-f", "--function", type=int, help="token stream of one function's scripts")
    s.set_defaults(fn=cmd_spfunc)
