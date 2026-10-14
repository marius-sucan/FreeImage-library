/* JPEG XL writer tests: round trips of every type, metadata, animations, the page cache, refusals */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "FreeImage.h"
#include "pattern.h"
#ifdef JXL_TEST_LIBJXL
#include <jxl/decode.h>
#endif

static int g_failures = 0;
static char g_last[512];

#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); g_failures++; } } while (0)

static void message(FREE_IMAGE_FORMAT fif, const char *msg) {
    snprintf(g_last, sizeof(g_last), "%s", msg);
}

static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("JXL_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

static int same_pixels(FIBITMAP *a, FIBITMAP *b) {
    unsigned y, h;
    if (!a || !b || FreeImage_GetImageType(a) != FreeImage_GetImageType(b) || FreeImage_GetBPP(a) != FreeImage_GetBPP(b) ||
        FreeImage_GetWidth(a) != FreeImage_GetWidth(b) || (h = FreeImage_GetHeight(a)) != FreeImage_GetHeight(b)) return 0;
    for (y = 0; y < h; y++)
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), FreeImage_GetLine(a))) return 0;
    return 1;
}

/* save to memory, load back */
static FIBITMAP *roundtrip(FIBITMAP *d, int save_flags, int load_flags, DWORD *size) {
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    FIBITMAP *r = NULL;
    BYTE *data; DWORD n = 0;
    if (FreeImage_SaveToMemory(FIF_JXL, d, mem, save_flags)) {
        FreeImage_AcquireMemory(mem, &data, &n);
        FreeImage_SeekMemory(mem, 0, SEEK_SET);
        if (FreeImage_GetFileTypeFromMemory(mem, 0) == FIF_JXL)
            r = FreeImage_LoadFromMemory(FIF_JXL, mem, load_flags);
    }
    if (size) *size = n;
    FreeImage_CloseMemory(mem);
    return r;
}

static double channel(FIBITMAP *d, unsigned x, unsigned y, int c) {
    const BYTE *row = FreeImage_GetScanLine(d, FreeImage_GetHeight(d) - 1 - y);
    static const int order[4] = { FI_RGBA_RED, FI_RGBA_GREEN, FI_RGBA_BLUE, FI_RGBA_ALPHA };
    switch (FreeImage_GetImageType(d)) {
        case FIT_BITMAP: { unsigned n = FreeImage_GetBPP(d) / 8; return (n == 1 ? row[x] : row[x * n + order[c]]) / 255.0; }
        case FIT_UINT16: return ((const WORD *)row)[x] / 65535.0;
        case FIT_RGB16: return ((const WORD *)row)[3 * x + c] / 65535.0;
        case FIT_RGBA16: return ((const WORD *)row)[4 * x + c] / 65535.0;
        case FIT_FLOAT: return ((const float *)row)[x];
        case FIT_RGBF: return ((const float *)row)[3 * x + c];
        case FIT_RGBAF: return ((const float *)row)[4 * x + c];
        default: return 0;
    }
}

static int has_alpha(FIBITMAP *d) {
    FREE_IMAGE_TYPE t = FreeImage_GetImageType(d);
    return t == FIT_RGBA16 || t == FIT_RGBAF || (t == FIT_BITMAP && FreeImage_GetBPP(d) == 32);
}

/* the colour under an alpha of 0 is the encoder's to choose */
static double psnr(FIBITMAP *a, FIBITMAP *b, int channels, double range) {
    unsigned x, y, w = FreeImage_GetWidth(a), h = FreeImage_GetHeight(a);
    double se = 0; int c, n = 0;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) for (c = 0; c < channels; c++) {
        double e;
        if (has_alpha(a) && channel(a, x, y, 3) == 0) continue;
        e = channel(a, x, y, c) - channel(b, x, y, c); se += e * e; n++;
    }
    return se == 0 ? 99 : 10 * log10(range * range / (se / n));
}

static const struct { FREE_IMAGE_TYPE type; unsigned bpp; const char *name; int channels; double range; } TYPES[] = {
    {FIT_BITMAP, 8, "8-bit grey", 1, 1}, {FIT_BITMAP, 24, "24-bit", 3, 1}, {FIT_BITMAP, 32, "32-bit", 3, 1},
    {FIT_UINT16, 16, "UINT16", 1, 1}, {FIT_RGB16, 48, "RGB16", 3, 1}, {FIT_RGBA16, 64, "RGBA16", 3, 1},
    {FIT_FLOAT, 32, "FLOAT", 1, 2}, {FIT_RGBF, 96, "RGBF", 3, 2}, {FIT_RGBAF, 128, "RGBAF", 3, 2},
};
#define NTYPES (sizeof(TYPES) / sizeof(TYPES[0]))

static void test_lossless(void) {
    static const unsigned sizes[][2] = { {1, 1}, {7, 3}, {257, 131} };
    unsigned t, s;
    int effort;
    for (t = 0; t < NTYPES; t++) for (s = 0; s < 3; s++) {
        FIBITMAP *d = pattern(TYPES[t].type, TYPES[t].bpp, sizes[s][0], sizes[s][1], (int)(t + s));
        FIBITMAP *r = roundtrip(d, JXL_LOSSLESS, 0, NULL);
        CHECK(same_pixels(d, r), "lossless %s %ux%u: not bit exact (%s)", TYPES[t].name, sizes[s][0], sizes[s][1], g_last);
        if (r) FreeImage_Unload(r);
        /* quality 100 is lossless too, at every effort */
        for (effort = 1; effort <= 9 && s == 2; effort += 4) {
            r = roundtrip(d, 100 | (effort << 16), 0, NULL);
            CHECK(same_pixels(d, r), "quality 100, effort %d, %s: not bit exact", effort, TYPES[t].name);
            if (r) FreeImage_Unload(r);
        }
        FreeImage_Unload(d);
    }
    printf("lossless round trips: done\n");
}

