/* built-in profiles, profile information, known colorimetry and profiles saved into files */
/* ./profiles --record reprints the built-in profiles' checksums */
#include "common.h"
#include "../../Source/LibTIFF4/tiffio.h"

static const char *scratch(const char *name) {
    static char buf[512];
    const char *dir = getenv("ICC_TEST_TMP");
    snprintf(buf, sizeof(buf), "%s/%s", (dir && *dir) ? dir : ".", name);
    return buf;
}

static const char *NAMES[7] = {
    "sRGB (FreeImage)", "Linear sRGB (FreeImage)", "Gray, sRGB tone curve (FreeImage)", "Linear gray (FreeImage)",
    "Adobe RGB (1998) compatible (FreeImage)", "Display P3 (FreeImage)", "ProPhoto RGB (FreeImage)"
};

/* the built-in profiles' bytes; a Little CMS update may change them, then check and --record */
static const DWORD CRC[7] = { 0x95cd8f4b, 0x94916850, 0xd8cf48f8, 0x01d892f4, 0x264a7af4, 0x9f7c8ada, 0x7b41089a };

static void builtins(int record) {
    int i;
    for (i = 0; i < 7; i++) {
        DWORD size = 0, size2 = 0, crc;
        const BYTE *p = (const BYTE *)FreeImage_GetBuiltInICCProfile(i, &size);
        const BYTE *p2 = (const BYTE *)FreeImage_GetBuiltInICCProfile(i, &size2);
        char desc[128];
        cmsHPROFILE h;
        if (!p) { fail("built-in %d missing", i); continue; }
        crc = FreeImage_ZLibCRC32(0, (BYTE *)p, size);
        if (record) { printf("    0x%08lx,\n", (unsigned long)crc); continue; }
        CHECK(p == p2 && size == size2, "built-in %d: not the same block on every call", i);
        CHECK(crc == CRC[i], "built-in %d: checksum %08lx, expected %08lx", i, (unsigned long)crc, (unsigned long)CRC[i]);
        CHECK(FreeImage_GetICCProfileDescription(p, size, desc, sizeof(desc)) == strlen(NAMES[i]) + 1 && !strcmp(desc, NAMES[i]), "built-in %d: description '%s'", i, desc);
        CHECK(FreeImage_GetICCProfileColorSpace(p, size) == ((i == 2 || i == 3) ? FICMS_COLORSPACE_GRAY : FICMS_COLORSPACE_RGB), "built-in %d: color space", i);
        /* header: size, version 4, display class, fixed date, no platform */
        CHECK(((DWORD)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]) == size, "built-in %d: header size", i);
        CHECK(p[8] == 4 && !memcmp(p + 12, "mntr", 4) && !memcmp(p + 20, "XYZ ", 4), "built-in %d: header class/version/PCS", i);
        CHECK(p[24] == 0x07 && p[25] == 0xEA && p[27] == 1 && p[29] == 1 && !memcmp(p + 40, "\0\0\0\0", 4), "built-in %d: date or platform", i);
        h = cmsOpenProfileFromMem(p, size);
        CHECK(h && cmsIsMatrixShaper(h), "built-in %d: not a matrix/TRC profile", i);
        if (h) cmsCloseProfile(h);
    }
    CHECK(!FreeImage_GetBuiltInICCProfile(-1, NULL) && !FreeImage_GetBuiltInICCProfile(7, NULL), "out of range built-in");
    {
        DWORD size = 123;
        CHECK(!FreeImage_GetBuiltInICCProfile(99, &size) && size == 0, "out of range built-in leaves a size");
    }
}

/* the built-in sRGB is Little CMS's own sRGB */
static void srgb_is_srgb(void) {
    cmsHPROFILE a = cmsCreate_sRGBProfile();
    Bytes s = builtin(FICMS_PROFILE_SRGB);
    cmsHPROFILE b = cmsOpenProfileFromMem(s.data, s.size);
    cmsHTRANSFORM t = cmsCreateTransform(a, TYPE_RGB_16, b, TYPE_RGB_16, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE);
    int i, worst = 0;
    for (i = 0; i < 4096; i++) {
        WORD in[3], out[3];
        int c;
        rng_state += i;
        in[0] = rnd() & 0xFFFF; in[1] = rnd() & 0xFFFF; in[2] = (i * 16) & 0xFFFF;
        cmsDoTransform(t, in, out, 1);
        for (c = 0; c < 3; c++) if (abs(out[c] - in[c]) > worst) worst = abs(out[c] - in[c]);
    }
    /* s15Fixed16 colorants and curve parameters: a quarter of an 8-bit step at most */
    CHECK(worst <= 64, "the built-in sRGB is not Little CMS's sRGB (16-bit difference %d)", worst);
    cmsDeleteTransform(t); cmsCloseProfile(a); cmsCloseProfile(b);
}

/* conversions against colorimetry computed without Little CMS */
static void colorimetry(void) {
    static const BYTE IN[6][3] = { {255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 0}, {128, 64, 32}, {10, 200, 150} };
    static const BYTE ADOBE[6][3] = { {219, 0, 0}, {144, 255, 60}, {0, 0, 250}, {255, 255, 60}, {114, 66, 39}, {113, 199, 151} };
    static const BYTE P3[6][3] = { {234, 51, 35}, {117, 251, 76}, {0, 0, 245}, {255, 255, 84}, {120, 67, 39}, {91, 197, 153} };
    static const BYTE ROMM[6][3] = { {179, 70, 26}, {138, 237, 78}, {86, 35, 235}, {234, 251, 84}, {83, 57, 31}, {114, 176, 138} };
    const BYTE (*EXP[3])[3] = { ADOBE, P3, ROMM };
    const int IDS[3] = { FICMS_PROFILE_ADOBE_RGB, FICMS_PROFILE_DISPLAY_P3, FICMS_PROFILE_PROPHOTO_RGB };
    const char *N[3] = { "Adobe RGB", "Display P3", "ProPhoto" };
    FIBITMAP *src = FreeImage_Allocate(6, 1, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK), *dst;
    BYTE *p = FreeImage_GetScanLine(src, 0);
    int i, j, c;
    for (i = 0; i < 6; i++) { p[3 * i + FI_RGBA_RED] = IN[i][0]; p[3 * i + FI_RGBA_GREEN] = IN[i][1]; p[3 * i + FI_RGBA_BLUE] = IN[i][2]; }
    for (j = 0; j < 3; j++) {
        Bytes b = builtin(IDS[j]);
        dst = FreeImage_ConvertToICCProfile(src, b.data, b.size, FICMS_INTENT_RELATIVE_COLORIMETRIC);
        if (!dst) { fail("sRGB -> %s: NULL", N[j]); continue; }
        p = FreeImage_GetScanLine(dst, 0);
        for (i = 0; i < 6; i++) {
            const BYTE got[3] = { p[3 * i + FI_RGBA_RED], p[3 * i + FI_RGBA_GREEN], p[3 * i + FI_RGBA_BLUE] };
            for (c = 0; c < 3; c++)
                CHECK(abs(got[c] - EXP[j][i][c]) <= 1, "sRGB (%d,%d,%d) -> %s is (%d,%d,%d), expected (%d,%d,%d)", IN[i][0], IN[i][1], IN[i][2], N[j],
                    got[0], got[1], got[2], EXP[j][i][0], EXP[j][i][1], EXP[j][i][2]);
        }
        /* and back, in 16 bits: 8-bit wide-gamut values are too coarse near black */
        {
            FIBITMAP *s16 = FreeImage_ConvertToRGB16(src), *wide = FreeImage_ConvertToICCProfile(s16, b.data, b.size, FICMS_INTENT_RELATIVE_COLORIMETRIC);
            FIBITMAP *back = FreeImage_ConvertToICCProfile(wide, NULL, 0, FICMS_INTENT_RELATIVE_COLORIMETRIC);
            WORD *q = (WORD *)FreeImage_GetScanLine(back, 0), *o = (WORD *)FreeImage_GetScanLine(s16, 0);
            for (i = 0; i < 18; i++)
                CHECK(abs(q[i] - o[i]) <= 64, "%s -> sRGB in 16 bits does not return sample %d (%d, expected %d)", N[j], i, q[i], o[i]);
            FreeImage_Unload(back); FreeImage_Unload(wide); FreeImage_Unload(s16);
        }
        FreeImage_Unload(dst);
    }
    FreeImage_Unload(src);

    /* grey: sRGB tone curve against linear */
    {
        FIBITMAP *g = FreeImage_Allocate(1, 1, 8, 0, 0, 0), *lin;
        RGBQUAD *pal = FreeImage_GetPalette(g);
        Bytes b = builtin(FICMS_PROFILE_LINEAR_GRAY);
        for (i = 0; i < 256; i++) pal[i].rgbRed = pal[i].rgbGreen = pal[i].rgbBlue = (BYTE)i;
        *FreeImage_GetScanLine(g, 0) = 128;
        lin = FreeImage_ConvertToICCProfile(g, b.data, b.size, 0);
        CHECK(lin && *FreeImage_GetScanLine(lin, 0) == 55, "grey 128 -> linear is %d, expected 55", lin ? *FreeImage_GetScanLine(lin, 0) : -1);
        if (lin) FreeImage_Unload(lin);
        FreeImage_Unload(g);
    }

    /* floating point: linear by default, values above 1 kept */
    {
        FIBITMAP *f = FreeImage_AllocateT(FIT_RGBF, 2, 1, 96, 0, 0, 0), *r;
        FIRGBF *px = (FIRGBF *)FreeImage_GetScanLine(f, 0);
        Bytes s = builtin(FICMS_PROFILE_SRGB);
        cmsToneCurve *lin = cmsBuildGamma(NULL, 1.0), *c3[3];
        cmsCIExyY d65 = { 0.3127, 0.3290, 1.0 };
        cmsCIExyYTRIPLE p3 = { {0.680, 0.320, 1.0}, {0.265, 0.690, 1.0}, {0.150, 0.060, 1.0} };
        cmsHPROFILE hp;
        Bytes lp3;
        c3[0] = c3[1] = c3[2] = lin;
        hp = cmsCreateRGBProfile(&d65, &p3, c3);
        lp3 = save_profile(hp);
        cmsCloseProfile(hp);
        cmsFreeToneCurve(lin);
        px[0].red = px[0].green = px[0].blue = 0.5f;
        px[1].red = 2.0f; px[1].green = 1.0f; px[1].blue = 0.5f;
        r = FreeImage_ConvertToICCProfile(f, s.data, s.size, FICMS_INTENT_RELATIVE_COLORIMETRIC);
        px = (FIRGBF *)FreeImage_GetScanLine(r, 0);
        CHECK(r && fabs(px[0].red - 0.73536f) < 1e-3 && fabs(px[0].blue - 0.73536f) < 1e-3, "linear 0.5 -> sRGB is %f, expected 0.7354", r ? px[0].red : -1);
        if (r) FreeImage_Unload(r);
        r = FreeImage_ConvertToICCProfile(f, lp3.data, lp3.size, FICMS_INTENT_RELATIVE_COLORIMETRIC);
        px = (FIRGBF *)FreeImage_GetScanLine(r, 0);
        /* linear sRGB (2, 1, 0.5) in linear Display P3 */
        CHECK(r && fabs(px[1].red - 1.82246f) < 1e-3 && fabs(px[1].green - 1.03319f) < 1e-3 && fabs(px[1].blue - 0.56182f) < 1e-3,
            "linear sRGB (2, 1, 0.5) -> linear P3 is (%f %f %f)", r ? px[1].red : -1, r ? px[1].green : -1, r ? px[1].blue : -1);
        if (r) FreeImage_Unload(r);
        free(lp3.data);
        FreeImage_Unload(f);
    }
}

static int valid_utf8_prefix(const char *s, const char *full) {
    size_t n = strlen(s);
    if (strncmp(s, full, n)) return 0;
    return ((unsigned char)full[n] & 0xC0) != 0x80;
}

static void descriptions(const Bytes *press) {
    const char *text = "\xCE\xA9mega \xE2\x9C\x93 \xD0\xBF\xD1\x80\xD0\xBE\xD0\xB1\xD0\xB0";  /* Ωmega ✓ проба */
    cmsHPROFILE h = cmsCreate_sRGBProfile();
    cmsMLU *mlu = cmsMLUalloc(NULL, 1);
    Bytes b, t5;
    char buf[64];
    unsigned n, size;
    cmsMLUsetUTF8(mlu, "en", "US", text);
    cmsWriteTag(h, cmsSigProfileDescriptionTag, mlu);
    cmsMLUfree(mlu);
    b = save_profile(h);
    cmsCloseProfile(h);

    n = FreeImage_GetICCProfileDescription(b.data, b.size, NULL, 0);
    CHECK(n == strlen(text) + 1, "needed size %u, expected %u", n, (unsigned)strlen(text) + 1);
    CHECK(FreeImage_GetICCProfileDescription(b.data, b.size, buf, sizeof(buf)) == n && !strcmp(buf, text), "UTF-8 description '%s'", buf);
    for (size = 1; size <= n; size++) {
        memset(buf, 'x', sizeof(buf));
        CHECK(FreeImage_GetICCProfileDescription(b.data, b.size, buf, size) == n, "size %u: return value", size);
        CHECK(memchr(buf, 0, size) != NULL, "size %u: not terminated", size);
        CHECK(valid_utf8_prefix(buf, text), "size %u: '%s' is not a whole-character prefix", size, buf);
        CHECK(buf[size] == 'x', "size %u: wrote past the buffer", size);
    }
    memset(buf, 'x', sizeof(buf));
    CHECK(FreeImage_GetICCProfileDescription(b.data, b.size, buf, 0) == n && buf[0] == 'x', "buffer size 0 wrote");
    CHECK(FreeImage_GetICCProfileDescription(b.data, 100, buf, sizeof(buf)) == 0 && buf[0] == 0, "a cut profile has a description");
    CHECK(FreeImage_GetICCProfileDescription(NULL, 0, buf, sizeof(buf)) == 0 && buf[0] == 0, "NULL profile has a description");
    CHECK(FreeImage_GetICCProfileDescription(press->data, press->size, buf, sizeof(buf)) && !strcmp(buf, "Test press, CMYK"), "v2 description '%s'", buf);
    t5 = read_file("data/test5.icc");
    CHECK(t5.data && FreeImage_GetICCProfileDescription(t5.data, t5.size, buf, sizeof(buf)) > 1, "test5.icc has no description");
    free(t5.data);
    free(b.data);

    CHECK(FreeImage_GetICCProfileColorSpace(press->data, press->size) == FICMS_COLORSPACE_CMYK, "press: not CMYK");
    {
        cmsHPROFILE lab = cmsCreateLab4Profile(NULL);
        Bytes l = save_profile(lab);
        cmsCloseProfile(lab);
        CHECK(FreeImage_GetICCProfileColorSpace(l.data, l.size) == FICMS_COLORSPACE_LAB, "Lab profile color space");
        free(l.data);
    }
    CHECK(!FreeImage_GetICCProfileColorSpace(NULL, 0) && !FreeImage_GetICCProfileColorSpace(press->data, 127), "invalid profile has a color space");
}

