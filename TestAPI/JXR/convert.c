/* FreeImage 3 - JPEG XR pixel formats converted on load: data/ holds 8x140 lossless 64bppRGBHalf, 64bppRGBFixedPoint,
   32bppRGBE and 128bppRGBFloat (loaded as RGBF), 64bppRGBAHalf with planar alpha (RGBAF), each made by jxrlib from
   values exact in either, and 9x140, an odd width, 32bppRGB101010 (RGB16), 32bppBGR and 32bppRGB (24-bit, their
   padding dropped), 32bppRGBA, premultiplied 32bppPBGRA, 32bppPRGBA, 64bppPRGBA and 128bppPRGBAFloat (straight alpha),
   and 32bppCMYK, 64bppCMYK, 40bppCMYKAlpha and 80bppCMYKAlpha (RGB, their CMYK profile dropped; with JXR_CMYK their
   CMYK samples, the alpha dropped, the profile kept and marked CMYK); crop.jxr, 40x140 32bppBGRA with planar alpha
   cropped by jxrlib at 16,32 of a 64x200 image, its image plane keeping a 16-row margin its alpha plane lacks;
   hardtiles.jxr, 96x140 24bppBGR with two levels of overlap and hard tiles 2, 2, 2 macroblocks wide and 5, 4 high;
   interleaved alpha in ileave420.jxr, 8x140 32bppBGRA with grey 4:2:0 colour scaled and its lossless alpha not, ileave16.jxr,
   9x140 64bppRGBA with SHIFT_BITS 3, and ileave_q.jxr, 9x140 32bppBGRA at QP index 40, held to the reference decoder's
   output in ileave_q.ref (B, G, R, A); lpqp.jxr, 32x140 24bppBGR in frequency mode with 2x2 tiles of two LP QPs each and
   an LP QP index per macroblock, held to lpqp.ref (B, G, R, 255); every container orientation must load the values the
   file was made from, and a header-only load the same type and size; FIF_LOAD_DISPLAY_ICC on an sRGB display, through device CMYK
   as the CMYK profiles cannot be linked, the pixels of a plain load, alpha included. lpqp_bad.jxr, lpqp.jxr with the reserved LP QP
   index 2 in one macroblock, must be refused. n-channel images, 9x140 lossless, alpha planar and last: nch3 24bpp3Channels,
   nch3a 32bpp3ChannelsAlpha and nch3ax16 64bpp3ChannelsAlpha load as RGB(A), channel 0 red; nch4 32bpp4Channels,
   nch4a 40bpp4ChannelsAlpha, nch5a 48bpp5ChannelsAlpha and nch8x16 128bpp8Channels as CMYK, their first four channels,
   loaded as the CMYK files are, the further channels dropped. ycc444, 24bppYCC444, must be refused without PTM_COLOR_INFO, header only too */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static unsigned get16(const BYTE *p) { return p[0] | (p[1] << 8); }
static unsigned get32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

/* the reference decoder's output of a lossy file, B, G, R, A, top-down */
static const BYTE *ref = NULL;
static unsigned ref_width = 0;

