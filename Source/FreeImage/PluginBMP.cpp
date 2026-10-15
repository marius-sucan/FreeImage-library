// ==========================================================
// BMP Loader and Writer
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
// - Markus Loibl (markus.loibl@epost.de)
// - Martin Weber (martweb@gmx.net)
// - Hervé Drolon (drolon@infonie.fr)
// - Michal Novotny (michal@etc.cz)
// - Mihail Naydenov (mnaydenov@users.sourceforge.net)
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
#include "Plugin.h"

// ----------------------------------------------------------
//   Constants + headers
// ----------------------------------------------------------

static const BYTE RLE_COMMAND     = 0;
static const BYTE RLE_ENDOFLINE   = 0;
static const BYTE RLE_ENDOFBITMAP = 1;
static const BYTE RLE_DELTA       = 2;

static const BYTE BI_RGB            = 0;	// compression: none
static const BYTE BI_RLE8           = 1;	// compression: RLE 8-bit/pixel
static const BYTE BI_RLE4           = 2;	// compression: RLE 4-bit/pixel
static const BYTE BI_BITFIELDS      = 3;	// compression: Bit field or Huffman 1D compression for BITMAPCOREHEADER2
static const BYTE BI_JPEG           = 4;	// compression: JPEG or RLE-24 compression for BITMAPCOREHEADER2
static const BYTE BI_PNG            = 5;	// compression: PNG
static const BYTE BI_ALPHABITFIELDS = 6;	// compression: Bit field (this value is valid in Windows CE .NET 4.0 and later)

static const DWORD BMP_V5_HEADER_SIZE   = 124;			// sizeof(BITMAPV5HEADER)
static const DWORD BMP_LCS_SRGB         = 0x73524742;	// bV5CSType 'sRGB'
static const DWORD BMP_PROFILE_EMBEDDED = 0x4D424544;	// bV5CSType 'MBED': an ICC profile in the file
static const DWORD BMP_LCS_GM_IMAGES    = 4;			// bV5Intent: perceptual
static const DWORD BMP_MAX_PROFILE_SIZE = 64 << 20;

// ----------------------------------------------------------

#ifdef _WIN32
#pragma pack(push, 1)
#else
#pragma pack(1)
#endif

typedef struct tagBITMAPCOREHEADER {
  DWORD   bcSize;
  WORD    bcWidth;
  WORD    bcHeight;
  WORD    bcPlanes;
  WORD    bcBitCnt;
} BITMAPCOREHEADER, *PBITMAPCOREHEADER; 

typedef struct tagBITMAPINFOOS2_1X_HEADER {
  DWORD  biSize;
  WORD   biWidth;
  WORD   biHeight; 
  WORD   biPlanes; 
  WORD   biBitCount;
} BITMAPINFOOS2_1X_HEADER, *PBITMAPINFOOS2_1X_HEADER; 

