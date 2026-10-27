/* FreeImage 3 - I/O test: every allocation libfreeimage.a makes, failed in turn, crashes nothing */
/* malloc, calloc and realloc are wrapped at link time, new(std::nothrow) replaced; each case runs in a child per failed allocation */
#include <new>
#include <errno.h>
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
#ifndef _WIN32
#include "../../Source/LibTIFF4/tiffio.h"
#include <jxl/encode.h>
#endif

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

/* a failed allocation sets errno, as the C library's do */
static void *no_memory(void) {
    errno = ENOMEM;
    return NULL;
}

/* blocks allocated and not freed: a load that fails has to give back what it took */
static long live = 0;

/* allocations of at least fail_size bytes fail too, of the kinds fail_kinds names: a realloc grows a block or makes one */
enum { KIND_MALLOC = 1, KIND_CALLOC = 2, KIND_GROW = 4, KIND_FRESH = 8 };
static size_t fail_size = 0;
static int fail_kinds = 0;

static int too_big(size_t n, int kind) {
    return fail_size && (n >= fail_size) && (fail_kinds & kind);
}

extern "C" {
void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t s);
void *__real_realloc(void *p, size_t n);
void __real_free(void *p);
void *__wrap_malloc(size_t n) { void *p = (should_fail() || too_big(n, KIND_MALLOC)) ? no_memory() : __real_malloc(n); if (p) live++; return p; }
void *__wrap_calloc(size_t n, size_t s) { void *p = (should_fail() || too_big((s && n > (size_t)-1 / s) ? (size_t)-1 : n * s, KIND_CALLOC)) ? no_memory() : __real_calloc(n, s); if (p) live++; return p; }
void *__wrap_realloc(void *p, size_t n) { void *q = (should_fail() || too_big(n, p ? KIND_GROW : KIND_FRESH)) ? no_memory() : __real_realloc(p, n); if (!p && q) live++; return q; }
void __wrap_free(void *p) { if (p) live--; __real_free(p); }
}

/* new(std::nothrow) too: the code that uses it checks for NULL */
static void *counted(void *p) { if (p) live++; return p; }
void *operator new(size_t n, const std::nothrow_t &) noexcept { return counted(should_fail() ? no_memory() : __real_malloc(n ? n : 1)); }
void *operator new[](size_t n, const std::nothrow_t &) noexcept { return counted(should_fail() ? no_memory() : __real_malloc(n ? n : 1)); }

/* the throwing operator new, failed only for the cases that set fail_new (STL nodes, plugin state) */
static int fail_new = 0;
void *operator new(size_t n) { if (fail_new && should_fail()) { no_memory(); throw std::bad_alloc(); } void *p = __real_malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return counted(p); }
void *operator new[](size_t n) { return operator new(n); }
/* through the wrapped free, so that deletes are counted */
void operator delete(void *p) noexcept { free(p); }
void operator delete[](void *p) noexcept { free(p); }
void operator delete(void *p, size_t) noexcept { free(p); }
void operator delete[](void *p, size_t) noexcept { free(p); }

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

/* a PSD with IPTC keywords, categories and a caption: the IPTC reader's strings fail too; nothing is left allocated */
static int psd_iptc_case(void) {
    FIBITMAP *dib = pattern(FIT_BITMAP, 40, 24, 24), *back;
    const char *dir = getenv("IO_TEST_TMP");
    char path[512];
    int whole;
    long before;
    if (!dib) return CASE_BROKEN;
    snprintf(path, sizeof(path), "%s/fi_io_psd_%d.psd", (dir && *dir) ? dir : ".", (int)getpid());
    FreeImage_SetMetadataKeyValue(FIMD_IPTC, dib, "Keywords", "first;second;third");
    FreeImage_SetMetadataKeyValue(FIMD_IPTC, dib, "SupplementalCategories", "one;two");
    FreeImage_SetMetadataKeyValue(FIMD_IPTC, dib, "Caption-Abstract", "a caption longer than a short string holds");
    if (!FreeImage_Save(FIF_PSD, dib, path, 0)) { FreeImage_Unload(dib); return CASE_BROKEN; }
    /* a first load fills what stays allocated */
    FreeImage_Unload(FreeImage_Load(FIF_PSD, path, 0));
    before = live;

    armed = 1; fail_new = 1;
    back = FreeImage_Load(FIF_PSD, path, 0);
    armed = 0; fail_new = 0;

    whole = !back || (((int)FreeImage_GetWidth(back) == 40) && ((int)FreeImage_GetHeight(back) == 24) && tags_whole(back));
    FreeImage_Unload(back);
    whole = whole && (live == before);
    FreeImage_Unload(dib);
    remove(path);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

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

/* GetMetadata, removing a tag and DestroyICCProfile copy keys too long for a short string: no exception, no tag freed twice */
static int meta_longkey_case(void) {
    FIBITMAP *dib = FreeImage_Allocate(4, 4, 24, 0, 0, 0);
    static BYTE profile[132];
    FITAG *tag = NULL;
    int whole;
    if (!dib) return CASE_BROKEN;
    FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "A comment key of some length", "first");
    FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Another long comment key", "second");
    FreeImage_SetMetadataKeyValue(FIMD_EXIF_MAIN, dib, "ImageDescription", "a description");
    FreeImage_SetMetadataKeyValue(FIMD_EXIF_MAIN, dib, "InterColorProfile", "a profile");
    FreeImage_CreateICCProfile(dib, profile, sizeof(profile));

    armed = 1; fail_new = 1;
    FreeImage_GetMetadata(FIMD_COMMENTS, dib, "A comment key of some length", &tag);
    FreeImage_SetMetadata(FIMD_COMMENTS, dib, "Another long comment key", NULL);
    FreeImage_DestroyICCProfile(dib);
    armed = 0; fail_new = 0;

    whole = tags_whole(dib);
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
static BYTE *s_press = NULL, *s_lut = NULL, *s_png = NULL, *s_cmyk_tif = NULL;
static DWORD s_press_size = 0, s_lut_size = 0, s_png_size = 0, s_cmyk_tif_size = 0;

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
    /* a CMYK TIFF in the press profile, which a FIF_LOAD_DISPLAY_ICC load replaces with an RGB image */
    dib = pattern(FIT_BITMAP, 40, 24, 32);
    mem = FreeImage_OpenMemory(NULL, 0);
    if (dib && mem && s_press) {
        FreeImage_CreateICCProfile(dib, s_press, (long)s_press_size);
        FreeImage_GetICCProfile(dib)->flags |= FIICC_COLOR_IS_CMYK;
        if (FreeImage_SaveToMemory(FIF_TIFF, dib, mem, TIFF_CMYK | TIFF_NONE) && FreeImage_AcquireMemory(mem, &data, &size) && (s_cmyk_tif = (BYTE *)malloc(size)) != NULL) {
            memcpy(s_cmyk_tif, data, size);
            s_cmyk_tif_size = size;
        }
    }
    if (mem) FreeImage_CloseMemory(mem);
    FreeImage_Unload(dib);
    return s_adobe && s_p3 && s_prophoto && s_press && s_png && s_cmyk_tif && FreeImage_SetDisplayICCProfile(s_p3, s_p3_size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION);
}

