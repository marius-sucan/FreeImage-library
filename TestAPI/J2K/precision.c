/* JPEG 2000 samples of 1 to 7 and 9 to 15 bits: spread over 8 or 16 bits by default, kept as the
   file holds them with J2K_UNSCALED / JP2_UNSCALED; the SignificantBits and UnscaledBits tags;
   FreeImage_MustTonemap's verdict. The files are encoded with OpenJPEG itself, since FreeImage
   writes 8 and 16 bits only. Prints a report, exits non-zero on failure; scratch files in
   $J2K_TEST_TMP, or the current directory. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"
#define OPJ_STATIC
#include "../../Source/LibOpenJPEG/openjpeg.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

#define W 61
#define H 37

static const char *scratch(char *path, size_t size, const char *name) {
    const char *dir = getenv("J2K_TEST_TMP");
    snprintf(path, size, "%s/%s", (dir && *dir) ? dir : ".", name);
    return path;
}

/* sample i of component c; the first sample of each component is the largest */
static unsigned value(int c, unsigned i, int prec) {
    const unsigned maximum = (1u << prec) - 1;
    return i ? (i * 7919u + (unsigned)c * 104729u) % (maximum + 1) : maximum;
}

/* the rule of the loader: rounded, so that the largest sample becomes the largest of the bitmap */
static unsigned spread(unsigned v, int prec) {
    const unsigned long long maximum = (1u << prec) - 1;
    const unsigned long long top = (prec < 8) ? 255 : 65535;
    return (prec == 8 || prec == 16) ? v : (unsigned)((v * top * 2 + maximum) / (2 * maximum));
}

static int encode(const char *path, OPJ_CODEC_FORMAT format, int ncomps, int prec, int sgnd) {
    opj_cparameters_t params;
    opj_image_cmptparm_t cmpt[4];
    opj_image_t *image;
    opj_codec_t *codec;
    opj_stream_t *stream;
    int c, ok;
    unsigned i;
    opj_set_default_encoder_parameters(&params);
    params.tcp_numlayers = 1;
    params.tcp_rates[0] = 0;    /* lossless */
    params.cp_disto_alloc = 1;
    params.numresolution = 3;
    memset(cmpt, 0, sizeof(cmpt));
    for (c = 0; c < ncomps; c++) {
        cmpt[c].dx = cmpt[c].dy = 1;
        cmpt[c].w = W;
        cmpt[c].h = H;
        cmpt[c].prec = prec;
        cmpt[c].sgnd = sgnd;
    }
    image = opj_image_create(ncomps, cmpt, (ncomps >= 3) ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY);
    if (!image) return 0;
    image->x0 = image->y0 = 0;
    image->x1 = W;
    image->y1 = H;
    for (c = 0; c < ncomps; c++) {
        for (i = 0; i < W * H; i++) {
            image->comps[c].data[i] = (OPJ_INT32)value(c, i, prec) - (sgnd ? (1 << (prec - 1)) : 0);
        }
    }
    if (ncomps == 4) image->comps[3].alpha = 1;
    codec = opj_create_compress(format);
    ok = codec && opj_setup_encoder(codec, &params, image);
    stream = ok ? opj_stream_create_default_file_stream(path, OPJ_FALSE) : NULL;
    ok = ok && stream && opj_start_compress(codec, image, stream) && opj_encode(codec, stream) && opj_end_compress(codec, stream);
    if (stream) opj_stream_destroy(stream);
    if (codec) opj_destroy_codec(codec);
    opj_image_destroy(image);
    return ok;
}

static int tag_byte(FIBITMAP *dib, const char *key) {
    FITAG *tag = NULL;
    if (!FreeImage_GetMetadata(FIMD_CUSTOM, dib, key, &tag) || !tag) return -1;
    if (FreeImage_GetTagType(tag) != FIDT_BYTE || FreeImage_GetTagCount(tag) != 1) return -2;
    return *(const BYTE *)FreeImage_GetTagValue(tag);
}

/* the sample of component c at pixel i, in the file's order (rows top-down) */
static unsigned sample(FIBITMAP *dib, int ncomps, int c, unsigned i) {
    const unsigned x = i % W, y = H - 1 - i / W;
    const BYTE *line = FreeImage_GetScanLine(dib, y);
    static const int order[4] = { FI_RGBA_RED, FI_RGBA_GREEN, FI_RGBA_BLUE, FI_RGBA_ALPHA };
    switch (FreeImage_GetImageType(dib)) {
        case FIT_BITMAP: return (ncomps == 1) ? line[x] : line[x * ncomps + order[c]];
        case FIT_UINT16: return ((const WORD *)line)[x];
        case FIT_RGB16:  return ((const WORD *)line)[x * 3 + c];
        case FIT_RGBA16: return ((const WORD *)line)[x * 4 + c];
        default:         return 0xFFFFFFFF;
    }
}

/* every sample of the bitmap against the file's values, spread or not */
static int same_samples(FIBITMAP *dib, int ncomps, int prec, int spread_them) {
    int c;
    unsigned i;
    for (c = 0; c < ncomps; c++) {
        for (i = 0; i < W * H; i++) {
            const unsigned want = spread_them ? spread(value(c, i, prec), prec) : value(c, i, prec);
            if (sample(dib, ncomps, c, i) != want) return 0;
        }
    }
    return 1;
}

