/* FreeImage 3 - APNG robustness test; best run as "make asan-run" */
/* scratch files: $APNG_TEST_TMP or the current directory */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "FreeImage.h"

static int failures = 0;
static int checks = 0;

static void fail(const char *fmt, ...) {
	va_list ap;
	printf("  FAIL ");
	va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
	printf("\n");
	failures++;
}

static void ok(const char *fmt, ...) {
	va_list ap;
	printf("  ok   ");
	va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
	printf("\n");
	checks++;
}

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static char tmpbuf[2][4096];
static int tmpnext = 0;

static const char *scratch(const char *name) {
	const char *dir = getenv("APNG_TEST_TMP");
	char *buf = tmpbuf[tmpnext++ & 1];
	snprintf(buf, sizeof(tmpbuf[0]), "%s/%s", dir && *dir ? dir : ".", name);
	return buf;
}

/* ------------------------------------------------------------------ */
/*  Building PNG chunks by hand                                       */
/* ------------------------------------------------------------------ */

static const BYTE SIGNATURE[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

/* PNG CRC-32 */
static unsigned long crc_table[256];
static int crc_ready = 0;

static unsigned long crc(const BYTE *buf, size_t len) {
	unsigned long c = 0xFFFFFFFFUL;
	size_t n;
	int k;
	if (!crc_ready) {
		for (n = 0; n < 256; n++) {
			c = (unsigned long)n;
			for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1;
			crc_table[n] = c;
		}
		crc_ready = 1;
		c = 0xFFFFFFFFUL;
	}
	for (n = 0; n < len; n++) c = crc_table[(c ^ buf[n]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFUL;
}

typedef struct { BYTE *data; size_t size, cap; } buf_t;

static void put(buf_t *b, const void *p, size_t n) {
	if (b->size + n > b->cap) {
		b->cap = (b->size + n) * 2 + 256;
		b->data = (BYTE *)realloc(b->data, b->cap);
	}
	memcpy(b->data + b->size, p, n);
	b->size += n;
}

static void put32(buf_t *b, unsigned long v) {
	BYTE t[4];
	t[0] = (BYTE)(v >> 24); t[1] = (BYTE)(v >> 16); t[2] = (BYTE)(v >> 8); t[3] = (BYTE)v;
	put(b, t, 4);
}

/* length != (size_t)-1 overrides the real length */
static void chunk(buf_t *b, const char *type, const BYTE *data, size_t n, size_t length) {
	BYTE *tmp = (BYTE *)malloc(n + 4);
	put32(b, (unsigned long)(length == (size_t)-1 ? n : length));
	memcpy(tmp, type, 4);
	if (n) memcpy(tmp + 4, data, n);
	put(b, tmp, n + 4);
	put32(b, crc(tmp, n + 4));
	free(tmp);
}

/* zlib stream of a solid w x h RGBA image, from the PNG writer */
static BYTE *image_data(unsigned w, unsigned h, size_t *out_size, BYTE **out_ihdr) {
	FIBITMAP *dib = FreeImage_Allocate(w, h, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	FIMEMORY *hmem;
	BYTE *raw = NULL, *png = NULL, *idat = NULL;
	DWORD png_size = 0;
	size_t pos = 8, total = 0;
	unsigned x, y;

	if (!dib) return NULL;
	for (y = 0; y < h; y++) {
		BYTE *l = FreeImage_GetScanLine(dib, y);
		for (x = 0; x < w; x++) {
			l[FI_RGBA_RED] = (BYTE)(x * 3); l[FI_RGBA_GREEN] = (BYTE)(y * 5);
			l[FI_RGBA_BLUE] = (BYTE)(x + y); l[FI_RGBA_ALPHA] = 255;
			l += 4;
		}
	}
	hmem = FreeImage_OpenMemory(NULL, 0);
	if (!hmem || !FreeImage_SaveToMemory(FIF_PNG, dib, hmem, 0)) {
		FreeImage_Unload(dib);
		if (hmem) FreeImage_CloseMemory(hmem);
		return NULL;
	}
	FreeImage_AcquireMemory(hmem, &png, &png_size);

	/* pull out the IHDR payload and every IDAT payload */
	while (pos + 8 <= png_size) {
		unsigned long len = ((unsigned long)png[pos] << 24) | ((unsigned long)png[pos + 1] << 16)
			| ((unsigned long)png[pos + 2] << 8) | png[pos + 3];
		const BYTE *type = png + pos + 4;
		if (memcmp(type, "IHDR", 4) == 0 && out_ihdr) {
			*out_ihdr = (BYTE *)malloc(13);
			memcpy(*out_ihdr, png + pos + 8, 13);
		} else if (memcmp(type, "IDAT", 4) == 0) {
			idat = (BYTE *)realloc(idat, total + len);
			memcpy(idat + total, png + pos + 8, len);
			total += len;
		} else if (memcmp(type, "IEND", 4) == 0) {
			break;
		}
		pos += 12 + len;
	}
	raw = idat;
	*out_size = total;
	FreeImage_CloseMemory(hmem);
	FreeImage_Unload(dib);
	return raw;
}

static void fctl(BYTE *p, unsigned long seq, unsigned long w, unsigned long h,
	unsigned long x, unsigned long y, unsigned dn, unsigned dd, BYTE dispose, BYTE blend) {
	p[0] = (BYTE)(seq >> 24); p[1] = (BYTE)(seq >> 16); p[2] = (BYTE)(seq >> 8); p[3] = (BYTE)seq;
	p[4] = (BYTE)(w >> 24); p[5] = (BYTE)(w >> 16); p[6] = (BYTE)(w >> 8); p[7] = (BYTE)w;
	p[8] = (BYTE)(h >> 24); p[9] = (BYTE)(h >> 16); p[10] = (BYTE)(h >> 8); p[11] = (BYTE)h;
	p[12] = (BYTE)(x >> 24); p[13] = (BYTE)(x >> 16); p[14] = (BYTE)(x >> 8); p[15] = (BYTE)x;
	p[16] = (BYTE)(y >> 24); p[17] = (BYTE)(y >> 16); p[18] = (BYTE)(y >> 8); p[19] = (BYTE)y;
	p[20] = (BYTE)(dn >> 8); p[21] = (BYTE)dn;
	p[22] = (BYTE)(dd >> 8); p[23] = (BYTE)dd;
	p[24] = dispose; p[25] = blend;
}

static void actl(BYTE *p, unsigned long frames, unsigned long plays) {
	p[0] = (BYTE)(frames >> 24); p[1] = (BYTE)(frames >> 16); p[2] = (BYTE)(frames >> 8); p[3] = (BYTE)frames;
	p[4] = (BYTE)(plays >> 24); p[5] = (BYTE)(plays >> 16); p[6] = (BYTE)(plays >> 8); p[7] = (BYTE)plays;
}

/* ------------------------------------------------------------------ */
/*  What a file is expected to be worth                               */
/* ------------------------------------------------------------------ */

#define WANT_REFUSED   0	/* the plugin may not open it at all */
#define WANT_STATIC    1	/* one page: the default image, animation abandoned */
#define WANT_ANIMATED  2	/* an animation of exactly `pages` frames */
#define WANT_ANY       3	/* anything at all, as long as it does not crash */

static void expect(const char *what, const buf_t *file, int want, int pages) {
	FIMEMORY *hmem = FreeImage_OpenMemory(file->data, (DWORD)file->size);
	FIMULTIBITMAP *mb;
	int got = -1;

	if (!hmem) { fail("%s: out of memory", what); return; }
	mb = FreeImage_LoadMultiBitmapFromMemory(FIF_APNG, hmem, 0);
	if (mb) {
		int i;
		got = FreeImage_GetPageCount(mb);
		for (i = 0; i < got; i++) {
			FIBITMAP *page = FreeImage_LockPage(mb, i);
			if (page) FreeImage_UnlockPage(mb, page, FALSE);
		}
		FreeImage_CloseMultiBitmap(mb, 0);
	}
	FreeImage_CloseMemory(hmem);

	switch (want) {
		case WANT_REFUSED:
			if (mb && got > 0) fail("%s: opened with %d page(s), expected to be refused", what, got);
			else ok("%s: refused", what);
			break;
		case WANT_STATIC:
			if (got != 1) fail("%s: %d page(s), expected the default image alone", what, got);
			else ok("%s: the default image is still there", what);
			break;
		case WANT_ANIMATED:
			if (got != pages) fail("%s: %d page(s), expected %d", what, got, pages);
			else ok("%s: %d frames", what, pages);
			break;
		default:
			ok("%s: survived (%d page(s))", what, got);
			break;
	}
}

/* ------------------------------------------------------------------ */

/* valid 3-frame animation; each variant breaks one rule */
static void build(buf_t *b, const BYTE *ihdr, const BYTE *idat, size_t idat_size,
	const BYTE *small, size_t small_size, int variant) {
	BYTE f[26], a[8];

	memset(b, 0, sizeof(*b));
	put(b, SIGNATURE, 8);
	chunk(b, "IHDR", ihdr, 13, (size_t)-1);

	if (variant == 8) {			/* acTL after IDAT */
		chunk(b, "IDAT", idat, idat_size, (size_t)-1);
		actl(a, 2, 0);
		chunk(b, "acTL", a, 8, (size_t)-1);
		fctl(f, 0, 32, 24, 0, 0, 1, 10, 0, 0);
		chunk(b, "fcTL", f, 26, (size_t)-1);
		chunk(b, "fdAT", small, small_size, (size_t)-1);
		chunk(b, "IEND", NULL, 0, (size_t)-1);
		return;
	}

	actl(a, (variant == 15) ? 0 : 3, 0);	/* 15 = an acTL counting no frames */
	chunk(b, "acTL", a, 8, (size_t)-1);
	if (variant == 9) chunk(b, "acTL", a, 8, (size_t)-1);		/* repeated acTL */

	if (variant == 1) {			/* fcTL before IDAT, not covering the canvas */
		fctl(f, 0, 16, 12, 0, 0, 1, 10, 0, 0);
		chunk(b, "fcTL", f, 26, (size_t)-1);
	} else if (variant == 10) {	/* two fcTL before IDAT */
		fctl(f, 0, 32, 24, 0, 0, 1, 10, 0, 0);
		chunk(b, "fcTL", f, 26, (size_t)-1);
		fctl(f, 1, 32, 24, 0, 0, 1, 10, 0, 0);
		chunk(b, "fcTL", f, 26, (size_t)-1);
	} else if (variant != 7) {	/* 7 = no fcTL at all before IDAT */
		fctl(f, 0, 32, 24, 0, 0, 1, 10, 0, 0);
		chunk(b, "fcTL", f, 26, (size_t)-1);
	}

	chunk(b, "IDAT", idat, idat_size, (size_t)-1);

	/* the two frames after the default image */
	{
		unsigned long seq = (variant == 7) ? 0 : 1;
		BYTE fd[4096];
		size_t n;

		if (variant == 2) seq += 1;			/* gap in the sequence */
		if (variant == 3) seq = 0;			/* duplicate */
		fctl(f, seq, 12, 10, 4, 4, 1, 10, 0, 0);
		if (variant == 4) fctl(f, seq, 12, 10, 40, 40, 1, 10, 0, 0);	/* outside the canvas */
		if (variant == 5) fctl(f, seq, 0, 10, 0, 0, 1, 10, 0, 0);		/* zero width */
		if (variant == 6) fctl(f, seq, 12, 10, 4, 4, 1, 10, 99, 99);	/* nonsense ops */
		chunk(b, "fcTL", f, 26, (size_t)-1);
		seq++;

		n = small_size < sizeof(fd) - 4 ? small_size : sizeof(fd) - 4;
		fd[0] = (BYTE)(seq >> 24); fd[1] = (BYTE)(seq >> 16);
		fd[2] = (BYTE)(seq >> 8); fd[3] = (BYTE)seq;
		memcpy(fd + 4, small, n);
		if (variant == 11) {				/* fdAT claiming more than it holds */
			chunk(b, "fdAT", fd, n + 4, n + 4 + 10000);
		} else if (variant == 12) {			/* fdAT with no payload at all */
			chunk(b, "fdAT", fd, 2, (size_t)-1);
		} else {
			chunk(b, "fdAT", fd, n + 4, (size_t)-1);
		}
		seq++;

		if (variant != 13) {				/* 13 = an fcTL whose fdAT never comes */
			fctl(f, seq, 12, 10, 4, 4, 1, 10, 0, 0);
			chunk(b, "fcTL", f, 26, (size_t)-1);
			seq++;
			fd[0] = (BYTE)(seq >> 24); fd[1] = (BYTE)(seq >> 16);
			fd[2] = (BYTE)(seq >> 8); fd[3] = (BYTE)seq;
			chunk(b, "fdAT", fd, n + 4, (size_t)-1);
		} else {
			fctl(f, seq, 12, 10, 4, 4, 1, 10, 0, 0);
			chunk(b, "fcTL", f, 26, (size_t)-1);
		}
	}

	if (variant == 14) {		/* a critical chunk nobody knows */
		chunk(b, "ZzZz", (const BYTE *)"junk", 4, (size_t)-1);
	}
	chunk(b, "IEND", NULL, 0, (size_t)-1);
}

static void test_invalid(void) {
	BYTE *ihdr = NULL, *ihdr_small = NULL;
	size_t idat_size = 0, small_size = 0;
	BYTE *idat = image_data(32, 24, &idat_size, &ihdr);
	BYTE *small = image_data(12, 10, &small_size, &ihdr_small);
	buf_t b;

	printf("\n=== files built wrong on purpose\n");
	if (!idat || !small || !ihdr) { fail("could not build the image data"); return; }

	build(&b, ihdr, idat, idat_size, small, small_size, 0);
	expect("a correct three-frame file", &b, WANT_ANIMATED, 3); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 1);
	expect("the default image's fcTL does not cover the canvas", &b, WANT_STATIC, 0); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 2);
	expect("a gap in the sequence numbers", &b, WANT_STATIC, 0); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 3);
	expect("a duplicated sequence number", &b, WANT_STATIC, 0); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 4);
	expect("a frame that falls outside the canvas", &b, WANT_STATIC, 0); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 5);
	expect("a frame of zero width", &b, WANT_STATIC, 0); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 6);
	expect("dispose_op and blend_op out of range", &b, WANT_STATIC, 0); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 7);
	expect("no fcTL for the default image", &b, WANT_ANIMATED, 2); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 8);
	expect("acTL after IDAT", &b, WANT_STATIC, 0); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 9);
	expect("a repeated acTL", &b, WANT_STATIC, 0); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 10);
	expect("two fcTL before IDAT", &b, WANT_STATIC, 0); free(b.data);

	/* a cut file keeps the frames it holds: the fdAT takes the rest of the file as its data */
	build(&b, ihdr, idat, idat_size, small, small_size, 11);
	expect("an fdAT claiming more than the file holds", &b, WANT_ANIMATED, 2); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 12);
	expect("an fdAT shorter than its sequence number", &b, WANT_STATIC, 0); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 13);
	expect("an fcTL whose fdAT never arrives", &b, WANT_ANIMATED, 2); free(b.data);

	build(&b, ihdr, idat, idat_size, small, small_size, 14);
	expect("a critical chunk nobody knows", &b, WANT_REFUSED, 0); free(b.data);

	/* 0 is not a valid frame count: no animation, as browsers read it */
	build(&b, ihdr, idat, idat_size, small, small_size, 15);
	expect("an acTL counting no frames", &b, WANT_STATIC, 0); free(b.data);

	/* the file ends inside the last frame's fcTL, or its fdAT's sequence number: the frames before it stay */
	{
		static const char *types[2] = { "fcTL", "fdAT" };
		static const size_t into[2] = { 8 + 13, 8 + 2 };
		int t;
		for (t = 0; t < 2; t++) {
			size_t pos = 8, last = 0;
			build(&b, ihdr, idat, idat_size, small, small_size, 0);
			while (pos + 8 <= b.size) {
				const size_t len = ((size_t)b.data[pos] << 24) | ((size_t)b.data[pos + 1] << 16) | ((size_t)b.data[pos + 2] << 8) | b.data[pos + 3];
				if (!memcmp(b.data + pos + 4, types[t], 4)) last = pos;
				pos += 12 + len;
			}
			b.size = last + into[t];
			expect(t ? "a file ending inside an fdAT's sequence number" : "a file ending inside an fcTL", &b, WANT_ANIMATED, 2);
			free(b.data);
		}
	}

	memset(&b, 0, sizeof(b));
	put(&b, SIGNATURE, 8);
	chunk(&b, "IHDR", ihdr, 13, (size_t)-1);
	chunk(&b, "IEND", NULL, 0, (size_t)-1);
	expect("a PNG with no IDAT", &b, WANT_REFUSED, 0); free(b.data);

	{
		BYTE zero[13];
		memcpy(zero, ihdr, 13);
		memset(zero, 0, 8);
		memset(&b, 0, sizeof(b));
		put(&b, SIGNATURE, 8);
		chunk(&b, "IHDR", zero, 13, (size_t)-1);
		chunk(&b, "IDAT", idat, idat_size, (size_t)-1);
		chunk(&b, "IEND", NULL, 0, (size_t)-1);
		expect("a canvas of 0x0", &b, WANT_REFUSED, 0); free(b.data);
	}

	{
		BYTE a[8];
		actl(a, 2, 0);
		memset(&b, 0, sizeof(b));
		put(&b, SIGNATURE, 8);
		chunk(&b, "IHDR", ihdr, 13, (size_t)-1);
		chunk(&b, "acTL", a, 8, (size_t)-1);
		chunk(&b, "IDAT", idat, idat_size, 0x7FFFFFFF);
		chunk(&b, "IEND", NULL, 0, (size_t)-1);
		/* the IDAT takes the rest of the file, read in steps, and the default image loads */
		expect("a chunk claiming 2GB it does not hold", &b, WANT_STATIC, 0); free(b.data);
	}

	free(idat); free(small); free(ihdr); free(ihdr_small);
}

