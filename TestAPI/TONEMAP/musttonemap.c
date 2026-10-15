/* FreeImage_MustTonemap(): the verdicts on synthetic bitmaps, on profiles, on the CICP tag of the
   PNG, AVIF, HEIF and RAW loaders, and on the sample files; prints a report, exits non-zero on failure */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include "FreeImage.h"
#include "../../Source/LibLCMS2/include/lcms2.h"

/* exported, but declared in Source/Utilities.h only */
DLL_API FIBITMAP *DLL_CALLCONV FreeImage_AllocateHeaderT(BOOL header_only, FREE_IMAGE_TYPE type, int width, int height, int bpp, unsigned red_mask, unsigned green_mask, unsigned blue_mask);

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

static void expect(FIBITMAP *dib, const char *file, int want, const char *what) {
    FREE_IMAGE_FORMAT fif = (file && *file) ? FreeImage_GetFIFFromFilename(file) : FIF_UNKNOWN;
    int got = FreeImage_MustTonemap(dib, fif);
    CHECK(got == want, "%s: %d, expected %d", what, got, want);
}

static unsigned rng_state = 2463534242u;
static unsigned rnd(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

static double now_ms(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

/* ----------------------------------------------------------------------------------------------
   synthetic bitmaps
   ---------------------------------------------------------------------------------------------- */

enum Fill { FULL, SHIFTED8, TIMES257, LOW12, BLACK };

static WORD sample16(enum Fill fill) {
    switch (fill) {
        case FULL:     return (WORD)rnd();
        case SHIFTED8: return (WORD)((rnd() & 0xFF) << 8);
        case TIMES257: return (WORD)((rnd() & 0xFF) * 257);
        case LOW12:    return (WORD)(rnd() & 0xFFF);
        default:       return 0;
    }
}

/* UINT16, RGB16 or RGBA16; the alpha is opaque */
static FIBITMAP *make16(FREE_IMAGE_TYPE type, int w, int h, enum Fill fill) {
    FIBITMAP *dib = FreeImage_AllocateT(type, w, h, 8, 0, 0, 0);
    unsigned words = FreeImage_GetBPP(dib) / 16, x, y, c;
    for (y = 0; y < (unsigned)h; y++) {
        WORD *row = (WORD *)FreeImage_GetScanLine(dib, y);
        for (x = 0; x < (unsigned)w; x++) {
            for (c = 0; c < words; c++) {
                row[x * words + c] = (words == 4 && c == 3) ? 65535 : sample16(fill);
            }
        }
    }
    return dib;
}

/* FLOAT, RGBF or RGBAF in [lo, hi] */
static FIBITMAP *makeFloat(FREE_IMAGE_TYPE type, int w, int h, float lo, float hi) {
    FIBITMAP *dib = FreeImage_AllocateT(type, w, h, 8, 0, 0, 0);
    unsigned floats = FreeImage_GetBPP(dib) / 32, x, y, c;
    for (y = 0; y < (unsigned)h; y++) {
        float *row = (float *)FreeImage_GetScanLine(dib, y);
        for (x = 0; x < (unsigned)w; x++) {
            for (c = 0; c < floats; c++) {
                row[x * floats + c] = lo + (hi - lo) * (float)(rnd() % 100001) / 100000.0f;
            }
        }
    }
    return dib;
}

/* every n-th pixel of a FIT_FLOAT image set to value */
static void spike(FIBITMAP *dib, unsigned every, float value) {
    unsigned w = FreeImage_GetWidth(dib), h = FreeImage_GetHeight(dib), i;
    for (i = 0; i < w * h; i += every) {
        ((float *)FreeImage_GetScanLine(dib, i / w))[i % w] = value;
    }
}

static void set_cicp(FIBITMAP *dib, BYTE primaries, BYTE transfer, BYTE matrix, BYTE full) {
    BYTE value[4];
    FITAG *tag = FreeImage_CreateTag();
    value[0] = primaries; value[1] = transfer; value[2] = matrix; value[3] = full;
    FreeImage_SetTagKey(tag, "CICP");
    FreeImage_SetTagType(tag, FIDT_BYTE);
    FreeImage_SetTagCount(tag, 4);
    FreeImage_SetTagLength(tag, 4);
    FreeImage_SetTagValue(tag, value);
    FreeImage_SetMetadata(FIMD_CUSTOM, dib, "CICP", tag);
    FreeImage_DeleteTag(tag);
}

/* the transfer code of the CICP tag, -1 without one */
static int cicp_transfer(FIBITMAP *dib, BYTE out[4]) {
    FITAG *tag = NULL;
    if (!FreeImage_GetMetadata(FIMD_CUSTOM, dib, "CICP", &tag) || !tag) return -1;
    if (FreeImage_GetTagType(tag) != FIDT_BYTE || FreeImage_GetTagCount(tag) != 4) return -2;
    memcpy(out, FreeImage_GetTagValue(tag), 4);
    return out[1];
}

/* ----------------------------------------------------------------------------------------------
   profiles
   ---------------------------------------------------------------------------------------------- */

static void attach_builtin(FIBITMAP *dib, int which) {
    DWORD size = 0;
    const void *data = FreeImage_GetBuiltInICCProfile(which, &size);
    FreeImage_CreateICCProfile(dib, (void *)data, (long)size);
}

static void attach_profile(FIBITMAP *dib, cmsHPROFILE h) {
    cmsUInt32Number size = 0;
    void *data;
    cmsSaveProfileToMem(h, NULL, &size);
    data = malloc(size);
    cmsSaveProfileToMem(h, data, &size);
    FreeImage_CreateICCProfile(dib, data, (long)size);
    free(data);
    cmsCloseProfile(h);
}

static float pq_eotf(float v) {
    const double m1 = 0.1593017578125, m2 = 78.84375, c1 = 0.8359375, c2 = 18.8515625, c3 = 18.6875;
    double p = pow(v, 1 / m2);
    return (float)pow(fmax(p - c1, 0) / (c2 - c3 * p), 1 / m1);
}

/* an RGB profile with sRGB primaries, a tone curve and optionally a cicp tag (ICC 4.4) */
static cmsHPROFILE rgb_profile(cmsToneCurve *curve, int cicp_transfer_code) {
    cmsCIExyY d65 = { 0.3127, 0.3290, 1.0 };
    cmsCIExyYTRIPLE primaries = { { 0.64, 0.33, 1.0 }, { 0.30, 0.60, 1.0 }, { 0.15, 0.06, 1.0 } };
    cmsToneCurve *curves[3];
    cmsHPROFILE h;
    curves[0] = curves[1] = curves[2] = curve;
    h = cmsCreateRGBProfile(&d65, &primaries, curves);
    if (cicp_transfer_code >= 0) {
        cmsVideoSignalType cicp;
        cicp.ColourPrimaries = 1;
        cicp.TransferCharacteristics = (cmsUInt8Number)cicp_transfer_code;
        cicp.MatrixCoefficients = 0;
        cicp.VideoFullRangeFlag = 1;
        cmsSetProfileVersion(h, 4.4);
        cmsWriteTag(h, cmsSigcicpTag, &cicp);
    }
    cmsFreeToneCurve(curve);
    return h;
}

static cmsToneCurve *pq_curve(void) {
    float table[4096];
    int i;
    for (i = 0; i < 4096; i++) table[i] = pq_eotf(i / 4095.0f);
    return cmsBuildTabulatedToneCurveFloat(NULL, 4096, table);
}

/* ----------------------------------------------------------------------------------------------
   PNG with chunks inserted after IHDR
   ---------------------------------------------------------------------------------------------- */

static void put32(BYTE *p, DWORD v) { p[0] = (BYTE)(v >> 24); p[1] = (BYTE)(v >> 16); p[2] = (BYTE)(v >> 8); p[3] = (BYTE)v; }

static size_t chunk(BYTE *out, const char *type, const BYTE *data, DWORD length) {
    put32(out, length);
    memcpy(out + 4, type, 4);
    if (length) memcpy(out + 8, data, length);
    put32(out + 8 + length, FreeImage_ZLibCRC32(0, out + 4, length + 4));
    return 12 + length;
}

/* dib saved as PNG, with a gAMA of 1.0, which the loader corrects for a 2.2 screen, and a cICP when transfer >= 0, loaded back */
static FIBITMAP *png_round_trip(FIBITMAP *dib, int gama, int transfer, int flags) {
    FIMEMORY *saved = FreeImage_OpenMemory(NULL, 0), *loaded;
    BYTE *png = NULL, *edited, *p;
    DWORD size = 0;
    FIBITMAP *result;
    FreeImage_SaveToMemory(FIF_PNG, dib, saved, 0);
    FreeImage_AcquireMemory(saved, &png, &size);
    edited = (BYTE *)malloc(size + 64);
    p = edited;
    memcpy(p, png, 33);     /* signature and IHDR */
    p += 33;
    if (gama) {
        BYTE g[4];
        put32(g, 100000);
        p += chunk(p, "gAMA", g, 4);
    }
    if (transfer >= 0) {
        BYTE c[4] = { 9, (BYTE)transfer, 0, 1 };
        p += chunk(p, "cICP", c, 4);
    }
    memcpy(p, png + 33, size - 33);
    p += size - 33;
    loaded = FreeImage_OpenMemory(edited, (DWORD)(p - edited));
    result = FreeImage_LoadFromMemory(FIF_PNG, loaded, flags);
    FreeImage_CloseMemory(loaded);
    FreeImage_CloseMemory(saved);
    free(edited);
    return result;
}

static int same_pixels(FIBITMAP *a, FIBITMAP *b) {
    unsigned y;
    if (!a || !b || FreeImage_GetLine(a) != FreeImage_GetLine(b) || FreeImage_GetHeight(a) != FreeImage_GetHeight(b)) return 0;
    for (y = 0; y < FreeImage_GetHeight(a); y++) {
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), FreeImage_GetLine(a))) return 0;
    }
    return 1;
}

/* ----------------------------------------------------------------------------------------------
   the tests
   ---------------------------------------------------------------------------------------------- */

static void test_types(void) {
    static const FREE_IMAGE_TYPE scalars[] = { FIT_INT16, FIT_UINT32, FIT_INT32, FIT_DOUBLE, FIT_COMPLEX };
    static const int bpps[] = { 1, 4, 8, 16, 24, 32 };
    FIBITMAP *dib;
    unsigned i;
    printf("image types\n");
    expect(NULL, NULL, FITM_ERROR, "NULL");
    for (i = 0; i < sizeof(bpps) / sizeof(bpps[0]); i++) {
        dib = FreeImage_Allocate(16, 16, bpps[i], 0, 0, 0);
        expect(dib, "x.hdr", FITM_NONE, "a standard bitmap");
        FreeImage_Unload(dib);
    }
    for (i = 0; i < sizeof(scalars) / sizeof(scalars[0]); i++) {
        dib = FreeImage_AllocateT(scalars[i], 16, 16, 8, 0, 0, 0);
        expect(dib, NULL, FITM_NONE, "a scalar type ConvertToStandardType shows");
        FreeImage_Unload(dib);
    }
    dib = makeFloat(FIT_RGBF, 64, 64, 0, 0.5f);
    expect(dib, NULL, FITM_REQUIRED, "RGBF within 0..1");
    expect(dib, "x.tif", FITM_REQUIRED, "RGBF from a TIFF");
    FreeImage_Unload(dib);
    dib = makeFloat(FIT_RGBAF, 64, 64, -0.3f, 20);
    expect(dib, "x.jxr", FITM_REQUIRED, "RGBAF with negatives and highlights");
    FreeImage_Unload(dib);
    dib = FreeImage_AllocateHeaderT(TRUE, FIT_RGBF, 64, 64, 8, 0, 0, 0);
    expect(dib, NULL, FITM_REQUIRED, "header-only RGBF");
    FreeImage_Unload(dib);
}