typedef struct tagBITMAPFILEHEADER {
  WORD    bfType;		//! The file type
  DWORD   bfSize;		//! The size, in bytes, of the bitmap file
  WORD    bfReserved1;	//! Reserved; must be zero
  WORD    bfReserved2;	//! Reserved; must be zero
  DWORD   bfOffBits;	//! The offset, in bytes, from the beginning of the BITMAPFILEHEADER structure to the bitmap bits
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;

#ifdef _WIN32
#pragma pack(pop)
#else
#pragma pack()
#endif

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ==========================================================
// Internal functions
// ==========================================================

#ifdef FREEIMAGE_BIGENDIAN
static void
SwapInfoHeader(BITMAPINFOHEADER *header) {
	SwapLong(&header->biSize);
	SwapLong((DWORD *)&header->biWidth);
	SwapLong((DWORD *)&header->biHeight);
	SwapShort(&header->biPlanes);
	SwapShort(&header->biBitCount);
	SwapLong(&header->biCompression);
	SwapLong(&header->biSizeImage);
	SwapLong((DWORD *)&header->biXPelsPerMeter);
	SwapLong((DWORD *)&header->biYPelsPerMeter);
	SwapLong(&header->biClrUsed);
	SwapLong(&header->biClrImportant);
}

static void
SwapCoreHeader(BITMAPCOREHEADER *header) {
	SwapLong(&header->bcSize);
	SwapShort(&header->bcWidth);
	SwapShort(&header->bcHeight);
	SwapShort(&header->bcPlanes);
	SwapShort(&header->bcBitCnt);
}

static void
SwapOS21XHeader(BITMAPINFOOS2_1X_HEADER *header) {
	SwapLong(&header->biSize);
	SwapShort(&header->biWidth);
	SwapShort(&header->biHeight);
	SwapShort(&header->biPlanes);
	SwapShort(&header->biBitCount);
}

static void
SwapFileHeader(BITMAPFILEHEADER *header) {
	SwapShort(&header->bfType);
  	SwapLong(&header->bfSize);
  	SwapShort(&header->bfReserved1);
  	SwapShort(&header->bfReserved2);
	SwapLong(&header->bfOffBits);
}
#endif

// --------------------------------------------------------------------------

/**
Check if a BITMAPINFOHEADER is valid
@return Returns TRUE if successful, returns FALSE otherwise
*/
static BOOL
CheckBitmapInfoHeader(BITMAPINFOHEADER *bih) {
	// the 40-byte header, or a larger one that extends it (V2 to V5, OS/2 2.x)
	if (bih->biSize < sizeof(BITMAPINFOHEADER)) {
		return FALSE;
	}
	if (bih->biWidth < 0) {
		return FALSE;
	}
	if (bih->biHeight < 0) {
		// If biHeight is negative, indicating a top-down DIB, biCompression must be either BI_RGB or BI_BITFIELDS. 
		// Top-down DIBs cannot be compressed.
		// If biCompression is BI_JPEG or BI_PNG, the biHeight member specifies the height of the decompressed JPEG or PNG image file, respectively.
		if ((bih->biCompression != BI_RGB) && (bih->biCompression != BI_BITFIELDS) && (bih->biCompression != BI_ALPHABITFIELDS)) {
			return FALSE;
		}
	}
	if (bih->biPlanes != 1) {
		//  The number of planes for the target device. This value must be set to 1.
		return FALSE;
	}
	switch (bih->biBitCount) {
		case 0:
			// The number of bits-per-pixel is specified or is implied by the JPEG or PNG format.
			// JPEG or PNG is not yet supported			
			return FALSE;
			break;
		case 1:
		case 4:
		case 8:
		case 16:
		case 24:
		case 32:
			break;
		default:
			// Unsupported bitdepth
			return FALSE;
	}
	switch (bih->biCompression) {
		case BI_RGB:
		case BI_RLE8:
		case BI_RLE4:
		case BI_BITFIELDS:
			break;
		case BI_ALPHABITFIELDS:
			// four masks, for 16 and 32 bits only
			if ((bih->biBitCount != 16) && (bih->biBitCount != 32)) {
				return FALSE;
			}
			break;
		case BI_JPEG:
		case BI_PNG:
		default:
			return FALSE;
	}

	return TRUE;
}

// the compressions each depth decodes: RLE4 at 4 bits, RLE8 at 8, masks at 16 and 32 (and 24, off-spec, as GIMP 2.10.22 wrote)
static BOOL
CompressionSupported(unsigned bit_count, unsigned compression) {
	switch (compression) {
		case BI_RGB:
			return TRUE;
		case BI_RLE4:
			return (bit_count == 4);
		case BI_RLE8:
			return (bit_count == 8);
		case BI_BITFIELDS:
			return (bit_count >= 16);
		case BI_ALPHABITFIELDS:
			return (bit_count == 16) || (bit_count == 32);
		default:
			return FALSE;
	}
}

// --------------------------------------------------------------------------

/**
Load uncompressed image pixels for 1-, 4-, 8-, 16-, 24- and 32-bit dib
@param io FreeImage IO
@param handle FreeImage IO handle
@param dib Image to be loaded 
@param height Image height
@param pitch Image pitch
@param bit_count Image bit-depth (1-, 4-, 8-, 16-, 24- or 32-bit)
@param bounded TRUE: a cut file that holds too few bytes for the size it claims returns 0 rows
@return Returns the rows read: fewer than the height for a cut file
*/
static int
LoadPixelData(FreeImageIO *io, fi_handle handle, FIBITMAP *dib, int height, unsigned pitch, unsigned bit_count, BOOL bounded = FALSE) {
	// Load pixel data
	// NB: height can be < 0 for BMP data
	BYTE *scan0 = FreeImage_GetBits(dib);
	INT64 positiveHeight = abs(height);
	// a row that lacks only its padding is complete
	const unsigned used = FreeImage_GetLine(dib);
	int rows = (int)positiveHeight;
	BYTE *line = NULL;
	for (INT64 c = 0; c < positiveHeight; c++) {
		if (height>0)
		   line = (BYTE*)((unsigned long long)scan0 + (unsigned long long)pitch * c);
		else
		   line = (BYTE*)((unsigned long long)scan0 + (unsigned long long)pitch * (positiveHeight - c - 1));
		const unsigned got = io->read_proc(line, 1, pitch, handle);
		if(got < pitch) {
			rows = (int)c + ((got >= used) ? 1 : 0);
			if (bounded && !PlausibleImageSize((UINT64)pitch * positiveHeight, (UINT64)pitch * c + got, 1)) {
				return 0;
			}
			break;
		}
	}

	// swap as needed
#ifdef FREEIMAGE_BIGENDIAN
	if (bit_count == 16) {
		for(unsigned y = 0; y < FreeImage_GetHeight(dib); y++) {
			WORD *pixel = (WORD *)FreeImage_GetScanLine(dib, y);
			for(unsigned x = 0; x < FreeImage_GetWidth(dib); x++) {
				SwapShort(pixel);
				pixel++;
			}
		}
	}
#endif

#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB
	if (bit_count == 24 || bit_count == 32) {
		for(unsigned y = 0; y < FreeImage_GetHeight(dib); y++) {
			BYTE *pixel = FreeImage_GetScanLine(dib, y);
			for(unsigned x = 0; x < FreeImage_GetWidth(dib); x++) {
				INPLACESWAP(pixel[0], pixel[2]);
				pixel += (bit_count >> 3);
			}
		}
	}
#endif

	return rows;
}

/**
Load image pixels for 4-bit RLE compressed dib
@param io FreeImage IO
@param handle FreeImage IO handle
@param width Image width
@param height Image height
@param dib 4-bit image to be loaded 
@return Returns the rows decoded: fewer than the height for a cut or damaged file, -1 on error
*/
static int
LoadPixelDataRLE4(FreeImageIO *io, fi_handle handle, int width, int height, FIBITMAP *dib) {
	int status_byte = 0;
	BYTE second_byte = 0;
	int bits = 0;

	BYTE *pixels = NULL;	// temporary 8-bit buffer

	try {
		height = abs(height);

		// a cut file keeps the rows it holds; a full row still waiting for its end-of-line counts
		int rows = height;
		int scanline = 0;
		const INT64 start = io->tell_proc(handle);

		pixels = (BYTE*)malloc((unsigned long long)width * height * sizeof(BYTE));
		if (!pixels) {
			throw(1);
		}
		memset(pixels, 0, (unsigned long long)width * height * sizeof(BYTE));

		BYTE *q = pixels;
		BYTE *end = pixels + height * width;

		for (; scanline < height; ) {
			if (q < pixels || q  >= end) {
				break;
			}
			if(io->read_proc(&status_byte, sizeof(BYTE), 1, handle) != 1) {
				goto cut;
			}
			if (status_byte != 0)	{
				status_byte = (int)MIN((size_t)status_byte, (size_t)(end - q));
				// Encoded mode
				if(io->read_proc(&second_byte, sizeof(BYTE), 1, handle) != 1) {
					goto cut;
				}
				for (int i = 0; i < status_byte; i++)	{
					*q++ = (BYTE)((i & 0x01) ? (second_byte & 0x0f) : ((second_byte >> 4) & 0x0f));
				}
				bits += status_byte;
			}
			else {
				// Escape mode
				if(io->read_proc(&status_byte, sizeof(BYTE), 1, handle) != 1) {
					goto cut;
				}
				switch (status_byte) {
					case RLE_ENDOFLINE:
					{
						// End of line
						bits = 0;
						scanline++;
						q = pixels + scanline * width;
					}
					break;

					case RLE_ENDOFBITMAP:
						// End of bitmap
						q = end;
						break;

					case RLE_DELTA:
					{
						// read the delta values
						BYTE delta_x = 0;
						BYTE delta_y = 0;

						if(io->read_proc(&delta_x, sizeof(BYTE), 1, handle) != 1) {
							goto cut;
						}
						if(io->read_proc(&delta_y, sizeof(BYTE), 1, handle) != 1) {
							goto cut;
						}

						// apply them
						bits += delta_x;
						scanline += delta_y;
						q = pixels + scanline*width+bits;
					}
					break;

					default:
					{
						// Absolute mode
						status_byte = (int)MIN((size_t)status_byte, (size_t)(end - q));
						for (int i = 0; i < status_byte; i++) {
							if ((i & 0x01) == 0) {
								if(io->read_proc(&second_byte, sizeof(BYTE), 1, handle) != 1) {
									goto cut;
								}
							}
							*q++ = (BYTE)((i & 0x01) ? (second_byte & 0x0f) : ((second_byte >> 4) & 0x0f));
							bits++;
						}
						// Read pad byte
						if (((status_byte & 0x03) == 1) || ((status_byte & 0x03) == 2)) {
							BYTE padding = 0;
							if(io->read_proc(&padding, sizeof(BYTE), 1, handle) != 1) {
								goto cut;
							}
						}
					}
					break;
				}
			}
		}
		
		if (false) {
cut:
			rows = scanline + ((bits >= width) ? 1 : 0);
			// nothing is kept of a huge claim a few bytes long
			const INT64 stop = io->tell_proc(handle);
			if (!PlausibleImageSize(((UINT64)width * height + 1) / 2, (stop > start) ? (UINT64)(stop - start) : 0, 64)) {
				rows = 0;
			}
		}
		
		{
			// Convert to 4-bit
			for(int y = 0; y < height; y++) {
				const BYTE *src = (BYTE*)pixels + (unsigned long long)y * width;
				BYTE *dst = FreeImage_GetScanLine(dib, y);

				BOOL hinibble = TRUE;

				for (int cols = 0; cols < width; cols++){
					if ((src + cols) >= end) {
						throw(1);
					}

					if (hinibble) {
						dst[cols >> 1] = (src[cols] << 4);
					} else {
						dst[cols >> 1] |= src[cols];
					}

					hinibble = !hinibble;
				}
			}
		}

		free(pixels);

		return rows;

	} catch(int) {
		if (pixels) {
			free(pixels);
		}
		return -1;
	}
}

/**
Load image pixels for 8-bit RLE compressed dib
@param io FreeImage IO
@param handle FreeImage IO handle
@param width Image width
@param height Image height
@param dib 8-bit image to be loaded 
@return Returns the rows decoded: fewer than the height for a cut or damaged file
*/
static int
DecodeRLE8(FreeImageIO *io, fi_handle handle, int width, int height, FIBITMAP *dib) {
   BYTE status_byte = 0;
   BYTE second_byte = 0;
   int scanline = 0;
   INT64 bits = 0;	// up to width + 1, past the row end
   int count = 0;
   BYTE delta_x = 0;
   BYTE delta_y = 0;

   height = abs(height);

   // a cut or damaged file keeps the rows before it; a full row still waiting for its end-of-line counts
   while(scanline < height) {
      if (io->read_proc(&status_byte, sizeof(BYTE), 1, handle) != 1) {
         return scanline + ((bits >= width) ? 1 : 0);
      }

      if (status_byte == RLE_COMMAND) {
         if (io->read_proc(&status_byte, sizeof(BYTE), 1, handle) != 1) {
            return scanline + ((bits >= width) ? 1 : 0);
         }

         switch (status_byte) {
            case RLE_ENDOFLINE:
               bits = 0;
               scanline++;
               break;

            case RLE_ENDOFBITMAP:
               return height;

            case RLE_DELTA:
               // read the delta values
               delta_x = 0;
               delta_y = 0;
               if (io->read_proc(&delta_x, sizeof(BYTE), 1, handle) != 1) {
                  return scanline;
               }
               if (io->read_proc(&delta_y, sizeof(BYTE), 1, handle) != 1) {
                  return scanline;
               }
               // apply them, no further than just past the row end or the last row
               bits = MIN(bits + delta_x, (INT64)width + 1);
               scanline = (int)MIN((INT64)scanline + delta_y, (INT64)height);
               break;

            default:
            {
               // absolute mode
               count = (int)MIN((INT64)status_byte, width - bits);
               if (count < 0) {
                  return scanline;
               }
               if (count > 0) {
                  BYTE *sline = FreeImage_GetScanLine(dib, scanline);
                  if (io->read_proc((void *)(sline + bits), sizeof(BYTE) * count, 1, handle) != 1) {
                     return scanline;
                  }
                  bits += count;
               }
               // skip the pixels past the row end, and the pad to an even count
               BYTE skipped[256];
               const int rest = (status_byte - count) + (status_byte & 1);
               if ((rest > 0) && (io->read_proc(skipped, rest, 1, handle) != 1)) {
                  return scanline + ((bits >= width) ? 1 : 0);
               }
               break;
            }

         } // switch (status_byte)
      }
      else {
         count = (int)MIN((INT64)status_byte, width - bits);
         if (count < 0) {
            return scanline;
         }
         BYTE *sline = FreeImage_GetScanLine(dib, scanline);
         if (io->read_proc(&second_byte, sizeof(BYTE), 1, handle) != 1) {
            return scanline;
         }
         for (int i = 0; i < count; i++) {
            *(sline + bits) = second_byte;
            bits++;
         }
      }
   }

   return height;
}

// as DecodeRLE8(), but nothing is kept of a huge claim a few bytes long
static int
LoadPixelDataRLE8(FreeImageIO *io, fi_handle handle, int width, int height, FIBITMAP *dib) {
	const INT64 start = io->tell_proc(handle);
	const int rows = DecodeRLE8(io, handle, width, height, dib);
	if (rows < abs(height)) {
		const INT64 stop = io->tell_proc(handle);
		if (!PlausibleImageSize((UINT64)width * abs(height), (stop > start) ? (UINT64)(stop - start) : 0, 128)) {
			return 0;
		}
	}
	return rows;
}

// the rows a cut or damaged file holds are kept, with the error and a warning; none at all refuses the file
static BOOL
KeptRows(int rows, int height, const char *error) {
	height = abs(height);
	if (rows < 1) {
		return FALSE;
	}
	if (rows < height) {
		FreeImage_OutputMessageProc(s_format_id, error);
		PartialImageWarning(s_format_id, rows, height);
	}
	return TRUE;
}

// a cut file keeps what it holds, as it always did here, now with a warning
static void
WarnRows(int rows, int height) {
	height = abs(height);
	if (rows < height) {
		PartialImageWarning(s_format_id, (rows > 0) ? rows : 0, height);
	}
}

// nothing is allocated for a raster over 64 MB that the pixel data the file holds could not fill
static BOOL
PixelDataCanFill(FreeImageIO *io, fi_handle handle, INT64 bitmap_bits_offset, UINT64 width, UINT64 height, unsigned bit_count, unsigned compression) {
	const UINT64 pitch = (((width * bit_count) + 7) / 8 + 3) & ~(UINT64)3;
	const UINT64 raster = (height && (pitch > ~(UINT64)0 / height)) ? ~(UINT64)0 : pitch * height;
	// under the 64 MB floor the file size is not needed
	if (PlausibleImageSize(raster, 0, 1)) {
		return TRUE;
	}
	const INT64 here = io->tell_proc(handle);
	io->seek_proc(handle, 0, SEEK_END);
	const INT64 end = io->tell_proc(handle);
	const UINT64 data = ((end > 0) && ((UINT64)end > (UINT64)bitmap_bits_offset)) ? (UINT64)end - (UINT64)bitmap_bits_offset : 0;
	// RLE8 expands up to 128 times, RLE4 64 times, except where it skips pixels
	BOOL plausible = PlausibleImageSize(raster, data, (compression == BI_RLE8) ? 128 : (compression == BI_RLE4) ? 64 : 1) ? TRUE : FALSE;
	if (!plausible && ((compression == BI_RLE8) || (compression == BI_RLE4)) && (data >= 4)) {
		// an RLE image that ends with its end-of-bitmap code, maybe padded, is whole however little it holds
		BYTE tail[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
		io->seek_proc(handle, end - 4, SEEK_SET);
		if (io->read_proc(tail, 1, 4, handle) == 4) {
			plausible = ((tail[2] == RLE_COMMAND) && (tail[3] == RLE_ENDOFBITMAP)) ||
				((tail[0] == RLE_COMMAND) && (tail[1] == RLE_ENDOFBITMAP) && (tail[2] == 0) && (tail[3] == 0));
		}
	}
	io->seek_proc(handle, here, SEEK_SET);
	return plausible;
}

// one channel of a BI_BITFIELDS layout
typedef struct tagBMPCHANNEL {
	DWORD mask;
	unsigned shift;
	DWORD max;
	BYTE *table;
} BMPCHANNEL;

static void
InitChannel(BMPCHANNEL *channel, DWORD mask) {
	channel->mask = mask;
	channel->shift = 0;
	channel->max = 0;
	channel->table = NULL;
	if (mask) {
		while (((mask >> channel->shift) & 1) == 0) {
			channel->shift++;
		}
		channel->max = mask >> channel->shift;
		// up to 16 bits wide, the 8-bit values come from a table
		if ((channel->max != 0xFF) && (channel->max <= 0xFFFF)) {
			channel->table = (BYTE*)malloc(channel->max + 1);
			if (channel->table) {
				for (DWORD value = 0; value <= channel->max; value++) {
					channel->table[value] = (BYTE)((value * 255 + channel->max / 2) / channel->max);
				}
			}
		}
	}
}

// the channel scaled to 8 bits, rounded; a missing channel is 0
static inline BYTE
ChannelValue(const BMPCHANNEL *channel, DWORD pixel) {
	const DWORD value = (pixel & channel->mask) >> channel->shift;
	if (channel->table) {
		return channel->table[value];
	}
	if (channel->max == 0xFF) {
		return (BYTE)value;
	}
	return channel->max ? (BYTE)(((UINT64)value * 255 + channel->max / 2) / channel->max) : (BYTE)0;
}

/**
Load 16-, 24- or 32-bit pixels in a BI_BITFIELDS layout FreeImage does not keep, into a 24- or 32-bit dib
@param masks Red, green, blue and alpha
@param alpha FALSE: a 32-bit dib is opaque
@return Returns the rows read: fewer than the height for a cut file, -1 when out of memory
*/
static int
LoadPixelDataBitfields(FreeImageIO *io, fi_handle handle, FIBITMAP *dib, int height, unsigned pitch, unsigned bit_count, const DWORD *masks, BOOL alpha) {
	const unsigned width = FreeImage_GetWidth(dib);
	const unsigned src_bytes = bit_count / 8;
	const unsigned dst_bytes = FreeImage_GetBPP(dib) / 8;
	// a row that lacks only its padding is complete
	const unsigned used = width * src_bytes;
	const int lines = abs(height);
	int rows = -1;
	BMPCHANNEL channels[4];
	for (int i = 0; i < 4; i++) {
		InitChannel(&channels[i], masks[i]);
	}
	BYTE *row = (BYTE*)malloc(pitch);
	if (row) {
		rows = lines;
		for (int c = 0; c < lines; c++) {
			const unsigned got = io->read_proc(row, 1, pitch, handle);
			BYTE *dst = FreeImage_GetScanLine(dib, (height > 0) ? c : (lines - 1 - c));
			const BYTE *src = row;
			const unsigned pixels = MIN(got, used) / src_bytes;
			for (unsigned x = 0; x < pixels; x++) {
				DWORD pixel = src[0] | ((DWORD)src[1] << 8);
				if (src_bytes >= 3) {
					pixel |= (DWORD)src[2] << 16;
				}
				if (src_bytes == 4) {
					pixel |= (DWORD)src[3] << 24;
				}
				dst[FI_RGBA_RED]   = ChannelValue(&channels[0], pixel);
				dst[FI_RGBA_GREEN] = ChannelValue(&channels[1], pixel);
				dst[FI_RGBA_BLUE]  = ChannelValue(&channels[2], pixel);
				if (dst_bytes == 4) {
					dst[FI_RGBA_ALPHA] = alpha ? ChannelValue(&channels[3], pixel) : (BYTE)0xFF;
				}
				src += src_bytes;
				dst += dst_bytes;
			}
			if (got < pitch) {
				rows = c + ((got >= used) ? 1 : 0);
				break;
			}
		}
		free(row);
	}
	for (int i = 0; i < 4; i++) {
		free(channels[i].table);
	}
	return rows;
}

// a 32-bit dib without alpha: every fourth byte becomes 0xFF
static void
SetOpaque(FIBITMAP *dib) {
	const unsigned width = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	for (unsigned y = 0; y < height; y++) {
		BYTE *bits = FreeImage_GetScanLine(dib, y);
		for (unsigned x = 0; x < width; x++, bits += 4) {
			bits[FI_RGBA_ALPHA] = 0xFF;
		}
	}
	FreeImage_SetTransparent(dib, FALSE);
}

// BI_RGB leaves the fourth byte "not used", yet writers store alpha there: zero everywhere means no alpha
static void
SetAlphaFromUnusedByte(FIBITMAP *dib) {
	const unsigned width = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	for (unsigned y = 0; y < height; y++) {
		const BYTE *bits = FreeImage_GetScanLine(dib, y);
		for (unsigned x = 0; x < width; x++, bits += 4) {
			if (bits[FI_RGBA_ALPHA]) {
				FreeImage_SetTransparent(dib, (FreeImage_GetColorType(dib) == FIC_RGBALPHA));
				return;
			}
		}
	}
	SetOpaque(dib);
}

// a little-endian DWORD of the file
static BOOL
ReadDWORD(FreeImageIO *io, fi_handle handle, DWORD *value) {
	BYTE b[4];
	if (io->read_proc(b, 4, 1, handle) != 1) {
		return FALSE;
	}
	*value = (DWORD)b[0] | ((DWORD)b[1] << 8) | ((DWORD)b[2] << 16) | ((DWORD)b[3] << 24);
	return TRUE;
}

/**
Attach the ICC profile a BITMAPV5HEADER embeds: bV5ProfileSize bytes at bV5ProfileData from the header's start.
A linked profile names a file and is not followed; only an RGB profile describes a DIB's colors.
*/
static void
LoadEmbeddedProfile(FreeImageIO *io, fi_handle handle, INT64 header_start, FIBITMAP *dib) {
	DWORD cs_type = 0, offset = 0, size = 0;
	io->seek_proc(handle, header_start + 56, SEEK_SET);
	if (!ReadDWORD(io, handle, &cs_type) || (cs_type != BMP_PROFILE_EMBEDDED)) {
		return;
	}
	io->seek_proc(handle, header_start + 112, SEEK_SET);
	if (!ReadDWORD(io, handle, &offset) || !ReadDWORD(io, handle, &size)) {
		return;
	}
	io->seek_proc(handle, 0, SEEK_END);
	const INT64 end = io->tell_proc(handle);
	const INT64 start = header_start + (INT64)offset;
	if ((offset < BMP_V5_HEADER_SIZE) || (start + (INT64)size > end)) {
		FreeImage_OutputMessageProc(s_format_id, "The embedded ICC profile lies outside the file: ignored");
		return;
	}
	if ((size < 132) || (size > BMP_MAX_PROFILE_SIZE)) {
		FreeImage_OutputMessageProc(s_format_id, "The embedded ICC profile is damaged or not an RGB profile: ignored");
		return;
	}
	BYTE *profile = (BYTE*)malloc(size);
	if (!profile) {
		return;
	}
	BOOL valid = FALSE;
	io->seek_proc(handle, start, SEEK_SET);
	if (io->read_proc(profile, size, 1, handle) == 1) {
		// a profile padded past its own size keeps its own size
		const DWORD own_size = ((DWORD)profile[0] << 24) | ((DWORD)profile[1] << 16) | ((DWORD)profile[2] << 8) | (DWORD)profile[3];
		if ((own_size >= 132) && (own_size < size)) {
			size = own_size;
		}
		valid = PNG_IsEmbeddableProfile(profile, size, TRUE);
	}
	if (valid) {
		FreeImage_CreateICCProfile(dib, profile, (long)size);
	} else {
		FreeImage_OutputMessageProc(s_format_id, "The embedded ICC profile is damaged or not an RGB profile: ignored");
	}
	free(profile);
}

// --------------------------------------------------------------------------

static FIBITMAP *
LoadWindowsBMP(FreeImageIO *io, fi_handle handle, int flags, INT64 bitmap_bits_offset, int type) {
	FIBITMAP *dib = NULL;

	try {
		BOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

		// load the info header
		BITMAPINFOHEADER bih;
		memset(&bih, 0, sizeof(BITMAPINFOHEADER));
		if (io->read_proc(&bih, sizeof(BITMAPINFOHEADER), 1, handle) != 1) {
			throw FI_MSG_ERROR_INVALID_FORMAT;
		}

#ifdef FREEIMAGE_BIGENDIAN
		SwapInfoHeader(&bih);
#endif

		if (CheckBitmapInfoHeader(&bih) == FALSE) {
			throw FI_MSG_ERROR_INVALID_FORMAT;
		}
		// before a header-only load returns, as the full load would refuse it
		if (!CompressionSupported(bih.biBitCount, bih.biCompression)) {
			throw FI_MSG_ERROR_UNSUPPORTED_COMPRESSION;
		}

		// keep some general information about the bitmap

		unsigned used_colors		= bih.biClrUsed;
		INT64 width					= bih.biWidth;
		INT64 height				= bih.biHeight;		// WARNING: height can be < 0 => check each call using 'height' as a parameter
		unsigned bit_count		= bih.biBitCount;
		unsigned compression		= bih.biCompression;
		unsigned long pitch		= CalculatePitch(CalculateLine(width, bit_count));

		if (!header_only && !PixelDataCanFill(io, handle, bitmap_bits_offset, (UINT64)width, (UINT64)((height < 0) ? -height : height), bit_count, compression)) {
			throw "The file holds too little data for its size";
		}

		switch (bit_count) {
			case 1 :
			case 4 :
			case 8 :
			{
				if ((used_colors == 0) || (used_colors > CalculateUsedPaletteEntries(bit_count))) {
					used_colors = CalculateUsedPaletteEntries(bit_count);
				}
				
				// allocate enough memory to hold the bitmap (header, palette, pixels) and read the palette

				dib = FreeImage_AllocateHeader(header_only, width, height, bit_count);
				if (dib == NULL) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}

				// set resolution information
				FreeImage_SetDotsPerMeterX(dib, bih.biXPelsPerMeter);
				FreeImage_SetDotsPerMeterY(dib, bih.biYPelsPerMeter);

				// seek to the end of the header (depending on the BMP header version)
				// type == sizeof(BITMAPVxINFOHEADER)
				switch(type) {
					case 40:	// sizeof(BITMAPINFOHEADER) - all Windows versions since Windows 3.0
						break;
					case 52:	// sizeof(BITMAPV2INFOHEADER) (undocumented)
					case 56:	// sizeof(BITMAPV3INFOHEADER) (undocumented)
					case 108:	// sizeof(BITMAPV4HEADER) - all Windows versions since Windows 95/NT4 (not supported)
					case 124:	// sizeof(BITMAPV5HEADER) - Windows 98/2000 and newer (not supported)
						io->seek_proc(handle, (INT64)(type - sizeof(BITMAPINFOHEADER)), SEEK_CUR);
						break;
				}
				
				// load the palette
				io->read_proc(FreeImage_GetPalette(dib), used_colors * sizeof(RGBQUAD), 1, handle);

#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB
				RGBQUAD *pal = FreeImage_GetPalette(dib);
				for(unsigned i = 0; i < used_colors; i++) {
					INPLACESWAP(pal[i].rgbRed, pal[i].rgbBlue);
				}
#endif

				if(header_only) {
					// header only mode
					return dib;
				}

				// seek to the actual pixel data.
				// this is needed because sometimes the palette is larger than the entries it contains predicts
				io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);

				// read the pixel data

				switch (compression) {
					case BI_RGB :
						if( KeptRows(LoadPixelData(io, handle, dib, height, pitch, bit_count, TRUE), height, "Error encountered while decoding BMP data") ) {
							return dib;
						} else {
							throw "Error encountered while decoding BMP data";
						}
						break;

					case BI_RLE4 :
						if( (bit_count == 4) && KeptRows(LoadPixelDataRLE4(io, handle, width, height, dib), height, "Error encountered while decoding RLE4 BMP data") ) {
							return dib;
						} else {
							throw "Error encountered while decoding RLE4 BMP data";
						}
						break;

					case BI_RLE8 :
						if( (bit_count == 8) && KeptRows(LoadPixelDataRLE8(io, handle, width, height, dib), height, "Error encountered while decoding RLE8 BMP data") ) {
							return dib;
						} else {
							throw "Error encountered while decoding RLE8 BMP data";
						}
						break;

					default :
						throw FI_MSG_ERROR_UNSUPPORTED_COMPRESSION;
				}
			}
			break; // 1-, 4-, 8-bit

			case 16 :
			case 24 :
			case 32 :
			{
				// the masks follow a 40-byte header and sit inside a larger one; an alpha mask needs 56 bytes or BI_ALPHABITFIELDS
				DWORD masks[4] = { 0, 0, 0, 0 };
				BOOL bitfields = (compression == BI_BITFIELDS) || (compression == BI_ALPHABITFIELDS);
				if (bitfields) {
					const unsigned count = ((compression == BI_ALPHABITFIELDS) || (bih.biSize >= 56)) ? 4 : 3;
					if (io->read_proc(masks, count * sizeof(DWORD), 1, handle) != 1) {
						masks[0] = masks[1] = masks[2] = masks[3] = 0;
					}
					for (int i = 0; i < 4; i++) {
#ifdef FREEIMAGE_BIGENDIAN
						SwapLong(&masks[i]);
#endif
						// no channel lies past the pixel's bits
						if (bit_count < 32) {
							masks[i] &= ((DWORD)1 << bit_count) - 1;
						}
					}
					// an alpha mask that overlaps the colours is no alpha
					if (masks[3] & (masks[0] | masks[1] | masks[2])) {
						masks[3] = 0;
					}
					// no colour mask at all: the default layout
					bitfields = (masks[0] | masks[1] | masks[2]) ? TRUE : FALSE;
				}
				if (!bitfields) {
					masks[0] = (bit_count == 16) ? FI16_555_RED_MASK : 0x00FF0000;
					masks[1] = (bit_count == 16) ? FI16_555_GREEN_MASK : 0x0000FF00;
					masks[2] = (bit_count == 16) ? FI16_555_BLUE_MASK : 0x000000FF;
					masks[3] = 0;
				}
				const BOOL alpha = (masks[3] != 0);

				// the layouts FreeImage keeps as they are: 5-5-5 or 5-6-5 without alpha, B, G, R with or without alpha in a fourth byte
				BOOL native = FALSE;
				if (bit_count == 16) {
					native = !alpha && (masks[2] == FI16_555_BLUE_MASK) &&
						(((masks[0] == FI16_555_RED_MASK) && (masks[1] == FI16_555_GREEN_MASK)) || ((masks[0] == FI16_565_RED_MASK) && (masks[1] == FI16_565_GREEN_MASK)));
				} else {
					native = (masks[0] == 0x00FF0000) && (masks[1] == 0x0000FF00) && (masks[2] == 0x000000FF) && (!alpha || (masks[3] == 0xFF000000));
				}

				if (native && (bit_count == 16)) {
					dib = FreeImage_AllocateHeader(header_only, (int)width, (int)height, bit_count, masks[0], masks[1], masks[2]);
				} else {
					// a 32-bit layout stays 32-bit; a 16- or 24-bit one becomes 24-bit, or 32-bit with alpha
					const int bpp = ((bit_count == 32) || alpha) ? 32 : 24;
					dib = FreeImage_AllocateHeader(header_only, (int)width, (int)height, bpp, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
				}

				if (dib == NULL) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}

				// set resolution information
				FreeImage_SetDotsPerMeterX(dib, bih.biXPelsPerMeter);
				FreeImage_SetDotsPerMeterY(dib, bih.biYPelsPerMeter);

				if(header_only) {
					// header only mode
					return dib;
				}

				// seek to the actual pixel data
				io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);

				if (native) {
					// load pixel data and swap as needed if OS is Big Endian
					WarnRows(LoadPixelData(io, handle, dib, (int)height, (unsigned)pitch, bit_count), (int)height);
					if (bit_count == 32) {
						if (!bitfields) {
							SetAlphaFromUnusedByte(dib);
						} else if (alpha) {
							FreeImage_SetTransparent(dib, (FreeImage_GetColorType(dib) == FIC_RGBALPHA));
						} else {
							SetOpaque(dib);
						}
					}
				} else {
					const int rows = LoadPixelDataBitfields(io, handle, dib, (int)height, (unsigned)pitch, bit_count, masks, alpha);
					if (rows < 0) {
						throw FI_MSG_ERROR_MEMORY;
					}
					WarnRows(rows, (int)height);
					if (alpha) {
						FreeImage_SetTransparent(dib, (FreeImage_GetColorType(dib) == FIC_RGBALPHA));
					} else if ((bit_count == 32) && (rows < abs((int)height))) {
						// without alpha, the rows a cut file lacks are opaque too
						SetOpaque(dib);
					}
				}

				return dib;
			}
			break; // 16-, 24-, 32-bit
		}
	} catch(const char *message) {
		if(dib) {
			FreeImage_Unload(dib);
		}
		if(message) {
			FreeImage_OutputMessageProc(s_format_id, message);
		}
	}

	return NULL;
}