/* ------------------------------------------------------------------ */

/* load and touch every page; not crashing is the test */
static void survive(const BYTE *data, size_t size) {
	FIMEMORY *hmem = FreeImage_OpenMemory((BYTE *)data, (DWORD)size);
	FIMULTIBITMAP *mb;
	if (!hmem) return;
	mb = FreeImage_LoadMultiBitmapFromMemory(FIF_APNG, hmem, APNG_PLAYBACK);
	if (mb) {
		int i, n = FreeImage_GetPageCount(mb);
		for (i = 0; i < n && i < 64; i++) {
			FIBITMAP *page = FreeImage_LockPage(mb, i);
			if (page) FreeImage_UnlockPage(mb, page, FALSE);
		}
		FreeImage_CloseMultiBitmap(mb, 0);
	}
	FreeImage_CloseMemory(hmem);
	/* and as a single image (a different code path) */
	hmem = FreeImage_OpenMemory((BYTE *)data, (DWORD)size);
	if (hmem) {
		FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_APNG, hmem, 0);
		if (dib) FreeImage_Unload(dib);
		FreeImage_CloseMemory(hmem);
	}
}

/* a valid 4-frame 48x36 animation, as the writer stores it; NULL on failure */
static BYTE *sample_animation(long *size) {
	const char *path = scratch("apng_robust.png");
	FIBITMAP *frames[4];
	BYTE *good = NULL;
	FILE *f;
	int i;

	for (i = 0; i < 4; i++) {
		unsigned x, y;
		frames[i] = FreeImage_Allocate(48, 36, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
		for (y = 0; y < 36; y++) {
			BYTE *l = FreeImage_GetScanLine(frames[i], y);
			for (x = 0; x < 48; x++) {
				l[FI_RGBA_RED] = (BYTE)(x * 5 + i * 20); l[FI_RGBA_GREEN] = (BYTE)(y * 7);
				l[FI_RGBA_BLUE] = (BYTE)(x + y + i); l[FI_RGBA_ALPHA] = (BYTE)(128 + i * 30);
				l += 4;
			}
		}
	}
	{
		FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, TRUE, FALSE, FALSE, 0);
		if (mb) {
			for (i = 0; i < 4; i++) FreeImage_AppendPage(mb, frames[i]);
			FreeImage_CloseMultiBitmap(mb, 0);
		}
	}
	for (i = 0; i < 4; i++) FreeImage_Unload(frames[i]);

	if ((f = fopen(path, "rb")) == NULL) return NULL;
	fseek(f, 0, SEEK_END); *size = ftell(f); fseek(f, 0, SEEK_SET);
	good = (BYTE *)malloc(*size);
	if (fread(good, 1, *size, f) != (size_t)*size) { free(good); good = NULL; }
	fclose(f);
	remove(path);
	return good;
}

