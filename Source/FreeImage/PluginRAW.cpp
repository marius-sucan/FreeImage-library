// ==========================================================
// RAW camera image loader
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

#include "../LibRawLite/libraw/libraw.h"

#include "FreeImage.h"
#include "Utilities.h"
#include "../Metadata/FreeImageTag.h"

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ==========================================================
// Internal functions
// ==========================================================

// ----------------------------------------------------------
//   FreeImage datastream wrapper
// ----------------------------------------------------------

class LibRaw_freeimage_datastream : public LibRaw_abstract_datastream {
private: 
	FreeImageIO *_io;
	fi_handle _handle;
	INT64 _start;
	INT64 _eof;
	INT64 _fsize;
	// LibRaw's bit readers take a byte per call: they read from here, not from the handle
	BYTE _buffer[4096];
	// the stream position of _buffer[0]; the handle is at _bufpos + _buflen
	INT64 _bufpos;
	unsigned _buflen;
	unsigned _bufcur;

	// refill the consumed buffer: FALSE at the end of the stream
	BOOL fill() {
		_bufpos += _buflen;
		_bufcur = 0;
		_buflen = _io->read_proc(_buffer, 1, (unsigned)sizeof(_buffer), _handle);
		return (_buflen > 0) ? TRUE : FALSE;
	}

public:
	LibRaw_freeimage_datastream(FreeImageIO *io, fi_handle handle) : _io(io), _handle(handle), _buflen(0), _bufcur(0) {
		_start = io->tell_proc(handle);
		io->seek_proc(handle, 0, SEEK_END);
		_eof = io->tell_proc(handle);
		_fsize = _eof - _start;
		io->seek_proc(handle, _start, SEEK_SET);
		_bufpos = _start;
	}

	~LibRaw_freeimage_datastream() {
	}

    int valid() {
		return (_io && _handle) ? 1 : 0;
	}

    int read(void *buffer, size_t size, size_t count) {
		if(size == 0) {
			return 0;
		}
		BYTE *dst = (BYTE*)buffer;
		const size_t total = size * count;
		size_t done = 0;
		while(done < total) {
			if(_bufcur == _buflen) {
				// a large read skips the buffer
				if(total - done >= sizeof(_buffer)) {
					_bufpos += _buflen;
					_buflen = _bufcur = 0;
					const size_t got = FreeImage_ReadBytes(_io, _handle, dst + done, total - done);
					_bufpos += got;
					done += got;
					break;
				}
				if(!fill()) {
					break;
				}
			}
			size_t n = _buflen - _bufcur;
			if(n > total - done) {
				n = total - done;
			}
			memcpy(dst + done, _buffer + _bufcur, n);
			_bufcur += (unsigned)n;
			done += n;
		}
		return (int)(done / size);
	}

	// LibRaw offsets are relative to where the stream started
    int seek(INT64 offset, int origin) {
		INT64 base;

		switch(origin) {
			case SEEK_SET:
				base = _start;
				break;
			case SEEK_END:
				base = _eof;
				break;
			case SEEK_CUR:
			default:
				base = _bufpos + _bufcur;
				break;
		}
		// an offset from the file may not fit past the base
		if((base < 0) || (offset > (std::numeric_limits<INT64>::max)() - base)) {
			return -1;
		}
		const INT64 target = base + offset;
		if(target < _start) {
			return -1;
		}
		// within the buffer, nothing is read again
		if((target >= _bufpos) && (target <= _bufpos + _buflen)) {
			_bufcur = (unsigned)(target - _bufpos);
			return 0;
		}

		const int result = _io->seek_proc(_handle, target, SEEK_SET);
		if(result == 0) {
			_bufpos = target;
			_buflen = _bufcur = 0;
		}
		return result;
	}

    INT64 tell() {
        return _bufpos + _bufcur - _start;
    }
	
	INT64 size() {
		return _fsize;
	}

	// 0..255, or -1 at EOF
    int get_char() { 
		if((_bufcur == _buflen) && !fill()) {
			return -1;
		}
		return _buffer[_bufcur++];
   }
	
	char* gets(char *buffer, int length) { 
		memset(buffer, 0, length);
		for(int i = 0; i < length; i++) {
			const int c = get_char();
			if (c < 0) {
				return NULL;
			}
			buffer[i] = (char)c;
			if (c == 0x0A) {
				break;
			}
		}
		return buffer;
	}

