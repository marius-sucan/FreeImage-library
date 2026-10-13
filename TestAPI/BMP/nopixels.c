/* FreeImage 3 - BMP test: a header-only load (FIF_LOAD_NOPIXELS) describes what the full load returns */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

#define W 8
#define H 4

static int failures = 0;

static void report(const char *what, int ok) {
    printf("  %-64s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

typedef struct { BYTE *p; unsigned n; } BUF;

static void put(BUF *b, const void *data, unsigned n) {
    b->p = (BYTE *)realloc(b->p, b->n + n);
    memcpy(b->p + b->n, data, n);
    b->n += n;
}
static void put8(BUF *b, unsigned v) { BYTE c = (BYTE)v; put(b, &c, 1); }
static void put16(BUF *b, unsigned v) { put8(b, v & 255); put8(b, (v >> 8) & 255); }
static void put32(BUF *b, DWORD v) { put16(b, v & 0xFFFF); put16(b, (v >> 16) & 0xFFFF); }

/* a W x H file: the header, masks after a 40-byte one, a palette up to 8 bits, raw pixels, an RLE end of bitmap */
static BUF build(unsigned header, unsigned bpp, unsigned compression) {
    BUF info = { NULL, 0 }, b = { NULL, 0 };
    const unsigned colors = (bpp <= 8) ? (1u << bpp) : 0, pitch = ((W * bpp + 31) / 32) * 4;
    unsigned i;
    if (header == 12) {
        put32(&info, 12); put16(&info, W); put16(&info, H); put16(&info, 1); put16(&info, bpp);
        for (i = 0; i < colors; i++) { put8(&info, i); put8(&info, i); put8(&info, i); }
    } else {
        put32(&info, header); put32(&info, W); put32(&info, H); put16(&info, 1); put16(&info, bpp); put32(&info, compression);
        put32(&info, 0); put32(&info, 2835); put32(&info, 2835); put32(&info, colors); put32(&info, 0);
        while (info.n < header) put8(&info, 0);
        if (header == 40 && (compression == 3 || compression == 6)) {
            put32(&info, 0xF800); put32(&info, 0x07E0); put32(&info, 0x001F);
            if (compression == 6) put32(&info, 0);
        }
        for (i = 0; i < colors; i++) put32(&info, i * 0x010101);
    }
    put8(&b, 'B'); put8(&b, 'M'); put32(&b, 14 + info.n + pitch * H + 2); put32(&b, 0); put32(&b, 14 + info.n);
    put(&b, info.p, info.n);
    for (i = 0; i < pitch * H; i++) put8(&b, (compression == 1 || compression == 2) ? 0 : i);
    put8(&b, 0); put8(&b, 1);
    free(info.p);
    return b;
}

static FIBITMAP *load(const BUF *b, int flags) {
    FIMEMORY *mem = FreeImage_OpenMemory(b->p, b->n);
    FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_BMP, mem, flags);
    FreeImage_CloseMemory(mem);
    return dib;
}

/* both refused, or the same size, depth, palette and masks */
static int agree(FIBITMAP *hdr, FIBITMAP *full) {
    if (!hdr || !full) return !hdr && !full;
    return !FreeImage_HasPixels(hdr) && FreeImage_GetWidth(hdr) == FreeImage_GetWidth(full) &&
           FreeImage_GetHeight(hdr) == FreeImage_GetHeight(full) && FreeImage_GetBPP(hdr) == FreeImage_GetBPP(full) &&
           FreeImage_GetColorsUsed(hdr) == FreeImage_GetColorsUsed(full) && FreeImage_GetRedMask(hdr) == FreeImage_GetRedMask(full) &&
           FreeImage_GetGreenMask(hdr) == FreeImage_GetGreenMask(full) && FreeImage_GetBlueMask(hdr) == FreeImage_GetBlueMask(full);
}

static void DLL_CALLCONV quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

int main(void) {
    static const unsigned headers[] = { 12, 40, 56, 64, 124 };
    static const unsigned bpps[] = { 1, 2, 4, 8, 16, 24, 32, 64 };
    static const unsigned compressions[] = { 0, 1, 2, 3, 4, 5, 6, 11, 12, 13 };
    unsigned h, d, c;
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);

    printf("Every bit depth with every compression: header-only and full loads agree\n");
    for (h = 0; h < sizeof(headers) / sizeof(headers[0]); h++) {
        unsigned cases = 0, agreed = 0, loaded = 0;
        char line[128];
        for (d = 0; d < sizeof(bpps) / sizeof(bpps[0]); d++) {
            for (c = 0; c < sizeof(compressions) / sizeof(compressions[0]); c++) {
                BUF b;
                FIBITMAP *hdr, *full;
                if (headers[h] == 12 && c > 0) break;   /* no compression field */
                b = build(headers[h], bpps[d], compressions[c]);
                hdr = load(&b, FIF_LOAD_NOPIXELS);
                full = load(&b, 0);
                cases++;
                if (agree(hdr, full)) agreed++;
                if (full) loaded++;
                if (hdr) FreeImage_Unload(hdr);
                if (full) FreeImage_Unload(full);
                free(b.p);
            }
        }
        snprintf(line, sizeof line, "%u-byte header: %u of %u agree (%u load)", headers[h], agreed, cases, loaded);
        report(line, agreed == cases);
    }

    printf("Compressions a depth cannot use: refused, header-only too\n");
    {
        static const struct { unsigned bpp, compression; const char *what; } bad[] = {
            { 1, 1, "1-bit RLE8" }, { 1, 2, "1-bit RLE4" }, { 1, 3, "1-bit compression 3 (OS/2 Huffman 1D)" },
            { 4, 1, "4-bit RLE8" }, { 4, 3, "4-bit compression 3" }, { 8, 2, "8-bit RLE4" }, { 8, 3, "8-bit compression 3" },
            { 16, 1, "16-bit RLE8" }, { 16, 2, "16-bit RLE4" }, { 24, 1, "24-bit RLE8" }, { 24, 2, "24-bit RLE4" },
            { 32, 1, "32-bit RLE8" }, { 32, 2, "32-bit RLE4" },
        };
        unsigned i;
        for (i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
            BUF win = build(40, bad[i].bpp, bad[i].compression), os2 = build(64, bad[i].bpp, bad[i].compression);
            FIBITMAP *a = load(&win, FIF_LOAD_NOPIXELS), *b = load(&os2, FIF_LOAD_NOPIXELS);
            char line[128];
            snprintf(line, sizeof line, "%s, 40- and 64-byte headers", bad[i].what);
            report(line, !a && !b);
            if (a) FreeImage_Unload(a);
            if (b) FreeImage_Unload(b);
            free(win.p);
            free(os2.p);
        }
    }

    {
        /* masks at 24 bits are off-spec, yet GIMP 2.10.22 wrote them; OS/2's compression 3 is Huffman 1D */
        BUF win = build(40, 24, 3), os2 = build(64, 24, 3);
        FIBITMAP *a = load(&win, FIF_LOAD_NOPIXELS), *b = load(&win, 0), *c = load(&os2, FIF_LOAD_NOPIXELS), *d = load(&os2, 0);
        report("24-bit compression 3: loads under a 40-byte header", a && b && FreeImage_GetBPP(a) == 24 && FreeImage_GetBPP(b) == 24);
        report("  refused under an OS/2 2.x one", !c && !d);
        if (a) FreeImage_Unload(a);
        if (b) FreeImage_Unload(b);
        if (c) FreeImage_Unload(c);
        if (d) FreeImage_Unload(d);
        free(win.p);
        free(os2.p);
    }

    printf("--- %d failure(s) ---\n", failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