/* a new Display P3 state: as many blocks as the one the cases start from, no transform cached */
static void fresh_display(void) {
    FreeImage_SetDisplayICCProfile(s_p3, s_p3_size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION);
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

/* the display becomes Adobe RGB, or stays Display P3; nothing is left allocated */
static int icc_display_set_case(void) {
    BYTE now[4096];
    DWORD n;
    BOOL done;
    long before = live;
    int whole;

    armed = 1; fail_new = 1;
    done = FreeImage_SetDisplayICCProfile(s_adobe, s_adobe_size, FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION);
    armed = 0; fail_new = 0;

    n = FreeImage_GetDisplayICCProfile(now, sizeof(now));
    if (seen < fail_at) return NOT_REACHED;
    if (done) whole = (n == s_adobe_size) && !memcmp(now, s_adobe, n);
    else whole = (n == s_p3_size) && !memcmp(now, s_p3, n);
    fresh_display();
    return (whole && (live == before)) ? CASE_OK : CASE_BROKEN;
}

/* FIF_LOAD_DISPLAY_ICC: the image in the display's colors, as it was loaded when own names its profile, or not at all;
   with the display made new before and after, which empties its transform cache, nothing is left allocated */
static int display_load_case(FREE_IMAGE_FORMAT fif, BYTE *file, DWORD size, const void *own, DWORD own_size) {
    FIMEMORY *mem = FreeImage_OpenMemory(file, size);
    FIBITMAP *dib;
    long before;
    int whole;
    if (!mem) return CASE_BROKEN;
    /* a first load fills what stays allocated */
    FreeImage_Unload(FreeImage_LoadFromMemory(fif, mem, FIF_LOAD_DISPLAY_ICC));
    FreeImage_SeekMemory(mem, 0, SEEK_SET);
    fresh_display();
    before = live;

    armed = 1; fail_new = 1;
    dib = FreeImage_LoadFromMemory(fif, mem, FIF_LOAD_DISPLAY_ICC);
    armed = 0; fail_new = 0;

    whole = !dib || converted_ok(dib, s_p3, s_p3_size) || (own && converted_ok(dib, own, own_size));
    FreeImage_Unload(dib);
    fresh_display();
    whole = whole && (live == before);
    FreeImage_CloseMemory(mem);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* an Adobe RGB PNG, converted in place */
static int icc_display_load_case(void) {
    return display_load_case(FIF_PNG, s_png, s_png_size, s_adobe, s_adobe_size);
}

/* a CMYK TIFF, replaced with RGB */
static int icc_display_cmyk_case(void) {
    return display_load_case(FIF_TIFF, s_cmyk_tif, s_cmyk_tif_size, NULL, 0);
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

/* an animation page, as QPV makes them */
static void frame_time(FIBITMAP *dib) {
    FITAG *tag = FreeImage_CreateTag();
    LONG ms = 100;
    if (!tag) return;
    FreeImage_SetTagKey(tag, "FrameTime");
    FreeImage_SetTagType(tag, FIDT_LONG);
    FreeImage_SetTagCount(tag, 1);
    FreeImage_SetTagLength(tag, 4);
    FreeImage_SetTagValue(tag, &ms);
    FreeImage_SetMetadata(FIMD_ANIMATION, dib, "FrameTime", tag);
    FreeImage_DeleteTag(tag);
}

/* the same for an animated WebP, whose pages are animation frames in its page cache too */
static int webp_anim_case(void) {
    FIBITMAP *a = tagged(FIT_BITMAP, 24, s_adobe, s_adobe_size), *b = tagged(FIT_BITMAP, 24, s_p3, s_p3_size);
    const char *dir = getenv("IO_TEST_TMP");
    char path[512];
    FIMULTIBITMAP *doc;
    BOOL saved = FALSE;
    int pages = 0, whole = 1;
    if (!a || !b) return CASE_BROKEN;
    frame_time(a);
    frame_time(b);
    snprintf(path, sizeof(path), "%s/fi_io_webp_%d.webp", (dir && *dir) ? dir : ".", (int)getpid());

    armed = 1; fail_new = 1;
    doc = FreeImage_OpenMultiBitmap(FIF_WEBP, path, TRUE, FALSE, TRUE, 0);
    if (doc) {
        pages += FreeImage_AppendPage(doc, a) ? 1 : 0;
        pages += FreeImage_AppendPage(doc, b) ? 1 : 0;
        saved = FreeImage_CloseMultiBitmap(doc, 0);
    }
    armed = 0; fail_new = 0;

    if (saved && pages) {
        FIMULTIBITMAP *back = FreeImage_OpenMultiBitmap(FIF_WEBP, path, FALSE, TRUE, TRUE, 0);
        whole = back && (FreeImage_GetPageCount(back) == pages);
        if (back) FreeImage_CloseMultiBitmap(back, 0);
    }
    remove(path);
    FreeImage_Unload(a);
    FreeImage_Unload(b);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* --------------------------------------------------------------- CMYK + alpha: no load or save drops the alpha */

static tmsize_t t_read(thandle_t h, void *b, tmsize_t n) { return (tmsize_t)fread(b, 1, (size_t)n, (FILE *)h); }
static tmsize_t t_write(thandle_t h, void *b, tmsize_t n) { return (tmsize_t)fwrite(b, 1, (size_t)n, (FILE *)h); }
static toff_t t_seek(thandle_t h, toff_t o, int w) { fseek((FILE *)h, (long)o, w); return (toff_t)ftell((FILE *)h); }
static int t_close(thandle_t h) { return fclose((FILE *)h); }
static toff_t t_size(thandle_t h) { long p = ftell((FILE *)h), s; fseek((FILE *)h, 0, SEEK_END); s = ftell((FILE *)h); fseek((FILE *)h, p, SEEK_SET); return (toff_t)s; }
static int t_map(thandle_t h, void **b, toff_t *s) { (void)h; (void)b; (void)s; return 0; }
static void t_unmap(thandle_t h, void *b, toff_t s) { (void)h; (void)b; (void)s; }

/* 32 x 16 CMYK and an alpha, by libtiff: 8-bit strips, or 16-bit 16 x 16 tiles */
static int write_cmyka_tiff(const char *path, int tiled) {
    static BYTE buf[16 * 16 * 5 * 2];
    uint16_t extra = EXTRASAMPLE_UNASSALPHA;
    FILE *f = fopen(path, "w+b");
    TIFF *t = f ? TIFFClientOpen(path, "w", (thandle_t)f, t_read, t_write, t_seek, t_close, t_size, t_map, t_unmap) : NULL;
    int ok = 1;
    if (!t) { if (f) fclose(f); return 0; }
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (BYTE)(i * 7 + (i >> 5));
    TIFFSetField(t, TIFFTAG_IMAGEWIDTH, 32); TIFFSetField(t, TIFFTAG_IMAGELENGTH, 16);
    TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, tiled ? 16 : 8); TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, 5);
    TIFFSetField(t, TIFFTAG_EXTRASAMPLES, 1, &extra);
    TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_SEPARATED); TIFFSetField(t, TIFFTAG_INKSET, INKSET_CMYK);
    TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    if (tiled) {
        TIFFSetField(t, TIFFTAG_TILEWIDTH, 16); TIFFSetField(t, TIFFTAG_TILELENGTH, 16);
        for (int x = 0; x < 32; x += 16) ok = ok && (TIFFWriteTile(t, buf, x, 0, 0, 0) >= 0);
    } else {
        TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, 16);
        for (int y = 0; y < 16; y++) ok = ok && (TIFFWriteScanline(t, buf + y * 32 * 5, y, 0) >= 0);
    }
    TIFFClose(t);
    return ok;
}