	// as fscanf: white space before the value is skipped; white space, a null or the end of the stream ends it
	int scanf_one(const char *fmt, void* val) {
		std::string buffer;
		int c = get_char();
		while((c > 0) && isspace(c)) {
			c = get_char();
		}
		while((c > 0) && !isspace(c)) {
			buffer.append(1, (char)c);
			c = get_char();
		}

		return buffer.empty() ? 0 : sscanf(buffer.c_str(), fmt, val);
	}

	int eof() { 
        return (_bufpos + _bufcur >= _eof);
    }
};

// ----------------------------------------------------------

// LibRaw data errors go to the output message, not stderr
static void
libraw_data_error_handler(void *data, const char *file, const INT64 offset) {
	if (offset < 0) {
		FreeImage_OutputMessageProc(s_format_id, "LibRaw : unexpected end of file");
	} else {
		// OutputMessageProc's %d is only an int
		char position[32];
		sprintf(position, "%lld", (long long)offset);
		FreeImage_OutputMessageProc(s_format_id, "LibRaw : data corrupted at offset %s", position);
	}
}

/**
Convert a processed raw data array to a FIBITMAP
@param RawProcessor LibRaw handle containing the processed raw image
@return Returns the converted dib if successfull, returns NULL otherwise
*/
static FIBITMAP * 
libraw_ConvertProcessedRawToDib(LibRaw *RawProcessor) {
	FIBITMAP *dib = NULL;
    int width, height, colors, bpp;

	try {
		int bgr = 0;	// pixel copy order: RGB if (bgr == 0) and BGR otherwise

		// get image info
		RawProcessor->get_mem_image_format(&width, &height, &colors, &bpp);

		// 3 colors, or 1 from a monochrome sensor
		if((colors != 1) && (colors != 3)) {
			throw "LibRaw : only 1- and 3-color images supported";
		}

		if(bpp == 16) {
			// allocate output dib
			dib = FreeImage_AllocateT((colors == 3) ? FIT_RGB16 : FIT_UINT16, width, height);
			if(!dib) {
				throw FI_MSG_ERROR_DIB_MEMORY;
			}

		} else if(bpp == 8) {
#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_BGR
			bgr = 1;	// only useful for FIT_BITMAP types
#endif

			// allocate output dib
			dib = FreeImage_AllocateT(FIT_BITMAP, width, height, 8 * colors);
			if(!dib) {
				throw FI_MSG_ERROR_DIB_MEMORY;
			}
		}

		// copy post-processed bitmap data into FIBITMAP buffer
		if(RawProcessor->copy_mem_image(FreeImage_GetBits(dib), FreeImage_GetPitch(dib), bgr) != LIBRAW_SUCCESS) {
			throw "LibRaw : failed to copy data into dib";
		}

		// flip vertically
		FreeImage_FlipVertical(dib);

		return dib;

	} catch(const char *text) {
		FreeImage_Unload(dib);
		FreeImage_OutputMessageProc(s_format_id, text);
		return NULL;
	}
}


