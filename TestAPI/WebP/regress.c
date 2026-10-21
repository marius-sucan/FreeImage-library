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

static void set_anim_short(FIBITMAP *dib, const char *key, WORD value) {
    FITAG *tag = FreeImage_CreateTag();
    if (!tag) return;
    FreeImage_SetTagKey(tag, key);
    FreeImage_SetTagType(tag, FIDT_SHORT);
    FreeImage_SetTagCount(tag, 1);
    FreeImage_SetTagLength(tag, 2);
    FreeImage_SetTagValue(tag, &value);
    FreeImage_SetMetadata(FIMD_ANIMATION, dib, key, tag);
    FreeImage_DeleteTag(tag);
}

static unsigned char *read_file(const char *path, long *size) {
    FILE *f = fopen(path, "rb");
    unsigned char *buf = NULL;
    *size = -1;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (*size = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        buf = (unsigned char *)malloc(*size > 0 ? *size : 1);
        if (buf && fread(buf, 1, (size_t)*size, f) != (size_t)*size) { free(buf); buf = NULL; }
    }
    fclose(f);
    return buf;
}

/* the canvas is known when Close() writes the animation: its failure fails the save, the file stays */
static void unfinished_animation(FIBITMAP *rgba) {
    const char *path = scratch("fi_webp_rt_anim.webp");
    FIMULTIBITMAP *mb;
    FIMEMORY *mem;
    FIBITMAP *far_frame;
    unsigned char *before, *after;
    long n_before, n_after;

    printf("animation the writer cannot finish\n");
    remove(path);
    mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, TRUE, FALSE, TRUE, 0);
    if (!mb) { fail("unfinished animation: cannot create the animation"); return; }
    FreeImage_AppendPage(mb, rgba);
    FreeImage_AppendPage(mb, rgba);
    if (!FreeImage_CloseMultiBitmap(mb, 0)) { fail("unfinished animation: the 2-frame animation was not written"); return; }
    before = read_file(path, &n_before);
    if (!before) { fail("unfinished animation: cannot read the animation"); remove(path); return; }

    /* this far out, the frames need a canvas of 2^32 pixels, more than WebP holds */
    far_frame = FreeImage_Copy(rgba, 0, 0, 16, 16);
    set_anim_short(far_frame, "FrameLeft", 65534);
    set_anim_short(far_frame, "FrameTop", 65534);

    mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, FALSE, FALSE, TRUE, 0);
    if (!mb) fail("unfinished animation: cannot reopen the animation");
    else {
        if (!FreeImage_AppendPage(mb, far_frame)) fail("unfinished animation: the frame was refused on append");
        mem = FreeImage_OpenMemory(NULL, 0);
        if (FreeImage_SaveMultiBitmapToMemory(FIF_WEBP, mb, mem, 0))
            fail("unfinished animation: SaveMultiBitmapToMemory reported success");
        FreeImage_CloseMemory(mem);
        if (FreeImage_CloseMultiBitmap(mb, 0))
            fail("unfinished animation: CloseMultiBitmap reported success");
        else
            printf("  ok %-34s both saves fail\n", "canvas of 2^32 pixels");
    }
    after = read_file(path, &n_after);
    if (!after || n_after != n_before || memcmp(after, before, (size_t)n_before) != 0)
        fail("unfinished animation: the failed save changed the file (%ld -> %ld bytes)", n_before, n_after);
    else
        printf("  ok %-34s the file is as it was\n", "failed close");
    free(before);
    free(after);
    FreeImage_Unload(far_frame);
    remove(path);
}

/* ------------------------------------------------------------------ frames in different profiles */

/* the writer's conversions */
#define CONVERT_FLAGS (FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION)

enum { SAME, CONVERTED, TWIN };

typedef struct {
    int bpp;
    const void *profile;
    DWORD size;
    int expect;     /* SAME: its own colors; CONVERTED: the file's; TWIN: its own, which a conversion would change */
} FrameSpec;

static DWORD be32(const BYTE *p) {
    return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | p[3];
}

/* a built-in profile with the values of one XYZ tag added to (add) or replaced */
static BYTE *patched_profile(int builtin, const char *sig, const DWORD xyz[3], int add, DWORD *size) {
    const BYTE *src = (const BYTE *)FreeImage_GetBuiltInICCProfile(builtin, size);
    BYTE *p = src ? (BYTE *)malloc(*size) : NULL;
    DWORD n, i, k;
    if (!p) return NULL;
    memcpy(p, src, *size);
    n = be32(p + 128);
    for (i = 0; i < n && 132 + 12 * i + 12 <= *size; i++) {
        const BYTE *entry = p + 132 + 12 * i;
        if (!memcmp(entry, sig, 4) && be32(entry + 4) + 20 <= *size) {
            for (k = 0; k < 3; k++) {
                BYTE *x = p + be32(entry + 4) + 8 + 4 * k;
                const DWORD v = add ? be32(x) + xyz[k] : xyz[k];
                x[0] = (BYTE)(v >> 24); x[1] = (BYTE)(v >> 16); x[2] = (BYTE)(v >> 8); x[3] = (BYTE)v;
            }
            return p;
        }
    }
    free(p);
    return NULL;
}

/* a page of an animation, as QPV makes them */
static void set_frame_time(FIBITMAP *dib) {
    FITAG *tag = FreeImage_CreateTag();
    LONG ms = 100;
    if (!tag) return;
    FreeImage_SetTagKey(tag, "FrameTime");
    FreeImage_SetTagType(tag, FIDT_LONG);
    FreeImage_SetTagCount(tag, 1);
    FreeImage_SetTagLength(tag, 4);
    FreeImage_SetTagValue(tag, &ms);
    FreeImage_SetMetadata(FIMD_ANIMATION, dib, "FrameTime", tag);
    FreeImage_DeleteTag(tag);
}

