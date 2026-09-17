/*
 * FreeImage 3 - APNG round-trip and multi-page test
 *
 * Covers the APNG plugin (Source/FreeImage/PluginAPNG.cpp) through the API a
 * caller actually uses for an animation: FreeImage_OpenMultiBitmap and
 * AppendPage/InsertPage/DeletePage to build one, FreeImage_LockPage to read it
 * back, and FreeImage_Save/FreeImage_Load for the single-image case.
 *
 * What is asserted:
 *
 *   - what went in comes back. Every frame written is compared, pixel for
 *     pixel, against the canvas APNG_PLAYBACK produces for it. The writer is
 *     allowed to store a frame as the rectangle that changed rather than whole
 *     - that is the point of it - so the raw pages are checked for being that
 *     rectangle, and the composited ones for being identical to the input.
 *   - a single page is written as a plain PNG, palette, bit depth and all,
 *     because an animation of one frame is not an animation.
 *   - the animation metadata (delays, placement, disposal, blending, loop
 *     count and canvas) survives a round trip.
 *   - editing a multi-page file - inserting, deleting, reordering - produces
 *     the frames in the order asked for.
 *   - the memory stream and the file agree.
 *
 * Scratch files go to $APNG_TEST_TMP, or the current directory.
 *
 * Standalone: build with the Makefile in this directory, run from it.
 */
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

/* Several of the tests hold two scratch paths at once, so these rotate rather
   than share one buffer. */
static char tmpbuf[4][4096];
static int tmpnext = 0;

static const char *scratch(const char *name) {
	const char *dir = getenv("APNG_TEST_TMP");
	char *buf = tmpbuf[tmpnext++ & 3];
	snprintf(buf, sizeof(tmpbuf[0]), "%s/%s", dir && *dir ? dir : ".", name);
	return buf;
}

/* ------------------------------------------------------------------ */

/* A frame whose every pixel is a function of (x, y, seed), so that two frames
   with different seeds differ everywhere, plus a solid block at (bx, by) that
   moves - which is what the writer's difference region has to find. */
