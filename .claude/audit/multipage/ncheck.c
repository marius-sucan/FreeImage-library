/* Verification harness for the "by inspection" findings N3, N5, N8, N10, N12, N15
   of AUDIT-MULTIPAGE.md - are they real, and can they be reproduced?
   N7 has its own fault-injection shim (renamefail.c) and N16 its own sample
   generator (mkpsd_exif3.py). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static int g_quiet = 0;

static void msg(FREE_IMAGE_FORMAT fif, const char *m) {
	if (!g_quiet) {
		printf("      [FI] %s: %s\n", fif == FIF_UNKNOWN ? "?" : FreeImage_GetFormatFromFIF(fif), m);
	}
}

static FIBITMAP *page(int v, int w, int h, int bpp) {
	FIBITMAP *d = FreeImage_Allocate(w, h, bpp, 0, 0, 0);
	int y;
	if (bpp == 8) {
		RGBQUAD *p = FreeImage_GetPalette(d);
		int i;
		for (i = 0; i < 256; i++) { p[i].rgbRed = p[i].rgbGreen = p[i].rgbBlue = (BYTE)i; }
	}
	for (y = 0; y < h; y++) memset(FreeImage_GetScanLine(d, y), v, FreeImage_GetLine(d));
	return d;
}

static int firstpix(FIBITMAP *d) {
	if (!d) return -1;
	return FreeImage_GetScanLine(d, 0)[0];
}

/* ---- FIMD_ANIMATION helpers (N12) ---- */

/* Each FIMD_ANIMATION tag has one type the plugins will accept:
   FreeImage_GetMetadataEx() filters on it, so a "FrameLeft" written as FIDT_LONG is
   simply not seen by the GIF writer, which asks for FIDT_SHORT. Write each one the
   way PluginGIF.cpp and PluginAPNG.cpp emit it. */
static void set_anim(FIBITMAP *d, const char *key, LONG value, FREE_IMAGE_MDTYPE type) {
	FITAG *tag = FreeImage_CreateTag();
	BYTE  b = (BYTE)value;
	WORD  w = (WORD)value;
	DWORD l = (DWORD)value;
	const void *v = &l;
	DWORD len = 4;

	if (!tag) return;
	if (type == FIDT_BYTE)  { v = &b; len = 1; }
	if (type == FIDT_SHORT) { v = &w; len = 2; }

	FreeImage_SetTagKey(tag, key);
	FreeImage_SetTagType(tag, type);
	FreeImage_SetTagCount(tag, 1);
	FreeImage_SetTagLength(tag, len);
	FreeImage_SetTagValue(tag, (void*)v);
	FreeImage_SetMetadata(FIMD_ANIMATION, d, key, tag);
	FreeImage_DeleteTag(tag);
}

static void set_anim_long(FIBITMAP *d, const char *key, LONG value) {
	set_anim(d, key, value, FIDT_LONG);
}

static long get_anim(FIBITMAP *d, const char *key) {
	FITAG *tag = NULL;
	if (!d) return -1;
	if (!FreeImage_GetMetadata(FIMD_ANIMATION, d, key, &tag) || !tag) return -1;
	switch (FreeImage_GetTagType(tag)) {
		case FIDT_LONG:  return (long)*(DWORD*)FreeImage_GetTagValue(tag);
		case FIDT_SHORT: return (long)*(WORD*)FreeImage_GetTagValue(tag);
		case FIDT_BYTE:  return (long)*(BYTE*)FreeImage_GetTagValue(tag);
		default: return -2;
	}
}

static int mkfile(FREE_IMAGE_FORMAT fif, const char *fn, int n, int bpp) {
	FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(fif, fn, TRUE, FALSE, TRUE, 0);
	int k;
	if (!m) return 1;
	for (k = 0; k < n; k++) {
		FIBITMAP *d = page((k + 1) * 20, 16, 16, bpp);
		FreeImage_AppendPage(m, d);
		FreeImage_Unload(d);
	}
	return FreeImage_CloseMultiBitmap(m, 0) ? 0 : 1;
}

