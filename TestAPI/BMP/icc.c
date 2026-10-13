/* FreeImage 3 - BMP test: ICC profiles embedded in BITMAPV5HEADER files, and color management on load */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

#define W 16
#define H 6
#define CS_SRGB     0x73524742  /* 'sRGB' */
#define CS_LINKED   0x4C494E4B  /* 'LINK' */
#define CS_EMBEDDED 0x4D424544  /* 'MBED' */

static int failures = 0;

static void report(const char *what, int ok) {
    printf("  %-64s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

typedef struct { BYTE *p; unsigned n; } BUF;

static void put(BUF *b, const void *data, unsigned n) {
    b->p = (BYTE *)realloc(b->p, b->n + n);
    memcpy(b->p + b->n, data, n);
    b->n += n;
}
static void put8(BUF *b, unsigned v) { BYTE c = (BYTE)v; put(b, &c, 1); }
static void put16(BUF *b, unsigned v) { put8(b, v & 255); put8(b, (v >> 8) & 255); }
static void put32(BUF *b, DWORD v) { put16(b, v & 0xFFFF); put16(b, (v >> 16) & 0xFFFF); }

/* saturated colors, which a wide-gamut profile moves */
static BYTE chan(unsigned x, unsigned y, int c) { return (BYTE)(((x * 17 + y * 41 + c * 85) & 1) ? 255 : (x * 16 + c * 40 + y * 9) & 255); }

typedef struct {
    unsigned header;        /* 108 or 124 */
    unsigned bpp;           /* 24, or 8 with a 16-color palette */
    DWORD cs_type;
    const BYTE *profile;    /* written after the pixels */
    unsigned profile_size;
    unsigned padding;       /* zero bytes after the profile, counted in bV5ProfileSize */
    int bad_offset;         /* 1: bV5ProfileData points past the file, 2: into the header */
    DWORD forced_size;      /* non-zero: bV5ProfileSize */
} SPEC;

static BUF build(const SPEC *s) {
    BUF b = { NULL, 0 }, info = { NULL, 0 }, bits = { NULL, 0 }, tail = { NULL, 0 };
    const unsigned pitch = ((W * s->bpp + 31) / 32) * 4;
    const unsigned colors = (s->bpp == 8) ? 16 : 0;
    unsigned x, y, i;
    for (y = 0; y < H; y++) {
        const unsigned row = H - 1 - y;
        for (x = 0; x < W; x++) {
            if (s->bpp == 8) put8(&bits, (x + row) & 15);
            else { put8(&bits, chan(x, row, 2)); put8(&bits, chan(x, row, 1)); put8(&bits, chan(x, row, 0)); }
        }
        for (x = W * s->bpp / 8; x < pitch; x++) put8(&bits, 0);
    }
    if (s->profile) {
        put(&tail, s->profile, s->profile_size);
        for (i = 0; i < s->padding; i++) put8(&tail, 0);
    }
    {
        const DWORD off_bits = 14 + s->header + colors * 4;
        DWORD profile_data = s->header + colors * 4 + bits.n;
        DWORD profile_size = s->forced_size ? s->forced_size : tail.n;
        if (s->bad_offset == 1) profile_data += 1000;
        if (s->bad_offset == 2) profile_data = 40;
        put32(&info, s->header); put32(&info, W); put32(&info, H); put16(&info, 1); put16(&info, s->bpp);
        put32(&info, 0); put32(&info, bits.n); put32(&info, 2835); put32(&info, 2835); put32(&info, colors); put32(&info, 0);
        for (i = 0; i < 4; i++) put32(&info, 0);            /* masks */
        put32(&info, s->cs_type);
        for (i = 0; i < 12; i++) put32(&info, 0);           /* endpoints, gammas */
        if (s->header >= 124) {
            put32(&info, 4);
            put32(&info, s->profile ? profile_data : 0);
            put32(&info, s->profile ? profile_size : 0);
            put32(&info, 0);
        }
        for (i = 0; i < colors; i++) { put8(&info, (BYTE)(i * 16)); put8(&info, (BYTE)(255 - i * 16)); put8(&info, (BYTE)(i * 8)); put8(&info, 0); }
        put8(&b, 'B'); put8(&b, 'M');
        put32(&b, 14 + info.n + bits.n + tail.n); put16(&b, 0); put16(&b, 0); put32(&b, off_bits);
        put(&b, info.p, info.n);
        put(&b, bits.p, bits.n);
        put(&b, tail.p, tail.n);
    }
    free(info.p); free(bits.p); free(tail.p);
    return b;
}

static FIBITMAP *load(const SPEC *s, int flags) {
    BUF b = build(s);
    FIMEMORY *mem = FreeImage_OpenMemory(b.p, b.n);
    FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_BMP, mem, flags);
    FreeImage_CloseMemory(mem);
    free(b.p);
    return dib;
}

static int has_profile(FIBITMAP *dib, const BYTE *profile, unsigned size) {
    FIICCPROFILE *icc = dib ? FreeImage_GetICCProfile(dib) : NULL;
    return icc && icc->data && (unsigned)icc->size == size && memcmp(icc->data, profile, size) == 0;
}

static int no_profile(FIBITMAP *dib) {
    return dib && FreeImage_GetWidth(dib) == W && FreeImage_GetICCProfile(dib)->size == 0;
}

static int same_pixels(FIBITMAP *a, FIBITMAP *b) {
    unsigned y;
    if (!a || !b || FreeImage_GetBPP(a) != FreeImage_GetBPP(b)) return 0;
    for (y = 0; y < H; y++) {
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), FreeImage_GetLine(a)) != 0) return 0;
    }
    if (FreeImage_GetBPP(a) == 8) return memcmp(FreeImage_GetPalette(a), FreeImage_GetPalette(b), 16 * sizeof(RGBQUAD)) == 0;
    return 1;
}