/* 32 x 16 CMYK and an alpha with the press profile, by libjxl, lossless */
static int write_cmyka_jxl(const char *path) {
    static BYTE px[32 * 16 * 4], k[32 * 16];
    JxlPixelFormat pf = { 4, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 }, pk = { 1, JXL_TYPE_UINT8, JXL_NATIVE_ENDIAN, 0 };
    JxlEncoder *enc = JxlEncoderCreate(NULL);
    JxlEncoderFrameSettings *fs = NULL;
    JxlBasicInfo bi;
    JxlExtraChannelInfo alpha, black;
    size_t cap = 1 << 16, pos = 0;
    BYTE *out = NULL;
    int ok = 0;
    if (!enc) return 0;
    for (size_t i = 0; i < sizeof(px); i++) px[i] = (BYTE)(i * 5 + (i >> 4));
    for (size_t i = 0; i < sizeof(k); i++) k[i] = (BYTE)(i * 3);
    JxlEncoderInitBasicInfo(&bi);
    bi.xsize = 32; bi.ysize = 16; bi.bits_per_sample = 8; bi.num_color_channels = 3;
    bi.alpha_bits = 8; bi.num_extra_channels = 2; bi.uses_original_profile = JXL_TRUE;
    JxlEncoderInitExtraChannelInfo(JXL_CHANNEL_ALPHA, &alpha);
    JxlEncoderInitExtraChannelInfo(JXL_CHANNEL_BLACK, &black);
    alpha.bits_per_sample = black.bits_per_sample = 8;
    if (JxlEncoderSetBasicInfo(enc, &bi) == JXL_ENC_SUCCESS && JxlEncoderSetExtraChannelInfo(enc, 0, &alpha) == JXL_ENC_SUCCESS &&
        JxlEncoderSetExtraChannelInfo(enc, 1, &black) == JXL_ENC_SUCCESS && JxlEncoderSetICCProfile(enc, s_press, s_press_size) == JXL_ENC_SUCCESS) {
        fs = JxlEncoderFrameSettingsCreate(enc, NULL);
    }
    if (fs && JxlEncoderSetFrameLossless(fs, JXL_TRUE) == JXL_ENC_SUCCESS && JxlEncoderAddImageFrame(fs, &pf, px, sizeof(px)) == JXL_ENC_SUCCESS &&
        JxlEncoderSetExtraChannelBuffer(fs, &pk, k, sizeof(k), 1) == JXL_ENC_SUCCESS && (out = (BYTE *)malloc(cap)) != NULL) {
        JxlEncoderCloseInput(enc);
        for (;;) {
            uint8_t *next = out + pos;
            size_t avail = cap - pos;
            JxlEncoderStatus st = JxlEncoderProcessOutput(enc, &next, &avail);
            pos = (size_t)(next - out);
            if (st == JXL_ENC_NEED_MORE_OUTPUT) {
                BYTE *more = (BYTE *)realloc(out, cap * 2);
                if (!more) break;
                out = more;
                cap *= 2;
                continue;
            }
            if (st == JXL_ENC_SUCCESS) {
                FILE *f = fopen(path, "wb");
                ok = f && (fwrite(out, 1, pos, f) == pos);
                if (f) fclose(f);
            }
            break;
        }
    }
    free(out);
    JxlEncoderDestroy(enc);
    return ok;
}

typedef struct { FREE_IMAGE_FORMAT fif; int cmyk_flag; char path[512]; } CMYKAFile;
static CMYKAFile s_cmyka[5];
static int s_cmyka_count = 0;
/* the files a case covers */
static int s_from = 0, s_to = 0;

static void cmyka_add(FREE_IMAGE_FORMAT fif, int cmyk_flag, const char *path) {
    s_cmyka[s_cmyka_count].fif = fif;
    s_cmyka[s_cmyka_count].cmyk_flag = cmyk_flag;
    snprintf(s_cmyka[s_cmyka_count].path, sizeof(s_cmyka[0].path), "%s", path);
    s_cmyka_count++;
}

/* an RGBA JPEG XR in frequency order, which holds an index table, by FreeImage */
static char s_jxr_indexed[512];

/* TIFF strips, TIFF tiles, JPEG XL, then the JPEG XR files of ../JXR/data if there; needs the press profile */
static int cmyka_setup(void) {
    const char *dir = getenv("IO_TEST_TMP");
    char path[512];
    FIBITMAP *rgba = pattern(FIT_BITMAP, 32, 16, 32);
    int saved;
    snprintf(s_jxr_indexed, sizeof(s_jxr_indexed), "%s/fi_io_indexed.jxr", (dir && *dir) ? dir : ".");
    saved = rgba && FreeImage_Save(FIF_JXR, rgba, s_jxr_indexed, JXR_PROGRESSIVE | JXR_LOSSLESS);
    FreeImage_Unload(rgba);
    if (!saved) return 0;
    snprintf(path, sizeof(path), "%s/fi_io_cmyka.tif", (dir && *dir) ? dir : ".");
    if (!write_cmyka_tiff(path, 0)) return 0;
    cmyka_add(FIF_TIFF, TIFF_CMYK, path);
    snprintf(path, sizeof(path), "%s/fi_io_cmyka_tiles.tif", (dir && *dir) ? dir : ".");
    if (!write_cmyka_tiff(path, 1)) return 0;
    cmyka_add(FIF_TIFF, TIFF_CMYK, path);
    snprintf(path, sizeof(path), "%s/fi_io_cmyka.jxl", (dir && *dir) ? dir : ".");
    if (!write_cmyka_jxl(path)) return 0;
    cmyka_add(FIF_JXL, JXL_CMYK, path);
    if (access("../JXR/data/cmyka40.jxr", R_OK) == 0 && access("../JXR/data/cmyka80.jxr", R_OK) == 0) {
        cmyka_add(FIF_JXR, JXR_CMYK, "../JXR/data/cmyka40.jxr");
        cmyka_add(FIF_JXR, JXR_CMYK, "../JXR/data/cmyka80.jxr");
    }
    return 1;
}

/* the type, size, CMYK flag and alpha of ref: a conversion short of memory may take another route to its colors */
static int same_alpha(FIBITMAP *ref, FIBITMAP *got) {
    FREE_IMAGE_TYPE type;
    unsigned x, y;
    if (!ref || !got) return 0;
    type = FreeImage_GetImageType(ref);
    if ((type != FreeImage_GetImageType(got)) || (FreeImage_GetBPP(ref) != FreeImage_GetBPP(got)) ||
        (FreeImage_GetWidth(ref) != FreeImage_GetWidth(got)) || (FreeImage_GetHeight(ref) != FreeImage_GetHeight(got)) ||
        ((FreeImage_GetICCProfile(ref)->flags ^ FreeImage_GetICCProfile(got)->flags) & FIICC_COLOR_IS_CMYK)) return 0;
    for (y = 0; y < FreeImage_GetHeight(ref); y++) {
        const BYTE *p = FreeImage_GetScanLine(ref, y), *q = FreeImage_GetScanLine(got, y);
        for (x = 0; x < FreeImage_GetWidth(ref); x++) {
            unsigned want = 0, have = 0;
            if ((type == FIT_BITMAP) && (FreeImage_GetBPP(ref) == 32)) { want = p[4 * x + FI_RGBA_ALPHA]; have = q[4 * x + FI_RGBA_ALPHA]; }
            if (type == FIT_RGBA16) { want = ((const WORD *)p)[4 * x + 3]; have = ((const WORD *)q)[4 * x + 3]; }
            if (have != want) return 0;
        }
    }
    return 1;
}