// --------------------------------------------------------------------------

static FIBITMAP *
LoadOS22XBMP(FreeImageIO *io, fi_handle handle, int flags, INT64 bitmap_bits_offset) {
	FIBITMAP *dib = NULL;

	try {
		BOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;
		// the palette follows the header, wherever the file starts in the stream
		const INT64 header_start = io->tell_proc(handle);

		// load the info header
		BITMAPINFOHEADER bih;
		memset(&bih, 0, sizeof(BITMAPINFOHEADER));
		if (io->read_proc(&bih, sizeof(BITMAPINFOHEADER), 1, handle) != 1) {
			throw FI_MSG_ERROR_INVALID_FORMAT;
		}

#ifdef FREEIMAGE_BIGENDIAN
		SwapInfoHeader(&bih);
#endif

		if (CheckBitmapInfoHeader(&bih) == FALSE) {
			throw FI_MSG_ERROR_INVALID_FORMAT;
		}
		// before a header-only load returns, as the full load would refuse it; OS/2 has no masks at 24 bits
		if (!CompressionSupported(bih.biBitCount, bih.biCompression) || ((bih.biBitCount == 24) && (bih.biCompression != BI_RGB))) {
			throw FI_MSG_ERROR_UNSUPPORTED_COMPRESSION;
		}

		// keep some general information about the bitmap

		unsigned used_colors	= bih.biClrUsed;
		int width				= bih.biWidth;
		int height				= bih.biHeight;		// WARNING: height can be < 0 => check each read_proc using 'height' as a parameter
		unsigned bit_count		= bih.biBitCount;
		unsigned compression	= bih.biCompression;
		unsigned pitch			= CalculatePitch(CalculateLine(width, bit_count));
		
		if (!header_only && !PixelDataCanFill(io, handle, bitmap_bits_offset, (UINT64)width, (UINT64)((height < 0) ? -(INT64)height : height), bit_count, compression)) {
			throw "The file holds too little data for its size";
		}

		switch (bit_count) {
			case 1 :
			case 4 :
			case 8 :
			{
				if ((used_colors == 0) || (used_colors > CalculateUsedPaletteEntries(bit_count)))
					used_colors = CalculateUsedPaletteEntries(bit_count);
					
				// allocate enough memory to hold the bitmap (header, palette, pixels) and read the palette

				dib = FreeImage_AllocateHeader(header_only, width, height, bit_count);

				if (dib == NULL) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}

				// set resolution information
				FreeImage_SetDotsPerMeterX(dib, bih.biXPelsPerMeter);
				FreeImage_SetDotsPerMeterY(dib, bih.biYPelsPerMeter);
				
				// load the palette
				// note that it may contain RGB or RGBA values : we will calculate this
				const INT64 pal_bytes = (INT64)bitmap_bits_offset - header_start - (INT64)bih.biSize;
				unsigned pal_size = (pal_bytes > 0) ? (unsigned)(pal_bytes / used_colors) : 0;

				io->seek_proc(handle, header_start + (INT64)bih.biSize, SEEK_SET);

				RGBQUAD *pal = FreeImage_GetPalette(dib);

				// 4-byte entries are OS/2 2.x's own; more room is padding
				if(pal_size >= 4) {
					for (unsigned count = 0; count < used_colors; count++) {
						FILE_BGRA bgra;

						// the entries a cut palette does not reach keep their default grey
						if (io->read_proc(&bgra, sizeof(FILE_BGRA), 1, handle) != 1) {
							break;
						}
						
						pal[count].rgbRed	= bgra.r;
						pal[count].rgbGreen = bgra.g;
						pal[count].rgbBlue	= bgra.b;
					} 
				} else if(pal_size == 3) {
					for (unsigned count = 0; count < used_colors; count++) {
						FILE_BGR bgr;

						// the entries a cut palette does not reach keep their default grey
						if (io->read_proc(&bgr, sizeof(FILE_BGR), 1, handle) != 1) {
							break;
						}
						
						pal[count].rgbRed	= bgr.r;
						pal[count].rgbGreen = bgr.g;
						pal[count].rgbBlue	= bgr.b;
					} 
				}
				
				if(header_only) {
					// header only mode
					return dib;
				}

				// seek to the actual pixel data.
				// this is needed because sometimes the palette is larger than the entries it contains predicts

				if (bitmap_bits_offset > (INT64)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + (used_colors * 3))) {
					io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);
				}

				// read the pixel data

				switch (compression) {
					case BI_RGB :
						// load pixel data 
						if (KeptRows(LoadPixelData(io, handle, dib, height, pitch, bit_count, TRUE), height, "Error encountered while decoding BMP data")) {
							return dib;
						}
						throw "Error encountered while decoding BMP data";

					case BI_RLE4 :
						if ((bit_count == 4) && KeptRows(LoadPixelDataRLE4(io, handle, width, height, dib), height, "Error encountered while decoding RLE4 BMP data")) {
							return dib;
						}
						else {
							throw "Error encountered while decoding RLE4 BMP data";
						}
						break;

					case BI_RLE8 :
						if ((bit_count == 8) && KeptRows(LoadPixelDataRLE8(io, handle, width, height, dib), height, "Error encountered while decoding RLE8 BMP data")) {
							return dib;
						}
						else {
							throw "Error encountered while decoding RLE8 BMP data";
						}
						break;

					default :		
						throw FI_MSG_ERROR_UNSUPPORTED_COMPRESSION;
				}	
			}

			case 16 :
			{
				if (bih.biCompression == BI_BITFIELDS) {
					DWORD bitfields[3];

					io->read_proc(bitfields, 3 * sizeof(DWORD), 1, handle);

					dib = FreeImage_AllocateHeader(header_only, width, height, bit_count, bitfields[0], bitfields[1], bitfields[2]);
				} else {
					dib = FreeImage_AllocateHeader(header_only, width, height, bit_count, FI16_555_RED_MASK, FI16_555_GREEN_MASK, FI16_555_BLUE_MASK);
				}

				if (dib == NULL) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}

				// set resolution information
				FreeImage_SetDotsPerMeterX(dib, bih.biXPelsPerMeter);
				FreeImage_SetDotsPerMeterY(dib, bih.biYPelsPerMeter);

				if(header_only) {
					// header only mode
					return dib;
				}

				if (bitmap_bits_offset > (INT64)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + (used_colors * 3))) {
					io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);
				}

				// load pixel data and swap as needed if OS is Big Endian
				WarnRows(LoadPixelData(io, handle, dib, height, pitch, bit_count), height);

				return dib;
			}

			case 24 :
			case 32 :
			{
				if( bit_count == 32 ) {
					dib = FreeImage_AllocateHeader(header_only, width, height, bit_count, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
				} else {
					dib = FreeImage_AllocateHeader(header_only, width, height, bit_count, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
				}

				if (dib == NULL) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}
				
				// set resolution information
				FreeImage_SetDotsPerMeterX(dib, bih.biXPelsPerMeter);
				FreeImage_SetDotsPerMeterY(dib, bih.biYPelsPerMeter);

				if(header_only) {
					// header only mode
					return dib;
				}

				// Skip over the optional palette 
				// A 24 or 32 bit DIB may contain a palette for faster color reduction

				if (bitmap_bits_offset > (INT64)(sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + (used_colors * 3))) {
					io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);
				}

				// read in the bitmap bits
				// load pixel data and swap as needed if OS is Big Endian
				WarnRows(LoadPixelData(io, handle, dib, height, pitch, bit_count), height);

				if (bit_count == 32) {
					SetAlphaFromUnusedByte(dib);
				}

				return dib;
			}
		}
	} catch(const char *message) {
		if (dib) {
			FreeImage_Unload(dib);
		}
		FreeImage_OutputMessageProc(s_format_id, message);
	}

	return NULL;
}

