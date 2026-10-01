/* FreeImage 3 - MNG robustness test; best run as "make asan-run" */
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

#define W 16
#define H 16

/* any answer is fine; not crashing is the test */
static void survive(const char *path, const char *what) {
	static const int flags[3] = { 0, MNG_PLAYBACK, FIF_LOAD_NOPIXELS };
	int f;

	for (f = 0; f < 3; f++) {
		FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE, TRUE,
													  FALSE, flags[f]);
		if (mb) {
			int pages = FreeImage_GetPageCount(mb);
			int i;

			if (pages < 0) {
				fail("%s: a negative page count (%d)", what, pages);
			}
			for (i = 0; i < pages; i++) {
				FIBITMAP *dib = FreeImage_LockPage(mb, i);
				if (dib) {
					/* touch the pixels so a bad pitch is used */
					if (FreeImage_HasPixels(dib)) {
						RGBQUAD colour;
						FreeImage_GetPixelColor(dib, 0, 0, &colour);
					}
					FreeImage_UnlockPage(mb, dib, FALSE);
				}
			}
			FreeImage_CloseMultiBitmap(mb, 0);
		}

		{
			FIBITMAP *dib = FreeImage_Load(FIF_MNG, path, flags[f]);
			if (dib) {
				FreeImage_Unload(dib);
			}
		}
	}
	ok("%s", what);
}

/* a valid file with every parser-steering chunk, to damage below */
static void build_good(Buf *mng) {
	int i;
	static const BYTE COLOUR[3][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 } };

	buf_init(mng);
	mng_signature(mng);
	mng_mhdr(mng, 64, 64, 100, 3, 3, 0, 3);
	mng_term(mng, 3, 4);
	mng_back(mng, 0x2020, 0x4040, 0x8080, 1);
	mng_fram(mng, 1, 2, 10);
	mng_loop(mng, 0, 2);
	for (i = 0; i < 3; i++) {
		mng_defi(mng, 0, 0, 0, i * 8, i * 8);
		mng_image(mng, W, H, COLOUR[i][0], COLOUR[i][1], COLOUR[i][2], 24);
	}
	mng_endl(mng, 0);
	mng_mend(mng);
}

static void test_truncation(void) {
	Buf good;
	size_t cut;

	printf("truncation\n");
	build_good(&good);

	for (cut = 1; cut < good.size; cut++) {
		Buf part;
		const char *path;

		buf_init(&part);
		buf_add(&part, good.data, cut);
		path = write_file("mng_trunc.mng", &part);
		buf_free(&part);

		{
			static const int flags[2] = { 0, MNG_PLAYBACK };
			int f;
			for (f = 0; f < 2; f++) {
				FIMULTIBITMAP *mb = FreeImage_OpenMultiBitmap(FIF_MNG, path, FALSE,
															  TRUE, FALSE, flags[f]);
				if (mb) {
					int pages = FreeImage_GetPageCount(mb), i;
					for (i = 0; i < pages; i++) {
						FIBITMAP *dib = FreeImage_LockPage(mb, i);
						if (dib) {
							FreeImage_UnlockPage(mb, dib, FALSE);
						}
					}
					FreeImage_CloseMultiBitmap(mb, 0);
				}
			}
		}
	}
	ok("%d truncations of a valid file, all survived", (int)good.size - 1);
	buf_free(&good);
}

static void test_chunk_lengths(void) {
	static const DWORD lengths[] = { 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 100000u };
	size_t i;

	printf("impossible chunk lengths\n");

	for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
		Buf mng;
		const char *path;
		char what[128];

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, W, H, 100, 1, 1, 0, 3);
		buf_u32(&mng, lengths[i]);
		buf_add(&mng, "FRAM", 4);
		buf_u32(&mng, 0);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_mend(&mng);
		path = write_file("mng_badlen.mng", &mng);
		buf_free(&mng);

		snprintf(what, sizeof(what), "a chunk claiming 0x%08x bytes", lengths[i]);
		survive(path, what);
	}
}

