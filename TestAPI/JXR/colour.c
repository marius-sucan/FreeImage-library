/* FreeImage 3 - JPEG XR colour descriptions: COLOR_SPACE and PTM_COLOR_INFO (T.832 A.7.17, A.7.21) added to the first
   IFD of lossless saves and of data/ files. Without an ICC profile in the file, PTM_COLOR_INFO's code points become the
   CICP tag and, unless they describe sRGB, PQ, HLG or nothing, an ICC profile of them; COLOR_SPACE 1 alone the CICP tag
   of sRGB. Only unsigned RGB and grey of 8, 10 and 16 bits take either, an entry of another type or count is ignored,
   and a file's own ICC profile wins. Header-only loads describe the same. Each profile converts to sRGB as an
   independent reference does, the spec's curves and primaries through RP 177 matrices and Bradford adaptation;
   FIF_LOAD_DISPLAY_ICC converts as FreeImage_ApplyICCProfile does, and leaves PQ alone */
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

typedef struct {
    BYTE *data;
    DWORD size;
} Bytes;

enum { W = 64, H = 40 };

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static unsigned get16(const BYTE *p) { return p[0] | (p[1] << 8); }
static unsigned get32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

/* an IFD entry, its value in the entry */
static void entry(BYTE e[12], unsigned tag, unsigned type, unsigned count, BYTE v0, BYTE v1, BYTE v2, BYTE v3) {
    e[0] = (BYTE)tag, e[1] = (BYTE)(tag >> 8), e[2] = (BYTE)type, e[3] = 0;
    e[4] = (BYTE)count, e[5] = (BYTE)(count >> 8), e[6] = 0, e[7] = 0;
    e[8] = v0, e[9] = v1, e[10] = v2, e[11] = v3;
}

/* PTM_COLOR_INFO: primaries, transfer, matrix, then RESERVED_K and FULL_RANGE_FLAG */
static void ptm(BYTE e[12], BYTE p, BYTE t, BYTE m, BYTE last) { entry(e, 0xbc05, 1, 4, p, t, m, last); }
static void colour_space(BYTE e[12], unsigned v) { entry(e, 0xa001, 3, 1, (BYTE)v, (BYTE)(v >> 8), 0, 0); }

/* a copy whose first IFD, with these entries added in tag order, is appended and made the first */
static Bytes with_entries(Bytes in, const BYTE (*added)[12], unsigned count) {
    const DWORD ifd = get32(in.data + 4), at = (in.size + 1) & ~1u;
    const unsigned n = get16(in.data + ifd);
    unsigned i = 0, j = 0, k = 0;
    Bytes out;
    out.size = at + 2 + 12 * (n + count) + 4;
    out.data = (BYTE *)calloc(out.size, 1);
    memcpy(out.data, in.data, in.size);
    out.data[4] = (BYTE)at, out.data[5] = (BYTE)(at >> 8), out.data[6] = (BYTE)(at >> 16), out.data[7] = (BYTE)(at >> 24);
    out.data[at] = (BYTE)(n + count), out.data[at + 1] = (BYTE)((n + count) >> 8);
    while (i < n || j < count) {
        const BYTE *e = (j == count || (i < n && get16(in.data + ifd + 2 + 12 * i) < get16(added[j]))) ? in.data + ifd + 2 + 12 * i++ : added[j++];
        memcpy(out.data + at + 2 + 12 * k++, e, 12);
    }
    return out;
}

static Bytes with1(Bytes in, const BYTE e[12]) { return with_entries(in, (const BYTE (*)[12])e, 1); }

static Bytes save(FIBITMAP *dib) {
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    Bytes b = { NULL, 0 };
    BYTE *data = NULL;
    if (FreeImage_SaveToMemory(FIF_JXR, dib, mem, JXR_LOSSLESS) && FreeImage_AcquireMemory(mem, &data, &b.size)) {
        b.data = (BYTE *)malloc(b.size);
        memcpy(b.data, data, b.size);
    }
    FreeImage_CloseMemory(mem);
    return b;
}

static Bytes read_file(const char *path) {
    Bytes b = { NULL, 0 };
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return b;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    b.data = (BYTE *)malloc(n > 0 ? n : 1);
    b.size = (n > 0 && fread(b.data, 1, n, f) == (size_t)n) ? (DWORD)n : 0;
    fclose(f);
    return b;
}

static FIBITMAP *load(Bytes b, int flags) {
    FIMEMORY *mem = FreeImage_OpenMemory(b.data, b.size);
    FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_JXR, mem, flags);
    FreeImage_CloseMemory(mem);
    return dib;
}

/* the CICP tag, or 0 */
static int cicp(FIBITMAP *dib, BYTE out[4]) {
    FITAG *tag = NULL;
    if (!dib || !FreeImage_GetMetadata(FIMD_CUSTOM, dib, "CICP", &tag) || !tag) return 0;
    if (FreeImage_GetTagType(tag) != FIDT_BYTE || FreeImage_GetTagCount(tag) != 4) return 0;
    memcpy(out, FreeImage_GetTagValue(tag), 4);
    return 1;
}

static int has_cicp(FIBITMAP *dib, BYTE p, BYTE t, BYTE m, BYTE f) {
    BYTE c[4];
    return cicp(dib, c) && c[0] == p && c[1] == t && c[2] == m && c[3] == f;
}

static int has_profile(FIBITMAP *dib) { return dib && FreeImage_GetICCProfile(dib)->data != NULL; }

