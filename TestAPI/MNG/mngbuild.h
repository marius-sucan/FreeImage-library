/* Builds MNG datastreams by hand for the MNG tests. */

#ifndef MNG_TEST_BUILD_H
#define MNG_TEST_BUILD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeImage.h"

/* ---- a growable byte buffer ---- */

typedef struct {
	BYTE *data;
	size_t size;
	size_t capacity;
} Buf;

static void buf_init(Buf *b) {
	b->data = NULL;
	b->size = 0;
	b->capacity = 0;
}

static void buf_free(Buf *b) {
	free(b->data);
	buf_init(b);
}

static void buf_add(Buf *b, const void *bytes, size_t n) {
	if (b->size + n > b->capacity) {
		size_t want = (b->capacity ? b->capacity * 2 : 256);
		while (want < b->size + n) {
			want *= 2;
		}
		b->data = (BYTE *)realloc(b->data, want);
		if (!b->data) {
			fprintf(stderr, "out of memory building a test file\n");
			exit(2);
		}
		b->capacity = want;
	}
	memcpy(b->data + b->size, bytes, n);
	b->size += n;
}

static void buf_byte(Buf *b, BYTE v) {
	buf_add(b, &v, 1);
}

static void buf_u32(Buf *b, DWORD v) {
	BYTE out[4];
	out[0] = (BYTE)(v >> 24); out[1] = (BYTE)(v >> 16);
	out[2] = (BYTE)(v >> 8);  out[3] = (BYTE)v;
	buf_add(b, out, 4);
}

static void buf_u16(Buf *b, WORD v) {
	BYTE out[2];
	out[0] = (BYTE)(v >> 8); out[1] = (BYTE)v;
	buf_add(b, out, 2);
}

/* ---- chunks ---- */

static const BYTE MNG_SIG[8] = { 138, 77, 78, 71, 13, 10, 26, 10 };

static void chunk(Buf *out, const char *type, const void *payload, DWORD length) {
	DWORD crc;

	buf_u32(out, length);
	buf_add(out, type, 4);
	if (length) {
		buf_add(out, payload, length);
	}
	crc = FreeImage_ZLibCRC32(0, (BYTE *)type, 4);
	if (length) {
		crc = FreeImage_ZLibCRC32(crc, (BYTE *)payload, length);
	}
	buf_u32(out, crc);
}

static void chunk_buf(Buf *out, const char *type, Buf *body) {
	chunk(out, type, body->data, (DWORD)body->size);
	buf_free(body);
}

static void mng_signature(Buf *out) {
	buf_add(out, MNG_SIG, 8);
}

static void mng_mhdr(Buf *out, DWORD width, DWORD height, DWORD ticks_per_second,
					 DWORD layers, DWORD frames, DWORD play_time, DWORD simplicity) {
	Buf body; buf_init(&body);
	buf_u32(&body, width);
	buf_u32(&body, height);
	buf_u32(&body, ticks_per_second);
	buf_u32(&body, layers);
	buf_u32(&body, frames);
	buf_u32(&body, play_time);
	buf_u32(&body, simplicity);
	chunk_buf(out, "MHDR", &body);
}

static void mng_mend(Buf *out) {
	chunk(out, "MEND", NULL, 0);
}

/* change_delay: 0 none, 1 next subframe, 2 also the default */
static void mng_fram(Buf *out, BYTE framing_mode, BYTE change_delay, DWORD delay) {
	Buf body; buf_init(&body);
	buf_byte(&body, framing_mode);
	if (change_delay) {
		buf_byte(&body, 0);              /* empty name's NUL */
		buf_byte(&body, change_delay);
		buf_byte(&body, 0);              /* change_timeout_and_termination */
		buf_byte(&body, 0);              /* change_layer_clipping_boundaries */
		buf_byte(&body, 0);              /* change_sync_id_list */
		buf_u32(&body, delay);
	}
	chunk_buf(out, "FRAM", &body);
}

static void mng_fram_empty(Buf *out) {
	chunk(out, "FRAM", NULL, 0);
}

static void mng_defi(Buf *out, WORD object_id, BYTE do_not_show, BYTE concrete,
					 LONG x, LONG y) {
	Buf body; buf_init(&body);
	buf_u16(&body, object_id);
	buf_byte(&body, do_not_show);
	buf_byte(&body, concrete);
	buf_u32(&body, (DWORD)x);
	buf_u32(&body, (DWORD)y);
	chunk_buf(out, "DEFI", &body);
}

static void mng_defi_short(Buf *out, WORD object_id) {
	Buf body; buf_init(&body);
	buf_u16(&body, object_id);
	chunk_buf(out, "DEFI", &body);
}

static void mng_back(Buf *out, WORD red, WORD green, WORD blue, BYTE mandatory) {
	Buf body; buf_init(&body);
	buf_u16(&body, red);
	buf_u16(&body, green);
	buf_u16(&body, blue);
	buf_byte(&body, mandatory);
	chunk_buf(out, "BACK", &body);
}

