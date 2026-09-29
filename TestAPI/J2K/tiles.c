/* FreeImage 3 - JPEG 2000 test: images whose code could pass 4 GB are encoded in tiles, and a JP2 stops at 4 GB.
   'make tiles' links J2KHelper and PluginJP2 rebuilt with small limits: every image is tiled, in tiles of 256,
   and a JP2 may not pass 1 MB. Each save must reload exactly (lossless) or at its size (16:1). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static int failures = 0;

static void report(const char *what, int ok) {
    printf("  %-62s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("J2K_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

static unsigned be32(const BYTE *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

/* the image and tile widths in the SIZ marker, which follows SOC in either container */
static int siz(FIMEMORY *mem, unsigned *xsiz, unsigned *xtsiz, unsigned *ytsiz) {
    BYTE *p = NULL;
    DWORD n = 0;
    DWORD i;
    if (!FreeImage_AcquireMemory(mem, &p, &n)) return 0;
    for (i = 0; i + 32 < n; i++) {
        if (p[i] == 0xFF && p[i + 1] == 0x4F && p[i + 2] == 0xFF && p[i + 3] == 0x51) {
            /* Lsiz Rsiz Xsiz Ysiz XOsiz YOsiz XTsiz YTsiz */
            *xsiz = be32(p + i + 8);
            *xtsiz = be32(p + i + 24);
            *ytsiz = be32(p + i + 28);
            return 1;
        }
    }
    return 0;
}

/* smooth, or noise that does not compress */
static unsigned sample(int x, int y, int c, int noise) {
    if (noise) {
        unsigned v = (unsigned)(x * 73856093u) ^ (unsigned)(y * 19349663u) ^ (unsigned)(c * 83492791u);
        v ^= v >> 13; v *= 0x5bd1e995u; v ^= v >> 15;
        return v;
    }
    return (unsigned)(x * 3 + y * 5 + c * 40);
}

static FIBITMAP *make(FREE_IMAGE_TYPE type, int bpp, int w, int h, int noise) {
    FIBITMAP *dib = FreeImage_AllocateT(type, w, h, bpp, 0, 0, 0);
    int x, y, c;
    const int channels = (type == FIT_BITMAP) ? bpp / 8 : (type == FIT_UINT16) ? 1 : (type == FIT_RGB16) ? 3 : 4;
    if (!dib) return NULL;
    for (y = 0; y < h; y++) {
        BYTE *row = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < w; x++) {
            for (c = 0; c < channels; c++) {
                const unsigned v = sample(x, y, c, noise);
                if (type == FIT_BITMAP) row[x * channels + c] = (BYTE)v;
                else ((WORD *)row)[x * channels + c] = (WORD)v;
            }
        }
    }
    return dib;
}

static int same(FIBITMAP *a, FIBITMAP *b) {
    unsigned y;
    const unsigned line = FreeImage_GetLine(a);
    if (!a || !b) return 0;
    if (FreeImage_GetImageType(a) != FreeImage_GetImageType(b) || FreeImage_GetBPP(a) != FreeImage_GetBPP(b)
        || FreeImage_GetWidth(a) != FreeImage_GetWidth(b) || FreeImage_GetHeight(a) != FreeImage_GetHeight(b)) return 0;
    for (y = 0; y < FreeImage_GetHeight(a); y++)
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), line)) return 0;
    return 1;
}

/* save to memory, check the tiling, reload */
static void round_trip(const char *name, FREE_IMAGE_FORMAT fif, FIBITMAP *dib, int rate) {
    char what[128];
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    unsigned xsiz = 0, xtsiz = 0, ytsiz = 0;
    FIBITMAP *back;
    const BOOL saved = FreeImage_SaveToMemory(fif, dib, mem, rate);
    snprintf(what, sizeof(what), "%s %s rate %d: saved in tiles of 256", fif == FIF_J2K ? "J2K" : "JP2", name, rate);
    report(what, saved && siz(mem, &xsiz, &xtsiz, &ytsiz) && xsiz == FreeImage_GetWidth(dib) && xtsiz == 256 && ytsiz == 256);
    FreeImage_SeekMemory(mem, 0, SEEK_SET);
    back = saved ? FreeImage_LoadFromMemory(fif, mem, 0) : NULL;
    snprintf(what, sizeof(what), "  %s", rate == 1 ? "reloads exactly" : "reloads at its size");
    report(what, rate == 1 ? same(dib, back)
        : (back && FreeImage_GetWidth(back) == FreeImage_GetWidth(dib) && FreeImage_GetHeight(back) == FreeImage_GetHeight(dib)));
    if (back) FreeImage_Unload(back);
    FreeImage_CloseMemory(mem);
}

int main(void) {
    static const struct { const char *name; FREE_IMAGE_TYPE type; int bpp; } kinds[] = {
        { "8-bit", FIT_BITMAP, 8 }, { "24-bit", FIT_BITMAP, 24 }, { "32-bit", FIT_BITMAP, 32 },
        { "UINT16", FIT_UINT16, 16 }, { "RGB16", FIT_RGB16, 48 }, { "RGBA16", FIT_RGBA16, 64 },
    };
    const char *path = tmppath("fi_j2k_tiles.jp2");
    FIBITMAP *dib;
    FILE *f;
    size_t k;

    FreeImage_Initialise(FALSE);
    printf("600 x 300, every image tiled\n");
    for (k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        dib = make(kinds[k].type, kinds[k].bpp, 600, 300, 0);
        round_trip(kinds[k].name, FIF_J2K, dib, 1);
        round_trip(kinds[k].name, FIF_JP2, dib, 1);
        round_trip(kinds[k].name, FIF_J2K, dib, 16);
        FreeImage_Unload(dib);
    }

    printf("JP2 limited to 1 MB\n");
    dib = make(FIT_BITMAP, 24, 1024, 512, 1);
    round_trip("24-bit noise, 1.5 MB", FIF_J2K, dib, 1);
    remove(path);
    report("FreeImage_Save, the same as JP2: FALSE", !FreeImage_Save(FIF_JP2, dib, path, 1));
    f = fopen(path, "rb");
    report("  and the file is removed", f == NULL);
    if (f) fclose(f);
    remove(path);
    FreeImage_Unload(dib);

    FreeImage_DeInitialise();
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
