/* FreeImage 3 - RAW regression test: PluginRAW's own behaviour */
/* every load runs from a file and a memory stream; both must agree */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "FreeImage.h"

static const char *FILES[] = {
	"data/fi_raw_rggb.dng",       /* RGGB, preview, ICC, 4px active-area margin */
	"data/fi_raw_bggr.dng",       /* the other Bayer phase                      */
	"data/fi_raw_nopreview.dng",  /* no embedded preview: RAW_PREVIEW falls back */
	"data/fi_raw_odd.dng",        /* odd dimensions, no margin                  */
	"data/fi_raw_rot90.dng",      /* Orientation 6: the processed image turns   */
	"data/fi_raw_rot180.dng",     /* Orientation 3: turned, the same size       */
	"data/fi_raw_mono.dng",       /* monochrome, a greyscale preview            */
	"data/fi_raw_mono_rot270.dng", /* the same, Orientation 8                   */
};
#define NFILES ((int)(sizeof(FILES) / sizeof(FILES[0])))

/* a monochrome sensor decodes to one color, and LibRaw halves only Bayer data */
static int is_mono(const char *file) { return strstr(file, "_mono") != NULL; }

static int failures = 0;

static void fail(const char *file, const char *what, const char *fmt, ...) {
	va_list ap;
	printf("  FAIL %s: %s - ", file, what);
	va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
	printf("\n");
	failures++;
}

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static unsigned long long digest(FIBITMAP *dib) {
	unsigned long long h = 1469598103934665603ULL;
	unsigned w = FreeImage_GetWidth(dib), ht = FreeImage_GetHeight(dib);
	unsigned bpp = FreeImage_GetBPP(dib), y;
	size_t row = (size_t)w * (bpp / 8), i;
	const unsigned char *p;
	if (!FreeImage_HasPixels(dib)) return 0ULL;
	for (y = 0; y < ht; y++) {
		p = (const unsigned char *)FreeImage_GetScanLine(dib, y);
		for (i = 0; i < row; i++) { h ^= p[i]; h *= 1099511628211ULL; }
	}
	return h;
}

static BYTE *slurp(const char *path, long *len) {
	FILE *f = fopen(path, "rb");
	BYTE *buf;
	if (!f) return NULL;
	fseek(f, 0, SEEK_END); *len = ftell(f); fseek(f, 0, SEEK_SET);
	buf = (BYTE *)malloc(*len ? (size_t)*len : 1);
	if (!buf || fread(buf, 1, (size_t)*len, f) != (size_t)*len) {
		free(buf); fclose(f); return NULL;
	}
	fclose(f);
	return buf;
}

static long meta_long(FIBITMAP *dib, const char *key, long dflt) {
	FITAG *tag = NULL;
	if (!FreeImage_GetMetadata(FIMD_COMMENTS, dib, key, &tag) || !tag) return dflt;
	{
		const char *v = (const char *)FreeImage_GetTagValue(tag);
		return v ? strtol(v, NULL, 10) : dflt;
	}
}

static const char *meta_str(FIBITMAP *dib, const char *key) {
	FITAG *tag = NULL;
	if (!FreeImage_GetMetadata(FIMD_COMMENTS, dib, key, &tag) || !tag) return "";
	{
		const char *v = (const char *)FreeImage_GetTagValue(tag);
		return v ? v : "";
	}
}

/* --- the datastream wrapper ----------------------------------------------- */
static void test_stream(void) {
	static const struct { const char *name; int flags; } PATHS[] = {
		{ "default16",   0                    },
		{ "display8",    RAW_DISPLAY          },
		{ "preview",     RAW_PREVIEW          },
		{ "unprocessed", RAW_UNPROCESSED      },
		{ "halfsize",    RAW_HALFSIZE         },
		{ "header",      FIF_LOAD_NOPIXELS    },
	};
	const int NPATHS = (int)(sizeof(PATHS) / sizeof(PATHS[0]));
	int i, j;
	printf("-- the file and a memory stream decode alike\n");
	for (i = 0; i < NFILES; i++) {
		long n = 0;
		BYTE *bytes = slurp(FILES[i], &n);
		int agreed = 0;
		if (!bytes) { fail(FILES[i], "stream", "cannot read the file"); continue; }
		for (j = 0; j < NPATHS; j++) {
			FIBITMAP *a = FreeImage_Load(FIF_RAW, FILES[i], PATHS[j].flags);
			FIMEMORY *m = FreeImage_OpenMemory(bytes, (DWORD)n);
			FIBITMAP *b = m ? FreeImage_LoadFromMemory(FIF_RAW, m, PATHS[j].flags) : NULL;
			if (!a || !b) {
				fail(FILES[i], PATHS[j].name, "file=%s stream=%s",
				     a ? "loaded" : "failed", b ? "loaded" : "failed");
			} else if (FreeImage_GetWidth(a) != FreeImage_GetWidth(b) ||
			           FreeImage_GetHeight(a) != FreeImage_GetHeight(b) ||
			           FreeImage_GetImageType(a) != FreeImage_GetImageType(b) ||
			           digest(a) != digest(b)) {
				fail(FILES[i], PATHS[j].name, "the memory stream decoded differently");
			} else {
				agreed++;
			}
			if (a) FreeImage_Unload(a);
			if (b) FreeImage_Unload(b);
			if (m) FreeImage_CloseMemory(m);
		}
		/* identification through a stream too */
		{
			FIMEMORY *m = FreeImage_OpenMemory(bytes, (DWORD)n);
			FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromMemory(m, 0);
			if (fif != FIF_RAW)
				fail(FILES[i], "GetFileTypeFromMemory", "want FIF_RAW, got %d", (int)fif);
			if (!FreeImage_ValidateFromMemory(FIF_RAW, m))
				fail(FILES[i], "ValidateFromMemory", "refused");
			FreeImage_CloseMemory(m);
		}
		if (agreed == NPATHS)
			printf("  ok   %-30s %d paths agree\n", FILES[i], agreed);
		free(bytes);
	}
}

