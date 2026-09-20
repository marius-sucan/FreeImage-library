/*
 * FreeImage 3 - MNG robustness test
 *
 * The MNG plugin parses the container by hand: chunk headers, lengths, the
 * extent of each embedded datastream, the variable-length bodies of FRAM and
 * DEFI.  This throws damaged files at it and insists it neither crashes, hangs
 * nor allocates wildly.  Refusing a file is a perfectly good answer; the only
 * wrong answers are dying and lying.
 *
 * Worth running under AddressSanitizer - "make asan-run" in this directory -
 * because most of what can go wrong here is a read past the end of a buffer
 * that a normal build will not notice.
 *
 * Scratch files go to $MNG_TEST_TMP, or the current directory.
 *
 * Standalone: build with the Makefile in this directory, run from it.
 */

#include <stdarg.h>

#include "mngbuild.h"

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

#define W 16
#define H 16

/**
 * Open a file, walk every page it claims, and let go.  Whatever it answers is
 * acceptable; getting here again is the test.
 */
static void survive(const char *path, const char *what) {
	static const int flags[3] = { 0, MNG_PLAYBACK, FIF_LOAD_NOPIXELS };
	int f;

	for (f = 0; f < 3; f++) {
		FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE,
													  FALSE, flags[f]);
		if (mb) {
			int pages = FreeImage_GetPageCount(mb);
			int i;

			if (pages < 0) {
				fail("%s: a negative page count (%d)", what, pages);
			}
			for (i = 0; i < pages; i++) {
				FIBITMAP *dib = FreeImage_LockPage(mb, i);
				if (dib) {
					/* touch the pixels, so a bad pitch or size is not merely
					   allocated but used */
					if (FreeImage_HasPixels(dib)) {
						RGBQUAD colour;
						FreeImage_GetPixelColor(dib, 0, 0, &colour);
					}
					FreeImage_UnlockPage(mb, dib, FALSE);
				}
			}
			FreeImage_CloseMultiBitmap(mb, 0);
		}

		/* the single-image entry point takes the same walk */
		{
			FIBITMAP *dib = FreeImage_Load(FIF_MNG, path, flags[f]);
			if (dib) {
				FreeImage_Unload(dib);
			}
		}
	}
	ok("%s", what);
}

/**
 * A complete, valid file, to cut up in the tests below.
 *
 * It carries one of every chunk that steers the parser - MHDR, TERM, BACK,
 * FRAM, DEFI, LOOP and ENDL - so that the truncation and CRC tests below reach
 * all of them rather than only the two a minimal file would have.
 */
static void build_good(Buf *mng) {
	int i;
	static const BYTE COLOUR[3][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };

	buf_init(mng);
	mng_signature(mng);
	mng_mhdr(mng, 64, 64, 100, 3, 3, 0, 3);
	mng_term(mng, 3, 4);
	mng_back(mng, 0x2020, 0x4040, 0x8080, 1);
	mng_fram(mng, 1, 2, 10);
	mng_loop(mng, 0, 2);
	for (i = 0; i < 3; i++) {
		mng_defi(mng, 0, 0, 0, i * 8, i * 8);
		mng_image(mng, W, H, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2], 24);
	}
	mng_endl(mng, 0);
	mng_mend(mng);
}

/** Every truncation of a valid file. */
static void test_truncation(void) {
	Buf good;
	size_t cut;

	printf("truncation\n");
	build_good(&good);

	for (cut = 1; cut < good.size; cut++) {
		Buf part;
		const char *path;

		buf_init(&part);
		buf_add(&part, good.data, cut);
		path = write_file("mng_trunc.mng", &part);
		buf_free(&part);

		{
			static const int flags[2] = { 0, MNG_PLAYBACK };
			int f;
			for (f = 0; f < 2; f++) {
				FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE,
															  TRUE, FALSE, flags[f]);
				if (mb) {
					int pages = FreeImage_GetPageCount(mb), i;
					for (i = 0; i < pages; i++) {
						FIBITMAP *dib = FreeImage_LockPage(mb, i);
						if (dib) {
							FreeImage_UnlockPage(mb, dib, FALSE);
						}
					}
					FreeImage_CloseMultiBitmap(mb, 0);
				}
			}
		}
	}
	ok("%d truncations of a valid file, all survived", (int)good.size - 1);
	buf_free(&good);
}

