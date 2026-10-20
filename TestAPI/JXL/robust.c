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
    "data/anim_icc_lossy.jxl",      /* XYB frames blended in ICC colours */
    "data/cmyk16_lossy.jxl",        /* CMYK: modular, a black channel    */
    "data/cmyka8_xyb.jxl",          /* CMYK + alpha by cjxl: XYB         */
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

/* ---- Exif blocks: the reader gets them in buffers of their own size ---- */

static int failures = 0;

static void put16le(BYTE *p, unsigned v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }
static void put32le(BYTE *p, unsigned v) { put16le(p, v & 0xFFFF); put16le(p + 2, v >> 16); }

/* an IFD entry holding its value, or the offset of its value */
static BYTE *ifd_entry(BYTE *p, unsigned tag, unsigned type, unsigned count, unsigned value) {
    put16le(p, tag); put16le(p + 2, type); put32le(p + 4, count); put32le(p + 8, value);
    return p + 12;
}

/* a 16x16 image saved with "Exif\0\0" and the TIFF data as its raw Exif */
static FIMEMORY *saved_with_exif(const BYTE *tiff, unsigned length) {
    BYTE raw[512];
    FIBITMAP *dib = FreeImage_Allocate(16, 16, 24, 0, 0, 0);
    FITAG *tag = FreeImage_CreateTag();
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    BOOL ok = dib && tag && mem && (length + 6 <= sizeof(raw));
    if (ok) {
        memcpy(raw, "Exif\0\0", 6);
        memcpy(raw + 6, tiff, length);
        FreeImage_SetTagKey(tag, "ExifRaw");
        FreeImage_SetTagType(tag, FIDT_BYTE);
        FreeImage_SetTagCount(tag, length + 6);
        FreeImage_SetTagLength(tag, length + 6);
        FreeImage_SetTagValue(tag, raw);
        FreeImage_SetMetadata(FIMD_EXIF_RAW, dib, "ExifRaw", tag);
        ok = FreeImage_SaveToMemory(FIF_JXL, dib, mem, JXL_LOSSLESS | 0x10000);
    }
    if (tag) FreeImage_DeleteTag(tag);
    if (dib) FreeImage_Unload(dib);
    if (!ok && mem) { FreeImage_CloseMemory(mem); mem = NULL; }
    return mem;
}

/* the tags of a model after a load of the saved file, -1 if it did not load */
static int tags_after_load(FIMEMORY *mem, FREE_IMAGE_MDMODEL model) {
    BYTE *data = NULL;
    DWORD size = 0;
    FIBITMAP *dib;
    int n = -1;
    FreeImage_AcquireMemory(mem, &data, &size);
    try_load(data, (long)size);
    FreeImage_SeekMemory(mem, 0, SEEK_SET);
    dib = FreeImage_LoadFromMemory(FIF_JXL, mem, 0);
    if (dib) { n = (int)FreeImage_GetMetadataCount(model, dib); FreeImage_Unload(dib); }
    return n;
}

/* saves, loads, and expects 'want' tags of the model (-1: any) */
static void expect_tags(const BYTE *tiff, unsigned length, FREE_IMAGE_MDMODEL model, int want, const char *what) {
    FIMEMORY *mem = saved_with_exif(tiff, length);
    int got;
    if (!mem) { printf("  FAIL %s: not saved\n", what); failures++; return; }
    got = tags_after_load(mem, model);
    if ((got < 0) || ((want >= 0) && (got != want))) { printf("  FAIL %s: %d tags, expected %d\n", what, got, want); failures++; }
    FreeImage_CloseMemory(mem);
}

