// ==========================================================
// FreeImage 3 Test Script
//
// Design and implementation by
// - Hervé Drolon (drolon@infonie.fr)
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


#include "TestSuite.h"

#include <string.h>
#ifdef _WIN32
#include <io.h>		// _wfindfirst
#endif

void  
testBuildMPage(const char *src_filename, const char *dst_filename, FREE_IMAGE_FORMAT dst_fif, unsigned bpp) {
	// get the file type
	FREE_IMAGE_FORMAT src_fif = FreeImage_GetFileType(src_filename);
	// load the file
	FIBITMAP *src = FreeImage_Load(src_fif, src_filename, 0); //24bit image 

	FIMULTIBITMAP *out = FreeImage_OpenMultiBitmap(dst_fif, dst_filename, TRUE, FALSE, FALSE); 
	for(int size = 16; size <= 48; size += 16 ) { 
		FIBITMAP *rescaled = FreeImage_Rescale(src, size, size, FILTER_CATMULLROM);

		if(FreeImage_GetBPP(rescaled) != bpp) {
			// convert to the requested bitdepth
			FIBITMAP *tmp = NULL;
			switch(bpp) {
				case 8:
					tmp = FreeImage_ConvertTo8Bits(rescaled);
					break;
				case 24:
					tmp = FreeImage_ConvertTo24Bits(rescaled);
					break;
			}
			assert(tmp != NULL);
			FreeImage_Unload(rescaled); 
			rescaled = tmp;
		}

		FreeImage_AppendPage(out, rescaled); 
		FreeImage_Unload(rescaled); 
	} 
	
	FreeImage_Unload(src); 
	
	FreeImage_CloseMultiBitmap(out, 0); 

}

void testMPageCache(const char *src_filename, const char *dst_filename) {

	BOOL keep_cache_in_memory = FALSE;

	// get the file type
	FREE_IMAGE_FORMAT src_fif = FreeImage_GetFileType(src_filename);
	// load the file
	FIBITMAP *src = FreeImage_Load(src_fif, src_filename, 0); //24bit image 
	assert(src != NULL);

	// convert to 24-bit
	if(FreeImage_GetBPP(src) != 24) {
		FIBITMAP *tmp = FreeImage_ConvertTo24Bits(src);
		assert(tmp != NULL);
		FreeImage_Unload(src); 
		src = tmp;
	}

	FIMULTIBITMAP *out = FreeImage_OpenMultiBitmap(FIF_TIFF, dst_filename, TRUE, FALSE, keep_cache_in_memory); 

	// attempt to create 16 480X360 images in a 24-bit TIFF multipage file
	FIBITMAP *rescaled = FreeImage_Rescale(src, 480, 360, FILTER_CATMULLROM);
	for(int i = 0; i < 16; i++) { 		
		FreeImage_AppendPage(out, rescaled); 
	} 
	FreeImage_Unload(rescaled); 
	
	FreeImage_Unload(src); 
	
	FreeImage_CloseMultiBitmap(out, 0); 
}

// --------------------------------------------------------------------------

BOOL testCloneMultiPage(FREE_IMAGE_FORMAT fif, const char *input, const char *output, int output_flag) {

	BOOL bMemoryCache = TRUE;

	// Open src file (read-only, use memory cache)
	FIMULTIBITMAP *src = FreeImage_OpenMultiBitmap(fif, input, FALSE, TRUE, bMemoryCache);

	if(src) {
		// Open dst file (creation, use memory cache)
		FIMULTIBITMAP *dst = FreeImage_OpenMultiBitmap(fif, output, TRUE, FALSE, bMemoryCache);

		// Get src page count
		int count = FreeImage_GetPageCount(src);

		// Clone src to dst
		for(int page = 0; page < count; page++) {
			// Load the bitmap at position 'page'
			FIBITMAP *dib = FreeImage_LockPage(src, page);
			if(dib) {
				// add a new bitmap to dst
				FreeImage_AppendPage(dst, dib);
				// Unload the bitmap (do not apply any change to src)
				FreeImage_UnlockPage(src, dib, FALSE);
			}
		}

		// Close src
		FreeImage_CloseMultiBitmap(src, 0);
		// Save and close dst
		FreeImage_CloseMultiBitmap(dst, output_flag);

		return TRUE;
	}

	return FALSE;
}