static const char *description(FIBITMAP *dib) {
    static char text[256];
    FIICCPROFILE *icc = dib ? FreeImage_GetICCProfile(dib) : NULL;
    text[0] = 0;
    if (icc && icc->data) FreeImage_GetICCProfileDescription(icc->data, icc->size, text, sizeof(text));
    return text;
}

static DWORD space(FIBITMAP *dib) {
    FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
    return icc->data ? FreeImage_GetICCProfileColorSpace(icc->data, icc->size) : 0;
}

static int same_profile(FIBITMAP *a, FIBITMAP *b) {
    FIICCPROFILE *x = FreeImage_GetICCProfile(a), *y = FreeImage_GetICCProfile(b);
    if (!x->data || !y->data) return !x->data && !y->data;
    return x->size == y->size && !memcmp(x->data, y->data, x->size);
}

static int same_cicp(FIBITMAP *a, FIBITMAP *b) {
    BYTE x[4], y[4];
    const int hx = cicp(a, x), hy = cicp(b, y);
    return hx == hy && (!hx || !memcmp(x, y, 4));
}

static unsigned long long pixel_digest(FIBITMAP *dib) {
    unsigned long long h = 1469598103934665603ull;
    unsigned y, i;
    if (!dib || !FreeImage_HasPixels(dib)) return 0;
    for (y = 0; y < FreeImage_GetHeight(dib); y++) {
        const BYTE *p = FreeImage_GetScanLine(dib, y);
        for (i = 0; i < FreeImage_GetLine(dib); i++) h = (h ^ p[i]) * 1099511628211ull;
    }
    return h;
}

/* W x H, each 8- or 16-bit channel its own ramp */
static FIBITMAP *image(FREE_IMAGE_TYPE type, unsigned bpp) {
    FIBITMAP *dib = FreeImage_AllocateT(type, W, H, bpp, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
    unsigned x, y, i;
    if (!dib) return NULL;
    if (type == FIT_BITMAP && bpp <= 8) {
        RGBQUAD *pal = FreeImage_GetPalette(dib);
        const unsigned n = 1u << bpp;
        for (i = 0; i < n; i++) pal[i].rgbRed = pal[i].rgbGreen = pal[i].rgbBlue = (BYTE)(i * 255 / (n - 1));
    }
    for (y = 0; y < H; y++) {
        BYTE *line = FreeImage_GetScanLine(dib, y);
        if (type == FIT_BITMAP && bpp == 1) {
            for (i = 0; i < FreeImage_GetLine(dib); i++) line[i] = (BYTE)(0x55 << (y & 1));
        } else if (type == FIT_BITMAP) {
            for (i = 0; i < FreeImage_GetLine(dib); i++) line[i] = (BYTE)(i * 4 + y * 67 + (i % (bpp / 8)) * 41);
        } else if (type == FIT_UINT16 || type == FIT_RGB16 || type == FIT_RGBA16) {
            WORD *w = (WORD *)line;
            for (i = 0; i < FreeImage_GetLine(dib) / 2; i++) w[i] = (WORD)(i * 1021 + y * 4099 + 16411 * (i % 4));
        } else {
            float *f = (float *)line;
            for (i = 0; i < FreeImage_GetLine(dib) / 4; i++) f[i] = (float)((i * 7 + y * 3) % 101) / 100.0f;
        }
    }
    return dib;
}

/* ------------------------------------------------------------------ an independent sRGB reference */

/* T.832 Table A.9, signal to light */
static double from_signal(BYTE t, double v) {
    switch (t) {
        case 1: case 6: case 11: case 12: case 14: case 15: return (v < 0.081) ? v / 4.5 : pow((v + 0.099) / 1.099, 1 / 0.45);
        case 4: return pow(v, 2.2);
        case 5: return pow(v, 2.8);
        case 7: return (v < 0.0913) ? v / 4.0 : pow((v + 0.1115) / 1.1115, 1 / 0.45);
        case 8: return v;
        default: return (v <= 0.04045) ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
    }
}

static double to_srgb(double l) {
    if (l <= 0) return 0;
    if (l >= 1) return 1;
    return (l <= 0.0031308) ? 12.92 * l : 1.055 * pow(l, 1 / 2.4) - 0.055;
}

/* T.832 Table A.8 and ITU-T H.273: red, green, blue and white xy */
static int primaries_xy(BYTE p, double xy[8]) {
    static const struct { BYTE code; double xy[8]; } T[] = {
        { 1, { 0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290 } },
        { 4, { 0.670, 0.330, 0.210, 0.710, 0.140, 0.080, 0.310, 0.316 } },
        { 5, { 0.640, 0.330, 0.290, 0.600, 0.150, 0.060, 0.3127, 0.3290 } },
        { 6, { 0.630, 0.340, 0.310, 0.595, 0.155, 0.070, 0.3127, 0.3290 } },
        { 7, { 0.630, 0.340, 0.310, 0.595, 0.155, 0.070, 0.3127, 0.3290 } },
        { 8, { 0.681, 0.319, 0.243, 0.692, 0.145, 0.049, 0.310, 0.316 } },
        { 9, { 0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290 } },
        { 11, { 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.314, 0.351 } },
        { 12, { 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290 } },
        { 22, { 0.630, 0.340, 0.295, 0.605, 0.155, 0.077, 0.3127, 0.3290 } }
    };
    unsigned i;
    for (i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
        if (T[i].code == p) { memcpy(xy, T[i].xy, sizeof(T[i].xy)); return 1; }
    }
    return 0;
}

static void mul(const double a[3][3], const double b[3][3], double c[3][3]) {
    int i, j, k;
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) { c[i][j] = 0; for (k = 0; k < 3; k++) c[i][j] += a[i][k] * b[k][j]; }
}

