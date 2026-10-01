/* FreeImage 3 - MNG container test; expectations follow the MNG 1.0 spec */
/* scratch files: $MNG_TEST_TMP or the current directory */

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

/* --------------------------------------------------------------------- */

#define W 16
#define H 16

static const BYTE COLOUR[6][3] = {
	{ 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 },
	{ 255, 255, 0 }, { 0, 255, 255 }, { 255, 0, 255 }
};

/* FIMD_ANIMATION tag as a long, or `missing` */
static long anim_tag(FIBITMAP *dib, const char *key, long missing) {
	FITAG *tag = NULL;
	const void *value;

	if (!FreeImage_GetMetadata(FIMD_ANIMATION, dib, key, &tag) || !tag) {
		return missing;
	}
	value = FreeImage_GetTagValue(tag);
	if (!value) {
		return missing;
	}
	switch (FreeImage_GetTagType(tag)) {
		case FIDT_BYTE:  return *(const BYTE *)value;
		case FIDT_SHORT: return *(const WORD *)value;
		case FIDT_LONG:  return (long)*(const DWORD *)value;
		case FIDT_SLONG: return (long)*(const LONG *)value;
		default: return missing;
	}
}

/* colour at (x, y), y counted down from the top */
static int pixel_at(FIBITMAP *dib, int x, int y, RGBQUAD *out) {
	unsigned fi_y;

	if (x < 0 || y < 0 || (unsigned)x >= FreeImage_GetWidth(dib)
		|| (unsigned)y >= FreeImage_GetHeight(dib)) {
		return 0;
	}
	fi_y = FreeImage_GetHeight(dib) - 1 - (unsigned)y;
	return FreeImage_GetPixelColor(dib, (unsigned)x, fi_y, out) ? 1 : 0;
}

static int same_rgb(const RGBQUAD *got, const BYTE *want) {
	return got->rgbRed == want[0] && got->rgbGreen == want[1] && got->rgbBlue == want[2];
}

/* --------------------------------------------------------------------- */
/* the tests                                                             */
/* --------------------------------------------------------------------- */

/* MNG-VLC, no FRAM: default 1 tick = 100 ms at 10 ticks/s */
static void test_vlc(void) {
	Buf mng;
	const char *path;
	FIMULTIBITMAP *mb;
	int i;

	printf("MNG-VLC, a bare sequence of images\n");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 10, 5, 5, 0, 1);
	for (i = 0; i < 5; i++) {
		mng_image(&mng, W, H, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2], 24);
	}
	mng_mend(&mng);
	path = write_file("mng_vlc5.mng", &mng);
	buf_free(&mng);

	if (FreeImage_GetFileType(path, 0) != FIF_MNG) {
		fail("the file is not recognised as a MNG");
		return;
	}
	ok("recognised as a MNG");

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap returned NULL");
		return;
	}

	if (FreeImage_GetPageCount(mb) != 5) {
		fail("%d pages, expected 5 - one per embedded image", FreeImage_GetPageCount(mb));
	} else {
		ok("five embedded images are five pages");
	}

	for (i = 0; i < 5 && i < FreeImage_GetPageCount(mb); i++) {
		FIBITMAP *dib = FreeImage_LockPage(mb, i);
		RGBQUAD colour;

		if (!dib) {
			fail("page %d could not be locked", i);
			continue;
		}
		if (FreeImage_GetWidth(dib) != W || FreeImage_GetHeight(dib) != H) {
			fail("page %d is %ux%u, expected %dx%d", i,
				 FreeImage_GetWidth(dib), FreeImage_GetHeight(dib), W, H);
		} else if (!pixel_at(dib, 0, 0, &colour) || !same_rgb(&colour, COLOUR[i])) {
			fail("page %d is (%u,%u,%u), expected (%u,%u,%u)", i,
				 colour.rgbRed, colour.rgbGreen, colour.rgbBlue,
				 COLOUR[i][0], COLOUR[i][1], COLOUR[i][2]);
		} else if (anim_tag(dib, "FrameTime", -1) != 100) {
			fail("page %d is %ld ms, expected 100 - one tick at 10 ticks a second",
				 i, anim_tag(dib, "FrameTime", -1));
		} else if (anim_tag(dib, "Loop", -1) != 1) {
			fail("page %d loops %ld times, expected 1 - there is no TERM or LOOP",
				 i, anim_tag(dib, "Loop", -1));
		} else {
			ok("page %d: the right frame, 100 ms, one play", i);
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}

	FreeImage_CloseMultiBitmap(mb, 0);
}