/* --- a stream that does not start at byte zero ---------------------------- */
static void test_offset(void) {
	static const long PREFIX[] = { 1, 3, 64, 1000 };
	static const struct { const char *name; int flags; } PATHS[] = {
		{ "default16",   0               },
		{ "unprocessed", RAW_UNPROCESSED },
		{ "preview",     RAW_PREVIEW     },
	};
	int i, j, p;
	printf("-- a stream starting at a non-zero offset\n");
	for (i = 0; i < NFILES; i++) {
		long n = 0;
		BYTE *bytes = slurp(FILES[i], &n);
		int ok = 1;
		if (!bytes) { fail(FILES[i], "offset", "cannot read the file"); continue; }
		for (p = 0; p < (int)(sizeof(PREFIX) / sizeof(PREFIX[0])); p++) {
			long pre = PREFIX[p];
			BYTE *buf = (BYTE *)malloc((size_t)(n + pre));
			if (!buf) { fail(FILES[i], "offset", "out of memory"); ok = 0; break; }
			/* junk prefix */
			memset(buf, 0xAB, (size_t)pre);
			memcpy(buf + pre, bytes, (size_t)n);

			for (j = 0; j < (int)(sizeof(PATHS) / sizeof(PATHS[0])); j++) {
				FIBITMAP *want = FreeImage_Load(FIF_RAW, FILES[i], PATHS[j].flags);
				FIMEMORY *m = FreeImage_OpenMemory(buf, (DWORD)(n + pre));
				FIBITMAP *got = NULL;
				if (m) {
					FreeImage_SeekMemory(m, pre, SEEK_SET);
					got = FreeImage_LoadFromMemory(FIF_RAW, m, PATHS[j].flags);
				}
				if (!want) {
					fail(FILES[i], "offset", "the reference load failed");
					ok = 0;
				} else if (!got) {
					fail(FILES[i], PATHS[j].name,
					     "not loaded from a stream at offset %ld", pre);
					ok = 0;
				} else if (FreeImage_GetWidth(got) != FreeImage_GetWidth(want) ||
				           FreeImage_GetHeight(got) != FreeImage_GetHeight(want) ||
				           FreeImage_GetImageType(got) != FreeImage_GetImageType(want) ||
				           digest(got) != digest(want)) {
					fail(FILES[i], PATHS[j].name,
					     "decoded differently at offset %ld", pre);
					ok = 0;
				}
				if (want) FreeImage_Unload(want);
				if (got) FreeImage_Unload(got);
				if (m) FreeImage_CloseMemory(m);
			}

			/* identification has to work from there too */
			{
				FIMEMORY *m = FreeImage_OpenMemory(buf, (DWORD)(n + pre));
				if (m) {
					FREE_IMAGE_FORMAT fif;
					FreeImage_SeekMemory(m, pre, SEEK_SET);
					fif = FreeImage_GetFileTypeFromMemory(m, 0);
					if (fif != FIF_RAW) {
						fail(FILES[i], "offset", "GetFileTypeFromMemory at %ld gave %d",
						     pre, (int)fif);
						ok = 0;
					}
					FreeImage_SeekMemory(m, pre, SEEK_SET);
					if (!FreeImage_ValidateFromMemory(FIF_RAW, m)) {
						fail(FILES[i], "offset", "ValidateFromMemory refused at %ld", pre);
						ok = 0;
					}
					if (FreeImage_TellMemory(m) < pre) {
						fail(FILES[i], "offset", "the handle moved in front of the stream");
						ok = 0;
					}
					FreeImage_CloseMemory(m);
				}
			}
			free(buf);
		}
		if (ok)
			printf("  ok   %-30s offsets 1, 3, 64, 1000 all decode alike\n", FILES[i]);
		free(bytes);
	}
}