/**
Convert a processed raw image to a FIBITMAP
@param image Processed raw image
@param flip LibRaw flip to apply: bit 2 transposes, bit 1 turns upside down, bit 0 mirrors
@param header_only TRUE to allocate the header only
@return Returns the converted dib if successfull, returns NULL otherwise
@see libraw_LoadEmbeddedPreview
*/
static FIBITMAP * 
libraw_ConvertProcessedImageToDib(libraw_processed_image_t *image, int flip, BOOL header_only) {
	FIBITMAP *dib = NULL;

	try {
		const unsigned src_width = image->width;
		const unsigned src_height = image->height;
		const unsigned bpp = image->bits;
		const size_t pixels = (size_t)src_width * src_height;
		if(((bpp != 8) && (bpp != 16)) || !pixels) {
			return NULL;
		}
		const size_t bytes = bpp / 8;
		// LibRaw can count 3 colors in a thumbnail of one
		unsigned colors = image->colors;
		if(image->data_size < pixels * colors * bytes) {
			colors = (image->data_size == pixels * bytes) ? 1 : 0;
		}
		if((colors != 1) && (colors != 3)) {
			return NULL;
		}

		// the thumbnail turns like the image
		const unsigned width = (flip & 4) ? src_height : src_width;
		const unsigned height = (flip & 4) ? src_width : src_height;

		// allocate output dib
		if(bpp == 16) {
			dib = FreeImage_AllocateHeaderT(header_only, (colors == 3) ? FIT_RGB16 : FIT_UINT16, width, height);
		} else {
			dib = FreeImage_AllocateHeaderT(header_only, FIT_BITMAP, width, height, 8 * colors);
		}
		if(!dib) {
			throw FI_MSG_ERROR_DIB_MEMORY;
		}
		if(header_only) {
			return dib;
		}

		// write data: pixel (x, y) is the source pixel LibRaw's flip_index(y, x) gives
		for(unsigned y = 0; y < height; y++) {
			BYTE *output = FreeImage_GetScanLine(dib, height - 1 - y);
			for(unsigned x = 0; x < width; x++) {
				unsigned row = (flip & 4) ? x : y;
				unsigned col = (flip & 4) ? y : x;
				if(flip & 2) {
					row = src_height - 1 - row;
				}
				if(flip & 1) {
					col = src_width - 1 - col;
				}
				const BYTE *input = image->data + ((size_t)row * src_width + col) * colors * bytes;
				if(bpp == 16) {
					const WORD *sample = (const WORD*)input;
					if(colors == 3) {
						FIRGB16 *pixel = (FIRGB16*)output + x;
						pixel->red   = sample[0];
						pixel->green = sample[1];
						pixel->blue  = sample[2];
					} else {
						((WORD*)output)[x] = sample[0];
					}
				} else if(colors == 3) {
					RGBTRIPLE *pixel = (RGBTRIPLE*)output + x;
					pixel->rgbtRed   = input[0];
					pixel->rgbtGreen = input[1];
					pixel->rgbtBlue  = input[2];
				} else {
					output[x] = input[0];
				}
			}
		}
		
		return dib;

	} catch(const char *text) {
		FreeImage_Unload(dib);
		FreeImage_OutputMessageProc(s_format_id, text);
		return NULL;
	}
}