/* a 64x48 frame spanning red and green; alpha is never 0, under which lossless may change the colors */
static FIBITMAP *make_profiled(int bpp, int seed, const void *profile, DWORD size) {
    FIBITMAP *rgba = FreeImage_Allocate(64, 48, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK), *dib;
    unsigned x, y;
    if (!rgba) return NULL;
    for (y = 0; y < 48; y++) {
        BYTE *line = FreeImage_GetScanLine(rgba, y);
        for (x = 0; x < 64; x++) {
            line[FI_RGBA_RED] = (BYTE)(x * 4);
            line[FI_RGBA_GREEN] = (BYTE)(y * 5);
            line[FI_RGBA_BLUE] = (BYTE)(x * y + seed * 20);
            line[FI_RGBA_ALPHA] = (BYTE)(128 + ((x + y) & 0x7F));
            line += 4;
        }
    }
    dib = (bpp == 24) ? FreeImage_ConvertTo24Bits(rgba) : FreeImage_Clone(rgba);
    FreeImage_Unload(rgba);
    if (dib && profile) FreeImage_CreateICCProfile(dib, (void *)profile, (long)size);
    if (dib) set_frame_time(dib);
    return dib;
}

/* the frame in a profile's colors (NULL: sRGB) as 8-bit RGBA, by the writer's steps */
static FIBITMAP *in_colors(FIBITMAP *frame, const void *profile, DWORD size) {
    FIBITMAP *converted = FreeImage_ConvertToICCProfile(frame, profile, size, CONVERT_FLAGS), *out = NULL;
    if (converted) {
        out = FreeImage_ConvertTo32Bits(converted);
        FreeImage_Unload(converted);
    }
    return out;
}

static int has_profile(FIBITMAP *dib, const void *profile, DWORD size) {
    FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
    return icc->data && icc->size == size && !memcmp(icc->data, profile, size);
}

/* the pixels that differ as 8-bit RGBA, -1 when the images cannot be compared */
static int pixel_diff(FIBITMAP *a, FIBITMAP *b) {
    FIBITMAP *ra = FreeImage_ConvertTo32Bits(a), *rb = FreeImage_ConvertTo32Bits(b);
    unsigned w, h, x, y;
    int n = 0;
    if (!ra || !rb || FreeImage_GetWidth(ra) != FreeImage_GetWidth(rb) || FreeImage_GetHeight(ra) != FreeImage_GetHeight(rb)) {
        n = -1;
    } else {
        w = FreeImage_GetWidth(ra); h = FreeImage_GetHeight(ra);
        for (y = 0; y < h; y++) {
            const BYTE *pa = FreeImage_GetScanLine(ra, y), *pb = FreeImage_GetScanLine(rb, y);
            for (x = 0; x < w; x++) if (memcmp(pa + 4 * x, pb + 4 * x, 4)) n++;
        }
    }
    if (ra) FreeImage_Unload(ra);
    if (rb) FreeImage_Unload(rb);
    return n;
}

/* written lossless through the multi-page functions and read back, every frame has the colors expected and the file's profile */
static int check_mixed(const char *what, const FrameSpec *spec, int n, const void *file_profile, DWORD file_size) {
    const char *path = scratch("fi_webp_rt_mixed.webp");
    FIBITMAP *frames[8];
    FIMULTIBITMAP *mb;
    int i, bad = 0;

    for (i = 0; i < n; i++) frames[i] = make_profiled(spec[i].bpp, i, spec[i].profile, spec[i].size);
    remove(path);
    mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, TRUE, FALSE, TRUE, 0);
    if (!mb) { fail("%s: cannot create the animation", what); bad++; goto out; }
    for (i = 0; i < n; i++) FreeImage_AppendPage(mb, frames[i]);
    if (!FreeImage_CloseMultiBitmap(mb, WEBP_LOSSLESS)) { fail("%s: the animation was not written", what); bad++; goto out; }
    mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, FALSE, TRUE, TRUE, 0);
    if (!mb || FreeImage_GetPageCount(mb) != n) {
        fail("%s: %d frames came back, not %d", what, mb ? FreeImage_GetPageCount(mb) : 0, n); bad++;
        if (mb) FreeImage_CloseMultiBitmap(mb, 0);
        goto out;
    }
    for (i = 0; i < n; i++) {
        FIBITMAP *page = FreeImage_LockPage(mb, i);
        FIBITMAP *own = in_colors(frames[i], spec[i].profile, spec[i].size);
        FIBITMAP *file = in_colors(frames[i], file_profile, file_size);
        FIBITMAP *want = (spec[i].expect == CONVERTED) ? file : own;
        if (!page || !own || !file) {
            fail("%s: frame %d did not load or convert", what, i); bad++;
        } else {
            const int diff = pixel_diff(page, want);
            if (diff) { fail("%s: frame %d, %d pixels not in the %s colors", what, i, diff, (spec[i].expect == CONVERTED) ? "file's" : "frame's own"); bad++; }
            if (spec[i].expect != SAME && !pixel_diff(own, file)) { fail("%s: frame %d looks the same in both profiles: the check proves nothing", what, i); bad++; }
            if (file_profile ? !has_profile(page, file_profile, file_size) : (FreeImage_GetICCProfile(page)->data != NULL)) {
                fail("%s: frame %d does not carry the file's profile", what, i); bad++;
            }
        }
        if (own) FreeImage_Unload(own);
        if (file) FreeImage_Unload(file);
        if (page) FreeImage_UnlockPage(mb, page, FALSE);
    }
    FreeImage_CloseMultiBitmap(mb, 0);

out:
    for (i = 0; i < n; i++) if (frames[i]) FreeImage_Unload(frames[i]);
    remove(path);
    return bad;
}

