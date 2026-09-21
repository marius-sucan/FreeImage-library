/*
 * FreeImage 3 - HEIF image sequence test
 *
 * An image sequence (animated HEIC, the 'msf1' / 'hevc' brands) opens as a multi-page bitmap
 * with one page per frame, like GIF, APNG, WebP and AVIF. This checks, on the seq-* files of
 * data/ (see data/mkseqdata.sh for how each was made and what it is for):
 *
 * - the frame count, size and bitmap type, and the FIMD_ANIMATION tags on every page -
 *   "FrameTime" from the track's own sample tables, not the duration libheif attaches to a
 *   decoded frame (which belongs to a later one), "Loop" from the edit list, the canvas,
 *   and the constant position, disposal and blend of a frame that is a whole picture;
 * - that a page is the same picture however it is reached: libheif decodes a track forwards
 *   only, so the plugin keeps its place and starts the track over to go back, and pages read
 *   in order, backwards, at random, twice over, one per session or from memory must agree;
 * - header-only pages (FIF_LOAD_NOPIXELS), which describe a page without decoding a frame;
 * - HEIF_PLAYBACK: every frame as 32-bit, equal to the plain page converted;
 * - which track and which pages a file gets: a thumbnail track written first is not the
 *   animation, a file holding both a still image and a sequence is read as its major brand
 *   says, a frame padded by its encoder is cropped to the size the sample entry declares;
 * - an ICC profile in the sample entry, which libheif does not read for a track;
 * - damage: a decoder configuration that yields no frame must fail the page rather than
 *   keep libheif pushing samples forever, a file claiming more frames than the plugin allows
 *   is refused at once, and a file cut short fails its missing frames and still serves the
 *   ones before them;
 * - the page API end to end: the frames and their tags rebuilt as an animated WebP.
 *
 * Standalone: build with the Makefile in this directory, run from it. Scratch files go to
 * $HEIF_TEST_TMP, or the current directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "FreeImage.h"

#define MAXF 16

typedef struct {
    const char *file;
    int frames;
    int width, height;          /* every page */
    FREE_IMAGE_TYPE type;       /* a plain page */
    int bpp;
    long loop;
    int icc;                    /* ICC profile size, 0 = none */
    long frametime[MAXF];       /* milliseconds, one per frame */
    unsigned long long sum;     /* checksum over every page's pixels, in order */
} Expected;

/* Recorded from the first passing run, after checking every file against an independent
 * decoder: ffmpeg 8 (through PyAV) decodes the same tracks to the same number of frames, in the
 * same order, with the same durations - ffmpeg honours the sample tables that libheif's decode
 * gets wrong - and to the same pictures, compared in luma to within the rounding of the colour
 * conversion (the luma planes are bit-identical where they could be compared exactly). The
 * files are written by pillow-heif's libheif and x265, not by the bundled libheif. */