static FIBITMAP *make_frame(unsigned w, unsigned h, int seed, int bx, int by, int alpha) {
	FIBITMAP *dib = FreeImage_Allocate(w, h, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	unsigned x, y;
	if (!dib) return NULL;
	for (y = 0; y < h; y++) {
		BYTE *line = FreeImage_GetScanLine(dib, h - 1 - y);
		for (x = 0; x < w; x++) {
			int inblock = (bx >= 0) && (x >= (unsigned)bx) && (x < (unsigned)bx + 8)
				&& (y >= (unsigned)by) && (y < (unsigned)by + 8);
			line[FI_RGBA_RED]   = (BYTE)(inblock ? 255 : (x * 3 + seed * 7));
			line[FI_RGBA_GREEN] = (BYTE)(inblock ? 128 : (y * 5 + seed * 11));
			line[FI_RGBA_BLUE]  = (BYTE)(inblock ? 0 : (x + y + seed));
			line[FI_RGBA_ALPHA] = (BYTE)(alpha < 0 ? (BYTE)(200 + ((x + y) & 0x3F)) : alpha);
			line += 4;
		}
	}
	return dib;
}

/* A frame identical to another apart from one square, so the writer's
   difference region has a known right answer. */
static FIBITMAP *patch(FIBITMAP *src, int px, int py, int pw, int ph) {
	FIBITMAP *dib = FreeImage_Clone(src);
	unsigned h = FreeImage_GetHeight(dib);
	int x, y;
	if (!dib) return NULL;
	for (y = py; y < py + ph; y++) {
		BYTE *line = FreeImage_GetScanLine(dib, h - 1 - y) + px * 4;
		for (x = px; x < px + pw; x++) {
			line[FI_RGBA_RED] = 1; line[FI_RGBA_GREEN] = 2; line[FI_RGBA_BLUE] = 3;
			line += 4;
		}
	}
	return dib;
}

/* Every pixel equal, once both are 32-bit. Returns the number that are not. */
static int pixel_diff(FIBITMAP *a, FIBITMAP *b) {
	FIBITMAP *ra = FreeImage_ConvertTo32Bits(a), *rb = FreeImage_ConvertTo32Bits(b);
	unsigned w, h, x, y;
	int n = 0;
	if (!ra || !rb) { n = -1; goto out; }
	w = FreeImage_GetWidth(ra); h = FreeImage_GetHeight(ra);
	if (w != FreeImage_GetWidth(rb) || h != FreeImage_GetHeight(rb)) { n = -1; goto out; }
	for (y = 0; y < h; y++) {
		const BYTE *pa = FreeImage_GetScanLine(ra, y), *pb = FreeImage_GetScanLine(rb, y);
		for (x = 0; x < w; x++) {
			if (pa[FI_RGBA_ALPHA] == 0 && pb[FI_RGBA_ALPHA] == 0) { pa += 4; pb += 4; continue; }
			if (memcmp(pa, pb, 4) != 0) n++;
			pa += 4; pb += 4;
		}
	}
out:
	if (ra) FreeImage_Unload(ra);
	if (rb) FreeImage_Unload(rb);
	return n;
}

static long tag_long(FIBITMAP *dib, const char *key, long missing) {
	FITAG *tag = NULL;
	if (!FreeImage_GetMetadata(FIMD_ANIMATION, dib, key, &tag)) return missing;
	switch (FreeImage_GetTagType(tag)) {
		case FIDT_BYTE:  return *(BYTE *)FreeImage_GetTagValue(tag);
		case FIDT_SHORT: return *(WORD *)FreeImage_GetTagValue(tag);
		case FIDT_LONG:  return *(LONG *)FreeImage_GetTagValue(tag);
		default: return missing;
	}
}

/* The ANIMTAG_* ids live in a private header; FIMD_ANIMATION is keyed by name,
   which is what both PluginGIF.cpp and PluginAPNG.cpp read, so 0 will do. */
static void set_tag(FIBITMAP *dib, const char *key, WORD id, FREE_IMAGE_MDTYPE type, DWORD len, const void *val) {
	FITAG *tag = FreeImage_CreateTag();
	if (!tag) return;
	FreeImage_SetTagKey(tag, key);
	FreeImage_SetTagID(tag, id);
	FreeImage_SetTagType(tag, type);
	FreeImage_SetTagCount(tag, 1);
	FreeImage_SetTagLength(tag, len);
	FreeImage_SetTagValue(tag, val);
	FreeImage_SetMetadata(FIMD_ANIMATION, dib, key, tag);
	FreeImage_DeleteTag(tag);
}

/* Write frames[0..n) as one APNG, through the multi-page API. */
static int write_animation(const char *path, FIBITMAP **frames, int n) {
	FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, TRUE, FALSE, FALSE, 0);
	int i;
	if (!mb) return 0;
	for (i = 0; i < n; i++) FreeImage_AppendPage(mb, frames[i]);
	return FreeImage_CloseMultiBitmap(mb, 0) ? 1 : 0;
}

/* ------------------------------------------------------------------ */

static void test_roundtrip(void) {
	const char *path = scratch("apng_roundtrip.png");
	FIBITMAP *frames[5];
	FIMULTIBITMAP *mb;
	int i, bad = 0;

	printf("\n=== round trip: five 64x48 frames, composited output must be identical\n");

	for (i = 0; i < 5; i++) frames[i] = make_frame(64, 48, i, i * 10, i * 6, 255);
	if (!write_animation(path, frames, 5)) { fail("could not write %s", path); return; }

	if (FreeImage_GetFileType(path, 0) != FIF_APNG)
		fail("what we wrote is not detected as APNG");
	else
		ok("detected as APNG");

	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, APNG_PLAYBACK);
	if (!mb) { fail("cannot reopen %s", path); goto out; }

	if (FreeImage_GetPageCount(mb) != 5)
		fail("page count %d, expected 5", FreeImage_GetPageCount(mb));
	else
		ok("page count is 5");

	for (i = 0; i < 5; i++) {
		FIBITMAP *page = FreeImage_LockPage(mb, i);
		int d;
		if (!page) { fail("page %d did not load", i); bad++; continue; }
		d = pixel_diff(page, frames[i]);
		if (d != 0) { fail("page %d differs in %d pixel(s)", i, d); bad++; }
		FreeImage_UnlockPage(mb, page, FALSE);
	}
	if (!bad) ok("all five composited frames are identical to what went in");
	FreeImage_CloseMultiBitmap(mb, 0);

	/* the same file read backwards: the playback cache has to rewind, not drift */
	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, APNG_PLAYBACK);
	if (mb) {
		bad = 0;
		for (i = 4; i >= 0; i--) {
			FIBITMAP *page = FreeImage_LockPage(mb, i);
			if (!page || pixel_diff(page, frames[i]) != 0) { bad++; }
			if (page) FreeImage_UnlockPage(mb, page, FALSE);
		}
		if (bad) fail("%d frame(s) wrong when the pages are read in reverse", bad);
		else ok("reading the pages in reverse gives the same frames");
		FreeImage_CloseMultiBitmap(mb, 0);
	}

