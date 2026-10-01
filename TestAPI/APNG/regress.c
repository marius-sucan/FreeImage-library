/* FreeImage 3 - APNG round-trip and multi-page test */
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

/* rotating buffers: some tests hold two paths at once */
static char tmpbuf[4][4096];
static int tmpnext = 0;

static const char *scratch(const char *name) {
	const char *dir = getenv("APNG_TEST_TMP");
	char *buf = tmpbuf[tmpnext++ & 3];
	snprintf(buf, sizeof(tmpbuf[0]), "%s/%s", dir && *dir ? dir : ".", name);
	return buf;
}

/* ------------------------------------------------------------------ */

/* pixels depend on (x, y, seed), plus a moving block at (bx, by) */
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

/* src with one square changed */
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

/* FIMD_ANIMATION is read by key, so the id can be 0 */
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

	/* read backwards: the playback cache must rewind */
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

	/* raw pages are the stored rectangles */
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

	/* composited: identical to the input */
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

	/* a palette must survive */
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

/* page order, from each frame's first pixel */
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

	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, FALSE, FALSE, 0);
	if (!mb) { fail("cannot reopen for editing"); goto out; }
	{
		FIBITMAP *extra = make_frame(24, 16, 9, -1, 0, 255);
		FreeImage_InsertPage(mb, 1, extra);
		FreeImage_Unload(extra);
	}
	FreeImage_CloseMultiBitmap(mb, 0);
	check_order(path, want5, 5, "InsertPage put the new frame at position 1");

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

/* a cICP chunk after IHDR; FALSE when the file is not a PNG */
static int insert_cicp(const char *path, const BYTE cicp[4]) {
	FILE *f = fopen(path, "rb");
	BYTE *data, chunk[16];
	long size;
	DWORD crc;
	int done = 0;
	if (!f) return 0;
	fseek(f, 0, SEEK_END); size = ftell(f); fseek(f, 0, SEEK_SET);
	data = (BYTE *)malloc(size);
	if (data && fread(data, 1, size, f) == (size_t)size && size > 33 && !memcmp(data + 12, "IHDR", 4)) {
		chunk[0] = 0; chunk[1] = 0; chunk[2] = 0; chunk[3] = 4;
		memcpy(chunk + 4, "cICP", 4);
		memcpy(chunk + 8, cicp, 4);
		crc = FreeImage_ZLibCRC32(0, chunk + 4, 8);
		chunk[12] = (BYTE)(crc >> 24); chunk[13] = (BYTE)(crc >> 16); chunk[14] = (BYTE)(crc >> 8); chunk[15] = (BYTE)crc;
		fclose(f);
		f = fopen(path, "wb");
		/* signature + IHDR is 33 bytes */
		done = f && fwrite(data, 1, 33, f) == 33 && fwrite(chunk, 1, 16, f) == 16 && fwrite(data + 33, 1, size - 33, f) == (size_t)(size - 33);
	}
	if (f) fclose(f);
	free(data);
	return done;
}

static int has_profile(FIBITMAP *dib, const void *profile, DWORD size) {
	FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
	return icc->data && icc->size == size && !memcmp(icc->data, profile, size);
}

static int has_cicp(FIBITMAP *dib, const BYTE cicp[4]) {
	FITAG *tag = NULL;
	return FreeImage_GetMetadata(FIMD_CUSTOM, dib, "CICP", &tag) && tag && FreeImage_GetTagCount(tag) == 4 &&
		!memcmp(FreeImage_GetTagValue(tag), cicp, 4);
}

/* ---- APNG files built chunk by chunk ---- */

typedef struct { BYTE *data; size_t size; } Bytes;

static void bytes_add(Bytes *b, const void *p, size_t n) {
	b->data = (BYTE *)realloc(b->data, b->size + n);
	memcpy(b->data + b->size, p, n);
	b->size += n;
}

static void put32(BYTE *p, DWORD v) {
	p[0] = (BYTE)(v >> 24); p[1] = (BYTE)(v >> 16); p[2] = (BYTE)(v >> 8); p[3] = (BYTE)v;
}

static void add_chunk(Bytes *b, const char *type, const void *data, DWORD length) {
	BYTE head[8], tail[4];
	DWORD crc = FreeImage_ZLibCRC32(0, (BYTE *)type, 4);
	if (length) crc = FreeImage_ZLibCRC32(crc, (BYTE *)data, length);
	put32(head, length);
	memcpy(head + 4, type, 4);
	put32(tail, crc);
	bytes_add(b, head, 8);
	if (length) bytes_add(b, data, length);
	bytes_add(b, tail, 4);
}

