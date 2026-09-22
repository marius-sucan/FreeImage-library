/* FreeImage 3 - RAW decode test: every load path against TABLE */
/* TABLE is the oracle; ./decode --record reprints it */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "FreeImage.h"

typedef struct {
	const char *file;
	const char *path;           /* the load path, for the report        */
	int flags;                  /* the flag that selects it             */
	int type;                   /* FREE_IMAGE_TYPE                      */
	unsigned width, height, bpp;
	unsigned long long pixels;  /* digest of the decoded image, 0 if none */
	int icc;                    /* attached colour profile, in bytes    */
} Expect;

#define HDR FIF_LOAD_NOPIXELS

static const Expect TABLE[] = {
	{ "data/fi_raw_rggb.dng",        "default16",   0,                FIT_RGB16,    88, 56, 48, 0xa300b79403246db2ULL,  516 },
	{ "data/fi_raw_rggb.dng",        "display8",    RAW_DISPLAY,      FIT_BITMAP,   88, 56, 24, 0x7e31e282f700895cULL,  516 },
	{ "data/fi_raw_rggb.dng",        "preview",     RAW_PREVIEW,      FIT_BITMAP,   48, 32, 24, 0x41789f0360a65acfULL,  516 },
	{ "data/fi_raw_rggb.dng",        "unprocessed", RAW_UNPROCESSED,  FIT_UINT16,   96, 64, 16, 0x6c67ac91471603bbULL,  516 },
	{ "data/fi_raw_rggb.dng",        "halfsize",    RAW_HALFSIZE,     FIT_RGB16,    44, 28, 48, 0xc168924f2c2ca1bdULL,  516 },
	{ "data/fi_raw_rggb.dng",        "header",      HDR,              FIT_RGB16,    88, 56, 48, 0x0000000000000000ULL,  516 },

	{ "data/fi_raw_bggr.dng",        "default16",   0,                FIT_RGB16,    88, 56, 48, 0x7042acf16e081fdbULL,    0 },
	{ "data/fi_raw_bggr.dng",        "display8",    RAW_DISPLAY,      FIT_BITMAP,   88, 56, 24, 0xb41865e78d21e39dULL,    0 },
	{ "data/fi_raw_bggr.dng",        "preview",     RAW_PREVIEW,      FIT_BITMAP,   48, 32, 24, 0x41789f0360a65acfULL,    0 },
	{ "data/fi_raw_bggr.dng",        "unprocessed", RAW_UNPROCESSED,  FIT_UINT16,   96, 64, 16, 0x29a8fa76552de1b3ULL,    0 },
	{ "data/fi_raw_bggr.dng",        "halfsize",    RAW_HALFSIZE,     FIT_RGB16,    44, 28, 48, 0xd0cfa5554f51a30bULL,    0 },
	{ "data/fi_raw_bggr.dng",        "header",      HDR,              FIT_RGB16,    88, 56, 48, 0x0000000000000000ULL,    0 },

	{ "data/fi_raw_nopreview.dng",   "default16",   0,                FIT_RGB16,    96, 64, 48, 0x261c6e381668adc2ULL,    0 },
	{ "data/fi_raw_nopreview.dng",   "display8",    RAW_DISPLAY,      FIT_BITMAP,   96, 64, 24, 0xb34c0b8ad43ad4a6ULL,    0 },
	{ "data/fi_raw_nopreview.dng",   "preview",     RAW_PREVIEW,      FIT_BITMAP,   96, 64, 24, 0xb34c0b8ad43ad4a6ULL,    0 },
	{ "data/fi_raw_nopreview.dng",   "unprocessed", RAW_UNPROCESSED,  FIT_UINT16,   96, 64, 16, 0x6c67ac91471603bbULL,    0 },
	{ "data/fi_raw_nopreview.dng",   "halfsize",    RAW_HALFSIZE,     FIT_RGB16,    48, 32, 48, 0x9f51eed3a448918dULL,    0 },
	{ "data/fi_raw_nopreview.dng",   "header",      HDR,              FIT_RGB16,    96, 64, 48, 0x0000000000000000ULL,    0 },

	{ "data/fi_raw_odd.dng",         "default16",   0,                FIT_RGB16,    70, 46, 48, 0xf87e1bdf76e016b3ULL,    0 },
	{ "data/fi_raw_odd.dng",         "display8",    RAW_DISPLAY,      FIT_BITMAP,   70, 46, 24, 0xbc9a3ad7c1cef8f7ULL,    0 },
	{ "data/fi_raw_odd.dng",         "preview",     RAW_PREVIEW,      FIT_BITMAP,   70, 46, 24, 0xbc9a3ad7c1cef8f7ULL,    0 },
	{ "data/fi_raw_odd.dng",         "unprocessed", RAW_UNPROCESSED,  FIT_UINT16,   70, 46, 16, 0x09b64a078b123623ULL,    0 },
	{ "data/fi_raw_odd.dng",         "halfsize",    RAW_HALFSIZE,     FIT_RGB16,    35, 23, 48, 0xd61b9518aaab1fa7ULL,    0 },
	{ "data/fi_raw_odd.dng",         "header",      HDR,              FIT_RGB16,    70, 46, 48, 0x0000000000000000ULL,    0 },
};
#define NCASES ((int)(sizeof(TABLE) / sizeof(TABLE[0])))

