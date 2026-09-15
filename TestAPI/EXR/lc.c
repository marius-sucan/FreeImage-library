/*
 * FreeImage 3 - OpenEXR luminance/chroma (EXR_LC) round-trip test
 *
 * Saves a flat-coloured image with EXR_LC and checks every row of what comes
 * back. EXR_LC writes Imf::WRITE_YC for an RGBF image and Imf::WRITE_YCA for an
 * RGBAF one, and both go back through Imf::RgbaInputFile on the way in, which
 * is a different reader from the one the rest of the suite exercises.
 *
 * It is deliberately not a checksum test. regress.c hashes the whole bitmap,
 * and a hash cannot tell a scanline the reader forgot to write from one it
 * wrote correctly - the zeros FreeImage_AllocateHeaderT left there hash just as
 * stably as pixels do. Two bugs lived behind that until 2026-09-15:
 *
 *   - the chunk loop copied (dw.max.y - dw.min.y) rows out of the
 *     (dw.max.y - dw.min.y + 1) it had read, so the bottom scanline of every
 *     Y/BY/RY image was left black. It is visible in the openexr-images set
 *     too: Chromaticities/Rec709_YC.exr and the four LuminanceChroma files
 *     each lost their bottom row.
 *   - an RGBAF image saved with EXR_LC could not be loaded at all, because only
 *     the three-channel form of the layout was recognised.
 *
 * A flat colour makes both visible: every row must come back as that colour,
 * and the alpha of an RGBAF round-trip must survive as 1.
 *
 * Standalone: build with the Makefile in this directory, run from it.
 * Scratch files go to $EXR_TEST_TMP, or the current directory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

/* the chroma is subsampled and stored as half, so the colour comes back close
 * rather than exact; anything beyond this is a bug, not a rounding difference */
#define TOLERANCE 0.02f

static const float RED = 0.5f, GREEN = 0.25f, BLUE = 0.75f;

static int failures = 0;
static char message[512];

static void collect(FREE_IMAGE_FORMAT fif, const char *msg) {
    (void)fif;
    snprintf(message, sizeof message, "%s", msg ? msg : "");
}

static const char *tmpdir(void) {
    const char *d = getenv("EXR_TEST_TMP");
    return (d && *d) ? d : ".";
}

static int near(float a, float b) { float d = a - b; return (d < 0 ? -d : d) <= TOLERANCE; }

static void run(FREE_IMAGE_TYPE type, const char *type_name, int width, int height, int flags,
                const char *flag_name) {
    char path[512];
    snprintf(path, sizeof path, "%s/fi_exr_lc_%d_%d.exr", tmpdir(), (int)type, height);

    FIBITMAP *src = FreeImage_AllocateT(type, width, height, 0, 0, 0, 0);
    if (!src) { printf("  FAIL allocate %s %dx%d\n", type_name, width, height); failures++; return; }
    for (int y = 0; y < height; y++) {
        void *row = FreeImage_GetScanLine(src, y);
        for (int x = 0; x < width; x++) {
            if (type == FIT_RGBF) {
                FIRGBF *p = &((FIRGBF *)row)[x];
                p->red = RED; p->green = GREEN; p->blue = BLUE;
            } else {
                FIRGBAF *p = &((FIRGBAF *)row)[x];
                p->red = RED; p->green = GREEN; p->blue = BLUE; p->alpha = 1.0f;
            }
        }
    }

    message[0] = 0;
    if (!FreeImage_Save(FIF_EXR, src, path, flags)) {
        printf("  %-9s %-7s %3dx%-3d FAIL save: %s\n", type_name, flag_name, width, height, message);
        failures++; FreeImage_Unload(src); return;
    }
    message[0] = 0;
    FIBITMAP *back = FreeImage_Load(FIF_EXR, path, 0);
    if (!back) {
        printf("  %-9s %-7s %3dx%-3d FAIL reload: %s\n", type_name, flag_name, width, height, message);
        failures++; FreeImage_Unload(src); remove(path); return;
    }

    /* an RGBF source stays RGBF; an RGBAF one has to keep its alpha channel */
    FREE_IMAGE_TYPE want = type;
    FREE_IMAGE_TYPE got = FreeImage_GetImageType(back);
    int bad_rows = 0, bad_alpha = 0, first_bad = -1;
    for (int y = 0; y < height; y++) {
        void *row = FreeImage_GetScanLine(back, y);
        float r, g, b, a = 1.0f;
        if (got == FIT_RGBF) {
            FIRGBF *p = &((FIRGBF *)row)[0]; r = p->red; g = p->green; b = p->blue;
        } else if (got == FIT_RGBAF) {
            FIRGBAF *p = &((FIRGBAF *)row)[0]; r = p->red; g = p->green; b = p->blue; a = p->alpha;
        } else break;
        if (!near(r, RED) || !near(g, GREEN) || !near(b, BLUE)) {
            if (first_bad < 0) first_bad = y;
            bad_rows++;
        }
        if (!near(a, 1.0f)) bad_alpha++;
    }

    int bad = (got != want) || bad_rows || bad_alpha;
    printf("  %-9s %-7s %3dx%-3d -> type=%-2d rows wrong=%d/%d alpha wrong=%d%s\n",
           type_name, flag_name, width, height, (int)got, bad_rows, height, bad_alpha,
           bad ? "   *** FAIL" : "");
    if (bad && first_bad >= 0) printf("      first wrong row: y=%d\n", first_bad);
    if (bad) failures++;

    FreeImage_Unload(back); FreeImage_Unload(src); remove(path);
}

int main(void) {
    FreeImage_Initialise(TRUE);
    FreeImage_SetOutputMessage(collect);

    /* EXR_LC needs even dimensions; heights on both sides of the reader's
     * 16-row chunk boundary, since the bug was in the short final chunk */
    static const int HEIGHTS[] = { 2, 8, 16, 18, 32, 34, 48, 96 };

    printf("EXR_LC round-trip\n");
    for (unsigned i = 0; i < sizeof(HEIGHTS) / sizeof(HEIGHTS[0]); i++) {
        run(FIT_RGBF,  "FIT_RGBF",  32, HEIGHTS[i], EXR_LC, "LC");
        run(FIT_RGBAF, "FIT_RGBAF", 32, HEIGHTS[i], EXR_LC, "LC");
    }
    printf("\nEXR_LC with B44\n");
    for (unsigned i = 0; i < sizeof(HEIGHTS) / sizeof(HEIGHTS[0]); i++) {
        run(FIT_RGBAF, "FIT_RGBAF", 32, HEIGHTS[i], EXR_B44 | EXR_LC, "B44|LC");
    }

    printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