/* channel c (R, G, B, A) of upright pixel (x, y) as made, y counted from the top; alpha is channel 3, CMYK's alpha 4 */
static double made(const char *name, unsigned x, unsigned y, unsigned c) {
    const unsigned k = (3 * x + 5 * y + 17 * c) % 97, k3 = (3 * x + 5 * y + 17 * 3) % 97, k4 = (3 * x + 5 * y + 17 * 4) % 97;
    if (ref) return ref[((size_t)y * ref_width + x) * 4 + (c == 3 ? 3 : 2 - c)];
    if (!strcmp(name, "ileave420")) return (c == 3) ? 64 + k3 : 2.0 * ((3 * x + 5 * y) % 97);
    if (!strcmp(name, "ileave16")) return (c == 3) ? 16384 + 256 * k3 : 512.0 * k;
    if (!strcmp(name, "crop")) {
        /* the source's x, y, ..., 5y + x / 16, at (x + 16, y + 32) */
        const unsigned sx = x + 16, sy = y + 32;
        const unsigned v[4] = { sx & 255, sy & 255, ((sx >> 8) * 64 + (sy >> 8) * 16) & 255, (sy * 5 + (sx >> 4)) & 255 };
        return v[c];
    }
    if (!strcmp(name, "rgb101010")) return (double)((k * 8) << 6);
    if (!strcmp(name, "bgr32") || !strcmp(name, "rgb32") || !strcmp(name, "hardtiles")) return 2.0 * k;
    /* mantissa 128 + k, exponent 130 + k(alpha) % 4 */
    if (!strcmp(name, "rgbe")) return (128.0 + k) / 64.0 * (1 << (k3 % 4));
    if (!strcmp(name, "rgba32")) return (c == 3) ? 64 + k3 : 2.0 * k;
    if (!strcmp(name, "pbgra32") || !strcmp(name, "prgba32")) {
        /* stored premultiplied, rounded; loaded as undone, rounded */
        const unsigned a = 64 + k3, s = (2 * k * a + 127) / 255, u = (s * 255 + a / 2) / a;
        return (c == 3) ? a : (u > 255 ? 255 : u);
    }
    if (!strcmp(name, "prgba64")) {
        const unsigned long long a = 16384 + 256 * k3, s = (512ull * k * a + 32767) / 65535, u = (s * 65535 + a / 2) / a;
        return (c == 3) ? (double)a : (double)(u > 65535 ? 65535 : u);
    }
    if (!strcmp(name, "prgbaf128")) {
        const float a = 0.25f * (float)(1 + k3 % 4), s = (float)k / 32.0f * a;
        return (c == 3) ? a : s / a;
    }
    /* (max - C,M,Y) * (max - K) / max */
    if (!strcmp(name, "cmyk32") || !strcmp(name, "cmyka40")) return (c == 3) ? 64 + k4 : (255 - 2 * k) * (255 - k3) / 255;
    if (!strcmp(name, "cmyk64") || !strcmp(name, "cmyka80")) return (c == 3) ? 16384 + 256 * k4 : (65535 - 512 * k) * (65535 - 256 * k3) / 65535;
    if (!strncmp(name, "nch", 3)) {
        /* channel n, after the n colour channels, is the alpha */
        const unsigned n = (unsigned)(name[3] - '0'), deep = (strstr(name, "x16") != NULL), ka = (3 * x + 5 * y + 17 * n) % 97;
        if (c == 3) return deep ? 16384 + 256 * ka : 64 + ka;
        if (n == 3) return deep ? 512.0 * k : 2.0 * k;
        return deep ? (65535 - 512 * k) * (65535 - 256 * k3) / 65535 : (255 - 2 * k) * (255 - k3) / 255;
    }
    return k / 32.0;
}

/* sample c (C, M, Y, K) of upright pixel (x, y) as made, for a CMYK file loaded with JXR_CMYK */
static double made_cmyk(const char *name, unsigned x, unsigned y, unsigned c) {
    const unsigned k = (3 * x + 5 * y + 17 * c) % 97;
    const int deep = (strstr(name, "64") != NULL) || (strstr(name, "80") != NULL) || (strstr(name, "x16") != NULL);
    if (c == 3) return deep ? 256.0 * k : k;
    return deep ? 512.0 * k : 2.0 * k;
}

/* the same pixel type, size and samples */
static int same_pixels(FIBITMAP *a, FIBITMAP *b) {
    unsigned y;
    if (FreeImage_GetImageType(a) != FreeImage_GetImageType(b) || FreeImage_GetBPP(a) != FreeImage_GetBPP(b) ||
        FreeImage_GetWidth(a) != FreeImage_GetWidth(b) || FreeImage_GetHeight(a) != FreeImage_GetHeight(b)) return 0;
    for (y = 0; y < FreeImage_GetHeight(a); y++) {
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), FreeImage_GetLine(a))) return 0;
    }
    return 1;
}