// --------------------------------------------------------------------------

void testLockDeleteMultiPage(const char *input) {

	BOOL bCreateNew = FALSE;
	BOOL bReadOnly = FALSE;
	BOOL bMemoryCache = TRUE;

	// Open src file (read/write, use memory cache)
	FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(input);
	FIMULTIBITMAP *src = FreeImage_OpenMultiBitmap(fif, input, bCreateNew, bReadOnly, bMemoryCache);
	
	if(src) {
		// get the page count
		int count = FreeImage_GetPageCount(src);
		if(count > 2) {
			// Load the bitmap at position '2'
			FIBITMAP *dib = FreeImage_LockPage(src, 2);
			if(dib) {
				FreeImage_Invert(dib);
				// Unload the bitmap (apply change to src)
				FreeImage_UnlockPage(src, dib, TRUE);
			}
		}
		// Close src
		FreeImage_CloseMultiBitmap(src, 0);
	}

	src = FreeImage_OpenMultiBitmap(fif, input, bCreateNew, bReadOnly, bMemoryCache);
	
	if(src) {
		// get the page count
		int count = FreeImage_GetPageCount(src);
		if(count > 1) {
			// delete page 0
			FreeImage_DeletePage(src, 0);
		}
		// Close src
		FreeImage_CloseMultiBitmap(src, 0);
	}
}

// --------------------------------------------------------------------------

// FreeImage_OpenMultiBitmapU with a name no ANSI code page can spell
static void testMultiPageU(const char *lpszPathName) {
	printf("testMultiPageU ...\n");

	// Romanian, Chinese, and a character outside the BMP
	const wchar_t *filename = L"mpage-\u0219\u021b-\u4e2d\u6587-\U0001F600.tif";

#ifdef _WIN32
	FIBITMAP *src = FreeImage_Load(FreeImage_GetFileType(lpszPathName), lpszPathName, 0);
	assert(src != NULL);
	FIBITMAP *page = FreeImage_ConvertTo24Bits(src);
	assert(page != NULL);
	FreeImage_Unload(src);

	// create it, with the page cache on disk
	FIMULTIBITMAP *mpage = FreeImage_OpenMultiBitmapU(FIF_TIFF, filename, TRUE, FALSE, FALSE);
	assert(mpage != NULL);
	for(int i = 0; i < 3; i++) {
		BOOL bAdded = FreeImage_AppendPage(mpage, page);
		assert(bAdded);
	}
	BOOL bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);

	// read it back
	mpage = FreeImage_OpenMultiBitmapU(FIF_TIFF, filename, FALSE, TRUE, FALSE);
	assert(mpage != NULL);
	assert(FreeImage_GetPageCount(mpage) == 3);
	FreeImage_CloseMultiBitmap(mpage, 0);

	// edit: invert page 2, delete page 1
	mpage = FreeImage_OpenMultiBitmapU(FIF_TIFF, filename, FALSE, FALSE, FALSE);
	assert(mpage != NULL);
	FIBITMAP *dib = FreeImage_LockPage(mpage, 1);
	assert(dib != NULL);
	FreeImage_Invert(dib);
	FreeImage_UnlockPage(mpage, dib, TRUE);
	bResult = FreeImage_DeletePage(mpage, 0);
	assert(bResult);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);

	// verify the edit
	mpage = FreeImage_OpenMultiBitmapU(FIF_TIFF, filename, FALSE, TRUE, TRUE);
	assert(mpage != NULL);
	assert(FreeImage_GetPageCount(mpage) == 2);
	dib = FreeImage_LockPage(mpage, 0);
	assert(dib != NULL);
	RGBQUAD original, inverted;
	FreeImage_GetPixelColor(page, 0, 0, &original);
	FreeImage_GetPixelColor(dib, 0, 0, &inverted);
	assert(inverted.rgbRed == 255 - original.rgbRed);
	assert(inverted.rgbGreen == 255 - original.rgbGreen);
	assert(inverted.rgbBlue == 255 - original.rgbBlue);
	FreeImage_UnlockPage(mpage, dib, FALSE);
	FreeImage_CloseMultiBitmap(mpage, 0);

	// no companions left; "name.*" would also match the file itself
	const wchar_t *companions[] = {
		L"mpage-\u0219\u021b-\u4e2d\u6587-\U0001F600.tif.*.ficache",
		L"mpage-\u0219\u021b-\u4e2d\u6587-\U0001F600.tif.*.fispool"
	};
	for(int i = 0; i < 2; i++) {
		struct _wfinddata_t found;
		intptr_t hFind = _wfindfirst(companions[i], &found);
		assert(hFind == -1);
		if(hFind != -1) {
			_findclose(hFind);
		}
	}

	FreeImage_Unload(page);
	_wremove(filename);