static void mng_loop(Buf *out, BYTE nest_level, DWORD iterations) {
	Buf body; buf_init(&body);
	buf_byte(&body, nest_level);
	buf_u32(&body, iterations);
	chunk_buf(out, "LOOP", &body);
}

static void mng_endl(Buf *out, BYTE nest_level) {
	Buf body; buf_init(&body);
	buf_byte(&body, nest_level);
	chunk_buf(out, "ENDL", &body);
}

/* TERM 3 repeats; 0x7fffffff means forever */
static void mng_term(Buf *out, BYTE action, DWORD iteration_max) {
	Buf body; buf_init(&body);
	buf_byte(&body, action);
	if (action == 3) {
		buf_byte(&body, 0);          /* action after iterations */
		buf_u32(&body, 0);           /* delay before repeating */
		buf_u32(&body, iteration_max);
	}
	chunk_buf(out, "TERM", &body);
}

static void mng_show(Buf *out, WORD first, WORD last, BYTE mode) {
	Buf body; buf_init(&body);
	buf_u16(&body, first);
	buf_u16(&body, last);
	buf_byte(&body, mode);
	chunk_buf(out, "SHOW", &body);
}

/* ---- embedded images ---- */

/* a solid PNG minus its signature */
static int mng_image(Buf *out, int width, int height,
					 BYTE red, BYTE green, BYTE blue, int bpp) {
	FIBITMAP *dib;
	FIMEMORY *mem;
	BYTE *png = NULL;
	DWORD size = 0;
	int y, x, ok;

	dib = FreeImage_Allocate(width, height, bpp,
							 FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	if (!dib) {
		return 0;
	}
	for (y = 0; y < height; y++) {
		BYTE *line = FreeImage_GetScanLine(dib, (unsigned)y);
		for (x = 0; x < width; x++) {
			line[FI_RGBA_RED] = red;
			line[FI_RGBA_GREEN] = green;
			line[FI_RGBA_BLUE] = blue;
			if (bpp == 32) {
				line[FI_RGBA_ALPHA] = 255;
			}
			line += bpp / 8;
		}
	}

	mem = FreeImage_OpenMemory(NULL, 0);
	if (!mem) {
		FreeImage_Unload(dib);
		return 0;
	}
	ok = FreeImage_SaveToMemory(FIF_PNG, dib, mem, 0);
	if (ok && FreeImage_AcquireMemory(mem, &png, &size) && size > 8) {
		buf_add(out, png + 8, size - 8);   /* everything but the signature */
	} else {
		ok = 0;
	}
	FreeImage_CloseMemory(mem);
	FreeImage_Unload(dib);
	return ok;
}

static int mng_image_alpha(Buf *out, int width, int height,
						   BYTE red, BYTE green, BYTE blue, BYTE alpha) {
	FIBITMAP *dib;
	FIMEMORY *mem;
	BYTE *png = NULL;
	DWORD size = 0;
	int y, x, ok;

	dib = FreeImage_Allocate(width, height, 32,
							 FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	if (!dib) {
		return 0;
	}
	for (y = 0; y < height; y++) {
		BYTE *line = FreeImage_GetScanLine(dib, (unsigned)y);
		for (x = 0; x < width; x++) {
			line[FI_RGBA_RED] = red;
			line[FI_RGBA_GREEN] = green;
			line[FI_RGBA_BLUE] = blue;
			line[FI_RGBA_ALPHA] = alpha;
			line += 4;
		}
	}

	mem = FreeImage_OpenMemory(NULL, 0);
	if (!mem) {
		FreeImage_Unload(dib);
		return 0;
	}
	ok = FreeImage_SaveToMemory(FIF_PNG, dib, mem, 0);
	if (ok && FreeImage_AcquireMemory(mem, &png, &size) && size > 8) {
		buf_add(out, png + 8, size - 8);
	} else {
		ok = 0;
	}
	FreeImage_CloseMemory(mem);
	FreeImage_Unload(dib);
	return ok;
}

/* ---- scratch files ---- */

static char mng_tmpbuf[4][4096];
static int mng_tmpnext = 0;

/* in $MNG_TEST_TMP, else the current directory */
static const char *scratch(const char *name) {
	const char *dir = getenv("MNG_TEST_TMP");
	char *out = mng_tmpbuf[mng_tmpnext++ & 3];

	if (dir && *dir) {
		snprintf(out, sizeof(mng_tmpbuf[0]), "%s/%s", dir, name);
	} else {
		snprintf(out, sizeof(mng_tmpbuf[0]), "%s", name);
	}
	return out;
}

static const char *write_file(const char *name, Buf *b) {
	const char *path = scratch(name);
	FILE *f = fopen(path, "wb");

	if (!f) {
		fprintf(stderr, "cannot write %s\n", path);
		exit(2);
	}
	if (b->size) {
		fwrite(b->data, 1, b->size, f);
	}
	fclose(f);
	return path;
}

#endif /* MNG_TEST_BUILD_H */
