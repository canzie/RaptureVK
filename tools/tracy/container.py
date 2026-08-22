"""Tracy .tracy container format: block framing and decompression.

Layout (Tracy 0.13.x, see server/TracyFileRead.hpp):
    magic "tr" 0xFD 'P' | type u8 | streamCount u8
    then repeating: blockSize u32, followed by blockSize compressed bytes.

Block i is fed to stream (i % streamCount). Each stream is one continuous
compressed frame flushed at block boundaries, so decompressor state carries
across that stream's blocks. The logical payload is the concatenation of
decompressed blocks in file order.
"""

import struct
from compression import zstd

MAGIC = b"tr\xfdP"
LZ4_MAGIC = b"tlZ4"
ZSTD_MAGIC = b"tZst"

TYPE_LZ4 = 0
TYPE_ZSTD = 1


class UnsupportedContainer(Exception):
    pass


def decompressPayload(path):
    """Read a .tracy file and return its decompressed payload as bytes."""
    with open(path, "rb") as f:
        raw = f.read()

    magic = raw[:4]
    if magic == MAGIC:
        streamType = raw[4]
        streamCount = raw[5]
        offset = 6
    elif magic == ZSTD_MAGIC:
        streamType = TYPE_ZSTD
        streamCount = 1
        offset = 4
    elif magic == LZ4_MAGIC:
        streamType = TYPE_LZ4
        streamCount = 1
        offset = 4
    else:
        raise UnsupportedContainer("not a tracy dump (bad magic)")

    if streamType != TYPE_ZSTD:
        raise UnsupportedContainer("lz4 traces are not supported, only zstd")

    decompressors = [zstd.ZstdDecompressor() for _ in range(streamCount)]
    chunks = []
    streamId = 0
    total = len(raw)

    while offset < total:
        (blockSize,) = struct.unpack_from("<I", raw, offset)
        offset += 4
        block = raw[offset:offset + blockSize]
        offset += blockSize
        chunks.append(decompressors[streamId].decompress(block))
        streamId = (streamId + 1) % streamCount

    return b"".join(chunks)
