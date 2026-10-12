/* Writes data/: small JPEG XL files, each for one reader path; "./mkdata name..." writes only those.
   Most go through FreeImage's writer; the rest through libjxl, which libfreeimage.a holds.
   Kept as documentation: decode.c pins what this libjxl decodes them to, so do not regenerate. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "FreeImage.h"
#include <jxl/encode.h>
#include "../../Source/LibLCMS2/include/lcms2.h"
#include "pattern.h"

static void message(FREE_IMAGE_FORMAT fif, const char *msg) {
    printf("    [%s] %s\n", fif == FIF_UNKNOWN ? "?" : FreeImage_GetFormatFromFIF(fif), msg);
}

static int g_argc;
static char **g_argv;

static int wanted(const char *name) {
    int i;
    for (i = 1; i < g_argc; i++) if (!strcmp(g_argv[i], name)) return 1;
    return g_argc < 2;
}

static void save(FIBITMAP *d, const char *name, int flags) {
    char path[256];
    if (!wanted(name)) return;
    snprintf(path, sizeof(path), "data/%s", name);
    printf("%-24s %s\n", name, FreeImage_Save(FIF_JXL, d, path, flags) ? "ok" : "FAILED");
}

static void set_cicp(FIBITMAP *d, BYTE p, BYTE t) {
    BYTE v[4] = { p, t, 0, 1 };
    FITAG *tag = FreeImage_CreateTag();
    FreeImage_SetTagKey(tag, "CICP"); FreeImage_SetTagType(tag, FIDT_BYTE);
    FreeImage_SetTagCount(tag, 4); FreeImage_SetTagLength(tag, 4); FreeImage_SetTagValue(tag, v);
    FreeImage_SetMetadata(FIMD_CUSTOM, d, "CICP", tag);
    FreeImage_DeleteTag(tag);
}

static void set_anim(FIBITMAP *d, WORD left, WORD top, LONG ms, BYTE disposal, BYTE blend, WORD lw, WORD lh) {
    struct { const char *key; FREE_IMAGE_MDTYPE type; DWORD len; const void *v; } t[] = {
        {"FrameLeft", FIDT_SHORT, 2, &left}, {"FrameTop", FIDT_SHORT, 2, &top}, {"FrameTime", FIDT_LONG, 4, &ms},
        {"DisposalMethod", FIDT_BYTE, 1, &disposal}, {"BlendMethod", FIDT_BYTE, 1, &blend},
        {"LogicalWidth", FIDT_SHORT, 2, &lw}, {"LogicalHeight", FIDT_SHORT, 2, &lh},
    };
    for (unsigned i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        FITAG *tag = FreeImage_CreateTag();
        FreeImage_SetTagKey(tag, t[i].key); FreeImage_SetTagType(tag, t[i].type);
        FreeImage_SetTagCount(tag, 1); FreeImage_SetTagLength(tag, t[i].len); FreeImage_SetTagValue(tag, t[i].v);
        FreeImage_SetMetadata(FIMD_ANIMATION, d, t[i].key, tag);
        FreeImage_DeleteTag(tag);
    }
}

/* ---- files written through libjxl itself ---- */

typedef struct { uint32_t w, h, channels, bits; int orientation, premul, frames, layered, container, brob_exif; } Raw;

static void finish(JxlEncoder *enc, const char *name);

