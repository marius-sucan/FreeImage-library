/* JPEG XL decode test: data/ against the table below */
/* "./decode --record" prints the table as decoded; lossy files are compared with the patterns they were made from */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"
#include "pattern.h"
#ifdef JXL_TEST_LIBJXL
#include <jxl/decode.h>
#include <jxl/encode.h>
#endif

typedef struct {
    const char *file;
    int pages;
    int width, height;          /* page 0 */
    FREE_IMAGE_TYPE type;
    int bpp;
    int icc;                    /* ICC profile size, 0 = none */
    const char *cicp;           /* "primaries transfer", NULL = no CICP tag */
    int exif;                   /* FIMD_EXIF_MAIN tags */
    int xmp;
    long frametime, loop;       /* FIMD_ANIMATION on page 0, -1 = absent */
    unsigned long long sum;     /* page 0 pixels; 0 for lossy files, see LOSSY */
} Expected;

static const Expected EXPECTED[] = {
    {"grey8_lossless.jxl",      1, 61, 37, FIT_BITMAP,   8,    0, "1 13",  0, 0,  -1, -1, 0x10ef31ce1ad9c6bbULL},
    {"rgb8_lossy.jxl",          1, 64, 48, FIT_BITMAP,  24,    0, "1 13",  0, 0,  -1, -1, 0},
    {"rgba8_lossless.jxl",      1, 64, 48, FIT_BITMAP,  32,    0, "1 13",  0, 0,  -1, -1, 0x1f2c705a5d26c5d5ULL},
    {"rgba8_lossy.jxl",         1, 64, 48, FIT_BITMAP,  32,    0, "1 13",  0, 0,  -1, -1, 0},
    {"grey16_lossless.jxl",     1, 40, 30, FIT_UINT16,  16,    0, "1 13",  0, 0,  -1, -1, 0x75f1bead50404c18ULL},
    {"rgb16_pq_lossless.jxl",   1, 40, 30, FIT_RGB16,   48, 4324, "9 16",  0, 0,  -1, -1, 0x9253d2cdca088a05ULL},
    {"rgbaf_lossless.jxl",      1, 32, 24, FIT_RGBAF,  128,  536, "1 8",   0, 0,  -1, -1, 0x8576e60287251817ULL},
    {"rgbf_lossy.jxl",          1, 32, 24, FIT_RGBF,    96,  536, "1 8",   0, 0,  -1, -1, 0},
    {"meta_lossless.jxl",       1, 75, 50, FIT_BITMAP,  24, 3144, NULL,   13, 1,  -1, -1, 0x69432c8474895196ULL},
    {"meta_lossy.jxl",          1, 75, 50, FIT_BITMAP,  24,    0, "1 13", 13, 1,  -1, -1, 0},
    {"icc_sampled_lossy.jxl",   1, 64, 48, FIT_BITMAP,  24, 1076, NULL,    0, 0,  -1, -1, 0},
    {"anim_lossless.jxl",       5, 48, 40, FIT_BITMAP,  32,    0, "1 13",  0, 0, 100,  0, 0x7f371b3e13054fc4ULL},
    {"anim16_lossy.jxl",        3, 36, 28, FIT_RGBA16,  64,    0, "1 13",  0, 0,  50,  0, 0},
    {"anim_ticks.jxl",          4, 24, 16, FIT_BITMAP,  24,    0, "1 13",  0, 0,  70,  3, 0xf3b2304a09748106ULL},
    {"oriented6.jxl",           1, 20, 40, FIT_BITMAP,  24,    0, "1 13",  0, 0,  -1, -1, 0xb8db63153e1d7f92ULL},
    {"oriented5_alpha.jxl",     1, 18, 30, FIT_BITMAP,  32,    0, "1 13",  0, 0,  -1, -1, 0x7cda242239f5e91dULL},
    {"bare_grey_alpha.jxl",     1, 33, 21, FIT_BITMAP,  32,    0, "1 13",  0, 0,  -1, -1, 0x747dc1bbeccf9f96ULL},
    {"rgb12_lossless.jxl",      1, 30, 20, FIT_RGB16,   48,    0, "1 13",  0, 0,  -1, -1, 0xec3c5a249659fc56ULL},
    {"premultiplied.jxl",       1, 32, 24, FIT_RGBA16,  64,    0, "1 13",  0, 0,  -1, -1, 0x9a3603bce14dc441ULL},
    {"layers_still.jxl",        1, 40, 32, FIT_BITMAP,  32,    0, "1 13",  0, 0,  -1, -1, 0x46784bc773b3376cULL},
    {"brob_exif.jxl",           1, 20, 10, FIT_BITMAP,  24,    0, "1 13",  1, 0,  -1, -1, 0x0947e8c58d733f0cULL},
    {"test-jxl.jxl",           22, 1170, 1455, FIT_BITMAP, 32, 524, NULL, 0, 0, 400,  0, 0},
    {"anim_icc_lossy.jxl",      3, 48, 40, FIT_BITMAP,  32, 1076, NULL,    0, 0, 100,  0, 0},
    {"cmyk8_lossless.jxl",      1, 40, 30, FIT_BITMAP,  24,    0, NULL,    0, 0,  -1, -1, 0xb5130a8127fefae0ULL},
    {"cmyk16_lossy.jxl",        1, 40, 30, FIT_RGB16,   48,    0, NULL,    0, 0,  -1, -1, 0},
    {"cmyk8_xyb.jxl",           1, 40, 30, FIT_BITMAP,  24,    0, "1 13",  0, 0,  -1, -1, 0},
    {"cmyka8_xyb.jxl",          1, 40, 30, FIT_BITMAP,  32,    0, "1 13",  0, 0,  -1, -1, 0},
};
#define NEXPECTED (sizeof(EXPECTED) / sizeof(EXPECTED[0]))