static void test_sixteen(void) {
    FIBITMAP *dib;
    double t;
    printf("16-bit integer images\n");

    dib = make16(FIT_RGB16, 300, 200, FULL);
    expect(dib, NULL, FITM_NONE, "RGB16, no description");
    expect(dib, "x.png", FITM_NONE, "RGB16 named .png");
    expect(dib, "x.tif", FITM_NONE, "RGB16 named .tif");
    expect(dib, "x.cr2", FITM_OPTIONAL, "RGB16 named .cr2: LibRaw's linear output");
    FreeImage_Unload(dib);

    dib = make16(FIT_RGBA16, 300, 200, FULL);
    expect(dib, "x.png", FITM_NONE, "RGBA16 named .png");
    FreeImage_Unload(dib);
    dib = make16(FIT_UINT16, 300, 200, FULL);
    expect(dib, "x.pgm", FITM_NONE, "UINT16 named .pgm");
    expect(dib, "x.nef", FITM_OPTIONAL, "UINT16 named .nef: a monochrome camera");
    FreeImage_Unload(dib);

    dib = make16(FIT_RGB16, 300, 200, SHIFTED8);
    expect(dib, "x.cr2", FITM_NONE, "8-bit samples shifted up: a preview, not LibRaw's output");
    FreeImage_Unload(dib);
    dib = make16(FIT_RGB16, 300, 200, TIMES257);
    expect(dib, "x.cr2", FITM_NONE, "8-bit samples multiplied by 257");
    FreeImage_Unload(dib);

    dib = make16(FIT_RGB16, 300, 200, LOW12);
    expect(dib, "x.jp2", FITM_OPTIONAL, "12-bit samples stored unscaled");
    FreeImage_Unload(dib);
    dib = make16(FIT_RGB16, 300, 200, BLACK);
    expect(dib, NULL, FITM_NONE, "black");
    FreeImage_Unload(dib);
    {
        /* 8-bit values below 16: a dark image, not unscaled data */
        unsigned y, x;
        dib = FreeImage_AllocateT(FIT_RGB16, 64, 64, 8, 0, 0, 0);
        for (y = 0; y < 64; y++) {
            WORD *row = (WORD *)FreeImage_GetScanLine(dib, y);
            for (x = 0; x < 64 * 3; x++) row[x] = (WORD)((rnd() & 0x0F) * 257);
        }
        expect(dib, NULL, FITM_NONE, "a dark 8-bit image widened");
        FreeImage_Unload(dib);
    }

    /* larger than the sample budget: the grid misses the one bright pixel, the confirming pass does not */
    dib = make16(FIT_RGB16, 2048, 1024, LOW12);
    t = now_ms();
    expect(dib, NULL, FITM_OPTIONAL, "2 MP of 12-bit samples");
    printf("  2 MP of 12-bit samples, with the confirming pass: %.1f ms\n", now_ms() - t);
    ((FIRGB16 *)FreeImage_GetScanLine(dib, 1))[1].green = 60000;
    expect(dib, NULL, FITM_NONE, "2 MP of 12-bit samples and one bright pixel off the grid");
    FreeImage_Unload(dib);

    dib = make16(FIT_RGB16, 6000, 4000, FULL);
    t = now_ms();
    expect(dib, "x.png", FITM_NONE, "24 MP RGB16");
    printf("  24 MP RGB16, sampled: %.1f ms\n", now_ms() - t);
    FreeImage_Unload(dib);

    dib = FreeImage_AllocateHeaderT(TRUE, FIT_RGB16, 64, 64, 8, 0, 0, 0);
    expect(dib, "x.png", FITM_NONE, "header-only RGB16 named .png");
    expect(dib, "x.arw", FITM_OPTIONAL, "header-only RGB16 named .arw");
    set_cicp(dib, 9, 16, 9, 1);
    expect(dib, NULL, FITM_PQ, "header-only RGB16 tagged PQ");
    FreeImage_Unload(dib);

    dib = make16(FIT_RGBA16, 64, 64, FULL);
    attach_builtin(dib, FICMS_PROFILE_LINEAR_SRGB);
    FreeImage_GetICCProfile(dib)->flags |= FIICC_COLOR_IS_CMYK;
    CHECK(FreeImage_GetColorType(dib) == FIC_CMYK, "the CMYK flag makes RGBA16 CMYK");
    expect(dib, "x.tif", FITM_NONE, "16-bit CMYK");
    FreeImage_Unload(dib);
}

static void test_cicp_tag(void) {
    static const struct { BYTE transfer; int want; const char *what; } cases[] = {
        { 16, FITM_PQ, "PQ" },
        { 18, FITM_NONE, "HLG" },
        { 8, FITM_OPTIONAL, "linear" },
        { 9, FITM_OPTIONAL, "logarithmic" },
        { 13, FITM_NONE, "sRGB" },
        { 1, FITM_NONE, "BT.709" },
        { 14, FITM_NONE, "BT.2020" },
        { 2, FITM_NONE, "unspecified" },
    };
    FIBITMAP *dib;
    unsigned i;
    char what[64];
    printf("the CICP tag\n");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        dib = make16(FIT_RGB16, 64, 64, FULL);
        set_cicp(dib, 9, cases[i].transfer, 9, 1);
        snprintf(what, sizeof(what), "RGB16 tagged %s", cases[i].what);
        expect(dib, NULL, cases[i].want, what);
        FreeImage_Unload(dib);
    }
    /* PQ values someone turned into floating point are still PQ */
    dib = makeFloat(FIT_RGBF, 64, 64, 0, 1);
    set_cicp(dib, 9, 16, 9, 1);
    expect(dib, "x.tif", FITM_PQ, "RGBF tagged PQ");
    FreeImage_Unload(dib);
    dib = makeFloat(FIT_FLOAT, 64, 64, 0, 1);
    set_cicp(dib, 9, 16, 9, 1);
    expect(dib, "x.exr", FITM_PQ, "FLOAT tagged PQ");
    FreeImage_Unload(dib);
    /* the code points outrank the profile, as in PNG */
    dib = make16(FIT_RGB16, 64, 64, FULL);
    attach_builtin(dib, FICMS_PROFILE_LINEAR_SRGB);
    set_cicp(dib, 1, 13, 0, 1);
    expect(dib, NULL, FITM_NONE, "tagged sRGB with a linear profile");
    set_cicp(dib, 1, 2, 0, 1);
    expect(dib, NULL, FITM_OPTIONAL, "tagged unspecified: the linear profile decides");
    FreeImage_Unload(dib);
    /* a camera RAW tagged with a display curve is what it says */
    dib = make16(FIT_RGB16, 64, 64, FULL);
    set_cicp(dib, 1, 13, 0, 1);
    expect(dib, "x.cr2", FITM_NONE, "RGB16 named .cr2, tagged sRGB");
    FreeImage_Unload(dib);
}

static void set_bits_tag(FIBITMAP *dib, const char *key, BYTE bits) {
    FITAG *tag = FreeImage_CreateTag();
    FreeImage_SetTagKey(tag, key);
    FreeImage_SetTagType(tag, FIDT_BYTE);
    FreeImage_SetTagCount(tag, 1);
    FreeImage_SetTagLength(tag, 1);
    FreeImage_SetTagValue(tag, &bits);
    FreeImage_SetMetadata(FIMD_CUSTOM, dib, key, tag);
    FreeImage_DeleteTag(tag);
}

static void test_unscaled(void) {
    FIBITMAP *dib;
    printf("unscaled samples\n");
    dib = make16(FIT_RGB16, 64, 64, LOW12);
    set_bits_tag(dib, "UnscaledBits", 12);
    expect(dib, "x.jp2", FITM_UNSCALED, "RGB16 tagged UnscaledBits 12");
    set_cicp(dib, 9, 16, 9, 1);
    expect(dib, NULL, FITM_UNSCALED, "unscaled outranks PQ");
    FreeImage_Unload(dib);
    dib = make16(FIT_UINT16, 64, 64, FULL);
    set_bits_tag(dib, "UnscaledBits", 16);
    expect(dib, NULL, FITM_NONE, "UINT16 tagged UnscaledBits 16: all its bits");
    set_bits_tag(dib, "UnscaledBits", 15);
    expect(dib, NULL, FITM_UNSCALED, "UINT16 tagged UnscaledBits 15");
    FreeImage_Unload(dib);
    dib = FreeImage_Allocate(64, 64, 8, 0, 0, 0);
    set_bits_tag(dib, "UnscaledBits", 4);
    expect(dib, NULL, FITM_UNSCALED, "8-bit tagged UnscaledBits 4");
    FreeImage_Unload(dib);
    dib = make16(FIT_RGB16, 64, 64, FULL);
    set_bits_tag(dib, "SignificantBits", 12);
    expect(dib, "x.jp2", FITM_NONE, "RGB16 tagged SignificantBits 12: scaled");
    FreeImage_Unload(dib);
    dib = makeFloat(FIT_RGBF, 64, 64, 0, 1);
    set_bits_tag(dib, "UnscaledBits", 12);
    expect(dib, NULL, FITM_REQUIRED, "RGBF tagged UnscaledBits: not integer samples");
    FreeImage_Unload(dib);
}

