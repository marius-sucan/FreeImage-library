/* every pixel layout, embedded profile, destination and intent against a per-pixel Little CMS reference */
/* ./convert [-digest | -quick]: -digest prints one digest of every result, -quick skips the large images */
#include "common.h"

enum {
    K_PAL1, K_BW1, K_PAL4, K_PAL8, K_GREYPAL8, K_PAL8T, K_GREYPAL8T, K_GREY8, K_GREY8R,
    K_555, K_565, K_RGB8, K_RGBA8, K_CMYK8,
    K_GRAY16, K_GRAY16R, K_RGB16, K_RGBA16, K_CMYK16,
    K_GRAYF, K_RGBF, K_RGBAF, K_COUNT
};

enum { M_NONE, M_GRAY, M_RGB, M_CMYK };

typedef struct {
    const char *name;
    int model;      /* of the pixels */
    int depth;      /* 8, 16, 32 (float) */
    int alpha;      /* alpha channel or transparency table */
    int family;     /* 1 palette, 2 grey ramp, 3 16-bit RGB, 0 direct */
} Kind;

static const Kind KINDS[K_COUNT] = {
    { "1-bit palette",          M_RGB,  8,  0, 1 },
    { "1-bit black/white",      M_GRAY, 8,  0, 1 },
    { "4-bit palette",          M_RGB,  8,  0, 1 },
    { "8-bit palette",          M_RGB,  8,  0, 1 },
    { "8-bit grey palette",     M_GRAY, 8,  0, 1 },
    { "8-bit transparent",      M_RGB,  8,  1, 1 },
    { "8-bit grey transparent", M_GRAY, 8,  1, 1 },
    { "8-bit grey",             M_GRAY, 8,  0, 2 },
    { "8-bit grey min-is-white",M_GRAY, 8,  0, 2 },
    { "16-bit 555",             M_RGB,  8,  0, 3 },
    { "16-bit 565",             M_RGB,  8,  0, 3 },
    { "24-bit",                 M_RGB,  8,  0, 0 },
    { "32-bit",                 M_RGB,  8,  1, 0 },
    { "32-bit CMYK",            M_CMYK, 8,  0, 0 },
    { "FIT_UINT16",             M_GRAY, 16, 0, 0 },
    { "FIT_UINT16 min-is-white",M_GRAY, 16, 0, 0 },
    { "FIT_RGB16",              M_RGB,  16, 0, 0 },
    { "FIT_RGBA16",             M_RGB,  16, 1, 0 },
    { "FIT_RGBA16 CMYK",        M_CMYK, 16, 0, 0 },
    { "FIT_FLOAT",              M_GRAY, 32, 0, 0 },
    { "FIT_RGBF",               M_RGB,  32, 0, 0 },
    { "FIT_RGBAF",              M_RGB,  32, 1, 0 },
};

/* ------------------------------------------------------------------ images */

static void set_minis_white(FIBITMAP *dib) {
    FITAG *tag = FreeImage_CreateTag();
    WORD v = 0;
    FreeImage_SetTagKey(tag, "PhotometricInterpretation");
    FreeImage_SetTagID(tag, 0x0106);
    FreeImage_SetTagType(tag, FIDT_SHORT);
    FreeImage_SetTagCount(tag, 1);
    FreeImage_SetTagLength(tag, 2);
    FreeImage_SetTagValue(tag, &v);
    FreeImage_SetMetadata(FIMD_EXIF_MAIN, dib, "PhotometricInterpretation", tag);
    FreeImage_DeleteTag(tag);
}

static float rndf(void) {
    return (float)(rnd() % 20001) / 16000.0f;   /* 0 .. 1.25 */
}

static FIBITMAP *make_image(int kind, int w, int h) {
    FIBITMAP *dib = NULL;
    RGBQUAD *pal;
    BYTE table[256];
    int x, y, i;
    rng_state = 0x9E3779B9u ^ (unsigned)(kind * 7919 + w * 31 + h);
    switch (kind) {
        case K_PAL1: case K_BW1:
            dib = FreeImage_Allocate(w, h, 1, 0, 0, 0);
            pal = FreeImage_GetPalette(dib);
            if (kind == K_BW1) {
                pal[0].rgbRed = pal[0].rgbGreen = pal[0].rgbBlue = 0;
                pal[1].rgbRed = pal[1].rgbGreen = pal[1].rgbBlue = 255;
            } else {
                pal[0].rgbRed = 200; pal[0].rgbGreen = 30; pal[0].rgbBlue = 40;
                pal[1].rgbRed = 20; pal[1].rgbGreen = 90; pal[1].rgbBlue = 230;
            }
            break;
        case K_PAL4:
            dib = FreeImage_Allocate(w, h, 4, 0, 0, 0);
            pal = FreeImage_GetPalette(dib);
            for (i = 0; i < 16; i++) { pal[i].rgbRed = rnd() & 255; pal[i].rgbGreen = rnd() & 255; pal[i].rgbBlue = rnd() & 255; }
            break;
        case K_PAL8: case K_PAL8T: case K_GREYPAL8: case K_GREYPAL8T: case K_GREY8: case K_GREY8R:
            dib = FreeImage_Allocate(w, h, 8, 0, 0, 0);
            pal = FreeImage_GetPalette(dib);
            for (i = 0; i < 256; i++) {
                if (kind == K_PAL8 || kind == K_PAL8T) {
                    pal[i].rgbRed = rnd() & 255; pal[i].rgbGreen = rnd() & 255; pal[i].rgbBlue = rnd() & 255;
                } else {
                    BYTE v = (kind == K_GREYPAL8) ? (BYTE)(rnd() & 255) : (kind == K_GREY8R) ? (BYTE)(255 - i) : (BYTE)i;
                    pal[i].rgbRed = pal[i].rgbGreen = pal[i].rgbBlue = v;
                }
            }
            if (kind == K_PAL8T || kind == K_GREYPAL8T) {
                for (i = 0; i < 256; i++) table[i] = rnd() & 255;
                FreeImage_SetTransparencyTable(dib, table, kind == K_PAL8T ? 200 : 256);
            }
            break;
        case K_555:
            dib = FreeImage_Allocate(w, h, 16, FI16_555_RED_MASK, FI16_555_GREEN_MASK, FI16_555_BLUE_MASK);
            break;
        case K_565:
            dib = FreeImage_Allocate(w, h, 16, FI16_565_RED_MASK, FI16_565_GREEN_MASK, FI16_565_BLUE_MASK);
            break;
        case K_RGB8: dib = FreeImage_Allocate(w, h, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK); break;
        case K_RGBA8: case K_CMYK8: dib = FreeImage_Allocate(w, h, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK); break;
        case K_GRAY16: case K_GRAY16R: dib = FreeImage_AllocateT(FIT_UINT16, w, h, 16, 0, 0, 0); break;
        case K_RGB16: dib = FreeImage_AllocateT(FIT_RGB16, w, h, 48, 0, 0, 0); break;
        case K_RGBA16: case K_CMYK16: dib = FreeImage_AllocateT(FIT_RGBA16, w, h, 64, 0, 0, 0); break;
        case K_GRAYF: dib = FreeImage_AllocateT(FIT_FLOAT, w, h, 32, 0, 0, 0); break;
        case K_RGBF: dib = FreeImage_AllocateT(FIT_RGBF, w, h, 96, 0, 0, 0); break;
        case K_RGBAF: dib = FreeImage_AllocateT(FIT_RGBAF, w, h, 128, 0, 0, 0); break;
    }
    for (y = 0; y < h; y++) {
        BYTE *bits = FreeImage_GetScanLine(dib, y);
        if (KINDS[kind].depth == 32) {
            float *f = (float *)bits;
            int n = w * (FreeImage_GetBPP(dib) / 32);
            for (x = 0; x < n; x++) f[x] = rndf();
        } else {
            unsigned line = FreeImage_GetLine(dib);
            for (x = 0; x < (int)line; x++) bits[x] = rnd() & 255;
        }
    }
    if (kind == K_CMYK8 || kind == K_CMYK16) FreeImage_GetICCProfile(dib)->flags |= FIICC_COLOR_IS_CMYK;
    if (kind == K_GRAY16R) set_minis_white(dib);
    FreeImage_SetDotsPerMeterX(dib, 2835);
    FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Comment", "icc test");
    return dib;
}

