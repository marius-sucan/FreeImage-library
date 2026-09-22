/* JPEG decode test: data/ against a recorded table */
/* data/ is libjpeg 9d output, a cross-version oracle: never regenerate it */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "FreeImage.h"

typedef struct {
    const char *file;
    unsigned width, height, bpp;
    int colortype;
    unsigned long long fast;     /* digest with JPEG_DEFAULT / JPEG_FAST */
    unsigned long long accurate; /* digest with JPEG_ACCURATE            */
    int icc, xmp, exif;          /* attached metadata, in bytes          */
} Expect;

/* --record reprints this table. */
static const Expect TABLE[] = {
    { "data/fi_jpeg_444.jpg",        256, 192, 24, 2, 0xebbd644dacc2beb5ULL, 0x65a9410a36ec11b5ULL,    0,   0,    0 },
    { "data/fi_jpeg_422.jpg",        256, 192, 24, 2, 0x861dda8ae0c22324ULL, 0xe7023f7be5b0f077ULL,    0,   0,    0 },
    { "data/fi_jpeg_420.jpg",        256, 192, 24, 2, 0x84a2a3f689c77c77ULL, 0xf0fd0c87170a7868ULL,    0,   0,    0 },
    { "data/fi_jpeg_411.jpg",        256, 192, 24, 2, 0x2160b3cca7644c62ULL, 0x7271759e17af7b54ULL,    0,   0,    0 },
    { "data/fi_jpeg_progressive.jpg",256, 192, 24, 2, 0x84a2a3f689c77c77ULL, 0xf0fd0c87170a7868ULL,    0,   0,    0 },
    { "data/fi_jpeg_arithmetic.jpg", 256, 192, 24, 2, 0x84a2a3f689c77c77ULL, 0xf0fd0c87170a7868ULL,    0,   0,    0 },
    { "data/fi_jpeg_optimized.jpg",  256, 192, 24, 2, 0xbc6b90a05f5972d3ULL, 0x99ba97d44f776250ULL,    0,   0,    0 },
    { "data/fi_jpeg_restart.jpg",    256, 192, 24, 2, 0x84a2a3f689c77c77ULL, 0xf0fd0c87170a7868ULL,    0,   0,    0 },
    { "data/fi_jpeg_grey.jpg",       256, 192,  8, 1, 0x3fbad077292d81a2ULL, 0xcf0b0ad214239520ULL,    0,   0,    0 },
    { "data/fi_jpeg_rgb.jpg",        256, 192, 24, 2, 0xe910f5dd401f2af9ULL, 0xf63a0a7cb71c5be0ULL,    0,   0,    0 },
    { "data/fi_jpeg_cmyk.jpg",       256, 192, 24, 2, 0x829de56c3b169a8cULL, 0x7277455461a0212dULL,    0,   0,    0 },
    { "data/fi_jpeg_q100.jpg",       256, 192, 24, 2, 0x67bb47a628d78888ULL, 0x8a044161f7c2c0c3ULL,    0,   0,    0 },
    { "data/fi_jpeg_q10.jpg",        256, 192, 24, 2, 0xa95d49d8830148faULL, 0x620b3faff3baa670ULL,    0,   0,    0 },
    { "data/fi_jpeg_odd.jpg",         33,  17, 24, 2, 0x0d0f47c470fab6d6ULL, 0x4c64a7b774403cdcULL,    0,   0,    0 },
    { "data/fi_jpeg_1x1.jpg",          1,   1, 24, 2, 0x9bae4a6f6653d6c3ULL, 0x9bae4a6f6653d6c3ULL,    0,   0,    0 },
    { "data/fi_jpeg_meta.jpg",       256, 192, 24, 2, 0x84a2a3f689c77c77ULL, 0xf0fd0c87170a7868ULL, 3144, 7661, 4706 },
};
#define NFILES ((int)(sizeof(TABLE) / sizeof(TABLE[0])))

static int failures = 0;

static void fail(const char *file, const char *what, const char *fmt, ...) {
    va_list ap;
    printf("  FAIL %s: %s - ", file, what);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
    failures++;
}