// --------------------------------------------------------------------------

static FIBITMAP *
LoadOS21XBMP(FreeImageIO *io, fi_handle handle, int flags, INT64 bitmap_bits_offset) {
	FIBITMAP *dib = NULL;

	try {
		BOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

		// load the info header
		BITMAPINFOOS2_1X_HEADER bios2_1x;
		memset(&bios2_1x, 0, sizeof(BITMAPINFOOS2_1X_HEADER));
		if (io->read_proc(&bios2_1x, sizeof(BITMAPINFOOS2_1X_HEADER), 1, handle) != 1) {
			throw FI_MSG_ERROR_INVALID_FORMAT;
		}

#ifdef FREEIMAGE_BIGENDIAN
		SwapOS21XHeader(&bios2_1x);
#endif
		// keep some general information about the bitmap

		unsigned used_colors = 0;
		unsigned width		= bios2_1x.biWidth;
		unsigned height		= bios2_1x.biHeight;	// WARNING: height can be < 0 => check each read_proc using 'height' as a parameter
		unsigned bit_count	= bios2_1x.biBitCount;
		unsigned pitch		= CalculatePitch(CalculateLine(width, bit_count));
		
		if (!header_only && !PixelDataCanFill(io, handle, bitmap_bits_offset, width, height, bit_count, BI_RGB)) {
			throw "The file holds too little data for its size";
		}

		switch (bit_count) {
			case 1 :
			case 4 :
			case 8 :
			{
				used_colors = CalculateUsedPaletteEntries(bit_count);
				
				// allocate enough memory to hold the bitmap (header, palette, pixels) and read the palette

				dib = FreeImage_AllocateHeader(header_only, width, height, bit_count);

				if (dib == NULL) {
					throw FI_MSG_ERROR_DIB_MEMORY;
				}

				// set resolution information to default values (72 dpi in english units)
				FreeImage_SetDotsPerMeterX(dib, 2835);
				FreeImage_SetDotsPerMeterY(dib, 2835);
				
				// load the palette

				RGBQUAD *pal = FreeImage_GetPalette(dib);

				for (unsigned count = 0; count < used_colors; count++) {
					FILE_BGR bgr;

					// the entries a cut palette does not reach keep their default grey
					if (io->read_proc(&bgr, sizeof(FILE_BGR), 1, handle) != 1) {
						break;
					}
					
					pal[count].rgbRed	= bgr.r;
					pal[count].rgbGreen = bgr.g;
					pal[count].rgbBlue	= bgr.b;
				}
				
				if(header_only) {
					// header only mode
					return dib;
				}

				// Skip over the optional palette 
				// A 24 or 32 bit DIB may contain a palette for faster color reduction

				io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);
				
				// read the pixel data

				// load pixel data 
				WarnRows(LoadPixelData(io, handle, dib, height, pitch, bit_count), height);

				return dib;
			}

			case 16 :
			{
				dib = FreeImage_AllocateHeader(header_only, width, height, bit_count, FI16_555_RED_MASK, FI16_555_GREEN_MASK, FI16_555_BLUE_MASK);

				if (dib == NULL) {
					throw FI_MSG_ERROR_DIB_MEMORY;						
				}

				// set resolution information to default values (72 dpi in english units)
				FreeImage_SetDotsPerMeterX(dib, 2835);
				FreeImage_SetDotsPerMeterY(dib, 2835);

				if(header_only) {
					// header only mode
					return dib;
				}

				// the pixels start at bfOffBits; an offset inside the headers is not trusted
				if (bitmap_bits_offset > io->tell_proc(handle)) {
					io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);
				}

				// load pixel data and swap as needed if OS is Big Endian
				WarnRows(LoadPixelData(io, handle, dib, height, pitch, bit_count), height);

				return dib;
			}

			case 24 :
			case 32 :
			{
				if( bit_count == 32 ) {
					dib = FreeImage_AllocateHeader(header_only, width, height, bit_count, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
				} else {
					dib = FreeImage_AllocateHeader(header_only, width, height, bit_count, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
				}

				if (dib == NULL) {
					throw FI_MSG_ERROR_DIB_MEMORY;						
				}

				// set resolution information to default values (72 dpi in english units)
				FreeImage_SetDotsPerMeterX(dib, 2835);
				FreeImage_SetDotsPerMeterY(dib, 2835);

				if(header_only) {
					// header only mode
					return dib;
				}

				// Skip over the optional palette 
				// A 24 or 32 bit DIB may contain a palette for faster color reduction

				// the pixels start at bfOffBits; an offset inside the headers is not trusted
				if (bitmap_bits_offset > io->tell_proc(handle)) {
					io->seek_proc(handle, bitmap_bits_offset, SEEK_SET);
				}

				// load pixel data and swap as needed if OS is Big Endian
				WarnRows(LoadPixelData(io, handle, dib, height, pitch, bit_count), height);

				if (bit_count == 32) {
					SetAlphaFromUnusedByte(dib);
				}

				return dib;
			}
		}
	} catch(const char *message) {	
		if (dib) {
			FreeImage_Unload(dib);
		}
		FreeImage_OutputMessageProc(s_format_id, message);
	}

	return NULL;
}

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "BMP";
}

