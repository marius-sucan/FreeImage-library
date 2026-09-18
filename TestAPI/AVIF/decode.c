/*
 * FreeImage 3 - AVIF regression test
 *
 * Loads every file of the corpus in data/ (libavif's own test images, BSD-2)
 * and checks what the plugin is responsible for: format detection, the bitmap
 * type picked for each pixel format, the clap/irot/imir transforms, the
 * metadata, multi-page access, header-only loads and memory streams. One line
 * per file; anything that deviates from the expected table prints
 * "*** MISMATCH" and the program exits non-zero.
 *
 * Standalone: build with the Makefile in this directory, run from it.
 * "./decode --png" also writes every decoded page as fi_avif_<file>_<page>.png
 * (to $AVIF_TEST_TMP, or the current directory) so the transforms can be
 * eyeballed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

typedef struct {
    const char *file;
    int pages;                  /* page count */
    int width, height;          /* page 0, after the transforms */
    FREE_IMAGE_TYPE type;
    int bpp;
    int icc;                    /* ICC profile size, 0 = none */
    int exif;                   /* number of FIMD_EXIF_MAIN tags */
    int xmp;                    /* 1 when an XMP packet is attached */
    long frametime, loop;       /* FIMD_ANIMATION on page 0, -1 = absent */
    unsigned long long sum;     /* pixel checksum of page 0 */
} Expected;

/* Recorded from the first passing run and checked against libavif's own view
 * of each container (dimensions, depth, alpha, frame count, repetition count,
 * metadata sizes, transforms) and, for the rotated files, against the PNG
 * exports: "abc" rotated by irot angle 1 reads bottom-to-top, i.e. 90 degrees
 * anti-clockwise. A page count of 0 means the file must be refused: libavif
 * rejects a 'clap' that is not marked essential, whatever the strictness. */
static const Expected EXPECTED[] = {
    {"white_1x1.avif",                              1,    1,    1, FIT_BITMAP, 24,     0,  0, 0,   -1, -1, 0x80ad7f4f245a2ae8ULL},
    {"alpha_noispe.avif",                           1,   80,   80, FIT_BITMAP, 32,     0,  0, 0,   -1, -1, 0x4f9e52edf7b38f7ULL},
    {"clap_irot_imir_non_essential.avif",           0,    0,    0, FIT_BITMAP,  0,     0,  0, 0,   -1, -1, 0x0ULL},
    {"clop_irot_imor.avif",                         1,   34,   12, FIT_RGBA16, 64,     0,  0, 0,   -1, -1, 0xb3eb636bdebd2b49ULL},
    {"abc_color_irot_alpha_irot.avif",              1,  256,  512, FIT_BITMAP, 32,     0,  0, 0,   -1, -1, 0xe2acc89d49ddbcd8ULL},
    {"abc_color_irot_alpha_NOirot.avif",            1,  256,  512, FIT_BITMAP, 32,     0,  0, 0,   -1, -1, 0xe2acc89d49ddbcd8ULL},
    {"colors-animated-8bpc.avif",                   5,  150,  150, FIT_BITMAP, 24,     0,  0, 0,   33,  1, 0xc5df0a8a8f4c5913ULL},
    {"colors-animated-8bpc-alpha-exif-xmp.avif",    5,  150,  150, FIT_BITMAP, 32,     0,  9, 1,  167,  0, 0x74d8a883f5163043ULL},
    {"colors-animated-12bpc-keyframes-0-2-3.avif",  5,   64,   64, FIT_RGBA16, 64,     0,  0, 0, 1000,  0, 0xab529faec3d54383ULL},
    /* the same pixels as colors-animated-8bpc.avif, retimed so that no two frames last
     * the same time and no frame lasts a whole number of milliseconds (see data/retime.py) */
    {"colors-animated-8bpc-variable-delays.avifs",  5,  150,  150, FIT_BITMAP, 24,     0,  0, 0,    7,  1, 0xc5df0a8a8f4c5913ULL},
    {"paris_icc_exif_xmp.avif",                     1,  403,  302, FIT_BITMAP, 24,   596,  9, 1,   -1, -1, 0x2ad7a668c73089dfULL},
    {"sofa_grid1x5_420.avif",                       1, 1024,  770, FIT_BITMAP, 24,     0,  0, 0,   -1, -1, 0x7701ca0843b52fc3ULL},
    {"seine_hdr_rec2020.avif",                      1,  400,  300, FIT_RGB16,  48,     0,  4, 1,   -1, -1, 0x580a626c78e36b5aULL},
    {"color_grid_alpha_nogrid.avif",                1,   80,   80, FIT_BITMAP, 32,     0,  0, 0,   -1, -1, 0xc3be607506ccdb20ULL},
    {"draw_points_idat.avif",                       1,   33,   11, FIT_BITMAP, 32,     0,  0, 0,   -1, -1, 0x44ac3d591023db1ULL},
    {"draw_points_idat_two_ipma.avif",              1,   33,   11, FIT_BITMAP, 32,     0,  0, 0,   -1, -1, 0x44ac3d591023db1ULL},
    {"circle_custom_properties.avif",               1,  100,   60, FIT_BITMAP, 32,     0,  0, 0,   -1, -1, 0x58f31c4067b13045ULL},
    /* MinimizedImageBox files (brand 'mif3'), which libavif reads only when it is built
     * with AVIF_ENABLE_EXPERIMENTAL_MINI. The three carry the same 256x256 tile. */
    {"simple_osm_tile_meta.avif",                   1,  256,  256, FIT_BITMAP, 24,   672, 10, 1,   -1, -1, 0xa3054d39d9bb88bULL},
    {"simple_osm_tile_alpha.avif",                  1,  256,  256, FIT_BITMAP, 32,   672,  0, 0,   -1, -1, 0xcbcec88ea8d79b7cULL},
    {"mini_size_zero.avif",                         1,  256,  256, FIT_BITMAP, 32,   672,  0, 0,   -1, -1, 0xcbcec88ea8d79b7cULL},
};
#define NEXPECTED (sizeof(EXPECTED) / sizeof(EXPECTED[0]))

