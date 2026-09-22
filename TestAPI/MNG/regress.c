/* MNG writer and page-editing round trips */

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

/* ---- test bitmaps ---- */

/* position-dependent pixels; distinctive palette */
static FIBITMAP *
pattern(int width, int height, FREE_IMAGE_TYPE type, int bpp, int seed) {
	FIBITMAP *dib = (type == FIT_BITMAP)
		? FreeImage_Allocate(width, height, bpp, 0, 0, 0)
		: FreeImage_AllocateT(type, width, height, bpp, 0, 0, 0);
	unsigned x, y;

	if (!dib) {
		return NULL;
	}

	if ((type == FIT_BITMAP) && (bpp <= 8)) {
		RGBQUAD *palette = FreeImage_GetPalette(dib);
		const unsigned entries = 1u << bpp;

		for (x = 0; x < entries; x++) {
			palette[x].rgbRed   = (BYTE)(x * 7 + seed + 1);
			palette[x].rgbGreen = (BYTE)(x * 13 + seed + 2);
			palette[x].rgbBlue  = (BYTE)(x * 29 + seed + 3);
		}
		for (y = 0; y < FreeImage_GetHeight(dib); y++) {
			for (x = 0; x < FreeImage_GetWidth(dib); x++) {
				BYTE index = (BYTE)((x + y + seed) % entries);
				FreeImage_SetPixelIndex(dib, x, y, &index);
			}
		}
	} else {
		const unsigned line = FreeImage_GetLine(dib);
		for (y = 0; y < FreeImage_GetHeight(dib); y++) {
			BYTE *scanline = FreeImage_GetScanLine(dib, y);
			unsigned b;
			for (b = 0; b < line; b++) {
				scanline[b] = (BYTE)((y * 31 + b * 17 + seed * 5 + 7) & 0xFF);
			}
		}
	}
	return dib;
}

static FIBITMAP *
solid(int width, int height, BYTE red, BYTE green, BYTE blue) {
	FIBITMAP *dib = FreeImage_Allocate(width, height, 24, 0, 0, 0);
	unsigned x, y;

	if (!dib) {
		return NULL;
	}
	for (y = 0; y < FreeImage_GetHeight(dib); y++) {
		BYTE *scanline = FreeImage_GetScanLine(dib, y);
		for (x = 0; x < FreeImage_GetWidth(dib); x++) {
			scanline[FI_RGBA_RED] = red;
			scanline[FI_RGBA_GREEN] = green;
			scanline[FI_RGBA_BLUE] = blue;
			scanline += 3;
		}
	}
	return dib;
}

/* ignores row padding; compares palette images by colour */
static int
same_picture(FIBITMAP *a, FIBITMAP *b) {
	unsigned x, y, bpp;

	if (!a || !b) {
		return 0;
	}
	if (FreeImage_GetWidth(a) != FreeImage_GetWidth(b)
		|| FreeImage_GetHeight(a) != FreeImage_GetHeight(b)
		|| FreeImage_GetBPP(a) != FreeImage_GetBPP(b)
		|| FreeImage_GetImageType(a) != FreeImage_GetImageType(b)) {
		return 0;
	}

	bpp = FreeImage_GetBPP(a);

	if ((FreeImage_GetImageType(a) == FIT_BITMAP) && (bpp <= 8)) {
		RGBQUAD *pa = FreeImage_GetPalette(a);
		RGBQUAD *pb = FreeImage_GetPalette(b);

		if (!pa || !pb) {
			return 0;
		}
		for (y = 0; y < FreeImage_GetHeight(a); y++) {
			for (x = 0; x < FreeImage_GetWidth(a); x++) {
				BYTE ia = 0, ib = 0;
				FreeImage_GetPixelIndex(a, x, y, &ia);
				FreeImage_GetPixelIndex(b, x, y, &ib);
				if (memcmp(&pa[ia], &pb[ib], 3) != 0) {
					return 0;
				}
			}
		}
		return 1;
	}

	{
		const unsigned bytes = FreeImage_GetWidth(a) * (bpp / 8);
		for (y = 0; y < FreeImage_GetHeight(a); y++) {
			if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), bytes) != 0) {
				return 0;
			}
		}
	}
	return 1;
}

/* ---- animation metadata ---- */

#define ANIM_LOGICALWIDTH	0x0001
#define ANIM_LOGICALHEIGHT	0x0002
#define ANIM_LOOP			0x0004
#define ANIM_FRAMELEFT		0x1001
#define ANIM_FRAMETOP		0x1002
#define ANIM_FRAMETIME		0x1005
#define ANIM_DISPOSALMETHOD	0x1006

