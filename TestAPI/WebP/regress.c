/* WebP round-trip test: lossless must be exact, lossy keeps geometry */
/* WebPConfig.exact is 0: RGB under alpha 0 may change */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "FreeImage.h"

static int failures = 0;

static void fail(const char *fmt, ...) {
    va_list ap;
    printf("  FAIL ");
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
    failures++;
}

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static unsigned long long digest(FIBITMAP *dib) {
    unsigned long long h = 1469598103934665603ULL;
    unsigned w = FreeImage_GetWidth(dib), ht = FreeImage_GetHeight(dib);
    unsigned bpp = FreeImage_GetBPP(dib), y;
    size_t row = (size_t)w * (bpp / 8), i;
    const unsigned char *p;
    unsigned hdr[3];
    hdr[0] = w; hdr[1] = ht; hdr[2] = bpp;
    p = (const unsigned char *)hdr;
    for (i = 0; i < sizeof(hdr); i++) { h ^= p[i]; h *= 1099511628211ULL; }
    for (y = 0; y < ht; y++) {
        p = (const unsigned char *)FreeImage_GetScanLine(dib, y);
        for (i = 0; i < row; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    }
    return h;
}

static const char *scratch(const char *name) {
    static char buf[512];
    const char *dir = getenv("WEBP_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

/* 1 when no visible pixel differs */
static int same_visibly(FIBITMAP *a, FIBITMAP *b, long *hidden_out) {
    unsigned w = FreeImage_GetWidth(a), h = FreeImage_GetHeight(a), x, y;
    long visible = 0, hidden = 0;
    if (FreeImage_GetWidth(b) != w || FreeImage_GetHeight(b) != h ||
        FreeImage_GetBPP(b) != FreeImage_GetBPP(a)) return 0;
    if (FreeImage_GetBPP(a) != 32) return digest(a) == digest(b);
    for (y = 0; y < h; y++) {
        const BYTE *pa = (const BYTE *)FreeImage_GetScanLine(a, y);
        const BYTE *pb = (const BYTE *)FreeImage_GetScanLine(b, y);
        for (x = 0; x < w; x++) {
            BYTE aa = pa[x * 4 + FI_RGBA_ALPHA], ab = pb[x * 4 + FI_RGBA_ALPHA];
            int c, rgb = 0;
            if (aa != ab) { visible++; continue; }
            for (c = 0; c < 4; c++)
                if (c != FI_RGBA_ALPHA && pa[x * 4 + c] != pb[x * 4 + c]) rgb = 1;
            if (rgb) { if (aa == 0) hidden++; else visible++; }
        }
    }
    if (hidden_out) *hidden_out = hidden;
    return visible == 0;
}

/* alpha varies on both axes; 'holes' adds alpha 0 */
static FIBITMAP *make_alpha(FIBITMAP *src, int holes) {
    FIBITMAP *d = FreeImage_ConvertTo32Bits(src);
    unsigned w, h, x, y;
    if (!d) return NULL;
    w = FreeImage_GetWidth(d); h = FreeImage_GetHeight(d);
    for (y = 0; y < h; y++) {
        BYTE *line = (BYTE *)FreeImage_GetScanLine(d, y);
        for (x = 0; x < w; x++) {
            unsigned v = (x * 7 + y * 13) % 255;
            line[x * 4 + FI_RGBA_ALPHA] = (BYTE)(holes ? ((y % 16 < 4) ? 0 : v) : 1 + v);
        }
    }
    return d;
}

static const char *XMP =
    "<?xpacket begin=\"\357\273\277\" id=\"W5M0MpCehiHzreSzNTczkc9d\"?>"
    "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\"/><?xpacket end=\"w\"?>";

static void attach_xmp(FIBITMAP *dib) {
    FITAG *tag = FreeImage_CreateTag();
    DWORD len = (DWORD)strlen(XMP) + 1;
    if (!tag) return;
    FreeImage_SetTagKey(tag, "XMLPacket");
    FreeImage_SetTagLength(tag, len);
    FreeImage_SetTagCount(tag, len);
    FreeImage_SetTagType(tag, FIDT_ASCII);
    FreeImage_SetTagValue(tag, (void *)XMP);
    FreeImage_SetMetadata(FIMD_XMP, dib, "XMLPacket", tag);
    FreeImage_DeleteTag(tag);
}

/* minimal valid ICC header */
static void attach_icc(FIBITMAP *dib, BYTE *buf, int len) {
    memset(buf, 0, (size_t)len);
    buf[3] = (BYTE)len;
    memcpy(buf + 4, "FIMG", 4); memcpy(buf + 12, "mntr", 4);
    memcpy(buf + 16, "RGB ", 4); memcpy(buf + 20, "XYZ ", 4);
    memcpy(buf + 36, "acsp", 4);
    FreeImage_CreateICCProfile(dib, buf, len);
}

/* file and memory round trips, compared (and to src when bitexact) */
static void round_trip(const char *label, FIBITMAP *src, int flags, int bitexact) {
    const char *path = scratch("fi_webp_rt_tmp.webp");
    FIBITMAP *from_file = NULL, *from_mem = NULL;
    FIMEMORY *mem = NULL;
    BYTE *bytes = NULL;
    DWORD size = 0;
    long hidden = 0;

    if (!FreeImage_Save(FIF_WEBP, src, path, flags)) { fail("%s: save to file", label); return; }
    from_file = FreeImage_Load(FIF_WEBP, path, 0);
    if (!from_file) { fail("%s: reload from file", label); remove(path); return; }

    mem = FreeImage_OpenMemory(NULL, 0);
    if (!mem || !FreeImage_SaveToMemory(FIF_WEBP, src, mem, flags)) {
        fail("%s: save to memory", label);
    } else {
        FreeImage_AcquireMemory(mem, &bytes, &size);
        FreeImage_SeekMemory(mem, 0, SEEK_SET);
        from_mem = FreeImage_LoadFromMemory(FIF_WEBP, mem, 0);
        if (!from_mem) fail("%s: reload from memory", label);
    }

    if (FreeImage_GetWidth(from_file) != FreeImage_GetWidth(src) ||
        FreeImage_GetHeight(from_file) != FreeImage_GetHeight(src))
        fail("%s: geometry changed, %ux%u -> %ux%u", label,
             FreeImage_GetWidth(src), FreeImage_GetHeight(src),
             FreeImage_GetWidth(from_file), FreeImage_GetHeight(from_file));
    if (FreeImage_GetBPP(from_file) != FreeImage_GetBPP(src))
        fail("%s: depth changed, %u -> %u bpp", label,
             FreeImage_GetBPP(src), FreeImage_GetBPP(from_file));

    if (from_mem && digest(from_mem) != digest(from_file))
        fail("%s: the memory stream and the file decode differently", label);

    if (bitexact) {
        if (digest(from_file) != digest(src))
            fail("%s: lossless round trip is not bit exact", label);
    } else if (FreeImage_GetBPP(src) == 32 && (flags & WEBP_LOSSLESS)) {
        /* transparent regions: only what is visible has to survive */
        if (!same_visibly(src, from_file, &hidden))
            fail("%s: a visible pixel changed", label);
    }

    printf("  ok %-34s %5u bytes%s\n", label, (unsigned)size,
           hidden ? "  (differs only under alpha == 0)" : "");

    if (from_file) FreeImage_Unload(from_file);
    if (from_mem) FreeImage_Unload(from_mem);
    if (mem) FreeImage_CloseMemory(mem);
    remove(path);
}

int main(void) {
    FIBITMAP *png, *rgb, *rgba, *holes, *meta, *grey, *tiny, *reloaded;
    BYTE icc[128];
    FITAG *etag = NULL;
    FIBITMAP *jpg;
    const int QUALITIES[] = { 1, 50, 100 };
    char label[64];
    int i;

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);
    printf("WebP round-trip test\n\n");

    png = FreeImage_Load(FIF_PNG, "../sample.png", 0);
    if (!png) { printf("FAIL: cannot load ../sample.png\n"); return 1; }
    rgb = FreeImage_ConvertTo24Bits(png);
    rgba = make_alpha(png, 0);
    holes = make_alpha(png, 1);
    grey = FreeImage_ConvertTo8Bits(png);
    /* odd size: partial blocks */
    tiny = FreeImage_Copy(rgb, 0, 0, 7, 3);
    FreeImage_Unload(png);
    if (!rgb || !rgba || !holes || !grey || !tiny) { printf("FAIL: conversion\n"); return 1; }

    printf("24-bit source\n");
    round_trip("rgb lossless", rgb, WEBP_LOSSLESS, 1);
    round_trip("rgb default quality", rgb, WEBP_DEFAULT, 0);
    for (i = 0; i < 3; i++) {
        snprintf(label, sizeof(label), "rgb quality %d", QUALITIES[i]);
        round_trip(label, rgb, QUALITIES[i], 0);
    }

    printf("32-bit source\n");
    round_trip("alpha lossless", rgba, WEBP_LOSSLESS, 1);
    round_trip("alpha default quality", rgba, WEBP_DEFAULT, 0);
    round_trip("transparent regions lossless", holes, WEBP_LOSSLESS, 0);

    printf("edge cases\n");
    round_trip("7x3 lossless", tiny, WEBP_LOSSLESS, 1);
    round_trip("7x3 default quality", tiny, WEBP_DEFAULT, 0);

    /* 24 and 32 bpp only */
    printf("refusals\n");
    if (FreeImage_FIFSupportsExportBPP(FIF_WEBP, 8))
        fail("the plugin claims it can export 8 bpp");
    else if (FreeImage_Save(FIF_WEBP, grey, scratch("fi_webp_rt_grey.webp"), 0))
        fail("saving an 8-bit image succeeded, but export of 8 bpp is unsupported");
    else
        printf("  ok %-34s refused as unsupported\n", "8-bit greyscale");
    remove(scratch("fi_webp_rt_grey.webp"));

    /* ICC, XMP and EXIF chunks must survive */
    printf("metadata\n");
    meta = FreeImage_Clone(rgb);
    attach_icc(meta, icc, (int)sizeof(icc));
    attach_xmp(meta);
    jpg = FreeImage_Load(FIF_JPEG, "../exif.jpg", 0);
    if (jpg && FreeImage_GetMetadata(FIMD_EXIF_RAW, jpg, "ExifRaw", &etag))
        FreeImage_SetMetadata(FIMD_EXIF_RAW, meta, "ExifRaw", etag);
    else
        printf("  note: ../exif.jpg carried no EXIF, that part is skipped\n");
    if (jpg) FreeImage_Unload(jpg);

    for (i = 0; i < 2; i++) {
        const int flags = i ? WEBP_LOSSLESS : WEBP_DEFAULT;
        const char *path = scratch("fi_webp_rt_meta.webp");
        FIICCPROFILE *p;
        FITAG *tag = NULL;
        int want_exif = 0;
        if (FreeImage_GetMetadata(FIMD_EXIF_RAW, meta, "ExifRaw", &tag))
            want_exif = (int)FreeImage_GetTagLength(tag);

        if (!FreeImage_Save(FIF_WEBP, meta, path, flags)) { fail("metadata: save"); continue; }
        reloaded = FreeImage_Load(FIF_WEBP, path, 0);
        if (!reloaded) { fail("metadata: reload"); remove(path); continue; }

        p = FreeImage_GetICCProfile(reloaded);
        if (!p || p->size != sizeof(icc) || memcmp(p->data, icc, sizeof(icc)) != 0)
            fail("metadata: the ICC profile did not survive %s", i ? "lossless" : "lossy");
        tag = NULL;
        if (!FreeImage_GetMetadata(FIMD_XMP, reloaded, "XMLPacket", &tag) ||
            FreeImage_GetTagLength(tag) != (DWORD)strlen(XMP) + 1 ||
            memcmp(FreeImage_GetTagValue(tag), XMP, strlen(XMP)) != 0)
            fail("metadata: the XMP packet did not survive %s", i ? "lossless" : "lossy");
        tag = NULL;
        if (want_exif) {
            if (!FreeImage_GetMetadata(FIMD_EXIF_RAW, reloaded, "ExifRaw", &tag) ||
                (int)FreeImage_GetTagLength(tag) != want_exif)
                fail("metadata: the EXIF block did not survive %s", i ? "lossless" : "lossy");
        }
        printf("  ok %-34s icc + xmp%s intact\n",
               i ? "metadata through lossless" : "metadata through lossy",
               want_exif ? " + exif" : "");
        FreeImage_Unload(reloaded);
        remove(path);
    }

    FreeImage_Unload(rgb); FreeImage_Unload(rgba); FreeImage_Unload(holes);
    FreeImage_Unload(grey); FreeImage_Unload(tiny); FreeImage_Unload(meta);

    printf("\n%s\n", failures ? "FAILED" : "every round trip came back as it should");
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
