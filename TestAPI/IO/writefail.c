/* FreeImage 3 - I/O test: a save whose writes fail, as on a full disk, returns FALSE, and returns */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// windows.h before FreeImage.h, which otherwise defines its guard and stands in for its types
#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif
#include "FreeImage.h"

static int failures = 0;

static void report(const char *what, int ok) {
    printf("  %-60s %s\n", what, ok ? "ok" : "FAILED");
    if (!ok) failures++;
}

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

/* a FreeImageIO over memory whose writes stop after 'budget' bytes */
typedef struct { BYTE *buf; INT64 cap, pos, len, budget; } sink_t;

static unsigned DLL_CALLCONV s_read(void *b, unsigned s, unsigned c, fi_handle h) {
    sink_t *k = (sink_t *)h; INT64 want = (INT64)s * c, have = k->len - k->pos;
    if (want > have) want = (have < 0) ? 0 : have;
    memcpy(b, k->buf + k->pos, (size_t)want); k->pos += want;
    return s ? (unsigned)(want / s) : 0;
}
static unsigned DLL_CALLCONV s_write(void *b, unsigned s, unsigned c, fi_handle h) {
    sink_t *k = (sink_t *)h; INT64 want = (INT64)s * c, room = k->budget - k->pos;
    unsigned items = c;
    if (room < 0) room = 0;
    if (want > room) { items = s ? (unsigned)(room / s) : 0; want = (INT64)items * s; }
    if (k->pos + want > k->cap) { k->cap = (k->pos + want) * 2; k->buf = (BYTE *)realloc(k->buf, (size_t)k->cap); }
    memcpy(k->buf + k->pos, b, (size_t)want); k->pos += want;
    if (k->pos > k->len) k->len = k->pos;
    return items;
}
static int DLL_CALLCONV s_seek(fi_handle h, INT64 off, int origin) {
    sink_t *k = (sink_t *)h; INT64 base = (origin == SEEK_SET) ? 0 : (origin == SEEK_CUR) ? k->pos : k->len;
    if (base + off < 0) return -1;
    k->pos = base + off; return 0;
}
static INT64 DLL_CALLCONV s_tell(fi_handle h) { return ((sink_t *)h)->pos; }

/* the size of the save, and whether it succeeded, with writes stopping after 'budget' bytes */
static INT64 save(FREE_IMAGE_FORMAT fif, FIBITMAP *dib, int flags, INT64 budget, BOOL *ok) {
    FreeImageIO io = { s_read, s_write, s_seek, s_tell };
    sink_t k;
    memset(&k, 0, sizeof(k));
    k.budget = budget;
    k.cap = 1 << 16;
    k.buf = (BYTE *)malloc((size_t)k.cap);
    *ok = FreeImage_SaveToHandle(fif, dib, &io, (fi_handle)&k, flags);
    free(k.buf);
    return k.len;
}

/* a save that never returns fails here instead of hanging the suite */
#define WATCHDOG_SECONDS 120
#ifdef _WIN32
static DWORD WINAPI watchdog(LPVOID arg) {
    (void)arg;
    Sleep(WATCHDOG_SECONDS * 1000);
    printf("  a save did not return within %d s                           FAILED\n", WATCHDOG_SECONDS);
    fflush(stdout);
    ExitProcess(3);
    return 0;
}
#else
static void watchdog(int sig) {
    static const char text[] = "  a save did not return within 120 s                           FAILED\n";
    (void)sig;
    if (write(1, text, sizeof(text) - 1) < 0) _exit(4);
    _exit(3);
}
#endif