/* the profile and pixels a saved file brings back */
static void round_trip(const char *what, FREE_IMAGE_FORMAT fif, FIBITMAP *dib, int save_flags, int load_flags, int exact) {
    const char *path = scratch("icc_round_trip.bin");
    FIBITMAP *back;
    FIICCPROFILE *a = FreeImage_GetICCProfile(dib), *b;
    if (!FreeImage_Save(fif, dib, path, save_flags)) { fail("%s: not saved", what); return; }
    back = FreeImage_Load(fif, path, load_flags);
    remove(path);
    if (!back) { fail("%s: not loaded", what); return; }
    b = FreeImage_GetICCProfile(back);
    CHECK(b->size == a->size && (!a->size || (b->data && !memcmp(a->data, b->data, a->size))), "%s: the profile did not come back", what);
    CHECK((a->flags & FIICC_COLOR_IS_CMYK) == (b->flags & FIICC_COLOR_IS_CMYK), "%s: the CMYK flag did not come back", what);
    CHECK(FreeImage_GetBPP(back) == FreeImage_GetBPP(dib) && FreeImage_GetImageType(back) == FreeImage_GetImageType(dib), "%s: pixel format", what);
    if (exact) {
        unsigned y, diff = 0;
        for (y = 0; y < FreeImage_GetHeight(dib); y++) diff += memcmp(FreeImage_GetScanLine(dib, y), FreeImage_GetScanLine(back, y), FreeImage_GetLine(dib)) != 0;
        CHECK(diff == 0, "%s: %u rows differ", what, diff);
    }
    FreeImage_Unload(back);
}

static void files(const Bytes *press) {
    FIBITMAP *rgb = FreeImage_Allocate(37, 23, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK), *cmyk, *adobe, *cmyk16, *grey, *g18;
    FIBITMAP *rgb16;
    Bytes a = builtin(FICMS_PROFILE_ADOBE_RGB), gp = make_gray_profile(1.8);
    unsigned y, x;
    for (y = 0; y < 23; y++) { BYTE *p = FreeImage_GetScanLine(rgb, y); for (x = 0; x < 37 * 3; x++) p[x] = rnd() & 255; }

    cmyk = FreeImage_ConvertToCMYK(rgb, press->data, press->size, 0);
    round_trip("CMYK TIFF", FIF_TIFF, cmyk, TIFF_DEFAULT, TIFF_CMYK, 1);
    round_trip("CMYK JPEG", FIF_JPEG, cmyk, JPEG_QUALITYSUPERB, JPEG_CMYK, 0);
    round_trip("CMYK PSD", FIF_PSD, cmyk, PSD_DEFAULT, PSD_CMYK, 1);
    rgb16 = FreeImage_ConvertToRGB16(rgb);
    cmyk16 = FreeImage_ConvertToCMYK(rgb16, press->data, press->size, 0);
    round_trip("16-bit CMYK TIFF", FIF_TIFF, cmyk16, TIFF_DEFAULT, TIFF_CMYK, 1);
    round_trip("16-bit CMYK PSD", FIF_PSD, cmyk16, PSD_DEFAULT, PSD_CMYK, 1);
    {
        /* without a profile, the CMYK flag alone comes back */
        FIBITMAP *bare = FreeImage_Clone(cmyk), *bare16 = FreeImage_Clone(cmyk16);
        FreeImage_DestroyICCProfile(bare);
        FreeImage_DestroyICCProfile(bare16);
        round_trip("untagged CMYK PSD", FIF_PSD, bare, PSD_DEFAULT, PSD_CMYK, 1);
        round_trip("untagged 16-bit CMYK PSD", FIF_PSD, bare16, PSD_DEFAULT, PSD_CMYK, 1);
        FreeImage_Unload(bare); FreeImage_Unload(bare16);
    }
    adobe = FreeImage_ConvertToICCProfile(rgb, a.data, a.size, 0);
    round_trip("Adobe RGB PNG", FIF_PNG, adobe, PNG_DEFAULT, PNG_DEFAULT, 1);
    round_trip("Adobe RGB TIFF", FIF_TIFF, adobe, TIFF_DEFAULT, TIFF_DEFAULT, 1);
    round_trip("Adobe RGB JPEG", FIF_JPEG, adobe, JPEG_QUALITYSUPERB, JPEG_DEFAULT, 0);
    round_trip("Adobe RGB WebP", FIF_WEBP, adobe, WEBP_LOSSLESS, WEBP_DEFAULT, 1);
    grey = FreeImage_ConvertToGreyscale(rgb);
    g18 = FreeImage_ConvertToICCProfile(grey, gp.data, gp.size, 0);
    round_trip("grey PNG", FIF_PNG, g18, PNG_DEFAULT, PNG_DEFAULT, 1);

    /* JNG: an iCCP chunk next to JPEG data written without markers */
    round_trip("Adobe RGB JNG", FIF_JNG, adobe, 0, 0, 0);
    round_trip("grey JNG", FIF_JNG, g18, 0, 0, 0);
    {
        FIBITMAP *adobe32 = FreeImage_ConvertTo32Bits(adobe), *back;
        const char *path = scratch("icc_jng.jng");
        for (y = 0; y < 23; y++) { BYTE *p = FreeImage_GetScanLine(adobe32, y); for (x = 0; x < 37; x++) p[4 * x + FI_RGBA_ALPHA] = (BYTE)(x * 7); }
        FreeImage_CreateICCProfile(adobe32, a.data, a.size);
        round_trip("Adobe RGB JNG with alpha", FIF_JNG, adobe32, 0, 0, 0);
        /* header-only loads carry it */
        FreeImage_Save(FIF_JNG, adobe32, path, 0);
        back = FreeImage_Load(FIF_JNG, path, FIF_LOAD_NOPIXELS);
        CHECK(back && FreeImage_GetICCProfile(back)->size == a.size && !memcmp(FreeImage_GetICCProfile(back)->data, a.data, a.size), "JNG header-only: the profile did not come back");
        if (back) FreeImage_Unload(back);
        /* a grey JNG cannot hold an RGB profile: left out, the save succeeds */
        FreeImage_CreateICCProfile(grey, a.data, a.size);
        CHECK(FreeImage_Save(FIF_JNG, grey, path, 0), "grey JNG with an RGB profile: not saved");
        {
            Bytes file = read_file(path);
            size_t pos = 8;
            int iccp = 0;
            while (pos + 12 <= file.size) {
                const DWORD length = ((DWORD)file.data[pos] << 24) | ((DWORD)file.data[pos + 1] << 16) | ((DWORD)file.data[pos + 2] << 8) | file.data[pos + 3];
                if (!memcmp(file.data + pos + 4, "iCCP", 4)) iccp = 1;
                pos += 12 + length;
            }
            CHECK(file.size && !iccp, "grey JNG with an RGB profile: an iCCP chunk was written");
            free(file.data);
        }
        back = FreeImage_Load(FIF_JNG, path, 0);
        CHECK(back && !FreeImage_GetICCProfile(back)->data, "grey JNG with an RGB profile: the profile came back");
        if (back) FreeImage_Unload(back);
        FreeImage_DestroyICCProfile(grey);
        remove(path);
        FreeImage_Unload(adobe32);
    }

    /* a CMYK JPEG loaded as CMYK, then shown */
    {
        const char *path = scratch("icc_cmyk.jpg");
        FIBITMAP *loaded, *shown;
        FreeImage_Save(FIF_JPEG, cmyk, path, JPEG_QUALITYSUPERB);
        loaded = FreeImage_Load(FIF_JPEG, path, JPEG_CMYK);
        remove(path);
        shown = loaded ? FreeImage_ConvertCMYKToRGB(loaded, NULL, 0, FICMS_INTENT_PERCEPTUAL) : NULL;
        CHECK(shown && FreeImage_GetBPP(shown) == 24 && !FreeImage_GetICCProfile(shown)->data, "CMYK JPEG to screen");
        if (shown) {
            /* close to the picture it came from: press gamut, JPEG loss */
            double sum = 0;
            for (y = 0; y < 23; y++) {
                BYTE *p = FreeImage_GetScanLine(shown, y), *q = FreeImage_GetScanLine(rgb, y);
                for (x = 0; x < 37 * 3; x++) sum += abs(p[x] - q[x]);
            }
            CHECK(sum / (37 * 23 * 3) < 40, "CMYK JPEG to screen: mean difference %.1f", sum / (37 * 23 * 3));
            FreeImage_Unload(shown);
        }
        if (loaded) FreeImage_Unload(loaded);
    }
    FreeImage_Unload(rgb); FreeImage_Unload(cmyk); FreeImage_Unload(rgb16); FreeImage_Unload(cmyk16);
    FreeImage_Unload(adobe); FreeImage_Unload(grey); FreeImage_Unload(g18);
    free(gp.data);
}

/* ------------------------------------------------------------------ loaders: the profile describes the pixels */

static void put32(BYTE *p, DWORD v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }

static size_t png_chunk(BYTE *out, const char *type, const BYTE *data, DWORD size) {
    put32(out, size);
    memcpy(out + 4, type, 4);
    if (size) memcpy(out + 8, data, size);
    put32(out + 8 + size, FreeImage_ZLibCRC32(0, out + 4, size + 4));
    return 12 + size;
}

/* an RGB PNG with an ICC profile and a gAMA chunk */
static void png_with_profile_and_gamma(void) {
    static BYTE file[8192], scratch[4096], rows[16];
    static const BYTE SIG[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    const BYTE values[4] = { 0, 64, 128, 200 };
    Bytes lin = builtin(FICMS_PROFILE_LINEAR_SRGB);
    BYTE ihdr[13], gama[4];
    size_t n = 8;
    DWORD z;
    int i;
    FIMEMORY *mem;
    FIBITMAP *dib;
    memcpy(file, SIG, 8);
    put32(ihdr, 4); put32(ihdr + 4, 1); ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
    n += png_chunk(file + n, "IHDR", ihdr, 13);
    put32(gama, 100000);
    n += png_chunk(file + n, "gAMA", gama, 4);
    memcpy(scratch, "linear\0\0", 8);
    z = FreeImage_ZLibCompress(scratch + 8, sizeof(scratch) - 8, lin.data, lin.size);
    n += png_chunk(file + n, "iCCP", scratch, z + 8);
    rows[0] = 0;
    for (i = 0; i < 4; i++) rows[1 + 3 * i] = rows[2 + 3 * i] = rows[3 + 3 * i] = values[i];
    z = FreeImage_ZLibCompress(scratch, sizeof(scratch), rows, 13);
    n += png_chunk(file + n, "IDAT", scratch, z);
    n += png_chunk(file + n, "IEND", NULL, 0);

    mem = FreeImage_OpenMemory(file, (DWORD)n);
    dib = FreeImage_LoadFromMemory(FIF_PNG, mem, PNG_DEFAULT);
    FreeImage_CloseMemory(mem);
    if (!dib) { fail("PNG with iCCP and gAMA: not loaded"); return; }
    for (i = 0; i < 4; i++)
        CHECK(FreeImage_GetScanLine(dib, 0)[3 * i] == values[i], "PNG with iCCP and gAMA: pixel %d is %d, gamma-corrected, expected %d", i, FreeImage_GetScanLine(dib, 0)[3 * i], values[i]);
    CHECK(FreeImage_GetICCProfile(dib)->size == lin.size, "PNG with iCCP and gAMA: the profile is missing");
    /* linear 64/255 is sRGB 137 */
    CHECK(FreeImage_ApplyICCProfile(dib, NULL, 0, FICMS_INTENT_RELATIVE_COLORIMETRIC) && abs(FreeImage_GetScanLine(dib, 0)[3] - 137) <= 1,
        "PNG with iCCP and gAMA to sRGB: %d, expected 137", FreeImage_GetScanLine(dib, 0)[3]);
    FreeImage_Unload(dib);
}

static tmsize_t t_read(thandle_t h, void *b, tmsize_t n) { return (tmsize_t)fread(b, 1, (size_t)n, (FILE *)h); }
static tmsize_t t_write(thandle_t h, void *b, tmsize_t n) { return (tmsize_t)fwrite(b, 1, (size_t)n, (FILE *)h); }
static toff_t t_seek(thandle_t h, toff_t o, int w) { fseek((FILE *)h, (long)o, w); return (toff_t)ftell((FILE *)h); }
static int t_close(thandle_t h) { return fclose((FILE *)h); }
static toff_t t_size(thandle_t h) { long p = ftell((FILE *)h), s; fseek((FILE *)h, 0, SEEK_END); s = ftell((FILE *)h); fseek((FILE *)h, p, SEEK_SET); return (toff_t)s; }
static int t_map(thandle_t h, void **b, toff_t *s) { (void)h; (void)b; (void)s; return 0; }
static void t_unmap(thandle_t h, void *b, toff_t s) { (void)h; (void)b; (void)s; }

static int write_cmyk_tiff(const char *path, int bits, int tiled, const Bytes *icc) {
    static BYTE px[48 * 32 * 8];
    TIFF *t;
    int i;
    FILE *f = fopen(path, "w+b");
    if (!f) return 0;
    t = TIFFClientOpen(path, "w", (thandle_t)f, t_read, t_write, t_seek, t_close, t_size, t_map, t_unmap);
    if (!t) { fclose(f); return 0; }
    for (i = 0; i < (int)sizeof(px); i++) px[i] = (BYTE)(i * 7 + 3);
    TIFFSetField(t, TIFFTAG_IMAGEWIDTH, 48); TIFFSetField(t, TIFFTAG_IMAGELENGTH, 32);
    TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, bits); TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, 4);
    TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_SEPARATED); TIFFSetField(t, TIFFTAG_INKSET, INKSET_CMYK);
    TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(t, TIFFTAG_ICCPROFILE, (uint32_t)icc->size, icc->data);
    if (tiled) {
        TIFFSetField(t, TIFFTAG_TILEWIDTH, 16); TIFFSetField(t, TIFFTAG_TILELENGTH, 16);
        for (i = 0; i < 6; i++) TIFFWriteTile(t, px, (i % 3) * 16, (i / 3) * 16, 0, 0);
    } else {
        /* every tile is the same 16x16 block: a row repeats one of its rows three times */
        static BYTE row[48 * 8];
        const int tile_row = 16 * 4 * (bits / 8);
        TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, 8);
        for (i = 0; i < 32; i++) {
            int k;
            for (k = 0; k < 3; k++) memcpy(row + k * tile_row, px + (i % 16) * tile_row, tile_row);
            TIFFWriteScanline(t, row, i, 0);
        }
    }
    TIFFClose(t);
    return 1;
}

