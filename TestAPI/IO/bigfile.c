/* FreeImage 3 - I/O test: images that sit beyond 2 GB and 4 GB in a file */
/* Two BigTIFFs are written sparse: 4.5 GB apparent, a few hundred KB on disk. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

#ifdef _WIN32
#define fi_fseek64(f, o, w) _fseeki64((f), (o), (w))
#define fi_ftell64(f) _ftelli64(f)
#else
#define fi_fseek64(f, o, w) fseeko((f), (off_t)(o), (w))
#define fi_ftell64(f) ((INT64)ftello(f))
#endif

/* the callbacks' position type; a build against the old header passes -DFI_TEST_OFF_T=long */
#ifndef FI_TEST_OFF_T
#define FI_TEST_OFF_T INT64
#endif

#define W 256
#define H 256

static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("IO_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

static BYTE pixel(int x, int y, int page) {
    return (BYTE)((x * 7 + y * 13 + page * 101) & 0xFF);
}

/* FreeImageIO over stdio, 64-bit positions */
static unsigned DLL_CALLCONV rd(void *b, unsigned s, unsigned c, fi_handle h) { return (unsigned)fread(b, s, c, (FILE *)h); }
static unsigned DLL_CALLCONV wr(void *b, unsigned s, unsigned c, fi_handle h) { return (unsigned)fwrite(b, s, c, (FILE *)h); }
static int DLL_CALLCONV sk(fi_handle h, FI_TEST_OFF_T off, int origin) { return fi_fseek64((FILE *)h, off, origin); }
static FI_TEST_OFF_T DLL_CALLCONV tl(fi_handle h) { return (FI_TEST_OFF_T)fi_ftell64((FILE *)h); }

/* --- a two-page 8-bit grey BigTIFF, little-endian ------------------------- */

static void put16(BYTE *p, unsigned v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }
static void put32(BYTE *p, unsigned v) { put16(p, v & 0xFFFF); put16(p + 2, v >> 16); }
static void put64(BYTE *p, UINT64 v) { put32(p, (unsigned)(v & 0xFFFFFFFFu)); put32(p + 4, (unsigned)(v >> 32)); }

/* 9 entries + count + next: 196 bytes */
#define IFD_SIZE (8 + 9 * 20 + 8)

static void make_ifd(BYTE *ifd, UINT64 strip, UINT64 next) {
    BYTE *e = ifd + 8;
    int i;
    struct { unsigned tag, type; UINT64 value; } entries[9] = {
        { 256, 4, W }, { 257, 4, H }, { 258, 3, 8 }, { 259, 3, 1 }, { 262, 3, 1 },
        { 273, 16, strip }, { 277, 3, 1 }, { 278, 4, H }, { 279, 16, (UINT64)W * H }
    };
    memset(ifd, 0, IFD_SIZE);
    put64(ifd, 9);
    for (i = 0; i < 9; i++, e += 20) {
        put16(e, entries[i].tag);
        put16(e + 2, entries[i].type);
        put64(e + 4, 1);
        put64(e + 12, entries[i].value);
    }
    put64(e, next);
}

static int write_pixels(FILE *f, int page) {
    BYTE row[W];
    int x, y;
    for (y = 0; y < H; y++) {
        for (x = 0; x < W; x++) row[x] = pixel(x, y, page);
        if (fwrite(row, 1, W, f) != W) return 0;
    }
    return 1;
}

/* page 0's strip at 'strip'; page 1's IFD and strip follow it */
static int write_bigtiff(const char *path, INT64 strip) {
    FILE *f = fopen(path, "wb");
    BYTE header[16], ifd[IFD_SIZE];
    const UINT64 ifd1 = (UINT64)strip + (UINT64)W * H;
    int ok = 0;
    if (!f) return 0;
    memset(header, 0, sizeof(header));
    header[0] = 'I'; header[1] = 'I';
    put16(header + 2, 43);
    put16(header + 4, 8);
    put64(header + 8, 16);
    make_ifd(ifd, (UINT64)strip, ifd1);
    if (fwrite(header, 1, 16, f) == 16 && fwrite(ifd, 1, IFD_SIZE, f) == IFD_SIZE &&
        fi_fseek64(f, strip, SEEK_SET) == 0 && write_pixels(f, 0)) {
        make_ifd(ifd, ifd1 + IFD_SIZE, 0);
        ok = fwrite(ifd, 1, IFD_SIZE, f) == IFD_SIZE && write_pixels(f, 1);
    }
    return (fclose(f) == 0) && ok;
}

/* --- checks --------------------------------------------------------------- */

static int failures = 0;

static void report(const char *what, int ok) {
    printf("  %-52s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

static int check(FIBITMAP *dib, int page) {
    int x, y;
    if (!dib) return 0;
    if (FreeImage_GetWidth(dib) != W || FreeImage_GetHeight(dib) != H || FreeImage_GetBPP(dib) != 8) return 0;
    for (y = 0; y < H; y++) {
        const BYTE *row = FreeImage_GetScanLine(dib, H - 1 - y);
        for (x = 0; x < W; x++) if (row[x] != pixel(x, y, page)) return 0;
    }
    return 1;
}

static void test_file(const char *name, INT64 strip) {
    FreeImageIO io = { rd, wr, sk, tl };
    const char *path = tmppath(name);
    FIBITMAP *dib;
    FIMULTIBITMAP *mb;
    FILE *f;

    printf("%s: page 0 at %.2f GB, page 1 after it\n", name, (double)strip / (1024.0 * 1024.0 * 1024.0));
    if (!write_bigtiff(path, strip)) { report("write the sparse BigTIFF", 0); return; }

    report("FreeImage_GetFileType -> TIFF", FreeImage_GetFileType(path, 0) == FIF_TIFF);

    dib = FreeImage_Load(FIF_TIFF, path, 0);
    report("FreeImage_Load, page 0 pixels", check(dib, 0));
    if (dib) FreeImage_Unload(dib);

    f = fopen(path, "rb");
    dib = f ? FreeImage_LoadFromHandle(FIF_TIFF, &io, (fi_handle)f, 0) : NULL;
    report("FreeImage_LoadFromHandle, page 0 pixels", check(dib, 0));
    if (dib) FreeImage_Unload(dib);
    if (f) fclose(f);

    mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, FALSE, TRUE, TRUE, 0);
    report("FreeImage_OpenMultiBitmap -> 2 pages", mb && FreeImage_GetPageCount(mb) == 2);
    dib = mb ? FreeImage_LockPage(mb, 1) : NULL;
    report("  page 1 pixels", check(dib, 1));
    if (dib) FreeImage_UnlockPage(mb, dib, FALSE);
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);

    f = fopen(path, "rb");
    mb = f ? FreeImage_OpenMultiBitmapFromHandle(FIF_TIFF, &io, (fi_handle)f, 0) : NULL;
    report("FreeImage_OpenMultiBitmapFromHandle -> 2 pages", mb && FreeImage_GetPageCount(mb) == 2);
    dib = mb ? FreeImage_LockPage(mb, 1) : NULL;
    report("  page 1 pixels", check(dib, 1));
    if (dib) FreeImage_UnlockPage(mb, dib, FALSE);
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
    if (f) fclose(f);

    remove(path);
}

/* --- TIFF_BIGTIFF_FORMAT: BigTIFF when asked, classic TIFF otherwise ----------- */

/* "II" then 42 for classic TIFF, 43 for BigTIFF */
static int tiff_version(const char *path) {
    BYTE h[4] = { 0 };
    FILE *f = fopen(path, "rb");
    size_t n = f ? fread(h, 1, 4, f) : 0;
    if (f) fclose(f);
    return (n == 4 && h[0] == 'I' && h[1] == 'I') ? (h[2] | (h[3] << 8)) : -1;
}

static FIBITMAP *make_page(int page) {
    int x, y;
    FIBITMAP *dib = FreeImage_Allocate(W, H, 8, 0, 0, 0);
    if (!dib) return NULL;
    for (y = 0; y < H; y++) {
        BYTE *row = FreeImage_GetScanLine(dib, H - 1 - y);
        for (x = 0; x < W; x++) row[x] = pixel(x, y, page);
    }
    return dib;
}

static void test_bigtiff_flag(void) {
    const char *path = tmppath("fi_io_flag.tif");
    FIBITMAP *pages[2] = { make_page(0), make_page(1) };
    FIBITMAP *dib;
    FIMULTIBITMAP *mb;
    int ok;

    printf("TIFF_BIGTIFF_FORMAT, %dx%d\n", W, H);
    report("FreeImage_Save, no flag: classic TIFF", FreeImage_Save(FIF_TIFF, pages[0], path, TIFF_DEFAULT) && tiff_version(path) == 42);
    report("FreeImage_Save, TIFF_BIGTIFF_FORMAT: BigTIFF", FreeImage_Save(FIF_TIFF, pages[0], path, TIFF_BIGTIFF_FORMAT) && tiff_version(path) == 43);
    dib = FreeImage_Load(FIF_TIFF, path, 0);
    report("  FreeImage_Load, pixels", check(dib, 0));
    if (dib) FreeImage_Unload(dib);
    remove(path);

    mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, TRUE, FALSE, TRUE, 0);
    ok = mb && FreeImage_AppendPageEx(mb, pages[0]) && FreeImage_AppendPageEx(mb, pages[1]);
    ok = FreeImage_CloseMultiBitmap(mb, TIFF_BIGTIFF_FORMAT | TIFF_LZW) && ok;
    report("CloseMultiBitmap, TIFF_BIGTIFF_FORMAT: BigTIFF", ok && tiff_version(path) == 43);
    mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, FALSE, TRUE, TRUE, 0);
    report("  FreeImage_OpenMultiBitmap -> 2 pages", mb && FreeImage_GetPageCount(mb) == 2);
    dib = mb ? FreeImage_LockPage(mb, 1) : NULL;
    report("  page 1 pixels", check(dib, 1));
    if (dib) FreeImage_UnlockPage(mb, dib, FALSE);
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
    remove(path);

    FreeImage_Unload(pages[0]);
    FreeImage_Unload(pages[1]);
}

