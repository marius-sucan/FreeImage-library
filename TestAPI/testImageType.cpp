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

// Local test functions
// ----------------------------------------------------------

BOOL testClone(const char *lpszPathName) {
	FIBITMAP *dib1 = NULL, *dib2 = NULL; 

	try {
		FREE_IMAGE_FORMAT fif = FreeImage_GetFIFFromFilename(lpszPathName);

		dib1 = FreeImage_Load(fif, lpszPathName, 0); 
		if(!dib1) throw(1);
		
		dib2 = FreeImage_Clone(dib1); 
		if(!dib2) throw(1);
		
		FreeImage_Unload(dib1); 
		FreeImage_Unload(dib2); 

		return TRUE;
	} 
	catch(int) {
		if(dib1) FreeImage_Unload(dib1); 
		if(dib2) FreeImage_Unload(dib2); 
	}
	
	return FALSE; 
}

void testAllocateCloneUnload(const char *lpszPathName) {
	printf("testAllocateCloneUnload ...\n");

	BOOL bResult = testClone(lpszPathName);
	assert(bResult);
}

BOOL testAllocateCloneUnloadType(FREE_IMAGE_TYPE image_type, unsigned width, unsigned height) {
	FIBITMAP *image = NULL;
	FIBITMAP *clone = NULL;

	unsigned x, y;

	try {
		// test allocation function
		image = FreeImage_AllocateT(image_type, width, height, 8);
		if(!image) throw(1);

		FREE_IMAGE_TYPE type = FreeImage_GetImageType(image);
		if(image_type != type) throw(1);

		// test pixel access
		switch(image_type) {
			case FIT_BITMAP:
				if(FreeImage_GetBPP(image) == 8) {
					for(y = 0; y < FreeImage_GetHeight(image); y++) {
						BYTE *bits = (BYTE *)FreeImage_GetScanLine(image, y);
						for(x = 0; x < FreeImage_GetWidth(image); x++) {
							bits[x] = 128;
						}
					}
				}
				break;
			case FIT_UINT16:
				for(y = 0; y < FreeImage_GetHeight(image); y++) {
					unsigned short *bits = (unsigned short *)FreeImage_GetScanLine(image, y);
					for(x = 0; x < FreeImage_GetWidth(image); x++) {
						bits[x] = 128;
					}
				}
				break;
			case FIT_INT16:
				for(y = 0; y < FreeImage_GetHeight(image); y++) {
					short *bits = (short *)FreeImage_GetScanLine(image, y);
					for(x = 0; x < FreeImage_GetWidth(image); x++) {
						bits[x] = 128;
					}
				}
				break;
			case FIT_UINT32:
				for(y = 0; y < FreeImage_GetHeight(image); y++) {
					DWORD *bits = (DWORD *)FreeImage_GetScanLine(image, y);
					for(x = 0; x < FreeImage_GetWidth(image); x++) {
						bits[x] = 128;
					}
				}
				break;
			case FIT_INT32:
				for(y = 0; y < FreeImage_GetHeight(image); y++) {
					LONG *bits = (LONG *)FreeImage_GetScanLine(image, y);
					for(x = 0; x < FreeImage_GetWidth(image); x++) {
						bits[x] = 128;
					}
				}
				break;
			case FIT_FLOAT:
				for(y = 0; y < FreeImage_GetHeight(image); y++) {
					float *bits = (float *)FreeImage_GetScanLine(image, y);
					for(x = 0; x < FreeImage_GetWidth(image); x++) {
						bits[x] = 128;
					}
				}
				break;
			case FIT_DOUBLE:
				for(y = 0; y < FreeImage_GetHeight(image); y++) {
					double *bits = (double *)FreeImage_GetScanLine(image, y);
					for(x = 0; x < FreeImage_GetWidth(image); x++) {
						bits[x] = 128;
					}
				}
				break;
			case FIT_COMPLEX:
				for(y = 0; y < FreeImage_GetHeight(image); y++) {
					FICOMPLEX *bits = (FICOMPLEX *)FreeImage_GetScanLine(image, y);
					for(x = 0; x < FreeImage_GetWidth(image); x++) {
						bits[x].r = 128;
						bits[x].i = 128;
					}
				}
				break;
			case FIT_RGB16:
				for(y = 0; y < FreeImage_GetHeight(image); y++) {
					FIRGB16 *bits = (FIRGB16 *)FreeImage_GetScanLine(image, y);
					for(x = 0; x < FreeImage_GetWidth(image); x++) {
						bits[x].red = 128;
						bits[x].green = 128;
						bits[x].blue = 128;
					}
				}
				break;
			case FIT_RGBF:
				for(y = 0; y < FreeImage_GetHeight(image); y++) {
					FIRGBF *bits = (FIRGBF *)FreeImage_GetScanLine(image, y);
					for(x = 0; x < FreeImage_GetWidth(image); x++) {
						bits[x].red = 128;
						bits[x].green = 128;
						bits[x].blue = 128;
					}
				}
				break;
			case FIT_RGBA16:
				for(y = 0; y < FreeImage_GetHeight(image); y++) {
					FIRGBA16 *bits = (FIRGBA16 *)FreeImage_GetScanLine(image, y);
					for(x = 0; x < FreeImage_GetWidth(image); x++) {
						bits[x].red = 128;
						bits[x].green = 128;
						bits[x].blue = 128;
						bits[x].alpha = 128;
					}
				}
				break;	
			case FIT_RGBAF:
				for(y = 0; y < FreeImage_GetHeight(image); y++) {
					FIRGBAF *bits = (FIRGBAF *)FreeImage_GetScanLine(image, y);
					for(x = 0; x < FreeImage_GetWidth(image); x++) {
						bits[x].red = 128;
						bits[x].green = 128;
						bits[x].blue = 128;
						bits[x].alpha = 128;
					}
				}
				break;
		}

		
		// test clone function
		clone = FreeImage_Clone(image);
		if(!clone) throw(1);

		if(FreeImage_GetImageType(clone) != image_type)	throw(1);

		switch(image_type) {
			case FIT_BITMAP:
				if(FreeImage_GetBPP(clone) == 8) {
					for(y = 0; y < FreeImage_GetHeight(clone); y++) {
						BYTE *bits = (BYTE *)FreeImage_GetScanLine(clone, y);
						for(x = 0; x < FreeImage_GetWidth(clone); x++) {
							if(bits[x] != 128)
								throw(1);
						}
					}
				}
				break;
			case FIT_UINT16:
				for(y = 0; y < FreeImage_GetHeight(clone); y++) {
					unsigned short *bits = (unsigned short *)FreeImage_GetScanLine(clone, y);
					for(x = 0; x < FreeImage_GetWidth(clone); x++) {
						if(bits[x] != 128)
							throw(1);
					}
				}
				break;
			case FIT_INT16:
				for(y = 0; y < FreeImage_GetHeight(clone); y++) {
					short *bits = (short *)FreeImage_GetScanLine(clone, y);
					for(x = 0; x < FreeImage_GetWidth(clone); x++) {
						if(bits[x] != 128)
							throw(1);
					}
				}
				break;
			case FIT_UINT32:
				for(y = 0; y < FreeImage_GetHeight(clone); y++) {
					DWORD *bits = (DWORD *)FreeImage_GetScanLine(clone, y);
					for(x = 0; x < FreeImage_GetWidth(clone); x++) {
						if(bits[x] != 128)
							throw(1);
					}
				}
				break;
			case FIT_INT32:
				for(y = 0; y < FreeImage_GetHeight(clone); y++) {
					LONG *bits = (LONG *)FreeImage_GetScanLine(clone, y);
					for(x = 0; x < FreeImage_GetWidth(clone); x++) {
						if(bits[x] != 128)
							throw(1);
					}
				}
				break;
			case FIT_FLOAT:
				for(y = 0; y < FreeImage_GetHeight(clone); y++) {
					float *bits = (float *)FreeImage_GetScanLine(clone, y);
					for(x = 0; x < FreeImage_GetWidth(clone); x++) {
						if(bits[x] != 128)
							throw(1);
					}
				}
				break;
			case FIT_DOUBLE:
				for(y = 0; y < FreeImage_GetHeight(clone); y++) {
					double *bits = (double *)FreeImage_GetScanLine(clone, y);
					for(x = 0; x < FreeImage_GetWidth(clone); x++) {
						if(bits[x] != 128)
							throw(1);
					}
				}
				break;
			case FIT_COMPLEX:
				for(y = 0; y < FreeImage_GetHeight(clone); y++) {
					FICOMPLEX *bits = (FICOMPLEX *)FreeImage_GetScanLine(clone, y);
					for(x = 0; x < FreeImage_GetWidth(clone); x++) {
						if((bits[x].r != 128) || ((bits[x].r - bits[x].i) != 0))
							throw(1);
					}
				}
				break;
			case FIT_RGB16:
				for(y = 0; y < FreeImage_GetHeight(clone); y++) {
					FIRGB16 *bits = (FIRGB16 *)FreeImage_GetScanLine(clone, y);
					for(x = 0; x < FreeImage_GetWidth(clone); x++) {
						if((bits[x].red != 128) || (bits[x].green != 128) || (bits[x].blue != 128))
							throw(1);
					}
				}
				break;
			case FIT_RGBF:
				for(y = 0; y < FreeImage_GetHeight(clone); y++) {
					FIRGBF *bits = (FIRGBF *)FreeImage_GetScanLine(clone, y);
					for(x = 0; x < FreeImage_GetWidth(clone); x++) {
						if((bits[x].red != 128) || (bits[x].green != 128) || (bits[x].blue != 128))
							throw(1);
					}
				}
				break;
			case FIT_RGBA16:
				for(y = 0; y < FreeImage_GetHeight(clone); y++) {
					FIRGBA16 *bits = (FIRGBA16 *)FreeImage_GetScanLine(clone, y);
					for(x = 0; x < FreeImage_GetWidth(clone); x++) {
						if((bits[x].red != 128) || (bits[x].green != 128) || (bits[x].blue != 128) || (bits[x].alpha != 128))
							throw(1);
					}
				}
				break;	
			case FIT_RGBAF:
				for(y = 0; y < FreeImage_GetHeight(clone); y++) {
					FIRGBAF *bits = (FIRGBAF *)FreeImage_GetScanLine(clone, y);
					for(x = 0; x < FreeImage_GetWidth(clone); x++) {
						if((bits[x].red != 128) || (bits[x].green != 128) || (bits[x].blue != 128) || (bits[x].alpha != 128))
							throw(1);
					}
				}
				break;

		}

		// test unload function
		FreeImage_Unload(clone);
		clone = NULL;
		FreeImage_Unload(image);
		image = NULL;

	} catch(int) {
		if(image) FreeImage_Unload(image);
		if(clone) FreeImage_Unload(clone);
		return FALSE;
	}
	return TRUE;
}

