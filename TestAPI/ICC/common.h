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

#endif
