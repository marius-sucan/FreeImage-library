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

/**
FreeImage_OpenMultiBitmapU: create, read and edit a document under a name that no
ANSI code page can spell. The page cache, and the spool FreeImage_CloseMultiBitmap()
rewrites the file in, are made beside it under that name as well, so the edit only
survives when every one of those went through the wide calls.
Like the other ...U functions this is Windows-only; anywhere else it returns NULL.
*/
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
		BOOL bAdded = FreeImage_AppendPageEx(mpage, page);
		assert(bAdded);
	}
	BOOL bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);

	// read it back
	mpage = FreeImage_OpenMultiBitmapU(FIF_TIFF, filename, FALSE, TRUE, FALSE);
	assert(mpage != NULL);
	assert(FreeImage_GetPageCount(mpage) == 3);
	FreeImage_CloseMultiBitmap(mpage, 0);

	// edit it, the page cache on disk again: invert the second page, delete the first
	mpage = FreeImage_OpenMultiBitmapU(FIF_TIFF, filename, FALSE, FALSE, FALSE);
	assert(mpage != NULL);
	FIBITMAP *dib = FreeImage_LockPage(mpage, 1);
	assert(dib != NULL);
	FreeImage_Invert(dib);
	FreeImage_UnlockPage(mpage, dib, TRUE);
	bResult = FreeImage_DeletePageEx(mpage, 0);
	assert(bResult);
	bResult = FreeImage_CloseMultiBitmap(mpage, 0);
	assert(bResult);

	// the file under its wide name is the edited document
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

	// and nothing is left beside it. The cache and the spool are named after the file
	// and each has its own extension, which is spelled out here: to FindFirstFile,
	// "name.*" also matches "name" itself
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
}