/* a frame of 1/10 s, disposed of to nothing, its pixels replacing the canvas's */
static void add_fctl(Bytes *b, DWORD seq, DWORD w, DWORD h, DWORD x, DWORD y) {
	BYTE f[26];
	put32(f, seq); put32(f + 4, w); put32(f + 8, h); put32(f + 12, x); put32(f + 16, y);
	f[20] = 0; f[21] = 1; f[22] = 0; f[23] = 10;
	f[24] = 0; f[25] = 0;
	add_chunk(b, "fcTL", f, 26);
}

/* zlib data of w x h pixels of one value, unfiltered: bytes 1 an 8-bit index, 2 16-bit grey, 3 RGB (low, high byte, 0) */
static DWORD image_data(BYTE *out, DWORD out_size, unsigned w, unsigned h, unsigned bytes, WORD value) {
	const DWORD raw_size = h * (1 + bytes * w);
	BYTE *raw = (BYTE *)malloc(raw_size);
	DWORD n;
	unsigned x, y;
	for (y = 0; y < h; y++) {
		BYTE *row = raw + y * (1 + bytes * w);
		row[0] = 0;
		for (x = 0; x < w; x++) {
			if (bytes == 3) {
				row[1 + 3 * x] = (BYTE)value; row[2 + 3 * x] = (BYTE)(value >> 8); row[3 + 3 * x] = 0;
			} else if (bytes == 2) {
				row[1 + 2 * x] = (BYTE)(value >> 8); row[2 + 2 * x] = (BYTE)value;
			} else {
				row[1 + x] = (BYTE)value;
			}
		}
	}
	n = FreeImage_ZLibCompress(out, out_size, raw, raw_size);
	free(raw);
	return n;
}

static int write_bytes(const char *path, const Bytes *b) {
	FILE *f = fopen(path, "wb");
	int done = f && fwrite(b->data, 1, b->size, f) == b->size;
	if (f) fclose(f);
	return done;
}

/* a 16-bit grey animation: frame 0 16x16 of value0, frame 1 8x8 of value1 at (4,4) */
static int grey16_animation(const char *path, WORD value0, WORD value1) {
	static const BYTE signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	BYTE ihdr[13], actl[8], zdata[8192];
	Bytes b = { NULL, 0 };
	DWORD n;
	int done;
	bytes_add(&b, signature, 8);
	put32(ihdr, 16); put32(ihdr + 4, 16);
	ihdr[8] = 16; ihdr[9] = 0; ihdr[10] = ihdr[11] = ihdr[12] = 0;
	add_chunk(&b, "IHDR", ihdr, 13);
	put32(actl, 2); put32(actl + 4, 0);
	add_chunk(&b, "acTL", actl, 8);
	add_fctl(&b, 0, 16, 16, 0, 0);
	n = image_data(zdata, sizeof(zdata), 16, 16, 2, value0);
	add_chunk(&b, "IDAT", zdata, n);
	add_fctl(&b, 1, 8, 8, 4, 4);
	put32(zdata, 2);
	n = image_data(zdata + 4, sizeof(zdata) - 4, 8, 8, 2, value1);
	add_chunk(&b, "fdAT", zdata, n + 4);
	add_chunk(&b, "IEND", NULL, 0);
	done = write_bytes(path, &b);
	free(b.data);
	return done;
}

/* a 16-bit grey animation plays, each frame by its high byte */
static void test_grey16_playback(void) {
	const char *path = scratch("apng_grey16.png");
	FIMULTIBITMAP *mb;
	int page, bad = 0;

	printf("\n=== a 16-bit grey animation plays\n");

	if (!grey16_animation(path, 0x8080, 0x4000)) { fail("could not write %s", path); return; }
	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, APNG_PLAYBACK);
	for (page = 0; page < 2; page++) {
		FIBITMAP *dib = mb ? FreeImage_LockPage(mb, page) : NULL;
		RGBQUAD under, over;
		if (!dib) { fail("canvas %d did not load", page); bad++; continue; }
		/* (1,1) and (6,6), counted from the top */
		FreeImage_GetPixelColor(dib, 1, 14, &under);
		FreeImage_GetPixelColor(dib, 6, 9, &over);
		if (under.rgbRed != 128 || under.rgbGreen != 128 || under.rgbBlue != 128 || under.rgbReserved != 255) {
			fail("canvas %d at (1,1): (%u,%u,%u,%u), want (128,128,128,255)", page, under.rgbRed, under.rgbGreen, under.rgbBlue, under.rgbReserved);
			bad++;
		}
		if (page == 1 && (over.rgbRed != 64 || over.rgbGreen != 64 || over.rgbBlue != 64)) {
			fail("canvas 1 at (6,6): (%u,%u,%u), want (64,64,64)", over.rgbRed, over.rgbGreen, over.rgbBlue);
			bad++;
		}
		FreeImage_UnlockPage(mb, dib, FALSE);
	}
	if (mb) FreeImage_CloseMultiBitmap(mb, 0);
	if (!bad) ok("both canvases of a 16-bit grey APNG, 0x8080 as 128 and 0x4000 as 64");
	remove(path);
}