/* --- how the paths relate to one another ---------------------------------- */
static void test_paths(void) {
	int i;
	printf("-- the load paths agree about geometry\n");
	for (i = 0; i < NFILES; i++) {
		FIBITMAP *full = FreeImage_Load(FIF_RAW, FILES[i], 0);
		FIBITMAP *hdr  = FreeImage_Load(FIF_RAW, FILES[i], FIF_LOAD_NOPIXELS);
		FIBITMAP *half = FreeImage_Load(FIF_RAW, FILES[i], RAW_HALFSIZE);
		FIBITMAP *disp = FreeImage_Load(FIF_RAW, FILES[i], RAW_DISPLAY);
		if (!full || !hdr || !half || !disp) {
			fail(FILES[i], "paths", "one of the loads failed");
		} else {
			const int mono = is_mono(FILES[i]);
			unsigned w = FreeImage_GetWidth(full), h = FreeImage_GetHeight(full);
			unsigned half_w = mono ? w : w / 2, half_h = mono ? h : h / 2;
			if (FreeImage_GetWidth(hdr) != w || FreeImage_GetHeight(hdr) != h)
				fail(FILES[i], "header", "size %ux%u, full load %ux%u",
				     FreeImage_GetWidth(hdr), FreeImage_GetHeight(hdr), w, h);
			if (FreeImage_HasPixels(hdr))
				fail(FILES[i], "header", "FIF_LOAD_NOPIXELS returned pixels");
			if (FreeImage_GetWidth(half) != half_w || FreeImage_GetHeight(half) != half_h)
				fail(FILES[i], "halfsize", "%ux%u, want %ux%u",
				     FreeImage_GetWidth(half), FreeImage_GetHeight(half), half_w, half_h);
			if (FreeImage_GetImageType(full) != (mono ? FIT_UINT16 : FIT_RGB16) ||
			    FreeImage_GetBPP(full) != (mono ? 16u : 48u))
				fail(FILES[i], "default", "want %s, got type %d/%ubpp", mono ? "FIT_UINT16/16bpp" : "FIT_RGB16/48bpp",
				     (int)FreeImage_GetImageType(full), FreeImage_GetBPP(full));
			if (FreeImage_GetImageType(disp) != FIT_BITMAP || FreeImage_GetBPP(disp) != (mono ? 8u : 24u))
				fail(FILES[i], "display", "want FIT_BITMAP/%ubpp, got type %d/%ubpp", mono ? 8u : 24u,
				     (int)FreeImage_GetImageType(disp), FreeImage_GetBPP(disp));
			if (mono && (FreeImage_GetColorType(full) != FIC_MINISBLACK || FreeImage_GetColorType(disp) != FIC_MINISBLACK))
				fail(FILES[i], "mono", "the greyscale loads are not FIC_MINISBLACK");
			if (FreeImage_GetWidth(disp) != w || FreeImage_GetHeight(disp) != h)
				fail(FILES[i], "display", "size differs from the 16-bit load");
			printf("  ok   %-30s %ux%u, half %ux%u\n", FILES[i], w, h,
			       FreeImage_GetWidth(half), FreeImage_GetHeight(half));
		}
		if (full) FreeImage_Unload(full);
		if (hdr)  FreeImage_Unload(hdr);
		if (half) FreeImage_Unload(half);
		if (disp) FreeImage_Unload(disp);
	}

	printf("-- RAW_PREVIEW uses the embedded preview, or decodes\n");
	{
		FIBITMAP *p = FreeImage_Load(FIF_RAW, "data/fi_raw_nopreview.dng", RAW_PREVIEW);
		FIBITMAP *d = FreeImage_Load(FIF_RAW, "data/fi_raw_nopreview.dng", RAW_DISPLAY);
		if (!p || !d) fail("data/fi_raw_nopreview.dng", "preview", "load failed");
		else if (digest(p) != digest(d))
			fail("data/fi_raw_nopreview.dng", "preview",
			     "no embedded preview, so it should equal RAW_DISPLAY");
		else printf("  ok   %-30s fell back to a decode (%ux%u)\n",
		            "data/fi_raw_nopreview.dng",
		            FreeImage_GetWidth(p), FreeImage_GetHeight(p));
		if (p) FreeImage_Unload(p);
		if (d) FreeImage_Unload(d);
	}
	{
		FIBITMAP *p = FreeImage_Load(FIF_RAW, "data/fi_raw_rggb.dng", RAW_PREVIEW);
		if (!p) fail("data/fi_raw_rggb.dng", "preview", "load failed");
		else {
			/* the generator writes a 48x32 preview into IFD0 */
			if (FreeImage_GetWidth(p) != 48 || FreeImage_GetHeight(p) != 32)
				fail("data/fi_raw_rggb.dng", "preview", "want 48x32, got %ux%u",
				     FreeImage_GetWidth(p), FreeImage_GetHeight(p));
			else printf("  ok   %-30s used the embedded 48x32 preview\n",
			            "data/fi_raw_rggb.dng");
			FreeImage_Unload(p);
		}
	}
}

/* --- a header-only load describes the full load --------------------------- */
static int same_raw_keys(FIBITMAP *a, FIBITMAP *b) {
	static const char *KEYS[] = {
		"Raw.Output.Width", "Raw.Output.Height", "Raw.Frame.Left", "Raw.Frame.Top",
		"Raw.Frame.Width", "Raw.Frame.Height", "Raw.BayerPattern",
	};
	int k;
	if (FreeImage_GetMetadataCount(FIMD_COMMENTS, a) != FreeImage_GetMetadataCount(FIMD_COMMENTS, b))
		return 0;
	for (k = 0; k < (int)(sizeof(KEYS) / sizeof(KEYS[0])); k++)
		if (strcmp(meta_str(a, KEYS[k]), meta_str(b, KEYS[k])) != 0) return 0;
	return 1;
}

