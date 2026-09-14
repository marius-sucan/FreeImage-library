/*
 * FreeImage 3 - HEIF regression test
 *
 * Loads every file of the corpus in data/ (libheif's test images and fuzzing
 * corpus, pillow-heif's synthetic images, and four files derived from one of
 * libheif's, see data/README.md) and checks what the plugin is responsible for:
 * format detection (an AVIF is left to the AVIF plugin, a HEIF with a codec
 * FreeImage does not have is claimed and then refused with a message), the
 * bitmap type picked for each pixel format (8-bit to 24/32-bit, 10/12-bit to
 * FIT_RGB16/FIT_RGBA16, monochrome to 8-bit or FIT_UINT16), the geometry after
 * the clap/irot/imir transforms and the direction of the transforms, the
 * metadata (ICC, Exif, XMP), the 'thmb' thumbnail, multi-page access, header-only
 * loads, memory streams, a truncated stream, and a pixel checksum per file. One
 * line per file; anything that deviates from the expected table prints
 * "*** MISMATCH" and the program exits non-zero.
 *
 * Standalone: build with the Makefile in this directory, run from it.
 * "./decode --png" also writes every decoded page as fi_heif_<file>_<page>.png
 * (to $HEIF_TEST_TMP, or the current directory) so the results can be eyeballed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

typedef struct {
    const char *file;
    FREE_IMAGE_FORMAT detect;   /* what FreeImage_GetFileType must say */
    int pages;                  /* page count; 0 = detected as HEIF but must be refused when loading */
    int width, height;          /* page 0, after the transforms */
    FREE_IMAGE_TYPE type;
    int bpp;
    int icc;                    /* ICC profile size, 0 = none */
    int exif;                   /* number of FIMD_EXIF_MAIN tags */
    int xmp;                    /* 1 when an XMP packet is attached */
    int thumb_w, thumb_h;       /* FreeImage_GetThumbnail size, 0 = none */
    unsigned long long sum;     /* pixel checksum of page 0 */
} Expected;

/* Recorded from the first passing run after checking every value against
 * libheif's own view of the file (handle size, depth, alpha, metadata and
 * thumbnail counts) and, for the pixels, against libheif decoding the same file
 * at its native depth into an independently written comparison (all exact).
 * example.heic was eyeballed as well. The rainbow_* files pin the transforms:
 * rainbow_irot1 must equal rainbow_irot0 turned by FreeImage_Rotate(90), i.e.
 * anti-clockwise, imir0 its vertical and imir1 its horizontal flip. */
