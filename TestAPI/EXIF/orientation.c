/* The orientation of images that load upright: their Exif tag, raw Exif block and XMP packet say 1 */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "FreeImage.h"

static int failures = 0, checks = 0;
static char msgbuf[4096];

static void DLL_CALLCONV msgproc(FREE_IMAGE_FORMAT fif, const char *msg) {
    (void)fif;
    snprintf(msgbuf, sizeof msgbuf, "%s", msg);
}

static void check(int good, const char *what, const char *fmt, ...) {
    va_list ap;
    checks++;
    if (good) return;
    printf("  FAIL %s - ", what);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
    failures++;
}

static BYTE *slurp(const char *path, long *len) {
    FILE *f = fopen(path, "rb");
    BYTE *buf;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
    buf = (BYTE *)malloc(*len ? (size_t)*len : 1);
    if (!buf || fread(buf, 1, (size_t)*len, f) != (size_t)*len) { free(buf); fclose(f); return NULL; }
    fclose(f);
    return buf;
}

static FIBITMAP *load_mem(FREE_IMAGE_FORMAT fif, BYTE *buf, long len, int flags) {
    FIMEMORY *m = FreeImage_OpenMemory(buf, (DWORD)len);
    FIBITMAP *dib = m ? FreeImage_LoadFromMemory(fif, m, flags) : NULL;
    if (m) FreeImage_CloseMemory(m);
    return dib;
}

/* saved to memory in fif, then loaded back */
static FIBITMAP *roundtrip(FIBITMAP *dib, FREE_IMAGE_FORMAT fif, int load_flags) {
    FIMEMORY *m = FreeImage_OpenMemory(NULL, 0);
    FIBITMAP *back = NULL;
    if (m && FreeImage_SaveToMemory(fif, dib, m, 0)) {
        FreeImage_SeekMemory(m, 0, SEEK_SET);
        back = FreeImage_LoadFromMemory(fif, m, load_flags);
    }
    if (m) FreeImage_CloseMemory(m);
    return back;
}

/* ---- TIFF structures ---------------------------------------------------- */
static unsigned rd16(const BYTE *p, int be) { return be ? (unsigned)((p[0] << 8) | p[1]) : (unsigned)(p[0] | (p[1] << 8)); }
static unsigned long rd32(const BYTE *p, int be) {
    return be ? ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) | ((unsigned long)p[2] << 8) | p[3]
              : ((unsigned long)p[3] << 24) | ((unsigned long)p[2] << 16) | ((unsigned long)p[1] << 8) | p[0];
}
static void wr16(BYTE *p, unsigned v, int be) { if (be) { p[0] = (BYTE)(v >> 8); p[1] = (BYTE)v; } else { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); } }
static void wr32(BYTE *p, unsigned long v, int be) {
    if (be) { wr16(p, (unsigned)(v >> 16), 1); wr16(p + 2, (unsigned)(v & 0xFFFF), 1); }
    else { wr16(p, (unsigned)(v & 0xFFFF), 0); wr16(p + 2, (unsigned)(v >> 16), 0); }
}

/* IFD0's Orientation entry, or NULL */
static BYTE *orientation_entry(BYTE *tiff, size_t size, int *be) {
    size_t ifd, count, k;
    if (size < 8 || tiff[0] != tiff[1] || (tiff[0] != 'I' && tiff[0] != 'M')) return NULL;
    *be = tiff[0] == 'M';
    if (rd16(tiff + 2, *be) != 42) return NULL;
    ifd = rd32(tiff + 4, *be);
    if (ifd + 2 > size) return NULL;
    count = rd16(tiff + ifd, *be);
    for (k = 0; k < count && ifd + 2 + 12 * (k + 1) <= size; k++)
        if (rd16(tiff + ifd + 2 + 12 * k, *be) == 0x0112) return tiff + ifd + 2 + 12 * k;
    return NULL;
}

/* ---- the three places an orientation lives ------------------------------ */
/* -1 without the tag */
static int tag_orientation(FIBITMAP *dib) {
    FITAG *tag = NULL;
    if (!FreeImage_GetMetadata(FIMD_EXIF_MAIN, dib, "Orientation", &tag) || !tag || FreeImage_GetTagLength(tag) < 2) return -1;
    return *(const WORD *)FreeImage_GetTagValue(tag);
}

