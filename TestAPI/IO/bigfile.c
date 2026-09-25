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

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    FreeImage_Initialise(FALSE);
    printf("FreeImage %s, %s positions\n", FreeImage_GetVersion(), sizeof(FI_TEST_OFF_T) == 8 ? "64-bit" : "32-bit");
    test_file("fi_io_big2g.tif", (INT64)0xA0000000);      /* 2.5 GB */
    test_file("fi_io_big4g.tif", (INT64)0x120000000);     /* 4.5 GB */
    FreeImage_DeInitialise();
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