static const Expected EXPECTED[] = {
    /* libheif tests/data and examples */
    {"clap_cropped.heic",               FIF_HEIF, 1,   64,   64, FIT_BITMAP, 24,   0,  0, 0,   0,   0, 0x82c4f02d23133209ULL},
    {"conformance_window_padding.heic", FIF_HEIF, 1,    1,    1, FIT_BITMAP, 24,   0,  3, 0,   0,   0, 0x4e1faf4f9806b7e0ULL},
    {"rainbow-451x461.heic",            FIF_HEIF, 1,  451,  461, FIT_BITMAP, 24, 672,  0, 1,   0,   0, 0x4e9b6fbd675d7801ULL},
    {"with-alpha-512x512.heic",         FIF_HEIF, 1,  512,  512, FIT_BITMAP, 32, 672,  0, 0,   0,   0, 0xf988382349a0dfc5ULL},
    {"example.heic",                    FIF_HEIF, 2, 1280,  854, FIT_BITMAP, 24,   0,  0, 0, 320, 212, 0x618bed750c918eaaULL},
    /* derived from rainbow-451x461.heic: its clap box rewritten as irot / imir */
    {"rainbow_irot0.heic",              FIF_HEIF, 1,  452,  462, FIT_BITMAP, 24, 672,  0, 1,   0,   0, 0x99739efb6c7e587ULL},
    {"rainbow_irot1.heic",              FIF_HEIF, 1,  462,  452, FIT_BITMAP, 24, 672,  0, 1,   0,   0, 0xa9ce7204f2066137ULL},
    {"rainbow_imir0.heic",              FIF_HEIF, 1,  452,  462, FIT_BITMAP, 24, 672,  0, 1,   0,   0, 0x554a59ca9df78a6fULL},
    {"rainbow_imir1.heic",              FIF_HEIF, 1,  452,  462, FIT_BITMAP, 24, 672,  0, 1,   0,   0, 0xf9bd3d0d79efba73ULL},
    /* libheif fuzzing corpus */
    {"colors-no-alpha.heic",            FIF_HEIF, 1,   64,   64, FIT_BITMAP, 24,   0,  0, 0,   0,   0, 0x9cd7b0b18f5eb2bcULL},
    {"colors-no-alpha-thumbnail.heic",  FIF_HEIF, 1,   72,   72, FIT_BITMAP, 24,   0,  0, 0,  64,  64, 0x74b206253ed61465ULL},
    {"colors-with-alpha.heic",          FIF_HEIF, 1,   64,   64, FIT_BITMAP, 32,   0,  0, 0,   0,   0, 0x1ff17dbf8aeecbe4ULL},
    {"colors-with-alpha-thumbnail.heic",FIF_HEIF, 1,   72,   72, FIT_BITMAP, 32,   0,  0, 0,  64,  64, 0xcbc3046549fe9d83ULL},
    {"hevc32.heif",                     FIF_HEIF, 1,   32,   32, FIT_BITMAP, 24,   0,  0, 0,   0,   0, 0x845e97485c2b2315ULL},
    {"hevc32-mini.heif",                FIF_HEIF, 1,   64,   64, FIT_BITMAP, 24,   0,  0, 0,   0,   0, 0x5c248d9cf2aef1f7ULL},
    {"avif32.heif",                     FIF_AVIF, 0,    0,    0, FIT_BITMAP,  0,   0,  0, 0,   0,   0, 0x0ULL},
    {"avc32.heif",                      FIF_HEIF, 0,    0,    0, FIT_BITMAP,  0,   0,  0, 0,   0,   0, 0x0ULL},
    {"jpeg32.heif",                     FIF_HEIF, 0,    0,    0, FIT_BITMAP,  0,   0,  0, 0,   0,   0, 0x0ULL},
    {"j2k32.heif",                      FIF_HEIF, 0,    0,    0, FIT_BITMAP,  0,   0,  0, 0,   0,   0, 0x0ULL},
    {"unci32.heif",                     FIF_HEIF, 0,    0,    0, FIT_BITMAP,  0,   0,  0, 0,   0,   0, 0x0ULL},
    /* pillow-heif's synthetic images */
    {"RGB_8__128x128.heif",             FIF_HEIF, 1,  128,  128, FIT_BITMAP, 24,   0,  0, 0,   0,   0, 0x963f9a51718c4545ULL},
    {"RGB_10__128x128.heif",            FIF_HEIF, 1,  128,  128, FIT_RGB16,  48,   0,  0, 0,   0,   0, 0x9bff5679bfa622a0ULL},
    {"RGB_12__128x128.heif",            FIF_HEIF, 1,  128,  128, FIT_RGB16,  48,   0,  0, 0,   0,   0, 0x222b44d92ff15932ULL},
    {"RGBA_8__128x128.heif",            FIF_HEIF, 1,  128,  128, FIT_BITMAP, 32,   0,  0, 0,   0,   0, 0x998b4a81867c88d1ULL},
    {"RGBA_10__128x128.heif",           FIF_HEIF, 1,  128,  128, FIT_RGBA16, 64,   0,  0, 0,   0,   0, 0x6e2853a44e4b1031ULL},
    {"RGBA_12__128x128.heif",           FIF_HEIF, 1,  128,  128, FIT_RGBA16, 64,   0,  0, 0,   0,   0, 0x96ddf11feec6bfefULL},
    {"L_8__128x128.heif",               FIF_HEIF, 1,  128,  128, FIT_BITMAP,  8,   0,  0, 0,   0,   0, 0xf96ef6274869df03ULL},
    {"L_10__128x128.heif",              FIF_HEIF, 1,  128,  128, FIT_UINT16, 16,   0,  0, 0,   0,   0, 0x1bbfb50605e6c383ULL},
    {"L_12__128x128.heif",              FIF_HEIF, 1,  128,  128, FIT_UINT16, 16,   0,  0, 0,   0,   0, 0xb5416546dd9e3d83ULL},
    {"LA_8__128x128.heif",              FIF_HEIF, 1,  128,  128, FIT_BITMAP, 32,   0,  0, 0,   0,   0, 0x8b11bdad316bafebULL},
    {"L_xmp.heif",                      FIF_HEIF, 1,   32,   32, FIT_BITMAP,  8,   0,  0, 1,   0,   0, 0x1a38bf429868d5e3ULL},
    {"zPug_3.heic",                     FIF_HEIF, 3,   64,   64, FIT_BITMAP,  8,   0,  0, 0,  32,  32, 0x54b6250bdc043570ULL},
};
#define NEXPECTED (sizeof(EXPECTED) / sizeof(EXPECTED[0]))