#else
	FIMULTIBITMAP *mpage = FreeImage_OpenMultiBitmapU(FIF_TIFF, filename, TRUE, FALSE, FALSE);
	assert(mpage == NULL);
#endif
}

// --------------------------------------------------------------------------

static int countPages(const char *filename) {
	FIMULTIBITMAP *mpage = FreeImage_OpenMultiBitmap(FIF_TIFF, filename, FALSE, TRUE, TRUE);
	if(!mpage) {
		return -1;
	}
	const int count = FreeImage_GetPageCount(mpage);
	FreeImage_CloseMultiBitmap(mpage, 0);
	return count;
}

// each page function reports its own failure; CloseMultiBitmap fails only when the file is not written
static void testPageResults(const char *lpszPathName) {
	printf("testPageResults ...\n");

	const char *filename = "page-results.tif";

	FIBITMAP *src = FreeImage_Load(FreeImage_GetFileType(lpszPathName), lpszPathName, 0);
	assert(src != NULL);
	FIBITMAP *page = FreeImage_ConvertTo24Bits(src);
	assert(page != NULL);
	FreeImage_Unload(src);

	FIMULTIBITMAP *mpage = FreeImage_OpenMultiBitmap(FIF_TIFF, filename, TRUE, FALSE, TRUE);
	assert(mpage != NULL);
	BOOL bResult = FreeImage_AppendPage(mpage, page);
	assert(bResult);
	bResult = FreeImage_AppendPage(mpage, page);
	assert(bResult);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);

	// refused calls fail themselves, not the close that saves the accepted page
	mpage = FreeImage_OpenMultiBitmap(FIF_TIFF, filename, FALSE, FALSE, TRUE);
	assert(mpage != NULL);
	bResult = FreeImage_AppendPage(mpage, page);
	assert(bResult);
	bResult = FreeImage_InsertPage(mpage, 99, page);
	assert(!bResult);
	bResult = FreeImage_DeletePage(mpage, 99);
	assert(!bResult);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);
	assert(countPages(filename) == 3);

	// read-only: every edit is refused and there is nothing to write
	mpage = FreeImage_OpenMultiBitmap(FIF_TIFF, filename, FALSE, TRUE, TRUE);
	assert(mpage != NULL);
	bResult = FreeImage_AppendPage(mpage, page);
	assert(!bResult);
	bResult = FreeImage_DeletePage(mpage, 0);
	assert(!bResult);
	FIBITMAP *dib = FreeImage_LockPage(mpage, 0);
	assert(dib != NULL);
	// a bitmap that is not a locked page
	bResult = FreeImage_UnlockPage(mpage, page, FALSE);
	assert(!bResult);
	// a change a read-only bitmap cannot keep
	bResult = FreeImage_UnlockPage(mpage, dib, TRUE);
	assert(!bResult);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);
	assert(countPages(filename) == 3);

	// kept changes
	mpage = FreeImage_OpenMultiBitmap(FIF_TIFF, filename, FALSE, FALSE, TRUE);
	assert(mpage != NULL);
	dib = FreeImage_LockPage(mpage, 1);
	assert(dib != NULL);
	bResult = FreeImage_UnlockPage(mpage, dib, FALSE);
	assert(bResult);
	dib = FreeImage_LockPage(mpage, 1);
	assert(dib != NULL);
	FreeImage_Invert(dib);
	bResult = FreeImage_UnlockPage(mpage, dib, TRUE);
	assert(bResult);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);

	mpage = FreeImage_OpenMultiBitmap(FIF_TIFF, filename, FALSE, TRUE, TRUE);
	assert(mpage != NULL);
	dib = FreeImage_LockPage(mpage, 1);
	assert(dib != NULL);
	RGBQUAD original, inverted;
	FreeImage_GetPixelColor(page, 0, 0, &original);
	FreeImage_GetPixelColor(dib, 0, 0, &inverted);
	assert(inverted.rgbRed == 255 - original.rgbRed);
	assert(inverted.rgbGreen == 255 - original.rgbGreen);
	assert(inverted.rgbBlue == 255 - original.rgbBlue);
	FreeImage_UnlockPage(mpage, dib, FALSE);
	FreeImage_CloseMultiBitmap(mpage, 0);

	FreeImage_Unload(page);
	remove(filename);
}