/* a case that cannot start fails once and ends its sweep */
static int cannot_start(void) {
    return (fail_at > 1) ? NOT_REACHED : CASE_BROKEN;
}

/* plain and FIF_LOAD_DISPLAY_ICC loads: NULL, or the alpha a load without failures gives */
static int cmyka_load_case(void) {
    static const int flags[2] = { 0, FIF_LOAD_DISPLAY_ICC };
    FIBITMAP *ref[10] = { NULL }, *got[10] = { NULL };
    int i, k, n = 0, whole = 1;
    for (i = s_from; i < s_to; i++) {
        for (k = 0; k < 2; k++, n++) ref[n] = FreeImage_Load(s_cmyka[i].fif, s_cmyka[i].path, flags[k]);
    }
    for (i = 0; i < n; i++) {
        if (!ref[i]) return cannot_start();
    }

    armed = 1;
    for (i = s_from, n = 0; i < s_to; i++) {
        for (k = 0; k < 2; k++, n++) got[n] = FreeImage_Load(s_cmyka[i].fif, s_cmyka[i].path, flags[k]);
    }
    armed = 0;

    for (i = 0; i < n; i++) {
        if (got[i] && !same_alpha(ref[i], got[i])) whole = 0;
        FreeImage_Unload(got[i]);
        FreeImage_Unload(ref[i]);
    }
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* documents opened with the CMYK flag: no save of their unchanged CMYK + alpha page succeeds, none leaves a block allocated */
static int cmyka_save_case(void) {
    FIMULTIBITMAP *doc[5] = { NULL };
    FIMEMORY *mem[5] = { NULL };
    int i, saved = 0;
    long before;
    for (i = s_from; i < s_to; i++) {
        doc[i] = FreeImage_OpenMultiBitmap(s_cmyka[i].fif, s_cmyka[i].path, FALSE, TRUE, TRUE, s_cmyka[i].cmyk_flag);
        mem[i] = FreeImage_OpenMemory(NULL, 0);
        if (!doc[i] || !mem[i]) return cannot_start();
        /* a first save fills what stays allocated */
        FreeImage_SaveMultiBitmapToMemory(FIF_TIFF, doc[i], mem[i], 0);
        FreeImage_CloseMemory(mem[i]);
    }
    before = live;
    for (i = s_from; i < s_to; i++) {
        mem[i] = FreeImage_OpenMemory(NULL, 0);
        if (!mem[i]) return cannot_start();
    }

    armed = 1;
    for (i = s_from; i < s_to; i++) saved += FreeImage_SaveMultiBitmapToMemory(FIF_TIFF, doc[i], mem[i], 0) ? 1 : 0;
    armed = 0;

    for (i = s_from; i < s_to; i++) FreeImage_CloseMemory(mem[i]);
    const int whole = (live == before);
    for (i = s_from; i < s_to; i++) FreeImage_CloseMultiBitmap(doc[i], 0);
    if (seen < fail_at) return NOT_REACHED;
    return (!saved && whole) ? CASE_OK : CASE_BROKEN;
}

/* plain loads, which hold no display transform, of the CMYK + alpha files and the indexed JPEG XR: nothing left allocated */
static int cmyka_leak_case(void) {
    FIBITMAP *got[6] = { NULL };
    const char *path[6];
    FREE_IMAGE_FORMAT fif[6];
    int i, n = 0;
    long before;
    for (i = 0; i < s_cmyka_count; i++, n++) { path[n] = s_cmyka[i].path; fif[n] = s_cmyka[i].fif; }
    path[n] = s_jxr_indexed; fif[n++] = FIF_JXR;
    /* a first load fills what stays allocated */
    for (i = 0; i < n; i++) {
        got[i] = FreeImage_Load(fif[i], path[i], 0);
        if (!got[i]) return cannot_start();
        FreeImage_Unload(got[i]);
    }
    before = live;

    armed = 1;
    for (i = 0; i < n; i++) got[i] = FreeImage_Load(fif[i], path[i], 0);
    armed = 0;

    for (i = 0; i < n; i++) FreeImage_Unload(got[i]);
    if (seen < fail_at) return NOT_REACHED;
    return (live == before) ? CASE_OK : CASE_BROKEN;
}

/* --------------------------------------------------------------- TIFF: a read short of memory is no damaged strip */

/* 32 x 24 by libtiff: RGB in LZW strips of 8 rows, or RGBA in deflate 16 x 16 tiles */
static int write_coded_tiff(const char *path, int tiled) {
    static BYTE buf[4 * 16 * 16 * 4];
    uint16_t extra = EXTRASAMPLE_UNASSALPHA;
    FILE *f = fopen(path, "w+b");
    TIFF *t = f ? TIFFClientOpen(path, "w", (thandle_t)f, t_read, t_write, t_seek, t_close, t_size, t_map, t_unmap) : NULL;
    int ok = 1;
    if (!t) { if (f) fclose(f); return 0; }
    for (size_t i = 0; i < sizeof(buf); i++) buf[i] = (BYTE)(i * 11 + (i >> 6));
    TIFFSetField(t, TIFFTAG_IMAGEWIDTH, 32); TIFFSetField(t, TIFFTAG_IMAGELENGTH, 24);
    TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 8); TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, tiled ? 4 : 3);
    if (tiled) TIFFSetField(t, TIFFTAG_EXTRASAMPLES, 1, &extra);
    TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB); TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(t, TIFFTAG_COMPRESSION, tiled ? COMPRESSION_ADOBE_DEFLATE : COMPRESSION_LZW);
    if (tiled) {
        TIFFSetField(t, TIFFTAG_TILEWIDTH, 16); TIFFSetField(t, TIFFTAG_TILELENGTH, 16);
        for (int i = 0; i < 4; i++) ok = ok && (TIFFWriteTile(t, buf + i * 16 * 16 * 4, (i & 1) * 16, (i >> 1) * 16, 0, 0) >= 0);
    } else {
        TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, 8);
        for (int y = 0; y < 24; y++) ok = ok && (TIFFWriteScanline(t, buf + y * 32 * 3, y, 0) >= 0);
    }
    TIFFClose(t);
    return ok;
}

/* three 8 x 8 RGB pages in LZW, by libtiff */
static int write_pages_tiff(const char *path) {
    static BYTE row[8 * 3];
    FILE *f = fopen(path, "w+b");
    TIFF *t = f ? TIFFClientOpen(path, "w", (thandle_t)f, t_read, t_write, t_seek, t_close, t_size, t_map, t_unmap) : NULL;
    int ok = 1;
    if (!t) { if (f) fclose(f); return 0; }
    for (int p = 0; p < 3; p++) {
        TIFFSetField(t, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE); TIFFSetField(t, TIFFTAG_PAGENUMBER, p, 3);
        TIFFSetField(t, TIFFTAG_IMAGEWIDTH, 8); TIFFSetField(t, TIFFTAG_IMAGELENGTH, 8);
        TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 8); TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, 3);
        TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB); TIFFSetField(t, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
        TIFFSetField(t, TIFFTAG_COMPRESSION, COMPRESSION_LZW); TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, 8);
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8 * 3; x++) row[x] = (BYTE)(p * 50 + y * 8 + x);
            ok = ok && (TIFFWriteScanline(t, row, y, 0) >= 0);
        }
        ok = ok && TIFFWriteDirectory(t);
    }
    TIFFClose(t);
    return ok;
}