static int g_png = 0;
static int g_failures = 0;

static void message(FREE_IMAGE_FORMAT fif, const char *msg) {
    printf("    [%s] %s\n", fif == FIF_UNKNOWN ? "?" : FreeImage_GetFormatFromFIF(fif), msg);
}

static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("HEIF_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

/* order-sensitive checksum of the pixel rows, padding excluded */
static unsigned long long sum_pixels(FIBITMAP *d) {
    unsigned long long h = 1469598103934665603ULL;
    unsigned y, x, n = FreeImage_GetLine(d);
    if (!d || !FreeImage_HasPixels(d)) return 0;
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

static int icc_size(FIBITMAP *d) {
    FIICCPROFILE *p = FreeImage_GetICCProfile(d);
    return (p && p->data) ? (int)p->size : 0;
}

static int has_xmp(FIBITMAP *d) {
    FITAG *tag = NULL;
    return FreeImage_GetMetadata(FIMD_XMP, d, "XMLPacket", &tag) && tag ? 1 : 0;
}

static void observe(FIBITMAP *d, Expected *o) {
    FIBITMAP *t = FreeImage_GetThumbnail(d);
    o->width = FreeImage_GetWidth(d);
    o->height = FreeImage_GetHeight(d);
    o->type = FreeImage_GetImageType(d);
    o->bpp = FreeImage_GetBPP(d);
    o->icc = icc_size(d);
    o->exif = count_tags(d, FIMD_EXIF_MAIN);
    o->xmp = has_xmp(d);
    o->thumb_w = t ? (int)FreeImage_GetWidth(t) : 0;
    o->thumb_h = t ? (int)FreeImage_GetHeight(t) : 0;
    o->sum = sum_pixels(d);
}

static const char *typename_(FREE_IMAGE_TYPE t) {
    switch (t) {
        case FIT_BITMAP: return "FIT_BITMAP"; case FIT_UINT16: return "FIT_UINT16";
        case FIT_RGB16: return "FIT_RGB16"; case FIT_RGBA16: return "FIT_RGBA16";
        default: return "FIT_?";
    }
}

static const char *fifname(FREE_IMAGE_FORMAT fif) {
    return fif == FIF_UNKNOWN ? "UNKNOWN" : FreeImage_GetFormatFromFIF(fif);
}

static void fail(const char *file, const char *what) {
    printf("    *** MISMATCH in %s: %s\n", file, what);
    g_failures++;
}

static void save_png(FIBITMAP *d, const char *file, int page) {
    char name[256]; FIBITMAP *c;
    if (!g_png) return;
    snprintf(name, sizeof(name), "fi_heif_%s_%d.png", file, page);
    c = (FreeImage_GetImageType(d) == FIT_BITMAP) ? FreeImage_Clone(d) : FreeImage_ConvertToType(d, FIT_BITMAP, TRUE);
    if (!c || !FreeImage_Save(FIF_PNG, c, tmppath(name), 0)) printf("    (could not write %s)\n", name);
    if (c) FreeImage_Unload(c);
}

static FIBITMAP *load_from_memory(const char *path, unsigned long long *sum, int truncate_to) {
    FILE *f = fopen(path, "rb"); long n; BYTE *buf; FIMEMORY *mem; FIBITMAP *d = NULL;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (truncate_to > 0 && truncate_to < n) n = truncate_to;
    buf = (BYTE *)malloc(n);
    if (fread(buf, 1, n, f) == (size_t)n) {
        mem = FreeImage_OpenMemory(buf, (DWORD)n);
        if (FreeImage_GetFileTypeFromMemory(mem, 0) == FIF_HEIF) {
            d = FreeImage_LoadFromMemory(FIF_HEIF, mem, 0);
        }
        FreeImage_CloseMemory(mem);
    }
    fclose(f); free(buf);
    if (d && sum) *sum = sum_pixels(d);
    return d;
}

static void run(const Expected *e) {
    char path[512]; Expected o; FIBITMAP *d, *h, *m; FIMULTIBITMAP *mb; int pages = 0, p;
    FREE_IMAGE_FORMAT fif;
    unsigned long long msum = 0;
    snprintf(path, sizeof(path), "data/%s", e->file);
    memset(&o, 0, sizeof(o)); o.file = e->file;

    fif = FreeImage_GetFileType(path, 0);
    if (fif != e->detect) {
        char what[128]; snprintf(what, sizeof(what), "FreeImage_GetFileType is %s, not %s", fifname(fif), fifname(e->detect));
        fail(e->file, what);
        return;
    }
    if (e->detect != FIF_HEIF) {
        /* another plugin's file: the HEIF plugin must not claim it, that is all */
        printf("    {\"%s\", detected as %s, not HEIF -> ok}\n", e->file, fifname(fif));
        return;
    }
    if (FreeImage_GetFIFFromFilename(path) != FIF_HEIF) fail(e->file, "FreeImage_GetFIFFromFilename is not FIF_HEIF");

    d = FreeImage_Load(FIF_HEIF, path, 0);
    if (e->pages == 0) {
        /* a HEIF whose payload FreeImage cannot decode: the message above says why */
        printf("    {\"%s\", refused -> %s}\n", e->file, d ? "LOADED" : "ok");
        if (d) { fail(e->file, "a file with an unsupported payload was loaded"); FreeImage_Unload(d); }
        m = load_from_memory(path, NULL, 0);
        if (m) { fail(e->file, "a file with an unsupported payload was loaded from memory"); FreeImage_Unload(m); }
        return;
    }
    if (!d) { fail(e->file, "FreeImage_Load returned NULL"); return; }
    observe(d, &o);
    save_png(d, e->file, 0);

    /* header only: same geometry, type, metadata and thumbnail, no pixels */
    h = FreeImage_Load(FIF_HEIF, path, FIF_LOAD_NOPIXELS);
    if (!h) fail(e->file, "header-only load returned NULL");
    else {
        FIBITMAP *t = FreeImage_GetThumbnail(h);
        if (FreeImage_HasPixels(h)) fail(e->file, "header-only load has pixels");
        if ((int)FreeImage_GetWidth(h) != o.width || (int)FreeImage_GetHeight(h) != o.height) fail(e->file, "header-only size differs");
        if (FreeImage_GetImageType(h) != o.type || (int)FreeImage_GetBPP(h) != o.bpp) fail(e->file, "header-only type differs");
        if (icc_size(h) != o.icc || count_tags(h, FIMD_EXIF_MAIN) != o.exif || has_xmp(h) != o.xmp) fail(e->file, "header-only metadata differs");
        if ((t ? (int)FreeImage_GetWidth(t) : 0) != o.thumb_w || (t ? (int)FreeImage_GetHeight(t) : 0) != o.thumb_h) fail(e->file, "header-only thumbnail differs");
        FreeImage_Unload(h);
    }

    /* memory stream: identical pixels */
    m = load_from_memory(path, &msum, 0);
    if (!m) fail(e->file, "FreeImage_LoadFromMemory returned NULL");
    else { if (msum != o.sum) fail(e->file, "memory load differs from file load"); FreeImage_Unload(m); }

    /* truncated stream: must fail cleanly (an AddressSanitizer target) */
    m = load_from_memory(path, NULL, 100);
    if (m) { fail(e->file, "a file cut at 100 bytes loaded"); FreeImage_Unload(m); }

    /* multi-page access: page 0 equals the plain load, every page decodes */
    mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, 0);
    if (!mb) fail(e->file, "FreeImage_OpenMultiBitmap returned NULL");
    else {
        pages = FreeImage_GetPageCount(mb);
        for (p = 0; p < pages; p++) {
            FIBITMAP *pg = FreeImage_LockPage(mb, p);
            if (!pg) { fail(e->file, "a page failed to load"); break; }
            if (p == 0 && sum_pixels(pg) != o.sum) fail(e->file, "page 0 differs from the plain load");
            if (p == 0 && ((int)FreeImage_GetWidth(pg) != o.width || (int)FreeImage_GetHeight(pg) != o.height)) fail(e->file, "page 0 size differs");
            if (p > 0) save_png(pg, e->file, p);
            FreeImage_UnlockPage(mb, pg, FALSE);
        }
        FreeImage_CloseMultiBitmap(mb, 0);
    }
    o.pages = pages;
    FreeImage_Unload(d);

    printf("    {\"%s\", FIF_HEIF, %d, %d, %d, %s, %d, %d, %d, %d, %d, %d, 0x%llxULL},\n",
           o.file, o.pages, o.width, o.height, typename_(o.type), o.bpp, o.icc, o.exif, o.xmp, o.thumb_w, o.thumb_h, o.sum);

    if (o.pages != e->pages) fail(e->file, "page count");
    if (o.width != e->width || o.height != e->height) fail(e->file, "size");
    if (o.type != e->type || o.bpp != e->bpp) fail(e->file, "bitmap type");
    if (o.icc != e->icc) fail(e->file, "ICC profile size");
    if (o.exif != e->exif) fail(e->file, "Exif tag count");
    if (o.xmp != e->xmp) fail(e->file, "XMP presence");
    if (o.thumb_w != e->thumb_w || o.thumb_h != e->thumb_h) fail(e->file, "thumbnail size");
    if (o.sum != e->sum) fail(e->file, "pixel checksum");
}

/* the transforms: irot 1 is a quarter turn anti-clockwise (FreeImage_Rotate(+90)),
 * imir 0 exchanges top and bottom, imir 1 left and right */
static void check_transform(const char *base, const char *derived, const char *op) {
    char path[512]; FIBITMAP *a, *b, *t; int ok;
    snprintf(path, sizeof(path), "data/%s", base);
    a = FreeImage_Load(FIF_HEIF, path, 0);
    snprintf(path, sizeof(path), "data/%s", derived);
    b = FreeImage_Load(FIF_HEIF, path, 0);
    if (!a || !b) { fail(derived, "transform check: load failed"); if (a) FreeImage_Unload(a); if (b) FreeImage_Unload(b); return; }
    if (strcmp(op, "rot90") == 0) { t = FreeImage_Rotate(a, 90, NULL); FreeImage_Unload(a); a = t; }
    else if (strcmp(op, "flipv") == 0) FreeImage_FlipVertical(a);
    else FreeImage_FlipHorizontal(a);
    ok = a && FreeImage_GetWidth(a) == FreeImage_GetWidth(b) && FreeImage_GetHeight(a) == FreeImage_GetHeight(b) && sum_pixels(a) == sum_pixels(b);
    printf("    %s == %s(%s) -> %s\n", derived, op, base, ok ? "exact" : "DIFFERENT");
    if (!ok) fail(derived, "transform direction");
    if (a) FreeImage_Unload(a);
    FreeImage_Unload(b);
}

int main(int argc, char **argv) {
    size_t i;
    g_png = (argc > 1 && strcmp(argv[1], "--png") == 0);
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(message);
    printf("FreeImage %s, HEIF plugin: %s (%s), extensions \"%s\", mime \"%s\"\n", FreeImage_GetVersion(),
           FreeImage_GetFormatFromFIF(FIF_HEIF), FreeImage_GetFIFDescription(FIF_HEIF),
           FreeImage_GetFIFExtensionList(FIF_HEIF), FreeImage_GetFIFMimeType(FIF_HEIF));
    if (!FreeImage_FIFSupportsReading(FIF_HEIF)) fail("plugin", "FIFSupportsReading is FALSE");
    if (FreeImage_FIFSupportsWriting(FIF_HEIF)) fail("plugin", "FIFSupportsWriting is TRUE for a read-only plugin");
    if (!FreeImage_FIFSupportsNoPixels(FIF_HEIF)) fail("plugin", "FIFSupportsNoPixels is FALSE");
    if (!FreeImage_FIFSupportsICCProfiles(FIF_HEIF)) fail("plugin", "FIFSupportsICCProfiles is FALSE");
    if (FreeImage_GetFIFFromMime("image/heic") != FIF_HEIF) fail("plugin", "GetFIFFromMime");
    if (FreeImage_GetFIFFromFormat("HEIF") != FIF_HEIF) fail("plugin", "GetFIFFromFormat");
    if (FreeImage_GetFIFFromFilename("x.heic") != FIF_HEIF) fail("plugin", "GetFIFFromFilename(.heic)");
    if (FreeImage_GetFIFFromFilename("x.hif") != FIF_HEIF) fail("plugin", "GetFIFFromFilename(.hif)");
    if (FreeImage_GetFileType("../sample.png", 0) == FIF_HEIF) fail("plugin", "a PNG was detected as HEIF");
    printf("--- observed (paste into EXPECTED after checking) ---\n");
    for (i = 0; i < NEXPECTED; i++) run(&EXPECTED[i]);
    printf("--- transforms ---\n");
    check_transform("rainbow_irot0.heic", "rainbow_irot1.heic", "rot90");
    check_transform("rainbow_irot0.heic", "rainbow_imir0.heic", "flipv");
    check_transform("rainbow_irot0.heic", "rainbow_imir1.heic", "fliph");
    printf("--- %d failure(s) ---\n", g_failures);
    FreeImage_DeInitialise();
    return g_failures ? 1 : 0;
}