// --------------------------------------------------------------------------

static BOOL samePixels(FIBITMAP *a, FIBITMAP *b) {
	if (!a || !b || (FreeImage_GetWidth(a) != FreeImage_GetWidth(b)) || (FreeImage_GetHeight(a) != FreeImage_GetHeight(b))
		|| (FreeImage_GetBPP(a) != FreeImage_GetBPP(b))) {
		return FALSE;
	}
	for (unsigned y = 0; y < FreeImage_GetHeight(a); y++) {
		if (memcmp(FreeImage_GetScanLine(a, y), FreeImage_GetScanLine(b, y), FreeImage_GetLine(a)) != 0) {
			return FALSE;
		}
	}
	return TRUE;
}

// page 0, locked again and after a save, matches the first lock
static void relockPage(FIMULTIBITMAP *mpage) {
	assert(mpage != NULL);
	FIBITMAP *first = FreeImage_LockPage(mpage, 0);
	assert(first != NULL);
	FIBITMAP *copy = FreeImage_Clone(first);
	FreeImage_UnlockPage(mpage, first, FALSE);

	FIBITMAP *dib = FreeImage_LockPage(mpage, 0);
	assert(samePixels(dib, copy));
	FreeImage_UnlockPage(mpage, dib, FALSE);

	// the save reads the page through the same stream
	FIMEMORY *hmem = FreeImage_OpenMemory();
	BOOL bResult = FreeImage_SaveMultiBitmapToMemory(FIF_TIFF, mpage, hmem, 0);
	assert(bResult);
	FreeImage_CloseMemory(hmem);
	dib = FreeImage_LockPage(mpage, 0);
	assert(samePixels(dib, copy));
	FreeImage_UnlockPage(mpage, dib, FALSE);

	FreeImage_Unload(copy);
	FreeImage_CloseMultiBitmap(mpage, 0);
}

