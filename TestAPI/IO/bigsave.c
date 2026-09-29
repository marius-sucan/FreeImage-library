/* FreeImage 3 - I/O test: a save that crosses 2 GB, or with --4g 4 GB */
/* 47000 x 47000 8-bit, uncompressed TIFF: 2.2 GB of RAM and of disk, the IFD lands past 2 GB, and the file stays
   classic TIFF. --4g: 65536 x 65600, 4.3 GB, which only BigTIFF holds. */
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
    int full = 0, big = 0, i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--full") == 0) full = 1;
        else if (strcmp(argv[i], "--4g") == 0) big = 1;
    }
    const int W = big ? 65536 : 47000, H = big ? 65600 : 47000;
    const char *path = tmppath("fi_io_bigsave.tif");
    FIBITMAP *dib;
    FILE *f;
    INT64 size = -1;
    BYTE header[4] = { 0, 0, 0, 0 };
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
        if (fread(header, 1, sizeof(header), f) != sizeof(header)) memset(header, 0, sizeof(header));
        if (fi_fseek64(f, 0, SEEK_END) == 0) size = fi_ftell64(f);
        fclose(f);
    }
    printf("  file size: %.0f bytes\n", (double)size);
    report(big ? "the file is larger than 4 GB" : "the file is larger than 2 GB", size > (big ? (INT64)0xFFFFFFFFu : (INT64)0x7FFFFFFF));
    /* "II" then 42 for classic TIFF, 43 for BigTIFF */
    report(big ? "BigTIFF: 64-bit offsets reach past 4 GB" : "classic TIFF: it fits in 32-bit offsets", header[0] == 'I' && header[2] == (big ? 43 : 42));

    dib = FreeImage_Load(FIF_TIFF, path, FIF_LOAD_NOPIXELS);
    report(big ? "header-only reload: the IFD past 4 GB is read" : "header-only reload: the IFD past 2 GB is read", dib && FreeImage_GetWidth(dib) == (unsigned)W && FreeImage_GetHeight(dib) == (unsigned)H);
    if (dib) FreeImage_Unload(dib);

    if (full) {
        int ok = 0;
        dib = FreeImage_Load(FIF_TIFF, path, 0);
        if (dib && FreeImage_GetWidth(dib) == (unsigned)W && FreeImage_GetHeight(dib) == (unsigned)H) {
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
