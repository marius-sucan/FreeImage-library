/* HEIF third-party sample test; get the files with fetch_samples.sh */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

typedef struct {
    const char *file;           /* under samples/ */
    const char *what;
    int pages;
    int width, height;          /* every page */
    int bpp;                    /* every page is FIT_BITMAP */
    long frametime;             /* ms; -1 = still image, no tags */
    long loop;
    unsigned long long sum;     /* over every page, in order */
} Expected;

/* the nine MPEG files on one bitstream share a checksum */
static const Expected EXPECTED[] = {
    /* Nokia (https://nokiatech.github.io/heif/examples.html) */
    {"nokia/starfield_animation.heic", "animation, a cover image too, major brand msf1", 120, 256, 144, 24, 40, 1, 0x767c3bbdffcd1a3fULL},
    {"nokia/sea1_animation.heic",      "animation, a cover image too, major brand msf1", 120, 256, 144, 24, 40, 1, 0x16900339c6701b87ULL},
    {"nokia/bird_burst.heic",          "burst: 4 still images, a 640x360 track and its 128x72 thumbnail track", 90, 640, 360, 24, 33, 1, 0xf5e95dd16ec3906eULL},
    {"nokia/rally_burst.heic",         "burst: 4 still images, a 640x360 track and its 128x72 thumbnail track", 60, 640, 360, 24, 40, 1, 0xca20763d3db798a0ULL},
    {"nokia/random_collection_1440x960.heic", "a collection of still images", 4, 1440, 960, 24, -1, 0, 0x8b6e254b6d415296ULL},
    {"nokia/stereo_1200x800.heic",     "a stereo pair of still images", 2, 1200, 800, 24, -1, 0, 0xb35b966b40ed20abULL},
    /* MPEG file-format conformance (https://github.com/MPEGGroup/FileFormatConformance) */
    {"mpeg/C001.heic", "an image item and an image sequence, the item primary; major brand msf1", 8, 1280, 720, 24, 200, 1, 0x25a143bee721d2feULL},
    {"mpeg/C026.heic", "an image sequence", 8, 1280, 720, 24, 20, 1, 0x25a143bee721d2feULL},
    {"mpeg/C027.heic", "inter prediction, no intra prediction in the inter frames", 16, 1280, 720, 24, 20, 1, 0xa27e576aece3ae20ULL},
    {"mpeg/C028.heic", "inter prediction with intra prediction in the inter frames", 16, 1280, 720, 24, 20, 1, 0xb0d54282e34523f6ULL},
    {"mpeg/C029.heic", "an edit list playing the first 5 samples twice: every sample once here", 8, 1280, 720, 24, 100, 1, 0x25a143bee721d2feULL},
    {"mpeg/C030.heic", "an edit list with a pause at the beginning: not played", 8, 1280, 720, 24, 100, 1, 0x25a143bee721d2feULL},
    {"mpeg/C031.heic", "an image sequence and a video track as alternatives: the image sequence", 8, 1280, 720, 24, 100, 1, 0x25a143bee721d2feULL},
    {"mpeg/C032.heic", "an image sequence with a thumbnail track", 8, 1280, 720, 24, 100, 1, 0x25a143bee721d2feULL},
    {"mpeg/C036.heic", "all intra, repeated 3 times", 8, 1280, 720, 24, 100, 3, 0x25a143bee721d2feULL},
    {"mpeg/C037.heic", "all intra, repeated 1.5 times: libheif rounds down", 8, 1280, 720, 24, 100, 1, 0x25a143bee721d2feULL},
    {"mpeg/C038.heic", "all intra, repeated forever", 8, 1280, 720, 24, 100, 0, 0x25a143bee721d2feULL},
    {"mpeg/C041.heic", "a sample not for display, then 8 inter frames: libheif shows all 9", 9, 1920, 1080, 24, 100, 1, 0x92a60a4d5771a6fbULL},
};
#define NEXPECTED (sizeof(EXPECTED) / sizeof(EXPECTED[0]))

static int g_failures = 0;
static int g_record = 0;

static void message(FREE_IMAGE_FORMAT fif, const char *msg) {
    printf("    [%s] %s\n", fif == FIF_UNKNOWN ? "?" : FreeImage_GetFormatFromFIF(fif), msg);
}

