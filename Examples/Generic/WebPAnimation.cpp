// ==========================================================
// Animated WebP playback demonstration
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

// This sample plays animated WebP/GIF, composited and as stored
// Seeking backwards replays from the first frame: play forward
//
// Functions used in this sample :
// FreeImage_OpenMultiBitmap, FreeImage_LoadMultiBitmapFromMemory,
// FreeImage_GetPageCount, FreeImage_LockPage, FreeImage_UnlockPage,
// FreeImage_CloseMultiBitmap, FreeImage_GetMetadata, FreeImage_SetOutputMessage
//
// ==========================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeImage.h"

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

// the tags differ in width (LONG, SHORT, BYTE): check the type
static long
GetAnimationTag(FIBITMAP *dib, const char *key, long defaultValue) {
	FITAG *tag = NULL;

	if (!FreeImage_GetMetadata(FIMD_ANIMATION, dib, key, &tag) || (tag == NULL)) {
		return defaultValue;
	}

	const void *value = FreeImage_GetTagValue(tag);
	if (value == NULL) {
		return defaultValue;
	}

	switch (FreeImage_GetTagType(tag)) {
		case FIDT_BYTE:
			return *(const BYTE *)value;
		case FIDT_SHORT:
			return *(const WORD *)value;
		case FIDT_LONG:
			return *(const LONG *)value;
		default:
			return defaultValue;
	}
}

// ----------------------------------------------------------

// fif: FIF_WEBP or FIF_GIF, playback_flag to match
static BOOL
PlayAnimation(FREE_IMAGE_FORMAT fif, const char *filename, int playback_flag) {
	// read-only, cache in memory; flags apply to every LockPage()
	FIMULTIBITMAP *animation = FreeImage_OpenMultiBitmap(fif, filename, FALSE, TRUE, TRUE, playback_flag);

	if (animation == NULL) {
		return FALSE;
	}

	// 1 for a still image
	const int frame_count = FreeImage_GetPageCount(animation);

	printf("%s : %d frame(s)\n", filename, frame_count);

	long elapsed = 0;

	for (int frame = 0; frame < frame_count; frame++) {
		// owned by the multibitmap until unlocked: do not unload
		FIBITMAP *dib = FreeImage_LockPage(animation, frame);

		if (dib == NULL) {
			// damaged frame: keep going
			printf("  frame %3d : could not be decoded\n", frame);
			continue;
		}

		// milliseconds, for WebP and GIF alike
		const long delay = GetAnimationTag(dib, "FrameTime", 0);

		if (frame == 0) {
			// canvas and loop count (0 = forever)
			printf("  canvas    : %ld x %ld, loop %ld\n",
				GetAnimationTag(dib, "LogicalWidth", (long)FreeImage_GetWidth(dib)),
				GetAnimationTag(dib, "LogicalHeight", (long)FreeImage_GetHeight(dib)),
				GetAnimationTag(dib, "Loop", 0));
		}

		// draw dib (32-bit canvas), then wait 'delay' ms
		printf("  frame %3d : %u x %u, %u bpp, %ld ms (at %ld ms)\n",
			frame, FreeImage_GetWidth(dib), FreeImage_GetHeight(dib),
			FreeImage_GetBPP(dib), delay, elapsed);

		elapsed += delay;

		// FALSE: no changes to keep
		FreeImage_UnlockPage(animation, dib, FALSE);
	}

	printf("  total     : %ld ms\n", elapsed);

	FreeImage_CloseMultiBitmap(animation, 0);

	return TRUE;
}

// ----------------------------------------------------------