out:
	for (i = 0; i < 5; i++) FreeImage_Unload(frames[i]);
	remove(path);
}

static void test_difference_region(void) {
	const char *path = scratch("apng_diff.png");
	FIBITMAP *frames[3];
	FIMULTIBITMAP *mb;
	int i;

	printf("\n=== difference region: a frame is stored as the rectangle that changed\n");

	frames[0] = make_frame(64, 48, 0, -1, 0, 255);
	frames[1] = patch(frames[0], 20, 12, 6, 5);
	frames[2] = FreeImage_Clone(frames[1]);		/* identical to the one before it */
	if (!write_animation(path, frames, 3)) { fail("could not write %s", path); goto out; }

	/* without APNG_PLAYBACK the pages are the rectangles the file stores */
	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) { fail("cannot reopen %s", path); goto out; }

	{
		FIBITMAP *p0 = FreeImage_LockPage(mb, 0);
		FIBITMAP *p1 = FreeImage_LockPage(mb, 1);
		FIBITMAP *p2 = FreeImage_LockPage(mb, 2);
		if (!p0 || !p1 || !p2) {
			fail("a page did not load");
		} else {
			if (FreeImage_GetWidth(p0) != 64 || FreeImage_GetHeight(p0) != 48)
				fail("frame 0 is %ux%u, expected the whole 64x48 canvas",
					FreeImage_GetWidth(p0), FreeImage_GetHeight(p0));
			else
				ok("frame 0 is the whole canvas");

			if (FreeImage_GetWidth(p1) != 6 || FreeImage_GetHeight(p1) != 5 ||
				tag_long(p1, "FrameLeft", -1) != 20 || tag_long(p1, "FrameTop", -1) != 12)
				fail("frame 1 is %ux%u at (%ld,%ld), expected 6x5 at (20,12)",
					FreeImage_GetWidth(p1), FreeImage_GetHeight(p1),
					tag_long(p1, "FrameLeft", -1), tag_long(p1, "FrameTop", -1));
			else
				ok("frame 1 is exactly the 6x5 rectangle that changed, at (20,12)");

			if (FreeImage_GetWidth(p2) != 1 || FreeImage_GetHeight(p2) != 1)
				fail("frame 2 is %ux%u, expected the 1x1 a repeated frame collapses to",
					FreeImage_GetWidth(p2), FreeImage_GetHeight(p2));
			else
				ok("a frame identical to the one before it collapses to 1x1");
		}
		if (p0) FreeImage_UnlockPage(mb, p0, FALSE);
		if (p1) FreeImage_UnlockPage(mb, p1, FALSE);
		if (p2) FreeImage_UnlockPage(mb, p2, FALSE);
		FreeImage_CloseMultiBitmap(mb, 0);
	}

	/* and composited, all three must still be what went in */
	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, APNG_PLAYBACK);
	if (mb) {
		int bad = 0;
		for (i = 0; i < 3; i++) {
			FIBITMAP *page = FreeImage_LockPage(mb, i);
			int d = page ? pixel_diff(page, frames[i]) : -1;
			if (d != 0) { fail("composited frame %d differs in %d pixel(s)", i, d); bad++; }
			if (page) FreeImage_UnlockPage(mb, page, FALSE);
		}
		if (!bad) ok("the shrunk frames still composite to the originals");
		FreeImage_CloseMultiBitmap(mb, 0);
	}

