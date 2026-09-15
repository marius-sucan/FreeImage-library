/*
 * FreeImage 3 - OpenEXR save/reload regression test
 *
 * Round-trips the three pixel types the EXR plugin can export (FIT_FLOAT,
 * FIT_RGBF, FIT_RGBAF) across a range of sizes and every EXR_* save flag,
 * through a file and through a memory stream. Prints the encoded size and the
 * checksum of the reloaded pixels for each combination.
 *
 * The assertions are: every save and every reload must work; the file and the
 * memory stream must give the same pixels; and the lossless flags (DEFAULT,
 * NONE, ZIP, PIZ, and their FLOAT variants) must all reload to the same
 * checksum as each other, since they are the same pixels through different
 * compressors. PXR24, B44 and LC are lossy and only have to reload - which
 * EXR_LC could not do at all until 2026-09-15, see lc.c.
 * Encoded sizes change legitimately when the compressor changes - they moved
 * for ZIP and PXR24 when OpenEXR 3.3 replaced zlib with libdeflate - so they
 * are reported, not asserted. The FAIL lines and the tally are the assertions.
 *
 * Standalone: build with the Makefile in this directory, run from it.
 * Scratch files go to $EXR_TEST_TMP, or the current directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "FreeImage.h"

static int failures = 0, cases = 0;
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

/* A picture whose values survive half-float exactly, so that the lossless
 * flags really are lossless and can be compared against each other. */
static FIBITMAP *make(FREE_IMAGE_TYPE type, int w, int h) {
    FIBITMAP *dib = FreeImage_AllocateT(type, w, h, 0, 0, 0, 0);
    if (!dib) return NULL;
    for (int y = 0; y < h; y++) {
        void *row = FreeImage_GetScanLine(dib, y);
        for (int x = 0; x < w; x++) {
            /* eighths in [0,4): exact in half, in float and after PXR24 */
            float a = (float)((x * 3 + y) % 32) / 8.0f;
            float b = (float)((x + y * 5) % 32) / 8.0f;
            float c = (float)((x * 7 + y * 3) % 32) / 8.0f;
            if (type == FIT_FLOAT)      ((float *)row)[x] = a;
            else if (type == FIT_RGBF) {
                FIRGBF *p = &((FIRGBF *)row)[x];
                p->red = a; p->green = b; p->blue = c;
            } else {
                FIRGBAF *p = &((FIRGBAF *)row)[x];
                p->red = a; p->green = b; p->blue = c; p->alpha = 1.0f;
            }
        }
    }
    return dib;
}

typedef struct { const char *name; int flag; int lossless; } Mode;

static const Mode MODES[] = {
    {"DEFAULT",    EXR_DEFAULT,              1},
    {"NONE",       EXR_NONE,                 1},
    {"ZIP",        EXR_ZIP,                  1},
    {"PIZ",        EXR_PIZ,                  1},
    {"FLOAT",      EXR_FLOAT,                1},
    {"NONE|FLOAT", EXR_NONE  | EXR_FLOAT,    1},
    {"ZIP|FLOAT",  EXR_ZIP   | EXR_FLOAT,    1},
    {"PIZ|FLOAT",  EXR_PIZ   | EXR_FLOAT,    1},
    {"PXR24",      EXR_PXR24,                0},
    {"B44",        EXR_B44,                  0},
    {"LC",         EXR_LC,                   0},
    {"B44|LC",     EXR_B44   | EXR_LC,       0},
};
#define NMODES ((int)(sizeof(MODES) / sizeof(MODES[0])))

static const struct { FREE_IMAGE_TYPE t; const char *n; } TYPES[] = {
    {FIT_FLOAT, "FIT_FLOAT"}, {FIT_RGBF, "FIT_RGBF"}, {FIT_RGBAF, "FIT_RGBAF"},
};
static const struct { int w, h; } SIZES[] = {
    {1, 1}, {1, 17}, {17, 1}, {32, 32}, {63, 31}, {129, 65},
};

