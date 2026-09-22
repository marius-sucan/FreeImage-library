/* FreeImage 3 - RAW robustness test */
/* damaged input may load or be refused, never crash; run under ASan */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static const char *FILES[] = {
	"data/fi_raw_rggb.dng",       /* preview + ICC + an active-area margin */
	"data/fi_raw_bggr.dng",       /* the other Bayer phase                 */
	"data/fi_raw_nopreview.dng",  /* no preview: a different thumbnail path */
	"data/fi_raw_odd.dng",        /* odd dimensions                        */
};
#define NFILES ((int)(sizeof(FILES) / sizeof(FILES[0])))

static long loaded = 0, refused = 0, cases = 0;

static void quiet(FREE_IMAGE_FORMAT fif, const char *msg) { (void)fif; (void)msg; }

static int try_load(const BYTE *data, long len, int flags) {
	FIMEMORY *mem;
	FIBITMAP *dib;
	int ok;
	cases++;
	mem = FreeImage_OpenMemory((BYTE *)data, (DWORD)len);
	if (!mem) return 0;
	dib = FreeImage_LoadFromMemory(FIF_RAW, mem, flags);
	ok = dib != NULL;
	if (dib) {
		/* touch every row: a short buffer faults here */
		unsigned h = FreeImage_GetHeight(dib), line = FreeImage_GetLine(dib), y, i;
		unsigned long long acc = 0;
		if (FreeImage_HasPixels(dib) && line) {
			for (y = 0; y < h; y++) {
				const unsigned char *p =
					(const unsigned char *)FreeImage_GetScanLine(dib, y);
				if (!p) break;
				for (i = 0; i < line; i += 64) acc += p[i];
			}
		}
		(void)acc;
		FreeImage_Unload(dib);
	}
	FreeImage_CloseMemory(mem);
	if (ok) loaded++; else refused++;
	return ok;
}

/* identify only: LibRaw's parsing without decoding */
static void try_identify(const BYTE *data, long len) {
	FIMEMORY *mem = FreeImage_OpenMemory((BYTE *)data, (DWORD)len);
	if (!mem) return;
	cases++;
	FreeImage_GetFileTypeFromMemory(mem, 0);
	FreeImage_ValidateFromMemory(FIF_RAW, mem);
	FreeImage_CloseMemory(mem);
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

int main(void) {
	int i;
	long before;
	FreeImage_Initialise(TRUE);
	FreeImage_SetOutputMessage(quiet);
	printf("RAW robustness test - FreeImage %s\n", FreeImage_GetVersion());

	for (i = 0; i < NFILES; i++) {
		long n = 0, k;
		BYTE *orig = slurp(FILES[i], &n);
		BYTE *tmp;
		if (!orig) { printf("  cannot read %s\n", FILES[i]); continue; }
		tmp = (BYTE *)malloc((size_t)n + 4096);
		before = cases;

		/* the undamaged file, as a control */
		if (!try_load(orig, n, 0))
			printf("  NOTE %s does not load undamaged\n", FILES[i]);

		/* truncated prefixes */
		for (k = 0; k < n; k += 97) {
			try_load(orig, k, RAW_UNPROCESSED);
			try_identify(orig, k);
		}

		/* junk appended */
		memcpy(tmp, orig, (size_t)n);
		memset(tmp + n, 0x5A, 4096);
		try_load(tmp, n + 4096, 0);
		try_load(tmp, n + 1, RAW_UNPROCESSED);

		/* single-byte corruption, dense over the 1 KB of IFDs */
		for (k = 0; k < n; k += (k < 1024 ? 1 : 251)) {
			memcpy(tmp, orig, (size_t)n);
			tmp[k] ^= 0xFF;
			try_load(tmp, n, RAW_UNPROCESSED);
			if (k < 1024) try_identify(tmp, n);
		}

		/* whole 32-bit words set to 0xFFFFFFFF */
		for (k = 0; k + 4 <= n && k < 1024; k += 2) {
			memcpy(tmp, orig, (size_t)n);
			tmp[k] = tmp[k + 1] = tmp[k + 2] = tmp[k + 3] = 0xFF;
			try_load(tmp, n, RAW_UNPROCESSED);
		}

		/* 64-byte regions wiped */
		for (k = 0; k + 64 <= n; k += 64) {
			memcpy(tmp, orig, (size_t)n);
			memset(tmp + k, 0, 64);
			try_load(tmp, n, RAW_UNPROCESSED);
		}

		/* full decodes of damaged headers */
		for (k = 0; k + 4 <= n && k < 512; k += 16) {
			memcpy(tmp, orig, (size_t)n);
			tmp[k] = tmp[k + 1] = tmp[k + 2] = tmp[k + 3] = 0xFF;
			try_load(tmp, n, 0);
			memcpy(tmp, orig, (size_t)n);
			tmp[k] ^= 0xFF;
			try_load(tmp, n, RAW_PREVIEW);
		}

		printf("  %-30s %ld cases\n", FILES[i], cases - before);
		free(tmp);
		free(orig);
	}

	/* degenerate buffers */
	{
		static const BYTE tiny[] = {
			'I', 'I', 42, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
		};
		long k;
		BYTE one = 0;
		try_load(&one, 0, 0);
		try_load(&one, 1, 0);
		for (k = 1; k <= (long)sizeof(tiny); k++) try_load(tiny, k, 0);
		try_identify(tiny, sizeof(tiny));
		printf("  %-30s done\n", "degenerate buffers");
	}

	printf("survived %ld cases: %ld loaded, %ld refused\n", cases, loaded, refused);
	FreeImage_DeInitialise();
	return 0;
}