out:
	for (i = 0; i < 3; i++) if (frames[i]) FreeImage_Unload(frames[i]);
	remove(path);
}

static void test_single_page(void) {
	const char *path = scratch("apng_single.png");
	FIBITMAP *dib = make_frame(32, 16, 3, 4, 4, 255);
	FIBITMAP *rgb = FreeImage_ConvertTo24Bits(dib);
	FIBITMAP *pal = rgb ? FreeImage_ColorQuantize(rgb, FIQ_WUQUANT) : NULL;
	FIBITMAP *back;

	printf("\n=== a single page is a plain PNG, not an animation of one frame\n");

	if (!FreeImage_Save(FIF_APNG, dib, path, 0)) {
		fail("FreeImage_Save(FIF_APNG) of one image failed");
	} else {
		if (FreeImage_GetFileType(path, 0) != FIF_PNG)
			fail("a one-image save is detected as %d, expected FIF_PNG",
				(int)FreeImage_GetFileType(path, 0));
		else
			ok("a one-image save is an ordinary PNG");

		back = FreeImage_Load(FIF_APNG, path, 0);
		if (!back) fail("FIF_APNG cannot read back its own single-image file");
		else {
			if (pixel_diff(back, dib) != 0) fail("the single image did not survive");
			else ok("the single image came back identical");
			FreeImage_Unload(back);
		}
	}

	/* a palette has to survive too - that is the reason for the special case */
	if (pal && FreeImage_Save(FIF_APNG, pal, path, 0)) {
		back = FreeImage_Load(FIF_PNG, path, 0);
		if (!back) fail("the paletted single image did not load");
		else {
			if (FreeImage_GetBPP(back) != 8)
				fail("an 8-bit image came back as %u-bit", FreeImage_GetBPP(back));
			else if (pixel_diff(back, pal) != 0)
				fail("the paletted image did not survive");
			else
				ok("an 8-bit paletted image stays 8-bit and paletted");
			FreeImage_Unload(back);
		}
	} else {
		fail("could not save the paletted image");
	}

	/* one AppendPage has to behave the same way */
	{
		FIBITMAP *one[1];
		one[0] = dib;
		if (write_animation(path, one, 1)) {
			if (FreeImage_GetFileType(path, 0) != FIF_PNG)
				fail("a one-page multi-bitmap is not a plain PNG");
			else
				ok("a one-page multi-bitmap is a plain PNG too");
		} else {
			fail("could not write a one-page multi-bitmap");
		}
	}

	if (pal) FreeImage_Unload(pal);
	if (rgb) FreeImage_Unload(rgb);
	FreeImage_Unload(dib);
	remove(path);
}

