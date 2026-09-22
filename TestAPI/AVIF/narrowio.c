/* AVIF stream I/O test: capped 'long' seeks/tells, non-zero start offset */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "FreeImage.h"

#define SAMPLE "data/paris_icc_exif_xmp.avif"
#define JUNK 777

static long g_cap = LONG_MAX;
static int g_refused_seeks = 0, g_refused_tells = 0, g_steps = 0;

static void message(FREE_IMAGE_FORMAT fif, const char *msg) {
    printf("    [%s] %s\n", fif == FIF_UNKNOWN ? "?" : FreeImage_GetFormatFromFIF(fif), msg);
}

static unsigned DLL_CALLCONV rd(void *buf, unsigned size, unsigned count, fi_handle h) {
    return (unsigned)fread(buf, size, count, (FILE *)h);
}
static unsigned DLL_CALLCONV wr(void *buf, unsigned size, unsigned count, fi_handle h) {
    return (unsigned)fwrite(buf, size, count, (FILE *)h);
}
static int DLL_CALLCONV sk(fi_handle h, long offset, int origin) {
    if (origin == SEEK_SET && offset > g_cap) { g_refused_seeks++; return -1; }
    if (origin == SEEK_CUR && offset > 0) g_steps++;
    return fseek((FILE *)h, offset, origin);
}
static long DLL_CALLCONV tl(fi_handle h) {
    long pos = ftell((FILE *)h);
    if (pos > g_cap) { g_refused_tells++; return -1; }
    return pos;
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
    const char *dir = getenv("AVIF_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

int main(int argc, char **argv) {
    FreeImageIO io = { rd, wr, sk, tl };
    FIBITMAP *ref, *d; FILE *f; unsigned long long want; int failures = 0;

    if (argc > 1) g_cap = atol(argv[1]);
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(message);

    ref = FreeImage_Load(FIF_AVIF, SAMPLE, 0);
    if (!ref) { printf("reference load failed\n"); return 1; }
    want = sum_pixels(ref);
    printf("reference: %ux%u %u bpp, sum %016llx\n", FreeImage_GetWidth(ref), FreeImage_GetHeight(ref), FreeImage_GetBPP(ref), want);

    /* 1. capped absolute seeks and tells */
    f = fopen(SAMPLE, "rb");
    d = FreeImage_LoadFromHandle(FIF_AVIF, &io, (fi_handle)f, 0);
    fclose(f);
    printf("capped I/O (cap %ld): load -> %s, refused %d absolute seeks and %d tells, %d forward steps with SEEK_CUR\n",
           g_cap, d ? "ok" : "FAILED", g_refused_seeks, g_refused_tells, g_steps);
    if (!d) failures++;
    else { if (sum_pixels(d) != want) { printf("    pixels differ\n"); failures++; } else printf("    pixels -> exact\n"); FreeImage_Unload(d); }
    if (g_cap < LONG_MAX && (g_refused_tells == 0 || g_steps == 0)) { printf("    the cap was never hit: nothing was exercised\n"); failures++; }

    /* 2. the AVIF preceded by junk, loaded from the current position */
    {
        const char *path = tmppath("fi_avif_offset.bin");
        FILE *out = fopen(path, "wb"); FILE *in = fopen(SAMPLE, "rb"); BYTE buf[4096]; size_t n;
        for (n = 0; n < JUNK; n++) fputc((int)(n * 7), out);
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
        fclose(in); fclose(out);
        g_cap = LONG_MAX;
        f = fopen(path, "rb");
        fseek(f, JUNK, SEEK_SET);
        if (FreeImage_GetFileTypeFromHandle(&io, (fi_handle)f, 0) != FIF_AVIF) { printf("offset stream: not detected as AVIF\n"); failures++; }
        d = FreeImage_LoadFromHandle(FIF_AVIF, &io, (fi_handle)f, 0);
        fclose(f);
        printf("offset stream (%d bytes of junk first): load -> %s\n", JUNK, d ? "ok" : "FAILED");
        if (!d) failures++;
        else { if (sum_pixels(d) != want) { printf("    pixels differ\n"); failures++; } else printf("    pixels -> exact\n"); FreeImage_Unload(d); }
        remove(path);
    }

    FreeImage_Unload(ref);
    FreeImage_DeInitialise();
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
