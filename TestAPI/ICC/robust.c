/* damaged profiles as the embedded, destination and proofing profile: refused or used, never a crash */
/* ./robust [damaged copies per profile], default 100 */
#include "common.h"

static FIBITMAP *small_image(FREE_IMAGE_TYPE type, int bpp, int w, int h, int cmyk) {
    FIBITMAP *dib = FreeImage_AllocateT(type, w, h, bpp, 0, 0, 0);
    unsigned y, x;
    if (!dib) return NULL;
    for (y = 0; y < (unsigned)h; y++) {
        BYTE *p = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < FreeImage_GetLine(dib); x++) p[x] = rnd() & 255;
    }
    if (type == FIT_FLOAT || type == FIT_RGBF || type == FIT_RGBAF) {
        for (y = 0; y < (unsigned)h; y++) {
            float *f = (float *)FreeImage_GetScanLine(dib, y);
            for (x = 0; x < FreeImage_GetLine(dib) / 4; x++) f[x] = (rnd() % 1000) / 999.0f;
        }
    }
    if (bpp == 8 && type == FIT_BITMAP) {
        RGBQUAD *pal = FreeImage_GetPalette(dib);
        for (x = 0; x < 256; x++) pal[x].rgbRed = pal[x].rgbGreen = pal[x].rgbBlue = (BYTE)x;
    }
    if (cmyk) FreeImage_GetICCProfile(dib)->flags |= FIICC_COLOR_IS_CMYK;
    return dib;
}

static long used = 0, refused = 0;

static void count(FIBITMAP *dib) {
    if (dib) { used++; FreeImage_Unload(dib); } else refused++;
}

/* every entry point with this profile */
/* gamut: also a soft proof with a gamut check, which costs Little CMS a table of about 0.1-0.3 s */
static void exercise(const BYTE *p, DWORD size, int gamut) {
    static const struct { FREE_IMAGE_TYPE type; int bpp; int cmyk; } IMAGES[] = {
        { FIT_BITMAP, 24, 0 }, { FIT_BITMAP, 32, 1 }, { FIT_BITMAP, 8, 0 }, { FIT_RGB16, 48, 0 }, { FIT_RGBA16, 64, 1 }, { FIT_RGBF, 96, 0 }, { FIT_UINT16, 16, 0 }
    };
    char desc[64];
    unsigned i;
    FreeImage_GetICCProfileDescription(p, size, desc, sizeof(desc));
    FreeImage_GetICCProfileColorSpace(p, size);
    for (i = 0; i < sizeof(IMAGES) / sizeof(IMAGES[0]); i++) {
        int w = 1 + rnd() % 7, h = 1 + rnd() % 5;
        FIBITMAP *dib = small_image(IMAGES[i].type, IMAGES[i].bpp, w, h, IMAGES[i].cmyk), *copy;
        if (!dib) continue;
        /* as the destination and the proof */
        count(FreeImage_ConvertToICCProfile(dib, p, size, rnd() % 4));
        count(FreeImage_SoftProof(dib, p, size, NULL, 0, FICMS_SIMULATE_PAPER));
        if (gamut && i == 0) count(FreeImage_SoftProof(dib, p, size, NULL, 0, FICMS_GAMUT_CHECK));
        if (IMAGES[i].cmyk) count(FreeImage_ConvertCMYKToRGB(dib, p, size, 0));
        else count(FreeImage_ConvertToCMYK(dib, p, size, 0));
        copy = FreeImage_Clone(dib);
        FreeImage_ApplyICCProfile(copy, p, size, FICMS_BLACKPOINT_COMPENSATION);
        FreeImage_Unload(copy);
        /* as the embedded profile */
        FreeImage_CreateICCProfile(dib, (void *)p, (long)size);
        count(FreeImage_ConvertToICCProfile(dib, NULL, 0, 0));
        count(IMAGES[i].cmyk ? FreeImage_ConvertCMYKToRGB(dib, NULL, 0, 0) : FreeImage_ConvertToCMYK(dib, NULL, 0, 0));
        FreeImage_ApplyICCProfile(dib, NULL, 0, 0);
        FreeImage_Unload(dib);
    }
}