/* tiled CMYK TIFFs load as the striped ones do */
static void tiled_cmyk_tiff(const Bytes *press) {
    int bits, flags, tiled;
    for (bits = 8; bits <= 16; bits += 8) {
        FIBITMAP *img[2][2];
        for (tiled = 0; tiled < 2; tiled++) {
            const char *path = scratch(tiled ? "icc_tiled.tif" : "icc_strips.tif");
            if (!write_cmyk_tiff(path, bits, tiled, press)) { fail("cannot write %s", path); return; }
            for (flags = 0; flags < 2; flags++) img[tiled][flags] = FreeImage_Load(FIF_TIFF, path, flags ? TIFF_CMYK : TIFF_DEFAULT);
            remove(path);
        }
        for (flags = 0; flags < 2; flags++) {
            FIBITMAP *s = img[0][flags], *t = img[1][flags];
            unsigned y, diff = 0;
            if (!s || !t) { fail("%d-bit CMYK TIFF, flags %d: not loaded", bits, flags); continue; }
            CHECK(FreeImage_GetBPP(s) == FreeImage_GetBPP(t) && FreeImage_GetColorType(s) == FreeImage_GetColorType(t), "%d-bit CMYK TIFF, flags %d: tiled %u bpp, strips %u bpp", bits, flags, FreeImage_GetBPP(t), FreeImage_GetBPP(s));
            if (FreeImage_GetBPP(s) == FreeImage_GetBPP(t))
                for (y = 0; y < 32; y++) diff += memcmp(FreeImage_GetScanLine(s, y), FreeImage_GetScanLine(t, y), FreeImage_GetLine(s)) != 0;
            CHECK(diff == 0, "%d-bit CMYK TIFF, flags %d: %u rows of the tiled file differ", bits, flags, diff);
            CHECK(FreeImage_GetICCProfile(t)->size == (flags ? press->size : 0), "%d-bit CMYK TIFF, flags %d: the tiled file keeps a profile that does not describe it", bits, flags);
            FreeImage_Unload(s); FreeImage_Unload(t);
        }
    }
}

/* ------------------------------------------------------------------ header-only loads describe the full load */

/* what: a name; full and head: the same file loaded with and without FIF_LOAD_NOPIXELS */
static void same_as_full(const char *what, FIBITMAP *full, FIBITMAP *head) {
    const FIICCPROFILE *a, *b;
    FIBITMAP *ta, *tb;
    if (!full || !head) {
        CHECK(!full && !head, "%s: the full load %s, the header-only one %s", what, full ? "succeeds" : "fails", head ? "succeeds" : "fails");
        if (full) FreeImage_Unload(full);
        if (head) FreeImage_Unload(head);
        return;
    }
    CHECK(!FreeImage_HasPixels(head), "%s: the header-only load has pixels", what);
    CHECK(FreeImage_GetWidth(head) == FreeImage_GetWidth(full) && FreeImage_GetHeight(head) == FreeImage_GetHeight(full) &&
        FreeImage_GetBPP(head) == FreeImage_GetBPP(full) && FreeImage_GetImageType(head) == FreeImage_GetImageType(full) &&
        FreeImage_GetLine(head) == FreeImage_GetLine(full) && FreeImage_GetRedMask(head) == FreeImage_GetRedMask(full) &&
        FreeImage_GetBlueMask(head) == FreeImage_GetBlueMask(full),
        "%s: header-only %u-bit type %d, full %u-bit type %d", what, FreeImage_GetBPP(head), (int)FreeImage_GetImageType(head),
        FreeImage_GetBPP(full), (int)FreeImage_GetImageType(full));
    a = FreeImage_GetICCProfile(full);
    b = FreeImage_GetICCProfile(head);
    CHECK(a->size == b->size && a->flags == b->flags && (!a->size || !memcmp(a->data, b->data, a->size)),
        "%s: header-only profile %u bytes, flags %x; full %u bytes, flags %x", what, (unsigned)b->size, b->flags, (unsigned)a->size, a->flags);
    CHECK(FreeImage_GetDotsPerMeterX(head) == FreeImage_GetDotsPerMeterX(full) && FreeImage_GetDotsPerMeterY(head) == FreeImage_GetDotsPerMeterY(full),
        "%s: header-only resolution %u, full %u", what, FreeImage_GetDotsPerMeterX(head), FreeImage_GetDotsPerMeterX(full));
    if (FreeImage_GetPalette(full))
        CHECK(FreeImage_GetPalette(head) && FreeImage_GetColorsUsed(head) == FreeImage_GetColorsUsed(full) &&
            !memcmp(FreeImage_GetPalette(head), FreeImage_GetPalette(full), FreeImage_GetColorsUsed(full) * sizeof(RGBQUAD)), "%s: palette", what);
    ta = FreeImage_GetThumbnail(full);
    tb = FreeImage_GetThumbnail(head);
    CHECK(!ta == !tb && (!ta || (FreeImage_GetWidth(ta) == FreeImage_GetWidth(tb) && FreeImage_GetBPP(ta) == FreeImage_GetBPP(tb))),
        "%s: header-only thumbnail %s, full %s", what, tb ? "present" : "missing", ta ? "present" : "missing");
    FreeImage_Unload(full);
    FreeImage_Unload(head);
}

#define TAGGED 4

/* samples of bits each (32: float), 300 dpi, strips (layout 0), planes (1) or 16x16 tiles (2), | TAGGED: SampleFormat unsigned; samples past the colours are extra */
static int write_test_tiff(const char *path, int photometric, int bits, int spp, int flags, const Bytes *icc) {
    static BYTE px[48 * 32 * 8 * 4];
    const int layout = flags & 3, bytes = bits / 8, row = 48 * (layout == 1 ? 1 : spp) * bytes;
    const int colours = (photometric == PHOTOMETRIC_SEPARATED) ? 4 : (photometric == PHOTOMETRIC_RGB) ? 3 : 1;
    TIFF *t;
    int i, s;
    FILE *f = fopen(path, "w+b");
    if (!f) return 0;
    t = TIFFClientOpen(path, "w", (thandle_t)f, t_read, t_write, t_seek, t_close, t_size, t_map, t_unmap);
    if (!t) { fclose(f); return 0; }
    for (i = 0; i < (int)sizeof(px); i++) px[i] = (BYTE)(i * 11 + 5);
    if (bits == 32) for (i = 0; i < (int)(sizeof(px) / 4); i++) ((float *)px)[i] = (i % 97) / 96.0f;
    TIFFSetField(t, TIFFTAG_IMAGEWIDTH, 48); TIFFSetField(t, TIFFTAG_IMAGELENGTH, 32);
    TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, bits); TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, spp);
    TIFFSetField(t, TIFFTAG_PHOTOMETRIC, photometric);
    TIFFSetField(t, TIFFTAG_PLANARCONFIG, layout == 1 ? PLANARCONFIG_SEPARATE : PLANARCONFIG_CONTIG);
    TIFFSetField(t, TIFFTAG_XRESOLUTION, 300.0); TIFFSetField(t, TIFFTAG_YRESOLUTION, 300.0); TIFFSetField(t, TIFFTAG_RESOLUTIONUNIT, RESUNIT_INCH);
    if (spp > colours) {
        uint16_t extra[4] = { EXTRASAMPLE_UNASSALPHA, EXTRASAMPLE_UNSPECIFIED, EXTRASAMPLE_UNSPECIFIED, EXTRASAMPLE_UNSPECIFIED };
        TIFFSetField(t, TIFFTAG_EXTRASAMPLES, spp - colours, extra);
    }
    if (bits == 32) TIFFSetField(t, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
    else if (flags & TAGGED) TIFFSetField(t, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_UINT);
    if (icc) TIFFSetField(t, TIFFTAG_ICCPROFILE, (uint32_t)icc->size, icc->data);
    if (layout == 2) {
        TIFFSetField(t, TIFFTAG_TILEWIDTH, 16); TIFFSetField(t, TIFFTAG_TILELENGTH, 16);
        for (i = 0; i < 6; i++) TIFFWriteTile(t, px + i * 64, (i % 3) * 16, (i / 3) * 16, 0, 0);
    } else {
        TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, 8);
        for (s = 0; s < (layout == 1 ? spp : 1); s++)
            for (i = 0; i < 32; i++) TIFFWriteScanline(t, px + ((i + s) % 32) * row, i, (uint16_t)s);
    }
    TIFFClose(t);
    return 1;
}

static int write_separated_tiff(const char *path, int bits, int spp, int layout, const Bytes *icc) {
    return write_test_tiff(path, PHOTOMETRIC_SEPARATED, bits, spp, layout, icc);
}

static void header_only_cmyk_tiff(const Bytes *press) {
    static const struct { int bits, spp, layout; const char *name; } FILES[] = {
        { 8, 4, 0, "8-bit CMYK strips" }, { 8, 4, 1, "8-bit CMYK planes" }, { 8, 4, 2, "8-bit CMYK tiles" },
        { 16, 4, 0, "16-bit CMYK strips" }, { 16, 4, 2, "16-bit CMYK tiles" }, { 8, 6, 0, "8-bit CMYK + 2 extra samples" },
        { 8, 5, 0, "8-bit CMYK + alpha" }, { 16, 5, 1 | TAGGED, "16-bit planar CMYK + alpha, SampleFormat" },
        { 8, 3, 0, "8-bit 3-ink separation" }, { 32, 4, 0, "float CMYK strips" }, { 32, 4, 2, "float CMYK tiles" }
    };
    const char *path = scratch("icc_separated.tif");
    unsigned i;
    int k;
    char what[128];
    for (i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++) {
        if (!write_separated_tiff(path, FILES[i].bits, FILES[i].spp, FILES[i].layout, press)) { fail("cannot write %s", path); return; }
        for (k = 0; k < 2; k++) {
            const int flags = k ? TIFF_CMYK : TIFF_DEFAULT;
            snprintf(what, sizeof(what), "TIFF, %s, flags %d", FILES[i].name, flags);
            same_as_full(what, FreeImage_Load(FIF_TIFF, path, flags), FreeImage_Load(FIF_TIFF, path, flags | FIF_LOAD_NOPIXELS));
        }
        remove(path);
    }
}

/* tiles are copied as they are, or RGB and CMYK sample by sample: a layout FreeImage stores in pixels of another size is refused, not written past the image */
static void tiled_layouts_refused(const Bytes *press) {
    static const struct { int photometric, bits, spp; const char *name; } FILES[] = {
        { PHOTOMETRIC_SEPARATED, 8, 2, "2-ink 8-bit" }
    };
    static const int FLAGS[4] = { TIFF_DEFAULT, FIF_LOAD_NOPIXELS, TIFF_CMYK, TIFF_CMYK | FIF_LOAD_NOPIXELS };
    const char *path = scratch("icc_tiled_layout.tif");
    unsigned i;
    int k;
    for (i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++) {
        const Bytes *icc = (FILES[i].photometric == PHOTOMETRIC_SEPARATED) ? press : NULL;
        if (!write_test_tiff(path, FILES[i].photometric, FILES[i].bits, FILES[i].spp, 2, icc)) { fail("cannot write %s", path); return; }
        for (k = 0; k < 4; k++) {
            FIBITMAP *dib = FreeImage_Load(FIF_TIFF, path, FLAGS[k]);
            CHECK(!dib, "tiled %s, flags %x: loaded, its tiles would be copied past the image", FILES[i].name, FLAGS[k]);
            if (dib) FreeImage_Unload(dib);
        }
        remove(path);
    }
}

/* what write_test_tiff stores for sample c of pixel x in file row y, 8 or 16 bits */
static unsigned test_sample(int bits, int spp, int flags, int x, int y, int c) {
    const int bytes = bits / 8;
    const size_t o = ((flags & 3) == 1) ? ((size_t)((y + c) % 32) * 48 + x) * bytes : ((size_t)(y % 32) * 48 * spp + (size_t)x * spp + c) * bytes;
    BYTE b[2];
    WORD w;
    b[0] = (BYTE)(o * 11 + 5);
    b[1] = (BYTE)((o + 1) * 11 + 5);
    if (bytes == 1) return b[0];
    memcpy(&w, b, 2);
    return w;
}

