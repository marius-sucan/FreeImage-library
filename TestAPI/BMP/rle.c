/* FreeImage 3 - BMP test: RLE8 runs past the row end, long moves, and the sizes an RLE8 save states */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

#define BMP_RLE8 1

static int failures = 0;
static char messages[1024];

static void report(const char *what, int ok) {
    printf("  %-64s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

static void DLL_CALLCONV keep(FREE_IMAGE_FORMAT fif, const char *msg) {
    (void)fif;
    if (strlen(messages) + strlen(msg) + 4 < sizeof(messages)) {
        strcat(messages, msg);
        strcat(messages, " | ");
    }
}

/* ---------- writing a BMP ---------- */

typedef struct { BYTE *p; size_t n, cap; } BUF;

static void put(BUF *b, const void *data, size_t n) {
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2;
        b->p = (BYTE *)realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, data, n);
    b->n += n;
}
static void put8(BUF *b, unsigned v) { BYTE c = (BYTE)v; put(b, &c, 1); }
static void put16(BUF *b, unsigned v) { put8(b, v & 255); put8(b, (v >> 8) & 255); }
static void put32(BUF *b, DWORD v) { put16(b, v & 0xFFFF); put16(b, (v >> 16) & 0xFFFF); }

/* an 8-bit BI_RLE8 file of w x h with a grey palette, holding the given RLE data */
static FIBITMAP *load_rle(unsigned w, unsigned h, const BYTE *data, size_t n) {
    BUF b = { NULL, 0, 0 };
    FIMEMORY *mem;
    FIBITMAP *dib;
    unsigned i;
    put8(&b, 'B'); put8(&b, 'M'); put32(&b, (DWORD)(14 + 40 + 1024 + n)); put32(&b, 0); put32(&b, 14 + 40 + 1024);
    put32(&b, 40); put32(&b, w); put32(&b, h); put16(&b, 1); put16(&b, 8); put32(&b, BMP_RLE8);
    put32(&b, (DWORD)n); put32(&b, 2835); put32(&b, 2835); put32(&b, 256); put32(&b, 0);
    for (i = 0; i < 256; i++) { put8(&b, i); put8(&b, i); put8(&b, i); put8(&b, 0); }
    put(&b, data, n);
    messages[0] = 0;
    mem = FreeImage_OpenMemory(b.p, (DWORD)b.n);
    dib = FreeImage_LoadFromMemory(FIF_BMP, mem, 0);
    FreeImage_CloseMemory(mem);
    free(b.p);
    return dib;
}

/* row y, counted from the bottom like the file's rows, holds these indexes */
static int row_is(FIBITMAP *dib, unsigned y, const BYTE *want) {
    return dib && memcmp(FreeImage_GetScanLine(dib, y), want, FreeImage_GetWidth(dib)) == 0;
}

/* ---------- reading a save ---------- */

static DWORD le32(const BYTE *p) { return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24); }

static BUF save(FIBITMAP *dib, int flags) {
    BUF b = { NULL, 0, 0 };
    BYTE *bytes = NULL;
    DWORD size = 0;
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    if (FreeImage_SaveToMemory(FIF_BMP, dib, mem, flags) && FreeImage_AcquireMemory(mem, &bytes, &size)) put(&b, bytes, size);
    FreeImage_CloseMemory(mem);
    return b;
}

/* bfSize is the file's size, biSizeImage the RLE data's */
static int rle_sizes_ok(const BUF *f) {
    return f->n > 54 && le32(f->p + 30) == BMP_RLE8 && le32(f->p + 2) == f->n && le32(f->p + 34) == f->n - le32(f->p + 10);
}

