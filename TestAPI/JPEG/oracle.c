/* JPEG oracle, not a test: run one build per library and diff the reports */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include "FreeImage.h"

static char msgbuf[8192];
static size_t msglen;

static void DLL_CALLCONV msgproc(FREE_IMAGE_FORMAT fif, const char *msg) {
    (void)fif;
    if (msglen + strlen(msg) + 4 < sizeof msgbuf)
        msglen += sprintf(msgbuf + msglen, "[%s]", msg);
}

static unsigned long long fnv(const void *p, size_t n, unsigned long long h) {
    const unsigned char *b = (const unsigned char *)p;
    while (n--) { h ^= *b++; h *= 1099511628211ULL; }
    return h;
}

static unsigned long long dibhash(FIBITMAP *d) {
    unsigned long long h = 14695981039346656037ULL;
    unsigned y, H = FreeImage_GetHeight(d), line = FreeImage_GetLine(d);
    for (y = 0; y < H; y++)
        h = fnv(FreeImage_GetScanLine(d, y), line, h);
    if (FreeImage_GetPalette(d))
        h = fnv(FreeImage_GetPalette(d), FreeImage_GetColorsUsed(d) * 4, h);
    return h;
}

static void describe(const char *tag, FIBITMAP *d) {
    if (!d) { printf("%-44s NULL %s\n", tag, msgbuf); return; }
    printf("%-44s %4ux%-4u bpp=%-2u type=%d ct=%d tr=%d hash=%016llx %s\n",
           tag, FreeImage_GetWidth(d), FreeImage_GetHeight(d),
           FreeImage_GetBPP(d), (int)FreeImage_GetImageType(d),
           (int)FreeImage_GetColorType(d), FreeImage_IsTransparent(d),
           dibhash(d), msgbuf);
}

struct flagrow { const char *name; int flag; };

static struct flagrow loadflags[] = {
    { "default",   JPEG_DEFAULT },
    { "fast",      JPEG_FAST },
    { "accurate",  JPEG_ACCURATE },
    { "cmyk",      JPEG_CMYK },
    { "greyscale", JPEG_GREYSCALE },
    { "exifrot",   JPEG_EXIFROTATE },
    { "acc|cmyk",  JPEG_ACCURATE | JPEG_CMYK },
};

static struct flagrow saveflags[] = {
    { "q100",        JPEG_QUALITYSUPERB },
    { "good",        JPEG_QUALITYGOOD },
    { "normal",      JPEG_QUALITYNORMAL },
    { "average",     JPEG_QUALITYAVERAGE },
    { "bad",         JPEG_QUALITYBAD },
    { "default",     JPEG_DEFAULT },
    { "prog",        JPEG_PROGRESSIVE },
    { "s411",        JPEG_SUBSAMPLING_411 },
    { "s420",        JPEG_SUBSAMPLING_420 },
    { "s422",        JPEG_SUBSAMPLING_422 },
    { "s444",        JPEG_SUBSAMPLING_444 },
    { "opt",         JPEG_OPTIMIZE },
    { "baseline",    JPEG_BASELINE },
    { "q100s444",    JPEG_QUALITYSUPERB | JPEG_SUBSAMPLING_444 },
    { "q100s444opt", JPEG_QUALITYSUPERB | JPEG_SUBSAMPLING_444 | JPEG_OPTIMIZE },
    { "q100progopt", JPEG_QUALITYSUPERB | JPEG_PROGRESSIVE | JPEG_OPTIMIZE },
    { "q90",         90 },
    { "q30",         30 },
};

