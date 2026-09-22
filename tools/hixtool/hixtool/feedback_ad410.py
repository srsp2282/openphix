"""AD410 Feedback.bin outer decoder.

The tested ANCEL AD410 uses the same position/rotation schedule as the
Openphix image cipher, but a different fixed 256-byte XOR table for
Feedback payloads.

The table below is the canonical post-rotation key recovered from repeated
controlled captures.  Decoding is:

    j = offset % 4864
    v = (j & 0xff) + (j >> 8)
    k = v & 0xff
    r = (v >> 3) & 7

    plain = rotr8(ciphertext, r) ^ FEEDBACK_POST_ROTATION_KEY[k]

`offset` is the original byte offset inside the Feedback area.  It must not
be reset at stored-record or logical-event boundaries.

Validated key SHA-256:
    b64f5505aa9f324b35763784fa66f33ce9b048afe13640706c1638e39b7571c4
"""

import hashlib

from . import cipher


PROFILE_NAME = "ad410"

FEEDBACK_POST_ROTATION_KEY = bytes.fromhex(
    """
    6b 97 99 b5 e4 ff 2b dd 14 be 43 d9 96 14 ef 0e
    75 34 98 36 ed 43 3d 0d 55 84 57 69 7f 88 ff 90
    f7 a4 5f 9d 81 27 af 5a 81 c7 35 db 05 66 14 48
    3c 0e 76 54 68 53 8b ab e2 ae 66 95 f0 56 33 1e
    8b 9b 66 c1 6a 6e d9 e7 be 62 71 01 08 ff 66 fa
    2b 72 b6 4e 4f 20 9f f8 5e 0a de 86 14 0a b4 c4
    65 bd ee 62 de 13 e7 c5 0a cf 36 8b de 1e 26 64
    81 a1 97 64 e0 ec b8 36 09 d9 00 7a 2c e4 c2 c3
    8d a5 10 67 8c d0 60 48 b3 3c b4 c4 1b b3 7f 0f
    ea e7 42 ee a1 8f 21 0a 61 5b dc b8 fa e8 71 8f
    e5 8f 80 90 a6 c2 c8 5b 9e 0b 93 41 59 d9 69 39
    c6 c6 53 b5 61 10 5b 25 b4 74 60 c7 02 31 72 f5
    d4 b3 41 c5 9c 22 e4 4f e9 bd cb b2 bb 96 91 ab
    58 7f d4 28 1d 9f 6a c1 86 0f 5c 6a 4d b1 d0 43
    9a 51 94 47 ad 30 f6 63 d2 91 9c 57 80 29 36 a5
    e2 52 08 88 13 7d 8f 1d 17 6c 12 e1 1b a6 cb ba
    """
)

FEEDBACK_POST_ROTATION_KEY_SHA256 = (
    "b64f5505aa9f324b35763784fa66f33ce9b048afe13640706c1638e39b7571c4"
)

assert len(FEEDBACK_POST_ROTATION_KEY) == 256
assert (
    hashlib.sha256(FEEDBACK_POST_ROTATION_KEY).hexdigest()
    == FEEDBACK_POST_ROTATION_KEY_SHA256
)


def _rotr8(value, rotation):
    """Rotate one byte right by 0..7 bits."""
    if not rotation:
        return value
    return (
        (value >> rotation)
        | (value << (8 - rotation))
    ) & 0xff


def _state(offset):
    """Return (key_index, rotation) for a Feedback-area-relative offset."""
    j = offset % cipher.PERIOD
    v = (j & 0xff) + (j >> 8)
    return v & 0xff, (v >> 3) & 7


def decrypt(data, offset=0):
    """Decode AD410 Feedback payload bytes starting at Feedback offset."""
    out = bytearray(len(data))

    for i, value in enumerate(data):
        key_index, rotation = _state(offset + i)
        out[i] = (
            _rotr8(value, rotation)
            ^ FEEDBACK_POST_ROTATION_KEY[key_index]
        )

    return bytes(out)