/* ------------------------------------------------------------------ pixels in canonical order */

typedef struct {
    double v[4];    /* grey; RGB; or CMYK */
    double a;       /* alpha, -1 when none */
} Pix;

static int depth_index(int depth) { return depth == 8 ? 0 : depth == 16 ? 1 : 2; }

static unsigned palette_index(FIBITMAP *dib, int x, int y) {
    BYTE i = 0;
    FreeImage_GetPixelIndex(dib, x, y, &i);
    return i;
}

/* model: the one the pixels are read as (a grey palette may be read as RGB) */
static Pix read_pixel(FIBITMAP *dib, int x, int y, int model) {
    Pix p;
    FREE_IMAGE_TYPE t = FreeImage_GetImageType(dib);
    unsigned bpp = FreeImage_GetBPP(dib);
    BYTE *bits = FreeImage_GetScanLine(dib, y);
    int cmyk = (FreeImage_GetICCProfile(dib)->flags & FIICC_COLOR_IS_CMYK) != 0;
    memset(&p, 0, sizeof(p));
    p.a = -1;
    if (t == FIT_BITMAP && bpp <= 8) {
        unsigned i = palette_index(dib, x, y);
        RGBQUAD c = FreeImage_GetPalette(dib)[i];
        if (model == M_GRAY) { p.v[0] = c.rgbRed; }
        else { p.v[0] = c.rgbRed; p.v[1] = c.rgbGreen; p.v[2] = c.rgbBlue; }
        if (FreeImage_IsTransparent(dib) && FreeImage_GetTransparencyCount(dib) > 0)
            p.a = (i < FreeImage_GetTransparencyCount(dib)) ? FreeImage_GetTransparencyTable(dib)[i] : 255;
    } else if (t == FIT_BITMAP && bpp == 16) {
        RGBQUAD c;
        FreeImage_GetPixelColor(dib, x, y, &c);
        p.v[0] = c.rgbRed; p.v[1] = c.rgbGreen; p.v[2] = c.rgbBlue;
    } else if (t == FIT_BITMAP) {
        BYTE *px = bits + x * (bpp / 8);
        if (cmyk) { p.v[0] = px[0]; p.v[1] = px[1]; p.v[2] = px[2]; p.v[3] = px[3]; }
        else {
            p.v[0] = px[FI_RGBA_RED]; p.v[1] = px[FI_RGBA_GREEN]; p.v[2] = px[FI_RGBA_BLUE];
            if (bpp == 32) p.a = px[FI_RGBA_ALPHA];
        }
    } else if (t == FIT_UINT16) {
        p.v[0] = ((WORD *)bits)[x];
    } else if (t == FIT_RGB16) {
        FIRGB16 *px = (FIRGB16 *)bits + x;
        p.v[0] = px->red; p.v[1] = px->green; p.v[2] = px->blue;
    } else if (t == FIT_RGBA16) {
        FIRGBA16 *px = (FIRGBA16 *)bits + x;
        p.v[0] = px->red; p.v[1] = px->green; p.v[2] = px->blue;
        if (cmyk) p.v[3] = px->alpha; else p.a = px->alpha;
    } else if (t == FIT_FLOAT) {
        p.v[0] = ((float *)bits)[x];
    } else if (t == FIT_RGBF) {
        FIRGBF *px = (FIRGBF *)bits + x;
        p.v[0] = px->red; p.v[1] = px->green; p.v[2] = px->blue;
    } else if (t == FIT_RGBAF) {
        FIRGBAF *px = (FIRGBAF *)bits + x;
        p.v[0] = px->red; p.v[1] = px->green; p.v[2] = px->blue; p.a = px->alpha;
    }
    return p;
}

/* ------------------------------------------------------------------ the rules, restated */

typedef struct {
    Bytes bytes;    /* NULL for device CMYK */
    int model;
} Eff;