/* an RGB animation of two frames with an Adobe RGB iCCP before or after the first IDAT, and a tEXt after it */
static int trailing_animation(const char *path, int iccp_after) {
	static const BYTE signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
	static const char text[] = "Comment\0after";
	DWORD adobe_size = 0;
	const BYTE *adobe = (const BYTE *)FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
	BYTE ihdr[13], actl[8], zdata[8192], iccp[2048];
	Bytes b = { NULL, 0 };
	DWORD n, iccp_size;
	int done;
	memcpy(iccp, "p\0\0", 3);
	iccp_size = 3 + FreeImage_ZLibCompress(iccp + 3, sizeof(iccp) - 3, (BYTE *)adobe, adobe_size);
	bytes_add(&b, signature, 8);
	put32(ihdr, 16); put32(ihdr + 4, 16);
	ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = ihdr[11] = ihdr[12] = 0;
	add_chunk(&b, "IHDR", ihdr, 13);
	if (!iccp_after) add_chunk(&b, "iCCP", iccp, iccp_size);
	put32(actl, 2); put32(actl + 4, 0);
	add_chunk(&b, "acTL", actl, 8);
	add_fctl(&b, 0, 16, 16, 0, 0);
	n = image_data(zdata, sizeof(zdata), 16, 16, 3, 0x40C0);
	add_chunk(&b, "IDAT", zdata, n);
	if (iccp_after) add_chunk(&b, "iCCP", iccp, iccp_size);
	add_chunk(&b, "tEXt", text, sizeof(text) - 1);
	add_fctl(&b, 1, 8, 8, 4, 4);
	put32(zdata, 2);
	n = image_data(zdata + 4, sizeof(zdata) - 4, 8, 8, 3, 0x8020);
	add_chunk(&b, "fdAT", zdata, n + 4);
	add_chunk(&b, "IEND", NULL, 0);
	done = write_bytes(path, &b);
	free(b.data);
	return done;
}

static int has_comment(FIBITMAP *dib) {
	FITAG *tag = NULL;
	return FreeImage_GetMetadata(FIMD_COMMENTS, dib, "Comment", &tag) && tag && FreeImage_GetTagLength(tag) >= 5 &&
		!memcmp(FreeImage_GetTagValue(tag), "after", 5);
}

