/* FreeImage 3 - I/O test: images that do not start at byte 0 of their stream, and offsets a format keeps in 32 bits */
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

#define JUNK 777

static int failures = 0;
static char last_message[512];

static void report(const char *what, int ok) {
    printf("  %-60s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

static void message(FREE_IMAGE_FORMAT fif, const char *msg) {
    (void)fif;
    snprintf(last_message, sizeof(last_message), "%s", msg);
}

/* scratch file 'name' in $IO_TEST_TMP or the current directory, into buf */
static const char *tmppath(char *buf, size_t size, const char *name) {
    const char *dir = getenv("IO_TEST_TMP");
    snprintf(buf, size, "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

/* FreeImageIO over stdio */
static unsigned DLL_CALLCONV rd(void *b, unsigned s, unsigned c, fi_handle h) { return (unsigned)fread(b, s, c, (FILE *)h); }
static unsigned DLL_CALLCONV wr(void *b, unsigned s, unsigned c, fi_handle h) { return (unsigned)fwrite(b, s, c, (FILE *)h); }
static int DLL_CALLCONV sk(fi_handle h, INT64 off, int origin) { return fi_fseek64((FILE *)h, off, origin); }
static INT64 DLL_CALLCONV tl(fi_handle h) { return (INT64)fi_ftell64((FILE *)h); }

static FIBITMAP *synth(int bpp, int w, int h, int seed) {
    FIBITMAP *d = FreeImage_Allocate(w, h, bpp, 0, 0, 0);
    int x, y;
    if (!d) return NULL;
    if (bpp == 8) {
        RGBQUAD *pal = FreeImage_GetPalette(d);
        for (x = 0; x < 256; x++) { pal[x].rgbRed = (BYTE)x; pal[x].rgbGreen = (BYTE)(x * 3 + seed); pal[x].rgbBlue = (BYTE)(255 - x); }
    }
    for (y = 0; y < h; y++) {
        BYTE *row = FreeImage_GetScanLine(d, y);
        for (x = 0; x < (int)FreeImage_GetLine(d); x++) row[x] = (BYTE)(x * 7 + y * 13 + seed * 29);
    }
    return d;
}

static int same_pixels(FIBITMAP *a, FIBITMAP *b) {
    unsigned y;
    if (!a || !b) return 0;
    if (FreeImage_GetWidth(a) != FreeImage_GetWidth(b) || FreeImage_GetHeight(a) != FreeImage_GetHeight(b) || FreeImage_GetBPP(a) != FreeImage_GetBPP(b)) return 0;
    for (y = 0; y < FreeImage_GetHeight(a); y++) {
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), FreeImage_GetLine(a)) != 0) return 0;
    }
    return 1;
}

/* a file of JUNK bytes, open for update and positioned after them */
static FILE *junk_file(const char *path) {
    FILE *f = fopen(path, "w+b");
    int i;
    if (!f) return NULL;
    for (i = 0; i < JUNK; i++) fputc((i * 7) & 0xFF, f);
    return f;
}

/* --- ICO: the directory's offsets count from where the ICO starts ------------ */

static void test_ico(void) {
    FreeImageIO io = { rd, wr, sk, tl };
    char path[1024], mp_path[1024];
    FIBITMAP *small = synth(24, 48, 48, 1), *big = synth(32, 256, 256, 2), *d;
    FIMULTIBITMAP *mb;
    FILE *f;

    printf("ICO saved behind %d bytes of other data\n", JUNK);
    tmppath(path, sizeof(path), "fi_io_streams.ico");
    tmppath(mp_path, sizeof(mp_path), "fi_io_streams_mp.ico");

    f = junk_file(path);
    report("FreeImage_SaveToHandle", f && FreeImage_SaveToHandle(FIF_ICO, small, &io, (fi_handle)f, 0));
    if (f) {
        fi_fseek64(f, JUNK, SEEK_SET);
        d = FreeImage_LoadFromHandle(FIF_ICO, &io, (fi_handle)f, 0);
        report("  reloaded from there: pixels", same_pixels(d, small));
        if (d) FreeImage_Unload(d);
        fclose(f);
    }

    /* two pages: saving page 1 reads page 0 back from the stream being written */
    remove(mp_path);
    mb = FreeImage_OpenMultiBitmap(FIF_ICO, mp_path, TRUE, FALSE, TRUE, 0);
    if (mb) { FreeImage_AppendPage(mb, small); FreeImage_AppendPage(mb, big); }
    f = junk_file(path);
    report("FreeImage_SaveMultiBitmapToHandle, 2 pages", mb && f && FreeImage_SaveMultiBitmapToHandle(FIF_ICO, mb, &io, (fi_handle)f, 0));
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
    remove(mp_path);
    if (f) {
        FIMULTIBITMAP *in;
        fi_fseek64(f, JUNK, SEEK_SET);
        in = FreeImage_OpenMultiBitmapFromHandle(FIF_ICO, &io, (fi_handle)f, 0);
        report("  reopened from there: 2 pages", in && FreeImage_GetPageCount(in) == 2);
        d = in ? FreeImage_LockPage(in, 0) : NULL;
        report("  page 0 pixels", same_pixels(d, small));
        if (d) FreeImage_UnlockPage(in, d, FALSE);
        d = in ? FreeImage_LockPage(in, 1) : NULL;
        report("  page 1 (a PNG icon) pixels", same_pixels(d, big));
        if (d) FreeImage_UnlockPage(in, d, FALSE);
        if (in) FreeImage_CloseMultiBitmap(in, 0);
        fclose(f);
    }
    remove(path);
    FreeImage_Unload(small);
    FreeImage_Unload(big);
}

/* --- GIF: the logical screen follows the header, wherever it is --------------- */

static int animation_size(FIBITMAP *dib, WORD *w, WORD *h) {
    FITAG *tag = NULL;
    *w = *h = 0;
    if (FreeImage_GetMetadata(FIMD_ANIMATION, dib, "LogicalWidth", &tag) && tag) *w = *(WORD *)FreeImage_GetTagValue(tag);
    if (FreeImage_GetMetadata(FIMD_ANIMATION, dib, "LogicalHeight", &tag) && tag) *h = *(WORD *)FreeImage_GetTagValue(tag);
    return *w && *h;
}

static void test_gif(void) {
    FreeImageIO io = { rd, wr, sk, tl };
    char plain[1024], path[1024];
    FIBITMAP *f0 = synth(8, 40, 30, 3), *f1 = synth(8, 60, 50, 4), *d;
    FIMULTIBITMAP *mb;
    FILE *in, *f;
    BYTE buf[4096];
    size_t n;
    WORD w, h;

    printf("GIF read behind %d bytes of other data\n", JUNK);
    tmppath(plain, sizeof(plain), "fi_io_streams_plain.gif");
    tmppath(path, sizeof(path), "fi_io_streams.gif");
    remove(plain);
    mb = FreeImage_OpenMultiBitmap(FIF_GIF, plain, TRUE, FALSE, TRUE, 0);
    if (mb) { FreeImage_AppendPage(mb, f0); FreeImage_AppendPage(mb, f1); FreeImage_CloseMultiBitmap(mb, 0); }

    f = junk_file(path);
    in = fopen(plain, "rb");
    if (!f || !in) { report("write the GIF", 0); if (f) fclose(f); if (in) fclose(in); return; }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, f);
    fclose(in);

    fi_fseek64(f, JUNK, SEEK_SET);
    d = FreeImage_LoadFromHandle(FIF_GIF, &io, (fi_handle)f, 0);
    report("page 0: the logical screen is 60 x 50", d && animation_size(d, &w, &h) && w == 60 && h == 50);
    if (d) FreeImage_Unload(d);

    fi_fseek64(f, JUNK, SEEK_SET);
    d = FreeImage_LoadFromHandle(FIF_GIF, &io, (fi_handle)f, GIF_PLAYBACK);
    report("page 0 with GIF_PLAYBACK: a 60 x 50 canvas", d && FreeImage_GetWidth(d) == 60 && FreeImage_GetHeight(d) == 50);
    if (d) FreeImage_Unload(d);

    fclose(f);
    remove(path);
    remove(plain);
    FreeImage_Unload(f0);
    FreeImage_Unload(f1);
}