static void mutate(const Bytes *base, BYTE *out, DWORD *size) {
    DWORD n = base->size, i, k;
    memcpy(out, base->data, n);
    switch (rnd() % 6) {
        case 0:     /* a few random bytes */
            for (k = 1 + rnd() % 8, i = 0; i < k; i++) out[rnd() % n] = rnd() & 255;
            break;
        case 1:     /* cut */
            n = rnd() % n;
            break;
        case 2: {   /* a tag's offset or size */
            DWORD count = (DWORD)out[128] << 24 | out[129] << 16 | out[130] << 8 | out[131];
            if (count && 132 + 12 * (count % 64) <= n) {
                DWORD at = 132 + 12 * (rnd() % (count < 64 ? count : 64)) + 4 + 4 * (rnd() & 1);
                static const DWORD V[] = { 0, 1, 0x7FFFFFFF, 0xFFFFFFFF, 0x80000000, 127, 128 };
                DWORD v = (rnd() & 1) ? V[rnd() % 7] : rnd() % (2 * n);
                if (at + 4 <= n) { out[at] = v >> 24; out[at + 1] = v >> 16; out[at + 2] = v >> 8; out[at + 3] = v; }
            }
            break;
        }
        case 3: {   /* header fields: size, class, color space, PCS, version */
            static const char *SIGS[] = { "RGB ", "CMYK", "GRAY", "Lab ", "XYZ ", "mntr", "prtr", "link", "abst", "nmcl", "spac", "scnr", "\0\0\0\0", "6CLR" };
            static const unsigned AT[] = { 12, 16, 20 };
            if (rnd() & 1) memcpy(out + AT[rnd() % 3], SIGS[rnd() % 14], 4);
            else { DWORD v = rnd(); out[0] = v >> 24; out[1] = v >> 16; out[2] = v >> 8; out[3] = v; }
            if (rnd() % 4 == 0) out[8] = rnd() & 7;
            break;
        }
        case 4:     /* a run of zeros or 0xFF inside the tag data */
            if (n > 200) { DWORD at = 132 + rnd() % (n - 150); memset(out + at, (rnd() & 1) ? 0xFF : 0, 1 + rnd() % 64); }
            break;
        default:    /* bytes swapped between two places */
            for (k = 1 + rnd() % 4, i = 0; i < k; i++) { DWORD a = rnd() % n, b = rnd() % n; BYTE t = out[a]; out[a] = out[b]; out[b] = t; }
            break;
    }
    *size = n;
}

int main(int argc, char **argv) {
    int iterations = (argc > 1) ? atoi(argv[1]) : 100, b, i;
    Bytes bases[12];
    const char *names[12] = { "sRGB", "linear sRGB", "grey", "linear grey", "Adobe RGB", "Display P3", "ProPhoto", "press CMYK", "grey 1.8", "test3.icc", "test5.icc", "Lab" };
    const char *fixtures[3] = { "data/bad.icc", "data/bad_mpe.icc", "data/toosmall.icc" };
    BYTE *buf;
    cmsHPROFILE lab;

    FreeImage_SetOutputMessage(on_message);
    for (b = 0; b < 7; b++) bases[b] = builtin(b);
    bases[7] = make_press_profile();
    bases[8] = make_gray_profile(1.8);
    bases[9] = read_file("data/test3.icc");
    bases[10] = read_file("data/test5.icc");
    lab = cmsCreateLab4Profile(NULL);
    bases[11] = save_profile(lab);
    cmsCloseProfile(lab);

    for (i = 0; i < 3; i++) {
        Bytes f = read_file(fixtures[i]);
        if (!f.data) { fail("cannot read %s", fixtures[i]); continue; }
        exercise(f.data, f.size, 1);
        free(f.data);
    }
    buf = (BYTE *)malloc(1 << 20);
    for (b = 0; b < 12; b++) {
        if (!bases[b].data) { fail("no %s profile", names[b]); continue; }
        exercise(bases[b].data, bases[b].size, 1);
        for (i = 0; i < iterations; i++) {
            DWORD size;
            mutate(&bases[b], buf, &size);
            exercise(buf, size, i % 20 == 0);
        }
        printf("%-12s %d damaged copies\n", names[b], iterations);
    }
    free(buf);
    for (b = 7; b < 12; b++) free(bases[b].data);
    printf("%ld results, %ld refusals, %d failures\n", used, refused, failures);
    return failures ? 1 : 0;
}