#define GIF_DISPOSAL_LEAVE		1
#define GIF_DISPOSAL_BACKGROUND	2
#define GIF_DISPOSAL_PREVIOUS	3

static void
set_tag(FIBITMAP *dib, const char *key, WORD id, FREE_IMAGE_MDTYPE type,
		DWORD length, const void *value) {
	FITAG *tag = FreeImage_CreateTag();
	if (!tag) {
		return;
	}
	FreeImage_SetTagKey(tag, key);
	FreeImage_SetTagID(tag, id);
	FreeImage_SetTagType(tag, type);
	FreeImage_SetTagCount(tag, 1);
	FreeImage_SetTagLength(tag, length);
	FreeImage_SetTagValue(tag, value);
	FreeImage_SetMetadata(FIMD_ANIMATION, dib, key, tag);
	FreeImage_DeleteTag(tag);
}

static void set_delay(FIBITMAP *dib, LONG ms) {
	set_tag(dib, "FrameTime", ANIM_FRAMETIME, FIDT_LONG, 4, &ms);
}
static void set_place(FIBITMAP *dib, WORD left, WORD top) {
	set_tag(dib, "FrameLeft", ANIM_FRAMELEFT, FIDT_SHORT, 2, &left);
	set_tag(dib, "FrameTop", ANIM_FRAMETOP, FIDT_SHORT, 2, &top);
}
static void set_canvas(FIBITMAP *dib, WORD width, WORD height) {
	set_tag(dib, "LogicalWidth", ANIM_LOGICALWIDTH, FIDT_SHORT, 2, &width);
	set_tag(dib, "LogicalHeight", ANIM_LOGICALHEIGHT, FIDT_SHORT, 2, &height);
}
static void set_loop(FIBITMAP *dib, LONG loop) {
	set_tag(dib, "Loop", ANIM_LOOP, FIDT_LONG, 4, &loop);
}
static void set_disposal(FIBITMAP *dib, BYTE disposal) {
	set_tag(dib, "DisposalMethod", ANIM_DISPOSALMETHOD, FIDT_BYTE, 1, &disposal);
}

static long
anim_tag(FIBITMAP *dib, const char *key, long missing) {
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

/* ---- the tests ---- */

/* must come back as a MNG, not a bare PNG, and unchanged */
static void test_single_image(void) {
	static const struct {
		const char *name;
		FREE_IMAGE_TYPE type;
		int bpp;
		int width;
	} cases[] = {
		{ "1-bit",  FIT_BITMAP, 1,  13 },   /* a width that ends mid-byte */
		{ "1-bit",  FIT_BITMAP, 1,  16 },
		{ "4-bit",  FIT_BITMAP, 4,  13 },
		{ "8-bit",  FIT_BITMAP, 8,  13 },
		{ "24-bit", FIT_BITMAP, 24, 13 },
		{ "32-bit", FIT_BITMAP, 32, 13 },
		{ "UINT16", FIT_UINT16, 16, 13 },
		{ "RGB16",  FIT_RGB16,  48, 13 },
		{ "RGBA16", FIT_RGBA16, 64, 13 },
	};
	size_t i;

	printf("a single image, saved and read back\n");

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		const char *path = scratch("mng_one.mng");
		FIBITMAP *src, *back;

		if (!FreeImage_FIFSupportsExportType(FIF_MNG, cases[i].type)
			|| ((cases[i].type == FIT_BITMAP)
				&& !FreeImage_FIFSupportsExportBPP(FIF_MNG, cases[i].bpp))) {
			fail("%s/%d: MNG says it cannot export this, but PNG can",
				 cases[i].name, cases[i].width);
			continue;
		}

		src = pattern(cases[i].width, 7, cases[i].type, cases[i].bpp, (int)i);
		if (!src) {
			fail("%s: could not be allocated", cases[i].name);
			continue;
		}

		if (!FreeImage_Save(FIF_MNG, src, path, 0)) {
			fail("%s/%d: FreeImage_Save returned FALSE", cases[i].name, cases[i].width);
			FreeImage_Unload(src);
			continue;
		}
		if (FreeImage_GetFileType(path, 0) != FIF_MNG) {
			fail("%s/%d: what was written is not recognised as a MNG",
				 cases[i].name, cases[i].width);
			FreeImage_Unload(src);
			continue;
		}

		back = FreeImage_Load(FIF_MNG, path, 0);
		if (!back) {
			fail("%s/%d: it cannot be read back", cases[i].name, cases[i].width);
		} else if (!same_picture(src, back)) {
			fail("%s/%d: %ux%u %u bpp went in, %ux%u %u bpp came back, and they differ",
				 cases[i].name, cases[i].width,
				 FreeImage_GetWidth(src), FreeImage_GetHeight(src), FreeImage_GetBPP(src),
				 FreeImage_GetWidth(back), FreeImage_GetHeight(back), FreeImage_GetBPP(back));
		} else {
			ok("%s, %d wide: identical after a round trip", cases[i].name, cases[i].width);
		}

		if (back) {
			FreeImage_Unload(back);
		}
		FreeImage_Unload(src);
	}
}

