// ==========================================================
// Animated WebP playback demonstration
//
// Reads an animated WebP the way a viewer would: one composited frame per
// page, in order, with the delay each frame asks for.
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

// An animated WebP is a canvas plus a list of frames, each of which paints a
// rectangle somewhere on that canvas. A frame on its own is therefore not a
// picture: it has to be drawn over what the frames before it left behind. The
// WEBP_PLAYBACK load flag asks the plugin to do that and hand back the finished
// canvas, 32-bit, one per page. Without the flag a page is the rectangle the
// file actually stores, which is what you want if you are re-encoding rather
// than displaying. Both are shown below.
//
// GIF works the same way, with GIF_PLAYBACK in place of WEBP_PLAYBACK, so the
// loop in PlayAnimation() below drives either format unchanged once the flag
// has been chosen. HEIF and AVIF also page. Their frames are always whole
// pictures, so nothing has to be composited there; AVIF_PLAYBACK only asks for
// them as 32-bit images, which is the form this loop wants.
//
// One thing worth knowing before you build a UI on this: the decoder underneath
// can only move forwards, so it remembers the frame it last produced. Asking
// for the next frame, or for the frame you already have, is cheap; jumping
// backwards replays the animation from the beginning. Play forwards and loop by
// starting over, rather than stepping back one frame at a time.
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

/**
	Read one FIMD_ANIMATION tag as an integer.

	The animation tags are not all the same width - FrameTime is a LONG,
	FrameLeft and FrameTop are SHORTs, DisposalMethod and BlendMethod are
	single bytes - so read the type before the value rather than casting.

	@param dib Bitmap to read from
	@param key Tag name, e.g. "FrameTime"
	@param defaultValue Returned when the tag is absent (a still image has none)
	@return The tag value, or defaultValue
*/
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

/**
	Play an animation from a file, one composited frame at a time.

	@param fif FIF_WEBP, or FIF_GIF with GIF_PLAYBACK in playback_flag
	@param filename File to read
	@param playback_flag WEBP_PLAYBACK or GIF_PLAYBACK
	@return TRUE if the file was opened
*/
static BOOL
PlayAnimation(FREE_IMAGE_FORMAT fif, const char *filename, int playback_flag) {
	// Open read-only, with the cache in memory. The load flags given here are
	// the ones every FreeImage_LockPage() call will use.
	FIMULTIBITMAP *animation = FreeImage_OpenMultiBitmap(fif, filename, FALSE, TRUE, TRUE, playback_flag);

	if (animation == NULL) {
		return FALSE;
	}

	// A still image is a single page, so this is 1 for an ordinary .webp
	const int frame_count = FreeImage_GetPageCount(animation);

	printf("%s : %d frame(s)\n", filename, frame_count);

	long elapsed = 0;

	for (int frame = 0; frame < frame_count; frame++) {
		// The composited canvas for this frame. It belongs to the
		// FIMULTIBITMAP until it is unlocked, so do not unload it.
		FIBITMAP *dib = FreeImage_LockPage(animation, frame);

		if (dib == NULL) {
			// A damaged frame: the rest of the file may still be readable
			printf("  frame %3d : could not be decoded\n", frame);
			continue;
		}

		// How long this frame stays on screen, in milliseconds. WebP stores
		// milliseconds; the GIF plugin converts its hundredths of a second to
		// the same unit, so this reads the same for both formats.
		const long delay = GetAnimationTag(dib, "FrameTime", 0);

		if (frame == 0) {
			// The canvas the frames are drawn on, and how many times the
			// animation is meant to repeat - 0 means forever. These are only
			// attached to the first frame.
			printf("  canvas    : %ld x %ld, loop %ld\n",
				GetAnimationTag(dib, "LogicalWidth", (long)FreeImage_GetWidth(dib)),
				GetAnimationTag(dib, "LogicalHeight", (long)FreeImage_GetHeight(dib)),
				GetAnimationTag(dib, "Loop", 0));
		}

		// dib is a 32-bit BGRA bitmap the size of the canvas: hand it to
		// whatever draws, then wait `delay` milliseconds before the next one.
		printf("  frame %3d : %u x %u, %u bpp, %ld ms (at %ld ms)\n",
			frame, FreeImage_GetWidth(dib), FreeImage_GetHeight(dib),
			FreeImage_GetBPP(dib), delay, elapsed);

		elapsed += delay;

		// FALSE : this is a read-only bitmap, keep no changes
		FreeImage_UnlockPage(animation, dib, FALSE);
	}

	printf("  total     : %ld ms\n", elapsed);

	FreeImage_CloseMultiBitmap(animation, 0);

	return TRUE;
}

// ----------------------------------------------------------

/**
	Show how the file stores its frames, rather than how they look.

	Without a playback flag a page is the rectangle the file holds, at its own
	size. That is what you want when re-encoding, or when doing the compositing
	yourself - in which case FrameLeft, FrameTop, DisposalMethod and BlendMethod
	are what tell you where the rectangle goes and what to do with the canvas
	around it.

	DisposalMethod uses the GIF numbering for both formats : 1 leaves the canvas
	as it is, 2 restores this frame's rectangle to the background before the
	next frame is drawn. BlendMethod is WebP only - GIF has a single fully
	transparent colour instead - and says whether the frame is alpha-blended
	over the canvas (0) or overwrites it (1).

	@param fif FIF_WEBP or FIF_GIF
	@param filename File to read
	@return TRUE if the file was opened
*/
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

/**
	The same playback, from a file already in memory.

	This is the form a viewer usually wants: the bytes are read once and the
	frames come out of them, with no further file access. The buffer has to
	outlive the FIMEMORY, and the FIMEMORY has to outlive the FIMULTIBITMAP.

	@param fif FIF_WEBP, or FIF_GIF with GIF_PLAYBACK in playback_flag
	@param filename File to read into memory
	@param playback_flag WEBP_PLAYBACK or GIF_PLAYBACK
	@return TRUE if the file was opened
*/
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

	// Wrap the buffer in a memory stream. FreeImage does not take ownership of
	// it, so it must stay alive and be freed here.
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

	// Work out the format from the file itself rather than from its name
	FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(filename, 0);
	if (fif == FIF_UNKNOWN) {
		fif = FreeImage_GetFIFFromFilename(filename);
	}

	// The playback flag is the only thing that differs between the formats
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
			// and how those frames are actually stored
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