static int g_png = 0;
static int g_failures = 0;

static void message(FREE_IMAGE_FORMAT fif, const char *msg) {
    printf("    [%s] %s\n", fif == FIF_UNKNOWN ? "?" : FreeImage_GetFormatFromFIF(fif), msg);
}

static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("AVIF_TEST_TMP");
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

static long long_tag(FIBITMAP *d, const char *key) {
    FITAG *tag = NULL;
    if (!FreeImage_GetMetadata(FIMD_ANIMATION, d, key, &tag) || !tag) return -1;
    return *(const LONG *)FreeImage_GetTagValue(tag);
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
    o->width = FreeImage_GetWidth(d);
    o->height = FreeImage_GetHeight(d);
    o->type = FreeImage_GetImageType(d);
    o->bpp = FreeImage_GetBPP(d);
    o->icc = icc_size(d);
    o->exif = count_tags(d, FIMD_EXIF_MAIN);
    o->xmp = has_xmp(d);
    o->frametime = long_tag(d, "FrameTime");
    o->loop = long_tag(d, "Loop");
    o->sum = sum_pixels(d);
}

static const char *typename_(FREE_IMAGE_TYPE t) {
    switch (t) {
        case FIT_BITMAP: return "FIT_BITMAP"; case FIT_UINT16: return "FIT_UINT16";
        case FIT_RGB16: return "FIT_RGB16"; case FIT_RGBA16: return "FIT_RGBA16";
        default: return "FIT_?";
    }
}

static void fail(const char *file, const char *what) {
    printf("    *** MISMATCH in %s: %s\n", file, what);
    g_failures++;
}

static void save_png(FIBITMAP *d, const char *file, int page) {
    char name[256];
    if (!g_png) return;
    snprintf(name, sizeof(name), "fi_avif_%s_%d.png", file, page);
    if (!FreeImage_Save(FIF_PNG, d, tmppath(name), 0)) printf("    (could not write %s)\n", name);
}

