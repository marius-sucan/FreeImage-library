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
    CHECK(b->size == a->size && b->data && !memcmp(a->data, b->data, a->size), "%s: the profile did not come back", what);
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
    rgb16 = FreeImage_ConvertToRGB16(rgb);
    cmyk16 = FreeImage_ConvertToCMYK(rgb16, press->data, press->size, 0);
    round_trip("16-bit CMYK TIFF", FIF_TIFF, cmyk16, TIFF_DEFAULT, TIFF_CMYK, 1);
    adobe = FreeImage_ConvertToICCProfile(rgb, a.data, a.size, 0);
    round_trip("Adobe RGB PNG", FIF_PNG, adobe, PNG_DEFAULT, PNG_DEFAULT, 1);
    round_trip("Adobe RGB TIFF", FIF_TIFF, adobe, TIFF_DEFAULT, TIFF_DEFAULT, 1);
    round_trip("Adobe RGB JPEG", FIF_JPEG, adobe, JPEG_QUALITYSUPERB, JPEG_DEFAULT, 0);
    round_trip("Adobe RGB WebP", FIF_WEBP, adobe, WEBP_LOSSLESS, WEBP_DEFAULT, 1);
    grey = FreeImage_ConvertToGreyscale(rgb);
    g18 = FreeImage_ConvertToICCProfile(grey, gp.data, gp.size, 0);
    round_trip("grey PNG", FIF_PNG, g18, PNG_DEFAULT, PNG_DEFAULT, 1);

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
    png_with_profile_and_gamma();
    tiled_cmyk_tiff(&press);
    free(press.data);
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
