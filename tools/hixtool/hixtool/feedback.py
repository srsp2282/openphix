"""The "Feedback" bus log (the 128 KiB area at flash end - 0x50000, saved by
`openphix-tool feedback` as Feedback.bin).

    0x00  "AUTOPHIX" + 5 zero bytes
    0x0D  records, back to back, until 0xFF 0xFF 0xFF 0xFF:
            u32   length, counting itself
            bytes payload (length - 4), encrypted with the image cipher
                  keyed by the payload's own offset in the area

Payload text seen so far has the shape

    "Str:" id2hex " " len4hex string NUL junk

where len counts the string and its NUL, and one uninitialised byte
follows. Records with bus traffic have not been captured yet.
"""
import struct

from . import cipher

MAGIC = b"AUTOPHIX\0\0\0\0\0"


def records(data):
    """Yield (offset, plaintext payload) for every record."""
    if not data.startswith(b"AUTOPHIX"):
        return
    p = len(MAGIC)
    while p + 4 <= len(data):
        if data[p:p + 4] == b"\xff\xff\xff\xff":
            return
        (length,) = struct.unpack_from("<I", data, p)
        if length < 4 or p + length > len(data):
            return
        yield p, cipher.decrypt(data[p + 4:p + length], p + 4)
        p += length


def describe(payload):
    """Best effort text form of one payload."""
    if payload.startswith(b"Str:") and len(payload) >= 11:
        try:
            rid = int(payload[4:6], 16)
            n = int(payload[7:11], 16)
        except ValueError:
            return repr(payload)
        text = payload[11:11 + n].split(b"\0")[0].decode("latin-1", "replace")
        return "Str id=%02x %r" % (rid, text)
    return payload.hex(" ")


def cmd_feedback(a):
    data = open(a.file, "rb").read()
    n = 0
    for off, payload in records(data):
        n += 1
        if a.verbose or not payload.startswith(b"Str:"):
            print("0x%05x %s" % (off, describe(payload)))
    if not a.verbose:
        from collections import Counter
        c = Counter(describe(p) for _, p in records(data))
        for text, cnt in c.most_common():
            print("%6d x %s" % (cnt, text))
    print("%d records" % n)


def register(sub):
    s = sub.add_parser("feedback", help="decode a Feedback.bin bus log")
    s.add_argument("file")
    s.add_argument("-v", "--verbose", action="store_true", help="every record in order")
    s.set_defaults(fn=cmd_feedback)