/* mode 1: delay on every layer; mode 2: on each subframe's last layer */
static void test_framing_modes(void) {
	Buf mng;
	const char *path;
	FIMULTIBITMAP *mb;
	int i;
	/* mode 2, three images, a FRAM, two more: two subframes */
	static const long expect_mode2[5] = { 0, 0, 500, 0, 500 };

	printf("FRAM framing modes\n");

	/* --- mode 1: 25 ticks at 100 a second is 250 ms on every layer --- */
	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 3, 3, 0, 3);
	mng_fram(&mng, 1, 2, 25);
	for (i = 0; i < 3; i++) {
		mng_image(&mng, W, H, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2], 24);
	}
	mng_mend(&mng);
	path = write_file("mng_mode1.mng", &mng);
	buf_free(&mng);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("mode 1: FreeImage_OpenMultiBitmap returned NULL");
	} else {
		int bad = 0;
		for (i = 0; i < FreeImage_GetPageCount(mb); i++) {
			FIBITMAP *dib = FreeImage_LockPage(mb, i);
			if (!dib) { bad = 1; continue; }
			if (anim_tag(dib, "FrameTime", -1) != 250) {
				fail("mode 1: page %d is %ld ms, expected 250 on every layer",
					 i, anim_tag(dib, "FrameTime", -1));
				bad = 1;
			}
			FreeImage_UnlockPage(mb, dib, FALSE);
		}
		if (FreeImage_GetPageCount(mb) != 3) {
			fail("mode 1: %d pages, expected 3", FreeImage_GetPageCount(mb));
		} else if (!bad) {
			ok("mode 1: the delay is on every foreground layer");
		}
		FreeImage_CloseMultiBitmap(mb, 0);
	}

	/* --- mode 2: the delay belongs to the last layer of each subframe --- */
	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 5, 2, 0, 3);
	mng_fram(&mng, 2, 2, 50);
	for (i = 0; i < 3; i++) {
		mng_image(&mng, W, H, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2], 24);
	}
	mng_fram(&mng, 0, 0, 0);
	for (i = 3; i < 5; i++) {
		mng_image(&mng, W, H, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2], 24);
	}
	mng_mend(&mng);
	path = write_file("mng_mode2.mng", &mng);
	buf_free(&mng);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("mode 2: FreeImage_OpenMultiBitmap returned NULL");
	} else {
		int bad = 0;
		if (FreeImage_GetPageCount(mb) != 5) {
			fail("mode 2: %d pages, expected 5", FreeImage_GetPageCount(mb));
			bad = 1;
		}
		for (i = 0; i < FreeImage_GetPageCount(mb) && i < 5; i++) {
			FIBITMAP *dib = FreeImage_LockPage(mb, i);
			if (!dib) { bad = 1; continue; }
			if (anim_tag(dib, "FrameTime", -1) != expect_mode2[i]) {
				fail("mode 2: page %d is %ld ms, expected %ld", i,
					 anim_tag(dib, "FrameTime", -1), expect_mode2[i]);
				bad = 1;
			}
			FreeImage_UnlockPage(mb, dib, FALSE);
		}
		if (!bad) {
			ok("mode 2: only the last layer of a subframe carries the delay");
		}
		FreeImage_CloseMultiBitmap(mb, 0);
	}

	/* --- a one-shot delay lasts until the next FRAM, then the default --- */
	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 3, 3, 0, 3);
	mng_fram(&mng, 1, 2, 10);              /* default becomes 10 ticks */
	mng_image(&mng, W, H, COLOUR[0][0], COLOUR[0][1], COLOUR[0][2], 24);
	mng_fram(&mng, 0, 1, 50);              /* upcoming subframe only */
	mng_image(&mng, W, H, COLOUR[1][0], COLOUR[1][1], COLOUR[1][2], 24);
	mng_fram_empty(&mng);                  /* just a delimiter: back to default */
	mng_image(&mng, W, H, COLOUR[2][0], COLOUR[2][1], COLOUR[2][2], 24);
	mng_mend(&mng);
	path = write_file("mng_oneshot.mng", &mng);
	buf_free(&mng);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("one-shot: FreeImage_OpenMultiBitmap returned NULL");
	} else {
		static const long expect[3] = { 100, 500, 100 };
		int bad = 0;
		if (FreeImage_GetPageCount(mb) != 3) {
			fail("one-shot: %d pages, expected 3 - an empty FRAM is not an image",
				 FreeImage_GetPageCount(mb));
			bad = 1;
		}
		for (i = 0; i < FreeImage_GetPageCount(mb) && i < 3; i++) {
			FIBITMAP *dib = FreeImage_LockPage(mb, i);
			if (!dib) { bad = 1; continue; }
			if (anim_tag(dib, "FrameTime", -1) != expect[i]) {
				fail("one-shot: page %d is %ld ms, expected %ld", i,
					 anim_tag(dib, "FrameTime", -1), expect[i]);
				bad = 1;
			}
			FreeImage_UnlockPage(mb, dib, FALSE);
		}
		if (!bad) {
			ok("a one-shot delay lasts one subframe, then the default returns");
		}
		FreeImage_CloseMultiBitmap(mb, 0);
	}
}