static FIBITMAP *load_from_memory(const char *path, unsigned long long *sum, int truncate_to) {
    FILE *f = fopen(path, "rb"); long n; BYTE *buf; FIMEMORY *mem; FIBITMAP *d = NULL;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (truncate_to > 0 && truncate_to < n) n = truncate_to;
    buf = (BYTE *)malloc(n);
    if (fread(buf, 1, n, f) == (size_t)n) {
        mem = FreeImage_OpenMemory(buf, (DWORD)n);
        if (FreeImage_GetFileTypeFromMemory(mem, 0) == FIF_AVIF) {
            d = FreeImage_LoadFromMemory(FIF_AVIF, mem, 0);
        }
        FreeImage_CloseMemory(mem);
    }
    fclose(f); free(buf);
    if (d && sum) *sum = sum_pixels(d);
    return d;
}

/* --- FIMD_ANIMATION ------------------------------------------------------- */

/* One animation tag, insisting on the type the convention gives it: a tag of the wrong
 * type is not the tag a GIF, APNG or WebP writer reads back. */
static int anim_tag(FIBITMAP *d, const char *key, FREE_IMAGE_MDTYPE type, long *value) {
    FITAG *tag = NULL;
    if (!FreeImage_GetMetadata(FIMD_ANIMATION, d, key, &tag) || !tag) return 0;
    if (FreeImage_GetTagType(tag) != type || FreeImage_GetTagCount(tag) != 1) return 0;
    switch (type) {
        case FIDT_BYTE:  *value = *(const BYTE *)FreeImage_GetTagValue(tag); return 1;
        case FIDT_SHORT: *value = *(const WORD *)FreeImage_GetTagValue(tag); return 1;
        case FIDT_LONG:  *value = *(const LONG *)FreeImage_GetTagValue(tag); return 1;
        default: return 0;
    }
}

static int any_anim_tag(FIBITMAP *d) {
    static const char *keys[] = {"FrameTime", "FrameLeft", "FrameTop", "DisposalMethod",
                                 "BlendMethod", "LogicalWidth", "LogicalHeight", "Loop"};
    FITAG *tag = NULL;
    size_t i;
    for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        if (FreeImage_GetMetadata(FIMD_ANIMATION, d, keys[i], &tag) && tag) return 1;
    return 0;
}

/* Every page of a sequence describes its frame the way GIF, APNG and WebP describe
 * theirs: its own duration, where it sits on the canvas, how the canvas is treated, and
 * the canvas and loop count of the file. An AVIF frame is the whole canvas, so the
 * position is 0,0, the disposal is 1 (leave) and the blend is 1 (source); what differs
 * from page to page is the duration. A still image is not an animation and says nothing.
 * 'expected_times' is the per-page duration in milliseconds, or NULL to take page 0's
 * from the table and leave the rest unchecked. */
