/* HEIF stream I/O test: a FreeImageIO of our own, and a HEIF not at offset 0 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

#define SAMPLE "data/rainbow-451x461.heic"
#define SEQUENCE "data/seq-bframes.heics"
#define JUNK 777

static void message(FREE_IMAGE_FORMAT fif, const char *msg) {
    printf("    [%s] %s\n", fif == FIF_UNKNOWN ? "?" : FreeImage_GetFormatFromFIF(fif), msg);
}

/* 64-bit positions, as the callbacks carry them */
static unsigned DLL_CALLCONV rd(void *buf, unsigned size, unsigned count, fi_handle h) {
    return (unsigned)fread(buf, size, count, (FILE *)h);
}
static unsigned DLL_CALLCONV wr(void *buf, unsigned size, unsigned count, fi_handle h) {
    return (unsigned)fwrite(buf, size, count, (FILE *)h);
}
static int DLL_CALLCONV sk(fi_handle h, INT64 offset, int origin) {
    return fseeko((FILE *)h, (off_t)offset, origin);
}
static INT64 DLL_CALLCONV tl(fi_handle h) {
    return (INT64)ftello((FILE *)h);
}

static unsigned long long sum_pixels(FIBITMAP *d) {
    unsigned long long s = 1469598103934665603ULL;
    unsigned y, x, n = FreeImage_GetLine(d);
    for (y = 0; y < FreeImage_GetHeight(d); y++) {
        const BYTE *p = FreeImage_GetScanLine(d, y);
        for (x = 0; x < n; x++) { s ^= p[x]; s *= 1099511628211ULL; }
    }
    return s;
}

static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("HEIF_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

/* JUNK bytes, then the file */
static void write_behind_junk(const char *path, const char *src) {
    FILE *out = fopen(path, "wb"); FILE *in = fopen(src, "rb"); BYTE buf[4096]; size_t n;
    for (n = 0; n < JUNK; n++) fputc((int)(n * 7), out);
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
}

int main(void) {
    FreeImageIO io = { rd, wr, sk, tl };
    FIBITMAP *ref, *d; FILE *f; unsigned long long want; int failures = 0;

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(message);

    ref = FreeImage_Load(FIF_HEIF, SAMPLE, 0);
    if (!ref) { printf("reference load failed\n"); return 1; }
    want = sum_pixels(ref);
    printf("reference: %ux%u %u bpp, sum %016llx\n", FreeImage_GetWidth(ref), FreeImage_GetHeight(ref), FreeImage_GetBPP(ref), want);

    /* 1. through our FreeImageIO */
    f = fopen(SAMPLE, "rb");
    d = FreeImage_LoadFromHandle(FIF_HEIF, &io, (fi_handle)f, 0);
    fclose(f);
    printf("handle stream: load -> %s\n", d ? "ok" : "FAILED");
    if (!d) failures++;
    else { if (sum_pixels(d) != want) { printf("    pixels differ\n"); failures++; } else printf("    pixels -> exact\n"); FreeImage_Unload(d); }

    /* 2. HEIF after junk, loaded from the current position */
    {
        const char *path = tmppath("fi_heif_offset.bin");
        write_behind_junk(path, SAMPLE);
        f = fopen(path, "rb");
        fseek(f, JUNK, SEEK_SET);
        if (FreeImage_GetFileTypeFromHandle(&io, (fi_handle)f, 0) != FIF_HEIF) { printf("offset stream: not detected as HEIF\n"); failures++; }
        d = FreeImage_LoadFromHandle(FIF_HEIF, &io, (fi_handle)f, 0);
        fclose(f);
        printf("offset stream (%d bytes of junk first): load -> %s\n", JUNK, d ? "ok" : "FAILED");
        if (!d) failures++;
        else { if (sum_pixels(d) != want) { printf("    pixels differ\n"); failures++; } else printf("    pixels -> exact\n"); FreeImage_Unload(d); }
        remove(path);
    }

    /* 3. a sequence through our FreeImageIO, backwards; then after junk */
    {
        const char *path = tmppath("fi_heif_offset_seq.bin");
        unsigned long long sums[16];
        FIMULTIBITMAP *plain = FreeImage_OpenMultiBitmap(FIF_HEIF, SEQUENCE, FALSE, TRUE, TRUE, 0);
        FIMULTIBITMAP *mb;
        int pages = plain ? FreeImage_GetPageCount(plain) : 0, p, exact = 0;
        if (pages > 16) pages = 16;
        for (p = 0; p < pages; p++) {
            FIBITMAP *pg = FreeImage_LockPage(plain, p);
            sums[p] = pg ? sum_pixels(pg) : 0;
            if (pg) FreeImage_UnlockPage(plain, pg, FALSE);
        }
        if (plain) FreeImage_CloseMultiBitmap(plain, 0);

        f = fopen(SEQUENCE, "rb");
        mb = FreeImage_OpenMultiBitmapFromHandle(FIF_HEIF, &io, (fi_handle)f, 0);
        for (p = pages - 1; mb && p >= 0; p--) {
            FIBITMAP *pg = FreeImage_LockPage(mb, p);
            if (pg && sum_pixels(pg) == sums[p]) exact++;
            if (pg) FreeImage_UnlockPage(mb, pg, FALSE);
        }
        if (mb) FreeImage_CloseMultiBitmap(mb, 0);
        fclose(f);
        printf("sequence, handle stream, backwards: %d of %d pages exact\n", exact, pages);
        if (pages != 10 || exact != pages) failures++;

        write_behind_junk(path, SEQUENCE);
        f = fopen(path, "rb");
        fseek(f, JUNK, SEEK_SET);
        d = FreeImage_LoadFromHandle(FIF_HEIF, &io, (fi_handle)f, 0);
        fclose(f);
        printf("sequence behind %d bytes of junk: first frame -> %s\n", JUNK, !d ? "FAILED" : (sum_pixels(d) == sums[0] ? "exact" : "DIFFERENT"));
        if (!d || sum_pixels(d) != sums[0]) failures++;
        if (d) FreeImage_Unload(d);
        remove(path);
    }

    FreeImage_Unload(ref);
    FreeImage_DeInitialise();
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
