/* FreeImage 3 - JPEG XR pixel formats jxrlib converts on load: data/ holds 8x140 lossless 64bppRGBHalf and
   64bppRGBFixedPoint (loaded as RGBF) and 64bppRGBAHalf with planar alpha (RGBAF), each made by jxrlib from values
   exact in either, and 9x140 32bppRGB101010 (RGB16), an odd width; every container orientation must load the values
   the file was made from */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static unsigned get16(const BYTE *p) { return p[0] | (p[1] << 8); }
static unsigned get32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }

/* channel c of upright pixel (x, y), y counted from the top: k / 32 as a float, or 8k in 10 bits as RGB16 */
static unsigned made_k(unsigned x, unsigned y, unsigned c) {
    return (3 * x + 5 * y + 17 * c) % 97;
}

static void formats(const char *dir, const char *name, unsigned w, FREE_IMAGE_TYPE type) {
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
        FIBITMAP *dib;
        unsigned x, y, c, W, H, bad = 0;
        orient[8] = (BYTE)o;
        mem = FreeImage_OpenMemory(data, (DWORD)size);
        dib = FreeImage_LoadFromMemory(FIF_JXR, mem, 0);
        FreeImage_CloseMemory(mem);
        W = (o < 4) ? w : h;
        H = (o < 4) ? h : w;
        CHECK(dib && FreeImage_GetImageType(dib) == type && FreeImage_GetWidth(dib) == W && FreeImage_GetHeight(dib) == H,
              "%s orientation %d: does not load as %ux%u of type %d", name, o, W, H, (int)type);
        if (dib && FreeImage_GetImageType(dib) == type && FreeImage_GetWidth(dib) == W && FreeImage_GetHeight(dib) == H) {
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
                        const unsigned k = made_k(sx, sy, c);
                        if (type == FIT_RGB16) {
                            if (((const unsigned short *)line)[x * 3 + c] != (unsigned short)((k * 8) << 6)) bad++;
                        } else if (((const float *)line)[x * channels + c] != (float)k / 32.0f) {
                            bad++;
                        }
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
    formats(dir, "rgb64half", 8, FIT_RGBF);
    formats(dir, "rgb64fixed", 8, FIT_RGBF);
    formats(dir, "rgba64half", 8, FIT_RGBAF);
    formats(dir, "rgb101010", 9, FIT_RGB16);
    printf("%d checks, %d failures\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