static void test_header(void) {
	static const struct { const char *name; int flags; } PATHS[] = {
		{ "default16",   0                          },
		{ "display8",    RAW_DISPLAY                },
		{ "halfsize",    RAW_HALFSIZE               },
		{ "half8",       RAW_HALFSIZE | RAW_DISPLAY },
		{ "preview",     RAW_PREVIEW                },
		{ "unprocessed", RAW_UNPROCESSED            },
	};
	const int NPATHS = (int)(sizeof(PATHS) / sizeof(PATHS[0]));
	int i, j;
	printf("-- a header-only load describes the full load, on every path\n");
	for (i = 0; i < NFILES; i++) {
		int agreed = 0;
		for (j = 0; j < NPATHS; j++) {
			FIBITMAP *full = FreeImage_Load(FIF_RAW, FILES[i], PATHS[j].flags);
			FIBITMAP *hdr  = FreeImage_Load(FIF_RAW, FILES[i], PATHS[j].flags | FIF_LOAD_NOPIXELS);
			if (!full || !hdr) {
				fail(FILES[i], PATHS[j].name, "full load %s, header-only load %s",
				     full ? "loaded" : "failed", hdr ? "loaded" : "failed");
			} else if (FreeImage_HasPixels(hdr)) {
				fail(FILES[i], PATHS[j].name, "FIF_LOAD_NOPIXELS returned pixels");
			} else if (FreeImage_GetWidth(hdr) != FreeImage_GetWidth(full) ||
			           FreeImage_GetHeight(hdr) != FreeImage_GetHeight(full)) {
				fail(FILES[i], PATHS[j].name, "header %ux%u, full load %ux%u",
				     FreeImage_GetWidth(hdr), FreeImage_GetHeight(hdr),
				     FreeImage_GetWidth(full), FreeImage_GetHeight(full));
			} else if (FreeImage_GetImageType(hdr) != FreeImage_GetImageType(full) ||
			           FreeImage_GetBPP(hdr) != FreeImage_GetBPP(full)) {
				fail(FILES[i], PATHS[j].name, "header type %d/%ubpp, full load %d/%ubpp",
				     (int)FreeImage_GetImageType(hdr), FreeImage_GetBPP(hdr),
				     (int)FreeImage_GetImageType(full), FreeImage_GetBPP(full));
			} else if (FreeImage_GetICCProfile(hdr)->size != FreeImage_GetICCProfile(full)->size) {
				fail(FILES[i], PATHS[j].name, "header profile %d bytes, full load %d",
				     (int)FreeImage_GetICCProfile(hdr)->size, (int)FreeImage_GetICCProfile(full)->size);
			} else if (!same_raw_keys(hdr, full)) {
				fail(FILES[i], PATHS[j].name, "the Raw.* keys differ from the full load's");
			} else {
				agreed++;
			}
			if (full) FreeImage_Unload(full);
			if (hdr)  FreeImage_Unload(hdr);
		}
		if (agreed == NPATHS)
			printf("  ok   %-30s %d paths agree\n", FILES[i], agreed);
	}
}

/* --- a bitmap preview turns like the image -------------------------------- */
static int same_pixel(FIBITMAP *a, unsigned ax, unsigned ay, FIBITMAP *b, unsigned bx, unsigned by) {
	/* top-down coordinates */
	ay = FreeImage_GetHeight(a) - 1 - ay;
	by = FreeImage_GetHeight(b) - 1 - by;
	if (FreeImage_GetBPP(a) == 8) {
		BYTE ia = 0, ib = 0;
		FreeImage_GetPixelIndex(a, ax, ay, &ia);
		FreeImage_GetPixelIndex(b, bx, by, &ib);
		return ia == ib;
	} else {
		RGBQUAD ca, cb;
		FreeImage_GetPixelColor(a, ax, ay, &ca);
		FreeImage_GetPixelColor(b, bx, by, &cb);
		return ca.rgbRed == cb.rgbRed && ca.rgbGreen == cb.rgbGreen && ca.rgbBlue == cb.rgbBlue;
	}
}

