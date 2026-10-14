/* helpers shared by the color management tests */
#ifndef ICC_TEST_COMMON_H
#define ICC_TEST_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <math.h>
#include "FreeImage.h"
#include "../../Source/LibLCMS2/include/lcms2.h"

static int failures = 0;
static int checks = 0;

static void fail(const char *fmt, ...) {
    va_list ap;
    printf("  FAIL ");
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
    failures++;
}

#define CHECK(cond, ...) do { checks++; if (!(cond)) fail(__VA_ARGS__); } while (0)

/* the last message FreeImage sent */
static char last_message[512];
static int message_count = 0;

static void on_message(FREE_IMAGE_FORMAT fif, const char *msg) {
    (void)fif;
    snprintf(last_message, sizeof(last_message), "%s", msg);
    message_count++;
}

static void clear_messages(void) {
    last_message[0] = 0;
    message_count = 0;
}

static unsigned long long fnv(unsigned long long h, const void *data, size_t size) {
    const unsigned char *p = (const unsigned char *)data;
    size_t i;
    for (i = 0; i < size; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

/* pixels, palette, profile and CMYK flag */
static unsigned long long digest(FIBITMAP *dib) {
    unsigned long long h = 1469598103934665603ULL;
    unsigned w = FreeImage_GetWidth(dib), ht = FreeImage_GetHeight(dib), y;
    unsigned hdr[4];
    FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
    hdr[0] = w; hdr[1] = ht; hdr[2] = FreeImage_GetBPP(dib); hdr[3] = FreeImage_GetImageType(dib);
    h = fnv(h, hdr, sizeof(hdr));
    for (y = 0; y < ht; y++) h = fnv(h, FreeImage_GetScanLine(dib, y), FreeImage_GetLine(dib));
    if (FreeImage_GetPalette(dib)) h = fnv(h, FreeImage_GetPalette(dib), FreeImage_GetColorsUsed(dib) * sizeof(RGBQUAD));
    if (icc->data) h = fnv(h, icc->data, icc->size);
    h = fnv(h, &icc->flags, sizeof(icc->flags));
    return h;
}

static unsigned rng_state = 2463534242u;
static unsigned rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

typedef struct {
    BYTE *data;
    DWORD size;
} Bytes;

static Bytes builtin(int which) {
    Bytes b;
    b.data = (BYTE *)FreeImage_GetBuiltInICCProfile(which, &b.size);
    return b;
}

static Bytes save_profile(cmsHPROFILE h) {
    Bytes b = { NULL, 0 };
    cmsUInt32Number n = 0;
    if (h && cmsSaveProfileToMem(h, NULL, &n) && n) {
        b.data = (BYTE *)malloc(n);
        if (b.data && cmsSaveProfileToMem(h, b.data, &n)) b.size = n;
        /* Little CMS stamps the time: the same bytes on every run */
        if (b.size >= 128) memset(b.data + 24, 0, 12);
    }
    return b;
}

static Bytes read_file(const char *path) {
    Bytes b = { NULL, 0 };
    FILE *f = fopen(path, "rb");
    long n;
    if (!f) return b;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    b.data = (BYTE *)malloc(n > 0 ? n : 1);
    if (b.data && fread(b.data, 1, n, f) == (size_t)n) b.size = (DWORD)n;
    fclose(f);
    return b;
}

/* a grey profile with a pure gamma */
static Bytes make_gray_profile(double gamma) {
    cmsToneCurve *curve = cmsBuildGamma(NULL, gamma);
    cmsHPROFILE h = cmsCreateGrayProfile(cmsD50_xyY(), curve);
    Bytes b = save_profile(h);
    cmsFreeToneCurve(curve);
    cmsCloseProfile(h);
    return b;
}

/* a printer profile: CMYK -> sRGB with dot gain, a raised black and a grey paper; 60% grey component replacement back */
#define PRESS_BLACK 0.12
#define PRESS_RANGE 0.85
typedef struct {
    cmsHTRANSFORM rgb_to_lab;
    cmsHTRANSFORM lab_to_rgb;
} PressModel;

static double gain(double x) { return 1.0 - pow(1.0 - x, 1.3); }
static double ungain(double x) { return 1.0 - pow(1.0 - x, 1.0 / 1.3); }

static cmsInt32Number press_forward(const cmsUInt16Number in[], cmsUInt16Number out[], void *cargo) {
    const PressModel *m = (const PressModel *)cargo;
    double k = gain(in[3] / 65535.0), rgb[3];
    int i;
    for (i = 0; i < 3; i++) rgb[i] = PRESS_BLACK + PRESS_RANGE * (1.0 - gain(in[i] / 65535.0)) * (1.0 - k);
    cmsDoTransform(m->rgb_to_lab, rgb, out, 1);
    return TRUE;
}

static cmsInt32Number press_reverse(const cmsUInt16Number in[], cmsUInt16Number out[], void *cargo) {
    const PressModel *m = (const PressModel *)cargo;
    double rgb[3], cmy[3], k;
    int i;
    cmsDoTransform(m->lab_to_rgb, in, rgb, 1);
    for (i = 0; i < 3; i++) {
        double v = (rgb[i] - PRESS_BLACK) / PRESS_RANGE;
        cmy[i] = 1.0 - (v < 0 ? 0 : v > 1 ? 1 : v);
    }
    k = 0.6 * fmin(cmy[0], fmin(cmy[1], cmy[2]));
    for (i = 0; i < 3; i++) {
        double v = (k < 1.0) ? (cmy[i] - k) / (1.0 - k) : 0.0;
        out[i] = (cmsUInt16Number)floor(ungain(v) * 65535.0 + 0.5);
    }
    out[3] = (cmsUInt16Number)floor(ungain(k) * 65535.0 + 0.5);
    return TRUE;
}

static Bytes make_press_profile(void) {
    Bytes b = { NULL, 0 };
    PressModel m;
    cmsHPROFILE srgb = cmsCreate_sRGBProfile(), lab = cmsCreateLab4Profile(NULL), h;
    cmsPipeline *a2b = cmsPipelineAlloc(NULL, 4, 3), *b2a = cmsPipelineAlloc(NULL, 3, 4);
    cmsStage *clut;
    cmsMLU *mlu = cmsMLUalloc(NULL, 1);

    m.rgb_to_lab = cmsCreateTransform(srgb, TYPE_RGB_DBL, lab, TYPE_Lab_16, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE);
    m.lab_to_rgb = cmsCreateTransform(lab, TYPE_Lab_16, srgb, TYPE_RGB_DBL, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE);

    clut = cmsStageAllocCLut16bit(NULL, 9, 4, 3, NULL);
    cmsStageSampleCLut16bit(clut, press_forward, &m, 0);
    cmsPipelineInsertStage(a2b, cmsAT_END, clut);
    clut = cmsStageAllocCLut16bit(NULL, 17, 3, 4, NULL);
    cmsStageSampleCLut16bit(clut, press_reverse, &m, 0);
    cmsPipelineInsertStage(b2a, cmsAT_END, clut);

    h = cmsCreateProfilePlaceholder(NULL);
    cmsSetProfileVersion(h, 2.1);
    cmsSetDeviceClass(h, cmsSigOutputClass);
    cmsSetColorSpace(h, cmsSigCmykData);
    cmsSetPCS(h, cmsSigLabData);
    cmsMLUsetASCII(mlu, "en", "US", "Test press, CMYK");
    cmsWriteTag(h, cmsSigProfileDescriptionTag, mlu);
    cmsWriteTag(h, cmsSigMediaWhitePointTag, cmsD50_XYZ());
    cmsWriteTag(h, cmsSigAToB0Tag, a2b);
    cmsWriteTag(h, cmsSigBToA0Tag, b2a);
    b = save_profile(h);

    cmsMLUfree(mlu);
    cmsCloseProfile(h);
    cmsPipelineFree(a2b);
    cmsPipelineFree(b2a);
    cmsDeleteTransform(m.rgb_to_lab);
    cmsDeleteTransform(m.lab_to_rgb);
    cmsCloseProfile(srgb);
    cmsCloseProfile(lab);
    return b;
}

/* same size, type and pixels; palette and profile aside */
static int same_plane(FIBITMAP *a, FIBITMAP *b) {
    unsigned y;
    if (!a || !b || FreeImage_GetImageType(a) != FreeImage_GetImageType(b) || FreeImage_GetBPP(a) != FreeImage_GetBPP(b) ||
        FreeImage_GetWidth(a) != FreeImage_GetWidth(b) || FreeImage_GetHeight(a) != FreeImage_GetHeight(b)) return 0;
    for (y = 0; y < FreeImage_GetHeight(a); y++) {
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), FreeImage_GetLine(a))) return 0;
    }
    return 1;
}