/* =================== N12 =================== */
/* Every page goes through SaveToMemory(cache_fif, ..., 0) on its way into the
   cache and LoadFromMemory on its way out. Does a frame's FIMD_ANIMATION survive
   that trip? If it does not, building an animation with AppendPage loses the frame
   timings, which is the whole point of an animation. */
static int n12(FREE_IMAGE_FORMAT fif, const char *fn, int bpp, int even_offsets) {
	/* every FIMD_ANIMATION tag the GIF and APNG writers understand for a frame */
	static const char *keys[] = { "FrameTime", "FrameLeft", "FrameTop", "DisposalMethod" };
	static const FREE_IMAGE_MDTYPE types[] = { FIDT_LONG, FIDT_SHORT, FIDT_SHORT, FIDT_BYTE };
	static const LONG odd[4][3] = {
		{ 120, 340, 560 },   /* FrameTime      */
		{ 0, 3, 5 },         /* FrameLeft      */
		{ 0, 2, 4 },         /* FrameTop       */
		{ 1, 2, 1 }          /* DisposalMethod */
	};
	/* WebP stores frame offsets in even pixels only (the mux snaps with offset &= ~1),
	   so an odd offset is not a faithful round trip to ask for */
	static const LONG even[4][3] = {
		{ 120, 340, 560 },
		{ 0, 4, 8 },
		{ 0, 2, 4 },
		{ 1, 2, 1 }
	};
	const LONG (*vals)[3] = even_offsets ? even : odd;
	const unsigned nkeys = sizeof(keys) / sizeof(keys[0]);
	FIMULTIBITMAP *m;
	unsigned t;
	int k, bad = 0;

	m = FreeImage_OpenMultiBitmap(fif, fn, TRUE, FALSE, TRUE, 0);
	if (!m) { printf("    %-6s open failed\n", FreeImage_GetFormatFromFIF(fif)); return 1; }
	for (k = 0; k < 3; k++) {
		/* a canvas bigger than the frame, so FrameLeft/FrameTop mean something */
		FIBITMAP *d = page((k + 1) * 20, 16, 16, bpp);
		set_anim(d, "LogicalWidth", 32, FIDT_SHORT);
		set_anim(d, "LogicalHeight", 32, FIDT_SHORT);
		for (t = 0; t < nkeys; t++) set_anim(d, keys[t], vals[t][k], types[t]);
		FreeImage_AppendPage(m, d);
		FreeImage_Unload(d);
	}
	FreeImage_CloseMultiBitmap(m, 0);

	m = FreeImage_OpenMultiBitmap(fif, fn, FALSE, TRUE, TRUE, 0);
	if (!m) { printf("    %-6s reopen failed\n", FreeImage_GetFormatFromFIF(fif)); return 1; }
	for (t = 0; t < nkeys; t++) {
		int lost = 0;
		printf("    %-6s %-15s in [%3ld %3ld %3ld]  out [",
		       FreeImage_GetFormatFromFIF(fif), keys[t],
		       (long)vals[t][0], (long)vals[t][1], (long)vals[t][2]);
		for (k = 0; k < 3; k++) {
			FIBITMAP *d = FreeImage_LockPage(m, k);
			long got = get_anim(d, keys[t]);
			printf("%s%3ld", k ? " " : "", got);
			if (got != (long)vals[t][k]) lost++;
			if (d) FreeImage_UnlockPage(m, d, FALSE);
		}
		printf("]  %s\n", lost ? "*** DIFFERS ***" : "same");
		bad += lost;
	}
	FreeImage_CloseMultiBitmap(m, 0);
	return bad;
}

/* =================== N5 =================== */
/* LockPage keeps a decoder open for the life of the multi-bitmap (f3ed0f7), while
   the public SaveMultiBitmapTo* open a second one on the same handle. Does the
   first one still work afterwards? */