static void test_profiles(void) {
    static const struct { int which; FREE_IMAGE_TYPE type; int want; const char *what; } builtins[] = {
        { FICMS_PROFILE_SRGB, FIT_RGB16, FITM_NONE, "sRGB" },
        { FICMS_PROFILE_LINEAR_SRGB, FIT_RGB16, FITM_OPTIONAL, "linear sRGB" },
        { FICMS_PROFILE_GRAY, FIT_UINT16, FITM_NONE, "grey" },
        { FICMS_PROFILE_LINEAR_GRAY, FIT_UINT16, FITM_OPTIONAL, "linear grey" },
        { FICMS_PROFILE_ADOBE_RGB, FIT_RGBA16, FITM_NONE, "Adobe RGB" },
        { FICMS_PROFILE_DISPLAY_P3, FIT_RGB16, FITM_NONE, "Display P3" },
        { FICMS_PROFILE_PROPHOTO_RGB, FIT_RGB16, FITM_NONE, "ProPhoto RGB, gamma 1.8" },
    };
    static const char *colord[] = { "ECI-RGBv2.icc", "Rec709.icc", "AdobeRGB1998.icc", "ProPhotoRGB.icc", "Gamma6500K.icc" };
    FIBITMAP *dib;
    unsigned i;
    char what[96];
    printf("ICC profiles\n");
    for (i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        dib = make16(builtins[i].type, 64, 64, FULL);
        attach_builtin(dib, builtins[i].which);
        snprintf(what, sizeof(what), "the built-in %s profile", builtins[i].what);
        expect(dib, "x.tif", builtins[i].want, what);
        FreeImage_Unload(dib);
    }

    dib = make16(FIT_RGB16, 64, 64, FULL);
    attach_profile(dib, rgb_profile(cmsBuildGamma(NULL, 1.0), -1));
    expect(dib, NULL, FITM_OPTIONAL, "gamma 1.0");
    FreeImage_Unload(dib);
    dib = make16(FIT_RGB16, 64, 64, FULL);
    attach_profile(dib, rgb_profile(cmsBuildGamma(NULL, 2.8), -1));
    expect(dib, NULL, FITM_NONE, "gamma 2.8");
    FreeImage_Unload(dib);
    dib = make16(FIT_RGB16, 64, 64, FULL);
    attach_profile(dib, rgb_profile(cmsBuildGamma(NULL, 5.0), -1));
    expect(dib, NULL, FITM_OPTIONAL, "gamma 5: steeper than any display");
    FreeImage_Unload(dib);
    dib = make16(FIT_RGB16, 64, 64, FULL);
    attach_profile(dib, rgb_profile(pq_curve(), -1));
    expect(dib, NULL, FITM_OPTIONAL, "a PQ tone curve without a cicp tag");
    FreeImage_Unload(dib);
    dib = make16(FIT_RGB16, 64, 64, FULL);
    attach_profile(dib, rgb_profile(pq_curve(), 16));
    expect(dib, NULL, FITM_PQ, "a PQ profile with its cicp tag");
    FreeImage_Unload(dib);
    dib = make16(FIT_RGB16, 64, 64, FULL);
    attach_profile(dib, rgb_profile(cmsBuildGamma(NULL, 2.2), 18));
    expect(dib, NULL, FITM_NONE, "a cicp tag saying HLG");
    FreeImage_Unload(dib);
    dib = make16(FIT_RGB16, 64, 64, FULL);
    attach_profile(dib, rgb_profile(cmsBuildGamma(NULL, 2.2), 8));
    expect(dib, NULL, FITM_OPTIONAL, "a cicp tag saying linear, over a gamma 2.2 curve");
    FreeImage_Unload(dib);
    dib = make16(FIT_RGB16, 64, 64, FULL);
    attach_profile(dib, cmsCreateLab4Profile(NULL));
    expect(dib, NULL, FITM_NONE, "a Lab profile on RGB pixels");
    FreeImage_Unload(dib);
    {
        BYTE junk[600];
        for (i = 0; i < sizeof(junk); i++) junk[i] = (BYTE)rnd();
        dib = make16(FIT_RGB16, 64, 64, FULL);
        FreeImage_CreateICCProfile(dib, junk, sizeof(junk));
        expect(dib, NULL, FITM_NONE, "a damaged profile");
        FreeImage_Unload(dib);
    }

    for (i = 0; i < sizeof(colord) / sizeof(colord[0]); i++) {
        char path[256];
        FILE *f;
        snprintf(path, sizeof(path), "/usr/share/color/icc/colord/%s", colord[i]);
        if ((f = fopen(path, "rb")) != NULL) {
            BYTE *data;
            long size;
            fseek(f, 0, SEEK_END);
            size = ftell(f);
            fseek(f, 0, SEEK_SET);
            data = (BYTE *)malloc(size);
            if (fread(data, 1, size, f) == (size_t)size) {
                dib = make16(FIT_RGB16, 64, 64, FULL);
                FreeImage_CreateICCProfile(dib, data, size);
                snprintf(what, sizeof(what), "colord's %s", colord[i]);
                expect(dib, NULL, FITM_NONE, what);
                FreeImage_Unload(dib);
            }
            free(data);
            fclose(f);
        }
    }
}

static void test_float(void) {
    FIBITMAP *dib;
    printf("grey float images\n");
    dib = makeFloat(FIT_FLOAT, 1000, 1000, 0, 0.9f);
    expect(dib, "x.pfm", FITM_REQUIRED, "FLOAT named .pfm: light");
    expect(dib, "x.exr", FITM_REQUIRED, "FLOAT named .exr");
    expect(dib, "x.tif", FITM_OPTIONAL, "FLOAT within 0..1 named .tif");
    expect(dib, NULL, FITM_OPTIONAL, "FLOAT within 0..1, no name");
    spike(dib, 1000003, 40);
    expect(dib, "x.tif", FITM_OPTIONAL, "one hot pixel in a million");
    spike(dib, 997, 40);
    expect(dib, "x.tif", FITM_REQUIRED, "0.1% of the pixels above white");
    spike(dib, 23, -5);
    expect(dib, "x.tif", FITM_NONE, "4% negative: signed data");
    spike(dib, 1, NAN);
    expect(dib, "x.tif", FITM_NONE, "nothing but NaN");
    FreeImage_Unload(dib);
    dib = FreeImage_AllocateHeaderT(TRUE, FIT_FLOAT, 64, 64, 8, 0, 0, 0);
    expect(dib, "x.tif", FITM_OPTIONAL, "header-only FLOAT named .tif");
    expect(dib, "x.hdr", FITM_REQUIRED, "header-only FLOAT named .hdr");
    FreeImage_Unload(dib);
}

static void test_png(void) {
    static const struct { int transfer; int want; const char *what; } cases[] = {
        { 16, FITM_PQ, "PQ" },
        { 18, FITM_NONE, "HLG" },
        { 8, FITM_OPTIONAL, "linear" },
        { 13, FITM_NONE, "sRGB" },
    };
    FIBITMAP *src = make16(FIT_RGB16, 96, 64, FULL), *dib;
    BYTE cicp[4];
    unsigned i;
    char what[96];
    printf("PNG cICP\n");
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        dib = png_round_trip(src, 1, cases[i].transfer, 0);
        snprintf(what, sizeof(what), "PNG with gAMA and cICP %s", cases[i].what);
        CHECK(dib != NULL, "%s: not loaded", what);
        if (!dib) continue;
        CHECK(cicp_transfer(dib, cicp) == cases[i].transfer && cicp[0] == 9 && cicp[2] == 0 && cicp[3] == 1, "%s: the CICP tag", what);
        CHECK(same_pixels(src, dib), "%s: cICP outranks gAMA, the pixels are the file's", what);
        expect(dib, NULL, cases[i].want, what);
        FreeImage_Unload(dib);
    }
    dib = png_round_trip(src, 0, 16, FIF_LOAD_NOPIXELS);
    CHECK(dib && !FreeImage_HasPixels(dib) && cicp_transfer(dib, cicp) == 16, "header-only PNG: the CICP tag");
    expect(dib, NULL, FITM_PQ, "header-only PNG with cICP PQ");
    FreeImage_Unload(dib);
    dib = png_round_trip(src, 1, -1, 0);
    CHECK(dib && !same_pixels(src, dib), "a PNG with gAMA only is still gamma corrected");
    CHECK(dib && cicp_transfer(dib, cicp) == -1, "a PNG without cICP has no CICP tag");
    FreeImage_Unload(dib);
    FreeImage_Unload(src);
}

static FIBITMAP *load(const char *path, int flags) {
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(path, 0);
    return (fif == FIF_UNKNOWN) ? NULL : FreeImage_Load(fif, path, flags);
}

static void test_files(void) {
    static const struct { const char *path; int flags; int want; int transfer; const char *what; } files[] = {
        { "../AVIF/data/seine_hdr_rec2020.avif", 0, FITM_PQ, 16, "10-bit AVIF, BT.2020 PQ" },
        { "../AVIF/data/white_1x1.avif", 0, FITM_NONE, 13, "8-bit AVIF, sRGB" },
        { "../AVIF/data/colors-animated-12bpc-keyframes-0-2-3.avif", 0, FITM_NONE, -1, "12-bit AVIF, unspecified" },
        { "../HEIF/data/RGB_10__128x128.heif", 0, FITM_NONE, -1, "10-bit HEIF, no nclx" },
        { "../HEIF/data/RGBA_12__128x128.heif", 0, FITM_NONE, -1, "12-bit HEIF with alpha, no nclx" },
        { "../HEIF/data/hevc32.heif", 0, FITM_NONE, 13, "8-bit HEIF, sRGB" },
        { "../../HDR-tests/leadenhall_market_4k.hdr", 0, FITM_REQUIRED, -1, "Radiance HDR" },
        { "../../HDR-tests/rogland_clear_night_4k.exr", 0, FITM_REQUIRED, -1, "OpenEXR" },
        { "../../HDR-tests/orange_colorballs.jxr", 0, FITM_REQUIRED, -1, "JPEG XR, half floats" },
        { "../../HDR-tests/canon_eos_70d_02.cr2", RAW_DEFAULT | RAW_HALFSIZE, FITM_OPTIONAL, 8, "CR2 decoded at 16 bits" },
        { "../../HDR-tests/canon_eos_70d_02.cr2", RAW_DISPLAY | RAW_HALFSIZE, FITM_NONE, -1, "CR2 decoded at 8 bits" },
        { "../../HDR-tests/canon_eos_70d_02.cr2", RAW_PREVIEW, FITM_NONE, -1, "CR2 preview" },
        { "../../HDR-tests/olympus_pen_f_06.orf", RAW_DEFAULT | RAW_HALFSIZE, FITM_OPTIONAL, 8, "ORF decoded at 16 bits" },
        { "../../HDR-tests/P1050532.RAW", RAW_DEFAULT | FIF_LOAD_NOPIXELS, FITM_OPTIONAL, 8, "Panasonic RAW, header only" },
    };
    unsigned i;
    printf("sample files\n");
    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        BYTE cicp[4];
        FIBITMAP *dib;
        FILE *f = fopen(files[i].path, "rb");
        if (!f) {
            printf("  skipped, not found: %s\n", files[i].path);
            continue;
        }
        fclose(f);
        dib = load(files[i].path, files[i].flags);
        CHECK(dib != NULL, "%s: not loaded", files[i].what);
        if (!dib) continue;
        CHECK(cicp_transfer(dib, cicp) == files[i].transfer, "%s: CICP transfer %d, expected %d", files[i].what, cicp_transfer(dib, cicp), files[i].transfer);
        expect(dib, files[i].path, files[i].want, files[i].what);
        /* the tag alone is enough: no file name */
        if (files[i].transfer >= 0) expect(dib, NULL, files[i].want, files[i].what);
        FreeImage_Unload(dib);
    }

    /* a RAW without its tag still counts as linear by its format */
    {
        const char *path = "../../HDR-tests/canon_eos_70d_02.cr2";
        FIBITMAP *dib = FreeImage_Load(FIF_RAW, path, RAW_DEFAULT | FIF_LOAD_NOPIXELS);
        if (dib) {
            FreeImage_SetMetadata(FIMD_CUSTOM, dib, "CICP", NULL);
            expect(dib, path, FITM_OPTIONAL, "CR2 without its CICP tag, by its file name");
            FreeImage_Unload(dib);
        }
    }
}


/* ----------------------------------------------------------------------------------------------
   FreeImage_ConvertToLinear, against the curves of the standards written out here
   ---------------------------------------------------------------------------------------------- */

