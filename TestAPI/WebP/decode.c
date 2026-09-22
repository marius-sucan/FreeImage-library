/* WebP decode test: data/ against the table below */
/* data/ is a cross-version oracle: never regenerate it */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "FreeImage.h"

typedef struct {
    const char *file;
    unsigned width, height, bpp;
    unsigned long long pixels;  /* digest of the decoded image */
    int icc, xmp, exif;         /* attached metadata, in bytes */
} Expect;

/* --record reprints this table. */
static const Expect TABLE[] = {
    { "data/fi_webp_rgb_lossy.webp",       240, 215, 24, 0xbec606cb61d83c5dULL,   0,   0,    0 },
    { "data/fi_webp_rgb_lossless.webp",    240, 215, 24, 0xe8e69ea8e48ee170ULL,   0,   0,    0 },
    { "data/fi_webp_alpha_lossy.webp",     240, 215, 32, 0xe0c5cd888e4be3e5ULL,   0,   0,    0 },
    { "data/fi_webp_alpha_lossless.webp",  240, 215, 32, 0x8d3b99f6b22123f0ULL,   0,   0,    0 },
    { "data/fi_webp_holes_lossless.webp",  240, 215, 32, 0x31dbf26eb50e4aafULL,   0,   0,    0 },
    { "data/fi_webp_meta_lossless.webp",   240, 215, 24, 0xe8e69ea8e48ee170ULL, 128, 187, 4706 },
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

/* visible pixels only, padding excluded */
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
    printf("WebP decode test - %d files\n\n", NFILES);
    if (record) printf("static const Expect TABLE[] = {\n");

    for (i = 0; i < NFILES; i++) {
        const Expect *e = &TABLE[i];
        FIBITMAP *dib, *hdr_only, *from_mem;
        FIMEMORY *mem;
        BYTE *raw;
        long len;
        unsigned long long d;
        int icc, xmp, exif;

        /* format detection must not need the extension */
        if (FreeImage_GetFileType(e->file, 0) != FIF_WEBP)
            fail(e->file, "detection", "GetFileType did not say FIF_WEBP");

        dib = FreeImage_Load(FIF_WEBP, e->file, 0);
        if (!dib) { fail(e->file, "load", "returned NULL"); continue; }

        d = digest(dib);
        icc = icc_len(dib); xmp = meta_len(dib, FIMD_XMP, "XMLPacket");
        exif = meta_len(dib, FIMD_EXIF_RAW, "ExifRaw");

        if (record) {
            printf("    { \"%s\", %3u, %3u, %2u, 0x%016llxULL, %3d, %3d, %4d },\n",
                   e->file, FreeImage_GetWidth(dib), FreeImage_GetHeight(dib),
                   FreeImage_GetBPP(dib), d, icc, xmp, exif);
        } else {
            if (FreeImage_GetWidth(dib) != e->width || FreeImage_GetHeight(dib) != e->height)
                fail(e->file, "geometry", "got %ux%u, expected %ux%u",
                     FreeImage_GetWidth(dib), FreeImage_GetHeight(dib), e->width, e->height);
            if (FreeImage_GetBPP(dib) != e->bpp)
                fail(e->file, "depth", "got %u bpp, expected %u", FreeImage_GetBPP(dib), e->bpp);
            if (d != e->pixels)
                fail(e->file, "pixels", "got %016llx, expected %016llx", d, e->pixels);
            if (icc != e->icc)  fail(e->file, "ICC",  "got %d bytes, expected %d", icc, e->icc);
            if (xmp != e->xmp)  fail(e->file, "XMP",  "got %d bytes, expected %d", xmp, e->xmp);
            if (exif != e->exif) fail(e->file, "EXIF", "got %d bytes, expected %d", exif, e->exif);
            printf("  ok %-38s %ux%u %2u bpp  icc=%-4d xmp=%-4d exif=%d\n",
                   e->file, e->width, e->height, e->bpp, icc, xmp, exif);
        }

        /* a header-only load must agree */
        hdr_only = FreeImage_Load(FIF_WEBP, e->file, FIF_LOAD_NOPIXELS);
        if (!hdr_only) {
            fail(e->file, "header-only", "returned NULL");
        } else {
            if (FreeImage_GetWidth(hdr_only) != FreeImage_GetWidth(dib) ||
                FreeImage_GetHeight(hdr_only) != FreeImage_GetHeight(dib) ||
                FreeImage_GetBPP(hdr_only) != FreeImage_GetBPP(dib))
                fail(e->file, "header-only", "disagrees with the full load");
            FreeImage_Unload(hdr_only);
        }

        /* a memory stream must decode identically */
        raw = slurp(e->file, &len);
        if (!raw) {
            fail(e->file, "read", "could not read the file");
        } else {
            mem = FreeImage_OpenMemory(raw, (DWORD)len);
            from_mem = mem ? FreeImage_LoadFromMemory(FIF_WEBP, mem, 0) : NULL;
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

        FreeImage_Unload(dib);
    }

    if (record) { printf("};\n"); FreeImage_DeInitialise(); return 0; }

    printf("\n%s\n", failures ? "FAILED" : "all files decoded as recorded");
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