// J2K, JP2 and JXR decoders read once: each lock of a single image needs its own
static void testSingleImageRelock(const char *lpszPathName) {
	printf("testSingleImageRelock ...\n");

	FIBITMAP *src = FreeImage_Load(FreeImage_GetFileType(lpszPathName), lpszPathName, 0);
	assert(src != NULL);
	FIBITMAP *image = FreeImage_ConvertTo24Bits(src);
	assert(image != NULL);
	FreeImage_Unload(src);

	const struct { FREE_IMAGE_FORMAT fif; const char *filename; } formats[] = {
		{ FIF_J2K, "relock.j2k" }, { FIF_JP2, "relock.jp2" }, { FIF_JXR, "relock.jxr" }
	};
	for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
		BOOL bResult = FreeImage_Save(formats[i].fif, image, formats[i].filename, 0);
		assert(bResult);
		relockPage(FreeImage_OpenMultiBitmap(formats[i].fif, formats[i].filename, FALSE, TRUE, TRUE));
		remove(formats[i].filename);

		FIMEMORY *hmem = FreeImage_OpenMemory();
		bResult = FreeImage_SaveToMemory(formats[i].fif, image, hmem, 0);
		assert(bResult);
		FreeImage_SeekMemory(hmem, 0, SEEK_SET);
		relockPage(FreeImage_LoadMultiBitmapFromMemory(formats[i].fif, hmem, 0));
		FreeImage_CloseMemory(hmem);
	}

	FreeImage_Unload(image);
}

// --------------------------------------------------------------------------

static void setAnimationTag(FIBITMAP *dib, const char *key, FREE_IMAGE_MDTYPE type, DWORD count, DWORD length, const void *value) {
	FITAG *tag = FreeImage_CreateTag();
	FreeImage_SetTagKey(tag, key);
	FreeImage_SetTagType(tag, type);
	FreeImage_SetTagCount(tag, count);
	FreeImage_SetTagLength(tag, length);
	FreeImage_SetTagValue(tag, value);
	FreeImage_SetMetadata(FIMD_ANIMATION, dib, key, tag);
	FreeImage_DeleteTag(tag);
}

// a palette image's color at x, y
static RGBQUAD paletteColorAt(FIBITMAP *dib, unsigned x, unsigned y) {
	BYTE index = 0;
	FreeImage_GetPixelIndex(dib, x, y, &index);
	return FreeImage_GetPalette(dib)[index];
}

// the colors of page 'page' of a GIF, against frame 'frame' as built below
static BOOL gifPageHasFrameColors(const char *filename, int page, int frame, const RGBQUAD *palette) {
	FIMULTIBITMAP *mpage = FreeImage_OpenMultiBitmap(FIF_GIF, filename, FALSE, TRUE, TRUE);
	if(!mpage) {
		return FALSE;
	}
	FIBITMAP *dib = FreeImage_LockPage(mpage, page);
	BOOL same = (dib != NULL) && (FreeImage_GetBPP(dib) == 8);
	for(unsigned y = 0; same && y < 6; y++) {
		for(unsigned x = 0; same && x < 8; x++) {
			const RGBQUAD c = paletteColorAt(dib, x, y);
			const RGBQUAD &want = palette[(frame * 50 + x + 8 * y) & 0xFF];
			same = (c.rgbRed == want.rgbRed) && (c.rgbGreen == want.rgbGreen) && (c.rgbBlue == want.rgbBlue);
		}
	}
	if(dib) {
		FreeImage_UnlockPage(mpage, dib, FALSE);
	}
	FreeImage_CloseMultiBitmap(mpage, 0);
	return same;
}

