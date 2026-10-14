/* FreeImage 3 - BMP test: ICC profiles embedded in BITMAPV5HEADER files, read and written, and color management on load */
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

static int warnings = 0;
static void DLL_CALLCONV quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; if (strstr(msg, "left out")) warnings++; }

/* --- the writer ------------------------------------------------------------ */

static DWORD le32(const BYTE *p) { return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24); }

static unsigned pitch_of(unsigned bpp) { return ((W * bpp + 31) / 32) * 4; }

/* W x H (16-bit: 5-6-5), mixed pixels and palette; 32-bit alpha: 'alpha' everywhere, or mixed when negative */
static FIBITMAP *make(unsigned bpp, int alpha) {
    const DWORD r = (bpp == 16) ? FI16_565_RED_MASK : 0, g = (bpp == 16) ? FI16_565_GREEN_MASK : 0, b = (bpp == 16) ? FI16_565_BLUE_MASK : 0;
    FIBITMAP *dib = FreeImage_Allocate(W, H, bpp, r, g, b);
    RGBQUAD *pal = FreeImage_GetPalette(dib);
    unsigned x, y;
    for (x = 0; pal && x < FreeImage_GetColorsUsed(dib); x++) {
        pal[x].rgbRed = (BYTE)(x * 37); pal[x].rgbGreen = (BYTE)(255 - x * 11); pal[x].rgbBlue = (BYTE)(x * 5); pal[x].rgbReserved = 0;
    }
    for (y = 0; y < H; y++) {
        BYTE *row = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < FreeImage_GetLine(dib); x++) row[x] = (BYTE)(x * 29 + y * 71);
        for (x = 0; bpp == 32 && x < W; x++) row[x * 4 + FI_RGBA_ALPHA] = (BYTE)((alpha >= 0) ? alpha : (int)x * 16);
    }
    return dib;
}

static BUF save_bmp(FIBITMAP *dib, int flags) {
    BUF b = { NULL, 0 };
    BYTE *bytes = NULL;
    DWORD size = 0;
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    if (dib && FreeImage_SaveToMemory(FIF_BMP, dib, mem, flags) && FreeImage_AcquireMemory(mem, &bytes, &size)) put(&b, bytes, size);
    FreeImage_CloseMemory(mem);
    return b;
}

static FIBITMAP *load_bmp(const BUF *b, int flags) {
    FIMEMORY *mem = FreeImage_OpenMemory(b->p, b->n);
    FIBITMAP *dib = b->p ? FreeImage_LoadFromMemory(FIF_BMP, mem, flags) : NULL;
    FreeImage_CloseMemory(mem);
    return dib;
}

/* same depth, rows and palette */
static int identical(FIBITMAP *a, FIBITMAP *b) {
    const unsigned colors = a ? FreeImage_GetColorsUsed(a) : 0;
    unsigned y;
    if (!a || !b || FreeImage_GetBPP(a) != FreeImage_GetBPP(b) || FreeImage_GetColorsUsed(b) != colors) return 0;
    for (y = 0; y < H; y++) {
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), FreeImage_GetLine(a)) != 0) return 0;
    }
    return colors == 0 || memcmp(FreeImage_GetPalette(a), FreeImage_GetPalette(b), colors * sizeof(RGBQUAD)) == 0;
}

/* a V5 header saying MBED, the pixels, then the profile, which ends the file; bfSize is the file's size */
static int embedded(const BUF *f, DWORD compression, const BYTE *profile, DWORD size) {
    const BYTE *h = f->p + 14;
    DWORD image, data;
    if (!f->p || f->n < 14 + 124) return 0;
    image = (compression == 0) ? pitch_of(h[14] | (h[15] << 8)) * H : le32(h + 20);
    data = le32(h + 112);
    return le32(f->p + 2) == f->n && le32(h) == 124 && le32(h + 16) == compression &&
        le32(h + 56) == CS_EMBEDDED && le32(h + 108) == 4 && le32(h + 116) == size &&
        14 + data == le32(f->p + 10) + image && 14 + (UINT64)data + size == f->n &&
        memcmp(f->p + 14 + data, profile, size) == 0;
}