static void raw(const char *name, Raw r) {
    if (!wanted(name)) return;
    JxlEncoder *enc = JxlEncoderCreate(NULL);
    JxlBasicInfo bi; JxlEncoderInitBasicInfo(&bi);
    bi.xsize = r.w; bi.ysize = r.h; bi.bits_per_sample = r.bits;
    bi.num_color_channels = (r.channels >= 3) ? 3 : 1;
    if (r.channels == 2 || r.channels == 4) { bi.alpha_bits = r.bits; bi.num_extra_channels = 1; bi.alpha_premultiplied = r.premul ? JXL_TRUE : JXL_FALSE; }
    bi.uses_original_profile = JXL_TRUE;
    bi.orientation = (JxlOrientation)(r.orientation ? r.orientation : 1);
    if (r.frames > 1 && !r.layered) { bi.have_animation = JXL_TRUE; bi.animation.tps_numerator = 100; bi.animation.tps_denominator = 1; bi.animation.num_loops = 3; }
    JxlEncoderUseContainer(enc, r.container ? JXL_TRUE : JXL_FALSE);
    JxlEncoderSetBasicInfo(enc, &bi);
    if (bi.alpha_bits) {
        JxlExtraChannelInfo ec; JxlEncoderInitExtraChannelInfo(JXL_CHANNEL_ALPHA, &ec);
        ec.bits_per_sample = r.bits; ec.alpha_premultiplied = bi.alpha_premultiplied;
        JxlEncoderSetExtraChannelInfo(enc, 0, &ec);
    }
    JxlColorEncoding ce; JxlColorEncodingSetToSRGB(&ce, bi.num_color_channels == 1);
    JxlEncoderSetColorEncoding(enc, &ce);
    if (r.brob_exif) {
        /* little-endian TIFF, one IFD entry: Orientation 1 */
        static const uint8_t exif[] = { 0, 0, 0, 0, 'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 };
        JxlEncoderUseBoxes(enc);
        JxlEncoderAddBox(enc, "Exif", exif, sizeof(exif), JXL_TRUE);
        JxlEncoderCloseBoxes(enc);
    }
    JxlEncoderFrameSettings *fs = JxlEncoderFrameSettingsCreate(enc, NULL);
    JxlEncoderSetFrameLossless(fs, JXL_TRUE);
    JxlEncoderFrameSettingsSetOption(fs, JXL_ENC_FRAME_SETTING_EFFORT, 3);
    JxlPixelFormat pf = { r.channels, r.bits > 8 ? JXL_TYPE_UINT16 : JXL_TYPE_UINT8, JXL_LITTLE_ENDIAN, 0 };
    const size_t sample = r.bits > 8 ? 2 : 1, max = (1u << r.bits) - 1;
    size_t n = (size_t)r.w * r.h * r.channels, i;
    unsigned char *px = malloc(n * sample);
    for (int f = 0; f < (r.frames ? r.frames : 1); f++) {
        for (uint32_t y = 0; y < r.h; y++) for (uint32_t x = 0; x < r.w; x++) for (uint32_t c = 0; c < r.channels; c++) {
            double v = wave(x, y, r.w, r.h, (int)c, f);
            if ((r.channels == 2 || r.channels == 4) && c == r.channels - 1) v = (x < r.w / 2) ? 1.0 : 0.25 + 0.5 * ((double)y / r.h);
            if (v < 0) v = 0; if (v > 1) v = 1;
            if (r.premul && (r.channels == 4) && c < 3) v *= (x < r.w / 2) ? 1.0 : 0.25 + 0.5 * ((double)y / r.h);
            i = (((size_t)y * r.w + x) * r.channels + c);
            unsigned s = (unsigned)(v * max + 0.5) * (r.bits > 8 ? 65535 / max : 255 / max);
            if (sample == 2) { px[2 * i] = (unsigned char)s; px[2 * i + 1] = (unsigned char)(s >> 8); } else px[i] = (unsigned char)s;
        }
        if (r.frames > 1) {
            JxlFrameHeader fh; JxlEncoderInitFrameHeader(&fh);
            fh.duration = r.layered ? 0 : 7 + 3 * f;
            if (r.layered && f == 0) {
                /* a bottom layer, then a smaller one on top of it */
            } else if (r.layered) {
                fh.layer_info.have_crop = JXL_TRUE; fh.layer_info.crop_x0 = (int32_t)r.w / 4; fh.layer_info.crop_y0 = (int32_t)r.h / 4;
                fh.layer_info.xsize = r.w; fh.layer_info.ysize = r.h;
                fh.layer_info.blend_info.blendmode = JXL_BLEND_BLEND;
            }
            JxlEncoderSetFrameHeader(fs, &fh);
        }
        JxlEncoderAddImageFrame(fs, &pf, px, n * sample);
    }
    free(px);
    finish(enc, name);
}

/* the encoder's output into data/ */
static void finish(JxlEncoder *enc, const char *name) {
    JxlEncoderCloseInput(enc);
    size_t cap = 1 << 16, pos = 0; unsigned char *out = malloc(cap);
    for (;;) {
        uint8_t *next = out + pos; size_t avail = cap - pos;
        JxlEncoderStatus st = JxlEncoderProcessOutput(enc, &next, &avail);
        pos = next - out;
        if (st == JXL_ENC_NEED_MORE_OUTPUT) { cap *= 2; out = realloc(out, cap); continue; }
        if (st != JXL_ENC_SUCCESS) { printf("%-24s FAILED (%d)\n", name, JxlEncoderGetError(enc)); pos = 0; }
        break;
    }
    if (pos) {
        char path[256]; snprintf(path, sizeof(path), "data/%s", name);
        FILE *f = fopen(path, "wb"); fwrite(out, 1, pos, f); fclose(f);
        printf("%-24s ok\n", name);
    }
    free(out); JxlEncoderDestroy(enc);
}

/* an RGB profile with a sampled tone curve: no JPEG XL enum describes it */
static void sampled_profile(BYTE **data, DWORD *size) {
    cmsCIExyY d65 = { 0.3127, 0.3290, 1.0 };
    cmsCIExyYTRIPLE prim = { {0.680, 0.320, 1.0}, {0.265, 0.690, 1.0}, {0.150, 0.060, 1.0} };
    cmsUInt16Number table[256];
    for (int i = 0; i < 256; i++) table[i] = (cmsUInt16Number)(pow(i / 255.0, 2.0) * 65535 + 0.5);
    cmsToneCurve *curve = cmsBuildTabulatedToneCurve16(NULL, 256, table);
    cmsToneCurve *curves[3] = { curve, curve, curve };
    cmsHPROFILE p = cmsCreateRGBProfile(&d65, &prim, curves);
    cmsUInt32Number n = 0;
    cmsSaveProfileToMem(p, NULL, &n);
    *data = malloc(n); cmsSaveProfileToMem(p, *data, &n); *size = n;
    memset(*data + 24, 0, 12);  /* the creation date, so the file is the same every time */
    cmsCloseProfile(p); cmsFreeToneCurve(curve);
}

/* lossy frames whose colours are an ICC profile, cropped and blended over the frame before */
static void icc_blended(const char *name) {
    static const struct { uint32_t w, h; int32_t x, y; } fr[] = { {48, 40, 0, 0}, {20, 16, 5, 6}, {24, 12, 20, 20} };
    BYTE *icc; DWORD size;
    uint32_t x, y;
    int i;
    if (!wanted(name)) return;
    sampled_profile(&icc, &size);
    JxlEncoder *enc = JxlEncoderCreate(NULL);
    JxlBasicInfo bi; JxlEncoderInitBasicInfo(&bi);
    bi.xsize = 48; bi.ysize = 40; bi.alpha_bits = 8; bi.num_extra_channels = 1;
    bi.have_animation = JXL_TRUE; bi.animation.tps_numerator = 1000; bi.animation.tps_denominator = 1;
    JxlEncoderSetBasicInfo(enc, &bi);
    JxlExtraChannelInfo ec; JxlEncoderInitExtraChannelInfo(JXL_CHANNEL_ALPHA, &ec); ec.bits_per_sample = 8;
    JxlEncoderSetExtraChannelInfo(enc, 0, &ec);
    JxlEncoderSetICCProfile(enc, icc, size);
    JxlEncoderFrameSettings *fs = JxlEncoderFrameSettingsCreate(enc, NULL);
    JxlEncoderSetFrameDistance(fs, 1.0f);
    JxlPixelFormat pf = { 4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
    for (i = 0; i < 3; i++) {
        FIBITMAP *d = pattern(FIT_BITMAP, 32, fr[i].w, fr[i].h, 30 + i);
        unsigned char *px = malloc((size_t)fr[i].w * fr[i].h * 4), *o = px;
        for (y = 0; y < fr[i].h; y++) {
            const BYTE *p = FreeImage_GetScanLine(d, fr[i].h - 1 - y);
            for (x = 0; x < fr[i].w; x++, p += 4, o += 4) { o[0] = p[FI_RGBA_RED]; o[1] = p[FI_RGBA_GREEN]; o[2] = p[FI_RGBA_BLUE]; o[3] = p[FI_RGBA_ALPHA]; }
        }
        JxlFrameHeader fh; JxlEncoderInitFrameHeader(&fh);
        fh.duration = 100;
        fh.layer_info.have_crop = (i > 0) ? JXL_TRUE : JXL_FALSE;
        fh.layer_info.crop_x0 = fr[i].x; fh.layer_info.crop_y0 = fr[i].y;
        fh.layer_info.xsize = fr[i].w; fh.layer_info.ysize = fr[i].h;
        fh.layer_info.blend_info.blendmode = JXL_BLEND_BLEND;
        fh.layer_info.blend_info.source = 1;
        fh.layer_info.save_as_reference = 1;
        JxlEncoderSetFrameHeader(fs, &fh);
        JxlEncoderSetExtraChannelBlendInfo(fs, 0, &fh.layer_info.blend_info);
        JxlEncoderAddImageFrame(fs, &pf, px, (size_t)fr[i].w * fr[i].h * 4);
        free(px); FreeImage_Unload(d);
    }
    free(icc);
    finish(enc, name);
}

int main(int argc, char **argv) {
    g_argc = argc; g_argv = argv;
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(message);
    FIBITMAP *d;

    d = pattern(FIT_BITMAP, 8, 61, 37, 1); save(d, "grey8_lossless.jxl", JXL_LOSSLESS); FreeImage_Unload(d);
    d = pattern(FIT_BITMAP, 24, 64, 48, 2); save(d, "rgb8_lossy.jxl", 0); FreeImage_Unload(d);
    d = pattern(FIT_BITMAP, 32, 64, 48, 3); save(d, "rgba8_lossless.jxl", JXL_LOSSLESS); save(d, "rgba8_lossy.jxl", 80); FreeImage_Unload(d);
    d = pattern(FIT_UINT16, 16, 40, 30, 4); save(d, "grey16_lossless.jxl", JXL_LOSSLESS); FreeImage_Unload(d);
    d = pattern(FIT_RGB16, 48, 40, 30, 5); set_cicp(d, 9, 16); save(d, "rgb16_pq_lossless.jxl", JXL_LOSSLESS); FreeImage_Unload(d);
    d = pattern(FIT_RGBAF, 128, 32, 24, 6); save(d, "rgbaf_lossless.jxl", JXL_LOSSLESS); FreeImage_Unload(d);
    d = pattern(FIT_RGBF, 96, 32, 24, 7); save(d, "rgbf_lossy.jxl", 0); FreeImage_Unload(d);
    if (wanted("meta_lossy.jxl") || wanted("meta_lossless.jxl")) {
        /* an image with Exif, XMP and an sRGB profile, made smaller */
        FIBITMAP *src = FreeImage_Load(FIF_JPEG, "../exif.jpg", 0);
        d = FreeImage_Rescale(src, 75, 50, FILTER_BILINEAR);
        FreeImage_CloneMetadata(d, src);
        FIICCPROFILE *icc = FreeImage_GetICCProfile(src);
        FreeImage_CreateICCProfile(d, icc->data, icc->size);
        save(d, "meta_lossy.jxl", 0);
        save(d, "meta_lossless.jxl", JXL_LOSSLESS);
        FreeImage_Unload(d); FreeImage_Unload(src);
    }
    {
        BYTE *icc; DWORD size;
        sampled_profile(&icc, &size);
        d = pattern(FIT_BITMAP, 24, 64, 48, 8);
        FreeImage_CreateICCProfile(d, icc, size);
        save(d, "icc_sampled_lossy.jxl", 0);
        FreeImage_Unload(d); free(icc);
    }
    if (wanted("anim_lossless.jxl")) {
        /* frames at offsets, every disposal, both blends */
        FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(FIF_JXL, "data/anim_lossless.jxl", TRUE, FALSE, TRUE, 0);
        struct { unsigned w, h; WORD x, y; LONG ms; BYTE disposal, blend; } fr[] = {
            {48, 40, 0, 0, 100, 1, 1}, {20, 16, 5, 6, 80, 2, 0}, {24, 12, 20, 20, 60, 3, 0}, {16, 16, 30, 4, 40, 1, 1}, {48, 40, 0, 0, 20, 1, 0},
        };
        for (int i = 0; i < 5; i++) {
            d = pattern(FIT_BITMAP, 32, fr[i].w, fr[i].h, 10 + i);
            set_anim(d, fr[i].x, fr[i].y, fr[i].ms, fr[i].disposal, fr[i].blend, 48, 40);
            FreeImage_AppendPage(m, d);
            FreeImage_Unload(d);
        }
        printf("%-24s %s\n", "anim_lossless.jxl", FreeImage_CloseMultiBitmap(m, JXL_LOSSLESS) ? "ok" : "FAILED");
    }
    if (wanted("anim16_lossy.jxl")) {
        FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(FIF_JXL, "data/anim16_lossy.jxl", TRUE, FALSE, TRUE, 0);
        for (int i = 0; i < 3; i++) {
            d = pattern(FIT_RGB16, 48, 36, 28, 20 + i);
            set_anim(d, 0, 0, 50, 1, 1, 36, 28);
            FreeImage_AppendPage(m, d);
            FreeImage_Unload(d);
        }
        printf("%-24s %s\n", "anim16_lossy.jxl", FreeImage_CloseMultiBitmap(m, 0) ? "ok" : "FAILED");
    }

    raw("oriented6.jxl", (Raw){ 40, 20, 3, 8, 6, 0, 1, 0, 0, 0 });
    raw("oriented5_alpha.jxl", (Raw){ 30, 18, 4, 8, 5, 0, 1, 0, 1, 0 });
    raw("bare_grey_alpha.jxl", (Raw){ 33, 21, 2, 8, 0, 0, 1, 0, 0, 0 });
    raw("rgb12_lossless.jxl", (Raw){ 30, 20, 3, 12, 0, 0, 1, 0, 0, 0 });
    raw("premultiplied.jxl", (Raw){ 32, 24, 4, 16, 0, 1, 1, 0, 1, 0 });
    raw("layers_still.jxl", (Raw){ 40, 32, 4, 8, 0, 0, 2, 1, 1, 0 });
    raw("anim_ticks.jxl", (Raw){ 24, 16, 3, 8, 0, 0, 4, 0, 1, 0 });
    raw("brob_exif.jxl", (Raw){ 20, 10, 3, 8, 0, 0, 1, 0, 1, 1 });
    icc_blended("anim_icc_lossy.jxl");
    return 0;
}