/* hash visible pixels only, not the pitch padding */
static unsigned long long digest(FIBITMAP *dib) {
    unsigned long long h = 1469598103934665603ULL;
    unsigned w = FreeImage_GetWidth(dib), ht = FreeImage_GetHeight(dib);
    unsigned bpp = FreeImage_GetBPP(dib), y;
    size_t row = (size_t)w * (bpp / 8), i;
    const unsigned char *p;
    unsigned hdr[3];
    hdr[0] = w; hdr[1] = ht; hdr[2] = bpp;
    p = (const unsigned char *)hdr;
    for (i = 0; i < sizeof(hdr); i++) { h ^= p[i]; h *= 1099511628211ULL; }
    for (y = 0; y < ht; y++) {
        p = (const unsigned char *)FreeImage_GetScanLine(dib, y);
        for (i = 0; i < row; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    }
    return h;
}

static int meta_len(FIBITMAP *dib, FREE_IMAGE_MDMODEL model, const char *key) {
    FITAG *tag = NULL;
    if (!FreeImage_GetMetadata(model, dib, key, &tag) || !tag) return 0;
    return (int)FreeImage_GetTagLength(tag);
}

static int icc_len(FIBITMAP *dib) {
    FIICCPROFILE *p = FreeImage_GetICCProfile(dib);
    return (p && p->data) ? (int)p->size : 0;
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

int main(int argc, char **argv) {
    int record = (argc > 1 && strcmp(argv[1], "--record") == 0);
    int i;

    FreeImage_Initialise(FALSE);
    printf("JPEG decode test - %d files\n\n", NFILES);
    if (record) printf("static const Expect TABLE[] = {\n");

    for (i = 0; i < NFILES; i++) {
        const Expect *e = &TABLE[i];
        FIBITMAP *dib, *acc, *hdr_only, *from_mem;
        FIMEMORY *mem;
        BYTE *raw;
        long len;
        unsigned long long d, da;
        int icc, xmp, exif;

        if (FreeImage_GetFileType(e->file, 0) != FIF_JPEG)
            fail(e->file, "detection", "GetFileType did not say FIF_JPEG");

        dib = FreeImage_Load(FIF_JPEG, e->file, JPEG_DEFAULT);
        if (!dib) { fail(e->file, "load", "returned NULL"); continue; }
        acc = FreeImage_Load(FIF_JPEG, e->file, JPEG_ACCURATE);
        if (!acc) { fail(e->file, "load", "JPEG_ACCURATE returned NULL"); FreeImage_Unload(dib); continue; }

        d = digest(dib);
        da = digest(acc);
        icc = icc_len(dib); xmp = meta_len(dib, FIMD_XMP, "XMLPacket");
        exif = meta_len(dib, FIMD_EXIF_RAW, "ExifRaw");

        if (record) {
            printf("    { \"%s\",%*s%3u, %3u, %2u, %d, 0x%016llxULL, 0x%016llxULL, %4d, %3d, %4d },\n",
                   e->file, (int)(28 - strlen(e->file)), "",
                   FreeImage_GetWidth(dib), FreeImage_GetHeight(dib),
                   FreeImage_GetBPP(dib), (int)FreeImage_GetColorType(dib),
                   d, da, icc, xmp, exif);
        } else {
            if (FreeImage_GetWidth(dib) != e->width || FreeImage_GetHeight(dib) != e->height)
                fail(e->file, "geometry", "got %ux%u, expected %ux%u",
                     FreeImage_GetWidth(dib), FreeImage_GetHeight(dib), e->width, e->height);
            if (FreeImage_GetBPP(dib) != e->bpp)
                fail(e->file, "depth", "got %u bpp, expected %u", FreeImage_GetBPP(dib), e->bpp);
            if ((int)FreeImage_GetColorType(dib) != e->colortype)
                fail(e->file, "color type", "got %d, expected %d",
                     (int)FreeImage_GetColorType(dib), e->colortype);
            if (d != e->fast)
                fail(e->file, "pixels (fast IDCT)", "got %016llx, expected %016llx", d, e->fast);
            if (da != e->accurate)
                fail(e->file, "pixels (accurate IDCT)", "got %016llx, expected %016llx", da, e->accurate);
            if (icc != e->icc)   fail(e->file, "ICC",  "got %d bytes, expected %d", icc, e->icc);
            if (xmp != e->xmp)   fail(e->file, "XMP",  "got %d bytes, expected %d", xmp, e->xmp);
            if (exif != e->exif) fail(e->file, "EXIF", "got %d bytes, expected %d", exif, e->exif);
            printf("  ok %-30s %3ux%-3u %2u bpp  icc=%-4d xmp=%-4d exif=%d\n",
                   e->file, e->width, e->height, e->bpp, icc, xmp, exif);
        }

        /* JPEG_FAST must equal JPEG_DEFAULT */
        {
            FIBITMAP *f = FreeImage_Load(FIF_JPEG, e->file, JPEG_FAST);
            if (!f) {
                fail(e->file, "JPEG_FAST", "returned NULL");
            } else {
                if (digest(f) != d)
                    fail(e->file, "JPEG_FAST", "differs from JPEG_DEFAULT");
                FreeImage_Unload(f);
            }
        }

        hdr_only = FreeImage_Load(FIF_JPEG, e->file, FIF_LOAD_NOPIXELS);
        if (!hdr_only) {
            fail(e->file, "header-only", "returned NULL");
        } else {
            if (FreeImage_GetWidth(hdr_only) != FreeImage_GetWidth(dib) ||
                FreeImage_GetHeight(hdr_only) != FreeImage_GetHeight(dib) ||
                FreeImage_GetBPP(hdr_only) != FreeImage_GetBPP(dib))
                fail(e->file, "header-only", "disagrees with the full load");
            FreeImage_Unload(hdr_only);
        }

        raw = slurp(e->file, &len);
        if (!raw) {
            fail(e->file, "read", "could not read the file");
        } else {
            mem = FreeImage_OpenMemory(raw, (DWORD)len);
            from_mem = mem ? FreeImage_LoadFromMemory(FIF_JPEG, mem, JPEG_DEFAULT) : NULL;
            if (!from_mem) {
                fail(e->file, "memory", "LoadFromMemory returned NULL");
            } else {
                if (digest(from_mem) != d)
                    fail(e->file, "memory", "decodes differently from the file");
                FreeImage_Unload(from_mem);
            }
            if (mem) FreeImage_CloseMemory(mem);
            free(raw);
        }

        FreeImage_Unload(acc);
        FreeImage_Unload(dib);
    }

    if (record) { printf("};\n"); FreeImage_DeInitialise(); return 0; }

    /* JPEG_CMYK keeps the four channels; without it they become RGB */
    {
        const char *f = "data/fi_jpeg_cmyk.jpg";
        FIBITMAP *rgb = FreeImage_Load(FIF_JPEG, f, JPEG_DEFAULT);
        FIBITMAP *raw4 = FreeImage_Load(FIF_JPEG, f, JPEG_CMYK);
        if (!rgb || !raw4) {
            fail(f, "CMYK", "one of the two loads returned NULL");
        } else {
            if (FreeImage_GetBPP(rgb) != 24)
                fail(f, "CMYK", "without JPEG_CMYK expected 24 bpp, got %u", FreeImage_GetBPP(rgb));
            if (FreeImage_GetBPP(raw4) != 32)
                fail(f, "CMYK", "with JPEG_CMYK expected 32 bpp, got %u", FreeImage_GetBPP(raw4));
            if (FreeImage_GetColorType(raw4) != FIC_CMYK)
                fail(f, "CMYK", "with JPEG_CMYK expected FIC_CMYK, got %d",
                     (int)FreeImage_GetColorType(raw4));
            printf("  ok %-30s JPEG_CMYK -> %u bpp FIC_CMYK\n", f, FreeImage_GetBPP(raw4));
        }
        if (rgb) FreeImage_Unload(rgb);
        if (raw4) FreeImage_Unload(raw4);
    }

    printf("\n%s\n", failures ? "FAILED" : "all files decoded as recorded");
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