/* ticks_per_second 0: the tick is infinite, every delay is 0 */
static void test_infinite_tick(void) {
	Buf mng;
	const char *path;
	FIMULTIBITMAP *mb;
	int i, bad = 0;

	printf("an infinite tick\n");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 0, 3, 3, 0, 3);
	mng_fram(&mng, 1, 2, 25);
	for (i = 0; i < 3; i++) {
		mng_image(&mng, W, H, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2], 24);
	}
	mng_mend(&mng);
	path = write_file("mng_tps0.mng", &mng);
	buf_free(&mng);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap returned NULL");
		return;
	}
	for (i = 0; i < FreeImage_GetPageCount(mb); i++) {
		FIBITMAP *dib = FreeImage_LockPage(mb, i);
		if (!dib) { bad = 1; continue; }
		if (anim_tag(dib, "FrameTime", -1) != 0) {
			fail("page %d is %ld ms; with ticks_per_second 0 it must be 0",
				 i, anim_tag(dib, "FrameTime", -1));
			bad = 1;
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	if (!bad) {
		ok("ticks_per_second 0 leaves every delay at 0");
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

static void test_loop_counts(void) {
	struct { const char *name; int use_term; DWORD iterations; long expect; } cases[] = {
		{ "mng_loop3.mng",   0, 3,          3 },
		{ "mng_term5.mng",   1, 5,          5 },
		{ "mng_forever.mng", 1, 0x7FFFFFFF, 0 },   /* forever is spelled 0 */
	};
	size_t c;

	printf("loop counts\n");

	for (c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
		Buf mng;
		const char *path;
		FIMULTIBITMAP *mb;
		FIBITMAP *dib;
		int i;

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, W, H, 100, 3, 3, 0, 3);
		if (cases[c].use_term) {
			mng_term(&mng, 3, cases[c].iterations);
		}
		mng_fram(&mng, 1, 2, 10);
		if (!cases[c].use_term) {
			mng_loop(&mng, 0, cases[c].iterations);
		}
		for (i = 0; i < 3; i++) {
			mng_image(&mng, W, H, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2], 24);
		}
		if (!cases[c].use_term) {
			mng_endl(&mng, 0);
		}
		mng_mend(&mng);
		path = write_file(cases[c].name, &mng);
		buf_free(&mng);

		mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
		if (!mb) {
			fail("%s: FreeImage_OpenMultiBitmap returned NULL", cases[c].name);
			continue;
		}
		if (FreeImage_GetPageCount(mb) != 3) {
			fail("%s: %d pages, expected 3 - a loop must not repeat the images",
				 cases[c].name, FreeImage_GetPageCount(mb));
			FreeImage_CloseMultiBitmap(mb, 0);
			continue;
		}
		dib = FreeImage_LockPage(mb, 0);
		if (!dib) {
			fail("%s: page 0 could not be locked", cases[c].name);
		} else {
			if (anim_tag(dib, "Loop", -1) != cases[c].expect) {
				fail("%s: loop is %ld, expected %ld", cases[c].name,
					 anim_tag(dib, "Loop", -1), cases[c].expect);
			} else {
				ok("%s: three pages, loop %ld", cases[c].name, cases[c].expect);
			}
			FreeImage_UnlockPage(mb, dib, FALSE);
		}
		FreeImage_CloseMultiBitmap(mb, 0);
	}
}

/* a zero-count LOOP skips its body */
static void test_zero_loop(void) {
	Buf mng;
	const char *path;
	FIMULTIBITMAP *mb;
	FIBITMAP *dib;
	RGBQUAD colour;

	printf("a loop that runs no times\n");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 1, 1, 0, 3);
	mng_fram(&mng, 1, 2, 10);
	mng_image(&mng, W, H, COLOUR[0][0], COLOUR[0][1], COLOUR[0][2], 24);
	mng_loop(&mng, 0, 0);                    /* skip to the matching ENDL */
	mng_image(&mng, W, H, COLOUR[1][0], COLOUR[1][1], COLOUR[1][2], 24);
	mng_image(&mng, W, H, COLOUR[2][0], COLOUR[2][1], COLOUR[2][2], 24);
	mng_endl(&mng, 0);
	mng_image(&mng, W, H, COLOUR[3][0], COLOUR[3][1], COLOUR[3][2], 24);
	mng_mend(&mng);
	path = write_file("mng_loop0.mng", &mng);
	buf_free(&mng);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap returned NULL");
		return;
	}
	if (FreeImage_GetPageCount(mb) != 2) {
		fail("%d pages, expected 2 - the two images inside the loop are skipped",
			 FreeImage_GetPageCount(mb));
		FreeImage_CloseMultiBitmap(mb, 0);
		return;
	}
	dib = FreeImage_LockPage(mb, 1);
	if (!dib) {
		fail("page 1 could not be locked");
	} else {
		if (!pixel_at(dib, 0, 0, &colour) || !same_rgb(&colour, COLOUR[3])) {
			fail("page 1 is (%u,%u,%u), expected the image after the ENDL (%u,%u,%u)",
				 colour.rgbRed, colour.rgbGreen, colour.rgbBlue,
				 COLOUR[3][0], COLOUR[3][1], COLOUR[3][2]);
		} else {
			ok("a zero-count loop skips its body and resumes after the ENDL");
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

static void test_placement_and_playback(void) {
	Buf mng;
	const char *path;
	FIMULTIBITMAP *mb;
	FIBITMAP *dib;
	RGBQUAD colour;
	static const BYTE BACKGROUND[3] = { 0x20, 0x40, 0x80 };

	printf("DEFI placement, BACK, and MNG_PLAYBACK\n");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, 64, 64, 100, 2, 2, 0, 3);
	mng_back(&mng, 0x2020, 0x4040, 0x8080, 1);
	mng_fram(&mng, 1, 2, 10);
	mng_defi(&mng, 0, 0, 0, 8, 8);
	mng_image(&mng, W, H, COLOUR[0][0], COLOUR[0][1], COLOUR[0][2], 24);
	mng_defi(&mng, 0, 0, 0, 40, 40);
	mng_image(&mng, W, H, COLOUR[1][0], COLOUR[1][1], COLOUR[1][2], 24);
	mng_mend(&mng);
	path = write_file("mng_placed.mng", &mng);
	buf_free(&mng);

	/* raw: the stored rectangle, tagged with its position */
	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap returned NULL");
		return;
	}
	dib = FreeImage_LockPage(mb, 1);
	if (!dib) {
		fail("page 1 could not be locked");
	} else {
		if (FreeImage_GetWidth(dib) != W || FreeImage_GetHeight(dib) != H) {
			fail("raw page 1 is %ux%u, expected the stored %dx%d rectangle",
				 FreeImage_GetWidth(dib), FreeImage_GetHeight(dib), W, H);
		} else if (anim_tag(dib, "FrameLeft", -1) != 40
				   || anim_tag(dib, "FrameTop", -1) != 40) {
			fail("raw page 1 is at (%ld,%ld), expected (40,40)",
				 anim_tag(dib, "FrameLeft", -1), anim_tag(dib, "FrameTop", -1));
		} else if (anim_tag(dib, "LogicalWidth", -1) != 64
				   || anim_tag(dib, "LogicalHeight", -1) != 64) {
			fail("the canvas is %ldx%ld, expected the MHDR's 64x64",
				 anim_tag(dib, "LogicalWidth", -1), anim_tag(dib, "LogicalHeight", -1));
		} else {
			ok("a raw page is the stored rectangle, tagged with its place on the canvas");
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);

	/* --- playback: the canvas a viewer would show --- */
	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, MNG_PLAYBACK);
	if (!mb) {
		fail("MNG_PLAYBACK: FreeImage_OpenMultiBitmap returned NULL");
		return;
	}
	dib = FreeImage_LockPage(mb, 1);
	if (!dib) {
		fail("MNG_PLAYBACK: page 1 could not be locked");
	} else {
		int bad = 0;

		if (FreeImage_GetWidth(dib) != 64 || FreeImage_GetHeight(dib) != 64) {
			fail("MNG_PLAYBACK: page 1 is %ux%u, expected the 64x64 canvas",
				 FreeImage_GetWidth(dib), FreeImage_GetHeight(dib));
			bad = 1;
		}
		if (FreeImage_GetBPP(dib) != 32) {
			fail("MNG_PLAYBACK: page 1 is %u bpp, expected 32", FreeImage_GetBPP(dib));
			bad = 1;
		}
		/* mode 1 adds no background layer: image 1 stays under image 2 */
		if (!bad && (!pixel_at(dib, 10, 10, &colour) || !same_rgb(&colour, COLOUR[0]))) {
			fail("MNG_PLAYBACK: (10,10) is (%u,%u,%u), expected the first image - "
				 "framing mode 1 does not restore the background",
				 colour.rgbRed, colour.rgbGreen, colour.rgbBlue);
			bad = 1;
		}
		if (!bad && (!pixel_at(dib, 45, 45, &colour) || !same_rgb(&colour, COLOUR[1]))) {
			fail("MNG_PLAYBACK: (45,45) is (%u,%u,%u), expected the second image",
				 colour.rgbRed, colour.rgbGreen, colour.rgbBlue);
			bad = 1;
		}
		if (!bad && (!pixel_at(dib, 30, 30, &colour) || !same_rgb(&colour, BACKGROUND))) {
			fail("MNG_PLAYBACK: (30,30) is (%u,%u,%u), expected the BACK colour "
				 "(%u,%u,%u)", colour.rgbRed, colour.rgbGreen, colour.rgbBlue,
				 BACKGROUND[0], BACKGROUND[1], BACKGROUND[2]);
			bad = 1;
		}
		if (!bad) {
			ok("MNG_PLAYBACK composites the frames onto the BACK colour");
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

/* a short DEFI of a known id keeps the fields it omits */
static void test_defi_omitted_fields(void) {
	Buf mng;
	const char *path;
	FIMULTIBITMAP *mb;
	FIBITMAP *dib;

	printf("DEFI's omitted fields\n");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, 64, 64, 100, 2, 2, 0, 3);
	mng_fram(&mng, 1, 2, 10);
	mng_defi(&mng, 7, 0, 0, 10, 20);
	mng_image(&mng, W, H, COLOUR[0][0], COLOUR[0][1], COLOUR[0][2], 24);
	mng_defi_short(&mng, 7);                 /* names object 7 and nothing else */
	mng_image(&mng, W, H, COLOUR[1][0], COLOUR[1][1], COLOUR[1][2], 24);
	mng_mend(&mng);
	path = write_file("mng_defi_short.mng", &mng);
	buf_free(&mng);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap returned NULL");
		return;
	}
	dib = FreeImage_LockPage(mb, 1);
	if (!dib) {
		fail("page 1 could not be locked");
	} else {
		if (anim_tag(dib, "FrameLeft", -1) != 10 || anim_tag(dib, "FrameTop", -1) != 20) {
			fail("page 1 is at (%ld,%ld); a 2-byte DEFI naming a known object must "
				 "leave its location at (10,20)",
				 anim_tag(dib, "FrameLeft", -1), anim_tag(dib, "FrameTop", -1));
		} else {
			ok("a short DEFI leaves a known object's location alone");
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

/* SHOW reuses objects: 2 definitions, 3 SHOWs, 3 pages */
static void test_show(void) {
	Buf mng;
	const char *path;
	FIMULTIBITMAP *mb;
	int i, bad = 0;
	static const int expect[3] = { 0, 1, 0 };

	printf("SHOW displays stored objects\n");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 3, 3, 0, 3);
	mng_fram(&mng, 1, 2, 10);
	mng_defi(&mng, 1, 1, 0, 0, 0);           /* do_not_show: not a page */
	mng_image(&mng, W, H, COLOUR[0][0], COLOUR[0][1], COLOUR[0][2], 24);
	mng_defi(&mng, 2, 1, 0, 0, 0);
	mng_image(&mng, W, H, COLOUR[1][0], COLOUR[1][1], COLOUR[1][2], 24);
	mng_show(&mng, 1, 1, 0);
	mng_show(&mng, 2, 2, 0);
	mng_show(&mng, 1, 1, 0);
	mng_mend(&mng);
	path = write_file("mng_show.mng", &mng);
	buf_free(&mng);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap returned NULL");
		return;
	}
	if (FreeImage_GetPageCount(mb) != 3) {
		fail("%d pages, expected 3 - the definitions are hidden, the SHOWs are the "
			 "layers", FreeImage_GetPageCount(mb));
		FreeImage_CloseMultiBitmap(mb, 0);
		return;
	}
	for (i = 0; i < 3; i++) {
		FIBITMAP *dib = FreeImage_LockPage(mb, i);
		RGBQUAD colour;

		if (!dib) { fail("page %d could not be locked", i); bad = 1; continue; }
		if (!pixel_at(dib, 0, 0, &colour) || !same_rgb(&colour, COLOUR[expect[i]])) {
			fail("page %d is (%u,%u,%u), expected object %d", i,
				 colour.rgbRed, colour.rgbGreen, colour.rgbBlue, expect[i] + 1);
			bad = 1;
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	if (!bad) {
		ok("three SHOWs of two objects are three pages, in the order shown");
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

static void test_header_only(void) {
	Buf mng;
	const char *path;
	FIMULTIBITMAP *mb;
	int i, bad = 0;

	printf("header-only loads\n");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 10, 3, 3, 0, 1);
	for (i = 0; i < 3; i++) {
		mng_image(&mng, W, H, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2], 24);
	}
	mng_mend(&mng);
	path = write_file("mng_nopixels.mng", &mng);
	buf_free(&mng);

	if (!FreeImage_FIFSupportsNoPixels(FIF_MNG)) {
		fail("FIF_MNG says it does not support FIF_LOAD_NOPIXELS");
		return;
	}

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, FIF_LOAD_NOPIXELS);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap returned NULL");
		return;
	}
	if (FreeImage_GetPageCount(mb) != 3) {
		fail("%d pages, expected 3", FreeImage_GetPageCount(mb));
		bad = 1;
	}
	for (i = 0; i < FreeImage_GetPageCount(mb); i++) {
		FIBITMAP *dib = FreeImage_LockPage(mb, i);
		if (!dib) { fail("page %d could not be locked", i); bad = 1; continue; }
		if (FreeImage_HasPixels(dib)) {
			fail("page %d carries pixels under FIF_LOAD_NOPIXELS", i);
			bad = 1;
		} else if (FreeImage_GetWidth(dib) != W || FreeImage_GetHeight(dib) != H) {
			fail("page %d is %ux%u, expected %dx%d even with no pixels", i,
				 FreeImage_GetWidth(dib), FreeImage_GetHeight(dib), W, H);
			bad = 1;
		} else if (anim_tag(dib, "FrameTime", -1) != 100) {
			fail("page %d is %ld ms with no pixels, expected 100",
				 i, anim_tag(dib, "FrameTime", -1));
			bad = 1;
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	if (!bad) {
		ok("a header-only load gives the size and the timing, and no pixels");
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

/** FreeImage_Load on an animation gives its first frame. */
static void test_single_load(void) {
	Buf mng;
	const char *path;
	FIBITMAP *dib;
	RGBQUAD colour;

	printf("FreeImage_Load on an animation\n");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 10, 3, 3, 0, 1);
	mng_image(&mng, W, H, COLOUR[0][0], COLOUR[0][1], COLOUR[0][2], 24);
	mng_image(&mng, W, H, COLOUR[1][0], COLOUR[1][1], COLOUR[1][2], 24);
	mng_image(&mng, W, H, COLOUR[2][0], COLOUR[2][1], COLOUR[2][2], 24);
	mng_mend(&mng);
	path = write_file("mng_single.mng", &mng);
	buf_free(&mng);

	dib = FreeImage_Load(FIF_MNG, path, 0);
	if (!dib) {
		fail("FreeImage_Load returned NULL");
		return;
	}
	if (!pixel_at(dib, 0, 0, &colour) || !same_rgb(&colour, COLOUR[0])) {
		fail("FreeImage_Load gave (%u,%u,%u), expected the first frame (%u,%u,%u)",
			 colour.rgbRed, colour.rgbGreen, colour.rgbBlue,
			 COLOUR[0][0], COLOUR[0][1], COLOUR[0][2]);
	} else {
		ok("FreeImage_Load gives the first frame");
	}
	FreeImage_Unload(dib);
}

static void test_alpha_compositing(void) {
	Buf mng;
	const char *path;
	FIMULTIBITMAP *mb;
	FIBITMAP *dib;
	RGBQUAD colour;

	printf("alpha compositing\n");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 2, 2, 0, 0x0B);
	mng_fram(&mng, 1, 2, 10);
	mng_image(&mng, W, H, 0, 255, 0, 24);                 /* opaque green */
	mng_image_alpha(&mng, W, H, 255, 0, 0, 0);            /* fully transparent */
	mng_mend(&mng);
	path = write_file("mng_alpha.mng", &mng);
	buf_free(&mng);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, MNG_PLAYBACK);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap returned NULL");
		return;
	}
	dib = FreeImage_LockPage(mb, 1);
	if (!dib) {
		fail("page 1 could not be locked");
	} else {
		static const BYTE GREEN[3] = { 0, 255, 0 };
		if (!pixel_at(dib, 0, 0, &colour) || !same_rgb(&colour, GREEN)) {
			fail("(0,0) is (%u,%u,%u); a fully transparent frame must leave the "
				 "green under it showing",
				 colour.rgbRed, colour.rgbGreen, colour.rgbBlue);
		} else {
			ok("a transparent frame composites over what is under it");
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

/* huge MHDR canvas: composing is refused, the pages still read */
static void test_absurd_canvas(void) {
	Buf mng;
	const char *path;
	FIMULTIBITMAP *mb;
	FIBITMAP *dib;
	RGBQUAD colour;

	printf("a canvas the file cannot justify\n");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, 65535, 65535, 100, 1, 1, 0, 3);
	mng_fram(&mng, 1, 2, 10);
	mng_image(&mng, W, H, COLOUR[0][0], COLOUR[0][1], COLOUR[0][2], 24);
	mng_mend(&mng);
	path = write_file("mng_absurd.mng", &mng);
	buf_free(&mng);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("the file cannot be opened at all");
		return;
	}
	dib = FreeImage_LockPage(mb, 0);
	if (!dib) {
		fail("page 0 could not be read, though it is only %dx%d", W, H);
	} else {
		if (FreeImage_GetWidth(dib) != W || FreeImage_GetHeight(dib) != H) {
			fail("page 0 is %ux%u, expected the stored %dx%d",
				 FreeImage_GetWidth(dib), FreeImage_GetHeight(dib), W, H);
		} else if (!pixel_at(dib, 0, 0, &colour) || !same_rgb(&colour, COLOUR[0])) {
			fail("page 0 is (%u,%u,%u), expected the stored image",
				 colour.rgbRed, colour.rgbGreen, colour.rgbBlue);
		} else {
			ok("the images are still readable without MNG_PLAYBACK");
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, MNG_PLAYBACK);
	if (!mb) {
		fail("MNG_PLAYBACK refused to open the file; only the composing should fail");
		return;
	}
	dib = FreeImage_LockPage(mb, 0);
	if (dib) {
		fail("MNG_PLAYBACK composed a %ux%u canvas instead of refusing it",
			 FreeImage_GetWidth(dib), FreeImage_GetHeight(dib));
		FreeImage_UnlockPage(mb, dib, FALSE);
	} else {
		ok("MNG_PLAYBACK refuses to compose a canvas that size");
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

/* --------------------------------------------------------------------- */

/* a PNG of dib minus its signature, with a chunk of the given type after IHDR when type is given */
static int mng_png_with(Buf *out, FIBITMAP *dib, const char *type, const void *payload, DWORD length) {
	FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
	BYTE *png = NULL;
	DWORD size = 0;
	int ok = mem && FreeImage_SaveToMemory(FIF_PNG, dib, mem, 0) && FreeImage_AcquireMemory(mem, &png, &size) && size > 33;
	if (ok) {
		buf_add(out, png + 8, 25);	/* IHDR */
		if (type) {
			chunk(out, type, payload, length);
		}
		buf_add(out, png + 33, size - 33);
	}
	if (mem) {
		FreeImage_CloseMemory(mem);
	}
	return ok;
}

/* a PNG of dib minus its signature, with a cICP chunk after IHDR when cicp is given */
static int mng_png(Buf *out, FIBITMAP *dib, const BYTE *cicp) {
	return mng_png_with(out, dib, cicp ? "cICP" : NULL, cicp, 4);
}

/* the payload of a PNG's iCCP chunk, as FreeImage writes it for this profile */
static int iccp_payload(Buf *out, const void *profile, DWORD profile_size) {
	FIBITMAP *dib = FreeImage_Allocate(1, 1, 24, 0, 0, 0);
	Buf png;
	size_t pos = 0;
	int found = 0;
	buf_init(&png);
	FreeImage_CreateICCProfile(dib, (void *)profile, (long)profile_size);
	if (mng_png(&png, dib, NULL)) {
		while (pos + 12 <= png.size && !found) {
			const DWORD length = ((DWORD)png.data[pos] << 24) | ((DWORD)png.data[pos + 1] << 16) | ((DWORD)png.data[pos + 2] << 8) | png.data[pos + 3];
			if (!memcmp(png.data + pos + 4, "iCCP", 4)) {
				buf_add(out, png.data + pos + 8, length);
				found = 1;
			}
			pos += 12 + length;
		}
	}
	buf_free(&png);
	FreeImage_Unload(dib);
	return found;
}

static int same_profile(FIBITMAP *dib, const void *profile, DWORD size) {
	FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
	if (!profile) {
		return icc->data == NULL;
	}
	return icc->data && icc->size == size && !memcmp(icc->data, profile, size);
}

static int same_cicp(FIBITMAP *dib, const BYTE *cicp) {
	FITAG *tag = NULL;
	if (!FreeImage_GetMetadata(FIMD_CUSTOM, dib, "CICP", &tag) || !tag) {
		return cicp == NULL;
	}
	return cicp && FreeImage_GetTagCount(tag) == 4 && !memcmp(FreeImage_GetTagValue(tag), cicp, 4);
}

/* the canvas carries the profile and CICP tag of the page's own image: global, local, grey left out */
static void test_color_description(void) {
	const BYTE cicp[4] = { 9, 16, 0, 1 };	/* BT.2020, PQ */
	DWORD adobe_size = 0, p3_size = 0, grey_size = 0;
	const void *adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
	const void *p3 = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_DISPLAY_P3, &p3_size);
	const void *grey = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_GRAY, &grey_size);
	const void *want_profile[4];
	DWORD want_size[4];
	const BYTE *want_cicp[4] = { NULL, NULL, cicp, NULL };
	FIBITMAP *rgb = FreeImage_Allocate(W, H, 24, 0, 0, 0), *mono = FreeImage_Allocate(W, H, 8, 0, 0, 0);
	Buf mng, iccp;
	const char *path;
	int i, mode, ok_all = 1;

	printf("the color description of the playback canvas\n");

	want_profile[0] = adobe; want_size[0] = adobe_size;	/* the global iCCP */
	want_profile[1] = p3; want_size[1] = p3_size;		/* its own iCCP */
	want_profile[2] = NULL; want_size[2] = 0;			/* its own cICP: the global iCCP is not inherited */
	want_profile[3] = NULL; want_size[3] = 0;			/* a grey profile cannot describe the RGBA canvas */
	for (i = 0; i < 256; i++) {
		FreeImage_GetPalette(mono)[i].rgbRed = FreeImage_GetPalette(mono)[i].rgbGreen = FreeImage_GetPalette(mono)[i].rgbBlue = (BYTE)i;
	}

	buf_init(&mng);
	buf_init(&iccp);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 10, 4, 4, 0, 1);
	if (!iccp_payload(&iccp, adobe, adobe_size)) {
		fail("no iCCP chunk to copy");
		goto out;
	}
	chunk_buf(&mng, "iCCP", &iccp);
	mng_png(&mng, rgb, NULL);
	FreeImage_CreateICCProfile(rgb, (void *)p3, (long)p3_size);
	mng_png(&mng, rgb, NULL);
	FreeImage_DestroyICCProfile(rgb);
	mng_png(&mng, rgb, cicp);
	FreeImage_CreateICCProfile(mono, (void *)grey, (long)grey_size);
	mng_png(&mng, mono, NULL);
	mng_mend(&mng);
	path = write_file("mng_color.mng", &mng);

	for (mode = 0; mode < 2; mode++) {
		const int flags = MNG_PLAYBACK | (mode ? FIF_LOAD_NOPIXELS : 0);
		FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, flags);
		if (!mb || FreeImage_GetPageCount(mb) != 4) {
			fail("%s: %d pages, expected 4", mode ? "header only" : "pixels", mb ? FreeImage_GetPageCount(mb) : -1);
			ok_all = 0;
			if (mb) FreeImage_CloseMultiBitmap(mb, 0);
			continue;
		}
		/* backwards too: each page is described by its own image, not the last one drawn */
		for (i = 3; i >= -3; i--) {
			const int page = (i < 0) ? -i : i;
			FIBITMAP *dib = FreeImage_LockPage(mb, page);
			if (!dib) { fail("page %d could not be locked", page); ok_all = 0; continue; }
			if (!same_profile(dib, want_profile[page], want_size[page])) {
				fail("%s canvas %d: a %u-byte profile", mode ? "header-only" : "the", page, (unsigned)FreeImage_GetICCProfile(dib)->size);
				ok_all = 0;
			}
			if (!same_cicp(dib, want_cicp[page])) {
				fail("%s canvas %d: the CICP tag", mode ? "header-only" : "the", page);
				ok_all = 0;
			}
			FreeImage_UnlockPage(mb, dib, FALSE);
		}
		FreeImage_CloseMultiBitmap(mb, 0);
	}
	if (ok_all) {
		ok("each canvas carries its image's RGB profile, global or its own, and its CICP tag, with pixels or without");
	}
out:
	buf_free(&mng);
	FreeImage_Unload(rgb);
	FreeImage_Unload(mono);
}

/* the image as a PNG of its own, with a chunk after IHDR when type is given */
static FIBITMAP *standalone_png(const Buf *image, const char *type, const Buf *payload) {
	static const BYTE signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	FIMEMORY *mem;
	FIBITMAP *dib = NULL;
	Buf png;
	buf_init(&png);
	buf_add(&png, signature, 8);
	buf_add(&png, image->data, 25);
	if (type) {
		chunk(&png, type, payload->data, (DWORD)payload->size);
	}
	buf_add(&png, image->data + 25, image->size - 25);
	mem = FreeImage_OpenMemory(png.data, (DWORD)png.size);
	if (mem) {
		dib = FreeImage_LoadFromMemory(FIF_PNG, mem, 0);
		FreeImage_CloseMemory(mem);
	}
	buf_free(&png);
	return dib;
}

/* the same profile, CICP tag and pixel (0, 0) */
static int same_description(FIBITMAP *got, FIBITMAP *want) {
	FIICCPROFILE *icc = FreeImage_GetICCProfile(want);
	FITAG *tag = NULL;
	const BYTE *cicp = NULL;
	RGBQUAD a, b;
	if (FreeImage_GetMetadata(FIMD_CUSTOM, want, "CICP", &tag) && tag) {
		cicp = (const BYTE *)FreeImage_GetTagValue(tag);
	}
	return same_profile(got, icc->data, icc->size) && same_cicp(got, cicp) && pixel_at(got, 0, 0, &a) && pixel_at(want, 0, 0, &b)
		&& a.rgbRed == b.rgbRed && a.rgbGreen == b.rgbGreen && a.rgbBlue == b.rgbBlue;
}

/* the top-level colour chunks reach only an image with none of its own, as if they stood in its PNG (4.2.3) */
static void test_inherited_colour_space(void) {
	static const BYTE srgb[1] = { 0 };
	static const BYTE gama[4] = { 0, 1, 0x86, 0xA0 };	/* 1.0 */
	static const BYTE chrm[32] = {
		0, 0, 0x7A, 0x26, 0, 0, 0x80, 0x84, 0, 0, 0xFA, 0x00, 0, 0, 0x80, 0xE8,
		0, 0, 0x75, 0x30, 0, 0, 0xEA, 0x60, 0, 0, 0x3A, 0x98, 0, 0, 0x17, 0x70
	};
	static const BYTE cicp[4] = { 1, 13, 0, 1 };		/* sRGB */
	static const struct { const char *what; const char *type; const BYTE *payload; DWORD length; int p3; } cases[] = {
		{ "no colour chunk", NULL, NULL, 0, 0 },
		{ "its own sRGB chunk", "sRGB", srgb, 1, 0 },
		{ "its own gAMA chunk", "gAMA", gama, 4, 0 },
		{ "its own cHRM chunk", "cHRM", chrm, 32, 0 },
		{ "its own cICP chunk", "cICP", cicp, 4, 0 },
		{ "its own iCCP chunk", NULL, NULL, 0, 1 }
	};
	enum { COUNT = sizeof(cases) / sizeof(cases[0]) };
	DWORD adobe_size = 0, p3_size = 0;
	const void *adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
	const void *p3 = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_DISPLAY_P3, &p3_size);
	FIBITMAP *rgb = FreeImage_Allocate(W, H, 24, 0, 0, 0);
	FIMULTIBITMAP *mb = NULL;
	Buf mng, iccp, images[COUNT];
	RGBQUAD colour;
	const char *path;
	int i, bad = 0;

	printf("colour chunks inherited from the top level\n");

	colour.rgbRed = 128;
	colour.rgbGreen = 64;
	colour.rgbBlue = 200;
	colour.rgbReserved = 0;

	FreeImage_FillBackground(rgb, &colour, 0);
	buf_init(&mng);
	buf_init(&iccp);
	for (i = 0; i < COUNT; i++) {
		buf_init(&images[i]);
	}
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 10, COUNT, COUNT, 0, 1);
	if (!iccp_payload(&iccp, adobe, adobe_size)) {
		fail("no iCCP chunk to copy");
		goto out;
	}
	chunk(&mng, "iCCP", iccp.data, (DWORD)iccp.size);
	for (i = 0; i < COUNT; i++) {
		if (cases[i].p3) {
			FreeImage_CreateICCProfile(rgb, (void *)p3, (long)p3_size);
		}
		mng_png_with(&images[i], rgb, cases[i].type, cases[i].payload, cases[i].length);
		FreeImage_DestroyICCProfile(rgb);
		buf_add(&mng, images[i].data, images[i].size);
	}
	mng_mend(&mng);
	path = write_file("mng_inherit.mng", &mng);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb || FreeImage_GetPageCount(mb) != COUNT) {
		fail("%d pages, expected %d", mb ? FreeImage_GetPageCount(mb) : -1, (int)COUNT);
		goto out;
	}
	for (i = 0; i < COUNT; i++) {
		const int inherits = !cases[i].type && !cases[i].p3;
		FIBITMAP *want = standalone_png(&images[i], inherits ? "iCCP" : NULL, &iccp);
		FIBITMAP *got = FreeImage_LockPage(mb, i);
		RGBQUAD pixel;
		if (!want || !got) {
			fail("an image with %s could not be decoded", cases[i].what);
			bad = 1;
		} else if (inherits != same_profile(want, adobe, adobe_size)) {
			fail("premise: the PNG of the image with %s %s the Adobe RGB profile", cases[i].what, inherits ? "lacks" : "has");
			bad = 1;
		} else if (cases[i].payload == gama && (!pixel_at(want, 0, 0, &pixel) || pixel.rgbRed == 128)) {
			fail("premise: a gAMA of 1.0 leaves the pixels as they are");
			bad = 1;
		} else if (!same_description(got, want)) {
			fail("an image with %s: a %u-byte profile, or other pixels than its own PNG with%s the global iCCP",
				 cases[i].what, (unsigned)FreeImage_GetICCProfile(got)->size, inherits ? "" : "out");
			bad = 1;
		}
		if (want) FreeImage_Unload(want);
		if (got) FreeImage_UnlockPage(mb, got, FALSE);
	}
	if (!bad) {
		ok("the global iCCP reaches an image without colour chunks, not one with an sRGB, gAMA, cHRM, cICP or iCCP chunk");
	}