static double ref_srgb(double v) { return (v <= 0.04045) ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4); }
static double ref_bt709(double v) { return (v < 0.081) ? v / 4.5 : pow((v + 0.099) / 1.099, 1 / 0.45); }
static double ref_240m(double v) { return (v < 0.0913) ? v / 4 : pow((v + 0.1115) / 1.1115, 1 / 0.45); }
static double ref_log100(double v) { return (v <= 0) ? 0 : pow(10, 2 * (v - 1)); }
static double ref_pq(double v) { return pq_eotf((float)v) * 10000.0 / 203.0; }
static double ref_hlg_scene(double v) {
    const double a = 0.17883277, b = 0.28466892, c = 0.55991073;
    return (v <= 0.5) ? v * v / 3 : (exp((v - c) / a) + b) / 12;
}

static int near(double got, double want, double tolerance) {
    return fabs(got - want) <= tolerance * fmax(1.0, fabs(want));
}

/* a row of RGB16 pixels from 0..1 signals */
static FIBITMAP *rgb16_row(const double *rgb, unsigned pixels) {
    FIBITMAP *dib = FreeImage_AllocateT(FIT_RGB16, pixels, 1, 8, 0, 0, 0);
    WORD *row = (WORD *)FreeImage_GetScanLine(dib, 0);
    unsigned i;
    for (i = 0; i < pixels * 3; i++) row[i] = (WORD)floor(rgb[i] * 65535 + 0.5);
    return dib;
}

static double word_signal(FIBITMAP *dib, unsigned i) {
    return ((WORD *)FreeImage_GetScanLine(dib, 0))[i] / 65535.0;
}

static float out(FIBITMAP *dib, unsigned x, unsigned c) {
    const unsigned step = (FreeImage_GetImageType(dib) == FIT_RGBAF) ? 4 : 3;
    return ((float *)FreeImage_GetScanLine(dib, 0))[x * step + c];
}

static FIBITMAP *linear_of(FIBITMAP *dib, int flags, const char *what) {
    FIBITMAP *lin = FreeImage_ConvertToLinear(dib, flags);
    CHECK(lin && (FreeImage_GetImageType(lin) == FIT_RGBF || FreeImage_GetImageType(lin) == FIT_RGBAF), "%s: no RGBF result", what);
    return lin;
}

/* each grey signal of a row through the curve the CICP code names */
static void check_curve(BYTE transfer, double (*ref)(double), const char *what) {
    static const double greys[] = { 0, 0.02, 0.1, 0.3, 0.5, 0.58, 0.75, 0.9, 1.0 };
    double rgb[27];
    unsigned i, c, bad = 0;
    FIBITMAP *dib, *lin;
    BYTE cicp[4];
    for (i = 0; i < 9; i++) rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = greys[i];
    dib = rgb16_row(rgb, 9);
    set_cicp(dib, 1, transfer, 0, 1);
    lin = linear_of(dib, 0, what);
    if (lin) {
        for (i = 0; i < 9; i++) for (c = 0; c < 3; c++) {
            if (!near(out(lin, i, c), ref(word_signal(dib, i * 3 + c)), 2e-5)) bad++;
        }
        CHECK(bad == 0, "%s: %u samples off the curve", what, bad);
        CHECK(cicp_transfer(lin, cicp) == 8 && cicp[0] == 1, "%s: the result's CICP tag", what);
        CHECK(FreeImage_MustTonemap(lin, FIF_UNKNOWN) == FITM_REQUIRED, "%s: the result is not classified as light", what);
        FreeImage_Unload(lin);
    }
    FreeImage_Unload(dib);
}

static double gamma22(double v) { return pow(v, 2.2); }
static double gamma28(double v) { return pow(v, 2.8); }
static double identity(double v) { return v; }

/* ----------------------------------------------------------------------------------------------
   the ICC profile of a result
   ---------------------------------------------------------------------------------------------- */

/* ITU-T H.273 table 2: red, green, blue and white */
static const double *code_xy(int code) {
    static const struct { int code; double xy[8]; } table[] = {
        { 1, { 0.640, 0.330, 0.300, 0.600, 0.150, 0.060, 0.3127, 0.3290 } },
        { 4, { 0.670, 0.330, 0.210, 0.710, 0.140, 0.080, 0.3100, 0.3160 } },
        { 5, { 0.640, 0.330, 0.290, 0.600, 0.150, 0.060, 0.3127, 0.3290 } },
        { 9, { 0.708, 0.292, 0.170, 0.797, 0.131, 0.046, 0.3127, 0.3290 } },
        { 11, { 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3140, 0.3510 } },
        { 12, { 0.680, 0.320, 0.265, 0.690, 0.150, 0.060, 0.3127, 0.3290 } },
    };
    unsigned i;
    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) if (table[i].code == code) return table[i].xy;
    return NULL;
}

static cmsHPROFILE open_icc(FIBITMAP *dib) {
    FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
    return (icc && icc->data && icc->size) ? cmsOpenProfileFromMem(icc->data, icc->size) : NULL;
}

static unsigned icc_size(FIBITMAP *dib) {
    return dib ? (unsigned)FreeImage_GetICCProfile(dib)->size : 0;
}

/* the colorants of an RGB profile against those of primaries and a white, adapted to D50 */
static int same_colorants(cmsHPROFILE h, const double xy[8]) {
    static const cmsTagSignature tags[3] = { cmsSigRedColorantTag, cmsSigGreenColorantTag, cmsSigBlueColorantTag };
    cmsCIExyY white;
    cmsCIExyYTRIPLE rgb;
    cmsToneCurve *linear = cmsBuildGamma(NULL, 1.0), *curves[3];
    cmsHPROFILE ref;
    int ok, c;
    white.x = xy[6]; white.y = xy[7]; white.Y = 1;
    rgb.Red.x = xy[0]; rgb.Red.y = xy[1]; rgb.Red.Y = 1;
    rgb.Green.x = xy[2]; rgb.Green.y = xy[3]; rgb.Green.Y = 1;
    rgb.Blue.x = xy[4]; rgb.Blue.y = xy[5]; rgb.Blue.Y = 1;
    curves[0] = curves[1] = curves[2] = linear;
    ref = cmsCreateRGBProfile(&white, &rgb, curves);
    cmsFreeToneCurve(linear);
    ok = (ref != NULL);
    for (c = 0; ok && c < 3; c++) {
        const cmsCIEXYZ *a = (const cmsCIEXYZ *)cmsReadTag(h, tags[c]), *b = (const cmsCIEXYZ *)cmsReadTag(ref, tags[c]);
        ok = a && b && fabs(a->X - b->X) < 2e-4 && fabs(a->Y - b->Y) < 2e-4 && fabs(a->Z - b->Z) < 2e-4;
    }
    if (ref) cmsCloseProfile(ref);
    return ok;
}

/* every tone curve of an RGB profile against a curve written out here */
static int curves_are(cmsHPROFILE h, double (*ref)(double)) {
    static const cmsTagSignature tags[3] = { cmsSigRedTRCTag, cmsSigGreenTRCTag, cmsSigBlueTRCTag };
    static const double v[] = { 0.01, 0.04, 0.1, 0.3, 0.5, 0.75, 0.95 };
    unsigned c, i;
    for (c = 0; c < 3; c++) {
        const cmsToneCurve *t = (const cmsToneCurve *)cmsReadTag(h, tags[c]);
        if (!t) return 0;
        for (i = 0; i < sizeof(v) / sizeof(v[0]); i++) {
            if (fabs(cmsEvalToneCurveFloat(t, (cmsFloat32Number)v[i]) - ref(v[i])) > 1e-4) return 0;
        }
    }
    return 1;
}

/* the image carries the RGB profile of a primaries code point with a tone curve, its cicp tag saying so */
static void check_code_profile(FIBITMAP *dib, int primaries, int transfer, double (*curve)(double), const char *what) {
    cmsHPROFILE h = open_icc(dib);
    const cmsVideoSignalType *cicp;
    CHECK(h != NULL, "%s: no ICC profile", what);
    if (!h) return;
    CHECK(cmsGetColorSpace(h) == cmsSigRgbData, "%s: not an RGB profile", what);
    CHECK(same_colorants(h, code_xy(primaries)), "%s: not the colorants of primaries %d", what, primaries);
    CHECK(curves_are(h, curve), "%s: not the tone curve of transfer %d", what, transfer);
    cicp = (const cmsVideoSignalType *)cmsReadTag(h, cmsSigcicpTag);
    CHECK(cicp && cicp->ColourPrimaries == primaries && cicp->TransferCharacteristics == transfer && cicp->MatrixCoefficients == 0 && cicp->VideoFullRangeFlag == 1,
          "%s: the profile's cicp tag", what);
    cmsCloseProfile(h);
}

/* the result keeps the primaries the CICP tag names, and carries their profile with a linear tone curve */
static void test_linear_profiles(void) {
    static const struct { BYTE code, transfer; const char *what; } kept[] = {
        { 9, 16, "BT.2020 PQ" }, { 9, 1, "BT.2020" }, { 12, 13, "Display P3" }, { 5, 6, "BT.601 625 lines" },
        { 11, 1, "DCI-P3, its own white" }, { 4, 1, "BT.470 System M, illuminant C" } };
    static const struct { BYTE code, transfer; const char *what; } none[] = {
        { 1, 13, "sRGB" }, { 2, 16, "unspecified primaries" }, { 10, 1, "XYZ primaries" } };
    static const double rgb[12] = { 0.9, 0.2, 0.1, 0.1, 0.8, 0.3, 0.2, 0.3, 0.9, 0.5, 0.5, 0.5 };
    DWORD size = 0;
    const void *linear_srgb = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_LINEAR_SRGB, &size);
    unsigned k, x, c;
    char what[128];
    printf("FreeImage_ConvertToLinear, the result's profile\n");
    for (k = 0; k < sizeof(kept) / sizeof(kept[0]); k++) {
        FIBITMAP *dib = rgb16_row(rgb, 4), *lin, *lin709, *cm;
        set_cicp(dib, kept[k].code, kept[k].transfer, 0, 1);
        snprintf(what, sizeof(what), "linear %s", kept[k].what);
        lin = linear_of(dib, 0, what);
        lin709 = linear_of(dib, FI_LINEAR_SRGB_PRIMARIES, what);
        if (lin && lin709) {
            check_code_profile(lin, kept[k].code, 8, identity, what);
            CHECK(icc_size(lin709) == 0, "%s, in sRGB primaries: a profile on linear sRGB", what);
            /* the profile takes the colors where the flag's matrix does */
            cm = FreeImage_ConvertToICCProfile(lin, linear_srgb, size, FICMS_INTENT_RELATIVE_COLORIMETRIC);
            CHECK(cm != NULL, "%s: not converted to linear sRGB", what);
            if (cm) {
                unsigned bad = 0;
                for (x = 0; x < 4; x++) for (c = 0; c < 3; c++) if (!near(out(cm, x, c), out(lin709, x, c), 1e-3)) bad++;
                CHECK(bad == 0, "%s: %u of 12 samples converted through its profile miss the flag's (red %.4f, not %.4f)",
                      what, bad, out(cm, 0, 0), out(lin709, 0, 0));
                FreeImage_Unload(cm);
            }
        }
        FreeImage_Unload(lin);
        FreeImage_Unload(lin709);
        FreeImage_Unload(dib);
    }
    for (k = 0; k < sizeof(none) / sizeof(none[0]); k++) {
        FIBITMAP *dib = rgb16_row(rgb, 4), *lin;
        set_cicp(dib, none[k].code, none[k].transfer, 0, 1);
        snprintf(what, sizeof(what), "linear %s", none[k].what);
        lin = linear_of(dib, 0, what);
        CHECK(lin && icc_size(lin) == 0, "%s: the result has a profile", what);
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);
    }
}

