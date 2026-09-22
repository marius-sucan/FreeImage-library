/* FreeImage 3 - OpenEXR robustness test */
/* damaged input may load or be refused, never crash; run under ASan */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static const char *FILES[] = {
    "data/fi_exr_zip.exr",      /* scanline, the common case  */
    "data/fi_exr_piz.exr",      /* the wavelet + huffman path */
    "data/fi_exr_dwaa.exr",     /* the DWA path               */
    "data/fi_exr_b44.exr",      /* the B44 path               */
    "data/fi_exr_tiled.exr",    /* the tiled reader           */
    "data/fi_exr_float.exr",    /* 32-bit channels            */
};
#define NFILES ((int)(sizeof(FILES) / sizeof(FILES[0])))

static long loaded = 0, refused = 0;

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static int try_load(const BYTE *data, long len) {
    FIMEMORY *mem = FreeImage_OpenMemory((BYTE *)data, (DWORD)len);
    if (!mem) return 0;
    FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_EXR, mem, 0);
    int ok = dib != NULL;
    if (dib) {
        /* touch every row: a short buffer faults here */
        unsigned h = FreeImage_GetHeight(dib), line = FreeImage_GetLine(dib);
        unsigned long long acc = 0;
        for (unsigned y = 0; y < h; y++) {
            const unsigned char *p = (const unsigned char *)FreeImage_GetScanLine(dib, y);
            for (unsigned i = 0; i < line; i += 64) acc += p[i];
        }
        (void)acc;
        FreeImage_Unload(dib);
    }
    FreeImage_CloseMemory(mem);
    if (ok) loaded++; else refused++;
    return ok;
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

int main(void) {
    FreeImage_Initialise(TRUE);
    FreeImage_SetOutputMessage(quiet);

    /* degenerate buffers */
    {
        static const BYTE magic[] = {0x76, 0x2f, 0x31, 0x01};
        try_load((const BYTE *)"", 0);
        try_load(magic, 1);
        try_load(magic, 4);
        try_load((const BYTE *)"not an exr file at all, not even close", 38);
        printf("degenerate buffers: survived\n");
    }

    for (int fi = 0; fi < NFILES; fi++) {
        long len = 0;
        BYTE *orig = slurp(FILES[fi], &len);
        if (!orig) { printf("%-24s MISSING\n", FILES[fi]); continue; }
        long before = loaded + refused;

        /* 1. truncated prefixes */
        for (long n = 0; n < len; n += (n < 512 ? 7 : len / 64 + 1))
            try_load(orig, n);

        /* 2. junk appended */
        {
            BYTE *big = (BYTE *)malloc(len + 4096);
            memcpy(big, orig, len);
            for (long i = 0; i < 4096; i++) big[len + i] = (BYTE)(i * 37 + 11);
            try_load(big, len + 4096);
            free(big);
        }

        /* 3. single-byte corruptions */
        {
            BYTE *copy = (BYTE *)malloc(len);
            for (long i = 0; i < len; i += 13) {
                memcpy(copy, orig, len);
                copy[i] ^= 0xFF;
                try_load(copy, len);
            }
            /* and the length fields a parser is most likely to trust */
            for (long i = 4; i < (len < 400 ? len : 400); i += 3) {
                memcpy(copy, orig, len);
                copy[i] = 0xFF; if (i + 1 < len) copy[i + 1] = 0xFF;
                if (i + 2 < len) copy[i + 2] = 0xFF; if (i + 3 < len) copy[i + 3] = 0x7F;
                try_load(copy, len);
            }
            free(copy);
        }

        /* 4. wiped 64-byte regions */
        {
            BYTE *copy = (BYTE *)malloc(len);
            for (long i = 0; i < len; i += 64) {
                memcpy(copy, orig, len);
                long n = (len - i < 64) ? len - i : 64;
                memset(copy + i, 0, n);
                try_load(copy, len);
            }
            free(copy);
        }

        printf("%-24s %ld damaged variants, survived\n",
               FILES[fi], loaded + refused - before);
        free(orig);
    }

    printf("\n%ld loads attempted: %ld decoded, %ld refused, 0 crashes\n",
           loaded + refused, loaded, refused);
    FreeImage_DeInitialise();
    return 0;
}