/** 
Get the embedded JPEG preview image from RAW picture with included Exif Data. 
@param RawProcessor Libraw handle
@param flags JPEG load flags
@return Returns the loaded dib if successfull, returns NULL otherwise
*/
static FIBITMAP * 
libraw_LoadEmbeddedPreview(LibRaw *RawProcessor, int flags) {
	FIBITMAP *dib = NULL;
	libraw_processed_image_t *thumb_image = NULL;
	
	try {
		// unpack data
		if(RawProcessor->unpack_thumb() != LIBRAW_SUCCESS) {
			// run silently "LibRaw : failed to run unpack_thumb"
			return NULL;
		}

		// retrieve thumb image
		int error_code = 0;
		thumb_image = RawProcessor->dcraw_make_mem_thumb(&error_code);
		if(thumb_image) {
			if(thumb_image->type != LIBRAW_IMAGE_BITMAP) {
				// attach the binary data to a memory stream
				FIMEMORY *hmem = FreeImage_OpenMemory((BYTE*)thumb_image->data, (DWORD)thumb_image->data_size);
				// get the file type
				FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromMemory(hmem, 0);
				if(fif == FIF_JPEG) {
					// rotate according to Exif orientation
					flags |= JPEG_EXIFROTATE;
				}
				// load an image from the memory stream
				dib = FreeImage_LoadFromMemory(fif, hmem, flags);
				// close the stream
				FreeImage_CloseMemory(hmem);
			} else {
				// convert processed data to output dib
				dib = libraw_ConvertProcessedImageToDib(thumb_image, RawProcessor->imgdata.sizes.flip, (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS);
			}
		} else {
			throw "LibRaw : failed to run dcraw_make_mem_thumb";
		}

		// clean-up and return
		RawProcessor->dcraw_clear_mem(thumb_image);

		return dib;

	} catch(const char *text) {
		// clean-up and return
		if(thumb_image) {
			RawProcessor->dcraw_clear_mem(thumb_image);
		}
		if(text != NULL) {
			FreeImage_OutputMessageProc(s_format_id, text);
		}
	}

	return NULL;
}

/**
The 16-bit output is linear light (gamm 1/1, set below) in LibRaw's default sRGB primaries: the CICP tag and a linear profile say so
*/
static FIBITMAP *
DescribeOutput(FIBITMAP *dib, int bitspersample) {
	if(dib && (bitspersample == 16)) {
		const BOOL color = (FreeImage_GetImageType(dib) == FIT_RGB16) ? TRUE : FALSE;
		SetCICPMetadata(dib, color ? 1 : 2, 8, 0, TRUE);
		DWORD size = 0;
		const void *profile = FreeImage_GetBuiltInICCProfile(color ? FICMS_PROFILE_LINEAR_SRGB : FICMS_PROFILE_LINEAR_GRAY, &size);
		if(profile) {
			FreeImage_CreateICCProfile(dib, (void*)profile, (long)size);
		}
	}
	return dib;
}

/**
Load raw data and convert to FIBITMAP
@param RawProcessor Libraw handle
@param bitspersample Output bitdepth (8- or 16-bit)
@param header_only TRUE to allocate the header only
@return Returns the loaded dib if successfull, returns NULL otherwise
*/
static FIBITMAP * 
libraw_LoadRawData(LibRaw *RawProcessor, int bitspersample, BOOL header_only) {
	FIBITMAP *dib = NULL;

	try {
		// set decoding parameters
		// -----------------------
		
		// (-6) 16-bit or 8-bit
		RawProcessor->imgdata.params.output_bps = bitspersample;
		// (-g power toe_slope)
		if(bitspersample == 16) {
			// set -g 1 1 for linear curve
			RawProcessor->imgdata.params.gamm[0] = 1;
			RawProcessor->imgdata.params.gamm[1] = 1;
		} else if(bitspersample == 8) {
			// by default settings for rec. BT.709 are used: power 2.222 (i.e. gamm[0]=1/2.222) and slope 4.5
			RawProcessor->imgdata.params.gamm[0] = 1/2.222;
			RawProcessor->imgdata.params.gamm[1] = 4.5;
		}
		// (-W) Don't use automatic increase of brightness by histogram
		RawProcessor->imgdata.params.no_auto_bright = 1;
		// (-q 3) Adaptive homogeneity-directed demosaicing algorithm (AHD)
		RawProcessor->imgdata.params.user_qual = 3;

		// -----------------------

		if(header_only) {
			// the size dcraw_process() gives the image: half size, Fuji rotation, pixel aspect and flip
			if(RawProcessor->adjust_sizes_info_only() != LIBRAW_SUCCESS) {
				throw "LibRaw : failed to compute the output size";
			}
			const unsigned width = RawProcessor->imgdata.sizes.iwidth;
			const unsigned height = RawProcessor->imgdata.sizes.iheight;
			// a monochrome sensor stays one color
			const int colors = (RawProcessor->imgdata.idata.colors == 1) ? 1 : 3;
			if(bitspersample == 16) {
				dib = FreeImage_AllocateHeaderT(TRUE, (colors == 3) ? FIT_RGB16 : FIT_UINT16, width, height);
			} else {
				dib = FreeImage_AllocateHeaderT(TRUE, FIT_BITMAP, width, height, 8 * colors);
			}
			if(!dib) {
				throw FI_MSG_ERROR_DIB_MEMORY;
			}
			return DescribeOutput(dib, bitspersample);
		}

		// unpack data
		if(RawProcessor->unpack() != LIBRAW_SUCCESS) {
			throw "LibRaw : failed to unpack data";
		}

		// process data (... most consuming task ...)
		if(RawProcessor->dcraw_process() != LIBRAW_SUCCESS) {
			throw "LibRaw : failed to process data";
		}

		// retrieve processed image
		dib = libraw_ConvertProcessedRawToDib(RawProcessor);
	
		return DescribeOutput(dib, bitspersample);

	} catch(const char *text) {
		FreeImage_OutputMessageProc(s_format_id, text);
		return NULL;
	}
}

/**
Load the Bayer matrix (unprocessed raw data) as a FIT_UINT16 image. 
Note that some formats don't have a Bayer matrix (e.g. Foveon, Canon sRAW, demosaiced DNG files). 
@param RawProcessor Libraw handle
@param header_only TRUE to allocate the header only
@return Returns the loaded dib if successfull, returns NULL otherwise
*/
static FIBITMAP * 
libraw_LoadUnprocessedData(LibRaw *RawProcessor, BOOL header_only) {
	FIBITMAP *dib = NULL;

	try {
		// unpack data
		if(!header_only && (RawProcessor->unpack() != LIBRAW_SUCCESS)) {
			throw "LibRaw : failed to unpack data";
		}

		// check for a supported Bayer format
		if(!(RawProcessor->imgdata.idata.filters || RawProcessor->imgdata.idata.colors == 1)) {
			throw "LibRaw : only Bayer-pattern RAW files are supported";
		}

		// allocate output dib
		const unsigned width = RawProcessor->imgdata.sizes.raw_width;
		const unsigned height = RawProcessor->imgdata.sizes.raw_height;
		const size_t line_size = width * sizeof(WORD);
		const WORD *src_bits = (WORD*)RawProcessor->imgdata.rawdata.raw_image;

		// the raw image is not read before unpack()
		if(src_bits || header_only) {
			dib = FreeImage_AllocateHeaderT(header_only, FIT_UINT16, width, height);
		}
		if(!dib) {
			throw FI_MSG_ERROR_DIB_MEMORY;
		}

		// retrieve the raw image
		if(!header_only) {
			for(unsigned y = 0; y < height; y++) {
				WORD *dst_bits = (WORD*)FreeImage_GetScanLine(dib, height - 1 - y);
				memcpy(dst_bits, src_bits, line_size);
				src_bits += width;
			}
		}

		// store metadata needed for post-processing
		{
			char value[512];

			const libraw_image_sizes_t *sizes = &RawProcessor->imgdata.sizes;

			// image output width & height
			{
				sprintf(value, "%d", sizes->iwidth);
				FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Raw.Output.Width", value);
				
				sprintf(value, "%d", sizes->iheight);
				FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Raw.Output.Height", value);
			}

			// image output frame
			{
				const unsigned f_left = sizes->left_margin;
				const unsigned f_top = sizes->top_margin;
				const unsigned f_width = sizes->width;
				const unsigned f_height = sizes->height;
				
				sprintf(value, "%d", f_left);
				FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Raw.Frame.Left", value);

				sprintf(value, "%d", f_top);
				FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Raw.Frame.Top", value);

				sprintf(value, "%d", f_width);
				FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Raw.Frame.Width", value);

				sprintf(value, "%d", f_height);
				FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Raw.Frame.Height", value);
			}

			// Bayer pattern
			// Mask describing the order of color pixels in the matrix. 
			// This field describe 16 pixels (8 rows with two pixels in each, from left to right and from top to bottom). 

			if(RawProcessor->imgdata.idata.filters) {
				// description of colors numbered from 0 to 3 (RGBG,RGBE,GMCY, or GBTG)
				char *cdesc = RawProcessor->imgdata.idata.cdesc;
				if(!cdesc[3]) {
					cdesc[3] = 'G';
				}
				char *pattern = &value[0];
				for(int i = 0; i < 16; i++) {
					pattern[i] = cdesc[ RawProcessor->fcol(i >> 1, i & 1) ];
				}
				pattern[16] = 0;

				FreeImage_SetMetadataKeyValue(FIMD_COMMENTS, dib, "Raw.BayerPattern", value);
			}
		}
	
		return dib;

	} catch(const char *text) {
		FreeImage_Unload(dib);
		FreeImage_OutputMessageProc(s_format_id, text);
		return NULL;
	}
}

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "RAW";
}