/* --- a BMP that would pass 4 GB ------------------------------------------------ */

/* 65536 x 65600 8-bit, its rows 1 byte apart in a small buffer: the save must stop before it reads any */
static void test_bmp_4g(void) {
    const char *path = tmppath("fi_io_big.bmp");
    BYTE *bits = (BYTE *)calloc(65536 + 65600, 1);
    FIBITMAP *dib = bits ? FreeImage_ConvertFromRawBitsEx(FALSE, bits, FIT_BITMAP, 65536, 65600, 1, 8, 0, 0, 0, FALSE) : NULL;
    FILE *f;

    printf("fi_io_big.bmp: 65536 x 65600 8-bit, 4.3 GB as a BMP, whose size field is 32-bit\n");
    report("FreeImage_Save refuses it", dib && !FreeImage_Save(FIF_BMP, dib, path, BMP_DEFAULT));
    report("  with BMP_SAVE_RLE too", dib && !FreeImage_Save(FIF_BMP, dib, path, BMP_SAVE_RLE));
    f = fopen(path, "rb");
    report("  and leaves no file", f == NULL);
    if (f) fclose(f);
    remove(path);
    if (dib) FreeImage_Unload(dib);
    free(bits);
}

/* --- a PSD whose pixels pass 2 GB ------------------------------------------------ */