/* an animation has one profile, the first frame's: the other frames are converted to it, an untagged one being sRGB */
static void mixed_profiles(void) {
    DWORD adobe_size = 0, p3_size = 0, srgb_size = 0, grey_size = 0, prophoto_size = 0, twin_size = 0, white_size = 0;
    const void *adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
    const void *p3 = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_DISPLAY_P3, &p3_size);
    const void *srgb = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_SRGB, &srgb_size);
    const void *grey = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_GRAY, &grey_size);
    const void *prophoto = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_PROPHOTO_RGB, &prophoto_size);
    /* sRGB with the red primary nudged: within FreeImage's sRGB tolerance, yet a conversion changes pixels */
    static const DWORD nudge[3] = { 0x20, 0, 0 };
    /* Display P3 with a D65 media white: only an absolute colorimetric conversion would see it */
    static const DWORD d65[3] = { 0xF353, 0x10000, 0x116C8 };
    BYTE *twin = patched_profile(FICMS_PROFILE_SRGB, "rXYZ", nudge, 1, &twin_size);
    BYTE *white = patched_profile(FICMS_PROFILE_DISPLAY_P3, "wtpt", d65, 0, &white_size);

    printf("animation frames in different profiles\n");
    if (!adobe || !p3 || !srgb || !grey || !prophoto || !twin || !white) {
        fail("mixed profiles: the built-in profiles are not available");
        free(twin);
        free(white);
        return;
    }
    {
        const FrameSpec spec[6] = {
            { 32, adobe, adobe_size, SAME },
            { 32, p3, p3_size, CONVERTED },
            { 32, NULL, 0, CONVERTED },
            { 32, twin, twin_size, CONVERTED },
            { 24, prophoto, prophoto_size, CONVERTED },
            { 32, white, white_size, CONVERTED }
        };
        if (!check_mixed("Adobe RGB file", spec, 6, adobe, adobe_size))
            printf("  ok %-34s Display P3, untagged, sRGB-like, ProPhoto and D65-white frames converted\n", "an Adobe RGB first frame");
    }
    {
        const FrameSpec spec[4] = {
            { 32, NULL, 0, SAME },
            { 32, twin, twin_size, TWIN },
            { 32, p3, p3_size, CONVERTED },
            { 24, srgb, srgb_size, SAME }
        };
        if (!check_mixed("sRGB file", spec, 4, NULL, 0))
            printf("  ok %-34s sRGB look-alikes kept, Display P3 converted\n", "an untagged first frame");
    }
    {
        const FrameSpec spec[3] = {
            { 32, twin, twin_size, SAME },
            { 32, NULL, 0, TWIN },
            { 24, adobe, adobe_size, CONVERTED }
        };
        if (!check_mixed("sRGB-like file", spec, 3, twin, twin_size))
            printf("  ok %-34s untagged frames kept as they are\n", "an sRGB-like first frame");
    }

    /* a grey profile cannot describe the RGB frames: the file is sRGB */
    {
        const char *path = scratch("fi_webp_rt_grey_icc.webp");
        FIBITMAP *frames[2], *want0 = NULL, *want1 = NULL;
        FIMULTIBITMAP *mb = NULL;
        int i, bad = 0;
        frames[0] = make_profiled(32, 0, grey, grey_size);
        frames[1] = make_profiled(32, 1, adobe, adobe_size);
        remove(path);
        if (frames[0] && frames[1] && (mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, TRUE, FALSE, TRUE, 0)) != NULL) {
            FreeImage_AppendPage(mb, frames[0]);
            FreeImage_AppendPage(mb, frames[1]);
            if (!FreeImage_CloseMultiBitmap(mb, WEBP_LOSSLESS)) bad++;
            mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, FALSE, TRUE, TRUE, 0);
        }
        want0 = frames[0] ? FreeImage_ConvertTo32Bits(frames[0]) : NULL;
        want1 = frames[1] ? in_colors(frames[1], NULL, 0) : NULL;
        if (bad || !mb || FreeImage_GetPageCount(mb) != 2 || !want0 || !want1) {
            fail("grey-profile first frame: the animation was not written or read");
        } else {
            for (i = 0; i < 2; i++) {
                FIBITMAP *page = FreeImage_LockPage(mb, i);
                if (!page || pixel_diff(page, i ? want1 : want0) || FreeImage_GetICCProfile(page)->data) bad++;
                if (page) FreeImage_UnlockPage(mb, page, FALSE);
            }
            if (bad) fail("grey-profile first frame: a frame is not sRGB, or the file carries a profile");
            else printf("  ok %-34s no profile, the Adobe RGB frame converted to sRGB\n", "a grey-profile first frame");
        }
        if (mb) FreeImage_CloseMultiBitmap(mb, 0);
        for (i = 0; i < 2; i++) if (frames[i]) FreeImage_Unload(frames[i]);
        if (want0) FreeImage_Unload(want0);
        if (want1) FreeImage_Unload(want1);
        remove(path);
    }

    /* a CMYK page saved alone as a frame: RGB, not its inks taken for RGBA */
    {
        const char *path = scratch("fi_webp_rt_cmyk.webp");
        FIBITMAP *cmyk = FreeImage_Allocate(64, 48, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK), *page = NULL, *want = NULL;
        unsigned x, y;
        int diff = -1;
        if (cmyk) {
            FreeImage_GetICCProfile(cmyk)->flags |= FIICC_COLOR_IS_CMYK;
            for (y = 0; y < 48; y++) {
                BYTE *line = FreeImage_GetScanLine(cmyk, y);
                for (x = 0; x < 64; x++) {
                    line[0] = (BYTE)(x * 4); line[1] = (BYTE)(y * 5); line[2] = (BYTE)((x + y) * 2); line[3] = (BYTE)(x * y / 16);
                    line += 4;
                }
            }
            set_frame_time(cmyk);
            if (FreeImage_Save(FIF_WEBP, cmyk, path, WEBP_LOSSLESS)) page = FreeImage_Load(FIF_WEBP, path, 0);
            want = in_colors(cmyk, NULL, 0);
            if (page && want) diff = pixel_diff(page, want);
        }
        if (diff) fail("a CMYK page: %d pixels not converted to sRGB", diff);
        else printf("  ok %-34s converted to sRGB\n", "a CMYK page");
        if (page) FreeImage_Unload(page);
        if (want) FreeImage_Unload(want);
        if (cmyk) FreeImage_Unload(cmyk);
        remove(path);
    }
    free(twin);
    free(white);
}

