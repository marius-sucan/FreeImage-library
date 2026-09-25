/* FreeImage 3 - I/O test: a save that crosses 2 GB */
/* 47000 x 47000 8-bit, uncompressed TIFF: 2.2 GB of RAM and of disk, the IFD lands past 2 GB. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

#ifdef _WIN32
#define fi_fseek64(f, o, w) _fseeki64((f), (o), (w))
#define fi_ftell64(f) _ftelli64(f)
#else
#define fi_fseek64(f, o, w) fseeko((f), (off_t)(o), (w))
#define fi_ftell64(f) ((INT64)ftello(f))
#endif

#define W 47000
#define H 47000

static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("IO_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

static int failures = 0;

static void report(const char *what, int ok) {
    printf("  %-52s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

static BYTE pixel(int x, int y) {
    return (BYTE)((x + y * 3) & 0xFF);
}

int main(int argc, char **argv) {
    const int full = (argc > 1) && (strcmp(argv[1], "--full") == 0);
    const char *path = tmppath("fi_io_bigsave.tif");
    FIBITMAP *dib;
    FILE *f;
    INT64 size = -1;
    int x, y;

    FreeImage_Initialise(FALSE);
    printf("%d x %d 8-bit, saved uncompressed: %.2f GB\n", W, H, (double)W * H / (1024.0 * 1024.0 * 1024.0));

    dib = FreeImage_Allocate(W, H, 8, 0, 0, 0);
    if (!dib) { printf("cannot allocate the image\n"); return 1; }
    for (y = 0; y < H; y++) {
        BYTE *row = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < W; x++) row[x] = pixel(x, y);
    }

    report("FreeImage_Save", FreeImage_Save(FIF_TIFF, dib, path, TIFF_NONE));
    FreeImage_Unload(dib);

    f = fopen(path, "rb");
    if (f) {
        if (fi_fseek64(f, 0, SEEK_END) == 0) size = fi_ftell64(f);
        fclose(f);
    }
    printf("  file size: %.0f bytes\n", (double)size);
    report("the file is larger than 2 GB", size > (INT64)0x7FFFFFFF);

    dib = FreeImage_Load(FIF_TIFF, path, FIF_LOAD_NOPIXELS);
    report("header-only reload: the IFD past 2 GB is read", dib && FreeImage_GetWidth(dib) == W && FreeImage_GetHeight(dib) == H);
    if (dib) FreeImage_Unload(dib);

    if (full) {
        int ok = 0;
        dib = FreeImage_Load(FIF_TIFF, path, 0);
        if (dib && FreeImage_GetWidth(dib) == W && FreeImage_GetHeight(dib) == H) {
            ok = 1;
            for (y = 0; y < H && ok; y += 997) {
                const BYTE *row = FreeImage_GetScanLine(dib, y);
                for (x = 0; x < W; x++) if (row[x] != pixel(x, y)) { ok = 0; break; }
            }
        }
        report("full reload: sampled rows exact", ok);
        if (dib) FreeImage_Unload(dib);
    }

    remove(path);
    FreeImage_DeInitialise();
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