/* Raw.* keys from the RAW_UNPROCESSED path */
typedef struct {
	const char *file;
	const char *out_w, *out_h;
	const char *left, *top, *frame_w, *frame_h;
	const char *bayer;
} ExpectMeta;

static const ExpectMeta META[] = {
	{ "data/fi_raw_rggb.dng",      "88", "56", "4", "4", "88", "56", "RGGBRGGBRGGBRGGB" },
	{ "data/fi_raw_bggr.dng",      "88", "56", "4", "4", "88", "56", "BGGRBGGRBGGRBGGR" },
	{ "data/fi_raw_nopreview.dng", "96", "64", "0", "0", "96", "64", "RGGBRGGBRGGBRGGB" },
	{ "data/fi_raw_odd.dng",       "70", "46", "0", "0", "70", "46", "RGGBRGGBRGGBRGGB" },
};
#define NMETA ((int)(sizeof(META) / sizeof(META[0])))

static int failures = 0;

static void fail(const char *file, const char *what, const char *fmt, ...) {
	va_list ap;
	printf("  FAIL %s: %s - ", file, what);
	va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
	printf("\n");
	failures++;
}

/* visible pixels only, not the pitch padding */
static unsigned long long digest(FIBITMAP *dib) {
	unsigned long long h = 1469598103934665603ULL;
	unsigned w = FreeImage_GetWidth(dib), ht = FreeImage_GetHeight(dib);
	unsigned bpp = FreeImage_GetBPP(dib), y;
	size_t row = (size_t)w * (bpp / 8), i;
	const unsigned char *p;
	unsigned hdr[3];
	if (!FreeImage_HasPixels(dib)) return 0ULL;
	hdr[0] = w; hdr[1] = ht; hdr[2] = bpp;
	p = (const unsigned char *)hdr;
	for (i = 0; i < sizeof(hdr); i++) { h ^= p[i]; h *= 1099511628211ULL; }
	for (y = 0; y < ht; y++) {
		p = (const unsigned char *)FreeImage_GetScanLine(dib, y);
		for (i = 0; i < row; i++) { h ^= p[i]; h *= 1099511628211ULL; }
	}
	return h;
}

static int icc_len(FIBITMAP *dib) {
	FIICCPROFILE *p = FreeImage_GetICCProfile(dib);
	return (p && p->data) ? (int)p->size : 0;
}

static const char *meta_str(FIBITMAP *dib, const char *key) {
	FITAG *tag = NULL;
	if (!FreeImage_GetMetadata(FIMD_COMMENTS, dib, key, &tag) || !tag) return "";
	{
		const char *v = (const char *)FreeImage_GetTagValue(tag);
		return v ? v : "";
	}
}

static void check_meta(void) {
	int i;
	printf("-- Raw.* metadata on the RAW_UNPROCESSED path\n");
	for (i = 0; i < NMETA; i++) {
		const ExpectMeta *e = &META[i];
		FIBITMAP *dib = FreeImage_Load(FIF_RAW, e->file, RAW_UNPROCESSED);
		if (!dib) { fail(e->file, "metadata", "did not load"); continue; }
		{
			struct { const char *key, *want; } k[] = {
				{ "Raw.Output.Width",  e->out_w   },
				{ "Raw.Output.Height", e->out_h   },
				{ "Raw.Frame.Left",    e->left    },
				{ "Raw.Frame.Top",     e->top     },
				{ "Raw.Frame.Width",   e->frame_w },
				{ "Raw.Frame.Height",  e->frame_h },
				{ "Raw.BayerPattern",  e->bayer   },
			};
			int j, bad = 0;
			for (j = 0; j < (int)(sizeof(k) / sizeof(k[0])); j++) {
				const char *got = meta_str(dib, k[j].key);
				if (strcmp(got, k[j].want) != 0) {
					fail(e->file, k[j].key, "want %s, got %s", k[j].want, got);
					bad = 1;
				}
			}
			if (!bad) printf("  ok   %-30s %sx%s at %s,%s  %s\n", e->file,
			                 e->out_w, e->out_h, e->left, e->top, e->bayer);
		}
		FreeImage_Unload(dib);
	}
}