/* ------------------------------------------------------------------ played pages saved again */

static void set_anim(FIBITMAP *dib, const char *key, FREE_IMAGE_MDTYPE type, DWORD value) {
    FITAG *tag = FreeImage_CreateTag();
    BYTE b = (BYTE)value;
    WORD w = (WORD)value;
    DWORD length = (type == FIDT_BYTE) ? 1 : (type == FIDT_SHORT) ? 2 : 4;
    if (!tag) return;
    FreeImage_SetTagKey(tag, key);
    FreeImage_SetTagType(tag, type);
    FreeImage_SetTagCount(tag, 1);
    FreeImage_SetTagLength(tag, length);
    FreeImage_SetTagValue(tag, (type == FIDT_BYTE) ? (const void *)&b : (type == FIDT_SHORT) ? (const void *)&w : (const void *)&value);
    FreeImage_SetMetadata(FIMD_ANIMATION, dib, key, tag);
    FreeImage_DeleteTag(tag);
}

static long get_anim(FIBITMAP *dib, const char *key) {
    FITAG *tag = NULL;
    if (!FreeImage_GetMetadata(FIMD_ANIMATION, dib, key, &tag) || !tag || !FreeImage_GetTagValue(tag)) return -1;
    switch (FreeImage_GetTagType(tag)) {
        case FIDT_BYTE: return *(const BYTE *)FreeImage_GetTagValue(tag);
        case FIDT_SHORT: return *(const WORD *)FreeImage_GetTagValue(tag);
        case FIDT_LONG: return *(const LONG *)FreeImage_GetTagValue(tag);
        default: return -1;
    }
}

/* the pixels that differ visibly as 8-bit RGBA: alpha, or colour where alpha is not 0; -1 when not comparable */
static int visible_diff(FIBITMAP *a, FIBITMAP *b) {
    FIBITMAP *ra = FreeImage_ConvertTo32Bits(a), *rb = FreeImage_ConvertTo32Bits(b);
    unsigned w, h, x, y;
    int n = 0;
    if (!ra || !rb || FreeImage_GetWidth(ra) != FreeImage_GetWidth(rb) || FreeImage_GetHeight(ra) != FreeImage_GetHeight(rb)) {
        n = -1;
    } else {
        w = FreeImage_GetWidth(ra); h = FreeImage_GetHeight(ra);
        for (y = 0; y < h; y++) {
            const BYTE *pa = FreeImage_GetScanLine(ra, y), *pb = FreeImage_GetScanLine(rb, y);
            for (x = 0; x < w; x++, pa += 4, pb += 4) {
                if (pa[FI_RGBA_ALPHA] != pb[FI_RGBA_ALPHA] || (pa[FI_RGBA_ALPHA] && memcmp(pa, pb, 4))) n++;
            }
        }
    }
    if (ra) FreeImage_Unload(ra);
    if (rb) FreeImage_Unload(rb);
    return n;
}