static void test_short_fram(void) {
	static const int sizes[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	size_t i;

	printf("a FRAM cut short\n");

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		Buf mng, body;
		const char *path;
		char what[128];
		int n;

		buf_init(&body);
		buf_byte(&body, 1);          /* framing mode */
		buf_byte(&body, 0);          /* the empty name's separator */
		buf_byte(&body, 2);          /* change_interframe_delay: reset the default */
		buf_byte(&body, 1);          /* change_timeout: promises four more bytes */
		buf_byte(&body, 2);          /* change_clipping: promises seventeen more */
		buf_byte(&body, 1);          /* change_sync_id_list */
		buf_u32(&body, 25);          /* the delay, and then nothing else */
		/* keep only the first `sizes[i]` bytes of that */
		n = sizes[i] < (int)body.size ? sizes[i] : (int)body.size;

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, W, H, 100, 1, 1, 0, 3);
		chunk(&mng, "FRAM", body.data, (DWORD)n);
		buf_free(&body);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_mend(&mng);
		path = write_file("mng_shortfram.mng", &mng);
		buf_free(&mng);

		snprintf(what, sizeof(what), "a %d-byte FRAM", n);
		survive(path, what);
	}
}

static void test_defi_lengths(void) {
	int n;

	printf("a DEFI of every length\n");

	for (n = 0; n <= 28; n++) {
		Buf mng;
		const char *path;
		BYTE body[28];
		char what[64];

		memset(body, 0, sizeof(body));
		body[1] = 3;             /* object id 3 */
		body[7] = 10;            /* part of an x location */
		body[11] = 20;           /* part of a y location */

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, 64, 64, 100, 1, 1, 0, 3);
		chunk(&mng, "DEFI", body, (DWORD)n);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_mend(&mng);
		path = write_file("mng_defi_n.mng", &mng);
		buf_free(&mng);

		snprintf(what, sizeof(what), "a %d-byte DEFI", n);
		survive(path, what);
	}
}

static void test_unterminated_image(void) {
	Buf mng, good;
	const char *path;
	size_t i, cut = 0;

	printf("an embedded image with no IEND\n");

	build_good(&good);
	/* find the first IEND and stop the file just before it */
	for (i = 8; i + 8 <= good.size; i++) {
		if (memcmp(good.data + i + 4, "IEND", 4) == 0) {
			cut = i;
			break;
		}
	}
	buf_init(&mng);
	buf_add(&mng, good.data, cut ? cut : good.size);
	buf_free(&good);
	path = write_file("mng_noiend.mng", &mng);
	buf_free(&mng);

	survive(path, "an image whose IEND never arrives");
}

static void test_misplaced_chunks(void) {
	Buf mng;
	const char *path;

	printf("misplaced chunks\n");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 0, 0, 0, 3);
	mng_mend(&mng);
	path = write_file("mng_empty.mng", &mng);
	buf_free(&mng);
	survive(path, "a MEND with no image before it");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 3, 3, 0, 3);
	path = write_file("mng_headeronly.mng", &mng);
	buf_free(&mng);
	survive(path, "a MHDR and nothing else");

	buf_init(&mng);
	mng_signature(&mng);
	path = write_file("mng_sigonly.mng", &mng);
	buf_free(&mng);
	survive(path, "a signature and nothing else");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 1, 1, 0, 3);
	mng_endl(&mng, 0);
	mng_image(&mng, W, H, 255, 0, 0, 24);
	mng_loop(&mng, 0, 4);
	mng_mend(&mng);
	path = write_file("mng_unbalanced.mng", &mng);
	buf_free(&mng);
	survive(path, "an ENDL with no LOOP and a LOOP with no ENDL");

	buf_init(&mng);
	mng_signature(&mng);
	mng_mhdr(&mng, W, H, 100, 1, 1, 0, 3);
	mng_loop(&mng, 0, 0);
	mng_image(&mng, W, H, 255, 0, 0, 24);
	mng_mend(&mng);
	path = write_file("mng_loop0_open.mng", &mng);
	buf_free(&mng);
	survive(path, "a zero-count LOOP with no ENDL");

	{
		int d;
		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, W, H, 100, 1, 1, 0, 3);
		for (d = 0; d < 250; d++) {
			mng_loop(&mng, (BYTE)d, 2);
		}
		mng_image(&mng, W, H, 255, 0, 0, 24);
		for (d = 249; d >= 0; d--) {
			mng_endl(&mng, (BYTE)d);
		}
		mng_mend(&mng);
		path = write_file("mng_deep.mng", &mng);
		buf_free(&mng);
		survive(path, "250 nested loops");
	}
}

