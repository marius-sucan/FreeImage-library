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
};
#define NFILES ((int)(sizeof(FILES) / sizeof(FILES[0])))

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
			unsigned w = FreeImage_GetWidth(full), h = FreeImage_GetHeight(full);
			if (FreeImage_GetWidth(hdr) != w || FreeImage_GetHeight(hdr) != h)
				fail(FILES[i], "header", "size %ux%u, full load %ux%u",
				     FreeImage_GetWidth(hdr), FreeImage_GetHeight(hdr), w, h);
			if (FreeImage_HasPixels(hdr))
				fail(FILES[i], "header", "FIF_LOAD_NOPIXELS returned pixels");
			if (FreeImage_GetWidth(half) != w / 2 || FreeImage_GetHeight(half) != h / 2)
				fail(FILES[i], "halfsize", "%ux%u, want %ux%u",
				     FreeImage_GetWidth(half), FreeImage_GetHeight(half), w / 2, h / 2);
			if (FreeImage_GetImageType(full) != FIT_RGB16 || FreeImage_GetBPP(full) != 48)
				fail(FILES[i], "default", "want FIT_RGB16/48bpp, got type %d/%ubpp",
				     (int)FreeImage_GetImageType(full), FreeImage_GetBPP(full));
			if (FreeImage_GetImageType(disp) != FIT_BITMAP || FreeImage_GetBPP(disp) != 24)
				fail(FILES[i], "display", "want FIT_BITMAP/24bpp, got type %d/%ubpp",
				     (int)FreeImage_GetImageType(disp), FreeImage_GetBPP(disp));
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

/* --- the embedded colour profile ------------------------------------------ */
static void test_icc(void) {
	int i;
	printf("-- the embedded ICC profile reaches the bitmap\n");
	for (i = 0; i < NFILES; i++) {
		FIBITMAP *dib = FreeImage_Load(FIF_RAW, FILES[i], 0);
		FIICCPROFILE *p;
		int want = (strcmp(FILES[i], "data/fi_raw_rggb.dng") == 0) ? 516 : 0;
		if (!dib) { fail(FILES[i], "icc", "load failed"); continue; }
		p = FreeImage_GetICCProfile(dib);
		if (!p || (int)p->size != want) {
			fail(FILES[i], "icc", "want %d bytes, got %d", want, p ? (int)p->size : 0);
		} else if (want) {
			const BYTE *d = (const BYTE *)p->data;
			unsigned declared = ((unsigned)d[0] << 24) | ((unsigned)d[1] << 16) |
			                    ((unsigned)d[2] << 8) | d[3];
			/* a profile is its own length followed, at offset 36, by 'acsp' */
			if (declared != (unsigned)want)
				fail(FILES[i], "icc", "the profile's own length field says %u", declared);
			else if (memcmp(d + 36, "acsp", 4) != 0)
				fail(FILES[i], "icc", "no 'acsp' signature at offset 36");
			else
				printf("  ok   %-30s %d bytes, well formed\n", FILES[i], want);
		} else {
			printf("  ok   %-30s no profile, as expected\n", FILES[i]);
		}
		FreeImage_Unload(dib);
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
	test_crop();
	test_icc();
	test_bayer();
	test_readonly();
	printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	FreeImage_DeInitialise();
	return failures ? 1 : 0;
}