/* a frame inherits the chunks before the first IDAT; one after it is out of place, as in a PNG, a tEXt still read */
static void test_trailing_chunks(void) {
	const char *path = scratch("apng_trailing.png");
	DWORD adobe_size = 0;
	const void *adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
	int after, page, bad = 0;

	printf("\n=== chunks after the first IDAT\n");

	for (after = 0; after < 2; after++) {
		FIMULTIBITMAP *mb;
		FIBITMAP *still;
		if (!trailing_animation(path, after)) { fail("could not write %s", path); return; }
		still = FreeImage_Load(FIF_PNG, path, 0);
		if (!still || (has_profile(still, adobe, adobe_size) ? 1 : 0) != !after) {
			fail("premise: the PNG plugin %s the iCCP %s IDAT", after ? "takes" : "drops", after ? "after" : "before");
			bad++;
		}
		if (still) FreeImage_Unload(still);
		mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, 0);
		for (page = 0; page < 2; page++) {
			FIBITMAP *dib = mb ? FreeImage_LockPage(mb, page) : NULL;
			if (!dib) { fail("frame %d did not load", page); bad++; continue; }
			if ((has_profile(dib, adobe, adobe_size) ? 1 : 0) != !after) {
				fail("iCCP %s IDAT: frame %d %s the profile", after ? "after" : "before", page, after ? "has" : "lacks");
				bad++;
			}
			if (!has_comment(dib)) {
				fail("iCCP %s IDAT: frame %d lost the tEXt after IDAT", after ? "after" : "before", page);
				bad++;
			}
			FreeImage_UnlockPage(mb, dib, FALSE);
		}
		if (mb) FreeImage_CloseMultiBitmap(mb, 0);
	}
	/* a PLTE after IDAT, which the PNG plugin refuses, is still the palette every frame needs */
	{
		static const BYTE signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
		static const BYTE palette[6] = { 0, 0, 0, 200, 50, 50 };
		BYTE ihdr[13], actl[8], zdata[4096];
		Bytes b = { NULL, 0 };
		FIMULTIBITMAP *mb;
		DWORD n;
		bytes_add(&b, signature, 8);
		put32(ihdr, 16); put32(ihdr + 4, 16);
		ihdr[8] = 8; ihdr[9] = 3; ihdr[10] = ihdr[11] = ihdr[12] = 0;
		add_chunk(&b, "IHDR", ihdr, 13);
		put32(actl, 2); put32(actl + 4, 0);
		add_chunk(&b, "acTL", actl, 8);
		add_fctl(&b, 0, 16, 16, 0, 0);
		n = image_data(zdata, sizeof(zdata), 16, 16, 1, 1);
		add_chunk(&b, "IDAT", zdata, n);
		add_chunk(&b, "PLTE", palette, 6);
		add_fctl(&b, 1, 8, 8, 4, 4);
		put32(zdata, 2);
		n = image_data(zdata + 4, sizeof(zdata) - 4, 8, 8, 1, 1);
		add_chunk(&b, "fdAT", zdata, n + 4);
		add_chunk(&b, "IEND", NULL, 0);
		if (!write_bytes(path, &b)) { fail("could not write %s", path); bad++; }
		free(b.data);
		mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, 0);
		for (page = 0; page < 2; page++) {
			FIBITMAP *dib = mb ? FreeImage_LockPage(mb, page) : NULL;
			const RGBQUAD *pal = dib ? FreeImage_GetPalette(dib) : NULL;
			BYTE index = 0;
			if (!pal || !FreeImage_GetPixelIndex(dib, 0, 0, &index) || pal[index].rgbRed != 200 || pal[index].rgbGreen != 50 || pal[index].rgbBlue != 50) {
				fail("PLTE after IDAT: frame %d did not load in its palette", page);
				bad++;
			}
			if (dib) FreeImage_UnlockPage(mb, dib, FALSE);
		}
		if (mb) FreeImage_CloseMultiBitmap(mb, 0);
	}
	if (!bad) ok("an iCCP before IDAT reaches every frame, one after it none, as in a PNG; a tEXt after IDAT and a misplaced PLTE still do");
	remove(path);
}

/* the composited canvas carries the profile and CICP tag every frame carries */
static void test_color_description(void) {
	const char *path = scratch("apng_color.png");
	const BYTE cicp[4] = { 12, 13, 0, 1 };	/* Display P3 primaries, sRGB curve */
	DWORD size = 0;
	const void *adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &size);
	FIBITMAP *frames[2];
	int i, flags;

	printf("\n=== the playback canvas keeps the file's profile and CICP tag\n");

	for (i = 0; i < 2; i++) {
		frames[i] = make_frame(40, 30, i, i * 8, 4, 255);
		FreeImage_CreateICCProfile(frames[i], (void *)adobe, (long)size);
	}
	if (!write_animation(path, frames, 2) || !insert_cicp(path, cicp)) { fail("could not write %s", path); goto out; }

	for (flags = 0; flags < 2; flags++) {
		const int load = APNG_PLAYBACK | (flags ? FIF_LOAD_NOPIXELS : 0);
		const char *what = flags ? "header-only canvas" : "canvas";
		FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, load);
		int bad = 0;
		if (!mb) { fail("cannot reopen %s", path); goto out; }
		for (i = 1; i >= 0; i--) {
			FIBITMAP *page = FreeImage_LockPage(mb, i);
			if (!page) { fail("%s %d did not load", what, i); bad++; continue; }
			if (!has_profile(page, adobe, size)) { fail("%s %d: a %u-byte profile, not Adobe RGB", what, i, (unsigned)FreeImage_GetICCProfile(page)->size); bad++; }
			if (!has_cicp(page, cicp)) { fail("%s %d: no CICP tag 12/13/0/1", what, i); bad++; }
			FreeImage_UnlockPage(mb, page, FALSE);
		}
		if (!bad) ok("every %s carries the Adobe RGB profile and the CICP tag", what);
		FreeImage_CloseMultiBitmap(mb, 0);
	}