static BYTE *psd_be(BYTE *p, unsigned v, int n) {
    while (n--) *p++ = (BYTE)(v >> (8 * n));
    return p;
}

/* a layer and mask section with one empty layer of colours + 1 channels, signals as make_cmyka_psd's; NULL p: its size */
static DWORD psd_layer_section(BYTE *p, int signal, int colours) {
    const unsigned n = colours + 1, info = 2 + 16 + 2 + 6 * n + 12 + 4 + 12 + 2 * n;
    const DWORD body = (signal == 1 || signal == 3) ? 4 + info + 4 : (signal == 2) ? 8 + 12 + 12 + info :
        (signal == 4) ? 8 + 12 + info : (signal == 5) ? 40 : 0;
    unsigned c;
    if (!p) return 4 + body;
    memset(p, 0, 4 + body);
    p = psd_be(p, body, 4);
    if (signal == 0) return 4;
    if (signal == 5) {
        memcpy(p + 8, "8BIMzzzz", 8); psd_be(p + 16, 5, 4); memcpy(p + 28, "8BIMMtrn", 8);
        return 4 + body;
    }
    if (signal == 1 || signal == 3) {
        p = psd_be(p, info, 4);
    } else {
        p += 8;
        if (signal == 2) { memcpy(p, "8BIMMt16", 8); p += 12; }
        memcpy(p, "8BIMLr16", 8); p = psd_be(p + 8, info, 4);
    }
    /* one layer of no pixels, its channels holding only their compression */
    p = psd_be(p, (signal == 1 || signal == 4) ? 0xFFFF : 1, 2) + 16;
    p = psd_be(p, n, 2);
    for (c = 0; c < n; c++) { p = psd_be(p, c ? c - 1 : 0xFFFF, 2); p = psd_be(p, 2, 4); }
    memcpy(p, "8BIMnorm", 8); p[8] = 255; p += 12;
    p = psd_be(p, 12, 4) + 8;
    p[0] = 1; p[1] = 'a';
    return 4 + body;
}