static void test_show_ranges(void) {
	static const struct { WORD first, last; BYTE mode; const char *what; } cases[] = {
		{ 1,      1,      0, "SHOW of an object that was never defined" },
		{ 1,      0xFFFF, 0, "SHOW of every object id there is" },
		{ 0xFFFF, 1,      0, "SHOW with the range the wrong way round" },
		{ 1,      2,      6, "SHOW stepping through a range" },
		{ 1,      2,      7, "SHOW stepping without displaying" },
		{ 1,      2,      9, "SHOW with a mode the spec does not define" },
	};
	size_t i;

	printf("SHOW ranges\n");

	for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
		Buf mng;
		const char *path;
		int s;

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, W, H, 100, 2, 2, 0, 3);
		mng_fram(&mng, 1, 2, 10);
		mng_defi(&mng, 1, 1, 0, 0, 0);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_defi(&mng, 2, 1, 0, 0, 0);
		mng_image(&mng, W, H, 0, 255, 0, 24);
		/* several SHOWs, so a stepping mode gets to step */
		for (s = 0; s < 5; s++) {
			mng_show(&mng, cases[i].first, cases[i].last, cases[i].mode);
		}
		mng_mend(&mng);
		path = write_file("mng_show_range.mng", &mng);
		buf_free(&mng);

		survive(path, cases[i].what);
	}
}

static void test_bad_crc(void) {
	static const char *types[] = { "MHDR", "FRAM", "DEFI", "BACK", "TERM", "LOOP" };
	size_t t;

	printf("bad CRCs\n");

	for (t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
		Buf good;
		const char *path;
		size_t i;
		int found = 0;

		build_good(&good);
		/* flip a bit in the named chunk's CRC */
		for (i = 8; i + 12 <= good.size; ) {
			DWORD length = ((DWORD)good.data[i] << 24) | ((DWORD)good.data[i + 1] << 16)
						 | ((DWORD)good.data[i + 2] << 8) | good.data[i + 3];
			if (length > good.size) {
				break;
			}
			if (memcmp(good.data + i + 4, types[t], 4) == 0) {
				good.data[i + 8 + length + 3] ^= 0x01;
				found = 1;
				break;
			}
			i += 12 + length;
		}
		if (found) {
			char what[64];
			path = write_file("mng_badcrc.mng", &good);
			snprintf(what, sizeof(what), "a %s with a bad CRC", types[t]);
			survive(path, what);
		} else {
			fail("build_good() carries no %s to damage", types[t]);
		}
		buf_free(&good);
	}
}

static void test_huge_canvas(void) {
	static const DWORD sizes[][2] = {
		{ 0xFFFFFFFFu, 0xFFFFFFFFu },
		{ 0x7FFFFFFFu, 0x7FFFFFFFu },
		{ 100000u, 100000u },
		{ 0u, 0u },
	};
	size_t i;

	printf("an impossible canvas\n");

	for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
		Buf mng;
		const char *path;
		char what[96];

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, sizes[i][0], sizes[i][1], 100, 1, 1, 0, 3);
		mng_fram(&mng, 1, 2, 10);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_mend(&mng);
		path = write_file("mng_bigcanvas.mng", &mng);
		buf_free(&mng);

		snprintf(what, sizeof(what), "a %ux%u canvas", sizes[i][0], sizes[i][1]);
		survive(path, what);
	}
}