static int n5(FREE_IMAGE_FORMAT fif, const char *fn, int bpp) {
	FIMULTIBITMAP *m;
	FIBITMAP *a, *b, *c;
	FIMEMORY *mem;
	int bad = 0, s1, s2;

	if (mkfile(fif, fn, 4, bpp)) { printf("    %-6s build failed\n", FreeImage_GetFormatFromFIF(fif)); return 1; }

	m = FreeImage_OpenMultiBitmap(fif, fn, FALSE, TRUE, TRUE, 0);
	if (!m) return 1;

	a = FreeImage_LockPage(m, 0);
	if (firstpix(a) != 20) bad++;

	mem = FreeImage_OpenMemory(NULL, 0);
	s1 = FreeImage_SaveMultiBitmapToMemory(fif, m, mem, 0);
	FreeImage_CloseMemory(mem);

	/* the decoder LockPage opened is still the one used here */
	b = FreeImage_LockPage(m, 1);
	if (firstpix(b) != 40) bad++;

	/* and again, with two saves around it */
	mem = FreeImage_OpenMemory(NULL, 0);
	s2 = FreeImage_SaveMultiBitmapToMemory(fif, m, mem, 0);
	FreeImage_CloseMemory(mem);

	c = FreeImage_LockPage(m, 3);
	if (firstpix(c) != 80) bad++;

	printf("    %-6s lock(0)=%d save=%d lock(1)=%d save=%d lock(3)=%d  %s\n",
	       FreeImage_GetFormatFromFIF(fif), firstpix(a), s1, firstpix(b), s2, firstpix(c),
	       bad ? "*** WRONG ***" : "ok");

	if (a) FreeImage_UnlockPage(m, a, FALSE);
	if (b) FreeImage_UnlockPage(m, b, FALSE);
	if (c) FreeImage_UnlockPage(m, c, FALSE);
	FreeImage_CloseMultiBitmap(m, 0);
	return bad;
}

/* Is it the cache round trip that loses them? This is exactly what
   FreeImage_SavePageToBlock() and FreeImage_UnlockPage() do to every page:
   SaveToMemory(cache_fif, dib, hmem, 0) on the way in, LoadFromMemory on the way
   out. Nothing else is involved. */
static int n12_isolate(FREE_IMAGE_FORMAT fif, int bpp, int even_offsets) {
	static const char *keys[] = { "FrameTime", "FrameLeft", "FrameTop", "DisposalMethod" };
	static const FREE_IMAGE_MDTYPE types[] = { FIDT_LONG, FIDT_SHORT, FIDT_SHORT, FIDT_BYTE };
	/* WebP snaps odd frame offsets to even, so ask it for an even one */
	static const LONG odd[] = { 340, 3, 2, 2 };
	static const LONG even[] = { 340, 4, 2, 2 };
	const LONG *vals = even_offsets ? even : odd;
	const unsigned nkeys = sizeof(keys) / sizeof(keys[0]);
	FIBITMAP *d = page(60, 16, 16, bpp);
	FIMEMORY *hmem;
	FIBITMAP *back;
	unsigned t;
	int bad = 0;

	set_anim(d, "LogicalWidth", 32, FIDT_SHORT);
	set_anim(d, "LogicalHeight", 32, FIDT_SHORT);
	for (t = 0; t < nkeys; t++) set_anim(d, keys[t], vals[t], types[t]);

	hmem = FreeImage_OpenMemory(NULL, 0);
	if (!FreeImage_SaveToMemory(fif, d, hmem, 0)) {
		printf("    %-6s SaveToMemory failed\n", FreeImage_GetFormatFromFIF(fif));
		FreeImage_CloseMemory(hmem); FreeImage_Unload(d);
		return 1;
	}
	FreeImage_SeekMemory(hmem, 0, SEEK_SET);
	back = FreeImage_LoadFromMemory(fif, hmem, 0);
	if (!back) {
		printf("    %-6s LoadFromMemory failed\n", FreeImage_GetFormatFromFIF(fif));
		FreeImage_CloseMemory(hmem); FreeImage_Unload(d);
		return 1;
	}

	printf("    %-6s one SaveToMemory+LoadFromMemory: ", FreeImage_GetFormatFromFIF(fif));
	for (t = 0; t < nkeys; t++) {
		long got = get_anim(back, keys[t]);
		printf("%s%s %ld->%ld", t ? ", " : "", keys[t], (long)vals[t], got);
		if (got != (long)vals[t]) bad++;
	}
	printf("  %s\n", bad ? "*** the cache trip loses them ***" : "all preserved");

	FreeImage_Unload(back);
	FreeImage_CloseMemory(hmem);
	FreeImage_Unload(d);
	return bad;
}