static int profile_model(const Bytes *b) {
    cmsHPROFILE h;
    int model = M_NONE;
    if (!b->data || b->size < 128) return M_NONE;
    h = cmsOpenProfileFromMem(b->data, b->size);
    if (!h) return M_NONE;
    switch (cmsGetDeviceClass(h)) {
        case cmsSigInputClass: case cmsSigDisplayClass: case cmsSigOutputClass: case cmsSigColorSpaceClass:
            switch (cmsGetColorSpace(h)) {
                case cmsSigGrayData: model = M_GRAY; break;
                case cmsSigRgbData: model = M_RGB; break;
                case cmsSigCmykData: model = M_CMYK; break;
                default: break;
            }
        default: break;
    }
    cmsCloseProfile(h);
    return model;
}

static Bytes default_profile(int model, int depth) {
    if (model == M_GRAY) return builtin(depth == 32 ? FICMS_PROFILE_LINEAR_GRAY : FICMS_PROFILE_GRAY);
    return builtin(depth == 32 ? FICMS_PROFILE_LINEAR_SRGB : FICMS_PROFILE_SRGB);
}

/* the embedded profile when it describes the pixels, else FreeImage's default */
static Eff effective_source(int kind, const Bytes *tag) {
    Eff e;
    int m = tag ? profile_model(tag) : M_NONE;
    int model = KINDS[kind].model;
    if (m && (m == model || (KINDS[kind].family && KINDS[kind].family != 3 && model == M_GRAY && m == M_RGB))) {
        e.bytes = *tag;
        e.model = m;
    } else if (model == M_CMYK) {
        e.bytes.data = NULL; e.bytes.size = 0;
        e.model = M_CMYK;
    } else {
        e.bytes = default_profile(model, KINDS[kind].depth);
        e.model = model;
    }
    return e;
}

static int same_bytes(const Bytes *a, const Bytes *b) {
    return a->data && b->data && a->size == b->size && memcmp(a->data, b->data, a->size) == 0;
}

/* the layout of the result of FreeImage_ConvertToICCProfile */
typedef struct {
    FREE_IMAGE_TYPE type;
    unsigned bpp;
    int model;
    int depth;
    int alpha;
    int palette;    /* the source copied with its palette converted */
} Out;

static Out output_layout(int kind, int dst_model) {
    Out o;
    const Kind *k = &KINDS[kind];
    memset(&o, 0, sizeof(o));
    o.model = dst_model;
    o.depth = k->depth;
    if (dst_model == M_RGB) {
        o.alpha = k->alpha;
        if (k->depth == 8) { o.type = FIT_BITMAP; o.bpp = k->alpha ? 32 : 24; }
        else if (k->depth == 16) { o.type = k->alpha ? FIT_RGBA16 : FIT_RGB16; o.bpp = k->alpha ? 64 : 48; }
        else { o.type = k->alpha ? FIT_RGBAF : FIT_RGBF; o.bpp = k->alpha ? 128 : 96; }
    } else if (dst_model == M_GRAY) {
        if (k->depth == 8) {
            o.type = FIT_BITMAP;
            if (k->family == 1 && k->alpha) { o.palette = 1; o.alpha = 1; }
            else o.bpp = 8;
        } else if (k->depth == 16) { o.type = FIT_UINT16; o.bpp = 16; }
        else { o.type = FIT_FLOAT; o.bpp = 32; }
    } else {
        o.depth = (k->depth == 8) ? 8 : 16;
        o.type = (k->depth == 8) ? FIT_BITMAP : FIT_RGBA16;
        o.bpp = (k->depth == 8) ? 32 : 64;
    }
    return o;
}

/* ------------------------------------------------------------------ the reference */

static cmsUInt32Number fmt(int model, int depth, int alpha, int reversed) {
    int d = depth_index(depth);
    if (model == M_GRAY) {
        const cmsUInt32Number g[3] = { TYPE_GRAY_8, TYPE_GRAY_16, TYPE_GRAY_FLT };
        const cmsUInt32Number ga[3] = { TYPE_GRAYA_8, TYPE_GRAYA_16, TYPE_GRAYA_FLT };
        if (reversed) return d == 0 ? TYPE_GRAY_8_REV : TYPE_GRAY_16_REV;
        return alpha ? ga[d] : g[d];
    }
    if (model == M_RGB) {
        const cmsUInt32Number c[3] = { TYPE_RGB_8, TYPE_RGB_16, TYPE_RGB_FLT };
        const cmsUInt32Number ca[3] = { TYPE_RGBA_8, TYPE_RGBA_16, TYPE_RGBA_FLT };
        return alpha ? ca[d] : c[d];
    }
    return d == 0 ? TYPE_CMYK_8 : TYPE_CMYK_16;
}

static int channels(int model) { return model == M_GRAY ? 1 : model == M_RGB ? 3 : 4; }

typedef struct {
    cmsHTRANSFORM t;
    int in_model, in_depth, in_alpha;
    int out_model, out_depth, out_alpha;
} Stage;

static void run_stage(const Stage *s, const Pix *in, Pix *out) {
    BYTE ib[64], ob[64];
    int n = channels(s->in_model), m = channels(s->out_model), i;
    for (i = 0; i < n + s->in_alpha; i++) {
        double v = (i < n) ? in->v[i] : in->a;
        if (s->in_depth == 8) ib[i] = (BYTE)v;
        else if (s->in_depth == 16) ((WORD *)ib)[i] = (WORD)v;
        else ((float *)ib)[i] = (float)v;
    }
    cmsDoTransform(s->t, ib, ob, 1);
    for (i = 0; i < m + s->out_alpha; i++) {
        double v = (s->out_depth == 8) ? ob[i] : (s->out_depth == 16) ? ((WORD *)ob)[i] : ((float *)ob)[i];
        if (i < m) out->v[i] = v; else out->a = v;
    }
}

static unsigned cmyk_to_rgb(unsigned c, unsigned k, unsigned max) {
    return (max - c) * (max - k) / max;
}

