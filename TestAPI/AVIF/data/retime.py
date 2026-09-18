"""Rewrite an AVIF image sequence so that its frames no longer last the same time, by
replacing the single stts entry with one entry per sample and scaling the media timescale.
The new timescale is chosen so that the whole track lasts exactly as long as it did, which
is what keeps tkhd, mvhd and elst - all of them in the movie timescale - correct without
touching them. Usage:

    retime.py src dst timescale delta [delta ...]

where the deltas are the per-sample durations in the new timescale, and their sum, divided
by the new timescale, must equal the original duration. The stts grows, so every absolute
offset into mdat (the chunk offsets of every track, and the item offsets in meta) is moved
by the same amount; files whose data lives before the moov, or in an idat box, need none of
that and are left alone by it either way."""
import struct
import sys

CONTAINERS = {b"moov", b"trak", b"mdia", b"minf", b"stbl", b"edts", b"dinf"}


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


def box(typ, payload):
    return struct.pack(">I4s", 8 + len(payload), typ) + payload


def retime_mdhd(payload, timescale, duration):
    """mdhd: version, flags, creation, modification, timescale, duration, language, quality"""
    version = payload[0]
    if version == 1:
        head, tail = payload[:20], payload[32:]
        return head + struct.pack(">IQ", timescale, duration) + tail
    head, tail = payload[:12], payload[20:]
    return head + struct.pack(">II", timescale, duration) + tail


def retime_stts(payload, deltas):
    """stts: version, flags, entry_count, then (sample_count, sample_delta) pairs"""
    count = struct.unpack(">I", payload[4:8])[0]
    samples = 0
    for i in range(count):
        samples += struct.unpack(">I", payload[8 + i * 8:12 + i * 8])[0]
    if samples != len(deltas):
        raise SystemExit("the track has %d samples, %d durations given" % (samples, len(deltas)))
    out = payload[:4] + struct.pack(">I", len(deltas))
    for d in deltas:
        out += struct.pack(">II", 1, d)
    return out


def rebuild(data, start, end, timescale, deltas):
    out = b""
    for pos, hdr, size, typ in boxes(data, start, end):
        payload = data[pos + hdr:pos + size]
        if typ in CONTAINERS:
            out += box(typ, rebuild(data, pos + hdr, pos + size, timescale, deltas))
        elif typ == b"mdhd":
            out += box(typ, retime_mdhd(payload, timescale, sum(deltas)))
        elif typ == b"stts":
            out += box(typ, retime_stts(payload, deltas))
        else:
            out += data[pos:pos + size]
    return out


def shift_stco(data, start, end, shift, floor):
    """add 'shift' to every chunk offset at or past 'floor', in place"""
    for pos, hdr, size, typ in boxes(data, start, end):
        if typ in CONTAINERS:
            shift_stco(data, pos + hdr, pos + size, shift, floor)
        elif typ in (b"stco", b"co64"):
            wide = typ == b"co64"
            count = struct.unpack(">I", data[pos + hdr + 4:pos + hdr + 8])[0]
            for i in range(count):
                at = pos + hdr + 8 + i * (8 if wide else 4)
                fmt = ">Q" if wide else ">I"
                off = struct.unpack(fmt, data[at:at + (8 if wide else 4)])[0]
                if off >= floor:
                    data[at:at + (8 if wide else 4)] = struct.pack(fmt, off + shift)


def shift_iloc(data, pos, hdr, size, shift, floor):
    """add 'shift' to the base and extent offsets of every item at or past 'floor'"""
    version = data[pos + hdr]
    at = pos + hdr + 4
    offset_size, length_size = data[at] >> 4, data[at] & 15
    base_size, index_size = data[at + 1] >> 4, data[at + 1] & 15
    at += 2
    if version < 2:
        count = struct.unpack(">H", data[at:at + 2])[0]
        at += 2
    else:
        count = struct.unpack(">I", data[at:at + 4])[0]
        at += 4

    def bump(at, width):
        if not width:
            return at
        fmt = {4: ">I", 8: ">Q"}[width]
        value = struct.unpack(fmt, data[at:at + width])[0]
        if value >= floor:
            data[at:at + width] = struct.pack(fmt, value + shift)
        return at + width

    for _ in range(count):
        at += 2 if version < 2 else 4          # item_ID
        if version >= 1:
            at += 2                            # construction_method
        at += 2                                # data_reference_index
        at = bump(at, base_size)
        extents = struct.unpack(">H", data[at:at + 2])[0]
        at += 2
        for _ in range(extents):
            if version >= 1 and index_size:
                at += index_size
            at = bump(at, offset_size)
            at += length_size


src, dst, timescale = sys.argv[1], sys.argv[2], int(sys.argv[3])
deltas = [int(x) for x in sys.argv[4:]]
with open(src, "rb") as f:
    original = f.read()

top = list(boxes(original, 0, len(original)))
moov = [b for b in top if b[3] == b"moov"]
mdat = [b for b in top if b[3] == b"mdat"]
if len(moov) != 1:
    raise SystemExit("expected exactly one moov box")
pos, hdr, size, _ = moov[0]
new_moov = box(b"moov", rebuild(original, pos + hdr, pos + size, timescale, deltas))
shift = len(new_moov) - size

out = bytearray(original[:pos] + new_moov + original[pos + size:])
if shift and mdat and mdat[0][0] > pos:
    floor = mdat[0][0]
    for bpos, bhdr, bsize, btyp in boxes(out, 0, len(out)):
        if btyp == b"moov":
            shift_stco(out, bpos + bhdr, bpos + bsize, shift, floor)
        elif btyp == b"meta":
            for ipos, ihdr, isize, ityp in boxes(out, bpos + bhdr + 4, bpos + bsize):
                if ityp == b"iloc":
                    shift_iloc(out, ipos, ihdr, isize, shift, floor)

with open(dst, "wb") as f:
    f.write(bytes(out))
print("%s -> %s: timescale %d, per-frame ms %s, total %.4f ms (moov grew by %d)"
      % (src, dst, timescale, [round(d * 1000 / timescale, 4) for d in deltas],
         sum(deltas) * 1000 / timescale, shift))
