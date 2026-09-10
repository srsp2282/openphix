"""Exp.BIN: the expression table. Index as in table.py, records are
`u16 len (including NUL), text, NUL` in a C-like language:

    Y=X1*256+X2;
    if(x1==0xFF&&x2==0xFF) Y=string(0x02,0x01,0x00,0x0A); else Y=1.0/32768*(x1*256+x2);

X1..X41 are response bytes, Y the result. Functions: string(a,b,c,d) (a
string id a<<24|b<<16|c<<8|d), strcmp, ASCII, SPRINTF, CHARCONVERT, HEX,
INT, ID(n) (another expression), TRANSID, RANGE. See
docs/data-image-format.md section 7 and firmware/core/exp.c for the
interpreter.
"""
import re
import struct

from . import table


def load(data):
    """-> list of (id, text)."""
    out = []
    for rid, off, end in table.records(data):
        (ln,) = struct.unpack_from("<H", data, off)
        raw = data[off + 2:off + 2 + ln]
        out.append((rid, raw.rstrip(b"\0").decode("latin-1")))
    return out


def orphans(data):
    """Records present in the byte stream but not in the index (one in the
    dump, after id 0x02080053)."""
    ents = table.parse_index(data)
    indexed = {off for _, off in ents}
    p = 4 + 8 * len(ents)
    out = []
    while p + 2 <= len(data):
        (ln,) = struct.unpack_from("<H", data, p)
        if ln == 0 or p + 2 + ln > len(data):
            break
        if p not in indexed:
            out.append((p, data[p + 2:p + 2 + ln - 1].decode("latin-1")))
        p += 2 + ln
    return out


def string_ids(text):
    """String ids referenced through string(a,b,c,d) and literal
    0x01xxxxxx style constants."""
    ids = []
    for m in re.finditer(r"string\((0x[0-9a-fA-F]+),(0x[0-9a-fA-F]+),(0x[0-9a-fA-F]+),(0x[0-9a-fA-F]+)\)", text):
        a, b, c, d = (int(x, 16) for x in m.groups())
        ids.append(a << 24 | b << 16 | c << 8 | d)
    return ids


def cmd_exp(a):
    from .__main__ import load_plain
    from . import container
    image = load_plain(a.image)
    blob = next(b for e, b in container.files(image) if e.name == "Exp.BIN")
    recs = load(blob)
    if a.id is not None:
        for rid, text in recs:
            if rid == a.id:
                print(text)
        return
    for rid, text in recs:
        if a.group is None or rid >> 16 == a.group:
            print("%08x\t%s" % (rid, text))
    for off, text in orphans(blob):
        print("orphan@%06x\t%s" % (off, text))


def register(sub):
    s = sub.add_parser("exp", help="dump the expression table")
    s.add_argument("image")
    s.add_argument("-g", "--group", type=lambda x: int(x, 0), help="only this id group (id >> 16)")
    s.add_argument("-i", "--id", type=lambda x: int(x, 0), help="print one expression")
    s.set_defaults(fn=cmd_exp)