/* translucent pixels that depend on (x, y, seed) */
static FIBITMAP *translucent(unsigned w, unsigned h, int seed) {
    FIBITMAP *dib = FreeImage_Allocate(w, h, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
    unsigned x, y;
    if (!dib) return NULL;
    for (y = 0; y < h; y++) {
        BYTE *line = FreeImage_GetScanLine(dib, h - 1 - y);
        for (x = 0; x < w; x++, line += 4) {
            line[FI_RGBA_RED] = (BYTE)(x * 9 + seed * 40);
            line[FI_RGBA_GREEN] = (BYTE)(y * 13 + seed * 70);
            line[FI_RGBA_BLUE] = (BYTE)(seed * 90 + x * y);
            line[FI_RGBA_ALPHA] = (BYTE)(((x + y + seed) % 3 == 0) ? 255 : 60 + ((x * 7 + y * 3) % 150));
        }
    }
    return dib;
}

/* the pages of a document as played, or NULL */
static int played_pages(const char *path, FREE_IMAGE_FORMAT fif, int flags, FIBITMAP **out, int max) {
    FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(fif, path, FALSE, TRUE, TRUE, flags);
    int i, n;
    if (!mb) return -1;
    n = FreeImage_GetPageCount(mb);
    for (i = 0; i < n && i < max; i++) {
        FIBITMAP *page = FreeImage_LockPage(mb, i);
        out[i] = page ? FreeImage_Clone(page) : NULL;
        if (page) FreeImage_UnlockPage(mb, page, FALSE);
    }
    FreeImage_CloseMultiBitmap(mb, 0);
    return n;
}

/* every played page of src copied to a new WebP, lossless */
static int copy_played(const char *src, FREE_IMAGE_FORMAT fif, int flags, const char *dst) {
    FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(fif, src, FALSE, TRUE, TRUE, flags);
    FIMULTIBITMAP *out = FreeImage_OpenMultiBitmap(FIF_WEBP, dst, TRUE, FALSE, TRUE, 0);
    int i, done = (mb && out) ? 1 : 0;
    for (i = 0; done && i < FreeImage_GetPageCount(mb); i++) {
        FIBITMAP *page = FreeImage_LockPage(mb, i);
        done = page && FreeImage_AppendPage(out, page);
        if (page) FreeImage_UnlockPage(mb, page, FALSE);
    }
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
    if (out && !FreeImage_CloseMultiBitmap(out, WEBP_LOSSLESS)) done = 0;
    return done;
}

/* a played page is the whole canvas: copied to a new document or edited in place, the animation plays as before */
static void playback_pages(void) {
    static const WORD X[4] = { 0, 6, 20, 30 }, Y[4] = { 0, 4, 10, 18 };
    static const BYTE DISPOSE[4] = { 1, 2, 1, 1 };
    const char *src = "fi_webp_rt_play_src.webp", *copy = "fi_webp_rt_play_copy.webp", *edit = "fi_webp_rt_play_edit.webp";
    char src_path[512], copy_path[512], edit_path[512];
    FIBITMAP *played[4] = { NULL, NULL, NULL, NULL }, *got[4] = { NULL, NULL, NULL, NULL };
    FIMULTIBITMAP *mb;
    int i, k, n, bad = 0;

    printf("WEBP_PLAYBACK pages saved again\n");
    snprintf(src_path, sizeof(src_path), "%s", scratch(src));
    snprintf(copy_path, sizeof(copy_path), "%s", scratch(copy));
    snprintf(edit_path, sizeof(edit_path), "%s", scratch(edit));

    for (k = 0; k < 2; k++) {
        mb = FreeImage_OpenMultiBitmap(FIF_WEBP, k ? edit_path : src_path, TRUE, FALSE, TRUE, 0);
        for (i = 0; mb && i < 4; i++) {
            FIBITMAP *frame = i ? translucent(16, 12, i) : translucent(64, 48, 0);
            set_anim(frame, "FrameLeft", FIDT_SHORT, X[i]);
            set_anim(frame, "FrameTop", FIDT_SHORT, Y[i]);
            set_anim(frame, "FrameTime", FIDT_LONG, 100 + 10 * i);
            set_anim(frame, "DisposalMethod", FIDT_BYTE, DISPOSE[i]);
            set_anim(frame, "BlendMethod", FIDT_BYTE, 0);
            set_anim(frame, "LogicalWidth", FIDT_SHORT, 64);
            set_anim(frame, "LogicalHeight", FIDT_SHORT, 48);
            set_anim(frame, "Loop", FIDT_LONG, 3);
            FreeImage_AppendPage(mb, frame);
            FreeImage_Unload(frame);
        }
        if (!mb || !FreeImage_CloseMultiBitmap(mb, WEBP_LOSSLESS)) { fail("playback pages: the animation was not written"); return; }
    }

    if (played_pages(src_path, FIF_WEBP, WEBP_PLAYBACK, played, 4) != 4) { fail("playback pages: the animation does not play"); bad++; }
    for (i = 0; i < 4 && played[i]; i++) {
        if (get_anim(played[i], "FrameLeft") != 0 || get_anim(played[i], "FrameTop") != 0 || get_anim(played[i], "BlendMethod") != 1 ||
            get_anim(played[i], "DisposalMethod") != -1 || get_anim(played[i], "FrameTime") != 100 + 10 * i ||
            get_anim(played[i], "Loop") != 3 || get_anim(played[i], "LogicalWidth") != 64 || get_anim(played[i], "LogicalHeight") != 48) {
            fail("playback pages: page %d at (%ld,%ld), blend %ld, disposal %ld, %ld ms, loop %ld, canvas %ldx%ld", i,
                 get_anim(played[i], "FrameLeft"), get_anim(played[i], "FrameTop"), get_anim(played[i], "BlendMethod"),
                 get_anim(played[i], "DisposalMethod"), get_anim(played[i], "FrameTime"), get_anim(played[i], "Loop"),
                 get_anim(played[i], "LogicalWidth"), get_anim(played[i], "LogicalHeight"));
            bad++;
        }
    }
    if (!copy_played(src_path, FIF_WEBP, WEBP_PLAYBACK, copy_path)) { fail("playback pages: the copy was not written"); bad++; }
    mb = FreeImage_OpenMultiBitmap(FIF_WEBP, edit_path, FALSE, FALSE, TRUE, WEBP_PLAYBACK);
    if (!mb || !FreeImage_DeletePage(mb, 1) || !FreeImage_CloseMultiBitmap(mb, WEBP_LOSSLESS)) { fail("playback pages: page 1 could not be deleted in place"); bad++; }

    for (k = 0; k < 2; k++) {
        static const int KEPT[2][4] = { { 0, 1, 2, 3 }, { 0, 2, 3, -1 } };
        n = played_pages(k ? edit_path : copy_path, FIF_WEBP, WEBP_PLAYBACK, got, 4);
        if (n != (k ? 3 : 4)) { fail("playback pages: the %s has %d pages", k ? "edited file" : "copy", n); bad++; }
        for (i = 0; i < n && i < 4; i++) {
            const int d = (got[i] && played[KEPT[k][i]]) ? visible_diff(got[i], played[KEPT[k][i]]) : -1;
            if (d) {
                fail("playback pages: %s page %d is %ux%u, %d pixel(s) differ from the original played", k ? "edited" : "copied", i,
                     got[i] ? FreeImage_GetWidth(got[i]) : 0, got[i] ? FreeImage_GetHeight(got[i]) : 0, d);
                bad++;
            }
            if (got[i]) FreeImage_Unload(got[i]);
            got[i] = NULL;
        }
    }
    if (!bad) printf("  ok %-34s whole-canvas tags; copied or edited in place, the 64x48 animation plays as before\n", "played pages saved again");
    for (i = 0; i < 4; i++) if (played[i]) FreeImage_Unload(played[i]);
    remove(src_path);
    remove(copy_path);
    remove(edit_path);
}

/* a GIF played and saved as WebP: a disposed frame leaves no trail, the loop count stays */
static void gif_playback_pages(void) {
    char gif_path[512], webp_path[512];
    FIBITMAP *gif_played[2] = { NULL, NULL }, *webp_played[2] = { NULL, NULL };
    FIMULTIBITMAP *mb;
    int i, bad = 0;

    printf("GIF_PLAYBACK pages saved as WebP\n");
    snprintf(gif_path, sizeof(gif_path), "%s", scratch("fi_webp_rt_play.gif"));
    snprintf(webp_path, sizeof(webp_path), "%s", scratch("fi_webp_rt_play_gif.webp"));

    mb = FreeImage_OpenMultiBitmap(FIF_GIF, gif_path, TRUE, FALSE, TRUE, 0);
    for (i = 0; mb && i < 2; i++) {
        FIBITMAP *frame = FreeImage_Allocate(16, 12, 8, 0, 0, 0);
        RGBQUAD *pal = FreeImage_GetPalette(frame);
        unsigned x, y;
        memset(pal, 0, 256 * sizeof(RGBQUAD));
        pal[1].rgbRed = 255;
        pal[2].rgbBlue = 255;
        for (y = 0; y < 12; y++) for (x = 0; x < 16; x++) FreeImage_GetScanLine(frame, y)[x] = (BYTE)(i + 1);
        set_anim(frame, "FrameLeft", FIDT_SHORT, i * 16);
        set_anim(frame, "FrameTop", FIDT_SHORT, i * 12);
        set_anim(frame, "FrameTime", FIDT_LONG, 100);
        set_anim(frame, "DisposalMethod", FIDT_BYTE, i ? 1 : 2);
        set_anim(frame, "LogicalWidth", FIDT_SHORT, 32);
        set_anim(frame, "LogicalHeight", FIDT_SHORT, 24);
        set_anim(frame, "Loop", FIDT_LONG, 3);
        FreeImage_AppendPage(mb, frame);
        FreeImage_Unload(frame);
    }
    if (!mb || !FreeImage_CloseMultiBitmap(mb, 0)) { fail("GIF playback: the GIF was not written"); return; }

    if (played_pages(gif_path, FIF_GIF, GIF_PLAYBACK, gif_played, 2) != 2 || !gif_played[1]) {
        fail("GIF playback: the GIF does not play");
        bad++;
    } else if (get_anim(gif_played[1], "FrameLeft") != 0 || get_anim(gif_played[1], "BlendMethod") != 1 ||
               get_anim(gif_played[1], "Loop") != 3 || get_anim(gif_played[1], "LogicalWidth") != 32) {
        fail("GIF playback: page 1 at %ld, blend %ld, loop %ld, canvas width %ld", get_anim(gif_played[1], "FrameLeft"),
             get_anim(gif_played[1], "BlendMethod"), get_anim(gif_played[1], "Loop"), get_anim(gif_played[1], "LogicalWidth"));
        bad++;
    }
    if (!copy_played(gif_path, FIF_GIF, GIF_PLAYBACK, webp_path) || played_pages(webp_path, FIF_WEBP, WEBP_PLAYBACK, webp_played, 2) != 2) {
        fail("GIF playback: the WebP was not written or does not play");
        bad++;
    }
    for (i = 0; i < 2; i++) {
        const int d = (gif_played[i] && webp_played[i]) ? visible_diff(gif_played[i], webp_played[i]) : -1;
        if (d) { fail("GIF playback: WebP frame %d, %d pixel(s) differ from the GIF's", i, d); bad++; }
    }
    if (webp_played[0] && get_anim(webp_played[0], "Loop") != 3) { fail("GIF playback: the WebP loops %ld times, not 3", get_anim(webp_played[0], "Loop")); bad++; }
    if (!bad) printf("  ok %-34s the disposed frame leaves no trail, the loop count stays\n", "GIF_PLAYBACK pages as WebP");
    for (i = 0; i < 2; i++) {
        if (gif_played[i]) FreeImage_Unload(gif_played[i]);
        if (webp_played[i]) FreeImage_Unload(webp_played[i]);
    }
    remove(gif_path);
    remove(webp_path);
}

/* 1 when the RIFF holds an ANIM or ANMF chunk or VP8X says it is animated, 0 for a still, -1 unreadable */
static int webp_animated(const char *path) {
    long size = 0;
    unsigned char *b = read_file(path, &size);
    size_t pos = 12;
    int animated = 0;
    if (!b || size < 12 || memcmp(b, "RIFF", 4) || memcmp(b + 8, "WEBP", 4)) { free(b); return -1; }
    while (pos + 8 <= (size_t)size) {
        const size_t n = (size_t)b[pos + 4] | ((size_t)b[pos + 5] << 8) | ((size_t)b[pos + 6] << 16) | ((size_t)b[pos + 7] << 24);
        if (!memcmp(b + pos, "ANIM", 4) || !memcmp(b + pos, "ANMF", 4)) animated = 1;
        if (!memcmp(b + pos, "VP8X", 4) && n >= 1 && pos + 8 < (size_t)size && (b[pos + 8] & 0x02)) animated = 1;
        pos += 8 + n + (n & 1);
    }
    free(b);
    return animated;
}

/* a page placed on a larger canvas, as an animation hands them out */
static FIBITMAP *placed_page(int seed) {
    FIBITMAP *page = translucent(16, 12, seed);
    set_anim(page, "FrameLeft", FIDT_SHORT, 10);
    set_anim(page, "FrameTop", FIDT_SHORT, 6);
    set_anim(page, "FrameTime", FIDT_LONG, 70);
    set_anim(page, "LogicalWidth", FIDT_SHORT, 96);
    set_anim(page, "LogicalHeight", FIDT_SHORT, 64);
    set_anim(page, "Loop", FIDT_LONG, 3);
    return page;
}

/* the file a still WebP holding exactly this page */
static int is_still_of(const char *path, FIBITMAP *page) {
    FIBITMAP *back = FreeImage_Load(FIF_WEBP, path, 0);
    const int same = back && webp_animated(path) == 0 && visible_diff(back, page) == 0 && get_anim(back, "FrameTime") == -1;
    if (back) FreeImage_Unload(back);
    return same;
}

/* an animation of one frame is saved as a still image, which every WebP decoder reads */
static void one_frame_still(void) {
    char path[512];
    FIBITMAP *page = placed_page(1), *pages[3];
    FIMULTIBITMAP *mb;
    int i, bad = 0;

    printf("one frame saved as a still image\n");
    snprintf(path, sizeof(path), "%s", scratch("fi_webp_rt_one.webp"));

    /* FreeImage_Save of a page */
    if (!FreeImage_Save(FIF_WEBP, page, path, WEBP_LOSSLESS) || !is_still_of(path, page)) {
        fail("one frame: FreeImage_Save of a page is not a still image of it (animated %d)", webp_animated(path));
        bad++;
    }
    if (!FreeImage_Save(FIF_WEBP, page, path, 80) || webp_animated(path) != 0) {
        fail("one frame: a lossy FreeImage_Save of a page is not a still image");
        bad++;
    }

    /* a document of one page */
    remove(path);
    mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, TRUE, FALSE, TRUE, 0);
    if (!mb || !FreeImage_AppendPage(mb, page) || !FreeImage_CloseMultiBitmap(mb, WEBP_LOSSLESS) || !is_still_of(path, page)) {
        fail("one frame: a document of one page is not a still image of it");
        bad++;
    }

    /* an animation edited down to one page */
    remove(path);
    mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, TRUE, FALSE, TRUE, 0);
    for (i = 0; i < 3; i++) {
        pages[i] = placed_page(i + 2);
        if (mb) FreeImage_AppendPage(mb, pages[i]);
    }
    if (!mb || !FreeImage_CloseMultiBitmap(mb, WEBP_LOSSLESS) || webp_animated(path) != 1) {
        fail("one frame: three pages did not make an animation");
        bad++;
    }
    mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, FALSE, FALSE, TRUE, 0);
    if (!mb || !FreeImage_DeletePage(mb, 2) || !FreeImage_DeletePage(mb, 0) || !FreeImage_CloseMultiBitmap(mb, WEBP_LOSSLESS) ||
        !is_still_of(path, pages[1])) {
        fail("one frame: an animation edited down to one page is not a still image of it");
        bad++;
    }

    if (!bad) printf("  ok %-34s a page saved alone, a one-page document and one edited down to a page are still images of it\n", "one frame as a still");
    for (i = 0; i < 3; i++) FreeImage_Unload(pages[i]);
    FreeImage_Unload(page);
    remove(path);
}