out:
	if (mb) FreeImage_CloseMultiBitmap(mb, 0);
	for (i = 0; i < COUNT; i++) {
		buf_free(&images[i]);
	}
	buf_free(&iccp);
	buf_free(&mng);
	FreeImage_Unload(rgb);
}

/* a format's bytes for this image */
static int save_bytes(FREE_IMAGE_FORMAT fif, FIBITMAP *dib, int flags, Buf *out) {
	FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
	BYTE *data = NULL;
	DWORD size = 0;
	int ok = mem && FreeImage_SaveToMemory(fif, dib, mem, flags) && FreeImage_AcquireMemory(mem, &data, &size);
	if (ok) {
		buf_add(out, data, size);
	}
	if (mem) {
		FreeImage_CloseMemory(mem);
	}
	return ok;
}

/* every chunk of this type, whole, from a PNG-style stream after its signature */
static void copy_chunks(Buf *out, const Buf *stream, const char *type) {
	size_t pos = 8;
	while (pos + 12 <= stream->size) {
		const DWORD length = ((DWORD)stream->data[pos] << 24) | ((DWORD)stream->data[pos + 1] << 16) | ((DWORD)stream->data[pos + 2] << 8) | stream->data[pos + 3];
		if (!memcmp(stream->data + pos + 4, type, 4)) {
			buf_add(out, stream->data + pos, 12 + length);
		}
		pos += 12 + length;
	}
}

