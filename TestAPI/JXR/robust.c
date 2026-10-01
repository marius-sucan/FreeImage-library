/* FreeImage 3 - JPEG XR robustness: files with damaged or inconsistent metadata, container and codestream may fail
   to load, but never crash, abort or leak; run it under AddressSanitizer too (make asan run) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static int checks = 0, failures = 0;
#define CHECK(cond, ...) do { checks++; if (!(cond)) { failures++; printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static long loaded = 0, refused = 0;

typedef struct {
    const char *name;
    BYTE *data;
    DWORD size;
    FIBITMAP *decoded;   /* the undamaged file, loaded */
} Seed;

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static unsigned get16(const BYTE *p) { return p[0] | (p[1] << 8); }
static unsigned get32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static void put16(BYTE *p, unsigned v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); }
static void put32(BYTE *p, unsigned v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); p[2] = (BYTE)(v >> 16); p[3] = (BYTE)(v >> 24); }

static unsigned rng = 12345;
static unsigned next(void) { rng = rng * 1103515245u + 12345u; return rng >> 8; }

/* the IFD entry of a tag, or NULL */
static BYTE *entry(BYTE *data, DWORD size, unsigned tag) {
    const DWORD ifd = get32(data + 4);
    unsigned i, n;
    if (ifd + 2 > size) return NULL;
    n = get16(data + ifd);
    for (i = 0; i < n && ifd + 2 + 12 * (i + 1) <= size; i++) {
        if (get16(data + ifd + 2 + 12 * i) == tag) return data + ifd + 2 + 12 * i;
    }
    return NULL;
}

/* loaded from memory, and header only; NULL or the image */
static FIBITMAP *load(const BYTE *data, DWORD size) {
    FIMEMORY *mem = FreeImage_OpenMemory((BYTE *)data, size);
    FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_JXR, mem, 0);
    FreeImage_SeekMemory(mem, 0, SEEK_SET);
    FreeImage_Unload(FreeImage_LoadFromMemory(FIF_JXR, mem, FIF_LOAD_NOPIXELS));
    FreeImage_CloseMemory(mem);
    if (dib) loaded++; else refused++;
    return dib;
}

static void attempt(const BYTE *data, DWORD size) {
    FreeImage_Unload(load(data, size));
}

static int same_pixels(FIBITMAP *a, FIBITMAP *b) {
    unsigned y;
    if (!a || !b || FreeImage_GetWidth(a) != FreeImage_GetWidth(b) || FreeImage_GetHeight(a) != FreeImage_GetHeight(b) ||
        FreeImage_GetImageType(a) != FreeImage_GetImageType(b) || FreeImage_GetBPP(a) != FreeImage_GetBPP(b)) return 0;
    for (y = 0; y < FreeImage_GetHeight(a); y++) {
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), FreeImage_GetLine(a))) return 0;
    }
    return 1;
}

static void put_tag(FIBITMAP *dib, const char *key, WORD id, FREE_IMAGE_MDTYPE type, DWORD count, DWORD width, const void *value) {
    FITAG *tag = FreeImage_CreateTag();
    FreeImage_SetTagKey(tag, key);
    FreeImage_SetTagID(tag, id);
    FreeImage_SetTagType(tag, type);
    FreeImage_SetTagCount(tag, count);
    FreeImage_SetTagLength(tag, count * width);
    FreeImage_SetTagValue(tag, value);
    FreeImage_SetMetadata(FIMD_EXIF_MAIN, dib, key, tag);
    FreeImage_DeleteTag(tag);
}

static const WORD PAGES[2] = { 2, 5 };

