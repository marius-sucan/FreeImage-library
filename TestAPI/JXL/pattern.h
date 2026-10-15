/* the test patterns of mkdata.c, shared with the tests that regenerate them */
#ifndef JXL_TEST_PATTERN_H
#define JXL_TEST_PATTERN_H

#include <math.h>
#include "FreeImage.h"

static double wave(unsigned x, unsigned y, unsigned w, unsigned h, int c, int seed) {
    double v = 0.5 + 0.45 * sin((x + seed * 7) * (0.11 + 0.05 * c)) * cos((y + seed * 3) * (0.07 + 0.03 * c));
    return v + 0.04 * ((double)x / w - (double)y / h);
}

/* a smooth test pattern of any exportable type */
static FIBITMAP *pattern(FREE_IMAGE_TYPE type, unsigned bpp, unsigned w, unsigned h, int seed) {
    FIBITMAP *d = (type == FIT_BITMAP) ? FreeImage_Allocate(w, h, bpp, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK)
                                       : FreeImage_AllocateT(type, w, h, bpp, 0, 0, 0);
    unsigned x, y; int c;
    if (type == FIT_BITMAP && bpp == 8) {
        RGBQUAD *pal = FreeImage_GetPalette(d);
        for (x = 0; x < 256; x++) pal[x].rgbRed = pal[x].rgbGreen = pal[x].rgbBlue = (BYTE)x;
    }
    for (y = 0; y < h; y++) {
        BYTE *row = FreeImage_GetScanLine(d, h - 1 - y);
        for (x = 0; x < w; x++) {
            double v[4];
            for (c = 0; c < 4; c++) v[c] = wave(x, y, w, h, c, seed);
            v[3] = ((x / 8 + y / 8) % 3 == 0) ? 0.0 : (x % 16 < 8 ? 1.0 : 0.5);
            for (c = 0; c < 4; c++) { if (v[c] < 0) v[c] = 0; if (v[c] > 1 && c == 3) v[c] = 1; }
            switch (type) {
                case FIT_BITMAP:
                    if (bpp == 8) row[x] = (BYTE)(fmin(v[0], 1) * 255 + 0.5);
                    else {
                        BYTE *p = row + x * (bpp / 8);
                        p[FI_RGBA_RED] = (BYTE)(fmin(v[0], 1) * 255 + 0.5);
                        p[FI_RGBA_GREEN] = (BYTE)(fmin(v[1], 1) * 255 + 0.5);
                        p[FI_RGBA_BLUE] = (BYTE)(fmin(v[2], 1) * 255 + 0.5);
                        if (bpp == 32) p[FI_RGBA_ALPHA] = (BYTE)(v[3] * 255 + 0.5);
                    }
                    break;
                case FIT_UINT16: ((WORD *)row)[x] = (WORD)(fmin(v[0], 1) * 65535 + 0.5); break;
                case FIT_RGB16: { FIRGB16 *p = (FIRGB16 *)row + x; p->red = (WORD)(fmin(v[0], 1) * 65535); p->green = (WORD)(fmin(v[1], 1) * 65535); p->blue = (WORD)(fmin(v[2], 1) * 65535); break; }
                case FIT_RGBA16: { FIRGBA16 *p = (FIRGBA16 *)row + x; p->red = (WORD)(fmin(v[0], 1) * 65535); p->green = (WORD)(fmin(v[1], 1) * 65535); p->blue = (WORD)(fmin(v[2], 1) * 65535); p->alpha = (WORD)(v[3] * 65535); break; }
                case FIT_FLOAT: ((float *)row)[x] = (float)(v[0] * 2); break;
                case FIT_RGBF: { FIRGBF *p = (FIRGBF *)row + x; p->red = (float)(v[0] * 2); p->green = (float)v[1]; p->blue = (float)v[2]; break; }
                case FIT_RGBAF: { FIRGBAF *p = (FIRGBAF *)row + x; p->red = (float)(v[0] * 2); p->green = (float)v[1]; p->blue = (float)v[2]; p->alpha = (float)v[3]; break; }
                default: break;
            }
        }
    }
    return d;
}

/* CMYK inks of 8 or 16 bits in bytes or words 0-3, 0 meaning no ink, flagged CMYK; icc may be NULL */
static FIBITMAP *cmyk_pattern(FREE_IMAGE_TYPE type, unsigned w, unsigned h, int seed, const void *icc, long icc_size) {
    FIBITMAP *d = (type == FIT_BITMAP) ? FreeImage_Allocate(w, h, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK)
                                       : FreeImage_AllocateT(FIT_RGBA16, w, h, 64, 0, 0, 0);
    unsigned x, y; int c;
    for (y = 0; y < h; y++) {
        BYTE *row = FreeImage_GetScanLine(d, h - 1 - y);
        for (x = 0; x < w; x++) for (c = 0; c < 4; c++) {
            double v = wave(x, y, w, h, c, seed) * (c == 3 ? 0.6 : 1.0);
            v = v < 0 ? 0 : v > 1 ? 1 : v;
            if (type == FIT_BITMAP) row[4 * x + c] = (BYTE)(v * 255 + 0.5);
            else ((WORD *)row)[4 * x + c] = (WORD)(v * 65535 + 0.5);
        }
    }
    if (icc) FreeImage_CreateICCProfile(d, (void *)icc, icc_size);
    FreeImage_GetICCProfile(d)->flags |= FIICC_COLOR_IS_CMYK;
    return d;
}

/* the alpha of the CMYK + alpha pattern, never 0: lossy encoders keep the colours under it */
static BYTE cmyk_alpha(unsigned x, unsigned y) {
    return (BYTE)(64 + ((x / 5 + y / 5) % 3) * 95);
}

#endif
