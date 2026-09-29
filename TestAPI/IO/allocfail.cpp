/* FreeImage 3 - I/O test: every allocation libfreeimage.a makes, failed in turn, crashes nothing */
/* malloc, calloc and realloc are wrapped at link time, new(std::nothrow) replaced; each case runs in a child per failed allocation */
#include <new>
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

#endif /* !_WIN32 */

int main(void) {
    printf("FreeImage %s - allocation failures\n", FreeImage_GetVersion());
    report("a model whose only tag was deleted enumerates as empty", empty_model());
#ifdef _WIN32
    printf("  %-60s %s\n", "needs fork() and a linker with --wrap", "skipped");
#else
    sweep("tags and metadata: no crash, no tag without its value", tags_case);
    sweep("WebP save: TRUE only for a file that loads", webp_case);
    sweep("GIF save: TRUE only for a file that loads", gif_save);
    sweep("GIF load: no crash", gif_load);
    sweep("ICO save: TRUE only for a file that loads", ico_save);
    sweep("JPEG 2000 save: TRUE only for a file that loads", j2k_save);
    sweep("JPEG 2000 load: no crash", j2k_load);
    sweep("EXR save: TRUE only for a file that loads", exr_save);
    sweep("JNG save: TRUE only for a file that loads", jng_save);
    sweep("TIFF load: no crash", tiff_load);
#endif
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