static void rgb_to_cmyk(const Pix *in, int grey, double max, Pix *out) {
    unsigned M = (unsigned)max, v[3], top, i;
    if (grey) {
        out->v[0] = out->v[1] = out->v[2] = 0;
        out->v[3] = max - in->v[0];
        return;
    }
    for (i = 0; i < 3; i++) v[i] = (unsigned)in->v[i];
    top = v[0] > v[1] ? v[0] : v[1];
    top = top > v[2] ? top : v[2];
    if (top == 0) { out->v[0] = out->v[1] = out->v[2] = 0; out->v[3] = max; return; }
    for (i = 0; i < 3; i++) out->v[i] = M - (M * v[i] + top - 1) / top;
    out->v[3] = M - top;
}

typedef struct {
    Eff src, dst;
    int intent;
    int kind;
    Out out;
    int device_in, device_out, grey_device;
    Stage stage;
    int has_stage;
} Reference;

static cmsHPROFILE open_bytes(const Bytes *b) {
    return cmsOpenProfileFromMem(b->data, b->size);
}

/* FALSE when Little CMS cannot link the profiles */
static int reference_init(Reference *r, int kind, const Eff *src, const Eff *dst, int flags, const Out *out, const Bytes *proof) {
    Bytes in_bytes, out_bytes;
    cmsHPROFILE hin, hout, hproof = NULL;
    cmsUInt32Number lflags = 0;
    memset(r, 0, sizeof(*r));
    r->kind = kind; r->src = *src; r->dst = *dst; r->intent = flags & 0xFF; r->out = *out;
    r->device_in = (src->model == M_CMYK && !src->bytes.data);
    r->device_out = (dst->model == M_CMYK && !dst->bytes.data);
    r->grey_device = r->device_out && src->model == M_GRAY;
    in_bytes = r->device_in ? builtin(FICMS_PROFILE_SRGB) : src->bytes;
    out_bytes = r->device_out ? builtin(r->grey_device ? FICMS_PROFILE_GRAY : FICMS_PROFILE_SRGB) : dst->bytes;

    r->stage.in_model = r->device_in ? M_RGB : src->model;
    r->stage.in_depth = KINDS[kind].depth;
    r->stage.out_model = r->device_out ? (r->grey_device ? M_GRAY : M_RGB) : out->model;
    r->stage.out_depth = r->device_out ? (KINDS[kind].depth == 8 ? 8 : 16) : out->depth;
    r->stage.in_alpha = r->stage.out_alpha = (out->alpha && KINDS[kind].alpha && !out->palette && out->model != M_CMYK && !r->device_out) ? 1 : 0;
    if (!proof && same_bytes(&in_bytes, &out_bytes)) return 1;

    hin = open_bytes(&in_bytes);
    hout = open_bytes(&out_bytes);
    if (flags & FICMS_BLACKPOINT_COMPENSATION) lflags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
    if (r->stage.in_alpha) lflags |= cmsFLAGS_COPY_ALPHA;
    /* 16 bits are computed exactly */
    if (r->stage.in_depth == 16 || r->stage.out_depth == 16) lflags |= cmsFLAGS_NOOPTIMIZE;
    {
        int rev_in = (kind == K_GRAY16R);
        int rev_out = rev_in && r->stage.out_model == M_GRAY && !r->device_out;
        cmsUInt32Number fi = fmt(r->stage.in_model, r->stage.in_depth, r->stage.in_alpha, rev_in);
        cmsUInt32Number fo = fmt(r->stage.out_model, r->stage.out_depth, r->stage.out_alpha, rev_out);
        if (proof) {
            hproof = open_bytes(proof);
            lflags |= cmsFLAGS_SOFTPROOFING;
            if (flags & FICMS_GAMUT_CHECK) lflags |= cmsFLAGS_GAMUTCHECK;
            r->stage.t = cmsCreateProofingTransform(hin, fi, hout, fo, hproof, r->intent,
                (flags & FICMS_SIMULATE_PAPER) ? INTENT_ABSOLUTE_COLORIMETRIC : INTENT_RELATIVE_COLORIMETRIC, lflags);
            cmsCloseProfile(hproof);
        } else {
            r->stage.t = cmsCreateTransform(hin, fi, hout, fo, r->intent, lflags);
        }
    }
    cmsCloseProfile(hin);
    cmsCloseProfile(hout);
    r->has_stage = 1;
    return r->stage.t != NULL;
}

static void reference_free(Reference *r) {
    if (r->stage.t) cmsDeleteTransform(r->stage.t);
}

/* the expected result for one source pixel */
static Pix reference_pixel(const Reference *r, Pix in) {
    Pix mid = in, out;
    int d = KINDS[r->kind].depth;
    memset(&out, 0, sizeof(out));
    out.a = -1;
    if (r->device_in) {
        unsigned max = (d == 8) ? 255 : 65535, k = (unsigned)in.v[3], i;
        for (i = 0; i < 3; i++) mid.v[i] = cmyk_to_rgb((unsigned)in.v[i], k, max);
        mid.v[3] = 0;
    }
    if (r->has_stage) run_stage(&r->stage, &mid, &out);
    else out = mid;
    if (r->device_out) {
        Pix c;
        rgb_to_cmyk(&out, r->grey_device, (d == 8) ? 255 : 65535, &c);
        c.a = -1;
        out = c;
    }
    if (r->out.alpha && !r->stage.in_alpha) out.a = in.a;
    if (!r->out.alpha) out.a = -1;
    return out;
}

/* the result's pixel in canonical order */
static Pix read_result(FIBITMAP *dib, int x, int y, int model) {
    Pix p = read_pixel(dib, x, y, model);
    if (FreeImage_GetImageType(dib) == FIT_BITMAP && FreeImage_GetBPP(dib) == 32 && (FreeImage_GetICCProfile(dib)->flags & FIICC_COLOR_IS_CMYK))
        p.a = -1;
    return p;
}

static int same_pix(const Pix *a, const Pix *b, int model) {
    int i;
    for (i = 0; i < channels(model); i++) if (a->v[i] != b->v[i]) return 0;
    if ((a->a >= 0) != (b->a >= 0)) return 0;
    return a->a < 0 || a->a == b->a;
}

