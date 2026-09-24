/* JPEG write and lossless transform test; scratch in $JPEG_TEST_TMP */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "FreeImage.h"

static int failures = 0;
static char msgbuf[4096];

static void DLL_CALLCONV msgproc(FREE_IMAGE_FORMAT fif, const char *msg) {
    (void)fif;
    snprintf(msgbuf, sizeof msgbuf, "%s", msg);
}

static void fail(const char *what, const char *fmt, ...) {
    va_list ap;
    printf("  FAIL %s - ", what);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
    failures++;
}

static void ok(const char *fmt, ...) {
    va_list ap;
    printf("  ok   ");
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
}

static const char *tmpdir(void) {
    const char *d = getenv("JPEG_TEST_TMP");
    return (d && *d) ? d : ".";
}

static void tmppath(char *out, size_t n, const char *name) {
    snprintf(out, n, "%s/%s", tmpdir(), name);
}

static unsigned long long digest(FIBITMAP *dib) {
    unsigned long long h = 1469598103934665603ULL;
    unsigned w = FreeImage_GetWidth(dib), ht = FreeImage_GetHeight(dib);
    unsigned bpp = FreeImage_GetBPP(dib), y;
    size_t row = (size_t)w * (bpp / 8), i;
    const unsigned char *p;
    for (y = 0; y < ht; y++) {
        p = (const unsigned char *)FreeImage_GetScanLine(dib, y);
        for (i = 0; i < row; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    }
    return h;
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

/* ---- quantisation tables actually written ---------------------------- */
/* returns the DQT count; dc[] gets each table's DC entry */
static int read_dqt(const BYTE *d, long n, int *dc, int maxt) {
    long i = 2;
    int found = 0;
    while (i < n - 3 && d[i] == 0xFF) {
        int m = d[i + 1];
        long seg;
        if (m == 0xD9 || m == 0xDA) break;
        seg = (d[i + 2] << 8) | d[i + 3];
        if (m == 0xDB) {
            long p = i + 4, end = i + 2 + seg;
            while (p < end) {
                int pq = d[p] >> 4;
                p++;
                if (found < maxt)
                    dc[found] = pq ? ((d[p] << 8) | d[p + 1]) : d[p];
                found++;
                p += pq ? 128 : 64;
            }
        }
        i += 2 + seg;
    }
    return found;
}

/* DC entry per table at each quality: base 16, clamped to >= 1 */
typedef struct { int quality; int ntables; int dc0, dc1; } QExpect;
static const QExpect QTABLE[] = {
    { 100, 2,  1,  1 },   /* JPEG_QUALITYSUPERB */
    {  90, 2,  3,  3 },
    {  75, 2,  8,  8 },   /* JPEG_QUALITYGOOD */
    {  50, 2, 16, 16 },   /* JPEG_QUALITYNORMAL */
    {  25, 2, 32, 32 },   /* JPEG_QUALITYAVERAGE */
    {  10, 2, 80, 80 },   /* JPEG_QUALITYBAD */
};
#define NQ ((int)(sizeof(QTABLE) / sizeof(QTABLE[0])))

int main(void) {
    FIBITMAP *src, *grey, *pal, *rgba, *cmyk;
    char path[512], path2[512];
    int i;

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(msgproc);
    printf("JPEG round trip and transform test\n\n");

    src = FreeImage_Load(FIF_JPEG, "data/fi_jpeg_444.jpg", JPEG_ACCURATE);
    if (!src) { printf("cannot load data/fi_jpeg_444.jpg\n"); return 1; }
    grey = FreeImage_ConvertToGreyscale(src);
    pal  = FreeImage_ColorQuantize(src, FIQ_WUQUANT);
    rgba = FreeImage_ConvertTo32Bits(src);
    /* the only 32-bit form the plugin accepts */
    cmyk = FreeImage_Load(FIF_JPEG, "data/fi_jpeg_cmyk.jpg", JPEG_CMYK);

    /* --- 1. every exportable depth, file vs memory stream --------------- */
    printf("round trip, file against memory stream\n");
    {
        static const struct { const char *name; int flag; } modes[] = {
            { "QUALITYSUPERB",  JPEG_QUALITYSUPERB },
            { "QUALITYGOOD",    JPEG_QUALITYGOOD },
            { "QUALITYNORMAL",  JPEG_QUALITYNORMAL },
            { "QUALITYAVERAGE", JPEG_QUALITYAVERAGE },
            { "QUALITYBAD",     JPEG_QUALITYBAD },
            { "PROGRESSIVE",    JPEG_PROGRESSIVE },
            { "OPTIMIZE",       JPEG_OPTIMIZE },
            { "BASELINE",       JPEG_BASELINE },
            { "SUBSAMPLING_411", JPEG_SUBSAMPLING_411 },
            { "SUBSAMPLING_420", JPEG_SUBSAMPLING_420 },
            { "SUBSAMPLING_422", JPEG_SUBSAMPLING_422 },
            { "SUBSAMPLING_444", JPEG_SUBSAMPLING_444 },
            { "quality 1",      1 },
            { "quality 99",     99 },
        };
        struct { const char *n; FIBITMAP *d; } srcs[3];
        int s, m;
        srcs[0].n = "24-bit RGB";   srcs[0].d = src;
        srcs[1].n = "8-bit grey";   srcs[1].d = grey;
        srcs[2].n = "8-bit palette"; srcs[2].d = pal;

        for (s = 0; s < 3; s++) {
            if (!srcs[s].d) { fail(srcs[s].n, "source bitmap is NULL"); continue; }
            for (m = 0; m < (int)(sizeof modes / sizeof modes[0]); m++) {
                FIMEMORY *mem;
                FIBITMAP *back;
                BYTE *fbytes = NULL, *mbytes = NULL;
                DWORD msize = 0;
                long fsize = 0;

                tmppath(path, sizeof path, "fi_jpeg_rt_a.jpg");
                if (!FreeImage_Save(FIF_JPEG, srcs[s].d, path, modes[m].flag)) {
                    fail(srcs[s].n, "save (%s) refused: %s", modes[m].name, msgbuf);
                    continue;
                }
                mem = FreeImage_OpenMemory(NULL, 0);
                if (!FreeImage_SaveToMemory(FIF_JPEG, srcs[s].d, mem, modes[m].flag)) {
                    fail(srcs[s].n, "SaveToMemory (%s) refused", modes[m].name);
                    FreeImage_CloseMemory(mem); continue;
                }
                FreeImage_AcquireMemory(mem, &mbytes, &msize);
                fbytes = slurp(path, &fsize);
                if (!fbytes) {
                    fail(srcs[s].n, "could not read back the saved file");
                } else if (fsize != (long)msize || memcmp(fbytes, mbytes, (size_t)fsize) != 0) {
                    fail(srcs[s].n, "%s: file is %ld bytes, memory stream %u, and they differ",
                         modes[m].name, fsize, (unsigned)msize);
                }
                free(fbytes);

                back = FreeImage_Load(FIF_JPEG, path, JPEG_ACCURATE);
                if (!back) {
                    fail(srcs[s].n, "%s: saved file will not load back", modes[m].name);
                } else {
                    if (FreeImage_GetWidth(back) != FreeImage_GetWidth(srcs[s].d) ||
                        FreeImage_GetHeight(back) != FreeImage_GetHeight(srcs[s].d))
                        fail(srcs[s].n, "%s: geometry changed across the round trip",
                             modes[m].name);
                    FreeImage_Unload(back);
                }
                FreeImage_CloseMemory(mem);
            }
            ok("%-14s %d modes through a file and a memory stream, identical bytes",
               srcs[s].n, (int)(sizeof modes / sizeof modes[0]));
        }
    }

    /* --- 2. depths JPEG cannot carry are refused ------------------------ */
    printf("\nunsupported depths\n");
    {
        FIBITMAP *bits1 = FreeImage_Allocate(32, 32, 1, 0, 0, 0);
        FIBITMAP *rgb16 = FreeImage_ConvertTo16Bits565(src);
        FIBITMAP *f32   = FreeImage_ConvertToType(src, FIT_RGBF, TRUE);
        struct { const char *n; FIBITMAP *d; } bad[4];
        int b;
        bad[0].n = "32-bit RGBA"; bad[0].d = rgba;
        bad[1].n = "1-bit";       bad[1].d = bits1;
        bad[2].n = "16-bit 565";  bad[2].d = rgb16;
        bad[3].n = "RGBF";        bad[3].d = f32;
        for (b = 0; b < 4; b++) {
            if (!bad[b].d) continue;
            tmppath(path, sizeof path, "fi_jpeg_rt_bad.jpg");
            msgbuf[0] = 0;
            if (FreeImage_Save(FIF_JPEG, bad[b].d, path, 0))
                fail(bad[b].n, "was saved as JPEG; it should have been refused");
            else
                ok("%-12s refused: %s", bad[b].n, msgbuf[0] ? msgbuf : "(no message)");
        }
        tmppath(path, sizeof path, "fi_jpeg_rt_cmyk.jpg");
        if (!cmyk || FreeImage_GetColorType(cmyk) != FIC_CMYK) {
            fail("32-bit CMYK", "could not build a FIC_CMYK bitmap to save");
        } else if (!FreeImage_Save(FIF_JPEG, cmyk, path, 0)) {
            fail("32-bit CMYK", "refused: %s", msgbuf);
        } else {
            FIBITMAP *back = FreeImage_Load(FIF_JPEG, path, JPEG_CMYK);
            if (!back || FreeImage_GetColorType(back) != FIC_CMYK)
                fail("32-bit CMYK", "did not come back as FIC_CMYK");
            else
                ok("32-bit CMYK accepted and round trips as FIC_CMYK");
            if (back) FreeImage_Unload(back);
        }
        /* a CMYK file's profile: kept with JPEG_CMYK, dropped when the file loads as RGB */
        if (cmyk && FreeImage_GetColorType(cmyk) == FIC_CMYK) {
            FIBITMAP *tagged = FreeImage_Clone(cmyk);
            BYTE icc[132];
            memset(icc, 0, sizeof icc);
            icc[3] = sizeof icc;
            memcpy(icc + 16, "CMYK", 4);
            memcpy(icc + 36, "acsp", 4);
            FreeImage_CreateICCProfile(tagged, icc, sizeof icc);
            tmppath(path, sizeof path, "fi_jpeg_rt_cmyk_icc.jpg");
            if (!FreeImage_Save(FIF_JPEG, tagged, path, 0)) {
                fail("CMYK profile", "a CMYK bitmap with a profile was refused: %s", msgbuf);
            } else {
                FIBITMAP *asCMYK = FreeImage_Load(FIF_JPEG, path, JPEG_CMYK);
                FIBITMAP *asRGB = FreeImage_Load(FIF_JPEG, path, JPEG_DEFAULT);
                FIBITMAP *header = FreeImage_Load(FIF_JPEG, path, FIF_LOAD_NOPIXELS);
                FIICCPROFILE *p = asCMYK ? FreeImage_GetICCProfile(asCMYK) : NULL;
                if (!p || p->size != sizeof icc || !(p->flags & FIICC_COLOR_IS_CMYK))
                    fail("CMYK profile", "JPEG_CMYK did not keep the profile, flagged CMYK");
                else if (!asRGB || FreeImage_GetBPP(asRGB) != 24 || FreeImage_GetICCProfile(asRGB)->size)
                    fail("CMYK profile", "the file loaded as RGB still carries the CMYK profile");
                else if (!header || FreeImage_GetICCProfile(header)->size)
                    fail("CMYK profile", "a header-only load still carries the CMYK profile");
                else
                    ok("CMYK profile kept with JPEG_CMYK, dropped when the file loads as RGB");
                if (asCMYK) FreeImage_Unload(asCMYK);
                if (asRGB) FreeImage_Unload(asRGB);
                if (header) FreeImage_Unload(header);
            }
            FreeImage_Unload(tagged);
        }
        if (bits1) FreeImage_Unload(bits1);
        if (rgb16) FreeImage_Unload(rgb16);
        if (f32) FreeImage_Unload(f32);
    }

    /* --- 3. the quantisation tables actually emitted -------------------- */
    printf("\nquantisation tables written (the libjpeg 10 change)\n");
    for (i = 0; i < NQ; i++) {
        BYTE *b; long n; int dc[8], got;
        tmppath(path, sizeof path, "fi_jpeg_rt_q.jpg");
        if (!FreeImage_Save(FIF_JPEG, src, path, QTABLE[i].quality)) {
            fail("quant tables", "save at quality %d refused", QTABLE[i].quality);
            continue;
        }
        b = slurp(path, &n);
        if (!b) { fail("quant tables", "could not read back the file"); continue; }
        got = read_dqt(b, n, dc, 8);
        if (got != QTABLE[i].ntables)
            fail("quant tables", "quality %d: %d tables, expected %d",
                 QTABLE[i].quality, got, QTABLE[i].ntables);
        else if (dc[0] != QTABLE[i].dc0 || dc[1] != QTABLE[i].dc1)
            fail("quant tables", "quality %d: DC entries %d/%d, expected %d/%d",
                 QTABLE[i].quality, dc[0], dc[1], QTABLE[i].dc0, QTABLE[i].dc1);
        else
            ok("quality %3d: %d tables, DC entries %d (luma) %d (chroma)",
               QTABLE[i].quality, got, dc[0], dc[1]);
        free(b);
    }
    {
        BYTE *b; long n; int dc[8], got;
        tmppath(path, sizeof path, "fi_jpeg_rt_g.jpg");
        if (grey && FreeImage_Save(FIF_JPEG, grey, path, 75)) {
            b = slurp(path, &n);
            if (b) {
                got = read_dqt(b, n, dc, 8);
                if (got != 1)
                    fail("quant tables", "a greyscale save wrote %d tables, expected 1", got);
                else
                    ok("greyscale at quality 75: 1 table, DC entry %d - no chrominance "
                       "table, which is why greyscale output did not move", dc[0]);
                free(b);
            }
        }
    }

    /* --- 4. the lossless transforms are lossless ------------------------ */
    printf("\nlossless transforms\n");
    {
        static const struct { const char *n; int op; int inv; } pairs[] = {
            { "FLIP_H",     FIJPEG_OP_FLIP_H,     FIJPEG_OP_FLIP_H },
            { "FLIP_V",     FIJPEG_OP_FLIP_V,     FIJPEG_OP_FLIP_V },
            { "TRANSPOSE",  FIJPEG_OP_TRANSPOSE,  FIJPEG_OP_TRANSPOSE },
            { "TRANSVERSE", FIJPEG_OP_TRANSVERSE, FIJPEG_OP_TRANSVERSE },
            { "ROTATE_180", FIJPEG_OP_ROTATE_180, FIJPEG_OP_ROTATE_180 },
            { "ROTATE_90",  FIJPEG_OP_ROTATE_90,  FIJPEG_OP_ROTATE_270 },
            { "ROTATE_270", FIJPEG_OP_ROTATE_270, FIJPEG_OP_ROTATE_90 },
        };
        /* 256x192 is whole 4:2:0 MCUs, 33x17 is not */
        static const char *ALIGNED = "data/fi_jpeg_420.jpg";
        static const char *RAGGED  = "data/fi_jpeg_odd.jpg";
        int p2;

        /* On an MCU-aligned image every operation is exact, in both modes. */
        {
            FIBITMAP *orig = FreeImage_Load(FIF_JPEG, ALIGNED, JPEG_ACCURATE);
            int done = 0, refused = 0;
            unsigned long long h0 = orig ? digest(orig) : 0;
            if (!orig) fail("transform", "cannot load %s", ALIGNED);
            for (p2 = 0; orig && p2 < (int)(sizeof pairs / sizeof pairs[0]); p2++) {
                FIBITMAP *back;
                tmppath(path,  sizeof path,  "fi_jpeg_xf_1.jpg");
                tmppath(path2, sizeof path2, "fi_jpeg_xf_2.jpg");
                if (!FreeImage_JPEGTransform(ALIGNED, path, pairs[p2].op, TRUE)) {
                    refused++;
                    continue;
                }
                if (!FreeImage_JPEGTransform(path, path2, pairs[p2].inv, TRUE)) {
                    fail("transform", "%s: inverse of %s refused", ALIGNED, pairs[p2].n);
                    continue;
                }
                back = FreeImage_Load(FIF_JPEG, path2, JPEG_ACCURATE);
                if (!back) {
                    fail("transform", "%s: %s round trip will not load", ALIGNED, pairs[p2].n);
                    continue;
                }
                if (FreeImage_GetWidth(back) != FreeImage_GetWidth(orig) ||
                    FreeImage_GetHeight(back) != FreeImage_GetHeight(orig))
                    fail("transform", "%s: %s and back changed the geometry",
                         ALIGNED, pairs[p2].n);
                else if (digest(back) != h0)
                    fail("transform", "%s: %s and back is not lossless", ALIGNED, pairs[p2].n);
                else
                    done++;
                FreeImage_Unload(back);
            }
            if (refused)
                fail("transform", "%s is MCU aligned, so perfect=TRUE should refuse "
                     "nothing, but it refused %d", ALIGNED, refused);
            else
                ok("%-22s %d of %d operations exact both ways, perfect=TRUE refuses none",
                   ALIGNED, done, (int)(sizeof pairs / sizeof pairs[0]));
            if (orig) FreeImage_Unload(orig);
        }

        /* ragged: TRUE refuses all but TRANSPOSE; FALSE trims once, then exact */
        {
            FIBITMAP *a, *b;
            int refused = 0, accepted = 0, trimmed = 0, stable = 0;
            for (p2 = 0; p2 < (int)(sizeof pairs / sizeof pairs[0]); p2++) {
                tmppath(path, sizeof path, "fi_jpeg_xf_3.jpg");
                if (FreeImage_JPEGTransform(RAGGED, path, pairs[p2].op, TRUE)) {
                    accepted++;
                    if (pairs[p2].op != FIJPEG_OP_TRANSPOSE)
                        fail("transform", "%s: perfect=TRUE accepted %s, which cannot "
                             "be exact on a ragged image", RAGGED, pairs[p2].n);
                } else {
                    refused++;
                    if (pairs[p2].op == FIJPEG_OP_TRANSPOSE)
                        fail("transform", "%s: perfect=TRUE refused TRANSPOSE, which "
                             "needs no alignment", RAGGED);
                }
            }
            ok("%-22s perfect=TRUE accepts %d (TRANSPOSE) and refuses %d",
               RAGGED, accepted, refused);

            for (p2 = 0; p2 < (int)(sizeof pairs / sizeof pairs[0]); p2++) {
                char path3[512], path4[512];
                tmppath(path,  sizeof path,  "fi_jpeg_xf_a.jpg");
                tmppath(path2, sizeof path2, "fi_jpeg_xf_b.jpg");
                tmppath(path3, sizeof path3, "fi_jpeg_xf_c.jpg");
                tmppath(path4, sizeof path4, "fi_jpeg_xf_d.jpg");
                /* first pass: operation and inverse, trimming allowed */
                if (!FreeImage_JPEGTransform(RAGGED, path, pairs[p2].op, FALSE) ||
                    !FreeImage_JPEGTransform(path, path2, pairs[p2].inv, FALSE)) {
                    fail("transform", "%s: %s refused with perfect=FALSE",
                         RAGGED, pairs[p2].n);
                    continue;
                }
                /* second pass over the already trimmed result */
                if (!FreeImage_JPEGTransform(path2, path3, pairs[p2].op, FALSE) ||
                    !FreeImage_JPEGTransform(path3, path4, pairs[p2].inv, FALSE)) {
                    fail("transform", "%s: second %s refused", RAGGED, pairs[p2].n);
                    continue;
                }
                a = FreeImage_Load(FIF_JPEG, path2, JPEG_ACCURATE);
                b = FreeImage_Load(FIF_JPEG, path4, JPEG_ACCURATE);
                if (!a || !b) {
                    fail("transform", "%s: %s output will not load", RAGGED, pairs[p2].n);
                } else {
                    if (FreeImage_GetWidth(a) > 33 || FreeImage_GetHeight(a) > 17)
                        fail("transform", "%s: %s grew the image to %ux%u",
                             RAGGED, pairs[p2].n, FreeImage_GetWidth(a), FreeImage_GetHeight(a));
                    else if (FreeImage_GetWidth(a) < 33 || FreeImage_GetHeight(a) < 17)
                        trimmed++;
                    if (FreeImage_GetWidth(a) != FreeImage_GetWidth(b) ||
                        FreeImage_GetHeight(a) != FreeImage_GetHeight(b))
                        fail("transform", "%s: %s kept trimming after the first pass "
                             "(%ux%u then %ux%u)", RAGGED, pairs[p2].n,
                             FreeImage_GetWidth(a), FreeImage_GetHeight(a),
                             FreeImage_GetWidth(b), FreeImage_GetHeight(b));
                    else if (digest(a) != digest(b))
                        fail("transform", "%s: %s is not exact once the image is "
                             "MCU aligned", RAGGED, pairs[p2].n);
                    else
                        stable++;
                }
                if (a) FreeImage_Unload(a);
                if (b) FreeImage_Unload(b);
            }
            ok("%-22s perfect=FALSE trims %d of %d to the MCU grid once, then %d "
               "of %d are exact", RAGGED, trimmed,
               (int)(sizeof pairs / sizeof pairs[0]), stable,
               (int)(sizeof pairs / sizeof pairs[0]));
        }

        tmppath(path, sizeof path, "fi_jpeg_xf_crop.jpg");
        if (!FreeImage_JPEGCrop("data/fi_jpeg_420.jpg", path, 16, 16, 144, 112)) {
            fail("crop", "FreeImage_JPEGCrop refused");
        } else {
            FIBITMAP *c = FreeImage_Load(FIF_JPEG, path, 0);
            if (!c) fail("crop", "the cropped file will not load");
            else {
                if (FreeImage_GetWidth(c) != 128 || FreeImage_GetHeight(c) != 96)
                    fail("crop", "got %ux%u, expected 128x96",
                         FreeImage_GetWidth(c), FreeImage_GetHeight(c));
                else
                    ok("JPEGCrop 16,16..144,112 -> 128x96");
                FreeImage_Unload(c);
            }
        }
    }

    /* --- 5. metadata across a save -------------------------------------- */
    printf("\nmetadata across a save\n");
    {
        FIBITMAP *m = FreeImage_Load(FIF_JPEG, "data/fi_jpeg_meta.jpg", 0);
        if (!m) {
            fail("metadata", "cannot load data/fi_jpeg_meta.jpg");
        } else {
            FIBITMAP *back;
            FIICCPROFILE *icc;
            tmppath(path, sizeof path, "fi_jpeg_rt_meta.jpg");
            if (!FreeImage_Save(FIF_JPEG, m, path, JPEG_QUALITYGOOD)) {
                fail("metadata", "save refused");
            } else {
                back = FreeImage_Load(FIF_JPEG, path, 0);
                if (!back) fail("metadata", "the saved file will not load");
                else {
                    icc = FreeImage_GetICCProfile(back);
                    if (!icc || icc->size == 0)
                        fail("metadata", "the ICC profile did not survive the save");
                    else if (FreeImage_GetMetadataCount(FIMD_EXIF_MAIN, back) == 0)
                        fail("metadata", "the EXIF tags did not survive the save");
                    else
                        ok("default save keeps ICC (%u bytes) and %u EXIF_MAIN tags",
                           icc->size, FreeImage_GetMetadataCount(FIMD_EXIF_MAIN, back));
                    FreeImage_Unload(back);
                }
            }
            tmppath(path, sizeof path, "fi_jpeg_rt_base.jpg");
            if (FreeImage_Save(FIF_JPEG, m, path, JPEG_BASELINE)) {
                back = FreeImage_Load(FIF_JPEG, path, 0);
                if (back) {
                    icc = FreeImage_GetICCProfile(back);
                    if (icc && icc->size)
                        fail("metadata", "JPEG_BASELINE kept the ICC profile");
                    else
                        ok("JPEG_BASELINE drops the markers, as documented");
                    FreeImage_Unload(back);
                }
            }
            FreeImage_Unload(m);
        }
    }

    if (src) FreeImage_Unload(src);
    if (grey) FreeImage_Unload(grey);
    if (pal) FreeImage_Unload(pal);
    if (rgba) FreeImage_Unload(rgba);
    if (cmyk) FreeImage_Unload(cmyk);

    printf("\n--- %d failure(s) ---\n", failures);
    FreeImage_DeInitialise();
    return failures ? 1 : 0;
}