/* CMYK and RGB strips with any number of extra samples: the first is the alpha; TIFF_CMYK gives the inks */
static void extra_sample_tiffs(const Bytes *press) {
    static const struct { int photometric, bits, spp, flags; const char *name; } FILES[] = {
        { PHOTOMETRIC_SEPARATED, 8, 5, 0, "8-bit CMYK + alpha" }, { PHOTOMETRIC_SEPARATED, 8, 5, 1 | TAGGED, "8-bit planar CMYK + alpha, SampleFormat" },
        { PHOTOMETRIC_SEPARATED, 16, 5, 0 | TAGGED, "16-bit CMYK + alpha, SampleFormat" }, { PHOTOMETRIC_SEPARATED, 16, 5, 1, "16-bit planar CMYK + alpha" },
        { PHOTOMETRIC_SEPARATED, 16, 6, 0 | TAGGED, "16-bit CMYK + 2 extra samples, SampleFormat" }, { PHOTOMETRIC_SEPARATED, 8, 7, 1, "8-bit planar CMYK + 3 extra samples" },
        { PHOTOMETRIC_RGB, 8, 5, 0, "8-bit RGB + 2 extra samples" }, { PHOTOMETRIC_RGB, 16, 5, 1 | TAGGED, "16-bit planar RGB + 2 extra samples, SampleFormat" }
    };
    const char *path = scratch("icc_extra_samples.tif");
    unsigned i;
    for (i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++) {
        const int cmyk = FILES[i].photometric == PHOTOMETRIC_SEPARATED, colours = cmyk ? 4 : 3, deep = FILES[i].bits == 16;
        unsigned wrong_alpha = 0, wrong_colour = 0, wrong_ink = 0;
        FIBITMAP *plain, *inks;
        int x, y, c;
        if (!write_test_tiff(path, FILES[i].photometric, FILES[i].bits, FILES[i].spp, FILES[i].flags, cmyk ? press : NULL)) { fail("cannot write %s", path); return; }
        plain = FreeImage_Load(FIF_TIFF, path, TIFF_DEFAULT);
        inks = cmyk ? FreeImage_Load(FIF_TIFF, path, TIFF_CMYK) : NULL;
        remove(path);
        if (!plain || FreeImage_GetImageType(plain) != (deep ? FIT_RGBA16 : FIT_BITMAP) || FreeImage_GetBPP(plain) != (deep ? 64u : 32u)) {
            fail("%s: not loaded as RGBA", FILES[i].name);
        } else {
            for (y = 0; y < 32; y++) {
                const BYTE *p = FreeImage_GetScanLine(plain, 31 - y);
                for (x = 0; x < 48; x++) {
                    unsigned v[4];
                    if (deep) {
                        const WORD *w = (const WORD *)p + 4 * x;
                        v[0] = w[0]; v[1] = w[1]; v[2] = w[2]; v[3] = w[3];
                    } else {
                        v[0] = p[4 * x + FI_RGBA_RED]; v[1] = p[4 * x + FI_RGBA_GREEN]; v[2] = p[4 * x + FI_RGBA_BLUE]; v[3] = p[4 * x + FI_RGBA_ALPHA];
                    }
                    wrong_alpha += v[3] != test_sample(FILES[i].bits, FILES[i].spp, FILES[i].flags, x, y, colours);
                    if (!cmyk) for (c = 0; c < 3; c++) wrong_colour += v[c] != test_sample(FILES[i].bits, FILES[i].spp, FILES[i].flags, x, y, c);
                }
            }
            CHECK(wrong_alpha == 0 && wrong_colour == 0, "%s: %u alphas and %u colours are not the file's", FILES[i].name, wrong_alpha, wrong_colour);
        }
        if (cmyk && (!inks || FreeImage_GetColorType(inks) != FIC_CMYK || !(FreeImage_GetICCProfile(inks)->flags & FIICC_COLOR_IS_CMYK))) {
            fail("%s, TIFF_CMYK: not CMYK with its profile", FILES[i].name);
        } else if (cmyk) {
            for (y = 0; y < 32; y++) {
                const BYTE *p = FreeImage_GetScanLine(inks, 31 - y);
                for (x = 0; x < 48; x++) {
                    for (c = 0; c < 4; c++) wrong_ink += (deep ? ((const WORD *)p)[4 * x + c] : p[4 * x + c]) != test_sample(FILES[i].bits, FILES[i].spp, FILES[i].flags, x, y, c);
                }
            }
            CHECK(wrong_ink == 0, "%s, TIFF_CMYK: %u inks are not the file's", FILES[i].name, wrong_ink);
        }
        if (plain) FreeImage_Unload(plain);
        if (inks) FreeImage_Unload(inks);
    }
}

/* ------------------------------------------------------------------ the ExtraSamples tag */

#define XS_W 37
#define XS_H 21
/* a bit depth of its own for 16-bit half floats */
#define XS_HALF 17

/* a profile write_xs_tiff embeds, when not NULL */
static const Bytes *xs_icc = NULL;

/* sample c of pixel x, y of write_xs_tiff's files: 0 and the maximum often, the rest spread */
static unsigned xs_sample(int bits, int x, int y, int c) {
    const unsigned max = (bits == 8) ? 255u : 65535u, k = (unsigned)(x + 2 * y + 3 * c) % 7;
    unsigned h = (unsigned)x * 73856093u ^ (unsigned)y * 19349663u ^ (unsigned)c * 83492791u;
    if (k == 0) return 0;
    if (k == 1) return max;
    h ^= h >> 13; h *= 0x5bd1e995u; h ^= h >> 15;
    return h % (max + 1);
}

static float xs_float(int x, int y, int c) { return (float)xs_sample(32, x, y, c) / 65535.0f; }

/* sample c of pixel x, y as a half, truncated */
static uint16_t xs_half(int x, int y, int c) {
    const float f = xs_float(x, y, c);
    float m;
    int e = 0;
    if (f <= 0) return 0;
    m = frexpf(f, &e);
    if (e < -13) return (uint16_t)(f * 16777216.0f);
    return (uint16_t)(((e + 14) << 10) | (int)((m * 2 - 1) * 1024));
}

/* a half's value, exactly */
static float xs_half_value(uint16_t h) {
    const int e = (h >> 10) & 31, m = h & 1023;
    const float v = (e == 0) ? ldexpf((float)m, -24) : ldexpf((float)(m | 1024), e - 25);
    return (h & 0x8000) ? -v : v;
}

static void xs_put(BYTE *p, int bits, int x, int y, int c) {
    if (bits == XS_HALF) { const uint16_t v = xs_half(x, y, c); memcpy(p, &v, 2); }
    else if (bits == 32) { const float f = xs_float(x, y, c); memcpy(p, &f, 4); }
    else if (bits == 16) { const uint16_t v = (uint16_t)xs_sample(16, x, y, c); memcpy(p, &v, 2); }
    else *p = (BYTE)xs_sample(8, x, y, c);
}

/* the ExtraSamples entry (338) renamed Matteing (32995), the tag it replaced */
static int rename_to_matteing(const char *path) {
    static BYTE d[1 << 16];
    FILE *f = fopen(path, "r+b");
    size_t size;
    unsigned ifd, n, i;
    if (!f) return 0;
    size = fread(d, 1, sizeof(d), f);
    ifd = d[4] | d[5] << 8 | d[6] << 16 | (unsigned)d[7] << 24;
    n = (ifd + 2 <= size) ? (unsigned)(d[ifd] | d[ifd + 1] << 8) : 0;
    for (i = 0; i < n && ifd + 14 + 12 * i <= size; i++) {
        BYTE *e = d + ifd + 2 + 12 * i;
        if (e[0] == 0x52 && e[1] == 0x01) {
            e[0] = 0xE3; e[1] = 0x80;
            fseek(f, (long)(ifd + 2 + 12 * i), SEEK_SET);
            fwrite(e, 1, 2, f);
            fclose(f);
            return 1;
        }
    }
    fclose(f);
    return 0;
}

/* 37x21 samples of bits each (32: float, XS_HALF: half float) in strips (layout 0), planes (1) or 16x16 tiles (2); types: the
   ExtraSamples values as digits, "" for no tag, "m" for Matteing; inkset 0 for none */
static int write_xs_tiff(const char *path, int photometric, int bits, int spp, int layout, const char *types, int inkset) {
    static BYTE buf[16 * 16 * 8 * 4];
    const int bytes = (bits == XS_HALF) ? 2 : bits / 8, n = (int)strlen(types);
    TIFF *t;
    int s, x, y, c, tx, ty;
    FILE *f = fopen(path, "w+b");
    if (!f) return 0;
    t = TIFFClientOpen(path, "w", (thandle_t)f, t_read, t_write, t_seek, t_close, t_size, t_map, t_unmap);
    if (!t) { fclose(f); return 0; }
    TIFFSetField(t, TIFFTAG_IMAGEWIDTH, XS_W); TIFFSetField(t, TIFFTAG_IMAGELENGTH, XS_H);
    TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, (bits == XS_HALF) ? 16 : bits); TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, spp);
    TIFFSetField(t, TIFFTAG_PHOTOMETRIC, photometric);
    if (inkset) TIFFSetField(t, TIFFTAG_INKSET, inkset);
    if (xs_icc) TIFFSetField(t, TIFFTAG_ICCPROFILE, (uint32_t)xs_icc->size, xs_icc->data);
    TIFFSetField(t, TIFFTAG_PLANARCONFIG, layout == 1 ? PLANARCONFIG_SEPARATE : PLANARCONFIG_CONTIG);
    if (bits == 32 || bits == XS_HALF) TIFFSetField(t, TIFFTAG_SAMPLEFORMAT, SAMPLEFORMAT_IEEEFP);
    if (n) {
        uint16_t es[4];
        if (types[0] == 'm') {
            es[0] = EXTRASAMPLE_ASSOCALPHA;
            TIFFSetField(t, TIFFTAG_EXTRASAMPLES, 1, es);
        } else {
            for (c = 0; c < n; c++) es[c] = (uint16_t)(types[c] - '0');
            TIFFSetField(t, TIFFTAG_EXTRASAMPLES, n, es);
        }
    }
    if (photometric == PHOTOMETRIC_PALETTE) {
        static uint16_t map[3][256];
        for (c = 0; c < 256; c++) { map[0][c] = (uint16_t)(c * 257); map[1][c] = (uint16_t)((255 - c) * 257); map[2][c] = (uint16_t)((c * 7 & 255) * 257); }
        TIFFSetField(t, TIFFTAG_COLORMAP, map[0], map[1], map[2]);
    }
    if (layout == 2) {
        TIFFSetField(t, TIFFTAG_TILEWIDTH, 16); TIFFSetField(t, TIFFTAG_TILELENGTH, 16);
        for (ty = 0; ty < XS_H; ty += 16) {
            for (tx = 0; tx < XS_W; tx += 16) {
                BYTE *p = buf;
                for (y = ty; y < ty + 16; y++) for (x = tx; x < tx + 16; x++) for (c = 0; c < spp; c++, p += bytes) xs_put(p, bits, x, y, c);
                TIFFWriteTile(t, buf, tx, ty, 0, 0);
            }
        }
    } else {
        TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, 5);
        for (s = 0; s < (layout == 1 ? spp : 1); s++) {
            for (y = 0; y < XS_H; y++) {
                BYTE *p = buf;
                for (x = 0; x < XS_W; x++) for (c = (layout == 1 ? s : 0); c < (layout == 1 ? s + 1 : spp); c++, p += bytes) xs_put(p, bits, x, y, c);
                TIFFWriteScanline(t, buf, y, (uint16_t)s);
            }
        }
    }
    TIFFClose(t);
    return (types[0] == 'm') ? rename_to_matteing(path) : 1;
}

/* a premultiplied colour divided by its alpha, as FreeImage rounds it */
static unsigned xs_straight(unsigned c, unsigned a, unsigned max) {
    unsigned long long v;
    if (a == 0 || a >= max) return c;
    v = ((unsigned long long)c * max + a / 2) / a;
    return v > max ? max : (unsigned)v;
}

/* the colours and alpha (sample alpha, -1 for none) that are not the file's: RGB, CMYK as RGB, the inks of TIFF_CMYK, 16-bit and float grey
   as RGBA16 and RGBAF or alone, or 8-bit grey and palette indices and their trns table */
static unsigned xs_wrong(FIBITMAP *d, int photometric, int bits, int alpha, int assoc, int inks) {
    const unsigned max = (bits == 8) ? 255u : 65535u;
    unsigned wrong = 0;
    int x, y, c;
    BYTE trns[256];
    memset(trns, 255, sizeof(trns));
    for (y = 0; y < XS_H; y++) {
        const BYTE *r = FreeImage_GetScanLine(d, XS_H - 1 - y);
        for (x = 0; x < XS_W; x++) {
            if (photometric == PHOTOMETRIC_RGB && (bits == 32 || bits == XS_HALF)) {
                const float *p = (const float *)r + (alpha >= 0 ? 4 : 3) * x;
                const float a = (alpha < 0) ? 1.0f : (bits == XS_HALF) ? xs_half_value(xs_half(x, y, alpha)) : xs_float(x, y, alpha);
                for (c = 0; c < 3; c++) {
                    float e = (bits == XS_HALF) ? xs_half_value(xs_half(x, y, c)) : xs_float(x, y, c);
                    if (assoc && a > 0) e /= a;
                    wrong += p[c] != e;
                }
                if (alpha >= 0) wrong += p[3] != a;
            } else if (photometric == PHOTOMETRIC_RGB || photometric == PHOTOMETRIC_SEPARATED) {
                const int colours = (photometric == PHOTOMETRIC_RGB) ? 3 : 4, step = (inks || alpha >= 0) ? 4 : 3;
                const unsigned a = (alpha >= 0) ? xs_sample(bits, x, y, alpha) : 0;
                unsigned want[4], got[4];
                for (c = 0; c < colours; c++) want[c] = assoc ? xs_straight(xs_sample(bits, x, y, c), a, max) : xs_sample(bits, x, y, c);
                if (bits == 16) {
                    const WORD *p = (const WORD *)r + step * x;
                    for (c = 0; c < step; c++) got[c] = p[c];
                } else {
                    const BYTE *p = r + step * x;
                    if (inks) for (c = 0; c < 4; c++) got[c] = p[c];
                    else { got[0] = p[FI_RGBA_RED]; got[1] = p[FI_RGBA_GREEN]; got[2] = p[FI_RGBA_BLUE]; got[3] = (step == 4) ? p[FI_RGBA_ALPHA] : 0; }
                }
                if (inks) {
                    for (c = 0; c < 4; c++) wrong += got[c] != want[c];
                    continue;
                }
                if (photometric == PHOTOMETRIC_SEPARATED) {
                    for (c = 0; c < 3; c++) want[c] = (max - want[c]) * (max - want[3]) / max;
                }
                for (c = 0; c < 3; c++) wrong += got[c] != want[c];
                if (alpha >= 0) wrong += got[3] != a;
            } else if (bits == 32) {
                /* min-is-white greys are turned in an RGBAF (1 - grey), premultiplied ones divided */
                const float *p = (const float *)r;
                float g = xs_float(x, y, 0);
                if (alpha >= 0) {
                    const float a = xs_float(x, y, alpha);
                    if (photometric == PHOTOMETRIC_MINISWHITE) g = 1.0f - g;
                    else if (assoc && a > 0) g /= a;
                    wrong += (p[4 * x] != g) + (p[4 * x + 1] != g) + (p[4 * x + 2] != g) + (p[4 * x + 3] != a);
                } else {
                    wrong += p[x] != g;
                }
            } else if (bits == 16) {
                /* min-is-white greys are turned in an RGBA16, premultiplied ones divided */
                const WORD *p = (const WORD *)r;
                unsigned g = xs_sample(16, x, y, 0);
                if (alpha >= 0) {
                    const unsigned a = xs_sample(16, x, y, alpha);
                    if (photometric == PHOTOMETRIC_MINISWHITE) g = 65535 - g;
                    else if (assoc) g = xs_straight(g, a, 65535);
                    wrong += (p[4 * x] != g) + (p[4 * x + 1] != g) + (p[4 * x + 2] != g) + (p[4 * x + 3] != a);
                } else {
                    wrong += p[x] != g;
                }
            } else {
                unsigned g = xs_sample(8, x, y, 0);
                if (alpha >= 0) {
                    const unsigned a = xs_sample(8, x, y, alpha);
                    if (assoc && photometric == PHOTOMETRIC_MINISBLACK) g = xs_straight(g, a, 255);
                    trns[g] = (BYTE)a;
                }
                wrong += r[x] != g;
            }
        }
    }
    if (alpha >= 0 && bits == 8 && photometric != PHOTOMETRIC_RGB && photometric != PHOTOMETRIC_SEPARATED) {
        const BYTE *t = FreeImage_GetTransparencyTable(d);
        wrong += !t || FreeImage_GetTransparencyCount(d) != 256 || memcmp(t, trns, 256) != 0;
    }
    return wrong;
}

