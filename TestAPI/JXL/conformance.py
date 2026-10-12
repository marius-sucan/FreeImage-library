#!/usr/bin/env python3
"""Decode the JPEG XL conformance suite (fetch_conformance.py) with FreeImage and compare every frame
with the reference arrays, within each test's tolerances plus the output's quantization step.
usage: conformance.py <libfreeimage.so> [conformance dir] [case ...]"""
import ctypes as C, json, os, sys
import numpy as np

lib = C.CDLL(sys.argv[1])
ROOT = sys.argv[2] if len(sys.argv) > 2 else 'conformance'
CASES = sys.argv[3:] or sorted(d for d in os.listdir(ROOT) if os.path.isdir(os.path.join(ROOT, d)))
FIF_JXL, JXL_CMYK = 40, 4

class ICC(C.Structure):
    _fields_ = [('flags', C.c_uint16), ('size', C.c_uint32), ('data', C.c_void_p)]

for name, res, args in [
    ('FreeImage_Initialise', None, [C.c_int]),
    ('FreeImage_SetOutputMessage', None, [C.c_void_p]),
    ('FreeImage_OpenMultiBitmap', C.c_void_p, [C.c_int, C.c_char_p, C.c_int, C.c_int, C.c_int, C.c_int]),
    ('FreeImage_GetPageCount', C.c_int, [C.c_void_p]),
    ('FreeImage_LockPage', C.c_void_p, [C.c_void_p, C.c_int]),
    ('FreeImage_UnlockPage', C.c_int, [C.c_void_p, C.c_void_p, C.c_int]),
    ('FreeImage_CloseMultiBitmap', C.c_int, [C.c_void_p, C.c_int]),
    ('FreeImage_GetWidth', C.c_uint, [C.c_void_p]), ('FreeImage_GetHeight', C.c_uint, [C.c_void_p]),
    ('FreeImage_GetBPP', C.c_uint, [C.c_void_p]), ('FreeImage_GetImageType', C.c_int, [C.c_void_p]),
    ('FreeImage_GetPitch', C.c_uint, [C.c_void_p]), ('FreeImage_GetBits', C.c_void_p, [C.c_void_p]),
    ('FreeImage_GetICCProfile', C.POINTER(ICC), [C.c_void_p]),
    ('FreeImage_AllocateT', C.c_void_p, [C.c_int, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_uint]),
    ('FreeImage_CreateICCProfile', C.c_void_p, [C.c_void_p, C.c_void_p, C.c_long]),
    ('FreeImage_ConvertToICCProfile', C.c_void_p, [C.c_void_p, C.c_void_p, C.c_uint32, C.c_int]),
    ('FreeImage_Unload', None, [C.c_void_p]),
]:
    f = getattr(lib, name); f.restype = res; f.argtypes = args

messages = []
MSG = C.CFUNCTYPE(None, C.c_int, C.c_char_p)
on_message = MSG(lambda fif, m: messages.append(m.decode(errors='replace')))
lib.FreeImage_SetOutputMessage(C.cast(on_message, C.c_void_p))
lib.FreeImage_Initialise(0)

# a page as float samples in [0, 1] (float types as stored), top row first, RGB order
def to_array(dib, bgr=True):
    w, h, bpp, t, pitch = (lib.FreeImage_GetWidth(dib), lib.FreeImage_GetHeight(dib), lib.FreeImage_GetBPP(dib),
                           lib.FreeImage_GetImageType(dib), lib.FreeImage_GetPitch(dib))
    raw = np.ctypeslib.as_array((C.c_ubyte * (pitch * h)).from_address(lib.FreeImage_GetBits(dib))).reshape(h, pitch)[::-1]
    if t == 1:
        ch = bpp // 8
        a = raw[:, :w * ch].reshape(h, w, ch).astype(np.float64) / 255.0
        return a[:, :, [2, 1, 0] + ([3] if ch == 4 else [])] if (ch >= 3 and bgr) else a
    dt, scale, ch = {2: (np.uint16, 65535, 1), 9: (np.uint16, 65535, 3), 10: (np.uint16, 65535, 4),
                     6: (np.float32, 1, 1), 11: (np.float32, 1, 3), 12: (np.float32, 1, 4)}[t]
    a = np.ascontiguousarray(raw[:, :w * ch * np.dtype(dt).itemsize]).view(dt).reshape(h, w, ch)
    return a.astype(np.float64) / scale

# the reference's colours in the profile of FreeImage's output, through FreeImage's own conversion
def to_profile(rgb, src_icc, dst_icc):
    h, w, ch = rgb.shape
    dib = lib.FreeImage_AllocateT({1: 6, 3: 11}[ch], w, h, 32 * ch, 0, 0, 0)
    pitch = lib.FreeImage_GetPitch(dib)
    raw = np.ctypeslib.as_array((C.c_ubyte * (pitch * h)).from_address(lib.FreeImage_GetBits(dib))).reshape(h, pitch)
    raw[:, :w * ch * 4] = np.ascontiguousarray(rgb[::-1].astype(np.float32)).view(np.uint8).reshape(h, w * ch * 4)
    src = C.create_string_buffer(src_icc, len(src_icc))
    lib.FreeImage_CreateICCProfile(dib, src, len(src_icc))
    conv = lib.FreeImage_ConvertToICCProfile(dib, dst_icc, len(dst_icc), 0)
    lib.FreeImage_Unload(dib)
    if not conv:
        return None
    out = to_array(conv)
    lib.FreeImage_Unload(conv)
    return out

