// ==========================================================
// The frame delays of an animation, without decoding a pixel
//
// This file is part of FreeImage 3
//
// COVERED CODE IS PROVIDED UNDER THIS LICENSE ON AN "AS IS" BASIS, WITHOUT WARRANTY
// OF ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING, WITHOUT LIMITATION, WARRANTIES
// THAT THE COVERED CODE IS FREE OF DEFECTS, MERCHANTABLE, FIT FOR A PARTICULAR PURPOSE
// OR NON-INFRINGING. THE ENTIRE RISK AS TO THE QUALITY AND PERFORMANCE OF THE COVERED
// CODE IS WITH YOU. SHOULD ANY COVERED CODE PROVE DEFECTIVE IN ANY RESPECT, YOU (NOT
// THE INITIAL DEVELOPER OR ANY OTHER CONTRIBUTOR) ASSUME THE COST OF ANY NECESSARY
// SERVICING, REPAIR OR CORRECTION. THIS DISCLAIMER OF WARRANTY CONSTITUTES AN ESSENTIAL
// PART OF THIS LICENSE. NO USE OF ANY COVERED CODE IS AUTHORIZED HEREUNDER EXCEPT UNDER
// THIS DISCLAIMER.
//
// Use at your own risk!
// ==========================================================

// This sample reads frame delays with FIF_LOAD_NOPIXELS and no playback flag
//
// Functions used in this sample :
// FreeImage_OpenMultiBitmap, FreeImage_GetPageCount, FreeImage_LockPage,
// FreeImage_UnlockPage, FreeImage_CloseMultiBitmap, FreeImage_GetMetadata,
// FreeImage_GetTagType, FreeImage_GetTagValue, FreeImage_GetFileType,
// FreeImage_GetFIFFromFilename, FreeImage_FIFSupportsReading,
// FreeImage_FIFSupportsNoPixels, FreeImage_SetOutputMessage
//
// ==========================================================

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "FreeImage.h"

// a real caller sizes the array from FreeImage_GetPageCount()
#define MAX_FRAMES	4096

// ----------------------------------------------------------

/**
	FreeImage error handler
*/
static void DLL_CALLCONV
MyMessageFunc(FREE_IMAGE_FORMAT fif, const char *message) {
	printf("\n*** ");
	if (fif != FIF_UNKNOWN) {
		printf("%s Format\n", FreeImage_GetFormatFromFIF(fif));
	}
	printf("%s ***\n", message);
}

// ----------------------------------------------------------

static FREE_IMAGE_FORMAT
FormatOf(const char *filename) {
	// content first, then the name
	FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(filename, 0);

	if (fif == FIF_UNKNOWN) {
		fif = FreeImage_GetFIFFromFilename(filename);
	}

	return FreeImage_FIFSupportsReading(fif) ? fif : FIF_UNKNOWN;
}

// ----------------------------------------------------------

// frame duration in ms, 0 when the page has none
static long
GetFrameTime(FIBITMAP *dib) {
	FITAG *tag = NULL;

	if (!FreeImage_GetMetadata(FIMD_ANIMATION, dib, "FrameTime", &tag) || (tag == NULL)) {
		return 0;
	}

	const void *value = FreeImage_GetTagValue(tag);

	if ((value == NULL) || (FreeImage_GetTagType(tag) != FIDT_LONG)) {
		return 0;
	}

	return (long)*(const LONG *)value;
}

// ----------------------------------------------------------

// fills delays[]; returns the frame count (may exceed max_frames) or -1
static int
GetFrameDelays(const char *filename, long *delays, int max_frames) {
	const FREE_IMAGE_FORMAT fif = FormatOf(filename);

	if (fif == FIF_UNKNOWN) {
		return -1;
	}

	// read-only, cache in memory, header-only pages, no playback flag
	FIMULTIBITMAP *animation = FreeImage_OpenMultiBitmap(fif, filename, FALSE, TRUE, TRUE, FIF_LOAD_NOPIXELS);

	if (animation == NULL) {
		return -1;
	}

	const int frame_count = FreeImage_GetPageCount(animation);
	const int wanted = (frame_count < max_frames) ? frame_count : max_frames;

	for (int frame = 0; frame < wanted; frame++) {
		// a pixel-less page; unlock it, do not unload it
		FIBITMAP *dib = FreeImage_LockPage(animation, frame);

		if (dib == NULL) {
			delays[frame] = 0;
			continue;
		}

		delays[frame] = GetFrameTime(dib);

		// FALSE: unchanged
		FreeImage_UnlockPage(animation, dib, FALSE);
	}

	FreeImage_CloseMultiBitmap(animation, 0);

	return frame_count;
}

// ----------------------------------------------------------