static const char * DLL_CALLCONV
Description() {
	return "Windows or OS/2 Bitmap";
}

static const char * DLL_CALLCONV
Extension() {
	return "bmp";
}

static const char * DLL_CALLCONV
RegExpr() {
	return "^BM";
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/bmp";
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	BYTE bmp_signature1[] = { 0x42, 0x4D };
	BYTE bmp_signature2[] = { 0x42, 0x41 };
	BYTE signature[2] = { 0, 0 };

	io->read_proc(signature, 1, sizeof(bmp_signature1), handle);

	if (memcmp(bmp_signature1, signature, sizeof(bmp_signature1)) == 0)
		return TRUE;

	if (memcmp(bmp_signature2, signature, sizeof(bmp_signature2)) == 0)
		return TRUE;

	return FALSE;
}

static BOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return (
			(depth == 1) ||
			(depth == 4) ||
			(depth == 8) ||
			(depth == 16) ||
			(depth == 24) ||
			(depth == 32)
		);
}

static BOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return (type == FIT_BITMAP) ? TRUE : FALSE;
}

static BOOL DLL_CALLCONV
SupportsNoPixels() {
	return TRUE;
}

// read from and written to BITMAPV5HEADER files
static BOOL DLL_CALLCONV
SupportsICCProfiles() {
	return TRUE;
}

