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

    printf("%d checks, %d failures\n", checks, failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