static void test_animation_round_trip(void) {
	static const BYTE COLOUR[4][3] = {
		{ 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 }
	};
	const char *path = scratch("mng_anim.mng");
	FIMULTIBITMAP *mb;
	int i, bad = 0;

	printf("an animation, written a page at a time\n");

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, TRUE, FALSE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap with create_new returned NULL");
		return;
	}
	for (i = 0; i < 4; i++) {
		FIBITMAP *dib = solid(16, 16, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2]);
		set_delay(dib, 120 + i * 10);
		set_place(dib, (WORD)(i * 4), (WORD)(i * 2));
		set_canvas(dib, 64, 48);
		set_loop(dib, 5);
		FreeImage_AppendPage(mb, dib);
		FreeImage_Unload(dib);
	}
	if (!FreeImage_CloseMultiBitmap(mb, 0)) {
		fail("FreeImage_CloseMultiBitmap reported failure");
		return;
	}

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("the file that was written cannot be opened");
		return;
	}
	if (FreeImage_GetPageCount(mb) != 4) {
		fail("%d pages came back, expected 4", FreeImage_GetPageCount(mb));
		FreeImage_CloseMultiBitmap(mb, 0);
		return;
	}
	for (i = 0; i < 4; i++) {
		FIBITMAP *dib = FreeImage_LockPage(mb, i);
		RGBQUAD colour;

		if (!dib) {
			fail("page %d could not be locked", i);
			bad = 1;
			continue;
		}
		memset(&colour, 0, sizeof(colour));
		FreeImage_GetPixelColor(dib, 0, 0, &colour);

		if (colour.rgbRed != COLOUR[i][0] || colour.rgbGreen != COLOUR[i][1]
			|| colour.rgbBlue != COLOUR[i][2]) {
			fail("page %d is (%u,%u,%u), expected (%u,%u,%u)", i,
				 colour.rgbRed, colour.rgbGreen, colour.rgbBlue,
				 COLOUR[i][0], COLOUR[i][1], COLOUR[i][2]);
			bad = 1;
		}
		if (anim_tag(dib, "FrameTime", -1) != 120 + i * 10) {
			fail("page %d is %ld ms, expected %d - the tick is a millisecond, so "
				 "nothing should have been rounded",
				 i, anim_tag(dib, "FrameTime", -1), 120 + i * 10);
			bad = 1;
		}
		if (anim_tag(dib, "FrameLeft", -1) != i * 4
			|| anim_tag(dib, "FrameTop", -1) != i * 2) {
			fail("page %d is at (%ld,%ld), expected (%d,%d)", i,
				 anim_tag(dib, "FrameLeft", -1), anim_tag(dib, "FrameTop", -1),
				 i * 4, i * 2);
			bad = 1;
		}
		if (anim_tag(dib, "Loop", -1) != 5) {
			fail("page %d says it loops %ld times, expected 5",
				 i, anim_tag(dib, "Loop", -1));
			bad = 1;
		}
		if (anim_tag(dib, "LogicalWidth", -1) != 64
			|| anim_tag(dib, "LogicalHeight", -1) != 48) {
			fail("page %d has a %ldx%ld canvas, expected 64x48", i,
				 anim_tag(dib, "LogicalWidth", -1), anim_tag(dib, "LogicalHeight", -1));
			bad = 1;
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	if (!bad) {
		ok("four pages, with their delays, placement, loop count and canvas intact");
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

static void test_untagged_default(void) {
	const char *path = scratch("mng_untagged.mng");
	FIMULTIBITMAP *mb;
	FIBITMAP *dib;
	int i, bad = 0;

	printf("a page with no timing of its own\n");

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, TRUE, FALSE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap with create_new returned NULL");
		return;
	}
	for (i = 0; i < 2; i++) {
		dib = solid(8, 8, (BYTE)(i * 100), 0, 0);
		FreeImage_AppendPage(mb, dib);
		FreeImage_Unload(dib);
	}
	FreeImage_CloseMultiBitmap(mb, 0);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("the file cannot be read back");
		return;
	}
	for (i = 0; i < FreeImage_GetPageCount(mb); i++) {
		dib = FreeImage_LockPage(mb, i);
		if (!dib) {
			bad = 1;
			continue;
		}
		if (anim_tag(dib, "FrameTime", -1) != 100) {
			fail("an untagged page came back at %ld ms, expected the 100 ms default",
				 anim_tag(dib, "FrameTime", -1));
			bad = 1;
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	if (!bad) {
		ok("an untagged page is written with the 100 ms default");
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

static void test_editing(void) {
	static const BYTE COLOUR[4][3] = {
		{ 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 255, 255, 0 }
	};
	const char *path = scratch("mng_edit.mng");
	FIMULTIBITMAP *mb;
	FIBITMAP *dib;
	int i, bad = 0;
	/* delete 1, insert cyan at 1, move 0 to the end */
	static const BYTE EXPECTED[4][3] = {
		{ 0, 255, 255 }, { 0, 0, 255 }, { 255, 255, 0 }, { 255, 0, 0 }
	};

	printf("deleting, inserting and moving pages\n");

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, TRUE, FALSE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap with create_new returned NULL");
		return;
	}
	for (i = 0; i < 4; i++) {
		dib = solid(16, 16, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2]);
		set_delay(dib, 200 + i);
		FreeImage_AppendPage(mb, dib);
		FreeImage_Unload(dib);
	}
	FreeImage_CloseMultiBitmap(mb, 0);

	/* no MNG_PLAYBACK: edit stored frames, not canvases */
	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, FALSE, FALSE, 0);
	if (!mb) {
		fail("the file cannot be opened for editing");
		return;
	}
	if (FreeImage_GetPageCount(mb) != 4) {
		fail("%d pages to edit, expected 4", FreeImage_GetPageCount(mb));
		FreeImage_CloseMultiBitmap(mb, 0);
		return;
	}

	FreeImage_DeletePage(mb, 1);

	dib = solid(16, 16, 0, 255, 255);
	set_delay(dib, 250);
	FreeImage_InsertPage(mb, 0, dib);
	FreeImage_Unload(dib);

	if (FreeImage_GetPageCount(mb) != 4) {
		fail("%d pages after a delete and an insert, expected 4",
			 FreeImage_GetPageCount(mb));
		bad = 1;
	}

	/* move red to the end; MovePage takes (target, source) */
	FreeImage_MovePage(mb, 3, 1);

	if (!FreeImage_CloseMultiBitmap(mb, 0)) {
		fail("FreeImage_CloseMultiBitmap reported failure");
		return;
	}

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("the edited file cannot be opened");
		return;
	}
	if (FreeImage_GetPageCount(mb) != 4) {
		fail("the edited file has %d pages, expected 4", FreeImage_GetPageCount(mb));
		FreeImage_CloseMultiBitmap(mb, 0);
		return;
	}
	for (i = 0; i < 4; i++) {
		RGBQUAD colour;
		dib = FreeImage_LockPage(mb, i);
		if (!dib) {
			fail("page %d could not be locked", i);
			bad = 1;
			continue;
		}
		memset(&colour, 0, sizeof(colour));
		FreeImage_GetPixelColor(dib, 0, 0, &colour);
		if (colour.rgbRed != EXPECTED[i][0] || colour.rgbGreen != EXPECTED[i][1]
			|| colour.rgbBlue != EXPECTED[i][2]) {
			fail("page %d is (%u,%u,%u), expected (%u,%u,%u)", i,
				 colour.rgbRed, colour.rgbGreen, colour.rgbBlue,
				 EXPECTED[i][0], EXPECTED[i][1], EXPECTED[i][2]);
			bad = 1;
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	if (!bad) {
		ok("delete, insert and move leave the pages in the order asked for");
	}

	/* timing must follow the page, not its position */
	dib = FreeImage_LockPage(mb, 3);
	if (dib) {
		if (anim_tag(dib, "FrameTime", -1) != 200) {
			fail("the page moved to the end is %ld ms, expected the 200 it was given",
				 anim_tag(dib, "FrameTime", -1));
		} else {
			ok("a page that was moved kept its own delay");
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

static void test_disposal(void) {
	const char *path = scratch("mng_disposal.mng");
	FIMULTIBITMAP *mb;
	FIBITMAP *dib;
	int i, bad = 0;
	static const BYTE WANT[3] = {
		GIF_DISPOSAL_BACKGROUND, GIF_DISPOSAL_LEAVE, GIF_DISPOSAL_LEAVE
	};

	printf("disposal methods\n");

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, TRUE, FALSE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap with create_new returned NULL");
		return;
	}
	for (i = 0; i < 3; i++) {
		dib = solid(16, 16, (BYTE)(60 * i), 0, 0);
		set_delay(dib, 100);
		set_disposal(dib, WANT[i]);
		FreeImage_AppendPage(mb, dib);
		FreeImage_Unload(dib);
	}
	FreeImage_CloseMultiBitmap(mb, 0);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("the file cannot be read back");
		return;
	}
	/* the last frame's disposal is not stored */
	for (i = 0; i < 2 && i < FreeImage_GetPageCount(mb); i++) {
		dib = FreeImage_LockPage(mb, i);
		if (!dib) {
			bad = 1;
			continue;
		}
		if (anim_tag(dib, "DisposalMethod", -1) != WANT[i]) {
			fail("page %d disposes %ld, expected %u",
				 i, anim_tag(dib, "DisposalMethod", -1), WANT[i]);
			bad = 1;
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	if (!bad) {
		ok("a frame that disposes to the background says so after a round trip");
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

static void test_loop_counts(void) {
	static const LONG LOOPS[] = { 1, 5, 0 };   /* once, five times, forever */
	size_t c;

	printf("loop counts\n");

	for (c = 0; c < sizeof(LOOPS) / sizeof(LOOPS[0]); c++) {
		const char *path = scratch("mng_loop.mng");
		FIMULTIBITMAP *mb;
		FIBITMAP *dib;
		int i;

		mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, TRUE, FALSE, FALSE, 0);
		if (!mb) {
			fail("loop %ld: create_new returned NULL", (long)LOOPS[c]);
			continue;
		}
		for (i = 0; i < 2; i++) {
			dib = solid(8, 8, (BYTE)(i * 90), 0, 0);
			set_delay(dib, 100);
			set_loop(dib, LOOPS[c]);
			FreeImage_AppendPage(mb, dib);
			FreeImage_Unload(dib);
		}
		FreeImage_CloseMultiBitmap(mb, 0);

		mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
		if (!mb) {
			fail("loop %ld: the file cannot be read back", (long)LOOPS[c]);
			continue;
		}
		dib = FreeImage_LockPage(mb, 0);
		if (!dib) {
			fail("loop %ld: page 0 could not be locked", (long)LOOPS[c]);
		} else {
			if (anim_tag(dib, "Loop", -1) != LOOPS[c]) {
				fail("loop %ld came back as %ld", (long)LOOPS[c],
					 anim_tag(dib, "Loop", -1));
			} else {
				ok("a loop count of %ld survives", (long)LOOPS[c]);
			}
			FreeImage_UnlockPage(mb, dib, FALSE);
		}
		FreeImage_CloseMultiBitmap(mb, 0);
	}
}

static void test_canvas_grows(void) {
	const char *path = scratch("mng_canvas.mng");
	FIMULTIBITMAP *mb;
	FIBITMAP *dib;

	printf("a canvas that has to hold every frame\n");

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, TRUE, FALSE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap with create_new returned NULL");
		return;
	}

	dib = solid(16, 16, 255, 0, 0);
	set_delay(dib, 100);
	set_canvas(dib, 20, 20);          /* too small for the frame placed below */
	FreeImage_AppendPage(mb, dib);
	FreeImage_Unload(dib);

	dib = solid(32, 24, 0, 255, 0);
	set_delay(dib, 100);
	set_place(dib, 40, 30);           /* reaches 72 x 54 */
	FreeImage_AppendPage(mb, dib);
	FreeImage_Unload(dib);

	FreeImage_CloseMultiBitmap(mb, 0);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("the file cannot be read back");
		return;
	}
	dib = FreeImage_LockPage(mb, 0);
	if (!dib) {
		fail("page 0 could not be locked");
	} else {
		const long width = anim_tag(dib, "LogicalWidth", -1);
		const long height = anim_tag(dib, "LogicalHeight", -1);
		if (width < 72 || height < 54) {
			fail("the canvas is %ldx%ld, too small for a 32x24 frame at (40,30)",
				 width, height);
		} else {
			ok("the canvas grew to %ldx%ld to hold every frame", width, height);
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);
}

static void test_background(void) {
	const char *path = scratch("mng_back.mng");
	FIBITMAP *dib, *back;
	RGBQUAD colour, got;

	printf("a background colour\n");

	dib = solid(16, 16, 10, 20, 30);
	colour.rgbRed = 0x21; colour.rgbGreen = 0x43; colour.rgbBlue = 0x65;
	colour.rgbReserved = 255;
	FreeImage_SetBackgroundColor(dib, &colour);

	if (!FreeImage_Save(FIF_MNG, dib, path, 0)) {
		fail("FreeImage_Save returned FALSE");
		FreeImage_Unload(dib);
		return;
	}
	FreeImage_Unload(dib);

	back = FreeImage_Load(FIF_MNG, path, 0);
	if (!back) {
		fail("the file cannot be read back");
		return;
	}
	memset(&got, 0, sizeof(got));
	if (!FreeImage_GetBackgroundColor(back, &got)) {
		fail("the background colour did not survive");
	} else if (got.rgbRed != 0x21 || got.rgbGreen != 0x43 || got.rgbBlue != 0x65) {
		fail("the background came back as (%u,%u,%u), expected (33,67,101)",
			 got.rgbRed, got.rgbGreen, got.rgbBlue);
	} else {
		ok("a background colour survives the round trip");
	}
	FreeImage_Unload(back);
}

static void test_memory_stream(void) {
	FIBITMAP *src, *back;
	FIMEMORY *hmem;

	printf("the memory stream\n");

	src = pattern(9, 5, FIT_BITMAP, 24, 3);
	if (!src) {
		fail("the bitmap could not be allocated");
		return;
	}

	hmem = FreeImage_OpenMemory(NULL, 0);
	if (!hmem) {
		fail("FreeImage_OpenMemory returned NULL");
		FreeImage_Unload(src);
		return;
	}
	if (!FreeImage_SaveToMemory(FIF_MNG, src, hmem, 0)) {
		fail("FreeImage_SaveToMemory returned FALSE");
	} else {
		FreeImage_SeekMemory(hmem, 0, SEEK_SET);
		if (FreeImage_GetFileTypeFromMemory(hmem, 0) != FIF_MNG) {
			fail("what went into the stream is not recognised as a MNG");
		}
		FreeImage_SeekMemory(hmem, 0, SEEK_SET);
		back = FreeImage_LoadFromMemory(FIF_MNG, hmem, 0);
		if (!back) {
			fail("it cannot be read back out of the stream");
		} else {
			if (!same_picture(src, back)) {
				fail("what came back out of the stream differs");
			} else {
				ok("a MNG written to a memory stream reads back identically");
			}
			FreeImage_Unload(back);
		}
	}
	FreeImage_CloseMemory(hmem);
	FreeImage_Unload(src);
}

static void test_save_flags(void) {
	static const struct { const char *name; int flags; } cases[] = {
		{ "PNG_Z_BEST_SPEED", PNG_Z_BEST_SPEED },
		{ "PNG_Z_BEST_COMPRESSION", PNG_Z_BEST_COMPRESSION },
		{ "PNG_Z_NO_COMPRESSION", PNG_Z_NO_COMPRESSION },
		{ "PNG_INTERLACED", PNG_INTERLACED },
	};
	size_t i;

	printf("the PNG writer's save flags\n");

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		const char *path = scratch("mng_flags.mng");
		FIBITMAP *src = pattern(24, 16, FIT_BITMAP, 24, 5);
		FIBITMAP *back;

		if (!src) {
			continue;
		}
		if (!FreeImage_Save(FIF_MNG, src, path, cases[i].flags)) {
			fail("%s: FreeImage_Save returned FALSE", cases[i].name);
			FreeImage_Unload(src);
			continue;
		}
		back = FreeImage_Load(FIF_MNG, path, 0);
		if (!back) {
			fail("%s: the file cannot be read back", cases[i].name);
		} else if (!same_picture(src, back)) {
			fail("%s: what came back differs", cases[i].name);
		} else {
			ok("%s produces a file that reads back unchanged", cases[i].name);
		}
		if (back) {
			FreeImage_Unload(back);
		}
		FreeImage_Unload(src);
	}
}

/* no pages: no file, as with APNG, GIF and TIFF */
static void test_empty_document(void) {
	const char *path = scratch("mng_empty_doc.mng");
	FIMULTIBITMAP *mb;

	printf("a document with no pages\n");

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, TRUE, FALSE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap with create_new returned NULL");
		return;
	}
	FreeImage_CloseMultiBitmap(mb, 0);

	if (FreeImage_GetFileType(path, 0) == FIF_MNG) {
		fail("a document that was never given a page produced a MNG");
	} else {
		ok("a document with no pages writes no MNG, as APNG, GIF and TIFF do");
	}
}

static void test_refuses_unsupported(void) {
	const char *path = scratch("mng_refuse.mng");
	FIBITMAP *dib;

	printf("refusing what cannot be written\n");

	dib = FreeImage_AllocateT(FIT_FLOAT, 8, 8, 32, 0, 0, 0);
	if (!dib) {
		fail("the float bitmap could not be allocated");
		return;
	}
	if (FreeImage_FIFSupportsExportType(FIF_MNG, FIT_FLOAT)) {
		fail("MNG claims it can export FIT_FLOAT, which PNG cannot hold");
	} else if (FreeImage_Save(FIF_MNG, dib, path, 0)) {
		fail("a FIT_FLOAT bitmap was accepted for saving");
	} else {
		ok("a type MNG cannot hold is refused by FreeImage_Save");
	}
	FreeImage_Unload(dib);

	/* a header-only bitmap has nothing to write */
	{
		FIBITMAP *solid_dib = solid(8, 8, 1, 2, 3);
		const char *source = scratch("mng_nopixels_src.mng");

		if (solid_dib && FreeImage_Save(FIF_MNG, solid_dib, source, 0)) {
			FIBITMAP *empty = FreeImage_Load(FIF_MNG, source, FIF_LOAD_NOPIXELS);
			if (empty && !FreeImage_HasPixels(empty)) {
				if (FreeImage_Save(FIF_MNG, empty, path, 0)) {
					fail("a bitmap with no pixels was accepted for saving");
				} else {
					ok("a bitmap with no pixels is refused");
				}
			} else {
				fail("a header-only load did not produce a pixel-less bitmap");
			}
			if (empty) {
				FreeImage_Unload(empty);
			}
		}
		if (solid_dib) {
			FreeImage_Unload(solid_dib);
		}
	}
}


static void test_multibitmap_memory(void) {
	static const BYTE COLOUR[3][3] = { { 200, 0, 0 }, { 0, 200, 0 }, { 0, 0, 200 } };
	const char *path = scratch("mng_mem_src.mng");
	FIMULTIBITMAP *mb;
	FIMEMORY *hmem;
	int i, bad = 0;

	printf("the page API over a memory stream\n");

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, TRUE, FALSE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap with create_new returned NULL");
		return;
	}
	for (i = 0; i < 3; i++) {
		FIBITMAP *dib = solid(12, 12, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2]);
		set_delay(dib, 140 + i);
		FreeImage_AppendPage(mb, dib);
		FreeImage_Unload(dib);
	}
	FreeImage_CloseMultiBitmap(mb, 0);

	/* out to a memory stream ... */
	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("the file cannot be reopened");
		return;
	}
	hmem = FreeImage_OpenMemory(NULL, 0);
	if (!hmem) {
		fail("FreeImage_OpenMemory returned NULL");
		FreeImage_CloseMultiBitmap(mb, 0);
		return;
	}
	if (!FreeImage_SaveMultiBitmapToMemory(FIF_MNG, mb, hmem, 0)) {
		fail("FreeImage_SaveMultiBitmapToMemory returned FALSE");
		bad = 1;
	}
	FreeImage_CloseMultiBitmap(mb, 0);

	/* ... and back in from one */
	FreeImage_SeekMemory(hmem, 0, SEEK_SET);
	mb = FreeImage_LoadMultiBitmapFromMemory(FIF_MNG, hmem, 0);
	if (!mb) {
		fail("FreeImage_LoadMultiBitmapFromMemory returned NULL");
		FreeImage_CloseMemory(hmem);
		return;
	}
	if (FreeImage_GetPageCount(mb) != 3) {
		fail("the stream holds %d pages, expected 3", FreeImage_GetPageCount(mb));
		bad = 1;
	}
	for (i = 0; i < FreeImage_GetPageCount(mb) && i < 3; i++) {
		FIBITMAP *dib = FreeImage_LockPage(mb, i);
		RGBQUAD colour;

		if (!dib) {
			fail("page %d could not be locked in the stream", i);
			bad = 1;
			continue;
		}
		memset(&colour, 0, sizeof(colour));
		FreeImage_GetPixelColor(dib, 0, 0, &colour);
		if (colour.rgbRed != COLOUR[i][0] || colour.rgbGreen != COLOUR[i][1]
			|| colour.rgbBlue != COLOUR[i][2]) {
			fail("page %d of the stream is (%u,%u,%u), expected (%u,%u,%u)", i,
				 colour.rgbRed, colour.rgbGreen, colour.rgbBlue,
				 COLOUR[i][0], COLOUR[i][1], COLOUR[i][2]);
			bad = 1;
		}
		if (anim_tag(dib, "FrameTime", -1) != 140 + i) {
			fail("page %d of the stream is %ld ms, expected %d",
				 i, anim_tag(dib, "FrameTime", -1), 140 + i);
			bad = 1;
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);
	FreeImage_CloseMemory(hmem);

	if (!bad) {
		ok("a document written to and read from a memory stream keeps its pages");
	}
}

static void test_unlock_changed(void) {
	const char *path = scratch("mng_unlock.mng");
	FIMULTIBITMAP *mb;
	FIBITMAP *dib;
	RGBQUAD colour;
	int i, bad = 0;

	printf("editing a page in place\n");

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, TRUE, FALSE, FALSE, 0);
	if (!mb) {
		fail("FreeImage_OpenMultiBitmap with create_new returned NULL");
		return;
	}
	for (i = 0; i < 3; i++) {
		dib = solid(10, 10, (BYTE)(50 * i), 0, 0);
		set_delay(dib, 111);
		FreeImage_AppendPage(mb, dib);
		FreeImage_Unload(dib);
	}
	FreeImage_CloseMultiBitmap(mb, 0);

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, FALSE, FALSE, 0);
	if (!mb) {
		fail("the file cannot be opened for editing");
		return;
	}
	dib = FreeImage_LockPage(mb, 1);
	if (!dib) {
		fail("page 1 could not be locked");
		FreeImage_CloseMultiBitmap(mb, 0);
		return;
	}
	/* repaint it, and hand it back as changed */
	for (i = 0; i < (int)FreeImage_GetHeight(dib); i++) {
		BYTE *scanline = FreeImage_GetScanLine(dib, (unsigned)i);
		unsigned x;
		for (x = 0; x < FreeImage_GetWidth(dib); x++) {
			scanline[FI_RGBA_RED] = 7;
			scanline[FI_RGBA_GREEN] = 99;
			scanline[FI_RGBA_BLUE] = 200;
			scanline += 3;
		}
	}
	FreeImage_UnlockPage(mb, dib, TRUE);
	if (!FreeImage_CloseMultiBitmap(mb, 0)) {
		fail("FreeImage_CloseMultiBitmap reported failure");
		return;
	}

	mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) {
		fail("the edited file cannot be opened");
		return;
	}
	if (FreeImage_GetPageCount(mb) != 3) {
		fail("the edited file has %d pages, expected 3", FreeImage_GetPageCount(mb));
		bad = 1;
	}
	for (i = 0; i < FreeImage_GetPageCount(mb) && i < 3; i++) {
		BYTE want_red = (i == 1) ? 7 : (BYTE)(50 * i);
		BYTE want_green = (i == 1) ? 99 : 0;
		BYTE want_blue = (i == 1) ? 200 : 0;

		dib = FreeImage_LockPage(mb, i);
		if (!dib) {
			bad = 1;
			continue;
		}
		memset(&colour, 0, sizeof(colour));
		FreeImage_GetPixelColor(dib, 0, 0, &colour);
		if (colour.rgbRed != want_red || colour.rgbGreen != want_green
			|| colour.rgbBlue != want_blue) {
			fail("page %d is (%u,%u,%u), expected (%u,%u,%u)", i,
				 colour.rgbRed, colour.rgbGreen, colour.rgbBlue,
				 want_red, want_green, want_blue);
			bad = 1;
		}
		/* the repaint must keep the page's timing */
		if (anim_tag(dib, "FrameTime", -1) != 111) {
			fail("page %d is %ld ms after the edit, expected the 111 it had before",
				 i, anim_tag(dib, "FrameTime", -1));
			bad = 1;
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);

	if (!bad) {
		ok("a page unlocked as changed reaches the file with its timing, and its "
		   "neighbours do not move");
	}
}

/* --------------------------------------------------------------------- */

int main(void) {
	FreeImage_Initialise(FALSE);
	FreeImage_SetOutputMessage(quiet);

	if (!FreeImage_FIFSupportsWriting(FIF_MNG)) {
		printf("  FAIL FIF_MNG says it does not support writing\n");
		FreeImage_DeInitialise();
		printf("\n0 check(s), 1 failure(s)\n");
		return 1;
	}

	test_single_image();
	test_animation_round_trip();
	test_untagged_default();
	test_editing();
	test_disposal();
	test_loop_counts();
	test_canvas_grows();
	test_background();
	test_memory_stream();
	test_save_flags();
	test_multibitmap_memory();
	test_unlock_changed();
	test_empty_document();
	test_refuses_unsupported();

	FreeImage_DeInitialise();

	printf("\n%d check(s), %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
