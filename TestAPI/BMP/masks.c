/* FreeImage 3 - BMP test: colour masks, alpha, BI_ALPHABITFIELDS and pixel offsets, on BMPs built in memory */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

#define W 9
#define H 5
#define BMP_BITFIELDS 3
#define BMP_ALPHABITFIELDS 6

static int failures = 0;

static void report(const char *what, int ok) {
    printf("  %-64s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

/* ---------- writing a BMP ---------- */

typedef struct { BYTE *p; unsigned n; } BUF;

static void put(BUF *b, const void *data, unsigned n) {
    b->p = (BYTE *)realloc(b->p, b->n + n);
    memcpy(b->p + b->n, data, n);
    b->n += n;
}
static void put8(BUF *b, unsigned v) { BYTE c = (BYTE)v; put(b, &c, 1); }
static void put16(BUF *b, unsigned v) { put8(b, v & 255); put8(b, (v >> 8) & 255); }
static void put32(BUF *b, DWORD v) { put16(b, v & 0xFFFF); put16(b, (v >> 16) & 0xFFFF); }

/* the test pattern, 8 bits per channel */
static BYTE chan(unsigned x, unsigned y, int c) { return (BYTE)((x * 29 + y * 53 + c * 101) & 255); }
static BYTE alpha8(unsigned x, unsigned y) { return (BYTE)((x * 31 + y * 7) & 255); }

static unsigned shift_of(DWORD mask) { unsigned s = 0; if (!mask) return 0; while (!((mask >> s) & 1)) s++; return s; }

/* an 8-bit value put into a mask's bits, and what a decoder should give back: the nearest 8-bit value */
static DWORD pack(DWORD mask, BYTE v) {
    const DWORD max = mask >> shift_of(mask);
    if (!mask) return 0;
    return ((DWORD)(((unsigned long long)v * max + 127) / 255) << shift_of(mask)) & mask;
}
static BYTE unpack(DWORD mask, DWORD pixel) {
    const DWORD max = mask >> shift_of(mask);
    if (!mask) return 0;
    return (BYTE)((((unsigned long long)((pixel & mask) >> shift_of(mask))) * 255 + max / 2) / max);
}

typedef struct {
    unsigned header;    /* 12, 40, 52, 56, 64, 108, 124 */
    unsigned bpp;       /* 16, 24 or 32 */
    unsigned compression;
    DWORD masks[4];     /* written for BI_BITFIELDS / BI_ALPHABITFIELDS, in the header from 52 bytes on */
    unsigned after;     /* masks written after a 40-byte header: 0, 3 or 4 */
    int topdown;
    int alpha_zero;     /* the fourth byte or alpha bits all 0 */
    DWORD junk;         /* set in every pixel, outside the masks */
    DWORD header_alpha; /* non-zero: the header's alpha mask, not masks[3] */
    long cut;           /* > 0: the file ends this many bytes into the pixels */
    unsigned gap;       /* bytes between the headers and the pixels */
} SPEC;

/* the pixel value of (x, y) for this layout; BI_RGB uses the default layouts */
static DWORD pixel_value(const SPEC *s, unsigned x, unsigned y) {
    DWORD m[4];
    int i;
    if (s->compression == 0) {
        if (s->bpp == 16) { m[0] = 0x7C00; m[1] = 0x03E0; m[2] = 0x001F; m[3] = 0; }
        else { m[0] = 0x00FF0000; m[1] = 0x0000FF00; m[2] = 0x000000FF; m[3] = 0xFF000000; }
    } else {
        memcpy(m, s->masks, sizeof m);
    }
    {
        DWORD v = 0;
        for (i = 0; i < 3; i++) v |= pack(m[i], chan(x, y, i));
        v |= pack(m[3], s->alpha_zero ? 0 : alpha8(x, y));
        return v | s->junk;
    }
}

static BUF build(const SPEC *s) {
    BUF b = { NULL, 0 }, info = { NULL, 0 }, bits = { NULL, 0 };
    const unsigned pitch = ((W * s->bpp + 31) / 32) * 4;
    unsigned x, i;
    if (s->header == 12) {
        put32(&info, 12); put16(&info, W); put16(&info, H); put16(&info, 1); put16(&info, s->bpp);
    } else {
        put32(&info, s->header); put32(&info, W); put32(&info, (DWORD)(s->topdown ? -H : H)); put16(&info, 1); put16(&info, s->bpp);
        put32(&info, s->compression); put32(&info, 0); put32(&info, 2835); put32(&info, 2835); put32(&info, 0); put32(&info, 0);
        if (s->header == 64) {
            for (i = 0; i < 24; i++) put8(&info, 0);
        } else {
            if (s->header >= 52) { put32(&info, s->masks[0]); put32(&info, s->masks[1]); put32(&info, s->masks[2]); }
            if (s->header >= 56) put32(&info, s->header_alpha ? s->header_alpha : s->masks[3]);
            if (s->header >= 108) { put32(&info, 0x73524742); for (i = 0; i < 48; i++) put8(&info, 0); }
            if (s->header >= 124) { put32(&info, 4); put32(&info, 0); put32(&info, 0); put32(&info, 0); }
        }
        for (i = 0; i < s->after; i++) put32(&info, s->masks[i]);
    }
    /* bottom-up unless top-down */
    for (i = 0; i < H; i++) {
        const unsigned y_img = s->topdown ? i : H - 1 - i;
        for (x = 0; x < W; x++) {
            const DWORD v = pixel_value(s, x, y_img);
            if (s->bpp == 16) put16(&bits, v);
            else if (s->bpp == 24) { put16(&bits, v & 0xFFFF); put8(&bits, (v >> 16) & 255); }
            else put32(&bits, v);
        }
        for (x = W * s->bpp / 8; x < pitch; x++) put8(&bits, 0);
    }
    put8(&b, 'B'); put8(&b, 'M');
    put32(&b, 14 + info.n + s->gap + bits.n); put16(&b, 0); put16(&b, 0); put32(&b, 14 + info.n + s->gap);
    put(&b, info.p, info.n);
    for (i = 0; i < s->gap; i++) put8(&b, 0xAA);
    put(&b, bits.p, (s->cut > 0) ? (unsigned)s->cut : bits.n);
    free(info.p); free(bits.p);
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

/* ---------- checking ---------- */

/* every pixel against the layout's own values; alpha: 1 the file's, 0 opaque */
static int pixels_ok(FIBITMAP *dib, const SPEC *s, int alpha) {
    const unsigned bytes = FreeImage_GetBPP(dib) / 8;
    DWORD m[4];
    unsigned x, y;
    if (s->compression == 0) {
        if (s->bpp == 16) { m[0] = 0x7C00; m[1] = 0x03E0; m[2] = 0x001F; m[3] = 0; }
        else { m[0] = 0x00FF0000; m[1] = 0x0000FF00; m[2] = 0x000000FF; m[3] = 0xFF000000; }
    } else {
        memcpy(m, s->masks, sizeof m);
    }
    for (y = 0; y < H; y++) {
        const BYTE *row = FreeImage_GetScanLine(dib, H - 1 - y);
        for (x = 0; x < W; x++) {
            const DWORD v = pixel_value(s, x, y);
            const BYTE *p = row + x * bytes;
            if (p[FI_RGBA_RED] != unpack(m[0], v) || p[FI_RGBA_GREEN] != unpack(m[1], v) || p[FI_RGBA_BLUE] != unpack(m[2], v)) return 0;
            if (bytes == 4 && p[FI_RGBA_ALPHA] != (alpha ? unpack(m[3], v) : 0xFF)) return 0;
        }
    }
    return 1;
}

static int is(FIBITMAP *dib, unsigned bpp, int transparent) {
    return dib && FreeImage_GetBPP(dib) == bpp && FreeImage_GetWidth(dib) == W && FreeImage_GetHeight(dib) == H &&
           FreeImage_IsTransparent(dib) == transparent;
}

/* a full load and a header-only load of the same file */
static void expect(const char *what, const SPEC *s, unsigned bpp, int alpha) {
    char line[128];
    FIBITMAP *dib = load(s, 0), *hdr = load(s, FIF_LOAD_NOPIXELS);
    snprintf(line, sizeof line, "%s: %u-bit%s", what, bpp, alpha ? ", alpha" : ", opaque");
    report(line, is(dib, bpp, alpha) && pixels_ok(dib, s, alpha) && hdr && FreeImage_GetBPP(hdr) == bpp);
    if (dib) FreeImage_Unload(dib);
    if (hdr) FreeImage_Unload(hdr);
}

/* a save to memory, the header it wrote and the image it loads back */
typedef struct { DWORD header, compression, alpha_mask, cs_type; int sizes_ok; FIBITMAP *reloaded; } SAVED;

static DWORD le32(const BYTE *p) { return (DWORD)p[0] | ((DWORD)p[1] << 8) | ((DWORD)p[2] << 16) | ((DWORD)p[3] << 24); }

static SAVED save_and_load(FIBITMAP *src) {
    SAVED r;
    BYTE *bytes = NULL;
    DWORD size = 0;
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    memset(&r, 0, sizeof r);
    if (FreeImage_SaveToMemory(FIF_BMP, src, mem, 0) && FreeImage_AcquireMemory(mem, &bytes, &size) && size > 54) {
        r.header = le32(bytes + 14);
        r.compression = le32(bytes + 30);
        if (r.header >= 124) { r.alpha_mask = le32(bytes + 66); r.cs_type = le32(bytes + 70); }
        r.sizes_ok = le32(bytes + 2) == size && le32(bytes + 10) == 14 + r.header && (r.compression == 0 || le32(bytes + 34) == size - 14 - r.header);
        FreeImage_SeekMemory(mem, 0, SEEK_SET);
        r.reloaded = FreeImage_LoadFromMemory(FIF_BMP, mem, 0);
    }
    FreeImage_CloseMemory(mem);
    return r;
}

static void DLL_CALLCONV quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

int main(void) {
    static const DWORD BGRA[4] = { 0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000 };
    static const DWORD BGRX[4] = { 0x00FF0000, 0x0000FF00, 0x000000FF, 0 };
    static const DWORD RGBA[4] = { 0x000000FF, 0x0000FF00, 0x00FF0000, 0xFF000000 };
    static const DWORD RGBX[4] = { 0x000000FF, 0x0000FF00, 0x00FF0000, 0 };
    static const DWORD R10[4] = { 0x3FF00000, 0x000FFC00, 0x000003FF, 0xC0000000 };
    static const DWORD R11[4] = { 0xFFE00000, 0x001FFC00, 0x000003FF, 0 };
    static const DWORD M565[4] = { 0xF800, 0x07E0, 0x001F, 0 };
    static const DWORD M4444[4] = { 0x0F00, 0x00F0, 0x000F, 0xF000 };
    static const DWORD M1555[4] = { 0x7C00, 0x03E0, 0x001F, 0x8000 };
    static const DWORD M444[4] = { 0x0F00, 0x00F0, 0x000F, 0 };
    SPEC s;
    FIBITMAP *dib;

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);

    printf("32-bit, the fourth byte\n");
    memset(&s, 0, sizeof s); s.header = 40; s.bpp = 32; s.alpha_zero = 1;
    expect("BI_RGB, fourth byte 0 everywhere", &s, 32, 0);
    s.alpha_zero = 0;
    expect("BI_RGB, alpha in the fourth byte (FreeImage's own saves)", &s, 32, 1);
    s.header = 12; s.alpha_zero = 1;
    expect("OS/2 1.x header, fourth byte 0", &s, 32, 0);
    s.header = 64;
    expect("OS/2 2.x header, fourth byte 0", &s, 32, 0);
    memset(&s, 0, sizeof s); s.header = 40; s.bpp = 32; s.compression = BMP_BITFIELDS; memcpy(s.masks, BGRA, sizeof s.masks); s.after = 3;
    expect("BI_BITFIELDS, 40-byte header: no alpha mask, opaque", &s, 32, 0);
    memset(&s, 0, sizeof s); s.header = 124; s.bpp = 32; s.compression = BMP_BITFIELDS; memcpy(s.masks, BGRX, sizeof s.masks); s.junk = 0x80000000;
    expect("V5, alpha mask 0, fourth byte non-zero: opaque", &s, 32, 0);
    s.junk = 0;
    s.masks[3] = 0xFF000000;
    expect("V5, alpha mask FF000000", &s, 32, 1);
    s.alpha_zero = 1;
    expect("V5, alpha mask FF000000, alpha 0: transparent as declared", &s, 32, 1);

    printf("32-bit masks\n");
    memset(&s, 0, sizeof s); s.header = 124; s.bpp = 32; s.compression = BMP_BITFIELDS; memcpy(s.masks, RGBA, sizeof s.masks);
    expect("V5, R and B swapped, alpha", &s, 32, 1);
    memcpy(s.masks, RGBX, sizeof s.masks); s.junk = 0x5A000000;
    expect("V5, R and B swapped, no alpha", &s, 32, 0);
    s.junk = 0;
    memcpy(s.masks, R10, sizeof s.masks);
    expect("V5, 10-10-10-2", &s, 32, 1);
    memset(&s, 0, sizeof s); s.header = 40; s.bpp = 32; s.compression = BMP_BITFIELDS; memcpy(s.masks, R11, sizeof s.masks); s.after = 3;
    expect("40-byte header, 11-11-10", &s, 32, 0);
    memset(&s, 0, sizeof s); s.header = 56; s.bpp = 32; s.compression = BMP_BITFIELDS; memcpy(s.masks, RGBA, sizeof s.masks);
    expect("56-byte header, R and B swapped, alpha", &s, 32, 1);
    memset(&s, 0, sizeof s); s.header = 52; s.bpp = 32; s.compression = BMP_BITFIELDS; memcpy(s.masks, RGBA, sizeof s.masks);
    expect("52-byte header, R and B swapped: no alpha mask there", &s, 32, 0);
    memset(&s, 0, sizeof s); s.header = 124; s.bpp = 32; s.compression = BMP_BITFIELDS; memcpy(s.masks, RGBX, sizeof s.masks);
    s.header_alpha = 0x00FF0000;
    expect("V5, alpha mask overlapping blue: no alpha", &s, 32, 0);

    printf("16-bit masks\n");
    memset(&s, 0, sizeof s); s.header = 40; s.bpp = 16;
    dib = load(&s, 0);
    report("BI_RGB: 16-bit 5-5-5 kept", dib && FreeImage_GetBPP(dib) == 16 && FreeImage_GetGreenMask(dib) == 0x03E0);
    if (dib) FreeImage_Unload(dib);
    s.compression = BMP_BITFIELDS; memcpy(s.masks, M565, sizeof s.masks); s.after = 3;
    dib = load(&s, 0);
    report("BI_BITFIELDS 5-6-5: 16-bit 5-6-5 kept", dib && FreeImage_GetBPP(dib) == 16 && FreeImage_GetGreenMask(dib) == 0x07E0);
    if (dib) FreeImage_Unload(dib);
    memcpy(s.masks, M444, sizeof s.masks);
    expect("40-byte header, 4-4-4", &s, 24, 0);
    memset(&s, 0, sizeof s); s.header = 124; s.bpp = 16; s.compression = BMP_BITFIELDS; memcpy(s.masks, M4444, sizeof s.masks);
    expect("V5, 4-4-4-4", &s, 32, 1);
    memcpy(s.masks, M1555, sizeof s.masks);
    expect("V5, 1-5-5-5", &s, 32, 1);
    s.masks[3] = 0xFF000000;
    dib = load(&s, 0);
    report("V5, alpha mask outside 16 bits: 16-bit 5-5-5 kept", dib && FreeImage_GetBPP(dib) == 16 && FreeImage_GetRedMask(dib) == 0x7C00);
    if (dib) FreeImage_Unload(dib);

    printf("BI_ALPHABITFIELDS\n");
    memset(&s, 0, sizeof s); s.header = 40; s.bpp = 32; s.compression = BMP_ALPHABITFIELDS; memcpy(s.masks, RGBA, sizeof s.masks); s.after = 4;
    expect("32-bit, four masks after a 40-byte header", &s, 32, 1);
    s.topdown = 1;
    expect("32-bit, top-down", &s, 32, 1);
    memset(&s, 0, sizeof s); s.header = 40; s.bpp = 16; s.compression = BMP_ALPHABITFIELDS; memcpy(s.masks, M4444, sizeof s.masks); s.after = 4;
    expect("16-bit 4-4-4-4", &s, 32, 1);
    {
        BUF b;
        FIMEMORY *mem;
        memset(&s, 0, sizeof s); s.header = 40; s.bpp = 32; s.compression = BMP_ALPHABITFIELDS; memcpy(s.masks, RGBA, sizeof s.masks); s.after = 4;
        b = build(&s);
        b.p[28] = 8;  /* biBitCount */
        mem = FreeImage_OpenMemory(b.p, b.n);
        dib = FreeImage_LoadFromMemory(FIF_BMP, mem, 0);
        report("8-bit: refused", dib == NULL);
        if (dib) FreeImage_Unload(dib);
        FreeImage_CloseMemory(mem);
        free(b.p);
    }

    printf("cut files\n");
    memset(&s, 0, sizeof s); s.header = 124; s.bpp = 32; s.compression = BMP_BITFIELDS; memcpy(s.masks, RGBX, sizeof s.masks);
    s.cut = 2 * W * 4;
    dib = load(&s, 0);
    {
        /* the rows the file holds are the bottom two; the missing rows are opaque black */
        int ok = is(dib, 32, 0);
        unsigned x, y;
        for (y = 0; ok && y < H - 2; y++) {
            const BYTE *row = FreeImage_GetScanLine(dib, H - 1 - y);
            for (x = 0; x < W; x++) ok = ok && row[x * 4 + FI_RGBA_RED] == 0 && row[x * 4 + FI_RGBA_ALPHA] == 0xFF;
        }
        for (y = H - 2; ok && y < H; y++) {
            const BYTE *row = FreeImage_GetScanLine(dib, H - 1 - y);
            for (x = 0; x < W; x++) ok = ok && row[x * 4 + FI_RGBA_RED] == chan(x, y, 0) && row[x * 4 + FI_RGBA_ALPHA] == 0xFF;
        }
        report("V5, R and B swapped, cut after 2 rows: kept, the rest opaque", ok);
    }
    if (dib) FreeImage_Unload(dib);
    memset(&s, 0, sizeof s); s.header = 40; s.bpp = 16; s.compression = BMP_BITFIELDS; memcpy(s.masks, M565, sizeof s.masks); s.after = 3;
    {
        /* the file ends inside the masks */
        BUF b = build(&s);
        FIMEMORY *mem = FreeImage_OpenMemory(b.p, 14 + 40 + 4);
        dib = FreeImage_LoadFromMemory(FIF_BMP, mem, 0);
        report("cut inside the masks: 16-bit 5-5-5, blank", dib && FreeImage_GetBPP(dib) == 16 && FreeImage_GetRedMask(dib) == 0x7C00);
        if (dib) FreeImage_Unload(dib);
        FreeImage_CloseMemory(mem);
        free(b.p);
    }

    printf("pixel offsets\n");
    memset(&s, 0, sizeof s); s.header = 12; s.bpp = 24; s.gap = 16;
    expect("OS/2 1.x header, 24-bit, 16 bytes before the pixels", &s, 24, 0);
    s.bpp = 32; s.alpha_zero = 1;
    expect("OS/2 1.x header, 32-bit, 16 bytes before the pixels", &s, 32, 0);
    memset(&s, 0, sizeof s); s.header = 40; s.bpp = 24; s.gap = 16;
    expect("40-byte header, 24-bit, 16 bytes before the pixels", &s, 24, 0);

    printf("FreeImage's own 32-bit saves\n");
    {
        FIBITMAP *src = FreeImage_Allocate(W, H, 32, 0, 0, 0);
        SAVED r;
        unsigned x, y;
        int ok;
        for (y = 0; y < H; y++) {
            BYTE *row = FreeImage_GetScanLine(src, y);
            for (x = 0; x < W; x++) {
                row[x * 4 + FI_RGBA_RED] = chan(x, y, 0); row[x * 4 + FI_RGBA_GREEN] = chan(x, y, 1);
                row[x * 4 + FI_RGBA_BLUE] = chan(x, y, 2); row[x * 4 + FI_RGBA_ALPHA] = alpha8(x, y);
            }
        }
        r = save_and_load(src);
        report("alpha: BITMAPV5HEADER, BI_BITFIELDS, alpha mask, sRGB", r.header == 124 && r.compression == BMP_BITFIELDS &&
               r.alpha_mask == 0xFF000000 && r.cs_type == 0x73524742 && r.sizes_ok);
        ok = r.reloaded && FreeImage_GetBPP(r.reloaded) == 32 && FreeImage_IsTransparent(r.reloaded);
        for (y = 0; ok && y < H; y++) ok = memcmp(FreeImage_GetScanLine(r.reloaded, y), FreeImage_GetScanLine(src, y), W * 4) == 0;
        report("  alpha survives a save and a load", ok);
        if (r.reloaded) FreeImage_Unload(r.reloaded);
        /* opaque, alpha 0 everywhere (never set), CMYK: the 40-byte header as before */
        for (y = 0; y < H; y++) {
            BYTE *row = FreeImage_GetScanLine(src, y);
            for (x = 0; x < W; x++) row[x * 4 + FI_RGBA_ALPHA] = 0xFF;
        }
        r = save_and_load(src);
        report("alpha 0xFF everywhere: 40-byte header, BI_RGB", r.header == 40 && r.compression == 0 && r.sizes_ok &&
               r.reloaded && !FreeImage_IsTransparent(r.reloaded));
        if (r.reloaded) FreeImage_Unload(r.reloaded);
        for (y = 0; y < H; y++) {
            BYTE *row = FreeImage_GetScanLine(src, y);
            for (x = 0; x < W; x++) row[x * 4 + FI_RGBA_ALPHA] = 0;
        }
        r = save_and_load(src);
        report("alpha 0 everywhere: 40-byte header, reloads opaque", r.header == 40 && r.compression == 0 && r.reloaded &&
               !FreeImage_IsTransparent(r.reloaded) && FreeImage_GetScanLine(r.reloaded, 0)[FI_RGBA_ALPHA] == 0xFF);
        if (r.reloaded) FreeImage_Unload(r.reloaded);
        for (y = 0; y < H; y++) {
            BYTE *row = FreeImage_GetScanLine(src, y);
            for (x = 0; x < W; x++) row[x * 4 + FI_RGBA_ALPHA] = alpha8(x, y);
        }
        FreeImage_GetICCProfile(src)->flags |= FIICC_COLOR_IS_CMYK;
        r = save_and_load(src);
        report("CMYK: 40-byte header, the fourth byte is K", r.header == 40 && r.compression == 0);
        if (r.reloaded) FreeImage_Unload(r.reloaded);
        FreeImage_Unload(src);
    }

    printf("--- %d failure(s) ---\n", failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