static const Expected EXPECTED[] = {
    /* 200, 600, 1000, 1200 and 2000 ticks of 30000 Hz: libheif's own decode attaches
     * 33, 40, 67, 7 and 7 ms to these frames; no edit list, so one play */
    {"seq-vardelay.heics",   5, 64, 64, FIT_BITMAP, 24, 1, 0, {7, 20, 33, 40, 67}, 0xb664b8c27d6f88a3ULL},
    /* B-frames: shown in another order than decoded; looping forever */
    {"seq-bframes.heics",   10, 64, 64, FIT_BITMAP, 24, 0, 0, {33, 33, 33, 33, 33, 33, 33, 33, 33, 33}, 0x41324ba7c88582a6ULL},
    /* an auxiliary alpha track */
    {"seq-alpha.heics",      6, 64, 64, FIT_BITMAP, 32, 0, 0, {100, 100, 100, 100, 100, 100}, 0x183cdbcc3a1c726eULL},
    /* 10 bits a sample, three plays */
    {"seq-10bit.heics",      4, 64, 64, FIT_RGB16,  48, 3, 0, {50, 50, 50, 50}, 0x2500528303a0de1aULL},
    /* monochrome, all intra */
    {"seq-mono.heics",       4, 64, 64, FIT_BITMAP,  8, 1, 0, {40, 40, 40, 40}, 0xb83aebcf51486dabULL},
    /* coded as 64 x 64, declared 64 x 48 (with alpha) and 33 x 17: cropped to the declared size */
    {"seq-crop.heics",       3, 64, 48, FIT_BITMAP, 32, 0, 0, {100, 100, 100}, 0xb5ff266c35caa1c4ULL},
    {"seq-odd.heics",        3, 33, 17, FIT_BITMAP, 24, 0, 0, {100, 100, 100}, 0x3e529a9120e1a9aeULL},
    /* seq-vardelay with an ICC profile in its sample entry */
    {"seq-icc.heics",        5, 64, 64, FIT_BITMAP, 24, 1, 132, {7, 20, 33, 40, 67}, 0xb664b8c27d6f88a3ULL},
    /* the thumbnail track (32 x 32) comes first and has the lower ID: the pages are the main track's */
    {"seq-thumbfirst.heic",  4, 64, 64, FIT_BITMAP, 24, 1, 0, {250, 250, 250, 250}, 0xfc0388df9669ab34ULL},
    /* a still image as well, but the major brand is 'hevc': the frames */
    {"seq-with-still.heic",  4, 64, 64, FIT_BITMAP, 24, 1, 0, {250, 250, 250, 250}, 0xfc0388df9669ab34ULL},
};
#define NEXPECTED (sizeof(EXPECTED) / sizeof(EXPECTED[0]))

static int g_failures = 0;
static int g_quiet = 0;

static void message(FREE_IMAGE_FORMAT fif, const char *msg) {
    if (!g_quiet) printf("    [%s] %s\n", fif == FIF_UNKNOWN ? "?" : FreeImage_GetFormatFromFIF(fif), msg);
}

static void fail(const char *file, const char *what) {
    printf("    *** MISMATCH in %s: %s\n", file, what);
    g_failures++;
}