static void test_palettes(void) {
    /* a colour palette, with and without transparency, and a 1-bit black and white image */
    FIBITMAP *pal = FreeImage_Allocate(33, 17, 8, 0, 0, 0);
    FIBITMAP *bw = FreeImage_Allocate(33, 17, 1, 0, 0, 0);
    FIBITMAP *r, *want;
    RGBQUAD *p = FreeImage_GetPalette(pal);
    BYTE trns[256];
    unsigned x, y;
    for (x = 0; x < 256; x++) { p[x].rgbRed = (BYTE)x; p[x].rgbGreen = (BYTE)(255 - x); p[x].rgbBlue = (BYTE)(x * 7); trns[x] = (BYTE)(x & 1 ? 255 : 0); }
    for (y = 0; y < 17; y++) for (x = 0; x < 33; x++) FreeImage_GetScanLine(pal, y)[x] = (BYTE)(x * 7 + y);
    FreeImage_GetPalette(bw)[1].rgbRed = FreeImage_GetPalette(bw)[1].rgbGreen = FreeImage_GetPalette(bw)[1].rgbBlue = 255;
    for (y = 0; y < 17; y++) FreeImage_GetScanLine(bw, y)[0] = (BYTE)(0xA5 ^ y);

    r = roundtrip(pal, JXL_LOSSLESS, 0, NULL); want = FreeImage_ConvertTo24Bits(pal);
    CHECK(same_pixels(r, want), "8-bit colour palette: not the 24-bit colours");
    FreeImage_Unload(r); FreeImage_Unload(want);

    FreeImage_SetTransparencyTable(pal, trns, 256);
    r = roundtrip(pal, JXL_LOSSLESS, 0, NULL); want = FreeImage_ConvertTo32Bits(pal);
    CHECK(same_pixels(r, want), "8-bit transparent palette: not the 32-bit colours");
    FreeImage_Unload(r); FreeImage_Unload(want);

    r = roundtrip(bw, JXL_LOSSLESS, 0, NULL); want = FreeImage_ConvertToGreyscale(bw);
    CHECK(same_pixels(r, want) && FreeImage_GetColorType(r) == FIC_MINISBLACK, "1-bit: not 8-bit grey");
    FreeImage_Unload(r); FreeImage_Unload(want);
    FreeImage_Unload(pal); FreeImage_Unload(bw);
    printf("palettes: done\n");
}

static void test_lossy(void) {
    unsigned t;
    for (t = 0; t < NTYPES; t++) {
        FIBITMAP *d = pattern(TYPES[t].type, TYPES[t].bpp, 160, 120, 40 + (int)t);
        DWORD small = 0, big = 0;
        FIBITMAP *r = roundtrip(d, 0, 0, NULL);
        double p = r ? psnr(d, r, TYPES[t].channels, TYPES[t].range) : 0;
        CHECK(r && FreeImage_GetImageType(r) == TYPES[t].type && FreeImage_GetBPP(r) == TYPES[t].bpp, "lossy %s: type changed", TYPES[t].name);
        CHECK(p > 33, "lossy %s at the default quality: PSNR %.1f dB", TYPES[t].name, p);
        if (getenv("JXL_TEST_VERBOSE")) printf("    lossy %s: PSNR %.1f dB\n", TYPES[t].name, p);
        if (r) FreeImage_Unload(r);
        r = roundtrip(d, 30, 0, &small); if (r) FreeImage_Unload(r);
        r = roundtrip(d, 95, 0, &big); if (r) FreeImage_Unload(r);
        CHECK(small > 0 && small < big, "lossy %s: quality 30 gives %u bytes, quality 95 %u", TYPES[t].name, (unsigned)small, (unsigned)big);
        FreeImage_Unload(d);
    }
    printf("lossy round trips: done\n");
}

static int cicp_is(FIBITMAP *d, int p, int t) {
    FITAG *tag = NULL;
    const BYTE *v;
    if (!FreeImage_GetMetadata(FIMD_CUSTOM, d, "CICP", &tag) || !tag) return 0;
    v = (const BYTE *)FreeImage_GetTagValue(tag);
    return v[0] == p && v[1] == t;
}

static void set_cicp(FIBITMAP *d, BYTE p, BYTE t) {
    BYTE v[4] = { p, t, 0, 1 };
    FITAG *tag = FreeImage_CreateTag();
    FreeImage_SetTagKey(tag, "CICP"); FreeImage_SetTagType(tag, FIDT_BYTE);
    FreeImage_SetTagCount(tag, 4); FreeImage_SetTagLength(tag, 4); FreeImage_SetTagValue(tag, v);
    FreeImage_SetMetadata(FIMD_CUSTOM, d, "CICP", tag);
    FreeImage_DeleteTag(tag);
}

static void test_colors(void) {
    DWORD size;
    const void *p3 = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_DISPLAY_P3, &size);
    const void *grey;
    DWORD grey_size;
    FIBITMAP *d = pattern(FIT_BITMAP, 24, 64, 48, 50), *r;
    FIICCPROFILE *icc;

    FreeImage_CreateICCProfile(d, (void *)p3, size);
    r = roundtrip(d, JXL_LOSSLESS, 0, NULL);
    icc = r ? FreeImage_GetICCProfile(r) : NULL;
    CHECK(icc && icc->size == size && !memcmp(icc->data, p3, size), "lossless: the ICC profile is not kept byte for byte");
    if (r) FreeImage_Unload(r);
    /* lossy: libjxl stores Display P3 as an enum, read back as CICP 12/13 with a profile */
    r = roundtrip(d, 0, 0, NULL);
    CHECK(r && cicp_is(r, 12, 13) && FreeImage_GetICCProfile(r)->size > 0, "lossy Display P3: not CICP 12/13 with a profile");
    if (r) FreeImage_Unload(r);
    FreeImage_Unload(d);

    /* a grey profile on a grey image; an RGB profile on a grey image is not written */
    grey = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_LINEAR_GRAY, &grey_size);
    d = pattern(FIT_UINT16, 16, 40, 30, 51);
    FreeImage_CreateICCProfile(d, (void *)grey, grey_size);
    r = roundtrip(d, JXL_LOSSLESS, 0, NULL);
    icc = r ? FreeImage_GetICCProfile(r) : NULL;
    CHECK(icc && icc->size == grey_size, "lossless grey: the grey profile is not kept");
    if (r) FreeImage_Unload(r);
    FreeImage_DestroyICCProfile(d);
    FreeImage_CreateICCProfile(d, (void *)p3, size);
    r = roundtrip(d, JXL_LOSSLESS, 0, NULL);
    CHECK(r && cicp_is(r, 1, 13), "grey with an RGB profile: not written as sRGB grey");
    if (r) FreeImage_Unload(r);
    FreeImage_Unload(d);

    /* the CICP tag: PQ in Rec. 2100, lossless and lossy */
    d = pattern(FIT_RGB16, 48, 40, 30, 52);
    set_cicp(d, 9, 16);
    r = roundtrip(d, JXL_LOSSLESS, 0, NULL);
    CHECK(r && cicp_is(r, 9, 16) && same_pixels(d, r), "lossless PQ: CICP or pixels lost");
    if (r) FreeImage_Unload(r);
    r = roundtrip(d, 0, 0, NULL);
    CHECK(r && cicp_is(r, 9, 16), "lossy PQ: CICP lost");
    if (r) FreeImage_Unload(r);
    FreeImage_Unload(d);

    /* float images are linear light */
    d = pattern(FIT_RGBF, 96, 40, 30, 53);
    r = roundtrip(d, JXL_LOSSLESS, 0, NULL);
    CHECK(r && cicp_is(r, 1, 8), "float: not linear sRGB");
    if (r) FreeImage_Unload(r);
    FreeImage_Unload(d);
    printf("colours: done\n");
}