static void fail(const char *file, const char *what) {
    printf("    *** MISMATCH in %s: %s\n", file, what);
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

static unsigned long long combine(unsigned long long h, unsigned long long v) {
    int i;
    for (i = 0; i < 8; i++) { h ^= (v >> (8 * i)) & 0xFF; h *= 1099511628211ULL; }
    return h;
}

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

static void check_page(const Expected *e, FIBITMAP *d, int page, const char *how) {
    char what[200];
    if ((int)FreeImage_GetWidth(d) != e->width || (int)FreeImage_GetHeight(d) != e->height) {
        snprintf(what, sizeof(what), "%s page %d is %ux%u", how, page, FreeImage_GetWidth(d), FreeImage_GetHeight(d)); fail(e->file, what);
    }
    if (FreeImage_GetImageType(d) != FIT_BITMAP || (int)FreeImage_GetBPP(d) != e->bpp) {
        snprintf(what, sizeof(what), "%s page %d: type %d, %u bpp", how, page, FreeImage_GetImageType(d), FreeImage_GetBPP(d)); fail(e->file, what);
    }
    if (e->frametime < 0) {
        if (FreeImage_GetMetadataCount(FIMD_ANIMATION, d) != 0) { snprintf(what, sizeof(what), "%s page %d: a still image with animation tags", how, page); fail(e->file, what); }
        return;
    }
    if (anim_tag(d, "FrameTime", FIDT_LONG) != e->frametime) { snprintf(what, sizeof(what), "%s page %d: FrameTime %ld", how, page, anim_tag(d, "FrameTime", FIDT_LONG)); fail(e->file, what); }
    if (anim_tag(d, "Loop", FIDT_LONG) != e->loop) { snprintf(what, sizeof(what), "%s page %d: Loop %ld", how, page, anim_tag(d, "Loop", FIDT_LONG)); fail(e->file, what); }
    if (anim_tag(d, "LogicalWidth", FIDT_SHORT) != e->width || anim_tag(d, "LogicalHeight", FIDT_SHORT) != e->height) { snprintf(what, sizeof(what), "%s page %d: canvas", how, page); fail(e->file, what); }
    if (anim_tag(d, "FrameLeft", FIDT_SHORT) != 0 || anim_tag(d, "FrameTop", FIDT_SHORT) != 0 ||
        anim_tag(d, "DisposalMethod", FIDT_BYTE) != 1 || anim_tag(d, "BlendMethod", FIDT_BYTE) != 1) { snprintf(what, sizeof(what), "%s page %d: position, disposal or blend", how, page); fail(e->file, what); }
    if (FreeImage_GetMetadataCount(FIMD_ANIMATION, d) != 8) { snprintf(what, sizeof(what), "%s page %d: %u animation tags", how, page, FreeImage_GetMetadataCount(FIMD_ANIMATION, d)); fail(e->file, what); }
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

static void run(const Expected *e) {
    char path[512], what[200];
    unsigned long long *sums, all = 1469598103934665603ULL;
    FIMULTIBITMAP *mb;
    int n, p, k;
    long size = 0;
    BYTE *data;

    snprintf(path, sizeof(path), "samples/%s", e->file);
    data = read_file(path, &size);
    if (!data) { fail(e->file, "missing - run: sh fetch_samples.sh"); return; }
    if (FreeImage_GetFileType(path, 0) != FIF_HEIF) { fail(e->file, "not detected as HEIF"); free(data); return; }

    /* every page in order */
    mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, 0);
    n = mb ? FreeImage_GetPageCount(mb) : 0;
    if (n != e->pages) { snprintf(what, sizeof(what), "%d pages, expected %d", n, e->pages); fail(e->file, what); }
    sums = (unsigned long long *)calloc(n > 0 ? n : 1, sizeof(unsigned long long));
    for (p = 0; p < n; p++) {
        FIBITMAP *d = FreeImage_LockPage(mb, p);
        if (!d) { snprintf(what, sizeof(what), "page %d did not load", p); fail(e->file, what); continue; }
        check_page(e, d, p, "plain");
        sums[p] = sum_pixels(d);
        all = combine(all, sums[p]);
        FreeImage_UnlockPage(mb, d, FALSE);
    }

    /* backwards, then at random: each a fresh start of the track */
    for (p = n - 1; mb && p >= 0 && p >= n - 3; p--) {
        FIBITMAP *d = FreeImage_LockPage(mb, p);
        if (!d || sum_pixels(d) != sums[p]) { snprintf(what, sizeof(what), "backwards: page %d differs", p); fail(e->file, what); }
        if (d) FreeImage_UnlockPage(mb, d, FALSE);
    }
    srand(99);
    for (k = 0; mb && n > 0 && k < 4; k++) {
        const int q = rand() % n;
        FIBITMAP *d = FreeImage_LockPage(mb, q);
        if (!d || sum_pixels(d) != sums[q]) { snprintf(what, sizeof(what), "at random: page %d differs", q); fail(e->file, what); }
        if (d) FreeImage_UnlockPage(mb, d, FALSE);
    }
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);

    /* header only, playback, and a memory stream */
    mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, FIF_LOAD_NOPIXELS);
    for (p = 0; mb && p < n; p++) {
        FIBITMAP *d = FreeImage_LockPage(mb, p);
        if (!d || FreeImage_HasPixels(d)) { fail(e->file, "a header-only page failed or has pixels"); if (d) FreeImage_UnlockPage(mb, d, FALSE); break; }
        check_page(e, d, p, "header-only");
        FreeImage_UnlockPage(mb, d, FALSE);
    }
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
    if (e->frametime >= 0 && n > 0) {
        FIMULTIBITMAP *plain = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, 0);
        mb = FreeImage_OpenMultiBitmap(FIF_HEIF, path, FALSE, TRUE, TRUE, HEIF_PLAYBACK);
        if (mb && plain) {
            FIBITMAP *d = FreeImage_LockPage(mb, n - 1), *q = FreeImage_LockPage(plain, n - 1), *c = q ? FreeImage_ConvertTo32Bits(q) : NULL;
            if (!d || FreeImage_GetBPP(d) != 32 || !c || sum_pixels(c) != sum_pixels(d)) fail(e->file, "the played last frame is not the plain one as 32-bit");
            if (c) FreeImage_Unload(c);
            if (d) FreeImage_UnlockPage(mb, d, FALSE);
            if (q) FreeImage_UnlockPage(plain, q, FALSE);
        }
        if (mb) FreeImage_CloseMultiBitmap(mb, 0);
        if (plain) FreeImage_CloseMultiBitmap(plain, 0);
    }
    {
        FIMEMORY *mem = FreeImage_OpenMemory(data, (DWORD)size);
        FIMULTIBITMAP *m = FreeImage_LoadMultiBitmapFromMemory(FIF_HEIF, mem, 0);
        FIBITMAP *d = (m && n > 0) ? FreeImage_LockPage(m, n - 1) : NULL;
        if (!d || sum_pixels(d) != sums[n - 1]) fail(e->file, "memory stream: the last page differs");
        if (d) FreeImage_UnlockPage(m, d, FALSE);
        if (m) FreeImage_CloseMultiBitmap(m, 0);
        FreeImage_CloseMemory(mem);
    }

    /* C041: the non-display sample is shown; same picture as the fifth */
    if (strcmp(e->file, "mpeg/C041.heic") == 0 && n == 9 && sums[0] != sums[4]) fail(e->file, "the first page is not the fifth's picture");

    printf("    {\"%s\", %d pages %dx%d, sum 0x%llxULL}\n", e->file, n, e->width, e->height, all);
    if (!g_record && all != e->sum) fail(e->file, "pixel checksum");
    free(sums);
    free(data);
}

int main(int argc, char **argv) {
    size_t i;
    g_record = (argc > 1 && strcmp(argv[1], "--record") == 0);
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(message);
    printf("--- third-party samples (samples/README.md) ---\n");
    for (i = 0; i < NEXPECTED; i++) run(&EXPECTED[i]);
    printf("--- %d failure(s) ---\n", g_failures);
    FreeImage_DeInitialise();
    return g_failures ? 1 : 0;
}
