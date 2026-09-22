// ==========================================================
// Flipping routines
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
// - Hervé Drolon (drolon@infonie.fr)
// - Jim Keir (jimkeir@users.sourceforge.net)
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

#include "FreeImage.h"
#include "Utilities.h"

/**
Flip the image horizontally along the vertical axis.
@param src Input image to be processed.
@return Returns TRUE if successful, FALSE otherwise.
*/
BOOL DLL_CALLCONV 
FreeImage_FlipHorizontal(FIBITMAP *src) {
	if (!FreeImage_HasPixels(src)) return FALSE;

	size_t line 	= FreeImage_GetLine(src);
	size_t width	= FreeImage_GetWidth(src);
	size_t height 	= FreeImage_GetHeight(src);

	unsigned bytespp = FreeImage_GetLine(src) / FreeImage_GetWidth(src);

	// copy between aligned memories
	BYTE *new_bits = (BYTE*)FreeImage_Aligned_Malloc(line * sizeof(BYTE), FIBITMAP_ALIGNMENT);
	if (!new_bits) return FALSE;

	// mirror the buffer

	for (unsigned y = 0; y < height; y++) {
		BYTE *bits = FreeImage_GetScanLine(src, y);
		memcpy(new_bits, bits, line);

		switch (FreeImage_GetBPP(src)) {
			case 1 :
			{				
				for(unsigned x = 0; x < width; x++) {
					// get pixel at (x, y)
					BOOL value = (new_bits[x >> 3] & (0x80 >> (x & 0x07))) != 0;
					// set pixel at (new_x, y)
					unsigned new_x = width - 1 - x;
					value ? bits[new_x >> 3] |= (0x80 >> (new_x & 0x7)) : bits[new_x >> 3] &= (0xff7f >> (new_x & 0x7));
				}
			}
			break;

			case 4 :
			{
				// per pixel: reversing bytes breaks odd widths
				for(size_t x = 0; x < width; x++) {
					// get pixel at (x, y)
					const BYTE value = (x & 1) ? (BYTE)(new_bits[x >> 1] & 0x0F) : (BYTE)(new_bits[x >> 1] >> 4);
					// set pixel at (new_x, y)
					const size_t new_x = width - 1 - x;
					if (new_x & 1) {
						bits[new_x >> 1] = (BYTE)((bits[new_x >> 1] & 0xF0) | value);
					} else {
						bits[new_x >> 1] = (BYTE)((bits[new_x >> 1] & 0x0F) | (value << 4));
					}
				}
			}
			break;

			case 8:
			{				
				BYTE *dst_data = (BYTE*) bits; 				
				BYTE *src_data = (BYTE*) (new_bits + line - bytespp); 				
				for(unsigned c = 0; c < width; c++) { 			
					*dst_data++ = *src_data--;  
				} 
			}
			break;

			case 16:
			{				
				WORD *dst_data = (WORD*) bits; 				
				WORD *src_data = (WORD*) (new_bits + line - bytespp); 				
				for(unsigned c = 0; c < width; c++) { 			
					*dst_data++ = *src_data--;  
				} 
			}
			break;

			case 24 :
			case 32 :
			case 48:
			case 64:
			case 96:
			case 128:
			{				
				BYTE *dst_data = (BYTE*) bits; 				
				BYTE *src_data = (BYTE*) (new_bits + line - bytespp); 				
				for(unsigned c = 0; c < width; c++) { 		
					for(unsigned k = 0; k < bytespp; k++) {
						*dst_data++ = src_data[k];  
					}
					src_data -= bytespp;
				} 
			}
			break;

		}
	}

	FreeImage_Aligned_Free(new_bits);

	return TRUE;
}


/**
Flip the image vertically along the horizontal axis.
@param src Input image to be processed.
@return Returns TRUE if successful, FALSE otherwise.
*/

BOOL DLL_CALLCONV 
FreeImage_FlipVertical(FIBITMAP *src) {
	BYTE *From, *Mid;

	if (!FreeImage_HasPixels(src)) return FALSE;

	// swap the buffer
	// use size_t in order to handle very large files

	size_t pitch  = FreeImage_GetPitch(src);
	size_t height = FreeImage_GetHeight(src);
	size_t line   = FreeImage_GetLine(src);
	const unsigned width = FreeImage_GetWidth(src);
	const unsigned bpp   = FreeImage_GetBPP(src);

	// copy between aligned memories
	Mid = (BYTE*)FreeImage_Aligned_Malloc(pitch * sizeof(BYTE), FIBITMAP_ALIGNMENT);
	if (!Mid) return FALSE;

	From = FreeImage_GetBits(src);
	size_t line_s = 0;
	size_t line_t = (height-1) * pitch;

	for(size_t y = 0; y < height/2; y++) {

		// copy pixels only: a view shares its row's last byte
		memcpy(Mid, From + line_s, line);
		CopyRowPixels(From + line_s, From + line_t, width, bpp);
		CopyRowPixels(From + line_t, Mid, width, bpp);
		line_s += pitch;
		line_t -= pitch;
	}

	FreeImage_Aligned_Free(Mid);

	return TRUE;
}

