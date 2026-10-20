/* WebP robustness test: damaged input may fail, never crash; run under ASan */
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

/* 1 if it decoded */
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
        /* touch every row: a short buffer faults here */
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

/* corrupt the RIFF size and each chunk size in turn */
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

/* ---- EXIF chunks: the reader gets them in the file's buffer, ending where the chunk ends ---- */

static int failures = 0;

static void put16le(BYTE *p, unsigned v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }

/* an IFD entry holding its value, or the offset of its value */
static BYTE *ifd_entry(BYTE *p, unsigned tag, unsigned type, unsigned count, unsigned value) {
    put16le(p, tag); put16le(p + 2, type); put32(p + 4, count); put32(p + 8, value);
    return p + 12;
}

/* a 16x16 image saved with 'raw' as its EXIF chunk, loaded back: the tags of a model, -1 if it did not load */
static int exif_round_trip(const BYTE *raw, unsigned length, FREE_IMAGE_MDMODEL model) {
    FIBITMAP *dib = FreeImage_Allocate(16, 16, 24, 0, 0, 0);
    FITAG *tag = FreeImage_CreateTag();
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    BYTE *data = NULL;
    DWORD size = 0;
    int n = -1;
    if (dib && tag && mem) {
        FreeImage_SetTagKey(tag, "ExifRaw");
        FreeImage_SetTagType(tag, FIDT_BYTE);
        FreeImage_SetTagCount(tag, length);
        FreeImage_SetTagLength(tag, length);
        FreeImage_SetTagValue(tag, raw);
        FreeImage_SetMetadata(FIMD_EXIF_RAW, dib, "ExifRaw", tag);
        if (FreeImage_SaveToMemory(FIF_WEBP, dib, mem, WEBP_LOSSLESS) && FreeImage_AcquireMemory(mem, &data, &size)) {
            /* a copy of its own size: the EXIF chunk ends the file */
            BYTE *copy = (BYTE *)malloc(size);
            if (copy) {
                FIMEMORY *in;
                FIBITMAP *back;
                memcpy(copy, data, size);
                try_load(copy, (long)size);
                in = FreeImage_OpenMemory(copy, size);
                back = in ? FreeImage_LoadFromMemory(FIF_WEBP, in, 0) : NULL;
                if (back) { n = (int)FreeImage_GetMetadataCount(model, back); FreeImage_Unload(back); }
                if (in) FreeImage_CloseMemory(in);
                free(copy);
            }
        }
    }
    if (tag) FreeImage_DeleteTag(tag);
    if (dib) FreeImage_Unload(dib);
    if (mem) FreeImage_CloseMemory(mem);
    return n;
}

/* an Exif block with Canon camera settings, cut at every length */
static void exif_chunks(void) {
    BYTE t[6 + 102], *p;
    unsigned n;
    long before = cases;
    int got;

    memcpy(t, "Exif\0\0II*\0\x08\0\0\0", 14);
    put16le(t + 14, 2);
    p = ifd_entry(t + 16, 0x010F, 2, 6, 38);
    p = ifd_entry(p, 0x927C, 7, 58, 44);
    put32(p, 0);
    memcpy(t + 6 + 38, "Canon\0", 6);
    put16le(t + 6 + 44, 1);
    p = ifd_entry(t + 6 + 46, 0x0001, 3, 20, 62);
    put32(p, 0);
    for (n = 0; n < 40; n++) t[6 + 62 + n] = (BYTE)n;

    got = exif_round_trip(t, sizeof(t), FIMD_EXIF_MAKERNOTE);
    if (got != 19) { printf("  FAIL Canon camera settings: %d maker note tags, expected 19\n", got); failures++; }
    for (n = 1; n < sizeof(t); n++) {
        if (exif_round_trip(t, n, FIMD_EXIF_MAIN) < 0) { printf("  FAIL an EXIF chunk of %u bytes: not loaded\n", n); failures++; }
    }
    printf("  %s EXIF chunks                              %ld cases\n", failures ? "FAIL" : "ok", cases - before);
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

    exif_chunks();

    for (f = 0; f < NFILES; f++) {
        long len, i, before = cases;
        BYTE *orig = slurp(FILES[f], &len), *copy;
        if (!orig) { printf("  FAIL cannot read %s\n", FILES[f]); FreeImage_DeInitialise(); return 1; }

        /* every truncation, coarsely, plus every byte of the first 64 */
        for (i = 1; i < len; i += (i < 64 ? 1 : 97)) try_load(orig, i);

        copy = (BYTE *)malloc((size_t)len + 4096);
        if (copy) {
            /* junk appended */
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
    FreeImage_DeInitialise();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("survived\n");
    return 0;
}