/* the layout a load has: RGB(A) of the file's depth (float for half), the 4 inks of TIFF_CMYK, RGBA16 or UINT16 for 16-bit grey,
   RGBAF or FLOAT for float grey, or 8 bits, with a trns table when there is an alpha */
static int xs_layout(FIBITMAP *d, int photometric, int bits, int alpha, int inks) {
    const FREE_IMAGE_TYPE type = FreeImage_GetImageType(d);
    if (inks) return (bits == 8) ? (type == FIT_BITMAP && FreeImage_GetBPP(d) == 32) : (type == FIT_RGBA16);
    if (photometric == PHOTOMETRIC_RGB || photometric == PHOTOMETRIC_SEPARATED) {
        if (bits == 8) return type == FIT_BITMAP && FreeImage_GetBPP(d) == ((alpha >= 0) ? 32u : 24u);
        if (bits == 16) return type == ((alpha >= 0) ? FIT_RGBA16 : FIT_RGB16);
        return type == ((alpha >= 0) ? FIT_RGBAF : FIT_RGBF);
    }
    if (bits == 16) return type == ((alpha >= 0) ? FIT_RGBA16 : FIT_UINT16);
    if (bits == 32) return type == ((alpha >= 0) ? FIT_RGBAF : FIT_FLOAT);
    return type == FIT_BITMAP && FreeImage_GetBPP(d) == 8 && (FreeImage_IsTransparent(d) ? 1 : 0) == (alpha >= 0);
}

/* TIFF 6.0: the alpha is the first extra sample (the last samples) typed 1 (premultiplied colours) or 2; 0 is no alpha. Without the
   tag, against the specification, the first extra sample is the alpha: FreeImage 3.18 saves 8-bit and float alphas so. Inks
   other than CMYK are refused. */
static void extra_samples_tag(void) {
    enum { XS_RGB = PHOTOMETRIC_RGB, XS_CMYK = PHOTOMETRIC_SEPARATED, XS_GREY = PHOTOMETRIC_MINISBLACK, XS_WHITE = PHOTOMETRIC_MINISWHITE, XS_PAL = PHOTOMETRIC_PALETTE };
    static const struct { int photometric, bits, spp, layout; const char *types; int alpha, assoc; } XS[] = {
        { XS_RGB, 8, 4, 0, "", 3, 0 }, { XS_RGB, 8, 4, 0, "0", -1, 0 }, { XS_RGB, 8, 4, 0, "2", 3, 0 }, { XS_RGB, 8, 4, 0, "1", 3, 1 },
        { XS_RGB, 8, 4, 0, "m", 3, 1 }, { XS_RGB, 8, 4, 0, "02", 3, 0 }, { XS_RGB, 8, 5, 0, "02", 4, 0 }, { XS_RGB, 8, 5, 0, "20", 3, 0 },
        { XS_RGB, 8, 5, 0, "2", 3, 0 }, { XS_RGB, 8, 5, 1, "01", 4, 1 }, { XS_RGB, 8, 6, 0, "000", -1, 0 }, { XS_RGB, 8, 4, 1, "0", -1, 0 },
        { XS_RGB, 8, 4, 2, "0", -1, 0 }, { XS_RGB, 8, 4, 2, "1", 3, 1 }, { XS_RGB, 8, 6, 2, "002", 5, 0 },
        { XS_RGB, 16, 4, 0, "0", -1, 0 }, { XS_RGB, 16, 4, 0, "1", 3, 1 }, { XS_RGB, 16, 5, 1, "02", 4, 0 }, { XS_RGB, 16, 5, 2, "10", 3, 1 },
        { XS_RGB, 32, 4, 0, "", 3, 0 }, { XS_RGB, 32, 4, 0, "0", -1, 0 }, { XS_RGB, 32, 4, 0, "1", 3, 1 }, { XS_RGB, 32, 4, 2, "0", -1, 0 },
        { XS_CMYK, 8, 5, 0, "", 4, 0 }, { XS_CMYK, 8, 5, 0, "0", -1, 0 }, { XS_CMYK, 8, 5, 0, "1", 4, 1 }, { XS_CMYK, 8, 6, 0, "02", 5, 0 },
        { XS_CMYK, 8, 6, 1, "02", 5, 0 }, { XS_CMYK, 8, 5, 2, "2", 4, 0 }, { XS_CMYK, 8, 6, 2, "00", -1, 0 }, { XS_CMYK, 8, 5, 2, "1", 4, 1 },
        { XS_CMYK, 16, 6, 1, "10", 4, 1 }, { XS_CMYK, 16, 6, 2, "02", 5, 0 }, { XS_CMYK, 16, 5, 0, "m", 4, 1 },
        { XS_GREY, 8, 2, 0, "", 1, 0 }, { XS_GREY, 8, 2, 0, "0", -1, 0 }, { XS_GREY, 8, 2, 0, "1", 1, 1 }, { XS_GREY, 8, 3, 0, "02", 2, 0 },
        { XS_GREY, 8, 3, 1, "02", 2, 0 }, { XS_GREY, 8, 4, 0, "000", -1, 0 },
        { XS_PAL, 8, 2, 0, "2", 1, 0 }, { XS_PAL, 8, 2, 0, "0", -1, 0 }, { XS_PAL, 8, 2, 0, "1", 1, 1 }, { XS_WHITE, 8, 2, 0, "1", 1, 1 },
        { XS_GREY, 8, 2, 2, "2", 1, 0 }, { XS_GREY, 8, 3, 2, "02", 2, 0 }, { XS_PAL, 8, 2, 2, "", 1, 0 }, { XS_WHITE, 8, 2, 2, "0", -1, 0 },
        { XS_GREY, 16, 2, 0, "", 1, 0 }, { XS_GREY, 16, 2, 0, "0", -1, 0 }, { XS_GREY, 16, 2, 0, "2", 1, 0 }, { XS_GREY, 16, 2, 1, "1", 1, 1 },
        { XS_GREY, 16, 3, 2, "02", 2, 0 }, { XS_GREY, 16, 2, 2, "m", 1, 1 }, { XS_WHITE, 16, 2, 0, "2", 1, 0 }, { XS_WHITE, 16, 3, 2, "00", -1, 0 },
        { XS_RGB, XS_HALF, 3, 0, "", -1, 0 }, { XS_RGB, XS_HALF, 3, 1, "", -1, 0 }, { XS_RGB, XS_HALF, 3, 2, "", -1, 0 },
        { XS_RGB, XS_HALF, 4, 0, "", 3, 0 }, { XS_RGB, XS_HALF, 4, 0, "0", -1, 0 }, { XS_RGB, XS_HALF, 4, 1, "1", 3, 1 }, { XS_RGB, XS_HALF, 5, 2, "02", 4, 0 },
        { XS_GREY, 32, 2, 0, "", 1, 0 }, { XS_GREY, 32, 2, 0, "0", -1, 0 }, { XS_GREY, 32, 2, 1, "2", 1, 0 }, { XS_GREY, 32, 2, 2, "1", 1, 1 },
        { XS_GREY, 32, 3, 0, "02", 2, 0 }, { XS_GREY, 32, 2, 0, "m", 1, 1 }, { XS_WHITE, 32, 2, 0, "2", 1, 0 }, { XS_WHITE, 32, 3, 1, "00", -1, 0 }
    };
    static const char *LAYOUT[3] = { "strips", "planes", "tiles" };
    const char *path = scratch("icc_extra_samples_tag.tif");
    unsigned i;
    int k, spp;
    char what[160];
    for (i = 0; i < sizeof(XS) / sizeof(XS[0]); i++) {
        const int cmyk = XS[i].photometric == XS_CMYK;
        if (!write_xs_tiff(path, XS[i].photometric, XS[i].bits, XS[i].spp, XS[i].layout, XS[i].types, cmyk ? INKSET_CMYK : 0)) { fail("cannot write %s", path); return; }
        for (k = 0; k < (cmyk ? 2 : 1); k++) {
            const int flags = k ? TIFF_CMYK : TIFF_DEFAULT;
            /* palette and min-is-white samples are not premultiplied colours */
            const int assoc = XS[i].assoc && XS[i].photometric != XS_PAL && XS[i].photometric != XS_WHITE;
            FIBITMAP *full = FreeImage_Load(FIF_TIFF, path, flags);
            snprintf(what, sizeof(what), "%d-bit %s TIFF, %d samples in %s, ExtraSamples %s, flags %d", (XS[i].bits == XS_HALF) ? 16 : XS[i].bits,
                cmyk ? "CMYK" : XS[i].bits == XS_HALF ? "half-float RGB" : XS[i].photometric == XS_RGB ? "RGB" : XS[i].photometric == XS_PAL ? "palette" :
                XS[i].photometric == XS_WHITE ? "min-is-white" : "grey", XS[i].spp, LAYOUT[XS[i].layout],
                XS[i].types[0] ? XS[i].types : "absent", flags);
            if (!full) { fail("%s: not loaded", what); continue; }
            if (!xs_layout(full, XS[i].photometric, XS[i].bits, k ? -1 : XS[i].alpha, k)) {
                fail("%s: %u-bit type %d, %s", what, FreeImage_GetBPP(full), (int)FreeImage_GetImageType(full), XS[i].alpha >= 0 ? "the alpha is not kept" : "an alpha that is none");
            } else {
                /* TIFF_CMYK leaves the alpha out, its inks straight */
                const unsigned wrong = xs_wrong(full, XS[i].photometric, XS[i].bits, XS[i].alpha, assoc, k);
                CHECK(wrong == 0, "%s: %u values are not the file's", what, wrong);
            }
            same_as_full(what, full, FreeImage_Load(FIF_TIFF, path, flags | FIF_LOAD_NOPIXELS));
        }
        remove(path);
    }
    /* inks other than CMYK have no RGB rendering */
    for (spp = 4; spp <= 6; spp += 2) {
        if (!write_xs_tiff(path, XS_CMYK, 8, spp, 0, "", INKSET_MULTIINK)) { fail("cannot write %s", path); return; }
        for (k = 0; k < 4; k++) {
            FIBITMAP *dib = FreeImage_Load(FIF_TIFF, path, ((k & 1) ? TIFF_CMYK : 0) | ((k & 2) ? FIF_LOAD_NOPIXELS : 0));
            CHECK(!dib, "%d inks, not CMYK, flags %d: loaded", spp, k);
            if (dib) FreeImage_Unload(dib);
        }
        remove(path);
    }
    /* a grey profile stays with a 16-bit or float grey, not with the RGBA16 or RGBAF of a grey and an alpha */
    {
        const Bytes grey = builtin(FICMS_PROFILE_GRAY);
        xs_icc = &grey;
        for (k = 0; k < 4; k++) {
            const int alpha = k & 1, bits = (k & 2) ? 32 : 16;
            FIBITMAP *dib;
            if (!write_xs_tiff(path, XS_GREY, bits, 2, 0, alpha ? "2" : "0", 0)) { fail("cannot write %s", path); break; }
            dib = FreeImage_Load(FIF_TIFF, path, TIFF_DEFAULT);
            CHECK(dib && FreeImage_GetICCProfile(dib)->size == (alpha ? 0 : grey.size), "%d-bit grey with %s and a grey profile: a profile of %u bytes", bits,
                alpha ? "an alpha" : "an unspecified sample", dib ? (unsigned)FreeImage_GetICCProfile(dib)->size : 0u);
            if (dib) FreeImage_Unload(dib);
            remove(path);
        }
        xs_icc = NULL;
    }
}

/* 16-bit CIELab loads as 24-bit RGB through libtiff's 8-bit RGBA, as 8-bit CIELab does */
static void cielab16_tiff(void) {
    static const double LAB[6][3] = { { 0, 0, 0 }, { 50, 0, 0 }, { 76, 0, 0 }, { 100, 0, 0 }, { 60, 40, -20 }, { 30, -50, 45 } };
    const char *paths[2] = { scratch("icc_lab8.tif"), scratch("icc_lab16.tif") };
    FIBITMAP *dib[2], *head;
    unsigned worst = 0;
    int k, x, c;
    for (k = 0; k < 2; k++) {
        FILE *f = fopen(paths[k], "w+b");
        TIFF *t = f ? TIFFClientOpen(paths[k], "w", (thandle_t)f, t_read, t_write, t_seek, t_close, t_size, t_map, t_unmap) : NULL;
        BYTE row[6 * 3 * 2];
        if (!t) { if (f) fclose(f); fail("cannot write %s", paths[k]); return; }
        TIFFSetField(t, TIFFTAG_IMAGEWIDTH, 6); TIFFSetField(t, TIFFTAG_IMAGELENGTH, 1);
        TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, k ? 16 : 8); TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, 3);
        TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_CIELAB); TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        for (x = 0; x < 6; x++) {
            if (k) {
                uint16_t w[3];
                w[0] = (uint16_t)(LAB[x][0] * 65535 / 100 + 0.5); w[1] = (uint16_t)(int16_t)(LAB[x][1] * 256); w[2] = (uint16_t)(int16_t)(LAB[x][2] * 256);
                memcpy(row + 6 * x, w, 6);
            } else {
                row[3 * x] = (BYTE)(LAB[x][0] * 255 / 100 + 0.5); row[3 * x + 1] = (BYTE)(signed char)LAB[x][1]; row[3 * x + 2] = (BYTE)(signed char)LAB[x][2];
            }
        }
        TIFFWriteScanline(t, row, 0, 0);
        TIFFClose(t);
        dib[k] = FreeImage_Load(FIF_TIFF, paths[k], TIFF_DEFAULT);
    }
    head = FreeImage_Load(FIF_TIFF, paths[1], FIF_LOAD_NOPIXELS);
    if (dib[0] && dib[1] && FreeImage_GetBPP(dib[0]) == 24 && FreeImage_GetBPP(dib[1]) == 24 && FreeImage_GetImageType(dib[1]) == FIT_BITMAP) {
        for (x = 0; x < 6 * 3; x++) {
            const int d = (int)FreeImage_GetScanLine(dib[0], 0)[x] - (int)FreeImage_GetScanLine(dib[1], 0)[x];
            if ((unsigned)abs(d) > worst) worst = (unsigned)abs(d);
        }
        c = FreeImage_GetScanLine(dib[1], 0)[3 * 3 + FI_RGBA_RED];
        CHECK(worst <= 1 && c > 200, "16-bit CIELab: colours %u from the 8-bit file's, white's red %d", worst, c);
    } else {
        fail("CIELab: 8-bit %s, 16-bit %s, not both 24-bit RGB", dib[0] ? "loaded" : "not loaded", dib[1] ? "loaded" : "not loaded");
    }
    CHECK(head && FreeImage_GetBPP(head) == 24 && FreeImage_GetImageType(head) == FIT_BITMAP, "16-bit CIELab: the header-only load is not 24-bit RGB");
    for (k = 0; k < 2; k++) { if (dib[k]) FreeImage_Unload(dib[k]); remove(paths[k]); }
    if (head) FreeImage_Unload(head);
}