out:
	for (i = 0; i < 2; i++) FreeImage_Unload(frames[i]);
	remove(path);
}

/* the iCCP chunk of a PNG file: 1 present, 0 absent, -1 unreadable */
static int has_iccp_chunk(const char *path) {
	FILE *f = fopen(path, "rb");
	BYTE head[8];
	int found = -1;
	if (!f) return -1;
	if (fseek(f, 8, SEEK_SET) == 0) {
		found = 0;
		while (fread(head, 1, 8, f) == 8) {
			const unsigned long length = ((unsigned long)head[0] << 24) | ((unsigned long)head[1] << 16) | ((unsigned long)head[2] << 8) | head[3];
			if (!memcmp(head + 4, "iCCP", 4)) { found = 1; break; }
			if (!memcmp(head + 4, "IEND", 4) || fseek(f, (long)length + 4, SEEK_CUR) != 0) break;
		}
	}
	fclose(f);
	return found;
}

/* the writer keeps frame 0's profile whatever its pixel format, and writes no profile an RGBA PNG cannot hold */
static void test_profile_written(void) {
	const char *path = scratch("apng_profile.png");
	DWORD adobe_size = 0, grey_size = 0;
	const void *adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
	const void *grey = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_GRAY, &grey_size);
	static const char *kinds[4] = { "24-bit", "opaque 32-bit", "RGB16", "8-bit palette" };
	int k, i, bad = 0;

	printf("\n=== the first frame's profile reaches the file\n");

	for (k = 0; k < 4; k++) {
		FIBITMAP *frames[2];
		FIMULTIBITMAP *mb;
		for (i = 0; i < 2; i++) {
			FIBITMAP *rgba = make_frame(32, 24, i, i * 8, 4, 255), *rgb = FreeImage_ConvertTo24Bits(rgba);
			frames[i] = (k == 0) ? FreeImage_Clone(rgb) : (k == 1) ? FreeImage_Clone(rgba) :
				(k == 2) ? FreeImage_ConvertToRGB16(rgb) : FreeImage_ColorQuantize(rgb, FIQ_WUQUANT);
			FreeImage_Unload(rgba);
			FreeImage_Unload(rgb);
			FreeImage_CreateICCProfile(frames[i], (void *)adobe, (long)adobe_size);
		}
		if (!write_animation(path, frames, 2)) {
			fail("%s: the animation was not written", kinds[k]); bad++;
		} else if (has_iccp_chunk(path) != 1) {
			fail("%s: no iCCP chunk", kinds[k]); bad++;
		} else if ((mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, 0)) != NULL) {
			for (i = 0; i < 2; i++) {
				FIBITMAP *page = FreeImage_LockPage(mb, i);
				if (!page || !has_profile(page, adobe, adobe_size)) { fail("%s: frame %d came back without Adobe RGB", kinds[k], i); bad++; }
				if (page) FreeImage_UnlockPage(mb, page, FALSE);
			}
			FreeImage_CloseMultiBitmap(mb, 0);
		} else {
			fail("%s: the animation does not open", kinds[k]); bad++;
		}
		for (i = 0; i < 2; i++) FreeImage_Unload(frames[i]);
		remove(path);
	}
	if (!bad) ok("24-bit, opaque 32-bit, RGB16 and palette animations keep the Adobe RGB profile");

	/* a grey profile on RGBA frames: the 8-bit grey pages of an animation, and a tagged page saved alone */
	bad = 0;
	{
		FIBITMAP *frames[2], *page;
		LONG ms = 100;
		for (i = 0; i < 2; i++) {
			FIBITMAP *rgba = make_frame(32, 24, i, i * 8, 4, 255);
			frames[i] = FreeImage_ConvertToGreyscale(rgba);
			FreeImage_Unload(rgba);
			FreeImage_CreateICCProfile(frames[i], (void *)grey, (long)grey_size);
		}
		if (!write_animation(path, frames, 2)) { fail("grey frames: the animation was not written"); bad++; }
		else if (has_iccp_chunk(path) != 0) { fail("grey frames: an iCCP chunk the RGBA file cannot hold"); bad++; }
		remove(path);
		for (i = 0; i < 2; i++) FreeImage_Unload(frames[i]);

		page = make_frame(32, 24, 0, -1, 0, 255);
		FreeImage_CreateICCProfile(page, (void *)grey, (long)grey_size);
		set_tag(page, "FrameTime", 0, FIDT_LONG, 4, &ms);
		if (!FreeImage_Save(FIF_APNG, page, path, 0)) { fail("a tagged page with a grey profile: not saved"); bad++; }
		else if (has_iccp_chunk(path) != 0) { fail("a tagged page with a grey profile: an iCCP chunk the RGBA file cannot hold"); bad++; }
		remove(path);
		FreeImage_Unload(page);
	}
	if (!bad) ok("a grey profile is left out of the RGBA file, and the save succeeds");
}

