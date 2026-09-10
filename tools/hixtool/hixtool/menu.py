"""Menu.BIN: the "For VW" menu tree (docs/data-image-format.md, section 13).

    node  := u32 title_string_id, u32 count, count x entry
    entry := u32 caption_string_id, u32 action, u32 param, u32 0, u32 0

action 0: param is the file offset of the sub menu. action with bit 31:
special function type, param the selector. caption in family 0x0102: a
module, action = its address. "Vehicle Scan": action 0x19ff, param = the
SYSSCAN procedure id (or the id in action for the Crafter branch).
"""
import struct

from . import container


def node(d, off):
    title, n = struct.unpack_from("<II", d, off)
    ents = [struct.unpack_from("<IIIII", d, off + 8 + 20 * k)[:3] for k in range(n)]
    return title, ents


def kind(caption, action, param):
    if action == 0 and param:
        return "submenu"
    if action & 0x80000000:
        return "spfunc"
    if caption >> 16 == 0x0102:
        return "module"
    if action == 0x19FF or (0xF000 <= action <= 0xF0FF):
        return "scan"
    return "?"


def dump(d, en, off=0, depth=0, seen=None, out=None):
    out = [] if out is None else out
    seen = set() if seen is None else seen
    if off in seen:
        return out
    seen.add(off)
    title, ents = node(d, off)
    t = en.get(title) if en else None
    out.append("%s[0x%04x %s n=%d]" % ("  " * depth, off, repr(t) if t else "0x%x" % title, len(ents)))
    for cap, act, par in ents:
        c = en.get(cap) if en else None
        k = kind(cap, act, par)
        desc = {"submenu": "-> 0x%x" % par, "spfunc": "special function type 0x%x selector 0x%08x" % (act & 0x7FFFFFFF, par),
                "module": "module 0x%04x" % act, "scan": "scan procedure 0x%x" % (par or act)}.get(k, "action 0x%x param 0x%x" % (act, par))
        out.append("%s  %-40s %s" % ("  " * depth, (repr(c) if c else "0x%08x" % cap)[:40], desc))
        if k == "submenu":
            dump(d, en, par, depth + 2, seen, out)
    return out


def cmd_menu(a):
    from .__main__ import load_plain
    from . import strings
    image = load_plain(a.image)
    blob = next(b for e, b in container.files(image) if e.name == "Menu.BIN")
    en = None
    try:
        en = strings.load(next(b for e, b in container.files(image) if e.name == "STRING.BIN"))["string_03_en.hix"]
    except (StopIteration, KeyError):
        pass
    lines = dump(blob, en)
    # unreferenced nodes
    p, seen = 0, set()
    while p + 8 <= len(blob):
        title, n = struct.unpack_from("<II", blob, p)
        if p not in {int(l.split()[0][3:], 16) for l in lines if l.strip().startswith("[")}:
            lines.append("[0x%04x unreferenced node, %d entries]" % (p, n))
        p += 8 + 20 * n
    print("\n".join(lines))


def register(sub):
    s = sub.add_parser("menu", help="dump the menu tree (Menu.BIN)")
    s.add_argument("image")
    s.set_defaults(fn=cmd_menu)