static void put16(BYTE *p, unsigned v) { p[0] = (BYTE)(v >> 8); p[1] = (BYTE)v; }

/* a raw 8-bit 32x24 PSD of the colour mode and channels, 300 dpi, with the profile when there is one */
static Bytes make_psd(int mode, int channels, const Bytes *icc) {
    Bytes b;
    const DWORD resources = 28 + (icc ? 12 + ((icc->size + 1) & ~1u) : 0);
    BYTE *p;
    int i;
    b.size = 26 + 4 + 4 + resources + 4 + 2 + channels * 32 * 24;
    b.data = p = (BYTE *)calloc(1, b.size);
    memcpy(p, "8BPS", 4); put16(p + 4, 1); put16(p + 12, channels); put32(p + 14, 24); put32(p + 18, 32); put16(p + 22, 8); put16(p + 24, mode);
    p += 30;                                    /* no colour mode data */
    put32(p, resources);
    memcpy(p + 4, "8BIM", 4); put16(p + 8, 1005); put32(p + 12, 16);
    put16(p + 16, 300); put16(p + 20, 1); put16(p + 22, 1); put16(p + 24, 300); put16(p + 28, 1); put16(p + 30, 1);
    p += 32;
    if (icc) {
        memcpy(p, "8BIM", 4); put16(p + 4, 1039); put32(p + 8, icc->size); memcpy(p + 12, icc->data, icc->size);
        p += 12 + ((icc->size + 1) & ~1u);
    }
    p += 4 + 2;                                 /* no layers, raw image data */
    for (i = 0; i < channels * 32 * 24; i++) p[i] = (BYTE)(i * 13 + 1);
    return b;
}

static FIBITMAP *load_memory(FREE_IMAGE_FORMAT fif, const Bytes *b, int flags) {
    FIMEMORY *m = FreeImage_OpenMemory(b->data, b->size);
    FIBITMAP *dib = FreeImage_LoadFromMemory(fif, m, flags);
    FreeImage_CloseMemory(m);
    return dib;
}

static Bytes save_memory_flags(FREE_IMAGE_FORMAT fif, FIBITMAP *dib, int flags) {
    Bytes b = { NULL, 0 };
    FIMEMORY *m = FreeImage_OpenMemory(NULL, 0);
    BYTE *data;
    DWORD size;
    if (FreeImage_SaveToMemory(fif, dib, m, flags) && FreeImage_AcquireMemory(m, &data, &size) && (b.data = (BYTE *)malloc(size))) {
        memcpy(b.data, data, size);
        b.size = size;
    }
    FreeImage_CloseMemory(m);
    return b;
}

static Bytes save_memory(FREE_IMAGE_FORMAT fif, FIBITMAP *dib) {
    return save_memory_flags(fif, dib, 0);
}

static void header_only_psd(const Bytes *press) {
    Bytes files[11];
    const char *names[11] = { "8-bit CMYK", "16-bit CMYK", "indexed", "bitmap", "3-channel multichannel", "4-channel multichannel",
        "CMYK + extra channel", "RGB", "untagged CMYK", "untagged CMYK + extra channel", "2-channel multichannel" };
    FIBITMAP *dib, *thumb = FreeImage_Allocate(10, 6, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
    char what[128];
    unsigned i, x, y;
    int k;
    for (y = 0; y < 6; y++) memset(FreeImage_GetScanLine(thumb, y), 90 + y * 20, 30);
    /* FreeImage's writer: CMYK with a profile and a thumbnail, a palette, 1-bit */
    for (i = 0; i < 2; i++) {
        dib = i ? FreeImage_AllocateT(FIT_RGBA16, 32, 24, 64, 0, 0, 0) : FreeImage_Allocate(32, 24, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
        for (y = 0; y < 24; y++) for (x = 0; x < FreeImage_GetLine(dib); x++) FreeImage_GetScanLine(dib, y)[x] = (BYTE)(x * 3 + y * 7);
        FreeImage_CreateICCProfile(dib, press->data, (long)press->size)->flags |= FIICC_COLOR_IS_CMYK;
        FreeImage_SetThumbnail(dib, thumb);
        FreeImage_SetDotsPerMeterX(dib, 11811); FreeImage_SetDotsPerMeterY(dib, 11811);
        files[i] = save_memory(FIF_PSD, dib);
        FreeImage_Unload(dib);
    }
    dib = FreeImage_Allocate(32, 24, 8, 0, 0, 0);
    for (x = 0; x < 256; x++) { RGBQUAD *q = FreeImage_GetPalette(dib) + x; q->rgbRed = (BYTE)(x * 5); q->rgbGreen = (BYTE)(255 - x); q->rgbBlue = (BYTE)(x * 3); }
    files[2] = save_memory(FIF_PSD, dib);
    FreeImage_Unload(dib);
    dib = FreeImage_Allocate(32, 24, 1, 0, 0, 0);
    files[3] = save_memory(FIF_PSD, dib);
    FreeImage_Unload(dib);
    /* colour modes FreeImage does not write */
    files[4] = make_psd(7, 3, NULL);
    files[5] = make_psd(7, 4, NULL);
    files[6] = make_psd(4, 5, press);
    files[7] = make_psd(3, 3, NULL);
    files[8] = make_psd(4, 4, NULL);
    files[9] = make_psd(4, 5, NULL);
    files[10] = make_psd(7, 2, press);
    FreeImage_Unload(thumb);

    for (i = 0; i < 11; i++) {
        if (!files[i].data) { fail("PSD, %s: not written", names[i]); continue; }
        for (k = 0; k < 2; k++) {
            const int flags = k ? PSD_CMYK : PSD_DEFAULT;
            snprintf(what, sizeof(what), "PSD, %s, flags %d", names[i], flags);
            same_as_full(what, load_memory(FIF_PSD, &files[i], flags), load_memory(FIF_PSD, &files[i], flags | FIF_LOAD_NOPIXELS));
        }
    }
    /* loaded as RGB, CMYK keeps its thumbnail */
    for (i = 0; i < 2; i++) {
        if (!files[i].data) continue;
        dib = load_memory(FIF_PSD, &files[i], PSD_DEFAULT);
        CHECK(dib && FreeImage_GetThumbnail(dib) && FreeImage_GetWidth(FreeImage_GetThumbnail(dib)) == 10, "PSD, %s loaded as RGB: the thumbnail is lost", names[i]);
        if (dib) FreeImage_Unload(dib);
    }
    /* PSD_CMYK without a profile: CMYK all the same, the file's inks inverted */
    for (i = 8; i < 10; i++) {
        unsigned wrong = 0;
        if (!files[i].data) continue;
        dib = load_memory(FIF_PSD, &files[i], PSD_CMYK);
        CHECK(dib && FreeImage_GetColorType(dib) == FIC_CMYK && !FreeImage_GetICCProfile(dib)->size && !FreeImage_IsTransparent(dib),
            "PSD, %s, PSD_CMYK: not loaded as CMYK", names[i]);
        if (dib && FreeImage_GetBPP(dib) == 32) {
            for (y = 0; y < 24; y++) for (x = 0; x < 32 * 4; x++)
                wrong += FreeImage_GetScanLine(dib, 23 - y)[x] != (BYTE)(255 - (BYTE)(((x & 3) * 768 + y * 32 + x / 4) * 13 + 1));
        }
        CHECK(wrong == 0, "PSD, %s, PSD_CMYK: %u samples are not the file's", names[i], wrong);
        if (dib) FreeImage_Unload(dib);
    }
    /* multichannel of fewer than 3 channels loads grey: no CMYK flag, profile or not */
    if (files[10].data) {
        dib = load_memory(FIF_PSD, &files[10], PSD_CMYK);
        CHECK(dib && FreeImage_GetBPP(dib) == 8 && !(FreeImage_GetICCProfile(dib)->flags & FIICC_COLOR_IS_CMYK), "PSD, %s, PSD_CMYK: grey flagged CMYK", names[10]);
        if (dib) FreeImage_Unload(dib);
    }
    for (i = 0; i < 11; i++) free(files[i].data);
}

/* a CMYK PSD's fifth channel is its alpha when the layer and mask section says so: kept loaded as RGB, header-only too,
   dropped with PSD_CMYK, opaque where the file is cut */
static void cmyk_alpha_psd(const Bytes *press) {
    static const struct { int deep, signal, rle, alpha; const char *name; } cases[] = {
        { 0, 1, 0, 1, "8-bit, negative layer count" }, { 0, 1, 1, 1, "8-bit RLE, negative layer count" },
        { 1, 2, 0, 1, "16-bit, Mt16" }, { 1, 2, 1, 1, "16-bit RLE, Mt16" }, { 1, 1, 0, 1, "16-bit, negative layer count" },
        { 1, 4, 0, 1, "16-bit, negative layer count in Lr16" }, { 0, 5, 1, 1, "8-bit RLE, Mtrn after a padded block" },
        { 0, 0, 0, 0, "8-bit, no layers" }, { 0, 3, 1, 0, "8-bit RLE, positive layer count" }, { 1, 3, 0, 0, "16-bit, positive layer count" },
    };
    FIBITMAP *ink[2], *alpha[2];
    unsigned i, x, y, k, ok;
    char what[128];
    for (k = 0; k < 2; k++) {
        ink[k] = k ? FreeImage_AllocateT(FIT_RGBA16, 40, 24, 64, 0, 0, 0) : FreeImage_Allocate(40, 24, 32, 0, 0, 0);
        alpha[k] = k ? FreeImage_AllocateT(FIT_UINT16, 40, 24, 16, 0, 0, 0) : FreeImage_Allocate(40, 24, 8, 0, 0, 0);
        for (y = 0; y < 24; y++) {
            BYTE *p = FreeImage_GetScanLine(ink[k], y), *a = FreeImage_GetScanLine(alpha[k], y);
            for (x = 0; x < FreeImage_GetLine(ink[k]); x++) p[x] = (BYTE)(x * 7 + y * 13);
            for (x = 0; x < 40; x++) {
                const unsigned v = (x < 3) ? 0 : (x > 36) ? 65535 : x * 1543 + y * 257;
                if (k) ((WORD *)a)[x] = (WORD)v;
                else a[x] = (BYTE)((x > 36) ? 255 : v);
            }
        }
    }
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const int deep = cases[i].deep;
        Bytes file = make_cmyka_psd(ink[deep], alpha[deep], deep ? NULL : press, cases[i].signal, cases[i].rle);
        Bytes twin = make_cmyka_psd(ink[deep], alpha[deep], deep ? NULL : press, 0, cases[i].rle);
        FIBITMAP *plain = load_memory(FIF_PSD, &file, PSD_DEFAULT), *rgb = load_memory(FIF_PSD, &twin, PSD_DEFAULT);
        FIBITMAP *cmyk = load_memory(FIF_PSD, &file, PSD_CMYK), *cmyk_twin = load_memory(FIF_PSD, &twin, PSD_CMYK);
        snprintf(what, sizeof(what), "CMYK + alpha PSD, %s", cases[i].name);
        same_as_full(what, load_memory(FIF_PSD, &file, PSD_DEFAULT), load_memory(FIF_PSD, &file, FIF_LOAD_NOPIXELS));
        CHECK(rgb && FreeImage_GetBPP(rgb) == (deep ? 48u : 24u), "%s: without layers, not %s RGB", what, deep ? "48-bit" : "24-bit");
        if (cases[i].alpha) {
            FIBITMAP *a = plain ? FreeImage_GetChannel(plain, FICC_ALPHA) : NULL;
            CHECK(plain && FreeImage_GetBPP(plain) == (deep ? 64u : 32u) && FreeImage_GetColorType(plain) == FIC_RGBALPHA, "%s: not RGBA", what);
            CHECK(same_plane(a, alpha[deep]), "%s: the alpha is not the file's fifth channel", what);
            for (k = FICC_RED, ok = 1; k <= FICC_BLUE; k++) {
                FIBITMAP *c1 = plain ? FreeImage_GetChannel(plain, (FREE_IMAGE_COLOR_CHANNEL)k) : NULL;
                FIBITMAP *c2 = rgb ? FreeImage_GetChannel(rgb, (FREE_IMAGE_COLOR_CHANNEL)k) : NULL;
                ok &= same_plane(c1, c2);
                if (c1) FreeImage_Unload(c1);
                if (c2) FreeImage_Unload(c2);
            }
            CHECK(ok, "%s: the colours are not those of the file without layers", what);
            if (a) FreeImage_Unload(a);
        } else {
            CHECK(same_plane(plain, rgb), "%s: not the image of the file without layers", what);
        }
        CHECK(cmyk && cmyk_twin && FreeImage_GetColorType(cmyk) == FIC_CMYK && digest(cmyk) == digest(cmyk_twin), "%s, PSD_CMYK: not the CMYK of the file without layers", what);
        if (plain) FreeImage_Unload(plain);
        if (rgb) FreeImage_Unload(rgb);
        if (cmyk) FreeImage_Unload(cmyk);
        if (cmyk_twin) FreeImage_Unload(cmyk_twin);
        free(file.data);
        free(twin.data);
    }
    /* the last 5 rows of the alpha cut off */
    for (k = 0; k < 2; k++) {
        Bytes file = make_cmyka_psd(ink[k], alpha[k], NULL, k ? 2 : 1, 0);
        FIBITMAP *dib, *a;
        file.size -= 5 * 40 * (k + 1);
        dib = load_memory(FIF_PSD, &file, PSD_DEFAULT);
        a = dib ? FreeImage_GetChannel(dib, FICC_ALPHA) : NULL;
        for (y = 0, ok = (a != NULL); a && y < 24; y++) {
            for (x = 0; x < 40; x++) {
                const unsigned got = k ? ((WORD *)FreeImage_GetScanLine(a, y))[x] : FreeImage_GetScanLine(a, y)[x];
                const unsigned want = (y < 5) ? (k ? 65535u : 255u) : k ? ((WORD *)FreeImage_GetScanLine(alpha[k], y))[x] : FreeImage_GetScanLine(alpha[k], y)[x];
                ok &= (got == want);
            }
        }
        CHECK(ok, "CMYK + alpha PSD, %d-bit, cut in its alpha: the rows it holds are not its alpha, or the others not opaque", k ? 16 : 8);
        if (a) FreeImage_Unload(a);
        if (dib) FreeImage_Unload(dib);
        free(file.data);
    }
    for (k = 0; k < 2; k++) { FreeImage_Unload(ink[k]); FreeImage_Unload(alpha[k]); }
}

/* an RGB PSD whose DisplayInfo of n extra channels comes before its profile and a 300 dpi resolution */
static Bytes make_psd_display_info(int entries, const Bytes *icc) {
    const DWORD info = 14 * entries, prof = (icc->size + 1) & ~1u, resources = 12 + info + 12 + prof + 28;
    Bytes b;
    BYTE *p;
    int i;
    b.size = 26 + 4 + 4 + resources + 4 + 2 + 3 * 32 * 24;
    b.data = p = (BYTE *)calloc(1, b.size);
    memcpy(p, "8BPS", 4); put16(p + 4, 1); put16(p + 12, 3); put32(p + 14, 24); put32(p + 18, 32); put16(p + 22, 8); put16(p + 24, 3);
    p += 30;
    put32(p, resources);
    p += 4;
    memcpy(p, "8BIM", 4); put16(p + 4, 1007); put32(p + 8, info);
    p += 12;
    for (i = 0; i < entries; i++, p += 14) { put16(p + 2, 65535); put16(p + 10, 50); }
    memcpy(p, "8BIM", 4); put16(p + 4, 1039); put32(p + 8, icc->size); memcpy(p + 12, icc->data, icc->size);
    p += 12 + prof;
    memcpy(p, "8BIM", 4); put16(p + 4, 1005); put32(p + 8, 16);
    put16(p + 12, 300); put16(p + 16, 1); put16(p + 18, 1); put16(p + 20, 300); put16(p + 24, 1); put16(p + 26, 1);
    p += 28 + 4 + 2;                            /* no layers, raw image data */
    for (i = 0; i < 3 * 32 * 24; i++) p[i] = (BYTE)(i * 13 + 1);
    return b;
}

/* the resources after a DisplayInfo of several extra channels are read: the profile and the resolution come back */
static void display_info_psd(void) {
    Bytes adobe = builtin(FICMS_PROFILE_ADOBE_RGB);
    int entries;
    for (entries = 1; entries <= 4; entries++) {
        Bytes b = make_psd_display_info(entries, &adobe);
        FIBITMAP *dib = load_memory(FIF_PSD, &b, PSD_DEFAULT);
        const FIICCPROFILE *icc = dib ? FreeImage_GetICCProfile(dib) : NULL;
        CHECK(dib && (icc->size == (long)adobe.size) && !memcmp(icc->data, adobe.data, adobe.size) && FreeImage_GetDotsPerMeterX(dib) == 11811,
            "PSD, DisplayInfo of %d entries: %s", entries, dib ? "the profile or the resolution after it is lost" : "not loaded");
        if (dib) FreeImage_Unload(dib);
        free(b.data);
    }
}

/* an RGB or Lab PSD's 4th channel is its alpha when the file says so, or says nothing (FreeImage's RGBA PSDs) unless an
   alpha identifier or the DisplayInfo makes it a selection or a spot ink; FreeImage's own RGBA PSD loads back whole */
static void rgb_extra_psd(void) {
    static const struct { int mode, deep, signal, id, kind, alpha; const char *name; } cases[] = {
        { 3, 0, 0, -1, -1, 1, "RGB, no layers" }, { 3, 0, 3, -1, -1, 0, "RGB, a positive layer count" },
        { 3, 0, 1, -1, -1, 1, "RGB, a negative layer count" }, { 3, 0, 0, 5, -1, 0, "RGB, no layers, alpha identifier 5" },
        { 3, 0, 0, 0, -1, 1, "RGB, no layers, alpha identifier 0" }, { 3, 0, 0, -1, 2, 0, "RGB, no layers, a spot channel (1007)" },
        { 3, 0, 0, -1, 0x102, 0, "RGB, no layers, a spot channel (1077)" }, { 3, 0, 0, -1, 1, 1, "RGB, no layers, an alpha channel (1007)" },
        { 3, 1, 2, -1, -1, 1, "16-bit RGB, Mt16" }, { 3, 1, 3, -1, -1, 0, "16-bit RGB, a positive layer count" },
        { 3, 1, 4, -1, -1, 1, "16-bit RGB, a negative layer count in Lr16" }, { 9, 0, 3, -1, -1, 0, "Lab, a positive layer count" },
        { 9, 0, 0, -1, -1, 1, "Lab, no layers" },
    };
    char what[128];
    unsigned i, k, x, y, ok;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const int deep = cases[i].deep;
        Bytes file = make_extra_psd(cases[i].mode, deep ? 16 : 8, cases[i].signal, cases[i].id, cases[i].kind, NULL);
        Bytes twin = make_extra_psd(cases[i].mode, deep ? 16 : 8, 3, -1, -1, NULL);
        FIBITMAP *dib = load_memory(FIF_PSD, &file, PSD_DEFAULT), *rgb = load_memory(FIF_PSD, &twin, PSD_DEFAULT);
        snprintf(what, sizeof(what), "PSD, %s", cases[i].name);
        same_as_full(what, load_memory(FIF_PSD, &file, PSD_DEFAULT), load_memory(FIF_PSD, &file, FIF_LOAD_NOPIXELS));
        if (cases[i].alpha) {
            FIBITMAP *a = dib ? FreeImage_GetChannel(dib, FICC_ALPHA) : NULL;
            CHECK(dib && FreeImage_GetBPP(dib) == (deep ? 64u : 32u) && FreeImage_GetColorType(dib) == FIC_RGBALPHA, "%s: not RGBA", what);
            for (y = 0, ok = (a != NULL); a && y < 24; y++) {
                for (x = 0; x < 32; x++) {
                    const unsigned want = (BYTE)(x * 8 + (23 - y));
                    ok &= deep ? (((WORD *)FreeImage_GetScanLine(a, y))[x] == want * 257) : (FreeImage_GetScanLine(a, y)[x] == want);
                }
            }
            CHECK(ok, "%s: the alpha is not the 4th channel", what);
            for (k = FICC_RED; k <= FICC_BLUE; k++) {
                FIBITMAP *c1 = dib ? FreeImage_GetChannel(dib, (FREE_IMAGE_COLOR_CHANNEL)k) : NULL;
                FIBITMAP *c2 = rgb ? FreeImage_GetChannel(rgb, (FREE_IMAGE_COLOR_CHANNEL)k) : NULL;
                ok &= same_plane(c1, c2);
                if (c1) FreeImage_Unload(c1);
                if (c2) FreeImage_Unload(c2);
            }
            CHECK(ok, "%s: the colours are not those of the file without its 4th channel", what);
            if (a) FreeImage_Unload(a);
        } else {
            CHECK(rgb && FreeImage_GetBPP(rgb) == (deep ? 48u : 24u) && same_plane(dib, rgb), "%s: not the image without its 4th channel", what);
        }
        if (dib) FreeImage_Unload(dib);
        if (rgb) FreeImage_Unload(rgb);
        free(file.data);
        free(twin.data);
    }
    /* FreeImage's RGBA PSD: no layers, a DisplayInfo of kind 0 */
    {
        FIBITMAP *rgba = FreeImage_Allocate(32, 24, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK), *back;
        Bytes file;
        for (y = 0; y < 24; y++) for (x = 0; x < 128; x++) FreeImage_GetScanLine(rgba, y)[x] = (BYTE)(x * 5 + y * 11);
        file = save_memory(FIF_PSD, rgba);
        back = file.data ? load_memory(FIF_PSD, &file, PSD_DEFAULT) : NULL;
        CHECK(back && same_plane(back, rgba), "PSD, FreeImage's RGBA: does not load back whole");
        if (back) FreeImage_Unload(back);
        FreeImage_Unload(rgba);
        free(file.data);
    }
}