static void test_metadata(void) {
    FIBITMAP *src = FreeImage_Load(FIF_JPEG, "../exif.jpg", 0), *r;
    FITAG *a = NULL, *b = NULL;
    int flags[2] = { JXL_LOSSLESS, 0 }, i;
    if (!src) { printf("FAIL: ../exif.jpg not found\n"); g_failures++; return; }
    for (i = 0; i < 2; i++) {
        r = roundtrip(src, flags[i], 0, NULL);
        CHECK(r && FreeImage_GetMetadataCount(FIMD_EXIF_MAIN, r) == FreeImage_GetMetadataCount(FIMD_EXIF_MAIN, src) &&
              FreeImage_GetMetadataCount(FIMD_EXIF_EXIF, r) == FreeImage_GetMetadataCount(FIMD_EXIF_EXIF, src), "Exif tags lost (flags %x)", flags[i]);
        a = b = NULL;
        FreeImage_GetMetadata(FIMD_XMP, src, "XMLPacket", &a);
        if (r) FreeImage_GetMetadata(FIMD_XMP, r, "XMLPacket", &b);
        CHECK(a && b && FreeImage_GetTagLength(a) == FreeImage_GetTagLength(b) && !memcmp(FreeImage_GetTagValue(a), FreeImage_GetTagValue(b), FreeImage_GetTagLength(a)),
              "XMP not kept (flags %x)", flags[i]);
        a = b = NULL;
        FreeImage_GetMetadata(FIMD_EXIF_RAW, src, "ExifRaw", &a);
        if (r) FreeImage_GetMetadata(FIMD_EXIF_RAW, r, "ExifRaw", &b);
        CHECK(a && b && FreeImage_GetTagLength(a) == FreeImage_GetTagLength(b) && !memcmp(FreeImage_GetTagValue(a), FreeImage_GetTagValue(b), FreeImage_GetTagLength(a)),
              "the raw Exif block differs (flags %x)", flags[i]);
        if (r) FreeImage_Unload(r);
    }
    FreeImage_Unload(src);
    printf("metadata: done\n");
}

static void set_anim(FIBITMAP *d, WORD left, WORD top, LONG ms, BYTE disposal, BYTE blend, WORD lw, WORD lh, LONG loop) {
    struct { const char *key; FREE_IMAGE_MDTYPE type; DWORD len; const void *v; } t[] = {
        {"FrameLeft", FIDT_SHORT, 2, &left}, {"FrameTop", FIDT_SHORT, 2, &top}, {"FrameTime", FIDT_LONG, 4, &ms},
        {"DisposalMethod", FIDT_BYTE, 1, &disposal}, {"BlendMethod", FIDT_BYTE, 1, &blend},
        {"LogicalWidth", FIDT_SHORT, 2, &lw}, {"LogicalHeight", FIDT_SHORT, 2, &lh}, {"Loop", FIDT_LONG, 4, &loop},
    };
    unsigned i;
    for (i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        FITAG *tag = FreeImage_CreateTag();
        FreeImage_SetTagKey(tag, t[i].key); FreeImage_SetTagType(tag, t[i].type);
        FreeImage_SetTagCount(tag, 1); FreeImage_SetTagLength(tag, t[i].len); FreeImage_SetTagValue(tag, t[i].v);
        FreeImage_SetMetadata(FIMD_ANIMATION, d, t[i].key, tag);
        FreeImage_DeleteTag(tag);
    }
}

static long anim_tag(FIBITMAP *d, const char *key) {
    FITAG *tag = NULL;
    if (!FreeImage_GetMetadata(FIMD_ANIMATION, d, key, &tag) || !tag) return -1;
    switch (FreeImage_GetTagType(tag)) {
        case FIDT_BYTE: return *(const BYTE *)FreeImage_GetTagValue(tag);
        case FIDT_SHORT: return *(const WORD *)FreeImage_GetTagValue(tag);
        default: return *(const LONG *)FreeImage_GetTagValue(tag);
    }
}

/* GIF and APNG semantics on a straight-alpha RGBA canvas, in double */
typedef struct { double c[4]; } Px;

static void composite(Px *canvas, unsigned cw, unsigned ch, FIBITMAP *frame, unsigned x0, unsigned y0, int blend) {
    unsigned x, y, w = FreeImage_GetWidth(frame), h = FreeImage_GetHeight(frame);
    int c;
    for (y = 0; y < h && y0 + y < ch; y++) for (x = 0; x < w && x0 + x < cw; x++) {
        Px *dst = &canvas[(y0 + y) * cw + x0 + x], src;
        for (c = 0; c < 4; c++) src.c[c] = channel(frame, x, y, c);
        if (blend) {
            double a = src.c[3] + dst->c[3] * (1 - src.c[3]);
            for (c = 0; c < 3; c++) dst->c[c] = a > 0 ? (src.c[c] * src.c[3] + dst->c[c] * dst->c[3] * (1 - src.c[3])) / a : 0;
            dst->c[3] = a;
        } else *dst = src;
    }
}