static void test_preview_turn(void) {
	static const struct { const char *file, *twin; int flip; } T[] = {
		{ "data/fi_raw_rot90.dng",       "data/fi_raw_rggb.dng", 6 },
		{ "data/fi_raw_mono_rot270.dng", "data/fi_raw_mono.dng", 5 },
	};
	int i;
	printf("-- a bitmap preview turns like the image\n");
	for (i = 0; i < (int)(sizeof(T) / sizeof(T[0])); i++) {
		FIBITMAP *turned = FreeImage_Load(FIF_RAW, T[i].file, RAW_PREVIEW);
		FIBITMAP *twin = FreeImage_Load(FIF_RAW, T[i].twin, RAW_PREVIEW);
		if (!turned || !twin) {
			fail(T[i].file, "preview", "load failed");
		} else if (FreeImage_GetWidth(turned) != FreeImage_GetHeight(twin) ||
		           FreeImage_GetHeight(turned) != FreeImage_GetWidth(twin) ||
		           FreeImage_GetBPP(turned) != FreeImage_GetBPP(twin)) {
			fail(T[i].file, "preview", "%ux%u, the unturned preview %ux%u",
			     FreeImage_GetWidth(turned), FreeImage_GetHeight(turned),
			     FreeImage_GetWidth(twin), FreeImage_GetHeight(twin));
		} else {
			/* pixel (x, y) is the source pixel LibRaw's flip_index(y, x) gives */
			const unsigned w = FreeImage_GetWidth(turned), h = FreeImage_GetHeight(turned);
			const unsigned sw = FreeImage_GetWidth(twin), sh = FreeImage_GetHeight(twin);
			unsigned x, y, bad = 0;
			for (y = 0; y < h; y++) {
				for (x = 0; x < w; x++) {
					unsigned row = (T[i].flip & 4) ? x : y, col = (T[i].flip & 4) ? y : x;
					if (T[i].flip & 2) row = sh - 1 - row;
					if (T[i].flip & 1) col = sw - 1 - col;
					if (!same_pixel(turned, x, y, twin, col, row)) bad++;
				}
			}
			if (bad)
				fail(T[i].file, "preview", "%u pixels are not where flip %d puts them", bad, T[i].flip);
			else
				printf("  ok   %-30s %ux%u preview turned like the image\n", T[i].file, w, h);
		}
		if (turned) FreeImage_Unload(turned);
		if (twin) FreeImage_Unload(twin);
	}
}

/* --- the Orientation tag -------------------------------------------------- */
static void test_orientation(void) {
	static const struct { const char *file; int turned; } ROT[] = {
		{ "data/fi_raw_rot90.dng",  1 },
		{ "data/fi_raw_rot180.dng", 0 },
		{ "data/fi_raw_rggb.dng",   0 },
		{ "data/fi_raw_mono_rot270.dng", 1 },
	};
	int i;
	printf("-- a turned camera turns the processed image, not the CFA field\n");
	for (i = 0; i < (int)(sizeof(ROT) / sizeof(ROT[0])); i++) {
		FIBITMAP *full = FreeImage_Load(FIF_RAW, ROT[i].file, 0);
		FIBITMAP *un   = FreeImage_Load(FIF_RAW, ROT[i].file, RAW_UNPROCESSED);
		if (!full || !un) {
			fail(ROT[i].file, "orientation", "load failed");
		} else {
			/* Raw.Output.* is the processed size before the turn */
			long ow = meta_long(un, "Raw.Output.Width", -1), oh = meta_long(un, "Raw.Output.Height", -1);
			long want_w = ROT[i].turned ? oh : ow, want_h = ROT[i].turned ? ow : oh;
			if ((long)FreeImage_GetWidth(full) != want_w || (long)FreeImage_GetHeight(full) != want_h)
				fail(ROT[i].file, "orientation", "processed %ux%u, want %ldx%ld",
				     FreeImage_GetWidth(full), FreeImage_GetHeight(full), want_w, want_h);
			else
				printf("  ok   %-30s %ldx%ld frame -> %ux%u\n", ROT[i].file, ow, oh,
				       FreeImage_GetWidth(full), FreeImage_GetHeight(full));
		}
		if (full) FreeImage_Unload(full);
		if (un) FreeImage_Unload(un);
	}
}

/* --- cropping ------------------------------------------------------------- */
static void test_crop(void) {
	static const struct {
		const char *file;
		unsigned raw_w, raw_h, out_w, out_h, left, top;
	} CROP[] = {
		{ "data/fi_raw_rggb.dng",      96, 64, 88, 56, 4, 4 },
		{ "data/fi_raw_bggr.dng",      96, 64, 88, 56, 4, 4 },
		{ "data/fi_raw_nopreview.dng", 96, 64, 96, 64, 0, 0 },
		{ "data/fi_raw_odd.dng",       70, 46, 70, 46, 0, 0 },
	};
	int i;
	printf("-- the active-area margin is applied, and RAW_UNPROCESSED keeps it\n");
	for (i = 0; i < (int)(sizeof(CROP) / sizeof(CROP[0])); i++) {
		FIBITMAP *full = FreeImage_Load(FIF_RAW, CROP[i].file, 0);
		FIBITMAP *un   = FreeImage_Load(FIF_RAW, CROP[i].file, RAW_UNPROCESSED);
		if (!full || !un) { fail(CROP[i].file, "crop", "load failed"); }
		else {
			int bad = 0;
			if (FreeImage_GetWidth(full) != CROP[i].out_w ||
			    FreeImage_GetHeight(full) != CROP[i].out_h) {
				fail(CROP[i].file, "crop", "processed %ux%u, want %ux%u",
				     FreeImage_GetWidth(full), FreeImage_GetHeight(full),
				     CROP[i].out_w, CROP[i].out_h);
				bad = 1;
			}
			if (FreeImage_GetWidth(un) != CROP[i].raw_w ||
			    FreeImage_GetHeight(un) != CROP[i].raw_h) {
				fail(CROP[i].file, "crop", "unprocessed %ux%u, want the whole CFA %ux%u",
				     FreeImage_GetWidth(un), FreeImage_GetHeight(un),
				     CROP[i].raw_w, CROP[i].raw_h);
				bad = 1;
			}
			if (meta_long(un, "Raw.Frame.Left", -1) != (long)CROP[i].left ||
			    meta_long(un, "Raw.Frame.Top", -1) != (long)CROP[i].top) {
				fail(CROP[i].file, "crop", "Raw.Frame.Left/Top %ld,%ld, want %u,%u",
				     meta_long(un, "Raw.Frame.Left", -1),
				     meta_long(un, "Raw.Frame.Top", -1), CROP[i].left, CROP[i].top);
				bad = 1;
			}
			/* Raw.Output.* describes the processed image */
			if (meta_long(un, "Raw.Output.Width", -1) != (long)CROP[i].out_w ||
			    meta_long(un, "Raw.Output.Height", -1) != (long)CROP[i].out_h) {
				fail(CROP[i].file, "crop", "Raw.Output.* disagrees with the 16-bit load");
				bad = 1;
			}
			if (!bad)
				printf("  ok   %-30s CFA %ux%u -> %ux%u at %u,%u\n", CROP[i].file,
				       CROP[i].raw_w, CROP[i].raw_h, CROP[i].out_w, CROP[i].out_h,
				       CROP[i].left, CROP[i].top);
		}
		if (full) FreeImage_Unload(full);
		if (un) FreeImage_Unload(un);
	}
}

