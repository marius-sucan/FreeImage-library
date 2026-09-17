// ==========================================================
// Upsampling / downsampling routine
//
// Design and implementation by
// - Hervé Drolon (drolon@infonie.fr)
// - Carsten Klein (cklein05@users.sourceforge.net)
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

#include "Resize.h"

FIBITMAP * DLL_CALLCONV
FreeImage_RescaleRect(FIBITMAP *src, int dst_width, int dst_height, int src_left, int src_top, int src_right, int src_bottom, FREE_IMAGE_FILTER filter, unsigned flags) {
	FIBITMAP *dst = NULL;

	const int src_width = FreeImage_GetWidth(src);
	const int src_height = FreeImage_GetHeight(src);

	if (!FreeImage_HasPixels(src) || (dst_width <= 0) || (dst_height <= 0) || (src_width <= 0) || (src_height <= 0)) {
		return NULL;
	}

	// normalize the rectangle
	if (src_right < src_left) {
		INPLACESWAP(src_left, src_right);
	}
	if (src_bottom < src_top) {
		INPLACESWAP(src_top, src_bottom);
	}

	// check the size of the sub image
	if((src_left < 0) || (src_right > src_width) || (src_top < 0) || (src_bottom > src_height)) {
		return NULL;
	}
	// an empty rectangle has no pixels to filter: the weights table would be built
	// against a zero source size, and the non-FIT_BITMAP filters divide by it
	if((src_right <= src_left) || (src_bottom <= src_top)) {
		return NULL;
	}

	// select the filter
	CGenericFilter *pFilter = NULL;
	switch (filter) {
		case FILTER_BOX:
			pFilter = new(std::nothrow) CBoxFilter();
			break;
		case FILTER_BICUBIC:
			pFilter = new(std::nothrow) CBicubicFilter();
			break;
		case FILTER_BILINEAR:
			pFilter = new(std::nothrow) CBilinearFilter();
			break;
		case FILTER_BSPLINE:
			pFilter = new(std::nothrow) CBSplineFilter();
			break;
		case FILTER_CATMULLROM:
			pFilter = new(std::nothrow) CCatmullRomFilter();
			break;
		case FILTER_LANCZOS3:
			pFilter = new(std::nothrow) CLanczos3Filter();
			break;
	}

	if (!pFilter) {
		return NULL;
	}

	CResizeEngine Engine(pFilter);

	dst = Engine.scale(src, dst_width, dst_height, src_left, src_top,
			src_right - src_left, src_bottom - src_top, flags, 0, 0, 0);

	delete pFilter;

	if ((flags & FI_RESCALE_OMIT_METADATA) != FI_RESCALE_OMIT_METADATA) {
		// copy metadata from src to dst
		FreeImage_CloneMetadata(dst, src);
	}

	return dst;
}

FIBITMAP * DLL_CALLCONV
FreeImage_Rescale(FIBITMAP *src, int dst_width, int dst_height, FREE_IMAGE_FILTER filter) {
	return FreeImage_RescaleRect(src, dst_width, dst_height, 0, 0, FreeImage_GetWidth(src), FreeImage_GetHeight(src), filter, FI_RESCALE_DEFAULT);
}