#ifdef JXL_TEST_LIBJXL
/* a file's frames as libjxl stores them; stock libjxl refuses lossy frames that blend in ICC colours */
typedef struct { int xyb, icc, frames, cropped, blended, kept, timeless, black; } Layout;

static int layout(const char *path, Layout *l) {
    FILE *f = fopen(path, "rb");
    BYTE *data = NULL;
    long n = 0;
    int ok = 0;
    memset(l, 0, sizeof(*l));
    if (!f) return 0;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    data = malloc(n > 0 ? (size_t)n : 1);
    if (data && fread(data, 1, (size_t)n, f) == (size_t)n) {
        JxlDecoder *dec = JxlDecoderCreate(NULL);
        JxlDecoderSubscribeEvents(dec, JXL_DEC_BASIC_INFO | JXL_DEC_COLOR_ENCODING | JXL_DEC_FRAME);
        JxlDecoderSetCoalescing(dec, JXL_FALSE);
        JxlDecoderSetInput(dec, data, (size_t)n);
        JxlDecoderCloseInput(dec);
        for (;;) {
            JxlDecoderStatus st = JxlDecoderProcessInput(dec);
            if (st == JXL_DEC_BASIC_INFO) {
                JxlBasicInfo info;
                uint32_t i;
                JxlDecoderGetBasicInfo(dec, &info);
                l->xyb = !info.uses_original_profile;
                for (i = 0; i < info.num_extra_channels; i++) {
                    JxlExtraChannelInfo ec;
                    if (JxlDecoderGetExtraChannelInfo(dec, i, &ec) == JXL_DEC_SUCCESS && ec.type == JXL_CHANNEL_BLACK) l->black++;
                }
            } else if (st == JXL_DEC_COLOR_ENCODING) {
                JxlColorEncoding enc;
                l->icc = JxlDecoderGetColorAsEncodedProfile(dec, JXL_COLOR_PROFILE_TARGET_ORIGINAL, &enc) != JXL_DEC_SUCCESS;
            } else if (st == JXL_DEC_FRAME) {
                JxlFrameHeader h;
                JxlBlendInfo alpha;
                alpha.blendmode = JXL_BLEND_REPLACE;
                JxlDecoderGetFrameHeader(dec, &h);
                JxlDecoderGetExtraChannelBlendInfo(dec, 0, &alpha);
                l->frames++;
                l->cropped += h.layer_info.have_crop ? 1 : 0;
                l->blended += (h.layer_info.blend_info.blendmode != JXL_BLEND_REPLACE || alpha.blendmode != JXL_BLEND_REPLACE) ? 1 : 0;
                l->kept += h.layer_info.save_as_reference ? 1 : 0;
                l->timeless += h.duration ? 0 : 1;
            } else {
                ok = (st == JXL_DEC_SUCCESS);
                break;
            }
        }
        JxlDecoderDestroy(dec);
    }
    free(data);
    fclose(f);
    return ok;
}
#endif

