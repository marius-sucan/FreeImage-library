/* FreeImage 3 - APNG test: image data too long for one chunk is split into several.
   'make chunks' links PluginAPNG rebuilt with chunks of at most 1000 bytes, standing in for the 2^31 - 1 a
   PNG chunk holds: a frame then takes many IDAT or fdAT chunks, each fdAT with the next sequence number,
   and every frame must come back exactly. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

#define W 64
#define H 64
#define FRAMES 3
#define MOST 1000

static int failures = 0;

static void report(const char *what, int ok) {
    printf("  %-60s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

static const char *tmppath(const char *name) {
    static char buf[1024];
    const char *dir = getenv("APNG_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

static unsigned be32(const BYTE *p) {
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

/* noise, which does not compress: about 16 KB of data per frame */
static FIBITMAP *make_frame(int seed) {
    FIBITMAP *dib = FreeImage_Allocate(W, H, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
    unsigned v = 2463534242u + (unsigned)seed * 7919u;
    int x, y;
    if (!dib) return NULL;
    for (y = 0; y < H; y++) {
        BYTE *row = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < W * 4; x++) {
            v ^= v << 13; v ^= v >> 17; v ^= v << 5;
            row[x] = (BYTE)v;
        }
        /* opaque: blending over the previous frame changes nothing */
        for (x = 0; x < W; x++) row[x * 4 + FI_RGBA_ALPHA] = 255;
    }
    return dib;
}

static int same(FIBITMAP *a, FIBITMAP *b) {
    int y;
    if (!a || !b || FreeImage_GetWidth(a) != W || FreeImage_GetHeight(a) != H || FreeImage_GetBPP(a) != 32
        || FreeImage_GetWidth(b) != W || FreeImage_GetHeight(b) != H || FreeImage_GetBPP(b) != 32) return 0;
    for (y = 0; y < H; y++)
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), W * 4)) return 0;
    return 1;
}

int main(void) {
    const char *path = tmppath("fi_apng_chunks.png");
    FIBITMAP *frames[FRAMES];
    FIMULTIBITMAP *mb;
    BYTE *file = NULL;
    long size = 0;
    FILE *f;
    int i, ok;

    FreeImage_Initialise(FALSE);
    printf("%d frames of %d x %d noise, image data in chunks of at most %d bytes\n", FRAMES, W, H, MOST);
    for (i = 0; i < FRAMES; i++) frames[i] = make_frame(i);

    remove(path);
    mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, TRUE, FALSE, FALSE, 0);
    ok = (mb != NULL);
    for (i = 0; ok && i < FRAMES; i++) ok = FreeImage_AppendPage(mb, frames[i]);
    ok = FreeImage_CloseMultiBitmap(mb, 0) && ok;
    report("saved", ok);

    f = fopen(path, "rb");
    if (f && fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 8 && fseek(f, 0, SEEK_SET) == 0) {
        file = (BYTE *)malloc((size_t)size);
        if (file && fread(file, 1, (size_t)size, f) != (size_t)size) { free(file); file = NULL; }
    }
    if (f) fclose(f);

    if (file) {
        long pos = 8;
        unsigned idat = 0, fdat = 0, fctl = 0, longest = 0, next_seq = 0;
        int seq_ok = 1, ended = 0;
        while (pos + 12 <= size) {
            const unsigned length = be32(file + pos);
            const BYTE *type = file + pos + 4;
            if (pos + 12 + (long)length > size) break;
            if (!memcmp(type, "IDAT", 4) || !memcmp(type, "fdAT", 4)) {
                if (length > longest) longest = length;
                if (!memcmp(type, "IDAT", 4)) idat++; else fdat++;
            }
            /* fcTL and fdAT share one sequence, without gaps */
            if (!memcmp(type, "fcTL", 4) || !memcmp(type, "fdAT", 4)) {
                if (length < 4 || be32(file + pos + 8) != next_seq) seq_ok = 0;
                next_seq++;
                if (!memcmp(type, "fcTL", 4)) fctl++;
            }
            if (!memcmp(type, "IEND", 4)) { ended = 1; break; }
            pos += 12 + (long)length;
        }
        printf("  %u IDAT and %u fdAT chunks, the longest %u bytes; %u fcTL\n", idat, fdat, longest, fctl);
        report("the file ends with IEND", ended);
        report("no image data chunk holds more than the limit", longest > 0 && longest <= MOST);
        report("frame 0 in several IDAT, the others in several fdAT", idat > 1 && fdat > (unsigned)(FRAMES - 1) && fctl == FRAMES);
        report("fcTL and fdAT sequence numbers run 0, 1, 2... without a gap", seq_ok);
        free(file);
    } else {
        report("read the file back", 0);
    }

    mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, APNG_PLAYBACK);
    report("reopened: every frame", mb && FreeImage_GetPageCount(mb) == FRAMES);
    for (i = 0; mb && i < FRAMES && i < FreeImage_GetPageCount(mb); i++) {
        char what[64];
        FIBITMAP *d = FreeImage_LockPage(mb, i);
        snprintf(what, sizeof(what), "  frame %d pixels", i);
        report(what, same(d, frames[i]));
        if (d) FreeImage_UnlockPage(mb, d, FALSE);
    }
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);

    remove(path);
    for (i = 0; i < FRAMES; i++) FreeImage_Unload(frames[i]);
    FreeImage_DeInitialise();
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