static void check_anim_tags(const Expected *o, const char *path, const long *expected_times) {
    FIMULTIBITMAP *mb;
    int pages, p;
    long first_loop = -1, v;

    mb = FreeImage_OpenMultiBitmap(FIF_AVIF, path, FALSE, TRUE, TRUE, 0);
    if (!mb) { fail(o->file, "OpenMultiBitmap returned NULL"); return; }
    pages = FreeImage_GetPageCount(mb);
    for (p = 0; p < pages; p++) {
        FIBITMAP *pg = FreeImage_LockPage(mb, p);
        if (!pg) { fail(o->file, "a page failed to load"); break; }

        if (o->pages <= 1) {
            if (any_anim_tag(pg)) fail(o->file, "a still image carries animation tags");
            FreeImage_UnlockPage(mb, pg, FALSE);
            continue;
        }

        if (!anim_tag(pg, "FrameTime", FIDT_LONG, &v)) fail(o->file, "a page has no FrameTime");
        else if (expected_times && v != expected_times[p]) fail(o->file, "a frame's duration");
        else if (!expected_times && p == 0 && v != o->frametime) fail(o->file, "page 0's duration");

        if (!anim_tag(pg, "FrameLeft", FIDT_SHORT, &v) || v != 0) fail(o->file, "FrameLeft");
        if (!anim_tag(pg, "FrameTop", FIDT_SHORT, &v) || v != 0) fail(o->file, "FrameTop");
        if (!anim_tag(pg, "DisposalMethod", FIDT_BYTE, &v) || v != 1) fail(o->file, "DisposalMethod");
        if (!anim_tag(pg, "BlendMethod", FIDT_BYTE, &v) || v != 1) fail(o->file, "BlendMethod");
        if (!anim_tag(pg, "LogicalWidth", FIDT_SHORT, &v) || v != (long)FreeImage_GetWidth(pg))
            fail(o->file, "LogicalWidth is not the canvas");
        if (!anim_tag(pg, "LogicalHeight", FIDT_SHORT, &v) || v != (long)FreeImage_GetHeight(pg))
            fail(o->file, "LogicalHeight is not the canvas");
        if (!anim_tag(pg, "Loop", FIDT_LONG, &v)) fail(o->file, "a page has no Loop");
        else if (p == 0) first_loop = v;
        else if (v != first_loop) fail(o->file, "Loop differs between pages");

        FreeImage_UnlockPage(mb, pg, FALSE);
    }
    FreeImage_CloseMultiBitmap(mb, 0);
    if (o->pages > 1)
        printf("    {\"%s\", tags -> %d page(s) with FrameTime/FrameLeft/FrameTop/Disposal/Blend"
               " + canvas %dx%d and Loop %ld}\n", o->file, pages, o->width, o->height, first_loop);
}

/* The tags exist so that another animated format can be written from these pages. Build
 * an animated WebP out of the AVIF's frames through the page API and read the durations
 * back: what survives the round trip is what the WebP writer found in FIMD_ANIMATION. */
static void check_webp_roundtrip(const char *file, const long *times, int n) {
    char path[512];
    const char *out = tmppath("fi_avif_anim.webp");
    FIMULTIBITMAP *src, *dst;
    int p, pages;
    long v;

    snprintf(path, sizeof(path), "data/%s", file);
    src = FreeImage_OpenMultiBitmap(FIF_AVIF, path, FALSE, TRUE, TRUE, 0);
    dst = FreeImage_OpenMultiBitmap(FIF_WEBP, out, TRUE, FALSE, TRUE, 0);
    if (!src || !dst) { fail(file, "could not open the animation or the WebP to write"); return; }
    pages = FreeImage_GetPageCount(src);
    for (p = 0; p < pages; p++) {
        FIBITMAP *pg = FreeImage_LockPage(src, p);
        if (!pg) { fail(file, "a page failed to load"); break; }
        FreeImage_AppendPage(dst, pg);
        FreeImage_UnlockPage(src, pg, FALSE);
    }
    FreeImage_CloseMultiBitmap(src, 0);
    if (!FreeImage_CloseMultiBitmap(dst, 0)) { fail(file, "writing the WebP animation failed"); return; }

    dst = FreeImage_OpenMultiBitmap(FIF_WEBP, out, FALSE, TRUE, TRUE, 0);
    if (!dst) { fail(file, "the WebP animation could not be reopened"); return; }
    if (FreeImage_GetPageCount(dst) != n) fail(file, "the WebP animation has the wrong frame count");
    else for (p = 0; p < n; p++) {
        FIBITMAP *pg = FreeImage_LockPage(dst, p);
        if (!pg) { fail(file, "a WebP frame failed to load"); break; }
        if (!anim_tag(pg, "FrameTime", FIDT_LONG, &v) || v != times[p])
            fail(file, "a frame's duration did not survive AVIF -> WebP");
        /* and the frame is still a replacement rather than something blended onto what
         * came before: without a BlendMethod tag to read, the WebP writer blends */
        if (!anim_tag(pg, "BlendMethod", FIDT_BYTE, &v) || v != 1)
            fail(file, "the frames are blended in the WebP, not replaced");
        if (!anim_tag(pg, "DisposalMethod", FIDT_BYTE, &v) || v != 1)
            fail(file, "a frame's disposal did not survive AVIF -> WebP");
        FreeImage_UnlockPage(dst, pg, FALSE);
    }
    FreeImage_CloseMultiBitmap(dst, 0);
    printf("    {\"%s\", AVIF -> WebP -> %d frame(s), durations, disposal and blend intact}\n", file, n);
}