static void test_animation(void) {
    static const struct { unsigned w, h; WORD x, y; LONG ms; BYTE disposal, blend; } fr[] = {
        {64, 48, 0, 0, 100, 1, 1}, {30, 20, 5, 6, 80, 2, 0}, {24, 16, 25, 20, 60, 3, 0}, {20, 20, 40, 4, 40, 1, 1},
        {28, 30, 10, 10, 30, 0, 0}, {64, 48, 0, 0, 20, 2, 0}, {16, 12, 50, 40, 10, 1, 0},
    };
    const unsigned n = sizeof(fr) / sizeof(fr[0]), cw = 64, ch = 48;
    /* profiles libjxl keeps for lossy frames: ProPhoto, wider than sRGB, and Instagram's c2, sRGB in all but name */
    enum { NONE, PROPHOTO, C2 };
    static const struct { const char *name; int flags; FREE_IMAGE_TYPE type; unsigned bpp; int profile; } pass[] = {
        {"lossless", JXL_LOSSLESS, FIT_BITMAP, 32, NONE}, {"lossy", 0, FIT_BITMAP, 32, NONE},
        {"lossy ProPhoto", 0, FIT_BITMAP, 32, PROPHOTO}, {"lossy c2", 0, FIT_BITMAP, 32, C2},
        {"lossy ProPhoto RGBA16", 0, FIT_RGBA16, 64, PROPHOTO}, {"lossy ProPhoto RGBAF", 0, FIT_RGBAF, 128, PROPHOTO},
    };
    const int npass = (int)(sizeof(pass) / sizeof(pass[0]));
    DWORD pro_size;
    const void *pro = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_PROPHOTO_RGB, &pro_size);
    FIBITMAP *c2 = FreeImage_Load(FIF_JXL, "data/test-jxl.jxl", FIF_LOAD_NOPIXELS);
    FIMULTIBITMAP *m;
    Px *canvas = calloc(cw * ch, sizeof(Px)), *saved = calloc(cw * ch, sizeof(Px));
    unsigned i, x, y;
    int f;

    CHECK(c2 && FreeImage_GetICCProfile(c2)->size == 524, "data/test-jxl.jxl: no c2 profile");
    for (f = 0; f < npass; f++) {
        m = FreeImage_OpenMultiBitmap(FIF_JXL, tmppath("fi_jxl_anim.jxl"), TRUE, FALSE, TRUE, 0);
        for (i = 0; i < n; i++) {
            FIBITMAP *d = pattern(pass[f].type, pass[f].bpp, fr[i].w, fr[i].h, 60 + (int)i);
            set_anim(d, fr[i].x, fr[i].y, fr[i].ms, fr[i].disposal, fr[i].blend, cw, ch, 2);
            if (pass[f].profile == PROPHOTO) FreeImage_CreateICCProfile(d, (void *)pro, (long)pro_size);
            if (pass[f].profile == C2 && c2) FreeImage_CreateICCProfile(d, FreeImage_GetICCProfile(c2)->data, FreeImage_GetICCProfile(c2)->size);
            CHECK(FreeImage_AppendPage(m, d), "AppendPage %u", i);
            FreeImage_Unload(d);
        }
        CHECK(FreeImage_CloseMultiBitmap(m, pass[f].flags), "animation %s: CloseMultiBitmap: %s", pass[f].name, g_last);
#ifdef JXL_TEST_LIBJXL
        {
            /* ProPhoto: every frame the whole canvas, replacing, kept for nothing; c2: sRGB, frames still cropped */
            Layout l;
            CHECK(layout(tmppath("fi_jxl_anim.jxl"), &l), "animation %s: libjxl cannot read it", pass[f].name);
            CHECK(!(l.xyb && l.icc && (l.cropped || l.blended || l.kept)), "animation %s: lossy frames blend in ICC colours", pass[f].name);
            if (pass[f].profile == PROPHOTO)
                CHECK(l.xyb && l.icc && l.frames == (int)n && !l.timeless, "animation %s: %d frames, %d without a duration, not %u whole frames", pass[f].name, l.frames, l.timeless, n);
            if (pass[f].profile == C2)
                CHECK(l.xyb && !l.icc && l.cropped > 0, "animation %s: not sRGB with cropped frames", pass[f].name);
        }
#endif

        memset(canvas, 0, cw * ch * sizeof(Px));
        m = FreeImage_OpenMultiBitmap(FIF_JXL, tmppath("fi_jxl_anim.jxl"), FALSE, TRUE, TRUE, 0);
        CHECK(m && FreeImage_GetPageCount(m) == (int)n, "animation: %d pages, not %u", m ? FreeImage_GetPageCount(m) : -1, n);
        for (i = 0; m && i < n; i++) {
            FIBITMAP *frame = pattern(pass[f].type, pass[f].bpp, fr[i].w, fr[i].h, 60 + (int)i);
            FIBITMAP *page = FreeImage_LockPage(m, (int)i);
            double worst = 0, se = 0;
            memcpy(saved, canvas, cw * ch * sizeof(Px));
            composite(canvas, cw, ch, frame, fr[i].x, fr[i].y, fr[i].blend == 0);
            if (!page) { CHECK(0, "animation %s page %u not locked", pass[f].name, i); FreeImage_Unload(frame); continue; }
            CHECK(FreeImage_GetWidth(page) == cw && FreeImage_GetHeight(page) == ch && FreeImage_GetImageType(page) == pass[f].type && FreeImage_GetBPP(page) == pass[f].bpp,
                  "animation %s page %u: not the %u-bit canvas", pass[f].name, i, pass[f].bpp);
            for (y = 0; y < ch; y++) for (x = 0; x < cw; x++) {
                const Px *p = &canvas[y * cw + x];
                int c;
                double a = channel(page, x, y, 3);
                if (fabs(a - p->c[3]) > worst) worst = fabs(a - p->c[3]);
                se += (a - p->c[3]) * (a - p->c[3]);
                if (p->c[3] > 0) for (c = 0; c < 3; c++) {
                    double e = fabs(channel(page, x, y, c) - p->c[c]) * p->c[3];
                    if (e > worst) worst = e;
                    se += e * e;
                }
            }
            if (pass[f].flags == JXL_LOSSLESS)
                CHECK(worst <= 1.01 / 255, "animation lossless page %u: off by %.4f from GIF/APNG compositing", i, worst);
            else
                CHECK(sqrt(se / (4.0 * cw * ch)) < 0.02, "animation %s page %u: RMS %.4f from GIF/APNG compositing", pass[f].name, i, sqrt(se / (4.0 * cw * ch)));
            if (pass[f].profile == PROPHOTO)
                CHECK(FreeImage_GetICCProfile(page)->size == (long)pro_size && !memcmp(FreeImage_GetICCProfile(page)->data, pro, pro_size), "animation %s page %u: not the ProPhoto profile", pass[f].name, i);
            if (pass[f].profile == C2)
                CHECK(cicp_is(page, 1, 13) && FreeImage_GetICCProfile(page)->size == 0, "animation %s page %u: not untagged sRGB", pass[f].name, i);
            if (getenv("JXL_TEST_VERBOSE")) printf("    %s page %u: worst %.4f RMS %.4f\n", pass[f].name, i, worst, sqrt(se / (4.0 * cw * ch)));
            CHECK(anim_tag(page, "FrameTime") == fr[i].ms && anim_tag(page, "Loop") == 2, "page %u: FrameTime %ld Loop %ld", i, anim_tag(page, "FrameTime"), anim_tag(page, "Loop"));
            FreeImage_UnlockPage(m, page, FALSE);
            FreeImage_Unload(frame);
            if (fr[i].disposal == 2) for (y = fr[i].y; y < fr[i].y + fr[i].h && y < ch; y++) for (x = fr[i].x; x < fr[i].x + fr[i].w && x < cw; x++) memset(&canvas[y * cw + x], 0, sizeof(Px));
            if (fr[i].disposal == 3) memcpy(canvas, saved, cw * ch * sizeof(Px));
        }
        if (m) FreeImage_CloseMultiBitmap(m, 0);
    }
    if (c2) FreeImage_Unload(c2);
    free(canvas); free(saved);
    printf("animations: done\n");
}

