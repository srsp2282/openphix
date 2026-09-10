"""Images: raw RGB565 little-endian screens and 1 bpp icons.

    BGSTART_*, LGSETUP_*, IMAGE0x.bin members   320x240 RGB565 (153600 bytes)
    LGMAKER.BIN                                 117x90 RGB565
    ArrowKey.bin                                5 icons 64x64, 1 bpp, LSB first
    Readyness.bin                               32x20 + 3 x 24x20, 1 bpp, LSB first
    BGMENU.BIN                                  all zero, unused
"""
import os
import struct

from . import container

SCREEN = (320, 240)


def rgb565_rows(data, width, height):
    px = struct.unpack_from("<%dH" % (width * height), data, 0)
    rows = []
    for y in range(height):
        row = []
        for x in range(width):
            v = px[y * width + x]
            row.append((((v >> 11) & 31) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31))
        rows.append(row)
    return rows


def bitmap_rows_lsb(data, width, height):
    rb = (width + 7) // 8
    return [[(data[y * rb + x // 8] >> (x % 8)) & 1 for x in range(width)] for y in range(height)]


def save_rgb(rows, path_noext):
    h, w = len(rows), len(rows[0])
    try:
        from PIL import Image
        img = Image.new("RGB", (w, h))
        img.putdata([p for row in rows for p in row])
        img.save(path_noext + ".png")
        return path_noext + ".png"
    except ImportError:
        with open(path_noext + ".ppm", "wb") as f:
            f.write(b"P6\n%d %d\n255\n" % (w, h))
            for row in rows:
                f.write(bytes(c for p in row for c in p))
        return path_noext + ".ppm"


def save_bitmap(bits, path_noext, scale=2):
    rows = [[(0, 0, 0) if v else (255, 255, 255) for v in row for _ in range(scale)]
            for row in bits for _ in range(scale)]
    return save_rgb(rows, path_noext)


def export(image, outdir):
    os.makedirs(outdir, exist_ok=True)
    written = []
    for e, blob in container.files(image):
        n = e.name
        if n.startswith(("BGSTART", "LGSETUP")) and len(blob) == 153600:
            written.append(save_rgb(rgb565_rows(blob, *SCREEN), os.path.join(outdir, n)))
        elif n.startswith("IMAGE") and container.is_container(blob):
            for e2, b2 in container.files(blob):
                written.append(save_rgb(rgb565_rows(b2, *SCREEN), os.path.join(outdir, n + "_" + e2.name)))
        elif n == "LGMAKER.BIN":
            written.append(save_rgb(rgb565_rows(blob, 117, 90), os.path.join(outdir, n)))
        elif n == "ArrowKey.bin":
            for k, nm in enumerate(["up", "down", "right", "left", "ok"]):
                written.append(save_bitmap(bitmap_rows_lsb(blob[k * 512:(k + 1) * 512], 64, 64),
                                           os.path.join(outdir, "ArrowKey_" + nm)))
        elif n == "Readyness.bin":
            written.append(save_bitmap(bitmap_rows_lsb(blob[:80], 32, 20), os.path.join(outdir, "Readyness_header")))
            for k, nm in enumerate(["not_available", "complete", "not_complete"]):
                p = 80 + 60 * k
                written.append(save_bitmap(bitmap_rows_lsb(blob[p:p + 60], 24, 20),
                                           os.path.join(outdir, "Readyness_" + nm)))
    return written


def cmd_images(a):
    from .__main__ import load_plain
    for p in export(load_plain(a.image), a.outdir):
        print(p)


def register(sub):
    s = sub.add_parser("images", help="export every screen and icon as PNG (or PPM)")
    s.add_argument("image")
    s.add_argument("outdir")
    s.set_defaults(fn=cmd_images)
