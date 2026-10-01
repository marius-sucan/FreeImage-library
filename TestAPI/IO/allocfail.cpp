/* FreeImage 3 - I/O test: every allocation libfreeimage.a makes, failed in turn, crashes nothing */
/* malloc, calloc and realloc are wrapped at link time, new(std::nothrow) replaced; each case runs in a child per failed allocation */
#include <new>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#endif
#include "FreeImage.h"
#define CMS_NO_REGISTER_KEYWORD 1
#include "../../Source/LibLCMS2/include/lcms2.h"

static int failures = 0;

static void report(const char *what, int ok) {
    printf("  %-60s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

/* a model whose only tag was deleted enumerates as empty */
static int empty_model(void) {
    FIBITMAP *dib = FreeImage_Allocate(4, 4, 24, 0, 0, 0);
    FITAG *tag = NULL;
    FIMETADATA *mdhandle;
    if (!dib) return 0;
    FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Comment", "some text");
    FreeImage_SetMetadata(FIMD_COMMENTS, dib, "Comment", NULL);
    mdhandle = FreeImage_FindFirstMetadata(FIMD_COMMENTS, dib, &tag);
    FreeImage_FindCloseMetadata(mdhandle);
    FreeImage_Unload(dib);
    return !mdhandle && !tag;
}

#ifndef _WIN32

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

/* --------------------------------------------------------------- allocation failures */
static int armed = 0;
static long seen = 0, fail_at = 0;

static int should_fail(void) {
    return armed && (++seen == fail_at);
}

extern "C" {
void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t s);
void *__real_realloc(void *p, size_t n);
void *__wrap_malloc(size_t n) { return should_fail() ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t s) { return should_fail() ? NULL : __real_calloc(n, s); }
void *__wrap_realloc(void *p, size_t n) { return should_fail() ? NULL : __real_realloc(p, n); }
}

/* new(std::nothrow) too: the code that uses it checks for NULL */
void *operator new(size_t n, const std::nothrow_t &) noexcept { return should_fail() ? NULL : __real_malloc(n ? n : 1); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return should_fail() ? NULL : __real_malloc(n ? n : 1); }

/* the throwing operator new, failed only for the cases that set fail_new (STL nodes, plugin state) */
static int fail_new = 0;
void *operator new(size_t n) { if (fail_new && should_fail()) throw std::bad_alloc(); void *p = __real_malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void *operator new[](size_t n) { return operator new(n); }

enum { CASE_OK = 0, CASE_BROKEN = 1, NOT_REACHED = 2 };

/* runs 'run' with the 1st, 2nd, ... allocation failing until none fails; names the failures */
static void sweep(const char *what, int (*run)(void)) {
    char line[160];
    long n, broken = 0, crashed = 0;
    for (n = 1; ; n++) {
        int status = 0;
        pid_t pid = fork();
        if (pid == 0) {
#ifdef __linux__
            prctl(PR_SET_DUMPABLE, 0);   /* no core dumps: they take seconds each */
#endif
            FreeImage_Initialise(FALSE);
            FreeImage_SetOutputMessage(quiet);
            seen = 0;
            fail_at = n;
            _exit(run());
        }
        waitpid(pid, &status, 0);
        if (WIFSIGNALED(status)) {
            if (crashed++ < 3) printf("    allocation %ld: signal %d\n", n, WTERMSIG(status));
            continue;
        }
        if (WEXITSTATUS(status) == NOT_REACHED) break;
        if (WEXITSTATUS(status) == CASE_BROKEN && broken++ < 3) printf("    allocation %ld: wrong result\n", n);
    }
    snprintf(line, sizeof(line), "%s, %ld allocations failed in turn", what, n - 1);
    report(line, !broken && !crashed);
}

/* --------------------------------------------------------------- tags and metadata */

/* every stored tag exists and holds its value */
static int tags_whole(FIBITMAP *dib) {
    int model;
    for (model = FIMD_COMMENTS; model <= FIMD_EXIF_RAW; model++) {
        FITAG *tag = NULL;
        FIMETADATA *mdhandle = FreeImage_FindFirstMetadata((FREE_IMAGE_MDMODEL)model, dib, &tag);
        if (!mdhandle) continue;
        do {
            if (!tag || (FreeImage_GetTagLength(tag) && !FreeImage_GetTagValue(tag))) {
                FreeImage_FindCloseMetadata(mdhandle);
                return 0;
            }
        } while (FreeImage_FindNextMetadata(mdhandle, &tag));
        FreeImage_FindCloseMetadata(mdhandle);
    }
    return 1;
}

static int tags_case(void) {
    FIBITMAP *dib = FreeImage_Allocate(4, 4, 24, 0, 0, 0), *clone, *other;
    FITAG *tag, *copy;
    WORD value = 7;
    int whole;
    if (!dib) return CASE_BROKEN;

    armed = 1;
    tag = FreeImage_CreateTag();
    FreeImage_SetTagKey(tag, "FrameTime");
    FreeImage_SetTagDescription(tag, "a description");
    FreeImage_SetTagType(tag, FIDT_SHORT);
    FreeImage_SetTagCount(tag, 1);
    FreeImage_SetTagLength(tag, 2);
    FreeImage_SetTagValue(tag, &value);
    FreeImage_SetMetadata(FIMD_ANIMATION, dib, "FrameTime", tag);
    FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Comment", "some text");
    copy = FreeImage_CloneTag(tag);
    clone = FreeImage_Clone(dib);
    other = FreeImage_Allocate(4, 4, 24, 0, 0, 0);
    if (other) FreeImage_CloneMetadata(other, dib);
    armed = 0;

    whole = tags_whole(dib) && (!clone || tags_whole(clone)) && (!other || tags_whole(other));
    FreeImage_DeleteTag(tag);
    FreeImage_DeleteTag(copy);
    FreeImage_Unload(clone);
    FreeImage_Unload(other);
    FreeImage_Unload(dib);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* --------------------------------------------------------------- WebP */

/* lossy with graded alpha: the alpha plane goes through the lossless encoder */
static int webp_case(void) {
    FIBITMAP *dib = FreeImage_Allocate(48, 40, 32, 0, 0, 0), *back;
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    BOOL saved;
    int x, y, whole = 1;
    if (!dib || !mem) return CASE_BROKEN;
    for (y = 0; y < 40; y++) {
        BYTE *p = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < 48; x++, p += 4) {
            p[FI_RGBA_RED] = (BYTE)(x * 5); p[FI_RGBA_GREEN] = (BYTE)(y * 6);
            p[FI_RGBA_BLUE] = (BYTE)(x ^ y); p[FI_RGBA_ALPHA] = (BYTE)(x * y);
        }
    }

    armed = 1;
    saved = FreeImage_SaveToMemory(FIF_WEBP, dib, mem, 0);
    armed = 0;

    /* a save that says TRUE loads back */
    if (saved) {
        FreeImage_SeekMemory(mem, 0, SEEK_SET);
        back = FreeImage_LoadFromMemory(FIF_WEBP, mem, 0);
        whole = back && (FreeImage_GetWidth(back) == 48) && (FreeImage_GetHeight(back) == 40);
        FreeImage_Unload(back);
    }
    FreeImage_CloseMemory(mem);
    FreeImage_Unload(dib);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* --------------------------------------------------------------- saves and loads of other formats */

static FIBITMAP *pattern(FREE_IMAGE_TYPE type, int width, int height, int bpp) {
    FIBITMAP *dib = FreeImage_AllocateT(type, width, height, bpp, 0, 0, 0);
    int x, y;
    if (!dib) return NULL;
    for (y = 0; y < height; y++) {
        BYTE *p = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < (int)FreeImage_GetLine(dib); x++) p[x] = (BYTE)(x * 7 + y * 13 + ((x * y) >> 3));
        if (type == FIT_RGBF) {
            float *f = (float *)p;
            for (x = 0; x < width * 3; x++) f[x] = (float)((x * 7 + y * 13) % 97) / 50.0f;
        }
    }
    return dib;
}

/* a save that says TRUE loads back at its size */
static int save_case(FREE_IMAGE_FORMAT fif, FREE_IMAGE_TYPE type, int width, int height, int bpp) {
    FIBITMAP *dib = pattern(type, width, height, bpp), *back;
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    BOOL saved;
    int whole = 1;
    if (!dib || !mem) return CASE_BROKEN;

    armed = 1;
    saved = FreeImage_SaveToMemory(fif, dib, mem, 0);
    armed = 0;

    if (saved) {
        FreeImage_SeekMemory(mem, 0, SEEK_SET);
        back = FreeImage_LoadFromMemory(fif, mem, 0);
        whole = back && ((int)FreeImage_GetWidth(back) == width) && ((int)FreeImage_GetHeight(back) == height);
        FreeImage_Unload(back);
    }
    FreeImage_CloseMemory(mem);
    FreeImage_Unload(dib);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* a load from a file returns the image at its size, or NULL */
static int load_case(FREE_IMAGE_FORMAT fif, FREE_IMAGE_TYPE type, int width, int height, int bpp) {
    FIBITMAP *dib = pattern(type, width, height, bpp), *back;
    const char *dir = getenv("IO_TEST_TMP");
    char path[512];
    int whole;
    /* a file per child, in $IO_TEST_TMP or the current directory */
    snprintf(path, sizeof(path), "%s/fi_io_allocfail_%d", (dir && *dir) ? dir : ".", (int)getpid());
    if (!dib || !FreeImage_Save(fif, dib, path, 0)) return CASE_BROKEN;

    armed = 1;
    back = FreeImage_Load(fif, path, 0);
    armed = 0;

    whole = !back || (((int)FreeImage_GetWidth(back) == width) && ((int)FreeImage_GetHeight(back) == height));
    FreeImage_Unload(back);
    FreeImage_Unload(dib);
    remove(path);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

static int gif_save(void) { return save_case(FIF_GIF, FIT_BITMAP, 40, 24, 8); }
static int gif_load(void) { return load_case(FIF_GIF, FIT_BITMAP, 40, 24, 8); }
static int ico_save(void) { return save_case(FIF_ICO, FIT_BITMAP, 32, 32, 24); }
static int j2k_save(void) { return save_case(FIF_J2K, FIT_BITMAP, 16, 16, 24); }
static int j2k_load(void) { return load_case(FIF_J2K, FIT_BITMAP, 16, 16, 24); }
static int exr_save(void) { return save_case(FIF_EXR, FIT_RGBF, 40, 24, 96); }
static int jng_save(void) { return save_case(FIF_JNG, FIT_BITMAP, 40, 24, 24); }
static int tiff_load(void) { return load_case(FIF_TIFF, FIT_BITMAP, 40, 24, 24); }

/* --------------------------------------------------------------- multi-page and metadata under a failing throwing new */

static const char *mpage_path(char *buf, size_t size) {
    const char *dir = getenv("IO_TEST_TMP");
    snprintf(buf, size, "%s/fi_io_mpage_%d.tif", (dir && *dir) ? dir : ".", (int)getpid());
    return buf;
}

static FIBITMAP *mpage_frame(int i) {
    FIBITMAP *dib = FreeImage_Allocate(16, 12, 24, 0, 0, 0);
    int x, y;
    if (!dib) return NULL;
    for (y = 0; y < 12; y++) {
        BYTE *p = FreeImage_GetScanLine(dib, y);
        for (x = 0; x < 48; x++) p[x] = (BYTE)(x * 5 + y * 11 + i * 40);
    }
    return dib;
}

/* every page it reports must lock */
static int mpage_consistent(FIMULTIBITMAP *mb) {
    int n = FreeImage_GetPageCount(mb), p;
    if (n < 1) return 0;
    for (p = 0; p < n; p++) {
        FIBITMAP *pg = FreeImage_LockPage(mb, p);
        if (!pg) return 0;
        FreeImage_UnlockPage(mb, pg, FALSE);
    }
    return 1;
}

/* append, insert, move, delete and a changed unlock on a spanned file: no crash, stays consistent */
static int mpage_case(void) {
    char path[512];
    FIMULTIBITMAP *mb;
    FIBITMAP *a, *b, *pg;
    int i, whole = 1;
    mpage_path(path, sizeof(path));
    remove(path);
    mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, TRUE, FALSE, TRUE, 0);
    if (!mb) return CASE_BROKEN;
    for (i = 0; i < 4; i++) {
        FIBITMAP *f = mpage_frame(i);
        int ok = f && FreeImage_AppendPage(mb, f);
        FreeImage_Unload(f);
        if (!ok) { FreeImage_CloseMultiBitmap(mb, 0); remove(path); return CASE_BROKEN; }
    }
    FreeImage_CloseMultiBitmap(mb, 0);

    /* open for edit: one BLOCK_CONTINUEUS span, so FindBlock splits */
    mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, FALSE, FALSE, TRUE, 0);
    if (!mb) { remove(path); return CASE_BROKEN; }

    armed = 1; fail_new = 1;
    a = mpage_frame(7);
    if (a) { FreeImage_AppendPage(mb, a); FreeImage_Unload(a); }
    b = mpage_frame(8);
    if (b) { FreeImage_InsertPage(mb, 2, b); FreeImage_Unload(b); }
    FreeImage_MovePage(mb, 0, 3);
    FreeImage_DeletePage(mb, 1);
    pg = FreeImage_LockPage(mb, 1);
    if (pg) { FreeImage_Invert(pg); FreeImage_UnlockPage(mb, pg, TRUE); }
    armed = 0; fail_new = 0;

    if (seen >= fail_at) whole = mpage_consistent(mb);
    FreeImage_CloseMultiBitmap(mb, 0);
    if (whole && seen >= fail_at) {
        FIMULTIBITMAP *rb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, FALSE, TRUE, TRUE, 0);
        if (rb) { whole = mpage_consistent(rb); FreeImage_CloseMultiBitmap(rb, 0); }
    }
    remove(path);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* SetMetadata, Clone and CloneMetadata: no crash, no tag without its value, the old tag survives */
static int meta_case(void) {
    FIBITMAP *dib = FreeImage_Allocate(4, 4, 24, 0, 0, 0), *clone = NULL, *other = NULL;
    FITAG *tag, *check = NULL;
    WORD value = 7;
    int whole;
    if (!dib) return CASE_BROKEN;
    FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Existing", "keep me");

    armed = 1; fail_new = 1;
    tag = FreeImage_CreateTag();
    if (tag) {
        FreeImage_SetTagKey(tag, "FrameTime");
        FreeImage_SetTagType(tag, FIDT_SHORT);
        FreeImage_SetTagCount(tag, 1);
        FreeImage_SetTagLength(tag, 2);
        FreeImage_SetTagValue(tag, &value);
        FreeImage_SetMetadata(FIMD_ANIMATION, dib, "FrameTime", tag);
        FreeImage_DeleteTag(tag);
    }
    FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Second", "another value");
    clone = FreeImage_Clone(dib);
    other = FreeImage_Allocate(4, 4, 24, 0, 0, 0);
    if (other) FreeImage_CloneMetadata(other, dib);
    armed = 0; fail_new = 0;

    whole = tags_whole(dib) && (!clone || tags_whole(clone)) && (!other || tags_whole(other));
    if (!FreeImage_GetMetadata(FIMD_COMMENTS, dib, "Existing", &check) || !check) whole = 0;

    FreeImage_Unload(clone);
    FreeImage_Unload(other);
    FreeImage_Unload(dib);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* FreeImage_LoadMultiBitmapFromMemory of a 2-page MNG: a plugin that throws while counting must not escape */
static int loadmem_case(void) {
    char path[512];
    FIMULTIBITMAP *mb;
    FILE *f;
    BYTE *buf = NULL;
    long size = 0;
    int i, whole = 1, ok = 1;
    mpage_path(path, sizeof(path));
    remove(path);
    mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, TRUE, FALSE, TRUE, 0);
    if (!mb) return CASE_BROKEN;
    for (i = 0; i < 2; i++) {
        FIBITMAP *fr = mpage_frame(i);
        ok = ok && fr && FreeImage_AppendPage(mb, fr);
        FreeImage_Unload(fr);
    }
    FreeImage_CloseMultiBitmap(mb, 0);
    if (ok) {
        f = fopen(path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
            buf = (BYTE *)malloc(size);
            if (!buf || fread(buf, 1, size, f) != (size_t)size) { free(buf); buf = NULL; }
            fclose(f);
        }
    }
    remove(path);
    if (!buf) return CASE_BROKEN;

    FIMEMORY *mem = FreeImage_OpenMemory(buf, (DWORD)size);
    if (!mem) { free(buf); return CASE_BROKEN; }

    armed = 1; fail_new = 1;
    FIMULTIBITMAP *load = FreeImage_LoadMultiBitmapFromMemory(FIF_MNG, mem, 0);
    armed = 0; fail_new = 0;

    if (seen >= fail_at && load) whole = mpage_consistent(load);
    if (load) FreeImage_CloseMultiBitmap(load, 0);
    FreeImage_CloseMemory(mem);
    free(buf);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* --------------------------------------------------------------- color management */

static const void *s_adobe = NULL, *s_p3 = NULL, *s_prophoto = NULL;
static DWORD s_adobe_size = 0, s_p3_size = 0, s_prophoto_size = 0;
static BYTE *s_press = NULL, *s_lut = NULL, *s_png = NULL;
static DWORD s_press_size = 0, s_lut_size = 0, s_png_size = 0;

/* a CMYK press profile built with Little CMS: the naive inks to Lab, and back */
static cmsInt32Number press_forward(const cmsUInt16Number in[], cmsUInt16Number out[], void *cargo) {
    const double k = in[3] / 65535.0;
    double rgb[3];
    for (int i = 0; i < 3; i++) rgb[i] = (1.0 - in[i] / 65535.0) * (1.0 - k);
    cmsDoTransform((cmsHTRANSFORM)cargo, rgb, out, 1);
    return TRUE;
}

static cmsInt32Number press_reverse(const cmsUInt16Number in[], cmsUInt16Number out[], void *cargo) {
    double rgb[3];
    cmsDoTransform((cmsHTRANSFORM)cargo, in, rgb, 1);
    const double k = 1.0 - fmax(rgb[0], fmax(rgb[1], rgb[2]));
    for (int i = 0; i < 3; i++) {
        const double c = (k < 1.0) ? (1.0 - rgb[i] - k) / (1.0 - k) : 0.0;
        out[i] = (cmsUInt16Number)(fmin(fmax(c, 0.0), 1.0) * 65535.0 + 0.5);
    }
    out[3] = (cmsUInt16Number)(fmin(fmax(k, 0.0), 1.0) * 65535.0 + 0.5);
    return TRUE;
}

static BYTE *make_press(DWORD *size) {
    cmsHPROFILE srgb = cmsCreate_sRGBProfile(), lab = cmsCreateLab4Profile(NULL), h = cmsCreateProfilePlaceholder(NULL);
    cmsHTRANSFORM to_lab = cmsCreateTransform(srgb, TYPE_RGB_DBL, lab, TYPE_Lab_16, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE);
    cmsHTRANSFORM to_rgb = cmsCreateTransform(lab, TYPE_Lab_16, srgb, TYPE_RGB_DBL, INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOOPTIMIZE | cmsFLAGS_NOCACHE);
    cmsPipeline *a2b = cmsPipelineAlloc(NULL, 4, 3), *b2a = cmsPipelineAlloc(NULL, 3, 4);
    cmsStage *forward = cmsStageAllocCLut16bit(NULL, 9, 4, 3, NULL), *reverse = cmsStageAllocCLut16bit(NULL, 17, 3, 4, NULL);
    cmsUInt32Number n = 0;
    BYTE *data = NULL;
    cmsStageSampleCLut16bit(forward, press_forward, to_lab, 0);
    cmsStageSampleCLut16bit(reverse, press_reverse, to_rgb, 0);
    cmsPipelineInsertStage(a2b, cmsAT_END, forward);
    cmsPipelineInsertStage(b2a, cmsAT_END, reverse);
    cmsSetProfileVersion(h, 2.1);
    cmsSetDeviceClass(h, cmsSigOutputClass);
    cmsSetColorSpace(h, cmsSigCmykData);
    cmsSetPCS(h, cmsSigLabData);
    cmsWriteTag(h, cmsSigMediaWhitePointTag, cmsD50_XYZ());
    cmsWriteTag(h, cmsSigAToB0Tag, a2b);
    cmsWriteTag(h, cmsSigBToA0Tag, b2a);
    if (cmsSaveProfileToMem(h, NULL, &n) && n && (data = (BYTE *)malloc(n)) != NULL && !cmsSaveProfileToMem(h, data, &n)) {
        free(data);
        data = NULL;
    }
    *size = data ? n : 0;
    cmsPipelineFree(a2b);
    cmsPipelineFree(b2a);
    cmsDeleteTransform(to_lab);
    cmsDeleteTransform(to_rgb);
    cmsCloseProfile(h);
    cmsCloseProfile(srgb);
    cmsCloseProfile(lab);
    return data;
}

static BYTE *read_bytes(const char *path, DWORD *size) {
    FILE *f = fopen(path, "rb");
    BYTE *data = NULL;
    long n;
    *size = 0;
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (n = ftell(f)) > 0 && fseek(f, 0, SEEK_SET) == 0 && (data = (BYTE *)malloc(n)) != NULL) {
        if (fread(data, 1, n, f) == (size_t)n) *size = (DWORD)n;
        else { free(data); data = NULL; }
    }
    fclose(f);
    return data;
}

/* the profiles, the display and an Adobe RGB PNG, made before the sweeps */
static int icc_setup(void) {
    FIBITMAP *dib = pattern(FIT_BITMAP, 40, 24, 24);
    FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
    BYTE *data = NULL;
    DWORD size = 0;
    s_adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &s_adobe_size);
    s_p3 = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_DISPLAY_P3, &s_p3_size);
    s_prophoto = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_PROPHOTO_RGB, &s_prophoto_size);
    s_press = make_press(&s_press_size);
    /* a LUT profile with a Lab connection space, from the color management tests */
    s_lut = read_bytes("../ICC/data/test3.icc", &s_lut_size);
    if (dib && mem) {
        FreeImage_CreateICCProfile(dib, (void *)s_adobe, (long)s_adobe_size);
        if (FreeImage_SaveToMemory(FIF_PNG, dib, mem, 0) && FreeImage_AcquireMemory(mem, &data, &size) && (s_png = (BYTE *)malloc(size)) != NULL) {
            memcpy(s_png, data, size);
            s_png_size = size;
        }
    }
    if (mem) FreeImage_CloseMemory(mem);
    FreeImage_Unload(dib);
    return s_adobe && s_p3 && s_prophoto && s_press && s_png && FreeImage_SetDisplayICCProfile(s_p3, s_p3_size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION);
}