BOOL testLoadSaveConvertImageType(FIBITMAP *src, FREE_IMAGE_TYPE image_type) {
	FIBITMAP *dst = NULL;
	FIBITMAP *chk = NULL;
	BOOL bResult = TRUE;

	try {
		// convert to type image_type
		dst = FreeImage_ConvertToType(src, image_type);
		if(!dst) throw(1);

		// save image as TIFF
		bResult = FreeImage_Save(FIF_TIFF, dst, "TestImageType.tif", TIFF_DEFAULT);
		if(!bResult) throw(1);

		// destroy dst
		FreeImage_Unload(dst);
		dst = NULL;

		// load image
		dst = FreeImage_Load(FIF_TIFF, "TestImageType.tif", TIFF_DEFAULT);
		if(!dst) throw(1);

		// convert to standard bitmap (linear scaling)
		chk = FreeImage_ConvertToType(dst, FIT_BITMAP, TRUE);
		if(!chk) throw(1);
		FreeImage_Unload(dst);
		dst = NULL;

		// save image as TIFF
		bResult = FreeImage_Save(FIF_TIFF, chk, "TestImageType.tif", TIFF_DEFAULT);
		if(!bResult) throw(1);
		FreeImage_Unload(chk);
		chk = NULL;


	} catch(int) {
		if(dst) FreeImage_Unload(dst);
		if(chk) FreeImage_Unload(chk);
		return FALSE;
	}

	return TRUE;
}