/* every page of the lossless animations */
static const struct { const char *file; int page; unsigned long long sum; } PAGES[] = {
    {"anim_lossless.jxl", 1, 0x411f3f9481795d2fULL},
    {"anim_lossless.jxl", 2, 0xd9d1c535acf1c47eULL},
    {"anim_lossless.jxl", 3, 0x1e5905c9bc77c9a1ULL},
    {"anim_lossless.jxl", 4, 0x2082d3e18ae6b7deULL},
    {"anim_ticks.jxl",    1, 0xedb7fab82ff26916ULL},
    {"anim_ticks.jxl",    2, 0x4cb795af1ae66e48ULL},
    {"anim_ticks.jxl",    3, 0x6a0d1c5392dada32ULL},
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))

/* lossy files against what they were made from; ref_file: a lossless twin instead of a pattern */
static const struct { const char *file; int page; FREE_IMAGE_TYPE type; unsigned bpp, w, h; int seed; const char *ref_file; double min_psnr; } LOSSY[] = {
    {"rgb8_lossy.jxl",        0, FIT_BITMAP, 24, 64, 48, 2, NULL, 34},
    {"rgba8_lossy.jxl",       0, FIT_BITMAP, 32, 64, 48, 3, NULL, 30},
    {"rgbf_lossy.jxl",        0, FIT_RGBF,   96, 32, 24, 7, NULL, 30},
    {"icc_sampled_lossy.jxl", 0, FIT_BITMAP, 24, 64, 48, 8, NULL, 34},
    {"anim16_lossy.jxl",      0, FIT_RGB16,  48, 36, 28, 20, NULL, 34},
    {"anim16_lossy.jxl",      2, FIT_RGB16,  48, 36, 28, 22, NULL, 34},
    {"meta_lossy.jxl",        0, FIT_BITMAP, 24, 0, 0, 0, "meta_lossless.jxl", 34},
    {"anim_icc_lossy.jxl",    0, FIT_BITMAP, 32, 48, 40, 30, NULL, 34},
};
#define NLOSSY (sizeof(LOSSY) / sizeof(LOSSY[0]))