/* float frames with FreeImage's linear sRGB profile, which libjxl keeps: written as linear sRGB */
static void test_animation_float(void) {
    DWORD size;
    const void *linear = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_LINEAR_SRGB, &size);
    FIBITMAP *a = pattern(FIT_RGBAF, 128, 64, 48, 75), *b = pattern(FIT_RGBAF, 128, 32, 24, 76), *page;
    FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(FIF_JXL, tmppath("fi_jxl_animf.jxl"), TRUE, FALSE, TRUE, 0);
    double p = 0;
    set_anim(a, 0, 0, 50, 1, 0, 64, 48, 0);
    set_anim(b, 8, 8, 50, 1, 0, 64, 48, 0);
    FreeImage_CreateICCProfile(a, (void *)linear, (long)size);
    FreeImage_CreateICCProfile(b, (void *)linear, (long)size);
    FreeImage_AppendPage(m, a);
    FreeImage_AppendPage(m, b);
    CHECK(FreeImage_CloseMultiBitmap(m, 0), "float animation: %s", g_last);
    m = FreeImage_OpenMultiBitmap(FIF_JXL, tmppath("fi_jxl_animf.jxl"), FALSE, TRUE, TRUE, 0);
    page = m ? FreeImage_LockPage(m, 0) : NULL;
    if (page) p = psnr(a, page, 3, 2);
    CHECK(page && FreeImage_GetImageType(page) == FIT_RGBAF && cicp_is(page, 1, 8) && p > 33, "float animation: page 0 is not linear sRGB within 33 dB (%.1f)", p);
    if (getenv("JXL_TEST_VERBOSE")) printf("    float animation page 0: PSNR %.1f dB\n", p);
    if (page) FreeImage_UnlockPage(m, page, FALSE);
    if (m) FreeImage_CloseMultiBitmap(m, 0);
#ifdef JXL_TEST_LIBJXL
    {
        Layout l;
        CHECK(layout(tmppath("fi_jxl_animf.jxl"), &l) && l.xyb && !l.icc && l.cropped == 1, "float animation: not linear sRGB with a cropped frame");
    }
#endif
    FreeImage_Unload(a);
    FreeImage_Unload(b);
    printf("float animation: done\n");
}

/* the inks of a CMYK bitmap, bytes or words 0-3 */
static double ink(FIBITMAP *d, unsigned x, unsigned y, int c) {
    const BYTE *row = FreeImage_GetScanLine(d, FreeImage_GetHeight(d) - 1 - y);
    return FreeImage_GetImageType(d) == FIT_RGBA16 ? ((const WORD *)row)[4 * x + c] / 65535.0 : row[4 * x + c] / 255.0;
}

static double ink_psnr(FIBITMAP *a, FIBITMAP *b) {
    unsigned x, y, w = FreeImage_GetWidth(a), h = FreeImage_GetHeight(a);
    double se = 0; int c;
    if (!b || FreeImage_GetImageType(a) != FreeImage_GetImageType(b) || FreeImage_GetBPP(a) != FreeImage_GetBPP(b) ||
        w != FreeImage_GetWidth(b) || h != FreeImage_GetHeight(b)) return 0;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) for (c = 0; c < 4; c++) { double e = ink(a, x, y, c) - ink(b, x, y, c); se += e * e; }
    return se == 0 ? 99 : 10 * log10(4.0 * w * h / se);
}

/* r is CMYK with a's profile */
static int cmyk_kept(FIBITMAP *a, FIBITMAP *r) {
    FIICCPROFILE *p = FreeImage_GetICCProfile(a), *q = r ? FreeImage_GetICCProfile(r) : NULL;
    return q && FreeImage_GetColorType(r) == FIC_CMYK && p->size > 0 && q->size == p->size && !memcmp(q->data, p->data, p->size);
}