/* --- AVIF_PLAYBACK -------------------------------------------------------- */

/* Largest per-channel difference between two 32-bit bitmaps of the same size,
 * -1 when they cannot be compared at all. */
static int max_channel_delta(FIBITMAP *a, FIBITMAP *b) {
    unsigned y, x, w, h;
    int worst = 0;
    if (!a || !b) return -1;
    w = FreeImage_GetWidth(a); h = FreeImage_GetHeight(a);
    if (w != FreeImage_GetWidth(b) || h != FreeImage_GetHeight(b)) return -1;
    if (FreeImage_GetBPP(a) != 32 || FreeImage_GetBPP(b) != 32) return -1;
    for (y = 0; y < h; y++) {
        const BYTE *pa = FreeImage_GetScanLine(a, y);
        const BYTE *pb = FreeImage_GetScanLine(b, y);
        for (x = 0; x < w * 4; x++) {
            int d = (int)pa[x] - (int)pb[x];
            if (d < 0) d = -d;
            if (d > worst) worst = d;
        }
    }
    return worst;
}

/* AVIF_PLAYBACK hands every frame of an image sequence back as a 32-bit bitmap,
 * whatever the file's depth, and a still image ignores it. Each page still has to
 * be the picture the plain load gives: for an 8-bit file that is libavif doing the
 * same conversion into a fourth channel, for a 10/12-bit one it is the same picture
 * quantized by two different routes (libavif rounds 12 bits to 8, FreeImage_ConvertTo32Bits
 * takes the top byte of the 16 the plain load scales to), so a channel may differ by one. */
static void check_playback(const Expected *o, const char *path) {
    FIMULTIBITMAP *mb, *raw;
    FIBITMAP *hdr;
    int pages, p, worst = 0, bound;

    /* the flag has to reach the header-only path too, or FIF_LOAD_NOPIXELS would
     * describe a page in one format and the pixel load produce another */
    hdr = FreeImage_Load(FIF_AVIF, path, FIF_LOAD_NOPIXELS | AVIF_PLAYBACK);
    if (!hdr) fail(o->file, "header-only playback load returned NULL");
    else {
        if (FreeImage_HasPixels(hdr)) fail(o->file, "header-only playback load has pixels");
        if (o->pages > 1) {
            if (FreeImage_GetImageType(hdr) != FIT_BITMAP || FreeImage_GetBPP(hdr) != 32)
                fail(o->file, "header-only playback load is not 32-bit");
        } else if (FreeImage_GetImageType(hdr) != o->type || (int)FreeImage_GetBPP(hdr) != o->bpp) {
            fail(o->file, "a still image did not ignore AVIF_PLAYBACK (header only)");
        }
        FreeImage_Unload(hdr);
    }

    if (o->pages <= 1) {
        /* nothing to play: the page must be exactly what it is without the flag */
        FIBITMAP *still = FreeImage_Load(FIF_AVIF, path, AVIF_PLAYBACK);
        if (!still) fail(o->file, "loading a still image with AVIF_PLAYBACK returned NULL");
        else {
            if (FreeImage_GetImageType(still) != o->type || (int)FreeImage_GetBPP(still) != o->bpp)
                fail(o->file, "a still image did not ignore AVIF_PLAYBACK");
            if (sum_pixels(still) != o->sum) fail(o->file, "AVIF_PLAYBACK changed a still image");
            FreeImage_Unload(still);
        }
        return;
    }

    mb = FreeImage_OpenMultiBitmap(FIF_AVIF, path, FALSE, TRUE, TRUE, AVIF_PLAYBACK);
    raw = FreeImage_OpenMultiBitmap(FIF_AVIF, path, FALSE, TRUE, TRUE, 0);
    if (!mb || !raw) {
        fail(o->file, "OpenMultiBitmap with AVIF_PLAYBACK returned NULL");
        if (mb) FreeImage_CloseMultiBitmap(mb, 0);
        if (raw) FreeImage_CloseMultiBitmap(raw, 0);
        return;
    }
    pages = FreeImage_GetPageCount(mb);
    if (pages != o->pages) fail(o->file, "playback page count differs");
    bound = (o->type == FIT_BITMAP) ? 0 : 1;
    for (p = 0; p < pages; p++) {
        FIBITMAP *pg = FreeImage_LockPage(mb, p);
        FIBITMAP *rp = FreeImage_LockPage(raw, p);
        FIBITMAP *ref = rp ? FreeImage_ConvertTo32Bits(rp) : NULL;
        int d;
        if (!pg || !ref) fail(o->file, "a playback page failed to load");
        else {
            if (FreeImage_GetImageType(pg) != FIT_BITMAP || FreeImage_GetBPP(pg) != 32)
                fail(o->file, "a playback page is not 32-bit");
            if ((int)FreeImage_GetWidth(pg) != o->width || (int)FreeImage_GetHeight(pg) != o->height)
                fail(o->file, "a playback page has the wrong size");
            d = max_channel_delta(pg, ref);
            if (d < 0) fail(o->file, "a playback page could not be compared with the plain page");
            else if (d > worst) worst = d;
        }
        if (ref) FreeImage_Unload(ref);
        if (rp) FreeImage_UnlockPage(raw, rp, FALSE);
        if (pg) FreeImage_UnlockPage(mb, pg, FALSE);
    }
    if (worst > bound) fail(o->file, "a playback page differs from the plain page");
    printf("    {\"%s\", playback -> %d page(s), 32bpp, max channel delta %d (<= %d)}\n",
           o->file, pages, worst, bound);
    FreeImage_CloseMultiBitmap(raw, 0);
    FreeImage_CloseMultiBitmap(mb, 0);
}