static void test_metadata(void) {
	const char *path = scratch("apng_meta.png");
	FIBITMAP *frames[3];
	FIMULTIBITMAP *mb;
	int i;
	LONG times[3] = { 40, 500, 1250 };
	LONG loop = 7;
	WORD canvas_w = 100, canvas_h = 80;
	WORD lefts[3] = { 0, 10, 30 }, tops[3] = { 0, 5, 20 };
	BYTE disposals[3] = { 1, 2, 3 };
	BYTE blends[3] = { 1, 0, 1 };

	printf("\n=== the animation metadata survives a round trip\n");

	for (i = 0; i < 3; i++) {
		frames[i] = make_frame(40, 30, i + 1, -1, 0, 255);
		set_tag(frames[i], "FrameTime", 0, FIDT_LONG, 4, &times[i]);
		set_tag(frames[i], "FrameLeft", 0, FIDT_SHORT, 2, &lefts[i]);
		set_tag(frames[i], "FrameTop", 0, FIDT_SHORT, 2, &tops[i]);
		set_tag(frames[i], "DisposalMethod", 0, FIDT_BYTE, 1, &disposals[i]);
		set_tag(frames[i], "BlendMethod", 0, FIDT_BYTE, 1, &blends[i]);
	}
	set_tag(frames[0], "LogicalWidth", 0, FIDT_SHORT, 2, &canvas_w);
	set_tag(frames[0], "LogicalHeight", 0, FIDT_SHORT, 2, &canvas_h);
	set_tag(frames[0], "Loop", 0, FIDT_LONG, 4, &loop);

	if (!write_animation(path, frames, 3)) { fail("could not write %s", path); goto out; }

	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, 0);
	if (!mb) { fail("cannot reopen %s", path); goto out; }

	for (i = 0; i < 3; i++) {
		FIBITMAP *page = FreeImage_LockPage(mb, i);
		if (!page) { fail("page %d did not load", i); continue; }
		if (tag_long(page, "FrameTime", -1) != times[i])
			fail("frame %d delay is %ld ms, expected %ld", i, tag_long(page, "FrameTime", -1), (long)times[i]);
		else if (tag_long(page, "FrameLeft", -1) != lefts[i] || tag_long(page, "FrameTop", -1) != tops[i])
			fail("frame %d is at (%ld,%ld), expected (%d,%d)", i,
				tag_long(page, "FrameLeft", -1), tag_long(page, "FrameTop", -1), lefts[i], tops[i]);
		else if (tag_long(page, "DisposalMethod", -1) != disposals[i])
			fail("frame %d disposal is %ld, expected %d", i, tag_long(page, "DisposalMethod", -1), disposals[i]);
		else if (tag_long(page, "BlendMethod", -1) != blends[i])
			fail("frame %d blend is %ld, expected %d", i, tag_long(page, "BlendMethod", -1), blends[i]);
		else if (i == 0 && (tag_long(page, "LogicalWidth", -1) != canvas_w ||
							tag_long(page, "LogicalHeight", -1) != canvas_h ||
							tag_long(page, "Loop", -1) != loop))
			fail("canvas/loop came back as %ldx%ld loop=%ld, expected %dx%d loop=%ld",
				tag_long(page, "LogicalWidth", -1), tag_long(page, "LogicalHeight", -1),
				tag_long(page, "Loop", -1), canvas_w, canvas_h, (long)loop);
		else
			ok("frame %d: delay, placement, disposal and blending all survived", i);
		FreeImage_UnlockPage(mb, page, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);

out:
	for (i = 0; i < 3; i++) FreeImage_Unload(frames[i]);
	remove(path);
}

/* The page order after editing, read back from each frame's first pixel. */
static void check_order(const char *path, const int *want, int n, const char *what) {
	FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, APNG_PLAYBACK);
	int i, bad = 0;
	if (!mb) { fail("%s: cannot reopen", what); return; }
	if (FreeImage_GetPageCount(mb) != n) {
		fail("%s: %d pages, expected %d", what, FreeImage_GetPageCount(mb), n);
		FreeImage_CloseMultiBitmap(mb, 0);
		return;
	}
	for (i = 0; i < n; i++) {
		FIBITMAP *page = FreeImage_LockPage(mb, i);
		RGBQUAD c;
		if (!page) { fail("%s: page %d did not load", what, i); bad++; continue; }
		/* the marker: make_frame() puts (x*3+seed*7, y*5+seed*11, x+y+seed) at (0,0) */
		if (FreeImage_GetPixelColor(page, 0, FreeImage_GetHeight(page) - 1, &c)) {
			int seed = (int)c.rgbBlue;
			if (seed != want[i]) {
				fail("%s: page %d holds frame %d, expected %d", what, i, seed, want[i]);
				bad++;
			}
		}
		FreeImage_UnlockPage(mb, page, FALSE);
	}
	if (!bad) ok("%s", what);
	FreeImage_CloseMultiBitmap(mb, 0);
}

static void test_multipage_editing(void) {
	const char *path = scratch("apng_edit.png");
	FIBITMAP *frames[4];
	FIMULTIBITMAP *mb;
	int i;
	int want4[4] = { 0, 1, 2, 3 };
	int want5[5] = { 0, 9, 1, 2, 3 };
	int want4b[4] = { 0, 9, 2, 3 };

	printf("\n=== editing a file: InsertPage, DeletePage\n");

	for (i = 0; i < 4; i++) frames[i] = make_frame(24, 16, i, -1, 0, 255);
	if (!write_animation(path, frames, 4)) { fail("could not write %s", path); goto out; }
	check_order(path, want4, 4, "four appended pages come back in order");

	/* insert a fifth frame at position 1 */
	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, FALSE, FALSE, 0);
	if (!mb) { fail("cannot reopen for editing"); goto out; }
	{
		FIBITMAP *extra = make_frame(24, 16, 9, -1, 0, 255);
		FreeImage_InsertPage(mb, 1, extra);
		FreeImage_Unload(extra);
	}
	FreeImage_CloseMultiBitmap(mb, 0);
	check_order(path, want5, 5, "InsertPage put the new frame at position 1");

	/* and delete what is now page 2 */
	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, FALSE, FALSE, 0);
	if (!mb) { fail("cannot reopen for deleting"); goto out; }
	FreeImage_DeletePage(mb, 2);
	FreeImage_CloseMultiBitmap(mb, 0);
	check_order(path, want4b, 4, "DeletePage removed page 2");

