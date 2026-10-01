/* The ICC profile of a JP2 colr box (method 2) reaches the bitmap, header-only loads too; an enumerated
   colour space (sRGB, CIELab) and a J2K codestream attach none. The files are encoded with OpenJPEG itself,
   since FreeImage writes no profile into JPEG 2000. Prints a report, exits non-zero on failure; scratch
   files in $J2K_TEST_TMP, or the current directory. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"
#define OPJ_STATIC
#include "../../Source/LibOpenJPEG/openjpeg.h"
#include "../../Source/LibLCMS2/include/lcms2.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, ...) do { checks++; if (!(cond)) { printf("  FAIL "); printf(__VA_ARGS__); printf("\n"); failures++; } } while (0)

#define W 29
#define H 17

static const char *scratch(char *path, size_t size, const char *name) {
    const char *dir = getenv("J2K_TEST_TMP");
    snprintf(path, size, "%s/%s", (dir && *dir) ? dir : ".", name);
    return path;
}

/* ncomps components of prec bits; with a profile, the colr box holds it */
static int encode(const char *path, OPJ_CODEC_FORMAT format, int ncomps, int prec, const void *profile, DWORD size) {
    opj_cparameters_t params;
    opj_image_cmptparm_t cmpt[3];
    opj_image_t *image;
    opj_codec_t *codec;
    opj_stream_t *stream;
    int c, ok;
    unsigned i;
    opj_set_default_encoder_parameters(&params);
    params.tcp_numlayers = 1;
    params.tcp_rates[0] = 0;
    params.cp_disto_alloc = 1;
    params.numresolution = 2;
    memset(cmpt, 0, sizeof(cmpt));
    for (c = 0; c < ncomps; c++) {
        cmpt[c].dx = cmpt[c].dy = 1;
        cmpt[c].w = W;
        cmpt[c].h = H;
        cmpt[c].prec = prec;
    }
    image = opj_image_create(ncomps, cmpt, (ncomps == 3) ? OPJ_CLRSPC_SRGB : OPJ_CLRSPC_GRAY);
    if (!image) return 0;
    image->x1 = W;
    image->y1 = H;
    for (c = 0; c < ncomps; c++) {
        for (i = 0; i < W * H; i++) image->comps[c].data[i] = (OPJ_INT32)((i * 37u + (unsigned)c * 101u) % (1u << prec));
    }
    if (profile) {
        /* OpenJPEG frees it with free() */
        image->icc_profile_buf = (OPJ_BYTE *)malloc(size);
        if (!image->icc_profile_buf) { opj_image_destroy(image); return 0; }
        memcpy(image->icc_profile_buf, profile, size);
        image->icc_profile_len = size;
    }
    codec = opj_create_compress(format);
    ok = codec && opj_setup_encoder(codec, &params, image);
    stream = ok ? opj_stream_create_default_file_stream(path, OPJ_FALSE) : NULL;
    ok = ok && stream && opj_start_compress(codec, image, stream) && opj_encode(codec, stream) && opj_end_compress(codec, stream);
    if (stream) opj_stream_destroy(stream);
    if (codec) opj_destroy_codec(codec);
    opj_image_destroy(image);
    return ok;
}

/* the enumerated colour space of the first colr box, rewritten */
static int set_enumcs(const char *path, unsigned enumcs) {
    FILE *f = fopen(path, "r+b");
    BYTE buf[4096];
    size_t n, i;
    int done = 0;
    if (!f) return 0;
    n = fread(buf, 1, sizeof(buf), f);
    for (i = 0; i + 11 <= n && !done; i++) {
        /* 'colr', METH 1, PREC, APPROX, EnumCS */
        if (!memcmp(buf + i, "colr", 4) && buf[i + 4] == 1) {
            buf[i + 7] = (BYTE)(enumcs >> 24); buf[i + 8] = (BYTE)(enumcs >> 16);
            buf[i + 9] = (BYTE)(enumcs >> 8); buf[i + 10] = (BYTE)enumcs;
            done = fseek(f, 0, SEEK_SET) == 0 && fwrite(buf, 1, n, f) == n;
        }
    }
    fclose(f);
    return done;
}

static int same_profile(FIBITMAP *dib, const void *profile, DWORD size) {
    FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
    if (!profile) return icc->data == NULL && icc->size == 0;
    return icc->data && icc->size == size && !memcmp(icc->data, profile, size);
}