static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("HEIF_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

static double now_ms(void) {
    return (double)clock() * 1000.0 / CLOCKS_PER_SEC;
}

/* order-sensitive checksum of the pixel rows, padding excluded */
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

static unsigned long long combine(unsigned long long h, unsigned long long v) {
    int i;
    for (i = 0; i < 8; i++) { h ^= (v >> (8 * i)) & 0xFF; h *= 1099511628211ULL; }
    return h;
}

static int icc_size(FIBITMAP *d) {
    FIICCPROFILE *p = FreeImage_GetICCProfile(d);
    return (p && p->data) ? (int)p->size : 0;
}

/* an animation tag, -1 when absent or not of the type the plugins agree on */
static long anim_tag(FIBITMAP *d, const char *key, FREE_IMAGE_MDTYPE type) {
    FITAG *tag = NULL;
    if (!FreeImage_GetMetadata(FIMD_ANIMATION, d, key, &tag) || !tag) return -1;
    if (FreeImage_GetTagType(tag) != type || FreeImage_GetTagCount(tag) != 1) return -2;
    switch (type) {
        case FIDT_LONG: return *(const LONG *)FreeImage_GetTagValue(tag);
        case FIDT_SHORT: return *(const WORD *)FreeImage_GetTagValue(tag);
        case FIDT_BYTE: return *(const BYTE *)FreeImage_GetTagValue(tag);
        default: return -3;
    }
}

static int count_anim_tags(FIBITMAP *d) {
    return FreeImage_GetMetadataCount(FIMD_ANIMATION, d);
}

/* the eight tags of a frame, as the GIF, APNG, WebP and AVIF plugins write them */
static void check_tags(const Expected *e, FIBITMAP *d, int page, const char *how) {
    char what[160];
    const long ft = anim_tag(d, "FrameTime", FIDT_LONG);
    if (ft != e->frametime[page]) { snprintf(what, sizeof(what), "%s page %d: FrameTime %ld, expected %ld", how, page, ft, e->frametime[page]); fail(e->file, what); }
    if (anim_tag(d, "Loop", FIDT_LONG) != e->loop) { snprintf(what, sizeof(what), "%s page %d: Loop %ld", how, page, anim_tag(d, "Loop", FIDT_LONG)); fail(e->file, what); }
    if (anim_tag(d, "LogicalWidth", FIDT_SHORT) != (long)FreeImage_GetWidth(d) || anim_tag(d, "LogicalHeight", FIDT_SHORT) != (long)FreeImage_GetHeight(d)) {
        snprintf(what, sizeof(what), "%s page %d: the canvas is not the page's size", how, page); fail(e->file, what);
    }
    if (anim_tag(d, "FrameLeft", FIDT_SHORT) != 0 || anim_tag(d, "FrameTop", FIDT_SHORT) != 0) { snprintf(what, sizeof(what), "%s page %d: frame position", how, page); fail(e->file, what); }
    if (anim_tag(d, "DisposalMethod", FIDT_BYTE) != 1 || anim_tag(d, "BlendMethod", FIDT_BYTE) != 1) { snprintf(what, sizeof(what), "%s page %d: disposal / blend", how, page); fail(e->file, what); }
    if (count_anim_tags(d) != 8) { snprintf(what, sizeof(what), "%s page %d: %d animation tags, not 8", how, page, count_anim_tags(d)); fail(e->file, what); }
    if (icc_size(d) != e->icc) { snprintf(what, sizeof(what), "%s page %d: ICC profile of %d bytes", how, page, icc_size(d)); fail(e->file, what); }
}

static BYTE *read_file(const char *path, long *size) {
    FILE *f = fopen(path, "rb"); BYTE *buf; long n;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (BYTE *)malloc(n > 0 ? n : 1);
    if (fread(buf, 1, n, f) != (size_t)n) { free(buf); buf = NULL; }
    fclose(f);
    *size = n;
    return buf;
}

/* the largest difference between two 32-bit bitmaps of one size, per channel sample */
static int max_diff32(FIBITMAP *a, FIBITMAP *b) {
    unsigned x, y; int m = 0;
    if (FreeImage_GetWidth(a) != FreeImage_GetWidth(b) || FreeImage_GetHeight(a) != FreeImage_GetHeight(b)) return 999;
    for (y = 0; y < FreeImage_GetHeight(a); y++) {
        const BYTE *p = FreeImage_GetScanLine(a, y), *q = FreeImage_GetScanLine(b, y);
        for (x = 0; x < FreeImage_GetWidth(a) * 4; x++) { int d = abs((int)p[x] - (int)q[x]); if (d > m) m = d; }
    }
    return m;
}

static void run(const Expected *e, int tolerance) {
    char path[512], what[160];
    unsigned long long sums[MAXF], all = 1469598103934665603ULL;
    FIMULTIBITMAP *mb;
    int n, p, k, worst = 0;
    long size = 0;
    BYTE *data;

    snprintf(path, sizeof(path), "data/%s", e->file);
    if (FreeImage_GetFileType(path, 0) != FIF_HEIF) { fail(e->file, "not detected as HEIF"); return; }
    if (FreeImage_GetFIFFromFilename(path) != FIF_HEIF) fail(e->file, "FreeImage_GetFIFFromFilename is not FIF_HEIF");

    /* --- in order, in one session --- */
    mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, 0);
    if (!mb) { fail(e->file, "FreeImage_OpenMultiBitmap returned NULL"); return; }
    n = FreeImage_GetPageCount(mb);
    if (n != e->frames) { snprintf(what, sizeof(what), "%d pages, expected %d", n, e->frames); fail(e->file, what); }
    if (n > MAXF) n = MAXF;
    for (p = 0; p < n; p++) {
        FIBITMAP *d = FreeImage_LockPage(mb, p);
        sums[p] = 0;
        if (!d) { snprintf(what, sizeof(what), "page %d did not load", p); fail(e->file, what); continue; }
        if ((int)FreeImage_GetWidth(d) != e->width || (int)FreeImage_GetHeight(d) != e->height) { snprintf(what, sizeof(what), "page %d is %ux%u", p, FreeImage_GetWidth(d), FreeImage_GetHeight(d)); fail(e->file, what); }
        if (FreeImage_GetImageType(d) != e->type || (int)FreeImage_GetBPP(d) != e->bpp) { snprintf(what, sizeof(what), "page %d: type %d, %u bpp", p, FreeImage_GetImageType(d), FreeImage_GetBPP(d)); fail(e->file, what); }
        check_tags(e, d, p, "plain");
        sums[p] = sum_pixels(d);
        all = combine(all, sums[p]);
        FreeImage_UnlockPage(mb, d, FALSE);
    }

    /* --- the same pages backwards, at random and twice over, in the same session --- */
    for (p = n - 1; p >= 0; p--) {
        FIBITMAP *d = FreeImage_LockPage(mb, p);
        if (!d || sum_pixels(d) != sums[p]) { snprintf(what, sizeof(what), "backwards: page %d differs", p); fail(e->file, what); }
        if (d) FreeImage_UnlockPage(mb, d, FALSE);
    }
    srand(1234);
    for (k = 0; k < 2 * n; k++) {
        const int q = rand() % n;
        FIBITMAP *d = FreeImage_LockPage(mb, q);
        if (!d || sum_pixels(d) != sums[q]) { snprintf(what, sizeof(what), "at random: page %d differs", q); fail(e->file, what); }
        if (d) FreeImage_UnlockPage(mb, d, FALSE);
    }
    for (p = 0; p < n; p++) {
        for (k = 0; k < 2; k++) {
            FIBITMAP *d = FreeImage_LockPage(mb, p);
            if (!d || sum_pixels(d) != sums[p]) { snprintf(what, sizeof(what), "twice: page %d differs", p); fail(e->file, what); }
            if (d) FreeImage_UnlockPage(mb, d, FALSE);
        }
    }
    FreeImage_CloseMultiBitmap(mb, 0);

    /* --- one session per page: nothing kept, every page decoded from the first frame --- */
    for (p = 0; p < n; p++) {
        FIMULTIBITMAP *one = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, 0);
        FIBITMAP *d = one ? FreeImage_LockPage(one, p) : NULL;
        if (!d || sum_pixels(d) != sums[p]) { snprintf(what, sizeof(what), "fresh session: page %d differs", p); fail(e->file, what); }
        if (d) FreeImage_UnlockPage(one, d, FALSE);
        if (one) FreeImage_CloseMultiBitmap(one, 0);
    }

    /* --- FreeImage_Load is page 0 --- */
    {
        FIBITMAP *d = FreeImage_Load(FIF_HEIF, path, 0);
        if (!d || sum_pixels(d) != sums[0]) fail(e->file, "FreeImage_Load differs from page 0");
        if (d) FreeImage_Unload(d);
    }

    /* --- header only: the same description, no pixels, no decoding --- */
    mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, FIF_LOAD_NOPIXELS);
    for (p = 0; mb && p < n; p++) {
        FIBITMAP *d = FreeImage_LockPage(mb, p);
        if (!d) { fail(e->file, "a header-only page did not load"); continue; }
        if (FreeImage_HasPixels(d)) fail(e->file, "a header-only page has pixels");
        if ((int)FreeImage_GetWidth(d) != e->width || (int)FreeImage_GetHeight(d) != e->height ||
            FreeImage_GetImageType(d) != e->type || (int)FreeImage_GetBPP(d) != e->bpp) fail(e->file, "a header-only page is described differently");
        check_tags(e, d, p, "header-only");
        FreeImage_UnlockPage(mb, d, FALSE);
    }
    if (mb) FreeImage_CloseMultiBitmap(mb, 0); else fail(e->file, "header-only open failed");

    /* --- HEIF_PLAYBACK: 32-bit frames, the plain page converted --- */
    mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, HEIF_PLAYBACK);
    for (p = 0; mb && p < n; p++) {
        FIMULTIBITMAP *plain_mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, 0);
        FIBITMAP *d = FreeImage_LockPage(mb, p);
        FIBITMAP *plain = plain_mb ? FreeImage_LockPage(plain_mb, p) : NULL;
        FIBITMAP *c = NULL;
        if (!d || !plain) { fail(e->file, "a playback page did not load"); }
        else {
            if (FreeImage_GetImageType(d) != FIT_BITMAP || FreeImage_GetBPP(d) != 32) fail(e->file, "a playback page is not 32-bit");
            check_tags(e, d, p, "playback");
            c = FreeImage_ConvertTo32Bits(plain);
            if (!c) fail(e->file, "FreeImage_ConvertTo32Bits failed");
            else { const int m = max_diff32(d, c); if (m > worst) worst = m; FreeImage_Unload(c); }
        }
        if (d) FreeImage_UnlockPage(mb, d, FALSE);
        if (plain) FreeImage_UnlockPage(plain_mb, plain, FALSE);
        if (plain_mb) FreeImage_CloseMultiBitmap(plain_mb, 0);
    }
    if (mb) FreeImage_CloseMultiBitmap(mb, 0); else fail(e->file, "playback open failed");
    if (worst > tolerance) { snprintf(what, sizeof(what), "a playback frame differs from the converted plain frame by %d", worst); fail(e->file, what); }

    /* header only under playback: the 32-bit page, still without pixels */
    mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, HEIF_PLAYBACK | FIF_LOAD_NOPIXELS);
    if (mb) {
        FIBITMAP *d = FreeImage_LockPage(mb, n - 1);
        if (!d || FreeImage_HasPixels(d) || FreeImage_GetBPP(d) != 32) fail(e->file, "a header-only playback page is not a 32-bit header");
        if (d) FreeImage_UnlockPage(mb, d, FALSE);
        FreeImage_CloseMultiBitmap(mb, 0);
    }

    /* --- from memory --- */
    data = read_file(path, &size);
    if (data) {
        FIMEMORY *mem = FreeImage_OpenMemory(data, (DWORD)size);
        FIMULTIBITMAP *m = FreeImage_LoadMultiBitmapFromMemory(FIF_HEIF, mem, 0);
        if (!m || FreeImage_GetPageCount(m) != e->frames) fail(e->file, "memory stream: page count");
        for (p = n - 1; m && p >= 0; p--) {
            FIBITMAP *d = FreeImage_LockPage(m, p);
            if (!d || sum_pixels(d) != sums[p]) { snprintf(what, sizeof(what), "memory stream: page %d differs", p); fail(e->file, what); }
            if (d) FreeImage_UnlockPage(m, d, FALSE);
        }
        if (m) FreeImage_CloseMultiBitmap(m, 0);
        FreeImage_CloseMemory(mem);
        free(data);
    }

    printf("    {\"%s\", %d frames %dx%d, sum 0x%llxULL, playback within %d}\n", e->file, n, e->width, e->height, all, worst);
    if (e->sum && all != e->sum) fail(e->file, "pixel checksum");
}

