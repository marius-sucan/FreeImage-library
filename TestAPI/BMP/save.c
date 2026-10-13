/* FreeImage 3 - BMP test: what a save stores besides the pixels: biSizeImage, palette bytes, row padding */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

#define BMP_RGB 0
#define BMP_BITFIELDS 3

static int failures = 0;

static void report(const char *what, int ok) {
    printf("  %-64s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

typedef struct { BYTE *p; DWORD n; } BUF;

static DWORD le32(const BYTE *p) { return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24); }

static BUF save(FIBITMAP *dib) {
    BUF b = { NULL, 0 };
    BYTE *bytes = NULL;
    DWORD size = 0;
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    if (FreeImage_SaveToMemory(FIF_BMP, dib, mem, 0) && FreeImage_AcquireMemory(mem, &bytes, &size) && size > 54) {
        b.p = (BYTE *)malloc(size);
        memcpy(b.p, bytes, size);
        b.n = size;
    }
    FreeImage_CloseMemory(mem);
    return b;
}

static unsigned pitch_of(unsigned width, unsigned bpp) { return ((width * bpp + 31) / 32) * 4; }

/* 'value' in every byte of every row, padding included */
static void fill(FIBITMAP *dib, BYTE value) {
    unsigned y;
    for (y = 0; y < FreeImage_GetHeight(dib); y++) memset(FreeImage_GetScanLine(dib, y), value, FreeImage_GetPitch(dib));
}

static FIBITMAP *allocate(unsigned width, unsigned height, unsigned bpp) {
    if (bpp == 16) return FreeImage_Allocate(width, height, 16, FI16_565_RED_MASK, FI16_565_GREEN_MASK, FI16_565_BLUE_MASK);
    return FreeImage_Allocate(width, height, bpp, 0, 0, 0);
}

/* each saved row: the dib's pixels, zeros in the unused bits of a 1- or 4-bit row's last byte, then zero padding */
static int rows_ok(const BUF *f, FIBITMAP *dib) {
    const unsigned width = FreeImage_GetWidth(dib), bpp = FreeImage_GetBPP(dib);
    const unsigned pitch = pitch_of(width, bpp), whole = width * bpp / 8, partial = (width * bpp) % 8;
    const BYTE mask = (BYTE)(0xFF << (8 - partial));
    unsigned y, i;
    if (!f->p || le32(f->p + 10) + (DWORD)pitch * FreeImage_GetHeight(dib) != f->n) return 0;
    for (y = 0; y < FreeImage_GetHeight(dib); y++) {
        const BYTE *row = f->p + le32(f->p + 10) + y * pitch, *src = FreeImage_GetScanLine(dib, y);
        if (memcmp(row, src, whole)) return 0;
        i = whole;
        if (partial) {
            if (row[i] != (src[i] & mask)) return 0;
            i++;
        }
        for (; i < pitch; i++) if (row[i]) return 0;
    }
    return 1;
}

int main(void) {
    static const unsigned bpps[] = { 1, 4, 8, 16, 24, 32 };
    unsigned i;
    FreeImage_Initialise(FALSE);

    printf("biSizeImage: the pixel data's size, except for BI_RGB, which may and should state 0\n");
    for (i = 0; i < sizeof(bpps) / sizeof(bpps[0]); i++) {
        FIBITMAP *dib = allocate(61, 23, bpps[i]);
        BUF f = save(dib);
        char line[128];
        if (bpps[i] == 16) {
            snprintf(line, sizeof line, "%u-bit, BI_BITFIELDS: biSizeImage = pitch x height", bpps[i]);
            report(line, f.p && le32(f.p + 30) == BMP_BITFIELDS && le32(f.p + 34) == pitch_of(61, 16) * 23 && le32(f.p + 34) == f.n - le32(f.p + 10));
        } else {
            snprintf(line, sizeof line, "%u-bit, BI_RGB: biSizeImage = 0", bpps[i]);
            report(line, f.p && le32(f.p + 30) == BMP_RGB && le32(f.p + 34) == 0 && le32(f.p + 2) == f.n);
        }
        free(f.p);
        FreeImage_Unload(dib);
    }
    {
        /* alpha: a BITMAPV5HEADER */
        FIBITMAP *dib = allocate(61, 23, 32);
        BUF f;
        unsigned x, y;
        for (y = 0; y < 23; y++) for (x = 0; x < 61; x++) FreeImage_GetScanLine(dib, y)[x * 4 + FI_RGBA_ALPHA] = (BYTE)(x * 4);
        f = save(dib);
        report("32-bit with alpha, BI_BITFIELDS: bV5SizeImage = pitch x height",
               f.p && le32(f.p + 14) == 124 && le32(f.p + 30) == BMP_BITFIELDS && le32(f.p + 34) == 61 * 4 * 23 && le32(f.p + 34) == f.n - le32(f.p + 10));
        free(f.p);
        FreeImage_Unload(dib);
    }

    printf("Palette entries: rgbReserved is written as 0, whatever the dib holds\n");
    for (i = 0; i < 3; i++) {
        FIBITMAP *dib = allocate(9, 3, bpps[i]);
        RGBQUAD *pal = FreeImage_GetPalette(dib);
        const unsigned colors = FreeImage_GetColorsUsed(dib);
        BUF f;
        unsigned c, nonzero = 0;
        char line[128];
        for (c = 0; c < colors; c++) pal[c].rgbReserved = 0x5A;
        f = save(dib);
        for (c = 0; f.p && c < colors; c++) if (f.p[14 + 40 + c * 4 + 3]) nonzero++;
        snprintf(line, sizeof line, "%u-bit, %u entries", bpps[i], colors);
        report(line, f.p && le32(f.p + 46) == colors && nonzero == 0);
        free(f.p);
        FreeImage_Unload(dib);
    }

    printf("Row padding: zeros, also for a view, whose parent has pixels there\n");
    for (i = 0; i < sizeof(bpps) / sizeof(bpps[0]); i++) {
        /* a 33-pixel view, 8 pixels into a parent filled with FF */
        FIBITMAP *parent = allocate(64, 3, bpps[i]);
        FIBITMAP *view;
        BUF f;
        char line[128];
        fill(parent, 0xFF);
        view = FreeImage_CreateView(parent, 8, 0, 41, 3);
        f = save(view);
        snprintf(line, sizeof line, "%u-bit view, 33 pixels: padding %u, unused bits %u", bpps[i],
                 pitch_of(33, bpps[i]) - (33 * bpps[i] + 7) / 8, (8 - (33 * bpps[i]) % 8) % 8);
        report(line, view && rows_ok(&f, view));
        free(f.p);
        if (view) FreeImage_Unload(view);
        FreeImage_Unload(parent);
    }
    for (i = 0; i < 3; i++) {
        /* a plain dib with FF in its padding, as a loader that copies whole rows leaves it */
        static const unsigned depth[] = { 1, 4, 24 };
        FIBITMAP *dib = allocate(33, 3, depth[i]);
        BUF f;
        char line[128];
        fill(dib, 0xFF);
        f = save(dib);
        snprintf(line, sizeof line, "%u-bit image, 33 pixels, FF in its padding", depth[i]);
        report(line, rows_ok(&f, dib));
        free(f.p);
        FreeImage_Unload(dib);
    }

    printf("--- %d failure(s) ---\n", failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
