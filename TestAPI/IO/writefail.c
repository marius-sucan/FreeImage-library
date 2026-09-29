/* FreeImage 3 - I/O test: a save whose writes fail, as on a full disk, returns FALSE, and returns */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

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

    struct { FREE_IMAGE_FORMAT fif; FIBITMAP *dib; int flags; const char *name; } t[] = {
        { FIF_J2K, rgb, 0, "J2K" }, { FIF_JP2, rgb, 0, "JP2" },
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

    FreeImage_DeInitialise();
    printf("--- %d failure(s) ---\n", failures);
    return failures ? 1 : 0;
}
