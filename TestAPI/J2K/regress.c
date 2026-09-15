/*
 * FreeImage 3 - JPEG 2000 round-trip regression test
 *
 * Saves every pixel format the J2K and JP2 plugins export, at a matrix of
 * image sizes and compression rates, through FreeImage's memory streams, and
 * reloads each result. Prints one line per combination (encoded size and an
 * order-sensitive checksum of the encoded bytes) and checks that
 *   - every save and reload succeeds and keeps the geometry and pixel type,
 *   - a rate of 1 (which OpenJPEG treats as lossless, 5/3 reversible) reloads
 *     pixel-exact for every format,
 *   - a file round-trip at the default rate works too.
 *
 * Encoded checksums change legitimately when the encoder changes (the 2.4.0
 * rework, for instance), so treat them as a record; the FAIL lines and the
 * final tally are the assertions. Exits non-zero on any failure.
 *
 * Standalone: build with the Makefile in this directory, run from anywhere.
 * Scratch files go to $J2K_TEST_TMP, or the current directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static int failures = 0, runs = 0;
static char msgbuf[2048];

static void handler(FREE_IMAGE_FORMAT fif, const char *msg) {
    size_t n = strlen(msgbuf);
    (void)fif;
    if (n + strlen(msg) + 4 < sizeof(msgbuf)) snprintf(msgbuf + n, sizeof(msgbuf) - n, "%s%s", n ? " | " : "", msg);
    for (n = 0; msgbuf[n]; n++) if (msgbuf[n] == '\n') msgbuf[n] = ' ';
}

static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("J2K_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

static unsigned long long fnv(const BYTE *p, size_t n) {
    unsigned long long h = 1469598103934665603ULL;
    size_t i;
    for (i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}

/* deterministic pseudo-random content, with a greyscale palette for 8bpp */
static FIBITMAP *mk(FREE_IMAGE_TYPE t, int w, int h, int bpp) {
    FIBITMAP *d = (t == FIT_BITMAP) ? FreeImage_Allocate(w, h, bpp, 0, 0, 0) : FreeImage_AllocateT(t, w, h, bpp, 0, 0, 0);
    unsigned y, s = 2463534242u;
    if (!d) return NULL;
    for (y = 0; y < (unsigned)h; y++) {
        BYTE *p = FreeImage_GetScanLine(d, y);
        unsigned n = FreeImage_GetLine(d), x;
        for (x = 0; x < n; x++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; p[x] = (BYTE)(s >> 7); }
    }
    if (bpp == 8) {
        RGBQUAD *pal = FreeImage_GetPalette(d); int i;
        for (i = 0; i < 256; i++) pal[i].rgbRed = pal[i].rgbGreen = pal[i].rgbBlue = (BYTE)i;
    }
    return d;
}

static int same(FIBITMAP *a, FIBITMAP *b) {
    unsigned y, n;
    if (!a || !b) return 0;
    if (FreeImage_GetWidth(a) != FreeImage_GetWidth(b) || FreeImage_GetHeight(a) != FreeImage_GetHeight(b)
        || FreeImage_GetBPP(a) != FreeImage_GetBPP(b) || FreeImage_GetImageType(a) != FreeImage_GetImageType(b)) return 0;
    n = (FreeImage_GetWidth(a) * FreeImage_GetBPP(a) + 7) / 8;   /* ignore pitch padding */
    for (y = 0; y < FreeImage_GetHeight(a); y++)
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), n)) return 0;
    return 1;
}

static int geometry_ok(FIBITMAP *a, FIBITMAP *b) {
    return b && FreeImage_GetWidth(a) == FreeImage_GetWidth(b) && FreeImage_GetHeight(a) == FreeImage_GetHeight(b)
        && FreeImage_GetBPP(a) == FreeImage_GetBPP(b) && FreeImage_GetImageType(a) == FreeImage_GetImageType(b);
}

