#!/usr/bin/env python3
"""Generate small, deterministic DNG files for the FreeImage RAW test suite.

The files are plain uncompressed DNGs: a preview image in IFD0 and an
uncompressed CFA (Bayer) image in a SubIFD, which is the layout LibRaw takes
apart with its TIFF parser.  Everything is synthesised from a fixed formula,
so running this again reproduces the same bytes.
"""
import os
import struct
import sys

BYTE, ASCII, SHORT, LONG, RATIONAL = 1, 2, 3, 4, 5
UNDEFINED = 7
SRATIONAL = 10

TYPESIZE = {BYTE: 1, ASCII: 1, SHORT: 2, LONG: 4, RATIONAL: 8,
            UNDEFINED: 1, SRATIONAL: 8}


def pack(typ, values):
    if typ == ASCII:
        return values.encode("ascii") + b"\0"
    if typ in (BYTE, UNDEFINED):
        return bytes(values)
    if typ == SHORT:
        return b"".join(struct.pack("<H", v) for v in values)
    if typ == LONG:
        return b"".join(struct.pack("<I", v) for v in values)
    if typ == RATIONAL:
        return b"".join(struct.pack("<II", n, d) for n, d in values)
    if typ == SRATIONAL:
        return b"".join(struct.pack("<ii", n, d) for n, d in values)
    raise ValueError(typ)


def count_of(typ, values, raw):
    if typ in (ASCII, UNDEFINED):
        return len(raw)
    return len(raw) // TYPESIZE[typ]


class Ifd(object):
    """One IFD.  Values longer than four bytes spill into an overflow area
    that directly follows the entry table."""

    def __init__(self):
        self.tags = {}

    def set(self, tag, typ, values):
        raw = pack(typ, values)
        self.tags[tag] = (typ, count_of(typ, values, raw), raw)

    def size(self):
        n = len(self.tags)
        size = 2 + 12 * n + 4
        for _, _, raw in self.tags.values():
            if len(raw) > 4:
                size += len(raw) + (len(raw) & 1)
        return size

    def emit(self, base, patches):
        """patches maps tag -> absolute offset to store as a LONG value."""
        tags = sorted(self.tags)
        out = bytearray(struct.pack("<H", len(tags)))
        overflow = bytearray()
        over_base = base + 2 + 12 * len(tags) + 4
        for tag in tags:
            typ, count, raw = self.tags[tag]
            if tag in patches:
                raw = struct.pack("<I", patches[tag])
            if len(raw) <= 4:
                out += struct.pack("<HHI", tag, typ, count) + raw.ljust(4, b"\0")
            else:
                out += struct.pack("<HHI", tag, typ, count)
                out += struct.pack("<I", over_base + len(overflow))
                overflow += raw
                if len(raw) & 1:
                    overflow += b"\0"
        out += struct.pack("<I", 0)
        out += overflow
        assert len(out) == self.size()
        return bytes(out)


# --- the colour matrix ------------------------------------------------------
# A plausible sRGB-ish ColorMatrix1; LibRaw wants one to build its camera to
# XYZ transform.  The numbers only have to be invertible, not real.
COLOR_MATRIX = [
    (6220, 10000), (-1341, 10000), (-609, 10000),
    (-4112, 10000), (11932, 10000), (2288, 10000),
    (-741, 10000), (1631, 10000), (4880, 10000),
]


def bayer(width, height, pattern):
    """A deterministic CFA field: a smooth gradient plus a per-channel offset
    and a fine checker, so demosaicing has something to chew on and a dropped
    or shifted row is visible in the checksum."""
    buf = bytearray()
    for y in range(height):
        row = bytearray()
        for x in range(width):
            c = pattern[(y & 1) * 2 + (x & 1)]
            v = (x * 397 + y * 211) % 4096
            v += c * 7000
            v += 800 if ((x >> 3) + (y >> 3)) & 1 else 0
            v = min(v, 65535)
            row += struct.pack("<H", v)
        buf += row
    return bytes(buf)