/* the writer's conversions */
#define CONVERT_FLAGS (FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION)

enum { K_RGBA, K_GREY, K_RGB16, K_UINT16 };
enum { SAME, CONVERTED, TWIN };

typedef struct {
	int kind;
	const void *profile;
	DWORD size;
	int expect;		/* SAME: its own colors; CONVERTED: the file's; TWIN: its own, which a conversion would change */
} FrameSpec;

static DWORD be32(const BYTE *p) {
	return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | p[3];
}

/* a built-in profile with the values of one XYZ tag added to (add) or replaced */
static BYTE *patched_profile(int builtin, const char *sig, const DWORD xyz[3], int add, DWORD *size) {
	const BYTE *src = (const BYTE *)FreeImage_GetBuiltInICCProfile(builtin, size);
	BYTE *p = src ? (BYTE *)malloc(*size) : NULL;
	DWORD n, i, k;
	if (!p) return NULL;
	memcpy(p, src, *size);
	n = be32(p + 128);
	for (i = 0; i < n && 132 + 12 * i + 12 <= *size; i++) {
		const BYTE *entry = p + 132 + 12 * i;
		if (!memcmp(entry, sig, 4) && be32(entry + 4) + 20 <= *size) {
			for (k = 0; k < 3; k++) {
				BYTE *x = p + be32(entry + 4) + 8 + 4 * k;
				const DWORD v = add ? be32(x) + xyz[k] : xyz[k];
				x[0] = (BYTE)(v >> 24); x[1] = (BYTE)(v >> 16); x[2] = (BYTE)(v >> 8); x[3] = (BYTE)v;
			}
			return p;
		}
	}
	free(p);
	return NULL;
}

/* a 64x48 frame of this kind spanning red and green, translucent when it has alpha */
static FIBITMAP *make_kind(int kind, int seed, const void *profile, DWORD size) {
	FIBITMAP *rgba = FreeImage_Allocate(64, 48, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK), *dib = NULL, *tmp;
	unsigned x, y;
	if (!rgba) return NULL;
	for (y = 0; y < 48; y++) {
		BYTE *line = FreeImage_GetScanLine(rgba, y);
		for (x = 0; x < 64; x++) {
			line[FI_RGBA_RED] = (BYTE)(x * 4);
			line[FI_RGBA_GREEN] = (BYTE)(y * 5);
			line[FI_RGBA_BLUE] = (BYTE)(x * y + seed * 20);
			line[FI_RGBA_ALPHA] = (BYTE)(200 + ((x + y) & 0x3F));
			line += 4;
		}
	}
	switch (kind) {
		case K_RGBA: dib = FreeImage_Clone(rgba); break;
		case K_GREY: dib = FreeImage_ConvertToGreyscale(rgba); break;
		case K_RGB16:
			tmp = FreeImage_ConvertTo24Bits(rgba);
			dib = tmp ? FreeImage_ConvertToRGB16(tmp) : NULL;
			if (tmp) FreeImage_Unload(tmp);
			break;
		case K_UINT16:
			tmp = FreeImage_ConvertToGreyscale(rgba);
			dib = tmp ? FreeImage_ConvertToUINT16(tmp) : NULL;
			if (tmp) FreeImage_Unload(tmp);
			break;
	}
	FreeImage_Unload(rgba);
	if (dib && profile) FreeImage_CreateICCProfile(dib, (void *)profile, (long)size);
	return dib;
}

