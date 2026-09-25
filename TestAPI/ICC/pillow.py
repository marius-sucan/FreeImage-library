"""FreeImage's color management against Pillow's ImageCms, a Little CMS of another version.

Needs Pillow, numpy, colord's CMYK profiles and Dist/libfreeimage-*.so; skips what is missing.
Differences of one step are tolerated; perceptual CMYK may differ more when the two Little CMS
versions detect a black point differently.
"""
import ctypes as C, glob, io, os, sys

try:
    import numpy as np
    from PIL import Image, ImageCms
except ImportError as e:
    print('skipped: %s' % e)
    sys.exit(0)

HERE = os.path.dirname(os.path.abspath(__file__))
SO = sorted(glob.glob(os.path.join(HERE, '../../Dist/libfreeimage-*.so')))
if not SO:
    print('skipped: build the shared library first (make -f Makefile.gnu dist)')
    sys.exit(0)
lib = C.CDLL(SO[-1])
P = C.c_void_p


def fn(name, res, *args):
    f = getattr(lib, 'FreeImage_' + name)
    f.restype, f.argtypes = res, list(args)
    return f


class FIICCPROFILE(C.Structure):
    _fields_ = [('flags', C.c_uint16), ('size', C.c_uint32), ('data', P)]


Allocate = fn('Allocate', P, C.c_int, C.c_int, C.c_int, C.c_uint, C.c_uint, C.c_uint)
Unload = fn('Unload', None, P)
GetScanLine = fn('GetScanLine', P, P, C.c_int)
GetPalette = fn('GetPalette', P, P)
GetICCProfile = fn('GetICCProfile', C.POINTER(FIICCPROFILE), P)
CreateICCProfile = fn('CreateICCProfile', P, P, P, C.c_long)
ConvertToICCProfile = fn('ConvertToICCProfile', P, P, P, C.c_uint32, C.c_int)
ConvertToCMYK = fn('ConvertToCMYK', P, P, P, C.c_uint32, C.c_int)
ConvertCMYKToRGB = fn('ConvertCMYKToRGB', P, P, P, C.c_uint32, C.c_int)
SoftProof = fn('SoftProof', P, P, P, C.c_uint32, P, C.c_uint32, C.c_int)
GetBuiltInICCProfile = fn('GetBuiltInICCProfile', P, C.c_int, C.POINTER(C.c_uint32))


def builtin(i):
    n = C.c_uint32()
    return C.string_at(GetBuiltInICCProfile(i, C.byref(n)), n.value)


def to_dib(a, kind, profile=None):
    h, w = a.shape[:2]
    bpp = {'rgb': 24, 'cmyk': 32, 'grey': 8}[kind]
    dib = Allocate(w, h, bpp, 0xFF0000, 0xFF00, 0xFF)
    px = a.copy()
    if kind == 'rgb':
        px[..., [0, 2]] = a[..., [2, 0]]
    if kind == 'grey':
        C.memmove(GetPalette(dib), bytes(b for i in range(256) for b in (i, i, i, 0)), 1024)
    for y in range(h):
        C.memmove(GetScanLine(dib, h - 1 - y), px[y].tobytes(), px[y].nbytes)
    if profile:
        CreateICCProfile(dib, C.cast(C.create_string_buffer(profile, len(profile)), P), len(profile))
    if kind == 'cmyk':
        GetICCProfile(dib).contents.flags |= 1
    return dib


def from_dib(dib, h, w, ch, cmyk=False):
    out = np.zeros((h, w, ch), np.uint8)
    for y in range(h):
        out[y] = np.frombuffer(C.string_at(GetScanLine(dib, h - 1 - y), w * ch), np.uint8).reshape(w, ch)
    if ch == 3 and not cmyk:
        out[..., [0, 2]] = out[..., [2, 0]]
    return out


def call(f, dib, *args):
    kept = []
    conv = []
    for a in args:
        if isinstance(a, bytes):
            b = C.create_string_buffer(a, len(a))
            kept.append(b)
            conv += [C.cast(b, P), len(a)]
        elif a is None:
            conv += [None, 0]
        else:
            conv.append(a)
    return f(dib, *conv)