def icc_profile():
    """A small but well-formed ICC v2 profile, so the plugin has something to
    hand to FreeImage_CreateICCProfile and LibRaw's profile reader has a real
    length to check against the file size."""
    def tag(sig, body):
        return sig, body

    desc = b"desc" + b"\0" * 4
    text = b"FreeImage synthetic RAW profile\0"
    desc += struct.pack(">I", len(text)) + text
    desc += b"\0" * 12                      # unicode language/count
    desc += b"\0" * 67                      # scriptcode
    desc = desc.ljust(len(desc) + (-len(desc) % 4), b"\0")

    def xyz(x, y, z):
        return b"XYZ " + b"\0" * 4 + struct.pack(">iii",
                                                 int(x * 65536), int(y * 65536),
                                                 int(z * 65536))

    def curv():
        return b"curv" + b"\0" * 4 + struct.pack(">I", 1) + struct.pack(">H", 2 << 8)

    cprt = b"text" + b"\0" * 4 + b"Public Domain\0"

    tags = [
        (b"desc", desc),
        (b"wtpt", xyz(0.9642, 1.0, 0.8249)),
        (b"rXYZ", xyz(0.4360, 0.2225, 0.0139)),
        (b"gXYZ", xyz(0.3851, 0.7169, 0.0971)),
        (b"bXYZ", xyz(0.1431, 0.0606, 0.7141)),
        (b"rTRC", curv()),
        (b"gTRC", curv()),
        (b"bTRC", curv()),
        (b"cprt", cprt),
    ]

    table = struct.pack(">I", len(tags))
    offset = 128 + 4 + 12 * len(tags)
    body = b""
    for sig, data in tags:
        pad = -len(data) % 4
        table += struct.pack(">4sII", sig, offset + len(body), len(data))
        body += data + b"\0" * pad

    size = 128 + len(table) + len(body)
    header = struct.pack(">I", size)
    header += b"FIMG"                        # preferred CMM
    header += struct.pack(">I", 0x02100000)  # version 2.1
    header += b"mntr" + b"RGB " + b"XYZ "
    header += b"\0" * 12                     # date/time
    header += b"acsp" + b"APPL"
    header += struct.pack(">I", 0)           # flags
    header += b"\0" * 8                      # device manufacturer/model
    header += b"\0" * 8                      # device attributes
    header += struct.pack(">I", 0)           # rendering intent: perceptual
    header += struct.pack(">iii", int(0.9642 * 65536), 65536, int(0.8249 * 65536))
    header += b"\0" * 4                      # profile creator
    header += b"\0" * 44                     # profile id + reserved
    assert len(header) == 128, len(header)
    return header + table + body


