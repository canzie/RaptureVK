"""Cursor over the decompressed Tracy payload.

All values are little-endian. Struct objects are precompiled because the zone
timeline sections read tens of millions of fields.
"""

import struct

_u8 = struct.Struct("<B")
_u16 = struct.Struct("<H")
_i16 = struct.Struct("<h")
_u32 = struct.Struct("<I")
_i32 = struct.Struct("<i")
_u64 = struct.Struct("<Q")
_i64 = struct.Struct("<q")
_f32 = struct.Struct("<f")
_f64 = struct.Struct("<d")


class Reader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def eof(self):
        return self.pos >= len(self.data)

    def skip(self, n):
        self.pos += n

    def bytes(self, n):
        start = self.pos
        self.pos = start + n
        return self.data[start:start + n]

    def u8(self):
        v = _u8.unpack_from(self.data, self.pos)[0]
        self.pos += 1
        return v

    def u16(self):
        v = _u16.unpack_from(self.data, self.pos)[0]
        self.pos += 2
        return v

    def i16(self):
        v = _i16.unpack_from(self.data, self.pos)[0]
        self.pos += 2
        return v

    def u32(self):
        v = _u32.unpack_from(self.data, self.pos)[0]
        self.pos += 4
        return v

    def i32(self):
        v = _i32.unpack_from(self.data, self.pos)[0]
        self.pos += 4
        return v

    def u64(self):
        v = _u64.unpack_from(self.data, self.pos)[0]
        self.pos += 8
        return v

    def i64(self):
        v = _i64.unpack_from(self.data, self.pos)[0]
        self.pos += 8
        return v

    def f32(self):
        v = _f32.unpack_from(self.data, self.pos)[0]
        self.pos += 4
        return v

    def f64(self):
        v = _f64.unpack_from(self.data, self.pos)[0]
        self.pos += 8
        return v

    def unpack(self, fmt):
        """Read one struct.Struct, advancing by its size."""
        v = fmt.unpack_from(self.data, self.pos)
        self.pos += fmt.size
        return v

    def u64Array(self, count):
        if count == 0:
            return []
        v = struct.unpack_from("<%dQ" % count, self.data, self.pos)
        self.pos += 8 * count
        return v

    def lengthPrefixedString(self):
        """Read a u64 length followed by that many bytes."""
        n = self.u64()
        return self.bytes(n).decode("utf-8", "replace")
