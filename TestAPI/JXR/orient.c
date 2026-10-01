/* FreeImage 3 - JPEG XR orientation: each pixel format saved losslessly, its container orientation set to each of the
   8 values; the image must load as the upright one flipped, or turned a quarter clockwise and then flipped, with
   width, height and resolution swapped on a quarter turn, in the header-only load too */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static unsigned get16(const BYTE *p) { return p[0] | (p[1] << 8); }
static unsigned get32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

static FIBITMAP *picture(FREE_IMAGE_TYPE type, unsigned bpp, unsigned w, unsigned h) {
    FIBITMAP *dib = FreeImage_AllocateT(type, w, h, bpp, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
    unsigned x, y;
    for (y = 0; y < h; y++) {
        BYTE *line = FreeImage_GetScanLine(dib, y);
        if (type == FIT_FLOAT || type == FIT_RGBF || type == FIT_RGBAF) {
            float *f = (float *)line;
            for (x = 0; x < FreeImage_GetLine(dib) / 4; x++) f[x] = (float)((x * 7 + y * 13) % 101) / 50.0f;
        } else {
            for (x = 0; x < FreeImage_GetLine(dib); x++) line[x] = (BYTE)(x * 7 + y * 13 + ((x * y) >> 2));
        }
    }
    if (bpp <= 8) {
        RGBQUAD *pal = FreeImage_GetPalette(dib);
        int i, colors = 1 << bpp;
        for (i = 0; i < colors; i++) pal[i].rgbRed = pal[i].rgbGreen = pal[i].rgbBlue = (BYTE)(i * 255 / (colors - 1));
    }
    FreeImage_SetDotsPerMeterX(dib, 2835);
    FreeImage_SetDotsPerMeterY(dib, 11811);
    return dib;
}

/* pixel (x, y) of a and (bx, by) of b, y counted from the top, alike */
static int same_px(FIBITMAP *a, unsigned x, unsigned y, FIBITMAP *b, unsigned bx, unsigned by) {
    const unsigned bpp = FreeImage_GetBPP(a);
    const BYTE *pa = FreeImage_GetScanLine(a, FreeImage_GetHeight(a) - 1 - y);
    const BYTE *pb = FreeImage_GetScanLine(b, FreeImage_GetHeight(b) - 1 - by);
    if (bpp == 1) return !!(pa[x >> 3] & (0x80 >> (x & 7))) == !!(pb[bx >> 3] & (0x80 >> (bx & 7)));
    return !memcmp(pa + x * (bpp / 8), pb + bx * (bpp / 8), bpp / 8);
}

static FIBITMAP *load(BYTE *data, DWORD size, int flags) {
    FIMEMORY *mem = FreeImage_OpenMemory(data, size);
    FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_JXR, mem, flags);
    FreeImage_CloseMemory(mem);
    return dib;
}

static void orientations(const char *name, FREE_IMAGE_TYPE type, unsigned bpp, unsigned w, unsigned h) {
    FIBITMAP *src = picture(type, bpp, w, h), *up = NULL;
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    BYTE *data = NULL, *orient = NULL, *p;
    DWORD size = 0, ifd, k;
    int o;
    if (FreeImage_SaveToMemory(FIF_JXR, src, mem, JXR_LOSSLESS) && FreeImage_AcquireMemory(mem, &p, &size)) {
        data = (BYTE *)malloc(size);
        memcpy(data, p, size);
        ifd = get32(data + 4);
        for (k = 0; ifd + 2 <= size && k < get16(data + ifd) && ifd + 14 + 12 * k <= size; k++) {
            if (get16(data + ifd + 2 + 12 * k) == 0xbc02) orient = data + ifd + 2 + 12 * k;
        }
    }
    FreeImage_CloseMemory(mem);
    FreeImage_Unload(src);
    CHECK(orient && get16(orient + 2) == 4 && get32(orient + 4) == 1, "%s: saved without a one-LONG orientation", name);
    for (o = 0; orient && o < 8; o++) {
        FIBITMAP *dib, *hdr;
        unsigned x, y, W, H, bad = 0;
        orient[8] = (BYTE)o;
        dib = load(data, size, 0);
        hdr = load(data, size, FIF_LOAD_NOPIXELS);
        CHECK(dib != NULL && hdr != NULL, "%s orientation %d: does not load", name, o);
        if (dib && hdr && o == 0) {
            up = dib;
        } else if (dib && hdr && up) {
            W = (o < 4) ? w : h;
            H = (o < 4) ? h : w;
            CHECK(FreeImage_GetWidth(dib) == W && FreeImage_GetHeight(dib) == H && FreeImage_GetWidth(hdr) == W && FreeImage_GetHeight(hdr) == H,
                  "%s orientation %d: %ux%u (header %ux%u), not %ux%u", name, o, FreeImage_GetWidth(dib), FreeImage_GetHeight(dib),
                  FreeImage_GetWidth(hdr), FreeImage_GetHeight(hdr), W, H);
            CHECK(FreeImage_GetDotsPerMeterX(dib) == ((o < 4) ? 2835u : 11811u) && FreeImage_GetDotsPerMeterY(dib) == ((o < 4) ? 11811u : 2835u),
                  "%s orientation %d: resolution %u x %u", name, o, FreeImage_GetDotsPerMeterX(dib), FreeImage_GetDotsPerMeterY(dib));
            if (FreeImage_GetWidth(dib) == W && FreeImage_GetHeight(dib) == H) {
                for (y = 0; y < H; y++) {
                    for (x = 0; x < W; x++) {
                        /* the upright pixel this one comes from: flips alone, or the quarter turn (x, y) -> (h - 1 - y, x) first */
                        unsigned sx, sy;
                        if (o < 4) {
                            sx = (o == 2 || o == 3) ? w - 1 - x : x;
                            sy = (o == 1 || o == 3) ? h - 1 - y : y;
                        } else {
                            sx = (o == 4 || o == 6) ? y : w - 1 - y;
                            sy = (o == 4 || o == 5) ? h - 1 - x : x;
                        }
                        if (!same_px(dib, x, y, up, sx, sy)) bad++;
                    }
                }
                CHECK(bad == 0, "%s orientation %d: %u pixels out of place", name, o, bad);
            }
        }
        if (dib != up) FreeImage_Unload(dib);
        FreeImage_Unload(hdr);
    }
    FreeImage_Unload(up);
    free(data);
}

int main(void) {
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);
    orientations("1-bit", FIT_BITMAP, 1, 40, 24);
    orientations("8-bit grey", FIT_BITMAP, 8, 40, 24);
    orientations("16-bit 555", FIT_BITMAP, 16, 40, 24);
    orientations("24-bit", FIT_BITMAP, 24, 40, 24);
    orientations("32-bit", FIT_BITMAP, 32, 40, 24);
    orientations("uint16", FIT_UINT16, 16, 40, 24);
    orientations("rgb16", FIT_RGB16, 48, 40, 24);
    orientations("rgba16", FIT_RGBA16, 64, 40, 24);
    orientations("float", FIT_FLOAT, 32, 40, 24);
    orientations("rgbf", FIT_RGBF, 96, 40, 24);
    orientations("rgbaf", FIT_RGBAF, 128, 40, 24);
    orientations("24-bit 17x5", FIT_BITMAP, 24, 17, 5);
    orientations("1-bit 131x77", FIT_BITMAP, 1, 131, 77);
    orientations("24-bit 130x70", FIT_BITMAP, 24, 130, 70);
    orientations("32-bit 1x1", FIT_BITMAP, 32, 1, 1);
    printf("%d checks, %d failures\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