static void test_linear(void) {
    FIBITMAP *dib, *lin;
    BYTE cicp[4];
    unsigned i;
    printf("FreeImage_ConvertToLinear\n");

    CHECK(FreeImage_ConvertToLinear(NULL, 0) == NULL, "NULL");
    dib = FreeImage_AllocateT(FIT_DOUBLE, 8, 8, 8, 0, 0, 0);
    CHECK(FreeImage_ConvertToLinear(dib, 0) == NULL, "FIT_DOUBLE is refused");
    FreeImage_Unload(dib);
    dib = FreeImage_AllocateHeaderT(TRUE, FIT_RGB16, 8, 8, 8, 0, 0, 0);
    CHECK(FreeImage_ConvertToLinear(dib, 0) == NULL, "a header-only bitmap is refused");
    FreeImage_Unload(dib);

    check_curve(16, ref_pq, "PQ");
    check_curve(13, ref_srgb, "sRGB");
    check_curve(1, ref_bt709, "BT.709");
    check_curve(14, ref_bt709, "BT.2020 10-bit");
    check_curve(4, gamma22, "gamma 2.2");
    check_curve(5, gamma28, "gamma 2.8");
    check_curve(7, ref_240m, "SMPTE 240M");
    check_curve(9, ref_log100, "logarithmic 100:1");
    check_curve(8, identity, "linear");

    {
        /* PQ: 58% is SDR white, 203 cd/m2; full signal is 10000 cd/m2 */
        double rgb[6] = { 0.58, 0.58, 0.58, 1, 1, 1 };
        dib = rgb16_row(rgb, 2);
        set_cicp(dib, 9, 16, 9, 1);
        lin = linear_of(dib, 0, "PQ white");
        CHECK(lin && fabs(out(lin, 0, 1) - 1.0) < 0.02, "PQ 0.58: %.4f, not near 1.0", lin ? out(lin, 0, 1) : -1);
        CHECK(lin && near(out(lin, 1, 1), 10000.0 / 203, 1e-4), "PQ 1.0: %.3f, not 49.26", lin ? out(lin, 1, 1) : -1);
        CHECK(lin && cicp_transfer(lin, cicp) == 8 && cicp[0] == 9, "PQ: primaries kept without the flag");
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);
    }
    {
        /* HLG: grey 0.75 is 203 cd/m2 on the 1000 cd/m2 reference display */
        double rgb[12] = { 0.75, 0.75, 0.75, 0.5, 0.5, 0.5, 1, 1, 1, 0.75, 0.5, 0.25 };
        static const double w2020[3] = { 0.2627, 0.6780, 0.0593 }, w709[3] = { 0.2126, 0.7152, 0.0722 };
        int primaries;
        for (primaries = 0; primaries < 2; primaries++) {
            const double *w = primaries ? w709 : w2020;
            double e[3], ys = 0, gain;
            unsigned c, bad = 0;
            dib = rgb16_row(rgb, 4);
            set_cicp(dib, primaries ? 1 : 9, 18, 0, 1);
            lin = linear_of(dib, 0, "HLG");
            if (lin) {
                CHECK(fabs(out(lin, 0, 0) - 1.0) < 0.002, "HLG grey 0.75: %.4f, not 1.0", out(lin, 0, 0));
                CHECK(near(out(lin, 2, 0), 1000.0 / 203, 1e-3), "HLG 1.0: %.3f, not 4.93", out(lin, 2, 0));
                for (c = 0; c < 3; c++) { e[c] = ref_hlg_scene(word_signal(dib, 9 + c)); ys += w[c] * e[c]; }
                gain = 1000.0 / 203 * pow(ys, 0.2);
                for (c = 0; c < 3; c++) if (!near(out(lin, 3, c), e[c] * gain, 1e-4)) bad++;
                CHECK(bad == 0, "HLG colour with %s weights: the display step", primaries ? "BT.709" : "BT.2020");
                FreeImage_Unload(lin);
            }
            if (!primaries) {
                /* the display step runs on BT.2020 light, before the matrix to BT.709 */
                static const double m[3][3] = { { 1.6605, -0.5876, -0.0728 }, { -0.1246, 1.1329, -0.0083 }, { -0.0182, -0.1006, 1.1187 } };
                lin = linear_of(dib, FI_LINEAR_SRGB_PRIMARIES, "HLG to BT.709");
                if (lin) {
                    bad = 0;
                    for (c = 0; c < 3; c++) {
                        const double want = (m[c][0] * e[0] + m[c][1] * e[1] + m[c][2] * e[2]) * gain;
                        if (fabs(out(lin, 3, c) - want) > 0.003) bad++;
                    }
                    CHECK(bad == 0, "HLG colour to BT.709: the display step and the matrix, in that order");
                    FreeImage_Unload(lin);
                }
            }
            FreeImage_Unload(dib);
        }
    }
    {
        /* no description: sRGB for 8 and 16 bits, light already for floating point */
        double rgb[3] = { 0.5, 0.25, 1 };
        BYTE *p;
        dib = rgb16_row(rgb, 1);
        lin = linear_of(dib, 0, "untagged RGB16");
        CHECK(lin && near(out(lin, 0, 0), ref_srgb(word_signal(dib, 0)), 2e-5) && near(out(lin, 0, 1), ref_srgb(word_signal(dib, 1)), 2e-5), "untagged RGB16: not sRGB");
        CHECK(lin && cicp_transfer(lin, cicp) == 8 && cicp[0] == 1, "untagged RGB16: the result's tag");
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);
        dib = FreeImage_Allocate(1, 1, 24, 0, 0, 0);
        p = FreeImage_GetScanLine(dib, 0);
        p[FI_RGBA_RED] = 128; p[FI_RGBA_GREEN] = 64; p[FI_RGBA_BLUE] = 255;
        lin = linear_of(dib, 0, "untagged 24-bit");
        CHECK(lin && near(out(lin, 0, 0), ref_srgb(128 / 255.0), 2e-5) && near(out(lin, 0, 1), ref_srgb(64 / 255.0), 2e-5) && near(out(lin, 0, 2), 1, 2e-5), "untagged 24-bit: not sRGB in R, G, B order");
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);
        dib = FreeImage_AllocateT(FIT_RGBF, 1, 1, 8, 0, 0, 0);
        ((float *)FreeImage_GetScanLine(dib, 0))[0] = 20; ((float *)FreeImage_GetScanLine(dib, 0))[1] = 0.5f; ((float *)FreeImage_GetScanLine(dib, 0))[2] = -0.1f;
        lin = linear_of(dib, 0, "untagged RGBF");
        CHECK(lin && out(lin, 0, 0) == 20 && out(lin, 0, 1) == 0.5f && out(lin, 0, 2) == -0.1f, "untagged RGBF: changed");
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);
    }
    {
        /* alpha: RGBA16 and 32-bit images give RGBAF, alpha unchanged */
        BYTE *p;
        dib = make16(FIT_RGBA16, 4, 1, FULL);
        ((FIRGBA16 *)FreeImage_GetScanLine(dib, 0))[2].alpha = 12345;
        lin = linear_of(dib, 0, "RGBA16");
        CHECK(lin && FreeImage_GetImageType(lin) == FIT_RGBAF && near(out(lin, 2, 3), 12345 / 65535.0, 1e-6) && near(out(lin, 0, 3), 1, 1e-6), "RGBA16: the alpha");
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);
        dib = FreeImage_Allocate(2, 1, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
        p = FreeImage_GetScanLine(dib, 0);
        memset(p, 200, 8);
        p[FI_RGBA_ALPHA] = 51;
        lin = linear_of(dib, 0, "32-bit with alpha");
        CHECK(lin && FreeImage_GetImageType(lin) == FIT_RGBAF && near(out(lin, 0, 3), 51 / 255.0, 1e-6) && near(out(lin, 1, 3), 200 / 255.0, 1e-6), "32-bit: the alpha");
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);
    }
    {
        /* unscaled samples are read in their own precision */
        double rgb[3] = { 4095 / 65535.0, 2048 / 65535.0, 0 };
        dib = rgb16_row(rgb, 1);
        set_bits_tag(dib, "UnscaledBits", 12);
        lin = linear_of(dib, 0, "unscaled 12-bit");
        CHECK(lin && near(out(lin, 0, 0), 1, 2e-5) && near(out(lin, 0, 1), ref_srgb(2048 / 4095.0), 2e-5), "unscaled 12-bit: not read in 12 bits");
        {
            FITAG *kept = NULL;
            CHECK(lin && !FreeImage_GetMetadata(FIMD_CUSTOM, lin, "UnscaledBits", &kept), "unscaled 12-bit: the result keeps the tag");
        }
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);
    }
    {
        /* the primaries matrix: BT.2020 and Display P3 to BT.709, and light above white kept */
        static const struct { BYTE code; double red[3]; const char *what; } cases[] = {
            { 9, { 1.6605, -0.1246, -0.0182 }, "BT.2020" },
            { 12, { 1.2249, -0.0420, -0.0197 }, "Display P3" },
        };
        unsigned k;
        for (k = 0; k < 2; k++) {
            double rgb[6] = { 1, 0, 0, 0.5, 0.5, 0.5 };
            dib = rgb16_row(rgb, 2);
            set_cicp(dib, cases[k].code, 8, 0, 1);
            lin = linear_of(dib, FI_LINEAR_SRGB_PRIMARIES, cases[k].what);
            if (lin) {
                CHECK(fabs(out(lin, 0, 0) - cases[k].red[0]) < 0.002 && fabs(out(lin, 0, 1) - cases[k].red[1]) < 0.002 && fabs(out(lin, 0, 2) - cases[k].red[2]) < 0.002,
                      "%s red: %.4f %.4f %.4f", cases[k].what, out(lin, 0, 0), out(lin, 0, 1), out(lin, 0, 2));
                CHECK(fabs(out(lin, 1, 0) - out(lin, 1, 1)) < 1e-4 && fabs(out(lin, 1, 1) - out(lin, 1, 2)) < 1e-4, "%s: grey is not kept grey", cases[k].what);
                CHECK(cicp_transfer(lin, cicp) == 8 && cicp[0] == 1, "%s: the result's primaries", cases[k].what);
                FreeImage_Unload(lin);
            }
            FreeImage_Unload(dib);
        }
        dib = FreeImage_AllocateT(FIT_RGBF, 1, 1, 8, 0, 0, 0);
        ((float *)FreeImage_GetScanLine(dib, 0))[0] = 20;
        ((float *)FreeImage_GetScanLine(dib, 0))[1] = ((float *)FreeImage_GetScanLine(dib, 0))[2] = 0;
        set_cicp(dib, 9, 8, 0, 1);
        lin = linear_of(dib, FI_LINEAR_SRGB_PRIMARIES, "RGBF 20.0, BT.2020");
        CHECK(lin && fabs(out(lin, 0, 0) - 20 * 1.6605) < 0.05, "20.0 through the CICP matrix: %.3f", lin ? out(lin, 0, 0) : -1);
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);
    }
    {
        /* ICC profiles: the tone curves, the colorants, a profile Little CMS has to run */
        double rgb[6] = { 0.5, 0.25, 0.75, 1, 0, 0 };
        FIBITMAP *icc_lin;
        dib = rgb16_row(rgb, 2);
        attach_builtin(dib, FICMS_PROFILE_LINEAR_SRGB);
        lin = linear_of(dib, 0, "linear sRGB profile");
        CHECK(lin && near(out(lin, 0, 0), word_signal(dib, 0), 1e-5) && near(out(lin, 0, 1), word_signal(dib, 1), 1e-5), "linear sRGB profile: changed");
        FreeImage_Unload(lin);
        FreeImage_DestroyICCProfile(dib);
        attach_builtin(dib, FICMS_PROFILE_SRGB);
        lin = linear_of(dib, 0, "sRGB profile");
        CHECK(lin && near(out(lin, 0, 0), ref_srgb(word_signal(dib, 0)), 1e-3) && near(out(lin, 0, 2), ref_srgb(word_signal(dib, 2)), 1e-3), "sRGB profile: %.5f, not %.5f", lin ? out(lin, 0, 0) : -1, ref_srgb(word_signal(dib, 0)));
        FreeImage_Unload(lin);
        FreeImage_DestroyICCProfile(dib);
        attach_profile(dib, rgb_profile(cmsBuildGamma(NULL, 2.2), -1));
        lin = linear_of(dib, 0, "gamma 2.2 profile");
        CHECK(lin && near(out(lin, 0, 0), pow(word_signal(dib, 0), 2.2), 1e-4), "gamma 2.2 profile: %.5f", lin ? out(lin, 0, 0) : -1);
        icc_lin = lin;
        if (lin) {
            /* without the flag the result keeps the colorants, with linear tone curves */
            FIICCPROFILE *icc = FreeImage_GetICCProfile(lin);
            cmsHPROFILE h = (icc && icc->data) ? cmsOpenProfileFromMem(icc->data, icc->size) : NULL;
            const cmsToneCurve *trc = h ? (const cmsToneCurve *)cmsReadTag(h, cmsSigRedTRCTag) : NULL;
            CHECK(trc && cmsIsToneCurveLinear(trc), "gamma 2.2 profile: the result's profile is not linear");
            if (h) cmsCloseProfile(h);
            FreeImage_Unload(icc_lin);
        }
        FreeImage_DestroyICCProfile(dib);
        {
            /* a tone curve of its own for each channel */
            cmsCIExyY d65 = { 0.3127, 0.3290, 1.0 };
            cmsCIExyYTRIPLE srgb = { { 0.64, 0.33, 1.0 }, { 0.30, 0.60, 1.0 }, { 0.15, 0.06, 1.0 } };
            cmsToneCurve *curves[3];
            curves[0] = cmsBuildGamma(NULL, 1.8);
            curves[1] = cmsBuildGamma(NULL, 2.2);
            curves[2] = cmsBuildGamma(NULL, 2.6);
            attach_profile(dib, cmsCreateRGBProfile(&d65, &srgb, curves));
            cmsFreeToneCurve(curves[0]); cmsFreeToneCurve(curves[1]); cmsFreeToneCurve(curves[2]);
            lin = linear_of(dib, 0, "gamma 1.8, 2.2, 2.6 profile");
            CHECK(lin && near(out(lin, 0, 0), pow(word_signal(dib, 0), 1.8), 1e-4) && near(out(lin, 0, 1), pow(word_signal(dib, 1), 2.2), 1e-4) && near(out(lin, 0, 2), pow(word_signal(dib, 2), 2.6), 1e-4),
                  "gamma 1.8, 2.2, 2.6 profile: %.5f %.5f %.5f", lin ? out(lin, 0, 0) : -1, lin ? out(lin, 0, 1) : -1, lin ? out(lin, 0, 2) : -1);
            FreeImage_Unload(lin);
            FreeImage_DestroyICCProfile(dib);
        }
        attach_builtin(dib, FICMS_PROFILE_PROPHOTO_RGB);
        lin = linear_of(dib, FI_LINEAR_SRGB_PRIMARIES, "ProPhoto to sRGB primaries");
        CHECK(lin && fabs(out(lin, 1, 0) - 2.0342) < 0.01 && fabs(out(lin, 1, 1) + 0.2288) < 0.01 && fabs(out(lin, 1, 2) + 0.0086) < 0.01,
              "ProPhoto red in BT.709: %.4f %.4f %.4f", lin ? out(lin, 1, 0) : 0, lin ? out(lin, 1, 1) : 0, lin ? out(lin, 1, 2) : 0);
        CHECK(lin && FreeImage_GetICCProfile(lin)->size == 0, "ProPhoto to sRGB primaries: a profile left on linear sRGB");
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);

        /* 20.0 through the colorants of a linear BT.2020 profile */
        {
            cmsCIExyY d65 = { 0.3127, 0.3290, 1.0 };
            cmsCIExyYTRIPLE bt2020 = { { 0.708, 0.292, 1.0 }, { 0.170, 0.797, 1.0 }, { 0.131, 0.046, 1.0 } };
            cmsToneCurve *linear = cmsBuildGamma(NULL, 1.0), *curves[3];
            curves[0] = curves[1] = curves[2] = linear;
            dib = FreeImage_AllocateT(FIT_RGBF, 1, 1, 8, 0, 0, 0);
            ((float *)FreeImage_GetScanLine(dib, 0))[0] = 20;
            ((float *)FreeImage_GetScanLine(dib, 0))[1] = ((float *)FreeImage_GetScanLine(dib, 0))[2] = 0;
            attach_profile(dib, cmsCreateRGBProfile(&d65, &bt2020, curves));
            cmsFreeToneCurve(linear);
            lin = linear_of(dib, FI_LINEAR_SRGB_PRIMARIES, "RGBF 20.0, linear BT.2020 profile");
            CHECK(lin && fabs(out(lin, 0, 0) - 20 * 1.6605) < 0.1 && fabs(out(lin, 0, 1) + 20 * 0.1246) < 0.05, "20.0 through the profile's matrix: %.3f %.3f", lin ? out(lin, 0, 0) : -1, lin ? out(lin, 0, 1) : -1);
            FreeImage_Unload(lin);
            FreeImage_Unload(dib);
        }

        /* grey */
        dib = make16(FIT_UINT16, 2, 1, FULL);
        attach_builtin(dib, FICMS_PROFILE_GRAY);
        lin = linear_of(dib, 0, "grey profile");
        CHECK(lin && near(out(lin, 0, 0), ref_srgb(((WORD *)FreeImage_GetScanLine(dib, 0))[0] / 65535.0), 1e-3) && out(lin, 0, 0) == out(lin, 0, 2), "grey profile: not the sRGB curve, or not grey");
        FreeImage_Unload(lin);
        FreeImage_DestroyICCProfile(dib);
        attach_builtin(dib, FICMS_PROFILE_LINEAR_GRAY);
        lin = linear_of(dib, 0, "linear grey profile");
        CHECK(lin && near(out(lin, 1, 1), ((WORD *)FreeImage_GetScanLine(dib, 0))[1] / 65535.0, 1e-5), "linear grey profile: changed");
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);

        /* a LUT-based profile, which only Little CMS can run */
        {
            FILE *f = fopen("../ICC/data/test3.icc", "rb");
            if (f) {
                BYTE data[65536];
                long size = (long)fread(data, 1, sizeof(data), f);
                fclose(f);
                dib = make16(FIT_RGB16, 8, 8, FULL);
                FreeImage_CreateICCProfile(dib, data, size);
                lin = linear_of(dib, 0, "LUT-based profile");
                CHECK(lin && cicp_transfer(lin, cicp) == 8 && cicp[0] == 1 && FreeImage_GetICCProfile(lin)->size == 0, "LUT-based profile: not converted to linear sRGB");
                FreeImage_Unload(lin);
                FreeImage_Unload(dib);
            } else {
                printf("  skipped, not found: ../ICC/data/test3.icc\n");
            }
        }
    }
    {
        /* CMYK is converted to RGB first */
        dib = make16(FIT_RGBA16, 4, 4, FULL);
        FreeImage_GetICCProfile(dib)->flags |= FIICC_COLOR_IS_CMYK;
        lin = linear_of(dib, 0, "16-bit CMYK");
        FreeImage_Unload(lin);
        FreeImage_Unload(dib);
    }
    {
        /* a PQ AVIF, end to end: tone mapped from linear light, then from its encoded values */
        const char *path = "../AVIF/data/seine_hdr_rec2020.avif";
        dib = FreeImage_Load(FIF_AVIF, path, 0);
        if (dib) {
            FIBITMAP *tm_lin = NULL, *tm_raw = NULL;
            float peak = 0;
            unsigned y, w = FreeImage_GetWidth(dib), h = FreeImage_GetHeight(dib);
            CHECK(FreeImage_MustTonemap(dib, FIF_AVIF) == FITM_PQ, "seine_hdr_rec2020.avif: not PQ");
            lin = linear_of(dib, FI_LINEAR_SRGB_PRIMARIES, "seine_hdr_rec2020.avif");
            if (lin) {
                for (y = 0; y < h; y++) for (i = 0; i < w * 3; i++) {
                    const float v = ((float *)FreeImage_GetScanLine(lin, y))[i];
                    if (v > peak) peak = v;
                }
                CHECK(peak > 1, "seine_hdr_rec2020.avif: nothing above SDR white, peak %.2f", peak);
                CHECK(FreeImage_MustTonemap(lin, FIF_AVIF) == FITM_REQUIRED, "seine_hdr_rec2020.avif, linear: not 2");
                tm_lin = FreeImage_ToneMapping(lin, FITMO_REINHARD05, 0, 0);
                tm_raw = FreeImage_ToneMapping(dib, FITMO_REINHARD05, 0, 0);
                CHECK(tm_lin && tm_raw, "seine_hdr_rec2020.avif: not tone mapped");
                if (tm_lin && tm_raw) {
                    double mean_lin = 0, mean_raw = 0;
                    for (y = 0; y < h; y++) for (i = 0; i < w * 3; i++) {
                        mean_lin += FreeImage_GetScanLine(tm_lin, y)[i];
                        mean_raw += FreeImage_GetScanLine(tm_raw, y)[i];
                    }
                    printf("  seine_hdr_rec2020.avif: peak %.1f x SDR white; Reinhard05 mean %.0f/255 from linear light, %.0f/255 from the PQ values\n",
                           peak, mean_lin / (w * h * 3.0), mean_raw / (w * h * 3.0));
                }
                FreeImage_Unload(tm_lin);
                FreeImage_Unload(tm_raw);
                FreeImage_Unload(lin);
            }
            /* its BT.2020 primaries kept: linear, then tone mapped, each with their profile */
            lin = linear_of(dib, 0, "seine_hdr_rec2020.avif, BT.2020");
            if (lin) {
                check_code_profile(lin, 9, 8, identity, "seine_hdr_rec2020.avif, linear BT.2020");
                tm_lin = FreeImage_ToneMapping(lin, FITMO_REINHARD05, 0, 0);
                CHECK(tm_lin != NULL, "seine_hdr_rec2020.avif, linear BT.2020: not tone mapped");
                if (tm_lin) check_code_profile(tm_lin, 9, 13, ref_srgb, "seine_hdr_rec2020.avif, tone mapped in BT.2020");
                FreeImage_Unload(tm_lin);
                FreeImage_Unload(lin);
            }
            FreeImage_Unload(dib);
        } else {
            printf("  skipped, not found: %s\n", path);
        }
    }
}