// frames that take the global color table keep their colors when they move to the front or are saved alone
static void testGIFGlobalPalette() {
	printf("testGIFGlobalPalette ...\n");

	const char *filename = "global-palette.gif";
	RGBQUAD palette[256];
	for(int i = 0; i < 256; i++) {
		palette[i].rgbRed = (BYTE)(i * 7 + 40);
		palette[i].rgbGreen = (BYTE)(255 - i * 3);
		palette[i].rgbBlue = (BYTE)(i * 37 + 90);
		palette[i].rgbReserved = 0;
	}

	// three frames, all on the global table: page 0 carries it
	FIMULTIBITMAP *mpage = FreeImage_OpenMultiBitmap(FIF_GIF, filename, TRUE, FALSE, TRUE);
	assert(mpage != NULL);
	for(int frame = 0; frame < 3; frame++) {
		FIBITMAP *dib = FreeImage_Allocate(8, 6, 8);
		assert(dib != NULL);
		RGBQUAD *pal = FreeImage_GetPalette(dib);
		for(int i = 0; i < 256; i++) {
			pal[i] = palette[i];
		}
		for(unsigned y = 0; y < 6; y++) {
			for(unsigned x = 0; x < 8; x++) {
				BYTE index = (BYTE)((frame * 50 + x + 8 * y) & 0xFF);
				FreeImage_SetPixelIndex(dib, x, y, &index);
			}
		}
		const BYTE no_local = 1;
		setAnimationTag(dib, "NoLocalPalette", FIDT_BYTE, 1, 1, &no_local);
		if(frame == 0) {
			setAnimationTag(dib, "GlobalPalette", FIDT_PALETTE, 256, sizeof(palette), palette);
		}
		BOOL bResult = FreeImage_AppendPage(mpage, dib);
		assert(bResult);
		FreeImage_Unload(dib);
	}
	BOOL bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);
	assert(gifPageHasFrameColors(filename, 1, 1, palette));

	// a changed page that took the global table comes back with its colors
	mpage = FreeImage_OpenMultiBitmap(FIF_GIF, filename, FALSE, FALSE, TRUE);
	assert(mpage != NULL);
	FIBITMAP *dib = FreeImage_LockPage(mpage, 2);
	assert(dib != NULL);
	bResult = FreeImage_UnlockPage(mpage, dib, TRUE);
	assert(bResult);
	dib = FreeImage_LockPage(mpage, 2);
	assert(dib != NULL);
	const RGBQUAD c = paletteColorAt(dib, 0, 0);
	assert((c.rgbRed == palette[100].rgbRed) && (c.rgbGreen == palette[100].rgbGreen) && (c.rgbBlue == palette[100].rgbBlue));
	FreeImage_UnlockPage(mpage, dib, FALSE);

	// without page 0, the file still has a color table for the others
	bResult = FreeImage_DeletePage(mpage, 0);
	assert(bResult);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);
	assert(gifPageHasFrameColors(filename, 0, 1, palette));
	assert(gifPageHasFrameColors(filename, 1, 2, palette));

	// a global table of 16 colors, then a frame whose 256 colors begin with those 16
	mpage = FreeImage_OpenMultiBitmap(FIF_GIF, filename, TRUE, FALSE, TRUE);
	assert(mpage != NULL);
	for(int frame = 0; frame < 2; frame++) {
		FIBITMAP *page = FreeImage_Allocate(8, 6, 8);
		assert(page != NULL);
		RGBQUAD *pal = FreeImage_GetPalette(page);
		for(int i = 0; i < 256; i++) {
			pal[i] = palette[i];
		}
		for(unsigned y = 0; y < 6; y++) {
			for(unsigned x = 0; x < 8; x++) {
				BYTE index = (BYTE)((frame * 50 + x + 8 * y) & (frame ? 0xFF : 0x0F));
				FreeImage_SetPixelIndex(page, x, y, &index);
			}
		}
		const BYTE no_local = 1;
		setAnimationTag(page, "NoLocalPalette", FIDT_BYTE, 1, 1, &no_local);
		if(frame == 0) {
			setAnimationTag(page, "GlobalPalette", FIDT_PALETTE, 16, 16 * sizeof(RGBQUAD), palette);
		}
		bResult = FreeImage_AppendPage(mpage, page);
		assert(bResult);
		FreeImage_Unload(page);
	}
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);
	assert(gifPageHasFrameColors(filename, 1, 1, palette));

	remove(filename);
}

// a byte buffer read through FreeImageIO; a loader that keeps reading past its end fails the test
struct CountedStream {
	const BYTE *data;
	INT64 size, pos;
	long reads;
};