static char s_coded[4][512];

/* LZW strips, deflate tiles and a FreeImage TIFF_JPEG save with a thumbnail in a SubIFD: libtiff's, zlib's and libjpeg's allocations */
static int coded_setup(void) {
    const char *dir = getenv("IO_TEST_TMP");
    FIBITMAP *dib = pattern(FIT_BITMAP, 32, 24, 24), *thumb = pattern(FIT_BITMAP, 16, 16, 24), *back = NULL;
    int ok;
    snprintf(s_coded[0], sizeof(s_coded[0]), "%s/fi_io_lzw.tif", (dir && *dir) ? dir : ".");
    snprintf(s_coded[1], sizeof(s_coded[1]), "%s/fi_io_deflate_tiles.tif", (dir && *dir) ? dir : ".");
    snprintf(s_coded[2], sizeof(s_coded[2]), "%s/fi_io_jpeg.tif", (dir && *dir) ? dir : ".");
    snprintf(s_coded[3], sizeof(s_coded[3]), "%s/fi_io_pages.tif", (dir && *dir) ? dir : ".");
    ok = dib && thumb && FreeImage_SetThumbnail(dib, thumb) && write_pages_tiff(s_coded[3]);
    ok = ok && write_coded_tiff(s_coded[0], 0) && write_coded_tiff(s_coded[1], 1) && FreeImage_Save(FIF_TIFF, dib, s_coded[2], TIFF_JPEG);
    /* the thumbnail made it into the file */
    ok = ok && (back = FreeImage_Load(FIF_TIFF, s_coded[2], 0)) != NULL && FreeImage_GetThumbnail(back);
    FreeImage_Unload(back);
    FreeImage_Unload(thumb);
    FreeImage_Unload(dib);
    return ok;
}

/* same type, size and pixels */
static int same_pixels(FIBITMAP *ref, FIBITMAP *got) {
    unsigned y;
    if (!ref || !got || (FreeImage_GetImageType(ref) != FreeImage_GetImageType(got)) || (FreeImage_GetBPP(ref) != FreeImage_GetBPP(got)) ||
        (FreeImage_GetWidth(ref) != FreeImage_GetWidth(got)) || (FreeImage_GetHeight(ref) != FreeImage_GetHeight(got))) return 0;
    for (y = 0; y < FreeImage_GetHeight(ref); y++) {
        if (memcmp(FreeImage_GetScanLine(ref, y), FreeImage_GetScanLine(got, y), FreeImage_GetLine(ref))) return 0;
    }
    return 1;
}

/* NULL, or every pixel of a load without failures: loaded alone, and as page 0 of a document, which reads its directory again;
   nothing left allocated */