out:
	for (i = 0; i < 4; i++) FreeImage_Unload(frames[i]);
	remove(path);
}

static void test_memory_stream(void) {
	const char *path = scratch("apng_mem.png");
	FIBITMAP *frames[3];
	FIMEMORY *hmem;
	FIMULTIBITMAP *mb, *out;
	int i, bad = 0;

	printf("\n=== the memory stream and the file agree\n");

	for (i = 0; i < 3; i++) frames[i] = make_frame(48, 32, i * 3, i * 4, 2, -1);
	if (!write_animation(path, frames, 3)) { fail("could not write %s", path); goto out; }

	/* copy the file into a memory stream through the multi-page API */
	hmem = FreeImage_OpenMemory(NULL, 0);
	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, 0);
	if (!hmem || !mb) { fail("could not set up the memory copy"); goto out; }
	if (!FreeImage_SaveMultiBitmapToMemory(FIF_APNG, mb, hmem, 0))
		fail("FreeImage_SaveMultiBitmapToMemory failed");
	else
		ok("the whole animation saved to a memory stream");
	FreeImage_CloseMultiBitmap(mb, 0);

	FreeImage_SeekMemory(hmem, 0, SEEK_SET);
	out = FreeImage_LoadMultiBitmapFromMemory(FIF_APNG, hmem, APNG_PLAYBACK);
	if (!out) { fail("cannot read the animation back out of memory"); goto out2; }
	if (FreeImage_GetPageCount(out) != 3) {
		fail("the memory copy has %d pages, expected 3", FreeImage_GetPageCount(out));
	} else {
		for (i = 0; i < 3; i++) {
			FIBITMAP *page = FreeImage_LockPage(out, i);
			int d = page ? pixel_diff(page, frames[i]) : -1;
			if (d != 0) { fail("memory frame %d differs in %d pixel(s)", i, d); bad++; }
			if (page) FreeImage_UnlockPage(out, page, FALSE);
		}
		if (!bad) ok("every frame of the memory copy matches the original");
	}
	FreeImage_CloseMultiBitmap(out, 0);

out2:
	FreeImage_CloseMemory(hmem);
out:
	for (i = 0; i < 3; i++) FreeImage_Unload(frames[i]);
	remove(path);
}

static void test_mixed_depths(void) {
	const char *path = scratch("apng_mixed.png");
	FIBITMAP *frames[3], *expect[3];
	FIMULTIBITMAP *mb;
	int i, bad = 0;

	for (i = 0; i < 3; i++) { frames[i] = NULL; expect[i] = NULL; }

	printf("\n=== frames of different depths all become one animation\n");

	frames[0] = make_frame(32, 24, 1, -1, 0, 255);
	frames[1] = FreeImage_ConvertTo24Bits(frames[0]);
	frames[2] = FreeImage_ConvertTo8Bits(frames[0]);
	if (!frames[1] || !frames[2]) { fail("could not build the frames"); goto out; }
	for (i = 0; i < 3; i++) expect[i] = FreeImage_ConvertTo32Bits(frames[i]);

	if (!write_animation(path, frames, 3)) { fail("could not write %s", path); goto out; }

	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, APNG_PLAYBACK);
	if (!mb) { fail("cannot reopen %s", path); goto out; }
	if (FreeImage_GetPageCount(mb) != 3) {
		fail("page count %d, expected 3", FreeImage_GetPageCount(mb));
	} else {
		for (i = 0; i < 3; i++) {
			FIBITMAP *page = FreeImage_LockPage(mb, i);
			int d = (page && expect[i]) ? pixel_diff(page, expect[i]) : -1;
			if (d != 0) { fail("frame %d (%u-bit input) differs in %d pixel(s)",
				i, FreeImage_GetBPP(frames[i]), d); bad++; }
			if (page) FreeImage_UnlockPage(mb, page, FALSE);
		}
		if (!bad) ok("32-, 24- and 8-bit frames all came back as the 32-bit originals");
	}
	FreeImage_CloseMultiBitmap(mb, 0);