static int
PlaybackFlag(FREE_IMAGE_FORMAT fif) {
	switch (fif) {
		case FIF_GIF:
			return GIF_PLAYBACK;
		case FIF_APNG:
			return APNG_PLAYBACK;
		case FIF_WEBP:
			return WEBP_PLAYBACK;
		case FIF_AVIF:
			return AVIF_PLAYBACK;
		case FIF_HEIF:
			return HEIF_PLAYBACK;
		case FIF_MNG:
			return MNG_PLAYBACK;
		default:
			return 0;
	}
}

// the same delays from decoded frames, for timing only
static int
GetFrameDelaysByDecoding(const char *filename, long *delays, int max_frames) {
	const FREE_IMAGE_FORMAT fif = FormatOf(filename);

	if (fif == FIF_UNKNOWN) {
		return -1;
	}

	FIMULTIBITMAP *animation = FreeImage_OpenMultiBitmap(fif, filename, FALSE, TRUE, TRUE, PlaybackFlag(fif));

	if (animation == NULL) {
		return -1;
	}

	const int frame_count = FreeImage_GetPageCount(animation);
	const int wanted = (frame_count < max_frames) ? frame_count : max_frames;

	for (int frame = 0; frame < wanted; frame++) {
		FIBITMAP *dib = FreeImage_LockPage(animation, frame);

		if (dib == NULL) {
			delays[frame] = 0;
			continue;
		}

		delays[frame] = GetFrameTime(dib);

		FreeImage_UnlockPage(animation, dib, FALSE);
	}

	FreeImage_CloseMultiBitmap(animation, 0);

	return frame_count;
}

// ----------------------------------------------------------

// ms between two clock() readings
static double
Elapsed(clock_t from, clock_t to) {
	return (double)(to - from) * 1000.0 / (double)CLOCKS_PER_SEC;
}

// times both methods; their delays must match exactly
static void
CompareCost(const char *filename, const long *delays, int frame_count) {
	static long decoded[MAX_FRAMES];

	const clock_t start = clock();
	GetFrameDelays(filename, decoded, MAX_FRAMES);
	const clock_t middle = clock();
	const int count = GetFrameDelaysByDecoding(filename, decoded, MAX_FRAMES);
	const clock_t end = clock();

	printf("  header only, no playback : %8.1f ms\n", Elapsed(start, middle));
	printf("  decoded and composited   : %8.1f ms\n", Elapsed(middle, end));

	if (count != frame_count) {
		printf("  the two runs disagree : %d frames against %d\n", count, frame_count);
		return;
	}

	const int checked = (frame_count < MAX_FRAMES) ? frame_count : MAX_FRAMES;

	for (int frame = 0; frame < checked; frame++) {
		if (decoded[frame] != delays[frame]) {
			printf("  the two runs disagree at frame %d : %ld ms against %ld ms\n",
				frame, decoded[frame], delays[frame]);
			return;
		}
	}

	printf("  the same %d delay(s), either way\n", checked);
}

// ----------------------------------------------------------

int
main(int argc, char *argv[]) {
	static long delays[MAX_FRAMES];

	if (argc < 2) {
		printf("usage : AnimationDelays <animation>   (an animated GIF, APNG, MNG, WebP, AVIF or HEIF)\n");
		return 1;
	}

	const char *filename = argv[1];

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_Initialise();
#endif // FREEIMAGE_LIB

	// initialize our own FreeImage error handler
	FreeImage_SetOutputMessage(MyMessageFunc);

	const int frame_count = GetFrameDelays(filename, delays, MAX_FRAMES);

	if (frame_count < 0) {
		printf("%s : unknown format, or could not be read\n", filename);
	} else {
		const FREE_IMAGE_FORMAT fif = FormatOf(filename);
		const int shown = (frame_count < MAX_FRAMES) ? frame_count : MAX_FRAMES;

		printf("%s : %d frame(s), %s\n", filename, frame_count,
			FreeImage_FIFSupportsNoPixels(fif) ?
				"read without decoding any of them" :
				"a format with no header-only load, so its frames were decoded after all");

		if (shown < frame_count) {
			printf("  (only the first %d fitted in the array)\n", shown);
		}

		long total = 0;

		for (int frame = 0; frame < shown; frame++) {
			printf("  delays[%4d] = %5ld ms\n", frame, delays[frame]);
			total += delays[frame];
		}

		// one pass; the "Loop" tag gives the pass count (0 = forever)
		printf("  total        = %5ld ms for one pass\n", total);

		if (frame_count > 1) {
			printf("\nwhat the shortcut is worth here :\n");
			CompareCost(filename, delays, frame_count);
		}
	}

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_DeInitialise();
#endif // FREEIMAGE_LIB

	return 0;
}