static int coded_load_case(void) {
    FIBITMAP *ref[3], *got[3], *page[3];
    FIMULTIBITMAP *doc[3];
    int i, whole = 1;
    long before;
    for (i = 0; i < 3; i++) {
        ref[i] = FreeImage_Load(FIF_TIFF, s_coded[i], 0);
        if (!ref[i]) return cannot_start();
        /* a first document fills what stays allocated */
        doc[i] = FreeImage_OpenMultiBitmap(FIF_TIFF, s_coded[i], FALSE, TRUE, TRUE, 0);
        page[i] = doc[i] ? FreeImage_LockPage(doc[i], 0) : NULL;
        if (!page[i]) return cannot_start();
        FreeImage_UnlockPage(doc[i], page[i], FALSE);
        FreeImage_CloseMultiBitmap(doc[i], 0);
    }
    /* an open document holds its directory: counted once it is closed */
    before = live;
    for (i = 0; i < 3; i++) {
        doc[i] = FreeImage_OpenMultiBitmap(FIF_TIFF, s_coded[i], FALSE, TRUE, TRUE, 0);
        if (!doc[i]) return cannot_start();
    }

    armed = 1;
    for (i = 0; i < 3; i++) {
        got[i] = FreeImage_Load(FIF_TIFF, s_coded[i], 0);
        page[i] = FreeImage_LockPage(doc[i], 0);
    }
    armed = 0;

    for (i = 0; i < 3; i++) {
        if ((got[i] && !same_pixels(ref[i], got[i])) || (page[i] && !same_pixels(ref[i], page[i]))) whole = 0;
        FreeImage_Unload(got[i]);
        if (page[i]) FreeImage_UnlockPage(doc[i], page[i], FALSE);
        FreeImage_CloseMultiBitmap(doc[i], 0);
    }
    whole = whole && (live == before);
    for (i = 0; i < 3; i++) FreeImage_Unload(ref[i]);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* a three-page document opened short of memory: all three pages, or no document, and nothing left allocated */
static int pages_case(void) {
    FIMULTIBITMAP *doc;
    int pages = 0;
    long before;
    /* a first open fills what stays allocated */
    doc = FreeImage_OpenMultiBitmap(FIF_TIFF, s_coded[3], FALSE, TRUE, TRUE, 0);
    if (!doc) return cannot_start();
    FreeImage_CloseMultiBitmap(doc, 0);
    before = live;

    armed = 1;
    doc = FreeImage_OpenMultiBitmap(FIF_TIFF, s_coded[3], FALSE, TRUE, TRUE, 0);
    if (doc) pages = FreeImage_GetPageCount(doc);
    armed = 0;

    if (doc) FreeImage_CloseMultiBitmap(doc, 0);
    if (seen < fail_at) return NOT_REACHED;
    return ((!doc || (pages == 3)) && (live == before)) ? CASE_OK : CASE_BROKEN;
}

/* --------------------------------------------------------------- JPEG XL: libjxl short of memory */

/* two Adobe RGB pages of 257 x 16, lossy: a page decodes in two groups, two tasks of one parallel run, both on the calling
   thread */
static char s_jxl[512];
static FIBITMAP *s_jxl_page[2];
/* the same pages lossless: libjxl reads the ICC profile in the header pass */
static char s_jxl_icc[512];

static int write_jxl_pages(const char *path, int flags) {
    FIMULTIBITMAP *doc;
    int i, ok = 1;
    remove(path);
    doc = FreeImage_OpenMultiBitmap(FIF_JXL, path, TRUE, FALSE, TRUE, 0);
    if (!doc) return 0;
    for (i = 0; i < 2; i++) {
        FIBITMAP *page = pattern(FIT_BITMAP, 257, 16, 24);
        unsigned k;
        if (!page) {
            ok = 0;
            continue;
        }
        for (k = 0; k < FreeImage_GetPitch(page) * 16; k++) FreeImage_GetBits(page)[k] ^= (BYTE)(i * 0x55);
        FreeImage_CreateICCProfile(page, (void *)s_adobe, (long)s_adobe_size);
        frame_time(page);
        ok = FreeImage_AppendPage(doc, page) && ok;
        FreeImage_Unload(page);
    }
    return FreeImage_CloseMultiBitmap(doc, flags) && ok;
}

static int jxl_setup(void) {
    const char *dir = getenv("IO_TEST_TMP");
    FIMULTIBITMAP *doc;
    int i, ok;
    snprintf(s_jxl, sizeof(s_jxl), "%s/fi_io_groups.jxl", (dir && *dir) ? dir : ".");
    snprintf(s_jxl_icc, sizeof(s_jxl_icc), "%s/fi_io_icc.jxl", (dir && *dir) ? dir : ".");
    ok = write_jxl_pages(s_jxl, 0) && write_jxl_pages(s_jxl_icc, JXL_LOSSLESS);
    doc = ok ? FreeImage_OpenMultiBitmap(FIF_JXL, s_jxl, FALSE, TRUE, TRUE, 0) : NULL;
    ok = doc && (FreeImage_GetPageCount(doc) == 2);
    for (i = 0; ok && (i < 2); i++) {
        FIBITMAP *page = FreeImage_LockPage(doc, i);
        s_jxl_page[i] = page ? FreeImage_Clone(page) : NULL;
        if (page) FreeImage_UnlockPage(doc, page, FALSE);
        ok = s_jxl_page[i] != NULL;
    }
    if (doc) FreeImage_CloseMultiBitmap(doc, 0);
    return ok;
}

/* a page locked short of memory, libjxl's tasks throwing too: NULL or the page, and every page locks after it as without
   failures (an exception out of a task left libjxl's runner counting it, and the next page waited for it: the alarm) */
static int jxl_lock_case(void) {
    FIMULTIBITMAP *doc = FreeImage_OpenMultiBitmap(FIF_JXL, s_jxl, FALSE, TRUE, TRUE, 0);
    FIBITMAP *page;
    int i, whole = 1;
    if (!doc) return cannot_start();
    alarm(10);

    armed = 1; fail_new = 1;
    page = FreeImage_LockPage(doc, 0);
    armed = 0; fail_new = 0;

    if (page && !same_pixels(s_jxl_page[0], page)) whole = 0;
    if (page) FreeImage_UnlockPage(doc, page, FALSE);
    for (i = 0; i < 2; i++) {
        page = FreeImage_LockPage(doc, i);
        if (!same_pixels(s_jxl_page[i], page)) whole = 0;
        if (page) FreeImage_UnlockPage(doc, page, FALSE);
    }
    FreeImage_CloseMultiBitmap(doc, 0);
    if (seen < fail_at) return NOT_REACHED;
    return whole ? CASE_OK : CASE_BROKEN;
}

/* the lossless document opened short of memory: both pages, or no document, and nothing left allocated (an ICC profile
   libjxl could not decode passed for a cut file: one page, and a save of the document dropped the other) */
static int jxl_open_case(void) {
    FIMULTIBITMAP *doc;
    int pages = 0;
    long before;
    /* a first open fills what stays allocated */
    doc = FreeImage_OpenMultiBitmap(FIF_JXL, s_jxl_icc, FALSE, TRUE, TRUE, 0);
    if (!doc) return cannot_start();
    FreeImage_CloseMultiBitmap(doc, 0);
    before = live;

    armed = 1;
    doc = FreeImage_OpenMultiBitmap(FIF_JXL, s_jxl_icc, FALSE, TRUE, TRUE, 0);
    if (doc) pages = FreeImage_GetPageCount(doc);
    armed = 0;

    if (doc) FreeImage_CloseMultiBitmap(doc, 0);
    if (seen < fail_at) return NOT_REACHED;
    return ((!doc || (pages == 2)) && (live == before)) ? CASE_OK : CASE_BROKEN;
}

/* runs 'run' once in a child: CASE_OK, or a wrong result or a crash */
static int in_child(int (*run)(void)) {
    int status = 0;
    pid_t pid = fork();
    if (pid == 0) {
        FreeImage_Initialise(FALSE);
        FreeImage_SetOutputMessage(quiet);
        _exit(run());
    }
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && (WEXITSTATUS(status) == CASE_OK);
}

/* a read callback that leaves ENOMEM in errno, as a caller's own code may */
static unsigned DLL_CALLCONV errno_read(void *buffer, unsigned size, unsigned count, fi_handle handle) {
    const unsigned n = (unsigned)fread(buffer, size, count, (FILE *)handle);
    errno = ENOMEM;
    return n;
}
static unsigned DLL_CALLCONV errno_write(void *buffer, unsigned size, unsigned count, fi_handle handle) { return (unsigned)fwrite(buffer, size, count, (FILE *)handle); }
static int DLL_CALLCONV errno_seek(fi_handle handle, INT64 offset, int origin) { return fseeko((FILE *)handle, (off_t)offset, origin); }
static INT64 DLL_CALLCONV errno_tell(fi_handle handle) { return (INT64)ftello((FILE *)handle); }

static FIBITMAP *errno_load(const char *path) {
    FreeImageIO io = { errno_read, errno_write, errno_seek, errno_tell };
    FILE *f = fopen(path, "rb");
    FIBITMAP *dib = f ? FreeImage_LoadFromHandle(FIF_TIFF, &io, (fi_handle)f, 0) : NULL;
    if (f) fclose(f);
    return dib;
}

static char s_damaged[512];

/* the LZW file with bytes of its second strip overwritten: a damaged strip, left blank */
static int write_damaged_tiff(void) {
    uint64_t *offsets = NULL;
    long at = 0, i = 0;
    int c, ok;
    FILE *f = fopen(s_coded[0], "rb"), *g;
    TIFF *t = f ? TIFFClientOpen(s_coded[0], "r", (thandle_t)f, t_read, t_write, t_seek, t_close, t_size, t_map, t_unmap) : NULL;
    if (!t) { if (f) fclose(f); return 0; }
    if (TIFFGetField(t, TIFFTAG_STRIPOFFSETS, &offsets) && (TIFFNumberOfStrips(t) > 1)) at = (long)offsets[1];
    TIFFClose(t);
    f = fopen(s_coded[0], "rb");
    g = fopen(s_damaged, "wb");
    ok = (at > 0) && f && g;
    while (ok && (c = fgetc(f)) != EOF) {
        fputc(((i >= at + 2) && (i < at + 12)) ? 0xFF : c, g);
        i++;
    }
    if (f) fclose(f);
    if (g) fclose(g);
    return ok;
}

/* through that callback: the LZW file whole, a three-page document, and the damaged file salvaged as without it */
static int errno_io_case(void) {
    FIBITMAP *ref = FreeImage_Load(FIF_TIFF, s_coded[0], 0), *got = errno_load(s_coded[0]);
    FIBITMAP *ref_damaged = FreeImage_Load(FIF_TIFF, s_damaged, 0), *got_damaged = errno_load(s_damaged);
    FreeImageIO io = { errno_read, errno_write, errno_seek, errno_tell };
    FILE *f = fopen(s_coded[3], "rb");
    FIMULTIBITMAP *doc = f ? FreeImage_OpenMultiBitmapFromHandle(FIF_TIFF, &io, (fi_handle)f, 0) : NULL;
    FIBITMAP *page = doc ? FreeImage_LockPage(doc, 2) : NULL;
    const int ok = same_pixels(ref, got) && same_pixels(ref_damaged, got_damaged) && (FreeImage_GetPageCount(doc) == 3) && page;
    if (page) FreeImage_UnlockPage(doc, page, FALSE);
    if (doc) FreeImage_CloseMultiBitmap(doc, 0);
    if (f) fclose(f);
    FreeImage_Unload(ref);
    FreeImage_Unload(got);
    FreeImage_Unload(ref_damaged);
    FreeImage_Unload(got_damaged);
    return ok ? CASE_OK : CASE_BROKEN;
}

/* 16 x 16 grey pages, each with an ICC profile of 'size' bytes (only its bytes count) and, as most files have, another custom tag, by libtiff */
static int write_icc_tiff(const char *path, size_t size, int pages) {
    static BYTE row[16];
    BYTE *icc = (BYTE *)malloc(size);
    FILE *f = fopen(path, "w+b");
    TIFF *t = (f && icc) ? TIFFClientOpen(path, "w", (thandle_t)f, t_read, t_write, t_seek, t_close, t_size, t_map, t_unmap) : NULL;
    int ok = 1;
    if (!t) { if (f) fclose(f); free(icc); return 0; }
    for (size_t i = 0; i < size; i++) icc[i] = (BYTE)(i * 7);
    for (int p = 0; p < pages; p++) {
        TIFFSetField(t, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE); TIFFSetField(t, TIFFTAG_PAGENUMBER, p, pages);
        TIFFSetField(t, TIFFTAG_IMAGEWIDTH, 16); TIFFSetField(t, TIFFTAG_IMAGELENGTH, 16);
        TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 8); TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, 1);
        TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISBLACK); TIFFSetField(t, TIFFTAG_ROWSPERSTRIP, 16);
        TIFFSetField(t, TIFFTAG_ICCPROFILE, (uint32_t)size, icc);
        TIFFSetField(t, TIFFTAG_SOFTWARE, "allocfail");
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) row[x] = (BYTE)(p * 40 + y * 16 + x);
            ok = ok && (TIFFWriteScanline(t, row, y, 0) >= 0);
        }
        ok = ok && TIFFWriteDirectory(t);
    }
    TIFFClose(t);
    free(icc);
    return ok;
}