/* channel c (R, G, B, A) of pixel x of a scanline */
static double loaded(FIBITMAP *dib, const BYTE *line, unsigned x, unsigned c) {
    static const int rgba[4] = { FI_RGBA_RED, FI_RGBA_GREEN, FI_RGBA_BLUE, FI_RGBA_ALPHA };
    switch (FreeImage_GetImageType(dib)) {
        case FIT_RGB16: return ((const unsigned short *)line)[x * 3 + c];
        case FIT_RGBA16: return ((const unsigned short *)line)[x * 4 + c];
        case FIT_RGBF: return ((const float *)line)[x * 3 + c];
        case FIT_RGBAF: return ((const float *)line)[x * 4 + c];
        default: return line[x * (FreeImage_GetBPP(dib) / 8) + rgba[c]];
    }
}

static void formats(const char *dir, const char *name, unsigned w, FREE_IMAGE_TYPE type, unsigned bpp, int flags) {
    const unsigned h = 140, channels = (type == FIT_RGBAF || type == FIT_RGBA16 || bpp == 32) ? 4 : 3;
    const int cmyk = (strstr(name, "cmyk") != NULL) || (!strncmp(name, "nch", 3) && name[3] >= '4'), kept = cmyk && ((flags & JXR_CMYK) == JXR_CMYK);
    char path[1024];
    BYTE *data = NULL, *orient = NULL;
    long size = 0;
    DWORD ifd, k;
    int o;
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s.jxr", dir, name);
    f = fopen(path, "rb");
    CHECK(f != NULL, "%s: missing", path);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (BYTE *)malloc(size);
    size = (long)fread(data, 1, size, f);
    fclose(f);
    snprintf(path, sizeof(path), "%s/%s.ref", dir, name);
    if ((f = fopen(path, "rb")) != NULL) {
        BYTE *r = (BYTE *)malloc((size_t)w * h * 4);
        if (fread(r, 1, (size_t)w * h * 4, f) == (size_t)w * h * 4) ref = r, ref_width = w;
        else free(r);
        fclose(f);
    }
    CHECK(ref != NULL || (strcmp(name, "ileave_q") && strcmp(name, "lpqp")), "%s: missing or short", path);
    ifd = get32(data + 4);
    for (k = 0; ifd + 2 <= (DWORD)size && k < get16(data + ifd) && ifd + 14 + 12 * k <= (DWORD)size; k++) {
        if (get16(data + ifd + 2 + 12 * k) == 0xbc02) orient = data + ifd + 2 + 12 * k;
    }
    CHECK(orient != NULL, "%s: no orientation", name);
    for (o = 0; orient && o < 8; o++) {
        FIMEMORY *mem;
        FIBITMAP *dib, *hdr;
        unsigned x, y, c, W, H, bad = 0;
        orient[8] = (BYTE)o;
        mem = FreeImage_OpenMemory(data, (DWORD)size);
        dib = FreeImage_LoadFromMemory(FIF_JXR, mem, flags);
        FreeImage_CloseMemory(mem);
        mem = FreeImage_OpenMemory(data, (DWORD)size);
        hdr = FreeImage_LoadFromMemory(FIF_JXR, mem, flags | FIF_LOAD_NOPIXELS);
        FreeImage_CloseMemory(mem);
        W = (o < 4) ? w : h;
        H = (o < 4) ? h : w;
        CHECK(hdr && !FreeImage_HasPixels(hdr) && FreeImage_GetImageType(hdr) == type && FreeImage_GetBPP(hdr) == bpp && FreeImage_GetWidth(hdr) == W && FreeImage_GetHeight(hdr) == H,
              "%s orientation %d: header-only load not %ux%u %u-bit of type %d", name, o, W, H, bpp, (int)type);
        if (cmyk && !kept) CHECK(hdr && FreeImage_GetICCProfile(hdr)->size == 0, "%s orientation %d: header-only load keeps the CMYK profile", name, o);
        if (kept) CHECK(hdr && FreeImage_GetICCProfile(hdr)->size == 132 && (FreeImage_GetICCProfile(hdr)->flags & FIICC_COLOR_IS_CMYK),
                        "%s JXR_CMYK orientation %d: header-only load without its profile marked CMYK", name, o);
        FreeImage_Unload(hdr);
        CHECK(dib && FreeImage_GetImageType(dib) == type && FreeImage_GetBPP(dib) == bpp && FreeImage_GetWidth(dib) == W && FreeImage_GetHeight(dib) == H,
              "%s orientation %d: does not load as %ux%u %u-bit of type %d", name, o, W, H, bpp, (int)type);
        if (dib && FreeImage_GetImageType(dib) == type && FreeImage_GetBPP(dib) == bpp && FreeImage_GetWidth(dib) == W && FreeImage_GetHeight(dib) == H) {
            for (y = 0; y < H; y++) {
                const BYTE *line = FreeImage_GetScanLine(dib, H - 1 - y);
                for (x = 0; x < W; x++) {
                    /* the upright pixel this one comes from: flips alone, or the quarter turn (x, y) -> (h - 1 - y, x) first */
                    unsigned sx, sy;
                    if (o < 4) {
                        sx = (o == 2 || o == 3) ? w - 1 - x : x;
                        sy = (o == 1 || o == 3) ? h - 1 - y : y;
                    } else {
                        sx = (o == 4 || o == 6) ? y : w - 1 - y;
                        sy = (o == 4 || o == 5) ? h - 1 - x : x;
                    }
                    for (c = 0; c < channels; c++) {
                        if (kept) {
                            /* CMYK samples in their own order */
                            const double v = (bpp == 64) ? ((const unsigned short *)line)[x * 4 + c] : line[x * 4 + c];
                            if (v != made_cmyk(name, sx, sy, c)) bad++;
                        } else if (loaded(dib, line, x, c) != made(name, sx, sy, c)) {
                            bad++;
                        }
                    }
                }
            }
            CHECK(bad == 0, "%s orientation %d: %u values not as made", name, o, bad);
            if (cmyk && !kept) CHECK(FreeImage_GetICCProfile(dib)->size == 0, "%s orientation %d: keeps the CMYK profile", name, o);
            if (kept) CHECK(FreeImage_GetICCProfile(dib)->size == 132 && (FreeImage_GetICCProfile(dib)->flags & FIICC_COLOR_IS_CMYK),
                            "%s JXR_CMYK orientation %d: without its profile marked CMYK", name, o);
        }
        if (cmyk && !kept) {
            FIBITMAP *shown;
            mem = FreeImage_OpenMemory(data, (DWORD)size);
            shown = FreeImage_LoadFromMemory(FIF_JXR, mem, flags | FIF_LOAD_DISPLAY_ICC);
            FreeImage_CloseMemory(mem);
            CHECK(shown && dib && same_pixels(shown, dib), "%s orientation %d: FIF_LOAD_DISPLAY_ICC does not give the pixels of a plain load", name, o);
            FreeImage_Unload(shown);
        }
        FreeImage_Unload(dib);
    }
    free((void *)ref);
    ref = NULL;
    free(data);
}