/* the value of a grey PSD's ramp channel, as an 8-, 16- or 32-bit plane holds it */
static int ramp_is(FIBITMAP *a, int bits, unsigned x, unsigned y, unsigned v) {
    const BYTE *line = FreeImage_GetScanLine(a, y);
    if (bits == 32) return ((const float *)line)[x] == v / 255.0f;
    if (bits == 16) return ((const WORD *)line)[x] == v * 257;
    return line[x] == v;
}

/* a grey PSD with transparency loads as RGBA, its grey profile left out, header-only too; with no layers or a positive
   layer count it stays grey with its profile; the alpha rows a cut file does not hold are opaque */
static void grey_alpha_psd(void) {
    static const struct { int bits, signal, alpha; const char *name; } cases[] = {
        { 8, 1, 1, "8-bit, a negative layer count" }, { 16, 2, 1, "16-bit, Mt16" }, { 16, 4, 1, "16-bit, a negative layer count in Lr16" },
        { 32, 1, 1, "32-bit, a negative layer count" }, { 8, 0, 0, "8-bit, no layers" }, { 8, 3, 0, "8-bit, a positive layer count" },
        { 32, 3, 0, "32-bit, a positive layer count" },
    };
    Bytes grey = make_gray_profile(1.8);
    char what[128];
    unsigned i, k, x, y, ok;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const int bits = cases[i].bits;
        const FREE_IMAGE_TYPE rgba_type = (bits == 32) ? FIT_RGBAF : (bits == 16) ? FIT_RGBA16 : FIT_BITMAP;
        Bytes file = make_extra_psd(1, bits, cases[i].signal, -1, -1, &grey);
        Bytes twin = make_extra_psd(1, bits, 3, -1, -1, &grey);
        FIBITMAP *dib = load_memory(FIF_PSD, &file, PSD_DEFAULT), *g = load_memory(FIF_PSD, &twin, PSD_DEFAULT);
        snprintf(what, sizeof(what), "grey PSD, %s", cases[i].name);
        same_as_full(what, load_memory(FIF_PSD, &file, PSD_DEFAULT), load_memory(FIF_PSD, &file, FIF_LOAD_NOPIXELS));
        CHECK(g && (FreeImage_GetICCProfile(g)->size == (long)grey.size) && (FreeImage_GetBPP(g) == (unsigned)bits), "%s: the grey twin is not grey with its profile", what);
        if (cases[i].alpha) {
            FIBITMAP *a = dib ? FreeImage_GetChannel(dib, FICC_ALPHA) : NULL;
            CHECK(dib && FreeImage_GetImageType(dib) == rgba_type && FreeImage_GetBPP(dib) == (unsigned)(bits * 4) &&
                FreeImage_GetColorType(dib) == FIC_RGBALPHA && !FreeImage_GetICCProfile(dib)->size, "%s: not RGBA without a profile", what);
            for (y = 0, ok = (a != NULL); a && y < 24; y++) {
                for (x = 0; x < 32; x++) ok &= ramp_is(a, bits, x, y, (BYTE)(x * 8 + (23 - y)));
            }
            CHECK(ok, "%s: the alpha is not the 2nd channel", what);
            for (k = FICC_RED; k <= FICC_BLUE; k++) {
                FIBITMAP *c = dib ? FreeImage_GetChannel(dib, (FREE_IMAGE_COLOR_CHANNEL)k) : NULL;
                ok &= same_plane(c, g);
                if (c) FreeImage_Unload(c);
            }
            CHECK(ok, "%s: red, green and blue are not the grey", what);
            if (a) FreeImage_Unload(a);
        } else {
            CHECK(same_plane(dib, g) && (FreeImage_GetICCProfile(dib)->size == (long)grey.size), "%s: not the grey image with its profile", what);
        }
        if (dib) FreeImage_Unload(dib);
        if (g) FreeImage_Unload(g);
        free(file.data);
        free(twin.data);
    }
    /* the last 5 rows of the alpha cut off */
    for (k = 0; k < 2; k++) {
        const int bits = k ? 32 : 8;
        Bytes file = make_extra_psd(1, bits, 1, -1, -1, NULL);
        FIBITMAP *dib, *a;
        file.size -= 5 * 32 * (bits / 8);
        dib = load_memory(FIF_PSD, &file, PSD_DEFAULT);
        a = dib ? FreeImage_GetChannel(dib, FICC_ALPHA) : NULL;
        for (y = 0, ok = (a != NULL); a && y < 24; y++) {
            for (x = 0; x < 32; x++) ok &= (y < 5) ? ramp_is(a, bits, x, y, 255) : ramp_is(a, bits, x, y, (BYTE)(x * 8 + (23 - y)));
        }
        CHECK(ok, "grey PSD, %d-bit, cut in its alpha: the rows it holds are not its alpha, or the others not opaque", bits);
        if (a) FreeImage_Unload(a);
        if (dib) FreeImage_Unload(dib);
        free(file.data);
    }
    free(grey.data);
}

/* CMYK pages keep CMYK through the multi-page cache: appended, inserted, changed, saved to a file and to memory */
static unsigned long long page_digest(FIMULTIBITMAP *mb, int page) {
    FIBITMAP *p = FreeImage_LockPage(mb, page);
    unsigned long long h = p ? digest(p) : 0;
    if (p) FreeImage_UnlockPage(mb, p, FALSE);
    return h;
}

