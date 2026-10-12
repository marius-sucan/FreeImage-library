/* FreeImage 3 - JPEG XR pixel formats converted on load: data/ holds 8x140 lossless 64bppRGBHalf, 64bppRGBFixedPoint,
   32bppRGBE and 128bppRGBFloat (loaded as RGBF), 64bppRGBAHalf with planar alpha (RGBAF), each made by jxrlib from
   values exact in either, and 9x140, an odd width, 32bppRGB101010 (RGB16), 32bppBGR and 32bppRGB (24-bit, their
   padding dropped); every container orientation must load the values the file was made from, and a header-only load
   the same type and size */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static unsigned get16(const BYTE *p) { return p[0] | (p[1] << 8); }
static unsigned get32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

/* channel c (R, G, B, A) of upright pixel (x, y) as made, y counted from the top */
static double made(const char *name, unsigned x, unsigned y, unsigned c) {
    const unsigned k = (3 * x + 5 * y + 17 * c) % 97;
    if (!strcmp(name, "rgb101010")) return (double)((k * 8) << 6);
    if (!strcmp(name, "bgr32") || !strcmp(name, "rgb32")) return 2.0 * k;
    /* mantissa 128 + k, exponent 130 + k(alpha) % 4 */
    if (!strcmp(name, "rgbe")) return (128.0 + k) / 64.0 * (1 << ((3 * x + 5 * y + 17 * 3) % 97 % 4));
    return k / 32.0;
}

/* channel c (R, G, B, A) of pixel x of a scanline */
static double loaded(FIBITMAP *dib, const BYTE *line, unsigned x, unsigned c) {
    static const int rgb[3] = { FI_RGBA_RED, FI_RGBA_GREEN, FI_RGBA_BLUE };
    switch (FreeImage_GetImageType(dib)) {
        case FIT_RGB16: return ((const unsigned short *)line)[x * 3 + c];
        case FIT_RGBF: return ((const float *)line)[x * 3 + c];
        case FIT_RGBAF: return ((const float *)line)[x * 4 + c];
        default: return line[x * 3 + rgb[c]];
    }
}

static void formats(const char *dir, const char *name, unsigned w, FREE_IMAGE_TYPE type, unsigned bpp) {
    const unsigned h = 140, channels = (type == FIT_RGBAF) ? 4 : 3;
    char path[1024];
    BYTE *data = NULL, *orient = NULL;
    long size = 0;
    DWORD ifd, k;
    int o;
    FILE *f;
    snprintf(path, sizeof(path), "%s/%s.jxr", dir, name);
    f = fopen(path, "rb");
    CHECK(f != NULL, "%s: missing", path);
    if (!f) return;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = (BYTE *)malloc(size);
    size = (long)fread(data, 1, size, f);
    fclose(f);
    ifd = get32(data + 4);
    for (k = 0; ifd + 2 <= (DWORD)size && k < get16(data + ifd) && ifd + 14 + 12 * k <= (DWORD)size; k++) {
        if (get16(data + ifd + 2 + 12 * k) == 0xbc02) orient = data + ifd + 2 + 12 * k;
    }
    CHECK(orient != NULL, "%s: no orientation", name);
    for (o = 0; orient && o < 8; o++) {
        FIMEMORY *mem;
        FIBITMAP *dib, *hdr;
        unsigned x, y, c, W, H, bad = 0;
        orient[8] = (BYTE)o;
        mem = FreeImage_OpenMemory(data, (DWORD)size);
        dib = FreeImage_LoadFromMemory(FIF_JXR, mem, 0);
        FreeImage_CloseMemory(mem);
        mem = FreeImage_OpenMemory(data, (DWORD)size);
        hdr = FreeImage_LoadFromMemory(FIF_JXR, mem, FIF_LOAD_NOPIXELS);
        FreeImage_CloseMemory(mem);
        W = (o < 4) ? w : h;
        H = (o < 4) ? h : w;
        CHECK(hdr && !FreeImage_HasPixels(hdr) && FreeImage_GetImageType(hdr) == type && FreeImage_GetBPP(hdr) == bpp && FreeImage_GetWidth(hdr) == W && FreeImage_GetHeight(hdr) == H,
              "%s orientation %d: header-only load not %ux%u %u-bit of type %d", name, o, W, H, bpp, (int)type);
        FreeImage_Unload(hdr);
        CHECK(dib && FreeImage_GetImageType(dib) == type && FreeImage_GetBPP(dib) == bpp && FreeImage_GetWidth(dib) == W && FreeImage_GetHeight(dib) == H,
              "%s orientation %d: does not load as %ux%u %u-bit of type %d", name, o, W, H, bpp, (int)type);
        if (dib && FreeImage_GetImageType(dib) == type && FreeImage_GetBPP(dib) == bpp && FreeImage_GetWidth(dib) == W && FreeImage_GetHeight(dib) == H) {
            for (y = 0; y < H; y++) {
                const BYTE *line = FreeImage_GetScanLine(dib, H - 1 - y);
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
                    for (c = 0; c < channels; c++) {
                        if (loaded(dib, line, x, c) != made(name, sx, sy, c)) bad++;
                    }
                }
            }
            CHECK(bad == 0, "%s orientation %d: %u values not as made", name, o, bad);
        }
        FreeImage_Unload(dib);
    }
    free(data);
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : "data";
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);
    formats(dir, "rgb64half", 8, FIT_RGBF, 96);
    formats(dir, "rgb64fixed", 8, FIT_RGBF, 96);
    formats(dir, "rgba64half", 8, FIT_RGBAF, 128);
    formats(dir, "rgb101010", 9, FIT_RGB16, 48);
    formats(dir, "rgbe", 8, FIT_RGBF, 96);
    formats(dir, "rgbf128", 8, FIT_RGBF, 96);
    formats(dir, "bgr32", 9, FIT_BITMAP, 24);
    formats(dir, "rgb32", 9, FIT_BITMAP, 24);
    printf("%d checks, %d failures\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