// ----------------------------------------------------------

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	if (handle != NULL) {
		BITMAPFILEHEADER bitmapfileheader;
		DWORD type = 0;

		// we use this offset value to make seemingly absolute seeks relative in the file		
		INT64 offset_in_file = io->tell_proc(handle);

		// read the fileheader
		memset(&bitmapfileheader, 0, sizeof(BITMAPFILEHEADER));
		if (io->read_proc(&bitmapfileheader, sizeof(BITMAPFILEHEADER), 1, handle) != 1) {
			return NULL;
		}

#ifdef FREEIMAGE_BIGENDIAN
		SwapFileHeader(&bitmapfileheader);
#endif

		// check the signature
		if((bitmapfileheader.bfType != 0x4D42) && (bitmapfileheader.bfType != 0x4142)) {
			FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_MAGIC_NUMBER);
			return NULL;
		}

		// read the first byte of the infoheader
		io->read_proc(&type, sizeof(DWORD), 1, handle);
		io->seek_proc(handle, 0 - (long long)sizeof(DWORD), SEEK_CUR);

#ifdef FREEIMAGE_BIGENDIAN
		SwapLong(&type);
#endif

		// call the appropriate load function for the found bitmap type

		switch(type) {
			case 12:
				// OS/2 and also all Windows versions since Windows 3.0
				return LoadOS21XBMP(io, handle, flags, offset_in_file + bitmapfileheader.bfOffBits);

			case 64:
				// OS/2
				return LoadOS22XBMP(io, handle, flags, offset_in_file + bitmapfileheader.bfOffBits);

			case 40:	// BITMAPINFOHEADER - all Windows versions since Windows 3.0
			case 52:	// BITMAPV2INFOHEADER (undocumented, partially supported)
			case 56:	// BITMAPV3INFOHEADER (undocumented, partially supported)
			case 108:	// BITMAPV4HEADER - all Windows versions since Windows 95/NT4 (partially supported)
			case 124:	// BITMAPV5HEADER - Windows 98/2000 and newer (partially supported)
			{
				const INT64 header_start = io->tell_proc(handle);
				FIBITMAP *dib = LoadWindowsBMP(io, handle, flags, offset_in_file + bitmapfileheader.bfOffBits, type);
				if (dib && (type == BMP_V5_HEADER_SIZE)) {
					LoadEmbeddedProfile(io, handle, header_start, dib);
				}
				return dib;
			}

			default:
				break;
		}

		FreeImage_OutputMessageProc(s_format_id, "Unknown bmp subtype with id %d", type);
	}

	return NULL;
}