/* CMYK + alpha PSD, alpha the transparency by signal 1: count < 0, 2: Mt16 + Lr16, 4: Lr16 count < 0, 5: Mtrn after a
   padded block; not by 0: no layers, 3: count > 0 */
static Bytes make_cmyka_psd(FIBITMAP *cmyk, FIBITMAP *alpha, const Bytes *icc, int signal, int rle) {
    const unsigned w = FreeImage_GetWidth(cmyk), h = FreeImage_GetHeight(cmyk);
    const unsigned bytes = (FreeImage_GetImageType(cmyk) == FIT_RGBA16) ? 2 : 1, line = w * bytes;
    const DWORD res = icc ? 12 + ((icc->size + 1) & ~1u) : 0;
    BYTE *p, *counts = NULL, *row = (BYTE *)malloc(line);
    Bytes b;
    unsigned c, x, y, i;
    b.size = 26 + 4 + 4 + res + psd_layer_section(NULL, signal, 4) + 2 + 5 * h * (rle ? 2 + line + (line + 127) / 128 : line);
    b.data = p = (BYTE *)calloc(1, b.size);
    memcpy(p, "8BPS", 4); psd_be(p + 4, 1, 2); psd_be(p + 12, 5, 2); psd_be(p + 14, h, 4); psd_be(p + 18, w, 4);
    psd_be(p + 22, 8 * bytes, 2); psd_be(p + 24, 4, 2);
    p = psd_be(p + 30, res, 4);
    if (icc) { memcpy(p, "8BIM", 4); psd_be(p + 4, 1039, 2); psd_be(p + 8, icc->size, 4); memcpy(p + 12, icc->data, icc->size); p += res; }
    p += psd_layer_section(p, signal, 4);
    p = psd_be(p, rle, 2);
    if (rle) { counts = p; p += 5 * h * 2; }
    for (c = 0; c < 5; c++) {
        for (y = 0; y < h; y++) {
            const BYTE *src = FreeImage_GetScanLine(c < 4 ? cmyk : alpha, h - 1 - y);
            for (x = 0; x < w; x++) {
                if (bytes == 2) psd_be(row + 2 * x, c < 4 ? 65535u - ((const WORD *)src)[4 * x + c] : ((const WORD *)src)[x], 2);
                else row[x] = (BYTE)(c < 4 ? 255 - src[4 * x + c] : src[x]);
            }
            if (!rle) { memcpy(p, row, line); p += line; continue; }
            psd_be(counts + 2 * (c * h + y), line + (line + 127) / 128, 2);
            /* literal packets of up to 128 bytes */
            for (i = 0; i < line; i += 128) {
                const unsigned n = (line - i < 128) ? line - i : 128;
                *p++ = (BYTE)(n - 1);
                memcpy(p, row + i, n); p += n;
            }
        }
    }
    free(row);
    b.size = (DWORD)(p - b.data);
    return b;
}

#endif