/* a FreeImageIO that keeps the first bytes written and discards the rest */
typedef struct { INT64 pos, size; BYTE head[32]; } SINK;

static unsigned DLL_CALLCONV sink_rd(void *b, unsigned s, unsigned c, fi_handle h) { return 0; }
static unsigned DLL_CALLCONV sink_wr(void *b, unsigned s, unsigned c, fi_handle h) {
    SINK *k = (SINK *)h;
    const INT64 n = (INT64)s * c;
    INT64 i;
    for (i = 0; i < n && k->pos + i < (INT64)sizeof(k->head); i++) k->head[k->pos + i] = ((BYTE *)b)[i];
    k->pos += n;
    if (k->pos > k->size) k->size = k->pos;
    return c;
}
static int DLL_CALLCONV sink_sk(fi_handle h, FI_TEST_OFF_T off, int origin) {
    SINK *k = (SINK *)h;
    const INT64 base = (origin == SEEK_SET) ? 0 : (origin == SEEK_CUR) ? k->pos : k->size;
    if (base + (INT64)off < 0) return -1;
    k->pos = base + (INT64)off;
    return 0;
}
static FI_TEST_OFF_T DLL_CALLCONV sink_tl(fi_handle h) { return (FI_TEST_OFF_T)((SINK *)h)->pos; }

/* the PSD version of a 'side' x 'side' 32-bit image, its rows 4 bytes apart in a small buffer; 0 if not saved */
static int psd_version(unsigned side, int flags) {
    FreeImageIO io = { sink_rd, sink_wr, sink_sk, sink_tl };
    SINK sink;
    BYTE *bits = (BYTE *)calloc((size_t)side * 8, 1);
    FIBITMAP *dib = bits ? FreeImage_ConvertFromRawBitsEx(FALSE, bits, FIT_BITMAP, side, side, 4, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK, FALSE) : NULL;
    int version = 0;
    memset(&sink, 0, sizeof(sink));
    if (dib && FreeImage_SaveToHandle(FIF_PSD, dib, &io, (fi_handle)&sink, flags) && !memcmp(sink.head, "8BPS", 4))
        version = (sink.head[4] << 8) | sink.head[5];
    if (dib) FreeImage_Unload(dib);
    free(bits);
    return version;
}