static char s_icc_big[512], s_icc_small[512];

static int icc_tiff_setup(void) {
    const char *dir = getenv("IO_TEST_TMP");
    snprintf(s_icc_big, sizeof(s_icc_big), "%s/fi_io_icc20.tif", (dir && *dir) ? dir : ".");
    snprintf(s_icc_small, sizeof(s_icc_small), "%s/fi_io_icc15.tif", (dir && *dir) ? dir : ".");
    return write_icc_tiff(s_icc_big, (size_t)20 << 20, 2) && write_icc_tiff(s_icc_small, (size_t)15 << 20, 1);
}

static int has_icc(FIBITMAP *dib) {
    return dib && (FreeImage_GetICCProfile(dib)->size > 0);
}

/* the 20 MB ICC profiles of a two-page TIFF that libtiff cannot read, or reads and cannot copy (kinds), are left out: the pages load,
   the document opens, without them, and no block is left */
static int big_tag(int kinds) {
    FIBITMAP *ref = FreeImage_Load(FIF_TIFF, s_icc_big, 0), *got, *page[2] = { NULL, NULL };
    FIMULTIBITMAP *doc = FreeImage_OpenMultiBitmap(FIF_TIFF, s_icc_big, FALSE, TRUE, TRUE, 0);
    long before;
    int ok, i;
    /* a first document fills what stays allocated */
    if (!has_icc(ref) || !doc) return CASE_BROKEN;
    FreeImage_CloseMultiBitmap(doc, 0);
    before = live;
    fail_size = (size_t)17 << 20;
    fail_kinds = kinds;
    got = FreeImage_Load(FIF_TIFF, s_icc_big, 0);
    doc = FreeImage_OpenMultiBitmap(FIF_TIFF, s_icc_big, FALSE, TRUE, TRUE, 0);
    for (i = 0; doc && (i < 2); i++) page[i] = FreeImage_LockPage(doc, i);
    ok = same_pixels(ref, got) && !has_icc(got) && (FreeImage_GetPageCount(doc) == 2) && page[0] && page[1] && !has_icc(page[1]);
    for (i = 0; i < 2; i++) if (page[i]) FreeImage_UnlockPage(doc, page[i], FALSE);
    if (doc) FreeImage_CloseMultiBitmap(doc, 0);
    FreeImage_Unload(got);
    fail_size = 0;
    ok = ok && (live == before);
    FreeImage_Unload(ref);
    return ok ? CASE_OK : CASE_BROKEN;
}

static int big_tag_case(void) { return big_tag(KIND_GROW | KIND_FRESH); }
static int big_copy_case(void) { return big_tag(KIND_FRESH); }

/* a 15 MB block that cannot be had is memory running out: no image, no document, no block left */
static int lost_tag_case(void) {
    FIBITMAP *dib;
    FIMULTIBITMAP *doc;
    long before = live;
    int ok;
    fail_size = (size_t)12 << 20;
    fail_kinds = KIND_GROW | KIND_FRESH;
    dib = FreeImage_Load(FIF_TIFF, s_icc_small, 0);
    doc = FreeImage_OpenMultiBitmap(FIF_TIFF, s_icc_small, FALSE, TRUE, TRUE, 0);
    fail_size = 0;
    ok = !dib && !doc;
    FreeImage_Unload(dib);
    if (doc) FreeImage_CloseMultiBitmap(doc, 0);
    return (ok && (live == before)) ? CASE_OK : CASE_BROKEN;
}

/* src's layout, red, green and blue equal, src's alpha */
static int grey_rgba_ok(FIBITMAP *dib, FIBITMAP *src) {
    unsigned x, y, w = FreeImage_GetWidth(src);
    FREE_IMAGE_TYPE t = FreeImage_GetImageType(src);
    if (!dib || FreeImage_GetImageType(dib) != t || FreeImage_GetBPP(dib) != FreeImage_GetBPP(src)) return 0;
    for (y = 0; y < FreeImage_GetHeight(src); y++) {
        const BYTE *d = FreeImage_GetScanLine(dib, y), *s = FreeImage_GetScanLine(src, y);
        for (x = 0; x < w; x++) {
            if (t == FIT_BITMAP) {
                const BYTE *p = d + 4 * x, *q = s + 4 * x;
                if (p[0] != p[1] || p[0] != p[2] || p[FI_RGBA_ALPHA] != q[FI_RGBA_ALPHA]) return 0;
            } else if (t == FIT_RGBA16) {
                const FIRGBA16 *p = (const FIRGBA16 *)d + x, *q = (const FIRGBA16 *)s + x;
                if (p->red != p->green || p->red != p->blue || p->alpha != q->alpha) return 0;
            } else {
                const FIRGBAF *p = (const FIRGBAF *)d + x, *q = (const FIRGBAF *)s + x;
                if (p->red != p->green || p->red != p->blue || p->alpha != q->alpha) return 0;
            }
        }
    }
    return 1;
}

/* RGBA and RGBA16 in Adobe RGB, untagged RGBAF */
static FIBITMAP *grey_alpha_source(int i) {
    FIBITMAP *dib;
    unsigned x, y;
    if (i == 0) return tagged(FIT_BITMAP, 32, s_adobe, s_adobe_size);
    if (i == 1) return tagged(FIT_RGBA16, 64, s_adobe, s_adobe_size);
    dib = FreeImage_AllocateT(FIT_RGBAF, 40, 24, 128, 0, 0, 0);
    for (y = 0; dib && y < 24; y++) {
        float *f = (float *)FreeImage_GetScanLine(dib, y);
        for (x = 0; x < 40 * 4; x++) f[x] = (float)((x * 7 + y * 13) % 97) / 80.0f;
    }
    return dib;
}