/* a JNG as other writers make it: the JPEG data keeps its APP2 profile, iCCP and sRGB chunks optional */
static int third_party_jng(Buf *out, int alpha, const Buf *iccp, int srgb, const void *app2, DWORD app2_size) {
	FIBITMAP *img = FreeImage_Allocate(W, H, alpha ? 32 : 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK), *rgb;
	Buf jng, jpeg;
	unsigned i;
	int ok;
	for (i = 0; i < FreeImage_GetPitch(img) * H; i++) {
		FreeImage_GetBits(img)[i] = (BYTE)(i * 7 + 3);
	}
	rgb = FreeImage_ConvertTo24Bits(img);
	FreeImage_CreateICCProfile(rgb, (void *)app2, (long)app2_size);
	buf_init(&jng);
	buf_init(&jpeg);
	ok = save_bytes(FIF_JNG, img, 0, &jng) && save_bytes(FIF_JPEG, rgb, JPEG_QUALITYGOOD, &jpeg);
	if (ok) {
		static const BYTE signature[8] = { 139, 74, 78, 71, 13, 10, 26, 10 };
		const BYTE intent = 0;
		buf_add(out, signature, 8);
		copy_chunks(out, &jng, "JHDR");
		if (iccp) {
			chunk(out, "iCCP", iccp->data, (DWORD)iccp->size);
		}
		if (srgb) {
			chunk(out, "sRGB", &intent, 1);
		}
		chunk(out, "JDAT", jpeg.data, (DWORD)jpeg.size);
		copy_chunks(out, &jng, "IDAT");
		chunk(out, "IEND", NULL, 0);
	}
	buf_free(&jng);
	buf_free(&jpeg);
	FreeImage_Unload(img);
	FreeImage_Unload(rgb);
	return ok;
}