static void test_file(FREE_IMAGE_FORMAT fif, int ncomps, int prec, int sgnd) {
    const OPJ_CODEC_FORMAT format = (fif == FIF_JP2) ? OPJ_CODEC_JP2 : OPJ_CODEC_J2K;
    const int raw_flag = (fif == FIF_JP2) ? JP2_UNSCALED : J2K_UNSCALED;
    const int odd = (prec != 8 && prec != 16);
    char path[512], name[64], what[96];
    FIBITMAP *dib;
    snprintf(name, sizeof(name), "fi_j2k_precision-%d-%d%s.%s", ncomps, prec, sgnd ? "s" : "", (fif == FIF_JP2) ? "jp2" : "j2k");
    snprintf(what, sizeof(what), "%s, %d component(s) of %d bits%s", (fif == FIF_JP2) ? "JP2" : "J2K", ncomps, prec, sgnd ? ", signed" : "");
    scratch(path, sizeof(path), name);
    if (!encode(path, format, ncomps, prec, sgnd)) {
        CHECK(0, "%s: OpenJPEG could not encode it", what);
        return;
    }

    dib = FreeImage_Load(fif, path, 0);
    CHECK(dib != NULL, "%s: not loaded", what);
    if (dib) {
        CHECK(same_samples(dib, ncomps, prec, 1), "%s: the samples are not spread over %d bits", what, (prec <= 8) ? 8 : 16);
        CHECK(tag_byte(dib, "SignificantBits") == (odd ? prec : -1), "%s: SignificantBits %d", what, tag_byte(dib, "SignificantBits"));
        CHECK(tag_byte(dib, "UnscaledBits") == -1, "%s: an UnscaledBits tag", what);
        CHECK(FreeImage_MustTonemap(dib, path) == FITM_NONE, "%s: verdict %d", what, FreeImage_MustTonemap(dib, path));
        if (odd && ncomps == 3 && prec > 8) {
            /* saved again (lossless) it reloads as it was, a 16-bit file */
            char again[512];
            FIBITMAP *reloaded;
            scratch(again, sizeof(again), "fi_j2k_precision-again.j2k");
            CHECK(FreeImage_Save(FIF_J2K, dib, again, 1), "%s: not saved", what);
            reloaded = FreeImage_Load(FIF_J2K, again, 0);
            CHECK(reloaded && same_samples(reloaded, ncomps, prec, 1) && tag_byte(reloaded, "SignificantBits") == -1, "%s: saved and reloaded, the samples changed", what);
            FreeImage_Unload(reloaded);
            remove(again);
        }
        FreeImage_Unload(dib);
    }

    dib = FreeImage_Load(fif, path, raw_flag);
    CHECK(dib != NULL, "%s, unscaled: not loaded", what);
    if (dib) {
        CHECK(same_samples(dib, ncomps, prec, 0), "%s, unscaled: the samples are not the file's", what);
        CHECK(tag_byte(dib, "UnscaledBits") == (odd ? prec : -1), "%s, unscaled: UnscaledBits %d", what, tag_byte(dib, "UnscaledBits"));
        CHECK(tag_byte(dib, "SignificantBits") == -1, "%s, unscaled: a SignificantBits tag", what);
        CHECK(FreeImage_MustTonemap(dib, NULL) == (odd ? FITM_UNSCALED : FITM_NONE), "%s, unscaled: verdict %d", what, FreeImage_MustTonemap(dib, NULL));
        FreeImage_Unload(dib);
    }

    /* header-only loads carry the tag a pixel load would */
    dib = FreeImage_Load(fif, path, FIF_LOAD_NOPIXELS);
    CHECK(dib && tag_byte(dib, "SignificantBits") == (odd ? prec : -1) && tag_byte(dib, "UnscaledBits") == -1, "%s, header only: the tags", what);
    FreeImage_Unload(dib);
    dib = FreeImage_Load(fif, path, FIF_LOAD_NOPIXELS | raw_flag);
    CHECK(dib && tag_byte(dib, "UnscaledBits") == (odd ? prec : -1) && tag_byte(dib, "SignificantBits") == -1, "%s, header only, unscaled: the tags", what);
    if (dib) CHECK(FreeImage_MustTonemap(dib, NULL) == (odd ? FITM_UNSCALED : FITM_NONE), "%s, header only, unscaled: verdict", what);
    FreeImage_Unload(dib);

    remove(path);
}

int main(void) {
    static const int precisions[] = { 1, 4, 7, 8, 9, 12, 15, 16 };
    static const int components[] = { 1, 3, 4 };
    unsigned p, c;
    FreeImage_Initialise(FALSE);
    printf("JPEG 2000 precision\n");
    for (p = 0; p < sizeof(precisions) / sizeof(precisions[0]); p++) {
        for (c = 0; c < sizeof(components) / sizeof(components[0]); c++) {
            test_file(FIF_J2K, components[c], precisions[p], 0);
            test_file(FIF_JP2, components[c], precisions[p], 0);
        }
    }
    test_file(FIF_J2K, 1, 12, 1);
    test_file(FIF_JP2, 3, 10, 1);
    test_file(FIF_J2K, 1, 5, 1);
    printf("%d checks, %d failures\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