/* each row's runs fill exactly w pixels and end the line; then the bitmap ends, where the file does */
static int rle_stream_ok(const BUF *f, unsigned w, unsigned h) {
    size_t p = le32(f->p + 10);
    unsigned y;
    for (y = 0; y < h; y++) {
        unsigned x = 0;
        for (;;) {
            if (p + 2 > f->n) return 0;
            if (f->p[p]) {
                x += f->p[p];
                p += 2;
            } else if (f->p[p + 1] == 0) {
                p += 2;
                break;
            } else if (f->p[p + 1] < 3) {
                return 0;   /* no end of bitmap or move inside the rows */
            } else {
                const unsigned n = f->p[p + 1];
                x += n;
                p += 2 + n + (n & 1);
            }
            if (x > w) return 0;
        }
        if (x != w) return 0;
    }
    return p + 2 == f->n && f->p[p] == 0 && f->p[p + 1] == 1;
}

static int same_pixels(FIBITMAP *a, FIBITMAP *b) {
    unsigned y;
    if (!a || !b || FreeImage_GetWidth(a) != FreeImage_GetWidth(b) || FreeImage_GetHeight(a) != FreeImage_GetHeight(b) ||
        FreeImage_GetBPP(a) != FreeImage_GetBPP(b)) return 0;
    for (y = 0; y < FreeImage_GetHeight(a); y++) {
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), FreeImage_GetLine(a))) return 0;
    }
    return 1;
}

/* an RLE8 save: its sizes, its stream, and the image it loads back */
static void expect_rle_save(const char *what, FIBITMAP *src) {
    BUF f = save(src, BMP_SAVE_RLE);
    FIMEMORY *mem = f.p ? FreeImage_OpenMemory(f.p, (DWORD)f.n) : NULL;
    FIBITMAP *back = mem ? FreeImage_LoadFromMemory(FIF_BMP, mem, 0) : NULL;
    char line[128];
    snprintf(line, sizeof line, "%s: %u bytes", what, (unsigned)f.n);
    report(line, f.p && rle_sizes_ok(&f) && rle_stream_ok(&f, FreeImage_GetWidth(src), FreeImage_GetHeight(src)) && same_pixels(src, back));
    if (back) FreeImage_Unload(back);
    if (mem) FreeImage_CloseMemory(mem);
    free(f.p);
}