failures = 0
for case in CASES:
    d = os.path.join(ROOT, case)
    tj = json.load(open(os.path.join(d, 'test.json')))
    extra = tj.get('extra_channel_type', [])
    if not os.path.exists(os.path.join(d, 'reference_image.npy')):
        print(f'{case:28s} skipped: no reference image'); continue
    ref = np.load(os.path.join(d, 'reference_image.npy'))
    ref_icc = open(os.path.join(d, 'reference.icc'), 'rb').read()
    cmyk = 'Black' in extra
    ncol = ref.shape[3] - len(extra)
    messages.clear()
    m = lib.FreeImage_OpenMultiBitmap(FIF_JXL, os.path.join(d, 'input.jxl').encode(), 0, 1, 1, JXL_CMYK if cmyk else 0)
    if not m:
        print(f'{case:28s} FAILED: not opened {messages}'); failures += 1; continue
    pages = lib.FreeImage_GetPageCount(m)
    status, worst, note = 'ok', (0.0, 0.0), ''
    if pages != ref.shape[0]:
        status = f'FAILED: {pages} pages, the reference has {ref.shape[0]}'
    for p in range(min(pages, ref.shape[0])):
        dib = lib.FreeImage_LockPage(m, p)
        if not dib:
            status = f'FAILED: page {p} not decoded {messages}'; break
        t = lib.FreeImage_GetImageType(dib)
        q = {1: 1 / 255, 2: 1 / 65535, 9: 1 / 65535, 10: 1 / 65535}.get(t, 0)
        icc = lib.FreeImage_GetICCProfile(dib).contents
        out_icc = C.string_at(icc.data, icc.size) if icc.size else b''
        r = ref[p].astype(np.float64)
        if cmyk:
            # FreeImage CMYK is ink, JPEG XL stores 1 - ink
            out = 1 - to_array(dib, bgr=False)
            rc = np.concatenate([r[:, :, :3], r[:, :, 3 + extra.index('Black'):4 + extra.index('Black')]], axis=2)
        else:
            out = to_array(dib)
            rc = r[:, :, :ncol]
            if out_icc and out_icc != ref_icc:
                rc = to_profile(rc, ref_icc, out_icc)
                note = ' (reference converted to the output profile)'
                if rc is None:
                    status = 'FAILED: the reference cannot be converted'; lib.FreeImage_UnlockPage(m, dib, 0); break
        oc = out[:, :, :rc.shape[2]]
        if t in (1, 2, 9, 10):
            rc = np.clip(rc, 0, 1)
        diff = np.abs(oc - rc)
        peak, rms = float(diff.max()), float(np.sqrt((diff ** 2).mean()))
        tol = tj['frames'][min(p, len(tj['frames']) - 1)]
        ok = peak <= tol['peak_error'] + q and rms <= tol['rms_error'] + q / 2
        if not ok and 'Alpha' in extra and out.shape[2] == 4 and not cmyk:
            # a premultiplied reference: FreeImage hands out straight alpha, clamped
            alpha = r[:, :, ncol + extra.index('Alpha')]
            visible = alpha > 1 / 256
            straight = np.clip(r[:, :, :ncol] / np.maximum(alpha, 1e-9)[:, :, None], 0, 1)
            pdiff = np.abs(oc - straight)[visible]
            if pdiff.max() <= 2e-3:
                peak, rms, ok = float(pdiff.max()), float(np.sqrt((pdiff ** 2).mean())), True
                note += ' (premultiplied reference, compared unpremultiplied)'
        if not ok and 'SpotColor' in extra:
            # FreeImage renders the spot colours into the image, the reference does not: compare where they are 0
            spots = [ncol + i for i, e in enumerate(extra) if e == 'SpotColor']
            plain = np.all(r[:, :, spots] == 0, axis=2)
            sdiff = diff[plain]
            if plain.any() and sdiff.max() <= tol['peak_error'] + q:
                peak, rms, ok = float(sdiff.max()), float(np.sqrt((sdiff ** 2).mean())), True
                note += f' (spot colours rendered: compared on the {plain.mean():.0%} without spot colour)'
        worst = (max(worst[0], peak), max(worst[1], rms))
        if not ok:
            status = f'FAILED: page {p} peak {peak:.5f} rms {rms:.6f}'
        lib.FreeImage_UnlockPage(m, dib, 0)
    lib.FreeImage_CloseMultiBitmap(m, 0)
    if status != 'ok':
        failures += 1
    tol = tj['frames'][0]
    print(f'{case:28s} {status:10s} pages {pages:3d} peak {worst[0]:.5f} rms {worst[1]:.6f} (tolerance {tol["peak_error"]}/{tol["rms_error"]}){note}')
print(f'{"passed" if not failures else "FAILED"}: {failures} failure(s)')
sys.exit(1 if failures else 0)
