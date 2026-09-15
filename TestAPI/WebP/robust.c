/*
 * FreeImage 3 - WebP robustness test
 *
 * Feeds the plugin damaged input and checks only that it survives: truncated
 * prefixes, junk appended, single-byte corruptions spread over the container
 * and the compressed data, wiped regions, nonsense in the RIFF and chunk
 * length fields, and empty and tiny buffers. Every one of these may load or be
 * refused - what it may not do is crash, hang, or leak.
 *
 * This is the test that is about the reason for the 1.2.1 -> 1.6.0 upgrade.
 * CVE-2023-4863 was a heap overflow in the lossless decoder's Huffman table
 * construction, reached from a crafted file and exploited in the wild; 1.3.2
 * fixed it and 1.2.1 predates the fix. A sanitized run over damaged input is
 * the shape of test that catches its like, so run it under AddressSanitizer -
 * "make asan-run" is what that is for. Without one it still catches aborts and
 * null dereferences.
 *
 * The corpus leans on the lossless files on purpose: VP8L is where that bug
 * lived and where the interesting parsing still is.
 *
 * Every load goes through a memory stream, so nothing is written to disk.
 *
 * Standalone: build with the Makefile in this directory, run from it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static const char *FILES[] = {
    "data/fi_webp_rgb_lossless.webp",     /* VP8L, the CVE-2023-4863 path   */
    "data/fi_webp_alpha_lossless.webp",   /* VP8L with an alpha channel     */
    "data/fi_webp_holes_lossless.webp",   /* VP8L, fully transparent runs   */
    "data/fi_webp_rgb_lossy.webp",        /* VP8, the lossy decoder         */
    "data/fi_webp_alpha_lossy.webp",      /* VP8 + a separate ALPH chunk    */
    "data/fi_webp_meta_lossless.webp",    /* extended format: ICCP/XMP/EXIF */
};
#define NFILES ((int)(sizeof(FILES) / sizeof(FILES[0])))

static long loaded = 0, refused = 0, cases = 0;

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

/* Loads one buffer and throws the result away. Returns 1 if it decoded. */
static int try_load(const BYTE *data, long len) {
    FIMEMORY *mem;
    FIBITMAP *dib;
    int ok;
    cases++;
    mem = FreeImage_OpenMemory((BYTE *)data, (DWORD)len);
    if (!mem) return 0;
    dib = FreeImage_LoadFromMemory(FIF_WEBP, mem, 0);
    ok = dib != NULL;
    if (dib) {
        /* touch every row, so a short buffer shows up as a fault here */
        unsigned h = FreeImage_GetHeight(dib), line = FreeImage_GetLine(dib), y, i;
        unsigned long long acc = 0;
        for (y = 0; y < h; y++) {
            const unsigned char *p = (const unsigned char *)FreeImage_GetScanLine(dib, y);
            for (i = 0; i < line; i += 64) acc += p[i];
        }
        (void)acc;
        FreeImage_Unload(dib);
    }
    FreeImage_CloseMemory(mem);
    if (ok) loaded++; else refused++;
    return ok;
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

static void put32(BYTE *p, unsigned v) {
    p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); p[2] = (BYTE)(v >> 16); p[3] = (BYTE)(v >> 24);
}
static unsigned get32(const BYTE *p) {
    return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}

/* A WebP file is RIFF: "RIFF" <size> "WEBP" then 4-byte-tag/4-byte-size chunks,
   each payload padded to an even length. Rewrite every one of those size fields
   in turn to values a decoder must not believe. */
static void maul_lengths(const BYTE *orig, long len) {
    static const unsigned BAD[] = { 0u, 1u, 0x7FFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFF0u };
    BYTE *copy = (BYTE *)malloc((size_t)len);
    long off;
    int i;
    if (!copy) return;

    /* the RIFF size itself */
    for (i = 0; i < (int)(sizeof(BAD) / sizeof(BAD[0])); i++) {
        memcpy(copy, orig, (size_t)len);
        put32(copy + 4, BAD[i]);
        try_load(copy, len);
    }

    /* then each chunk's size */
    for (off = 12; off + 8 <= len; ) {
        unsigned csize = get32(orig + off + 4);
        for (i = 0; i < (int)(sizeof(BAD) / sizeof(BAD[0])); i++) {
            memcpy(copy, orig, (size_t)len);
            put32(copy + off + 4, BAD[i]);
            try_load(copy, len);
        }
        if (csize > (unsigned)(len - off - 8)) break;      /* already past the end */
        off += 8 + csize + (csize & 1);
    }
    free(copy);
}

int main(void) {
    int f;

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);
    printf("WebP robustness test\n\n");

    /* degenerate buffers first */
    {
        static const BYTE tiny[] = { 'R','I','F','F',4,0,0,0,'W','E','B','P','V','P','8',' ' };
        BYTE one = 0;
        unsigned n;
        try_load(&one, 0);
        try_load(&one, 1);
        for (n = 1; n <= sizeof(tiny); n++) try_load(tiny, (long)n);
        printf("  ok degenerate buffers                     %ld cases\n", cases);
    }

    for (f = 0; f < NFILES; f++) {
        long len, i, before = cases;
        BYTE *orig = slurp(FILES[f], &len), *copy;
        if (!orig) { printf("  FAIL cannot read %s\n", FILES[f]); FreeImage_DeInitialise(); return 1; }

        /* every truncation, coarsely, plus every byte of the first 64 */
        for (i = 1; i < len; i += (i < 64 ? 1 : 97)) try_load(orig, i);

        copy = (BYTE *)malloc((size_t)len + 4096);
        if (copy) {
            /* junk appended - a decoder must stop at the length it was told */
            memcpy(copy, orig, (size_t)len);
            memset(copy + len, 0xA5, 4096);
            try_load(copy, len + 4096);
            try_load(copy, len + 1);

            /* single-byte corruptions, dense over the header, sparse after */
            for (i = 0; i < len; i += (i < 128 ? 1 : 251)) {
                memcpy(copy, orig, (size_t)len);
                copy[i] ^= 0xFF;
                try_load(copy, len);
                copy[i] = 0;
                try_load(copy, len);
            }

            /* 64-byte regions wiped out */
            for (i = 0; i + 64 <= len; i += 512) {
                memcpy(copy, orig, (size_t)len);
                memset(copy + i, 0, 64);
                try_load(copy, len);
            }
            free(copy);
        }

        maul_lengths(orig, len);
        free(orig);
        printf("  ok %-38s %ld cases\n", FILES[f], cases - before);
    }

    printf("\n%ld damaged inputs: %ld decoded, %ld refused, 0 crashed\n",
           cases, loaded, refused);
    printf("survived\n");
    FreeImage_DeInitialise();
    return 0;
}