/* RGBA, RGBA16 and RGBAF to a grey profile, copied and in place: RGBA with the profile, NULL, or FALSE and the image as it
   was; nothing left allocated. Not compared with each other: out of memory, an embedded profile is ignored as invalid */
static int icc_grey_alpha_case(void) {
    DWORD gsize = 0;
    const void *grey = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_GRAY, &gsize);
    long before = 0;
    int whole = 1, pass, i;
    /* the first pass, without failures, fills what stays allocated */
    for (pass = 0; pass < 2; pass++) {
        FIBITMAP *src[3], *copy[3], *res[3];
        BOOL applied[3];
        if (pass) before = live;
        for (i = 0; i < 3; i++) {
            src[i] = grey_alpha_source(i);
            copy[i] = src[i] ? FreeImage_Clone(src[i]) : NULL;
            if (!src[i] || !copy[i]) return CASE_BROKEN;
        }
        armed = pass; fail_new = pass;
        for (i = 0; i < 3; i++) {
            res[i] = FreeImage_ConvertToICCProfile(src[i], grey, gsize, FICMS_INTENT_RELATIVE_COLORIMETRIC);
            applied[i] = FreeImage_ApplyICCProfile(copy[i], grey, gsize, FICMS_INTENT_RELATIVE_COLORIMETRIC);
        }
        armed = 0; fail_new = 0;
        for (i = 0; i < 3; i++) {
            const FIICCPROFILE *was = FreeImage_GetICCProfile(src[i]);
            if (res[i]) whole = whole && converted_ok(res[i], grey, gsize) && grey_rgba_ok(res[i], src[i]);
            else whole = whole && pass;
            if (applied[i]) whole = whole && has_profile(copy[i], grey, gsize) && grey_rgba_ok(copy[i], src[i]);
            else whole = whole && pass && same_pixels(src[i], copy[i]) && (was->data ? has_profile(copy[i], was->data, (DWORD)was->size) : !FreeImage_GetICCProfile(copy[i])->data);
            FreeImage_Unload(res[i]);
            FreeImage_Unload(copy[i]);
            FreeImage_Unload(src[i]);
        }
    }
    if (seen < fail_at) return NOT_REACHED;
    return (whole && (live == before)) ? CASE_OK : CASE_BROKEN;
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
    sweep("long keys looked up and removed, a profile destroyed: no exception", meta_longkey_case);
    sweep("LoadMultiBitmapFromMemory: a throwing page count does not escape", loadmem_case);
    sweep("GIF save: TRUE only for a file that loads", gif_save);
    sweep("GIF load: no crash", gif_load);
    sweep("ICO save: TRUE only for a file that loads", ico_save);
    sweep("JPEG 2000 save: TRUE only for a file that loads", j2k_save);
    sweep("JPEG 2000 load: no crash", j2k_load);
    sweep("EXR save: TRUE only for a file that loads", exr_save);
    sweep("JNG save: TRUE only for a file that loads", jng_save);
    sweep("TIFF load: no crash", tiff_load);
    if (coded_setup()) {
        sweep("TIFF loads, LZW strips, deflate tiles, JPEG: NULL or every pixel, no leak", coded_load_case);
        sweep("TIFF document of three pages: all three, or not opened, no leak", pages_case);
        snprintf(s_damaged, sizeof(s_damaged), "%s.damaged.tif", s_coded[0]);
        report("TIFF read by a callback leaving ENOMEM: loads as without it", write_damaged_tiff() && in_child(errno_io_case));
        remove(s_damaged);
    } else {
        report("TIFF: the test files", 0);
    }
    for (int i = 0; i < 4; i++) remove(s_coded[i]);
    if (icc_tiff_setup()) {
        report("TIFF, 20 MB ICC profile libtiff cannot read: loads without it", in_child(big_tag_case));
        report("TIFF, 20 MB ICC profile read but not copied: loads without it", in_child(big_copy_case));
        report("TIFF, 15 MB ICC profile libtiff cannot read: NULL, no leak", in_child(lost_tag_case));
    } else {
        report("TIFF: the ICC test files", 0);
    }
    remove(s_icc_big);
    remove(s_icc_small);
    sweep("PSD load with IPTC: no crash, no tag without its value, no leak", psd_iptc_case);
    if (!icc_setup()) {
        report("color management: the test profiles", 0);
    } else {
        sweep("ICC conversions, 8-bit, 16-bit, in place: NULL or tagged", icc_convert_case);
        sweep("ICC RGBA, RGBA16, RGBAF to grey, in place too: RGBA or as before, no leak", icc_grey_alpha_case);
        sweep("ICC CMYK and back, black point compensation: NULL or tagged", icc_cmyk_case);
        sweep("ICC soft proof with a gamut check: NULL or tagged", icc_proof_case);
        if (s_lut) sweep("ICC LUT profile, Lab connection space: NULL or sRGB", icc_lut_case);
        sweep("display profile set: the new one or the old one, no leak", icc_display_set_case);
        sweep("FIF_LOAD_DISPLAY_ICC load: converted, as loaded or NULL, no leak", icc_display_load_case);
        sweep("FIF_LOAD_DISPLAY_ICC CMYK load: replaced with RGB or NULL, no leak", icc_display_cmyk_case);
        sweep("APNG save, frames in different profiles: TRUE only for a file that loads", apng_case);
        sweep("WebP animation, frames in different profiles: TRUE only for a file that loads", webp_anim_case);
        if (!cmyka_setup()) {
            report("CMYK + alpha: the test files", 0);
        } else {
            s_from = 0; s_to = 2;
            sweep("CMYK + alpha TIFF loads, strips and tiles: their alpha, or NULL", cmyka_load_case);
            s_from = 2; s_to = 3;
            sweep("CMYK + alpha JPEG XL loads: their alpha, or NULL", cmyka_load_case);
            if (s_cmyka_count > 3) {
                s_from = 3; s_to = s_cmyka_count;
                sweep("CMYK + alpha JPEG XR loads, 8- and 16-bit: their alpha, or NULL", cmyka_load_case);
            } else {
                printf("  %-60s %s\n", "CMYK + alpha JPEG XR: no ../JXR/data/cmyka40.jxr", "skipped");
            }
            s_from = 0; s_to = s_cmyka_count;
            sweep("CMYK + alpha documents opened with their CMYK flag: no save succeeds, no leak", cmyka_save_case);
            sweep("CMYK + alpha plain loads, an indexed JPEG XR: no leak", cmyka_leak_case);
        }
        for (int i = 0; i < s_cmyka_count; i++) {
            if (s_cmyka[i].fif != FIF_JXR) remove(s_cmyka[i].path);
        }
        remove(s_jxr_indexed);
        if (!jxl_setup()) {
            report("JPEG XL: the test file", 0);
        } else {
            sweep("JPEG XL page locked short of memory: NULL or it, every page after", jxl_lock_case);
            sweep("JPEG XL document opened short of memory: both pages or none, no leak", jxl_open_case);
        }
        remove(s_jxl);
        remove(s_jxl_icc);
        for (int i = 0; i < 2; i++) FreeImage_Unload(s_jxl_page[i]);
    }
#endif
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
