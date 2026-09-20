// ==========================================================
// The frame delays of an animation, without decoding a pixel
//
// Fills delays[frame] with how long every frame of an animation stays on
// screen, at the lowest cost this library allows.
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

// A player needs the timeline of an animation before it needs any of its
// pictures: to say how long the thing runs, to draw a scrubber, to decide
// which frame belongs to a moment. None of that wants pixels, and this shows
// how to get it without paying for any.
//
// Every animated format the library reads - GIF, APNG, animated WebP and AVIF
// image sequences - describes a frame with the same FIMD_ANIMATION tags, and
// the one that says how long the frame lasts is "FrameTime": a FIDT_LONG of
// milliseconds. Only WebP stores it that way - GIF holds hundredths of a
// second, APNG and AVIF each a rational number of seconds - but every plugin
// converts, so one loop reads all four formats.
//
// Tags arrive with the page, and the page does not have to carry any pixels
// for them to. Three decisions, all made before the loop starts, are what make
// the walk cheap:
//
//  1. One session for the whole file. FreeImage_OpenMultiBitmap() opens the
//     decoder once and every FreeImage_LockPage() reuses it. Loading frames
//     one at a time instead re-parses the file from the beginning each time -
//     for a GIF, a scan of every block in it.
//
//  2. FIF_LOAD_NOPIXELS. A plugin that acts on the flag returns a bitmap that
//     is a header and its metadata with no pixel buffer behind it: nothing was
//     decoded and nothing the size of an image was allocated. Plugins that do
//     not act on it ignore it, so it is always safe to pass, and
//     FreeImage_FIFSupportsNoPixels() says which ones will. Every animated
//     format here does, and the figures printed at the end are what the flag is
//     worth on the file it was given.
//
//  3. No playback flag. GIF_PLAYBACK, APNG_PLAYBACK, WEBP_PLAYBACK,
//     AVIF_PLAYBACK and MNG_PLAYBACK ask for the canvas a viewer would show at
//     this frame, which is a 32-bit allocation and a composite on top of the
//     decoding. It buys nothing here: the animation tags describe the frame as
//     the file stores it and are on the page either way.
//
// With the pixels gone, so is the one ordering rule playback imposes. The GIF,
// APNG and MNG plugins remember the canvas they last drew, so under a playback
// flag asking for the next frame is cheap while asking for an earlier one replays
// the animation from the beginning. Nothing is composited here, so the delays
// can be read in any order; this walks them forwards only because that is the
// natural way to fill an array.
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

// As many frames as this sample will report on. A real caller would size the
// array from FreeImage_GetPageCount(), which GetFrameDelays() returns below
// even when it is larger than the array it was given.
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

/**
	The format of a file : what is in it, rather than what it is called.

	@param filename File to identify
	@return Its format, or FIF_UNKNOWN if nothing here can read it
*/
static FREE_IMAGE_FORMAT
FormatOf(const char *filename) {
	// what the first bytes say, and only then what the name says
	FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(filename, 0);

	if (fif == FIF_UNKNOWN) {
		fif = FreeImage_GetFIFFromFilename(filename);
	}

	return FreeImage_FIFSupportsReading(fif) ? fif : FIF_UNKNOWN;
}

// ----------------------------------------------------------

/**
	How long this frame stays on screen, in milliseconds.

	"FrameTime" is a FIDT_LONG in every plugin that writes it, so the type is
	checked rather than assumed: a page with no such tag is a still image, or a
	page of a format that is paged but not animated - a multi-page TIFF, the
	several pictures in a HEIF file - and has no duration to report.

	@param dib Page to read
	@return The frame's duration in ms, or 0 when the page does not declare one
*/
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