/* --- TGA: a thumbnail must fit the bytes it is read from ---------------------- */

static unsigned get32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static void put32(BYTE *p, unsigned v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); p[2] = (BYTE)(v >> 16); p[3] = (BYTE)(v >> 24); }

static void test_tga_thumbnail(void) {
    FIBITMAP *dib = synth(24, 64, 48, 5), *th = synth(24, 20, 13, 6), *d;
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0), *in;
    BYTE *bytes = NULL, *copy;
    DWORD size = 0;

    printf("TGA thumbnails\n");
    FreeImage_SetThumbnail(dib, th);
    if (!FreeImage_SaveToMemory(FIF_TARGA, dib, mem, 0) || !FreeImage_AcquireMemory(mem, &bytes, &size) || size < 26 + 495) {
        report("save a TGA with a thumbnail", 0);
        return;
    }
    copy = (BYTE *)malloc(size);
    memcpy(copy, bytes, size);

    in = FreeImage_OpenMemory(copy, size);
    d = FreeImage_LoadFromMemory(FIF_TARGA, in, 0);
    report("a saved thumbnail loads back", d && same_pixels(FreeImage_GetThumbnail(d), th));
    if (d) FreeImage_Unload(d);
    FreeImage_CloseMemory(in);

    {
        /* the postage stamp 4 bytes before the footer: 200 x 200 pixels claimed, 2 bytes there */
        const unsigned footer = size - 26, ext = get32(copy + footer), stamp = footer - 4;
        put32(copy + ext + 486, stamp);
        copy[stamp] = 200;
        copy[stamp + 1] = 200;
        in = FreeImage_OpenMemory(copy, size);
        d = FreeImage_LoadFromMemory(FIF_TARGA, in, 0);
        report("a thumbnail the file cannot hold is dropped", d && !FreeImage_GetThumbnail(d));
        report("  the image itself still loads", d && same_pixels(d, (FIBITMAP *)dib));
        if (d) FreeImage_Unload(d);
        FreeImage_CloseMemory(in);
    }

    free(copy);
    FreeImage_CloseMemory(mem);
    FreeImage_Unload(th);
    FreeImage_Unload(dib);
}