int main(void) {
    FreeImage_Initialise(FALSE);
    FreeImage_SetOutputMessage(quiet);
    printf("FreeImage %s, a save whose writes stop after 100 bytes or half way must return FALSE\n", FreeImage_GetVersion());
#ifdef _WIN32
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
#else
    signal(SIGALRM, watchdog);
    alarm(WATCHDOG_SECONDS);
#endif

    FIBITMAP *rgb = FreeImage_Allocate(96, 64, 24, 0, 0, 0);
    unsigned seed = 12345;
    for (unsigned y = 0; y < 64; y++) {
        BYTE *line = FreeImage_GetScanLine(rgb, y);
        for (unsigned x = 0; x < 96 * 3; x++) { seed = seed * 1103515245u + 12345u; line[x] = (BYTE)(seed >> 16); }
    }
    FIBITMAP *rgba = FreeImage_ConvertTo32Bits(rgb);
    FIBITMAP *grey = FreeImage_ConvertTo8Bits(rgb);
    FIBITMAP *mono = FreeImage_Threshold(rgb, 128);
    FIBITMAP *flt = FreeImage_ConvertToFloat(rgb);
    FIBITMAP *rgbf = FreeImage_ConvertToRGBF(rgb);
    FIBITMAP *square = FreeImage_Rescale(rgb, 64, 64, FILTER_BOX);

    struct { FREE_IMAGE_FORMAT fif; FIBITMAP *dib; int flags; const char *name; } t[] = {
        { FIF_BMP, rgb, 0, "BMP" }, { FIF_ICO, square, 0, "ICO" }, { FIF_JPEG, rgb, 0, "JPEG" }, { FIF_JNG, rgb, 0, "JNG" },
        { FIF_PBM, mono, PNM_SAVE_ASCII, "PBM ascii" }, { FIF_PBMRAW, mono, PNM_SAVE_RAW, "PBM raw" },
        { FIF_PGM, grey, PNM_SAVE_ASCII, "PGM ascii" }, { FIF_PGMRAW, grey, PNM_SAVE_RAW, "PGM raw" },
        { FIF_PPM, rgb, PNM_SAVE_ASCII, "PPM ascii" }, { FIF_PPMRAW, rgb, PNM_SAVE_RAW, "PPM raw" },
        { FIF_PNG, rgb, 0, "PNG" }, { FIF_TARGA, rgb, 0, "TGA" }, { FIF_TIFF, rgb, 0, "TIFF" }, { FIF_TIFF, rgb, TIFF_NONE, "TIFF_NONE" },
        { FIF_WBMP, mono, 0, "WBMP" }, { FIF_PSD, rgb, 0, "PSD" }, { FIF_XPM, rgb, 0, "XPM" }, { FIF_GIF, grey, 0, "GIF" },
        { FIF_HDR, rgbf, 0, "HDR" }, { FIF_EXR, rgbf, 0, "EXR" }, { FIF_J2K, rgb, 0, "J2K" }, { FIF_JP2, rgb, 0, "JP2" },
        { FIF_PFM, flt, 0, "PFM" }, { FIF_WEBP, rgb, 0, "WebP" }, { FIF_JXR, rgb, 0, "JXR" }, { FIF_MNG, rgba, 0, "MNG" },
        { FIF_APNG, rgba, 0, "APNG" },
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        char what[128];
        BOOL ok;
        const INT64 full = save(t[i].fif, t[i].dib, t[i].flags, (INT64)1 << 40, &ok);
        snprintf(what, sizeof(what), "%s saves (%lld bytes)", t[i].name, (long long)full);
        report(what, ok && (full > 0));
        save(t[i].fif, t[i].dib, t[i].flags, 100, &ok);
        snprintf(what, sizeof(what), "  writes stop after 100 bytes -> FALSE");
        report(what, !ok);
        save(t[i].fif, t[i].dib, t[i].flags, full / 2, &ok);
        snprintf(what, sizeof(what), "  writes stop half way -> FALSE");
        report(what, !ok);
    }

    /* FreeImage_SaveMultiBitmapToHandle: two GIF pages */
    {
        char path[1024];
        const char *dir = getenv("IO_TEST_TMP");
        snprintf(path, sizeof(path), "%s/fi_io_writefail.tif", (dir && *dir) ? dir : ".");
        FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_TIFF, path, TRUE, FALSE, TRUE, 0);
        BOOL whole = FALSE, cut = TRUE;
        if (mb) {
            FreeImageIO io = { s_read, s_write, s_seek, s_tell };
            FreeImage_AppendPage(mb, grey);
            FreeImage_AppendPage(mb, grey);
            for (int pass = 0; pass < 2; pass++) {
                sink_t k;
                memset(&k, 0, sizeof(k));
                k.budget = pass ? 1000 : ((INT64)1 << 40);
                k.cap = 1 << 16;
                k.buf = (BYTE *)malloc((size_t)k.cap);
                const BOOL ok = FreeImage_SaveMultiBitmapToHandle(FIF_GIF, mb, &io, (fi_handle)&k, 0);
                if (pass) cut = ok; else whole = ok && (k.len > 1000);
                free(k.buf);
            }
            FreeImage_CloseMultiBitmap(mb, 0);
            remove(path);
        }
        report("two GIF pages through FreeImage_SaveMultiBitmapToHandle", whole);
        report("  writes stop after 1000 bytes -> FALSE", !cut);
    }

#if !defined(_WIN32) && defined(__linux__)
    /* a disk that fills while stdio flushes: FreeImage_Save must see fclose() fail (never as root: a failed save removes its file) */
    if (geteuid() != 0) {
        FIBITMAP *tiny = FreeImage_Allocate(16, 16, 24, 0, 0, 0);
        report("FreeImage_Save to /dev/full -> FALSE", !FreeImage_Save(FIF_BMP, tiny, "/dev/full", 0));
        FreeImage_Unload(tiny);
    }
#endif

    FreeImage_DeInitialise();
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
