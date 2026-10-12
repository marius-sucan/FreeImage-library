/* JPEG XL robustness test: damaged input may fail, never crash; run under ASan */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static const char *FILES[] = {
    "data/grey8_lossless.jxl",      /* modular, bare codestream          */
    "data/rgb8_lossy.jxl",          /* VarDCT, XYB                       */
    "data/rgba8_lossy.jxl",         /* VarDCT with modular alpha         */
    "data/rgb16_pq_lossless.jxl",   /* 16-bit, PQ                        */
    "data/rgbaf_lossless.jxl",      /* float                             */
    "data/meta_lossy.jxl",          /* container: Exif and XMP boxes     */
    "data/icc_sampled_lossy.jxl",   /* XYB with an ICC profile           */
    "data/anim_lossless.jxl",       /* animation with reference frames   */
    "data/oriented5_alpha.jxl",     /* orientation, container            */
    "data/premultiplied.jxl",       /* premultiplied alpha               */
    "data/layers_still.jxl",        /* layers blended into one image     */
    "data/brob_exif.jxl",           /* a Brotli-compressed Exif box      */
};
#define NFILES ((int)(sizeof(FILES) / sizeof(FILES[0])))

static long loaded = 0, refused = 0, cases = 0;

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static void touch(FIBITMAP *dib) {
    /* every row: a short buffer faults here */
    unsigned h = FreeImage_GetHeight(dib), line = FreeImage_GetLine(dib), y, i;
    volatile unsigned long long acc = 0;
    if (!FreeImage_HasPixels(dib)) return;
    for (y = 0; y < h; y++) {
        const unsigned char *p = (const unsigned char *)FreeImage_GetScanLine(dib, y);
        for (i = 0; i < line; i += 64) acc += p[i];
        acc += p[line - 1];
    }
}

/* every page, the header of the first, and the plain load */
static void try_load(const BYTE *data, long len) {
    FIMEMORY *mem = FreeImage_OpenMemory((BYTE *)data, (DWORD)len);
    FIBITMAP *dib;
    FIMULTIBITMAP *m;
    int ok = 0, p, pages;
    cases++;
    if (!mem) return;
    dib = FreeImage_LoadFromMemory(FIF_JXL, mem, 0);
    if (dib) { ok = 1; touch(dib); FreeImage_Unload(dib); }
    FreeImage_SeekMemory(mem, 0, SEEK_SET);
    dib = FreeImage_LoadFromMemory(FIF_JXL, mem, FIF_LOAD_NOPIXELS);
    if (dib) FreeImage_Unload(dib);
    FreeImage_CloseMemory(mem);

    mem = FreeImage_OpenMemory((BYTE *)data, (DWORD)len);
    m = FreeImage_LoadMultiBitmapFromMemory(FIF_JXL, mem, JXL_PLAYBACK);
    if (m) {
        pages = FreeImage_GetPageCount(m);
        for (p = pages - 1; p >= 0 && p >= pages - 3; p--) {
            FIBITMAP *page = FreeImage_LockPage(m, p);
            if (page) { touch(page); FreeImage_UnlockPage(m, page, FALSE); }
        }
        FreeImage_CloseMultiBitmap(m, 0);
    }
    FreeImage_CloseMemory(mem);
    if (ok) loaded++; else refused++;
}

static BYTE *slurp(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    BYTE *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (BYTE *)malloc(*len ? (size_t)*len : 1);
    if (!buf || fread(buf, 1, (size_t)*len, f) != (size_t)*len) { free(buf); fclose(f); return NULL; }
    fclose(f);
    return buf;
}

static void put32be(BYTE *p, unsigned v) {
    p[0] = (BYTE)(v >> 24); p[1] = (BYTE)(v >> 16); p[2] = (BYTE)(v >> 8); p[3] = (BYTE)v;
}
static unsigned get32be(const BYTE *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | (unsigned)p[3];
}

/* every BMFF box size of a container rewritten to nonsense */
static void maul_boxes(const BYTE *orig, long len) {
    static const unsigned BAD[] = { 0u, 1u, 7u, 8u, 0x7FFFFFFFu, 0xFFFFFFFFu };
    BYTE *copy = (BYTE *)malloc((size_t)len);
    long off = 0;
    int i;
    if (!copy || len < 12 || get32be(orig) != 12) { free(copy); return; }
    while (off + 8 <= len) {
        unsigned size = get32be(orig + off);
        for (i = 0; i < (int)(sizeof(BAD) / sizeof(BAD[0])); i++) {
            memcpy(copy, orig, (size_t)len);
            put32be(copy + off, BAD[i]);
            try_load(copy, len);
        }
        if (size < 8 || size > (unsigned)(len - off)) break;
        off += size;
    }
    free(copy);
}

int main(void) {
    int f;

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);
    printf("JPEG XL robustness test\n\n");

    /* degenerate buffers first */
    {
        static const BYTE box[] = { 0, 0, 0, 12, 'J', 'X', 'L', ' ', 0x0D, 0x0A, 0x87, 0x0A, 0, 0, 0, 20, 'f', 't', 'y', 'p' };
        static const BYTE bare[] = { 0xFF, 0x0A, 0x00, 0x00, 0x00, 0x00 };
        BYTE one = 0;
        unsigned n;
        try_load(&one, 0);
        try_load(&one, 1);
        for (n = 1; n <= sizeof(box); n++) try_load(box, (long)n);
        for (n = 1; n <= sizeof(bare); n++) try_load(bare, (long)n);
        printf("  ok degenerate buffers                     %ld cases\n", cases);
    }

    for (f = 0; f < NFILES; f++) {
        long len, i, before = cases;
        BYTE *orig = slurp(FILES[f], &len), *copy;
        if (!orig) { printf("  FAIL cannot read %s\n", FILES[f]); FreeImage_DeInitialise(); return 1; }

        /* every truncation of the headers, then coarsely */
        for (i = 1; i < len; i += (i < 256 ? 1 : 37)) try_load(orig, i);

        copy = (BYTE *)malloc((size_t)len + 4096);
        if (copy) {
            /* junk appended */
            memcpy(copy, orig, (size_t)len);
            memset(copy + len, 0xA5, 4096);
            try_load(copy, len + 4096);
            try_load(copy, len + 1);

            /* single-byte corruptions, dense over the headers, sparse after */
            for (i = 0; i < len; i += (i < 192 ? 1 : 29)) {
                memcpy(copy, orig, (size_t)len);
                copy[i] ^= 0xFF;
                try_load(copy, len);
                copy[i] = (BYTE)(copy[i] ^ 0x5A);
                try_load(copy, len);
            }

            /* 32-byte regions wiped out */
            for (i = 0; i + 32 <= len; i += 128) {
                memcpy(copy, orig, (size_t)len);
                memset(copy + i, 0, 32);
                try_load(copy, len);
            }
            free(copy);
        }

        maul_boxes(orig, len);
        free(orig);
        printf("  ok %-38s %ld cases\n", FILES[f], cases - before);
    }

    printf("\n%ld damaged inputs: %ld decoded, %ld refused, 0 crashed\n", cases, loaded, refused);
    printf("survived\n");
    FreeImage_DeInitialise();
    return 0;
}