/* -1 without a raw block, 0 without the entry */
static int raw_orientation(FIBITMAP *dib) {
    FITAG *tag = NULL;
    BYTE *copy, *e;
    DWORD n;
    int be = 0, v;
    if (!FreeImage_GetMetadata(FIMD_EXIF_RAW, dib, "ExifRaw", &tag) || !tag) return -1;
    n = FreeImage_GetTagLength(tag);
    if (n < 14 || memcmp(FreeImage_GetTagValue(tag), "Exif\0\0", 6) != 0) return -2;
    copy = (BYTE *)malloc(n);
    memcpy(copy, FreeImage_GetTagValue(tag), n);
    e = orientation_entry(copy + 6, n - 6, &be);
    v = !e ? 0 : (rd16(e + 2, be) == 4) ? (int)rd32(e + 8, be) : (rd16(e + 2, be) == 1) ? e[8] : (int)rd16(e + 8, be);
    free(copy);
    return v;
}

/* the digit after tiff:Orientation, an attribute or an element; -1 without one */
static int xmp_orientation(FIBITMAP *dib) {
    FITAG *tag = NULL;
    const char *p;
    if (!FreeImage_GetMetadata(FIMD_XMP, dib, "XMLPacket", &tag) || !tag) return -1;
    p = strstr((const char *)FreeImage_GetTagValue(tag), "tiff:Orientation");
    if (!p) return -1;
    for (p += 16; *p && strchr(" \t\r\n=\"'>", *p); p++) {}
    return (*p >= '0' && *p <= '9') ? *p - '0' : -1;
}

typedef struct { int tag, raw, xmp; } Where;

static Where where(FIBITMAP *dib) {
    Where w;
    w.tag = tag_orientation(dib);
    w.raw = raw_orientation(dib);
    w.xmp = xmp_orientation(dib);
    return w;
}

/* every place the file has says 1 */
static int upright(Where w, Where file) {
    return (file.tag < 0 || w.tag == 1) && (file.raw < 0 || w.raw == 1) && (file.xmp < 0 || w.xmp == 1);
}

static int same(Where a, Where b) {
    return a.tag == b.tag && a.raw == b.raw && a.xmp == b.xmp;
}

static void show(char *out, size_t n, Where w) {
    snprintf(out, n, "tag %d, raw %d, xmp %d", w.tag, w.raw, w.xmp);
}

/* ---- a JPEG with an Exif and an XMP orientation ------------------------- */
typedef struct {
    const char *name;
    int be;         /* big-endian Exif */
    WORD type;      /* of the Orientation entry: 1 BYTE, 3 SHORT, 4 LONG */
    WORD value;
    int xmp;        /* 1 tiff:Orientation="v", 2 <tiff:Orientation>v</tiff:Orientation> */
} Case;

static BYTE *make_jpeg(const BYTE *jpg, long n, const Case *c, long *outlen) {
    static const char XMP_SIG[] = "http://ns.adobe.com/xap/1.0/";
    static const char *HEAD = "<?xpacket begin=\"\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?><x:xmpmeta xmlns:x=\"adobe:ns:meta/\">"
                              "<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
                              "<rdf:Description rdf:about=\"\" xmlns:tiff=\"http://ns.adobe.com/tiff/1.0/\"";
    static const char *TAIL = "</rdf:RDF></x:xmpmeta><?xpacket end=\"w\"?>";
    const long tiff_len = 8 + 2 + 12 + 4;
    const long app1 = 2 + 6 + tiff_len;
    char xmp[1024];
    long xlen = 0, app1x = 0;
    BYTE *out, *p, *t;
    if (c->xmp == 1) xlen = snprintf(xmp, sizeof xmp, "%s tiff:Orientation=\"%u\"/>%s", HEAD, c->value, TAIL);
    if (c->xmp == 2) xlen = snprintf(xmp, sizeof xmp, "%s>\n <tiff:Orientation>%u</tiff:Orientation>\n</rdf:Description>%s", HEAD, c->value, TAIL);
    if (xlen) app1x = 2 + (long)sizeof XMP_SIG + xlen;
    out = (BYTE *)malloc((size_t)(n + 2 + app1 + (app1x ? 2 + app1x : 0)));
    if (!out) return NULL;
    p = out;
    *p++ = 0xFF; *p++ = 0xD8;
    *p++ = 0xFF; *p++ = 0xE1; *p++ = (BYTE)(app1 >> 8); *p++ = (BYTE)app1;
    memcpy(p, "Exif\0\0", 6); p += 6;
    t = p;
    memset(t, 0, (size_t)tiff_len);
    memcpy(t, c->be ? "MM\0*" : "II*\0", 4); wr32(t + 4, 8, c->be);
    wr16(t + 8, 1, c->be);
    wr16(t + 10, 0x0112, c->be); wr16(t + 12, c->type, c->be); wr32(t + 14, 1, c->be);
    if (c->type == 4) wr32(t + 18, c->value, c->be);
    else if (c->type == 1) t[18] = (BYTE)c->value;
    else wr16(t + 18, c->value, c->be);
    p += tiff_len;
    if (app1x) {
        *p++ = 0xFF; *p++ = 0xE1; *p++ = (BYTE)(app1x >> 8); *p++ = (BYTE)app1x;
        memcpy(p, XMP_SIG, sizeof XMP_SIG); p += sizeof XMP_SIG;
        memcpy(p, xmp, (size_t)xlen); p += xlen;
    }
    memcpy(p, jpg + 2, (size_t)(n - 2)); p += n - 2;
    *outlen = (long)(p - out);
    return out;
}