static void DLL_CALLCONV quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

int main(void) {
    DWORD adobe_size = 0, grey_size = 0, srgb_size = 0;
    const BYTE *adobe, *grey, *srgb;
    BYTE *copy;
    SPEC s;
    FIBITMAP *dib, *plain;

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);
    adobe = (const BYTE *)FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
    grey = (const BYTE *)FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_GRAY, &grey_size);
    srgb = (const BYTE *)FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_SRGB, &srgb_size);
    copy = (BYTE *)malloc(adobe_size);

    printf("reading the profile\n");
    report("FreeImage_FIFSupportsICCProfiles(FIF_BMP)", FreeImage_FIFSupportsICCProfiles(FIF_BMP));
    memset(&s, 0, sizeof s); s.header = 124; s.bpp = 24; s.cs_type = CS_EMBEDDED; s.profile = adobe; s.profile_size = adobe_size;
    dib = load(&s, 0);
    report("V5, embedded profile after the pixels: attached", has_profile(dib, adobe, adobe_size));
    if (dib) FreeImage_Unload(dib);
    dib = load(&s, FIF_LOAD_NOPIXELS);
    report("the same, header only", has_profile(dib, adobe, adobe_size) && !FreeImage_HasPixels(dib));
    if (dib) FreeImage_Unload(dib);
    s.bpp = 8;
    dib = load(&s, 0);
    report("8-bit palette image: attached", has_profile(dib, adobe, adobe_size) && FreeImage_GetBPP(dib) == 8);
    if (dib) FreeImage_Unload(dib);
    s.bpp = 24; s.padding = 3;
    dib = load(&s, 0);
    report("profile padded in the file: attached at its own size", has_profile(dib, adobe, adobe_size));
    if (dib) FreeImage_Unload(dib);

    printf("profiles that are ignored, the image loads\n");
    memset(&s, 0, sizeof s); s.header = 124; s.bpp = 24; s.cs_type = CS_LINKED;
    s.profile = (const BYTE *)"C:\\profiles\\wide.icc"; s.profile_size = 21;
    report("linked profile: not followed", no_profile(dib = load(&s, 0)));
    if (dib) FreeImage_Unload(dib);
    memset(&s, 0, sizeof s); s.header = 124; s.bpp = 24; s.cs_type = CS_SRGB; s.profile = adobe; s.profile_size = adobe_size;
    report("CSType sRGB with profile bytes present: none attached", no_profile(dib = load(&s, 0)));
    if (dib) FreeImage_Unload(dib);
    s.cs_type = CS_EMBEDDED; s.bad_offset = 1;
    report("profile offset past the end of the file", no_profile(dib = load(&s, 0)));
    if (dib) FreeImage_Unload(dib);
    s.bad_offset = 2;
    report("profile offset inside the header", no_profile(dib = load(&s, 0)));
    if (dib) FreeImage_Unload(dib);
    s.bad_offset = 0; s.forced_size = 100;
    report("profile size below an ICC header", no_profile(dib = load(&s, 0)));
    if (dib) FreeImage_Unload(dib);
    s.forced_size = 0xFFFFFFF0u;
    report("profile size of 4 GB", no_profile(dib = load(&s, 0)));
    if (dib) FreeImage_Unload(dib);
    s.forced_size = 0; s.profile = grey; s.profile_size = grey_size;
    report("grey profile: a DIB holds RGB", no_profile(dib = load(&s, 0)));
    if (dib) FreeImage_Unload(dib);
    memcpy(copy, adobe, adobe_size); memcpy(copy + 16, "CMYK", 4);
    s.profile = copy; s.profile_size = adobe_size;
    report("CMYK profile", no_profile(dib = load(&s, 0)) && FreeImage_GetColorType(dib) == FIC_RGB);
    if (dib) FreeImage_Unload(dib);
    memcpy(copy, adobe, adobe_size); memcpy(copy + 36, "xxxx", 4);
    report("damaged profile", no_profile(dib = load(&s, 0)));
    if (dib) FreeImage_Unload(dib);
    memset(&s, 0, sizeof s); s.header = 108; s.bpp = 24; s.cs_type = CS_EMBEDDED; s.profile = adobe; s.profile_size = adobe_size;
    report("V4 header: it has no profile fields", no_profile(dib = load(&s, 0)));
    if (dib) FreeImage_Unload(dib);

    printf("color management on load\n");
    FreeImage_SetDisplayICCProfile(srgb, srgb_size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION);
    memset(&s, 0, sizeof s); s.header = 124; s.bpp = 24; s.cs_type = CS_EMBEDDED; s.profile = adobe; s.profile_size = adobe_size;
    plain = load(&s, 0);
    dib = load(&s, FIF_LOAD_DISPLAY_ICC);
    report("FIF_LOAD_DISPLAY_ICC converts the Adobe RGB pixels", dib && plain && !same_pixels(dib, plain));
    if (plain) FreeImage_ApplyDisplayICCProfile(plain);
    report("  as FreeImage_ApplyDisplayICCProfile does", same_pixels(dib, plain) && has_profile(dib, srgb, srgb_size));
    if (dib) FreeImage_Unload(dib);
    if (plain) FreeImage_Unload(plain);
    s.bpp = 8;
    plain = load(&s, 0);
    dib = load(&s, FIF_LOAD_DISPLAY_ICC);
    report("8-bit: the palette is converted, the image stays 8-bit", dib && plain && FreeImage_GetBPP(dib) == 8 && !same_pixels(dib, plain));
    if (plain) FreeImage_ApplyDisplayICCProfile(plain);
    report("  as FreeImage_ApplyDisplayICCProfile does", same_pixels(dib, plain));
    if (dib) FreeImage_Unload(dib);
    if (plain) FreeImage_Unload(plain);
    memset(&s, 0, sizeof s); s.header = 124; s.bpp = 24; s.cs_type = CS_EMBEDDED; s.profile = srgb; s.profile_size = srgb_size;
    plain = load(&s, 0);
    dib = load(&s, FIF_LOAD_DISPLAY_ICC);
    report("an sRGB profile on an sRGB display changes no pixel", same_pixels(dib, plain));
    if (dib) FreeImage_Unload(dib);
    if (plain) FreeImage_Unload(plain);

    printf("the writer embeds no profile\n");
    {
        FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
        memset(&s, 0, sizeof s); s.header = 124; s.bpp = 24; s.cs_type = CS_EMBEDDED; s.profile = adobe; s.profile_size = adobe_size;
        plain = load(&s, 0);
        FreeImage_SaveToMemory(FIF_BMP, plain, mem, 0);
        FreeImage_SeekMemory(mem, 0, SEEK_SET);
        dib = FreeImage_LoadFromMemory(FIF_BMP, mem, 0);
        {
            BYTE *bytes = NULL;
            DWORD size = 0;
            FreeImage_AcquireMemory(mem, &bytes, &size);
            report("a 24-bit image with a profile: 40-byte header, no profile", no_profile(dib) && size > 18 && bytes[14] == 40);
        }
        if (dib) FreeImage_Unload(dib);
        if (plain) FreeImage_Unload(plain);
        FreeImage_CloseMemory(mem);
    }

    free(copy);
    printf("--- %d failure(s) ---\n", failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
