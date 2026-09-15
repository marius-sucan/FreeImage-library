/*
 * FreeImage 3 - JPEG 2000 corpus decoder
 *
 * Decodes every file on the command line through the J2K/JP2 plugins and prints
 * one line per file: geometry, pixel type, an order-sensitive checksum of the
 * pixel rows (pitch padding excluded), the decode time and any messages the
 * library emitted. Run it before and after a change to the bundled OpenJPEG
 * and diff the output; feed it the openjpeg-data conformance and
 * non-regression files (see README.md) and compare the dumps against the
 * reference decoder with compcheck.py.
 *
 *   corpus [-h] [-m] [-o N] [-d DIR] file...
 *     -h      header-only load (FIF_LOAD_NOPIXELS): geometry only, no checksum
 *     -m      load through FreeImage_LoadFromMemory instead of a file name
 *     -o N    load through a FreeImageIO handle positioned N bytes into a
 *             temporary copy that has N junk bytes prepended
 *     -d DIR  dump the decoded pixels of each file to DIR/<name>.raw: a text
 *             header "FIRAW <w> <h> <bpp> <type>" then the rows top to bottom
 *             without padding, in FreeImage's own byte order (BGR for 24-bit,
 *             BGRA for 32-bit, little-endian R,G,B[,A] for the 16-bit types)
 *
 * Exits non-zero if any file failed to load.
 *
 * Standalone: build with the Makefile in this directory, run from anywhere.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "FreeImage.h"

static char msgbuf[4096];

static void handler(FREE_IMAGE_FORMAT fif, const char *msg) {
    size_t n = strlen(msgbuf);
    (void)fif;
    if (n + strlen(msg) + 4 < sizeof(msgbuf)) {
        snprintf(msgbuf + n, sizeof(msgbuf) - n, "%s%s", n ? " | " : "", msg);
    }
    /* strip the newlines the plugins put in some messages */
    for (n = 0; msgbuf[n]; n++) if (msgbuf[n] == '\n') msgbuf[n] = ' ';
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static unsigned long long fnv_rows(FIBITMAP *dib) {
    unsigned long long h = 1469598103934665603ULL;
    unsigned y, x, n = (FreeImage_GetWidth(dib) * FreeImage_GetBPP(dib) + 7) / 8;
    for (y = 0; y < FreeImage_GetHeight(dib); y++) {
        const BYTE *p = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < n; x++) { h ^= p[x]; h *= 1099511628211ULL; }
    }
    return h;
}

static const char *type_name(FREE_IMAGE_TYPE t) {
    switch (t) {
        case FIT_BITMAP: return "BITMAP";
        case FIT_UINT16: return "UINT16";
        case FIT_RGB16:  return "RGB16";
        case FIT_RGBA16: return "RGBA16";
        default:         return "OTHER";
    }
}

static const char *base(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static FREE_IMAGE_FORMAT fif_of(const char *path) {
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(path, 0);
    if (fif == FIF_UNKNOWN) fif = FreeImage_GetFIFFromFilename(path);
    return fif;
}

static void dump_raw(FIBITMAP *dib, const char *dir, const char *name) {
    char path[1024];
    FILE *f;
    unsigned y, w = FreeImage_GetWidth(dib), h = FreeImage_GetHeight(dib);
    unsigned bpp = FreeImage_GetBPP(dib), rowbytes = (w * bpp + 7) / 8;
    snprintf(path, sizeof(path), "%s/%s.raw", dir, name);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "FIRAW %u %u %u %s\n", w, h, bpp, type_name(FreeImage_GetImageType(dib)));
    for (y = 0; y < h; y++) {
        fwrite(FreeImage_GetScanLine(dib, h - 1 - y), 1, rowbytes, f);
    }
    fclose(f);
}

/* --- a FreeImageIO over a plain FILE*, so the load can start at an offset --- */
static unsigned DLL_CALLCONV io_read(void *b, unsigned s, unsigned c, fi_handle h) { return (unsigned)fread(b, s, c, (FILE*)h); }
static unsigned DLL_CALLCONV io_write(void *b, unsigned s, unsigned c, fi_handle h) { return (unsigned)fwrite(b, s, c, (FILE*)h); }
static int DLL_CALLCONV io_seek(fi_handle h, long o, int w) { return fseek((FILE*)h, o, w); }
static long DLL_CALLCONV io_tell(fi_handle h) { return ftell((FILE*)h); }