/* the frame in a profile's colors (NULL: sRGB) as 8-bit RGBA, by the writer's steps */
static FIBITMAP *in_colors(FIBITMAP *frame, const void *profile, DWORD size) {
	FIBITMAP *src = frame, *standard = NULL, *converted, *out = NULL;
	if (FreeImage_GetImageType(frame) == FIT_UINT16) {
		FIICCPROFILE *icc = FreeImage_GetICCProfile(frame);
		standard = FreeImage_ConvertToStandardType(frame, TRUE);
		if (!standard) return NULL;
		if (icc->data) FreeImage_CreateICCProfile(standard, icc->data, (long)icc->size);
		src = standard;
	}
	converted = FreeImage_ConvertToICCProfile(src, profile, size, CONVERT_FLAGS);
	if (converted) {
		out = FreeImage_ConvertTo32Bits(converted);
		FreeImage_Unload(converted);
	}
	if (standard) FreeImage_Unload(standard);
	return out;
}

/* written and composited back, every frame has the colors expected and carries the file's profile */
static int check_mixed(const char *what, const FrameSpec *spec, int n, const void *file_profile, DWORD file_size) {
	const char *path = scratch("apng_mixed.png");
	FIBITMAP *frames[10];
	FIMULTIBITMAP *mb;
	int i, bad = 0;

	for (i = 0; i < n; i++) frames[i] = make_kind(spec[i].kind, i, spec[i].profile, spec[i].size);
	if (!write_animation(path, frames, n)) { fail("%s: the animation was not written", what); bad++; goto out; }
	if (has_iccp_chunk(path) != (file_profile ? 1 : 0)) { fail("%s: %s iCCP chunk", what, file_profile ? "no" : "an"); bad++; }
	mb = FreeImage_OpenMultiBitmap(FIF_APNG, path, FALSE, TRUE, FALSE, APNG_PLAYBACK);
	if (!mb) { fail("%s: the animation does not open", what); bad++; goto out; }
	for (i = 0; i < n; i++) {
		FIBITMAP *page = FreeImage_LockPage(mb, i);
		FIBITMAP *own = in_colors(frames[i], spec[i].profile, spec[i].size);
		FIBITMAP *file = in_colors(frames[i], file_profile, file_size);
		FIBITMAP *want = (spec[i].expect == CONVERTED) ? file : own;
		if (!page || !own || !file) {
			fail("%s: frame %d did not load or convert", what, i); bad++;
		} else {
			int diff = pixel_diff(page, want);
			if (diff) { fail("%s: frame %d, %d pixels not in the %s colors", what, i, diff, (spec[i].expect == CONVERTED) ? "file's" : "frame's own"); bad++; }
			if (spec[i].expect != SAME && !pixel_diff(own, file)) { fail("%s: frame %d looks the same in both profiles: the check proves nothing", what, i); bad++; }
			if (file_profile ? !has_profile(page, file_profile, file_size) : (FreeImage_GetICCProfile(page)->data != NULL)) {
				fail("%s: frame %d does not carry the file's profile", what, i); bad++;
			}
		}
		if (own) FreeImage_Unload(own);
		if (file) FreeImage_Unload(file);
		if (page) FreeImage_UnlockPage(mb, page, FALSE);
	}
	FreeImage_CloseMultiBitmap(mb, 0);

out:
	for (i = 0; i < n; i++) if (frames[i]) FreeImage_Unload(frames[i]);
	remove(path);
	return bad;
}

