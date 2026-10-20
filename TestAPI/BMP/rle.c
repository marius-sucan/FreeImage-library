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

/* ---------- sparse images over 64 MB ---------- */

/* a 4- or 8-bit RLE file of w x h: the RLE data, 'tail' bytes after it and, with a profile, a V5 header and the profile last */
static FIBITMAP *load_sparse(unsigned bpp, unsigned w, unsigned h, const BUF *data, const BYTE *tail, unsigned tail_n,
                             const BYTE *profile, DWORD profile_n) {
    BUF b = { NULL, 0, 0 };
    const unsigned colors = (bpp == 4) ? 16 : 256, header = profile ? 124 : 40;
    const DWORD off = 14 + header + colors * 4;
    FIMEMORY *mem;
    FIBITMAP *dib;
    unsigned i;
    put8(&b, 'B'); put8(&b, 'M'); put32(&b, (DWORD)(off + data->n + tail_n + profile_n)); put32(&b, 0); put32(&b, off);
    put32(&b, header); put32(&b, w); put32(&b, h); put16(&b, 1); put16(&b, bpp); put32(&b, (bpp == 4) ? 2 : BMP_RLE8);
    put32(&b, (DWORD)data->n); put32(&b, 2835); put32(&b, 2835); put32(&b, colors); put32(&b, 0);
    if (profile) {
        for (i = 0; i < 4; i++) put32(&b, 0);
        put32(&b, 0x4D424544);                         /* 'MBED' */
        for (i = 0; i < 12; i++) put32(&b, 0);
        put32(&b, 4); put32(&b, (DWORD)(header + colors * 4 + data->n + tail_n)); put32(&b, profile_n); put32(&b, 0);
    }
    for (i = 0; i < colors; i++) { put8(&b, i); put8(&b, i); put8(&b, i); put8(&b, 0); }
    put(&b, data->p, data->n);
    if (tail_n) put(&b, tail, tail_n);
    if (profile) put(&b, profile, profile_n);
    messages[0] = 0;
    mem = FreeImage_OpenMemory(b.p, (DWORD)b.n);
    dib = FreeImage_LoadFromMemory(FIF_BMP, mem, 0);
    FreeImage_CloseMemory(mem);
    free(b.p);
    return dib;
}

/* RLE data that leaves every row empty but the bottom and top ones. After the top row's first run, 'last' is
   0: end of bitmap, 1: end of line, 2: a move past the last row, 3: runs to the row end, then nothing;
   -1: the data stops after 'rows' rows, before the top one */
static BUF sparse(unsigned bpp, unsigned w, unsigned h, int last, unsigned rows) {
    BUF b = { NULL, 0, 0 };
    unsigned y, x;
    put8(&b, 200); put8(&b, (bpp == 4) ? 0x77 : 7); put8(&b, 0); put8(&b, 0);
    for (y = 1; y < h - 1 && y < rows; y++) { put8(&b, 0); put8(&b, 0); }
    if (last == -1) return b;
    put8(&b, 10); put8(&b, (bpp == 4) ? 0x33 : 3);
    switch (last) {
        case 0: put8(&b, 0); put8(&b, 1); break;
        case 1: put8(&b, 0); put8(&b, 0); break;
        case 2: put8(&b, 0); put8(&b, 2); put8(&b, 0); put8(&b, 255); break;
        case 3: for (x = 10; x < w; x += 255) { put8(&b, (w - x < 255) ? w - x : 255); put8(&b, (bpp == 4) ? 0x55 : 5); } break;
    }
    return b;
}

static unsigned pixel(FIBITMAP *dib, unsigned bpp, unsigned x, unsigned y) {
    const BYTE *row = FreeImage_GetScanLine(dib, y);
    return (bpp == 4) ? ((row[x / 2] >> ((x & 1) ? 0 : 4)) & 15) : row[x];
}