/** A chunk length that runs past the end of the file, and other impossible ones. */
static void test_chunk_lengths(void) {
	static const DWORD lengths[] = { 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 100000u };
	size_t i;

	printf("impossible chunk lengths\n");

	for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		Buf mng;
		const char *path;
		char what[128];

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, W, H, 100, 1, 1, 0, 3);
		/* a chunk header claiming more bytes than exist, with no payload */
		buf_u32(&mng, lengths[i]);
		buf_add(&mng, "FRAM", 4);
		buf_u32(&mng, 0);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_mend(&mng);
		path = write_file("mng_badlen.mng", &mng);
		buf_free(&mng);

		snprintf(what, sizeof(what), "a chunk claiming 0x%08x bytes", lengths[i]);
		survive(path, what);
	}
}

/** A FRAM that promises fields it does not carry. */
static void test_short_fram(void) {
	static const int sizes[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	size_t i;

	printf("a FRAM cut short\n");

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		Buf mng, body;
		const char *path;
		char what[128];
		int n;

		buf_init(&body);
		buf_byte(&body, 1);          /* framing mode */
		buf_byte(&body, 0);          /* the empty name's separator */
		buf_byte(&body, 2);          /* change_interframe_delay: reset the default */
		buf_byte(&body, 1);          /* change_timeout: promises four more bytes */
		buf_byte(&body, 2);          /* change_clipping: promises seventeen more */
		buf_byte(&body, 1);          /* change_sync_id_list */
		buf_u32(&body, 25);          /* the delay, and then nothing else */
		/* keep only the first `sizes[i]` bytes of that */
		n = sizes[i] < (int)body.size ? sizes[i] : (int)body.size;

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, W, H, 100, 1, 1, 0, 3);
		chunk(&mng, "FRAM", body.data, (DWORD)n);
		buf_free(&body);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_mend(&mng);
		path = write_file("mng_shortfram.mng", &mng);
		buf_free(&mng);

		snprintf(what, sizeof(what), "a %d-byte FRAM", n);
		survive(path, what);
	}
}

/** A DEFI of every length, including ones the spec does not allow. */
static void test_defi_lengths(void) {
	int n;

	printf("a DEFI of every length\n");

	for (n = 0; n <= 28; n++) {
		Buf mng;
		const char *path;
		BYTE body[28];
		char what[64];

		memset(body, 0, sizeof(body));
		body[1] = 3;             /* object id 3 */
		body[7] = 10;            /* part of an x location */
		body[11] = 20;           /* part of a y location */

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, 64, 64, 100, 1, 1, 0, 3);
		chunk(&mng, "DEFI", body, (DWORD)n);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_mend(&mng);
		path = write_file("mng_defi_n.mng", &mng);
		buf_free(&mng);

		snprintf(what, sizeof(what), "a %d-byte DEFI", n);
		survive(path, what);
	}
}

/** An embedded image with no IEND: the file ends inside it. */
static void test_unterminated_image(void) {
	Buf mng, good;
	const char *path;
	size_t i, cut = 0;

	printf("an embedded image with no IEND\n");

	build_good(&good);
	/* find the first IEND and stop the file just before it */
	for (i = 8; i + 8 <= good.size; i++) {
		if (memcmp(good.data + i + 4, "IEND", 4) == 0) {
			cut = i;
			break;
		}
	}
	buf_init(&mng);
	buf_add(&mng, good.data, cut ? cut : good.size);
	buf_free(&good);
	path = write_file("mng_noiend.mng", &mng);
	buf_free(&mng);

	survive(path, "an image whose IEND never arrives");
}