static unsigned DLL_CALLCONV countedRead(void *buffer, unsigned size, unsigned count, fi_handle handle) {
	CountedStream *s = (CountedStream*)handle;
	assert(++s->reads < 100000);
	const INT64 bytes = (INT64)size * count;
	const INT64 left = (s->pos < s->size) ? (s->size - s->pos) : 0;
	const INT64 n = (bytes < left) ? bytes : left;
	memcpy(buffer, s->data + s->pos, (size_t)n);
	s->pos += n;
	return size ? (unsigned)(n / size) : 0;
}

static unsigned DLL_CALLCONV countedWrite(void *buffer, unsigned size, unsigned count, fi_handle handle) {
	return 0;
}

static int DLL_CALLCONV countedSeek(fi_handle handle, INT64 offset, int origin) {
	CountedStream *s = (CountedStream*)handle;
	const INT64 base = (origin == SEEK_SET) ? 0 : (origin == SEEK_CUR) ? s->pos : s->size;
	if(base + offset < 0) {
		return -1;
	}
	s->pos = base + offset;
	return 0;
}

static INT64 DLL_CALLCONV countedTell(fi_handle handle) {
	return ((CountedStream*)handle)->pos;
}

// a GIF cut inside a comment after its frame loads, with the comment as far as the file holds it
static void testGIFCutComment() {
	printf("testGIFCutComment ...\n");

	// one frame, then a comment between it and the trailer
	FIBITMAP *dib = FreeImage_Allocate(8, 6, 8);
	assert(dib != NULL);
	FIMEMORY *hmem = FreeImage_OpenMemory();
	BOOL bResult = FreeImage_SaveToMemory(FIF_GIF, dib, hmem, 0);
	assert(bResult);
	FreeImage_Unload(dib);
	BYTE *gif = NULL;
	DWORD gif_size = 0;
	FreeImage_AcquireMemory(hmem, &gif, &gif_size);
	assert((gif_size > 1) && (gif[gif_size - 1] == 0x3B));
	static const BYTE comment[] = { 0x21, 0xFE, 5, 'h', 'e', 'l', 'l', 'o', 0, 0x3B };
	const DWORD start = gif_size - 1;
	const DWORD size = start + sizeof(comment);
	BYTE *data = (BYTE*)malloc(size);
	assert(data != NULL);
	memcpy(data, gif, start);
	memcpy(data + start, comment, sizeof(comment));
	FreeImage_CloseMemory(hmem);

	FreeImageIO io;
	io.read_proc = countedRead;
	io.write_proc = countedWrite;
	io.seek_proc = countedSeek;
	io.tell_proc = countedTell;
	for(DWORD n = start; n <= size; n++) {
		for(int header_only = 0; header_only < 2; header_only++) {
			CountedStream stream = { data, (INT64)n, 0, 0 };
			FIBITMAP *page = FreeImage_LoadFromHandle(FIF_GIF, &io, (fi_handle)&stream, header_only ? FIF_LOAD_NOPIXELS : 0);
			assert(page != NULL);
			// the comment's bytes in the file, at most "hello"
			const DWORD held = (n > start + 3) ? ((n - start - 3 < 5) ? n - start - 3 : 5) : 0;
			FITAG *tag = NULL;
			if(FreeImage_GetMetadata(FIMD_COMMENTS, page, "Comment0", &tag)) {
				assert(strlen((const char*)FreeImage_GetTagValue(tag)) == held);
				assert(strncmp((const char*)FreeImage_GetTagValue(tag), "hello", held) == 0);
			} else {
				assert(n < start + 2);
			}
			FreeImage_Unload(page);
		}
	}

	free(data);
}