static void one(const char *name, FIBITMAP *d, FREE_IMAGE_FORMAT fif, int rate) {
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    BYTE *data = NULL; DWORD size = 0;
    FIBITMAP *b = NULL;
    const char *verdict = "ok";
    int ok;
    msgbuf[0] = 0;
    runs++;
    ok = FreeImage_SaveToMemory(fif, d, mem, rate);
    if (ok) {
        FreeImage_AcquireMemory(mem, &data, &size);
        FreeImage_SeekMemory(mem, 0, SEEK_SET);
        b = FreeImage_LoadFromMemory(fif, mem, 0);
        if (!geometry_ok(d, b)) { verdict = "FAIL: reload"; failures++; }
        else if (rate == 1 && !same(d, b)) { verdict = "FAIL: rate 1 not pixel-exact"; failures++; }
        else if (rate == 1) verdict = "exact";
    } else { verdict = "FAIL: save refused"; failures++; }
    printf("%-11s %4ux%-4u %s rate=%-3d  %8lu bytes  sum=%016llx  %s  %s\n", name, FreeImage_GetWidth(d), FreeImage_GetHeight(d),
           FreeImage_GetFormatFromFIF(fif), rate, (unsigned long)size, size ? fnv(data, size) : 0ULL, verdict, msgbuf);
    if (b) FreeImage_Unload(b);
    FreeImage_CloseMemory(mem);
}

static void file_round_trip(const char *name, FIBITMAP *d, FREE_IMAGE_FORMAT fif) {
    const char *path = tmppath(fif == FIF_J2K ? "fi_j2k_regress.j2k" : "fi_j2k_regress.jp2");
    FIBITMAP *b;
    msgbuf[0] = 0;
    runs++;
    if (!FreeImage_Save(fif, d, path, 0)) { printf("%-11s file %s  FAIL: save refused  %s\n", name, FreeImage_GetFormatFromFIF(fif), msgbuf); failures++; return; }
    b = FreeImage_Load(fif, path, 0);
    if (!geometry_ok(d, b)) { printf("%-11s file %s  FAIL: reload  %s\n", name, FreeImage_GetFormatFromFIF(fif), msgbuf); failures++; }
    else printf("%-11s file %s  ok\n", name, FreeImage_GetFormatFromFIF(fif));
    if (b) FreeImage_Unload(b);
    remove(path);
}

int main(void) {
    static const struct { const char *name; FREE_IMAGE_TYPE t; int bpp; } F[] = {
        { "8bpp-grey", FIT_BITMAP, 8 }, { "24bpp-rgb", FIT_BITMAP, 24 }, { "32bpp-rgba", FIT_BITMAP, 32 },
        { "uint16", FIT_UINT16, 16 }, { "rgb16", FIT_RGB16, 48 }, { "rgba16", FIT_RGBA16, 64 },
    };
    static const int S[][2] = { {1,1}, {2,2}, {3,5}, {8,8}, {15,15}, {16,16}, {31,31}, {32,32}, {33,33},
                                {64,48}, {67,53}, {100,1}, {1,100}, {257,129} };
    static const int R[] = { 0, 1, 8, 100 };     /* 0 = plugin default (16:1) */
    static const FREE_IMAGE_FORMAT C[] = { FIF_J2K, FIF_JP2 };
    unsigned f, s, r, c;

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(handler);
    printf("--- JPEG 2000 round-trip matrix (FreeImage %s) ---\n", FreeImage_GetVersion());
    for (f = 0; f < sizeof(F) / sizeof(F[0]); f++) {
        for (s = 0; s < sizeof(S) / sizeof(S[0]); s++) {
            FIBITMAP *d = mk(F[f].t, S[s][0], S[s][1], F[f].bpp);
            if (!d) { printf("%s %dx%d ALLOC FAILED\n", F[f].name, S[s][0], S[s][1]); failures++; continue; }
            for (c = 0; c < 2; c++)
                for (r = 0; r < sizeof(R) / sizeof(R[0]); r++)
                    one(F[f].name, d, C[c], R[r]);
            FreeImage_Unload(d);
        }
    }
    printf("--- file round-trips ---\n");
    for (f = 0; f < sizeof(F) / sizeof(F[0]); f++) {
        FIBITMAP *d = mk(F[f].t, 64, 48, F[f].bpp);
        file_round_trip(F[f].name, d, FIF_J2K);
        file_round_trip(F[f].name, d, FIF_JP2);
        FreeImage_Unload(d);
    }
    printf("--- %d round-trips, %d failures ---\n", runs, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