static FIBITMAP *load_buf(FREE_IMAGE_FORMAT fif, const Buf *b, int flags) {
	FIMEMORY *mem = FreeImage_OpenMemory(b->data, (DWORD)b->size);
	FIBITMAP *dib = mem ? FreeImage_LoadFromMemory(fif, mem, flags) : NULL;
	if (mem) {
		FreeImage_CloseMemory(mem);
	}
	return dib;
}

/* a JNG's iCCP describes its JPEG data, ahead of the JPEG's own APP2 profile; an MNG's global iCCP reaches a JNG frame */
static void test_jng_profiles(void) {
	DWORD adobe_size = 0, pro_size = 0, grey_size = 0;
	const void *adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
	const void *pro = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_PROPHOTO_RGB, &pro_size);
	const void *grey = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_GRAY, &grey_size);
	static const struct { const char *what; int alpha; int iccp; int flags; int want; } cases[] = {
		{ "APP2 only", 0, 0, 0, 1 },
		{ "APP2 only, with alpha", 1, 0, 0, 1 },
		{ "iCCP and APP2, with alpha", 1, 1, 0, 2 },
		{ "iCCP and APP2, header only", 1, 1, FIF_LOAD_NOPIXELS, 2 },
		{ "a grey iCCP in a colour JNG, and APP2", 0, 2, 0, 1 },
	};
	Buf iccp_adobe, iccp_grey;
	unsigned i;
	int bad = 0;

	printf("JNG profiles\n");

	buf_init(&iccp_adobe);
	buf_init(&iccp_grey);
	if (!iccp_payload(&iccp_adobe, adobe, adobe_size)) {
		fail("no iCCP chunk to copy");
		return;
	}
	/* a grey profile's payload, from a grey PNG */
	{
		FIBITMAP *g = FreeImage_Allocate(1, 1, 8, 0, 0, 0);
		Buf png;
		size_t pos = 8;
		for (i = 0; i < 256; i++) {
			FreeImage_GetPalette(g)[i].rgbRed = FreeImage_GetPalette(g)[i].rgbGreen = FreeImage_GetPalette(g)[i].rgbBlue = (BYTE)i;
		}
		FreeImage_CreateICCProfile(g, (void *)grey, (long)grey_size);
		buf_init(&png);
		save_bytes(FIF_PNG, g, 0, &png);
		while (pos + 12 <= png.size) {
			const DWORD length = ((DWORD)png.data[pos] << 24) | ((DWORD)png.data[pos + 1] << 16) | ((DWORD)png.data[pos + 2] << 8) | png.data[pos + 3];
			if (!memcmp(png.data + pos + 4, "iCCP", 4)) {
				buf_add(&iccp_grey, png.data + pos + 8, length);
			}
			pos += 12 + length;
		}
		buf_free(&png);
		FreeImage_Unload(g);
	}

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		Buf jng;
		FIBITMAP *dib;
		const void *want = (cases[i].want == 2) ? adobe : pro;
		const DWORD want_size = (cases[i].want == 2) ? adobe_size : pro_size;
		buf_init(&jng);
		if (!third_party_jng(&jng, cases[i].alpha, (cases[i].iccp == 1) ? &iccp_adobe : (cases[i].iccp == 2) ? &iccp_grey : NULL, 0, pro, pro_size)) {
			fail("%s: no JNG made", cases[i].what);
			bad = 1;
			buf_free(&jng);
			continue;
		}
		dib = load_buf(FIF_JNG, &jng, cases[i].flags);
		if (!dib || !same_profile(dib, want, want_size)) {
			fail("%s: a %u-byte profile, want %s", cases[i].what, dib ? (unsigned)FreeImage_GetICCProfile(dib)->size : 0, (cases[i].want == 2) ? "Adobe RGB" : "ProPhoto");
			bad = 1;
		} else if (!(cases[i].flags & FIF_LOAD_NOPIXELS) && (FreeImage_GetBPP(dib) != (cases[i].alpha ? 32u : 24u))) {
			fail("%s: %u bits per pixel", cases[i].what, FreeImage_GetBPP(dib));
			bad = 1;
		}
		if (dib) {
			FreeImage_Unload(dib);
		}
		buf_free(&jng);
	}
	if (!bad) {
		ok("a JNG's iCCP wins over the JPEG's APP2 profile, which stands otherwise, alpha and header-only included");
	}

	/* an MNG's global iCCP reaches a JNG frame that defines no colour space of its own */
	bad = 0;
	{
		int srgb;
		for (srgb = 0; srgb < 2; srgb++) {
			Buf mng, jng;
			const char *path;
			FIMULTIBITMAP *mb;
			int mode;
			buf_init(&mng);
			buf_init(&jng);
			third_party_jng(&jng, 0, NULL, srgb, NULL, 0);
			mng_signature(&mng);
			mng_mhdr(&mng, W, H, 10, 1, 1, 0, 1 | (1 << 4));
			chunk(&mng, "iCCP", iccp_adobe.data, (DWORD)iccp_adobe.size);
			buf_add(&mng, jng.data + 8, jng.size - 8);
			mng_mend(&mng);
			path = write_file("mng_jng_iccp.mng", &mng);
			for (mode = 0; mode < 2; mode++) {
				mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, mode ? MNG_PLAYBACK : 0);
				if (!mb || FreeImage_GetPageCount(mb) != 1) {
					fail("JNG frame%s: %d pages", srgb ? " with sRGB" : "", mb ? FreeImage_GetPageCount(mb) : -1);
					bad = 1;
				} else {
					FIBITMAP *dib = FreeImage_LockPage(mb, 0);
					if (!dib || !same_profile(dib, srgb ? NULL : adobe, srgb ? 0 : adobe_size)) {
						fail("JNG frame%s, %s: a %u-byte profile, want %s", srgb ? " with sRGB" : "", mode ? "playback" : "page",
							dib ? (unsigned)FreeImage_GetICCProfile(dib)->size : 0, srgb ? "none" : "the global Adobe RGB");
						bad = 1;
					}
					if (dib) {
						FreeImage_UnlockPage(mb, dib, FALSE);
					}
				}
				if (mb) {
					FreeImage_CloseMultiBitmap(mb, 0);
				}
			}
			buf_free(&mng);
			buf_free(&jng);
		}
	}
	if (!bad) {
		ok("a JNG frame takes the global iCCP, page and canvas, unless it has an sRGB chunk");
	}
	buf_free(&iccp_adobe);
	buf_free(&iccp_grey);
}

int main(void) {
	FreeImage_Initialise(FALSE);
	FreeImage_SetOutputMessage(quiet);

	test_vlc();
	test_framing_modes();
	test_infinite_tick();
	test_loop_counts();
	test_zero_loop();
	test_placement_and_playback();
	test_defi_omitted_fields();
	test_show();
	test_header_only();
	test_single_load();
	test_alpha_compositing();
	test_absurd_canvas();
	test_color_description();
	test_inherited_colour_space();
	test_jng_profiles();

	FreeImage_DeInitialise();

	printf("\n%d check(s), %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