static const char * DLL_CALLCONV
Description() {
	return "RAW camera image";
}

static const char * DLL_CALLCONV
Extension() {
	/**
	Below are known RAW file extensions that you can check using FreeImage_GetFIFFromFormat. 
	If a file extension is not listed, that doesn't mean that you cannot load it. 
	Using FreeImage_GetFileType is the best way to know if a RAW file format is supported. 
	*/
	static const char *raw_extensions = 
		"3fr,"   // Hasselblad Digital Camera Raw Image Format.
		"arw,"   // Sony Digital Camera Raw Image Format for Alpha devices.
		"bay,"   // Casio Digital Camera Raw File Format.
		"bmq,"   // NuCore Raw Image File.
		"cap,"   // Phase One Digital Camera Raw Image Format.
		"cine,"  // Phantom Software Raw Image File.
		"cr2,"   // Canon Digital Camera RAW Image Format version 2.0. These images are based on the TIFF image standard.
		"crw,"   // Canon Digital Camera RAW Image Format version 1.0. 
		"cs1,"   // Sinar Capture Shop Raw Image File.
		"dc2,"   // Kodak DC25 Digital Camera File.
		"dcr,"   // Kodak Digital Camera Raw Image Format for these models: Kodak DSC Pro SLR/c, Kodak DSC Pro SLR/n, Kodak DSC Pro 14N, Kodak DSC PRO 14nx.
		"drf,"   // Kodak Digital Camera Raw Image Format.
		"dsc,"   // Kodak Digital Camera Raw Image Format.
		"dng,"   // Adobe Digital Negative: DNG is publicly available archival format for the raw files generated by digital cameras. By addressing the lack of an open standard for the raw files created by individual camera models, DNG helps ensure that photographers will be able to access their files in the future. 
		"erf,"   // Epson Digital Camera Raw Image Format.
		"fff,"   // Imacon Digital Camera Raw Image Format.
		"ia,"    // Sinar Raw Image File.
		"iiq,"   // Phase One Digital Camera Raw Image Format.
		"k25,"   // Kodak DC25 Digital Camera Raw Image Format.
		"kc2,"   // Kodak DCS200 Digital Camera Raw Image Format.
		"kdc,"   // Kodak Digital Camera Raw Image Format.
		"mdc,"   // Minolta RD175 Digital Camera Raw Image Format.
		"mef,"   // Mamiya Digital Camera Raw Image Format.
		"mos,"   // Leaf Raw Image File.
		"mrw,"   // Minolta Dimage Digital Camera Raw Image Format.
		"nef,"   // Nikon Digital Camera Raw Image Format.
		"nrw,"   // Nikon Digital Camera Raw Image Format.
		"orf,"   // Olympus Digital Camera Raw Image Format.
		"pef,"   // Pentax Digital Camera Raw Image Format.
		"ptx,"   // Pentax Digital Camera Raw Image Format.
		"pxn,"   // Logitech Digital Camera Raw Image Format.
		"qtk,"   // Apple Quicktake 100/150 Digital Camera Raw Image Format.
		"raf,"   // Fuji Digital Camera Raw Image Format.
		"raw,"   // Panasonic Digital Camera Image Format.
		"rdc,"   // Digital Foto Maker Raw Image File.
		"rw2,"   // Panasonic LX3 Digital Camera Raw Image Format.
		"rwl,"	 // Leica Camera Raw Image Format.
		"rwz,"   // Rawzor Digital Camera Raw Image Format.
		"sr2,"   // Sony Digital Camera Raw Image Format.
		"srf,"   // Sony Digital Camera Raw Image Format for DSC-F828 8 megapixel digital camera or Sony DSC-R1.
		"srw,"   // Samsung Raw Image Format.
		"sti,"   // Sinar Capture Shop Raw Image File.
		"x3f";   // Sigma Digital Camera Raw Image Format for devices based on Foveon X3 direct image sensor.
	return raw_extensions;
}

