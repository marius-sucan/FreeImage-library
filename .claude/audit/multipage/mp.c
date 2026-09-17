/* Multi-page API audit harness.
   Each subcommand reproduces one finding from AUDIT-MULTIPAGE.md. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "FreeImage.h"

static void msg(FREE_IMAGE_FORMAT fif, const char *m) {
	printf("    [FI] %s: %s\n", fif == FIF_UNKNOWN ? "?" : FreeImage_GetFormatFromFIF(fif), m);
}

/* page filled with byte value v; 8-bit greyscale-palette by default,
   24-bit grey when FI_BPP=24 (WebP and friends reject palettes) */
static int g_bpp = 8;
static int g_saveflags = 0;   /* flags handed to FreeImage_CloseMultiBitmap (FI_SAVEFLAGS) */

static FIBITMAP *page(int v, int w, int h) {
	FIBITMAP *d = FreeImage_Allocate(w, h, g_bpp, 0, 0, 0);
	int y;
	if (g_bpp == 8) {
		RGBQUAD *p = FreeImage_GetPalette(d);
		int i;
		for (i = 0; i < 256; i++) { p[i].rgbRed = p[i].rgbGreen = p[i].rgbBlue = (BYTE)i; }
	}
	for (y = 0; y < h; y++) memset(FreeImage_GetScanLine(d, y), v, FreeImage_GetLine(d));
	return d;
}

/* incompressible page: the cache stores pages in cache_fif format, and a solid
   colour compresses to well under one 64 KB block, so nothing ever spills to disk */
static FIBITMAP *noisepage(unsigned seed, int w, int h) {
	FIBITMAP *d = FreeImage_Allocate(w, h, 24, 0, 0, 0);
	int x, y;
	for (y = 0; y < h; y++) {
		BYTE *s = FreeImage_GetScanLine(d, y);
		for (x = 0; x < w * 3; x++) {
			seed = seed * 1103515245u + 12345u;
			s[x] = (BYTE)(seed >> 16);
		}
	}
	return d;
}

/* content checksum over the pixel rows */
static unsigned sum(FIBITMAP *d) {
	unsigned h = 2166136261u, y, x;
	if (!d) return 0;
	for (y = 0; y < FreeImage_GetHeight(d); y++) {
		BYTE *s = FreeImage_GetScanLine(d, y);
		for (x = 0; x < FreeImage_GetLine(d); x++) { h ^= s[x]; h *= 16777619u; }
	}
	return h;
}

static int firstpix(FIBITMAP *d) {
	BYTE *s;
	if (!d) return -1;
	s = FreeImage_GetScanLine(d, 0);
	if (FreeImage_GetBPP(d) == 8) return s[0];
	return s[2]; /* 24/32bpp: red */
}

/* build an n-page file via the multipage API */
static int mk(FREE_IMAGE_FORMAT fif, const char *fn, int n) {
	FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(fif, fn, TRUE, FALSE, TRUE, 0);
	int k;
	if (!m) { printf("  mk: OpenMultiBitmap(create_new) FAILED\n"); return 1; }
	for (k = 0; k < n; k++) {
		FIBITMAP *d = page((k + 1) * 20, 16, 16);
		FreeImage_AppendPage(m, d);
		FreeImage_Unload(d);
	}
	printf("  mk: %s pages=%d -> %s\n", FreeImage_GetFormatFromFIF(fif), FreeImage_GetPageCount(m), fn);
	return FreeImage_CloseMultiBitmap(m, g_saveflags) ? 0 : 1;
}

/* print the page count and the first pixel of every page */
static void dump(FREE_IMAGE_FORMAT fif, const char *fn, const char *label) {
	FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(fif, fn, FALSE, TRUE, TRUE, 0);
	int n, k;
	if (!m) { printf("  %s: open FAILED\n", label); return; }
	n = FreeImage_GetPageCount(m);
	printf("  %s: pages=%d  values=[", label, n);
	for (k = 0; k < n; k++) {
		FIBITMAP *d = FreeImage_LockPage(m, k);
		printf("%s%d", k ? " " : "", firstpix(d));
		if (d) FreeImage_UnlockPage(m, d, FALSE);
	}
	printf("]\n");
	FreeImage_CloseMultiBitmap(m, 0);
}

