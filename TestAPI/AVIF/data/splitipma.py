"""Rewrite an AVIF so that its single ipma box becomes two ipma boxes with the same version
and flags, the first holding the first entry and the second the remaining entries. This is
the container shape link-u's cavif produced (one ipma per item), which BMFF 8.11.14.1 forbids
and upstream libavif refuses. Only safe for files whose items live in idat (construction
method 1), because the meta box grows and absolute mdat offsets would move otherwise."""
import struct
import sys


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
        yield pos, hdr, size, typ
        pos += size


def box(typ, payload):
    return struct.pack(">I4s", 8 + len(payload), typ) + payload


src, dst = sys.argv[1], sys.argv[2]
with open(src, "rb") as f:
    d = f.read()

meta = [b for b in boxes(d, 0, len(d)) if b[3] == b"meta"][0]
mpos, mhdr, msize, _ = meta
# meta is a FullBox: 4 bytes of version/flags after the header
children = list(boxes(d, mpos + mhdr + 4, mpos + msize))
iprp = [b for b in children if b[3] == b"iprp"][0]
ipos, ihdr, isize, _ = iprp
ichildren = list(boxes(d, ipos + ihdr, ipos + isize))
ipco = [b for b in ichildren if b[3] == b"ipco"][0]
ipmas = [b for b in ichildren if b[3] == b"ipma"]
assert len(ipmas) == 1, "expected exactly one ipma"
apos, ahdr, asize, _ = ipmas[0]
vf = d[apos + ahdr:apos + ahdr + 4]
version = vf[0]
u15 = (vf[3] & 1) != 0
p = apos + ahdr + 4
count = struct.unpack(">I", d[p:p + 4])[0]
p += 4
entries = []
for _ in range(count):
    start = p
    p += 2 if version < 1 else 4
    n = d[p]
    p += 1
    p += n * (2 if u15 else 1)
    entries.append(d[start:p])
assert p == apos + asize, "ipma entries do not fill the box"
assert count >= 2, "need at least two entries to split"

ipma1 = box(b"ipma", vf + struct.pack(">I", 1) + entries[0])
ipma2 = box(b"ipma", vf + struct.pack(">I", count - 1) + b"".join(entries[1:]))
ipco_bytes = d[ipco[0]:ipco[0] + ipco[2]]
new_iprp = box(b"iprp", ipco_bytes + ipma1 + ipma2)
new_meta_payload = d[mpos + mhdr:mpos + mhdr + 4]
for cpos, chdr, csize, ctyp in children:
    if ctyp == b"iprp":
        new_meta_payload += new_iprp
    else:
        new_meta_payload += d[cpos:cpos + csize]
new_meta = box(b"meta", new_meta_payload)
out = d[:mpos] + new_meta + d[mpos + msize:]
with open(dst, "wb") as f:
    f.write(out)
print("wrote", dst, len(d), "->", len(out), "bytes; ipma split", count, "entries into 1 +", count - 1)