out:
	for (i = 0; i < 3; i++) {
		if (frames[i]) FreeImage_Unload(frames[i]);
		if (expect[i]) FreeImage_Unload(expect[i]);
	}
	remove(path);
}

static void test_transparency(void) {
	const char *path = scratch("apng_alpha.png");
	FIBITMAP *frames[4];
	FIMULTIBITMAP *mb;
	int i, bad = 0;

	printf("\n=== alpha survives, including a fully transparent frame\n");

	frames[0] = make_frame(40, 40, 1, -1, 0, 255);
	frames[1] = make_frame(40, 40, 2, -1, 0, -1);	/* varying alpha */
	frames[2] = make_frame(40, 40, 3, -1, 0, 0);	/* fully transparent */
	frames[3] = make_frame(40, 40, 4, -1, 0, 128);
	if (!write_animation(path, frames, 4)) { fail("could not write %s", path); goto out; }

	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, APNG_PLAYBACK);
	if (!mb) { fail("cannot reopen %s", path); goto out; }
	for (i = 0; i < 4; i++) {
		FIBITMAP *page = FreeImage_LockPage(mb, i);
		int d = page ? pixel_diff(page, frames[i]) : -1;
		if (d != 0) { fail("frame %d differs in %d pixel(s)", i, d); bad++; }
		if (page) FreeImage_UnlockPage(mb, page, FALSE);
	}
	if (!bad) ok("all four frames, alpha included, came back identical");
	FreeImage_CloseMultiBitmap(mb, 0);

out:
	for (i = 0; i < 4; i++) FreeImage_Unload(frames[i]);
	remove(path);
}

static void test_header_only(void) {
	const char *path = scratch("apng_hdr.png");
	FIBITMAP *frames[2];
	FIMULTIBITMAP *mb;
	int i;

	printf("\n=== FIF_LOAD_NOPIXELS gives the geometry without the pixels\n");

	for (i = 0; i < 2; i++) frames[i] = make_frame(70, 50, i, -1, 0, 255);
	if (!write_animation(path, frames, 2)) { fail("could not write %s", path); goto out; }

	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, FIF_LOAD_NOPIXELS | APNG_PLAYBACK);
	if (!mb) { fail("cannot reopen %s", path); goto out; }
	{
		FIBITMAP *page = FreeImage_LockPage(mb, 1);
		if (!page) fail("header-only page did not load");
		else if (FreeImage_HasPixels(page)) fail("header-only page came back with pixels");
		else if (FreeImage_GetWidth(page) != 70 || FreeImage_GetHeight(page) != 50)
			fail("header-only page is %ux%u, expected the 70x50 canvas",
				FreeImage_GetWidth(page), FreeImage_GetHeight(page));
		else ok("a composited page can be asked for as a header only");
		if (page) FreeImage_UnlockPage(mb, page, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);

out:
	for (i = 0; i < 2; i++) FreeImage_Unload(frames[i]);
	remove(path);
}

