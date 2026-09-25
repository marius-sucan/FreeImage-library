/* FreeImage 3 - I/O test: memory streams of 2 GB and more */
/* A 2.5 GB wrapped buffer costs no RAM until touched; --grow writes past 2 GB into a stream of FreeImage's own (2 GB of RAM). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

#define WRAPPED ((size_t)0xA0000000u)   /* 2.5 GB */

static int failures = 0;

static void report(const char *what, int ok) {
    printf("  %-52s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

static BYTE pixel(int x, int y, int c) {
    return (BYTE)((x * 5 + y * 11 + c * 37) & 0xFF);
}

static int check(FIBITMAP *dib) {
    int x, y;
    if (!dib || FreeImage_GetWidth(dib) != 64 || FreeImage_GetHeight(dib) != 64 || FreeImage_GetBPP(dib) != 24) return 0;
    for (y = 0; y < 64; y++) {
        const BYTE *row = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < 64; x++) {
            if (row[x * 3] != pixel(x, y, 0) || row[x * 3 + 1] != pixel(x, y, 1) || row[x * 3 + 2] != pixel(x, y, 2)) return 0;
        }
    }
    return 1;
}

int main(int argc, char **argv) {
    const int grow = (argc > 1) && (strcmp(argv[1], "--grow") == 0);
    FIBITMAP *dib;
    FIMEMORY *png, *mem;
    BYTE *bytes = NULL, *buffer;
    DWORD png_size = 0, size = 0;
    int x, y;

    FreeImage_Initialise(FALSE);

    /* a small PNG */
    dib = FreeImage_Allocate(64, 64, 24, 0, 0, 0);
    for (y = 0; y < 64; y++) {
        BYTE *row = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < 64; x++) { row[x * 3] = pixel(x, y, 0); row[x * 3 + 1] = pixel(x, y, 1); row[x * 3 + 2] = pixel(x, y, 2); }
    }
    png = FreeImage_OpenMemory(NULL, 0);
    FreeImage_SaveToMemory(FIF_PNG, dib, png, 0);
    FreeImage_AcquireMemory(png, &bytes, &png_size);
    FreeImage_Unload(dib);

    /* 1. a wrapped buffer of 2.5 GB, the PNG at its start */
    printf("wrapped buffer of %.2f GB\n", (double)WRAPPED / (1024.0 * 1024.0 * 1024.0));
    buffer = (BYTE *)calloc(WRAPPED, 1);
    if (!buffer) { printf("cannot reserve the buffer\n"); return 1; }
    memcpy(buffer, bytes, png_size);
    FreeImage_CloseMemory(png);

    mem = FreeImage_OpenMemory(buffer, (DWORD)WRAPPED);
    report("FreeImage_GetFileTypeFromMemory -> PNG", FreeImage_GetFileTypeFromMemory(mem, 0) == FIF_PNG);
    dib = FreeImage_LoadFromMemory(FIF_PNG, mem, 0);
    report("FreeImage_LoadFromMemory, pixels", check(dib));
    if (dib) FreeImage_Unload(dib);
    report("FreeImage_AcquireMemory reports 2.5 GB", FreeImage_AcquireMemory(mem, &bytes, &size) && size == (DWORD)WRAPPED && bytes == buffer);
    {
        BYTE tail[16];
        memset(tail, 0xAA, sizeof(tail));
        report("seek to 2.5 GB - 16", FreeImage_SeekMemory(mem, -16, SEEK_END));
        report("read the last 16 bytes", FreeImage_ReadMemory(tail, 1, 16, mem) == 16 && tail[0] == 0 && tail[15] == 0);
        report("read at the end -> 0", FreeImage_ReadMemory(tail, 1, 16, mem) == 0);
    }
    FreeImage_CloseMemory(mem);
    free(buffer);

    /* 2. a stream of FreeImage's own, grown past 2 GB */
    if (grow) {
        static const BYTE mark[64] = { 1, 2, 3, 4, 5, 6, 7, 8 };
        BYTE back[64];
        const long at = 0x7FFFFFF0L;
        printf("growing a stream past 2 GB\n");
        mem = FreeImage_OpenMemory(NULL, 0);
        report("seek to 2 GB - 16", FreeImage_SeekMemory(mem, at, SEEK_SET));
        report("write 64 bytes there", FreeImage_WriteMemory(mark, 1, 64, mem) == 64);
        report("FreeImage_AcquireMemory reports 2 GB + 48", FreeImage_AcquireMemory(mem, &bytes, &size) && size == (DWORD)0x80000030u);
        report("the gap reads as zeros", bytes && bytes[0] == 0 && bytes[at - 1] == 0);
        report("seek back and read the bytes", FreeImage_SeekMemory(mem, at, SEEK_SET) && FreeImage_ReadMemory(back, 1, 64, mem) == 64 && memcmp(back, mark, 64) == 0);
        FreeImage_CloseMemory(mem);
    }

    FreeImage_DeInitialise();
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
