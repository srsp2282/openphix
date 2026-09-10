"""Command line front end. Run as `python3 -m hixtool` or via the `hixtool`
wrapper script next to this package."""
import argparse
import os
import sys

from . import cipher, container


def load_plain(path):
    """Read an image file and return it decrypted, whatever state it is in."""
    data = open(path, "rb").read()
    if cipher.looks_encrypted(data):
        data = cipher.decrypt(data)
    return data


def cmd_decrypt(a):
    data = open(a.input, "rb").read()
    open(a.output, "wb").write(cipher.decrypt(data, a.offset))


def cmd_encrypt(a):
    data = open(a.input, "rb").read()
    open(a.output, "wb").write(cipher.encrypt(data, a.offset))


def _walk(data, prefix, depth, out):
    for e, blob in container.files(data):
        out.append((prefix + e.name, e.offset, e.size, blob))
        if depth and container.is_container(blob):
            _walk(blob, prefix + e.name + "/", depth - 1, out)


def cmd_ls(a):
    data = load_plain(a.image)
    if not container.is_container(data):
        sys.exit("not a data image (no file directory at offset 0)")
    out = []
    _walk(data, "", 2, out)
    print("%-44s %10s %10s" % ("name", "offset", "size"))
    for name, off, size, _ in out:
        print("%-44s 0x%08x %10d" % (name, off, size))


def cmd_extract(a):
    data = load_plain(a.image)
    if not container.is_container(data):
        sys.exit("not a data image (no file directory at offset 0)")
    out = []
    _walk(data, "", 2 if a.recursive else 0, out)
    os.makedirs(a.outdir, exist_ok=True)
    for name, _, _, blob in out:
        path = os.path.join(a.outdir, name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        open(path, "wb").write(blob)
    print("extracted %d files to %s" % (len(out), a.outdir))


def cmd_pack(a):
    """Rebuild an image from an extracted directory. The file order is taken
    from `order.txt` in the directory if present, else sorted names."""
    names = None
    order = os.path.join(a.indir, "order.txt")
    if os.path.exists(order):
        names = [l.strip() for l in open(order) if l.strip()]
    else:
        names = sorted(os.listdir(a.indir))
    items = [(n, open(os.path.join(a.indir, n), "rb").read()) for n in names]
    data = container.build(items)
    if a.encrypt:
        data = cipher.encrypt(data)
    open(a.output, "wb").write(data)
    print("wrote %d bytes to %s" % (len(data), a.output))


def main(argv=None):
    p = argparse.ArgumentParser(prog="hixtool", description=__doc__)
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("decrypt", help="remove the data image cipher")
    s.add_argument("input")
    s.add_argument("output")
    s.add_argument("-a", "--offset", type=lambda x: int(x, 0), default=0,
                   help="position of the input inside the image")
    s.set_defaults(fn=cmd_decrypt)

    s = sub.add_parser("encrypt", help="apply the data image cipher")
    s.add_argument("input")
    s.add_argument("output")
    s.add_argument("-a", "--offset", type=lambda x: int(x, 0), default=0)
    s.set_defaults(fn=cmd_encrypt)

    s = sub.add_parser("ls", help="list the files in an image (plain or encrypted)")
    s.add_argument("image")
    s.set_defaults(fn=cmd_ls)

    s = sub.add_parser("extract", help="write every file of an image to a directory")
    s.add_argument("image")
    s.add_argument("outdir")
    s.add_argument("-r", "--recursive", action="store_true",
                   help="also unpack the nested containers (STRING.BIN, IMAGE0x.bin)")
    s.set_defaults(fn=cmd_extract)

    s = sub.add_parser("pack", help="rebuild an image from an extracted directory")
    s.add_argument("indir")
    s.add_argument("output")
    s.add_argument("--encrypt", action="store_true", help="apply the cipher to the result")
    s.set_defaults(fn=cmd_pack)

    from . import decoders
    decoders.register(sub)

    a = p.parse_args(argv)
    a.fn(a)


if __name__ == "__main__":
    main()