/* the pages of a file and how many of them load; refused counts no page */
static int pages_of(const BYTE *data, size_t size, int flags, int *loaded) {
	FIMEMORY *hmem = FreeImage_OpenMemory((BYTE *)data, (DWORD)size);
	FIMULTIBITMAP *mb;
	int i, n = 0;
	*loaded = 0;
	if (!hmem) return -1;
	mb = FreeImage_LoadMultiBitmapFromMemory(FIF_APNG, hmem, flags);
	if (mb) {
		n = FreeImage_GetPageCount(mb);
		for (i = 0; i < n; i++) {
			FIBITMAP *page = FreeImage_LockPage(mb, i);
			if (page) { (*loaded)++; FreeImage_UnlockPage(mb, page, FALSE); }
		}
		FreeImage_CloseMultiBitmap(mb, 0);
	}
	FreeImage_CloseMemory(hmem);
	return n;
}

/* a file cut anywhere: full, header-only and played loads count the same pages, all of which load */
static void test_cut_pages(void) {
	long size = 0, n;
	BYTE *good = sample_animation(&size);
	int before = 0, bad = 0;

	printf("\n=== a cut file counts the frames it can show\n");
	if (!good) { fail("could not write the sample"); return; }

	for (n = 0; n <= size; n++) {
		int full_loaded, header_loaded, played_loaded;
		const int full = pages_of(good, n, 0, &full_loaded);
		const int header = pages_of(good, n, FIF_LOAD_NOPIXELS, &header_loaded);
		const int played = pages_of(good, n, APNG_PLAYBACK, &played_loaded);
		if (full != header || full != played || full_loaded != full || header_loaded != header || played_loaded != played || full < before) {
			if (bad++ < 5) {
				fail("cut at %ld of %ld bytes: %d page(s), %d loaded; header-only %d, %d; played %d, %d; %d before",
					n, size, full, full_loaded, header, header_loaded, played, played_loaded, before);
			}
		}
		before = full;
	}
	if (before != 4) { fail("the whole file has %d page(s), expected 4", before); bad++; }
	if (!bad) ok("cut at each of %ld lengths, every page counted loads, header-only and played alike", size + 1);
	free(good);
}

