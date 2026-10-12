/* JPEG XL writer tests: round trips of every type, metadata, animations, the page cache, refusals */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "FreeImage.h"
#include "pattern.h"

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

static void test_animation(void) {
    static const struct { unsigned w, h; WORD x, y; LONG ms; BYTE disposal, blend; } fr[] = {
        {64, 48, 0, 0, 100, 1, 1}, {30, 20, 5, 6, 80, 2, 0}, {24, 16, 25, 20, 60, 3, 0}, {20, 20, 40, 4, 40, 1, 1},
        {28, 30, 10, 10, 30, 0, 0}, {64, 48, 0, 0, 20, 2, 0}, {16, 12, 50, 40, 10, 1, 0},
    };
    const unsigned n = sizeof(fr) / sizeof(fr[0]), cw = 64, ch = 48;
    /* ProPhoto stays an ICC profile in libjxl, so the lossy frames blend outside the original colours */
    static const char *pass[3] = { "lossless", "lossy", "lossy ProPhoto" };
    DWORD pro_size;
    const void *pro = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_PROPHOTO_RGB, &pro_size);
    FIMULTIBITMAP *m;
    Px *canvas = calloc(cw * ch, sizeof(Px)), *saved = calloc(cw * ch, sizeof(Px));
    unsigned i, x, y;
    int flags[3] = { JXL_LOSSLESS, 0, 0 }, f;

    for (f = 0; f < 3; f++) {
        m = FreeImage_OpenMultiBitmap(FIF_JXL, tmppath("fi_jxl_anim.jxl"), TRUE, FALSE, TRUE, 0);
        for (i = 0; i < n; i++) {
            FIBITMAP *d = pattern(FIT_BITMAP, 32, fr[i].w, fr[i].h, 60 + (int)i);
            set_anim(d, fr[i].x, fr[i].y, fr[i].ms, fr[i].disposal, fr[i].blend, cw, ch, 2);
            if (f == 2) FreeImage_CreateICCProfile(d, (void *)pro, (long)pro_size);
            CHECK(FreeImage_AppendPage(m, d), "AppendPage %u", i);
            FreeImage_Unload(d);
        }
        CHECK(FreeImage_CloseMultiBitmap(m, flags[f]), "CloseMultiBitmap (flags %x): %s", flags[f], g_last);

        memset(canvas, 0, cw * ch * sizeof(Px));
        m = FreeImage_OpenMultiBitmap(FIF_JXL, tmppath("fi_jxl_anim.jxl"), FALSE, TRUE, TRUE, 0);
        CHECK(m && FreeImage_GetPageCount(m) == (int)n, "animation: %d pages, not %u", m ? FreeImage_GetPageCount(m) : -1, n);
        for (i = 0; m && i < n; i++) {
            FIBITMAP *frame = pattern(FIT_BITMAP, 32, fr[i].w, fr[i].h, 60 + (int)i);
            FIBITMAP *page = FreeImage_LockPage(m, (int)i);
            double worst = 0, se = 0;
            memcpy(saved, canvas, cw * ch * sizeof(Px));
            composite(canvas, cw, ch, frame, fr[i].x, fr[i].y, fr[i].blend == 0);
            if (!page) { CHECK(0, "animation %s page %u not locked", pass[f], i); FreeImage_Unload(frame); continue; }
            CHECK(FreeImage_GetWidth(page) == cw && FreeImage_GetHeight(page) == ch && FreeImage_GetBPP(page) == 32, "page %u: not the 32-bit canvas", i);
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
            if (f == 0)
                CHECK(worst <= 1.01 / 255, "animation lossless page %u: off by %.4f from GIF/APNG compositing", i, worst);
            else
                CHECK(sqrt(se / (4.0 * cw * ch)) < 0.02, "animation %s page %u: RMS %.4f from GIF/APNG compositing", pass[f], i, sqrt(se / (4.0 * cw * ch)));
            if (f == 2)
                CHECK(FreeImage_GetICCProfile(page)->size == (long)pro_size && !memcmp(FreeImage_GetICCProfile(page)->data, pro, pro_size), "page %u: not the ProPhoto profile", i);
            if (getenv("JXL_TEST_VERBOSE")) printf("    %s page %u: worst %.4f RMS %.4f\n", pass[f], i, worst, sqrt(se / (4.0 * cw * ch)));
            CHECK(anim_tag(page, "FrameTime") == fr[i].ms && anim_tag(page, "Loop") == 2, "page %u: FrameTime %ld Loop %ld", i, anim_tag(page, "FrameTime"), anim_tag(page, "Loop"));
            FreeImage_UnlockPage(m, page, FALSE);
            FreeImage_Unload(frame);
            if (fr[i].disposal == 2) for (y = fr[i].y; y < fr[i].y + fr[i].h && y < ch; y++) for (x = fr[i].x; x < fr[i].x + fr[i].w && x < cw; x++) memset(&canvas[y * cw + x], 0, sizeof(Px));
            if (fr[i].disposal == 3) memcpy(canvas, saved, cw * ch * sizeof(Px));
        }
        if (m) FreeImage_CloseMultiBitmap(m, 0);
    }
    free(canvas); free(saved);
    printf("animations: done\n");
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
    test_pages();
    test_refusals();
    test_offset();
    printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