static void test_off_canvas(void) {
	static const LONG places[][2] = {
		{ -1000, -1000 }, { 1000, 1000 }, { -8, -8 },
		{ 0x7FFFFFF0, 0x7FFFFFF0 }, { -2147483647 - 1, -2147483647 - 1 },
	};
	size_t i;

	printf("images off the canvas\n");

	for (i = 0; i < sizeof(places) / sizeof(places[0]); i++) {
		Buf mng;
		const char *path;
		char what[96];

		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, 32, 32, 100, 1, 1, 0, 3);
		mng_fram(&mng, 1, 2, 10);
		mng_defi(&mng, 0, 0, 0, places[i][0], places[i][1]);
		mng_image(&mng, W, H, 255, 0, 0, 24);
		mng_mend(&mng);
		path = write_file("mng_offcanvas.mng", &mng);
		buf_free(&mng);

		snprintf(what, sizeof(what), "an image at (%ld,%ld)",
				 (long)places[i][0], (long)places[i][1]);
		survive(path, what);
	}
}

static void test_garbage(void) {
	Buf mng;
	const char *path;
	int i;

	printf("garbage behind a valid signature\n");

	buf_init(&mng);
	mng_signature(&mng);
	for (i = 0; i < 4096; i++) {
		buf_byte(&mng, (BYTE)((i * 37) ^ (i >> 3)));
	}
	path = write_file("mng_garbage.mng", &mng);
	buf_free(&mng);

	survive(path, "4 KB of garbage behind a MNG signature");
}

/* an iCCP payload: name, NUL, method, then data zlib-compressed (raw when compress is 0) */
static void iccp(Buf *out, const char *name, int name_nul, int method, const BYTE *data, DWORD size, int compress) {
	buf_init(out);
	buf_add(out, name, strlen(name));
	if (name_nul) {
		buf_byte(out, 0);
	}
	if (method >= 0) {
		buf_byte(out, (BYTE)method);
	}
	if (data && compress) {
		const DWORD bound = size + size / 1000 + 64;
		BYTE *packed = (BYTE *)malloc(bound);
		const DWORD n = packed ? FreeImage_ZLibCompress(packed, bound, (BYTE *)data, size) : 0;
		if (n) {
			buf_add(out, packed, compress > 0 ? n : n / 2);
		}
		free(packed);
	} else if (data) {
		buf_add(out, data, size);
	}
}