/* --- the colour profiles ---------------------------------------------------- */
/* linear sRGB or grey at 16 bits, none at 8; the file's own profile, which describes its preview or the camera, only on RAW_PREVIEW and RAW_UNPROCESSED */
static void test_icc(void) {
	DWORD srgb_size = 0, grey_size = 0;
	const void *srgb = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_LINEAR_SRGB, &srgb_size);
	const void *grey = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_LINEAR_GRAY, &grey_size);
	int i;
	printf("-- linear profiles on the 16-bit output, the file's own on the preview and the sensor data\n");
	for (i = 0; i < NFILES; i++) {
		const int own = (strcmp(FILES[i], "data/fi_raw_rggb.dng") == 0) ? 516 : 0;
		const void *linear = is_mono(FILES[i]) ? grey : srgb;
		const DWORD linear_size = is_mono(FILES[i]) ? grey_size : srgb_size;
		FIBITMAP *d16 = FreeImage_Load(FIF_RAW, FILES[i], 0);
		FIBITMAP *d8 = FreeImage_Load(FIF_RAW, FILES[i], RAW_DISPLAY);
		FIBITMAP *preview = FreeImage_Load(FIF_RAW, FILES[i], RAW_PREVIEW);
		FIBITMAP *sensor = FreeImage_Load(FIF_RAW, FILES[i], RAW_UNPROCESSED);
		FIICCPROFILE *p;
		int bad = 0;
		if (!d16 || !d8 || !preview || !sensor) {
			fail(FILES[i], "icc", "a load failed");
		} else {
			p = FreeImage_GetICCProfile(d16);
			if (!p->data || p->size != linear_size || memcmp(p->data, linear, linear_size) != 0) {
				fail(FILES[i], "icc", "the 16-bit output has a %d-byte profile, not linear %s", (int)p->size, is_mono(FILES[i]) ? "grey" : "sRGB");
				bad = 1;
			}
			if (FreeImage_GetICCProfile(d8)->data) {
				fail(FILES[i], "icc", "RAW_DISPLAY has a %d-byte profile", (int)FreeImage_GetICCProfile(d8)->size);
				bad = 1;
			}
			if ((int)FreeImage_GetICCProfile(preview)->size != own || (int)FreeImage_GetICCProfile(sensor)->size != own) {
				fail(FILES[i], "icc", "want the file's %d bytes on RAW_PREVIEW and RAW_UNPROCESSED, got %d and %d", own,
					(int)FreeImage_GetICCProfile(preview)->size, (int)FreeImage_GetICCProfile(sensor)->size);
				bad = 1;
			} else if (own) {
				const BYTE *d = (const BYTE *)FreeImage_GetICCProfile(sensor)->data;
				unsigned declared = ((unsigned)d[0] << 24) | ((unsigned)d[1] << 16) |
				                    ((unsigned)d[2] << 8) | d[3];
				/* a profile is its own length followed, at offset 36, by 'acsp' */
				if (declared != (unsigned)own) {
					fail(FILES[i], "icc", "the profile's own length field says %u", declared);
					bad = 1;
				} else if (memcmp(d + 36, "acsp", 4) != 0) {
					fail(FILES[i], "icc", "no 'acsp' signature at offset 36");
					bad = 1;
				}
			}
			if (!bad) {
				printf("  ok   %-30s linear %s at 16 bits, %s\n", FILES[i], is_mono(FILES[i]) ? "grey" : "sRGB",
					own ? "its own 516 bytes, well formed, on the preview and the sensor data" : "no profile of its own");
			}
		}
		if (d16) FreeImage_Unload(d16);
		if (d8) FreeImage_Unload(d8);
		if (preview) FreeImage_Unload(preview);
		if (sensor) FreeImage_Unload(sensor);
	}
}

/* --- a JPEG preview's own profile ---------------------------------------- */
static unsigned le16(const BYTE *p) { return p[0] | (p[1] << 8); }
static unsigned le32(const BYTE *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned)p[3] << 24); }
static void put_le32(BYTE *p, unsigned v) { p[0] = (BYTE)v; p[1] = (BYTE)(v >> 8); p[2] = (BYTE)(v >> 16); p[3] = (BYTE)(v >> 24); }

