"""The indexed table layout shared by Exp.BIN, Cmd.BIN, the string tables
and (with a different record type) several other files:

    u32   count
    count x { u32 id, u32 offset }     sorted by id, offsets ascending
    records back to back from 4 + 8 * count

A record ends where the next one starts; the index offsets are the only
reliable delimiters (the per-record length bytes in Cmd.BIN are wrong in
about 170 records).
"""
import struct


def parse_index(data):
    (count,) = struct.unpack_from("<I", data, 0)
    end = 4 + 8 * count
    if end > len(data):
        raise ValueError("index larger than file")
    ents = [struct.unpack_from("<II", data, 4 + 8 * k) for k in range(count)]
    if ents and ents[0][1] != end:
        raise ValueError("first record does not follow the index")
    return ents


def records(data):
    """Yield (id, start, end)."""
    ents = parse_index(data)
    for k, (rid, off) in enumerate(ents):
        end = ents[k + 1][1] if k + 1 < len(ents) else len(data)
        yield rid, off, end


def find(data, rid):
    """Binary search; returns (start, end) or None."""
    ents = parse_index(data)
    lo, hi = 0, len(ents)
    while lo < hi:
        mid = (lo + hi) // 2
        if ents[mid][0] == rid:
            end = ents[mid + 1][1] if mid + 1 < len(ents) else len(data)
            return ents[mid][1], end
        if ents[mid][0] < rid:
            lo = mid + 1
        else:
            hi = mid
    return None