/* FreeImage_OpenMultiBitmap() with read_only FALSE asks for a document that can be
 * written back to its file, and AVIF has no writer: the pages come out all the same,
 * so an animation can still be played, and the close reports that nothing could be
 * saved. FreeImage_LoadMultiBitmapFromMemory() passes read_only FALSE itself and has
 * no file behind it, so it is not held to the same test. */
static void check_readwrite_close(const char *file) {
    char path[512];
    FIMULTIBITMAP *mb;
    FIBITMAP *pg;
    FILE *f;
    long n;
    BYTE *buf;
    FIMEMORY *mem;

    snprintf(path, sizeof(path), "data/%s", file);

    mb = FreeImage_OpenMultiBitmap(FIF_AVIF, path, FALSE, TRUE, TRUE, 0);
    if (!mb) fail(file, "read-only OpenMultiBitmap returned NULL");
    else if (!FreeImage_CloseMultiBitmap(mb, 0)) fail(file, "read-only CloseMultiBitmap returned FALSE");

    mb = FreeImage_OpenMultiBitmap(FIF_AVIF, path, FALSE, FALSE, TRUE, AVIF_PLAYBACK);
    if (!mb) fail(file, "OpenMultiBitmap with read_only FALSE returned NULL");
    else {
        if (FreeImage_GetPageCount(mb) < 2) fail(file, "a writable session lost the animation's pages");
        pg = FreeImage_LockPage(mb, 1);
        if (!pg) fail(file, "a page of a writable session failed to load");
        else {
            if (FreeImage_GetBPP(pg) != 32) fail(file, "a writable session ignored AVIF_PLAYBACK");
            FreeImage_UnlockPage(mb, pg, FALSE);
        }
        if (FreeImage_CloseMultiBitmap(mb, 0))
            fail(file, "CloseMultiBitmap returned TRUE for a format that cannot be written");
    }

    mb = FreeImage_OpenMultiBitmap(FIF_AVIF, tmppath("fi_avif_created.avifs"), TRUE, FALSE, TRUE, 0);
    if (mb) {
        fail(file, "OpenMultiBitmap created a document in a format that cannot be written");
        FreeImage_CloseMultiBitmap(mb, 0);
    }

    f = fopen(path, "rb");
    if (!f) { fail(file, "could not open the file"); return; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (BYTE *)malloc(n);
    if (fread(buf, 1, n, f) == (size_t)n) {
        mem = FreeImage_OpenMemory(buf, (DWORD)n);
        mb = FreeImage_LoadMultiBitmapFromMemory(FIF_AVIF, mem, 0);
        if (!mb) fail(file, "LoadMultiBitmapFromMemory returned NULL");
        else if (!FreeImage_CloseMultiBitmap(mb, 0))
            fail(file, "CloseMultiBitmap returned FALSE for a memory stream");
        FreeImage_CloseMemory(mem);
    }
    fclose(f); free(buf);
    printf("    {\"%s\", read_only=1 close -> TRUE, read_only=0 close -> FALSE, from memory close -> TRUE}\n", file);
}

static void run(const Expected *e) {
    char path[512]; Expected o; FIBITMAP *d, *h, *m; FIMULTIBITMAP *mb; int pages = 0, p;
    unsigned long long msum = 0;
    snprintf(path, sizeof(path), "data/%s", e->file);
    memset(&o, 0, sizeof(o)); o.file = e->file;

    if (FreeImage_GetFileType(path, 0) != FIF_AVIF) { fail(e->file, "FreeImage_GetFileType is not FIF_AVIF"); return; }
    if (FreeImage_GetFIFFromFilename(path) != FIF_AVIF) fail(e->file, "FreeImage_GetFIFFromFilename is not FIF_AVIF");

    d = FreeImage_Load(FIF_AVIF, path, 0);
    if (e->pages == 0) {
        /* a file the decoder must refuse: the message above says why */
        printf("    {\"%s\", refused -> %s}\n", e->file, d ? "LOADED" : "ok");
        if (d) { fail(e->file, "a file libavif rejects was loaded"); FreeImage_Unload(d); }
        m = load_from_memory(path, NULL, 0);
        if (m) { fail(e->file, "a file libavif rejects was loaded from memory"); FreeImage_Unload(m); }
        return;
    }
    if (!d) { fail(e->file, "FreeImage_Load returned NULL"); return; }
    observe(d, &o);
    save_png(d, e->file, 0);

    /* header only: same geometry, type and metadata, no pixels */
    h = FreeImage_Load(FIF_AVIF, path, FIF_LOAD_NOPIXELS);
    if (!h) fail(e->file, "header-only load returned NULL");
    else {
        if (FreeImage_HasPixels(h)) fail(e->file, "header-only load has pixels");
        if ((int)FreeImage_GetWidth(h) != o.width || (int)FreeImage_GetHeight(h) != o.height) fail(e->file, "header-only size differs");
        if (FreeImage_GetImageType(h) != o.type || (int)FreeImage_GetBPP(h) != o.bpp) fail(e->file, "header-only type differs");
        if (icc_size(h) != o.icc || count_tags(h, FIMD_EXIF_MAIN) != o.exif || has_xmp(h) != o.xmp) fail(e->file, "header-only metadata differs");
        if (long_tag(h, "FrameTime") != o.frametime) fail(e->file, "header-only FrameTime differs");
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
    mb = FreeImage_OpenMultiBitmap(FIF_AVIF, path, FALSE, TRUE, TRUE, 0);
    if (!mb) fail(e->file, "FreeImage_OpenMultiBitmap returned NULL");
    else {
        pages = FreeImage_GetPageCount(mb);
        for (p = 0; p < pages; p++) {
            FIBITMAP *pg = FreeImage_LockPage(mb, p);
            if (!pg) { fail(e->file, "a page failed to load"); break; }
            if (p == 0 && sum_pixels(pg) != o.sum) fail(e->file, "page 0 differs from the plain load");
            if ((int)FreeImage_GetWidth(pg) != o.width || (int)FreeImage_GetHeight(pg) != o.height) fail(e->file, "page size differs");
            if (p > 0) save_png(pg, e->file, p);
            FreeImage_UnlockPage(mb, pg, FALSE);
        }
        FreeImage_CloseMultiBitmap(mb, 0);
    }
    o.pages = pages;
    check_playback(&o, path);
    check_anim_tags(&o, path, NULL);
    FreeImage_Unload(d);

    printf("    {\"%s\", %d, %d, %d, %s, %d, %d, %d, %d, %ld, %ld, 0x%llxULL},\n",
           o.file, o.pages, o.width, o.height, typename_(o.type), o.bpp, o.icc, o.exif, o.xmp, o.frametime, o.loop, o.sum);

    if (o.pages != e->pages) fail(e->file, "page count");
    if (o.width != e->width || o.height != e->height) fail(e->file, "size");
    if (o.type != e->type || o.bpp != e->bpp) fail(e->file, "bitmap type");
    if (o.icc != e->icc) fail(e->file, "ICC profile size");
    if (o.exif != e->exif) fail(e->file, "Exif tag count");
    if (o.xmp != e->xmp) fail(e->file, "XMP presence");
    if (o.frametime != e->frametime || o.loop != e->loop) fail(e->file, "animation tags");
    if (o.sum != e->sum) fail(e->file, "pixel checksum");
}

int main(int argc, char **argv) {
    size_t i;
    g_png = (argc > 1 && strcmp(argv[1], "--png") == 0);
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(message);
    printf("FreeImage %s, AVIF plugin: %s (%s), extensions \"%s\", mime \"%s\"\n", FreeImage_GetVersion(),
           FreeImage_GetFormatFromFIF(FIF_AVIF), FreeImage_GetFIFDescription(FIF_AVIF),
           FreeImage_GetFIFExtensionList(FIF_AVIF), FreeImage_GetFIFMimeType(FIF_AVIF));
    if (!FreeImage_FIFSupportsReading(FIF_AVIF)) fail("plugin", "FIFSupportsReading is FALSE");
    if (FreeImage_FIFSupportsWriting(FIF_AVIF)) fail("plugin", "FIFSupportsWriting is TRUE for a read-only plugin");
    if (!FreeImage_FIFSupportsNoPixels(FIF_AVIF)) fail("plugin", "FIFSupportsNoPixels is FALSE");
    if (!FreeImage_FIFSupportsICCProfiles(FIF_AVIF)) fail("plugin", "FIFSupportsICCProfiles is FALSE");
    if (FreeImage_GetFIFFromMime("image/avif") != FIF_AVIF) fail("plugin", "GetFIFFromMime");
    if (FreeImage_GetFIFFromFormat("AVIF") != FIF_AVIF) fail("plugin", "GetFIFFromFormat");
    if (FreeImage_GetFIFFromFilename("x.avifs") != FIF_AVIF) fail("plugin", "GetFIFFromFilename(.avifs)");
    if (FreeImage_GetFileType("../sample.png", 0) == FIF_AVIF) fail("plugin", "a PNG was detected as AVIF");
    check_readwrite_close("colors-animated-8bpc.avif");
    {
        /* 200, 600, 1000, 1200 and 2000 ticks of a 30000 Hz timescale: 6.67, 20, 33.33,
         * 40 and 66.67 ms, none of them a whole millisecond but the second and fourth */
        static const long times[] = {7, 20, 33, 40, 67};
        const char *name = "colors-animated-8bpc-variable-delays.avifs";
        size_t k;
        for (k = 0; k < NEXPECTED && strcmp(EXPECTED[k].file, name) != 0; k++) { }
        if (k == NEXPECTED) fail("EXPECTED", "the variable-delay entry is missing");
        else {
            char path[512];
            snprintf(path, sizeof(path), "data/%s", name);
            check_anim_tags(&EXPECTED[k], path, times);
            check_webp_roundtrip(name, times, 5);
        }
    }
    printf("--- observed (paste into EXPECTED after checking) ---\n");
    for (i = 0; i < NEXPECTED; i++) run(&EXPECTED[i]);
    printf("--- %d failure(s) ---\n", g_failures);
    FreeImage_DeInitialise();
    return g_failures ? 1 : 0;
}