static void invert(const double m[3][3], double r[3][3]) {
    const double d = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) + m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    r[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / d; r[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / d; r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / d;
    r[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / d; r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / d; r[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / d;
    r[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / d; r[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / d; r[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / d;
}

/* SMPTE RP 177: linear RGB to XYZ */
static void rgb_to_xyz(const double xy[8], double m[3][3]) {
    double p[3][3], pi[3][3], w[3], s[3];
    int i, j;
    for (i = 0; i < 3; i++) {
        const double x = xy[2 * i], y = xy[2 * i + 1];
        p[0][i] = x / y; p[1][i] = 1; p[2][i] = (1 - x - y) / y;
    }
    w[0] = xy[6] / xy[7]; w[1] = 1; w[2] = (1 - xy[6] - xy[7]) / xy[7];
    invert(p, pi);
    for (i = 0; i < 3; i++) s[i] = pi[i][0] * w[0] + pi[i][1] * w[1] + pi[i][2] * w[2];
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) m[i][j] = p[i][j] * s[j];
}

/* Bradford adaptation from one white to another, as Little CMS adapts */
static void bradford(const double from[2], const double to[2], double m[3][3]) {
    static const double B[3][3] = { { 0.8951, 0.2664, -0.1614 }, { -0.7502, 1.7135, 0.0367 }, { 0.0389, -0.0685, 1.0296 } };
    double bi[3][3], d[3][3] = { { 0 } }, t[3][3], a[3], b[3], wa[3], wb[3];
    int i;
    wa[0] = from[0] / from[1]; wa[1] = 1; wa[2] = (1 - from[0] - from[1]) / from[1];
    wb[0] = to[0] / to[1]; wb[1] = 1; wb[2] = (1 - to[0] - to[1]) / to[1];
    for (i = 0; i < 3; i++) {
        a[i] = B[i][0] * wa[0] + B[i][1] * wa[1] + B[i][2] * wa[2];
        b[i] = B[i][0] * wb[0] + B[i][1] * wb[1] + B[i][2] * wb[2];
        d[i][i] = b[i] / a[i];
    }
    invert(B, bi);
    mul(bi, d, t);
    mul(t, B, m);
}

/* linear RGB of the primaries to linear sRGB, white to white */
static void to_srgb_matrix(BYTE p, double m[3][3]) {
    double xy[8], srgb[8], src[3][3], dst[3][3], dsti[3][3], adapt[3][3], t[3][3];
    primaries_xy(p, xy);
    primaries_xy(1, srgb);
    rgb_to_xyz(xy, src);
    rgb_to_xyz(srgb, dst);
    invert(dst, dsti);
    bradford(xy + 6, srgb + 6, adapt);
    mul(adapt, src, t);
    mul(dsti, t, m);
}

/* the largest difference between a conversion to FreeImage's sRGB and the reference, in 8- or 16-bit steps */
static int reference_error(FIBITMAP *dib, BYTE p, BYTE t, int grey) {
    DWORD size = 0;
    const void *srgb = FreeImage_GetBuiltInICCProfile(grey ? FICMS_PROFILE_GRAY : FICMS_PROFILE_SRGB, &size);
    FIBITMAP *out = FreeImage_ConvertToICCProfile(dib, srgb, size, FICMS_INTENT_RELATIVE_COLORIMETRIC);
    const FREE_IMAGE_TYPE type = FreeImage_GetImageType(dib);
    const int deep = (type != FIT_BITMAP);
    const double top = deep ? 65535.0 : 255.0;
    const unsigned channels = grey ? 1 : (deep ? FreeImage_GetBPP(dib) / 16 : FreeImage_GetBPP(dib) / 8);
    double m[3][3];
    unsigned x, y;
    int worst = 0;
    if (!out) return 1 << 20;
    if (!grey) to_srgb_matrix(p, m);
    for (y = 0; y < H; y++) {
        const BYTE *s8 = FreeImage_GetScanLine(dib, y), *d8 = FreeImage_GetScanLine(out, y);
        const WORD *s16 = (const WORD *)s8, *d16 = (const WORD *)d8;
        for (x = 0; x < W; x++) {
            double in[3], lin[3];
            int c;
            for (c = 0; c < (grey ? 1 : 3); c++) {
                /* FreeImage holds 8-bit RGB as BGR */
                const unsigned k = (deep || grey) ? (unsigned)c : (unsigned)(2 - c);
                in[c] = (deep ? s16[x * channels + k] : s8[x * channels + k]) / top;
                lin[c] = from_signal(t, in[c]);
            }
            for (c = 0; c < (grey ? 1 : 3); c++) {
                const unsigned k = (deep || grey) ? (unsigned)c : (unsigned)(2 - c);
                const double l = grey ? lin[0] : m[c][0] * lin[0] + m[c][1] * lin[1] + m[c][2] * lin[2];
                const int want = (int)floor(to_srgb(l) * top + 0.5);
                const int got = deep ? d16[x * channels + k] : d8[x * channels + k];
                if (abs(want - got) > worst) worst = abs(want - got);
            }
        }
    }
    FreeImage_Unload(out);
    return worst;
}

/* ------------------------------------------------------------------ concurrent loads */

typedef struct {
    Bytes file;
    FIBITMAP *want;
    int differences;
} Job;

static void *load_many(void *arg) {
    Job *job = (Job *)arg;
    int i;
    for (i = 0; i < 20; i++) {
        FIBITMAP *dib = load(job->file, 0);
        if (!dib || !same_profile(dib, job->want) || !same_cicp(dib, job->want)) job->differences++;
        if (dib) FreeImage_Unload(dib);
    }
    return NULL;
}

/* ------------------------------------------------------------------ FIF_LOAD_DISPLAY_ICC */

/* the flag's load is the load without it, converted by FreeImage_ApplyICCProfile; header-only loads ignore the flag */
static void display_check(const char *name, Bytes file, const void *display, DWORD display_size, const void *grey, DWORD grey_size) {
    FIBITMAP *plain = load(file, 0), *flag = load(file, FIF_LOAD_DISPLAY_ICC), *want = plain ? FreeImage_Clone(plain) : NULL;
    FIBITMAP *head = load(file, FIF_LOAD_NOPIXELS), *head_flag = load(file, FIF_LOAD_NOPIXELS | FIF_LOAD_DISPLAY_ICC);
    BYTE c[4];
    CHECK(plain && flag && want && head && head_flag, "%s: not loaded", name);
    if (plain && flag && want) {
        const int is_grey = (FreeImage_GetColorType(plain) == FIC_MINISBLACK);
        const int pq = cicp(plain, c) && c[1] == 16;
        if (!pq) {
            CHECK(FreeImage_ApplyICCProfile(want, is_grey ? grey : display, is_grey ? grey_size : display_size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION), "%s: no public conversion", name);
        }
        CHECK(pixel_digest(flag) == pixel_digest(want), "%s: other pixels than FreeImage_ApplyICCProfile's", name);
        CHECK(same_profile(flag, want), "%s: other profile than FreeImage_ApplyICCProfile's", name);
        CHECK(same_cicp(flag, want), "%s: other CICP tag than FreeImage_ApplyICCProfile's", name);
        if (pq) {
            CHECK(pixel_digest(flag) == pixel_digest(plain) && same_cicp(flag, plain) && FreeImage_MustTonemap(flag, FIF_JXR) == FITM_PQ, "%s: PQ converted", name);
        }
    }
    if (head && head_flag) {
        CHECK(same_profile(head, head_flag) && same_cicp(head, head_flag), "%s: the flag changed a header-only load", name);
    }
    if (plain) FreeImage_Unload(plain);
    if (flag) FreeImage_Unload(flag);
    if (want) FreeImage_Unload(want);
    if (head) FreeImage_Unload(head);
    if (head_flag) FreeImage_Unload(head_flag);
}

/* ------------------------------------------------------------------ the cases */

/* narrow range samples, black at 16 and white at 235 times 2^(bits - 8), in the full range, alpha left alone */
static FIBITMAP *expanded(FIBITMAP *src) {
    FIBITMAP *dst = FreeImage_Clone(src);
    const int deep = (FreeImage_GetImageType(dst) != FIT_BITMAP);
    const unsigned step = FreeImage_GetBPP(dst) / (deep ? 16 : 8), colors = (step > 3) ? 3 : step;
    unsigned x, y, c;
    for (y = 0; y < FreeImage_GetHeight(dst); y++) {
        BYTE *p8 = FreeImage_GetScanLine(dst, y);
        WORD *p16 = (WORD *)p8;
        for (x = 0; x < FreeImage_GetWidth(dst); x++) {
            for (c = 0; c < colors; c++) {
                if (deep) {
                    const double v = floor((p16[x * step + c] - 4096.0) * 65535.0 / 56064.0 + 0.5);
                    p16[x * step + c] = (WORD)(v < 0 ? 0 : v > 65535 ? 65535 : v);
                } else {
                    const double v = floor((p8[x * step + c] - 16.0) * 255.0 / 219.0 + 0.5);
                    p8[x * step + c] = (BYTE)(v < 0 ? 0 : v > 255 ? 255 : v);
                }
            }
        }
    }
    return dst;
}

/* loads with these entries: full and header-only describe the same; the pixels are the untagged file's, or with
   expand, those expanded from the narrow range */
static FIBITMAP *loaded_as(const char *name, Bytes base, const BYTE (*added)[12], unsigned count, int expand, Bytes *file) {
    Bytes tagged;
    FIBITMAP *plain, *dib, *head;
    if (file) file->data = NULL;
    if (!base.data) {
        CHECK(0, "%s: no file", name);
        return NULL;
    }
    tagged = with_entries(base, added, count);
    plain = load(base, 0), dib = load(tagged, 0), head = load(tagged, FIF_LOAD_NOPIXELS);
    CHECK(plain && dib && head, "%s: not loaded", name);
    if (plain && dib) {
        FIBITMAP *want = expand ? expanded(plain) : plain;
        CHECK(pixel_digest(want) == pixel_digest(dib), "%s: other pixels", name);
        if (want != plain) FreeImage_Unload(want);
    }
    if (dib && head) CHECK(same_profile(dib, head) && same_cicp(dib, head), "%s: the header-only load differs", name);
    if (plain) FreeImage_Unload(plain);
    if (head) FreeImage_Unload(head);
    if (file) *file = tagged; else free(tagged.data);
    return dib;
}

static FIBITMAP *loaded(const char *name, Bytes base, const BYTE (*added)[12], unsigned count, Bytes *file) {
    return loaded_as(name, base, added, count, 0, file);
}

/* PTM_COLOR_INFO p, t, m and the narrow range: the CICP tag and the pixels, expanded or not */
static void narrow(const char *name, Bytes base, BYTE p, BYTE t, BYTE m, int expand) {
    BYTE e[12];
    FIBITMAP *dib;
    ptm(e, p, t, m, 0);
    dib = loaded_as(name, base, (const BYTE (*)[12])e, 1, expand, NULL);
    CHECK(dib && has_cicp(dib, p, t, m, 0), "%s: CICP tag", name);
    if (dib) FreeImage_Unload(dib);
}

static FIBITMAP *loaded1(const char *name, Bytes base, const BYTE e[12], Bytes *file) {
    return loaded(name, base, (const BYTE (*)[12])e, 1, file);
}

/* PTM_COLOR_INFO p, t, m, full range: the CICP tag, and a profile with this description, or none */
static void described(const char *name, Bytes base, BYTE p, BYTE t, BYTE m, const char *expected) {
    BYTE e[12];
    FIBITMAP *dib;
    ptm(e, p, t, m, 1);
    dib = loaded1(name, base, e, NULL);
    if (!dib) return;
    CHECK(has_cicp(dib, p, t, m, 1), "%s: CICP tag", name);
    if (expected) {
        CHECK(!strcmp(description(dib), expected), "%s: profile \"%s\", not \"%s\"", name, description(dib), expected);
    } else {
        CHECK(!has_profile(dib), "%s: a profile, \"%s\"", name, description(dib));
    }
    FreeImage_Unload(dib);
}

/* nothing attached: no CICP tag, no profile */
static void untouched(const char *name, Bytes base, const BYTE (*added)[12], unsigned count) {
    FIBITMAP *plain = load(base, 0), *dib = loaded(name, base, added, count, NULL);
    if (plain && dib) {
        CHECK(!has_cicp(dib, 9, 1, 0, 1) && same_cicp(dib, plain) && same_profile(dib, plain), "%s: described", name);
    }
    if (plain) FreeImage_Unload(plain);
    if (dib) FreeImage_Unload(dib);
}

int main(void) {
    static const BYTE TRANSFERS[] = { 1, 4, 5, 6, 7, 8, 11, 12, 13, 14, 15 };
    static const BYTE PRIMARIES[] = { 1, 4, 5, 6, 7, 8, 9, 11, 12, 22 };
    FIBITMAP *rgb24, *rgba32, *grey8, *rgb16, *grey16, *rgba16, *rgbf, *bw, *rgb555, *dib;
    Bytes f_rgb24, f_rgba32, f_grey8, f_rgb16, f_grey16, f_rgba16, f_rgbf, f_bw, f_rgb555, f_adobe, tagged;
    BYTE e[12], two[2][12];
    char name[160];
    unsigned i;
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);

    rgb24 = image(FIT_BITMAP, 24);
    rgba32 = image(FIT_BITMAP, 32);
    grey8 = image(FIT_BITMAP, 8);
    rgb16 = image(FIT_RGB16, 48);
    grey16 = image(FIT_UINT16, 16);
    rgba16 = image(FIT_RGBA16, 64);
    rgbf = image(FIT_RGBF, 96);
    bw = image(FIT_BITMAP, 1);
    rgb555 = FreeImage_ConvertTo16Bits555(rgb24);
    f_rgb24 = save(rgb24), f_rgba32 = save(rgba32), f_grey8 = save(grey8), f_rgb16 = save(rgb16), f_grey16 = save(grey16);
    f_rgba16 = save(rgba16), f_rgbf = save(rgbf), f_bw = save(bw), f_rgb555 = save(rgb555);
    {
        DWORD size = 0;
        const void *adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &size);
        FIBITMAP *tagged_dib = FreeImage_Clone(rgb24);
        FreeImage_CreateICCProfile(tagged_dib, (void *)adobe, (long)size);
        f_adobe = save(tagged_dib);
        FreeImage_Unload(tagged_dib);
    }
    CHECK(FreeImage_GetColorType(grey8) == FIC_MINISBLACK, "the grey image is not FIC_MINISBLACK");
    CHECK(f_rgb24.data && f_rgba32.data && f_grey8.data && f_rgb16.data && f_grey16.data && f_rgba16.data && f_rgbf.data && f_bw.data && f_rgb555.data && f_adobe.data, "not saved");

    /* PTM_COLOR_INFO: the CICP tag, and a profile unless sRGB, PQ, HLG or unspecified */
    described("RGB 9/1", f_rgb24, 9, 1, 0, "BT.2020 primaries, BT.709 tone curve (FreeImage)");
    described("RGBA 12/13", f_rgba32, 12, 13, 0, "Display P3 primaries, sRGB tone curve (FreeImage)");
    described("RGB 2/8", f_rgb24, 2, 8, 2, "BT.709 primaries, linear tone curve (FreeImage)");
    described("RGB 9/2", f_rgb24, 9, 2, 0, "BT.2020 primaries, sRGB tone curve (FreeImage)");
    described("RGB 1/13", f_rgb24, 1, 13, 0, NULL);
    described("RGB 2/13", f_rgb24, 2, 13, 0, NULL);
    described("RGB 3/2 reserved", f_rgb24, 3, 2, 0, NULL);
    described("RGB16 9/16 PQ", f_rgb16, 9, 16, 0, NULL);
    described("RGB16 9/18 HLG", f_rgb16, 9, 18, 0, NULL);
    described("RGB16 1/9 logarithmic", f_rgb16, 1, 9, 0, NULL);
    described("RGB16 5/6", f_rgb16, 5, 6, 0, "BT.601 625-line primaries, BT.601 tone curve (FreeImage)");
    described("RGBA16 7/7", f_rgba16, 7, 7, 0, "SMPTE 240M primaries, SMPTE 240M tone curve (FreeImage)");
    described("grey 1/1", f_grey8, 1, 1, 1, "Gray, BT.709 tone curve (FreeImage)");
    described("grey 9/13", f_grey8, 9, 13, 9, NULL);
    described("grey16 5/8", f_grey16, 5, 8, 5, "Gray, linear tone curve (FreeImage)");
    described("grey16 9/16 PQ", f_grey16, 9, 16, 9, NULL);
    {
        Bytes b = read_file("data/rgb101010.jxr");
        CHECK(b.data != NULL, "data/rgb101010.jxr not read");
        if (b.data) described("32bppRGB101010 9/1", b, 9, 1, 0, "BT.2020 primaries, BT.709 tone curve (FreeImage)");
        free(b.data);
    }
    /* the profiles: a grey one, an RGB one with its code points */
    ptm(e, 1, 1, 1, 1);
    dib = loaded1("grey 1/1", f_grey8, e, NULL);
    if (dib) CHECK(space(dib) == FICMS_COLORSPACE_GRAY, "grey 1/1: not a grey profile");
    if (dib) FreeImage_Unload(dib);
    ptm(e, 9, 1, 0, 1);
    dib = loaded1("RGB 9/1", f_rgb24, e, NULL);
    if (dib) {
        FIBITMAP *linear;
        CHECK(space(dib) == FICMS_COLORSPACE_RGB, "RGB 9/1: not an RGB profile");
        /* without the CICP tag FreeImage_ConvertToLinear reads the profile's code points */
        FreeImage_SetMetadata(FIMD_CUSTOM, dib, "CICP", NULL);
        linear = FreeImage_ConvertToLinear(dib, 0);
        CHECK(linear && has_cicp(linear, 9, 8, 0, 1), "RGB 9/1: the profile's cicp tag not read");
        if (linear) FreeImage_Unload(linear);
        FreeImage_Unload(dib);
    }

    /* the RESERVED_K bits are ignored, FULL_RANGE_FLAG is the lowest */
    ptm(e, 9, 1, 0, 0xFF);
    dib = loaded1("RESERVED_K", f_rgb24, e, NULL);
    CHECK(dib && has_cicp(dib, 9, 1, 0, 1), "RESERVED_K: CICP tag");
    if (dib) FreeImage_Unload(dib);
    ptm(e, 9, 1, 0, 0xFE);
    dib = loaded_as("RESERVED_K, narrow", f_rgb24, (const BYTE (*)[12])e, 1, 1, NULL);
    CHECK(dib && has_cicp(dib, 9, 1, 0, 0), "RESERVED_K, narrow: CICP tag");
    if (dib) FreeImage_Unload(dib);

    /* FULL_RANGE_FLAG 0: RGB by matrix 0 and grey by a luma matrix are expanded, alpha not (A.7.21.5) */
    narrow("narrow RGB 9/1/0", f_rgb24, 9, 1, 0, 1);
    narrow("narrow RGBA 1/13/0", f_rgba32, 1, 13, 0, 1);
    narrow("narrow RGB16 1/1/0", f_rgb16, 1, 1, 0, 1);
    narrow("narrow RGBA16 9/16/0", f_rgba16, 9, 16, 0, 1);
    narrow("narrow grey 1/1/1", f_grey8, 1, 1, 1, 1);
    narrow("narrow grey 1/13/8", f_grey8, 1, 13, 8, 1);
    narrow("narrow grey16 9/18/9", f_grey16, 9, 18, 9, 1);
    narrow("narrow grey16 9/1/14", f_grey16, 9, 1, 14, 1);
    {
        Bytes b = read_file("data/rgb101010.jxr");
        if (b.data) narrow("narrow 32bppRGB101010 9/1/0", b, 9, 1, 0, 1);
        free(b.data);
    }
    /* no range: an unspecified, reserved or non-conformant matrix */
    narrow("narrow RGB 9/1/2", f_rgb24, 9, 1, 2, 0);
    narrow("narrow RGB 9/1/3", f_rgb24, 9, 1, 3, 0);
    narrow("narrow RGB 9/1/1", f_rgb24, 9, 1, 1, 0);
    narrow("narrow RGB16 9/1/9", f_rgb16, 9, 1, 9, 0);
    narrow("narrow grey 1/1/0", f_grey8, 1, 1, 0, 0);
    narrow("narrow grey 1/1/2", f_grey8, 1, 1, 2, 0);
    narrow("narrow grey16 1/1/11", f_grey16, 1, 1, 11, 0);
    /* nor with the file's ICC profile */
    ptm(e, 9, 1, 0, 0);
    dib = loaded1("narrow, with an ICC profile", f_adobe, e, NULL);
    CHECK(dib && !has_cicp(dib, 9, 1, 0, 0), "narrow, with an ICC profile: CICP tag");
    if (dib) FreeImage_Unload(dib);

    /* entries of another type or count are ignored (Table A.4) */
    entry(e, 0xbc05, 7, 4, 9, 1, 0, 1);
    untouched("PTM_COLOR_INFO as UNDEFINED", f_rgb24, (const BYTE (*)[12])e, 1);
    entry(e, 0xbc05, 1, 3, 9, 1, 0, 0);
    untouched("PTM_COLOR_INFO of 3 bytes", f_rgb24, (const BYTE (*)[12])e, 1);
    entry(e, 0xa001, 4, 1, 1, 0, 0, 0);
    untouched("COLOR_SPACE as LONG", f_rgb24, (const BYTE (*)[12])e, 1);
    entry(e, 0xa001, 3, 2, 1, 0, 1, 0);
    untouched("COLOR_SPACE of 2", f_rgb24, (const BYTE (*)[12])e, 1);

    /* the conformance suite's tags: everything unspecified */
    colour_space(two[0], 0xFFFF);
    ptm(two[1], 2, 2, 2, 0);
    untouched("COLOR_SPACE 0xFFFF, PTM_COLOR_INFO 2/2/2", f_rgb24, (const BYTE (*)[12])two, 2);

    /* COLOR_SPACE: 1 says sRGB, any other value nothing (A.7.17); PTM_COLOR_INFO comes first */
    colour_space(e, 1);
    dib = loaded1("COLOR_SPACE 1", f_rgb24, e, NULL);
    CHECK(dib && has_cicp(dib, 1, 13, 0, 1) && !has_profile(dib), "COLOR_SPACE 1: not sRGB");
    if (dib) FreeImage_Unload(dib);
    dib = loaded1("grey, COLOR_SPACE 1", f_grey8, e, NULL);
    CHECK(dib && has_cicp(dib, 1, 13, 0, 1) && !has_profile(dib), "grey, COLOR_SPACE 1: not sRGB");
    if (dib) FreeImage_Unload(dib);
    colour_space(e, 0xFFFF);
    untouched("COLOR_SPACE 0xFFFF", f_rgb24, (const BYTE (*)[12])e, 1);
    colour_space(e, 0);
    untouched("COLOR_SPACE 0", f_rgb24, (const BYTE (*)[12])e, 1);
    colour_space(e, 2);
    untouched("COLOR_SPACE 2", f_rgb24, (const BYTE (*)[12])e, 1);
    colour_space(two[0], 1);
    ptm(two[1], 9, 1, 0, 1);
    dib = loaded("COLOR_SPACE 1, PTM_COLOR_INFO 9/1", f_rgb24, (const BYTE (*)[12])two, 2, NULL);
    CHECK(dib && has_cicp(dib, 9, 1, 0, 1) && has_profile(dib), "COLOR_SPACE 1, PTM_COLOR_INFO 9/1: not PTM_COLOR_INFO's");
    if (dib) FreeImage_Unload(dib);

    /* the file's ICC profile wins */
    dib = loaded("ICC profile, COLOR_SPACE 1, PTM_COLOR_INFO 9/1", f_adobe, (const BYTE (*)[12])two, 2, NULL);
    if (dib) {
        DWORD size = 0;
        const void *adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &size);
        FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
        BYTE c[4];
        CHECK(icc->data && icc->size == size && !memcmp(icc->data, adobe, size) && !cicp(dib, c), "ICC profile, COLOR_SPACE 1, PTM_COLOR_INFO 9/1: not the file's profile alone");
        FreeImage_Unload(dib);
    }

    /* formats neither may describe */
    colour_space(two[0], 1);
    ptm(two[1], 9, 1, 0, 0);
    untouched("RGBF", f_rgbf, (const BYTE (*)[12])two, 2);
    untouched("BlackWhite", f_bw, (const BYTE (*)[12])two, 2);
    untouched("16bppRGB555", f_rgb555, (const BYTE (*)[12])two, 2);
    {
        static const char *FILES[] = { "data/rgbe.jxr", "data/rgb64half.jxr", "data/rgb64fixed.jxr", "data/rgbf128.jxr", "data/cmyk32.jxr", "data/nch3.jxr" };
        for (i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++) {
            Bytes b = read_file(FILES[i]);
            CHECK(b.data != NULL, "%s not read", FILES[i]);
            if (b.data) untouched(FILES[i], b, (const BYTE (*)[12])two, 2);
            free(b.data);
        }
    }

    /* each profile against the reference: every primaries with the BT.709 curve, every curve with BT.2020 primaries */
    for (i = 0; i < sizeof(PRIMARIES); i++) {
        int err;
        ptm(e, PRIMARIES[i], 1, 0, 1);
        snprintf(name, sizeof(name), "RGB %u/1 against the reference", PRIMARIES[i]);
        dib = loaded1(name, f_rgb24, e, NULL);
        if (!dib) continue;
        CHECK(has_profile(dib), "%s: no profile", name);
        err = reference_error(dib, PRIMARIES[i], 1, 0);
        CHECK(err <= 1, "%s: %d steps off", name, err);
        FreeImage_Unload(dib);
    }
    for (i = 0; i < sizeof(TRANSFERS); i++) {
        int err;
        ptm(e, 9, TRANSFERS[i], 0, 1);
        snprintf(name, sizeof(name), "RGB 9/%u against the reference", TRANSFERS[i]);
        dib = loaded1(name, f_rgb24, e, NULL);
        if (!dib) continue;
        err = reference_error(dib, 9, TRANSFERS[i], 0);
        CHECK(err <= 1, "%s: %d steps off", name, err);
        FreeImage_Unload(dib);
        /* 16 bits: within 1/8 of an 8-bit step, what the profile's s15Fixed16 colorants leave next to black */
        snprintf(name, sizeof(name), "RGB16 9/%u against the reference", TRANSFERS[i]);
        dib = loaded1(name, f_rgb16, e, NULL);
        if (!dib) continue;
        err = reference_error(dib, 9, TRANSFERS[i], 0);
        CHECK(err <= 32, "%s: %d steps off", name, err);
        FreeImage_Unload(dib);
        /* grey at 8 and 16 bits */
        if (TRANSFERS[i] != 13) {
            int deep;
            for (deep = 0; deep < 2; deep++) {
                ptm(e, 1, TRANSFERS[i], 1, 1);
                snprintf(name, sizeof(name), "grey%s 1/%u against the reference", deep ? "16" : "", TRANSFERS[i]);
                dib = loaded1(name, deep ? f_grey16 : f_grey8, e, NULL);
                if (!dib) continue;
                CHECK(has_profile(dib) && space(dib) == FICMS_COLORSPACE_GRAY, "%s: no grey profile", name);
                err = reference_error(dib, 1, TRANSFERS[i], 1);
                CHECK(err <= (deep ? 32 : 1), "%s: %d steps off", name, err);
                FreeImage_Unload(dib);
            }
        }
    }

    /* a profile travels with a save */
    ptm(e, 9, 1, 0, 1);
    dib = loaded1("RGB 9/1 saved", f_rgb24, e, NULL);
    if (dib) {
        Bytes again = save(dib);
        FIBITMAP *back = again.data ? load(again, 0) : NULL;
        CHECK(back && same_profile(back, dib), "RGB 9/1 saved: the profile did not come back");
        if (back) FreeImage_Unload(back);
        free(again.data);
        FreeImage_Unload(dib);
    }

    /* the same profile on eight threads at once */
    ptm(e, 4, 5, 0, 1);
    dib = loaded1("RGB 4/5 on eight threads", f_rgb24, e, &tagged);
    if (dib) {
        pthread_t threads[8];
        Job jobs[8];
        int differences = 0;
        for (i = 0; i < 8; i++) {
            jobs[i].file = tagged, jobs[i].want = dib, jobs[i].differences = 0;
            pthread_create(&threads[i], NULL, load_many, &jobs[i]);
        }
        for (i = 0; i < 8; i++) {
            pthread_join(threads[i], NULL);
            differences += jobs[i].differences;
        }
        CHECK(has_profile(dib) && differences == 0, "RGB 4/5 on eight threads: %d loads differ", differences);
        FreeImage_Unload(dib);
    }
    free(tagged.data);

    /* FIF_LOAD_DISPLAY_ICC on an sRGB and a Display P3 display */
    {
        static const int DISPLAYS[2] = { FICMS_PROFILE_SRGB, FICMS_PROFILE_DISPLAY_P3 };
        int d;
        for (d = 0; d < 2; d++) {
            DWORD size = 0, grey_size = 0;
            const void *display = FreeImage_GetBuiltInICCProfile(DISPLAYS[d], &size);
            void *grey = NULL;
            FIBITMAP *g;
            CHECK(FreeImage_SetDisplayICCProfile(display, size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION), "display %d: not accepted", d);
            /* the display's grey: what an untagged grey image gets */
            g = load(f_grey8, FIF_LOAD_DISPLAY_ICC);
            if (g && FreeImage_GetICCProfile(g)->data) {
                grey_size = FreeImage_GetICCProfile(g)->size;
                grey = malloc(grey_size);
                memcpy(grey, FreeImage_GetICCProfile(g)->data, grey_size);
            }
            if (g) FreeImage_Unload(g);
            CHECK(grey != NULL, "display %d: no grey profile", d);
            {
                static const struct { const char *name; int file; BYTE p, t, m, last; } CASES[] = {
                    { "RGB 9/1", 0, 9, 1, 0, 1 }, { "RGBA 12/13", 1, 12, 13, 0, 1 }, { "RGB16 9/16 PQ", 2, 9, 16, 0, 1 },
                    { "RGB16 9/18 HLG", 2, 9, 18, 0, 1 }, { "RGB16 5/6", 2, 5, 6, 0, 1 }, { "RGBA16 7/7", 3, 7, 7, 0, 1 },
                    { "grey 1/1", 4, 1, 1, 1, 1 }, { "grey16 5/8", 5, 5, 8, 5, 1 }, { "RGB 1/13", 0, 1, 13, 0, 1 },
                    { "narrow RGB 9/1", 0, 9, 1, 0, 0 }, { "narrow grey16 1/1", 5, 1, 1, 1, 0 }
                };
                const Bytes *files[6] = { &f_rgb24, &f_rgba32, &f_rgb16, &f_rgba16, &f_grey8, &f_grey16 };
                for (i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
                    Bytes file;
                    ptm(e, CASES[i].p, CASES[i].t, CASES[i].m, CASES[i].last);
                    file = with1(*files[CASES[i].file], e);
                    snprintf(name, sizeof(name), "%s display, %s", d ? "Display P3" : "sRGB", CASES[i].name);
                    display_check(name, file, display, size, grey, grey_size);
                    free(file.data);
                }
                colour_space(e, 1);
                tagged = with1(f_rgb24, e);
                snprintf(name, sizeof(name), "%s display, COLOR_SPACE 1", d ? "Display P3" : "sRGB");
                display_check(name, tagged, display, size, grey, grey_size);
                free(tagged.data);
            }
            free(grey);
        }
        FreeImage_SetDisplayICCProfile(NULL, 0, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION);
    }

    FreeImage_Unload(rgb24); FreeImage_Unload(rgba32); FreeImage_Unload(grey8); FreeImage_Unload(rgb16);
    FreeImage_Unload(grey16); FreeImage_Unload(rgba16); FreeImage_Unload(rgbf); FreeImage_Unload(bw); FreeImage_Unload(rgb555);
    free(f_rgb24.data); free(f_rgba32.data); free(f_grey8.data); free(f_rgb16.data); free(f_grey16.data);
    free(f_rgba16.data); free(f_rgbf.data); free(f_bw.data); free(f_rgb555.data); free(f_adobe.data);

    printf("%d checks, %d failures\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
