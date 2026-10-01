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
    int got = FreeImage_MustTonemap(dib, file);
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
    CHECK(FreeImage_MustTonemapU(NULL, NULL) == FITM_ERROR, "NULL through the U entry");
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
    CHECK(FreeImage_MustTonemapU(dib, NULL) == FITM_NONE, "RGB16 through the U entry");
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
    expect(dib, NULL, FITM_REQUIRED, "header-only RGB16 tagged PQ");
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
        { 16, FITM_REQUIRED, "PQ" },
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
        if (cases[i].transfer == 16) CHECK(FreeImage_MustTonemapU(dib, L"x.png") == FITM_REQUIRED, "PQ through the U entry");
        FreeImage_Unload(dib);
    }
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
    expect(dib, NULL, FITM_REQUIRED, "a PQ profile with its cicp tag");
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
        { 16, FITM_REQUIRED, "PQ" },
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
    expect(dib, NULL, FITM_REQUIRED, "header-only PNG with cICP PQ");
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
        { "../AVIF/data/seine_hdr_rec2020.avif", 0, FITM_REQUIRED, 16, "10-bit AVIF, BT.2020 PQ" },
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

int main(void) {
    FreeImage_Initialise(FALSE);
    test_types();
    test_sixteen();
    test_cicp_tag();
    test_profiles();
    test_float();
    test_png();
    test_files();
    printf("%d checks, %d failures\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