int main(void) {
    static const BYTE top8[] = { 2, 2, 2, 2, 2, 2, 2, 2 };
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(keep);

    printf("RLE8 literal runs that pass the row end: kept to the row, the stream read on past them\n");
    {
        /* 8 x 2: bottom row 5 x 1, then a literal of 6 whose last 3 pass the row end; top row 8 x 2 */
        static const BYTE data[] = { 5, 1, 0, 6, 3, 4, 5, 6, 7, 8, 0, 0, 8, 2, 0, 0, 0, 1 };
        static const BYTE bottom[] = { 1, 1, 1, 1, 1, 3, 4, 5 };
        FIBITMAP *dib = load_rle(8, 2, data, sizeof data);
        report("a literal of 6, 3 past the row end", row_is(dib, 0, bottom) && row_is(dib, 1, top8) && !messages[0]);
        if (dib) FreeImage_Unload(dib);
    }
    {
        /* the same with a literal of 5, 2 past the end, and its pad byte */
        static const BYTE data[] = { 5, 1, 0, 5, 3, 4, 5, 6, 7, 0, 0, 0, 8, 2, 0, 0, 0, 1 };
        static const BYTE bottom[] = { 1, 1, 1, 1, 1, 3, 4, 5 };
        FIBITMAP *dib = load_rle(8, 2, data, sizeof data);
        report("a literal of 5, 2 past the row end, and its pad byte", row_is(dib, 0, bottom) && row_is(dib, 1, top8) && !messages[0]);
        if (dib) FreeImage_Unload(dib);
    }
    {
        /* a literal that starts where the row ends */
        static const BYTE data[] = { 8, 1, 0, 4, 9, 9, 9, 9, 0, 0, 8, 2, 0, 0, 0, 1 };
        static const BYTE bottom[] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        FIBITMAP *dib = load_rle(8, 2, data, sizeof data);
        report("a literal of 4 starting at the row end", row_is(dib, 0, bottom) && row_is(dib, 1, top8) && !messages[0]);
        if (dib) FreeImage_Unload(dib);
    }
    {
        /* the file ends among the bytes past the row end: the full row is kept */
        static const BYTE data[] = { 5, 1, 0, 6, 3, 4, 5, 6 };
        static const BYTE bottom[] = { 1, 1, 1, 1, 1, 3, 4, 5 }, blank[] = { 0, 0, 0, 0, 0, 0, 0, 0 };
        FIBITMAP *dib = load_rle(8, 2, data, sizeof data);
        report("a file cut past the row end: the full row kept, with a warning",
               row_is(dib, 0, bottom) && row_is(dib, 1, blank) && strstr(messages, "1 of 2 rows") != NULL);
        if (dib) FreeImage_Unload(dib);
    }

    printf("RLE8 moves (deltas)\n");
    {
        /* a move past the row end, then end of line: the next row decodes */
        static const BYTE data[] = { 8, 1, 0, 2, 5, 0, 0, 0, 8, 2, 0, 0, 0, 1 };
        static const BYTE bottom[] = { 1, 1, 1, 1, 1, 1, 1, 1 };
        FIBITMAP *dib = load_rle(8, 2, data, sizeof data);
        report("past the row end, then end of line", row_is(dib, 0, bottom) && row_is(dib, 1, top8) && !messages[0]);
        if (dib) FreeImage_Unload(dib);
    }
    {
        /* 16 x 2: a full bottom row; on the top row, 8.4 million moves of 255 pixels right (34 MB), then a run */
        const long moves = 8421506L;
        BUF data = { NULL, 0, 0 };
        static const BYTE bottom[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, blank[16] = { 0 };
        static const BYTE move[] = { 0, 2, 255, 0 };
        FIBITMAP *dib;
        long i;
        put8(&data, 16); put8(&data, 1); put8(&data, 0); put8(&data, 0);
        for (i = 0; i < moves; i++) put(&data, move, sizeof move);
        put8(&data, 200); put8(&data, 5); put8(&data, 0); put8(&data, 0); put8(&data, 0); put8(&data, 1);
        dib = load_rle(16, 2, data.p, data.n);
        report("8.4 million moves right: past the row end, not 2^31 pixels on",
               row_is(dib, 0, bottom) && row_is(dib, 1, blank) && strstr(messages, "1 of 2 rows") != NULL);
        if (dib) FreeImage_Unload(dib);
        free(data.p);
    }

    printf("RLE8 saves: bfSize the file's size, biSizeImage the RLE data's\n");
    {
        FIBITMAP *bands = FreeImage_Allocate(61, 23, 8, 0, 0, 0);
        FIBITMAP *noise = FreeImage_Allocate(61, 23, 8, 0, 0, 0);
        FIBITMAP *wide = FreeImage_Allocate(90, 30, 8, 0, 0, 0);
        FIBITMAP *view;
        unsigned x, y;
        for (y = 0; y < 23; y++) {
            for (x = 0; x < 61; x++) {
                FreeImage_GetScanLine(bands, y)[x] = (BYTE)((x / 8 + y) & 7);
                FreeImage_GetScanLine(noise, y)[x] = (BYTE)(x * 2 + y * 3);    /* no two neighbours equal */
            }
        }
        for (y = 0; y < 30; y++) {
            for (x = 0; x < 90; x++) FreeImage_GetScanLine(wide, y)[x] = (BYTE)((x % 7 == 0) ? x : y / 3);
        }
        view = FreeImage_CreateView(wide, 13, 4, 74, 27);
        expect_rle_save("61 x 23 in runs of 8", bands);
        expect_rle_save("61 x 23 without runs, larger than uncompressed", noise);
        expect_rle_save("a 61 x 23 view", view);
        FreeImage_Unload(view);
        FreeImage_Unload(wide);
        FreeImage_Unload(noise);
        FreeImage_Unload(bands);
    }

    printf("--- %d failure(s) ---\n", failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