def prof(b):
    return ImageCms.ImageCmsProfile(io.BytesIO(b))


def pil(arr, mode, src, dst, intent, flags=0, outmode=None):
    im = Image.fromarray(arr[..., 0] if mode == 'L' else arr, mode)
    t = ImageCms.buildTransform(prof(src), prof(dst), mode, outmode or mode, renderingIntent=intent, flags=flags)
    out = np.asarray(ImageCms.applyTransform(im, t))
    return out[..., None] if out.ndim == 2 else out


failures = 0


def compare(name, got, ref, tol=1):
    global failures
    d = int(np.abs(got.astype(int) - ref.astype(int)).max())
    ok = d <= tol
    failures += not ok
    print('%s %-50s max difference %d' % ('ok  ' if ok else 'FAIL', name, d))


rng = np.random.default_rng(7)
H, W = 61, 97
rgb = rng.integers(0, 256, (H, W, 3), dtype=np.uint8)
cmyk = rng.integers(0, 256, (H, W, 4), dtype=np.uint8)
grey = rng.integers(0, 256, (H, W, 1), dtype=np.uint8)
B = [builtin(i) for i in range(7)]
BPC = 0x2000

for sname, s in [('Adobe RGB', B[4]), ('ProPhoto', B[6])]:
    for dname, d in [('sRGB', B[0]), ('Display P3', B[5])]:
        for intent in range(4):
            for bpc in (0, 1):
                dib = to_dib(rgb, 'rgb', s)
                out = call(ConvertToICCProfile, dib, d, intent | (0x100 if bpc else 0))
                compare('%s -> %s, intent %d, bpc %d' % (sname, dname, intent, bpc), from_dib(out, H, W, 3), pil(rgb, 'RGB', s, d, intent, BPC if bpc else 0))
                Unload(out); Unload(dib)

for dname, d in [('sRGB', B[0]), ('Adobe RGB', B[4])]:
    dib = to_dib(grey, 'grey')
    out = call(ConvertToICCProfile, dib, d, 0)
    compare('grey -> %s' % dname, from_dib(out, H, W, 3), pil(grey, 'L', B[2], d, 0, 0, 'RGB'))
    Unload(out); Unload(dib)

press = [p for p in ['/usr/share/color/icc/colord/FOGRA39L_coated.icc', '/usr/share/color/icc/colord/SWOP_TR005_coated_5.icc'] if os.path.exists(p)]
if not press:
    print('skipped the CMYK checks: no colord profiles')
for path in press:
    pb = open(path, 'rb').read()
    name = os.path.basename(path)
    for intent in (0, 1):
        dib = to_dib(rgb, 'rgb')
        out = call(ConvertToCMYK, dib, pb, intent)
        compare('sRGB -> %s, intent %d' % (name, intent), from_dib(out, H, W, 4, True), pil(rgb, 'RGB', B[0], pb, intent, 0, 'CMYK'))
        Unload(out); Unload(dib)
        dib = to_dib(cmyk, 'cmyk', pb)
        out = call(ConvertCMYKToRGB, dib, None, intent)
        # Little CMS 2.19 detects the perceptual black point of these profiles differently from 2.17
        compare('%s -> sRGB, intent %d' % (name, intent), from_dib(out, H, W, 3), pil(cmyk, 'CMYK', pb, B[0], intent, 0, 'RGB'), 1 if intent else 8)
        Unload(out); Unload(dib)
    for flags, proof_intent, pflags, what in [(0, 1, 0, 'relative'), (0x400, 3, 0, 'paper white'), (0x200, 1, 0x1000, 'gamut check')]:
        dib = to_dib(rgb, 'rgb')
        out = call(SoftProof, dib, pb, None, flags)
        t = ImageCms.buildProofTransform(prof(B[0]), prof(B[0]), prof(pb), 'RGB', 'RGB', renderingIntent=0, proofRenderingIntent=proof_intent, flags=0x4000 | pflags)
        compare('soft proof %s, %s' % (name, what), from_dib(out, H, W, 3), np.asarray(ImageCms.applyTransform(Image.fromarray(rgb, 'RGB'), t)))
        Unload(out); Unload(dib)

print('%d failures' % failures)
sys.exit(1 if failures else 0)