/* one profile for the file, frame 0's: the other frames are converted to it, an untagged frame being sRGB */
static void test_mixed_profiles(void) {
	DWORD adobe_size = 0, p3_size = 0, srgb_size = 0, grey_size = 0, linear_size = 0, prophoto_size = 0, twin_size = 0;
	const void *adobe = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
	const void *p3 = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_DISPLAY_P3, &p3_size);
	const void *srgb = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_SRGB, &srgb_size);
	const void *grey = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_GRAY, &grey_size);
	const void *linear = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_LINEAR_GRAY, &linear_size);
	const void *prophoto = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_PROPHOTO_RGB, &prophoto_size);
	DWORD white_size = 0;
	/* sRGB with the red primary nudged: within FreeImage's sRGB tolerance, yet a conversion changes pixels */
	static const DWORD nudge[3] = { 0x20, 0, 0 };
	/* Display P3 with a D65 media white: only an absolute colorimetric conversion would see it */
	static const DWORD d65[3] = { 0xF353, 0x10000, 0x116C8 };
	BYTE *twin = patched_profile(FICMS_PROFILE_SRGB, "rXYZ", nudge, 1, &twin_size);
	BYTE *white = patched_profile(FICMS_PROFILE_DISPLAY_P3, "wtpt", d65, 0, &white_size);

	printf("\n=== frames with different profiles are converted to the file's\n");

	if (!adobe || !p3 || !srgb || !grey || !linear || !prophoto || !twin || !white) {
		fail("the built-in profiles are not available");
		free(twin);
		free(white);
		return;
	}
	{
		const FrameSpec spec[9] = {
			{ K_RGBA, adobe, adobe_size, SAME },
			{ K_RGBA, p3, p3_size, CONVERTED },
			{ K_RGBA, NULL, 0, CONVERTED },
			{ K_RGBA, twin, twin_size, CONVERTED },
			{ K_RGBA, adobe, adobe_size, SAME },
			{ K_GREY, linear, linear_size, CONVERTED },
			{ K_RGB16, prophoto, prophoto_size, CONVERTED },
			{ K_UINT16, linear, linear_size, CONVERTED },
			{ K_RGBA, white, white_size, CONVERTED }
		};
		if (!check_mixed("Adobe RGB file", spec, 9, adobe, adobe_size))
			ok("Display P3, untagged, sRGB-like, linear grey, ProPhoto RGB16, UINT16 and D65-white frames take the first frame's Adobe RGB");
	}
	{
		const FrameSpec spec[5] = {
			{ K_RGBA, NULL, 0, SAME },
			{ K_RGBA, twin, twin_size, TWIN },
			{ K_GREY, grey, grey_size, SAME },
			{ K_RGBA, p3, p3_size, CONVERTED },
			{ K_RGBA, srgb, srgb_size, SAME }
		};
		if (!check_mixed("sRGB file", spec, 5, NULL, 0))
			ok("after an untagged first frame, sRGB and its look-alikes keep their pixels and Display P3 becomes sRGB");
	}
	{
		const FrameSpec spec[3] = {
			{ K_RGBA, twin, twin_size, SAME },
			{ K_RGBA, NULL, 0, TWIN },
			{ K_RGBA, adobe, adobe_size, CONVERTED }
		};
		if (!check_mixed("sRGB-like file", spec, 3, twin, twin_size))
			ok("a file whose profile is sRGB in all but name keeps untagged frames as they are");
	}
	{
		const FrameSpec spec[3] = {
			{ K_GREY, linear, linear_size, CONVERTED },
			{ K_RGBA, adobe, adobe_size, CONVERTED },
			{ K_RGBA, NULL, 0, SAME }
		};
		if (!check_mixed("grey-profile first frame", spec, 3, NULL, 0))
			ok("a first frame whose profile the RGBA file cannot hold makes an sRGB file, itself converted");
	}
	free(twin);
	free(white);

	/* a tagged CMYK page saved alone: RGB, not its inks as RGBA */
	{
		const char *path = scratch("apng_cmyk.png");
		FIBITMAP *cmyk = FreeImage_Allocate(40, 30, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK), *page = NULL, *want = NULL;
		LONG ms = 100;
		unsigned x, y;
		int diff = -1;
		if (cmyk) {
			FreeImage_GetICCProfile(cmyk)->flags |= FIICC_COLOR_IS_CMYK;
			for (y = 0; y < 30; y++) {
				BYTE *line = FreeImage_GetScanLine(cmyk, y);
				for (x = 0; x < 40; x++) {
					line[0] = (BYTE)(x * 6); line[1] = (BYTE)(y * 8); line[2] = (BYTE)((x + y) * 3); line[3] = (BYTE)(x * y / 8);
					line += 4;
				}
			}
			set_tag(cmyk, "FrameTime", 0, FIDT_LONG, 4, &ms);
			if (FreeImage_Save(FIF_APNG, cmyk, path, 0)) page = FreeImage_Load(FIF_APNG, path, 0);
			want = in_colors(cmyk, NULL, 0);
			if (page && want) diff = pixel_diff(page, want);
		}
		if (diff) fail("a CMYK page: %d pixels not converted to sRGB", diff);
		else ok("a CMYK page is converted to sRGB");
		if (page) FreeImage_Unload(page);
		if (want) FreeImage_Unload(want);
		if (cmyk) FreeImage_Unload(cmyk);
		remove(path);
	}
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

/* Save() refuses what it cannot write, before Close() writes anything */
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

	/* FreeImage_SaveMultiBitmapToHandle does not pre-check header-only pages */
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
	test_color_description();
	test_grey16_playback();
	test_trailing_chunks();
	test_profile_written();
	test_mixed_profiles();
	test_refusals();
	test_size();

	printf("\n%d check(s) passed, %d failure(s)\n", checks, failures);
	FreeImage_DeInitialise();
	return failures ? 1 : 0;
}