/* test-jxl.jxl: XYB with an ICC profile, opaque frames at the top left, each blended over the page before */
static const unsigned FRAMES[][2] = {
    {1170, 1455}, {964, 1350}, {1170, 1455}, {540, 810}, {875, 1332}, {1170, 1080}, {1024, 1455}, {1133, 1455},
    {1133, 1455}, {665, 1000}, {874, 1315}, {1024, 1455}, {512, 768}, {1170, 1455}, {1170, 1455}, {933, 1455},
    {1062, 1118}, {1170, 1080}, {640, 958}, {682, 1024}, {827, 1330}, {723, 1280},
};
#define NFRAMES (sizeof(FRAMES) / sizeof(FRAMES[0]))

static int g_record = 0;
static int g_failures = 0;

static void message(FREE_IMAGE_FORMAT fif, const char *msg) {
    printf("    [%s] %s\n", fif == FIF_UNKNOWN ? "?" : FreeImage_GetFormatFromFIF(fif), msg);
}

static void fail(const char *file, const char *what) {
    printf("FAIL %s: %s\n", file, what);
    g_failures++;
}

static unsigned long long sum_pixels(FIBITMAP *d) {
    unsigned long long h = 1469598103934665603ULL;
    unsigned y, x, n;
    if (!d || !FreeImage_HasPixels(d)) return 0;
    n = FreeImage_GetLine(d);
    for (y = 0; y < FreeImage_GetHeight(d); y++) {
        const BYTE *p = FreeImage_GetScanLine(d, y);
        for (x = 0; x < n; x++) { h ^= p[x]; h *= 1099511628211ULL; }
    }
    return h;
}

static int count_tags(FIBITMAP *d, FREE_IMAGE_MDMODEL model) {
    FITAG *tag = NULL; int n = 0;
    FIMETADATA *it = FreeImage_FindFirstMetadata(model, d, &tag);
    if (!it) return 0;
    do { n++; } while (FreeImage_FindNextMetadata(it, &tag));
    FreeImage_FindCloseMetadata(it);
    return n;
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

static void cicp(FIBITMAP *d, char *buf, size_t size) {
    FITAG *tag = NULL;
    buf[0] = 0;
    if (FreeImage_GetMetadata(FIMD_CUSTOM, d, "CICP", &tag) && tag) {
        const BYTE *v = (const BYTE *)FreeImage_GetTagValue(tag);
        snprintf(buf, size, "%u %u", v[0], v[1]);
    }
}

static int icc_size(FIBITMAP *d) {
    FIICCPROFILE *p = FreeImage_GetICCProfile(d);
    return (p && p->data) ? (int)p->size : 0;
}

static const char *path(const char *name) {
    static char buf[1024];
    snprintf(buf, sizeof(buf), "data/%s", name);
    return buf;
}

static FIBITMAP *load_page(const char *file, int page, int flags) {
    FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(FIF_JXL, path(file), FALSE, TRUE, TRUE, flags);
    FIBITMAP *d, *copy = NULL;
    if (!m) return NULL;
    d = FreeImage_LockPage(m, page);
    if (d) { copy = FreeImage_Clone(d); FreeImage_UnlockPage(m, d, FALSE); }
    FreeImage_CloseMultiBitmap(m, 0);
    return copy;
}

/* the colour samples of a page as doubles in [0, 1] (float: as stored) */
static double sample(FIBITMAP *d, unsigned x, unsigned y, int c) {
    const BYTE *row = FreeImage_GetScanLine(d, FreeImage_GetHeight(d) - 1 - y);
    switch (FreeImage_GetImageType(d)) {
        case FIT_BITMAP: {
            unsigned n = FreeImage_GetBPP(d) / 8;
            static const int order[3] = { FI_RGBA_RED, FI_RGBA_GREEN, FI_RGBA_BLUE };
            return (n == 1 ? row[x] : row[x * n + order[c]]) / 255.0;
        }
        case FIT_RGB16: return ((const WORD *)row)[3 * x + c] / 65535.0;
        case FIT_RGBA16: return ((const WORD *)row)[4 * x + c] / 65535.0;
        case FIT_RGBF: return ((const float *)row)[3 * x + c];
        case FIT_RGBAF: return ((const float *)row)[4 * x + c];
        default: return 0;
    }
}

/* the colour under an alpha of 0 is the encoder's to choose */
static int invisible(FIBITMAP *ref, unsigned x, unsigned y) {
    const BYTE *row = FreeImage_GetScanLine(ref, FreeImage_GetHeight(ref) - 1 - y);
    return FreeImage_GetImageType(ref) == FIT_BITMAP && FreeImage_GetBPP(ref) == 32 && row[4 * x + FI_RGBA_ALPHA] == 0;
}

static double psnr(FIBITMAP *a, FIBITMAP *b, double range) {
    unsigned x, y, w = FreeImage_GetWidth(a), h = FreeImage_GetHeight(a);
    double se = 0; int c, n = 0;
    if (w != FreeImage_GetWidth(b) || h != FreeImage_GetHeight(b)) return 0;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) for (c = 0; c < 3; c++) {
        double e;
        if (invisible(b, x, y)) continue;
        e = sample(a, x, y, c) - sample(b, x, y, c); se += e * e; n++;
    }
    return se == 0 ? 99 : 10 * log10(range * range / (se / n));
}

