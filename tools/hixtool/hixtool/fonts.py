"""Bitmap fonts.

Raw fonts (POL_WIN_*, RUSS_ISO_*, WESTISO_*): 256 glyphs, no header, each
glyph ceil(w/8) bytes per row, rows top to bottom, MSB = leftmost pixel.
The glyph code is the byte value in the font's code page.

MFL fonts (SCN16_CP936.BIN, SCN24_CP936.BIN): 16 byte header ("MFL" 0x10,
u32 file size, u8 0, u8 glyph size, u8 2, u8 0, 4 x 0) then 8178 glyphs in
GB2312 order (94 cells per row, rows 0xA1..0xF7), same bit layout.
"""
import struct

RAW_FONTS = {
    "POL_WIN_8x16.bin": (8, 16, "cp1250"),
    "POL_WIN_16x16.bin": (16, 16, "cp1250"),
    "POL_WIN_24x24.bin": (24, 24, "cp1250"),
    "RUSS_ISO_8x16.BIN": (8, 16, "iso8859-5"),
    "RUSS_ISO_16x16.BIN": (16, 16, "iso8859-5"),
    "RUSS_ISO_24x24.BIN": (24, 24, "iso8859-5"),
    "WESTISO_16X16.bin": (16, 16, "latin-1"),
    "WESTISO_20X20.BIN": (20, 20, "latin-1"),
    "WESTISO_20X32.BIN": (20, 32, "latin-1"),
    "WESTISO_24X24.bin": (24, 24, "latin-1"),
}


class Font:
    def __init__(self, data, width, height, codepage=None, offset=0, count=256):
        self.width, self.height, self.codepage = width, height, codepage
        self.row_bytes = (width + 7) // 8
        self.glyph_bytes = self.row_bytes * height
        self.data, self.offset, self.count = data, offset, count

    def glyph(self, index):
        p = self.offset + index * self.glyph_bytes
        return self.data[p:p + self.glyph_bytes]

    def rows(self, index):
        g = self.glyph(index)
        out = []
        for y in range(self.height):
            out.append("".join("#" if (g[y * self.row_bytes + x // 8] >> (7 - x % 8)) & 1 else "."
                               for x in range(self.width)))
        return out


def load_raw(name, data):
    w, h, cp = RAW_FONTS[name]
    if len(data) != 256 * ((w + 7) // 8) * h:
        raise ValueError("%s: unexpected size %d" % (name, len(data)))
    return Font(data, w, h, cp)


def load_mfl(data):
    if data[:4] != b"MFL\x10":
        raise ValueError("not an MFL font")
    (size,) = struct.unpack_from("<I", data, 4)
    if size != len(data):
        raise ValueError("MFL size mismatch")
    gs = (size - 16) // 8178
    dim = {32: 16, 72: 24}[gs]
    return Font(data, dim, dim, "gb2312", 16, 8178)


def gb2312_index(ch):
    b = ch.encode("gb2312")
    return (b[0] - 0xA1) * 94 + (b[1] - 0xA1)


def cmd_font(a):
    from .__main__ import load_plain
    from . import container
    image = load_plain(a.image)
    blob = None
    for e, b in container.files(image):
        if e.name.lower() == a.name.lower():
            blob, a.name = b, e.name
    if blob is None:
        raise SystemExit("no such font in image")
    if a.name.startswith("SCN"):
        f = load_mfl(blob)
        chars = a.chars or "中国±℃"
        for ch in chars:
            print("== %s (gb2312 %s)" % (ch, ch.encode("gb2312").hex()))
            print("\n".join(f.rows(gb2312_index(ch))))
    else:
        f = load_raw(a.name, blob)
        chars = a.chars or "AÄ"
        for ch in chars:
            code = ch.encode(f.codepage)[0]
            print("== 0x%02x %s" % (code, ch))
            print("\n".join(f.rows(code)))


def register(sub):
    s = sub.add_parser("font", help="render glyphs of a font as ASCII art")
    s.add_argument("image")
    s.add_argument("name", help="font file name, e.g. WESTISO_16X16.bin")
    s.add_argument("chars", nargs="?", help="characters to render")
    s.set_defaults(fn=cmd_font)