/* the full load and the header-only load carry the profile, or none */
static void check(const char *what, FREE_IMAGE_FORMAT fif, const char *path, const void *profile, DWORD size, int ncomps, int prec) {
    FIBITMAP *dib = FreeImage_Load(fif, path, 0), *head = FreeImage_Load(fif, path, FIF_LOAD_NOPIXELS);
    CHECK(dib && head, "%s: not loaded", what);
    if (dib) {
        const unsigned bpp = (prec <= 8 ? 8 : 16) * ncomps;
        CHECK(FreeImage_GetBPP(dib) == bpp, "%s: %u bits per pixel, want %u", what, FreeImage_GetBPP(dib), bpp);
        CHECK(same_profile(dib, profile, size), "%s: a %u-byte profile, want %u", what, (unsigned)FreeImage_GetICCProfile(dib)->size, profile ? (unsigned)size : 0);
    }
    if (head) {
        CHECK(!FreeImage_HasPixels(head) && same_profile(head, profile, size), "%s, header only: a %u-byte profile, want %u",
            what, (unsigned)FreeImage_GetICCProfile(head)->size, profile ? (unsigned)size : 0);
    }
    FreeImage_Unload(dib);
    FreeImage_Unload(head);
}

/* the METH byte of the first colr box, -1 without one */
static int colr_method(const char *path) {
    FILE *f = fopen(path, "rb");
    BYTE buf[4096];
    size_t n, i;
    int meth = -1;
    if (!f) return -1;
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    for (i = 0; i + 5 <= n; i++) {
        if (!memcmp(buf + i, "colr", 4)) { meth = buf[i + 4]; break; }
    }
    return meth;
}

static BYTE *read_all(const char *path, DWORD *size) {
    FILE *f = fopen(path, "rb");
    BYTE *data = NULL;
    long n;
    *size = 0;
    if (!f) return NULL;
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    if (n > 0 && (data = (BYTE *)malloc(n)) != NULL && fread(data, 1, n, f) == (size_t)n) *size = (DWORD)n;
    fclose(f);
    return data;
}

/* saved, reloaded: a restricted profile (matrix/TRC or grey TRC, PCS XYZ, the image's colour) in colr method 2; others left out */
static void written(const char *what, FREE_IMAGE_FORMAT fif, FIBITMAP *dib, const void *profile, DWORD size, int kept) {
    char path[512];
    FIBITMAP *back;
    scratch(path, sizeof(path), (fif == FIF_JP2) ? "fi_j2k_written.jp2" : "fi_j2k_written.j2k");
    if (profile) FreeImage_CreateICCProfile(dib, (void *)profile, (long)size);
    else FreeImage_DestroyICCProfile(dib);
    CHECK(FreeImage_Save(fif, dib, path, 1), "%s: not saved", what);
    back = FreeImage_Load(fif, path, 0);
    CHECK(back && FreeImage_GetBPP(back) == FreeImage_GetBPP(dib) && FreeImage_GetImageType(back) == FreeImage_GetImageType(dib), "%s: the pixel format changed", what);
    if (back) CHECK(same_profile(back, kept ? profile : NULL, size), "%s: a %u-byte profile came back", what, (unsigned)FreeImage_GetICCProfile(back)->size);
    if (fif == FIF_JP2) CHECK(colr_method(path) == (kept ? 2 : 1), "%s: colr method %d", what, colr_method(path));
    FreeImage_Unload(back);
    remove(path);
}

/* an RGB display profile of LUTs only, into PCS XYZ: neither matrix nor TRC tags */
static BYTE *lut_xyz_profile(DWORD *size) {
    cmsHPROFILE h = cmsCreateProfilePlaceholder(NULL);
    cmsPipeline *a2b = cmsPipelineAlloc(NULL, 3, 3);
    cmsUInt32Number n = 0;
    BYTE *out = NULL;
    *size = 0;
    if (!h || !a2b) return NULL;
    cmsSetDeviceClass(h, cmsSigDisplayClass);
    cmsSetColorSpace(h, cmsSigRgbData);
    cmsSetPCS(h, cmsSigXYZData);
    cmsPipelineInsertStage(a2b, cmsAT_END, cmsStageAllocCLut16bit(NULL, 2, 3, 3, NULL));
    cmsWriteTag(h, cmsSigAToB0Tag, a2b);
    cmsWriteTag(h, cmsSigMediaWhitePointTag, cmsD50_XYZ());
    if (cmsSaveProfileToMem(h, NULL, &n) && (out = (BYTE *)malloc(n)) != NULL && cmsSaveProfileToMem(h, out, &n)) *size = n;
    cmsPipelineFree(a2b);
    cmsCloseProfile(h);
    return out;
}

