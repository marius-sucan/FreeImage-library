/* files that describe their colors with CICP code points alone (AVIF and HEIF nclx boxes, PNG cICP chunks)
   load with the profile of their code points */
#include "common.h"

/* ------------------------------------------------------------------ helpers */

static int cicp_of(FIBITMAP *dib, BYTE out[4]) {
    FITAG *tag = NULL;
    if (!dib || !FreeImage_GetMetadata(FIMD_CUSTOM, dib, "CICP", &tag) || !tag) return 0;
    if (FreeImage_GetTagType(tag) != FIDT_BYTE || FreeImage_GetTagCount(tag) != 4) return 0;
    memcpy(out, FreeImage_GetTagValue(tag), 4);
    return 1;
}

static int has_cicp(FIBITMAP *dib, BYTE p, BYTE t, BYTE m, BYTE f) {
    BYTE c[4];
    return cicp_of(dib, c) && c[0] == p && c[1] == t && c[2] == m && c[3] == f;
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

static int profile_is(FIBITMAP *dib, const BYTE *data, DWORD size) {
    FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
    return icc->data && (DWORD)icc->size == size && !memcmp(icc->data, data, size);
}

static unsigned long long pixel_digest(FIBITMAP *dib) {
    unsigned long long h = 1469598103934665603ull;
    unsigned y;
    if (!dib || !FreeImage_HasPixels(dib)) return 0;
    for (y = 0; y < FreeImage_GetHeight(dib); y++) h = fnv(h, FreeImage_GetScanLine(dib, y), FreeImage_GetLine(dib));
    if (FreeImage_GetPalette(dib)) h = fnv(h, FreeImage_GetPalette(dib), FreeImage_GetColorsUsed(dib) * sizeof(RGBQUAD));
    return h;
}

static FIBITMAP *load_bytes(FREE_IMAGE_FORMAT fif, Bytes b, int flags) {
    FIMEMORY *mem = FreeImage_OpenMemory(b.data, b.size);
    FIBITMAP *dib = mem ? FreeImage_LoadFromMemory(fif, mem, flags) : NULL;
    FreeImage_CloseMemory(mem);
    return dib;
}

static Bytes from_memory(FIMEMORY *mem) {
    Bytes out = { NULL, 0 };
    BYTE *data = NULL;
    DWORD size = 0;
    if (FreeImage_AcquireMemory(mem, &data, &size) && size) {
        out.data = (BYTE *)malloc(size);
        memcpy(out.data, data, size);
        out.size = size;
    }
    return out;
}

static Bytes save_bytes(FREE_IMAGE_FORMAT fif, FIBITMAP *dib, int flags) {
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    Bytes out = { NULL, 0 };
    if (mem && FreeImage_SaveToMemory(fif, dib, mem, flags)) out = from_memory(mem);
    FreeImage_CloseMemory(mem);
    return out;
}

/* every nclx colr box of an ISOBMFF file given other primaries and transfer; the matrix stays */
static int patch_nclx(Bytes b, unsigned p, unsigned t) {
    int n = 0;
    DWORD i;
    for (i = 0; i + 16 <= b.size; i++) {
        if (!memcmp(b.data + i, "colrnclx", 8)) {
            b.data[i + 8] = (BYTE)(p >> 8); b.data[i + 9] = (BYTE)p;
            b.data[i + 10] = (BYTE)(t >> 8); b.data[i + 11] = (BYTE)t;
            n++;
        }
    }
    return n;
}

static Bytes patched(const char *path, unsigned p, unsigned t) {
    Bytes b = read_file(path);
    if (b.size && !patch_nclx(b, p, t)) { free(b.data); b.data = NULL; b.size = 0; }
    return b;
}

/* ------------------------------------------------------------------ PNG chunks */

static unsigned crc_table[256];

static unsigned crc32_of(const BYTE *p, size_t n) {
    unsigned c = 0xFFFFFFFFu;
    size_t i;
    if (!crc_table[1]) {
        unsigned k, j;
        for (k = 0; k < 256; k++) {
            unsigned v = k;
            for (j = 0; j < 8; j++) v = (v & 1) ? 0xEDB88320u ^ (v >> 1) : v >> 1;
            crc_table[k] = v;
        }
    }
    for (i = 0; i < n; i++) c = crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

static void put32(BYTE *p, unsigned v) { p[0] = (BYTE)(v >> 24); p[1] = (BYTE)(v >> 16); p[2] = (BYTE)(v >> 8); p[3] = (BYTE)v; }

/* the PNG with a cICP chunk of all four code points right after IHDR */
static Bytes with_cicp4(Bytes png, BYTE p, BYTE t, BYTE m, BYTE f) {
    const BYTE data[4] = { p, t, m, f };
    Bytes out = { NULL, 0 };
    BYTE *c;
    if (png.size < 33 || memcmp(png.data + 12, "IHDR", 4)) return out;
    out.size = png.size + 16;
    out.data = (BYTE *)malloc(out.size);
    memcpy(out.data, png.data, 33);
    c = out.data + 33;
    put32(c, 4);
    memcpy(c + 4, "cICP", 4);
    memcpy(c + 8, data, 4);
    put32(c + 12, crc32_of(c + 4, 8));
    memcpy(c + 16, png.data + 33, png.size - 33);
    return out;
}

static Bytes with_cicp(Bytes png, BYTE p, BYTE t) {
    return with_cicp4(png, p, t, 0, 1);
}

/* ------------------------------------------------------------------ a reference made without the library */

/* ITU-T H.273 table 2: red, green, blue and white xy */
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

/* ITU-T H.273 table 3, signal to light */
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

/* the profile of the code points, from a 4096-point curve */
static cmsHPROFILE reference_profile(BYTE p, BYTE t, int grey) {
    static float table[4096];
    double xy[8];
    cmsToneCurve *curve;
    cmsHPROFILE h = NULL;
    int i;
    for (i = 0; i < 4096; i++) table[i] = (float)from_signal(t, i / 4095.0);
    curve = cmsBuildTabulatedToneCurveFloat(NULL, 4096, table);
    if (grey) {
        h = cmsCreateGrayProfile(cmsD50_xyY(), curve);
    } else if (primaries_xy(p, xy)) {
        cmsCIExyY white = { xy[6], xy[7], 1.0 };
        cmsCIExyYTRIPLE rgb = { { xy[0], xy[1], 1.0 }, { xy[2], xy[3], 1.0 }, { xy[4], xy[5], 1.0 } };
        cmsToneCurve *curves[3] = { curve, curve, curve };
        h = cmsCreateRGBProfile(&white, &rgb, curves);
    }
    cmsFreeToneCurve(curve);
    return h;
}

/* the largest CIE76 difference between the attached profile and the reference, over a grid of signals */
static double profile_error(FIBITMAP *dib, BYTE p, BYTE t, int grey) {
    FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
    cmsHPROFILE got = icc->data ? cmsOpenProfileFromMem(icc->data, icc->size) : NULL;
    cmsHPROFILE want = reference_profile(p, t, grey);
    cmsHPROFILE lab = cmsCreateLab4Profile(NULL);
    const cmsUInt32Number format = grey ? TYPE_GRAY_DBL : TYPE_RGB_DBL;
    cmsHTRANSFORM a = got ? cmsCreateTransform(got, format, lab, TYPE_Lab_DBL, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE) : NULL;
    cmsHTRANSFORM b = want ? cmsCreateTransform(want, format, lab, TYPE_Lab_DBL, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE) : NULL;
    double worst = 1e9;
    if (a && b) {
        const int n = grey ? 0 : 8;
        int r, g, k;
        worst = 0;
        for (r = 0; r <= 64; r++) for (g = 0; g <= n; g++) for (k = 0; k <= n; k++) {
            double in[3];
            cmsCIELab x, y;
            in[0] = r / 64.0; in[1] = g / 8.0; in[2] = k / 8.0;
            cmsDoTransform(a, in, &x, 1);
            cmsDoTransform(b, in, &y, 1);
            if (cmsDeltaE(&x, &y) > worst) worst = cmsDeltaE(&x, &y);
        }
    }
    if (a) cmsDeleteTransform(a);
    if (b) cmsDeleteTransform(b);
    if (got) cmsCloseProfile(got);
    if (want) cmsCloseProfile(want);
    cmsCloseProfile(lab);
    return worst;
}

/* the profile's cicp tag holds the code points */
static int profile_cicp(FIBITMAP *dib, BYTE p, BYTE t) {
    FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
    cmsHPROFILE h = icc->data ? cmsOpenProfileFromMem(icc->data, icc->size) : NULL;
    const cmsVideoSignalType *v = h ? (const cmsVideoSignalType *)cmsReadTag(h, cmsSigcicpTag) : NULL;
    const int ok = v && v->ColourPrimaries == p && v->TransferCharacteristics == t && v->MatrixCoefficients == 0 && v->VideoFullRangeFlag == 1;
    if (h) cmsCloseProfile(h);
    return ok;
}

/* the profile of (p, t): its description, its color space, its colors; tp and tt are the code points it stands for */
static void described(const char *name, FIBITMAP *dib, BYTE tp, BYTE tt, int grey, const char *expected) {
    double error;
    CHECK(has_profile(dib), "%s: no profile", name);
    if (!has_profile(dib)) return;
    CHECK(!strcmp(description(dib), expected), "%s: profile '%s', expected '%s'", name, description(dib), expected);
    CHECK(space(dib) == (DWORD)(grey ? FICMS_COLORSPACE_GRAY : FICMS_COLORSPACE_RGB), "%s: profile color space %08lx", name, (unsigned long)space(dib));
    if (!grey) CHECK(profile_cicp(dib, tp, tt), "%s: the profile's cicp tag is not %u/%u/0/1", name, tp, tt);
    error = profile_error(dib, tp, tt, grey);
    CHECK(error < 0.05, "%s: the profile's colors are %.4f from the reference", name, error);
}

/* ------------------------------------------------------------------ AVIF and HEIF nclx boxes */

static const char *SOFA = "../AVIF/data/sofa_grid1x5_420.avif";		/* 1/13/6/1 */
static const char *ANIMATED = "../AVIF/data/colors-animated-8bpc.avif";	/* 1/13/6/1 */
static const char *HEVC = "../HEIF/data/hevc32.heif";					/* 1/13/6/1 */

/* the patched file: the profile, or none; pixels as the file has them either way; header-only alike */
static void nclx_case(FREE_IMAGE_FORMAT fif, const char *path, unsigned p, unsigned t, BYTE tp, BYTE tt, const char *expected) {
    char name[256];
    Bytes original = read_file(path), file = patched(path, p, t);
    FIBITMAP *plain = original.size ? load_bytes(fif, original, 0) : NULL;
    FIBITMAP *dib = file.size ? load_bytes(fif, file, 0) : NULL;
    FIBITMAP *head = file.size ? load_bytes(fif, file, FIF_LOAD_NOPIXELS) : NULL;
    BYTE c[4];
    snprintf(name, sizeof(name), "%s as %u/%u", path, p, t);
    CHECK(plain && dib && head, "%s: not loaded", name);
    if (plain && dib && head) {
        CHECK(cicp_of(dib, c) && c[0] == p && c[1] == t, "%s: CICP tag not %u/%u", name, p, t);
        CHECK(pixel_digest(plain) == pixel_digest(dib), "%s: pixels differ from the unpatched file's", name);
        CHECK(!has_profile(plain), "%s: the unpatched sRGB file has a profile '%s'", name, description(plain));
        if (expected) {
            described(name, dib, tp, tt, 0, expected);
        } else {
            CHECK(!has_profile(dib), "%s: a profile '%s'", name, description(dib));
        }
        CHECK(same_profile(dib, head), "%s: the header-only load has another profile", name);
        CHECK(has_cicp(head, c[0], c[1], c[2], c[3]), "%s: the header-only load has another CICP tag", name);
    }
    if (plain) FreeImage_Unload(plain);
    if (dib) FreeImage_Unload(dib);
    if (head) FreeImage_Unload(head);
    free(original.data);
    free(file.data);
}

static void nclx_boxes(void) {
    printf("AVIF and HEIF nclx boxes\n");
    nclx_case(FIF_AVIF, SOFA, 9, 1, 9, 1, "BT.2020 primaries, BT.709 tone curve (FreeImage)");
    nclx_case(FIF_AVIF, SOFA, 12, 13, 12, 13, "Display P3 primaries, sRGB tone curve (FreeImage)");
    nclx_case(FIF_AVIF, SOFA, 11, 13, 11, 13, "DCI-P3 primaries, sRGB tone curve (FreeImage)");
    nclx_case(FIF_AVIF, SOFA, 1, 4, 1, 4, "BT.709 primaries, gamma 2.2 tone curve (FreeImage)");
    nclx_case(FIF_AVIF, SOFA, 4, 5, 4, 5, "BT.470 System M primaries, gamma 2.8 tone curve (FreeImage)");
    nclx_case(FIF_AVIF, SOFA, 5, 6, 5, 6, "BT.601 625-line primaries, BT.601 tone curve (FreeImage)");
    nclx_case(FIF_AVIF, SOFA, 7, 7, 7, 7, "SMPTE 240M primaries, SMPTE 240M tone curve (FreeImage)");
    nclx_case(FIF_AVIF, SOFA, 8, 8, 8, 8, "Generic film primaries, linear tone curve (FreeImage)");
    nclx_case(FIF_AVIF, SOFA, 22, 15, 22, 15, "EBU Tech 3213-E primaries, BT.2020 tone curve (FreeImage)");
    /* an unspecified or unknown code point stands for sRGB's */
    nclx_case(FIF_AVIF, SOFA, 9, 2, 9, 13, "BT.2020 primaries, sRGB tone curve (FreeImage)");
    nclx_case(FIF_AVIF, SOFA, 2, 1, 1, 1, "BT.709 primaries, BT.709 tone curve (FreeImage)");
    nclx_case(FIF_AVIF, SOFA, 3, 14, 1, 14, "BT.709 primaries, BT.2020 tone curve (FreeImage)");
    nclx_case(FIF_AVIF, SOFA, 2, 2, 0, 0, NULL);
    nclx_case(FIF_AVIF, SOFA, 1, 13, 0, 0, NULL);
    /* PQ, HLG, SMPTE 428 and logarithmic curves get none */
    nclx_case(FIF_AVIF, SOFA, 9, 16, 0, 0, NULL);
    nclx_case(FIF_AVIF, SOFA, 9, 18, 0, 0, NULL);
    nclx_case(FIF_AVIF, SOFA, 11, 17, 0, 0, NULL);
    nclx_case(FIF_AVIF, SOFA, 1, 9, 0, 0, NULL);
    nclx_case(FIF_HEIF, HEVC, 9, 1, 9, 1, "BT.2020 primaries, BT.709 tone curve (FreeImage)");
    nclx_case(FIF_HEIF, HEVC, 12, 8, 12, 8, "Display P3 primaries, linear tone curve (FreeImage)");
    nclx_case(FIF_HEIF, HEVC, 9, 16, 0, 0, NULL);
}

/* files as they come: CICP-only ones gain the profile, a file's own profile stays */
static void real_files(void) {
    FIBITMAP *dib;
    Bytes file;
    printf("files as they come\n");
    dib = FreeImage_Load(FIF_AVIF, "../AVIF/data/draw_points_idat.avif", 0);
    CHECK(dib && has_cicp(dib, 1, 4, 6, 1), "draw_points_idat.avif: not loaded with its CICP tag 1/4/6/1");
    if (dib) { described("draw_points_idat.avif", dib, 1, 4, 0, "BT.709 primaries, gamma 2.2 tone curve (FreeImage)"); FreeImage_Unload(dib); }
    dib = FreeImage_Load(FIF_HEIF, "../HEIF/data/jpeg32.heif", 0);
    CHECK(dib && has_cicp(dib, 6, 6, 6, 1), "jpeg32.heif: not loaded with its CICP tag 6/6/6/1");
    if (dib) { described("jpeg32.heif", dib, 6, 6, 0, "BT.601 525-line primaries, BT.601 tone curve (FreeImage)"); FreeImage_Unload(dib); }

    /* an ICC profile in the file wins over its code points */
    file = read_file("../AVIF/data/paris_icc_exif_xmp.avif");
    dib = file.size ? load_bytes(FIF_AVIF, file, 0) : NULL;
    CHECK(dib != NULL, "paris_icc_exif_xmp.avif: not loaded");
    if (dib) {
        DWORD i;
        int found = 0;
        for (i = 4; i + 8 <= file.size; i++) {
            if (!memcmp(file.data + i, "colrprof", 8)) {
                const DWORD box = ((DWORD)file.data[i - 4] << 24) | ((DWORD)file.data[i - 3] << 16) | ((DWORD)file.data[i - 2] << 8) | file.data[i - 1];
                found = 1;
                CHECK(box > 12 && i - 4 + box <= file.size && profile_is(dib, file.data + i + 8, box - 12),
                      "paris_icc_exif_xmp.avif: the profile is not the file's ('%s')", description(dib));
                break;
            }
        }
        CHECK(found, "paris_icc_exif_xmp.avif: no prof colr box found");
        FreeImage_Unload(dib);
    }
    free(file.data);
}

/* ------------------------------------------------------------------ PNG cICP chunks */

#define PW 64
#define PH 16

/* every pixel layout a PNG loads as */
static FIBITMAP *png_image(int kind) {
    FIBITMAP *dib = NULL;
    unsigned x, y, i;
    switch (kind) {
        case 0: dib = FreeImage_Allocate(PW, PH, 8, 0, 0, 0); break;					/* grey */
        case 1: dib = FreeImage_AllocateT(FIT_UINT16, PW, PH, 16, 0, 0, 0); break;		/* 16-bit grey */
        case 2: dib = FreeImage_Allocate(PW, PH, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK); break;
        case 3: dib = FreeImage_Allocate(PW, PH, 8, 0, 0, 0); break;					/* colour palette */
        case 4: dib = FreeImage_AllocateT(FIT_RGB16, PW, PH, 48, 0, 0, 0); break;
        case 5: dib = FreeImage_Allocate(PW, PH, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK); break;
        case 6: dib = FreeImage_Allocate(PW, PH, 1, 0, 0, 0); break;					/* black and white */
    }
    if (!dib) return NULL;
    if (FreeImage_GetPalette(dib)) {
        RGBQUAD *pal = FreeImage_GetPalette(dib);
        const unsigned n = FreeImage_GetColorsUsed(dib);
        for (i = 0; i < n; i++) {
            const BYTE v = (BYTE)(i * 255 / (n - 1));
            pal[i].rgbRed = v;
            pal[i].rgbGreen = (kind == 3) ? (BYTE)(255 - v) : v;
            pal[i].rgbBlue = (kind == 3) ? (BYTE)(i * 7) : v;
        }
    }
    for (y = 0; y < PH; y++) {
        BYTE *line = FreeImage_GetScanLine(dib, y);
        if (kind == 6) {
            for (i = 0; i < FreeImage_GetLine(dib); i++) line[i] = (BYTE)(0x5A ^ (y * 37));
        } else if (kind == 1 || kind == 4) {
            WORD *w = (WORD *)line;
            for (i = 0; i < FreeImage_GetLine(dib) / 2; i++) w[i] = (WORD)(i * 1021 + y * 4099 + 16411 * (i % 3));
        } else {
            for (x = 0; x < FreeImage_GetLine(dib); x++) line[x] = (BYTE)(x * 5 + y * 67 + (x % 4) * 41);
            if (kind == 5) for (x = 0; x < PW; x++) line[x * 4 + FI_RGBA_ALPHA] = (BYTE)(64 + x * 3);
        }
    }
    return dib;
}

/* the palette of a PNG made a descending grey ramp: FreeImage writes min-is-white as grey */
static void descending_palette(Bytes png) {
    DWORD p = 8;
    while (p + 12 <= png.size) {
        const DWORD length = ((DWORD)png.data[p] << 24) | ((DWORD)png.data[p + 1] << 16) | ((DWORD)png.data[p + 2] << 8) | png.data[p + 3];
        if (!memcmp(png.data + p + 4, "PLTE", 4) && length >= 6 && p + 12 + length <= png.size) {
            const DWORD n = length / 3;
            DWORD i;
            for (i = 0; i < n; i++) memset(png.data + p + 8 + 3 * i, (int)(255 - i * 255 / (n - 1)), 3);
            put32(png.data + p + 8 + length, crc32_of(png.data + p + 4, 4 + length));
            return;
        }
        p += 12 + length;
    }
}

/* kinds as png_image(), and 7: grey, min-is-white */
static Bytes png_with_cicp(int kind, BYTE p, BYTE t) {
    FIBITMAP *dib = png_image((kind == 7) ? 3 : kind);
    Bytes png = dib ? save_bytes(FIF_PNG, dib, PNG_DEFAULT) : (Bytes){ NULL, 0 };
    Bytes out;
    if (kind == 7 && png.size) descending_palette(png);
    out = png.size ? with_cicp(png, p, t) : png;
    if (dib) FreeImage_Unload(dib);
    free(png.data);
    return out;
}

static void png_case(const char *name, int kind, BYTE p, BYTE t, BYTE tp, BYTE tt, int grey, const char *expected) {
    Bytes file = png_with_cicp(kind, p, t);
    FIBITMAP *dib = file.size ? load_bytes(FIF_PNG, file, PNG_DEFAULT) : NULL;
    FIBITMAP *head = file.size ? load_bytes(FIF_PNG, file, FIF_LOAD_NOPIXELS) : NULL;
    CHECK(dib && head, "%s: not loaded", name);
    if (dib && head) {
        CHECK(has_cicp(dib, p, t, 0, 1), "%s: no CICP tag %u/%u/0/1", name, p, t);
        if (expected) {
            described(name, dib, tp, tt, grey, expected);
        } else {
            CHECK(!has_profile(dib), "%s: a profile '%s'", name, description(dib));
        }
        CHECK(same_profile(dib, head), "%s: the header-only load has another profile", name);
    }
    if (dib) FreeImage_Unload(dib);
    if (head) FreeImage_Unload(head);
    free(file.data);
}

/* kinds as png_image(), with an iCCP chunk of profile when it is given and a cICP chunk of p/t/m/f when cicp is set */
static Bytes png_file(int kind, Bytes profile, int cicp, BYTE p, BYTE t, BYTE m, BYTE f) {
    FIBITMAP *dib = png_image(kind);
    Bytes png = { NULL, 0 }, out;
    if (dib && profile.size) FreeImage_CreateICCProfile(dib, profile.data, profile.size);
    if (dib) png = save_bytes(FIF_PNG, dib, PNG_DEFAULT);
    if (dib) FreeImage_Unload(dib);
    if (!cicp || !png.size) return png;
    out = with_cicp4(png, p, t, m, f);
    free(png.data);
    return out;
}

static int same_tag(FIBITMAP *a, FIBITMAP *b) {
    BYTE x[4], y[4];
    const int ha = cicp_of(a, x), hb = cicp_of(b, y);
    return ha == hb && (!ha || !memcmp(x, y, 4));
}

/* PNG 3, 4.3: a cICP chunk FreeImage reads outranks the iCCP chunk, one it cannot read gives way to it; the file loads
   as the PNG of the winning chunk alone, header-only too. expected names the code points' profile, NULL none */
static void precedence_case(const char *name, int kind, Bytes profile, BYTE p, BYTE t, BYTE m, BYTE f, int outranks,
                            BYTE tp, BYTE tt, int grey, const char *expected) {
    const Bytes none = { NULL, 0 };
    Bytes both = png_file(kind, profile, 1, p, t, m, f);
    Bytes alone = outranks ? png_file(kind, none, 1, p, t, m, f) : png_file(kind, profile, 0, 0, 0, 0, 0);
    FIBITMAP *dib = both.size ? load_bytes(FIF_PNG, both, PNG_DEFAULT) : NULL;
    FIBITMAP *head = both.size ? load_bytes(FIF_PNG, both, FIF_LOAD_NOPIXELS) : NULL;
    FIBITMAP *want = alone.size ? load_bytes(FIF_PNG, alone, PNG_DEFAULT) : NULL;
    BYTE c[4];
    CHECK(dib && head && want, "%s: not loaded", name);
    if (dib && head && want) {
        const char *winner = outranks ? "cICP" : "iCCP";
        CHECK(pixel_digest(dib) == pixel_digest(want), "%s: pixels differ from the PNG of its %s alone", name, winner);
        CHECK(same_profile(dib, want), "%s: profile '%s', not the one of the PNG of its %s alone", name, description(dib), winner);
        CHECK(same_tag(dib, want), "%s: another CICP tag than the PNG of its %s alone", name, winner);
        CHECK(same_profile(dib, head) && same_tag(dib, head), "%s: the header-only load has another profile or CICP tag", name);
        if (outranks) {
            CHECK(has_cicp(dib, p, t, m, f), "%s: no CICP tag %u/%u/%u/%u", name, p, t, m, f);
            if (expected) {
                described(name, dib, tp, tt, grey, expected);
            } else {
                CHECK(!has_profile(dib), "%s: a profile '%s'", name, description(dib));
            }
        } else {
            CHECK(!cicp_of(dib, c), "%s: a CICP tag beside the iCCP profile", name);
            CHECK(profile_is(dib, profile.data, profile.size), "%s: the profile is not the iCCP one ('%s')", name, description(dib));
        }
    }
    if (dib) FreeImage_Unload(dib);
    if (head) FreeImage_Unload(head);
    if (want) FreeImage_Unload(want);
    free(both.data);
    free(alone.data);
}

static void png_precedence(void) {
    Bytes adobe = builtin(FICMS_PROFILE_ADOBE_RGB), gray = make_gray_profile(1.8);
    printf("PNG cICP and iCCP chunks\n");
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 9/1", 2, adobe, 9, 1, 0, 1, 1, 9, 1, 0, "BT.2020 primaries, BT.709 tone curve (FreeImage)");
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 12/13", 2, adobe, 12, 13, 0, 1, 1, 12, 13, 0, "Display P3 primaries, sRGB tone curve (FreeImage)");
    precedence_case("palette PNG, Adobe RGB iCCP, cICP 12/1", 3, adobe, 12, 1, 0, 1, 1, 12, 1, 0, "Display P3 primaries, BT.709 tone curve (FreeImage)");
    precedence_case("48-bit PNG, Adobe RGB iCCP, cICP 9/8", 4, adobe, 9, 8, 0, 1, 1, 9, 8, 0, "BT.2020 primaries, linear tone curve (FreeImage)");
    precedence_case("RGBA PNG, Adobe RGB iCCP, cICP 9/4", 5, adobe, 9, 4, 0, 1, 1, 9, 4, 0, "BT.2020 primaries, gamma 2.2 tone curve (FreeImage)");
    precedence_case("grey PNG, grey iCCP, cICP 1/1", 0, gray, 1, 1, 0, 1, 1, 0, 1, 1, "Gray, BT.709 tone curve (FreeImage)");
    precedence_case("16-bit grey PNG, grey iCCP, cICP 1/8", 1, gray, 1, 8, 0, 1, 1, 0, 8, 1, "Gray, linear tone curve (FreeImage)");
    precedence_case("black and white PNG, grey iCCP, cICP 1/4", 6, gray, 1, 4, 0, 1, 1, 0, 4, 1, "Gray, gamma 2.2 tone curve (FreeImage)");
    /* a grey's primaries do not matter */
    precedence_case("grey PNG, grey iCCP, cICP 2/1", 0, gray, 2, 1, 0, 1, 1, 0, 1, 1, "Gray, BT.709 tone curve (FreeImage)");
    /* sRGB, PQ and HLG outrank it too, and leave no profile */
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 1/13", 2, adobe, 1, 13, 0, 1, 1, 0, 0, 0, NULL);
    precedence_case("grey PNG, grey iCCP, cICP 9/13", 0, gray, 9, 13, 0, 1, 1, 0, 0, 1, NULL);
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 9/16", 2, adobe, 9, 16, 0, 1, 1, 0, 0, 0, NULL);
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 9/18", 2, adobe, 9, 18, 0, 1, 1, 0, 0, 0, NULL);
    /* a matrix (libpng drops such a chunk), a range flag or code points FreeImage cannot read */
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 9/1 with matrix 1", 2, adobe, 9, 1, 1, 1, 0, 0, 0, 0, NULL);
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 9/1 with range flag 2", 2, adobe, 9, 1, 0, 2, 0, 0, 0, 0, NULL);
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 2/1", 2, adobe, 2, 1, 0, 1, 0, 0, 0, 0, NULL);
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 3/1", 2, adobe, 3, 1, 0, 1, 0, 0, 0, 0, NULL);
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 10/17", 2, adobe, 10, 17, 0, 1, 0, 0, 0, 0, NULL);
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 9/2", 2, adobe, 9, 2, 0, 1, 0, 0, 0, 0, NULL);
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 9/0", 2, adobe, 9, 0, 0, 1, 0, 0, 0, 0, NULL);
    precedence_case("RGB PNG, Adobe RGB iCCP, cICP 9/19", 2, adobe, 9, 19, 0, 1, 0, 0, 0, 0, NULL);
    precedence_case("grey PNG, grey iCCP, cICP 1/2", 0, gray, 1, 2, 0, 1, 0, 0, 0, 0, NULL);
    /* without an iCCP chunk, such code points stay in the CICP tag */
    png_case("RGB PNG 2/1", 2, 2, 1, 1, 1, 0, "BT.709 primaries, BT.709 tone curve (FreeImage)");
    free(gray.data);
}

static void png_chunks(void) {
    Bytes file = { NULL, 0 };
    FIBITMAP *dib;
    printf("PNG cICP chunks\n");
    png_case("grey PNG 1/1", 0, 1, 1, 0, 1, 1, "Gray, BT.709 tone curve (FreeImage)");
    png_case("16-bit grey PNG 9/8", 1, 9, 8, 0, 8, 1, "Gray, linear tone curve (FreeImage)");
    png_case("black and white PNG 1/4", 6, 1, 4, 0, 4, 1, "Gray, gamma 2.2 tone curve (FreeImage)");
    png_case("min-is-white PNG 1/1", 7, 1, 1, 0, 1, 1, "Gray, BT.709 tone curve (FreeImage)");
    file = png_with_cicp(7, 1, 1);
    dib = file.size ? load_bytes(FIF_PNG, file, PNG_DEFAULT) : NULL;
    CHECK(dib && FreeImage_GetColorType(dib) == FIC_MINISWHITE, "min-is-white PNG: color type %d", dib ? (int)FreeImage_GetColorType(dib) : -1);
    if (dib) FreeImage_Unload(dib);
    free(file.data);
    file.data = NULL;
    file.size = 0;
    png_case("RGB PNG 9/1", 2, 9, 1, 9, 1, 0, "BT.2020 primaries, BT.709 tone curve (FreeImage)");
    png_case("palette PNG 12/13", 3, 12, 13, 12, 13, 0, "Display P3 primaries, sRGB tone curve (FreeImage)");
    png_case("48-bit PNG 9/4", 4, 9, 4, 9, 4, 0, "BT.2020 primaries, gamma 2.2 tone curve (FreeImage)");
    png_case("RGBA PNG 12/1", 5, 12, 1, 12, 1, 0, "Display P3 primaries, BT.709 tone curve (FreeImage)");
    /* none for FreeImage's grey and sRGB, whatever a grey's primaries, nor for PQ */
    png_case("grey PNG 9/13", 0, 9, 13, 0, 0, 1, NULL);
    png_case("RGB PNG 1/13", 2, 1, 13, 0, 0, 0, NULL);
    png_case("RGB PNG 9/16", 2, 9, 16, 0, 0, 0, NULL);
}

/* ------------------------------------------------------------------ pages of multi-page files */

static const char *BT2020 = "BT.2020 primaries, BT.709 tone curve (FreeImage)";

/* the AVIF animation as 9/1: every locked page has the profile; pages saved without being locked carry it too */
static void pages(void) {
    Bytes file = patched(ANIMATED, 9, 1), tiff = { NULL, 0 };
    FIMEMORY *mem = file.size ? FreeImage_OpenMemory(file.data, file.size) : NULL;
    FIMULTIBITMAP *multi = mem ? FreeImage_LoadMultiBitmapFromMemory(FIF_AVIF, mem, 0) : NULL;
    int count = multi ? FreeImage_GetPageCount(multi) : 0, i;
    printf("pages\n");
    CHECK(count >= 2, "%s as 9/1: %d pages", ANIMATED, count);
    for (i = 0; i < count; i++) {
        FIBITMAP *page = FreeImage_LockPage(multi, i);
        CHECK(page && !strcmp(description(page), BT2020), "%s as 9/1, page %d: profile '%s'", ANIMATED, i, description(page));
        if (page) FreeImage_UnlockPage(multi, page, FALSE);
    }
    if (multi) {
        FIMEMORY *out = FreeImage_OpenMemory(NULL, 0);
        if (FreeImage_SaveMultiBitmapToMemory(FIF_TIFF, multi, out, 0)) tiff = from_memory(out);
        FreeImage_CloseMemory(out);
        FreeImage_CloseMultiBitmap(multi, 0);
    }
    FreeImage_CloseMemory(mem);
    CHECK(tiff.size > 0, "%s as 9/1: not saved as TIFF", ANIMATED);
    if (tiff.size) {
        FIMEMORY *in = FreeImage_OpenMemory(tiff.data, tiff.size);
        FIMULTIBITMAP *back = FreeImage_LoadMultiBitmapFromMemory(FIF_TIFF, in, 0);
        CHECK(back && FreeImage_GetPageCount(back) == count, "%s as 9/1: the TIFF has %d pages", ANIMATED, back ? FreeImage_GetPageCount(back) : -1);
        for (i = 0; back && i < FreeImage_GetPageCount(back); i++) {
            FIBITMAP *page = FreeImage_LockPage(back, i);
            CHECK(page && !strcmp(description(page), BT2020), "%s as 9/1, TIFF page %d: profile '%s'", ANIMATED, i, description(page));
            if (page) FreeImage_UnlockPage(back, page, FALSE);
        }
        if (back) FreeImage_CloseMultiBitmap(back, 0);
        FreeImage_CloseMemory(in);
    }
    free(file.data);
    free(tiff.data);
}

/* ------------------------------------------------------------------ FIF_LOAD_DISPLAY_ICC */

/* a load with the flag is the load without it, converted for the display */
static void display_case(const char *name, FREE_IMAGE_FORMAT fif, Bytes file, int flags) {
    FIBITMAP *plain = load_bytes(fif, file, flags);
    FIBITMAP *shown = load_bytes(fif, file, flags | FIF_LOAD_DISPLAY_ICC);
    FIBITMAP *converted = plain ? FreeImage_Clone(plain) : NULL;
    CHECK(plain && shown && converted, "%s: not loaded", name);
    if (plain && shown && converted) {
        CHECK(FreeImage_ApplyDisplayICCProfile(converted), "%s: not converted for the display", name);
        CHECK(pixel_digest(shown) == pixel_digest(converted), "%s: the display load is not the converted load", name);
        CHECK(pixel_digest(shown) != pixel_digest(plain), "%s: the display load kept the file's pixels", name);
    }
    if (plain) FreeImage_Unload(plain);
    if (shown) FreeImage_Unload(shown);
    if (converted) FreeImage_Unload(converted);
}

/* every page locked with the flag is the page locked without it, converted for the display */
static void display_pages(const char *name, FREE_IMAGE_FORMAT fif, Bytes file) {
    FIMEMORY *m1 = FreeImage_OpenMemory(file.data, file.size), *m2 = FreeImage_OpenMemory(file.data, file.size);
    FIMULTIBITMAP *plain = (m1 && file.size) ? FreeImage_LoadMultiBitmapFromMemory(fif, m1, 0) : NULL;
    FIMULTIBITMAP *shown = (m2 && file.size) ? FreeImage_LoadMultiBitmapFromMemory(fif, m2, FIF_LOAD_DISPLAY_ICC) : NULL;
    const int count = plain ? FreeImage_GetPageCount(plain) : 0;
    int i, changed = 0;
    CHECK(count >= 2 && shown && FreeImage_GetPageCount(shown) == count, "%s: pages not opened", name);
    for (i = 0; shown && i < count; i++) {
        FIBITMAP *a = FreeImage_LockPage(plain, i), *b = FreeImage_LockPage(shown, i);
        FIBITMAP *c = a ? FreeImage_Clone(a) : NULL;
        CHECK(a && b && c && FreeImage_ApplyDisplayICCProfile(c), "%s, page %d: not converted", name, i);
        if (a && b && c) {
            CHECK(pixel_digest(b) == pixel_digest(c), "%s, page %d: the display page is not the converted page", name, i);
            changed += (pixel_digest(b) != pixel_digest(a));
        }
        if (c) FreeImage_Unload(c);
        if (a) FreeImage_UnlockPage(plain, a, FALSE);
        if (b) FreeImage_UnlockPage(shown, b, FALSE);
    }
    /* a black page converts to itself */
    CHECK(changed > 0, "%s: every display page kept the file's pixels", name);
    if (plain) FreeImage_CloseMultiBitmap(plain, 0);
    if (shown) FreeImage_CloseMultiBitmap(shown, 0);
    FreeImage_CloseMemory(m1);
    FreeImage_CloseMemory(m2);
}

static Bytes apng_with_cicp(BYTE p, BYTE t);

static void display(void) {
    Bytes p3 = builtin(FICMS_PROFILE_DISPLAY_P3), srgb = builtin(FICMS_PROFILE_SRGB), adobe = builtin(FICMS_PROFILE_ADOBE_RGB);
    Bytes avif = patched(SOFA, 9, 1), heif = patched(HEVC, 12, 8), grey = png_with_cicp(0, 1, 1), rgb = png_with_cicp(2, 9, 1);
    Bytes anim = patched(ANIMATED, 9, 1), apng = apng_with_cicp(9, 1), both = png_file(2, adobe, 1, 9, 1, 0, 1);
    printf("FIF_LOAD_DISPLAY_ICC\n");
    CHECK(FreeImage_SetDisplayICCProfile(p3.data, p3.size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION), "Display P3 not taken as the display");
    display_case("AVIF 9/1 on Display P3", FIF_AVIF, avif, 0);
    display_case("HEIF 12/8 on Display P3", FIF_HEIF, heif, 0);
    display_case("grey PNG 1/1 on Display P3", FIF_PNG, grey, 0);
    display_case("RGB PNG 9/1 on Display P3", FIF_PNG, rgb, 0);
    display_case("RGB PNG with an Adobe RGB iCCP and a 9/1 cICP on Display P3", FIF_PNG, both, 0);
    free(both.data);
    display_pages("AVIF animation 9/1 on Display P3", FIF_AVIF, anim);
    display_pages("APNG 9/1 on Display P3", FIF_APNG, apng);
    free(anim.data);
    free(apng.data);
    /* on an sRGB display the code points still convert */
    CHECK(FreeImage_SetDisplayICCProfile(srgb.data, srgb.size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION), "sRGB not taken as the display");
    display_case("AVIF 9/1 on sRGB", FIF_AVIF, avif, 0);
    display_case("grey PNG 1/1 on sRGB", FIF_PNG, grey, 0);
    FreeImage_SetDisplayICCProfile(NULL, 0, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION);
    free(avif.data);
    free(heif.data);
    free(grey.data);
    free(rgb.data);
}

/* ------------------------------------------------------------------ saving */

/* the nclx boxes' full range flag cleared */
static void narrow_nclx(Bytes b) {
    DWORD i;
    for (i = 0; i + 16 <= b.size; i++) {
        if (!memcmp(b.data + i, "colrnclx", 8)) b.data[i + 14] &= 0x7F;
    }
}

/* the cICP chunk's full range flag cleared */
static void narrow_cicp(Bytes png) {
    DWORD i;
    for (i = 33; i + 12 <= png.size; i++) {
        if (!memcmp(png.data + i, "cICP", 4)) {
            png.data[i + 7] = 0;
            put32(png.data + i + 8, crc32_of(png.data + i, 8));
            return;
        }
    }
}

/* the file's image saved as a lossless JPEG XL keeps its pixels, its colors as the encoding of p/t or as its profile */
static void jxl_case(const char *name, FREE_IMAGE_FORMAT fif, Bytes file, int encoded, BYTE p, BYTE t) {
    FIBITMAP *dib = file.size ? load_bytes(fif, file, 0) : NULL;
    Bytes jxl = dib ? save_bytes(FIF_JXL, dib, JXL_LOSSLESS) : (Bytes){ NULL, 0 };
    FIBITMAP *b = jxl.size ? load_bytes(FIF_JXL, jxl, 0) : NULL;
    CHECK(dib && b && pixel_digest(dib) == pixel_digest(b), "%s saved as JPEG XL: not loaded back with its pixels", name);
    if (dib && b) {
        if (encoded) {
            CHECK(has_cicp(b, p, t, 0, 1), "%s saved as JPEG XL: not encoded as %u/%u", name, p, t);
            CHECK(!strstr(description(b), "(FreeImage)"), "%s saved as JPEG XL: the ICC profile was written ('%s')", name, description(b));
        } else {
            CHECK(has_profile(dib) && same_profile(dib, b), "%s saved as JPEG XL: profile '%s'", name, description(b));
        }
    }
    if (dib) FreeImage_Unload(dib);
    if (b) FreeImage_Unload(b);
    free(jxl.data);
}

/* PNG keeps the profile as iCCP; JPEG XL writes code points as their encoding, but for narrow-range RGB */
static void saves(void) {
    Bytes avif = patched(SOFA, 9, 1), heif = patched(HEVC, 9, 1), pq = read_file("../AVIF/data/seine_hdr_rec2020.avif");
    Bytes rgb = png_with_cicp(2, 9, 1), grey = png_with_cicp(0, 1, 4), narrow = png_with_cicp(2, 9, 1);
    FIBITMAP *a = load_bytes(FIF_AVIF, avif, 0);
    Bytes png = a ? save_bytes(FIF_PNG, a, PNG_DEFAULT) : (Bytes){ NULL, 0 };
    FIBITMAP *b = png.size ? load_bytes(FIF_PNG, png, PNG_DEFAULT) : NULL;
    printf("saving\n");
    CHECK(a && b && same_profile(a, b) && pixel_digest(a) == pixel_digest(b), "AVIF 9/1 saved as PNG: profile '%s'", description(b));
    narrow_nclx(heif);
    narrow_cicp(narrow);
    jxl_case("RGB PNG 9/1", FIF_PNG, rgb, 1, 9, 1);
    jxl_case("grey PNG 1/4", FIF_PNG, grey, 1, 1, 4);
    /* a YUV file's decoder undid its matrix and range */
    jxl_case("AVIF 9/1", FIF_AVIF, avif, 1, 9, 1);
    jxl_case("narrow-range HEIF 9/1", FIF_HEIF, heif, 1, 9, 1);
    jxl_case("PQ AVIF", FIF_AVIF, pq, 1, 9, 16);
    /* narrow-range RGB is not expanded: its profile goes as an ICC */
    jxl_case("narrow-range PNG 9/1", FIF_PNG, narrow, 0, 9, 1);
    if (a) FreeImage_Unload(a);
    if (b) FreeImage_Unload(b);
    free(png.data);
    free(avif.data);
    free(heif.data);
    free(pq.data);
    free(rgb.data);
    free(grey.data);
    free(narrow.data);
}

static const char *scratch(const char *name) {
    static char buf[512];
    const char *dir = getenv("ICC_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

/* two opaque frames that differ at every pixel, as an APNG with a cICP chunk */
static Bytes apng_with_cicp(BYTE p, BYTE t) {
    const char *path = scratch("cicp_anim.png");
    FIMULTIBITMAP *multi = FreeImage_OpenMultiBitmap(FIF_APNG, path, TRUE, FALSE, TRUE, 0);
    Bytes apng = { NULL, 0 }, out = { NULL, 0 };
    unsigned x, y, k;
    if (!multi) return out;
    for (k = 0; k < 2; k++) {
        FIBITMAP *frame = FreeImage_Allocate(PW, PH, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
        if (!frame) continue;
        for (y = 0; y < PH; y++) {
            BYTE *line = FreeImage_GetScanLine(frame, y);
            for (x = 0; x < PW; x++) {
                line[x * 4 + FI_RGBA_RED] = (BYTE)(x * 4 + k * 101);
                line[x * 4 + FI_RGBA_GREEN] = (BYTE)(y * 16 + k * 53);
                line[x * 4 + FI_RGBA_BLUE] = (BYTE)((x + y) * 3 + k * 29 + 1);
                line[x * 4 + FI_RGBA_ALPHA] = 255;
            }
        }
        FreeImage_AppendPage(multi, frame);
        FreeImage_Unload(frame);
    }
    if (FreeImage_CloseMultiBitmap(multi, 0)) {
        apng = read_file(path);
        out = apng.size ? with_cicp(apng, p, t) : apng;
    }
    remove(path);
    free(apng.data);
    return out;
}

/* an APNG with a cICP chunk saved as an animated JPEG XL: the frames keep their pixels, the file the code points */
static void animation(void) {
    Bytes apng = apng_with_cicp(9, 1), jxl = { NULL, 0 };
    FIMEMORY *mem = apng.size ? FreeImage_OpenMemory(apng.data, apng.size) : NULL;
    FIMULTIBITMAP *multi = mem ? FreeImage_LoadMultiBitmapFromMemory(FIF_APNG, mem, 0) : NULL;
    unsigned long long frames[2] = { 0, 0 };
    int i;
    printf("animation\n");
    CHECK(multi && FreeImage_GetPageCount(multi) == 2, "APNG 9/1: not 2 pages");
    for (i = 0; multi && i < 2; i++) {
        FIBITMAP *page = FreeImage_LockPage(multi, i);
        CHECK(page && has_cicp(page, 9, 1, 0, 1) && !strcmp(description(page), BT2020), "APNG 9/1, page %d: profile '%s'", i, description(page));
        frames[i] = pixel_digest(page);
        if (page) FreeImage_UnlockPage(multi, page, FALSE);
    }
    if (multi) {
        FIMEMORY *out = FreeImage_OpenMemory(NULL, 0);
        if (FreeImage_SaveMultiBitmapToMemory(FIF_JXL, multi, out, JXL_LOSSLESS)) jxl = from_memory(out);
        FreeImage_CloseMemory(out);
        FreeImage_CloseMultiBitmap(multi, 0);
    }
    FreeImage_CloseMemory(mem);
    CHECK(jxl.size > 0, "APNG 9/1: not saved as JPEG XL");
    if (jxl.size) {
        FIMEMORY *in = FreeImage_OpenMemory(jxl.data, jxl.size);
        FIMULTIBITMAP *back = FreeImage_LoadMultiBitmapFromMemory(FIF_JXL, in, 0);
        CHECK(back && FreeImage_GetPageCount(back) == 2, "APNG 9/1 as JPEG XL: %d pages", back ? FreeImage_GetPageCount(back) : -1);
        for (i = 0; back && i < 2 && i < FreeImage_GetPageCount(back); i++) {
            FIBITMAP *page = FreeImage_LockPage(back, i);
            CHECK(page && has_cicp(page, 9, 1, 0, 1), "APNG 9/1 as JPEG XL, page %d: not encoded as 9/1", i);
            CHECK(page && pixel_digest(page) == frames[i], "APNG 9/1 as JPEG XL, page %d: the pixels changed", i);
            if (page) FreeImage_UnlockPage(back, page, FALSE);
        }
        if (back) FreeImage_CloseMultiBitmap(back, 0);
        FreeImage_CloseMemory(in);
    }
    free(apng.data);
    free(jxl.data);
}

int main(void) {
    FreeImage_SetOutputMessage(on_message);
    nclx_boxes();
    real_files();
    png_chunks();
    png_precedence();
    pages();
    display();
    saves();
    animation();
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