BOOL DLL_CALLCONV
FreeImage_RescaleRawBits(BYTE *src_bits, BYTE *dst_bits, FREE_IMAGE_TYPE type, int width, int height, int src_pitch, int dst_pitch, unsigned bpp, int dst_width, int dst_height, int src_left, int src_top, int src_right, int src_bottom, FREE_IMAGE_FILTER filter) {
   FIBITMAP *src = NULL;
   if((src_pitch <= 0) || (dst_pitch <= 0)) {
      // both are signed here but unsigned inside the library, where a negative
      // value becomes a stride of billions
      return FALSE;
   }
   src = FreeImage_AllocateHeaderForBits(src_bits, src_pitch, type, width, height, bpp, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
   if(!src) {
      return FALSE;
   }

   const int src_width = FreeImage_GetWidth(src);
   const int src_height = FreeImage_GetHeight(src);

   if (!FreeImage_HasPixels(src) || (dst_width <= 0) || (dst_height <= 0) || (src_width <= 0) || (src_height <= 0)) {
      FreeImage_Unload(src);
      return FALSE;
   }

   FIBITMAP *dst = NULL;
   // normalize the rectangle
   if (src_right < src_left) {
      INPLACESWAP(src_left, src_right);
   }
   if (src_bottom < src_top) {
      INPLACESWAP(src_top, src_bottom);
   }

   // check the size of the sub image
   if((src_left < 0) || (src_right > src_width) || (src_top < 0) || (src_bottom > src_height)) {
      FreeImage_Unload(src);
      return FALSE;
   }
   // an empty rectangle has no pixels to filter: see FreeImage_RescaleRect
   if((src_right <= src_left) || (src_bottom <= src_top)) {
      FreeImage_Unload(src);
      return FALSE;
   }

   // select the filter
   CGenericFilter *pFilter = NULL;
   switch (filter) {
      case FILTER_BOX:
         pFilter = new(std::nothrow) CBoxFilter();
         break;
      case FILTER_BICUBIC:
         pFilter = new(std::nothrow) CBicubicFilter();
         break;
      case FILTER_BILINEAR:
         pFilter = new(std::nothrow) CBilinearFilter();
         break;
      case FILTER_BSPLINE:
         pFilter = new(std::nothrow) CBSplineFilter();
         break;
      case FILTER_CATMULLROM:
         pFilter = new(std::nothrow) CCatmullRomFilter();
         break;
      case FILTER_LANCZOS3:
         pFilter = new(std::nothrow) CLanczos3Filter();
         break;
   }

   if (!pFilter) {
      FreeImage_Unload(src);
      return FALSE;
   }

   CResizeEngine Engine(pFilter);
   dst = Engine.scale(src, dst_width, dst_height, src_left, src_top,
         src_right - src_left, src_bottom - src_top, 0, 1, dst_pitch, dst_bits);

   delete pFilter;

   if(!dst) {
      // either the destination bit depth would have had to change, which this
      // entry point cannot express, or the header allocation failed
      FreeImage_Unload(src);
      return FALSE;
   }

   BOOL bResult = TRUE;
   if(FreeImage_GetBits(dst) != dst_bits) {
      // scale() writes through the header it wrapped around dst_bits on the
      // filtered path, but its early exit - when the source rectangle is already
      // the destination size - returns a freshly allocated bitmap instead, which
      // would leave dst_bits untouched
      const unsigned line = FreeImage_GetLine(dst);
      if((unsigned)dst_pitch >= line) {
         for(unsigned y = 0; y < FreeImage_GetHeight(dst); y++) {
            memcpy(dst_bits + (size_t)y * dst_pitch, FreeImage_GetScanLine(dst, y), line);
         }
      } else {
         bResult = FALSE;
      }
   }

   FreeImage_Unload(dst);
   FreeImage_Unload(src);   // the header wrapped around src_bits, leaked until now
   return bResult;
}

FIBITMAP * DLL_CALLCONV
FreeImage_MakeThumbnail(FIBITMAP *dib, int max_pixel_size, BOOL convert) {
	FIBITMAP *thumbnail = NULL;
	int new_width, new_height;

	if(!FreeImage_HasPixels(dib) || (max_pixel_size <= 0)) return NULL;

	int width	= FreeImage_GetWidth(dib);
	int height = FreeImage_GetHeight(dib);

	if(max_pixel_size == 0) max_pixel_size = 1;

	if((width < max_pixel_size) && (height < max_pixel_size)) {
		// image is smaller than the requested thumbnail
		return FreeImage_Clone(dib);
	}

	if(width > height) {
		new_width = max_pixel_size;
		// change image height with the same ratio
		double ratio = ((double)new_width / (double)width);
		new_height = (int)(height * ratio + 0.5);
		if(new_height == 0) new_height = 1;
	} else {
		new_height = max_pixel_size;
		// change image width with the same ratio
		double ratio = ((double)new_height / (double)height);
		new_width = (int)(width * ratio + 0.5);
		if(new_width == 0) new_width = 1;
	}

	const FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib);

	// perform downsampling using a bilinear interpolation

	switch(image_type) {
		case FIT_BITMAP:
		case FIT_UINT16:
		case FIT_RGB16:
		case FIT_RGBA16:
		case FIT_FLOAT:
		case FIT_RGBF:
		case FIT_RGBAF:
		{
			FREE_IMAGE_FILTER filter = FILTER_BILINEAR;
			thumbnail = FreeImage_Rescale(dib, new_width, new_height, filter);
		}
		break;

		case FIT_INT16:
		case FIT_UINT32:
		case FIT_INT32:
		case FIT_DOUBLE:
		case FIT_COMPLEX:
		default:
			// cannot rescale this kind of image
			thumbnail = NULL;
			break;
	}

	if((thumbnail != NULL) && (image_type != FIT_BITMAP) && convert) {
		// convert to a standard bitmap
		FIBITMAP *bitmap = NULL;
		switch(image_type) {
			case FIT_UINT16:
				bitmap = FreeImage_ConvertTo8Bits(thumbnail);
				break;
			case FIT_RGB16:
				bitmap = FreeImage_ConvertTo24Bits(thumbnail);
				break;
			case FIT_RGBA16:
				bitmap = FreeImage_ConvertTo32Bits(thumbnail);
				break;
			case FIT_FLOAT:
				bitmap = FreeImage_ConvertToStandardType(thumbnail, TRUE);
				break;
			case FIT_RGBF:
				bitmap = FreeImage_ToneMapping(thumbnail, FITMO_DRAGO03);
				break;
			case FIT_RGBAF:
				// no way to keep the transparency yet ...
				FIBITMAP *rgbf = FreeImage_ConvertToRGBF(thumbnail);
				bitmap = FreeImage_ToneMapping(rgbf, FITMO_DRAGO03);
				FreeImage_Unload(rgbf);
				break;
		}
		if(bitmap != NULL) {
			FreeImage_Unload(thumbnail);
			thumbnail = bitmap;
		}
	}

	// copy metadata from src to dst
	FreeImage_CloneMetadata(thumbnail, dib);

	return thumbnail;
}