static void cmyk_pages_cached(const Bytes *press) {
    const char *path = scratch("icc_pages.tif");
    FIBITMAP *rgb = FreeImage_Allocate(37, 23, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK), *rgb16, *cmyk, *cmyk16, *device, *pages[4], *p, *changed = NULL;
    const char *names[4] = { "16-bit CMYK", "CMYK", "RGB", "CMYK without a profile" };
    FIMULTIBITMAP *mb;
    FIMEMORY *mem;
    unsigned x, y;
    int i;
    for (y = 0; y < 23; y++) { BYTE *q = FreeImage_GetScanLine(rgb, y); for (x = 0; x < 37 * 3; x++) q[x] = rnd() & 255; }
    cmyk = FreeImage_ConvertToCMYK(rgb, press->data, press->size, 0);
    rgb16 = FreeImage_ConvertToRGB16(rgb);
    cmyk16 = FreeImage_ConvertToCMYK(rgb16, press->data, press->size, 0);
    device = FreeImage_Clone(cmyk);
    FreeImage_DestroyICCProfile(device);
    pages[0] = cmyk16; pages[1] = cmyk; pages[2] = rgb; pages[3] = device;

    mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, TRUE, FALSE, TRUE, 0);
    FreeImage_AppendPage(mb, cmyk);
    FreeImage_AppendPage(mb, rgb);
    FreeImage_InsertPage(mb, 0, cmyk16);
    FreeImage_AppendPage(mb, device);
    for (i = 0; i < 4; i++) CHECK(page_digest(mb, i) == digest(pages[i]), "cached %s page %d came back changed", names[i], i);
    CHECK(FreeImage_CloseMultiBitmap(mb, 0), "CMYK pages: not saved");

    mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, FALSE, FALSE, TRUE, TIFF_CMYK);
    CHECK(mb && FreeImage_GetPageCount(mb) == 4, "CMYK pages: not 4 pages");
    for (i = 0; mb && i < 4; i++) CHECK(page_digest(mb, i) == digest(pages[i]), "saved %s page %d came back changed", names[i], i);
    /* a changed CMYK page */
    p = mb ? FreeImage_LockPage(mb, 1) : NULL;
    if (p) {
        FreeImage_Invert(p);
        changed = FreeImage_Clone(p);
        FreeImage_UnlockPage(mb, p, TRUE);
        CHECK(page_digest(mb, 1) == digest(changed), "a changed CMYK page came back changed again");
    }
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
    mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, FALSE, TRUE, TRUE, TIFF_CMYK);
    CHECK(mb && changed && page_digest(mb, 1) == digest(changed) && page_digest(mb, 0) == digest(cmyk16), "a changed CMYK page was not saved as CMYK");
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);

    /* the same through memory */
    mem = FreeImage_OpenMemory(NULL, 0);
    {
        Bytes file = read_file(path);
        FIMEMORY *in = FreeImage_OpenMemory(file.data, file.size);
        FIMULTIBITMAP *doc = FreeImage_LoadMultiBitmapFromMemory(FIF_TIFF, in, TIFF_CMYK);
        if (doc) {
            FreeImage_AppendPage(doc, cmyk);
            CHECK(FreeImage_SaveMultiBitmapToMemory(FIF_TIFF, doc, mem, 0), "CMYK pages: not saved to memory");
            FreeImage_CloseMultiBitmap(doc, 0);
        }
        FreeImage_CloseMemory(in);
        free(file.data);
        FreeImage_SeekMemory(mem, 0, SEEK_SET);
        doc = FreeImage_LoadMultiBitmapFromMemory(FIF_TIFF, mem, TIFF_CMYK);
        CHECK(doc && FreeImage_GetPageCount(doc) == 5 && page_digest(doc, 4) == digest(cmyk), "a CMYK page appended in memory came back changed");
        if (doc) FreeImage_CloseMultiBitmap(doc, 0);
    }
    FreeImage_CloseMemory(mem);
    remove(path);
    for (i = 0; i < 4; i++) FreeImage_Unload(pages[i]);
    FreeImage_Unload(rgb16);
    if (changed) FreeImage_Unload(changed);
}

/* TIFF_CMYK and PSD_CMYK on save: the 4 channels as the inks, unless the image has a non-CMYK profile */
static void cmyk_save_flags(const Bytes *press) {
    static const struct { FREE_IMAGE_FORMAT fif; int flag; const char *name; } FMT[2] = { { FIF_TIFF, TIFF_CMYK, "TIFF" }, { FIF_PSD, PSD_CMYK, "PSD" } };
    static const char *KIND[3] = { "without a profile", "with a CMYK profile but no CMYK flag", "with an unreadable profile" };
    const char *path = scratch("icc_cmyk_flag.tif");
    Bytes srgb = builtin(FICMS_PROFILE_SRGB);
    BYTE junk[300];
    FIBITMAP *rgb = FreeImage_Allocate(37, 23, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK), *rgb16, *rgba[2], *ink[2];
    FIMULTIBITMAP *mb;
    unsigned x, y;
    int f, d, k;
    for (y = 0; y < 23; y++) { BYTE *p = FreeImage_GetScanLine(rgb, y); for (x = 0; x < 37 * 3; x++) p[x] = rnd() & 255; }
    for (x = 0; x < sizeof(junk); x++) junk[x] = rnd() & 255;
    rgb16 = FreeImage_ConvertToRGB16(rgb);
    ink[0] = FreeImage_ConvertToCMYK(rgb, press->data, press->size, 0);
    ink[1] = FreeImage_ConvertToCMYK(rgb16, press->data, press->size, 0);
    rgba[0] = FreeImage_ConvertTo32Bits(rgb);
    rgba[1] = FreeImage_ConvertToRGBA16(rgb);
    for (y = 0; y < 23; y++) {
        BYTE *p = FreeImage_GetScanLine(rgba[0], y);
        FIRGBA16 *q = (FIRGBA16 *)FreeImage_GetScanLine(rgba[1], y);
        for (x = 0; x < 37; x++) { p[4 * x + FI_RGBA_ALPHA] = rnd() & 255; q[x].alpha = (WORD)rnd(); }
    }
    for (d = 0; d < 2; d++) FreeImage_CreateICCProfile(rgba[d], srgb.data, srgb.size);

    for (f = 0; f < 2; f++) {
        for (d = 0; d < 2; d++) {
            const char *rgba_name = d ? "RGBA16" : "32-bit RGBA", *cmyk_name = d ? "16-bit CMYK" : "CMYK";
            Bytes plain = save_memory_flags(FMT[f].fif, rgba[d], 0), flagged;
            clear_messages();
            flagged = save_memory_flags(FMT[f].fif, rgba[d], FMT[f].flag);
            CHECK(plain.size && flagged.size == plain.size && !memcmp(flagged.data, plain.data, plain.size), "%s with an sRGB profile, %s_CMYK: not saved as RGBA", rgba_name, FMT[f].name);
            CHECK(strstr(last_message, "non-CMYK") != NULL, "%s with an sRGB profile, %s_CMYK: no warning", rgba_name, FMT[f].name);
            free(plain.data);
            free(flagged.data);

            /* CMYK without its flag, as FreeImage_Rescale leaves it: the save flag says what it is */
            for (k = 0; k < 3; k++) {
                FIBITMAP *src = FreeImage_Clone(ink[d]), *back;
                Bytes b;
                if (k == 0) FreeImage_DestroyICCProfile(src);
                if (k == 2) FreeImage_CreateICCProfile(src, junk, sizeof(junk));
                FreeImage_GetICCProfile(src)->flags &= ~FIICC_COLOR_IS_CMYK;
                b = save_memory_flags(FMT[f].fif, src, FMT[f].flag);
                back = b.size ? load_memory(FMT[f].fif, &b, FMT[f].flag) : NULL;
                CHECK(back && FreeImage_GetColorType(back) == FIC_CMYK && same_plane(back, src), "%s %s, %s_CMYK: not saved as CMYK", cmyk_name, KIND[k], FMT[f].name);
                if (back) FreeImage_Unload(back);
                FreeImage_Unload(src);
                free(b.data);
            }
        }
    }

    /* one flag for every page: an RGBA page with an RGB profile stays RGBA next to a CMYK one */
    mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, TRUE, FALSE, TRUE, 0);
    FreeImage_AppendPage(mb, ink[0]);
    FreeImage_AppendPage(mb, rgba[0]);
    CHECK(FreeImage_CloseMultiBitmap(mb, TIFF_CMYK), "CMYK and RGBA pages, TIFF_CMYK: not saved");
    mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, FALSE, TRUE, TRUE, TIFF_CMYK);
    CHECK(mb && FreeImage_GetPageCount(mb) == 2 && page_digest(mb, 0) == digest(ink[0]) && page_digest(mb, 1) == digest(rgba[0]), "CMYK and RGBA pages, TIFF_CMYK: came back changed");
    if (mb) FreeImage_CloseMultiBitmap(mb, 0);
    remove(path);

    for (d = 0; d < 2; d++) { FreeImage_Unload(rgba[d]); FreeImage_Unload(ink[d]); }
    FreeImage_Unload(rgb16);
    FreeImage_Unload(rgb);
}

/* a little-endian Exif APP1 payload: ColorSpace, and an interoperability index when one is given */
static unsigned make_exif(BYTE *out, WORD color_space, const char *index) {
    BYTE *t = out + 6;
    unsigned n = index ? 2 : 1;
    memcpy(out, "Exif\0\0", 6);
    memset(t, 0, 80);
    memcpy(t, "II\x2a\0\x08\0\0\0", 8);
    /* IFD0 at 8: the Exif IFD pointer */
    t[8] = 1; t[10] = 0x69; t[11] = 0x87; t[12] = 4; t[14] = 1; t[18] = 26;
    /* the Exif IFD at 26: ColorSpace, then the interoperability IFD pointer */
    t[26] = (BYTE)n;
    t[28] = 0x01; t[29] = 0xA0; t[30] = 3; t[32] = 1; t[36] = (BYTE)color_space; t[37] = (BYTE)(color_space >> 8);
    if (index) {
        t[40] = 0x05; t[41] = 0xA0; t[42] = 4; t[44] = 1; t[48] = 56;
        /* the interoperability IFD at 56: InteroperabilityIndex, 4 ASCII bytes in the entry */
        t[56] = 1; t[58] = 0x01; t[60] = 2; t[62] = 4; memcpy(t + 66, index, 3);
        return 6 + 74;
    }
    return 6 + 44;
}

/* cameras in Adobe RGB mode say so in Exif, not with a profile: ColorSpace 2, or uncalibrated with DCF's "R03" */
static void exif_adobe_rgb(const Bytes *press) {
    static const struct { const char *what; WORD color_space; const char *index; int adobe; } cases[] = {
        { "uncalibrated, R03", 0xFFFF, "R03", 1 },
        { "ColorSpace 2", 2, NULL, 1 },
        { "sRGB, R03", 1, "R03", 0 },
        { "uncalibrated, R98", 0xFFFF, "R98", 0 },
        { "uncalibrated, no index", 0xFFFF, NULL, 0 },
    };
    const char *path = scratch("icc_exif.jpg");
    Bytes adobe = builtin(FICMS_PROFILE_ADOBE_RGB), pro = builtin(FICMS_PROFILE_PROPHOTO_RGB);
    FIBITMAP *rgb = FreeImage_Allocate(24, 16, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
    FIBITMAP *grey = FreeImage_ConvertToGreyscale(rgb), *cmyk = FreeImage_ConvertToCMYK(rgb, press->data, press->size, 0);
    BYTE exif[128];
    unsigned i, k;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const unsigned size = make_exif(exif, cases[i].color_space, cases[i].index);
        FITAG *tag = FreeImage_CreateTag();
        FreeImage_SetTagKey(tag, "ExifRaw");
        FreeImage_SetTagType(tag, FIDT_BYTE);
        FreeImage_SetTagCount(tag, size);
        FreeImage_SetTagLength(tag, size);
        FreeImage_SetTagValue(tag, exif);
        FreeImage_SetMetadata(FIMD_EXIF_RAW, rgb, "ExifRaw", tag);
        FreeImage_SetMetadata(FIMD_EXIF_RAW, grey, "ExifRaw", tag);
        FreeImage_SetMetadata(FIMD_EXIF_RAW, cmyk, "ExifRaw", tag);
        FreeImage_DeleteTag(tag);
        if (!FreeImage_Save(FIF_JPEG, rgb, path, JPEG_QUALITYGOOD)) { fail("Exif %s: not saved", cases[i].what); continue; }
        /* the full load, header-only, and as grey, like a profile in APP2 */
        for (k = 0; k < 3; k++) {
            const int flags[3] = { 0, FIF_LOAD_NOPIXELS, JPEG_GREYSCALE };
            FIBITMAP *back = FreeImage_Load(FIF_JPEG, path, flags[k]);
            FITAG *t = NULL;
            FIICCPROFILE *icc = back ? FreeImage_GetICCProfile(back) : NULL;
            CHECK(back && FreeImage_GetMetadata(FIMD_EXIF_EXIF, back, "ColorSpace", &t), "Exif %s: no Exif read back", cases[i].what);
            CHECK(icc && (cases[i].adobe ? (icc->data && icc->size == adobe.size && !memcmp(icc->data, adobe.data, adobe.size)) : !icc->data),
                "Exif %s, flags 0x%x: a %u-byte profile, want %s", cases[i].what, flags[k], icc ? (unsigned)icc->size : 0, cases[i].adobe ? "Adobe RGB" : "none");
            if (back) FreeImage_Unload(back);
        }
        if (!cases[i].adobe) continue;
        /* a profile of the file's own wins */
        FreeImage_CreateICCProfile(rgb, pro.data, pro.size);
        if (FreeImage_Save(FIF_JPEG, rgb, path, JPEG_QUALITYGOOD)) {
            FIBITMAP *back = FreeImage_Load(FIF_JPEG, path, 0);
            CHECK(back && FreeImage_GetICCProfile(back)->size == pro.size && !memcmp(FreeImage_GetICCProfile(back)->data, pro.data, pro.size), "Exif %s: the APP2 profile lost", cases[i].what);
            if (back) FreeImage_Unload(back);
        }
        FreeImage_DestroyICCProfile(rgb);
        /* Exif describes a camera's colour images: not grey or CMYK ones */
        if (FreeImage_Save(FIF_JPEG, grey, path, JPEG_QUALITYGOOD)) {
            FIBITMAP *back = FreeImage_Load(FIF_JPEG, path, 0);
            CHECK(back && !FreeImage_GetICCProfile(back)->data, "Exif %s: a grey JPEG has a profile", cases[i].what);
            if (back) FreeImage_Unload(back);
        }
        FreeImage_DestroyICCProfile(cmyk);
        FreeImage_GetICCProfile(cmyk)->flags |= FIICC_COLOR_IS_CMYK;
        if (FreeImage_Save(FIF_JPEG, cmyk, path, JPEG_QUALITYGOOD)) {
            FIBITMAP *back = FreeImage_Load(FIF_JPEG, path, JPEG_CMYK);
            CHECK(back && !FreeImage_GetICCProfile(back)->data, "Exif %s: an untagged CMYK JPEG has a profile", cases[i].what);
            if (back) FreeImage_Unload(back);
        }
    }
    remove(path);
    FreeImage_Unload(rgb); FreeImage_Unload(grey); FreeImage_Unload(cmyk);
}

int main(int argc, char **argv) {
    Bytes press;
    int record = (argc > 1 && !strcmp(argv[1], "--record"));
    FreeImage_SetOutputMessage(on_message);
    builtins(record);
    if (record) return 0;
    press = make_press_profile();
    srgb_is_srgb();
    colorimetry();
    descriptions(&press);
    files(&press);
    exif_adobe_rgb(&press);
    png_with_profile_and_gamma();
    tiled_cmyk_tiff(&press);
    header_only_cmyk_tiff(&press);
    extra_sample_tiffs(&press);
    extra_samples_tag();
    cielab16_tiff();
    tiled_layouts_refused(&press);
    header_only_psd(&press);
    cmyk_alpha_psd(&press);
    display_info_psd();
    rgb_extra_psd();
    grey_alpha_psd();
    cmyk_pages_cached(&press);
    cmyk_save_flags(&press);
    free(press.data);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