/* damaged iCCP chunks in a JNG with alpha: loaded without a profile, standalone and as an MNG frame */
static void test_jng_iccp(void) {
	DWORD adobe_size = 0, grey_size = 0;
	const BYTE *adobe = (const BYTE *)FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_ADOBE_RGB, &adobe_size);
	const BYTE *grey = (const BYTE *)FreeImage_GetBuiltInICCProfile(FICMS_PROFILE_GRAY, &grey_size);
	FIBITMAP *img = FreeImage_Allocate(W, H, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	FIMEMORY *mem = FreeImage_OpenMemory(NULL, 0);
	BYTE *jng = NULL, *wrong = (BYTE *)malloc(adobe_size), *zeros = (BYTE *)calloc(9000000, 1), noise[1000];
	DWORD jng_size = 0, jhdr_end = 8 + 12 + 16;
	char long_name[100];
	unsigned i;
	int k;

	printf("damaged iCCP chunks in a JNG\n");

	for (i = 0; i < FreeImage_GetPitch(img) * H; i++) {
		FreeImage_GetBits(img)[i] = (BYTE)(i * 5 + 1);
	}
	for (i = 0; i < sizeof(noise); i++) {
		noise[i] = (BYTE)(i * 131 + 7);
	}
	memset(long_name, 'a', sizeof(long_name) - 1);
	long_name[sizeof(long_name) - 1] = 0;
	memcpy(wrong, adobe, adobe_size);
	wrong[3] ^= 4;	/* the declared size no longer matches */
	if (!FreeImage_SaveToMemory(FIF_JNG, img, mem, 0) || !FreeImage_AcquireMemory(mem, &jng, &jng_size) || jng_size < jhdr_end || !zeros) {
		fail("no JNG to damage");
		goto out;
	}

	for (k = 0; k < 12; k++) {
		static const char *what[12] = {
			"an empty iCCP", "a name of 99 bytes", "no compression method", "compression method 1",
			"data that is not zlib", "a cut zlib stream", "a profile whose size field lies",
			"9 MB of zeros", "a grey profile in a colour JNG", "1000 bytes that are no profile",
			"no NUL after the name", "a damaged iCCP before a good one"
		};
		Buf payload, file, mng;
		char name[64];
		FIBITMAP *dib;
		int f;
		switch (k) {
			case 0: buf_init(&payload); break;
			case 1: iccp(&payload, long_name, 1, 0, adobe, adobe_size, 1); break;
			case 2: iccp(&payload, "p", 1, -1, NULL, 0, 0); break;
			case 3: iccp(&payload, "p", 1, 1, adobe, adobe_size, 1); break;
			case 4: iccp(&payload, "p", 1, 0, noise, sizeof(noise), 0); break;
			case 5: iccp(&payload, "p", 1, 0, adobe, adobe_size, -1); break;
			case 6: iccp(&payload, "p", 1, 0, wrong, adobe_size, 1); break;
			case 7: iccp(&payload, "p", 1, 0, zeros, 9000000, 1); break;
			case 8: iccp(&payload, "p", 1, 0, grey, grey_size, 1); break;
			case 9: iccp(&payload, "p", 1, 0, noise, sizeof(noise), 1); break;
			case 10: iccp(&payload, "profile", 0, 0, adobe, adobe_size, 1); break;
			default: iccp(&payload, "p", 1, 0, noise, sizeof(noise), 1); break;
		}
		buf_init(&file);
		buf_add(&file, jng, jhdr_end);
		chunk(&file, "iCCP", payload.data, (DWORD)payload.size);
		if (k == 11) {
			Buf good;
			iccp(&good, "p", 1, 0, adobe, adobe_size, 1);
			chunk(&file, "iCCP", good.data, (DWORD)good.size);
			buf_free(&good);
		}
		buf_add(&file, jng + jhdr_end, jng_size - jhdr_end);

		for (f = 0; f < 2; f++) {
			FIMEMORY *in = FreeImage_OpenMemory(file.data, (DWORD)file.size);
			dib = FreeImage_LoadFromMemory(FIF_JNG, in, f ? FIF_LOAD_NOPIXELS : 0);
			if (!dib) {
				fail("%s: the JNG did not load%s", what[k], f ? " header-only" : "");
			} else if (FreeImage_GetICCProfile(dib)->data) {
				fail("%s: a %u-byte profile came through%s", what[k], (unsigned)FreeImage_GetICCProfile(dib)->size, f ? " header-only" : "");
			} else if (!f && FreeImage_GetBPP(dib) != 32) {
				fail("%s: %u bits per pixel", what[k], FreeImage_GetBPP(dib));
			}
			if (dib) {
				FreeImage_Unload(dib);
			}
			FreeImage_CloseMemory(in);
		}

		/* the same JNG as an MNG frame */
		buf_init(&mng);
		mng_signature(&mng);
		mng_mhdr(&mng, W, H, 10, 1, 1, 0, 1 | (1 << 4));
		buf_add(&mng, file.data + 8, file.size - 8);
		mng_mend(&mng);
		snprintf(name, sizeof(name), "mng_jng_iccp_%d.mng", k);
		{
			const char *path = write_file(name, &mng);
			survive(path, what[k]);
			remove(path);
		}
		buf_free(&mng);
		buf_free(&file);
		buf_free(&payload);
	}

out:
	FreeImage_CloseMemory(mem);
	FreeImage_Unload(img);
	free(wrong);
	free(zeros);
}

/* --------------------------------------------------------------------- */

int main(void) {
	FreeImage_Initialise(FALSE);
	FreeImage_SetOutputMessage(quiet);

	test_truncation();
	test_chunk_lengths();
	test_short_fram();
	test_defi_lengths();
	test_unterminated_image();
	test_misplaced_chunks();
	test_show_ranges();
	test_bad_crc();
	test_huge_canvas();
	test_off_canvas();
	test_garbage();
	test_jng_iccp();

	FreeImage_DeInitialise();

	printf("\n%d check(s), %d failure(s)\n", checks, failures);
	return failures ? 1 : 0;
}