/** Chunks that arrive where nothing expects them. */
static void test_misplaced_chunks(void) {
	Buf mng;
	const char *path;

	printf("misplaced chunks\n");

	/* MEND before any image */
	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 0, 0, 0, 3);
	mng_mend(&mng);
	path = write_file("mng_empty.mng", &mng);
	buf_free(&mng);
	survive(path, "a MEND with no image before it");

	/* a header and nothing else */
	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 3, 3, 0, 3);
	path = write_file("mng_headeronly.mng", &mng);
	buf_free(&mng);
	survive(path, "a MHDR and nothing else");

	/* a signature and nothing else */
	buf_init(&mng);
	mng_signature(&mng);
	path = write_file("mng_sigonly.mng", &mng);
	buf_free(&mng);
	survive(path, "a signature and nothing else");

	/* an ENDL with no LOOP, and a LOOP with no ENDL */
	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 1, 1, 0, 3);
	mng_endl(&mng, 0);
	mng_image(&mng, W, H, 255, 0, 0, 24);
	mng_loop(&mng, 0, 4);
	mng_mend(&mng);
	path = write_file("mng_unbalanced.mng", &mng);
	buf_free(&mng);
	survive(path, "an ENDL with no LOOP and a LOOP with no ENDL");

	/* a zero-count LOOP that is never closed */
	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 1, 1, 0, 3);
	mng_loop(&mng, 0, 0);
	mng_image(&mng, W, H, 255, 0, 0, 24);
	mng_mend(&mng);
	path = write_file("mng_loop0_open.mng", &mng);
	buf_free(&mng);
	survive(path, "a zero-count LOOP with no ENDL");

	/* deeply nested loops */
	{
		int d;
		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, W, H, 100, 1, 1, 0, 3);
		for (d = 0; d < 250; d++) {
			mng_loop(&mng, (BYTE)d, 2);
		}
		mng_image(&mng, W, H, 255, 0, 0, 24);
		for (d = 249; d >= 0; d--) {
			mng_endl(&mng, (BYTE)d);
		}
		mng_mend(&mng);
		path = write_file("mng_deep.mng", &mng);
		buf_free(&mng);
		survive(path, "250 nested loops");
	}
}

/** A SHOW naming objects that do not exist, and huge ranges. */
static void test_show_ranges(void) {
	static const struct { WORD first, last; BYTE mode; const char *what; } cases[] = {
		{ 1,      1,      0, "SHOW of an object that was never defined" },
		{ 1,      0xFFFF, 0, "SHOW of every object id there is" },
		{ 0xFFFF, 1,      0, "SHOW with the range the wrong way round" },
		{ 1,      2,      6, "SHOW stepping through a range" },
		{ 1,      2,      7, "SHOW stepping without displaying" },
		{ 1,      2,      9, "SHOW with a mode the spec does not define" },
	};
	size_t i;

	printf("SHOW ranges\n");

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		Buf mng;
		const char *path;
		int s;

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, W, H, 100, 2, 2, 0, 3);
		mng_fram(&mng, 1, 2, 10);
		mng_defi(&mng, 1, 1, 0, 0, 0);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_defi(&mng, 2, 1, 0, 0, 0);
		mng_image(&mng, W, H, 0, 255, 0, 24);
		/* several SHOWs, so a stepping mode gets to step */
		for (s = 0; s < 5; s++) {
			mng_show(&mng, cases[i].first, cases[i].last, cases[i].mode);
		}
		mng_mend(&mng);
		path = write_file("mng_show_range.mng", &mng);
		buf_free(&mng);

		survive(path, cases[i].what);
	}
}