/* fi_raw_rggb.dng with its IFD0 preview replaced by this JPEG (compression 7) appended to the file */
static BYTE *with_jpeg_preview(const BYTE *dng, long dng_size, const BYTE *jpeg, unsigned jpeg_size, long *size) {
	const long at = (dng_size + 1) & ~1L;
	BYTE *out = (BYTE *)calloc((size_t)at + jpeg_size, 1);
	unsigned ifd, count, i, patched = 0;
	if (!out || dng_size < 8 || memcmp(dng, "II", 2) != 0) { free(out); return NULL; }
	memcpy(out, dng, (size_t)dng_size);
	memcpy(out + at, jpeg, jpeg_size);
	ifd = le32(out + 4);
	count = le16(out + ifd);
	for (i = 0; i < count; i++) {
		BYTE *entry = out + ifd + 2 + 12 * i;
		switch (le16(entry)) {
			case 259: entry[8] = 7; entry[9] = 0; patched++; break;		/* Compression: JPEG */
			case 273: put_le32(entry + 8, (unsigned)at); patched++; break;	/* StripOffsets */
			case 279: put_le32(entry + 8, jpeg_size); patched++; break;	/* StripByteCounts */
		}
	}
	if (patched != 3) { free(out); return NULL; }
	*size = at + (long)jpeg_size;
	return out;
}

static FIBITMAP *load_memory(BYTE *data, long size, int flags) {
	FIMEMORY *mem = FreeImage_OpenMemory(data, (DWORD)size);
	FIBITMAP *dib = mem ? FreeImage_LoadFromMemory(FIF_RAW, mem, flags) : NULL;
	if (mem) FreeImage_CloseMemory(mem);
	return dib;
}

/* RAW_PREVIEW: the JPEG preview's own profile wins over the file's, which stands in for a preview without one */
static void test_preview_profile(void) {
	const char *file = "data/fi_raw_rggb.dng";
	DWORD p3_size = 0;
	const void *p3 = FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_DISPLAY_P3, &p3_size);
	long dng_size = 0;
	BYTE *dng = slurp(file, &dng_size);
	FIBITMAP *bitmap_preview = FreeImage_Load(FIF_RAW, file, RAW_PREVIEW);
	int with_profile;
	printf("-- a JPEG preview keeps its own profile\n");
	if (!dng || !bitmap_preview) { fail(file, "preview profile", "cannot read the file"); free(dng); return; }
	for (with_profile = 0; with_profile < 2; with_profile++) {
		FIMEMORY *jmem = FreeImage_OpenMemory(NULL, 0);
		FIBITMAP *rgb = FreeImage_Clone(bitmap_preview), *jpeg_alone = NULL, *full, *head;
		BYTE *jpeg = NULL, *patched = NULL;
		DWORD jpeg_size = 0;
		long patched_size = 0;
		const char *what = with_profile ? "a JPEG preview with Display P3" : "a JPEG preview without a profile";
		if (with_profile) FreeImage_CreateICCProfile(rgb, (void *)p3, (long)p3_size);
		else FreeImage_DestroyICCProfile(rgb);
		if (!FreeImage_SaveToMemory(FIF_JPEG, rgb, jmem, JPEG_QUALITYSUPERB) || !FreeImage_AcquireMemory(jmem, &jpeg, &jpeg_size) ||
		    !(patched = with_jpeg_preview(dng, dng_size, jpeg, jpeg_size, &patched_size))) {
			fail(file, what, "no file made");
		} else {
			FIMEMORY *alone = FreeImage_OpenMemory(jpeg, jpeg_size);
			jpeg_alone = FreeImage_LoadFromMemory(FIF_JPEG, alone, 0);
			FreeImage_CloseMemory(alone);
			full = load_memory(patched, patched_size, RAW_PREVIEW);
			head = load_memory(patched, patched_size, RAW_PREVIEW | FIF_LOAD_NOPIXELS);
			if (!full || !head || !jpeg_alone) {
				fail(file, what, "a load failed");
			} else if (digest(full) != digest(jpeg_alone)) {
				fail(file, what, "LibRaw did not take the JPEG as the preview");
			} else {
				const int want = with_profile ? (int)p3_size : 516;
				FIICCPROFILE *a = FreeImage_GetICCProfile(full), *b = FreeImage_GetICCProfile(head);
				if ((int)a->size != want || (int)b->size != want ||
				    (with_profile && (memcmp(a->data, p3, p3_size) != 0 || memcmp(b->data, p3, p3_size) != 0))) {
					fail(file, what, "a %d-byte profile (header-only %d), want %s", (int)a->size, (int)b->size, with_profile ? "the preview's Display P3" : "the file's 516 bytes");
				} else {
					printf("  ok   %-30s %s\n", what, with_profile ? "keeps its Display P3, full and header-only" : "takes the file's 516 bytes, full and header-only");
				}
			}
			if (full) FreeImage_Unload(full);
			if (head) FreeImage_Unload(head);
		}
		if (jpeg_alone) FreeImage_Unload(jpeg_alone);
		FreeImage_Unload(rgb);
		FreeImage_CloseMemory(jmem);
		free(patched);
	}
	FreeImage_Unload(bitmap_preview);
	free(dng);
}