BOOL testLoadSaveConvertComplexType(FIBITMAP *src, FREE_IMAGE_COLOR_CHANNEL channel) {
	FIBITMAP *dst = NULL;
	FIBITMAP *chk_double = NULL;
	FIBITMAP *chk = NULL;
	BOOL bResult = TRUE;

	try {
		// convert to type FICOMPLEX
		dst = FreeImage_ConvertToType(src, FIT_COMPLEX);
		if(!dst) throw(1);

		// save image as TIFF
		bResult = FreeImage_Save(FIF_TIFF, dst, "TestImageType.tif", TIFF_DEFAULT);
		if(!bResult) throw(1);

		// destroy dst
		FreeImage_Unload(dst);
		dst = NULL;

		// load image
		dst = FreeImage_Load(FIF_TIFF, "TestImageType.tif", TIFF_DEFAULT);
		if(!dst) throw(1);

		
		// convert to type FIT_DOUBLE
		chk_double = FreeImage_GetComplexChannel(dst, channel);
		if(!chk_double) throw(1);
		FreeImage_Unload(dst);
		dst = NULL;
		
		// convert to standard bitmap (linear scaling)
		chk = FreeImage_ConvertToType(chk_double, FIT_BITMAP, TRUE);
		if(!chk) throw(1);
		FreeImage_Unload(chk_double);
		chk_double = NULL;

		// save image as TIFF
		bResult = FreeImage_Save(FIF_TIFF, chk, "TestImageType.tif", TIFF_DEFAULT);
		if(!bResult) throw(1);
		FreeImage_Unload(chk);
		chk = NULL;


	} catch(int) {
		if(dst) FreeImage_Unload(dst);
		if(chk_double) FreeImage_Unload(chk_double);
		if(chk) FreeImage_Unload(chk);
		return FALSE;
	}

	return TRUE;
}