// the frames as stored, for re-encoding or compositing yourself
static BOOL
ShowFrameLayout(FREE_IMAGE_FORMAT fif, const char *filename) {
	FIMULTIBITMAP *animation = FreeImage_OpenMultiBitmap(fif, filename, FALSE, TRUE, TRUE, 0);

	if (animation == NULL) {
		return FALSE;
	}

	const int frame_count = FreeImage_GetPageCount(animation);

	printf("%s : how the %d frame(s) are stored\n", filename, frame_count);

	for (int frame = 0; frame < frame_count; frame++) {
		FIBITMAP *dib = FreeImage_LockPage(animation, frame);

		if (dib == NULL) {
			continue;
		}

		printf("  frame %3d : %4u x %-4u at %4ld,%-4ld  %ld ms  dispose=%ld blend=%ld\n",
			frame, FreeImage_GetWidth(dib), FreeImage_GetHeight(dib),
			GetAnimationTag(dib, "FrameLeft", 0),
			GetAnimationTag(dib, "FrameTop", 0),
			GetAnimationTag(dib, "FrameTime", 0),
			GetAnimationTag(dib, "DisposalMethod", 1),
			GetAnimationTag(dib, "BlendMethod", 0));

		FreeImage_UnlockPage(animation, dib, FALSE);
	}

	FreeImage_CloseMultiBitmap(animation, 0);

	return TRUE;
}

// ----------------------------------------------------------

// buffer outlives the FIMEMORY, which outlives the FIMULTIBITMAP
static BOOL
PlayAnimationFromMemory(FREE_IMAGE_FORMAT fif, const char *filename, int playback_flag) {
	FILE *file = fopen(filename, "rb");
	if (file == NULL) {
		return FALSE;
	}

	fseek(file, 0, SEEK_END);
	const long file_size = ftell(file);
	fseek(file, 0, SEEK_SET);

	BYTE *buffer = (BYTE *)malloc(file_size);
	if (buffer == NULL) {
		fclose(file);
		return FALSE;
	}
	if (fread(buffer, 1, file_size, file) != (size_t)file_size) {
		free(buffer);
		fclose(file);
		return FALSE;
	}
	fclose(file);

	// FreeImage does not take ownership of the buffer
	FIMEMORY *stream = FreeImage_OpenMemory(buffer, (DWORD)file_size);

	FIMULTIBITMAP *animation = FreeImage_LoadMultiBitmapFromMemory(fif, stream, playback_flag);

	BOOL success = FALSE;

	if (animation != NULL) {
		const int frame_count = FreeImage_GetPageCount(animation);

		printf("%s : %d frame(s), from memory\n", filename, frame_count);

		for (int frame = 0; frame < frame_count; frame++) {
			FIBITMAP *dib = FreeImage_LockPage(animation, frame);
			if (dib != NULL) {
				// ... draw it, wait for FrameTime milliseconds ...
				FreeImage_UnlockPage(animation, dib, FALSE);
			}
		}

		FreeImage_CloseMultiBitmap(animation, 0);
		success = TRUE;
	}

	FreeImage_CloseMemory(stream);
	free(buffer);

	return success;
}

// ----------------------------------------------------------

int
main(int argc, char *argv[]) {
	const char *filename = (argc > 1) ? argv[1] : "images/animation.webp";

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_Initialise();
#endif // FREEIMAGE_LIB

	// initialize our own FreeImage error handler
	FreeImage_SetOutputMessage(MyMessageFunc);

	// detect the format from the content
	FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(filename, 0);
	if (fif == FIF_UNKNOWN) {
		fif = FreeImage_GetFIFFromFilename(filename);
	}

	// only the playback flag differs per format
	int playback_flag = 0;
	if (fif == FIF_WEBP) {
		playback_flag = WEBP_PLAYBACK;
	} else if (fif == FIF_GIF) {
		playback_flag = GIF_PLAYBACK;
	}

	if (fif == FIF_UNKNOWN || !FreeImage_FIFSupportsReading(fif)) {
		printf("%s : unknown or unsupported format\n", filename);
	} else {
		// what it looks like, frame by frame
		if (PlayAnimation(fif, filename, playback_flag)) {
			printf("\n");
			// how the frames are stored
			ShowFrameLayout(fif, filename);
			printf("\n");
			// the same thing, from a memory stream
			PlayAnimationFromMemory(fif, filename, playback_flag);
		} else {
			printf("%s : could not be opened\n", filename);
		}
	}

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_DeInitialise();
#endif // FREEIMAGE_LIB

	return 0;
}