/* every pixel of a result against the reference; the first mismatch is reported */
static int check_pixels(const char *what, FIBITMAP *src, int kind, FIBITMAP *res, const Reference *r, int read_model, int opaque) {
    int w = FreeImage_GetWidth(src), h = FreeImage_GetHeight(src), x, y;
    for (y = 0; y < h; y++) for (x = 0; x < w; x++) {
        Pix exp = reference_pixel(r, read_pixel(src, x, y, r->src.model == M_CMYK ? M_CMYK : read_model));
        Pix got = read_result(res, x, y, r->out.model);
        if (opaque) {
            if (got.a != (KINDS[kind].depth == 8 ? 255 : 65535)) { fail("%s: alpha %g at %d,%d, not opaque", what, got.a, x, y); return 0; }
            got.a = -1;
        }
        checks++;
        if (!same_pix(&exp, &got, r->out.model)) {
            fail("%s: pixel %d,%d is (%g %g %g %g a %g), expected (%g %g %g %g a %g)", what, x, y,
                got.v[0], got.v[1], got.v[2], got.v[3], got.a, exp.v[0], exp.v[1], exp.v[2], exp.v[3], exp.a);
            return 0;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ the matrix */

typedef struct { const char *name; Bytes bytes; } Named;

static unsigned long long all_digest = 1469598103934665603ULL;
static int digest_only = 0;
static int quick = 0;

static int apply_allowed(int kind, int dst_model) {
    const Kind *k = &KINDS[kind];
    if (k->family == 1) return dst_model != M_CMYK;
    if (k->family == 2) return dst_model != M_CMYK;
    if (k->family == 3) return 0;
    if (k->model == M_CMYK) return dst_model != M_GRAY;
    return dst_model == k->model;
}

static void convert_case(int kind, int w, int h, const Named *tag, const Named *dst, int flags) {
    char what[256];
    FIBITMAP *src = make_image(kind, w, h), *res, *copy;
    Eff es, ed;
    Out out;
    Reference ref;
    int dst_model, identity, read_model;
    BOOL applied;
    unsigned long long before;

    if (tag->bytes.data) FreeImage_CreateICCProfile(src, tag->bytes.data, tag->bytes.size);
    es = effective_source(kind, tag->bytes.data ? &tag->bytes : NULL);
    if (dst->bytes.data) { ed.bytes = dst->bytes; ed.model = profile_model(&dst->bytes); }
    else {
        ed.model = (es.model == M_GRAY) ? M_GRAY : M_RGB;
        ed.bytes = default_profile(ed.model, KINDS[kind].depth);
    }
    dst_model = ed.model;
    out = output_layout(kind, dst_model);
    read_model = es.model;
    snprintf(what, sizeof(what), "%s %dx%d, tag %s -> %s, flags %x", KINDS[kind].name, w, h, tag->name, dst->name, flags);

    res = FreeImage_ConvertToICCProfile(src, dst->bytes.data, dst->bytes.size, flags);
    if (!res) { fail("%s: NULL (%s)", what, last_message); FreeImage_Unload(src); return; }
    all_digest = fnv(all_digest, (unsigned long long[]){ digest(res) }, 8);
    if (digest_only) { FreeImage_Unload(res); FreeImage_Unload(src); return; }

    /* the layout */
    if (out.palette) out.bpp = FreeImage_GetBPP(src);
    CHECK(FreeImage_GetImageType(res) == out.type && FreeImage_GetBPP(res) == out.bpp, "%s: type %d bpp %u, expected type %d bpp %u", what, FreeImage_GetImageType(res), FreeImage_GetBPP(res), out.type, out.bpp);
    CHECK(((FreeImage_GetICCProfile(res)->flags & FIICC_COLOR_IS_CMYK) != 0) == (dst_model == M_CMYK), "%s: CMYK flag", what);
    if (dst->bytes.data) {
        FIICCPROFILE *icc = FreeImage_GetICCProfile(res);
        CHECK(icc->size == dst->bytes.size && !memcmp(icc->data, dst->bytes.data, icc->size), "%s: not tagged with the destination profile", what);
    } else {
        CHECK(!FreeImage_GetICCProfile(res)->data && !FreeImage_GetICCProfile(res)->size, "%s: a default destination must leave the result untagged", what);
    }
    CHECK(FreeImage_GetDotsPerMeterX(res) == 2835 && FreeImage_GetMetadataCount(FIMD_COMMENTS, res) == 1, "%s: resolution or metadata lost", what);
    if (out.type == FIT_BITMAP && out.bpp == 8 && !out.palette) {
        FREE_IMAGE_COLOR_TYPE ct = FreeImage_GetColorType(res);
        CHECK(ct == FIC_MINISBLACK || ct == FIC_MINISWHITE, "%s: the grey result lost its ramp (%d)", what, ct);
    }

    /* the pixels */
    if (!reference_init(&ref, kind, &es, &ed, flags, &out, NULL)) {
        fail("%s: the reference cannot link the profiles", what);
    } else {
        check_pixels(what, src, kind, res, &ref, read_model, 0);
    }

    /* in place */
    identity = (es.model == ed.model) && ((!es.bytes.data && !ed.bytes.data) || same_bytes(&es.bytes, &ed.bytes));
    copy = FreeImage_Clone(src);
    before = digest(copy);
    clear_messages();
    applied = FreeImage_ApplyICCProfile(copy, dst->bytes.data, dst->bytes.size, flags);
    if (identity || apply_allowed(kind, dst_model)) {
        CHECK(applied, "%s: FreeImage_ApplyICCProfile failed (%s)", what, last_message);
        if (applied) {
            int opaque = (KINDS[kind].model == M_CMYK && dst_model == M_RGB);
            Out in_place = out;
            FREE_IMAGE_TYPE t = FreeImage_GetImageType(copy);
            CHECK(t == FreeImage_GetImageType(src) && FreeImage_GetBPP(copy) == FreeImage_GetBPP(src), "%s: in place changed the pixel size", what);
            in_place.alpha = KINDS[kind].alpha;
            if (identity) {
                CHECK(!memcmp(FreeImage_GetScanLine(copy, 0), FreeImage_GetScanLine(src, 0), FreeImage_GetLine(src)), "%s: identity changed pixels", what);
            } else {
                Reference r2;
                in_place.model = dst_model;
                if (reference_init(&r2, kind, &es, &ed, flags, &in_place, NULL)) {
                    char w2[300];
                    snprintf(w2, sizeof(w2), "%s (in place)", what);
                    check_pixels(w2, src, kind, copy, &r2, read_model, opaque);
                }
                reference_free(&r2);
            }
            CHECK(((FreeImage_GetICCProfile(copy)->flags & FIICC_COLOR_IS_CMYK) != 0) == (dst_model == M_CMYK), "%s: in place CMYK flag", what);
        }
    } else {
        CHECK(!applied, "%s: FreeImage_ApplyICCProfile changed the pixel format", what);
        CHECK(digest(copy) == before, "%s: a refused FreeImage_ApplyICCProfile changed the image", what);
    }
    FreeImage_Unload(copy);

    reference_free(&ref);
    FreeImage_Unload(res);
    FreeImage_Unload(src);
}

static void matrix(int w, int h, const Named *tags, int ntags, const Named *dsts, int ndsts, const int *flags, int nflags) {
    int k, t, d, f;
    for (k = 0; k < K_COUNT; k++)
        for (t = 0; t < ntags; t++)
            for (d = 0; d < ndsts; d++)
                for (f = 0; f < nflags; f++)
                    convert_case(k, w, h, &tags[t], &dsts[d], flags[f]);
}

/* ------------------------------------------------------------------ the wrappers */

static void wrappers(const Bytes *press, const Bytes *adobe) {
    FIBITMAP *rgb = make_image(K_RGB8, 45, 31), *cmyk, *back, *a, *b;
    int x, y, diff = 0;

    /* device CMYK: the exact inverse of the loaders' conversion */
    cmyk = FreeImage_ConvertToCMYK(rgb, NULL, 0, 0);
    CHECK(cmyk && FreeImage_GetColorType(cmyk) == FIC_CMYK && !FreeImage_GetICCProfile(cmyk)->data, "ConvertToCMYK(NULL): device CMYK, untagged");
    back = cmyk ? FreeImage_ConvertCMYKToRGB(cmyk, NULL, 0, 0) : NULL;
    CHECK(back && FreeImage_GetBPP(back) == 24, "ConvertCMYKToRGB(NULL): 24-bit");
    for (y = 0; back && y < 31; y++)
        diff += memcmp(FreeImage_GetScanLine(rgb, y), FreeImage_GetScanLine(back, y), 45 * 3) != 0;
    CHECK(back && diff == 0, "device CMYK round trip is not exact (%d rows)", diff);
    /* ConvertCMYKtoRGBA()'s formula, as the TIFF and PSD loaders use it */
    for (y = 0; cmyk && back && y < 31; y++) {
        BYTE *c = FreeImage_GetScanLine(cmyk, y), *r = FreeImage_GetScanLine(back, y);
        for (x = 0; x < 45; x++) {
            unsigned k = c[4 * x + 3];
            CHECK(r[3 * x + FI_RGBA_RED] == (255 - c[4 * x]) * (255 - k) / 255 &&
                  r[3 * x + FI_RGBA_GREEN] == (255 - c[4 * x + 1]) * (255 - k) / 255 &&
                  r[3 * x + FI_RGBA_BLUE] == (255 - c[4 * x + 2]) * (255 - k) / 255, "device CMYK formula at %d,%d", x, y);
        }
    }
    /* already CMYK and no profile: a copy */
    a = cmyk ? FreeImage_ConvertToCMYK(cmyk, NULL, 0, 0) : NULL;
    CHECK(a && digest(a) == digest(cmyk), "ConvertToCMYK of a CMYK image without a profile is a copy");
    if (a) FreeImage_Unload(a);
    if (back) FreeImage_Unload(back);

    /* the wrappers equal FreeImage_ConvertToICCProfile */
    a = FreeImage_ConvertToCMYK(rgb, press->data, press->size, FICMS_INTENT_RELATIVE_COLORIMETRIC);
    b = FreeImage_ConvertToICCProfile(rgb, press->data, press->size, FICMS_INTENT_RELATIVE_COLORIMETRIC);
    CHECK(a && b && digest(a) == digest(b), "ConvertToCMYK(press) differs from ConvertToICCProfile(press)");
    if (b) FreeImage_Unload(b);
    b = a ? FreeImage_ConvertCMYKToRGB(a, adobe->data, adobe->size, 0) : NULL;
    back = a ? FreeImage_ConvertToICCProfile(a, adobe->data, adobe->size, 0) : NULL;
    CHECK(b && back && digest(b) == digest(back), "ConvertCMYKToRGB(adobe) differs from ConvertToICCProfile(adobe)");
    if (b) FreeImage_Unload(b);
    if (back) FreeImage_Unload(back);
    if (a) FreeImage_Unload(a);

    /* refusals */
    clear_messages();
    CHECK(!FreeImage_ConvertCMYKToRGB(rgb, NULL, 0, 0) && message_count, "ConvertCMYKToRGB accepted an RGB image");
    CHECK(!FreeImage_ConvertToCMYK(rgb, adobe->data, adobe->size, 0), "ConvertToCMYK accepted an RGB profile");
    CHECK(!cmyk || !FreeImage_ConvertCMYKToRGB(cmyk, press->data, press->size, 0), "ConvertCMYKToRGB accepted a CMYK profile");
    CHECK(!FreeImage_SoftProof(rgb, NULL, 0, NULL, 0, 0), "SoftProof without a proofing profile");
    CHECK(!FreeImage_SoftProof(rgb, press->data, press->size, press->data, press->size, 0), "SoftProof accepted a CMYK display");

    if (cmyk) FreeImage_Unload(cmyk);
    FreeImage_Unload(rgb);
}

/* ------------------------------------------------------------------ soft proofing */

static void soft_proof(const Bytes *press, const Bytes *p3) {
    const int kinds[] = { K_PAL8, K_GREY8, K_RGB8, K_RGBA8, K_CMYK8, K_RGB16, K_GRAY16, K_RGBF, K_CMYK16 };
    const int flags[] = { 0, FICMS_SIMULATE_PAPER, FICMS_GAMUT_CHECK, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION };
    unsigned i, f;
    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) for (f = 0; f < 4; f++) for (int d = 0; d < 2; d++) {
        int kind = kinds[i];
        const Bytes *display = d ? p3 : NULL;
        char what[200];
        FIBITMAP *src = make_image(kind, 29, 17), *res;
        Eff es = effective_source(kind, NULL), ed;
        Out out;
        Reference ref;
        ed.model = M_RGB;
        ed.bytes = display ? *display : default_profile(M_RGB, KINDS[kind].depth);
        out = output_layout(kind, M_RGB);
        snprintf(what, sizeof(what), "soft proof %s, flags %x, display %s", KINDS[kind].name, flags[f], d ? "P3" : "default");
        if (KINDS[kind].depth == 32 && (flags[f] & FICMS_GAMUT_CHECK)) {
            /* Little CMS has no gamut check on floating-point pixels */
            FreeImage_Unload(src);
            continue;
        }
        res = FreeImage_SoftProof(src, press->data, press->size, display ? display->data : NULL, display ? display->size : 0, flags[f]);
        if (!res) { fail("%s: NULL (%s)", what, last_message); FreeImage_Unload(src); continue; }
        all_digest = fnv(all_digest, (unsigned long long[]){ digest(res) }, 8);
        if (!digest_only && reference_init(&ref, kind, &es, &ed, flags[f], &out, press)) {
            CHECK(FreeImage_GetImageType(res) == out.type && FreeImage_GetBPP(res) == out.bpp, "%s: layout", what);
            check_pixels(what, src, kind, res, &ref, es.model, 0);
            reference_free(&ref);
        }
        FreeImage_Unload(res);
        FreeImage_Unload(src);
    }
}

/* ------------------------------------------------------------------ views, identity, refusals */

static void views(const Bytes *p3) {
    FIBITMAP *img = make_image(K_RGB8, 64, 48), *orig = FreeImage_Clone(img), *view, *region, *expected;
    int x, y, bad = 0;
    view = FreeImage_CreateView(img, 10, 5, 50, 40);
    region = FreeImage_Copy(orig, 10, 5, 50, 40);
    expected = FreeImage_ConvertToICCProfile(region, p3->data, p3->size, 0);
    CHECK(FreeImage_ApplyICCProfile(view, p3->data, p3->size, 0), "ApplyICCProfile on a view");
    for (y = 0; y < 48; y++) {
        const BYTE *a = FreeImage_GetScanLine(img, y), *b = FreeImage_GetScanLine(orig, y);
        for (x = 0; x < 64; x++) {
            int inside = (x >= 10 && x < 50 && (47 - y) >= 5 && (47 - y) < 40);
            if (!inside && memcmp(a + 3 * x, b + 3 * x, 3)) bad++;
            if (inside && memcmp(a + 3 * x, FreeImage_GetScanLine(expected, y - (48 - 40)) + 3 * (x - 10), 3)) bad++;
        }
    }
    CHECK(bad == 0, "a view converted in place: %d pixels wrong", bad);
    FreeImage_Unload(view); FreeImage_Unload(region); FreeImage_Unload(expected); FreeImage_Unload(orig); FreeImage_Unload(img);

    /* a palettized view has its own palette: the backing image keeps its colors */
    img = make_image(K_PAL8, 32, 32);
    orig = FreeImage_Clone(img);
    view = FreeImage_CreateView(img, 0, 0, 16, 16);
    CHECK(FreeImage_ApplyICCProfile(view, p3->data, p3->size, 0), "ApplyICCProfile on a palette view");
    CHECK(digest(img) == digest(orig), "a palette view's conversion reached the backing image");
    FreeImage_Unload(view); FreeImage_Unload(orig); FreeImage_Unload(img);
}

static void special_cases(const Bytes *adobe, const Bytes *press) {
    FIBITMAP *img, *res;
    FIICCPROFILE *icc;
    unsigned long long before;

    /* no profile, default destination: nothing to do */
    img = make_image(K_RGB8, 20, 10);
    before = digest(img);
    CHECK(FreeImage_ApplyICCProfile(img, NULL, 0, 0) && digest(img) == before, "untagged image to the default space changed");
    res = FreeImage_ConvertToICCProfile(img, NULL, 0, 0);
    CHECK(res && digest(res) == before, "untagged image to the default space is not a copy");
    if (res) FreeImage_Unload(res);

    /* the image's own profile as the destination */
    FreeImage_CreateICCProfile(img, adobe->data, adobe->size);
    before = digest(img);
    icc = FreeImage_GetICCProfile(img);
    CHECK(FreeImage_ApplyICCProfile(img, icc->data, icc->size, 0) && digest(img) == before, "own profile as destination changed the image");
    icc = FreeImage_GetICCProfile(img);
    CHECK(icc->size == adobe->size && !memcmp(icc->data, adobe->data, adobe->size), "own profile as destination lost the profile");

    /* a profile that does not match the pixels is ignored, and removed by a default conversion */
    FreeImage_CreateICCProfile(img, press->data, press->size);
    clear_messages();
    CHECK(FreeImage_ApplyICCProfile(img, NULL, 0, 0), "mismatched profile: ApplyICCProfile failed");
    CHECK(!FreeImage_GetICCProfile(img)->data && message_count == 1, "mismatched profile: not removed or not reported");
    FreeImage_Unload(img);

    /* header only */
    {
        FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
        img = make_image(K_RGB8, 10, 10);
        FreeImage_SaveToMemory(FIF_PNG, img, mem, 0);
        FreeImage_Unload(img);
        FreeImage_SeekMemory(mem, 0, SEEK_SET);
        img = FreeImage_LoadFromMemory(FIF_PNG, mem, FIF_LOAD_NOPIXELS);
        CHECK(img && !FreeImage_HasPixels(img), "no header-only image");
        CHECK(!FreeImage_ConvertToICCProfile(img, NULL, 0, 0) && !FreeImage_ApplyICCProfile(img, NULL, 0, 0), "header-only image accepted");
        FreeImage_Unload(img);
        FreeImage_CloseMemory(mem);
    }
    CHECK(!FreeImage_ConvertToICCProfile(NULL, NULL, 0, 0) && !FreeImage_ApplyICCProfile(NULL, NULL, 0, 0) && !FreeImage_ConvertToCMYK(NULL, NULL, 0, 0), "NULL image accepted");

    /* types without color */
    {
        const FREE_IMAGE_TYPE types[] = { FIT_INT16, FIT_UINT32, FIT_INT32, FIT_DOUBLE, FIT_COMPLEX };
        unsigned i;
        for (i = 0; i < 5; i++) {
            img = FreeImage_AllocateT(types[i], 4, 4, 8, 0, 0, 0);
            clear_messages();
            CHECK(!FreeImage_ConvertToICCProfile(img, NULL, 0, 0) && message_count, "type %d accepted", types[i]);
            FreeImage_Unload(img);
        }
        img = FreeImage_AllocateT(FIT_RGBAF, 4, 4, 128, 0, 0, 0);
        FreeImage_GetICCProfile(img)->flags |= FIICC_COLOR_IS_CMYK;
        CHECK(!FreeImage_ConvertToICCProfile(img, NULL, 0, 0), "float CMYK accepted");
        FreeImage_Unload(img);
    }

    /* destinations that are not device profiles */
    {
        cmsHPROFILE hp[4];
        const char *names[4] = { "Lab", "XYZ", "device link", "abstract" };
        int i;
        hp[0] = cmsCreateLab4Profile(NULL);
        hp[1] = cmsCreateXYZProfile();
        hp[2] = cmsCreateInkLimitingDeviceLink(cmsSigCmykData, 250);
        hp[3] = cmsCreateBCHSWabstractProfile(17, 0, 1, 10, 0, 6500, 6500);
        img = make_image(K_RGB8, 8, 8);
        for (i = 0; i < 4; i++) {
            Bytes b = save_profile(hp[i]);
            clear_messages();
            CHECK(b.data && !FreeImage_ConvertToICCProfile(img, b.data, b.size, 0) && message_count, "%s destination accepted", names[i]);
            CHECK(b.data && !FreeImage_ApplyICCProfile(img, b.data, b.size, 0), "%s destination accepted in place", names[i]);
            /* as the embedded profile: ignored */
            FreeImage_CreateICCProfile(img, b.data, b.size);
            res = FreeImage_ConvertToICCProfile(img, NULL, 0, 0);
            CHECK(res != NULL, "%s embedded: conversion failed", names[i]);
            if (res) FreeImage_Unload(res);
            FreeImage_DestroyICCProfile(img);
            free(b.data);
            cmsCloseProfile(hp[i]);
        }
        CHECK(!FreeImage_ConvertToICCProfile(img, "abc", 3, 0), "3-byte profile accepted");
        FreeImage_Unload(img);
    }
}

int main(int argc, char **argv) {
    Named tags[6], dsts[9];
    Bytes gray18, press, test3, test5;
    const int flags_all[] = { FICMS_INTENT_PERCEPTUAL, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION, FICMS_INTENT_ABSOLUTE_COLORIMETRIC };
    const int flags_one[] = { FICMS_INTENT_RELATIVE_COLORIMETRIC };
    static BYTE junk[300];
    unsigned i;

    digest_only = (argc > 1 && !strcmp(argv[1], "-digest"));
    quick = (argc > 1 && !strcmp(argv[1], "-quick"));
    FreeImage_SetOutputMessage(on_message);

    gray18 = make_gray_profile(1.8);
    press = make_press_profile();
    test3 = read_file("data/test3.icc");
    test5 = read_file("data/test5.icc");
    if (!gray18.data || !press.data || !test3.data || !test5.data) { printf("cannot build or read the test profiles\n"); return 1; }
    for (i = 0; i < sizeof(junk); i++) junk[i] = (BYTE)(i * 37 + 11);

    tags[0].name = "none"; tags[0].bytes.data = NULL; tags[0].bytes.size = 0;
    tags[1].name = "grey gamma 1.8"; tags[1].bytes = gray18;
    tags[2].name = "Adobe RGB"; tags[2].bytes = builtin(FICMS_PROFILE_ADOBE_RGB);
    tags[3].name = "press CMYK"; tags[3].bytes = press;
    tags[4].name = "LUT RGB (test3)"; tags[4].bytes = test3;
    tags[5].name = "junk"; tags[5].bytes.data = junk; tags[5].bytes.size = sizeof(junk);

    dsts[0].name = "default"; dsts[0].bytes.data = NULL; dsts[0].bytes.size = 0;
    dsts[1].name = "sRGB"; dsts[1].bytes = builtin(FICMS_PROFILE_SRGB);
    dsts[2].name = "Adobe RGB"; dsts[2].bytes = builtin(FICMS_PROFILE_ADOBE_RGB);
    dsts[3].name = "Display P3"; dsts[3].bytes = builtin(FICMS_PROFILE_DISPLAY_P3);
    dsts[4].name = "press CMYK"; dsts[4].bytes = press;
    dsts[5].name = "grey gamma 1.8"; dsts[5].bytes = gray18;
    dsts[6].name = "linear grey"; dsts[6].bytes = builtin(FICMS_PROFILE_LINEAR_GRAY);
    dsts[7].name = "v2 monitor (test5)"; dsts[7].bytes = test5;
    dsts[8].name = "LUT RGB (test3)"; dsts[8].bytes = test3;

    /* every combination on a small image, one thread */
    matrix(23, 13, tags, 6, dsts, 9, flags_all, 3);
    /* above the threading threshold */
    if (!quick) {
        const Named big_tags[3] = { tags[0], tags[2], tags[3] };
        const Named big_dsts[4] = { dsts[0], dsts[2], dsts[4], dsts[5] };
        matrix(173, 101, big_tags, 3, big_dsts, 4, flags_one, 1);
    }
    wrappers(&press, &dsts[2].bytes);
    soft_proof(&press, &dsts[3].bytes);
    if (!digest_only) {
        views(&dsts[3].bytes);
        special_cases(&dsts[2].bytes, &press);
    }

    free(gray18.data); free(press.data); free(test3.data); free(test5.data);
    if (digest_only) {
        printf("%016llx\n", all_digest);
        return 0;
    }
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