int main(int argc, char **argv) {
	const char *cmd = argc > 1 ? argv[1] : "";
	const char *bpp = getenv("FI_BPP");
	if (bpp) g_bpp = atoi(bpp);
	{ const char *sf = getenv("FI_SAVEFLAGS"); if (sf) g_saveflags = (int)strtol(sf, NULL, 0); }
	FreeImage_Initialise(FALSE);
	FreeImage_SetOutputMessage(msg);

	if (!strcmp(cmd, "mk")) {
		mk((FREE_IMAGE_FORMAT)atoi(argv[2]), argv[3], atoi(argv[4]));
		dump((FREE_IMAGE_FORMAT)atoi(argv[2]), argv[3], "readback");

	} else if (!strcmp(cmd, "move")) {
		/* F3: MovePage(target, source) semantics */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		int target = atoi(argv[4]), source = atoi(argv[5]);
		FIMULTIBITMAP *m;
		mk(fif, argv[3], 4);
		dump(fif, argv[3], "before  ");
		m = FreeImage_OpenMultiBitmap(fif, argv[3], FALSE, FALSE, TRUE, 0);
		printf("  MovePage(target=%d, source=%d) -> %d\n", target, source,
		       FreeImage_MovePage(m, target, source));
		FreeImage_CloseMultiBitmap(m, g_saveflags);
		dump(fif, argv[3], "after   ");

	} else if (!strcmp(cmd, "lockdel")) {
		/* F1: LockPage ignores the block list */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		int del = atoi(argv[4]);
		FIMULTIBITMAP *m;
		int k, n;
		mk(fif, argv[3], 3);
		m = FreeImage_OpenMultiBitmap(fif, argv[3], FALSE, FALSE, TRUE, 0);
		FreeImage_DeletePage(m, del);
		n = FreeImage_GetPageCount(m);
		printf("  after DeletePage(%d): GetPageCount=%d\n", del, n);
		for (k = 0; k < n; k++) {
			FIBITMAP *d = FreeImage_LockPage(m, k);
			printf("    LockPage(%d) -> value %d %s\n", k, firstpix(d), d ? "" : "(NULL)");
			if (d) FreeImage_UnlockPage(m, d, FALSE);
		}
		FreeImage_CloseMultiBitmap(m, g_saveflags);
		dump(fif, argv[3], "on disk ");

	} else if (!strcmp(cmd, "unlockedit")) {
		/* F1b: UnlockPage(changed) writes through the wrong block */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		int del = atoi(argv[4]), lock = atoi(argv[5]);
		FIMULTIBITMAP *m;
		FIBITMAP *d;
		mk(fif, argv[3], 3);
		m = FreeImage_OpenMultiBitmap(fif, argv[3], FALSE, FALSE, TRUE, 0);
		FreeImage_DeletePage(m, del);
		d = FreeImage_LockPage(m, lock);
		printf("  DeletePage(%d); LockPage(%d) -> value %d; overwrite with 200\n",
		       del, lock, firstpix(d));
		if (d) {
			int y;
			for (y = 0; y < (int)FreeImage_GetHeight(d); y++)
				memset(FreeImage_GetScanLine(d, y), 200, FreeImage_GetLine(d));
			FreeImage_UnlockPage(m, d, TRUE);
		}
		FreeImage_CloseMultiBitmap(m, g_saveflags);
		dump(fif, argv[3], "on disk ");

	} else if (!strcmp(cmd, "nonmp")) {
		/* F5: OpenMultiBitmap accepts single-image formats */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(fif, argv[3], TRUE, FALSE, TRUE, 0);
		int k, n = atoi(argv[4]);
		FILE *f;
		long sz;
		if (!m) { printf("  nonmp %s: refused at open (GOOD)\n",
		                 FreeImage_GetFormatFromFIF(fif)); goto done; }
		for (k = 0; k < n; k++) {
			FIBITMAP *d = page((k + 1) * 20, 16, 16);
			FreeImage_AppendPage(m, d);
			FreeImage_Unload(d);
		}
		{
			int np = FreeImage_GetPageCount(m);
			int rc = FreeImage_CloseMultiBitmap(m, 0);
			printf("  nonmp %s: accepted, GetPageCount=%d, Close=%d\n",
			       FreeImage_GetFormatFromFIF(fif), np, rc);
		}
		f = fopen(argv[3], "rb");
		if (f) {
			int np2;
			fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f);
			m = FreeImage_OpenMultiBitmap(fif, argv[3], FALSE, TRUE, TRUE, 0);
			np2 = m ? FreeImage_GetPageCount(m) : -1;
			printf("    output %ld bytes; re-read: pages seen = %d\n", sz, np2);
			if (m) FreeImage_CloseMultiBitmap(m, 0);
		}

	} else if (!strcmp(cmd, "negpage")) {
		/* F8: no bounds check on page indices */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		FIMULTIBITMAP *m;
		mk(fif, argv[3], 3);
		m = FreeImage_OpenMultiBitmap(fif, argv[3], FALSE, FALSE, TRUE, 0);
		printf("  pages=%d; calling DeletePage(%s)\n", FreeImage_GetPageCount(m), argv[4]);
		fflush(stdout);
		FreeImage_DeletePage(m, atoi(argv[4]));
		printf("  survived; GetPageCount=%d\n", FreeImage_GetPageCount(m));
		fflush(stdout);
		FreeImage_CloseMultiBitmap(m, g_saveflags);
		dump(fif, argv[3], "on disk ");

	} else if (!strcmp(cmd, "locknegpage")) {
		/* F8b: LockPage with an out-of-range index */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		FIMULTIBITMAP *m;
		FIBITMAP *d;
		mk(fif, argv[3], 3);
		m = FreeImage_OpenMultiBitmap(fif, argv[3], FALSE, TRUE, TRUE, 0);
		printf("  LockPage(%s)...\n", argv[4]);
		fflush(stdout);
		d = FreeImage_LockPage(m, atoi(argv[4]));
		printf("  -> %s (value %d)\n", d ? "non-NULL" : "NULL", firstpix(d));
		fflush(stdout);
		if (d) FreeImage_UnlockPage(m, d, FALSE);
		FreeImage_CloseMultiBitmap(m, 0);

	} else if (!strcmp(cmd, "savelock")) {
		/* F11: SaveMultiBitmapToMemory while LockPage's decoder is open */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		FIMULTIBITMAP *m;
		FIBITMAP *a, *b;
		FIMEMORY *mem;
		mk(fif, argv[3], 3);
		m = FreeImage_OpenMultiBitmap(fif, argv[3], FALSE, TRUE, TRUE, 0);
		a = FreeImage_LockPage(m, 0);
		printf("  LockPage(0) -> %d\n", firstpix(a));
		mem = FreeImage_OpenMemory(NULL, 0);
		printf("  SaveMultiBitmapToMemory while locked -> %d\n",
		       FreeImage_SaveMultiBitmapToMemory(fif, m, mem, 0));
		FreeImage_CloseMemory(mem);
		b = FreeImage_LockPage(m, 1);
		printf("  LockPage(1) after that -> %d %s\n", firstpix(b), b ? "" : "(NULL)");
		if (a) FreeImage_UnlockPage(m, a, FALSE);
		if (b) FreeImage_UnlockPage(m, b, FALSE);
		FreeImage_CloseMultiBitmap(m, 0);

	} else if (!strcmp(cmd, "cachename")) {
		/* F4: two multibitmaps whose filenames share a stem get the SAME .ficache.
		   Both open it "w+b", so the second truncates the first. Only bites once the
		   32-block memory cache overflows and blocks are actually swapped to disk. */
		int n = argc > 2 ? atoi(argv[2]) : 40, k, bad = 0;
		unsigned *wa = (unsigned*)calloc(n + 4, sizeof(unsigned));
		unsigned *wb = (unsigned*)calloc(n + 4, sizeof(unsigned));
		FIMULTIBITMAP *a = FreeImage_OpenMultiBitmap(FIF_TIFF, "coll.tif", TRUE, FALSE, FALSE, 0);
		FIMULTIBITMAP *b = FreeImage_OpenMultiBitmap(FIF_TIFF, "coll.tiff", TRUE, FALSE, FALSE, 0);
		printf("  opened coll.tif and coll.tiff, both rw, cache on DISK\n");
		system("ls -1 *.ficache 2>/dev/null | sed 's/^/    cache file: /'");
		for (k = 0; k < n; k++) {
			FIBITMAP *d = noisepage(k + 1, 300, 300);
			wa[k] = sum(d); FreeImage_AppendPage(a, d); FreeImage_Unload(d);
			d = noisepage(5000 + k, 300, 300);
			wb[k] = sum(d); FreeImage_AppendPage(b, d); FreeImage_Unload(d);
		}
		printf("  appended %d pages to each; close tif=%d", n, FreeImage_CloseMultiBitmap(a, 0));
		printf(" close tiff=%d\n", FreeImage_CloseMultiBitmap(b, 0));
		{ int i;
		  const char *fn[2]; fn[0] = "coll.tif"; fn[1] = "coll.tiff";
		  for (i = 0; i < 2; i++) {
			FIMULTIBITMAP *r = FreeImage_OpenMultiBitmap(FIF_TIFF, fn[i], FALSE, TRUE, TRUE, 0);
			unsigned *w = i ? wb : wa;
			int np = r ? FreeImage_GetPageCount(r) : -1, mism = 0;
			for (k = 0; k < np; k++) {
				FIBITMAP *d = FreeImage_LockPage(r, k);
				if (!d) { bad++; continue; }
				if (k < n && sum(d) != w[k]) mism++;
				FreeImage_UnlockPage(r, d, FALSE);
			}
			printf("  %-10s pages=%d (expect %d) unreadable=%d corrupt=%d\n",
			       fn[i], np, n, bad, mism);
			if (r) FreeImage_CloseMultiBitmap(r, 0);
		  } }
		free(wa); free(wb);

	} else if (!strcmp(cmd, "cachestress")) {
		/* CacheFile: disk-backed cache, >CACHE_SIZE blocks, block reuse after deletes.
		   Pages are incompressible so each really occupies several 64 KB blocks. */
		int n = atoi(argv[2]), k, bad = 0, mism = 0;
		int keep = getenv("FI_MEMCACHE") ? 1 : 0;
		unsigned *want = (unsigned*)calloc(n * 2 + 8, sizeof(unsigned));
		int nwant = 0;
		FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(FIF_TIFF, "stress.tif", TRUE, FALSE,
		                                             keep, 0);
		if (!m) { printf("  open failed\n"); goto done; }
		printf("  cache: %s\n", keep ? "memory" : "DISK");
		for (k = 0; k < n; k++) {
			FIBITMAP *d = noisepage(k + 1, 300, 300);   /* ~270 KB -> ~5 blocks each */
			want[nwant++] = sum(d);
			FreeImage_AppendPage(m, d);
			FreeImage_Unload(d);
		}
		printf("  after %d appends:    GetPageCount=%d\n", n, FreeImage_GetPageCount(m));
		for (k = 0; k < n / 3; k++) FreeImage_DeletePage(m, 0);   /* free blocks */
		memmove(want, want + n / 3, (nwant - n / 3) * sizeof(unsigned));
		nwant -= n / 3;
		printf("  after %d deletes:    GetPageCount=%d (expect %d)\n",
		       n / 3, FreeImage_GetPageCount(m), nwant);
		for (k = 0; k < n / 3; k++) {
			FIBITMAP *d = noisepage(1000 + k, 300, 300);
			want[nwant++] = sum(d);
			FreeImage_AppendPage(m, d);                            /* reuse freed nrs */
			FreeImage_Unload(d);
		}
		printf("  after %d re-appends: GetPageCount=%d (expect %d)\n",
		       n / 3, FreeImage_GetPageCount(m), nwant);
		{
			int np = FreeImage_GetPageCount(m);
			int rc = FreeImage_CloseMultiBitmap(m, 0);
			printf("  close=%d (had %d pages)\n", rc, np);
		}
		{ FIMULTIBITMAP *r = FreeImage_OpenMultiBitmap(FIF_TIFF, "stress.tif", FALSE, TRUE, TRUE, 0);
		  int np = r ? FreeImage_GetPageCount(r) : -1;
		  for (k = 0; k < np; k++) {
			FIBITMAP *d = FreeImage_LockPage(r, k);
			if (!d) { printf("    page %d: NULL\n", k); bad++; }
			else {
				unsigned g = sum(d);
				if (k < nwant && g != want[k]) {
					if (mism < 6) printf("    page %d: checksum %u, expected %u\n", k, g, want[k]);
					mism++;
				}
				FreeImage_UnlockPage(r, d, FALSE);
			}
		  }
		  printf("  readback pages=%d (expect %d) unreadable=%d corrupt=%d\n",
		         np, nwant, bad, mism);
		  if (r) FreeImage_CloseMultiBitmap(r, 0); }
		free(want);

	} else if (!strcmp(cmd, "insert")) {
		/* InsertPage placement and bounds */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		int at = atoi(argv[4]);
		FIMULTIBITMAP *m;
		FIBITMAP *d;
		mk(fif, argv[3], 3);
		dump(fif, argv[3], "before ");
		m = FreeImage_OpenMultiBitmap(fif, argv[3], FALSE, FALSE, TRUE, 0);
		d = page(199, 16, 16);
		printf("  InsertPage(page=%d, value 199); count before=%d\n",
		       at, FreeImage_GetPageCount(m));
		fflush(stdout);
		FreeImage_InsertPage(m, at, d);
		FreeImage_Unload(d);
		printf("  count after=%d\n", FreeImage_GetPageCount(m));
		FreeImage_CloseMultiBitmap(m, g_saveflags);
		dump(fif, argv[3], "after  ");

	} else if (!strcmp(cmd, "openmodes")) {
		/* Which (format, mode) combinations does OpenMultiBitmap accept, and can the
		   resulting bitmap actually do anything? */
		static const int fifs[] = { -1, 999, 18 /*TIFF*/, 0 /*BMP*/, 37 /*AVIF*/,
		                            38 /*HEIF*/, 34 /*RAW*/, 24 /*DDS*/ };
		unsigned f;
		mk(FIF_TIFF, "om.tif", 2);
		printf("  %-8s %-24s %s\n", "FIF", "mode", "OpenMultiBitmap");
		for (f = 0; f < sizeof(fifs)/sizeof(fifs[0]); f++) {
			FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)fifs[f];
			const char *name = (fifs[f] >= 0 && fifs[f] < FreeImage_GetFIFCount())
			                   ? FreeImage_GetFormatFromFIF(fif) : "<invalid>";
			int mode;
			for (mode = 0; mode < 3; mode++) {
				/* 0: read-only  1: read/write  2: create_new */
				BOOL create_new = (mode == 2);
				BOOL read_only  = (mode == 0);
				const char *label = mode == 0 ? "read-only" :
				                    mode == 1 ? "read/write (edit)" : "create_new";
				const char *fn = create_new ? "om_new.out" : "om.tif";
				FIMULTIBITMAP *m = FreeImage_OpenMultiBitmap(fif, fn, create_new, read_only, TRUE, 0);
				printf("  %-8s %-24s %s\n", name, label, m ? "ACCEPTED" : "NULL");
				if (m) FreeImage_CloseMultiBitmap(m, 0);
			}
		}

	} else if (!strcmp(cmd, "scenario")) {
		/* End-to-end model check: apply a mixed sequence of page operations, then
		   assert that LockPage agrees with the model AND with what Close writes.
		   Before the M1/M2 fix the locked values were the file's page order, which
		   stopped matching the model at the first DeletePage. */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		int want[8], nwant = 0, k, bad = 0;
		FIMULTIBITMAP *m;
		FIBITMAP *d;
		mk(fif, argv[3], 5);                       /* [20 40 60 80 100] */
		want[nwant++] = 20; want[nwant++] = 40; want[nwant++] = 60;
		want[nwant++] = 80; want[nwant++] = 100;

		m = FreeImage_OpenMultiBitmap(fif, argv[3], FALSE, FALSE, TRUE, 0);
		if (!m) { printf("  open failed\n"); goto done; }

		FreeImage_DeletePage(m, 1);                /* [20 60 80 100] */
		memmove(want + 1, want + 2, (nwant - 2) * sizeof(int)); nwant--;

		d = page(120, 16, 16); FreeImage_AppendPage(m, d); FreeImage_Unload(d);
		want[nwant++] = 120;                       /* [20 60 80 100 120] */

		d = page(140, 16, 16); FreeImage_InsertPage(m, 0, d); FreeImage_Unload(d);
		memmove(want + 1, want, nwant * sizeof(int)); want[0] = 140; nwant++;
		                                           /* [140 20 60 80 100 120] */

		FreeImage_MovePage(m, nwant - 1, 0);       /* move page 0 to the end */
		{ int t = want[0];
		  memmove(want, want + 1, (nwant - 1) * sizeof(int));
		  want[nwant - 1] = t; }                   /* [20 60 80 100 120 140] */

		printf("  model  : pages=%d  values=[", nwant);
		for (k = 0; k < nwant; k++) printf("%s%d", k ? " " : "", want[k]);
		printf("]\n");
		printf("  API    : pages=%d  values=[", FreeImage_GetPageCount(m));
		for (k = 0; k < FreeImage_GetPageCount(m); k++) {
			FIBITMAP *p = FreeImage_LockPage(m, k);
			int v = firstpix(p);
			printf("%s%d", k ? " " : "", v);
			if (k >= nwant || v != want[k]) bad++;
			if (p) FreeImage_UnlockPage(m, p, FALSE);
		}
		printf("]  %s\n", bad ? "*** LockPage DISAGREES WITH THE MODEL ***" : "matches");
		printf("  close=%d\n", FreeImage_CloseMultiBitmap(m, g_saveflags));
		dump(fif, argv[3], "on disk");

	} else if (!strcmp(cmd, "matrix")) {
		/* authoritative read/write capability, straight from the library */
		int f;
		printf("%-4s %-9s %-5s %-5s %s\n", "FIF", "FORMAT", "READ", "WRITE", "EXTENSIONS");
		for (f = 0; f < FreeImage_GetFIFCount(); f++) {
			FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)f;
			printf("%-4d %-9s %-5s %-5s %s\n", f,
			       FreeImage_GetFormatFromFIF(fif),
			       FreeImage_FIFSupportsReading(fif) ? "yes" : "-",
			       FreeImage_FIFSupportsWriting(fif) ? "yes" : "-",
			       FreeImage_GetFIFExtensionList(fif));
		}

	} else if (!strcmp(cmd, "savefif")) {
		/* FreeImage_SaveMultiBitmapToHandle never checks node->m_plugin->save_proc,
		   so asking it for a read-only format calls a NULL function pointer. */
		FREE_IMAGE_FORMAT dst = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		FIMULTIBITMAP *m;
		FIMEMORY *mem;
		mk(FIF_TIFF, "sf.tif", 3);
		m = FreeImage_OpenMultiBitmap(FIF_TIFF, "sf.tif", FALSE, TRUE, TRUE, 0);
		mem = FreeImage_OpenMemory(NULL, 0);
		printf("  FIFSupportsWriting(%s) = %d\n", FreeImage_GetFormatFromFIF(dst),
		       FreeImage_FIFSupportsWriting(dst));
		printf("  SaveMultiBitmapToMemory(%s, 3-page TIFF) ...\n", FreeImage_GetFormatFromFIF(dst));
		fflush(stdout);
		printf("  -> %d\n", FreeImage_SaveMultiBitmapToMemory(dst, m, mem, 0));
		fflush(stdout);
		FreeImage_CloseMemory(mem);
		FreeImage_CloseMultiBitmap(m, 0);

	} else if (!strcmp(cmd, "blockzero")) {
		/* CacheFile: block nr 0 is both a legal block number and the
		   end-of-chain sentinel (Block::next == 0). Free the blocks in an order
		   that puts 0 *second* on the free list, then write a page that needs two
		   blocks: its chain becomes N -> 0, and readFile stops at the sentinel,
		   so the tail of the page is never copied back. */
		int dim = argc > 2 ? atoi(argv[2]) : 200;
		FIMULTIBITMAP *m;
		FIBITMAP *d;
		unsigned want;
		int k;
		m = FreeImage_OpenMultiBitmap(FIF_TIFF, "bz.tif", TRUE, FALSE, TRUE, 0);
		if (!m) { printf("  open failed\n"); goto done; }
		/* three small pages -> one cache block each: nrs 0, 1, 2 */
		for (k = 0; k < 3; k++) { d = noisepage(k + 1, 8, 8); FreeImage_AppendPage(m, d); FreeImage_Unload(d); }
		if (getenv("FI_NODEL")) {
			printf("  CONTROL A: no deletes, free list empty\n");
		} else if (getenv("FI_DEL01")) {
			/* free block 0 first -> m_free_pages == [0, 1]; block 0 becomes the chain
			   HEAD, where the sentinel value is harmless */
			FreeImage_DeletePage(m, 0);
			FreeImage_DeletePage(m, 0);
			printf("  CONTROL B: deleted page 0 twice (free list [0, 1]) - 0 becomes the head\n");
		} else {
			/* free block 1 first, then block 0 -> m_free_pages == [1, 0]; block 0
			   becomes the CONTINUATION, which readFile mistakes for end-of-chain */
			FreeImage_DeletePage(m, 1);
			FreeImage_DeletePage(m, 0);
			printf("  TEST: deleted pages 1 then 0 (free list [1, 0]) - 0 becomes a continuation\n");
		}
		/* a page needing exactly two blocks: head pops 1, continuation pops 0 */
		d = noisepage(77, dim, dim);
		want = sum(d);
		printf("  appending a %dx%d page (raw %u bytes, block size 65528)\n",
		       dim, dim, (unsigned)(FreeImage_GetLine(d) * FreeImage_GetHeight(d)));
		FreeImage_AppendPage(m, d);
		FreeImage_Unload(d);
		{
			int np = FreeImage_GetPageCount(m);
			int rc = FreeImage_CloseMultiBitmap(m, 0);
			printf("  pages=%d, close=%d %s\n", np, rc, rc ? "" : "*** SAVE FAILED ***");
		}
		{ FIMULTIBITMAP *r = FreeImage_OpenMultiBitmap(FIF_TIFF, "bz.tif", FALSE, TRUE, TRUE, 0);
		  int np = r ? FreeImage_GetPageCount(r) : -1;
		  printf("  readback pages=%d\n", np);
		  for (k = 0; k < np; k++) {
			FIBITMAP *p = FreeImage_LockPage(r, k);
			printf("    page %d: %s", k, p ? "ok" : "NULL");
			if (p) {
				unsigned g = sum(p);
				printf("  %ux%u checksum=%u", FreeImage_GetWidth(p), FreeImage_GetHeight(p), g);
				if (k == np - 1) printf("  expected=%u  %s", want, g == want ? "MATCH" : "*** CORRUPT ***");
				FreeImage_UnlockPage(r, p, FALSE);
			}
			printf("\n");
		  }
		  if (r) FreeImage_CloseMultiBitmap(r, 0); }

	} else if (!strcmp(cmd, "wrongfif")) {
		/* F2: OpenMultiBitmap does not validate the format */
		FREE_IMAGE_FORMAT fif = (FREE_IMAGE_FORMAT)atoi(argv[2]);
		FIMULTIBITMAP *m;
		printf("  OpenMultiBitmap(%s, \"%s\") ...\n", FreeImage_GetFormatFromFIF(fif), argv[3]);
		fflush(stdout);
		m = FreeImage_OpenMultiBitmap(fif, argv[3], FALSE, FALSE, TRUE, 0);
		if (!m) { printf("  -> NULL (refused)\n"); goto done; }
		printf("  -> accepted, GetPageCount=%d\n", FreeImage_GetPageCount(m));
		fflush(stdout);
		{ FIBITMAP *d = FreeImage_LockPage(m, 0);
		  printf("  LockPage(0) -> %s\n", d ? "non-NULL" : "NULL");
		  if (d) FreeImage_UnlockPage(m, d, FALSE); }
		printf("  Close=%d\n", FreeImage_CloseMultiBitmap(m, 0));

	} else {
		printf("usage: mp <mk|move|lockdel|unlockedit|nonmp|negpage|locknegpage|savelock|cachename|cachestress|wrongfif> ...\n");
	}
done:
	FreeImage_DeInitialise();
	return 0;
}