/* ----------------------------------------------------------------------------------------------
   the tone mapping operators' output: display samples, in the primaries of the light they were given
   ---------------------------------------------------------------------------------------------- */

/* the output's samples read through the sRGB curve, which FreeImage_ConvertToLinear takes from its tag */
static void check_display_samples(FIBITMAP *dst, const char *what) {
    FIBITMAP *lin = linear_of(dst, 0, what);
    unsigned x, bad = 0, w = FreeImage_GetWidth(dst);
    if (!lin) return;
    for (x = 0; x < w; x++) {
        RGBQUAD q;
        FreeImage_GetPixelColor(dst, x, 0, &q);
        if (!near(out(lin, x, 0), ref_srgb(q.rgbRed / 255.0), 1e-4) || !near(out(lin, x, 1), ref_srgb(q.rgbGreen / 255.0), 1e-4) ||
            !near(out(lin, x, 2), ref_srgb(q.rgbBlue / 255.0), 1e-4)) bad++;
    }
    CHECK(bad == 0, "%s: %u of %u pixels not read as sRGB samples", what, bad, w);
    FreeImage_Unload(lin);
}

static void test_tonemapped(void) {
    static const struct { FREE_IMAGE_TMO op; const char *name; } ops[] = {
        { FITMO_DRAGO03, "Drago03" }, { FITMO_REINHARD05, "Reinhard05" }, { FITMO_FATTAL02, "Fattal02" } };
    static const BYTE primaries[3] = { 1, 9, 2 };
    unsigned i, p;
    char what[128];
    FITAG *kept = NULL;
    printf("tone mapped images\n");
    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        FIBITMAP *light, *dst;
        BYTE cicp[4];

        light = makeFloat(FIT_RGBF, 64, 48, 0, 4);
        FreeImage_SetMetadataKeyValue(FIMD_EXIF_MAIN, light, "InterColorProfile", "the input's");
        dst = FreeImage_ToneMapping(light, ops[i].op, 0, 0);
        CHECK(dst && cicp_transfer(dst, cicp) == -1, "%s, untagged light: the output has a CICP tag", ops[i].name);
        CHECK(dst && icc_size(dst) == 0, "%s, untagged light: the output has a profile", ops[i].name);
        CHECK(dst && !FreeImage_GetMetadata(FIMD_EXIF_MAIN, dst, "InterColorProfile", &kept), "%s, untagged light: the input's Exif InterColorProfile tag kept", ops[i].name);
        FreeImage_Unload(dst);
        FreeImage_Unload(light);

        for (p = 0; p < 3; p++) {
            light = makeFloat(FIT_RGBF, 64, 48, 0, 4);
            set_cicp(light, primaries[p], 8, 0, 1);
            FreeImage_SetMetadataKeyValue(FIMD_EXIF_MAIN, light, "InterColorProfile", "the input's");
            dst = FreeImage_ToneMapping(light, ops[i].op, 0, 0);
            snprintf(what, sizeof(what), "%s, linear light in primaries %d", ops[i].name, primaries[p]);
            CHECK(dst != NULL, "%s: not tone mapped", what);
            if (dst) {
                CHECK(cicp_transfer(dst, cicp) == 13 && cicp[0] == primaries[p] && cicp[2] == 0 && cicp[3] == 1,
                      "%s: the output's CICP tag is %d/%d/%d/%d", what, cicp[0], cicp[1], cicp[2], cicp[3]);
                CHECK(cicp_transfer(light, cicp) == 8, "%s: the input's tag changed", what);
                check_display_samples(dst, what);
                if (code_xy(primaries[p]) && (primaries[p] != 1)) {
                    check_code_profile(dst, primaries[p], 13, ref_srgb, what);
                } else {
                    CHECK(icc_size(dst) == 0, "%s: a profile on display samples taken for sRGB", what);
                }
                CHECK(!FreeImage_GetMetadata(FIMD_EXIF_MAIN, dst, "InterColorProfile", &kept), "%s: the input's Exif InterColorProfile tag kept", what);
                FreeImage_Unload(dst);
            }
            FreeImage_Unload(light);
        }

        /* 16-bit linear light, as camera RAW decodes */
        light = make16(FIT_RGB16, 64, 48, FULL);
        set_cicp(light, 1, 8, 0, 1);
        dst = FreeImage_ToneMapping(light, ops[i].op, 0, 0);
        snprintf(what, sizeof(what), "%s, RGB16 linear light", ops[i].name);
        CHECK(dst && cicp_transfer(dst, cicp) == 13 && cicp[0] == 1, "%s: the output is not described as display samples", what);
        if (dst) check_display_samples(dst, what);
        FreeImage_Unload(dst);
        FreeImage_Unload(light);
    }

    {
        const char *path = "../../HDR-tests/canon_eos_70d_02.cr2";
        FIBITMAP *dib = FreeImage_Load(FIF_RAW, path, RAW_DEFAULT | RAW_HALFSIZE), *dst;
        if (dib) {
            BYTE cicp[4];
            dst = FreeImage_ToneMapping(dib, FITMO_REINHARD05, 0, 0);
            CHECK(dst && cicp_transfer(dst, cicp) == 13 && cicp[0] == 1, "CR2 at 16 bits, tone mapped: the output is not described as display samples");
            FreeImage_Unload(dst);
            FreeImage_Unload(dib);
        } else {
            printf("  skipped, not found: %s\n", path);
        }
    }
}