def preview_rgb(width, height):
    """An uncompressed 8-bit RGB preview."""
    buf = bytearray()
    for y in range(height):
        for x in range(width):
            buf.append((x * 255) // max(width - 1, 1))
            buf.append((y * 255) // max(height - 1, 1))
            buf.append(((x + y) * 255) // max(width + height - 2, 1))
    return bytes(buf)


def make_dng(raw_w, raw_h, pattern, cfa_bytes, model,
             prev_w=0, prev_h=0, crop=(0, 0, 0, 0), icc=False):
    """Assemble a DNG.  crop is (left, top, right, bottom): the margins the
    ActiveArea tag hides, so the post-processed image comes out smaller than
    the CFA field and a mishandled margin is visible."""
    left, top, right, bottom = crop
    act_w = raw_w - left - right
    act_h = raw_h - top - bottom

    ifd0 = Ifd()
    ifd0.set(254, LONG, [1])                    # NewSubfileType: reduced res
    ifd0.set(256, LONG, [prev_w])               # ImageWidth
    ifd0.set(257, LONG, [prev_h])               # ImageLength
    ifd0.set(258, SHORT, [8, 8, 8])             # BitsPerSample
    ifd0.set(259, SHORT, [1])                   # Compression: none
    ifd0.set(262, SHORT, [2])                   # Photometric: RGB
    ifd0.set(271, ASCII, "FreeImage")           # Make
    ifd0.set(272, ASCII, model)                 # Model
    ifd0.set(273, LONG, [0])                    # StripOffsets (patched)
    ifd0.set(277, SHORT, [3])                   # SamplesPerPixel
    ifd0.set(278, LONG, [prev_h])               # RowsPerStrip
    ifd0.set(279, LONG, [prev_w * prev_h * 3])  # StripByteCounts
    ifd0.set(284, SHORT, [1])                   # PlanarConfiguration
    ifd0.set(330, LONG, [0])                    # SubIFDs (patched)
    ifd0.set(50706, BYTE, [1, 4, 0, 0])         # DNGVersion 1.4
    ifd0.set(50707, BYTE, [1, 1, 0, 0])         # DNGBackwardVersion 1.1
    ifd0.set(50708, ASCII, "FreeImage " + model)  # UniqueCameraModel
    ifd0.set(50721, SRATIONAL, COLOR_MATRIX)    # ColorMatrix1
    ifd0.set(50778, SHORT, [21])                # CalibrationIlluminant1: D65
    if icc:
        ifd0.set(34675, UNDEFINED, icc_profile())   # InterColorProfile

    sub = Ifd()
    sub.set(254, LONG, [0])                     # NewSubfileType: full res
    sub.set(256, LONG, [raw_w])
    sub.set(257, LONG, [raw_h])
    sub.set(258, SHORT, [16])                   # BitsPerSample
    sub.set(259, SHORT, [1])                    # Compression: none
    sub.set(262, SHORT, [32803])                # Photometric: CFA
    sub.set(273, LONG, [0])                     # StripOffsets (patched)
    sub.set(277, SHORT, [1])                    # SamplesPerPixel
    sub.set(278, LONG, [raw_h])                 # RowsPerStrip
    sub.set(279, LONG, [len(cfa_bytes)])        # StripByteCounts
    sub.set(284, SHORT, [1])                    # PlanarConfiguration
    sub.set(33421, SHORT, [2, 2])               # CFARepeatPatternDim
    sub.set(33422, BYTE, pattern)               # CFAPattern
    sub.set(50711, SHORT, [1])                  # CFALayout: rectangular
    sub.set(50713, SHORT, [1, 1])               # BlackLevelRepeatDim
    sub.set(50714, LONG, [0])                   # BlackLevel
    sub.set(50717, LONG, [65535])               # WhiteLevel
    if left or top or right or bottom:
        # ActiveArea is what dcraw/LibRaw turn into top_margin/left_margin,
        # so this is the tag that makes the output frame differ from the CFA.
        sub.set(50829, LONG, [top, left, raw_h - bottom, raw_w - right])
        sub.set(50719, LONG, [0, 0])            # DefaultCropOrigin
        sub.set(50720, LONG, [act_w, act_h])    # DefaultCropSize

    header = struct.pack("<2sHI", b"II", 42, 8)
    ifd0_off = 8
    sub_off = ifd0_off + ifd0.size()
    prev_off = sub_off + sub.size()
    prev = preview_rgb(prev_w, prev_h) if prev_w and prev_h else b""
    raw_off = prev_off + len(prev) + (len(prev) & 1)

    body = ifd0.emit(ifd0_off, {273: prev_off, 330: sub_off})
    body += sub.emit(sub_off, {273: raw_off})
    body += prev
    if len(prev) & 1:
        body += b"\0"
    body += cfa_bytes
    return header + body


RGGB = [0, 1, 1, 2]
BGGR = [2, 1, 1, 0]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "data"
    if not os.path.isdir(out):
        os.makedirs(out)

    files = []

    # The workhorse: 96x64 RGGB with an uncompressed RGB preview, an embedded
    # ICC profile and a four-pixel active-area margin, so the post-processed
    # size differs from the CFA size and a lost margin shows up.
    w, h = 96, 64
    files.append(("fi_raw_rggb.dng",
                  make_dng(w, h, RGGB, bayer(w, h, RGGB), "Synth RGGB",
                           prev_w=48, prev_h=32, crop=(4, 4, 4, 4), icc=True)))

    # The same image with the other common Bayer phase, to catch a decoder
    # that hard-codes one.
    files.append(("fi_raw_bggr.dng",
                  make_dng(w, h, BGGR, bayer(w, h, BGGR), "Synth BGGR",
                           prev_w=48, prev_h=32, crop=(4, 4, 4, 4))))

    # No preview at all: the RAW_PREVIEW path has to fall back to decoding.
    files.append(("fi_raw_nopreview.dng",
                  make_dng(w, h, RGGB, bayer(w, h, RGGB), "Synth NoPrev")))

    # Odd, uncropped dimensions, to catch off-by-one in the row loops.
    w2, h2 = 70, 46
    files.append(("fi_raw_odd.dng",
                  make_dng(w2, h2, RGGB, bayer(w2, h2, RGGB), "Synth Odd")))

    for name, data in files:
        path = os.path.join(out, name)
        with open(path, "wb") as f:
            f.write(data)
        print("%-24s %7d bytes" % (name, len(data)))


if __name__ == "__main__":
    main()
