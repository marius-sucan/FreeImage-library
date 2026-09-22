/* JPEG 2000 robustness: damaged or offset streams must never crash */
/* run under ASan: make asan */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static int failures = 0;
static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static FIBITMAP *mk(int w, int h) {
    FIBITMAP *d = FreeImage_Allocate(w, h, 24, 0, 0, 0);
    unsigned y, s = 88172645u;
    for (y = 0; y < (unsigned)h; y++) {
        BYTE *p = FreeImage_GetScanLine(d, y);
        unsigned n = FreeImage_GetLine(d), x;
        for (x = 0; x < n; x++) { s ^= s << 13; s ^= s >> 17; s ^= s << 5; p[x] = (BYTE)(s >> 9); }
    }
    return d;
}

static int same(FIBITMAP *a, FIBITMAP *b) {
    unsigned y, n;
    if (!a || !b) return 0;
    if (FreeImage_GetWidth(a) != FreeImage_GetWidth(b) || FreeImage_GetHeight(a) != FreeImage_GetHeight(b)
        || FreeImage_GetBPP(a) != FreeImage_GetBPP(b) || FreeImage_GetImageType(a) != FreeImage_GetImageType(b)) return 0;
    n = (FreeImage_GetWidth(a) * FreeImage_GetBPP(a) + 7) / 8;
    for (y = 0; y < FreeImage_GetHeight(a); y++)
        if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), n)) return 0;
    return 1;
}

/* encode d losslessly into a malloc'd buffer */
static BYTE *encode(FIBITMAP *d, FREE_IMAGE_FORMAT fif, DWORD *size) {
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    BYTE *p = NULL, *copy = NULL;
    if (FreeImage_SaveToMemory(fif, d, mem, 1) && FreeImage_AcquireMemory(mem, &p, size)) {
        copy = (BYTE*)malloc(*size);
        memcpy(copy, p, *size);
    }
    FreeImage_CloseMemory(mem);
    return copy;
}

static FIBITMAP *load_buf(FREE_IMAGE_FORMAT fif, const BYTE *p, DWORD n, int flags) {
    FIMEMORY *mem = FreeImage_OpenMemory((BYTE*)p, n);
    FIBITMAP *d = FreeImage_LoadFromMemory(fif, mem, flags);
    FreeImage_CloseMemory(mem);
    return d;
}

/* FreeImageIO over a memory block */
typedef struct { BYTE *p; long size, cap, pos; } membuf;
static unsigned DLL_CALLCONV mb_read(void *b, unsigned s, unsigned c, fi_handle h) {
    membuf *m = (membuf*)h; long n = (long)s * c;
    if (m->pos + n > m->size) n = m->size - m->pos;
    if (n <= 0) return 0;
    memcpy(b, m->p + m->pos, (size_t)n); m->pos += n; return (unsigned)(n / s);
}
static unsigned DLL_CALLCONV mb_write(void *b, unsigned s, unsigned c, fi_handle h) {
    membuf *m = (membuf*)h; long n = (long)s * c;
    if (m->pos + n > m->cap) { m->cap = (m->pos + n) * 2; m->p = (BYTE*)realloc(m->p, (size_t)m->cap); }
    memcpy(m->p + m->pos, b, (size_t)n); m->pos += n; if (m->pos > m->size) m->size = m->pos; return c;
}
static int DLL_CALLCONV mb_seek(fi_handle h, long o, int w) {
    membuf *m = (membuf*)h; long np = (w == SEEK_SET) ? o : (w == SEEK_CUR) ? m->pos + o : m->size + o;
    if (np < 0) return -1; m->pos = np; return 0;
}
static long DLL_CALLCONV mb_tell(fi_handle h) { return ((membuf*)h)->pos; }

static void check(int ok, const char *what) {
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok) failures++;
}