/**
	Read the duration of every frame of an animation, and nothing else.

	This is the whole point of the sample: a header-only, no-playback walk of
	the pages, filling delays[frame] as it goes. Where the plugin has a
	header-only load - FreeImage_FIFSupportsNoPixels() says which do, and every
	animated format here does - no frame is decoded and no image-sized buffer is
	allocated.

	@param filename Animation to read
	@param delays Array to fill, max_frames entries long
	@param max_frames How many entries delays has room for
	@return The number of frames in the file - which is larger than max_frames
	if the array was too small, in which case only the first max_frames delays
	were written - or -1 if the file could not be read
*/
static int
GetFrameDelays(const char *filename, long *delays, int max_frames) {
	const FREE_IMAGE_FORMAT fif = FormatOf(filename);

	if (fif == FIF_UNKNOWN) {
		return -1;
	}

	// FALSE : the file exists, do not create one
	// TRUE  : read-only, so nothing here can write back over it
	// TRUE  : keep the page cache in memory rather than in a temporary file.
	//         A read-only session that changes nothing never fills the cache,
	//         so this only avoids creating the file at all.
	// FIF_LOAD_NOPIXELS : the flags every FreeImage_LockPage() below will use.
	//         Note what is *not* here - GIF_PLAYBACK, APNG_PLAYBACK,
	//         WEBP_PLAYBACK, AVIF_PLAYBACK - which is what keeps this cheap.
	FIMULTIBITMAP *animation = FreeImage_OpenMultiBitmap(fif, filename, FALSE, TRUE, TRUE, FIF_LOAD_NOPIXELS);

	if (animation == NULL) {
		return -1;
	}

	// A still image is a single page, so this is 1 for an ordinary .gif
	const int frame_count = FreeImage_GetPageCount(animation);
	const int wanted = (frame_count < max_frames) ? frame_count : max_frames;

	for (int frame = 0; frame < wanted; frame++) {
		// The page: a header carrying this frame's metadata, with no pixels
		// behind it. It belongs to the FIMULTIBITMAP until it is unlocked, so
		// do not unload it.
		FIBITMAP *dib = FreeImage_LockPage(animation, frame);

		if (dib == NULL) {
			// a damaged frame: the rest of the file may still be readable
			delays[frame] = 0;
			continue;
		}

		delays[frame] = GetFrameTime(dib);

		// FALSE : nothing was changed, so nothing is written back
		FreeImage_UnlockPage(animation, dib, FALSE);
	}

	FreeImage_CloseMultiBitmap(animation, 0);

	return frame_count;
}

// ----------------------------------------------------------

/**
	The playback flag of a format, for the comparison below.

	@param fif Format to ask about
	@return The flag that asks this plugin for composited frames, or 0
*/
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
		case FIF_MNG:
			return MNG_PLAYBACK;
		default:
			return 0;
	}
}

/**
	The same delays, read off frames that were decoded and composited.

	This is what GetFrameDelays() avoids, and it is here only so the two can be
	timed against each other. It is the loop a player runs - and has to, since
	it wants the pictures as well - but it is the wrong one to run when all that
	is wanted is the timeline.

	@param filename Animation to read
	@param delays Array to fill, max_frames entries long
	@param max_frames How many entries delays has room for
	@return The number of frames in the file, or -1 if it could not be read
*/
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

/**
	Milliseconds between two clock() readings.
*/
static double
Elapsed(clock_t from, clock_t to) {
	return (double)(to - from) * 1000.0 / (double)CLOCKS_PER_SEC;
}

/**
	Time both ways of reading the delays, and check that they agree.

	The point of the check is that the quick way is not an approximation of the
	slow one. It reads the same tag off the same pages; all it leaves out is the
	decoding, so the two arrays have to match exactly.

	@param filename Animation to read
	@param delays The delays GetFrameDelays() already returned
	@param frame_count How many of them there are
*/
static void
CompareCost(const char *filename, const long *delays, int frame_count) {
	static long decoded[MAX_FRAMES];

	// The file is in the operating system's cache by now, so neither run pays
	// for reading it off the disk and the difference is the decoding alone.
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
		printf("usage : AnimationDelays <animation>   (an animated GIF, APNG, WebP or AVIF)\n");
		return 1;
	}

	const char *filename = argv[1];

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_Initialise();
#endif // FREEIMAGE_LIB

	// initialize our own FreeImage error handler
	FreeImage_SetOutputMessage(MyMessageFunc);

	// this is the whole job : delays[frame] is now the duration of that frame
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

		// what a player needs before it draws anything : how long one pass of
		// the animation lasts. The "Loop" tag, on the same pages, says how many
		// passes there are - 0 meaning forever.
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