static const char *tmpdir(void) {
    const char *d = getenv("EXR_TEST_TMP");
    return (d && *d) ? d : ".";
}

int main(void) {
    FreeImage_Initialise(TRUE);
    FreeImage_SetOutputMessage(collect);

    for (int ti = 0; ti < 3; ti++) {
        for (int si = 0; si < (int)(sizeof(SIZES) / sizeof(SIZES[0])); si++) {
            int w = SIZES[si].w, h = SIZES[si].h;
            FIBITMAP *src = make(TYPES[ti].t, w, h);
            if (!src) { printf("FAIL allocate %s %dx%d\n", TYPES[ti].n, w, h); failures++; continue; }

            /* PluginEXR documents two conditions on EXR_LC: the image must be
             * RGB[A]F, and both dimensions must be even. A refused save is the
             * right answer outside those, so it is not counted as a failure. */
            int lc_ok = (TYPES[ti].t != FIT_FLOAT) && (w % 2 == 0) && (h % 2 == 0);

            printf("\n--- %s %dx%d ---\n", TYPES[ti].n, w, h);
            unsigned long long lossless_sum = 0;
            int have_lossless = 0;

            for (int mi = 0; mi < NMODES; mi++) {
                const Mode *m = &MODES[mi];
                char path[512];
                snprintf(path, sizeof path, "%s/fi_exr_rt_%d_%d_%d.exr", tmpdir(), ti, si, mi);
                cases++;

                message[0] = 0;
                if (!FreeImage_Save(FIF_EXR, src, path, m->flag)) {
                    int known = (m->flag & EXR_LC) && !lc_ok;
                    printf("  %-11s %s: %s\n", m->name,
                           known ? "refused (expected)" : "FAIL save", message);
                    if (!known) failures++;
                    continue;
                }
                FILE *f = fopen(path, "rb");
                long size = 0;
                if (f) { fseek(f, 0, SEEK_END); size = ftell(f); fclose(f); }

                message[0] = 0;
                FIBITMAP *back = FreeImage_Load(FIF_EXR, path, 0);
                if (!back) {
                    printf("  %-11s size=%-8ld FAIL reload: %s\n", m->name, size, message);
                    failures++;
                    remove(path);
                    continue;
                }
                unsigned long long sum = checksum(back);

                /* the same bytes through a memory stream */
                int mem_ok = 0;
                f = fopen(path, "rb");
                if (f) {
                    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
                    BYTE *buf = (BYTE *)malloc(n ? n : 1);
                    if (fread(buf, 1, n, f) == (size_t)n) {
                        FIMEMORY *mem = FreeImage_OpenMemory(buf, (DWORD)n);
                        FIBITMAP *mb = FreeImage_LoadFromMemory(FIF_EXR, mem, 0);
                        mem_ok = mb && checksum(mb) == sum;
                        if (mb) FreeImage_Unload(mb);
                        FreeImage_CloseMemory(mem);
                    }
                    free(buf);
                    fclose(f);
                }

                int bad = !mem_ok;
                if (m->lossless) {
                    if (!have_lossless) { lossless_sum = sum; have_lossless = 1; }
                    else if (sum != lossless_sum) bad = 1;
                }
                printf("  %-11s size=%-8ld %3dx%-3d type=%-2d hash=%016llx mem=%s%s\n",
                       m->name, size, (int)FreeImage_GetWidth(back),
                       (int)FreeImage_GetHeight(back), (int)FreeImage_GetImageType(back),
                       sum, mem_ok ? "ok" : "BAD",
                       bad ? "   *** FAIL" : "");
                if (bad) failures++;

                FreeImage_Unload(back);
                remove(path);
            }
            FreeImage_Unload(src);
        }
    }

    printf("\n%d case%s, %d failure%s\n", cases, cases == 1 ? "" : "s",
           failures, failures == 1 ? "" : "s");
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