// ----------------------------------------------------------

/**
Encode a 8-bit source buffer into a 8-bit target buffer using a RLE compression algorithm. 
The size of the target buffer must be equal to the size of the source buffer. 
On return, the function will return the real size of the target buffer, which should be less that or equal to the source buffer size. 
@param target 8-bit Target buffer
@param source 8-bit Source buffer
@param size Source/Target input buffer size
@return Returns the target buffer size
*/
static int
RLEEncodeLine(BYTE *target, BYTE *source, int size) {
	BYTE buffer[256];
	int buffer_size = 0;
	int target_pos = 0;

	for (int i = 0; i < size; ++i) {
		if ((i < size - 1) && (source[i] == source[i + 1])) {
			// find a solid block of same bytes

			int j = i + 1;
			int jmax = 254 + i;

			while ((j < size - 1) && (j < jmax) && (source[j] == source[j + 1]))
				++j;

			// if the block is larger than 3 bytes, use it
			// else put the data into the larger pool

			if (((j - i) + 1) > 3) {
				// don't forget to write what we already have in the buffer

				switch(buffer_size) {
					case 0 :
						break;

					case RLE_DELTA :
						target[target_pos++] = 1;
						target[target_pos++] = buffer[0];
						target[target_pos++] = 1;
						target[target_pos++] = buffer[1];
						break;

					case RLE_ENDOFBITMAP :
						target[target_pos++] = (BYTE)buffer_size;
						target[target_pos++] = buffer[0];
						break;

					default :
						target[target_pos++] = RLE_COMMAND;
						target[target_pos++] = (BYTE)buffer_size;
						memcpy(target + target_pos, buffer, buffer_size);

						// prepare for next run
						
						target_pos += buffer_size;

						if ((buffer_size & 1) == 1)
							target[target_pos++] = 0;

						break;
				}

				// write the continuous data

				target[target_pos++] = (BYTE)((j - i) + 1);
				target[target_pos++] = source[i];

				buffer_size = 0;
			} else {
				for (int k = 0; k < (j - i) + 1; ++k) {
					buffer[buffer_size++] = source[i + k];

					if (buffer_size == 254) {
						// write what we have

						target[target_pos++] = RLE_COMMAND;
						target[target_pos++] = (BYTE)buffer_size;
						memcpy(target + target_pos, buffer, buffer_size);

						// prepare for next run

						target_pos += buffer_size;
						buffer_size = 0;
					}
				}
			}

			i = j;
		} else {
			buffer[buffer_size++] = source[i];
		}

		// write the buffer if it's full

		if (buffer_size == 254) {
			target[target_pos++] = RLE_COMMAND;
			target[target_pos++] = (BYTE)buffer_size;
			memcpy(target + target_pos, buffer, buffer_size);

			// prepare for next run

			target_pos += buffer_size;
			buffer_size = 0;
		}
	}

	// write the last bytes

	switch(buffer_size) {
		case 0 :
			break;

		case RLE_DELTA :
			target[target_pos++] = 1;
			target[target_pos++] = buffer[0];
			target[target_pos++] = 1;
			target[target_pos++] = buffer[1];
			break;

		case RLE_ENDOFBITMAP :
			target[target_pos++] = (BYTE)buffer_size;
			target[target_pos++] = buffer[0];
			break;

		default :
			target[target_pos++] = RLE_COMMAND;
			target[target_pos++] = (BYTE)buffer_size;
			memcpy(target + target_pos, buffer, buffer_size);

			// prepare for next run
			
			target_pos += buffer_size;

			if ((buffer_size & 1) == 1)
				target[target_pos++] = 0;

			break;			
	}

	// write the END_OF_LINE marker

	target[target_pos++] = RLE_COMMAND;
	target[target_pos++] = RLE_ENDOFLINE;

	// return the written size

	return target_pos;
}

// the RLE8 data of an 8-bit dib, written to the io if one is given; returns its size (stopping past limit), 0 on failure
static UINT64
EncodeRLE8(FreeImageIO *io, fi_handle handle, FIBITMAP *dib, UINT64 limit) {
	const unsigned line = FreeImage_GetLine(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	BYTE *buffer = (BYTE*)malloc((size_t)CalculatePitch(line) * 2);
	if (!buffer) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_MEMORY);
		return 0;
	}
	UINT64 size = 0;
	for (unsigned y = 0; (y < height) && (size <= limit); y++) {
		const int n = RLEEncodeLine(buffer, FreeImage_GetScanLine(dib, y), (int)line);
		if (io && (io->write_proc(buffer, (unsigned)n, 1, handle) != 1)) {
			free(buffer);
			return 0;
		}
		size += n;
	}
	if (size <= limit) {
		buffer[0] = RLE_COMMAND;
		buffer[1] = RLE_ENDOFBITMAP;
		if (io && (io->write_proc(buffer, 2, 1, handle) != 1)) {
			free(buffer);
			return 0;
		}
		size += 2;
	}
	free(buffer);
	return size;
}

// the alpha of a 32-bit dib says something: not 0xFF everywhere (opaque), not 0 everywhere (never set); CMYK keeps K there
static BOOL
CarriesAlpha(FIBITMAP *dib) {
	if ((FreeImage_GetICCProfile(dib)->flags & FIICC_COLOR_IS_CMYK) == FIICC_COLOR_IS_CMYK) {
		return FALSE;
	}
	BOOL opaque = TRUE;
	BOOL zero = TRUE;
	const unsigned width = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	for (unsigned y = 0; y < height; y++) {
		const BYTE *bits = FreeImage_GetScanLine(dib, y);
		for (unsigned x = 0; x < width; x++, bits += 4) {
			opaque = opaque && (bits[FI_RGBA_ALPHA] == 0xFF);
			zero = zero && (bits[FI_RGBA_ALPHA] == 0);
			if (!opaque && !zero) {
				return TRUE;
			}
		}
	}
	return FALSE;
}

static BOOL DLL_CALLCONV
Save(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	if ((dib != NULL) && (handle != NULL)) {
		const FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib);
		if (!FreeImage_HasPixels(dib) || !SupportsExportType(image_type) || ((image_type == FIT_BITMAP) && !SupportsExportDepth(FreeImage_GetBPP(dib)))) {
			FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_UNSUPPORTED_FORMAT);
			return FALSE;
		}

		// write the file header

		const UINT64 dst_width = FreeImage_GetWidth(dib);
		const UINT64 dst_height = FreeImage_GetHeight(dib);

		// note that the dib may have been created using FreeImage_CreateView
		// we need to recalculate the dst pitch here
		const UINT64 dst_bpp = FreeImage_GetBPP(dib);
		const UINT64 dst_pitch = CalculatePitch(CalculateLine(dst_width, dst_bpp));
		// ...but rows step by the dib's own pitch (a view's is its parent's)
		const UINT64 src_pitch = FreeImage_GetPitch(dib);

		// only an RGB profile describes a DIB's colors, and the loader reads 64 MB at most
		const FIICCPROFILE *icc = FreeImage_GetICCProfile(dib);
		const BOOL embed = icc->data && ((icc->flags & FIICC_COLOR_IS_CMYK) != FIICC_COLOR_IS_CMYK) &&
			(icc->size <= BMP_MAX_PROFILE_SIZE) && PNG_IsEmbeddableProfile((const BYTE*)icc->data, icc->size, TRUE);
		if (icc->data && icc->size && !embed) {
			FreeImage_OutputMessageProc(s_format_id, "Warning: the ICC profile is invalid for this BMP and was left out");
		}
		const DWORD profile_size = embed ? icc->size : 0;

		// alpha masks and profiles need a BITMAPV5HEADER; every other image keeps the 40-byte header
		const BOOL alpha = (dst_bpp == 32) && CarriesAlpha(dib);
		const BOOL v5 = alpha || embed;
		const DWORD info_size = v5 ? BMP_V5_HEADER_SIZE : (DWORD)sizeof(BITMAPINFOHEADER);
		const BOOL rle = (dst_bpp == 8) && ((flags & BMP_SAVE_RLE) == BMP_SAVE_RLE);

		BITMAPFILEHEADER bitmapfileheader;
		bitmapfileheader.bfType = 0x4D42;
		bitmapfileheader.bfOffBits = (DWORD)(sizeof(BITMAPFILEHEADER) + info_size + FreeImage_GetColorsUsed(dib) * sizeof(RGBQUAD));
		bitmapfileheader.bfReserved1 = 0;
		bitmapfileheader.bfReserved2 = 0;

		// 16-bit and V5 32-bit pixels go by masks, after a 40-byte header or inside a V5 one (no alpha mask: opaque)
		const bool bit_fields = (dst_bpp == 16) || (v5 && (dst_bpp == 32));

		if (bit_fields && !v5) {
			bitmapfileheader.bfOffBits += 3 * sizeof(DWORD);
		}

		// the pixel data's size: RLE8 is encoded once to count it, then again to write it
		UINT64 image_size = dst_height * dst_pitch;
		if (rle) {
			image_size = EncodeRLE8(NULL, NULL, dib, (UINT64)0xFFFFFFFFu - bitmapfileheader.bfOffBits - profile_size);
			if (image_size == 0) {
				return FALSE;
			}
		}

		// bfSize is 32-bit
		if ((UINT64)bitmapfileheader.bfOffBits + image_size + profile_size > (UINT64)0xFFFFFFFFu) {
			FreeImage_OutputMessageProc(s_format_id, "A BMP file cannot pass 4 GB, the most its 32-bit size field holds");
			return FALSE;
		}
		bitmapfileheader.bfSize = (DWORD)(bitmapfileheader.bfOffBits + image_size + profile_size);

		// the profile ends the file; bV5ProfileData counts from the V5 header
		const DWORD profile_offset = (DWORD)(bitmapfileheader.bfOffBits - sizeof(BITMAPFILEHEADER) + image_size);