/* ----------------------------------------------------------------------------------------------
   the tone mapping operators' output from light an ICC profile describes: its colorants, the sRGB curve
   ---------------------------------------------------------------------------------------------- */

static cmsHPROFILE named(cmsHPROFILE h, const char *name) {
    if (name) {
        cmsMLU *mlu = cmsMLUalloc(NULL, 1);
        cmsMLUsetASCII(mlu, "en", "US", name);
        cmsWriteTag(h, cmsSigProfileDescriptionTag, mlu);
        cmsMLUfree(mlu);
    } else {
        cmsWriteTag(h, cmsSigProfileDescriptionTag, NULL);
    }
    return h;
}

/* ROMM RGB, ProPhoto's primaries, with a linear tone curve */
static cmsHPROFILE linear_prophoto(const char *name) {
    cmsCIExyY d50 = { 0.3457, 0.3585, 1.0 };
    cmsCIExyYTRIPLE romm = { { 0.7347, 0.2653, 1.0 }, { 0.1596, 0.8404, 1.0 }, { 0.0366, 0.0001, 1.0 } };
    cmsToneCurve *linear = cmsBuildGamma(NULL, 1.0), *curves[3];
    cmsHPROFILE h;
    curves[0] = curves[1] = curves[2] = linear;
    h = cmsCreateRGBProfile(&d50, &romm, curves);
    cmsFreeToneCurve(linear);
    return named(h, name);
}

static int attach_file(FIBITMAP *dib, const char *path) {
    static BYTE data[65536];
    long size;
    FILE *f = fopen(path, "rb");
    if (!f) {
        printf("  skipped, not found: %s\n", path);
        return 0;
    }
    size = (long)fread(data, 1, sizeof(data), f);
    fclose(f);
    FreeImage_CreateICCProfile(dib, data, size);
    return 1;
}

/* the output carries the colorants of the input's profile with the sRGB tone curve, named after it, the same bytes on every run */
static void check_colorant_profile(FIBITMAP *dst, FIBITMAP *light, const char *what) {
    static const cmsTagSignature tags[3] = { cmsSigRedColorantTag, cmsSigGreenColorantTag, cmsSigBlueColorantTag };
    static const BYTE date[12] = { 0x07, 0xEA, 0x00, 0x01, 0x00, 0x01, 0, 0, 0, 0, 0, 0 };
    static const BYTE none[4] = { 0, 0, 0, 0 };
    cmsHPROFILE h = open_icc(dst), in = open_icc(light);
    char name[256] = "", desc[256] = "", want[300];
    int c, same = 1;
    CHECK(h != NULL, "%s: no ICC profile", what);
    if (h && in) {
        const BYTE *bytes = (const BYTE *)FreeImage_GetICCProfile(dst)->data;
        for (c = 0; c < 3; c++) {
            const cmsCIEXYZ *a = (const cmsCIEXYZ *)cmsReadTag(h, tags[c]), *b = (const cmsCIEXYZ *)cmsReadTag(in, tags[c]);
            if (!a || !b || fabs(a->X - b->X) > 2e-4 || fabs(a->Y - b->Y) > 2e-4 || fabs(a->Z - b->Z) > 2e-4) same = 0;
        }
        CHECK(same, "%s: not the input's colorants", what);
        CHECK(curves_are(h, ref_srgb), "%s: not the sRGB tone curve", what);
        CHECK(cmsReadTag(h, cmsSigcicpTag) == NULL, "%s: a cicp tag, with no code points to name", what);
        cmsGetProfileInfoASCII(in, cmsInfoDescription, "en", "US", name, sizeof(name));
        cmsGetProfileInfoASCII(h, cmsInfoDescription, "en", "US", desc, sizeof(desc));
        snprintf(want, sizeof(want), "%s, sRGB tone curve (FreeImage)", name[0] ? name : "RGB");
        CHECK(strcmp(desc, want) == 0, "%s: named '%s', not '%s'", what, desc, want);
        CHECK(memcmp(bytes + 24, date, 12) == 0 && memcmp(bytes + 40, none, 4) == 0, "%s: the profile's date or platform is not fixed", what);
    }
    if (h) cmsCloseProfile(h);
    if (in) cmsCloseProfile(in);
}

/* RGB to XYZ of primaries against their white, row-major */
static void rgb_to_xyz(const double xy[8], double m[9]) {
    double p[9], inv[9], s[3], w[3], det;
    int i;
    for (i = 0; i < 3; i++) {
        p[i] = xy[2 * i] / xy[2 * i + 1];
        p[3 + i] = 1;
        p[6 + i] = (1 - xy[2 * i] - xy[2 * i + 1]) / xy[2 * i + 1];
    }
    w[0] = xy[6] / xy[7]; w[1] = 1; w[2] = (1 - xy[6] - xy[7]) / xy[7];
    det = p[0] * (p[4] * p[8] - p[5] * p[7]) - p[1] * (p[3] * p[8] - p[5] * p[6]) + p[2] * (p[3] * p[7] - p[4] * p[6]);
    inv[0] = (p[4] * p[8] - p[5] * p[7]) / det; inv[1] = (p[2] * p[7] - p[1] * p[8]) / det; inv[2] = (p[1] * p[5] - p[2] * p[4]) / det;
    inv[3] = (p[5] * p[6] - p[3] * p[8]) / det; inv[4] = (p[0] * p[8] - p[2] * p[6]) / det; inv[5] = (p[2] * p[3] - p[0] * p[5]) / det;
    inv[6] = (p[3] * p[7] - p[4] * p[6]) / det; inv[7] = (p[1] * p[6] - p[0] * p[7]) / det; inv[8] = (p[0] * p[4] - p[1] * p[3]) / det;
    for (i = 0; i < 3; i++) s[i] = inv[3 * i] * w[0] + inv[3 * i + 1] * w[1] + inv[3 * i + 2] * w[2];
    for (i = 0; i < 9; i++) m[i] = p[i] * s[i % 3];
}