static FIBITMAP *tagged(FREE_IMAGE_TYPE type, int bpp, const void *profile, DWORD size) {
    FIBITMAP *dib = pattern(type, 40, 24, bpp);
    if (dib && profile) FreeImage_CreateICCProfile(dib, (void *)profile, (long)size);
    return dib;
}

static int has_profile(FIBITMAP *dib, const void *profile, DWORD size) {
    const FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
    return icc->data && (icc->size == size) && !memcmp(icc->data, profile, size);
}

/* NULL, or the whole image, carrying the profile its pixels are in */
static int converted_ok(FIBITMAP *dib, const void *profile, DWORD size) {
    return !dib || (FreeImage_HasPixels(dib) && (FreeImage_GetWidth(dib) == 40) && (FreeImage_GetHeight(dib) == 24) && has_profile(dib, profile, size));
}

/* Adobe RGB to Display P3 at 8 bits, in place too, and RGB16 to ProPhoto RGB at 16 bits */
static int icc_convert_case(void) {
    FIBITMAP *rgb = tagged(FIT_BITMAP, 24, s_adobe, s_adobe_size), *rgba = tagged(FIT_BITMAP, 32, s_adobe, s_adobe_size);
    FIBITMAP *rgb16 = tagged(FIT_RGB16, 48, s_adobe, s_adobe_size), *a, *b;
    BOOL applied;
    int whole;
    if (!rgb || !rgba || !rgb16) return CASE_BROKEN;

    armed = 1;
    a = FreeImage_ConvertToICCProfile(rgb, s_p3, s_p3_size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION);
    b = FreeImage_ConvertToICCProfile(rgb16, s_prophoto, s_prophoto_size, FICMS_INTENT_PERCEPTUAL);
    applied = FreeImage_ApplyICCProfile(rgba, s_p3, s_p3_size, FICMS_INTENT_RELATIVE_COLORIMETRIC);
    armed = 0;

    whole = converted_ok(a, s_p3, s_p3_size) && converted_ok(b, s_prophoto, s_prophoto_size) &&
        (applied ? has_profile(rgba, s_p3, s_p3_size) : has_profile(rgba, s_adobe, s_adobe_size));
    FreeImage_Unload(a);
    FreeImage_Unload(b);
    FreeImage_Unload(rgb);
    FreeImage_Unload(rgba);
    FreeImage_Unload(rgb16);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* sRGB to a CMYK press with black point compensation, and back to Adobe RGB */
static int icc_cmyk_case(void) {
    FIBITMAP *rgb = tagged(FIT_BITMAP, 24, NULL, 0), *cmyk, *back = NULL;
    int whole;
    if (!rgb) return CASE_BROKEN;

    armed = 1;
    cmyk = FreeImage_ConvertToCMYK(rgb, s_press, s_press_size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION);
    if (cmyk) back = FreeImage_ConvertCMYKToRGB(cmyk, s_adobe, s_adobe_size, FICMS_INTENT_PERCEPTUAL | FICMS_BLACKPOINT_COMPENSATION);
    armed = 0;

    whole = converted_ok(cmyk, s_press, s_press_size) && (!cmyk || (FreeImage_GetICCProfile(cmyk)->flags & FIICC_COLOR_IS_CMYK)) &&
        converted_ok(back, s_adobe, s_adobe_size);
    FreeImage_Unload(cmyk);
    FreeImage_Unload(back);
    FreeImage_Unload(rgb);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* Adobe RGB proofed on the press for a Display P3 display, the colors the press cannot print marked */
static int icc_proof_case(void) {
    FIBITMAP *rgb = tagged(FIT_BITMAP, 24, s_adobe, s_adobe_size), *proof;
    int whole;
    if (!rgb) return CASE_BROKEN;

    armed = 1;
    proof = FreeImage_SoftProof(rgb, s_press, s_press_size, s_p3, s_p3_size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_GAMUT_CHECK);
    armed = 0;

    whole = converted_ok(proof, s_p3, s_p3_size);
    FreeImage_Unload(proof);
    FreeImage_Unload(rgb);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* a LUT profile with a Lab connection space converted to sRGB, and its description */
static int icc_lut_case(void) {
    FIBITMAP *rgb = tagged(FIT_BITMAP, 24, s_lut, s_lut_size), *res;
    char text[64];
    int whole;
    if (!rgb) return CASE_BROKEN;
    memset(text, 'x', sizeof(text));

    armed = 1;
    res = FreeImage_ConvertToICCProfile(rgb, NULL, 0, FICMS_INTENT_PERCEPTUAL | FICMS_BLACKPOINT_COMPENSATION);
    FreeImage_GetICCProfileDescription(s_lut, s_lut_size, text, sizeof(text));
    FreeImage_GetICCProfileColorSpace(s_lut, s_lut_size);
    armed = 0;

    whole = (!res || (FreeImage_HasPixels(res) && !FreeImage_GetICCProfile(res)->data)) && memchr(text, 0, sizeof(text));
    FreeImage_Unload(res);
    FreeImage_Unload(rgb);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* the display becomes Adobe RGB, or stays Display P3 */
static int icc_display_set_case(void) {
    BYTE now[4096];
    DWORD n;
    BOOL done;

    armed = 1; fail_new = 1;
    done = FreeImage_SetDisplayICCProfile(s_adobe, s_adobe_size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION);
    armed = 0; fail_new = 0;

    n = FreeImage_GetDisplayICCProfile(now, sizeof(now));
    if (seen < fail_at) return NOT_REACHED;
    if (done) return ((n == s_adobe_size) && !memcmp(now, s_adobe, n)) ? CASE_OK : CASE_BROKEN;
    return ((n == s_p3_size) && !memcmp(now, s_p3, n)) ? CASE_OK : CASE_BROKEN;
}

/* FIF_LOAD_DISPLAY_ICC: an Adobe RGB PNG comes in the display's colors, as it was, or not at all */
static int icc_display_load_case(void) {
    FIMEMORY *mem = FreeImage_OpenMemory(s_png, s_png_size);
    FIBITMAP *dib;
    int whole;
    if (!mem) return CASE_BROKEN;

    armed = 1; fail_new = 1;
    dib = FreeImage_LoadFromMemory(FIF_PNG, mem, FIF_LOAD_DISPLAY_ICC);
    armed = 0; fail_new = 0;

    whole = !dib || converted_ok(dib, s_p3, s_p3_size) || converted_ok(dib, s_adobe, s_adobe_size);
    FreeImage_Unload(dib);
    FreeImage_CloseMemory(mem);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* an Adobe RGB frame, then a Display P3 one converted to it: a save that says TRUE loads with every page appended */
static int apng_case(void) {
    FIBITMAP *a = tagged(FIT_BITMAP, 24, s_adobe, s_adobe_size), *b = tagged(FIT_BITMAP, 24, s_p3, s_p3_size);
    const char *dir = getenv("IO_TEST_TMP");
    char path[512];
    FIMULTIBITMAP *doc;
    BOOL saved = FALSE;
    int pages = 0, whole = 1;
    if (!a || !b) return CASE_BROKEN;
    snprintf(path, sizeof(path), "%s/fi_io_apng_%d.png", (dir && *dir) ? dir : ".", (int)getpid());

    armed = 1; fail_new = 1;
    doc = FreeImage_OpenMultiBitmap(FIF_APNG, path, TRUE, FALSE, FALSE, 0);
    if (doc) {
        pages += FreeImage_AppendPage(doc, a) ? 1 : 0;
        pages += FreeImage_AppendPage(doc, b) ? 1 : 0;
        saved = FreeImage_CloseMultiBitmap(doc, 0);
    }
    armed = 0; fail_new = 0;

    if (saved && pages) {
        FIMULTIBITMAP *back = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, 0);
        whole = back && (FreeImage_GetPageCount(back) == pages);
        if (back) FreeImage_CloseMultiBitmap(back, 0);
    }
    remove(path);
    FreeImage_Unload(a);
    FreeImage_Unload(b);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

#endif /* !_WIN32 */

int main(void) {
    printf("FreeImage %s - allocation failures\n", FreeImage_GetVersion());
    report("a model whose only tag was deleted enumerates as empty", empty_model());
#ifdef _WIN32
    printf("  %-60s %s\n", "needs fork() and a linker with --wrap", "skipped");
#else
    sweep("tags and metadata: no crash, no tag without its value", tags_case);
    sweep("WebP save: TRUE only for a file that loads", webp_case);
    sweep("multi-page mutators: no crash, stays consistent", mpage_case);
    sweep("SetMetadata/Clone/CloneMetadata: no crash, tags kept", meta_case);
    sweep("LoadMultiBitmapFromMemory: a throwing page count does not escape", loadmem_case);
    sweep("GIF save: TRUE only for a file that loads", gif_save);
    sweep("GIF load: no crash", gif_load);
    sweep("ICO save: TRUE only for a file that loads", ico_save);
    sweep("JPEG 2000 save: TRUE only for a file that loads", j2k_save);
    sweep("JPEG 2000 load: no crash", j2k_load);
    sweep("EXR save: TRUE only for a file that loads", exr_save);
    sweep("JNG save: TRUE only for a file that loads", jng_save);
    sweep("TIFF load: no crash", tiff_load);
    if (!icc_setup()) {
        report("color management: the test profiles", 0);
    } else {
        sweep("ICC conversions, 8-bit, 16-bit, in place: NULL or tagged", icc_convert_case);
        sweep("ICC CMYK and back, black point compensation: NULL or tagged", icc_cmyk_case);
        sweep("ICC soft proof with a gamut check: NULL or tagged", icc_proof_case);
        if (s_lut) sweep("ICC LUT profile, Lab connection space: NULL or sRGB", icc_lut_case);
        sweep("display profile set: the new one or the old one", icc_display_set_case);
        sweep("FIF_LOAD_DISPLAY_ICC load: converted, as loaded or NULL", icc_display_load_case);
        sweep("APNG save, frames in different profiles: TRUE only for a file that loads", apng_case);
    }
#endif
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