/* ---- orientations patched into files ------------------------------------ */
/* the first TIFF structure with a SHORT Orientation gets value; 0 when there is none */
static int patch_exif(BYTE *buf, long n, WORD value) {
    long i;
    for (i = 0; i + 8 <= n; i++) {
        int be = 0;
        BYTE *e;
        if (!((buf[i] == 'I' && buf[i + 1] == 'I' && buf[i + 2] == 42 && buf[i + 3] == 0) ||
              (buf[i] == 'M' && buf[i + 1] == 'M' && buf[i + 2] == 0 && buf[i + 3] == 42))) continue;
        e = orientation_entry(buf + i, (size_t)(n - i), &be);
        if (e && rd16(e + 2, be) == 3) { wr16(e + 8, value, be); return 1; }
    }
    return 0;
}

static int patch_xmp(BYTE *buf, long n, char digit) {
    static const char ATTR[] = "tiff:Orientation=\"";
    long i;
    for (i = 0; i + (long)sizeof ATTR < n; i++)
        if (memcmp(buf + i, ATTR, sizeof ATTR - 1) == 0) { buf[i + sizeof ATTR - 1] = (BYTE)digit; return 1; }
    return 0;
}

/* a file loaded whole and header-only: upright, at the size of the unpatched file */
static void check_upright_file(const char *what, FREE_IMAGE_FORMAT fif, const char *path, int exif, char xmp_digit) {
    long n = 0;
    char a[128], b[128];
    BYTE *buf = slurp(path, &n);
    FIBITMAP *orig, *full, *hdr;
    Where file, w;
    if (!buf) { check(0, what, "cannot read %s", path); return; }
    orig = load_mem(fif, buf, n, 0);
    check(orig && tag_orientation(orig) <= 1 && raw_orientation(orig) <= 1 && xmp_orientation(orig) <= 1, what, "the unpatched file is not upright");
    if ((exif && !patch_exif(buf, n, exif)) || (xmp_digit && !patch_xmp(buf, n, xmp_digit))) {
        check(0, what, "nothing to patch in %s", path);
    } else {
        file.tag = exif ? exif : -1;
        file.raw = exif ? exif : -1;
        file.xmp = xmp_digit ? xmp_digit - '0' : -1;
        full = load_mem(fif, buf, n, 0);
        hdr = load_mem(fif, buf, n, FIF_LOAD_NOPIXELS);
        if (!orig || !full || !hdr) {
            check(0, what, "a load failed: %s", msgbuf);
        } else {
            w = where(full); show(a, sizeof a, w);
            check(upright(w, file), what, "full load: %s", a);
            w = where(hdr); show(b, sizeof b, w);
            check(upright(w, file), what, "header-only load: %s", b);
            check(FreeImage_GetWidth(full) == FreeImage_GetWidth(orig) && FreeImage_GetHeight(full) == FreeImage_GetHeight(orig),
                  what, "%ux%u, the unpatched file %ux%u", FreeImage_GetWidth(full), FreeImage_GetHeight(full), FreeImage_GetWidth(orig), FreeImage_GetHeight(orig));
            printf("  %-44s full: %s; header-only: %s\n", what, a, b);
        }
        if (full) FreeImage_Unload(full);
        if (hdr) FreeImage_Unload(hdr);
    }
    if (orig) FreeImage_Unload(orig);
    free(buf);
}