/* --- TGA: a thumbnail whose offset does not fit 32 bits is left out ------------ */

/* a memory stream whose positions past SHIFT_FROM read SHIFT more, as if the pixels ended past 4 GB;
   a write that lands far away is counted and dropped */
#define SHIFT_FROM 18
#define SHIFT ((INT64)5 << 30)
#define CAP (1 << 20)

typedef struct { BYTE p[CAP]; INT64 size, pos; int far_writes; } shift_io;

static unsigned DLL_CALLCONV s_rd(void *b, unsigned s, unsigned c, fi_handle h) {
    shift_io *m = (shift_io *)h; INT64 n = (INT64)s * c;
    if (m->pos + n > m->size) return 0;
    memcpy(b, m->p + m->pos, (size_t)n); m->pos += n; return c;
}
static unsigned DLL_CALLCONV s_wr(void *b, unsigned s, unsigned c, fi_handle h) {
    shift_io *m = (shift_io *)h; INT64 n = (INT64)s * c;
    if (m->pos + n > CAP) { m->far_writes++; m->pos += n; return c; }
    memcpy(m->p + m->pos, b, (size_t)n); m->pos += n; if (m->pos > m->size) m->size = m->pos; return c;
}
static INT64 DLL_CALLCONV s_tl(fi_handle h) {
    shift_io *m = (shift_io *)h;
    return (m->pos >= SHIFT_FROM) ? m->pos + SHIFT : m->pos;
}
static int DLL_CALLCONV s_sk(fi_handle h, INT64 off, int origin) {
    shift_io *m = (shift_io *)h;
    INT64 target = (origin == SEEK_SET) ? off : (origin == SEEK_CUR) ? s_tl(h) + off : m->size + ((m->size >= SHIFT_FROM) ? SHIFT : 0) + off;
    if (target >= SHIFT_FROM + SHIFT) target -= SHIFT;
    if (target < 0) return -1;
    m->pos = target;
    return 0;
}