int main(int argc, char **argv) {
	const char *cmd = argc > 1 ? argv[1] : "all";

	FreeImage_Initialise(FALSE);
	FreeImage_SetOutputMessage(msg);
	setvbuf(stdout, NULL, _IONBF, 0);

	if (!strcmp(cmd, "all") || !strcmp(cmd, "n12")) {
		printf("N12 - does a frame's FIMD_ANIMATION survive the cache round trip?\n");
		n12(FIF_GIF,  "n12.gif", 8, 0);
		n12(FIF_APNG, "n12.png", 8, 0);
		n12(FIF_WEBP, "n12.webp", 24, 1);
		printf("  isolating the cache round trip on its own:\n");
		n12_isolate(FIF_GIF, 8, 0);
		n12_isolate(FIF_APNG, 8, 0);
		n12_isolate(FIF_WEBP, 24, 1);   /* WebP has no palette, and wants even offsets */
		printf("\n");
	}

	if (!strcmp(cmd, "all") || !strcmp(cmd, "n5")) {
		printf("N5 - a second decoder opened on a handle that already has one\n");
		n5(FIF_TIFF, "n5.tif", 8);
		n5(FIF_GIF,  "n5.gif", 8);
		n5(FIF_ICO,  "n5.ico", 8);
		n5(FIF_APNG, "n5.png", 8);
		printf("\n");
	}

	if (!strcmp(cmd, "all") || !strcmp(cmd, "n10")) {
		printf("N10 - a file opened as the wrong format\n");
		mkfile(FIF_GIF, "n10.gif", 2, 8);
		{
			static const int fifs[] = { FIF_TIFF, FIF_ICO, FIF_APNG, FIF_WEBP };
			unsigned i;
			for (i = 0; i < sizeof(fifs)/sizeof(fifs[0]); i++) {
				FREE_IMAGE_FORMAT f = (FREE_IMAGE_FORMAT)fifs[i];
				FIMULTIBITMAP *m;
				g_quiet = 1;
				m = FreeImage_OpenMultiBitmap(f, "n10.gif", FALSE, TRUE, TRUE, 0);
				g_quiet = 0;
				if (!m) {
					printf("    a GIF opened as %-5s -> NULL (refused)\n", FreeImage_GetFormatFromFIF(f));
				} else {
					int n = FreeImage_GetPageCount(m);
					FIBITMAP *d;
					g_quiet = 1;
					d = FreeImage_LockPage(m, 0);
					g_quiet = 0;
					printf("    a GIF opened as %-5s -> ACCEPTED, GetPageCount=%d, LockPage(0)=%s\n",
					       FreeImage_GetFormatFromFIF(f), n, d ? "a page" : "NULL");
					if (d) FreeImage_UnlockPage(m, d, FALSE);
					FreeImage_CloseMultiBitmap(m, 0);
				}
			}
		}
		printf("\n");
	}

	if (!strcmp(cmd, "all") || !strcmp(cmd, "n15")) {
		printf("N15 - ICO's PageCount when its Open failed\n");
		mkfile(FIF_GIF, "n15.gif", 2, 8);
		{
			FIMULTIBITMAP *m;
			g_quiet = 1;
			m = FreeImage_OpenMultiBitmap(FIF_ICO, "n15.gif", FALSE, TRUE, TRUE, 0);
			g_quiet = 0;
			if (!m) {
				printf("    a GIF opened as ICO -> NULL (refused)\n");
			} else {
				printf("    a GIF opened as ICO -> ACCEPTED, GetPageCount=%d"
				       " (the other six plugins return 0 here)\n", FreeImage_GetPageCount(m));
				FreeImage_CloseMultiBitmap(m, 0);
			}
		}
		printf("\n");
	}

	if (!strcmp(cmd, "canvas")) {
		/* The canvas of an animation is declared by its first page alone (LogicalWidth
		   /LogicalHeight), which is how GIF does it too. So what happens to the canvas
		   when the first page is deleted? */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		const char *fn = argv[3];
		FIMULTIBITMAP *m;
		int k;

		m = FreeImage_OpenMultiBitmap(fif, fn, TRUE, FALSE, TRUE, 0);
		if (!m) { printf("    open failed\n"); goto canvasdone; }
		for (k = 0; k < 3; k++) {
			/* 16x16 frames near the top left of a 64x48 canvas */
			FIBITMAP *d = page((k + 1) * 60, 16, 16, 24);
			if (k == 0) {
				set_anim(d, "LogicalWidth", 64, FIDT_SHORT);
				set_anim(d, "LogicalHeight", 48, FIDT_SHORT);
				set_anim(d, "Loop", 3, FIDT_LONG);
			}
			set_anim(d, "FrameTime", 100, FIDT_LONG);
			set_anim(d, "FrameLeft", (LONG)(k * 2), FIDT_SHORT);
			set_anim(d, "FrameTop", (LONG)(k * 2), FIDT_SHORT);
			FreeImage_AppendPage(m, d);
			FreeImage_Unload(d);
		}
		FreeImage_CloseMultiBitmap(m, WEBP_LOSSLESS);

		m = FreeImage_OpenMultiBitmap(fif, fn, FALSE, TRUE, TRUE, 0);
		if (m) {
			FIBITMAP *d = FreeImage_LockPage(m, 0);
			printf("    as written    : %d frame(s), canvas %ldx%ld (asked for 64x48)\n",
			       FreeImage_GetPageCount(m),
			       get_anim(d, "LogicalWidth"), get_anim(d, "LogicalHeight"));
			if (d) FreeImage_UnlockPage(m, d, FALSE);
			FreeImage_CloseMultiBitmap(m, 0);
		}

		/* now delete the page that carried the canvas, and write it again */
		m = FreeImage_OpenMultiBitmap(fif, fn, FALSE, FALSE, TRUE, 0);
		if (m) {
			FreeImage_DeletePage(m, 0);
			FreeImage_CloseMultiBitmap(m, WEBP_LOSSLESS);
		}
		m = FreeImage_OpenMultiBitmap(fif, fn, FALSE, TRUE, TRUE, 0);
		if (m) {
			FIBITMAP *d = FreeImage_LockPage(m, 0);
			printf("    after deleting the page that declared it: %d frame(s), canvas %ldx%ld\n",
			       FreeImage_GetPageCount(m),
			       get_anim(d, "LogicalWidth"), get_anim(d, "LogicalHeight"));
			if (d) FreeImage_UnlockPage(m, d, FALSE);
			FreeImage_CloseMultiBitmap(m, 0);
		}
	canvasdone:
		printf("\n");
	}

	if (!strcmp(cmd, "anim")) {
		/* Walk an animation with the multi-page API and report what each frame says,
		   then rewrite it through the API and report again. Used with webpanim.py,
		   whose files come from a different encoder entirely. */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		const char *in = argv[3];
		const char *out = argc > 4 ? argv[4] : NULL;
		FIMULTIBITMAP *m;
		int k, n;

		m = FreeImage_OpenMultiBitmap(fif, in, FALSE, TRUE, TRUE, 0);
		if (!m) { printf("    open %s failed\n", in); n = -1; }
		if (m) {
		n = FreeImage_GetPageCount(m);
		printf("    %s: %d frame(s)\n", in, n);
		for (k = 0; k < n; k++) {
			FIBITMAP *d = FreeImage_LockPage(m, k);
			printf("      frame %d: %ux%u time=%ld left=%ld top=%ld disposal=%ld blend=%ld",
			       k, d ? FreeImage_GetWidth(d) : 0, d ? FreeImage_GetHeight(d) : 0,
			       get_anim(d, "FrameTime"), get_anim(d, "FrameLeft"), get_anim(d, "FrameTop"),
			       get_anim(d, "DisposalMethod"), get_anim(d, "BlendMethod"));
			if (k == 0) {
				printf(" canvas=%ldx%ld loop=%ld",
				       get_anim(d, "LogicalWidth"), get_anim(d, "LogicalHeight"), get_anim(d, "Loop"));
			}
			printf("\n");
			if (d) FreeImage_UnlockPage(m, d, FALSE);
		}
		/* Read it once more with WEBP_PLAYBACK. That path goes through libwebp's
		   demuxer and animation decoder rather than the mux, so it only works if the
		   ANIM and ANMF chunks are properly formed, and it returns composited frames
		   the size of the canvas. */
		{
			FIMULTIBITMAP *pb = FreeImage_OpenMultiBitmap(fif, in, FALSE, TRUE, TRUE, WEBP_PLAYBACK);
			if (pb != NULL) {
				int pn = FreeImage_GetPageCount(pb);
				FIBITMAP *d = FreeImage_LockPage(pb, pn - 1);
				if (d != NULL) {
					BYTE *px = FreeImage_GetScanLine(d, FreeImage_GetHeight(d) - 1);
					printf("      WEBP_PLAYBACK: %d frame(s); last composited to %ux%u %ubpp, top-left RGB=(%d,%d,%d)\n",
					       pn, FreeImage_GetWidth(d), FreeImage_GetHeight(d), FreeImage_GetBPP(d),
					       px[FI_RGBA_RED], px[FI_RGBA_GREEN], px[FI_RGBA_BLUE]);
					FreeImage_UnlockPage(pb, d, FALSE);
				} else {
					printf("      WEBP_PLAYBACK: could not composite\n");
				}
				FreeImage_CloseMultiBitmap(pb, 0);
			} else {
				printf("      WEBP_PLAYBACK: open failed\n");
			}
		}

		if (out != NULL) {
			/* rewrite it, frame for frame, through SaveMultiBitmapToHandle */
			FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
			BYTE *bytes = NULL;
			DWORD size = 0;
			int rc = FreeImage_SaveMultiBitmapToMemory(fif, m, mem, WEBP_LOSSLESS);
			FILE *f;
			FreeImage_AcquireMemory(mem, &bytes, &size);
			f = fopen(out, "wb");
			if (f) { fwrite(bytes, 1, size, f); fclose(f); }
			printf("    rewrote -> %s (%u bytes, save=%d)\n", out, (unsigned)size, rc);
			FreeImage_CloseMemory(mem);
		}
		FreeImage_CloseMultiBitmap(m, 0);
		}
		printf("\n");
	}

	if (!strcmp(cmd, "n16")) {
		/* load whatever file was named; the point is whether it comes back, fails
		   cleanly, or takes the process down through an assert */
		const char *fn = argc > 2 ? argv[2] : "exif3.psd";
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)(argc > 3 ? atoi(argv[3]) : FIF_PSD);
		FIBITMAP *d;
		printf("    FreeImage_Load(%s, \"%s\") ...\n", FreeImage_GetFormatFromFIF(fif), fn);
		d = FreeImage_Load(fif, fn, 0);
		printf("    -> %s\n", d ? "loaded" : "NULL (refused, which is fine)");
		if (d) FreeImage_Unload(d);
		printf("\n");
	}

	if (!strcmp(cmd, "n7prep")) {
		/* built WITHOUT the preload, so the file really exists */
		remove("n7.tif");
		mkfile(FIF_TIFF, "n7.tif", 3, 8);
		{
			FILE *f = fopen("n7.tif", "rb");
			long sz = 0;
			if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
			printf("    prepared n7.tif: %ld bytes\n", sz);
		}
	}

	if (!strcmp(cmd, "n7edit")) {
		/* run WITH renamefail.so, so the rename of the spool over the original
		   fails. Does the original survive? */
		FILE *f;
		FIMULTIBITMAP *m;
		int rc;
		long sz = 0;

		f = fopen("n7.tif", "rb");
		if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
		printf("    before: n7.tif %s (%ld bytes)\n", sz ? "exists" : "MISSING", sz);

		m = FreeImage_OpenMultiBitmap(FIF_TIFF, "n7.tif", FALSE, FALSE, TRUE, 0);
		if (!m) { printf("    open failed\n"); goto n7done; }
		FreeImage_DeletePage(m, 0);             /* give Close something to write */
		rc = FreeImage_CloseMultiBitmap(m, 0);  /* spool, then remove + rename */
		printf("    CloseMultiBitmap -> %d\n", rc);

		sz = 0;
		f = fopen("n7.tif", "rb");
		if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); }
		printf("    after : n7.tif %s%s\n",
		       sz ? "still there" : "*** GONE ***",
		       sz ? "" : " - the rename failed and the original had already been removed");
	n7done:
		printf("\n");
	}

	if (!strcmp(cmd, "all") || !strcmp(cmd, "n8")) {
		printf("N8 - the mutators return void\n");
		mkfile(FIF_TIFF, "n8.tif", 2, 8);
		g_quiet = 1;
		{   /* the void ones: the call itself says nothing */
			FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(FIF_TIFF, "n8.tif", FALSE, TRUE /*read only*/, TRUE, 0);
			FIBITMAP *d = page(99, 16, 16, 8);
			int before = FreeImage_GetPageCount(m);
			int after, rc;
			FreeImage_AppendPage(m, d);
			/* sequence these: GCC evaluates printf's arguments right to left, so
			   putting the Close inline would free the bitmap before counting it */
			after = FreeImage_GetPageCount(m);
			rc = FreeImage_CloseMultiBitmap(m, 0);
			printf("    AppendPage   (void) on a read-only bitmap: pages %d -> %d, Close=%d\n",
			       before, after, rc);
			FreeImage_Unload(d);
		}
		{   /* the _Ex ones: they answer */
			FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(FIF_TIFF, "n8.tif", FALSE, TRUE, TRUE, 0);
			FIBITMAP *d = page(99, 16, 16, 8);
			printf("    AppendPageEx (BOOL) on a read-only bitmap -> %d\n", FreeImage_AppendPageEx(m, d));
			printf("    InsertPageEx (BOOL) at a bad index        -> %d\n", FreeImage_InsertPageEx(m, 99, d));
			printf("    DeletePageEx (BOOL) on a read-only bitmap -> %d\n", FreeImage_DeletePageEx(m, 0));
			FreeImage_Unload(d);
			FreeImage_CloseMultiBitmap(m, 0);
		}
		{   /* and they answer TRUE when the work is done */
			FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(FIF_TIFF, "n8ok.tif", TRUE, FALSE, TRUE, 0);
			FIBITMAP *d = page(99, 16, 16, 8);
			int a = FreeImage_AppendPageEx(m, d);
			int b = FreeImage_AppendPageEx(m, d);
			int c = FreeImage_InsertPageEx(m, 0, d);
			int e = FreeImage_DeletePageEx(m, 0);
			printf("    on a working TIFF: append=%d append=%d insert=%d delete=%d, pages=%d\n",
			       a, b, c, e, FreeImage_GetPageCount(m));
			FreeImage_Unload(d);
			FreeImage_CloseMultiBitmap(m, 0);
		}
		g_quiet = 0;
		printf("\n");
	}

	FreeImage_DeInitialise();
	return 0;
}