/* CMYK with a CMYK profile is written as CMYK: CMY samples, a black channel and the profile, never XYB */
static void test_cmyk(void) {
    static const struct { FREE_IMAGE_TYPE type; const char *name; } T[] = { {FIT_BITMAP, "8-bit CMYK"}, {FIT_RGBA16, "16-bit CMYK"} };
    static const unsigned sizes[][2] = { {1, 1}, {7, 3}, {257, 131} };
    FIBITMAP *press = FreeImage_Load(FIF_JXL, "data/cmyk8_lossless.jxl", FIF_LOAD_NOPIXELS | JXL_CMYK), *d, *r, *want;
    FIICCPROFILE *icc = press ? FreeImage_GetICCProfile(press) : NULL;
    FIMULTIBITMAP *m;
    DWORD small = 0, big = 0;
    unsigned t, s;
    double p;

    CHECK(icc && icc->size > 0 && (icc->flags & FIICC_COLOR_IS_CMYK), "data/cmyk8_lossless.jxl: no CMYK profile");
    if (!icc || !icc->size) { if (press) FreeImage_Unload(press); return; }
    for (t = 0; t < 2; t++) for (s = 0; s < 3; s++) {
        d = cmyk_pattern(T[t].type, sizes[s][0], sizes[s][1], 90 + (int)(3 * t + s), icc->data, icc->size);
        r = roundtrip(d, JXL_LOSSLESS, JXL_CMYK, NULL);
        CHECK(same_pixels(d, r) && cmyk_kept(d, r), "lossless %s %ux%u: not the inks and the profile (%s)", T[t].name, sizes[s][0], sizes[s][1], g_last);
        if (r) FreeImage_Unload(r);
        /* a plain load renders the inks through the profile */
        r = roundtrip(d, JXL_LOSSLESS, 0, NULL);
        want = FreeImage_ConvertCMYKToRGB(d, NULL, 0, 0);
        CHECK(same_pixels(want, r), "lossless %s %ux%u: a plain load is not the profile's rendering", T[t].name, sizes[s][0], sizes[s][1]);
        if (r) FreeImage_Unload(r);
        FreeImage_Unload(want);
        /* lossy: CMYK with the profile at every size; modular codes a few pixels coarsely, so PSNR at 257x131 */
        r = roundtrip(d, 0, JXL_CMYK, NULL);
        p = ink_psnr(d, r);
        CHECK(cmyk_kept(d, r) && (s < 2 || p > 40), "lossy %s %ux%u: not CMYK within 40 dB (%.1f)", T[t].name, sizes[s][0], sizes[s][1], p);
        if (getenv("JXL_TEST_VERBOSE")) printf("    lossy %s %ux%u: PSNR %.1f dB\n", T[t].name, sizes[s][0], sizes[s][1], p);
        if (r) FreeImage_Unload(r);
        FreeImage_Unload(d);
    }

    /* the file: CMY samples and a black channel in the CMYK profile, a level 10 codestream */
    d = cmyk_pattern(FIT_BITMAP, 64, 48, 97, icc->data, icc->size);
    {
        FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
        BYTE *data = NULL; DWORD n = 0, i;
        int level = 0;
        if (FreeImage_SaveToMemory(FIF_JXL, d, mem, 0) && FreeImage_AcquireMemory(mem, &data, &n))
            for (i = 0; i + 4 < n && i < 64; i++) if (!memcmp(data + i, "jxll", 4)) { level = data[i + 4]; break; }
        CHECK(level == 10, "lossy CMYK: no jxll box of level 10");
        FreeImage_CloseMemory(mem);
    }
#ifdef JXL_TEST_LIBJXL
    {
        Layout l;
        CHECK(FreeImage_Save(FIF_JXL, d, tmppath("fi_jxl_cmyk.jxl"), 0) && layout(tmppath("fi_jxl_cmyk.jxl"), &l) && !l.xyb && l.icc && l.black == 1,
              "lossy CMYK: not CMY and a black channel in the CMYK profile");
    }
#endif
    r = roundtrip(d, 30, 0, &small); if (r) FreeImage_Unload(r);
    r = roundtrip(d, 95, 0, &big); if (r) FreeImage_Unload(r);
    CHECK(small > 0 && small < big, "lossy CMYK: quality 30 gives %u bytes, quality 95 %u", (unsigned)small, (unsigned)big);

    /* one page through the multi-page functions: the page cache keeps it CMYK */
    m = FreeImage_OpenMultiBitmap(FIF_JXL, tmppath("fi_jxl_cmyk_page.jxl"), TRUE, FALSE, FALSE, 0);
    FreeImage_AppendPage(m, d);
    r = FreeImage_LockPage(m, 0);
    CHECK(same_pixels(d, r) && cmyk_kept(d, r), "a CMYK page comes out of the page cache changed");
    if (r) FreeImage_UnlockPage(m, r, FALSE);
    CHECK(FreeImage_CloseMultiBitmap(m, JXL_LOSSLESS), "one CMYK page: close failed (%s)", g_last);
    r = FreeImage_Load(FIF_JXL, tmppath("fi_jxl_cmyk_page.jxl"), JXL_CMYK);
    CHECK(same_pixels(d, r) && cmyk_kept(d, r), "one CMYK page through the multi-page functions: not written as CMYK");
    if (r) FreeImage_Unload(r);

    /* an animation is written in RGB, its frames rendered through the profile */
    want = FreeImage_ConvertCMYKToRGB(d, NULL, 0, 0);
    m = FreeImage_OpenMultiBitmap(FIF_JXL, tmppath("fi_jxl_cmyk_anim.jxl"), TRUE, FALSE, TRUE, 0);
    FreeImage_AppendPage(m, d);
    FreeImage_AppendPage(m, d);
    CHECK(FreeImage_CloseMultiBitmap(m, JXL_LOSSLESS), "CMYK animation: close failed (%s)", g_last);
    m = FreeImage_OpenMultiBitmap(FIF_JXL, tmppath("fi_jxl_cmyk_anim.jxl"), FALSE, TRUE, TRUE, JXL_CMYK);
    r = m ? FreeImage_LockPage(m, 1) : NULL;
    p = r ? psnr(want, r, 3, 1) : 0;
    CHECK(m && FreeImage_GetPageCount(m) == 2 && r && FreeImage_GetColorType(r) != FIC_CMYK && p > 50, "CMYK animation: page 1 is not the RGB rendering (%.1f dB)", p);
    if (r) FreeImage_UnlockPage(m, r, FALSE);
    if (m) FreeImage_CloseMultiBitmap(m, 0);
    FreeImage_Unload(want);
    FreeImage_Unload(d);

    /* without a CMYK profile: its RGB rendering, without alpha, and a warning */
    d = cmyk_pattern(FIT_BITMAP, 40, 30, 98, NULL, 0);
    g_last[0] = 0;
    CHECK(FreeImage_Save(FIF_JXL, d, tmppath("fi_jxl_cmyk_rgb.jxl"), JXL_LOSSLESS) && strstr(g_last, "without a CMYK ICC profile"), "CMYK without a profile: no warning (%s)", g_last);
    r = FreeImage_Load(FIF_JXL, tmppath("fi_jxl_cmyk_rgb.jxl"), JXL_CMYK);
    want = FreeImage_ConvertCMYKToRGB(d, NULL, 0, 0);
    CHECK(same_pixels(want, r), "CMYK without a profile: not its 24-bit rendering");
    if (r) FreeImage_Unload(r);
    FreeImage_Unload(want);
    FreeImage_Unload(d);
    d = cmyk_pattern(FIT_RGBA16, 40, 30, 99, NULL, 0);
    r = roundtrip(d, JXL_LOSSLESS, JXL_CMYK, NULL);
    want = FreeImage_ConvertCMYKToRGB(d, NULL, 0, 0);
    CHECK(r && FreeImage_GetImageType(r) == FIT_RGB16 && psnr(want, r, 3, 1) == 99, "16-bit CMYK without a profile: not its RGB16 rendering");
    if (r) FreeImage_Unload(r);
    FreeImage_Unload(want);
    FreeImage_Unload(d);

    /* float CMYK has no rendering: refused */
    d = FreeImage_AllocateT(FIT_RGBAF, 4, 4, 128, 0, 0, 0);
    FreeImage_CreateICCProfile(d, icc->data, icc->size);
    FreeImage_GetICCProfile(d)->flags |= FIICC_COLOR_IS_CMYK;
    CHECK(!FreeImage_Save(FIF_JXL, d, tmppath("fi_jxl_bad.jxl"), 0), "float CMYK was written");
    FreeImage_Unload(d);
    FreeImage_Unload(press);
    printf("CMYK: done\n");
}

