"""String tables: STRING.BIN holds one string_NN_xx.hix per UI language.

    string_NN_xx.hix
        u32  count
        count x { u32 id, u32 offset }   sorted by id
        records { u16 declared_len, bytes, NUL } back to back

The declared length is unreliable (it counts escape sequences of the
source text); the real extent is the distance to the next record. Strings
are single byte, in the code page of the language (ISO-8859-1 for the
Latin languages, ISO-8859-5 for Russian). Bytes below 0x20 are glyph codes
in the fonts (arrows, degree signs). Every other database file refers to
strings by the u32 id; id >> 16 is a group. See docs/data-image-format.md.
"""
import collections
import struct

from . import container

CODEPAGE = {7: "iso8859-5"}          # everything else is latin-1
DEFAULT_CODEPAGE = "latin-1"

CONTROL_GLYPHS = {
    0x01: "[UP]", 0x02: "[DOWN]", 0x03: "[LEFT]", 0x04: "[RIGHT]",
    0x07: "\u00b0C", 0x08: "\u00b0F", 0x09: "\u00b0",
}

GROUPS = collections.OrderedDict([
    (0x0000, "core UI, prompts, on-screen keyboard"),
    (0x0001, "generic OBDII, tool setup"),
    (0x0010, "special function errors"),
    (0x00e0, "tool setup, battery test"),
    (0x00f0, "professional mode UI"),
    (0x00f1, "professional mode functions"),
    (0x0100, "ECU family names EV_*"),
    (0x0102, "VAG module names, index = address"),
    (0x0104, "DTC status and fault type suffixes"),
    (0x0108, "special functions"),
    (0x0109, "adaptation channels"),
    (0x0110, "battery part numbers"),
    (0x01ff, "late additions"),
    (0x0200, "ECU info labels, protocol names"),
    (0x0201, "datastream enumerations"),
    (0x0202, "UDS datastream names"),
    (0x0203, "measuring block field texts"),
    (0x0205, "measuring block status texts"),
    (0x0301, "units"),
    (0x0302, "units in brackets"),
    (0xa000, "VAG 5 digit DTC, index = number"),
    (0xa100, "VAG UDS DTC: a1 DDDD TT"),
    (0xa200, "VAG KWP2000 DTC: a2 VV DDDD"),
    (0xa300, "DTC text list"),
    (0xff00, "generic OBDII DTC: ff00 DDDD"),
    (0xff01, "manufacturer DTC: ff MM DDDD"),
])


def group_name(group):
    if group in GROUPS:
        return GROUPS[group]
    if 0xa100 <= group <= 0xa1ff:
        return GROUPS[0xa100]
    if 0xa200 <= group <= 0xa2ff:
        return GROUPS[0xa200]
    if 0xff01 <= group <= 0xffff:
        return GROUPS[0xff01]
    return "?"


class Table:
    """One language."""

    def __init__(self, name, data):
        self.name = name
        parts = name.split("_")
        self.number = int(parts[1])
        self.code = parts[2].split(".")[0]
        self.codepage = CODEPAGE.get(self.number, DEFAULT_CODEPAGE)
        (n,) = struct.unpack_from("<I", data, 0)
        self.index = [struct.unpack_from("<II", data, 4 + 8 * k) for k in range(n)]
        self.data = data
        self._byid = None

    def __len__(self):
        return len(self.index)

    def raw(self, k):
        sid, off = self.index[k]
        nxt = self.index[k + 1][1] if k + 1 < len(self.index) else len(self.data)
        raw = self.data[off + 2:nxt]
        return raw[:-1] if raw.endswith(b"\0") else raw

    def get(self, sid, symbolic=True):
        """Text for a string id, or None."""
        if self._byid is None:
            self._byid = {s: k for k, (s, _) in enumerate(self.index)}
        k = self._byid.get(sid)
        return None if k is None else decode(self.raw(k), self.codepage, symbolic)

    def items(self, symbolic=True):
        for k, (sid, _) in enumerate(self.index):
            yield sid, decode(self.raw(k), self.codepage, symbolic)


def decode(raw, codepage=DEFAULT_CODEPAGE, symbolic=True):
    s = raw.decode(codepage, errors="replace")
    if symbolic:
        for code, rep in CONTROL_GLYPHS.items():
            s = s.replace(chr(code), rep)
    return s


def load(string_bin):
    """STRING.BIN bytes -> OrderedDict name -> Table."""
    out = collections.OrderedDict()
    for e, blob in container.files(string_bin):
        out[e.name] = Table(e.name, blob)
    return out


def esc(s):
    return s.replace("\\", "\\\\").replace("\n", "\\n").replace("\t", "\\t")


def cmd_strings(a):
    from .__main__ import load_plain
    image = load_plain(a.image)
    tables = load(_string_bin(image))
    if a.lookup is not None:
        for name, t in tables.items():
            s = t.get(a.lookup)
            print("%-20s %s" % (name, esc(s) if s is not None else "(missing)"))
        return
    if a.lang:
        tables = collections.OrderedDict((n, t) for n, t in tables.items() if a.lang in n)
    for name, t in tables.items():
        if a.summary:
            c = collections.Counter(sid >> 16 for sid, _ in t.index)
            print("%s: %d strings" % (name, len(t)))
            for g, n in sorted(c.items()):
                print("   %04x %6d  %s" % (g, n, group_name(g)))
            continue
        print("# %s: %d strings (id, text)" % (name, len(t)))
        for sid, s in t.items():
            if a.group is None or (sid >> 16) == a.group:
                print("0x%08x\t%s" % (sid, esc(s)))


def _string_bin(image):
    for e, blob in container.files(image):
        if e.name.lower() == "string.bin":
            return blob
    raise SystemExit("no STRING.BIN in image")


def register(sub):
    s = sub.add_parser("strings", help="dump the string tables")
    s.add_argument("image")
    s.add_argument("-l", "--lang", help="only tables whose name contains this (e.g. _en)")
    s.add_argument("-g", "--group", type=lambda x: int(x, 0), help="only this id group (id >> 16)")
    s.add_argument("-s", "--summary", action="store_true", help="counts per group instead of text")
    s.add_argument("-i", "--lookup", type=lambda x: int(x, 0), help="print one id in every language")
    s.set_defaults(fn=cmd_strings)