static void test_written(void) {
    DWORD adobe_size = 0, grey_size = 0, p3_size = 0, lut_size = 0;
    const void *adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
    const void *grey = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_GRAY, &grey_size);
    const void *p3 = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_DISPLAY_P3, &p3_size);
    BYTE *lut = read_all("../ICC/data/test3.icc", &lut_size);
    FIBITMAP *rgb = FreeImage_Allocate(W, H, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
    FIBITMAP *rgba, *rgb16, *grey8, *grey16;
    unsigned i;
    for (i = 0; i < FreeImage_GetPitch(rgb) * H; i++) FreeImage_GetBits(rgb)[i] = (BYTE)(i * 11 + 5);
    rgba = FreeImage_ConvertTo32Bits(rgb);
    rgb16 = FreeImage_ConvertToRGB16(rgb);
    grey8 = FreeImage_ConvertToGreyscale(rgb);
    grey16 = FreeImage_ConvertToUINT16(grey8);

    written("24-bit Adobe RGB JP2", FIF_JP2, rgb, adobe, adobe_size, 1);
    written("32-bit Display P3 JP2", FIF_JP2, rgba, p3, p3_size, 1);
    written("RGB16 Adobe RGB JP2", FIF_JP2, rgb16, adobe, adobe_size, 1);
    written("8-bit grey JP2", FIF_JP2, grey8, grey, grey_size, 1);
    written("UINT16 grey JP2", FIF_JP2, grey16, grey, grey_size, 1);
    written("an RGB profile on a grey JP2", FIF_JP2, grey8, adobe, adobe_size, 0);
    written("a grey profile on an RGB JP2", FIF_JP2, rgb, grey, grey_size, 0);
    if (lut) written("a LUT-based RGB profile into PCS Lab (test3.icc)", FIF_JP2, rgb, lut, lut_size, 0);
    else CHECK(0, "../ICC/data/test3.icc: not read");
    {
        DWORD xyz_size = 0;
        BYTE *xyz = lut_xyz_profile(&xyz_size);
        CHECK(xyz && xyz_size == ((DWORD)xyz[0] << 24 | (DWORD)xyz[1] << 16 | (DWORD)xyz[2] << 8 | xyz[3]), "no LUT profile into PCS XYZ made");
        if (xyz) written("a LUT-based RGB profile into PCS XYZ", FIF_JP2, rgb, xyz, xyz_size, 0);
        free(xyz);
    }
    written("an untagged JP2", FIF_JP2, rgb, NULL, 0, 0);
    written("a J2K codestream", FIF_J2K, rgb, adobe, adobe_size, 0);

    FreeImage_Unload(rgb); FreeImage_Unload(rgba); FreeImage_Unload(rgb16);
    FreeImage_Unload(grey8); FreeImage_Unload(grey16);
    free(lut);
}

int main(void) {
    DWORD adobe_size = 0, grey_size = 0;
    const void *adobe, *grey;
    char path[512];
    FreeImage_Initialise(FALSE);
    adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
    grey = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_GRAY, &grey_size);
    printf("JPEG 2000 ICC profiles\n");

    CHECK(FreeImage_FIFSupportsICCProfiles(FIF_JP2), "JP2: FIFSupportsICCProfiles is FALSE");
    CHECK(!FreeImage_FIFSupportsICCProfiles(FIF_J2K), "J2K: FIFSupportsICCProfiles is TRUE, a codestream has no colr box");

    scratch(path, sizeof(path), "fi_j2k_profile.jp2");
    CHECK(encode(path, OPJ_CODEC_JP2, 3, 8, adobe, adobe_size), "Adobe RGB JP2: not encoded");
    check("Adobe RGB JP2", FIF_JP2, path, adobe, adobe_size, 3, 8);
    CHECK(encode(path, OPJ_CODEC_JP2, 3, 12, adobe, adobe_size), "12-bit Adobe RGB JP2: not encoded");
    check("12-bit Adobe RGB JP2", FIF_JP2, path, adobe, adobe_size, 3, 12);
    CHECK(encode(path, OPJ_CODEC_JP2, 1, 8, grey, grey_size), "grey JP2: not encoded");
    check("grey JP2", FIF_JP2, path, grey, grey_size, 1, 8);
    CHECK(encode(path, OPJ_CODEC_JP2, 3, 8, NULL, 0), "sRGB JP2: not encoded");
    check("enumerated sRGB JP2", FIF_JP2, path, NULL, 0, 3, 8);
    /* CIELab: OpenJPEG keeps its parameters where a profile would be, with a length of 0 */
    CHECK(set_enumcs(path, 14), "CIELab JP2: no colr box to rewrite");
    check("enumerated CIELab JP2", FIF_JP2, path, NULL, 0, 3, 8);
    remove(path);

    scratch(path, sizeof(path), "fi_j2k_profile.j2k");
    CHECK(encode(path, OPJ_CODEC_J2K, 3, 8, adobe, adobe_size), "J2K: not encoded");
    check("J2K codestream", FIF_J2K, path, NULL, 0, 3, 8);
    remove(path);

    test_written();

    printf("%d checks, %d failures\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