/* ------------------------------------------------------------------ frames WebP cannot hold as they are */

typedef struct {
    int x, y, w, h;
    BYTE rgba[4];
    BYTE disposal;      /* GIF numbering */
    BYTE blend;         /* 1: in place of what is there */
} SolidPage;

static FIBITMAP *solid_rgba(int w, int h, const BYTE rgba[4]) {
    FIBITMAP *dib = FreeImage_Allocate(w, h, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
    int x, y;
    for (y = 0; dib && y < h; y++) {
        BYTE *line = FreeImage_GetScanLine(dib, (unsigned)y);
        for (x = 0; x < w; x++, line += 4) {
            line[FI_RGBA_RED] = rgba[0]; line[FI_RGBA_GREEN] = rgba[1]; line[FI_RGBA_BLUE] = rgba[2]; line[FI_RGBA_ALPHA] = rgba[3];
        }
    }
    return dib;
}

/* the canvas after page n, top-down RGBA, as the tags play it: disposal 2 clears, 3 restores; blend 1 replaces */
static void play_tags(const SolidPage *pages, int n, int cw, int ch, BYTE *canvas) {
    BYTE *before = (BYTE *)malloc((size_t)cw * ch * 4);
    int i, x, y, c;
    memset(canvas, 0, (size_t)cw * ch * 4);
    for (i = 0; i <= n; i++) {
        const SolidPage *p = &pages[i];
        memcpy(before, canvas, (size_t)cw * ch * 4);
        for (y = p->y; y < p->y + p->h; y++) {
            for (x = p->x; x < p->x + p->w; x++) {
                BYTE *d = canvas + ((size_t)y * cw + x) * 4;
                const unsigned sa = p->rgba[3], weight = d[3] * (255 - sa), alpha = sa * 255 + weight;
                if (p->blend || sa == 255) memcpy(d, p->rgba, 4);
                else if (sa) {
                    for (c = 0; c < 3; c++) d[c] = (BYTE)((p->rgba[c] * sa * 255 + d[c] * weight + alpha / 2) / alpha);
                    d[3] = (BYTE)((alpha + 127) / 255);
                }
            }
        }
        if (i == n) break;
        if (p->disposal == 2) for (y = p->y; y < p->y + p->h; y++) memset(canvas + ((size_t)y * cw + p->x) * 4, 0, (size_t)p->w * 4);
        else if (p->disposal == 3) memcpy(canvas, before, (size_t)cw * ch * 4);
    }
    free(before);
}

/* pixels of a played page more than 2 off the canvas the tags give, alpha 0 matching alpha 0; -1 when not comparable */
static int canvas_diff(FIBITMAP *dib, const BYTE *want, int cw, int ch) {
    int x, y, c, n = 0;
    if (!dib || (int)FreeImage_GetWidth(dib) != cw || (int)FreeImage_GetHeight(dib) != ch || FreeImage_GetBPP(dib) != 32) return -1;
    for (y = 0; y < ch; y++) {
        const BYTE *line = FreeImage_GetScanLine(dib, (unsigned)(ch - 1 - y));
        for (x = 0; x < cw; x++, line += 4) {
            const BYTE *w = want + ((size_t)y * cw + x) * 4;
            const BYTE got[4] = { line[FI_RGBA_RED], line[FI_RGBA_GREEN], line[FI_RGBA_BLUE], line[FI_RGBA_ALPHA] };
            int off = abs(got[3] - w[3]) > 2;
            for (c = 0; c < 3 && w[3] && got[3]; c++) off |= abs(got[c] - w[c]) > 2;
            n += off;
        }
    }
    return n;
}

/* every page of a WebP, played, is the canvas the tags give */
static int plays_as_tags(const char *what, const char *path, const SolidPage *pages, int n, int cw, int ch) {
    FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, FALSE, TRUE, TRUE, WEBP_PLAYBACK);
    BYTE *want = (BYTE *)malloc((size_t)cw * ch * 4);
    int i, good = mb && FreeImage_GetPageCount(mb) == n;
    if (mb && !good) fail("%s: %d pages, expected %d", what, FreeImage_GetPageCount(mb), n);
    for (i = 0; good && i < n; i++) {
        FIBITMAP *dib = FreeImage_LockPage(mb, i);
        int d;
        play_tags(pages, i, cw, ch, want);
        d = canvas_diff(dib, want, cw, ch);
        if (d) { fail("%s: page %d, %d pixel(s) not as the tags play it", what, i, d); good = 0; }
        if (dib) FreeImage_UnlockPage(mb, dib, FALSE);
    }
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
    free(want);
    return good;
}