#ifdef FREEIMAGE_BIGENDIAN
		SwapFileHeader(&bitmapfileheader);
#endif
		if (io->write_proc(&bitmapfileheader, sizeof(BITMAPFILEHEADER), 1, handle) != 1) {
			return FALSE;
		}

		// update the bitmap info header

		BITMAPINFOHEADER bih;
		memcpy(&bih, FreeImage_GetInfoHeader(dib), sizeof(BITMAPINFOHEADER));

		if (v5) {
			bih.biSize = info_size;
		}
		if (bit_fields) {
			bih.biCompression = BI_BITFIELDS;
		}
		else if (rle) {
			bih.biCompression = BI_RLE8;
		}
		else {
			bih.biCompression = BI_RGB;
		}
		// only BI_RGB may leave it 0 (and should)
		bih.biSizeImage = (bih.biCompression == BI_RGB) ? 0 : (DWORD)image_size;

		// write the bitmap info header

#ifdef FREEIMAGE_BIGENDIAN
		SwapInfoHeader(&bih);
#endif
		if (io->write_proc(&bih, sizeof(BITMAPINFOHEADER), 1, handle) != 1) {
			return FALSE;
		}

		if (v5) {
			// the masks, then sRGB or the embedded profile: no endpoints or gammas
			DWORD tail[21] = { 0 };
			if (dst_bpp == 32) {
				tail[0] = 0x00FF0000;
				tail[1] = 0x0000FF00;
				tail[2] = 0x000000FF;
				tail[3] = alpha ? 0xFF000000 : 0;
			} else if (dst_bpp == 16) {
				tail[0] = FreeImage_GetRedMask(dib);
				tail[1] = FreeImage_GetGreenMask(dib);
				tail[2] = FreeImage_GetBlueMask(dib);
			}
			tail[4] = embed ? BMP_PROFILE_EMBEDDED : BMP_LCS_SRGB;
			tail[17] = BMP_LCS_GM_IMAGES;
			tail[18] = embed ? profile_offset : 0;
			tail[19] = profile_size;
#ifdef FREEIMAGE_BIGENDIAN
			for (int i = 0; i < 21; i++) {
				SwapLong(&tail[i]);
			}
#endif
			if (io->write_proc(tail, sizeof(tail), 1, handle) != 1) {
				return FALSE;
			}
		}

		// write the bit fields when we are dealing with a 16 bit BMP

		if (bit_fields && !v5) {
			DWORD d;

			d = FreeImage_GetRedMask(dib);

			if (io->write_proc(&d, sizeof(DWORD), 1, handle) != 1) {
				return FALSE;
			}

			d = FreeImage_GetGreenMask(dib);

			if (io->write_proc(&d, sizeof(DWORD), 1, handle) != 1) {
				return FALSE;
			}

			d = FreeImage_GetBlueMask(dib);

			if (io->write_proc(&d, sizeof(DWORD), 1, handle) != 1) {
				return FALSE;
			}
		}

		// write the palette

		if (FreeImage_GetPalette(dib) != NULL) {
			RGBQUAD *pal = FreeImage_GetPalette(dib);
			FILE_BGRA bgra;
			for(unsigned i = 0; i < FreeImage_GetColorsUsed(dib); i++ ) {
				bgra.b = pal[i].rgbBlue;
				bgra.g = pal[i].rgbGreen;
				bgra.r = pal[i].rgbRed;
				bgra.a = 0;	// reserved, must be zero
				if (io->write_proc(&bgra, sizeof(FILE_BGRA), 1, handle) != 1) {
					return FALSE;
				}
			}
		}

		// write the bitmap data... if RLE compression is enable, use it

		if (rle) {
			if (EncodeRLE8(io, handle, dib, image_size) != image_size) {
				return FALSE;
			}

#ifdef FREEIMAGE_BIGENDIAN
		} else if (dst_bpp == 16) {
			INT64 padding = dst_pitch - (unsigned long long)dst_width * sizeof(WORD);
			WORD pad = 0;
			WORD pixel;
			for(unsigned y = 0; y < dst_height; y++) {
				BYTE *line = FreeImage_GetScanLine(dib, y);
				for(unsigned x = 0; x < dst_width; x++) {
					pixel = ((WORD *)line)[x];
					SwapShort(&pixel);
					if (io->write_proc(&pixel, sizeof(WORD), 1, handle) != 1) {
						return FALSE;
					}
				}
				if(padding != 0) {
					if(io->write_proc(&pad, padding, 1, handle) != 1) {
						return FALSE;				
					}
				}
			}
#endif

#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB
		} else if (dst_bpp == 24) {
			BYTE *dBits = FreeImage_GetBits(dib);
			INT64 padding = dst_pitch - (unsigned long long)dst_width * sizeof(FILE_BGR);
			DWORD pad = 0;
			FILE_BGR bgr;
			for(unsigned y = 0; y < dst_height; y++) {
				BYTE *line = (BYTE*)((unsigned long long)dBits + src_pitch * y);
				for(unsigned x = 0; x < dst_width; x++) {
					RGBTRIPLE *triple = ((RGBTRIPLE *)line)+x;
					bgr.b = triple->rgbtBlue;
					bgr.g = triple->rgbtGreen;
					bgr.r = triple->rgbtRed;
					if (io->write_proc(&bgr, sizeof(FILE_BGR), 1, handle) != 1) {
						return FALSE;
					}
				}
				if(padding != 0) {
					if(io->write_proc(&pad, padding, 1, handle) != 1) {
						return FALSE;					
					}
				}
			}
		} else if (dst_bpp == 32) {
			FILE_BGRA bgra;
			BYTE *dBits = FreeImage_GetBits(dib);
			for(UINT64 y = 0; y < dst_height; y++) {
				BYTE *line = (BYTE*)((unsigned long long)dBits + src_pitch * y);
				for(UINT64 x = 0; x < dst_width; x++) {
					RGBQUAD *quad = ((RGBQUAD *)line)+x;
					bgra.b = quad->rgbBlue;
					bgra.g = quad->rgbGreen;
					bgra.r = quad->rgbRed;
					bgra.a = quad->rgbReserved;
					if (io->write_proc(&bgra, sizeof(FILE_BGRA), 1, handle) != 1) {
						return FALSE;
					}
				}
			}
#endif
		} else {
			// zeros past the last pixel, even inside a 1- or 4-bit row's last byte: a view's parent has pixels there
			const unsigned whole = CalculateWholeRowBytes((unsigned)dst_width, (unsigned)dst_bpp);
			const BYTE tail_mask = CalculateRowTailMask((unsigned)dst_width, (unsigned)dst_bpp);
			const unsigned tail_size = (unsigned)(dst_pitch - whole);
			BYTE tail[4] = { 0, 0, 0, 0 };
			BYTE *dBits = FreeImage_GetBits(dib);
			for (UINT64 y = 0; y < dst_height; y++) {
				BYTE *line = (BYTE*)((unsigned long long)dBits + src_pitch * y);
				if ((whole > 0) && (io->write_proc(line, whole, 1, handle) != 1)) {
					return FALSE;
				}
				if (tail_size > 0) {
					tail[0] = (BYTE)(tail_mask ? (line[whole] & tail_mask) : 0);
					if (io->write_proc(tail, tail_size, 1, handle) != 1) {
						return FALSE;
					}
				}
			}
		}

		if (embed && (io->write_proc(icc->data, profile_size, 1, handle) != 1)) {
			return FALSE;
		}

		return TRUE;

	} else {
		return FALSE;
	}
}

// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitBMP(Plugin *plugin, int format_id) {
	s_format_id = format_id;

	plugin->format_proc = Format;
	plugin->description_proc = Description;
	plugin->extension_proc = Extension;
	plugin->regexpr_proc = RegExpr;
	plugin->open_proc = NULL;
	plugin->close_proc = NULL;
	plugin->pagecount_proc = NULL;
	plugin->pagecapability_proc = NULL;
	plugin->load_proc = Load;
	plugin->save_proc = Save;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = SupportsICCProfiles;
	plugin->supports_no_pixels_proc = SupportsNoPixels;
}