/* the V5 header's masks */
static int masks(const BUF *f, DWORD r, DWORD g, DWORD b, DWORD a) {
    return f->p && le32(f->p + 54) == r && le32(f->p + 58) == g && le32(f->p + 62) == b && le32(f->p + 66) == a;
}

/* a 40-byte BI_RGB header and nothing after the pixels */
static int plain40(const BUF *f, unsigned bpp) {
    return f->p && le32(f->p + 14) == 40 && le32(f->p + 30) == 0 && le32(f->p + 2) == f->n && le32(f->p + 10) + pitch_of(bpp) * H == f->n;
}

/* 'profile' padded with zeros to 'to' bytes, its header saying so */
static BYTE *padded(const BYTE *profile, DWORD size, DWORD to) {
    BYTE *p = (BYTE *)calloc(to, 1);
    memcpy(p, profile, size);
    p[0] = (BYTE)(to >> 24); p[1] = (BYTE)(to >> 16); p[2] = (BYTE)(to >> 8); p[3] = (BYTE)to;
    return p;
}

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

    printf("the writer: an RGB profile goes into a V5 header, after the pixels\n");
    {
        static const unsigned depths[] = { 1, 4, 8, 24 };
        unsigned i;
        BUF f;
        char line[128];
        memset(&s, 0, sizeof s); s.header = 124; s.bpp = 24; s.cs_type = CS_EMBEDDED; s.profile = adobe; s.profile_size = adobe_size;
        plain = load(&s, 0);
        f = save_bmp(plain, 0);
        dib = load_bmp(&f, 0);
        report("a loaded V5 file saves its profile back", embedded(&f, 0, adobe, adobe_size) && has_profile(dib, adobe, adobe_size) && same_pixels(dib, plain));
        if (dib) FreeImage_Unload(dib);
        dib = load_bmp(&f, FIF_LOAD_NOPIXELS);
        report("  and a header-only load of it has the profile", has_profile(dib, adobe, adobe_size) && !FreeImage_HasPixels(dib));
        if (dib) FreeImage_Unload(dib);
        if (plain) FreeImage_Unload(plain);
        free(f.p);

        for (i = 0; i < sizeof(depths) / sizeof(depths[0]); i++) {
            plain = make(depths[i], 0);
            FreeImage_CreateICCProfile(plain, (void *)adobe, (long)adobe_size);
            f = save_bmp(plain, 0);
            dib = load_bmp(&f, 0);
            snprintf(line, sizeof line, "%u-bit: BI_RGB, no masks, the palette after the header, loads back", depths[i]);
            report(line, embedded(&f, 0, adobe, adobe_size) && masks(&f, 0, 0, 0, 0) &&
                   le32(f.p + 10) == 14 + 124 + FreeImage_GetColorsUsed(plain) * 4 && identical(dib, plain) && has_profile(dib, adobe, adobe_size));
            if (dib) FreeImage_Unload(dib);
            FreeImage_Unload(plain);
            free(f.p);
        }

        plain = make(8, 0);
        FreeImage_CreateICCProfile(plain, (void *)adobe, (long)adobe_size);
        f = save_bmp(plain, BMP_SAVE_RLE);
        dib = load_bmp(&f, 0);
        report("8-bit, BMP_SAVE_RLE: BI_RLE8, the profile after the RLE data", embedded(&f, 1, adobe, adobe_size) && identical(dib, plain) && has_profile(dib, adobe, adobe_size));
        if (dib) FreeImage_Unload(dib);
        FreeImage_Unload(plain);
        free(f.p);

        plain = make(16, 0);
        FreeImage_CreateICCProfile(plain, (void *)adobe, (long)adobe_size);
        f = save_bmp(plain, 0);
        dib = load_bmp(&f, 0);
        report("16-bit 5-6-5: BI_BITFIELDS, the masks in the header, none after it",
               embedded(&f, 3, adobe, adobe_size) && masks(&f, FI16_565_RED_MASK, FI16_565_GREEN_MASK, FI16_565_BLUE_MASK, 0) &&
               le32(f.p + 10) == 14 + 124 && identical(dib, plain) && FreeImage_GetGreenMask(dib) == FI16_565_GREEN_MASK && has_profile(dib, adobe, adobe_size));
        if (dib) FreeImage_Unload(dib);
        FreeImage_Unload(plain);
        free(f.p);

        plain = make(32, -1);
        FreeImage_CreateICCProfile(plain, (void *)adobe, (long)adobe_size);
        f = save_bmp(plain, 0);
        dib = load_bmp(&f, 0);
        report("32-bit with alpha: BI_BITFIELDS, alpha mask, both kept",
               embedded(&f, 3, adobe, adobe_size) && masks(&f, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000) && identical(dib, plain) && has_profile(dib, adobe, adobe_size));
        if (dib) FreeImage_Unload(dib);
        FreeImage_Unload(plain);
        free(f.p);

        plain = make(32, 255);
        FreeImage_CreateICCProfile(plain, (void *)adobe, (long)adobe_size);
        f = save_bmp(plain, 0);
        dib = load_bmp(&f, 0);
        report("32-bit opaque: BI_BITFIELDS, no alpha mask, loads back",
               embedded(&f, 3, adobe, adobe_size) && masks(&f, 0x00FF0000, 0x0000FF00, 0x000000FF, 0) && identical(dib, plain) && has_profile(dib, adobe, adobe_size));
        if (dib) FreeImage_Unload(dib);
        FreeImage_Unload(plain);
        free(f.p);

        plain = make(32, 0);
        FreeImage_CreateICCProfile(plain, (void *)adobe, (long)adobe_size);
        f = save_bmp(plain, 0);
        dib = load_bmp(&f, 0);
        {
            /* alpha 0 everywhere says nothing: no alpha mask, so V5 readers show it opaque too */
            int ok = embedded(&f, 3, adobe, adobe_size) && masks(&f, 0x00FF0000, 0x0000FF00, 0x000000FF, 0) && dib && FreeImage_GetBPP(dib) == 32 && has_profile(dib, adobe, adobe_size);
            unsigned x, y;
            for (y = 0; ok && y < H; y++) {
                const BYTE *a = FreeImage_GetScanLine(plain, y), *b = FreeImage_GetScanLine(dib, y);
                for (x = 0; ok && x < W; x++) ok = !memcmp(a + x * 4, b + x * 4, 3) && b[x * 4 + FI_RGBA_ALPHA] == 255;
            }
            report("32-bit, alpha never set: no alpha mask, loads back opaque", ok);
        }
        if (dib) FreeImage_Unload(dib);
        FreeImage_Unload(plain);
        free(f.p);

        {
            /* the loader's bound */
            BYTE *big = padded(adobe, adobe_size, 64u << 20);
            plain = make(24, 0);
            FreeImage_CreateICCProfile(plain, big, 64L << 20);
            f = save_bmp(plain, 0);
            dib = load_bmp(&f, 0);
            report("a 64 MB profile: kept", embedded(&f, 0, big, 64u << 20) && has_profile(dib, big, 64u << 20));
            if (dib) FreeImage_Unload(dib);
            FreeImage_Unload(plain);
            free(f.p);
            free(big);
            big = padded(adobe, adobe_size, (64u << 20) + 4);
            plain = make(24, 0);
            FreeImage_CreateICCProfile(plain, big, (64L << 20) + 4);
            warnings = 0;
            f = save_bmp(plain, 0);
            report("64 MB + 4 bytes: left out, with a warning", plain40(&f, 24) && warnings == 1);
            FreeImage_Unload(plain);
            free(f.p);
            free(big);
        }
    }

    printf("profiles left out, with a warning: the header of an image without one\n");
    {
        BUF f;
        plain = FreeImage_Allocate(W, H, 8, 0, 0, 0);
        FreeImage_CreateICCProfile(plain, (void *)grey, (long)grey_size);
        warnings = 0;
        f = save_bmp(plain, 0);
        dib = load_bmp(&f, 0);
        report("8-bit grey with a grey profile: a DIB holds RGB", plain40(&f, 8) && warnings == 1 && no_profile(dib));
        if (dib) FreeImage_Unload(dib);
        FreeImage_Unload(plain);
        free(f.p);

        memcpy(copy, adobe, adobe_size); memcpy(copy + 16, "CMYK", 4);
        plain = make(24, 0);
        FreeImage_CreateICCProfile(plain, copy, (long)adobe_size);
        warnings = 0;
        f = save_bmp(plain, 0);
        report("a CMYK profile", plain40(&f, 24) && warnings == 1);
        FreeImage_Unload(plain);
        free(f.p);

        memcpy(copy, adobe, adobe_size); memcpy(copy + 36, "xxxx", 4);
        plain = make(24, 0);
        FreeImage_CreateICCProfile(plain, copy, (long)adobe_size);
        warnings = 0;
        f = save_bmp(plain, 0);
        report("a damaged profile", plain40(&f, 24) && warnings == 1);
        FreeImage_Unload(plain);
        free(f.p);

        plain = make(32, -1);
        FreeImage_CreateICCProfile(plain, (void *)adobe, (long)adobe_size);
        FreeImage_GetICCProfile(plain)->flags |= FIICC_COLOR_IS_CMYK;
        warnings = 0;
        f = save_bmp(plain, 0);
        report("a CMYK image: 40-byte header, K in the fourth byte", plain40(&f, 32) && warnings == 1);
        FreeImage_Unload(plain);
        free(f.p);
    }

    printf("images without a profile keep their header\n");
    {
        BUF f;
        warnings = 0;
        plain = make(24, 0);
        f = save_bmp(plain, 0);
        report("24-bit: 40-byte header", plain40(&f, 24));
        FreeImage_Unload(plain);
        free(f.p);
        plain = make(8, 0);
        f = save_bmp(plain, 0);
        report("8-bit: 40-byte header", plain40(&f, 8));
        FreeImage_Unload(plain);
        free(f.p);
        plain = make(16, 0);
        f = save_bmp(plain, 0);
        report("16-bit: 40-byte header, three masks after it",
               f.p && le32(f.p + 14) == 40 && le32(f.p + 30) == 3 && le32(f.p + 10) == 14 + 40 + 12 && le32(f.p + 54) == FI16_565_RED_MASK && le32(f.p + 2) == f.n);
        FreeImage_Unload(plain);
        free(f.p);
        plain = make(32, 0);
        f = save_bmp(plain, 0);
        report("32-bit, alpha never set: 40-byte header", plain40(&f, 32));
        FreeImage_Unload(plain);
        free(f.p);
        plain = make(32, -1);
        f = save_bmp(plain, 0);
        report("32-bit with alpha: V5 header, sRGB, no profile",
               f.p && le32(f.p + 14) == 124 && le32(f.p + 30) == 3 && masks(&f, 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000) &&
               le32(f.p + 14 + 56) == CS_SRGB && le32(f.p + 14 + 112) == 0 && le32(f.p + 14 + 116) == 0 && le32(f.p + 2) == f.n && f.n == 14 + 124 + W * 4 * H);
        FreeImage_Unload(plain);
        free(f.p);
        report("no warning for any of them", warnings == 0);
    }

    free(copy);
    printf("--- %d failure(s) ---\n", failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