// the pages of a GIF in memory, and how many of them load
static int gifPagesOf(BYTE *data, DWORD size, int flags, int *loaded) {
	*loaded = 0;
	int count = 0;
	FIMEMORY *hmem = FreeImage_OpenMemory(data, size);
	assert(hmem != NULL);
	FIMULTIBITMAP *mpage = FreeImage_LoadMultiBitmapFromMemory(FIF_GIF, hmem, flags);
	if(mpage) {
		count = FreeImage_GetPageCount(mpage);
		for(int i = 0; i < count; i++) {
			FIBITMAP *dib = FreeImage_LockPage(mpage, i);
			if(dib) {
				(*loaded)++;
				FreeImage_UnlockPage(mpage, dib, FALSE);
			}
		}
		FreeImage_CloseMultiBitmap(mpage, 0);
	}
	FreeImage_CloseMemory(hmem);
	return count;
}

// a GIF cut anywhere: full, header-only and played loads count the same pages, every one of which loads
static void testGIFCutFrames() {
	printf("testGIFCutFrames ...\n");

	const char *filename = "cut-frames.gif";

	// two frames of noise, so the image data runs over more than one sub-block
	FIMULTIBITMAP *mpage = FreeImage_OpenMultiBitmap(FIF_GIF, filename, TRUE, FALSE, TRUE);
	assert(mpage != NULL);
	unsigned seed = 12345;
	for(int frame = 0; frame < 2; frame++) {
		FIBITMAP *dib = FreeImage_Allocate(24, 16, 8);
		assert(dib != NULL);
		for(unsigned y = 0; y < 16; y++) {
			BYTE *bits = FreeImage_GetScanLine(dib, y);
			for(unsigned x = 0; x < 24; x++) {
				seed = seed * 1103515245 + 12345;
				bits[x] = (BYTE)(seed >> 16);
			}
		}
		BOOL bResult = FreeImage_AppendPage(mpage, dib);
		assert(bResult);
		FreeImage_Unload(dib);
	}
	BOOL bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);

	FILE *file = fopen(filename, "rb");
	assert(file != NULL);
	fseek(file, 0, SEEK_END);
	const long size = ftell(file);
	fseek(file, 0, SEEK_SET);
	BYTE *data = (BYTE*)malloc(size);
	assert(data != NULL);
	const size_t got = fread(data, 1, size, file);
	assert(got == (size_t)size);
	fclose(file);
	remove(filename);

	int before = 0;
	for(long n = 0; n <= size; n++) {
		int full_loaded, header_loaded, played_loaded;
		const int full = gifPagesOf(data, (DWORD)n, 0, &full_loaded);
		const int header = gifPagesOf(data, (DWORD)n, FIF_LOAD_NOPIXELS, &header_loaded);
		const int played = gifPagesOf(data, (DWORD)n, GIF_PLAYBACK, &played_loaded);
		assert((full == header) && (full == played));
		assert((full_loaded == full) && (header_loaded == header) && (played_loaded == played));
		// a longer file never shows fewer frames
		assert(full >= before);
		before = full;
	}
	assert(before == 2);

	free(data);
}

// --------------------------------------------------------------------------

void testMultiPage(const char *lpszPathName) {
	printf("testMultiPage ...\n");

	// test multipage creation
	testBuildMPage(lpszPathName, "sample.ico", FIF_ICO, 24);
	testBuildMPage(lpszPathName, "sample.tif", FIF_TIFF, 24);
	testBuildMPage(lpszPathName, "sample.gif", FIF_GIF, 8);

	// test multipage copy
	testCloneMultiPage(FIF_TIFF, "sample.tif", "clone.tif", TIFF_LZW);

	// test multipage lock & delete
	testLockDeleteMultiPage("clone.tif");

	// test multipage cache
	testMPageCache(lpszPathName, "mpages.tif");

	// test multipage functions with a wide-character filename
	testMultiPageU(lpszPathName);

	// test what the page functions and the close report
	testPageResults(lpszPathName);

	// test single images locked again and after a save
	testSingleImageRelock(lpszPathName);

	// test GIF frames that take the global color table
	testGIFGlobalPalette();

	// test GIF files cut short
	testGIFCutComment();
	testGIFCutFrames();
}