/* crafted blocks, then one cut at every length and read from every offset */
static void exif_blocks(void) {
    static const BYTE head[8] = { 'I', 'I', 42, 0, 8, 0, 0, 0 };
    BYTE t[256], *p, *data = NULL, *copy;
    unsigned full, n, at;
    DWORD size = 0;
    long before = cases;
    FIMEMORY *mem;

    memcpy(t, head, 8);
    expect_tags(t, 8, FIMD_EXIF_MAIN, 0, "a TIFF header alone");

    /* IFD0 of 3 entries: complete without its next-IFD link, then 2 bytes short */
    put16le(t + 8, 3);
    p = ifd_entry(t + 10, 0x0112, 3, 1, 1);
    p = ifd_entry(p, 0x011A, 5, 1, 0);
    p = ifd_entry(p, 0x0128, 3, 1, 2);
    expect_tags(t, (unsigned)(p - t), FIMD_EXIF_MAIN, 3, "an IFD without its next-IFD link");
    expect_tags(t, (unsigned)(p - t) - 2, FIMD_EXIF_MAIN, 0, "an IFD 2 bytes short");

    /* maker notes: 5 bytes ending the block, 4 bytes in their entry, a Fujifilm one too short for its IFD offset */
    put16le(t + 8, 1);
    p = ifd_entry(t + 10, 0x927C, 7, 5, 26);
    put32le(p, 0);
    memcpy(p + 4, "Nikon", 5);
    expect_tags(t, 31, FIMD_EXIF_MAIN, -1, "a maker note ending the block");
    p = ifd_entry(t + 10, 0x927C, 7, 4, 0);
    memcpy(p - 4, "AGFA", 4);
    expect_tags(t, (unsigned)(p - t), FIMD_EXIF_MAIN, -1, "a maker note in its entry, ending the block");
    put16le(t + 8, 2);
    p = ifd_entry(t + 10, 0x010F, 2, 9, 38);
    p = ifd_entry(p, 0x927C, 7, 9, 47);
    put32le(p, 0);
    memcpy(t + 38, "FUJIFILM\0FUJIFILMx", 18);
    expect_tags(t, 56, FIMD_EXIF_MAIN, -1, "a Fujifilm maker note of 9 bytes");

    /* Canon camera settings: 40 bytes taken for 20 shorts before; as shorts, split into 19 tags */
    p = ifd_entry(t + 10, 0x010F, 2, 6, 38);
    p = ifd_entry(p, 0x927C, 7, 58, 44);
    put32le(p, 0);
    memcpy(t + 38, "Canon\0", 6);
    put16le(t + 44, 1);
    p = ifd_entry(t + 46, 0x0001, 1, 40, 62);
    put32le(p, 0);
    for (n = 0; n < 40; n++) t[62 + n] = (BYTE)n;
    expect_tags(t, 102, FIMD_EXIF_MAKERNOTE, 1, "Canon camera settings as bytes");
    ifd_entry(t + 46, 0x0001, 3, 20, 62);
    expect_tags(t, 102, FIMD_EXIF_MAKERNOTE, 19, "Canon camera settings as shorts");
    full = 102;

    /* the Canon block cut at every length the writer takes */
    for (n = 8; n < full; n++) {
        mem = saved_with_exif(t, n);
        if (mem) { tags_after_load(mem, FIMD_EXIF_MAIN); FreeImage_CloseMemory(mem); }
    }

    /* and its box's offset to the TIFF header set to every value: from 1 byte left to past the end */
    mem = saved_with_exif(t, full);
    if (mem && FreeImage_AcquireMemory(mem, &data, &size) && (copy = (BYTE *)malloc(size)) != NULL) {
        for (at = 0; at + 12 <= size; at++) {
            if ((memcmp(data + at + 4, "Exif", 4) == 0) && (get32be(data + at) == 8 + 4 + full)) break;
        }
        if (at + 12 > size) { printf("  FAIL no Exif box in the saved file\n"); failures++; }
        for (n = 0; (at + 12 <= size) && (n <= full + 1); n++) {
            memcpy(copy, data, size);
            put32be(copy + at + 8, n);
            try_load(copy, (long)size);
        }
        free(copy);
    }
    if (mem) FreeImage_CloseMemory(mem);
    printf("  %s Exif blocks                              %ld cases\n", failures ? "FAIL" : "ok", cases - before);
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

    exif_blocks();

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
    FreeImage_DeInitialise();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("survived\n");
    return 0;
}