/* a file whose codestream breaks a rule mid-image: the header loads, the pixels are refused */
static void refused(const char *dir, const char *name) {
    char path[1024];
    FIBITMAP *dib, *hdr;
    snprintf(path, sizeof(path), "%s/%s.jxr", dir, name);
    dib = FreeImage_Load(FIF_JXR, path, 0);
    hdr = FreeImage_Load(FIF_JXR, path, FIF_LOAD_NOPIXELS);
    CHECK(dib == NULL, "%s: loaded", name);
    CHECK(hdr != NULL, "%s: header-only load refused", name);
    FreeImage_Unload(dib);
    FreeImage_Unload(hdr);
}

/* a format FreeImage does not load: refused, header only too */
static void unsupported(const char *dir, const char *name) {
    char path[1024];
    FIBITMAP *dib, *hdr;
    snprintf(path, sizeof(path), "%s/%s.jxr", dir, name);
    dib = FreeImage_Load(FIF_JXR, path, 0);
    hdr = FreeImage_Load(FIF_JXR, path, FIF_LOAD_NOPIXELS);
    CHECK(dib == NULL, "%s: loaded", name);
    CHECK(hdr == NULL, "%s: header-only load not refused", name);
    FreeImage_Unload(dib);
    FreeImage_Unload(hdr);
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : "data";
    DWORD srgb_size = 0;
    const void *srgb;
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);
    srgb = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_SRGB, &srgb_size);
    FreeImage_SetDisplayICCProfile(srgb, srgb_size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION);
    formats(dir, "rgb64half", 8, FIT_RGBF, 96, 0);
    formats(dir, "rgb64fixed", 8, FIT_RGBF, 96, 0);
    formats(dir, "rgba64half", 8, FIT_RGBAF, 128, 0);
    formats(dir, "rgb101010", 9, FIT_RGB16, 48, 0);
    formats(dir, "rgbe", 8, FIT_RGBF, 96, 0);
    formats(dir, "rgbf128", 8, FIT_RGBF, 96, 0);
    formats(dir, "bgr32", 9, FIT_BITMAP, 24, 0);
    formats(dir, "rgb32", 9, FIT_BITMAP, 24, 0);
    formats(dir, "rgba32", 9, FIT_BITMAP, 32, 0);
    formats(dir, "pbgra32", 9, FIT_BITMAP, 32, 0);
    formats(dir, "prgba32", 9, FIT_BITMAP, 32, 0);
    formats(dir, "prgba64", 9, FIT_RGBA16, 64, 0);
    formats(dir, "prgbaf128", 9, FIT_RGBAF, 128, 0);
    formats(dir, "cmyk32", 9, FIT_BITMAP, 24, 0);
    formats(dir, "cmyk64", 9, FIT_RGB16, 48, 0);
    formats(dir, "cmyka40", 9, FIT_BITMAP, 32, 0);
    formats(dir, "cmyka80", 9, FIT_RGBA16, 64, 0);
    formats(dir, "cmyk32", 9, FIT_BITMAP, 32, JXR_CMYK);
    formats(dir, "cmyk64", 9, FIT_RGBA16, 64, JXR_CMYK);
    formats(dir, "cmyka40", 9, FIT_BITMAP, 32, JXR_CMYK);
    formats(dir, "cmyka80", 9, FIT_RGBA16, 64, JXR_CMYK);
    formats(dir, "crop", 40, FIT_BITMAP, 32, 0);
    formats(dir, "hardtiles", 96, FIT_BITMAP, 24, 0);
    formats(dir, "ileave420", 8, FIT_BITMAP, 32, 0);
    formats(dir, "ileave16", 9, FIT_RGBA16, 64, 0);
    formats(dir, "ileave_q", 9, FIT_BITMAP, 32, 0);
    formats(dir, "lpqp", 32, FIT_BITMAP, 24, 0);
    refused(dir, "lpqp_bad");
    formats(dir, "nch3", 9, FIT_BITMAP, 24, 0);
    formats(dir, "nch3", 9, FIT_BITMAP, 24, JXR_CMYK);
    formats(dir, "nch3a", 9, FIT_BITMAP, 32, 0);
    formats(dir, "nch3ax16", 9, FIT_RGBA16, 64, 0);
    formats(dir, "nch4", 9, FIT_BITMAP, 24, 0);
    formats(dir, "nch4a", 9, FIT_BITMAP, 32, 0);
    formats(dir, "nch5a", 9, FIT_BITMAP, 32, 0);
    formats(dir, "nch8x16", 9, FIT_RGB16, 48, 0);
    formats(dir, "nch4", 9, FIT_BITMAP, 32, JXR_CMYK);
    formats(dir, "nch4a", 9, FIT_BITMAP, 32, JXR_CMYK);
    formats(dir, "nch5a", 9, FIT_BITMAP, 32, JXR_CMYK);
    formats(dir, "nch8x16", 9, FIT_RGBA16, 64, JXR_CMYK);
    unsupported(dir, "ycc444");
    printf("%d checks, %d failures\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