static const char *flagname(int flags) {
	if (flags == HDR) return "HDR,";
	if (flags == RAW_DISPLAY) return "RAW_DISPLAY,";
	if (flags == RAW_PREVIEW) return "RAW_PREVIEW,";
	if (flags == RAW_UNPROCESSED) return "RAW_UNPROCESSED,";
	if (flags == RAW_HALFSIZE) return "RAW_HALFSIZE,";
	return "0,";
}

static const char *typename_of(FIBITMAP *dib) {
	switch (FreeImage_GetImageType(dib)) {
	case FIT_RGB16:  return "FIT_RGB16,";
	case FIT_UINT16: return "FIT_UINT16,";
	default:         return "FIT_BITMAP,";
	}
}

static void record(void) {
	int i;
	printf("/* --record reprints this table. */\n");
	printf("static const Expect TABLE[] = {\n");
	for (i = 0; i < NCASES; i++) {
		const Expect *e = &TABLE[i];
		char quoted[80], pathq[24];
		FIBITMAP *dib = FreeImage_Load(FIF_RAW, e->file, e->flags);
		if (!dib) { printf("\t/* %s %s DID NOT LOAD */\n", e->file, e->path); continue; }
		snprintf(quoted, sizeof quoted, "\"%s\",", e->file);
		snprintf(pathq, sizeof pathq, "\"%s\",", e->path);
		printf("\t{ %-30s %-14s %-17s %-12s %3u, %2u, %2u, 0x%016llxULL, %4d },\n",
		       quoted, pathq, flagname(e->flags), typename_of(dib),
		       FreeImage_GetWidth(dib), FreeImage_GetHeight(dib),
		       FreeImage_GetBPP(dib), digest(dib), icc_len(dib));
		FreeImage_Unload(dib);
	}
	printf("};\n");
}

int main(int argc, char **argv) {
	int i;
	FreeImage_Initialise(TRUE);
	if (argc > 1 && strcmp(argv[1], "--record") == 0) {
		record();
		FreeImage_DeInitialise();
		return 0;
	}
	printf("RAW decode test - FreeImage %s\n", FreeImage_GetVersion());

	printf("-- format detection\n");
	for (i = 0; i < NMETA; i++) {
		FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(META[i].file, 0);
		if (fif != FIF_RAW)
			fail(META[i].file, "GetFileType", "want FIF_RAW (%d), got %d",
			     (int)FIF_RAW, (int)fif);
		else if (!FreeImage_Validate(FIF_RAW, META[i].file))
			fail(META[i].file, "Validate", "the RAW plugin refused it");
		else
			printf("  ok   %-30s FIF_RAW\n", META[i].file);
	}

	printf("-- load paths\n");
	for (i = 0; i < NCASES; i++) {
		const Expect *e = &TABLE[i];
		FIBITMAP *dib = FreeImage_Load(FIF_RAW, e->file, e->flags);
		if (!dib) { fail(e->file, e->path, "did not load"); continue; }
		{
			int t = (int)FreeImage_GetImageType(dib);
			unsigned w = FreeImage_GetWidth(dib), h = FreeImage_GetHeight(dib);
			unsigned b = FreeImage_GetBPP(dib);
			unsigned long long d = digest(dib);
			int ic = icc_len(dib);
			int bad = 0;
			if (t != e->type) { fail(e->file, e->path, "type: want %d, got %d", e->type, t); bad = 1; }
			if (w != e->width || h != e->height)
				{ fail(e->file, e->path, "size: want %ux%u, got %ux%u", e->width, e->height, w, h); bad = 1; }
			if (b != e->bpp) { fail(e->file, e->path, "bpp: want %u, got %u", e->bpp, b); bad = 1; }
			if (d != e->pixels)
				{ fail(e->file, e->path, "pixels: want 0x%016llx, got 0x%016llx", e->pixels, d); bad = 1; }
			if (ic != e->icc) { fail(e->file, e->path, "icc: want %d, got %d", e->icc, ic); bad = 1; }
			if (e->flags == HDR && FreeImage_HasPixels(dib))
				{ fail(e->file, e->path, "FIF_LOAD_NOPIXELS returned pixels"); bad = 1; }
			if (!bad)
				printf("  ok   %-30s %-12s %ux%u %ubpp\n", e->file, e->path, w, h, b);
		}
		FreeImage_Unload(dib);
	}

	check_meta();

	printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
	FreeImage_DeInitialise();
	return failures ? 1 : 0;
}
