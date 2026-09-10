"""The "hix" file container used by the data image and by STRING.BIN,
IMAGE00.bin and IMAGE01.bin inside it.

    u8   count
    count times:
        u16le name_len          length of the name including its NUL
        char name[name_len]
        u32le offset            from the start of the container
    file data, in offset order, back to back

A file ends where the next one begins; the last one runs to the end of the
container. Nothing in the header gives sizes or checksums.
"""
import struct


class Entry:
    __slots__ = ("name", "offset", "size")

    def __init__(self, name, offset, size):
        self.name, self.offset, self.size = name, offset, size

    def __repr__(self):
        return "Entry(%r, 0x%x, %d)" % (self.name, self.offset, self.size)


def parse_directory(data):
    """Return the list of entries, or None when `data` is not a container."""
    if not data:
        return None
    count = data[0]
    if count == 0:
        return None
    p = 1
    entries = []
    for _ in range(count):
        if p + 2 > len(data):
            return None
        (name_len,) = struct.unpack_from("<H", data, p)
        p += 2
        if name_len == 0 or name_len > 64 or p + name_len + 4 > len(data):
            return None
        raw = data[p:p + name_len]
        if raw[-1] != 0 or not all(0x20 <= c < 0x7F for c in raw[:-1]):
            return None
        p += name_len
        (offset,) = struct.unpack_from("<I", data, p)
        p += 4
        entries.append(Entry(raw[:-1].decode("ascii"), offset, 0))
    if entries[0].offset != p:
        return None
    for i, e in enumerate(entries):
        end = entries[i + 1].offset if i + 1 < len(entries) else len(data)
        if end < e.offset or end > len(data):
            return None
        e.size = end - e.offset
    return entries


def is_container(data):
    return parse_directory(data) is not None


def read(data, entry):
    return data[entry.offset:entry.offset + entry.size]


def files(data):
    """Iterate (entry, bytes) pairs."""
    for e in parse_directory(data) or []:
        yield e, read(data, e)


def build(items):
    """Inverse of parse_directory: items is a list of (name, bytes)."""
    header_len = 1 + sum(2 + len(n) + 1 + 4 for n, _ in items)
    out = bytearray([len(items)])
    offset = header_len
    for name, blob in items:
        raw = name.encode("ascii") + b"\0"
        out += struct.pack("<H", len(raw)) + raw + struct.pack("<I", offset)
        offset += len(blob)
    assert len(out) == header_len
    for _, blob in items:
        out += blob
    return bytes(out)
