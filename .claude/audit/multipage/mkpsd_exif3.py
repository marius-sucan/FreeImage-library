#!/usr/bin/env python3
"""Build a PSD carrying an EXIF *3* image resource and no EXIF 1, for N16.

PSDParser.cpp reads resource 1058 (PSDP_RES_EXIF1) and 1059 (PSDP_RES_EXIF3). When a
file has 1059 but not 1058 it reaches

    } else if(NULL != _exif3._Data) {
        // I have not found any files with this resource.
        // Assume that we only want one Exif resource.
        assert(false);
        psd_read_exif_profile(...);

The assert is not guarding anything - the two lines under it do the work - but no GNU
makefile except Makefile.mingw defines NDEBUG, so in a release build on Linux, macOS,
Solaris, Cygwin and iPhone this aborts the process on a file it can read perfectly
well.

    python3 mkpsd_exif3.py base.psd exif3.psd
"""
import struct
import sys


def be16(v):
    return struct.pack(">H", v)


def be32(v):
    return struct.pack(">I", v)


def read_psd_sections(data):
    """Return (header_end, resources_start, resources_len, rest_start)."""
    if data[:4] != b"8BPS":
        raise SystemExit("not a PSD: bad signature")
    off = 26                                   # header is a fixed 26 bytes
    cmd_len = struct.unpack(">I", data[off:off + 4])[0]
    off += 4 + cmd_len                         # colour mode data
    res_len_at = off
    res_len = struct.unpack(">I", data[off:off + 4])[0]
    res_start = off + 4
    rest = res_start + res_len
    return res_len_at, res_start, res_len, rest


def make_resource(res_id, payload):
    """One '8BIM' image resource block, with an empty Pascal name."""
    block = b"8BIM" + be16(res_id) + b"\x00\x00" + be32(len(payload)) + payload
    if len(payload) % 2:
        block += b"\x00"                       # data is padded to an even length
    return block


def minimal_exif():
    """A tiny but well-formed little-endian TIFF/Exif block: 0 IFD entries."""
    return b"II" + struct.pack("<H", 42) + struct.pack("<I", 8) + struct.pack("<H", 0) + struct.pack("<I", 0)


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]

    with open(src, "rb") as f:
        data = f.read()

    res_len_at, res_start, res_len, rest = read_psd_sections(data)
    resources = data[res_start:res_start + res_len]

    # drop any EXIF1 (1058) the writer may have put there, so the parser has to fall
    # through to the EXIF3 branch
    kept, i = b"", 0
    while i + 12 <= len(resources):
        if resources[i:i + 4] != b"8BIM":
            kept += resources[i:]
            break
        rid = struct.unpack(">H", resources[i + 4:i + 6])[0]
        j = i + 6
        name_len = resources[j]
        name_field = 1 + name_len
        if name_field % 2:
            name_field += 1
        j += name_field
        size = struct.unpack(">I", resources[j:j + 4])[0]
        j += 4
        end = j + size + (size % 2)
        if rid != 1058:
            kept += resources[i:end]
        i = end

    kept += make_resource(1059, minimal_exif())      # PSDP_RES_EXIF3

    out = data[:res_len_at] + be32(len(kept)) + kept + data[rest:]
    with open(dst, "wb") as f:
        f.write(out)

    print("wrote %s: resources %d -> %d bytes, one EXIF3 (1059) block, no EXIF1 (1058)"
          % (dst, res_len, len(kept)))


if __name__ == "__main__":
    main()