static void test_tga_past_4gb(void) {
    FreeImageIO io = { s_rd, s_wr, s_sk, s_tl };
    FIBITMAP *dib = synth(24, 64, 48, 7), *th = synth(24, 20, 13, 8), *d;
    shift_io *m = (shift_io *)calloc(1, sizeof(shift_io));
    const INT64 pixels = (INT64)64 * 48 * 3;
    BOOL saved;

    printf("TGA whose pixels end past 4 GB (positions shifted by 5 GB)\n");
    FreeImage_SetThumbnail(dib, th);
    last_message[0] = 0;
    saved = FreeImage_SaveToHandle(FIF_TARGA, dib, &io, (fi_handle)m, 0);
    report("FreeImage_SaveToHandle", saved);
    report("  nothing was written at a wrapped offset", m->far_writes == 0);
    report("  header, pixels and footer only: the thumbnail is left out", m->size == 18 + pixels + 26);
    report("  the footer ends the stream, with no extension area", m->size >= 26 && memcmp(m->p + m->size - 18, "TRUEVISION-XFILE.", 17) == 0 && get32(m->p + m->size - 26) == 0);
    report("  a message says so", strstr(last_message, "thumbnail") != NULL);
    {
        FIMEMORY *in = FreeImage_OpenMemory(m->p, (DWORD)m->size);
        d = FreeImage_LoadFromMemory(FIF_TARGA, in, 0);
        report("  the pixels load back", same_pixels(d, dib));
        if (d) FreeImage_Unload(d);
        FreeImage_CloseMemory(in);
    }
    free(m);
    FreeImage_Unload(th);
    FreeImage_Unload(dib);
}

/* --- a multi-page memory stream that does not start at byte 0 ------------------ */

static void test_multipage_memory(void) {
    char path[1024];
    FIBITMAP *p0 = synth(24, 30, 20, 9), *p1 = synth(8, 50, 40, 10), *d;
    FIMEMORY *tif = FreeImage_OpenMemory(NULL, 0), *mem;
    FIMULTIBITMAP *mb;
    BYTE *bytes = NULL, *all;
    DWORD size = 0;
    int i;

    printf("multi-page TIFF in memory behind %d bytes of other data\n", JUNK);
    tmppath(path, sizeof(path), "fi_io_streams_mp.tif");
    remove(path);
    mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, TRUE, FALSE, TRUE, 0);
    if (mb) { FreeImage_AppendPage(mb, p0); FreeImage_AppendPage(mb, p1); }
    if (!mb || !FreeImage_SaveMultiBitmapToMemory(FIF_TIFF, mb, tif, 0) || !FreeImage_AcquireMemory(tif, &bytes, &size)) {
        report("save the TIFF to memory", 0);
        if (mb) FreeImage_CloseMultiBitmap(mb, 0);
        return;
    }
    FreeImage_CloseMultiBitmap(mb, 0);
    remove(path);

    all = (BYTE *)malloc(JUNK + size);
    for (i = 0; i < JUNK; i++) all[i] = (BYTE)(i * 7);
    memcpy(all + JUNK, bytes, size);
    mem = FreeImage_OpenMemory(all, JUNK + size);
    FreeImage_SeekMemory(mem, JUNK, SEEK_SET);
    mb = FreeImage_LoadMultiBitmapFromMemory(FIF_TIFF, mem, 0);
    report("FreeImage_LoadMultiBitmapFromMemory -> 2 pages", mb && FreeImage_GetPageCount(mb) == 2);
    d = mb ? FreeImage_LockPage(mb, 1) : NULL;
    report("  page 1 pixels", same_pixels(d, p1));
    if (d) FreeImage_UnlockPage(mb, d, FALSE);
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
    report("  a wrapped buffer seeks to its end", FreeImage_SeekMemory(mem, 0, SEEK_END) && FreeImage_TellMemory(mem) == (long)(JUNK + size));
    FreeImage_CloseMemory(mem);
    free(all);
    FreeImage_CloseMemory(tif);
    FreeImage_Unload(p0);
    FreeImage_Unload(p1);
}

int main(void) {
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(message);
    test_ico();
    test_gif();
    test_tga_thumbnail();
    test_tga_past_4gb();
    test_multipage_memory();
    FreeImage_DeInitialise();
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