/* the last frame, one row that changed, is whole but for its zlib trailer: it is a page that loads */
static void test_cut_last_row(void) {
	const char *path = scratch("apng_cut_row.png");
	FIBITMAP *frames[2];
	BYTE *data = NULL;
	long size = 0;
	size_t pos = 8, last = 0, last_len = 0, last_fctl = 0;
	FILE *f;
	int i, k, bad = 0;

	printf("\n=== a cut file whose last frame is one row\n");

	frames[0] = FreeImage_Allocate(48, 36, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	for (i = 0; i < 36; i++) {
		BYTE *l = FreeImage_GetScanLine(frames[0], i);
		unsigned x;
		for (x = 0; x < 48; x++, l += 4) {
			l[FI_RGBA_RED] = (BYTE)(x * 5); l[FI_RGBA_GREEN] = (BYTE)(i * 7); l[FI_RGBA_BLUE] = (BYTE)(x + i); l[FI_RGBA_ALPHA] = 255;
		}
	}
	/* the writer stores the second frame as the 48x1 rectangle that changed */
	frames[1] = FreeImage_Clone(frames[0]);
	memset(FreeImage_GetScanLine(frames[1], 10), 0x80, 48 * 4);
	{
		FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, TRUE, FALSE, FALSE, 0);
		if (mb) {
			for (i = 0; i < 2; i++) FreeImage_AppendPage(mb, frames[i]);
			FreeImage_CloseMultiBitmap(mb, 0);
		}
	}
	for (i = 0; i < 2; i++) FreeImage_Unload(frames[i]);
	if ((f = fopen(path, "rb")) != NULL) {
		fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
		data = (BYTE *)malloc(size);
		if (fread(data, 1, size, f) != (size_t)size) { free(data); data = NULL; }
		fclose(f);
	}
	remove(path);
	if (!data) { fail("could not write the sample"); return; }

	while (pos + 8 <= (size_t)size) {
		const size_t len = ((size_t)data[pos] << 24) | ((size_t)data[pos + 1] << 16) | ((size_t)data[pos + 2] << 8) | data[pos + 3];
		if (!memcmp(data + pos + 4, "fcTL", 4)) last_fctl = pos;
		if (!memcmp(data + pos + 4, "fdAT", 4)) { last = pos; last_len = len; }
		pos += 12 + len;
	}
	/* the fcTL's height */
	if (!last || !last_fctl || ((data[last_fctl + 16] << 24 | data[last_fctl + 17] << 16 | data[last_fctl + 18] << 8 | data[last_fctl + 19]) != 1)) {
		fail("the second frame is not stored as one row");
		bad++;
	}
	/* cut 1 to 4 bytes into its Adler-32 */
	for (k = 1; k <= 4 && !bad; k++) {
		const size_t n = last + 8 + last_len - k;
		int full_loaded, header_loaded, played_loaded;
		const int full = pages_of(data, n, 0, &full_loaded);
		const int header = pages_of(data, n, FIF_LOAD_NOPIXELS, &header_loaded);
		const int played = pages_of(data, n, APNG_PLAYBACK, &played_loaded);
		if (full != 2 || header != 2 || played != 2 || full_loaded != 2 || header_loaded != 2 || played_loaded != 2) {
			fail("cut %d bytes short: %d page(s), %d loaded; header-only %d; played %d, %d loaded", k, full, full_loaded, header, played, played_loaded);
			bad++;
		}
	}
	if (!bad) ok("cut 1 to 4 bytes into the last frame's zlib trailer, both frames are pages that load");
	free(data);
}