static const char * DLL_CALLCONV
RegExpr() {
	return NULL;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/x-dcraw";
}

static BOOL 
HasMagicHeader(FreeImageIO *io, fi_handle handle) {
	const unsigned signature_size = 32;
	BYTE signature[signature_size] = { 0 };
	/*
	note: classic TIFF signature is
	{ 0x49, 0x49, 0x2A, 0x00 } Classic TIFF, little-endian
	{ 0x4D, 0x4D, 0x00, 0x2A } Classic TIFF, big-endian
	*/
	// Canon (CR2), little-endian byte order
	static const BYTE CR2_II[] = { 0x49, 0x49, 0x2A, 0x00, 0x10, 0x00, 0x00, 0x00, 0x43, 0x52, 0x02, 0x00 };
	// Canon (CRW), little-endian byte order
	static const BYTE CRW_II[] = { 0x49, 0x49, 0x1A, 0x00, 0x00, 0x00, 0x48, 0x45, 0x41, 0x50, 0x43, 0x43, 0x44, 0x52, 0x02, 0x00 };
	// Minolta (MRW)
	static const BYTE MRW[] = { 0x00, 0x4D, 0x52, 0x4D, 0x00 };
	// Olympus (ORF), little-endian byte order
	static const BYTE ORF_IIRS[] = { 0x49, 0x49, 0x52, 0x53, 0x08, 0x00, 0x00, 0x00 };
	static const BYTE ORF_IIRO[] = { 0x49, 0x49, 0x52, 0x4F, 0x08, 0x00, 0x00, 0x00 };
	// Olympus (ORF), big-endian byte order
	static 	const BYTE ORF_MMOR[] = { 0x4D, 0x4D, 0x4F, 0x52, 0x00, 0x00, 0x00, 0x08 };
	// Fujifilm (RAF)
	static const BYTE RAF[] = { 0x46, 0x55, 0x4A, 0x49, 0x46, 0x49, 0x4C, 0x4D, 0x43, 0x43, 0x44, 0x2D, 0x52, 0x41, 0x57, 0x20 };
	// Panasonic (RW2) or Leica (RWL), little-endian byte order
	static const BYTE RWx_II[] = { 0x49, 0x49, 0x55, 0x00, 0x18, 0x00, 0x00, 0x00, 0x88, 0xE7, 0x74, 0xD8, 0xF8, 0x25, 0x1D, 0x4D, 0x94, 0x7A, 0x6E, 0x77, 0x82, 0x2B, 0x5D, 0x6A };
	// Panasonic (RAW) or Leica (RAW), little-endian byte order
	static const BYTE RAW_II[] = { 0x49, 0x49, 0x55, 0x00, 0x08, 0x00, 0x00, 0x00, 0x22, 0x00, 0x01, 0x00, 0x07, 0x00, 0x04, 0x00, 0x00, 0x00 };
	// Foveon (X3F)
	static const BYTE X3F[] = { 0x46, 0x4F, 0x56, 0x62 };

	if(io->read_proc(signature, 1, signature_size, handle) != signature_size) {
		return FALSE;
	}
	if (memcmp(CR2_II, signature, 12) == 0) {
		return TRUE;
	}
	if (memcmp(CRW_II, signature, 16) == 0) {
		return TRUE;
	}
	if (memcmp(MRW, signature, 5) == 0) {
		return TRUE;
	}
	if (memcmp(ORF_IIRS, signature, 8) == 0) {
		return TRUE;
	}
	if (memcmp(ORF_IIRO, signature, 8) == 0) {
		return TRUE;
	}
	if (memcmp(ORF_MMOR, signature, 8) == 0) {
		return TRUE;
	}
	if (memcmp(RAF, signature, 16) == 0) {
		return TRUE;
	}
	if (memcmp(RWx_II, signature, 24) == 0) {
		return TRUE;
	}
	if (memcmp(RAW_II, signature, 18) == 0) {
		return TRUE;
	}
	if (memcmp(X3F, signature, 4) == 0) {
		return TRUE;
	}

	return FALSE;
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	// some RAW files have a magic signature (most of them have a TIFF signature)
	// try to check this in order to speed up the file identification
	{
		INT64 tell = io->tell_proc(handle);
		if( HasMagicHeader(io, handle) ) {
			return TRUE;
		} else {
			io->seek_proc(handle, tell, SEEK_SET);
		}
	}

	// no magic signature : we need to open the file (it will take more time to identify it)
	// do not declare RawProcessor on the stack as it may be huge (300 KB)
	{
		LibRaw *RawProcessor = new(std::nothrow) LibRaw;

		if(RawProcessor) {
			BOOL bSuccess = TRUE;

			RawProcessor->set_dataerror_handler(libraw_data_error_handler, NULL);

			// wrap the input datastream
			LibRaw_freeimage_datastream datastream(io, handle);

			// open the datastream
			if(RawProcessor->open_datastream(&datastream) != LIBRAW_SUCCESS) {
				bSuccess = FALSE;	// LibRaw : failed to open input stream (unknown format)
			}

			// clean-up internal memory allocations
			RawProcessor->recycle();
			delete RawProcessor;

			return bSuccess;
		}
	}

	return FALSE;
}

static BOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return FALSE;
}

static BOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return FALSE;
}

static BOOL DLL_CALLCONV
SupportsICCProfiles() {
	return TRUE;
}

static BOOL DLL_CALLCONV
SupportsNoPixels() {
	return TRUE;
}

// ----------------------------------------------------------

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	FIBITMAP *dib = NULL;
	LibRaw *RawProcessor = NULL;

	BOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

	try {
		// do not declare RawProcessor on the stack as it may be huge (300 KB)
		RawProcessor = new(std::nothrow) LibRaw;
		if(!RawProcessor) {
			throw FI_MSG_ERROR_MEMORY;
		}

		RawProcessor->set_dataerror_handler(libraw_data_error_handler, NULL);

		// wrap the input datastream
		LibRaw_freeimage_datastream datastream(io, handle);

		// set decoding parameters
		// the following parameters affect data reading
		// --------------------------------------------

		// (-s [0..N-1]) Select one raw image from input file
		RawProcessor->imgdata.rawparams.shot_select = 0;
		// (-w) Use camera white balance, if possible (otherwise, fallback to auto_wb)
		RawProcessor->imgdata.params.use_camera_wb = 1;
		// (-M) Use any color matrix from the camera metadata. This option only affects Olympus, Leaf, and Phase One cameras.
		RawProcessor->imgdata.params.use_camera_matrix = 1;
		// (-h) outputs the image in 50% size
		RawProcessor->imgdata.params.half_size = ((flags & RAW_HALFSIZE) == RAW_HALFSIZE) ? 1 : 0;

		// open the datastream
		if(RawProcessor->open_datastream(&datastream) != LIBRAW_SUCCESS) {
			throw "LibRaw : failed to open input stream (unknown format)";
		}

		// the file's profile describes its preview or the sensor's colors, never LibRaw's output
		BOOL file_profile = FALSE;

		// a header-only load takes its load's path: each sizes and turns the image its own way
		if((flags & RAW_UNPROCESSED) == RAW_UNPROCESSED) {
			// load raw data without post-processing (i.e. as a Bayer matrix)
			dib = libraw_LoadUnprocessedData(RawProcessor, header_only);
			file_profile = TRUE;
		}
		else if((flags & RAW_PREVIEW) == RAW_PREVIEW) {
			// try to get the embedded JPEG
			dib = libraw_LoadEmbeddedPreview(RawProcessor, header_only ? FIF_LOAD_NOPIXELS : 0);
			file_profile = (dib != NULL);
			if(!dib) {
				// no JPEG preview: try to load as 8-bit/sample (i.e. RGB 24-bit)
				dib = libraw_LoadRawData(RawProcessor, 8, header_only);
			}
		} 
		else if((flags & RAW_DISPLAY) == RAW_DISPLAY) {
			// load raw data as 8-bit/sample (i.e. RGB 24-bit)
			dib = libraw_LoadRawData(RawProcessor, 8, header_only);
		} 
		else {
			// default: load raw data as linear 16-bit/sample (i.e. RGB 48-bit)
			dib = libraw_LoadRawData(RawProcessor, 16, header_only);
		}

		// a JPEG preview's own profile describes it better
		if(dib && file_profile && (NULL != RawProcessor->imgdata.color.profile) && !FreeImage_GetICCProfile(dib)->data) {
			FreeImage_CreateICCProfile(dib, RawProcessor->imgdata.color.profile, RawProcessor->imgdata.color.profile_length);
		}

		// try to get JPEG embedded Exif metadata
		if(dib && !((flags & RAW_PREVIEW) == RAW_PREVIEW)) {
			FIBITMAP *metadata_dib = libraw_LoadEmbeddedPreview(RawProcessor, FIF_LOAD_NOPIXELS);
			if(metadata_dib) {
				FreeImage_CloneMetadata(dib, metadata_dib);
				FreeImage_Unload(metadata_dib);
			}
		}

		// clean-up internal memory allocations
		RawProcessor->recycle();
		delete RawProcessor;

		return dib;

	} catch(const char *text) {
		if(RawProcessor) {
			RawProcessor->recycle();
			delete RawProcessor;
		}
		if(dib) {
			FreeImage_Unload(dib);
		}
		FreeImage_OutputMessageProc(s_format_id, text);
	}

	return NULL;
}

// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitRAW(Plugin *plugin, int format_id) {
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
	plugin->save_proc = NULL;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = SupportsICCProfiles;
	plugin->supports_no_pixels_proc = SupportsNoPixels;
}