static void test_psd_2g(void) {
    printf("PSD of 32-bit images, into a FreeImageIO that keeps nothing: Photoshop reads PSD up to 2 GB\n");
    report("20000 x 20000, 1.6 GB of pixels: PSD", psd_version(20000, PSD_DEFAULT) == 1);
    report("24000 x 24000, 2.3 GB of pixels: PSB", psd_version(24000, PSD_DEFAULT) == 2);
    report("64 x 64 with PSD_PSB: PSB", psd_version(64, PSD_PSB) == 2);
}

/* --- sizes taken from a file 4 GB long ----------------------------------------- */

/* extend an open file to 'size' bytes: a hole, then one byte */
static int extend_sparse(FILE *f, INT64 size) {
    return fi_fseek64(f, size - 1, SEEK_SET) == 0 && fputc(0, f) != EOF;
}

/* a 4 x 1 RLE TGA with 4 GB + 1 byte after its header: the read cache is sized from what follows */
static void test_tga_rle(void) {
    const char *path = tmppath("fi_io_big.tga");
    BYTE header[18], packet[4] = { 0x83, 0x11, 0x5A, 0xA5 };	/* a run of 4 pixels, B G R */
    FIBITMAP *dib;
    FILE *f;
    int ok = 0, x;

    printf("fi_io_big.tga: a 4 x 1 RLE TGA, 4 GB of other data after its pixels\n");
    memset(header, 0, sizeof(header));
    header[2] = 10;		/* RLE true colour */
    header[12] = 4;		/* width */
    header[14] = 1;		/* height */
    header[16] = 24;
    header[17] = 0x20;	/* top-left origin */
    f = fopen(path, "wb");
    ok = f && fwrite(header, 1, sizeof(header), f) == sizeof(header) && fwrite(packet, 1, sizeof(packet), f) == sizeof(packet)
        && extend_sparse(f, (INT64)sizeof(header) + ((INT64)1 << 32) + 1);
    if (f && fclose(f) != 0) ok = 0;
    if (!ok) { report("write the sparse TGA", 0); remove(path); return; }

    dib = FreeImage_Load(FIF_TARGA, path, 0);
    ok = dib && FreeImage_GetWidth(dib) == 4 && FreeImage_GetHeight(dib) == 1 && FreeImage_GetBPP(dib) == 24;
    for (x = 0; ok && x < 4; x++) {
        const BYTE *p = FreeImage_GetScanLine(dib, 0) + x * 3;
        ok = p[0] == 0x11 && p[1] == 0x5A && p[2] == 0xA5;
    }
    report("FreeImage_Load, every pixel", ok);
    if (dib) FreeImage_Unload(dib);
    remove(path);
}

/* --- an SGI RLE row 3 GB into the file ---------------------------------------- */

static void put32be(BYTE *p, unsigned v) { p[0] = (BYTE)(v >> 24); p[1] = (BYTE)(v >> 16); p[2] = (BYTE)(v >> 8); p[3] = (BYTE)v; }