/* the inks of a CMYK page, bytes or words 0-3 */
static double ink(FIBITMAP *d, unsigned x, unsigned y, int c) {
    const BYTE *row = FreeImage_GetScanLine(d, FreeImage_GetHeight(d) - 1 - y);
    return FreeImage_GetImageType(d) == FIT_RGBA16 ? ((const WORD *)row)[4 * x + c] / 65535.0 : row[4 * x + c] / 255.0;
}

static double ink_psnr(FIBITMAP *a, FIBITMAP *b) {
    unsigned x, y, w = FreeImage_GetWidth(a), h = FreeImage_GetHeight(a);
    double se = 0; int c;
    if (w != FreeImage_GetWidth(b) || h != FreeImage_GetHeight(b)) return 0;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) for (c = 0; c < 4; c++) { double e = ink(a, x, y, c) - ink(b, x, y, c); se += e * e; }
    return se == 0 ? 99 : 10 * log10(4.0 * w * h / se);
}

#ifdef JXL_TEST_LIBJXL
/* the largest difference between a page and libjxl's own 8-bit sRGB rendering of the file, alpha included; -1 if libjxl fails */
static int libjxl_srgb_diff(const char *file, FIBITMAP *d) {
    const unsigned n = FreeImage_GetBPP(d) / 8, w = FreeImage_GetWidth(d), h = FreeImage_GetHeight(d);
    JxlPixelFormat pf = { n, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
    static const int order[4] = { FI_RGBA_RED, FI_RGBA_GREEN, FI_RGBA_BLUE, FI_RGBA_ALPHA };
    JxlDecoder *dec = JxlDecoderCreate(NULL);
    BYTE *data = NULL, *out = (BYTE *)malloc((size_t)w * h * n);
    FILE *f = fopen(path(file), "rb");
    long size = 0;
    int worst = -1, done = 0;
    unsigned x, y, c;
    if (f && fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 && (data = (BYTE *)malloc(size)) && fread(data, 1, size, f) == (size_t)size &&
        dec && out && JxlDecoderSubscribeEvents(dec, JXL_DEC_COLOR_ENCODING | JXL_DEC_FULL_IMAGE) == JXL_DEC_SUCCESS &&
        JxlDecoderSetInput(dec, data, size) == JXL_DEC_SUCCESS) {
        JxlDecoderCloseInput(dec);
        for (;;) {
            JxlDecoderStatus s = JxlDecoderProcessInput(dec);
            if (s == JXL_DEC_COLOR_ENCODING) {
                JxlColorEncoding srgb;
                JxlColorEncodingSetToSRGB(&srgb, JXL_FALSE);
                if (JxlDecoderSetPreferredColorProfile(dec, &srgb) != JXL_DEC_SUCCESS) break;
            } else if (s == JXL_DEC_NEED_IMAGE_OUT_BUFFER) {
                if (JxlDecoderSetImageOutBuffer(dec, &pf, out, (size_t)w * h * n) != JXL_DEC_SUCCESS) break;
            } else if (s == JXL_DEC_FULL_IMAGE) {
                done = 1;
            } else {
                break;
            }
        }
    }
    if (done) {
        worst = 0;
        for (y = 0; y < h; y++) {
            const BYTE *row = FreeImage_GetScanLine(d, h - 1 - y), *ref = out + (size_t)y * w * n;
            for (x = 0; x < w; x++) for (c = 0; c < n; c++) {
                const int e = abs((int)row[x * n + order[c]] - (int)ref[x * n + c]);
                if (e > worst) worst = e;
            }
        }
    }
    if (f) fclose(f);
    if (dec) JxlDecoderDestroy(dec);
    free(data);
    free(out);
    return worst;
}
#endif

/* a header-only load describes what the full load returns */
static void check_header(const char *file, int page, FIBITMAP *full) {
    FIBITMAP *h = load_page(file, page, FIF_LOAD_NOPIXELS);
    char a[32], b[32];
    if (!h) { fail(file, "header-only load failed"); return; }
    cicp(full, a, sizeof(a)); cicp(h, b, sizeof(b));
    if (FreeImage_HasPixels(h) || FreeImage_GetWidth(h) != FreeImage_GetWidth(full) || FreeImage_GetHeight(h) != FreeImage_GetHeight(full) ||
        FreeImage_GetImageType(h) != FreeImage_GetImageType(full) || FreeImage_GetBPP(h) != FreeImage_GetBPP(full) ||
        icc_size(h) != icc_size(full) || strcmp(a, b) || count_tags(h, FIMD_EXIF_MAIN) != count_tags(full, FIMD_EXIF_MAIN) ||
        count_tags(h, FIMD_ANIMATION) != count_tags(full, FIMD_ANIMATION) || anim_tag(h, "FrameTime") != anim_tag(full, "FrameTime"))
        fail(file, "the header-only load differs from the full load");
    FreeImage_Unload(h);
}

int main(int argc, char **argv) {
    unsigned i;
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(message);
    g_record = (argc > 1 && !strcmp(argv[1], "--record"));

    for (i = 0; i < NEXPECTED; i++) {
        const Expected *e = &EXPECTED[i];
        FIMULTIBITMAP *m;
        FIBITMAP *d;
        char c[32];
        int pages;
        if (FreeImage_GetFileType(path(e->file), 0) != FIF_JXL) fail(e->file, "not identified as JPEG XL");
        m = FreeImage_OpenMultiBitmap(FIF_JXL, path(e->file), FALSE, TRUE, TRUE, 0);
        if (!m) { fail(e->file, "open failed"); continue; }
        pages = FreeImage_GetPageCount(m);
        FreeImage_CloseMultiBitmap(m, 0);
        d = load_page(e->file, 0, 0);
        if (!d) { fail(e->file, "load failed"); continue; }
        cicp(d, c, sizeof(c));
        if (g_record) {
            printf("    {\"%s\", %d, %u, %u, %d, %u, %d, %s%s%s, %d, %d, %ld, %ld, 0x%016llxULL},\n", e->file, pages,
                   FreeImage_GetWidth(d), FreeImage_GetHeight(d), FreeImage_GetImageType(d), FreeImage_GetBPP(d), icc_size(d),
                   c[0] ? "\"" : "", c[0] ? c : "NULL", c[0] ? "\"" : "", count_tags(d, FIMD_EXIF_MAIN), count_tags(d, FIMD_XMP) ? 1 : 0,
                   anim_tag(d, "FrameTime"), anim_tag(d, "Loop"), sum_pixels(d));
        } else {
            char what[256];
            snprintf(what, sizeof(what), "pages %d %ux%u type %d bpp %u icc %d cicp '%s' exif %d xmp %d frametime %ld loop %ld",
                     pages, FreeImage_GetWidth(d), FreeImage_GetHeight(d), FreeImage_GetImageType(d), FreeImage_GetBPP(d), icc_size(d),
                     c, count_tags(d, FIMD_EXIF_MAIN), count_tags(d, FIMD_XMP) ? 1 : 0, anim_tag(d, "FrameTime"), anim_tag(d, "Loop"));
            if (pages != e->pages || (int)FreeImage_GetWidth(d) != e->width || (int)FreeImage_GetHeight(d) != e->height ||
                FreeImage_GetImageType(d) != e->type || (int)FreeImage_GetBPP(d) != e->bpp || icc_size(d) != e->icc ||
                strcmp(c, e->cicp ? e->cicp : "") || count_tags(d, FIMD_EXIF_MAIN) != e->exif || (count_tags(d, FIMD_XMP) ? 1 : 0) != e->xmp ||
                anim_tag(d, "FrameTime") != e->frametime || anim_tag(d, "Loop") != e->loop)
                fail(e->file, what);
            else if (e->sum && sum_pixels(d) != e->sum)
                fail(e->file, "pixels differ");
            else
                printf("ok   %s: %s\n", e->file, what);
        }
        check_header(e->file, 0, d);
        FreeImage_Unload(d);
    }

    for (i = 0; i < NPAGES; i++) {
        FIBITMAP *d = load_page(PAGES[i].file, PAGES[i].page, 0);
        if (!d) { fail(PAGES[i].file, "page load failed"); continue; }
        if (g_record)
            printf("    {\"%s\", %d, 0x%016llxULL},\n", PAGES[i].file, PAGES[i].page, sum_pixels(d));
        else if (PAGES[i].sum && sum_pixels(d) != PAGES[i].sum)
            fail(PAGES[i].file, "a page's pixels differ");
        check_header(PAGES[i].file, PAGES[i].page, d);
        FreeImage_Unload(d);
    }

    for (i = 0; i < NLOSSY; i++) {
        FIBITMAP *d = load_page(LOSSY[i].file, LOSSY[i].page, 0);
        FIBITMAP *ref = LOSSY[i].ref_file ? FreeImage_Load(FIF_JXL, path(LOSSY[i].ref_file), 0)
                                          : pattern(LOSSY[i].type, LOSSY[i].bpp, LOSSY[i].w, LOSSY[i].h, LOSSY[i].seed);
        double p = (d && ref) ? psnr(d, ref, LOSSY[i].type == FIT_RGBF ? 2.0 : 1.0) : 0;
        if (g_record || p >= LOSSY[i].min_psnr)
            printf("%s %s page %d: PSNR %.1f dB (min %.0f)\n", g_record ? "    " : "ok  ", LOSSY[i].file, LOSSY[i].page, p, LOSSY[i].min_psnr);
        else {
            char what[128];
            snprintf(what, sizeof(what), "page %d: PSNR %.1f dB, below %.0f", LOSSY[i].page, p, LOSSY[i].min_psnr);
            fail(LOSSY[i].file, what);
        }
        if (d) FreeImage_Unload(d);
        if (ref) FreeImage_Unload(ref);
    }

    /* the CMYK files with JXL_CMYK: the inks they were made from, the profile flagged CMYK, header-only alike */
    {
        static const struct { const char *file; FREE_IMAGE_TYPE type; unsigned bpp; int seed; double min_psnr; } CMYK[] = {
            {"cmyk8_lossless.jxl", FIT_BITMAP, 32, 9, 99}, {"cmyk16_lossy.jxl", FIT_RGBA16, 64, 10, 38},
        };
        for (i = 0; i < sizeof(CMYK) / sizeof(CMYK[0]); i++) {
            FIBITMAP *d = load_page(CMYK[i].file, 0, JXL_CMYK), *h = load_page(CMYK[i].file, 0, JXL_CMYK | FIF_LOAD_NOPIXELS);
            FIBITMAP *ref = cmyk_pattern(CMYK[i].type, 40, 30, CMYK[i].seed, NULL, 0);
            int cmyk = d && FreeImage_GetImageType(d) == CMYK[i].type && FreeImage_GetBPP(d) == CMYK[i].bpp && FreeImage_GetColorType(d) == FIC_CMYK && icc_size(d) > 0;
            double p = cmyk ? ink_psnr(ref, d) : 0;
            if (!cmyk) fail(CMYK[i].file, "JXL_CMYK: not CMYK with its profile");
            else if (p < CMYK[i].min_psnr && !g_record) fail(CMYK[i].file, "JXL_CMYK: not the inks it was made from");
            else if (!h || FreeImage_HasPixels(h) || FreeImage_GetImageType(h) != CMYK[i].type || FreeImage_GetBPP(h) != CMYK[i].bpp ||
                     FreeImage_GetColorType(h) != FIC_CMYK || icc_size(h) != icc_size(d))
                fail(CMYK[i].file, "JXL_CMYK: the header-only load differs from the full load");
            else
                printf("%s %s with JXL_CMYK: inks PSNR %.1f dB (min %.0f)\n", g_record ? "    " : "ok  ", CMYK[i].file, p, CMYK[i].min_psnr);
            if (d) FreeImage_Unload(d);
            if (h) FreeImage_Unload(h);
            FreeImage_Unload(ref);
        }
    }

    /* XYB CMYK files by cjxl: their colours are the image through its profile, K included; JXL_CMYK gives that RGB too */
    {
        static const struct { const char *file; int seed, alpha; double min_psnr; } XYB[] = {
            {"cmyk8_xyb.jxl", 11, 0, 25}, {"cmyka8_xyb.jxl", 12, 1, 25},
        };
        FIBITMAP *press = load_page("cmyk8_lossless.jxl", 0, JXL_CMYK | FIF_LOAD_NOPIXELS);
        FIICCPROFILE *icc = press ? FreeImage_GetICCProfile(press) : NULL;
        if (!icc || !icc->size) fail("cmyk8_lossless.jxl", "no press profile");
        for (i = 0; icc && icc->size && i < sizeof(XYB) / sizeof(XYB[0]); i++) {
            FIBITMAP *d = load_page(XYB[i].file, 0, 0), *c = load_page(XYB[i].file, 0, JXL_CMYK), *h = load_page(XYB[i].file, 0, JXL_CMYK | FIF_LOAD_NOPIXELS);
            FIBITMAP *inks = cmyk_pattern(FIT_BITMAP, 40, 30, XYB[i].seed, icc->data, icc->size);
            FIBITMAP *ref = FreeImage_ConvertCMYKToRGB(inks, NULL, 0, 0);
            double p = (d && ref) ? psnr(d, ref, 1.0) : 0;
            unsigned bad_alpha = 0, x, y;
            for (y = 0; d && XYB[i].alpha && FreeImage_GetBPP(d) == 32 && y < 30; y++) {
                const BYTE *row = FreeImage_GetScanLine(d, 29 - y);
                for (x = 0; x < 40; x++) bad_alpha += abs((int)row[4 * x + FI_RGBA_ALPHA] - (int)cmyk_alpha(x, y)) > 1;
            }
            if (p < XYB[i].min_psnr && !g_record) {
                char what[96];
                snprintf(what, sizeof(what), "PSNR %.1f dB against its inks' rendering, below %.0f", p, XYB[i].min_psnr);
                fail(XYB[i].file, what);
            } else if (bad_alpha) fail(XYB[i].file, "not the alpha it was made with");
            else if (!c || FreeImage_GetColorType(c) == FIC_CMYK || sum_pixels(c) != sum_pixels(d)) fail(XYB[i].file, "JXL_CMYK: not the RGB image of a plain load");
            else if (!h || FreeImage_HasPixels(h) || FreeImage_GetImageType(h) != FreeImage_GetImageType(d) || FreeImage_GetBPP(h) != FreeImage_GetBPP(d) || icc_size(h) != icc_size(d))
                fail(XYB[i].file, "JXL_CMYK: the header-only load differs from the full load");
            else
                printf("%s %s: PSNR %.1f dB against its inks' rendering (min %.0f), JXL_CMYK alike\n", g_record ? "    " : "ok  ", XYB[i].file, p, XYB[i].min_psnr);
#ifdef JXL_TEST_LIBJXL
            if (d) {
                const int worst = libjxl_srgb_diff(XYB[i].file, d);
                if (worst < 0 || worst > 1) fail(XYB[i].file, "not libjxl's own sRGB rendering");
                else printf("ok   %s: libjxl's own sRGB rendering within %d\n", XYB[i].file, worst);
            }
#endif
            if (d) FreeImage_Unload(d);
            if (c) FreeImage_Unload(c);
            if (h) FreeImage_Unload(h);
            if (ref) FreeImage_Unload(ref);
            FreeImage_Unload(inks);
        }
        if (press) FreeImage_Unload(press);
    }

    /* an animation page out of order, then back: the decoder rewinds */
    {
        FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(FIF_JXL, path("anim_ticks.jxl"), FALSE, TRUE, TRUE, 0);
        static const int order[] = { 3, 1, 2, 0, 3 };
        for (i = 0; m && i < 5; i++) {
            FIBITMAP *d = FreeImage_LockPage(m, order[i]);
            unsigned long long want = order[i] == 0 ? EXPECTED[13].sum : PAGES[4 + order[i] - 1].sum;
            if (!d || sum_pixels(d) != want) fail("anim_ticks.jxl", "a page locked out of order differs");
            if (d) FreeImage_UnlockPage(m, d, FALSE);
        }
        if (m) {
            FIBITMAP *d = FreeImage_LockPage(m, 4);
            if (d) { fail("anim_ticks.jxl", "page 4 of 4 locked"); FreeImage_UnlockPage(m, d, FALSE); }
            FreeImage_CloseMultiBitmap(m, 0);
        }
    }

    /* JXL_PLAYBACK: every frame 32-bit */
    {
        FIBITMAP *d = load_page("anim16_lossy.jxl", 1, JXL_PLAYBACK);
        if (!d || FreeImage_GetImageType(d) != FIT_BITMAP || FreeImage_GetBPP(d) != 32) fail("anim16_lossy.jxl", "JXL_PLAYBACK did not give 32-bit frames");
        if (d) FreeImage_Unload(d);
        d = load_page("rgb12_lossless.jxl", 0, JXL_PLAYBACK);
        if (!d || FreeImage_GetImageType(d) != FIT_RGB16) fail("rgb12_lossless.jxl", "JXL_PLAYBACK changed a still image");
        if (d) FreeImage_Unload(d);
    }

    /* every page of test-jxl.jxl: opaque, and the page before outside its frame */
    {
        FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(FIF_JXL, path("test-jxl.jxl"), FALSE, TRUE, TRUE, 0);
        FIBITMAP *prev = NULL;
        int bad = 0;
        for (i = 0; m && i < NFRAMES; i++) {
            FIBITMAP *d = FreeImage_LockPage(m, (int)i);
            unsigned x, y, w = FRAMES[0][0], h = FRAMES[0][1];
            int ok = d && FreeImage_GetWidth(d) == w && FreeImage_GetHeight(d) == h && FreeImage_GetBPP(d) == 32;
            for (y = 0; ok && y < h; y++) {
                const BYTE *row = FreeImage_GetScanLine(d, h - 1 - y);
                const BYTE *old = prev ? FreeImage_GetScanLine(prev, h - 1 - y) : NULL;
                for (x = 0; ok && x < w; x++)
                    if (row[4 * x + FI_RGBA_ALPHA] != 255 || (old && (x >= FRAMES[i][0] || y >= FRAMES[i][1]) && memcmp(row + 4 * x, old + 4 * x, 4))) ok = 0;
            }
            if (!ok) {
                char what[96];
                snprintf(what, sizeof(what), "page %u is not opaque, or not the page before outside its frame", i);
                fail("test-jxl.jxl", what);
                bad = 1;
            }
            if (prev) FreeImage_Unload(prev);
            prev = d ? FreeImage_Clone(d) : NULL;
            if (d) FreeImage_UnlockPage(m, d, FALSE);
        }
        if (prev) FreeImage_Unload(prev);
        if (m) FreeImage_CloseMultiBitmap(m, 0);
        if (!m) fail("test-jxl.jxl", "open failed");
        else if (!bad) printf("ok   test-jxl.jxl: %u pages, each its frame over the page before\n", (unsigned)NFRAMES);
    }

    if (strcmp(FreeImage_GetFIFMimeType(FIF_JXL), "image/jxl") || FreeImage_GetFIFFromFilename("x.jxl") != FIF_JXL)
        fail("plugin", "MIME type or extension");

    printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}