static void test_pages(void) {
    FIMULTIBITMAP *m;
    FIBITMAP *d = pattern(FIT_RGB16, 48, 50, 40, 70), *r;
    /* one page through the multi-page functions is a still image */
    m = FreeImage_OpenMultiBitmap(FIF_JXL, tmppath("fi_jxl_one.jxl"), TRUE, FALSE, TRUE, 0);
    FreeImage_AppendPage(m, d);
    CHECK(FreeImage_CloseMultiBitmap(m, JXL_LOSSLESS), "one page: close failed");
    r = FreeImage_Load(FIF_JXL, tmppath("fi_jxl_one.jxl"), 0);
    CHECK(same_pixels(d, r) && anim_tag(r, "FrameTime") == -1, "one page: not the still image");
    if (r) FreeImage_Unload(r);

    /* the page cache keeps the pages exact (lossless, effort 1) */
    m = FreeImage_OpenMultiBitmap(FIF_JXL, tmppath("fi_jxl_cache.jxl"), TRUE, FALSE, FALSE, 0);
    FreeImage_AppendPage(m, d);
    r = FreeImage_LockPage(m, 0);
    CHECK(same_pixels(d, r), "the page cache changed a page");
    if (r) FreeImage_UnlockPage(m, r, FALSE);
    FreeImage_CloseMultiBitmap(m, 0);
    FreeImage_Unload(d);
    printf("pages: done\n");
}

static void test_refusals(void) {
    static const FREE_IMAGE_TYPE bad[] = { FIT_INT16, FIT_UINT32, FIT_INT32, FIT_DOUBLE, FIT_COMPLEX };
    unsigned i;
    FIBITMAP *d, *r;
    for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        d = FreeImage_AllocateT(bad[i], 8, 8, 8, 0, 0, 0);
        CHECK(!FreeImage_Save(FIF_JXL, d, tmppath("fi_jxl_bad.jxl"), 0), "type %d was written", bad[i]);
        CHECK(!FreeImage_FIFSupportsExportType(FIF_JXL, bad[i]), "type %d claimed", bad[i]);
        FreeImage_Unload(d);
    }
    d = FreeImage_Load(FIF_JXL, "data/rgb8_lossy.jxl", FIF_LOAD_NOPIXELS);
    CHECK(!FreeImage_Save(FIF_JXL, d, tmppath("fi_jxl_bad.jxl"), 0), "a header-only image was written");
    FreeImage_Unload(d);
    /* NaN and infinity are written as 0 and the largest float */
    d = FreeImage_AllocateT(FIT_RGBF, 4, 2, 96, 0, 0, 0);
    ((float *)FreeImage_GetScanLine(d, 0))[0] = NAN;
    ((float *)FreeImage_GetScanLine(d, 0))[1] = INFINITY;
    ((float *)FreeImage_GetScanLine(d, 1))[2] = 0.25f;
    r = roundtrip(d, JXL_LOSSLESS, 0, NULL);
    CHECK(r && ((float *)FreeImage_GetScanLine(r, 0))[0] == 0 && isfinite(((float *)FreeImage_GetScanLine(r, 0))[1]) &&
          ((float *)FreeImage_GetScanLine(r, 1))[2] == 0.25f, "NaN or infinity not written as finite samples");
    if (r) FreeImage_Unload(r);
    FreeImage_Unload(d);
    printf("refusals: done\n");
}

/* a JPEG XL behind other bytes, read through a memory stream and through a handle that cannot seek to its end */
static unsigned DLL_CALLCONV rd(void *buf, unsigned size, unsigned count, fi_handle h) {
    return (unsigned)fread(buf, size, count, (FILE *)h);
}
static int DLL_CALLCONV sk(fi_handle h, INT64 offset, int origin) {
    return origin == SEEK_END ? -1 : fseek((FILE *)h, (long)offset, origin);
}
static INT64 DLL_CALLCONV tl(fi_handle h) {
    return (INT64)ftell((FILE *)h);
}

static void test_offset(void) {
    FIBITMAP *d = pattern(FIT_BITMAP, 24, 40, 30, 80), *r;
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0), *view;
    FreeImageIO io = { rd, NULL, sk, tl };
    BYTE *data; DWORD n;
    FILE *f;
    char junk[777];
    memset(junk, 'x', sizeof(junk));
    FreeImage_SaveToMemory(FIF_JXL, d, mem, JXL_LOSSLESS);
    FreeImage_AcquireMemory(mem, &data, &n);
    f = fopen(tmppath("fi_jxl_offset.bin"), "wb");
    fwrite(junk, 1, sizeof(junk), f);
    fwrite(data, 1, n, f);
    fclose(f);

    f = fopen(tmppath("fi_jxl_offset.bin"), "rb");
    fseek(f, (long)sizeof(junk), SEEK_SET);
    CHECK(FreeImage_GetFileTypeFromHandle(&io, (fi_handle)f, 0) == FIF_JXL, "not identified 777 bytes into a handle");
    fseek(f, (long)sizeof(junk), SEEK_SET);
    r = FreeImage_LoadFromHandle(FIF_JXL, &io, (fi_handle)f, 0);
    CHECK(same_pixels(d, r), "a JPEG XL 777 bytes into a handle without SEEK_END");
    if (r) FreeImage_Unload(r);
    fclose(f);

    {
        BYTE *buf = malloc(n + sizeof(junk));
        memcpy(buf, junk, sizeof(junk));
        memcpy(buf + sizeof(junk), data, n);
        view = FreeImage_OpenMemory(buf, n + (DWORD)sizeof(junk));
        FreeImage_SeekMemory(view, (long)sizeof(junk), SEEK_SET);
        r = FreeImage_LoadFromMemory(FIF_JXL, view, 0);
        CHECK(same_pixels(d, r), "a JPEG XL 777 bytes into a memory stream");
        if (r) FreeImage_Unload(r);
        FreeImage_CloseMemory(view);
        free(buf);
    }
    FreeImage_CloseMemory(mem);
    FreeImage_Unload(d);
    printf("stream offset: done\n");
}

int main(void) {
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(message);
    test_lossless();
    test_palettes();
    test_lossy();
    test_colors();
    test_metadata();
    test_animation();
    test_animation_float();
    test_cmyk();
    test_pages();
    test_refusals();
    test_offset();
    printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