int main(void) {
    static const Case CASES[] = {
        /* name                            be type value xmp */
        { "Orientation 1",                 0, 3, 1, 0 },
        { "Orientation 2",                 0, 3, 2, 0 },
        { "Orientation 3",                 0, 3, 3, 0 },
        { "Orientation 4",                 0, 3, 4, 0 },
        { "Orientation 5",                 0, 3, 5, 0 },
        { "Orientation 6",                 0, 3, 6, 0 },
        { "Orientation 7",                 0, 3, 7, 0 },
        { "Orientation 8",                 0, 3, 8, 0 },
        { "Orientation 6, big-endian",     1, 3, 6, 0 },
        { "Orientation 3, big-endian",     1, 3, 3, 0 },
        { "Orientation 6 as a LONG",       0, 4, 6, 0 },
        { "Orientation 8 as a LONG, BE",   1, 4, 8, 0 },
        { "Orientation 6, XMP attribute",  0, 3, 6, 1 },
        { "Orientation 5, XMP element",    1, 3, 5, 2 },
        { "Orientation 1, XMP attribute",  0, 3, 1, 1 },
        { "Orientation 9, XMP attribute",  0, 3, 9, 1 },
        { "Orientation 6 as a BYTE",       0, 1, 6, 1 },
    };
    long bn = 0;
    int i;
    BYTE *base;

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(msgproc);
    base = slurp("../JPEG/data/fi_jpeg_420.jpg", &bn);
    if (!base) {
        printf("cannot read ../JPEG/data/fi_jpeg_420.jpg\n");
        return 1;
    }

    /* --- 1. JPEG_EXIFROTATE turns the pixels and sets 1 ------------------- */
    printf("JPEG_EXIFROTATE, full and header-only, then saved as JPEG and reloaded with it\n");
    for (i = 0; i < (int)(sizeof CASES / sizeof CASES[0]); i++) {
        const Case *c = &CASES[i];
        const int turns = (c->type == 3 || c->type == 4) && c->value >= 2 && c->value <= 8;
        long len = 0;
        char a[128], b[128];
        BYTE *buf = make_jpeg(base, bn, c, &len);
        FIBITMAP *plain = buf ? load_mem(FIF_JPEG, buf, len, 0) : NULL;
        FIBITMAP *full = buf ? load_mem(FIF_JPEG, buf, len, JPEG_EXIFROTATE) : NULL;
        FIBITMAP *hdr = buf ? load_mem(FIF_JPEG, buf, len, JPEG_EXIFROTATE | FIF_LOAD_NOPIXELS) : NULL;
        if (!plain || !full || !hdr) {
            check(0, c->name, "a load failed: %s", msgbuf);
        } else {
            Where file = where(plain), wf = where(full), wh = where(hdr);
            show(a, sizeof a, file);
            check(file.raw == c->value && (c->type == 1 || file.tag == c->value) && (!c->xmp || file.xmp == c->value),
                  c->name, "a load without JPEG_EXIFROTATE has %s", a);
            show(a, sizeof a, wf); show(b, sizeof b, wh);
            if (turns) {
                check(upright(wf, file), c->name, "full load: %s", a);
                check(upright(wh, file), c->name, "header-only load: %s", b);
            } else {
                check(same(wf, file), c->name, "nothing turns, yet the full load has %s", a);
                check(same(wh, file), c->name, "nothing turns, yet the header-only load has %s", b);
            }
            if (turns) {
                /* a viewer that honours the tag must not turn the saved JPEG again */
                FIBITMAP *back = roundtrip(full, FIF_JPEG, JPEG_EXIFROTATE);
                if (!back) {
                    check(0, c->name, "the JPEG save or reload failed: %s", msgbuf);
                } else {
                    Where wb = where(back);
                    show(b, sizeof b, wb);
                    check(upright(wb, file), c->name, "the saved JPEG has %s", b);
                    check(FreeImage_GetWidth(back) == FreeImage_GetWidth(full) && FreeImage_GetHeight(back) == FreeImage_GetHeight(full),
                          c->name, "the saved JPEG turned again: %ux%u, saved %ux%u",
                          FreeImage_GetWidth(back), FreeImage_GetHeight(back), FreeImage_GetWidth(full), FreeImage_GetHeight(full));
                    FreeImage_Unload(back);
                }
            }
            printf("  %-32s file: tag %d, raw %d, xmp %d -> %s\n", c->name, file.tag, file.raw, file.xmp, a);
        }
        if (plain) FreeImage_Unload(plain);
        if (full) FreeImage_Unload(full);
        if (hdr) FreeImage_Unload(hdr);
        free(buf);
    }

    /* --- 2. the writers copy what the loaders leave -------------------------- */
    printf("\nAn unturned JPEG with Orientation 6, saved as WebP and as JPEG XL\n");
    {
        static const Case C6 = { "Orientation 6", 0, 3, 6, 1 };
        long len = 0;
        char a[128];
        BYTE *buf = make_jpeg(base, bn, &C6, &len);
        FIBITMAP *plain = buf ? load_mem(FIF_JPEG, buf, len, 0) : NULL;
        FIBITMAP *webp = plain ? roundtrip(plain, FIF_WEBP, 0) : NULL;
        FIBITMAP *jxl = plain ? roundtrip(plain, FIF_JXL, 0) : NULL;
        FIBITMAP *jxlhdr = NULL;
        if (!plain || !webp || !jxl) {
            check(0, "WebP and JPEG XL", "a save or load failed: %s", msgbuf);
        } else {
            Where file = where(plain), ww = where(webp), wj = where(jxl);
            show(a, sizeof a, ww);
            /* WebP has no orientation of its own: the tag stays and still applies */
            check(same(ww, file), "WebP", "the reload has %s", a);
            printf("  %-32s %s\n", "WebP keeps it", a);
            show(a, sizeof a, wj);
            /* a JPEG XL decoder applies the codestream's orientation, 1 here, and ignores Exif's */
            check(upright(wj, file), "JPEG XL", "the reload has %s", a);
            check(FreeImage_GetWidth(jxl) == FreeImage_GetWidth(plain) && FreeImage_GetHeight(jxl) == FreeImage_GetHeight(plain),
                  "JPEG XL", "%ux%u, saved %ux%u", FreeImage_GetWidth(jxl), FreeImage_GetHeight(jxl), FreeImage_GetWidth(plain), FreeImage_GetHeight(plain));
            printf("  %-32s %s\n", "JPEG XL says 1", a);
            {
                FIMEMORY *m = FreeImage_OpenMemory(NULL, 0);
                if (m && FreeImage_SaveToMemory(FIF_JXL, plain, m, 0)) {
                    FreeImage_SeekMemory(m, 0, SEEK_SET);
                    jxlhdr = FreeImage_LoadFromMemory(FIF_JXL, m, FIF_LOAD_NOPIXELS);
                }
                if (m) FreeImage_CloseMemory(m);
                show(a, sizeof a, jxlhdr ? where(jxlhdr) : file);
                check(jxlhdr && upright(where(jxlhdr), file), "JPEG XL", "header-only reload: %s", a);
            }
        }
        if (plain) FreeImage_Unload(plain);
        if (webp) FreeImage_Unload(webp);
        if (jxl) FreeImage_Unload(jxl);
        if (jxlhdr) FreeImage_Unload(jxlhdr);
        free(buf);
    }

    /* --- 3. HEIF and AVIF: irot and imir outrank Exif ------------------------ */
    printf("\nHEIF and AVIF files with a patched orientation\n");
    check_upright_file("HEIF, Exif Orientation 6", FIF_HEIF, "../HEIF/data/hevc32-mini-exif.heif", 6, 0);
    check_upright_file("HEIF, irot, XMP tiff:Orientation 6", FIF_HEIF, "../HEIF/data/rainbow_irot1.heic", 0, '6');
    check_upright_file("AVIF, Exif Orientation 8", FIF_AVIF, "../AVIF/data/paris_icc_exif_xmp.avif", 8, 0);

    /* --- 4. camera RAW takes the metadata of a header-only preview load ----- */
    printf("\nThe metadata of a header-only JPEG_EXIFROTATE load, as PluginRAW clones it\n");
    {
        static const Case C6 = { "Orientation 6", 1, 3, 6, 2 };
        long len = 0;
        char a[128];
        BYTE *buf = make_jpeg(base, bn, &C6, &len);
        FIBITMAP *plain = buf ? load_mem(FIF_JPEG, buf, len, FIF_LOAD_NOPIXELS) : NULL;
        FIBITMAP *preview = buf ? load_mem(FIF_JPEG, buf, len, JPEG_EXIFROTATE | FIF_LOAD_NOPIXELS) : NULL;
        FIBITMAP *decode = FreeImage_Allocate(8, 8, 24, 0, 0, 0);
        if (!plain || !preview || !decode || !FreeImage_CloneMetadata(decode, preview)) {
            check(0, "RAW", "a load or the metadata clone failed: %s", msgbuf);
        } else {
            Where w = where(decode);
            show(a, sizeof a, w);
            check(upright(w, where(plain)), "RAW", "the decode gets %s", a);
            printf("  %-32s %s\n", "the decode gets", a);
        }
        if (plain) FreeImage_Unload(plain);
        if (preview) FreeImage_Unload(preview);
        if (decode) FreeImage_Unload(decode);
        free(buf);
    }

    free(base);
    printf("\n--- %d check(s), %d failure(s) ---\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
