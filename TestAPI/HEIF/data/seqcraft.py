#!/usr/bin/env python3
"""Derive the crafted image-sequence test files from the generated ones (see mkseqdata.sh).

Each derivation rewrites boxes and recomputes the sizes of the boxes around them; when the
boxes in front of the media data grow, the chunk offsets ('stco') move with them, so the
samples are still found where they are. Nothing else changes.

  seqcraft.py icc     seq-vardelay.heics  seq-icc.heics
      adds a 'colr' box of type 'prof' to the sample entry: an ICC profile, which libheif
      does not read for a track and the plugin reads itself.
  seqcraft.py brand   seq-with-still.heic seq-with-still-heic.heic
      the same file with major brand 'heic' instead of 'hevc': the still image wins.
  seqcraft.py corrupt seq-alpha.heics     seq-corrupt.heics
      the colour track's SPS in 'hvcC' becomes a reserved NAL unit type, so the decoder
      gets no sequence parameters and never hands a frame back. The track repeats forever,
      and libheif keeps pushing samples into the decoder until it has pushed as many as its
      edit list repeats the track - which is what the plugin's read budget has to stop.
  seqcraft.py limit   seq-mono.heics      seq-frames-limit.heics
      'stts', 'stsz' (constant size) and 'stsc' claim 2,592,001 samples in a few bytes, one
      more than the plugin lets libheif build tables for: the file must be refused at once.
"""
import struct
import sys

CONTAINERS = {b'moov', b'trak', b'mdia', b'minf', b'stbl', b'dinf', b'edts', b'udta',
              b'tref', b'iprp', b'ipco'}
VISUAL_ENTRIES = {b'hvc1', b'hev1', b'avc1', b'av01', b'mjpg', b'j2ki', b'uncv', b'vvc1'}


class Box:
    def __init__(self, type_, payload=b'', children=None, prefix=b''):
        self.type, self.payload, self.children, self.prefix = type_, payload, children, prefix

    def serialize(self):
        body = self.payload if self.children is None else self.prefix + b''.join(c.serialize() for c in self.children)
        return struct.pack('>I4s', 8 + len(body), self.type) + body

    def find(self, *path):
        node = self
        for t in path:
            node = next((c for c in (node.children or []) if c.type == t), None)
            if node is None:
                return None
        return node

    def find_all(self, t):
        out = []
        for c in (self.children or []):
            if c.type == t:
                out.append(c)
            out.extend(c.find_all(t))
        return out


def parse(data, parent=None):
    boxes, o = [], 0
    while o + 8 <= len(data):
        size, t = struct.unpack('>I4s', data[o:o + 8])
        hdr = 8
        if size == 1:
            size, hdr = struct.unpack('>Q', data[o + 8:o + 16])[0], 16
        elif size == 0:
            size = len(data) - o
        body = data[o + hdr:o + size]
        if t in CONTAINERS:
            boxes.append(Box(t, children=parse(body, t)))
        elif t == b'meta':
            boxes.append(Box(t, prefix=body[:4], children=parse(body[4:], t)))
        elif t == b'stsd':
            boxes.append(Box(t, prefix=body[:8], children=parse(body[8:], t)))
        elif t in VISUAL_ENTRIES and parent == b'stsd':
            boxes.append(Box(t, prefix=body[:78], children=parse(body[78:], t)))
        else:
            boxes.append(Box(t, payload=body))
        o += size
    return boxes


def load(path):
    data = open(path, 'rb').read()
    boxes = parse(data)
    assert b''.join(b.serialize() for b in boxes) == data, 'the file does not survive a round trip'
    return data, boxes


def save(path, original, boxes):
    """Write the boxes, moving every chunk offset by what the boxes in front of 'mdat' gained."""
    def before_mdat(bs):
        n = 0
        for b in bs:
            if b.type == b'mdat':
                break
            n += len(b.serialize())
        return n
    delta = before_mdat(boxes) - before_mdat(parse(original))
    if delta:
        for stco in [s for b in boxes if b.type == b'moov' for s in b.find_all(b'stco')]:
            n = struct.unpack('>I', stco.payload[4:8])[0]
            offsets = struct.unpack('>%dI' % n, stco.payload[8:8 + 4 * n])
            stco.payload = stco.payload[:8] + struct.pack('>%dI' % n, *[x + delta for x in offsets])
    open(path, 'wb').write(b''.join(b.serialize() for b in boxes))


def full_box(version, flags, body):
    return struct.pack('>I', (version << 24) | flags) + body


def synthetic_icc():
    """A minimal ICC v2 profile: the 128-byte header and an empty tag table."""
    p = bytearray(132)
    struct.pack_into('>I', p, 0, 132)
    p[4:8] = b'none'
    struct.pack_into('>I', p, 8, 0x02100000)
    p[12:16], p[16:20], p[20:24] = b'mntr', b'RGB ', b'XYZ '
    p[36:40] = b'acsp'
    p[68:80] = struct.pack('>iii', 0xF6D6, 0x10000, 0xD32D)
    p[80:84] = b'FIMG'
    return bytes(p)


def first_track(boxes, handler=b'pict'):
    moov = next(b for b in boxes if b.type == b'moov')
    for trak in moov.children:
        if trak.type == b'trak' and trak.find(b'mdia', b'hdlr').payload[8:12] == handler:
            return trak
    raise SystemExit('no %s track' % handler.decode())


def icc(src, dst):
    data, boxes = load(src)
    entry = first_track(boxes).find(b'mdia', b'minf', b'stbl', b'stsd', b'hvc1')
    entry.children.append(Box(b'colr', payload=b'prof' + synthetic_icc()))
    save(dst, data, boxes)


def brand(src, dst):
    data = open(src, 'rb').read()
    assert data[4:8] == b'ftyp' and data[8:12] == b'hevc'
    open(dst, 'wb').write(data[:8] + b'heic' + data[12:])


def corrupt(src, dst):
    data, boxes = load(src)
    hvcc = first_track(boxes).find(b'mdia', b'minf', b'stbl', b'stsd', b'hvc1', b'hvcC')
    p = bytearray(hvcc.payload)
    o, arrays, found = 23, p[22], False
    for _ in range(arrays):
        kind = p[o] & 0x3F
        count = struct.unpack('>H', p[o + 1:o + 3])[0]
        o += 3
        for _ in range(count):
            length = struct.unpack('>H', p[o:o + 2])[0]
            if kind == 33:                      # the SPS NAL unit: make its type 41 (reserved)
                p[o + 2] = (p[o + 2] & 0x81) | (41 << 1)
                found = True
            o += 2 + length
    assert found, 'no SPS in hvcC'
    hvcc.payload = bytes(p)
    save(dst, data, boxes)


def limit(src, dst, samples=2592001):
    data, boxes = load(src)
    stbl = first_track(boxes).find(b'mdia', b'minf', b'stbl')
    for c in stbl.children:
        if c.type == b'stts':
            c.payload = full_box(0, 0, struct.pack('>III', 1, samples, 40))
        elif c.type == b'stsz':
            c.payload = full_box(0, 0, struct.pack('>II', 1, samples))
        elif c.type == b'stsc':
            c.payload = full_box(0, 0, struct.pack('>IIII', 1, 1, samples, 1))
    stbl.children = [c for c in stbl.children if c.type not in (b'stss', b'ctts')]
    save(dst, data, boxes)


if __name__ == '__main__':
    what, src, dst = sys.argv[1:4]
    {'icc': icc, 'brand': brand, 'corrupt': corrupt, 'limit': limit}[what](src, dst)
    print('%s -> %s' % (src, dst))
