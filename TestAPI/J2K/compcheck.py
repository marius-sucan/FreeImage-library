#!/usr/bin/env python3
"""Compare FreeImage's decoded pixels (corpus -d dumps) with refdec's
component dumps from the reference libopenjp2 2.5.4.

usage: compcheck.py DUMPDIR REFDIR file...

Applies FreeImage's own rule for which components it keeps: all of them when
there are 1, 3 or 4 with equal dx/dy/prec, otherwise component 0 only (and
16-bit types are always compared at full precision). Works in row chunks so a
fuzzer file that expands to a gigabyte-sized image does not exhaust memory.
"""
import sys, os
import numpy as np

CHUNK_ROWS = 1 << 20

def read_fi_raw(path):
    with open(path, 'rb') as f:
        hdr = f.readline().decode().split()
        w, h, bpp, ftype = int(hdr[1]), int(hdr[2]), int(hdr[3]), hdr[4]
        data = np.fromfile(f, np.uint8)
    if ftype == 'BITMAP':
        if bpp == 8:  return data.reshape(h, w, 1), [0]
        if bpp == 24: return data.reshape(h, w, 3), [2, 1, 0]          # stored BGR
        if bpp == 32: return data.reshape(h, w, 4), [2, 1, 0, 3]       # stored BGRA
    if ftype == 'UINT16': return data.view('<u2').reshape(h, w, 1), [0]
    if ftype == 'RGB16':  return data.view('<u2').reshape(h, w, 3), [0, 1, 2]
    if ftype == 'RGBA16': return data.view('<u2').reshape(h, w, 4), [0, 1, 2, 3]
    raise ValueError(f'unsupported {bpp}bpp {ftype}')

def read_comps(path):
    comps = []
    with open(path, 'rb') as f:
        n = int(f.readline().split()[1])
        for _ in range(n):
            w, h, prec, sgnd, dx, dy = map(int, f.readline().split())
            bps = 1 if prec <= 8 else 2
            raw = np.fromfile(f, np.uint8, w * h * bps)
            a = (raw if bps == 1 else raw.view('<u2')).reshape(h, w)
            comps.append(dict(w=w, h=h, prec=prec, sgnd=sgnd, dx=dx, dy=dy, data=a))
    return comps

def expected_channels(comps):
    c0 = comps[0]
    same = all(c['dx'] == c0['dx'] and c['dy'] == c0['dy'] and c['prec'] == c0['prec'] for c in comps)
    return len(comps) if same and len(comps) in (1, 3, 4) else 1

def compare(fi, order, comps, n):
    h, w = fi.shape[:2]
    step = max(1, (16 << 20) // max(1, w))
    bad = 0; worst = 0
    for c in range(n):
        src = order[c]
        ref = comps[c]['data']
        for y in range(0, h, step):
            a = fi[y:y + step, :, src].astype(np.int32)
            b = ref[y:y + step, :].astype(np.int32)
            d = np.abs(a - b)
            bad += int(np.count_nonzero(d))
            if d.size:
                worst = max(worst, int(d.max()))
    return bad, h * w * n, worst

def main():
    dumpdir, refdir, files = sys.argv[1], sys.argv[2], sys.argv[3:]
    tally = {'identical': 0, 'differ': 0, 'other': 0}
    for path in files:
        name = os.path.basename(path)
        fi_path = os.path.join(dumpdir, name + '.raw')
        ref_path = os.path.join(refdir, name + '.comps')
        have_fi, have_ref = os.path.exists(fi_path), os.path.exists(ref_path)
        if not have_fi and not have_ref:
            print(f'{name:22s} both refused'); tally['other'] += 1; continue
        if not have_fi:
            c = read_comps(ref_path)
            print(f'{name:22s} FreeImage refused; reference decoded {c[0]["w"]}x{c[0]["h"]}, {len(c)} comps, prec {c[0]["prec"]}'); tally['other'] += 1; continue
        if not have_ref:
            print(f'{name:22s} FreeImage decoded; reference refused'); tally['other'] += 1; continue
        fi, order = read_fi_raw(fi_path)
        comps = read_comps(ref_path)
        h, w, nch = fi.shape
        exp = expected_channels(comps)
        if (h, w) != comps[0]['data'].shape:
            print(f'{name:22s} size differs: FreeImage {w}x{h} vs reference {comps[0]["w"]}x{comps[0]["h"]}'); tally['differ'] += 1; continue
        note = '' if nch == exp else f' (FreeImage kept {nch} ch, expected {exp} of {len(comps)} comps)'
        bad, total, worst = compare(fi, order, comps, min(nch, len(comps)))
        if bad == 0 and nch == exp:
            print(f'{name:22s} identical'); tally['identical'] += 1
        elif bad == 0:
            print(f'{name:22s} identical on compared channels{note}'); tally['differ'] += 1
        else:
            print(f'{name:22s} {bad} of {total} samples differ, max |d|={worst}{note}'); tally['differ'] += 1
    print(f'-- {tally["identical"]} identical, {tally["differ"]} differ, {tally["other"]} not comparable')

if __name__ == '__main__':
    main()