/* the bottom row starts with 200 pixels of 7, the top one with 10 of 3 (then 5 when filled), all else is 0; no message */
static int sparse_ok(FIBITMAP *dib, unsigned bpp, unsigned w, unsigned h, int filled) {
    unsigned x;
    if (!dib || FreeImage_GetWidth(dib) != w || FreeImage_GetHeight(dib) != h || FreeImage_GetBPP(dib) != bpp || messages[0]) return 0;
    for (x = 0; x < w; x += (x < 300) ? 1 : 997) {
        if (pixel(dib, bpp, x, 0) != ((x < 200) ? 7u : 0u)) return 0;
        if (pixel(dib, bpp, x, h - 1) != ((x < 10) ? 3u : (filled ? 5u : 0u))) return 0;
        if (pixel(dib, bpp, x, 1) != 0 || pixel(dib, bpp, x, h / 2) != 0 || pixel(dib, bpp, x, h - 2) != 0) return 0;
    }
    return pixel(dib, bpp, w - 1, h - 1) == (filled ? 5u : 0u);
}

static void expect_sparse(const char *what, unsigned bpp, unsigned w, unsigned h, int last, const BYTE *tail, unsigned tail_n,
                          const BYTE *profile, DWORD profile_n) {
    BUF data = sparse(bpp, w, h, last, h);
    FIBITMAP *dib = load_sparse(bpp, w, h, &data, tail, tail_n, profile, profile_n);
    FIICCPROFILE *icc = dib ? FreeImage_GetICCProfile(dib) : NULL;
    report(what, sparse_ok(dib, bpp, w, h, last == 3) &&
           (!profile || (icc && (DWORD)icc->size == profile_n && memcmp(icc->data, profile, profile_n) == 0)));
    if (dib) FreeImage_Unload(dib);
    free(data.p);
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

    printf("RLE images over 64 MB whose empty rows take no data: what follows the data does not count\n");
    {
        static const BYTE zeros[3] = { 0, 0, 0 }, junk[8] = { 0x5A, 1, 2, 3, 0xFF, 0, 1, 0 };
        DWORD srgb_n = 0;
        const BYTE *srgb = (const BYTE *)FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_SRGB, &srgb_n);
        expect_sparse("RLE8 60000 x 1200 ending with its end of bitmap", 8, 60000, 1200, 0, NULL, 0, NULL, 0);
        expect_sparse("  1 byte after the end of bitmap", 8, 60000, 1200, 0, zeros, 1, NULL, 0);
        expect_sparse("  3 bytes after it", 8, 60000, 1200, 0, zeros, 3, NULL, 0);
        expect_sparse("  8 bytes of junk after it", 8, 60000, 1200, 0, junk, sizeof junk, NULL, 0);
        expect_sparse("  no end of bitmap: an end of line after the last row", 8, 60000, 1200, 1, NULL, 0, NULL, 0);
        expect_sparse("  no end of bitmap: a move past the last row", 8, 60000, 1200, 2, NULL, 0, NULL, 0);
        expect_sparse("  no end of bitmap: runs fill the last row, then the file ends", 8, 60000, 1200, 3, NULL, 0, NULL, 0);
        expect_sparse("  V5 header with the ICC profile after the data: loads with it", 8, 60000, 1200, 0, NULL, 0, srgb, srgb_n);
        expect_sparse("RLE4 30000 x 4500, 1 byte after the end of bitmap", 4, 30000, 4500, 0, zeros, 1, NULL, 0);
        expect_sparse("  no end of bitmap: an end of line after the last row", 4, 30000, 4500, 1, NULL, 0, NULL, 0);
        expect_sparse("  no end of bitmap: runs fill the last row, then the file ends", 4, 30000, 4500, 3, NULL, 0, NULL, 0);
    }
    {
        /* the data stops after 600 empty rows: too little for the rows it would leave blank */
        BUF data8 = sparse(8, 60000, 1200, -1, 600), data4 = sparse(4, 30000, 4500, -1, 600);
        FIBITMAP *dib = load_sparse(8, 60000, 1200, &data8, NULL, 0, NULL, 0);
        report("RLE8 60000 x 1200 cut after 600 rows: refused before allocating", !dib && strstr(messages, "too little data") != NULL);
        if (dib) FreeImage_Unload(dib);
        dib = load_sparse(4, 30000, 4500, &data4, NULL, 0, NULL, 0);
        report("RLE4 30000 x 4500 cut after 600 rows: refused before allocating", !dib && strstr(messages, "too little data") != NULL);
        if (dib) FreeImage_Unload(dib);
        free(data8.p);
        free(data4.p);
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
