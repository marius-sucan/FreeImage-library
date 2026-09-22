/* FreeImage 3 - OpenEXR data window test */
/* impossible windows must be refused before any allocation */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static int failures = 0;
static char message[512];

static void collect(FREE_IMAGE_FORMAT fif, const char *msg) {
    (void)fif;
    snprintf(message, sizeof message, "%s", msg ? msg : "");
}

/* "dataWindow\0box2i\0", a 4-byte size, 4 LE int32 corners */
static const char MARKER[] = "dataWindow\0box2i";
#define MARKER_LEN ((long)sizeof(MARKER))

static void put32(BYTE *p, int v) {
    p[0] = (BYTE)(v & 0xFF); p[1] = (BYTE)((v >> 8) & 0xFF);
    p[2] = (BYTE)((v >> 16) & 0xFF); p[3] = (BYTE)((v >> 24) & 0xFF);
}

static BYTE *patched(const BYTE *src, long len, int xmin, int ymin, int xmax, int ymax) {
    long i, at = -1;
    for (i = 0; i + MARKER_LEN < len; i++)
        if (memcmp(src + i, MARKER, MARKER_LEN) == 0) { at = i; break; }
    if (at < 0) return NULL;
    BYTE *copy = (BYTE *)malloc(len);
    memcpy(copy, src, len);
    BYTE *w = copy + at + MARKER_LEN + 4 /* attribute size */;
    put32(w, xmin); put32(w + 4, ymin); put32(w + 8, xmax); put32(w + 12, ymax);
    return copy;
}

static void expect(const BYTE *src, long len, const char *what,
                   int xmin, int ymin, int xmax, int ymax, int want_load) {
    BYTE *buf = patched(src, len, xmin, ymin, xmax, ymax);
    if (!buf) { printf("  %-24s FAIL: data window not found in the template\n", what); failures++; return; }

    message[0] = 0;
    FIMEMORY *mem = FreeImage_OpenMemory(buf, (DWORD)len);
    FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_EXR, mem, 0);

    /* a header-only load must be refused too */
    FreeImage_SeekMemory(mem, 0, SEEK_SET);
    FIBITMAP *hdr = FreeImage_LoadFromMemory(FIF_EXR, mem, FIF_LOAD_NOPIXELS);

    int bad = 0;
    if (want_load) {
        bad = (dib == NULL) || (hdr == NULL);
        printf("  %-24s %-8s%s\n", what, dib ? "loaded" : "REFUSED", bad ? "   *** FAIL" : "");
        if (bad) printf("      message: \"%s\"  header-only: %s\n", message, hdr ? "loaded" : "REFUSED");
    } else {
        bad = (dib != NULL) || (hdr != NULL) || (message[0] == 0);
        printf("  %-24s %-8s %s%s\n", what, dib ? "LOADED" : "refused", message,
               bad ? "   *** FAIL" : "");
        if (hdr) printf("      *** header-only load was NOT refused\n");
    }
    if (bad) failures++;

    if (dib) FreeImage_Unload(dib);
    if (hdr) FreeImage_Unload(hdr);
    FreeImage_CloseMemory(mem);
    free(buf);
}

int main(void) {
    FreeImage_Initialise(TRUE);
    FreeImage_SetOutputMessage(collect);

    const char *template_file = "data/fi_exr_zip.exr";
    FILE *f = fopen(template_file, "rb");
    if (!f) { printf("cannot open %s\n", template_file); return 1; }
    fseek(f, 0, SEEK_END); long len = ftell(f); fseek(f, 0, SEEK_SET);
    BYTE *src = (BYTE *)malloc(len);
    if (fread(src, 1, len, f) != (size_t)len) { printf("short read\n"); return 1; }
    fclose(f);
    printf("template: %s, %ld bytes, 64x48\n\n", template_file, len);

    printf("must be refused\n");
    /* both huge: the chunk-table test catches it */
    expect(src, len, "60001 x 60001",      0, 0, 60000, 60000,      0);
    /* height only: same, 4 million chunks */
    expect(src, len, "1 x 67108865",       0, 0, 0, 67108864,       0);
    /* width only: only the pixel-size test catches it */
    expect(src, len, "268435457 x 48",     0, 0, 268435456, 47,     0);
    /* inverted and empty windows */
    expect(src, len, "inverted (max<min)", 100, 100, 10, 10,        0);
    expect(src, len, "empty (max=min-1)",  0, 0, -1, -1,            0);
    /* extreme corners: width must not overflow an int */
    expect(src, len, "extreme corners",    -1073741823, -1073741823, 1073741823, 1073741823, 0);

    printf("\nmust still load\n");
    expect(src, len, "64 x 48 (untouched)", 0, 0, 63, 47,           1);

    printf("\n%d failure%s\n", failures, failures == 1 ? "" : "s");
    free(src);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