static void test_size(void) {
	const char *path = scratch("apng_size.png");
	const char *one = scratch("apng_size_one.png");
	FIBITMAP *frames[10];
	long animated = 0, single = 0;
	FILE *f;
	int i;

	printf("\n=== the difference region is what keeps the file small\n");

	frames[0] = make_frame(200, 150, 0, -1, 0, 255);
	for (i = 1; i < 10; i++) frames[i] = patch(frames[0], 10 + i, 20, 5, 5);
	if (!write_animation(path, frames, 10)) { fail("could not write %s", path); goto out; }
	if (!FreeImage_Save(FIF_APNG, frames[0], one, 0)) { fail("could not write %s", one); goto out; }

	if ((f = fopen(path, "rb")) != NULL) { fseek(f, 0, SEEK_END); animated = ftell(f); fclose(f); }
	if ((f = fopen(one, "rb")) != NULL) { fseek(f, 0, SEEK_END); single = ftell(f); fclose(f); }

	printf("       ten near-identical 200x150 frames: %ld bytes; one frame alone: %ld,\n"
		"       so ten stored whole would be about %ld\n", animated, single, single * 10);
	if (animated <= 0 || single <= 0) fail("could not measure the files");
	else if (animated * 2 >= single * 10)
		fail("ten frames cost %ld bytes against about %ld for ten whole ones - "
			"the frames are not being shrunk", animated, single * 10);
	else
		ok("the animation is less than half of what ten whole frames would cost");

out:
	for (i = 0; i < 10; i++) if (frames[i]) FreeImage_Unload(frames[i]);
	remove(path);
	remove(one);
}

/* The file is only assembled in Close(), which cannot fail out loud, so anything
   that cannot be written has to be refused by Save() while FreeImage_Save() is
   still listening - otherwise it answers TRUE and leaves its output in place. */
static void test_refusals(void) {
	const char *path = scratch("apng_refuse.png");
	const char *seed = scratch("apng_refuse_seed.png");
	FIBITMAP *floats = FreeImage_AllocateT(FIT_FLOAT, 16, 16, 32, 0, 0, 0);
	FIBITMAP *rgb16 = FreeImage_Allocate(16, 16, 16, FI16_565_RED_MASK, FI16_565_GREEN_MASK, FI16_565_BLUE_MASK);
	FIBITMAP *header = NULL, *whole = make_frame(16, 16, 1, -1, 0, 255);
	FILE *f;

	/* the only way to a header-only bitmap through the public API */
	if (whole && FreeImage_Save(FIF_PNG, whole, seed, 0)) {
		header = FreeImage_Load(FIF_PNG, seed, FIF_LOAD_NOPIXELS);
	}

	printf("\n=== what cannot be written is refused, not half written\n");

	if (floats && FreeImage_Save(FIF_APNG, floats, path, 0)) {
		fail("saving an FIT_FLOAT image reported success");
		remove(path);
	} else if ((f = fopen(path, "rb")) != NULL) {
		fclose(f);
		fail("saving an FIT_FLOAT image left a file behind");
		remove(path);
	} else {
		ok("an FIT_FLOAT image is refused and leaves no file");
	}

	if (rgb16 && FreeImage_Save(FIF_APNG, rgb16, path, 0)) {
		fail("saving a 16-bit bitmap reported success, but the plugin does not export 16");
		remove(path);
	} else {
		ok("a 16-bit bitmap is refused, as SupportsExportDepth says");
	}

	/* FreeImage_SaveToHandle stops a header-only image itself; this is the path
	   that does not - the one FreeImage_SaveMultiBitmapToHandle takes */
	if (!header || FreeImage_HasPixels(header)) {
		fail("could not make a header-only bitmap to try");
	} else {
		FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, TRUE, FALSE, FALSE, 0);
		if (mb) {
			FreeImage_AppendPage(mb, header);
			FreeImage_CloseMultiBitmap(mb, 0);
			ok("a header-only page does not crash the writer");
			remove(path);
		} else {
			fail("could not open a multi-bitmap to try the header-only page on");
		}
	}

	if (floats) FreeImage_Unload(floats);
	if (header) FreeImage_Unload(header);
	if (whole) FreeImage_Unload(whole);
	if (rgb16) FreeImage_Unload(rgb16);
	remove(seed);
}

int main(void) {
	FreeImage_Initialise(FALSE);
	FreeImage_SetOutputMessage(quiet);

	printf("FreeImage %s - APNG round-trip tests\n", FreeImage_GetVersion());

	test_roundtrip();
	test_difference_region();
	test_single_page();
	test_metadata();
	test_multipage_editing();
	test_memory_stream();
	test_mixed_depths();
	test_transparency();
	test_header_only();
	test_refusals();
	test_size();

	printf("\n%d check(s) passed, %d failure(s)\n", checks, failures);
	FreeImage_DeInitialise();
	return failures ? 1 : 0;
}