static Seed make_seed(const char *name, FREE_IMAGE_TYPE type, unsigned bpp, int flags, int tags) {
    Seed s = { name, NULL, 0, NULL };
    FIBITMAP *dib = FreeImage_AllocateT(type, 48, 32, bpp, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    BYTE *p;
    DWORD n;
    unsigned x, y;
    for (y = 0; y < 32; y++) {
        BYTE *line = FreeImage_GetScanLine(dib, y);
        if (type == FIT_RGBAF || type == FIT_RGBF) {
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
    if (tags) {
        static const WORD STARS = 3;
        static const BYTE TITLE[] = { 'T', 0, 'i', 0, 't', 0, 0, 0 };
        put_tag(dib, "PageNumber", 0x0129, FIDT_SHORT, 2, 2, PAGES);
        put_tag(dib, "Rating", 0x4746, FIDT_SHORT, 1, 2, &STARS);
        put_tag(dib, "XPTitle", 0x9c9b, FIDT_BYTE, sizeof(TITLE), 1, TITLE);
        put_tag(dib, "Software", 0x0131, FIDT_ASCII, 10, 1, "FreeImage");
        put_tag(dib, "Artist", 0x013b, FIDT_ASCII, 3, 1, "Bo");
        put_tag(dib, "ImageDescription", 0x010e, FIDT_ASCII, 12, 1, "Description");
    }
    if (FreeImage_SaveToMemory(FIF_JXR, dib, mem, flags) && FreeImage_AcquireMemory(mem, &p, &n)) {
        s.data = (BYTE *)malloc(n);
        memcpy(s.data, p, n);
        s.size = n;
        s.decoded = load(s.data, s.size);
    }
    CHECK(s.decoded != NULL, "%s: not saved and loaded back", name);
    FreeImage_CloseMemory(mem);
    FreeImage_Unload(dib);
    return s;
}

/* the main codestream (ImageOffset) or the planar alpha one (AlphaOffset), or 0 */
static DWORD codestream(const Seed *s, unsigned tag) {
    BYTE *e = entry(s->data, s->size, tag);
    DWORD at = e ? get32(e + 8) : 0;
    return (at && at + 32 <= s->size && !memcmp(s->data + at, "WMPHOTO", 7)) ? at : 0;
}

int main(void) {
    Seed seeds[6];
    BYTE *work;
    int i, j, k;
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);

    seeds[0] = make_seed("rgb24 lossless, with tags", FIT_BITMAP, 24, JXR_LOSSLESS, 1);
    seeds[1] = make_seed("rgba32 q60", FIT_BITMAP, 32, 60, 0);
    seeds[2] = make_seed("grey8 lossless", FIT_BITMAP, 8, JXR_LOSSLESS, 0);
    seeds[3] = make_seed("bw1 lossless", FIT_BITMAP, 1, JXR_LOSSLESS, 0);
    seeds[4] = make_seed("rgb555 q60", FIT_BITMAP, 16, 60, 0);
    seeds[5] = make_seed("rgbaf lossless", FIT_RGBAF, 128, JXR_LOSSLESS, 0);
    for (i = 0; i < 6; i++) {
        if (!seeds[i].decoded) return 1;
    }

    /* descriptive tags: a PageNumber of one SHORT, as older release builds wrote it, loads as page, 0; a tag of
       another type or count leaves the pixels alone */
    {
        Seed *s = &seeds[0];
        static const unsigned TAGS[] = { 0x0129, 0x4746, 0x9c9b, 0x0131, 0x013b, 0x010e };
        static const unsigned TYPES[] = { 1, 2, 3, 4, 5, 7, 9, 11, 12, 13 };
        static const unsigned COUNTS[] = { 0, 1, 2, 3, 5, 0x7fffffff, 0xffffffff };
        BYTE *e;
        FIBITMAP *dib;
        FITAG *tag = NULL;
        work = (BYTE *)malloc(s->size);
        memcpy(work, s->data, s->size);
        e = entry(work, s->size, 0x0129);
        CHECK(e && get16(e + 2) == 3 && get32(e + 4) == 2, "PageNumber: not two SHORTs");
        if (e) put32(e + 4, 1);
        dib = load(work, s->size);
        CHECK(dib != NULL, "PageNumber of one SHORT: does not load");
        if (dib) {
            CHECK(FreeImage_GetMetadata(FIMD_EXIF_MAIN, dib, "PageNumber", &tag) && tag && FreeImage_GetTagCount(tag) == 2 &&
                  ((const WORD *)FreeImage_GetTagValue(tag))[0] == PAGES[0] && ((const WORD *)FreeImage_GetTagValue(tag))[1] == 0,
                  "PageNumber of one SHORT: not loaded as %u, 0", PAGES[0]);
            CHECK(same_pixels(dib, s->decoded), "PageNumber of one SHORT: other pixels");
        }
        FreeImage_Unload(dib);
        for (i = 0; i < (int)(sizeof(TAGS) / sizeof(TAGS[0])); i++) {
            for (j = 0; j < (int)(sizeof(TYPES) / sizeof(TYPES[0])); j++) {
                for (k = 0; k < (int)(sizeof(COUNTS) / sizeof(COUNTS[0])); k++) {
                    memcpy(work, s->data, s->size);
                    e = entry(work, s->size, TAGS[i]);
                    if (!e) continue;
                    put16(e + 2, TYPES[j]);
                    put32(e + 4, COUNTS[k]);
                    dib = load(work, s->size);
                    CHECK(dib && same_pixels(dib, s->decoded), "tag %04x of type %u, count %u: %s", TAGS[i], TYPES[j], COUNTS[k],
                          dib ? "other pixels" : "does not load");
                    FreeImage_Unload(dib);
                }
            }
        }
        free(work);
    }

    for (i = 0; i < 6; i++) {
        Seed *s = &seeds[i];
        const DWORD main_cs = codestream(s, 0xbcc0), alpha_cs = codestream(s, 0xbcc2);
        BYTE *pf;
        DWORD at, n;
        FIBITMAP *dib;
        work = (BYTE *)malloc(s->size);
        CHECK(main_cs != 0, "%s: no codestream", s->name);

        /* the container says another pixel format, or none */
        memcpy(work, s->data, s->size);
        pf = entry(work, s->size, 0xbc01);
        CHECK(pf != NULL && get32(pf + 8) + 16 <= s->size, "%s: no pixel format", s->name);
        if (pf) {
            const DWORD guid = get32(pf + 8);
            for (j = 0; j < 256; j++) {
                memcpy(work, s->data, s->size);
                work[guid + 15] = (BYTE)j;
                attempt(work, s->size);
            }
            memcpy(work, s->data, s->size);
            put16(pf, 0x010d);
            attempt(work, s->size);
        }

        /* codestream headers: output colour format and bit depth, internal colour format, the alpha flag, the size */
        for (k = 0; k < 2; k++) {
            at = k ? alpha_cs : main_cs;
            if (!at) continue;
            for (j = 0; j < 256; j++) {
                memcpy(work, s->data, s->size);
                work[at + 11] = (BYTE)j;
                attempt(work, s->size);
            }
            for (j = 0; j < 8; j++) {
                const DWORD plane = at + 12 + ((work[at + 10] & 0x80) ? 4 : 8);
                memcpy(work, s->data, s->size);
                work[plane] = (BYTE)((work[plane] & 0x1f) | (j << 5));
                attempt(work, s->size);
            }
            memcpy(work, s->data, s->size);
            work[at + 10] ^= 1;
            attempt(work, s->size);
            if (work[at + 10] & 0x80) {
                static const unsigned SIZES[] = { 0, 1, 15, 16, 17, 31, 33, 47, 63, 1055 };
                for (j = 0; j < (int)(sizeof(SIZES) / sizeof(SIZES[0])); j++) {
                    memcpy(work, s->data, s->size);
                    work[at + 12 + 2 * (j & 1)] = (BYTE)(SIZES[j] >> 8);
                    work[at + 13 + 2 * (j & 1)] = (BYTE)SIZES[j];
                    attempt(work, s->size);
                }
                /* a size its data cannot hold at a bit per macroblock is refused before allocating */
                memcpy(work, s->data, s->size);
                memset(work + at + 12, 0xff, 4);
                dib = load(work, s->size);
                CHECK(dib == NULL, "%s: 65536 x 65536 from %lu bytes loaded", s->name, (unsigned long)s->size);
                FreeImage_Unload(dib);
            }
        }

        /* cut short */
        for (n = 1; n < s->size; n += s->size / 64 + 1) attempt(s->data, n);

        /* a few bytes of damage past the codestream headers */
        for (j = 0; j < 300; j++) {
            const DWORD from = main_cs + 24, span = s->size - from;
            memcpy(work, s->data, s->size);
            for (k = (int)(next() % 4); k >= 0; k--) work[from + next() % span] = (BYTE)next();
            attempt(work, s->size);
        }
        free(work);
    }

    printf("%ld damaged files: %ld loaded, %ld refused\n", loaded + refused - 6, loaded - 6, refused);
    printf("%d checks, %d failures\n", checks, failures);
    for (i = 0; i < 6; i++) {
        FreeImage_Unload(seeds[i].decoded);
        free(seeds[i].data);
    }
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