/** A bad CRC on a chunk that steers the parser. */
static void test_bad_crc(void) {
	static const char *types[] = { "MHDR", "FRAM", "DEFI", "BACK", "TERM", "LOOP" };
	size_t t;

	printf("bad CRCs\n");

	for (t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
		Buf good;
		const char *path;
		size_t i;
		int found = 0;

		build_good(&good);
		/* flip a bit in the named chunk's CRC */
		for (i = 8; i + 12 <= good.size; ) {
			DWORD length = ((DWORD)good.data[i] << 24) | ((DWORD)good.data[i + 1] << 16)
						 | ((DWORD)good.data[i + 2] << 8) | good.data[i + 3];
			if (length > good.size) {
				break;
			}
			if (memcmp(good.data + i + 4, types[t], 4) == 0) {
				good.data[i + 8 + length + 3] ^= 0x01;
				found = 1;
				break;
			}
			i += 12 + length;
		}
		if (found) {
			char what[64];
			path = write_file("mng_badcrc.mng", &good);
			snprintf(what, sizeof(what), "a %s with a bad CRC", types[t]);
			survive(path, what);
		} else {
			/* a silently skipped case is a test that reports a check it never
			   ran, so say so instead */
			fail("build_good() carries no %s to damage", types[t]);
		}
		buf_free(&good);
	}
}

/** A MHDR claiming a canvas far bigger than anything in the file. */
static void test_huge_canvas(void) {
	static const DWORD sizes[][2] = {
		{ 0xFFFFFFFFu, 0xFFFFFFFFu },
		{ 0x7FFFFFFFu, 0x7FFFFFFFu },
		{ 100000u, 100000u },
		{ 0u, 0u },
	};
	size_t i;

	printf("an impossible canvas\n");

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		Buf mng;
		const char *path;
		char what[96];

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, sizes[i][0], sizes[i][1], 100, 1, 1, 0, 3);
		mng_fram(&mng, 1, 2, 10);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_mend(&mng);
		path = write_file("mng_bigcanvas.mng", &mng);
		buf_free(&mng);

		snprintf(what, sizeof(what), "a %ux%u canvas", sizes[i][0], sizes[i][1]);
		survive(path, what);
	}
}

/** An image whose DEFI puts it far off the canvas, in both directions. */
static void test_off_canvas(void) {
	static const LONG places[][2] = {
		{ -1000, -1000 }, { 1000, 1000 }, { -8, -8 },
		{ 0x7FFFFFF0, 0x7FFFFFF0 }, { -2147483647 - 1, -2147483647 - 1 },
	};
	size_t i;

	printf("images off the canvas\n");

	for (i = 0; i < sizeof(places) / sizeof(places[0]); i++) {
		Buf mng;
		const char *path;
		char what[96];

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, 32, 32, 100, 1, 1, 0, 3);
		mng_fram(&mng, 1, 2, 10);
		mng_defi(&mng, 0, 0, 0, places[i][0], places[i][1]);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_mend(&mng);
		path = write_file("mng_offcanvas.mng", &mng);
		buf_free(&mng);

		snprintf(what, sizeof(what), "an image at (%ld,%ld)",
				 (long)places[i][0], (long)places[i][1]);
		survive(path, what);
	}
}

/** Garbage that merely starts with the signature. */
static void test_garbage(void) {
	Buf mng;
	const char *path;
	int i;

	printf("garbage behind a valid signature\n");

	buf_init(&mng);
	mng_signature(&mng);
	for (i = 0; i < 4096; i++) {
		buf_byte(&mng, (BYTE)((i * 37) ^ (i >> 3)));
	}
	path = write_file("mng_garbage.mng", &mng);
	buf_free(&mng);

	survive(path, "4 KB of garbage behind a MNG signature");
}

/* --------------------------------------------------------------------- */

int main(void) {
	FreeImage_Initialise(FALSE);
	FreeImage_SetOutputMessage(quiet);

	test_truncation();
	test_chunk_lengths();
	test_short_fram();
	test_defi_lengths();
	test_unterminated_image();
	test_misplaced_chunks();
	test_show_ranges();
	test_bad_crc();
	test_huge_canvas();
	test_off_canvas();
	test_garbage();

	FreeImage_DeInitialise();

	printf("\n%d check(s), %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