/* --- Leaf metadata: numbers LibRaw reads as text, through scanf_one --------- */
static void test_leaf_text(void) {
	static const struct { const char *name; int flags; } P[] = {
		{ "default16", 0            },
		{ "display8",  RAW_DISPLAY  },
		{ "halfsize",  RAW_HALFSIZE },
	};
	int p;
	printf("-- a Leaf NeutObj_neutrals record gives the white balance of the same AsShotNeutral\n");
	for (p = 0; p < (int)(sizeof(P) / sizeof(P[0])); p++) {
		FIBITMAP *leaf = FreeImage_Load(FIF_RAW, "data/fi_raw_leaf.dng", P[p].flags);
		FIBITMAP *asn = FreeImage_Load(FIF_RAW, "data/fi_raw_leaf_asn.dng", P[p].flags);
		if (!leaf || !asn)
			fail("data/fi_raw_leaf.dng", P[p].name, "load failed");
		else if (digest(leaf) != digest(asn))
			fail("data/fi_raw_leaf.dng", P[p].name, "decoded unlike fi_raw_leaf_asn.dng");
		else
			printf("  ok   %-30s %s\n", "data/fi_raw_leaf.dng", P[p].name);
		if (leaf) FreeImage_Unload(leaf);
		if (asn) FreeImage_Unload(asn);
	}
}

/* --- the plugin is read-only ---------------------------------------------- */
static void test_readonly(void) {
	FIBITMAP *dib;
	printf("-- RAW is a read-only format\n");
	if (FreeImage_FIFSupportsWriting(FIF_RAW))
		fail("FIF_RAW", "writing", "FIFSupportsWriting says yes");
	if (FreeImage_FIFSupportsExportType(FIF_RAW, FIT_BITMAP) ||
	    FreeImage_FIFSupportsExportType(FIF_RAW, FIT_RGB16))
		fail("FIF_RAW", "export type", "SupportsExportType says yes");
	if (FreeImage_FIFSupportsExportBPP(FIF_RAW, 24) ||
	    FreeImage_FIFSupportsExportBPP(FIF_RAW, 48))
		fail("FIF_RAW", "export bpp", "SupportsExportBPP says yes");
	if (!FreeImage_FIFSupportsICCProfiles(FIF_RAW))
		fail("FIF_RAW", "icc", "SupportsICCProfiles says no");
	if (!FreeImage_FIFSupportsNoPixels(FIF_RAW))
		fail("FIF_RAW", "no pixels", "SupportsNoPixels says no");
	dib = FreeImage_Load(FIF_RAW, FILES[0], 0);
	if (dib) {
		FIMEMORY *m = FreeImage_OpenMemory(NULL, 0);
		if (FreeImage_SaveToMemory(FIF_RAW, dib, m, 0))
			fail("FIF_RAW", "save", "a save through the RAW plugin succeeded");
		FreeImage_CloseMemory(m);
		FreeImage_Unload(dib);
	}
	printf("  ok   FIF_RAW                        read-only, no pixels, ICC\n");
}

/* --- the Bayer pattern ---------------------------------------------------- */
static void test_bayer(void) {
	static const struct { const char *file, *pattern; } B[] = {
		{ "data/fi_raw_rggb.dng",      "RGGBRGGBRGGBRGGB" },
		{ "data/fi_raw_bggr.dng",      "BGGRBGGRBGGRBGGR" },
		{ "data/fi_raw_nopreview.dng", "RGGBRGGBRGGBRGGB" },
		{ "data/fi_raw_odd.dng",       "RGGBRGGBRGGBRGGB" },
		{ "data/fi_raw_rot90.dng",     "RGGBRGGBRGGBRGGB" },
		{ "data/fi_raw_rot180.dng",    "RGGBRGGBRGGBRGGB" },
	};
	int i;
	printf("-- Raw.BayerPattern follows the file's CFAPattern\n");
	for (i = 0; i < (int)(sizeof(B) / sizeof(B[0])); i++) {
		FIBITMAP *dib = FreeImage_Load(FIF_RAW, B[i].file, RAW_UNPROCESSED);
		if (!dib) { fail(B[i].file, "bayer", "load failed"); continue; }
		{
			const char *got = meta_str(dib, "Raw.BayerPattern");
			if (strcmp(got, B[i].pattern) != 0)
				fail(B[i].file, "bayer", "want %s, got %s", B[i].pattern, got);
			else
				printf("  ok   %-30s %s\n", B[i].file, got);
		}
		FreeImage_Unload(dib);
	}
}

int main(void) {
	FreeImage_Initialise(TRUE);
	FreeImage_SetOutputMessage(quiet);
	printf("RAW regression test - FreeImage %s\n", FreeImage_GetVersion());
	test_stream();
	test_offset();
	test_paths();
	test_header();
	test_orientation();
	test_preview_turn();
	test_crop();
	test_icc();
	test_preview_profile();
	test_bayer();
	test_leaf_text();
	test_readonly();
	printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	FreeImage_DeInitialise();
	return failures ? 1 : 0;
}
