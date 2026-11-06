/* Radiance RGBE saves: negative samples are written as 0, as Radiance's setcolr() writes them */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "FreeImage.h"

static int failures = 0, checks = 0;

int main(void) {
    static const float PIXELS[][3] = {
        { 1.0f, 1.0f, 1.0f },
        { 0.5f, 2.0f, 8.0f },
        { 37.0f, 0.01f, 0.01f },
        { 36.9f, 30.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 1e-40f, 1e-40f, 1e-40f },
        { -0.5f, 1.0f, 1.0f },
        { 37.0f, -16.9f, -2.7f },
        { 0.25f, -0.001f, 0.125f },
        { -1.0f, -2.0f, -3.0f },
    };
    const int n = (int)(sizeof PIXELS / sizeof PIXELS[0]);
    FIBITMAP *dib, *back = NULL;
    FIMEMORY *mem;
    FIRGBF *row;
    int i, c;

    FreeImage_Initialise(FALSE);
    dib = FreeImage_AllocateT(FIT_RGBF, n, 1, 96, 0, 0, 0);
    row = dib ? (FIRGBF *)FreeImage_GetScanLine(dib, 0) : NULL;
    for (i = 0; row && i < n; i++) {
        row[i].red = PIXELS[i][0];
        row[i].green = PIXELS[i][1];
        row[i].blue = PIXELS[i][2];
    }
    mem = FreeImage_OpenMemory(NULL, 0);
    if (dib && mem && FreeImage_SaveToMemory(FIF_HDR, dib, mem, 0)) {
        FreeImage_SeekMemory(mem, 0, SEEK_SET);
        back = FreeImage_LoadFromMemory(FIF_HDR, mem, 0);
    }
    if (!back || FreeImage_GetImageType(back) != FIT_RGBF || FreeImage_GetWidth(back) != (unsigned)n) {
        printf("  FAIL the save or the reload failed\n");
        return 1;
    }
    row = (FIRGBF *)FreeImage_GetScanLine(back, 0);
    for (i = 0; i < n; i++) {
        const float got[3] = { row[i].red, row[i].green, row[i].blue };
        float peak = 0;
        int good = 1;
        for (c = 0; c < 3; c++) {
            if (PIXELS[i][c] > peak) peak = PIXELS[i][c];
        }
        /* the shared exponent leaves 8 bits of mantissa below the largest sample */
        for (c = 0; c < 3; c++) {
            const float want = PIXELS[i][c] > 0 ? PIXELS[i][c] : 0;
            checks++;
            if (fabsf(got[c] - want) > peak / 128 + 1e-6f) good = 0;
        }
        printf("  %s (%g, %g, %g) -> (%g, %g, %g)\n", good ? "ok  " : "FAIL",
               PIXELS[i][0], PIXELS[i][1], PIXELS[i][2], got[0], got[1], got[2]);
        if (!good) failures++;
    }
    FreeImage_Unload(back);
    FreeImage_Unload(dib);
    FreeImage_CloseMemory(mem);
    printf("\n--- %d check(s), %d failure(s) ---\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