static void damage_tests(FREE_IMAGE_FORMAT fif, FIBITMAP *d, BYTE *buf, DWORD n) {
    const char *name = FreeImage_GetFormatFromFIF(fif);
    char what[128];
    unsigned i, loaded = 0, refused = 0, s = 12345;
    BYTE *tmp = (BYTE*)malloc(n + 4096);
    FIBITMAP *b;

    /* truncations: every length up to 64, then ~200 cuts */
    for (i = 1; i < n; i = (i < 64) ? i + 1 : i + (n / 200 > 1 ? n / 200 : 1)) {
        b = load_buf(fif, buf, i, 0);
        if (b) { loaded++; FreeImage_Unload(b); } else refused++;
    }
    snprintf(what, sizeof(what), "%s truncated prefixes: %u decoded, %u refused, no crash", name, loaded, refused);
    check(1, what);

    memcpy(tmp, buf, n); memset(tmp + n, 0xA5, 4096);
    b = load_buf(fif, tmp, n + 4096, 0);
    snprintf(what, sizeof(what), "%s with 4 KB of junk appended still decodes exactly", name);
    check(same(d, b), what);
    if (b) FreeImage_Unload(b);

    loaded = refused = 0;
    for (i = 0; i < 300; i++) {                 /* single corrupted bytes at pseudo-random positions */
        unsigned pos;
        s ^= s << 13; s ^= s >> 17; s ^= s << 5; pos = s % n;
        memcpy(tmp, buf, n);
        tmp[pos] ^= (BYTE)(0x01 << (i % 8));
        b = load_buf(fif, tmp, n, 0);
        if (b) { loaded++; FreeImage_Unload(b); } else refused++;
    }
    snprintf(what, sizeof(what), "%s single-bit corruptions: %u decoded, %u refused, no crash", name, loaded, refused);
    check(1, what);

    for (i = 0; i < 4; i++) {                    /* zeroed and 0xFF-filled tails of the header area */
        memcpy(tmp, buf, n);
        memset(tmp + 16 + i * 8, i & 1 ? 0xFF : 0x00, 64 < n - 16 - i * 8 ? 64 : n - 16 - i * 8);
        b = load_buf(fif, tmp, n, 0);
        if (b) FreeImage_Unload(b);
    }
    snprintf(what, sizeof(what), "%s wiped header regions: no crash", name);
    check(1, what);

    memset(tmp, 0, 64);
    b = load_buf(fif, tmp, 0, 0);  if (b) FreeImage_Unload(b);
    b = load_buf(fif, tmp, 64, 0); if (b) FreeImage_Unload(b);
    snprintf(what, sizeof(what), "%s empty and all-zero buffers: no crash", name);
    check(1, what);
    free(tmp);
}

static void geometry_tests(FREE_IMAGE_FORMAT fif, FIBITMAP *d, BYTE *buf, DWORD n) {
    const char *name = FreeImage_GetFormatFromFIF(fif);
    char what[128];
    FIBITMAP *b;
    FreeImageIO io = { mb_read, mb_write, mb_seek, mb_tell };
    membuf m;

    b = load_buf(fif, buf, n, FIF_LOAD_NOPIXELS);
    snprintf(what, sizeof(what), "%s header-only load reports the geometry without pixels", name);
    check(b && FreeImage_GetWidth(b) == FreeImage_GetWidth(d) && FreeImage_GetHeight(b) == FreeImage_GetHeight(d)
          && FreeImage_GetBPP(b) == 24 && !FreeImage_HasPixels(b), what);
    if (b) FreeImage_Unload(b);

    /* load at offset 1000, junk in front */
    m.cap = n + 1000; m.size = n + 1000; m.p = (BYTE*)malloc((size_t)m.cap); m.pos = 1000;
    memset(m.p, 0x5A, 1000); memcpy(m.p + 1000, buf, n);
    b = FreeImage_LoadFromHandle(fif, &io, (fi_handle)&m, 0);
    snprintf(what, sizeof(what), "%s load from a handle at offset 1000 decodes exactly", name);
    check(same(d, b), what);
    if (b) FreeImage_Unload(b);
    free(m.p);

    /* save and load at offset 777 */
    m.cap = 4096; m.size = 777; m.p = (BYTE*)malloc((size_t)m.cap); m.pos = 777;
    memset(m.p, 0x3C, 777);
    if (FreeImage_SaveToHandle(fif, d, &io, (fi_handle)&m, 1)) {
        m.pos = 777;
        b = FreeImage_LoadFromHandle(fif, &io, (fi_handle)&m, 0);
        snprintf(what, sizeof(what), "%s save to a handle at offset 777, reload from there, exact", name);
        check(same(d, b) && memcmp(m.p, m.p + 1, 776) == 0 && m.p[0] == 0x3C, what);
        if (b) FreeImage_Unload(b);
    } else {
        snprintf(what, sizeof(what), "%s save to a handle at offset 777", name);
        check(0, what);
    }
    free(m.p);
}

int main(void) {
    static const FREE_IMAGE_FORMAT C[] = { FIF_J2K, FIF_JP2 };
    FIBITMAP *d;
    unsigned c;

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);
    printf("--- JPEG 2000 robustness (FreeImage %s) ---\n", FreeImage_GetVersion());
    d = mk(97, 61);
    for (c = 0; c < 2; c++) {
        DWORD n = 0;
        BYTE *buf = encode(d, C[c], &n);
        char what[128];
        snprintf(what, sizeof(what), "%s lossless encode to memory (%lu bytes)", FreeImage_GetFormatFromFIF(C[c]), (unsigned long)n);
        check(buf != NULL && n > 0, what);
        if (!buf) continue;
        geometry_tests(C[c], d, buf, n);
        damage_tests(C[c], d, buf, n);
        free(buf);
    }
    FreeImage_Unload(d);
    printf("--- %d failures ---\n", failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