static FIBITMAP *load_at_offset(FREE_IMAGE_FORMAT fif, const char *path, long offset, int flags) {
    FreeImageIO io = { io_read, io_write, io_seek, io_tell };
    FILE *src = fopen(path, "rb"), *tmp;
    FIBITMAP *dib;
    char junk[4096];
    long left = offset;
    int c;
    if (!src) return NULL;
    tmp = tmpfile();
    if (!tmp) { fclose(src); return NULL; }
    memset(junk, 0xA5, sizeof(junk));
    while (left > 0) {
        size_t n = left > (long)sizeof(junk) ? sizeof(junk) : (size_t)left;
        fwrite(junk, 1, n, tmp);
        left -= (long)n;
    }
    while ((c = fgetc(src)) != EOF) fputc(c, tmp);
    fclose(src);
    fseek(tmp, offset, SEEK_SET);
    dib = FreeImage_LoadFromHandle(fif, &io, (fi_handle)tmp, flags);
    fclose(tmp);
    return dib;
}

static FIBITMAP *load_from_memory(FREE_IMAGE_FORMAT fif, const char *path, int flags) {
    FILE *f = fopen(path, "rb");
    long n;
    BYTE *buf;
    FIMEMORY *mem;
    FIBITMAP *dib;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (BYTE*)malloc((size_t)n);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    fclose(f);
    mem = FreeImage_OpenMemory(buf, (DWORD)n);
    dib = FreeImage_LoadFromMemory(fif, mem, flags);
    FreeImage_CloseMemory(mem);
    free(buf);
    return dib;
}

int main(int argc, char **argv) {
    int header_only = 0, from_memory = 0, i, failures = 0;
    long offset = -1;
    const char *dumpdir = NULL;

    for (i = 1; i < argc && argv[i][0] == '-'; i++) {
        if (!strcmp(argv[i], "-h")) header_only = 1;
        else if (!strcmp(argv[i], "-m")) from_memory = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) offset = atol(argv[++i]);
        else if (!strcmp(argv[i], "-d") && i + 1 < argc) dumpdir = argv[++i];
        else { fprintf(stderr, "unknown option %s\n", argv[i]); return 2; }
    }

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(handler);
    printf("FreeImage %s, %d files, mode=%s%s%s\n", FreeImage_GetVersion(), argc - i,
           header_only ? "header-only" : "full", from_memory ? " memory-io" : "",
           offset >= 0 ? " offset-io" : "");

    for (; i < argc; i++) {
        const char *path = argv[i];
        FREE_IMAGE_FORMAT fif = fif_of(path);
        int flags = header_only ? FIF_LOAD_NOPIXELS : 0;
        FIBITMAP *dib;
        double t0, t1;
        msgbuf[0] = 0;
        if (fif != FIF_J2K && fif != FIF_JP2) {
            printf("%-22s  SKIP (not JPEG 2000: %s)\n", base(path), FreeImage_GetFormatFromFIF(fif));
            continue;
        }
        t0 = now_ms();
        if (offset >= 0)       dib = load_at_offset(fif, path, offset, flags);
        else if (from_memory)  dib = load_from_memory(fif, path, flags);
        else                   dib = FreeImage_Load(fif, path, flags);
        t1 = now_ms();
        if (!dib) {
            printf("%-22s  %s  FAILED  %6.1fms  %s\n", base(path), FreeImage_GetFormatFromFIF(fif), t1 - t0, msgbuf);
            failures++;
            continue;
        }
        if (header_only) {
            printf("%-22s  %s  %5ux%-5u %2ubpp %-6s  header-only  %s\n", base(path),
                   FreeImage_GetFormatFromFIF(fif), FreeImage_GetWidth(dib), FreeImage_GetHeight(dib),
                   FreeImage_GetBPP(dib), type_name(FreeImage_GetImageType(dib)), msgbuf);
        } else {
            printf("%-22s  %s  %5ux%-5u %2ubpp %-6s  sum=%016llx  %6.1fms  %s\n", base(path),
                   FreeImage_GetFormatFromFIF(fif), FreeImage_GetWidth(dib), FreeImage_GetHeight(dib),
                   FreeImage_GetBPP(dib), type_name(FreeImage_GetImageType(dib)), fnv_rows(dib),
                   t1 - t0, msgbuf);
            if (dumpdir) dump_raw(dib, dumpdir, base(path));
        }
        FreeImage_Unload(dib);
    }
    FreeImage_DeInitialise();
    printf("%d failed\n", failures);
    return failures ? 1 : 0;
}