/* frames at odd positions, drawn in place, cleared, restoring the canvas: the animation plays as its tags say */
static void odd_positions(void) {
    static const SolidPage PAGES[7] = {
        { 0, 0, 40, 30, { 0, 0, 200, 255 }, 1, 0 },
        { 5, 3, 9, 7, { 0, 0, 0, 0 }, 1, 1 },             /* a transparent hole, in place, at an odd position */
        { 7, 13, 10, 8, { 220, 0, 0, 255 }, 2, 0 },       /* cleared alone at an odd position */
        { 21, 9, 11, 9, { 0, 180, 0, 255 }, 1, 0 },
        { 9, 9, 12, 12, { 255, 255, 0, 128 }, 1, 1 },     /* half transparent, in place over what is drawn */
        { 3, 1, 7, 6, { 255, 0, 255, 255 }, 3, 0 },       /* restores the canvas */
        { 31, 21, 6, 6, { 255, 255, 255, 200 }, 1, 0 },
    };
    char path[512], again[512];
    FIMULTIBITMAP *mb, *out;
    int i, bad = 0;

    printf("frames WebP cannot hold as they are\n");
    snprintf(path, sizeof(path), "%s", scratch("fi_webp_rt_odd.webp"));
    snprintf(again, sizeof(again), "%s", scratch("fi_webp_rt_odd_again.webp"));
    remove(path);
    mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, TRUE, FALSE, TRUE, 0);
    for (i = 0; mb && i < 7; i++) {
        FIBITMAP *dib = solid_rgba(PAGES[i].w, PAGES[i].h, PAGES[i].rgba);
        set_anim(dib, "FrameLeft", FIDT_SHORT, PAGES[i].x);
        set_anim(dib, "FrameTop", FIDT_SHORT, PAGES[i].y);
        set_anim(dib, "FrameTime", FIDT_LONG, 50);
        set_anim(dib, "DisposalMethod", FIDT_BYTE, PAGES[i].disposal);
        set_anim(dib, "BlendMethod", FIDT_BYTE, PAGES[i].blend);
        set_anim(dib, "LogicalWidth", FIDT_SHORT, 40);
        set_anim(dib, "LogicalHeight", FIDT_SHORT, 30);
        FreeImage_AppendPage(mb, dib);
        FreeImage_Unload(dib);
    }
    if (!mb || !FreeImage_CloseMultiBitmap(mb, WEBP_LOSSLESS)) { fail("odd positions: the animation was not written"); return; }
    bad |= !plays_as_tags("odd positions", path, PAGES, 7, 40, 30);

    /* its raw pages, saved again */
    remove(again);
    mb = FreeImage_OpenMultiBitmap(FIF_WEBP, path, FALSE, TRUE, TRUE, 0);
    out = FreeImage_OpenMultiBitmap(FIF_WEBP, again, TRUE, FALSE, TRUE, 0);
    for (i = 0; mb && out && i < FreeImage_GetPageCount(mb); i++) {
        FIBITMAP *page = FreeImage_LockPage(mb, i);
        if (page) { FreeImage_AppendPage(out, page); FreeImage_UnlockPage(mb, page, FALSE); }
    }
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
    if (!out || !FreeImage_CloseMultiBitmap(out, WEBP_LOSSLESS)) { fail("odd positions: the raw pages were not saved again"); bad = 1; }
    else bad |= !plays_as_tags("odd positions, raw pages saved again", again, PAGES, 7, 40, 30);

    if (!bad) printf("  ok %-34s odd positions, in place, cleared, restored: played as the tags say, and from the raw pages\n", "frames at odd positions");
    remove(path);
    remove(again);
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

    unfinished_animation(rgba);
    mixed_profiles();
    playback_pages();
    gif_playback_pages();
    one_frame_still();
    odd_positions();

    FreeImage_Unload(rgb); FreeImage_Unload(rgba); FreeImage_Unload(holes);
    FreeImage_Unload(grey); FreeImage_Unload(tiny); FreeImage_Unload(meta);

    printf("\n%s\n", failures ? "FAILED" : "every round trip came back as it should");
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