static void invert3(const double a[9], double out[9]) {
    const double det = a[0] * (a[4] * a[8] - a[5] * a[7]) - a[1] * (a[3] * a[8] - a[5] * a[6]) + a[2] * (a[3] * a[7] - a[4] * a[6]);
    out[0] = (a[4] * a[8] - a[5] * a[7]) / det; out[1] = (a[2] * a[7] - a[1] * a[8]) / det; out[2] = (a[1] * a[5] - a[2] * a[4]) / det;
    out[3] = (a[5] * a[6] - a[3] * a[8]) / det; out[4] = (a[0] * a[8] - a[2] * a[6]) / det; out[5] = (a[2] * a[3] - a[0] * a[5]) / det;
    out[6] = (a[3] * a[7] - a[4] * a[6]) / det; out[7] = (a[1] * a[6] - a[0] * a[7]) / det; out[8] = (a[0] * a[4] - a[1] * a[3]) / det;
}

static double encode_srgb(double v) {
    v = fmin(fmax(v, 0), 1);
    return (v <= 0.0031308) ? 12.92 * v : 1.055 * pow(v, 1 / 2.4) - 0.055;
}

/* the output converted to sRGB through its profile, against display samples in primaries of the same white written out here */
static void check_shown_as(FIBITMAP *dst, const double xy[8], const char *what) {
    DWORD size = 0;
    const void *srgb = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_SRGB, &size);
    FIBITMAP *shown = FreeImage_ConvertToICCProfile(dst, srgb, size, FICMS_INTENT_RELATIVE_COLORIMETRIC);
    double from[9], to[9], to_inv[9], m[9];
    unsigned x, y, worst = 0, w = FreeImage_GetWidth(dst), h = FreeImage_GetHeight(dst);
    int i;
    CHECK(shown != NULL, "%s: not converted to sRGB", what);
    if (!shown) return;
    rgb_to_xyz(xy, from);
    rgb_to_xyz(code_xy(1), to);
    invert3(to, to_inv);
    for (i = 0; i < 9; i++) m[i] = to_inv[3 * (i / 3)] * from[i % 3] + to_inv[3 * (i / 3) + 1] * from[3 + i % 3] + to_inv[3 * (i / 3) + 2] * from[6 + i % 3];
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        RGBQUAD q, s;
        double lin[3];
        int got[3], c;
        FreeImage_GetPixelColor(dst, x, y, &q);
        FreeImage_GetPixelColor(shown, x, y, &s);
        lin[0] = ref_srgb(q.rgbRed / 255.0); lin[1] = ref_srgb(q.rgbGreen / 255.0); lin[2] = ref_srgb(q.rgbBlue / 255.0);
        got[0] = s.rgbRed; got[1] = s.rgbGreen; got[2] = s.rgbBlue;
        for (c = 0; c < 3; c++) {
            const int d = abs(got[c] - (int)floor(encode_srgb(m[3 * c] * lin[0] + m[3 * c + 1] * lin[1] + m[3 * c + 2] * lin[2]) * 255 + 0.5));
            if ((unsigned)d > worst) worst = (unsigned)d;
        }
    }
    CHECK(worst <= 2, "%s: shown in sRGB through its profile, up to %u steps off the colors written out here", what, worst);
    FreeImage_Unload(shown);
}

static void test_tonemapped_profiles(void) {
    static const struct { FREE_IMAGE_TMO op; const char *name; } ops[] = {
        { FITMO_DRAGO03, "Drago03" }, { FITMO_REINHARD05, "Reinhard05" }, { FITMO_FATTAL02, "Fattal02" } };
    static const double adobe[8] = { 0.64, 0.33, 0.21, 0.71, 0.15, 0.06, 0.3127, 0.3290 };
    FIBITMAP *light, *dst, *first = NULL, *lin;
    BYTE cicp[4];
    unsigned i;
    char what[128];
    printf("tone mapped images, light an ICC profile describes\n");

    for (i = 0; i < sizeof(ops) / sizeof(ops[0]); i++) {
        light = makeFloat(FIT_RGBF, 64, 48, 0, 4);
        attach_profile(light, linear_prophoto("Linear ProPhoto RGB"));
        dst = FreeImage_ToneMapping(light, ops[i].op, 0, 0);
        snprintf(what, sizeof(what), "%s, linear ProPhoto light", ops[i].name);
        CHECK(dst != NULL, "%s: not tone mapped", what);
        if (dst) {
            check_colorant_profile(dst, light, what);
            CHECK(cicp_transfer(dst, cicp) == -1, "%s: the output has a CICP tag", what);
            if (first) {
                const FIICCPROFILE *a = FreeImage_GetICCProfile(first), *b = FreeImage_GetICCProfile(dst);
                CHECK(a->size == b->size && b->data && !memcmp(a->data, b->data, a->size), "%s: not the bytes Drago03 gave", what);
                FreeImage_Unload(dst);
            } else {
                first = dst;
            }
        }
        FreeImage_Unload(light);
    }
    FreeImage_Unload(first);

    /* the CICP tag's primaries outrank the profile; unspecified ones leave it to the profile */
    light = makeFloat(FIT_RGBF, 64, 48, 0, 4);
    attach_profile(light, linear_prophoto("Linear ProPhoto RGB"));
    set_cicp(light, 12, 8, 0, 1);
    dst = FreeImage_ToneMapping(light, FITMO_REINHARD05, 0, 0);
    if (dst) check_code_profile(dst, 12, 13, ref_srgb, "linear ProPhoto light tagged Display P3");
    FreeImage_Unload(dst);
    set_cicp(light, 2, 8, 0, 1);
    dst = FreeImage_ToneMapping(light, FITMO_REINHARD05, 0, 0);
    if (dst) check_colorant_profile(dst, light, "linear ProPhoto light tagged with unspecified primaries");
    FreeImage_Unload(dst);
    set_cicp(light, 1, 8, 0, 1);
    dst = FreeImage_ToneMapping(light, FITMO_REINHARD05, 0, 0);
    CHECK(dst && icc_size(dst) == 0, "linear ProPhoto light tagged BT.709: the output has a profile");
    FreeImage_Unload(dst);
    set_cicp(light, 10, 8, 0, 1);
    dst = FreeImage_ToneMapping(light, FITMO_REINHARD05, 0, 0);
    CHECK(dst && icc_size(dst) == 0, "linear ProPhoto light tagged XYZ: the output has a profile");
    FreeImage_Unload(dst);
    FreeImage_Unload(light);

    /* a profile without a description; RGB16 light */
    light = makeFloat(FIT_RGBF, 64, 48, 0, 4);
    attach_profile(light, linear_prophoto(NULL));
    dst = FreeImage_ToneMapping(light, FITMO_DRAGO03, 0, 0);
    if (dst) check_colorant_profile(dst, light, "linear ProPhoto light, no description");
    FreeImage_Unload(dst);
    FreeImage_Unload(light);
    light = make16(FIT_RGB16, 64, 48, FULL);
    attach_profile(light, linear_prophoto("Linear ProPhoto RGB"));
    dst = FreeImage_ToneMapping(light, FITMO_DRAGO03, 0, 0);
    if (dst) check_colorant_profile(dst, light, "RGB16 linear ProPhoto light");
    FreeImage_Unload(dst);
    FreeImage_Unload(light);

    /* no profile: sRGB's colorants, a LUT-based profile, grey, CMYK */
    light = makeFloat(FIT_RGBF, 64, 48, 0, 4);
    attach_builtin(light, FICMS_PROFILE_LINEAR_SRGB);
    dst = FreeImage_ToneMapping(light, FITMO_DRAGO03, 0, 0);
    CHECK(dst && icc_size(dst) == 0, "linear sRGB light: the output has a profile");
    FreeImage_Unload(dst);
    if (attach_file(light, "../ICC/data/test5.icc")) {
        dst = FreeImage_ToneMapping(light, FITMO_DRAGO03, 0, 0);
        CHECK(dst && icc_size(dst) == 0, "light in a v2 profile of sRGB's colorants: the output has a profile");
        FreeImage_Unload(dst);
    }
    if (attach_file(light, "../ICC/data/test3.icc")) {
        dst = FreeImage_ToneMapping(light, FITMO_DRAGO03, 0, 0);
        CHECK(dst && icc_size(dst) == 0, "light in a LUT-based profile: the output has a profile");
        FreeImage_Unload(dst);
    }
    FreeImage_DestroyICCProfile(light);
    attach_profile(light, linear_prophoto("Linear ProPhoto RGB"));
    FreeImage_GetICCProfile(light)->flags |= FIICC_COLOR_IS_CMYK;
    dst = FreeImage_ToneMapping(light, FITMO_DRAGO03, 0, 0);
    CHECK(dst && icc_size(dst) == 0, "light flagged CMYK: the output has a profile");
    FreeImage_Unload(dst);
    FreeImage_Unload(light);
    light = makeFloat(FIT_FLOAT, 64, 48, 0, 4);
    attach_builtin(light, FICMS_PROFILE_LINEAR_GRAY);
    dst = FreeImage_ToneMapping(light, FITMO_DRAGO03, 0, 0);
    CHECK(dst && icc_size(dst) == 0, "grey light in a linear grey profile: the output has a profile");
    FreeImage_Unload(dst);
    FreeImage_Unload(light);

    /* an Adobe RGB image made linear, which keeps a linear copy of its profile, tone mapped, then shown in sRGB */
    light = make16(FIT_RGB16, 64, 48, FULL);
    attach_builtin(light, FICMS_PROFILE_ADOBE_RGB);
    lin = FreeImage_ConvertToLinear(light, 0);
    dst = lin ? FreeImage_ToneMapping(lin, FITMO_DRAGO03, 0, 0) : NULL;
    CHECK(dst != NULL, "Adobe RGB, linear: not tone mapped");
    if (dst) {
        cmsHPROFILE h = open_icc(dst);
        check_colorant_profile(dst, lin, "Adobe RGB, linear, tone mapped");
        CHECK(h && same_colorants(h, adobe), "Adobe RGB, linear, tone mapped: not Adobe RGB's colorants");
        if (h) cmsCloseProfile(h);
        check_shown_as(dst, adobe, "Adobe RGB, linear, tone mapped");
    }
    FreeImage_Unload(dst);
    FreeImage_Unload(lin);
    FreeImage_Unload(light);
}

int main(void) {
    FreeImage_Initialise(FALSE);
    test_types();
    test_sixteen();
    test_cicp_tag();
    test_unscaled();
    test_profiles();
    test_float();
    test_png();
    test_files();
    test_linear();
    test_linear_profiles();
    test_tonemapped();
    test_tonemapped_profiles();
    printf("%d checks, %d failures\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