static void test_damage(void) {
	BYTE *good = NULL;
	long size = 0;
	int cases = 0;

	printf("\n=== a valid animation, damaged every way a file gets damaged\n");

	good = sample_animation(&size);
	if (!good) { fail("could not write the sample"); return; }

	printf("       the sample is %ld bytes\n", size);

	/* every truncation */
	{
		long n;
		for (n = 0; n <= size; n++) { survive(good, n); cases++; }
	}

	/* one byte changed, everywhere, to four different values */
	{
		long n;
		BYTE *copy = (BYTE *)malloc(size);
		static const BYTE values[4] = { 0x00, 0x01, 0x7F, 0xFF };
		int v;
		for (n = 0; n < size; n++) {
			for (v = 0; v < 4; v++) {
				memcpy(copy, good, size);
				copy[n] = values[v];
				survive(copy, size);
				cases++;
			}
		}
		free(copy);
	}

	/* 64-byte regions wiped */
	{
		long n;
		BYTE *copy = (BYTE *)malloc(size);
		for (n = 0; n < size; n += 16) {
			long len = (n + 64 <= size) ? 64 : size - n;
			memcpy(copy, good, size);
			memset(copy + n, 0, len);
			survive(copy, size);
			cases++;
		}
		free(copy);
	}

	/* every chunk length field rewritten to nonsense */
	{
		static const unsigned long lengths[] = { 0, 1, 0x7FFFFFFF, 0x80000000UL, 0xFFFFFFFFUL };
		size_t pos = 8;
		BYTE *copy = (BYTE *)malloc(size);
		while (pos + 8 <= (size_t)size) {
			unsigned long len = ((unsigned long)good[pos] << 24) | ((unsigned long)good[pos + 1] << 16)
				| ((unsigned long)good[pos + 2] << 8) | good[pos + 3];
			size_t k;
			for (k = 0; k < sizeof(lengths) / sizeof(lengths[0]); k++) {
				memcpy(copy, good, size);
				copy[pos] = (BYTE)(lengths[k] >> 24); copy[pos + 1] = (BYTE)(lengths[k] >> 16);
				copy[pos + 2] = (BYTE)(lengths[k] >> 8); copy[pos + 3] = (BYTE)lengths[k];
				survive(copy, size);
				cases++;
			}
			if (len > (unsigned long)size) break;
			pos += 12 + len;
		}
		free(copy);
	}

	free(good);
	ok("%d damaged inputs, none of them crashed", cases);
}

int main(void) {
	FreeImage_Initialise(FALSE);
	FreeImage_SetOutputMessage(quiet);

	printf("FreeImage %s - APNG robustness tests\n", FreeImage_GetVersion());

	test_invalid();
	test_cut_pages();
	test_cut_last_row();
	test_damage();

	printf("\n%d check(s) passed, %d failure(s)\n", checks, failures);
	FreeImage_DeInitialise();
	return failures ? 1 : 0;
}
