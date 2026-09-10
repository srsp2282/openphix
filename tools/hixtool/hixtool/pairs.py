"""Key to string id tables: DsTransID.BIN (measuring block texts) and
UDSDTCTransID.BIN (UDS fault code texts). Both are hix containers whose
members are `u32 count` then sorted `{u32 key, u32 string_id}` pairs.
See docs/data-image-format.md sections 10 and 11.
"""
import struct

from . import container


def sections(data):
    """-> list of (name, [(key, string_id), ...])."""
    out = []
    for e, blob in container.files(data):
        (n,) = struct.unpack_from("<I", blob, 0)
        out.append((e.name, [struct.unpack_from("<II", blob, 4 + 8 * i) for i in range(n)]))
    return out


def lookup(pairs, key):
    lo, hi = 0, len(pairs)
    while lo < hi:
        mid = (lo + hi) // 2
        if pairs[mid][0] == key:
            return pairs[mid][1]
        if pairs[mid][0] < key:
            lo = mid + 1
        else:
            hi = mid
    return None


def ds_key(group, block, field):
    return (group << 16) | (block << 8) | field


def _cmd(a, fname):
    from .__main__ import load_plain
    from . import strings
    image = load_plain(a.image)
    blob = next(b for e, b in container.files(image) if e.name == fname)
    en = None
    try:
        en = strings.load(next(b for e, b in container.files(image) if e.name == "STRING.BIN"))["string_03_en.hix"]
    except (StopIteration, KeyError):
        pass
    for name, pairs in sections(blob):
        if a.section and a.section.lower() != name.lower():
            continue
        if a.list:
            print("%-24s %6d records" % (name, len(pairs)))
            continue
        print("## %s (%d)" % (name, len(pairs)))
        for k, v in pairs:
            if a.key is not None and k != a.key:
                continue
            t = en.get(v) if en else None
            print("%08x -> %08x%s" % (k, v, ("  " + strings.esc(t)) if t else ""))


def cmd_dstext(a):
    _cmd(a, "DsTransID.BIN")


def cmd_udsdtc(a):
    _cmd(a, "UDSDTCTransID.BIN")


def register(sub):
    for name, fn, help_ in (("dstext", cmd_dstext, "measuring block text ids (DsTransID.BIN)"),
                            ("udsdtc", cmd_udsdtc, "UDS fault code text ids (UDSDTCTransID.BIN)")):
        s = sub.add_parser(name, help=help_)
        s.add_argument("image")
        s.add_argument("-s", "--section", help="only this member (e.g. TEXT, EV_ECM)")
        s.add_argument("-k", "--key", type=lambda x: int(x, 0), help="only this key")
        s.add_argument("-l", "--list", action="store_true", help="list members and counts")
        s.set_defaults(fn=fn)