// FILTER_NEAREST: each pixel is the source pixel under its centre, in the source's pixel format
static BOOL testRescaleNearestType(FREE_IMAGE_TYPE image_type, unsigned bpp) {
	const unsigned sw = 7, sh = 5;
	const unsigned sizes[][2] = { {3, 2}, {14, 10}, {7, 11}, {20, 5}, {1, 1} };

	FIBITMAP *src = FreeImage_AllocateT(image_type, sw, sh, bpp, FI16_565_RED_MASK, FI16_565_GREEN_MASK, FI16_565_BLUE_MASK);
	if(!src) return FALSE;
	for(unsigned y = 0; y < sh; y++) {
		BYTE *bits = FreeImage_GetScanLine(src, y);
		for(unsigned i = 0; i < FreeImage_GetLine(src); i++) {
			bits[i] = (BYTE)(y * 37 + i * 11 + 1);
		}
	}
	bpp = FreeImage_GetBPP(src);
	if(bpp <= 8) {
		RGBQUAD *pal = FreeImage_GetPalette(src);
		for(unsigned i = 0; i < FreeImage_GetColorsUsed(src); i++) {
			pal[i].rgbRed = (BYTE)(i * 3);
			pal[i].rgbGreen = (BYTE)(255 - i);
			pal[i].rgbBlue = (BYTE)(i * 7);
		}
		BYTE table[2] = { 0, 128 };
		FreeImage_SetTransparencyTable(src, table, 2);
	}

	BOOL bResult = TRUE;
	for(unsigned s = 0; (s < sizeof(sizes) / sizeof(sizes[0])) && bResult; s++) {
		const unsigned dw = sizes[s][0], dh = sizes[s][1];
		FIBITMAP *dst = FreeImage_Rescale(src, dw, dh, FILTER_NEAREST);
		bResult = dst && (FreeImage_GetImageType(dst) == image_type) && (FreeImage_GetBPP(dst) == bpp)
			&& (FreeImage_GetWidth(dst) == dw) && (FreeImage_GetHeight(dst) == dh);
		if(bResult && (bpp <= 8)) {
			bResult = (memcmp(FreeImage_GetPalette(dst), FreeImage_GetPalette(src), FreeImage_GetColorsUsed(src) * sizeof(RGBQUAD)) == 0)
				&& (FreeImage_GetTransparencyCount(dst) == 2) && FreeImage_IsTransparent(dst);
		}
		if(bResult && (bpp == 16) && (image_type == FIT_BITMAP)) {
			bResult = (FreeImage_GetRedMask(dst) == FI16_565_RED_MASK) && (FreeImage_GetGreenMask(dst) == FI16_565_GREEN_MASK);
		}
		// rows are counted from the top, as the image is seen
		for(unsigned y = 0; (y < dh) && bResult; y++) {
			const unsigned sy = sh - 1 - (2 * y + 1) * sh / (2 * dh);
			for(unsigned x = 0; (x < dw) && bResult; x++) {
				const unsigned sx = (2 * x + 1) * sw / (2 * dw);
				if(bpp < 8) {
					BYTE a = 0, b = 0;
					FreeImage_GetPixelIndex(src, sx, sy, &a);
					FreeImage_GetPixelIndex(dst, x, dh - 1 - y, &b);
					bResult = (a == b);
				} else {
					const unsigned n = bpp / 8;
					bResult = memcmp(FreeImage_GetScanLine(src, sy) + sx * n, FreeImage_GetScanLine(dst, dh - 1 - y) + x * n, n) == 0;
				}
			}
		}
		if(dst) FreeImage_Unload(dst);
	}

	FreeImage_Unload(src);
	return bResult;
}

