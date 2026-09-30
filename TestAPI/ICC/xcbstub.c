/* a stand-in libxcb.so.1 for display -x11: the _ICC_PROFILE of two screens, from the environment
   XCB_STUB_FILE / XCB_STUB_FILE1: the profile of screen 0 / 1 (none when unset)
   XCB_STUB_MODE: ok, error (no connection), noatom, format32, partial (bytes after the reply)
   XCB_STUB_SCREEN: the screen xcb_connect reports */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { int error; } xcb_connection_t;
typedef struct { uint32_t root; uint32_t rest[9]; } xcb_screen_t;
typedef struct { xcb_screen_t *data; int rem; int index; } xcb_screen_iterator_t;
typedef struct { unsigned int sequence; } xcb_cookie_t;
typedef struct { uint8_t response_type; uint8_t pad0; uint16_t sequence; uint32_t length; uint32_t atom; } xcb_intern_atom_reply_t;
typedef struct { uint8_t response_type; uint8_t format; uint16_t sequence; uint32_t length; uint32_t type; uint32_t bytes_after; uint32_t value_len; uint8_t pad0[12]; } xcb_get_property_reply_t;

#define ATOM 77
#define ROOT0 0x100
#define ROOT1 0x200

int xcb_stub_connects = 0;
static xcb_screen_t screens[2] = { { ROOT0, { 0 } }, { ROOT1, { 0 } } };
static uint32_t last_window = 0;

static const char *mode(void) {
    const char *m = getenv("XCB_STUB_MODE");
    return m ? m : "ok";
}

xcb_connection_t *xcb_connect(const char *name, int *screen) {
    xcb_connection_t *c = (xcb_connection_t *)calloc(1, sizeof(xcb_connection_t));
    const char *s = getenv("XCB_STUB_SCREEN");
    (void)name;
    xcb_stub_connects++;
    c->error = !strcmp(mode(), "error");
    if (screen) *screen = s ? atoi(s) : 0;
    return c;
}

int xcb_connection_has_error(xcb_connection_t *c) { return c->error; }
void xcb_disconnect(xcb_connection_t *c) { free(c); }
const void *xcb_get_setup(xcb_connection_t *c) { (void)c; return screens; }

xcb_screen_iterator_t xcb_setup_roots_iterator(const void *setup) {
    xcb_screen_iterator_t i;
    (void)setup;
    i.data = screens; i.rem = 2; i.index = 0;
    return i;
}

void xcb_screen_next(xcb_screen_iterator_t *i) { i->data++; i->rem--; i->index++; }

xcb_cookie_t xcb_intern_atom(xcb_connection_t *c, uint8_t only_if_exists, uint16_t length, const char *name) {
    xcb_cookie_t k;
    (void)c;
    k.sequence = (only_if_exists && length == 12 && !memcmp(name, "_ICC_PROFILE", 12)) ? 1 : 0;
    return k;
}

xcb_intern_atom_reply_t *xcb_intern_atom_reply(xcb_connection_t *c, xcb_cookie_t k, void **e) {
    xcb_intern_atom_reply_t *r = (xcb_intern_atom_reply_t *)calloc(1, sizeof(*r));
    (void)c;
    if (e) *e = NULL;
    r->atom = (k.sequence == 1 && strcmp(mode(), "noatom")) ? ATOM : 0;
    return r;
}

xcb_cookie_t xcb_get_property(xcb_connection_t *c, uint8_t del, uint32_t window, uint32_t property, uint32_t type, uint32_t offset, uint32_t length) {
    xcb_cookie_t k;
    (void)c; (void)del; (void)type; (void)offset; (void)length;
    last_window = window;
    k.sequence = (property == ATOM) ? 2 : 0;
    return k;
}

/* the reply is followed by its value, as libxcb's are */
xcb_get_property_reply_t *xcb_get_property_reply(xcb_connection_t *c, xcb_cookie_t k, void **e) {
    const char *path = getenv(last_window == ROOT1 ? "XCB_STUB_FILE1" : "XCB_STUB_FILE");
    xcb_get_property_reply_t *r;
    long n = 0;
    FILE *f = (k.sequence == 2 && path) ? fopen(path, "rb") : NULL;
    (void)c;
    if (e) *e = NULL;
    if (f) { fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET); }
    r = (xcb_get_property_reply_t *)calloc(1, sizeof(*r) + (size_t)n + 1);
    if (f) {
        if (fread(r + 1, 1, (size_t)n, f) != (size_t)n) n = 0;
        fclose(f);
        r->format = !strcmp(mode(), "format32") ? 32 : 8;
        r->type = 19;
        r->value_len = (uint32_t)(r->format == 32 ? n / 4 : n);
        r->bytes_after = !strcmp(mode(), "partial") ? 100 : 0;
    }
    return r;
}

void *xcb_get_property_value(const xcb_get_property_reply_t *r) { return (void *)(r + 1); }

int xcb_get_property_value_length(const xcb_get_property_reply_t *r) { return (int)(r->value_len * (r->format / 8)); }