/* a file with a still image and a sequence whose major brand says images: the still image */
static void check_still_wins(void) {
    const char *path = "data/seq-with-still-heic.heic";
    FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, 0);
    FIBITMAP *d;
    int n = mb ? FreeImage_GetPageCount(mb) : -1;
    d = (n == 1) ? FreeImage_LockPage(mb, 0) : NULL;
    printf("    {\"seq-with-still-heic.heic\", %d page(s)%s}\n", n, d ? (count_anim_tags(d) ? ", animation tags" : ", a still image") : "");
    if (n != 1 || !d) fail(path, "the still image did not win");
    else {
        if (count_anim_tags(d) != 0) fail(path, "the still image carries animation tags");
        if (FreeImage_GetWidth(d) != 64 || FreeImage_GetHeight(d) != 64) fail(path, "the still image is not 64 x 64");
    }
    if (d) FreeImage_UnlockPage(mb, d, FALSE);
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
}

/* a decoder configuration that yields no frame: every page fails, and fails promptly */
static void check_corrupt(void) {
    const char *path = "data/seq-corrupt.heics";
    FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, 0);
    int n = mb ? FreeImage_GetPageCount(mb) : -1, p, loaded = 0;
    const double start = now_ms();
    g_quiet = 1;
    for (p = 0; mb && p < n; p++) {
        FIBITMAP *d = FreeImage_LockPage(mb, p);
        if (d) { loaded++; FreeImage_UnlockPage(mb, d, FALSE); }
    }
    g_quiet = 0;
    {
        const double elapsed = now_ms() - start;
        printf("    {\"seq-corrupt.heics\", %d pages, %d loaded, %.0f ms of CPU}\n", n, loaded, elapsed);
        if (n != 6) fail(path, "page count");
        if (loaded != 0) fail(path, "a frame of a track with no decodable configuration loaded");
        /* before the read budget, libheif pushed samples for as long as the edit list repeats the
           track - forever here, capped only at FI_HEIF_MAX_FRAMES samples, i.e. minutes a page */
        if (elapsed > 10000.0) fail(path, "the failing pages took too long: the read budget did not stop libheif");
    }
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
}