// FILTER_NEAREST with FI_RESCALE_TRUE_COLOR: a transparent 8-bit image becomes 32-bit RGBA
static BOOL testRescaleNearestTrueColor() {
	FIBITMAP *src = FreeImage_AllocateT(FIT_BITMAP, 4, 4, 8);
	if(!src) return FALSE;
	RGBQUAD *pal = FreeImage_GetPalette(src);
	for(unsigned i = 0; i < 256; i++) {
		pal[i].rgbRed = (BYTE)i;
		pal[i].rgbGreen = (BYTE)(i ^ 0x55);
		pal[i].rgbBlue = (BYTE)(255 - i);
	}
	BYTE table[16];
	for(unsigned i = 0; i < 16; i++) {
		table[i] = (BYTE)(i * 16);
	}
	FreeImage_SetTransparencyTable(src, table, 16);
	for(unsigned y = 0; y < 4; y++) {
		for(unsigned x = 0; x < 4; x++) {
			BYTE index = (BYTE)(y * 4 + x);
			FreeImage_SetPixelIndex(src, x, y, &index);
		}
	}

	FIBITMAP *dst = FreeImage_RescaleRect(src, 8, 8, 0, 0, 4, 4, FILTER_NEAREST, FI_RESCALE_TRUE_COLOR);
	BOOL bResult = dst && (FreeImage_GetBPP(dst) == 32);
	for(unsigned y = 0; (y < 8) && bResult; y++) {
		for(unsigned x = 0; (x < 8) && bResult; x++) {
			RGBQUAD color;
			FreeImage_GetPixelColor(dst, x, y, &color);
			const BYTE index = (BYTE)((y / 2) * 4 + x / 2);
			bResult = (color.rgbRed == pal[index].rgbRed) && (color.rgbGreen == pal[index].rgbGreen)
				&& (color.rgbBlue == pal[index].rgbBlue) && (color.rgbReserved == table[index]);
		}
	}
	if(dst) FreeImage_Unload(dst);

	FreeImage_Unload(src);
	return bResult;
}

// Main test functions
// ----------------------------------------------------------

