/*
 * FreeImage 3 - OpenEXR decode regression test
 *
 * Loads every file of the corpus in data/ and checks what the plugin and the
 * bundled OpenEXR are jointly responsible for: format detection, the bitmap
 * type and depth picked for each channel layout, the decoded pixels, and that
 * a memory stream and a header-only load agree with the file. One line per
 * file; anything that deviates from the expected table prints "*** MISMATCH"
 * and the program exits non-zero.
 *
 * The corpus is generated (see README.md) and deliberately covers what
 * FreeImage itself never writes: RLE, ZIPS, B44A, DWAA and DWAB compression,
 * tiled and mipmapped layouts, and 32-bit float channels - plus the subsampled
 * luminance/chroma layout, which it does write but long could not read.
 *
 * Standalone: build with the Makefile in this directory, run from it.
 * "./decode --record" prints the table above in source form, for when a
 * deliberate change makes the recorded values move.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

typedef struct {
    const char *file;
    int width, height;
    FREE_IMAGE_TYPE type;
    int bpp;
    unsigned long long sum;     /* checksum of the decoded pixel rows */
} Expected;

/* Recorded from a passing run against OpenEXR 3.3.14.  The lossless entries -
 * none, rle, zips, zip, piz, pxr24, tiled and mipmap - all carry the same
 * checksum: they are one picture through eight different compressors and
 * layouts, so any of them drifting alone points at that codec.  b44/b44a and
 * dwaa/dwab are lossy and share a checksum per pair.  Every value here also
 * held for the OpenEXR 3.1.3 the library shipped before, except dwaa/dwab,
 * which 3.3 reimplemented in C and which moved by one or two half-float ULP
 * in a handful of pixels.
 *
 * A width of 0 would mean the file must be *refused*; no entry uses it now.
 * fi_exr_yc.exr is the subsampled luminance/chroma file (OpenEXR's WRITE_YCA,
 * which is what FreeImage's own EXR_LC save flag writes for an RGBAF image).
 * Until 2026-09-15 PluginEXR recognised only the three-channel Y/BY/RY form and
 * rejected this one with "Unsupported color model: A/BY/RY/Y", so FreeImage
 * could write EXR files it could not read back; it now loads as FIT_RGBAF
 * through Imf::RgbaInputFile, alpha included.  Its checksum stands alone
 * because subsampled chroma is lossy. */
static const Expected EXPECTED[] = {
    {"fi_exr_b44.exr",     64, 48, FIT_RGBAF, 128, 0xeb11267f4ec83547ULL},
    {"fi_exr_b44a.exr",    64, 48, FIT_RGBAF, 128, 0xeb11267f4ec83547ULL},
    {"fi_exr_dwaa.exr",    64, 48, FIT_RGBAF, 128, 0x34d24ce328dd6445ULL},
    {"fi_exr_dwab.exr",    64, 48, FIT_RGBAF, 128, 0x34d24ce328dd6445ULL},
    {"fi_exr_float.exr",   64, 48, FIT_RGBF,   96, 0x364325a75bb2aa46ULL},
    {"fi_exr_mipmap.exr",  64, 48, FIT_RGBAF, 128, 0x96dba6846837593eULL},
    {"fi_exr_none.exr",    64, 48, FIT_RGBAF, 128, 0x96dba6846837593eULL},
    {"fi_exr_offset.exr",  64, 48, FIT_RGBAF, 128, 0x96dba6846837593eULL},
    {"fi_exr_piz.exr",     64, 48, FIT_RGBAF, 128, 0x96dba6846837593eULL},
    {"fi_exr_pxr24.exr",   64, 48, FIT_RGBAF, 128, 0x96dba6846837593eULL},
    {"fi_exr_rle.exr",     64, 48, FIT_RGBAF, 128, 0x96dba6846837593eULL},
    {"fi_exr_tiled.exr",   64, 48, FIT_RGBAF, 128, 0x96dba6846837593eULL},
    {"fi_exr_yc.exr",      64, 48, FIT_RGBAF, 128, 0x79a3dc1c1e10ee85ULL},
    {"fi_exr_zip.exr",     64, 48, FIT_RGBAF, 128, 0x96dba6846837593eULL},
    {"fi_exr_zips.exr",    64, 48, FIT_RGBAF, 128, 0x96dba6846837593eULL},
};
#define NEXPECTED ((int)(sizeof(EXPECTED) / sizeof(EXPECTED[0])))

static int failures = 0;
static char message[512];

static void collect(FREE_IMAGE_FORMAT fif, const char *msg) {
    (void)fif;
    snprintf(message, sizeof message, "%s", msg ? msg : "");
}

