"""Give a MinimizedImageBox ('mini') HEIF file an Exif block, the way the files that carry
one store it: the Exif payload from its TIFF header on, without the four bytes of
exif_tiff_header_offset that an Exif *item* of a regular HEIF file starts with (ISO/IEC
23008-12 A.2.1). A 'mini' box holds its metadata inline, so nothing outside it moves and
no offset anywhere needs fixing. Usage:

    miniexif.py src dst

The box is a bit-packed header followed by the chunks, so adding the block means setting
exif_flag, inserting the three fields that flag brings with it (large_metadata_flag, which
is present only when the box carries metadata at all, then exif_xmp_compressed_flag and
exif_data_size), re-padding to the byte boundary and appending the Exif bytes after the
main item data - the order the chunks are stored in. The field layout is ISO/IEC 23008-12
Annex O, as parsed by Source/LibHEIF/libheif/mini.cc.
"""
import struct
import sys

# a small Exif block: a TIFF header and one IFD with six tags, ASCII values after the IFD
TAGS = [
    (0x010E, 2, b"A 'mini' HEIF with an Exif block\0"),   # ImageDescription
    (0x010F, 2, b"FreeImage\0"),                          # Make
    (0x0110, 2, b"MinimizedImageBox\0"),                  # Model
    (0x0112, 3, 1),                                       # Orientation, SHORT: 1 = as stored
    (0x0131, 2, b"TestAPI/HEIF/data/miniexif.py\0"),      # Software
    (0x0132, 2, b"2026:09:20 00:00:00\0"),                # DateTime
]


def exif_block():
    """The Exif block, little-endian, starting at its TIFF header"""
    entries = b""
    values = b""
    # TIFF header (8) + entry count (2) + entries (12 each) + next-IFD offset (4)
    values_at = 8 + 2 + 12 * len(TAGS) + 4
    for tag, typ, value in TAGS:
        if typ == 3:
            entries += struct.pack("<HHIHH", tag, typ, 1, value, 0)
        else:
            entries += struct.pack("<HHI", tag, typ, len(value))
            if len(value) <= 4:
                entries += value.ljust(4, b"\0")
            else:
                entries += struct.pack("<I", values_at + len(values))
                values += value
    return b"II*\0" + struct.pack("<I", 8) + struct.pack("<H", len(TAGS)) + entries + struct.pack("<I", 0) + values


class BitReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0     # in bits

    def bit(self):
        byte = self.data[self.pos >> 3]
        value = (byte >> (7 - (self.pos & 7))) & 1
        self.pos += 1
        return value

    def bits(self, n):
        value = 0
        for _ in range(n):
            value = (value << 1) | self.bit()
        return value


def bits_of(data, start, end):
    """the bits of 'data' in [start, end) as a list"""
    return [(data[i >> 3] >> (7 - (i & 7))) & 1 for i in range(start, end)]


def pack_bits(bits):
    """a bit list, padded with zeros to whole bytes"""
    out = bytearray((len(bits) + 7) // 8)
    for i, bit in enumerate(bits):
        if bit:
            out[i >> 3] |= 1 << (7 - (i & 7))
    return bytes(out)


def boxes(data, start, end):
    pos = start
    while pos + 8 <= end:
        size, typ = struct.unpack(">I4s", data[pos:pos + 8])
        hdr = 8
        if size == 1:
            size = struct.unpack(">Q", data[pos + 8:pos + 16])[0]
            hdr = 16
        elif size == 0:
            size = end - pos
        if size < hdr or pos + size > end:
            raise SystemExit("malformed box %r at %d" % (typ, pos))
        yield pos, hdr, size, typ
        pos += size


def add_exif(payload, exif):
    """the 'mini' box payload with the Exif block added"""
    bits = BitReader(payload)
    bits.bits(2)                                # version
    explicit_codec_types_flag = bits.bit()
    float_flag = bits.bit()
    bits.bit()                                  # full_range_flag
    alpha_flag = bits.bit()
    explicit_cicp_flag = bits.bit()
    hdr_flag = bits.bit()
    icc_flag = bits.bit()
    exif_flag = bits.bit()
    xmp_flag = bits.bit()
    chroma_subsampling = bits.bits(2)
    bits.bits(3)                                # orientation
    if hdr_flag or icc_flag or exif_flag or xmp_flag:
        raise SystemExit("this file already carries metadata or HDR fields: not handled")

    large_dimensions_flag = bits.bit()
    bits.bits(15 if large_dimensions_flag else 7)   # width
    bits.bits(15 if large_dimensions_flag else 7)   # height
    if chroma_subsampling in (1, 2):
        bits.bit()                              # chroma_is_horizontally_centered
    if chroma_subsampling == 1:
        bits.bit()                              # chroma_is_vertically_centered
    if float_flag:
        bits.bits(2)                            # bit_depth_log2
    elif bits.bit():                            # high_bit_depth_flag
        bits.bits(3)                            # bit_depth
    if alpha_flag:
        bits.bit()                              # alpha_is_premultiplied
    if explicit_cicp_flag:
        bits.bits(24)                           # colour_primaries, transfer_characteristics, matrix_coefficients
    if explicit_codec_types_flag:
        bits.bits(64)                           # infe_type, codec_config_type

    # the chunk sizes: large_metadata_flag is only present when the box carries metadata,
    # so it is one of the two fields this rewrite inserts
    chunk_sizes_at = bits.pos
    large_codec_config_flag = bits.bit()
    large_item_data_flag = bits.bit()
    bits.bits(12 if large_codec_config_flag else 3)     # main_item_codec_config_size
    bits.bits(28 if large_item_data_flag else 15)       # main_item_data_size - 1
    if alpha_flag:
        alpha_item_data_size = bits.bits(28 if large_item_data_flag else 15)
        if alpha_item_data_size > 0:
            bits.bits(12 if large_codec_config_flag else 3)  # alpha_item_codec_config_size
    sizes_end = bits.pos

    if len(exif) > 1024:
        # exif_data_size is 10 bits without large_metadata_flag, which nothing here needs
        raise SystemExit("the Exif block must be at most 1024 bytes")

    head = bits_of(payload, 0, chunk_sizes_at)
    head[9] = 1                                         # exif_flag
    head += [0]                                         # large_metadata_flag: sizes stay 10-bit
    head += bits_of(payload, chunk_sizes_at, sizes_end)
    head += [0]                                         # exif_xmp_compressed_flag
    head += [(len(exif) - 1) >> i & 1 for i in range(9, -1, -1)]     # exif_data_size - 1, 10 bits
    # the chunks follow the header on a byte boundary, and the Exif block comes after the
    # main item data, which is the last one this file has
    return pack_bits(head) + payload[(sizes_end + 7) // 8:] + exif


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    data = open(sys.argv[1], "rb").read()
    out = b""
    seen = False
    for pos, hdr, size, typ in boxes(data, 0, len(data)):
        if typ == b"mini":
            payload = add_exif(data[pos + hdr:pos + size], exif_block())
            out += struct.pack(">I4s", 8 + len(payload), b"mini") + payload
            seen = True
        else:
            out += data[pos:pos + size]
    if not seen:
        raise SystemExit("no 'mini' box: %s is not a 'mif3' file" % sys.argv[1])
    open(sys.argv[2], "wb").write(out)


main()