void testImageType(unsigned width, unsigned height) {
	BOOL bResult = FALSE;

	printf("testImageType ...\n");

	bResult = testAllocateCloneUnloadType(FIT_BITMAP, width, height);
	assert(bResult);
	bResult = testAllocateCloneUnloadType(FIT_UINT16, width, height);
	assert(bResult);
	bResult = testAllocateCloneUnloadType(FIT_INT16, width, height);
	assert(bResult);
	bResult = testAllocateCloneUnloadType(FIT_UINT32, width, height);
	assert(bResult);
	bResult = testAllocateCloneUnloadType(FIT_INT32, width, height);
	assert(bResult);
	bResult = testAllocateCloneUnloadType(FIT_FLOAT, width, height);
	assert(bResult);
	bResult = testAllocateCloneUnloadType(FIT_DOUBLE, width, height);
	assert(bResult);
	bResult = testAllocateCloneUnloadType(FIT_COMPLEX, width, height);
	assert(bResult);
	bResult = testAllocateCloneUnloadType(FIT_RGB16, width, height);
	assert(bResult);
	bResult = testAllocateCloneUnloadType(FIT_RGBA16, width, height);
	assert(bResult);
	bResult = testAllocateCloneUnloadType(FIT_RGBF, width, height);
	assert(bResult);
	bResult = testAllocateCloneUnloadType(FIT_RGBAF, width, height);
	assert(bResult);
}

void testRescaleNearest() {
	BOOL bResult = FALSE;

	printf("testRescaleNearest ...\n");

	const unsigned bitmap_bpp[] = { 1, 4, 8, 16, 24, 32 };
	for(unsigned i = 0; i < sizeof(bitmap_bpp) / sizeof(bitmap_bpp[0]); i++) {
		bResult = testRescaleNearestType(FIT_BITMAP, bitmap_bpp[i]);
		assert(bResult);
	}
	const FREE_IMAGE_TYPE types[] = { FIT_UINT16, FIT_INT16, FIT_UINT32, FIT_INT32, FIT_FLOAT, FIT_DOUBLE, FIT_COMPLEX, FIT_RGB16, FIT_RGBA16, FIT_RGBF, FIT_RGBAF };
	for(unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
		bResult = testRescaleNearestType(types[i], 0);
		assert(bResult);
	}
	bResult = testRescaleNearestTrueColor();
	assert(bResult);
}


void testImageTypeTIFF(unsigned width, unsigned height) {
	BOOL bResult = FALSE;

	printf("testImageTypeTIFF ...\n");

	// create a test 8-bit image
	FIBITMAP *src = createZonePlateImage(width, height, 128);
	assert(src != NULL);

	// save for further examination
	bResult = FreeImage_Save(FIF_PNG, src, "zoneplate.png", PNG_DEFAULT);
	assert(bResult);

	// test load /save / convert
	// -------------------------	

	bResult = testLoadSaveConvertImageType(src, FIT_BITMAP);
	assert(bResult);
	bResult = testLoadSaveConvertImageType(src, FIT_UINT16);
	assert(bResult);
	bResult = testLoadSaveConvertImageType(src, FIT_INT16);
	assert(bResult);
	bResult = testLoadSaveConvertImageType(src, FIT_UINT32);
	assert(bResult);
	bResult = testLoadSaveConvertImageType(src, FIT_INT32);
	assert(bResult);
	bResult = testLoadSaveConvertImageType(src, FIT_FLOAT);
	assert(bResult);
	bResult = testLoadSaveConvertImageType(src, FIT_DOUBLE);
	assert(bResult);

	// complex type
	bResult = testLoadSaveConvertComplexType(src, FICC_REAL);
	assert(bResult);
	bResult = testLoadSaveConvertComplexType(src, FICC_IMAG);
	assert(bResult);
	bResult = testLoadSaveConvertComplexType(src, FICC_MAG);
	assert(bResult);
	bResult = testLoadSaveConvertComplexType(src, FICC_PHASE);
	assert(bResult);

	// free test image
	FreeImage_Unload(src);

}