static unsigned long long checksum(FIBITMAP *dib) {
    unsigned long long h = 1469598103934665603ULL;
    unsigned height = FreeImage_GetHeight(dib), line = FreeImage_GetLine(dib);
    for (unsigned y = 0; y < height; y++) {
        const unsigned char *p = (const unsigned char *)FreeImage_GetScanLine(dib, y);
        for (unsigned i = 0; i < line; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    }
    return h;
}

static BYTE *slurp(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    BYTE *buf = (BYTE *)malloc(*len ? *len : 1);
    if (fread(buf, 1, *len, f) != (size_t)*len) { free(buf); fclose(f); return NULL; }
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    int record = (argc > 1 && strcmp(argv[1], "--record") == 0);

    FreeImage_Initialise(TRUE);
    FreeImage_SetOutputMessage(collect);

    if (record) printf("static const Expected EXPECTED[] = {\n");

    for (int i = 0; i < NEXPECTED; i++) {
        const Expected *e = &EXPECTED[i];
        char path[512];
        snprintf(path, sizeof path, "data/%s", e->file);

        /* format detection must not need the extension */
        FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(path, 0);

        message[0] = 0;
        FIBITMAP *dib = FreeImage_Load(FIF_EXR, path, 0);
        if (!dib) {
            int want = (e->width == 0);     /* 0 = the file must be refused */
            printf("%-20s refused: %-36s%s\n", e->file, message,
                   want ? "" : "   *** MISMATCH");
            if (!want) failures++;
            continue;
        }
        int w = (int)FreeImage_GetWidth(dib), h = (int)FreeImage_GetHeight(dib);
        int bpp = (int)FreeImage_GetBPP(dib);
        FREE_IMAGE_TYPE type = FreeImage_GetImageType(dib);
        unsigned long long sum = checksum(dib);

        if (!record && e->width == 0) {
            printf("%-20s loaded, but the table says it must be refused"
                   "   *** MISMATCH\n", e->file);
            failures++;
            FreeImage_Unload(dib);
            continue;
        }

        if (record) {
            printf("    {\"%s\",%*s%3d, %2d, %-10s %3d, 0x%016llxULL},\n",
                   e->file, (int)(18 - strlen(e->file)), "", w, h,
                   type == FIT_RGBAF ? "FIT_RGBAF," : type == FIT_RGBF ? "FIT_RGBF," : "FIT_?,",
                   bpp, sum);
            FreeImage_Unload(dib);
            continue;
        }

        /* the same file through a memory stream */
        long len = 0;
        BYTE *buf = slurp(path, &len);
        FIMEMORY *mem = buf ? FreeImage_OpenMemory(buf, (DWORD)len) : NULL;
        /* before the load: loading leaves the stream at its end */
        FREE_IMAGE_FORMAT mfif = mem ? FreeImage_GetFileTypeFromMemory(mem, 0) : FIF_UNKNOWN;
        FIBITMAP *m = mem ? FreeImage_LoadFromMemory(FIF_EXR, mem, 0) : NULL;
        int mem_ok = m && checksum(m) == sum &&
                     FreeImage_GetWidth(m) == (unsigned)w &&
                     FreeImage_GetHeight(m) == (unsigned)h;

        /* header-only: geometry and type without the pixels */
        FIBITMAP *hdr = FreeImage_Load(FIF_EXR, path, FIF_LOAD_NOPIXELS);
        int hdr_ok = hdr && FreeImage_GetWidth(hdr) == (unsigned)w &&
                     FreeImage_GetHeight(hdr) == (unsigned)h &&
                     FreeImage_GetImageType(hdr) == type;

        int bad = (w != e->width || h != e->height || type != e->type ||
                   bpp != e->bpp || sum != e->sum ||
                   fif != FIF_EXR || mfif != FIF_EXR || !mem_ok || !hdr_ok);

        printf("%-20s %3dx%-3d type=%-2d bpp=%3d hash=%016llx mem=%s hdr=%s%s\n",
               e->file, w, h, (int)type, bpp, sum,
               mem_ok ? "ok" : "BAD", hdr_ok ? "ok" : "BAD",
               bad ? "   *** MISMATCH" : "");
        if (bad) failures++;

        if (m) FreeImage_Unload(m);
        if (hdr) FreeImage_Unload(hdr);
        if (mem) FreeImage_CloseMemory(mem);
        free(buf);
        FreeImage_Unload(dib);
    }

    if (record) { printf("};\n"); FreeImage_DeInitialise(); return 0; }

    printf("\n%d file%s, %d failure%s\n", NEXPECTED, NEXPECTED == 1 ? "" : "s",
           failures, failures == 1 ? "" : "s");
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
