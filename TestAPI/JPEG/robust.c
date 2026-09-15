/*
 * FreeImage 3 - JPEG robustness test
 *
 * Feeds damaged JPEG data to the plugin and to the lossless transforms. Every
 * input here may load or be refused; none of them may crash, read out of
 * bounds or leak. A plain run only catches the crashes - the other two are
 * the sanitizer's to find, so run it under AddressSanitizer: "make asan-run" in this
 * directory rebuilds Source/LibJPEG, PluginJPEG.cpp and JPEGTransform.cpp with
 * the sanitizer into a private copy of the library - which is the point of the
 * test: a JPEG decoder is reachable from untrusted input in every program that
 * links FreeImage, and libjpeg 10 rewrote both DCTs and most of the decoder's
 * sample handling.
 *
 * The damage patterns are the ones that historically break JPEG readers:
 *   - truncation, which leaves the entropy decoder short of data mid-MCU;
 *   - junk appended past EOI;
 *   - single byte corruptions, which mostly land in the entropy stream but
 *     sometimes rewrite a marker or a table entry;
 *   - wiped regions, which take out whole tables and headers;
 *   - rewritten segment length fields, the classic way to make a parser walk
 *     off the end of a buffer;
 *   - the dimensions in SOF set to zero or to values that would overflow a
 *     size computation.
 *
 * Everything is decoded from memory so that nothing touches the filesystem.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static const char *FILES[] = {
    "data/fi_jpeg_420.jpg",
    "data/fi_jpeg_444.jpg",
    "data/fi_jpeg_progressive.jpg",
    "data/fi_jpeg_arithmetic.jpg",
    "data/fi_jpeg_grey.jpg",
    "data/fi_jpeg_cmyk.jpg",
    "data/fi_jpeg_meta.jpg",
    "data/fi_jpeg_odd.jpg",
};
#define NFILES ((int)(sizeof(FILES) / sizeof(FILES[0])))

static long loaded = 0, refused = 0, cases = 0;

static void DLL_CALLCONV quiet(FREE_IMAGE_FORMAT fif, const char *msg) {
    (void)fif; (void)msg;
}

/* Decode one damaged buffer with every load flag that reaches different code. */
static void try_buffer(const BYTE *buf, long len) {
    static const int flags[] = {
        JPEG_DEFAULT, JPEG_ACCURATE, JPEG_CMYK, JPEG_GREYSCALE,
        JPEG_EXIFROTATE, FIF_LOAD_NOPIXELS
    };
    unsigned f;
    for (f = 0; f < sizeof flags / sizeof flags[0]; f++) {
        FIMEMORY *m = FreeImage_OpenMemory((BYTE *)buf, (DWORD)len);
        FIBITMAP *d;
        if (!m) return;
        /* format detection runs on the damaged bytes too */
        FreeImage_GetFileTypeFromMemory(m, 0);
        FreeImage_SeekMemory(m, 0, SEEK_SET);
        d = FreeImage_LoadFromMemory(FIF_JPEG, m, flags[f]);
        cases++;
        if (d) { loaded++; FreeImage_Unload(d); } else refused++;
        FreeImage_CloseMemory(m);
    }
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

int main(void) {
    int i;
    BYTE *work;

    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);
    printf("JPEG robustness test - %d files\n\n", NFILES);

    for (i = 0; i < NFILES; i++) {
        long len, n;
        BYTE *orig = slurp(FILES[i], &len);
        long before = cases;
        if (!orig) { printf("  cannot read %s\n", FILES[i]); continue; }
        work = (BYTE *)malloc((size_t)len + 4096);
        if (!work) { free(orig); continue; }

        /* 1. every truncation, on a stride that keeps the run bounded */
        for (n = 1; n < len; n += (len / 200) + 1)
            try_buffer(orig, n);
        try_buffer(orig, len - 1);
        try_buffer(orig, 2);
        try_buffer(orig, 1);
        try_buffer(orig, 0);

        /* 2. junk appended after EOI */
        memcpy(work, orig, (size_t)len);
        memset(work + len, 0xA5, 4096);
        try_buffer(work, len + 4096);
        memset(work + len, 0xFF, 4096);
        try_buffer(work, len + 4096);

        /* 3. single byte corruptions */
        for (n = 0; n < len; n += (len / 300) + 1) {
            memcpy(work, orig, (size_t)len);
            work[n] ^= 0xFF;
            try_buffer(work, len);
            memcpy(work, orig, (size_t)len);
            work[n] = 0xFF;
            try_buffer(work, len);
            memcpy(work, orig, (size_t)len);
            work[n] = 0x00;
            try_buffer(work, len);
        }

        /* 4. 64-byte regions wiped: takes out whole tables and headers */
        for (n = 0; n + 64 <= len; n += (len / 60) + 1) {
            memcpy(work, orig, (size_t)len);
            memset(work + n, 0, 64);
            try_buffer(work, len);
        }

        /* 5. rewritten segment lengths: walk the marker chain and give each
              segment a nonsense length, both far too big and far too small */
        {
            long p = 2;
            while (p < len - 3 && orig[p] == 0xFF) {
                int m = orig[p + 1];
                long seg;
                if (m == 0xD9 || m == 0xDA) break;
                seg = (orig[p + 2] << 8) | orig[p + 3];
                memcpy(work, orig, (size_t)len);
                work[p + 2] = 0xFF; work[p + 3] = 0xFF;   /* 65535 */
                try_buffer(work, len);
                memcpy(work, orig, (size_t)len);
                work[p + 2] = 0x00; work[p + 3] = 0x00;   /* 0 */
                try_buffer(work, len);
                memcpy(work, orig, (size_t)len);
                work[p + 2] = 0x00; work[p + 3] = 0x01;   /* 1 */
                try_buffer(work, len);
                if (seg <= 0) break;
                p += 2 + seg;
            }
        }

        /* 6. the dimensions in SOF: zero, and large enough that width*height
              *bytes overflows a 32-bit computation */
        {
            long p = 2;
            while (p < len - 9 && orig[p] == 0xFF) {
                int m = orig[p + 1];
                long seg;
                if (m == 0xD9 || m == 0xDA) break;
                seg = (orig[p + 2] << 8) | orig[p + 3];
                if (m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC) {
                    static const unsigned short dims[][2] = {
                        { 0, 0 }, { 0, 192 }, { 256, 0 },
                        { 0xFFFF, 0xFFFF }, { 0xFFFF, 1 }, { 1, 0xFFFF }
                    };
                    unsigned k;
                    for (k = 0; k < sizeof dims / sizeof dims[0]; k++) {
                        memcpy(work, orig, (size_t)len);
                        work[p + 5] = (BYTE)(dims[k][1] >> 8);  /* height */
                        work[p + 6] = (BYTE)(dims[k][1] & 0xFF);
                        work[p + 7] = (BYTE)(dims[k][0] >> 8);  /* width  */
                        work[p + 8] = (BYTE)(dims[k][0] & 0xFF);
                        try_buffer(work, len);
                    }
                    /* and a component count of zero and of the maximum */
                    memcpy(work, orig, (size_t)len);
                    work[p + 9] = 0;
                    try_buffer(work, len);
                    memcpy(work, orig, (size_t)len);
                    work[p + 9] = 0xFF;
                    try_buffer(work, len);
                    break;
                }
                if (seg <= 0) break;
                p += 2 + seg;
            }
        }

        printf("  %-32s %ld cases\n", FILES[i], cases - before);
        free(work);
        free(orig);
    }

    /* 7. buffers that are not JPEG at all, and degenerate ones */
    {
        static const BYTE soi_only[]  = { 0xFF, 0xD8 };
        static const BYTE soi_eoi[]   = { 0xFF, 0xD8, 0xFF, 0xD9 };
        static const BYTE all_ff[64]  = { 0 };
        static const BYTE zeros[64]   = { 0 };
        BYTE ff[64];
        memset(ff, 0xFF, sizeof ff);
        try_buffer(soi_only, sizeof soi_only);
        try_buffer(soi_eoi, sizeof soi_eoi);
        try_buffer(ff, sizeof ff);
        try_buffer(zeros, sizeof zeros);
        try_buffer(all_ff, sizeof all_ff);
        try_buffer((const BYTE *)"not a jpeg at all", 17);
        printf("  %-32s done\n", "degenerate buffers");
    }

    /* 8. the lossless transform path over damaged input: transupp.c parses the
          coefficient arrays itself, so it has its own exposure */
    {
        long len;
        BYTE *orig = slurp("data/fi_jpeg_420.jpg", &len);
        int n, ops[] = { FIJPEG_OP_FLIP_H, FIJPEG_OP_ROTATE_90, FIJPEG_OP_TRANSPOSE };
        long tried = 0;
        if (orig) {
            BYTE *w = (BYTE *)malloc((size_t)len);
            for (n = 0; n < 3; n++) {
                long cut;
                for (cut = 64; cut < len; cut += len / 40 + 1) {
                    FIMEMORY *in, *out;
                    memcpy(w, orig, (size_t)cut);
                    in = FreeImage_OpenMemory(w, (DWORD)cut);
                    out = FreeImage_OpenMemory(NULL, 0);
                    FreeImage_JPEGTransformCombinedFromMemory(
                        in, out, (FREE_IMAGE_JPEG_OPERATION)ops[n],
                        NULL, NULL, NULL, NULL, FALSE);
                    tried++;
                    FreeImage_CloseMemory(in);
                    FreeImage_CloseMemory(out);
                }
            }
            free(w);
            free(orig);
            printf("  %-32s %ld truncated transforms\n", "JPEGTransform", tried);
        }
    }

    printf("\n%ld damaged inputs: %ld decoded, %ld refused, 0 crashed\nsurvived\n",
           cases, loaded, refused);
    FreeImage_DeInitialise();
    return 0;
}