/* 16 x 2 grey; after row 0, 1 MB of other runs for a failed seek to land in (glibc keeps its buffer, msvcrt drops it) */
static void test_sgi_rle(void) {
    const char *path = tmppath("fi_io_big.sgi");
    const INT64 row1_at = (INT64)3 << 30;
    BYTE header[512], table[16], row0[18], row1[3] = { 16, 0xAB, 0 }, decoy[2] = { 16, 0xCD };
    FIBITMAP *dib;
    FILE *f;
    int ok, x, i;

    printf("fi_io_big.sgi: a 16 x 2 RLE SGI whose second row is 3 GB into the file\n");
    memset(header, 0, sizeof(header));
    header[0] = 0x01; header[1] = 0xDA;		/* magic 474 */
    header[2] = 1;							/* RLE */
    header[3] = 1;							/* 1 byte per sample */
    header[5] = 2;							/* one channel */
    header[7] = 16;							/* width */
    header[9] = 2;							/* height */
    header[11] = 1;
    put32be(header + 16, 255);
    put32be(table, 512 + 16);
    put32be(table + 4, (unsigned)row1_at);
    put32be(table + 8, sizeof(row0));
    put32be(table + 12, sizeof(row1));
    row0[0] = 0x80 | 16;
    for (x = 0; x < 16; x++) row0[1 + x] = (BYTE)(x * 16 + 1);
    row0[17] = 0;
    f = fopen(path, "wb");
    ok = f && fwrite(header, 1, sizeof(header), f) == sizeof(header) && fwrite(table, 1, sizeof(table), f) == sizeof(table)
        && fwrite(row0, 1, sizeof(row0), f) == sizeof(row0);
    for (i = 0; ok && i < (1 << 19); i++) ok = fwrite(decoy, 1, sizeof(decoy), f) == sizeof(decoy);
    ok = ok && fi_fseek64(f, row1_at, SEEK_SET) == 0 && fwrite(row1, 1, sizeof(row1), f) == sizeof(row1);
    if (f && fclose(f) != 0) ok = 0;
    if (!ok) { report("write the sparse SGI", 0); remove(path); return; }

    dib = FreeImage_Load(FIF_SGI, path, 0);
    ok = dib && FreeImage_GetWidth(dib) == 16 && FreeImage_GetHeight(dib) == 2;
    for (x = 0; ok && x < 16; x++) {
        ok = FreeImage_GetScanLine(dib, 0)[x] == (BYTE)(x * 16 + 1) && FreeImage_GetScanLine(dib, 1)[x] == 0xAB;
    }
    report("FreeImage_Load, both rows", ok);
    if (dib) FreeImage_Unload(dib);
    remove(path);
}

/* stdio reads that note the largest request and refuse, writing nothing, one over 1 MB */
static size_t largest_read = 0;
static unsigned DLL_CALLCONV rd_bounded(void *b, unsigned s, unsigned c, fi_handle h) {
    const size_t n = (size_t)s * c;
    if (n > largest_read) largest_read = n;
    return (n > ((size_t)1 << 20)) ? 0 : (unsigned)fread(b, s, c, (FILE *)h);
}

/* a WebP and 4 GB - 1 bytes in all: a 32-bit size_t cannot hold the stream and its spare byte */
static void test_webp_4g(void) {
    const char *path = tmppath("fi_io_big.webp");
    FIBITMAP *dib = FreeImage_Allocate(16, 16, 24, 0, 0, 0);
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    BYTE *bytes = NULL;
    DWORD size = 0;
    FILE *f;
    int ok;

    printf("fi_io_big.webp: a WebP, then other data up to 4 GB - 1 bytes\n");
    if (sizeof(size_t) > 4) {
        printf("  (32-bit builds only)\n");
    } else {
        ok = FreeImage_SaveToMemory(FIF_WEBP, dib, mem, WEBP_LOSSLESS) && FreeImage_AcquireMemory(mem, &bytes, &size);
        f = ok ? fopen(path, "wb") : NULL;
        ok = f && fwrite(bytes, 1, size, f) == size && extend_sparse(f, (INT64)0xFFFFFFFFu);
        if (f && fclose(f) != 0) ok = 0;
        if (!ok) {
            report("write the sparse WebP", 0);
        } else {
            /* the old code allocated 0 bytes and read the 4 GB into them */
            FreeImageIO io = { rd_bounded, wr, sk, tl };
            FIBITMAP *d = NULL;
            f = fopen(path, "rb");
            largest_read = 0;
            if (f) { d = FreeImage_LoadFromHandle(FIF_WEBP, &io, (fi_handle)f, 0); fclose(f); }
            report("FreeImage_LoadFromHandle refuses it before reading it", f && d == NULL && largest_read <= ((size_t)1 << 20));
            if (d) FreeImage_Unload(d);
        }
        remove(path);
    }
    FreeImage_CloseMemory(mem);
    FreeImage_Unload(dib);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    FreeImage_Initialise(FALSE);
    printf("FreeImage %s, %s positions\n", FreeImage_GetVersion(), sizeof(FI_TEST_OFF_T) == 8 ? "64-bit" : "32-bit");
    test_file("fi_io_big2g.tif", (INT64)0xA0000000);      /* 2.5 GB */
    test_file("fi_io_big4g.tif", (INT64)0x120000000);     /* 4.5 GB */
    test_bigtiff_flag();
    test_bmp_4g();
    test_psd_2g();
    test_tga_rle();
    test_sgi_rle();
    test_webp_4g();
    FreeImage_DeInitialise();
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