/* more frames than libheif may build tables for: refused before any is allocated */
static void check_limit(void) {
    const char *path = "data/seq-frames-limit.heics";
    FIMULTIBITMAP *mb;
    FIBITMAP *d;
    g_quiet = 1;
    mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, FIF_LOAD_NOPIXELS);
    d = FreeImage_Load(FIF_HEIF, path, FIF_LOAD_NOPIXELS);
    g_quiet = 0;
    printf("    {\"seq-frames-limit.heics\", %s}\n", (!mb && !d) ? "refused" : "OPENED");
    if (mb) { fail(path, "a track of 2592001 frames was opened"); FreeImage_CloseMultiBitmap(mb, 0); }
    if (d) { fail(path, "a track of 2592001 frames was loaded"); FreeImage_Unload(d); }
}

/* cut short in the middle of the media data: the frames before the cut still load, the ones
   after it fail, and going back to the start afterwards works (the track is started over).
   seq-vardelay's five samples end at 1844, 3313, 4700, 6281 and 7544 bytes: a cut at three
   quarters, 5658, leaves three whole ones and half of the fourth - and two frames, because
   libde265 hands a frame back once the next sample has arrived. (seq-alpha would not do: its
   alpha track's samples all come after the colour ones, so any cut into the media data takes
   away every frame's alpha.) */