static int cmpstr(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static BYTE filebuf[16u << 20];

static long slurp_into_filebuf(const char *path) {
    FILE *fp = fopen(path, "rb");
    long n;
    if (!fp) return -1;
    n = (long)fread(filebuf, 1, sizeof filebuf, fp);
    fclose(fp);
    return n;
}

static void do_decode(const char *dir) {
    char *names[512];
    int n = 0, i;
    DIR *d = opendir(dir);
    struct dirent *e;
    if (!d) { printf("cannot open %s\n", dir); return; }
    while ((e = readdir(d)) != NULL) {
        size_t l = strlen(e->d_name);
        if (l > 4 && n < 512 &&
            (strcmp(e->d_name + l - 4, ".jpg") == 0 ||
             strcmp(e->d_name + l - 5, ".jpeg") == 0))
            names[n++] = strdup(e->d_name);
    }
    closedir(d);
    qsort(names, n, sizeof names[0], cmpstr);

    for (i = 0; i < n; i++) {
        char path[1024], tag[1200];
        unsigned f;
        long got;
        sprintf(path, "%s/%s", dir, names[i]);
        msgbuf[0] = 0; msglen = 0;
        printf("%-44s fif=%d\n", names[i], (int)FreeImage_GetFileType(path, 0));
        for (f = 0; f < sizeof loadflags / sizeof loadflags[0]; f++) {
            FIBITMAP *b;
            sprintf(tag, "  %s [%s]", names[i], loadflags[f].name);
            msgbuf[0] = 0; msglen = 0;
            b = FreeImage_Load(FIF_JPEG, path, loadflags[f].flag);
            describe(tag, b);
            if (b) {
                FIICCPROFILE *icc = FreeImage_GetICCProfile(b);
                sprintf(tag, "  %s [%s] md", names[i], loadflags[f].name);
                printf("%-44s exif=%u main=%u iptc=%u xmp=%u icc=%u\n", tag,
                       FreeImage_GetMetadataCount(FIMD_EXIF_EXIF, b),
                       FreeImage_GetMetadataCount(FIMD_EXIF_MAIN, b),
                       FreeImage_GetMetadataCount(FIMD_IPTC, b),
                       FreeImage_GetMetadataCount(FIMD_XMP, b),
                       icc ? icc->size : 0);
                FreeImage_Unload(b);
            }
        }
        msgbuf[0] = 0; msglen = 0;
        got = slurp_into_filebuf(path);
        if (got >= 0) {
            FIMEMORY *m = FreeImage_OpenMemory(filebuf, (DWORD)got);
            FIBITMAP *b = FreeImage_LoadFromMemory(FIF_JPEG, m, 0);
            sprintf(tag, "  %s [memory]", names[i]);
            describe(tag, b);
            if (b) FreeImage_Unload(b);
            FreeImage_CloseMemory(m);
        }
        free(names[i]);
    }
}

static void do_encode(const char *srcimg, const char *outdir) {
    FIBITMAP *rgb = FreeImage_Load(FreeImage_GetFileType(srcimg, 0), srcimg, 0);
    FIBITMAP *gr8, *rgb32;
    struct { const char *n; FIBITMAP *d; } srcs[3];
    unsigned s, f;

    if (!rgb) { printf("ENCODE: cannot load %s\n", srcimg); return; }
    gr8 = FreeImage_ConvertToGreyscale(rgb);
    rgb32 = FreeImage_ConvertTo32Bits(rgb);
    srcs[0].n = "rgb24";  srcs[0].d = rgb;
    srcs[1].n = "grey8";  srcs[1].d = gr8;
    srcs[2].n = "rgba32"; srcs[2].d = rgb32;

    for (s = 0; s < 3; s++) {
        for (f = 0; f < sizeof saveflags / sizeof saveflags[0]; f++) {
            char out[1024];
            BOOL okf;
            unsigned long long h = 0;
            long sz = -1;
            FIBITMAP *rt;
            sprintf(out, "%s/%s_%s.jpg", outdir, srcs[s].n, saveflags[f].name);
            msgbuf[0] = 0; msglen = 0;
            okf = FreeImage_Save(FIF_JPEG, srcs[s].d, out, saveflags[f].flag);
            if (okf) {
                sz = slurp_into_filebuf(out);
                if (sz >= 0) h = fnv(filebuf, (size_t)sz, 14695981039346656037ULL);
            }
            printf("SAVE %-7s %-12s ok=%d size=%-8ld filehash=%016llx %s\n",
                   srcs[s].n, saveflags[f].name, (int)okf, sz, h, msgbuf);
            rt = okf ? FreeImage_Load(FIF_JPEG, out, 0) : NULL;
            if (rt) {
                printf("  RT %-7s %-12s %ux%u bpp=%u hash=%016llx\n",
                       srcs[s].n, saveflags[f].name, FreeImage_GetWidth(rt),
                       FreeImage_GetHeight(rt), FreeImage_GetBPP(rt), dibhash(rt));
                FreeImage_Unload(rt);
            }
        }
    }
    FreeImage_Unload(gr8); FreeImage_Unload(rgb32); FreeImage_Unload(rgb);
}

static void do_transform(const char *src, const char *outdir) {
    static const struct { const char *n; int op; } ops[] = {
        { "none",       FIJPEG_OP_NONE },
        { "fliph",      FIJPEG_OP_FLIP_H },
        { "flipv",      FIJPEG_OP_FLIP_V },
        { "transpose",  FIJPEG_OP_TRANSPOSE },
        { "transverse", FIJPEG_OP_TRANSVERSE },
        { "rot90",      FIJPEG_OP_ROTATE_90 },
        { "rot180",     FIJPEG_OP_ROTATE_180 },
        { "rot270",     FIJPEG_OP_ROTATE_270 },
    };
    unsigned p, i;
    for (p = 0; p < 2; p++) {
        for (i = 0; i < sizeof ops / sizeof ops[0]; i++) {
            char out[1024];
            BOOL okf;
            unsigned long long h = 0;
            long sz = -1;
            sprintf(out, "%s/tr_%s_%s.jpg", outdir, ops[i].n, p ? "perfect" : "loose");
            msgbuf[0] = 0; msglen = 0;
            okf = FreeImage_JPEGTransform(src, out,
                      (FREE_IMAGE_JPEG_OPERATION)ops[i].op, p ? TRUE : FALSE);
            if (okf) {
                sz = slurp_into_filebuf(out);
                if (sz >= 0) h = fnv(filebuf, (size_t)sz, 14695981039346656037ULL);
            }
            printf("XFORM %-11s %-8s ok=%d size=%-8ld filehash=%016llx %s\n",
                   ops[i].n, p ? "perfect" : "loose", (int)okf, sz, h, msgbuf);
        }
    }
}

int main(int argc, char **argv) {
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(msgproc);
    if (argc > 2 && strcmp(argv[1], "decode") == 0)         do_decode(argv[2]);
    else if (argc > 3 && strcmp(argv[1], "encode") == 0)    do_encode(argv[2], argv[3]);
    else if (argc > 3 && strcmp(argv[1], "transform") == 0) do_transform(argv[2], argv[3]);
    else printf("usage: oracle decode <dir> | encode <src> <outdir> | "
                "transform <src.jpg> <outdir>\n");
    FreeImage_DeInitialise();
    return 0;
}