static void check_truncated(void) {
    const char *path = "data/seq-vardelay.heics";
    long size = 0;
    BYTE *data = read_file(path, &size);
    FIMEMORY *mem;
    FIMULTIBITMAP *mb;
    unsigned long long first = 0;
    int p, loaded = 0, failed = 0, again = 0;
    if (!data) { fail(path, "cannot read"); return; }
    {
        FIBITMAP *d = FreeImage_Load(FIF_HEIF, path, 0);
        first = sum_pixels(d);
        if (d) FreeImage_Unload(d);
    }
    mem = FreeImage_OpenMemory(data, (DWORD)(size * 3 / 4));
    mb = FreeImage_LoadMultiBitmapFromMemory(FIF_HEIF, mem, 0);
    g_quiet = 1;
    for (p = 0; mb && p < FreeImage_GetPageCount(mb); p++) {
        FIBITMAP *d = FreeImage_LockPage(mb, p);
        if (d) { loaded++; FreeImage_UnlockPage(mb, d, FALSE); } else failed++;
    }
    if (mb) {
        FIBITMAP *d = FreeImage_LockPage(mb, 0);
        again = (d && sum_pixels(d) == first);
        if (d) FreeImage_UnlockPage(mb, d, FALSE);
        FreeImage_CloseMultiBitmap(mb, 0);
    }
    g_quiet = 0;
    printf("    {\"seq-vardelay.heics\" cut to 3/4: %d frame(s) loaded, %d failed, page 0 again %s}\n", loaded, failed, again ? "ok" : "FAILED");
    if (loaded != 2 || failed != 3) fail(path, "a truncated sequence should serve its first two frames and fail the rest");
    if (!again) fail(path, "page 0 did not load again after the failed frames");
    FreeImage_CloseMemory(mem);
    free(data);
}

/* the page API end to end: the frames of seq-vardelay, played, appended to an animated WebP,
   which keeps their number, their durations, and their disposal and blend */
static void check_webp_scenario(void) {
    const char *out = tmppath("fi_heif_seq.webp");
    static const long frametime[5] = {7, 20, 33, 40, 67};
    FIMULTIBITMAP *src = FreeImage_OpenMultiBitmap(FIF_HEIF, "data/seq-vardelay.heics", FALSE, TRUE, TRUE, HEIF_PLAYBACK);
    FIMULTIBITMAP *dst;
    int p, n, ok = 1;
    if (!FreeImage_FIFSupportsWriting(FIF_WEBP)) { printf("    (no WebP writer: page API scenario skipped)\n"); if (src) FreeImage_CloseMultiBitmap(src, 0); return; }
    remove(out);
    dst = FreeImage_OpenMultiBitmap(FIF_WEBP, out, TRUE, FALSE, TRUE, 0);
    if (!src || !dst) { fail("seq-vardelay.heics", "WebP scenario: open failed"); if (src) FreeImage_CloseMultiBitmap(src, 0); if (dst) FreeImage_CloseMultiBitmap(dst, 0); return; }
    for (p = 0; p < FreeImage_GetPageCount(src); p++) {
        FIBITMAP *d = FreeImage_LockPage(src, p);
        if (d) { FreeImage_AppendPage(dst, d); FreeImage_UnlockPage(src, d, FALSE); }
    }
    FreeImage_CloseMultiBitmap(src, 0);
    if (!FreeImage_CloseMultiBitmap(dst, WEBP_LOSSLESS)) fail("seq-vardelay.heics", "WebP scenario: the animation was not written");
    dst = FreeImage_OpenMultiBitmap(FIF_WEBP, out, FALSE, TRUE, TRUE, 0);
    n = dst ? FreeImage_GetPageCount(dst) : 0;
    if (n != 5) ok = 0;
    for (p = 0; dst && p < n && p < 5; p++) {
        FIBITMAP *d = FreeImage_LockPage(dst, p);
        if (!d || anim_tag(d, "FrameTime", FIDT_LONG) != frametime[p] || anim_tag(d, "DisposalMethod", FIDT_BYTE) != 1 || anim_tag(d, "BlendMethod", FIDT_BYTE) != 1) ok = 0;
        if (d) FreeImage_UnlockPage(dst, d, FALSE);
    }
    if (dst) FreeImage_CloseMultiBitmap(dst, 0);
    printf("    {seq-vardelay.heics as an animated WebP: %d frames, durations and disposal %s}\n", n, ok ? "kept" : "LOST");
    if (!ok) fail("seq-vardelay.heics", "WebP scenario: frames or their tags were lost");
    remove(out);
}

int main(int argc, char **argv) {
    size_t i;
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(message);
    if (FreeImage_GetFIFFromFilename("x.heics") != FIF_HEIF) fail("plugin", "GetFIFFromFilename(.heics)");
    if (FreeImage_GetFIFFromFilename("x.heifs") != FIF_HEIF) fail("plugin", "GetFIFFromFilename(.heifs)");
    printf("--- sequences ---\n");
    for (i = 0; i < NEXPECTED; i++) {
        /* a 10-bit frame played is libheif's own conversion to 8 bits, done before the one to
           RGB, where FreeImage_ConvertTo32Bits takes the top byte of the 16 bits the plain page
           scales to: the two differ by up to 2 levels (up to 3 for a still image, see decode) */
        const int tolerance = (EXPECTED[i].type == FIT_BITMAP) ? 0 : 3;
        run(&EXPECTED[i], tolerance);
    }
    printf("--- which pages ---\n");
    check_still_wins();
    printf("--- damage ---\n");
    check_corrupt();
    check_limit();
    check_truncated();
    printf("--- the page API ---\n");
    check_webp_scenario();
    printf("--- %d failure(s) ---\n", g_failures);
    FreeImage_DeInitialise();
    return g_failures ? 1 : 0;
}
