// ==========================================================
// JPEG XR Loader & Writer
//
// Design and implementation by
// - Herve Drolon (drolon@infonie.fr)
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
#include "../Metadata/FreeImageTag.h"

#include "../LibJXR/jxrgluelib/JXRGlue.h"

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ==========================================================
// FreeImageIO interface (I/O streaming functions)
// ==========================================================

/**
JXR wrapper for FreeImage I/O handle
*/
typedef struct tagFreeImageJXRIO {
    FreeImageIO *io;
	fi_handle handle;
	size_t pos;			//! own position, from start
	INT64 start;		//! handle position of the JXR data, which jxrlib requires to be its position 0
} FreeImageJXRIO;

static ERR
_jxr_io_Read(WMPStream* pWS, void* pv, size_t cb) {
	FreeImageJXRIO *fio = (FreeImageJXRIO*)pWS->state.pvObj;
	if(FreeImage_ReadBytes(fio->io, fio->handle, pv, cb) != cb) {
		return WMP_errFileIO;
	}
	fio->pos += cb;
	return WMP_errSuccess;
}

static ERR
_jxr_io_Write(WMPStream* pWS, const void* pv, size_t cb) {
	FreeImageJXRIO *fio = (FreeImageJXRIO*)pWS->state.pvObj;
	if(0 != cb) {
		if(FreeImage_WriteBytes(fio->io, fio->handle, pv, cb) != cb) {
			return WMP_errFileIO;
		}
		fio->pos += cb;
		return WMP_errSuccess;
	}
	return WMP_errFileIO;
}

static ERR
_jxr_io_SetPos(WMPStream* pWS, size_t offPos) {
	FreeImageJXRIO *fio = (FreeImageJXRIO*)pWS->state.pvObj;
	if((UINT64)offPos > (UINT64)((std::numeric_limits<INT64>::max)() - fio->start)) {
		return WMP_errFileIO;
	}
	if(fio->io->seek_proc(fio->handle, fio->start + (INT64)offPos, SEEK_SET) != 0) {
		return WMP_errFileIO;
	}
	fio->pos = offPos;
	return WMP_errSuccess;
}

static ERR
_jxr_io_GetPos(WMPStream* pWS, size_t* poffPos) {
	FreeImageJXRIO *fio = (FreeImageJXRIO*)pWS->state.pvObj;
	*poffPos = fio->pos;
	return WMP_errSuccess;
}

static Bool
_jxr_io_EOS(WMPStream* pWS) {
	FreeImageJXRIO *fio = (FreeImageJXRIO*)pWS->state.pvObj;
	const size_t currentPos = fio->pos;
	BYTE byte = 0;
	// probe a byte
	const Bool bDataRemaining = (fio->io->read_proc(&byte, 1, 1, fio->handle) == 1) ? TRUE : FALSE;
	_jxr_io_SetPos(pWS, currentPos);
	return bDataRemaining;
}

/**
Bytes from the start of the codestream to the end of the stream, or the most there can be if the end is unknown
*/
static UINT64
CodestreamBytes(PKImageDecode *pDecoder) {
	WMPStream *pWS = pDecoder->pStream;
	FreeImageJXRIO *fio = (FreeImageJXRIO*)pWS->state.pvObj;
	const size_t currentPos = fio->pos;
	UINT64 bytes = (std::numeric_limits<UINT64>::max)();
	if(fio->io->seek_proc(fio->handle, 0, SEEK_END) == 0) {
		const INT64 end = fio->io->tell_proc(fio->handle);
		const UINT64 from = (UINT64)fio->start + pDecoder->WMP.wmiDEMisc.uImageOffset;
		if(end >= 0) {
			bytes = ((UINT64)end > from) ? (UINT64)end - from : 0;
		}
	}
	_jxr_io_SetPos(pWS, currentPos);
	return bytes;
}

static ERR 
_jxr_io_Close(WMPStream** ppWS) {
	WMPStream *pWS = *ppWS;
	// HACK : we use fMem to avoid a stream destruction by the library
	// because FreeImage MUST HAVE the ownership of the stream
	// see _jxr_io_Create
	if(pWS && pWS->fMem) {
		free(pWS);
		*ppWS = NULL;
	}
	return WMP_errSuccess;
}

static ERR 
_jxr_io_Create(WMPStream **ppWS, FreeImageJXRIO *jxr_io) {
	*ppWS = (WMPStream*)calloc(1, sizeof(**ppWS));
	if(*ppWS) {
		WMPStream *pWS = *ppWS;

		pWS->state.pvObj = jxr_io;
		pWS->Close = _jxr_io_Close;
		pWS->EOS = _jxr_io_EOS;
		pWS->Read = _jxr_io_Read;
		pWS->Write = _jxr_io_Write;
		pWS->SetPos = _jxr_io_SetPos;
		pWS->GetPos = _jxr_io_GetPos;

		// HACK : we use fMem to avoid a stream destruction by the library
		// because FreeImage MUST HAVE the ownership of the stream
		// see _jxr_io_Close
		pWS->fMem = FALSE;

		return WMP_errSuccess;
	}
	return WMP_errOutOfMemory;
}

// ==========================================================
// JPEG XR Error handling
// ==========================================================

static const char* 
JXR_ErrorMessage(const int error) {
	switch(error) {
		case WMP_errNotYetImplemented:
		case WMP_errAbstractMethod:
			return "Not yet implemented";
		case WMP_errOutOfMemory:
			return "Out of memory";
		case WMP_errFileIO:
			return "File I/O error";
		case WMP_errBufferOverflow:
			return "Buffer overflow";
		case WMP_errInvalidParameter:
			return "Invalid parameter";
		case WMP_errInvalidArgument:
			return "Invalid argument";
		case WMP_errUnsupportedFormat:
			return "Unsupported format";
		case WMP_errIncorrectCodecVersion:
			return "Incorrect codec version";
		case WMP_errIndexNotFound:
			return "Format converter: Index not found";
		case WMP_errOutOfSequence:
			return "Metadata: Out of sequence";
		case WMP_errMustBeMultipleOf16LinesUntilLastCall:
			return "Must be multiple of 16 lines until last call";
		case WMP_errPlanarAlphaBandedEncRequiresTempFile:
			return "Planar alpha banded encoder requires temp files";
		case WMP_errAlphaModeCannotBeTranscoded:
			return "Alpha mode cannot be transcoded";
		case WMP_errIncorrectCodecSubVersion:
			return "Incorrect codec subversion";
		case WMP_errFail:
		case WMP_errNotInitialized:
		default:
			return "Invalid instruction - please contact the FreeImage team";
	}
}

// ==========================================================
// Helper functions & macro
// ==========================================================

#define JXR_CHECK(error_code) \
	if(error_code < 0) { \
		const char *error_message = JXR_ErrorMessage(error_code); \
		throw error_message; \
	}

// --------------------------------------------------------------------------

/**
Input conversions natively understood by FreeImage
@see GetNativePixelFormat
*/
typedef struct tagJXRInputConversion {
	BITDEPTH_BITS bdBitDepth;
	U32 cbitUnit;
	FREE_IMAGE_TYPE image_type;
	unsigned red_mask;
	unsigned green_mask;
	unsigned blue_mask;
} JXRInputConversion;

/**
Conversion table for native FreeImage formats
@see GetNativePixelFormat
*/
static JXRInputConversion s_FreeImagePixelInfo[] = {
	// 1-bit bitmap
	{ BD_1, 1, FIT_BITMAP, 0, 0, 0 },
	// 8-, 24-, 32-bit bitmap
	{ BD_8, 8, FIT_BITMAP, 0, 0, 0 },
	{ BD_8, 24, FIT_BITMAP, 0, 0, 0 },
	{ BD_8, 32, FIT_BITMAP, 0, 0, 0 },
	// 16-bit RGB 565
	{ BD_565, 16, FIT_BITMAP, FI16_565_RED_MASK, FI16_565_GREEN_MASK, FI16_565_BLUE_MASK },
	// 16-bit RGB 555
	{ BD_5, 16, FIT_BITMAP, FI16_555_RED_MASK, FI16_555_GREEN_MASK, FI16_555_BLUE_MASK },
	// 16-bit greyscale, RGB16, RGBA16 bitmap
	{ BD_16, 16, FIT_UINT16, 0, 0, 0 },
	{ BD_16, 48, FIT_RGB16, 0, 0, 0 },
	{ BD_16, 64, FIT_RGBA16, 0, 0, 0 },
	// 32-bit float, RGBF, RGBAF bitmap
	{ BD_32F, 32, FIT_FLOAT, 0, 0, 0 },
	{ BD_32F, 96, FIT_RGBF, 0, 0, 0 },
	{ BD_32F, 128, FIT_RGBAF, 0, 0, 0 }
};

/**
What is done to decoded rows, in place, before they reach the dib
@see s_FreeImageRowLayouts
*/
typedef enum {
	ROWS_AS_DECODED = 0,
	ROWS_PADDED_8,			// RGB and a padding byte: the padding dropped
	ROWS_CMYK_8,			// CMYK, then any further channels: BGR, as ConvertCMYKtoRGBA
	ROWS_CMYK_16,			// CMYK: RGB16
	ROWS_CMYKA_8,			// CMYK, any further channels, alpha last: BGRA
	ROWS_CMYKA_16,			// CMYK and alpha: RGBA16
	ROWS_CMYK_KEPT_8,		// CMYK with JXR_CMYK: further channels and alpha dropped
	ROWS_CMYK_KEPT_16,
	ROWS_PREMULTIPLIED_8,	// premultiplied alpha undone
	ROWS_PREMULTIPLIED_16,
	ROWS_PREMULTIPLIED_FLOAT
} JXRRowLayout;

/** CMYK converted to RGB, or kept as CMYK with JXR_CMYK */
typedef enum {
	CMYK_NONE = 0,
	CMYK_CONVERTED,
	CMYK_KEPT
} JXRCMYKUse;

/**
Pixel formats decoded as they are and then given a layout of FreeImage's
@see GetNativePixelFormat
*/
static const struct {
	const PKPixelFormatGUID *in_guid_format;
	const PKPixelFormatGUID *out_guid_format;	// the rows' layout once done
	FREE_IMAGE_TYPE image_type;
	unsigned bpp;
	JXRRowLayout layout;
	JXRCMYKUse cmyk;
} s_FreeImageRowLayouts[] = {
	{ &GUID_PKPixelFormat32bppBGR, &GUID_PKPixelFormat24bppBGR, FIT_BITMAP, 24, ROWS_PADDED_8, CMYK_NONE },
	{ &GUID_PKPixelFormat32bppRGB, &GUID_PKPixelFormat24bppRGB, FIT_BITMAP, 24, ROWS_PADDED_8, CMYK_NONE },
	{ &GUID_PKPixelFormat32bppCMYK, &GUID_PKPixelFormat24bppBGR, FIT_BITMAP, 24, ROWS_CMYK_8, CMYK_CONVERTED },
	{ &GUID_PKPixelFormat64bppCMYK, &GUID_PKPixelFormat48bppRGB, FIT_RGB16, 48, ROWS_CMYK_16, CMYK_CONVERTED },
	{ &GUID_PKPixelFormat40bppCMYKAlpha, &GUID_PKPixelFormat32bppBGRA, FIT_BITMAP, 32, ROWS_CMYKA_8, CMYK_CONVERTED },
	{ &GUID_PKPixelFormat80bppCMYKAlpha, &GUID_PKPixelFormat64bppRGBA, FIT_RGBA16, 64, ROWS_CMYKA_16, CMYK_CONVERTED },
	{ &GUID_PKPixelFormat32bppCMYK, &GUID_PKPixelFormat32bppCMYK, FIT_BITMAP, 32, ROWS_CMYK_KEPT_8, CMYK_KEPT },
	{ &GUID_PKPixelFormat64bppCMYK, &GUID_PKPixelFormat64bppCMYK, FIT_RGBA16, 64, ROWS_CMYK_KEPT_16, CMYK_KEPT },
	{ &GUID_PKPixelFormat40bppCMYKAlpha, &GUID_PKPixelFormat32bppCMYK, FIT_BITMAP, 32, ROWS_CMYK_KEPT_8, CMYK_KEPT },
	{ &GUID_PKPixelFormat80bppCMYKAlpha, &GUID_PKPixelFormat64bppCMYK, FIT_RGBA16, 64, ROWS_CMYK_KEPT_16, CMYK_KEPT },
	{ &GUID_PKPixelFormat32bppPBGRA, &GUID_PKPixelFormat32bppBGRA, FIT_BITMAP, 32, ROWS_PREMULTIPLIED_8, CMYK_NONE },
	{ &GUID_PKPixelFormat32bppPRGBA, &GUID_PKPixelFormat32bppRGBA, FIT_BITMAP, 32, ROWS_PREMULTIPLIED_8, CMYK_NONE },
	{ &GUID_PKPixelFormat64bppPRGBA, &GUID_PKPixelFormat64bppRGBA, FIT_RGBA16, 64, ROWS_PREMULTIPLIED_16, CMYK_NONE },
	{ &GUID_PKPixelFormat128bppPRGBAFloat, &GUID_PKPixelFormat128bppRGBAFloat, FIT_RGBAF, 128, ROWS_PREMULTIPLIED_FLOAT, CMYK_NONE }
};

/**
Get the pixel format the rows of an n-channel format stand for: three channels are RGB, four or more CMYK followed by
channels to drop (T.832 A.7.18 and Annex C)
@param guid_format Decoded pixel format
@return Returns the RGB or CMYK format of the same bit depth and alpha, or guid_format for any other format
*/
static const PKPixelFormatGUID&
GetRowsFormat(const PKPixelFormatGUID &guid_format) {
	PKPixelInfo pixelInfo;
	pixelInfo.pGUIDPixFmt = &guid_format;
	if((PixelFormatLookup(&pixelInfo, LOOKUP_FORWARD) != WMP_errSuccess) || (pixelInfo.cfColorFormat != NCOMPONENT)) {
		return guid_format;
	}
	const BOOL alpha = (pixelInfo.grBit & PK_pixfmtHasAlpha) != 0;
	const BOOL deep = (pixelInfo.bdBitDepth == BD_16);
	if(pixelInfo.cChannel - (alpha ? 1 : 0) == 3) {
		return deep ? (alpha ? GUID_PKPixelFormat64bppRGBA : GUID_PKPixelFormat48bppRGB) : (alpha ? GUID_PKPixelFormat32bppRGBA : GUID_PKPixelFormat24bppRGB);
	}
	return deep ? (alpha ? GUID_PKPixelFormat80bppCMYKAlpha : GUID_PKPixelFormat64bppCMYK) : (alpha ? GUID_PKPixelFormat40bppCMYKAlpha : GUID_PKPixelFormat32bppCMYK);
}

/**
Find the entry of s_FreeImageRowLayouts for pixels decoded in a pixel format
@param in_guid_format Decoded pixel format
@param keep_cmyk TRUE with JXR_CMYK
@return Returns the entry's index, or -1 for a format FreeImage holds as it is, or jxrlib converts
*/
static int
FindRowLayout(const PKPixelFormatGUID &in_guid_format, BOOL keep_cmyk) {
	for(int i = 0; i < (int)(sizeof(s_FreeImageRowLayouts) / sizeof(*s_FreeImageRowLayouts)); i++) {
		const JXRCMYKUse cmyk = s_FreeImageRowLayouts[i].cmyk;
		if(IsEqualGUID(in_guid_format, *s_FreeImageRowLayouts[i].in_guid_format) && ((cmyk == CMYK_NONE) || (cmyk == (keep_cmyk ? CMYK_KEPT : CMYK_CONVERTED)))) {
			return i;
		}
	}
	return -1;
}

/**
Get what is done to rows decoded in one pixel format to give them the layout of another
@param in_guid_format Decoded pixel format
@param out_guid_format The dib's pixel format
@return Returns ROWS_AS_DECODED for a format FreeImage holds as it is, or jxrlib converts
*/
static JXRRowLayout
GetRowLayout(const PKPixelFormatGUID &in_guid_format, const PKPixelFormatGUID &out_guid_format) {
	for(size_t i = 0; i < sizeof(s_FreeImageRowLayouts) / sizeof(*s_FreeImageRowLayouts); i++) {
		if(IsEqualGUID(in_guid_format, *s_FreeImageRowLayouts[i].in_guid_format) && IsEqualGUID(out_guid_format, *s_FreeImageRowLayouts[i].out_guid_format)) {
			return s_FreeImageRowLayouts[i].layout;
		}
	}
	return ROWS_AS_DECODED;
}

/**
Scan input pixelInfo specifications and return the equivalent FreeImage info for loading
@param pixelInfo Image specifications
@param keep_cmyk TRUE with JXR_CMYK
@param out_guid_format (returned value) output pixel format
@param image_type (returned value) Image type
@param bpp (returned value) Image bit depth
@param red_mask (returned value) RGB mask
@param green_mask (returned value) RGB mask
@param blue_mask (returned value) RGB mask
@return Returns WMP_errSuccess if successful, returns WMP_errFail otherwise
@see GetInputPixelFormat
*/
static ERR
GetNativePixelFormat(const PKPixelInfo *pixelInfo, BOOL keep_cmyk, PKPixelFormatGUID *out_guid_format, FREE_IMAGE_TYPE *image_type, unsigned *bpp, unsigned *red_mask, unsigned *green_mask, unsigned *blue_mask) {
	const unsigned s_FreeImagePixelInfoSize = (unsigned)sizeof(s_FreeImagePixelInfo) / sizeof(*(s_FreeImagePixelInfo));
	const PKPixelFormatGUID &guid_format = *pixelInfo->pGUIDPixFmt;

	// shared exponents, or float RGB with padding: jxrlib converts them to RGBF
	if(IsEqualGUID(guid_format, GUID_PKPixelFormat32bppRGBE) || IsEqualGUID(guid_format, GUID_PKPixelFormat128bppRGBFloat)) {
		return WMP_errFail;
	}
	// YCC: only PTM_COLOR_INFO, which is not read, could say which matrix gives its RGB
	if((pixelInfo->cfColorFormat == YUV_444) || (pixelInfo->cfColorFormat == YUV_422) || (pixelInfo->cfColorFormat == YUV_420)) {
		return WMP_errFail;
	}
	const PKPixelFormatGUID &rows_format = GetRowsFormat(guid_format);
	if(!IsEqualGUID(rows_format, guid_format)) {
		PKPixelInfo rowsInfo;
		rowsInfo.pGUIDPixFmt = &rows_format;
		if(PixelFormatLookup(&rowsInfo, LOOKUP_FORWARD) != WMP_errSuccess) {
			return WMP_errFail;
		}
		return GetNativePixelFormat(&rowsInfo, keep_cmyk, out_guid_format, image_type, bpp, red_mask, green_mask, blue_mask);
	}
	const int layout = FindRowLayout(guid_format, keep_cmyk);
	if(layout >= 0) {
		*out_guid_format = *s_FreeImageRowLayouts[layout].out_guid_format;
		*image_type = s_FreeImageRowLayouts[layout].image_type;
		*bpp = s_FreeImageRowLayouts[layout].bpp;
		*red_mask = *green_mask = *blue_mask = 0;
		return WMP_errSuccess;
	}

	for(unsigned i = 0; i < s_FreeImagePixelInfoSize; i++) {
		if(pixelInfo->bdBitDepth == s_FreeImagePixelInfo[i].bdBitDepth) {
			if(pixelInfo->cbitUnit == s_FreeImagePixelInfo[i].cbitUnit) {
				// found ! now get dst image format specifications
				memcpy(out_guid_format, pixelInfo->pGUIDPixFmt, sizeof(PKPixelFormatGUID));
				*image_type = s_FreeImagePixelInfo[i].image_type;
				*bpp = s_FreeImagePixelInfo[i].cbitUnit;
				*red_mask	= s_FreeImagePixelInfo[i].red_mask;
				*green_mask	= s_FreeImagePixelInfo[i].green_mask;
				*blue_mask	= s_FreeImagePixelInfo[i].blue_mask;
				return WMP_errSuccess;
			}
		}
	}

	// not found : need pixel format conversion
	return WMP_errFail;
}

/**
Scan input file guid format and return the equivalent FreeImage info & target guid format for loading
@param pDecoder Decoder handle
@param keep_cmyk TRUE with JXR_CMYK
@param guid_format (returned value) Output pixel format
@param image_type (returned value) Image type
@param bpp (returned value) Image bit depth
@param red_mask (returned value) RGB mask
@param green_mask (returned value) RGB mask
@param blue_mask (returned value) RGB mask
@return Returns TRUE if successful, returns FALSE otherwise
*/
static ERR
GetInputPixelFormat(PKImageDecode *pDecoder, BOOL keep_cmyk, PKPixelFormatGUID *guid_format, FREE_IMAGE_TYPE *image_type, unsigned *bpp, unsigned *red_mask, unsigned *green_mask, unsigned *blue_mask) {
	ERR error_code = 0;		// error code as returned by the interface
	PKPixelInfo pixelInfo;	// image specifications

	try {		
		// get input file pixel format ...
		PKPixelFormatGUID pguidSourcePF;
		error_code = pDecoder->GetPixelFormat(pDecoder, &pguidSourcePF);
		JXR_CHECK(error_code);
		pixelInfo.pGUIDPixFmt = &pguidSourcePF;
		// ... check for a supported format and get the format specifications
		error_code = PixelFormatLookup(&pixelInfo, LOOKUP_FORWARD);
		JXR_CHECK(error_code);

		// search for an equivalent native FreeImage format
		error_code = GetNativePixelFormat(&pixelInfo, keep_cmyk, guid_format, image_type, bpp, red_mask, green_mask, blue_mask);

		if(error_code != WMP_errSuccess) {
			// try to find a suitable conversion function ...
			const PKPixelFormatGUID *ppguidTargetPF = NULL;	// target pixel format
			unsigned iIndex = 0;	// first available conversion function
			do {
				error_code = PKFormatConverter_EnumConversions(&pguidSourcePF, iIndex, &ppguidTargetPF);
				if(error_code == WMP_errSuccess) {
					// found a conversion function, is the converted format a native FreeImage format ?
					pixelInfo.pGUIDPixFmt = ppguidTargetPF;
					error_code = PixelFormatLookup(&pixelInfo, LOOKUP_FORWARD);
					JXR_CHECK(error_code);
					error_code = GetNativePixelFormat(&pixelInfo, keep_cmyk, guid_format, image_type, bpp, red_mask, green_mask, blue_mask);
					if(error_code == WMP_errSuccess) {
						break;
					}
				}
				// try next conversion function
				iIndex++;
			} while(error_code != WMP_errIndexNotFound);

		}

		return (error_code == WMP_errSuccess) ? WMP_errSuccess : WMP_errUnsupportedFormat;

	} catch(...) {
		return error_code;
	}
}

// --------------------------------------------------------------------------

/**
Scan input dib format and return the equivalent PKPixelFormatGUID format for saving
@param dib Image to be saved
@param guid_format (returned value) GUID format
@param bHasAlpha (returned value) TRUE if an alpha layer is present
@return Returns TRUE if successful, returns FALSE otherwise
*/
static ERR
GetOutputPixelFormat(FIBITMAP *dib, PKPixelFormatGUID *guid_format, BOOL *bHasAlpha) {
	const FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib);
	const unsigned bpp = FreeImage_GetBPP(dib);
	const FREE_IMAGE_COLOR_TYPE color_type = FreeImage_GetColorType(dib);

	*guid_format = GUID_PKPixelFormatDontCare;
	*bHasAlpha = FALSE;

	switch(image_type) {
		case FIT_BITMAP:	// standard image	: 1-, 4-, 8-, 16-, 24-, 32-bit
			switch(bpp) {
				case 1:
					// FIC_MINISWHITE is saved as BD1BLACK1
					if((color_type == FIC_MINISBLACK) || (color_type == FIC_MINISWHITE)) {
						*guid_format = GUID_PKPixelFormatBlackWhite;
					}
					break;
				case 8:
					// assume FIC_MINISBLACK
					if(color_type == FIC_MINISBLACK) {
						*guid_format = GUID_PKPixelFormat8bppGray;
					}
					break;
				case 16:
					if ((FreeImage_GetRedMask(dib) == FI16_565_RED_MASK) && (FreeImage_GetGreenMask(dib) == FI16_565_GREEN_MASK) && (FreeImage_GetBlueMask(dib) == FI16_565_BLUE_MASK)) {
						*guid_format = GUID_PKPixelFormat16bppRGB565;
					} else {
						// includes case where all the masks are 0
						*guid_format = GUID_PKPixelFormat16bppRGB555;
					}
					break;
#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_BGR
				case 24:
					*guid_format = GUID_PKPixelFormat24bppBGR;
					break;
				case 32:
					*guid_format = GUID_PKPixelFormat32bppBGRA;
					*bHasAlpha = TRUE;
					break;
#elif FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB
				case 24:
					*guid_format = GUID_PKPixelFormat24bppRGB;
					break;
				case 32:
					*guid_format = GUID_PKPixelFormat32bppRGBA;
					*bHasAlpha = TRUE;
					break;
#endif
				case 4:
				default:
					// not supported
					break;
			}
			break;
		case FIT_UINT16:	// array of unsigned short	: unsigned 16-bit
			*guid_format = GUID_PKPixelFormat16bppGray;
			break;
		case FIT_FLOAT:		// array of float			: 32-bit IEEE floating point
			*guid_format = GUID_PKPixelFormat32bppGrayFloat;
			break;
		case FIT_RGB16:		// 48-bit RGB image			: 3 x 16-bit
			*guid_format = GUID_PKPixelFormat48bppRGB;
			break;
		case FIT_RGBA16:	// 64-bit RGBA image		: 4 x 16-bit
			*guid_format = GUID_PKPixelFormat64bppRGBA;
			*bHasAlpha = TRUE;
			break;
		case FIT_RGBF:		// 96-bit RGB float image	: 3 x 32-bit IEEE floating point
			*guid_format = GUID_PKPixelFormat96bppRGBFloat;
			break;
		case FIT_RGBAF:		// 128-bit RGBA float image	: 4 x 32-bit IEEE floating point
			*guid_format = GUID_PKPixelFormat128bppRGBAFloat;
			*bHasAlpha = TRUE;
			break;

		case FIT_INT16:		// array of short			: signed 16-bit
		case FIT_UINT32:	// array of unsigned long	: unsigned 32-bit
		case FIT_INT32:		// array of long			: signed 32-bit
		case FIT_DOUBLE:	// array of double			: 64-bit IEEE floating point
		case FIT_COMPLEX:	// array of FICOMPLEX		: 2 x 64-bit IEEE floating point

		default:
			// unsupported format
			break;
	}

	return (*guid_format != GUID_PKPixelFormatDontCare) ? WMP_errSuccess : WMP_errUnsupportedFormat;
}

// ==========================================================
// Metadata loading
// ==========================================================

/**
Read a JPEG-XR IFD as a buffer
@see ReadMetadata
*/
static ERR
ReadProfile(WMPStream* pStream, unsigned cbByteCount, unsigned uOffset, BYTE **ppbProfile) {
	if(0 == cbByteCount) {
		// realloc(p, 0) may free p
		return WMP_errFileIO;
	}
	// (re-)allocate profile buffer
	BYTE *pbProfile = (BYTE*)realloc(*ppbProfile, cbByteCount);
	if(!pbProfile) {
		return WMP_errOutOfMemory;
	}
	// store at once: the caller frees *ppbProfile on error
	*ppbProfile = pbProfile;
	// read the profile
	if(WMP_errSuccess == pStream->SetPos(pStream, uOffset)) {
		if(WMP_errSuccess == pStream->Read(pStream, pbProfile, cbByteCount)) {
			return WMP_errSuccess;
		}
	}
	return WMP_errFileIO;
}

/**
Convert a DPKPROPVARIANT to a FITAG, then store the tag as FIMD_EXIF_MAIN
@see ReadDescriptiveMetadata
*/
static BOOL
ReadPropVariant(WORD tag_id, const DPKPROPVARIANT & varSrc, FIBITMAP *dib) {
	DWORD dwSize;

	if(varSrc.vt == DPKVT_EMPTY) {
		return FALSE;
	}
	// a string the decoder could not read
	if(((varSrc.vt == DPKVT_LPSTR) || (varSrc.vt == DPKVT_LPWSTR)) && !varSrc.VT.pszVal) {
		return FALSE;
	}

	// get the tag key
	TagLib& s = TagLib::instance();
	const char *key = s.getTagFieldName(TagLib::EXIF_MAIN, tag_id, NULL);
	if(!key) {
		return FALSE;
	}

	// create a tag
	FITAG *tag = FreeImage_CreateTag();
	if(tag) {
		// set tag ID
		FreeImage_SetTagID(tag, tag_id);
		// set tag type, count, length and value
		switch (varSrc.vt) {
			case DPKVT_LPSTR:
				FreeImage_SetTagType(tag, FIDT_ASCII);
				dwSize = (DWORD)strlen(varSrc.VT.pszVal) + 1;
				FreeImage_SetTagCount(tag, dwSize);
				FreeImage_SetTagLength(tag, dwSize);
				FreeImage_SetTagValue(tag, varSrc.VT.pszVal);
				break;
			
			case DPKVT_LPWSTR:
			{
				// UTF-16: wchar_t has 4 bytes outside Windows
				DWORD length = 0;
				while(varSrc.VT.pwszVal[length]) {
					length++;
				}
				FreeImage_SetTagType(tag, FIDT_UNDEFINED);
				dwSize = (DWORD)(sizeof(U16) * (length + 1)); // +1 for NULL term
				FreeImage_SetTagCount(tag, dwSize);
				FreeImage_SetTagLength(tag, dwSize);
				FreeImage_SetTagValue(tag, varSrc.VT.pwszVal);
				break;
			}
	            
			case DPKVT_UI2:
				FreeImage_SetTagType(tag, FIDT_SHORT);
				FreeImage_SetTagCount(tag, 1);
				FreeImage_SetTagLength(tag, 2);
				FreeImage_SetTagValue(tag, &varSrc.VT.uiVal);
				break;

			case DPKVT_UI4:
			{
				// PageNumber: the decoder packs its two SHORTs, the page and the page count
				const WORD pages[2] = { (WORD)(varSrc.VT.ulVal & 0xFFFF), (WORD)(varSrc.VT.ulVal >> 16) };
				FreeImage_SetTagType(tag, FIDT_SHORT);
				FreeImage_SetTagCount(tag, 2);
				FreeImage_SetTagLength(tag, 4);
				FreeImage_SetTagValue(tag, pages);
				break;
			}

			default:
				// type comes from the file: drop the tag, do not assert
				FreeImage_DeleteTag(tag);
				return FALSE;
		}
		// get the tag desctiption
		const char *description = s.getTagDescription(TagLib::EXIF_MAIN, tag_id);
		FreeImage_SetTagDescription(tag, description);

		// store the tag
		FreeImage_SetMetadata(FIMD_EXIF_MAIN, dib, key, tag);

		FreeImage_DeleteTag(tag);
	}
	return TRUE;
}

/**
Read JPEG-XR descriptive metadata and store as EXIF_MAIN metadata
@see ReadPropVariant
*/
static ERR
ReadDescriptiveMetadata(PKImageDecode *pID, FIBITMAP *dib) {
	// get Exif TIFF metadata
	const DESCRIPTIVEMETADATA *pDescMetadata = &pID->WMP.sDescMetadata;
	// convert metadata to FITAG and store into the EXIF_MAIN metadata model
	ReadPropVariant(WMP_tagImageDescription, pDescMetadata->pvarImageDescription, dib);
	ReadPropVariant(WMP_tagCameraMake, pDescMetadata->pvarCameraMake, dib);
	ReadPropVariant(WMP_tagCameraModel, pDescMetadata->pvarCameraModel, dib);
	ReadPropVariant(WMP_tagSoftware, pDescMetadata->pvarSoftware, dib);
	ReadPropVariant(WMP_tagDateTime, pDescMetadata->pvarDateTime, dib);
	ReadPropVariant(WMP_tagArtist, pDescMetadata->pvarArtist, dib);
	ReadPropVariant(WMP_tagCopyright, pDescMetadata->pvarCopyright, dib);
	ReadPropVariant(WMP_tagRatingStars, pDescMetadata->pvarRatingStars, dib);
	ReadPropVariant(WMP_tagRatingValue, pDescMetadata->pvarRatingValue, dib);
	ReadPropVariant(WMP_tagCaption, pDescMetadata->pvarCaption, dib);
	ReadPropVariant(WMP_tagDocumentName, pDescMetadata->pvarDocumentName, dib);
	ReadPropVariant(WMP_tagPageName, pDescMetadata->pvarPageName, dib);
	ReadPropVariant(WMP_tagPageNumber, pDescMetadata->pvarPageNumber, dib);
	ReadPropVariant(WMP_tagHostComputer, pDescMetadata->pvarHostComputer, dib);
	return WMP_errSuccess;
}

/**
Read ICC, XMP, Exif, Exif-GPS, IPTC, descriptive (i.e. Exif-TIFF) metadata
@see ReadProfile, ReadDescriptiveMetadata
*/
static ERR
ReadMetadata(PKImageDecode *pID, FIBITMAP *dib) {
	ERR error_code = 0;		// error code as returned by the interface
	size_t currentPos = 0;	// current stream position
	
	WMPStream *pStream = pID->pStream;
	WmpDEMisc *wmiDEMisc = &pID->WMP.wmiDEMisc;
	BYTE *pbProfile = NULL;

	try {
		// save current position
		error_code = pStream->GetPos(pStream, &currentPos);
		JXR_CHECK(error_code);

		// ICC profile
		if(0 != wmiDEMisc->uColorProfileByteCount) {
			unsigned cbByteCount = wmiDEMisc->uColorProfileByteCount;
			unsigned uOffset = wmiDEMisc->uColorProfileOffset;
			error_code = ReadProfile(pStream, cbByteCount, uOffset, &pbProfile);
			JXR_CHECK(error_code);
			FreeImage_CreateICCProfile(dib, pbProfile, cbByteCount);
		}

		// XMP metadata
		if(0 != wmiDEMisc->uXMPMetadataByteCount) {
			unsigned cbByteCount = wmiDEMisc->uXMPMetadataByteCount;
			unsigned uOffset = wmiDEMisc->uXMPMetadataOffset;
			error_code = ReadProfile(pStream, cbByteCount, uOffset, &pbProfile);
			JXR_CHECK(error_code);
			// store the tag as XMP
			FITAG *tag = FreeImage_CreateTag();
			if(tag) {
				FreeImage_SetTagLength(tag, cbByteCount);
				FreeImage_SetTagCount(tag, cbByteCount);
				FreeImage_SetTagType(tag, FIDT_ASCII);
				FreeImage_SetTagValue(tag, pbProfile);
				FreeImage_SetTagKey(tag, g_TagLib_XMPFieldName);
				FreeImage_SetMetadata(FIMD_XMP, dib, FreeImage_GetTagKey(tag), tag);
				FreeImage_DeleteTag(tag);
			}
		}

		// IPTC metadata
		if(0 != wmiDEMisc->uIPTCNAAMetadataByteCount) {
			unsigned cbByteCount = wmiDEMisc->uIPTCNAAMetadataByteCount;
			unsigned uOffset = wmiDEMisc->uIPTCNAAMetadataOffset;
			error_code = ReadProfile(pStream, cbByteCount, uOffset, &pbProfile);
			JXR_CHECK(error_code);
			// decode the IPTC profile
			read_iptc_profile(dib, pbProfile, cbByteCount);
		}

		// Exif metadata
		if(0 != wmiDEMisc->uEXIFMetadataByteCount) {
			unsigned cbByteCount = wmiDEMisc->uEXIFMetadataByteCount;
			unsigned uOffset = wmiDEMisc->uEXIFMetadataOffset;
			error_code = ReadProfile(pStream, cbByteCount, uOffset, &pbProfile);
			JXR_CHECK(error_code);
			// decode the Exif profile
			jpegxr_read_exif_profile(dib, pbProfile, cbByteCount, uOffset);
		}

		// Exif-GPS metadata
		if(0 != wmiDEMisc->uGPSInfoMetadataByteCount) {
			unsigned cbByteCount = wmiDEMisc->uGPSInfoMetadataByteCount;
			unsigned uOffset = wmiDEMisc->uGPSInfoMetadataOffset;
			error_code = ReadProfile(pStream, cbByteCount, uOffset, &pbProfile);
			JXR_CHECK(error_code);
			// decode the Exif-GPS profile
			jpegxr_read_exif_gps_profile(dib, pbProfile, cbByteCount, uOffset);
		}

		// free profile buffer
		free(pbProfile);
		pbProfile = NULL;

		// restore initial position
		error_code = pID->pStream->SetPos(pID->pStream, currentPos);
		JXR_CHECK(error_code);

		// as a LAST STEP, read descriptive metadata
		// these metadata overwrite possible identical Exif-TIFF metadata 
		// that could have been read inside the Exif IFD
		
		return ReadDescriptiveMetadata(pID, dib);

	} catch(...) {
		// free profile buffer
		free(pbProfile);
		pbProfile = NULL;
		if(currentPos) {
			// restore initial position
			pStream->SetPos(pStream, currentPos);
		}
		return error_code;
	}
}

// ==========================================================
// Metadata saving
// ==========================================================

/**
Convert a FITAG (coming from FIMD_EXIF_MAIN) to the DPKPROPVARIANT type the encoder takes for this tag, or leave it out.
No allocation is needed here, the function just copy pointers when needed. 
@see WriteDescriptiveMetadata
*/
static BOOL
WritePropVariant(FIBITMAP *dib, WORD tag_id, DPKVARTYPE vt, DPKPROPVARIANT & varDst) {
	FITAG *tag = NULL;

	TagLib& s = TagLib::instance();
	
	// clear output DPKPROPVARIANT
	varDst.vt = DPKVT_EMPTY;

	// given the tag id, get the tag key
	const char *key = s.getTagFieldName(TagLib::EXIF_MAIN, tag_id, NULL);
	// then, get the tag info
	if(!FreeImage_GetMetadata(FIMD_EXIF_MAIN, dib, key, &tag)) {
		return FALSE;
	}
	const FREE_IMAGE_MDTYPE type = FreeImage_GetTagType(tag);
	const DWORD count = FreeImage_GetTagCount(tag);
	const void *value = FreeImage_GetTagValue(tag);
	if(!value) {
		return FALSE;
	}

	// the encoder takes one type per tag: another asserts, or is written as garbage
	switch(vt) {
		case DPKVT_LPSTR:
			// FreeImage ends ASCII values with a NUL
			if(type == FIDT_ASCII) {
				varDst.vt = DPKVT_LPSTR;
				varDst.VT.pszVal = (char*)value;
			}
			break;
		case DPKVT_LPWSTR:
			// UTF-16, as Windows' XP tags hold it: only with its NUL
			if((type == FIDT_BYTE) || (type == FIDT_UNDEFINED)) {
				const U16 *text = (const U16*)value;
				for(DWORD i = 0; i < FreeImage_GetTagLength(tag) / sizeof(U16); i++) {
					if(text[i] == 0) {
						varDst.vt = DPKVT_LPWSTR;
						varDst.VT.pwszVal = (U16*)value;
						break;
					}
				}
			}
			break;
		case DPKVT_UI2:
			if((type == FIDT_SHORT) && count) {
				varDst.vt = DPKVT_UI2;
				varDst.VT.uiVal = *((const U16*)value);
			}
			break;
		case DPKVT_UI4:
			// PageNumber: the page and the page count, two SHORTs packed as the decoder reads them
			if((type == FIDT_SHORT) && count) {
				const U16 *pages = (const U16*)value;
				varDst.vt = DPKVT_UI4;
				varDst.VT.ulVal = pages[0] | ((count > 1) ? ((U32)pages[1] << 16) : 0);
			}
			break;
		default:
			break;
	}
	
	return TRUE;
}

/**
Write EXIF_MAIN metadata to JPEG-XR descriptive metadata
@see WritePropVariant
*/
static ERR
WriteDescriptiveMetadata(PKImageEncode *pIE, FIBITMAP *dib) {
	ERR error_code = 0;		// error code as returned by the interface
	DESCRIPTIVEMETADATA DescMetadata;

	// fill the DESCRIPTIVEMETADATA structure (use pointers to arrays when needed)
	WritePropVariant(dib, WMP_tagImageDescription, DPKVT_LPSTR, DescMetadata.pvarImageDescription);
	WritePropVariant(dib, WMP_tagCameraMake, DPKVT_LPSTR, DescMetadata.pvarCameraMake);
	WritePropVariant(dib, WMP_tagCameraModel, DPKVT_LPSTR, DescMetadata.pvarCameraModel);
	WritePropVariant(dib, WMP_tagSoftware, DPKVT_LPSTR, DescMetadata.pvarSoftware);
	WritePropVariant(dib, WMP_tagDateTime, DPKVT_LPSTR, DescMetadata.pvarDateTime);
	WritePropVariant(dib, WMP_tagArtist, DPKVT_LPSTR, DescMetadata.pvarArtist);
	WritePropVariant(dib, WMP_tagCopyright, DPKVT_LPSTR, DescMetadata.pvarCopyright);
	WritePropVariant(dib, WMP_tagRatingStars, DPKVT_UI2, DescMetadata.pvarRatingStars);
	WritePropVariant(dib, WMP_tagRatingValue, DPKVT_UI2, DescMetadata.pvarRatingValue);
	WritePropVariant(dib, WMP_tagCaption, DPKVT_LPWSTR, DescMetadata.pvarCaption);
	WritePropVariant(dib, WMP_tagDocumentName, DPKVT_LPSTR, DescMetadata.pvarDocumentName);
	WritePropVariant(dib, WMP_tagPageName, DPKVT_LPSTR, DescMetadata.pvarPageName);
	WritePropVariant(dib, WMP_tagPageNumber, DPKVT_UI4, DescMetadata.pvarPageNumber);
	WritePropVariant(dib, WMP_tagHostComputer, DPKVT_LPSTR, DescMetadata.pvarHostComputer);

	// copy the structure to the encoder
	error_code = pIE->SetDescriptiveMetadata(pIE, &DescMetadata);

	// no need to free anything here
	return error_code;
}

/**
Write ICC, XMP, Exif, Exif-GPS, IPTC, descriptive (i.e. Exif-TIFF) metadata
*/
static ERR
WriteMetadata(PKImageEncode *pIE, FIBITMAP *dib) {
	ERR error_code = 0;		// error code as returned by the interface
	BYTE *profile = NULL;
	unsigned profile_size = 0;
	
	try {
		// write ICC profile
		{
			FIICCPROFILE *iccProfile = FreeImage_GetICCProfile(dib);
			if(iccProfile->data) {
				error_code = pIE->SetColorContext(pIE, (U8*)iccProfile->data, iccProfile->size);
				JXR_CHECK(error_code);
			}
		}
		
		// write descriptive metadata
		if(FreeImage_GetMetadataCount(FIMD_EXIF_MAIN, dib)) {
			error_code = WriteDescriptiveMetadata(pIE, dib);
			JXR_CHECK(error_code);
		}

		// write IPTC metadata
		if(FreeImage_GetMetadataCount(FIMD_IPTC, dib)) {
			// create a binary profile
			if(write_iptc_profile(dib, &profile, &profile_size)) {
				// write the profile
				error_code = PKImageEncode_SetIPTCNAAMetadata_WMP(pIE, profile, profile_size);
				JXR_CHECK(error_code);
				// release profile
				free(profile);
				profile = NULL;
			}
		}

		// write XMP metadata
		{
			FITAG *tag_xmp = NULL;
			if(FreeImage_GetMetadata(FIMD_XMP, dib, g_TagLib_XMPFieldName, &tag_xmp)) {
				error_code = PKImageEncode_SetXMPMetadata_WMP(pIE, (BYTE*)FreeImage_GetTagValue(tag_xmp), FreeImage_GetTagLength(tag_xmp));
				JXR_CHECK(error_code);
			}
		}

		// write Exif metadata
		{
			if(tiff_get_ifd_profile(dib, FIMD_EXIF_EXIF, &profile, &profile_size)) {
				error_code = PKImageEncode_SetEXIFMetadata_WMP(pIE, profile, profile_size);
				JXR_CHECK(error_code);
				// release profile
				free(profile);
				profile = NULL;
			}
		}

		// write Exif GPS metadata
		{
			if(tiff_get_ifd_profile(dib, FIMD_EXIF_GPS, &profile, &profile_size)) {
				error_code = PKImageEncode_SetGPSInfoMetadata_WMP(pIE, profile, profile_size);
				JXR_CHECK(error_code);
				// release profile
				free(profile);
				profile = NULL;
			}
		}

		return WMP_errSuccess;

	} catch(...) {
		free(profile);
		profile = NULL;
		return error_code;
	}
}



// ==========================================================
// Quantization tables (Y, U, V, YHP, UHP, VHP), 
// optimized for PSNR
// ==========================================================

static const int DPK_QPS_420[11][6] = {      // for 8 bit only
    { 66, 65, 70, 72, 72, 77 },
    { 59, 58, 63, 64, 63, 68 },
    { 52, 51, 57, 56, 56, 61 },
    { 48, 48, 54, 51, 50, 55 },
    { 43, 44, 48, 46, 46, 49 },
    { 37, 37, 42, 38, 38, 43 },
    { 26, 28, 31, 27, 28, 31 },
    { 16, 17, 22, 16, 17, 21 },
    { 10, 11, 13, 10, 10, 13 },
    {  5,  5,  6,  5,  5,  6 },
    {  2,  2,  3,  2,  2,  2 }
};

static const int DPK_QPS_8[12][6] = {
    { 67, 79, 86, 72, 90, 98 },
    { 59, 74, 80, 64, 83, 89 },
    { 53, 68, 75, 57, 76, 83 },
    { 49, 64, 71, 53, 70, 77 },
    { 45, 60, 67, 48, 67, 74 },
    { 40, 56, 62, 42, 59, 66 },
    { 33, 49, 55, 35, 51, 58 },
    { 27, 44, 49, 28, 45, 50 },
    { 20, 36, 42, 20, 38, 44 },
    { 13, 27, 34, 13, 28, 34 },
    {  7, 17, 21,  8, 17, 21 }, // Photoshop 100%
    {  2,  5,  6,  2,  5,  6 }
};

static const int DPK_QPS_16[11][6] = {
    { 197, 203, 210, 202, 207, 213 },
    { 174, 188, 193, 180, 189, 196 },
    { 152, 167, 173, 156, 169, 174 },
    { 135, 152, 157, 137, 153, 158 },
    { 119, 137, 141, 119, 138, 142 },
    { 102, 120, 125, 100, 120, 124 },
    {  82,  98, 104,  79,  98, 103 },
    {  60,  76,  81,  58,  76,  81 },
    {  39,  52,  58,  36,  52,  58 },
    {  16,  27,  33,  14,  27,  33 },
    {   5,   8,   9,   4,   7,   8 }
};

static const int DPK_QPS_16f[11][6] = {
    { 148, 177, 171, 165, 187, 191 },
    { 133, 155, 153, 147, 172, 181 },
    { 114, 133, 138, 130, 157, 167 },
    {  97, 118, 120, 109, 137, 144 },
    {  76,  98, 103,  85, 115, 121 },
    {  63,  86,  91,  62,  96,  99 },
    {  46,  68,  71,  43,  73,  75 },
    {  29,  48,  52,  27,  48,  51 },
    {  16,  30,  35,  14,  29,  34 },
    {   8,  14,  17,   7,  13,  17 },
    {   3,   5,   7,   3,   5,   6 }
};

static const int DPK_QPS_32f[11][6] = {
    { 194, 206, 209, 204, 211, 217 },
    { 175, 187, 196, 186, 193, 205 },
    { 157, 170, 177, 167, 180, 190 },
    { 133, 152, 156, 144, 163, 168 },
    { 116, 138, 142, 117, 143, 148 },
    {  98, 120, 123,  96, 123, 126 },
    {  80,  99, 102,  78,  99, 102 },
    {  65,  79,  84,  63,  79,  84 },
    {  48,  61,  67,  45,  60,  66 },
    {  27,  41,  46,  24,  40,  45 },
    {   3,  22,  24,   2,  21,  22 }
};

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "JPEG-XR";
}

static const char * DLL_CALLCONV
Description() {
	return "JPEG XR image format";
}

static const char * DLL_CALLCONV
Extension() {
	return "jxr,wdp,hdp";
}

static const char * DLL_CALLCONV
RegExpr() {
	return NULL;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/vnd.ms-photo";
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	BYTE jxr_signature[4] = { 0x49, 0x49, 0xBC, 0x01 };
	BYTE signature[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	if (io->read_proc(&signature, 1, 8, handle) == 8) {
		if (memcmp(jxr_signature, signature, 4) == 0) {
			// FIRST_IFD_OFFSET (little-endian format) specifies the byte position, relative to the beginning of the file, of the first
			// IMAGE_FILE_DIRECTORY() syntax structure(subclause A.6) in the file.
			// The value of FIRST_IFD_OFFSET shall be an integer multiple of 2.
			unsigned first_ifd_offset = *((unsigned*)(&signature[0] + 4));
			return (first_ifd_offset % 2 == 0) ? TRUE : FALSE;
		}
	}

	return FALSE;
}

static BOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return (
		(depth == 1)  ||
		(depth == 8)  ||
		(depth == 16) ||
		(depth == 24) || 
		(depth == 32)
		);
}

static BOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return (
		(type == FIT_BITMAP) ||
		(type == FIT_UINT16) ||
		(type == FIT_RGB16)  ||
		(type == FIT_RGBA16) ||
		(type == FIT_FLOAT)  ||
		(type == FIT_RGBF)   ||
		(type == FIT_RGBAF)
	);
}

static BOOL DLL_CALLCONV
SupportsICCProfiles() {
	return TRUE;
}

static BOOL DLL_CALLCONV
SupportsNoPixels() {
	return TRUE;
}

// ==========================================================
//	Open & Close
// ==========================================================

static void * DLL_CALLCONV
Open(FreeImageIO *io, fi_handle handle, BOOL read) {
	WMPStream *pStream = NULL;	// stream interface
	if(io && handle) {
		// allocate the FreeImageIO stream wrapper
		FreeImageJXRIO *jxr_io = (FreeImageJXRIO*)malloc(sizeof(FreeImageJXRIO));
		if(jxr_io) {
			jxr_io->io = io;
			jxr_io->handle = handle;
			{
				const INT64 lOff = io->tell_proc(handle);
				jxr_io->start = (lOff > 0) ? lOff : 0;
				jxr_io->pos = 0;
			}
			// create a JXR stream wrapper
			if(_jxr_io_Create(&pStream, jxr_io) != WMP_errSuccess) {
				free(jxr_io);
				jxr_io = NULL;
				return NULL;
			}
		}
	}
	return pStream;
}

static void DLL_CALLCONV
Close(FreeImageIO *io, fi_handle handle, void *data) {
	WMPStream *pStream = (WMPStream*)data;
	if(pStream) {
		// free the FreeImageIO stream wrapper
		FreeImageJXRIO *jxr_io = (FreeImageJXRIO*)pStream->state.pvObj;
		free(jxr_io);
		jxr_io = NULL;
		// free the JXR stream wrapper
		pStream->fMem = TRUE;
		_jxr_io_Close(&pStream);
	}
}

// ==========================================================
//	Load
// ==========================================================

/**
Set decoder parameters
@param pDecoder Decoder handle
@param flags FreeImage load flags
*/
static void 
SetDecoderParameters(PKImageDecode *pDecoder, int flags) {
	// load image & alpha for formats with alpha
	pDecoder->WMP.wmiSCP.uAlphaMode = 2;
	// more options to come ...
}

/** dst_line pixels c0 to c1 from pixel x of src_lines, N bytes each */
template <size_t N> static void
TurnPixels(const BYTE * const *src_lines, unsigned c0, unsigned c1, unsigned x, BYTE *dst_line) {
	for(unsigned col = c0; col < c1; col++) {
		memcpy(dst_line + (size_t)col * N, src_lines[col - c0] + (size_t)x * N, N);
	}
}

/** upright rows decoded at a time, into a band buffer */
static const unsigned BAND_ROWS = 128;
static_assert(BAND_ROWS % 16 == 0, "a band has to end on a macroblock row");

/**
Turn upright rows into the columns a container orientation of a quarter turn puts them in: 90 degrees clockwise first, then its flips
@param src_bits Upright row y0, rows top-down
@param cbStride Bytes from one src row to the next
@param y0 First upright row
@param y1 Upright row past the last, at most BAND_ROWS rows after y0
@param dst Turned image, h x w for an upright image of w x h
@param oOrientation O_RCW, O_RCW_FLIPV, O_RCW_FLIPH or O_RCW_FLIPVH
*/
static void
TurnRows(const BYTE *src_bits, size_t cbStride, unsigned y0, unsigned y1, FIBITMAP *dst, ORIENTATION oOrientation) {
	const unsigned w = FreeImage_GetHeight(dst);
	const unsigned h = FreeImage_GetWidth(dst);
	const unsigned bytespp = FreeImage_GetBPP(dst) / 8;
	const BOOL flip_v = (oOrientation == O_RCW_FLIPV) || (oOrientation == O_RCW_FLIPVH);
	const BOOL flip_h = (oOrientation == O_RCW_FLIPH) || (oOrientation == O_RCW_FLIPVH);
	// upright row y lands in column flip_h ? y : h - 1 - y
	const unsigned c0 = flip_h ? y0 : h - y1;
	const unsigned c1 = flip_h ? y1 : h - y0;
	const BYTE *src_lines[BAND_ROWS];

	for(unsigned col = c0; col < c1; col++) {
		src_lines[col - c0] = src_bits + (size_t)((flip_h ? col : h - 1 - col) - y0) * cbStride;
	}
	// scanlines run bottom-up: dst scanline d is upright column x
	for(unsigned d = 0; d < w; d++) {
		const unsigned x = flip_v ? d : w - 1 - d;
		BYTE *dst_line = FreeImage_GetScanLine(dst, d);
		switch(bytespp) {
			case 0:
				for(unsigned col = c0; col < c1; col++) {
					const BYTE mask = (BYTE)(0x80 >> (col & 7));
					if(src_lines[col - c0][x >> 3] & (0x80 >> (x & 7))) {
						dst_line[col >> 3] |= mask;
					} else {
						dst_line[col >> 3] &= (BYTE)~mask;
					}
				}
				break;
			case 1: TurnPixels<1>(src_lines, c0, c1, x, dst_line); break;
			case 2: TurnPixels<2>(src_lines, c0, c1, x, dst_line); break;
			case 3: TurnPixels<3>(src_lines, c0, c1, x, dst_line); break;
			case 4: TurnPixels<4>(src_lines, c0, c1, x, dst_line); break;
			case 6: TurnPixels<6>(src_lines, c0, c1, x, dst_line); break;
			case 8: TurnPixels<8>(src_lines, c0, c1, x, dst_line); break;
			case 12: TurnPixels<12>(src_lines, c0, c1, x, dst_line); break;
			case 16: TurnPixels<16>(src_lines, c0, c1, x, dst_line); break;
			default: break;
		}
	}
}

/**
CMYK samples, and alpha, to BGR(A) in place, as ConvertCMYKtoRGBA: (255 - C) * (255 - K) / 255
@param row Row of pixels
@param width Row width (in pixels)
@param in_channels 4 to 9: CMYK, then channels to drop, alpha last
@param out_channels 3, or 4 with alpha
*/
static void
CMYKRow8(BYTE *row, unsigned width, unsigned in_channels, unsigned out_channels) {
	for(unsigned x = 0; x < width; x++) {
		const BYTE *src = row + (size_t)x * in_channels;
		const unsigned k = 255 - src[3];
		BYTE px[4];
		px[0] = (BYTE)((255 - src[2]) * k / 255);
		px[1] = (BYTE)((255 - src[1]) * k / 255);
		px[2] = (BYTE)((255 - src[0]) * k / 255);
		px[3] = src[in_channels - 1];
		memcpy(row + (size_t)x * out_channels, px, out_channels);
	}
}

/**
CMYK samples, and alpha, to RGB(A) in place, as ConvertCMYKtoRGBA: (65535 - C) * (65535 - K) / 65535
@see CMYKRow8
*/
static void
CMYKRow16(WORD *row, unsigned width, unsigned in_channels, unsigned out_channels) {
	for(unsigned x = 0; x < width; x++) {
		const WORD *src = row + (size_t)x * in_channels;
		const unsigned k = 65535 - src[3];
		WORD px[4];
		px[0] = (WORD)((65535 - src[0]) * k / 65535);
		px[1] = (WORD)((65535 - src[1]) * k / 65535);
		px[2] = (WORD)((65535 - src[2]) * k / 65535);
		px[3] = src[in_channels - 1];
		memcpy(row + (size_t)x * out_channels, px, out_channels * sizeof(WORD));
	}
}

/**
Undo premultiplied alpha in a row of 8-bit pixels, alpha last
*/
static void
UnpremultiplyRow8(BYTE *row, unsigned width) {
	for(unsigned x = 0; x < width; x++, row += 4) {
		const unsigned a = row[3];
		if((a > 0) && (a < 255)) {
			for(int c = 0; c < 3; c++) {
				const unsigned v = ((unsigned)row[c] * 255u + a / 2) / a;
				row[c] = (v > 255) ? 255 : (BYTE)v;
			}
		}
	}
}

/**
Undo premultiplied alpha in a row of 16-bit pixels, alpha last
*/
static void
UnpremultiplyRow16(WORD *row, unsigned width) {
	for(unsigned x = 0; x < width; x++, row += 4) {
		const UINT64 a = row[3];
		if((a > 0) && (a < 65535)) {
			for(int c = 0; c < 3; c++) {
				const UINT64 v = ((UINT64)row[c] * 65535u + a / 2) / a;
				row[c] = (v > 65535) ? 65535 : (WORD)v;
			}
		}
	}
}

/**
Undo premultiplied alpha in a row of float pixels, alpha last
*/
static void
UnpremultiplyRowFloat(float *row, unsigned width) {
	for(unsigned x = 0; x < width; x++, row += 4) {
		const float a = row[3];
		if(a > 0) {
			for(int c = 0; c < 3; c++) {
				row[c] /= a;
			}
		}
	}
}

/**
Give a decoded row the layout of the dib's rows, in place
@param row Row of pixels
@param width Row width (in pixels)
@param layout What the row needs
@param in_channels Channels of a decoded pixel
*/
static void
TransformRow(BYTE *row, unsigned width, JXRRowLayout layout, unsigned in_channels) {
	switch(layout) {
		case ROWS_PADDED_8:
			for(unsigned x = 1; x < width; x++) {
				row[3 * x] = row[4 * x];
				row[3 * x + 1] = row[4 * x + 1];
				row[3 * x + 2] = row[4 * x + 2];
			}
			break;
		case ROWS_CMYK_8: CMYKRow8(row, width, in_channels, 3); break;
		case ROWS_CMYKA_8: CMYKRow8(row, width, in_channels, 4); break;
		case ROWS_CMYK_16: CMYKRow16((WORD*)row, width, in_channels, 3); break;
		case ROWS_CMYKA_16: CMYKRow16((WORD*)row, width, in_channels, 4); break;
		case ROWS_CMYK_KEPT_8:
			for(unsigned x = 1; (in_channels > 4) && (x < width); x++) {
				memmove(row + (size_t)x * 4, row + (size_t)x * in_channels, 4);
			}
			break;
		case ROWS_CMYK_KEPT_16:
			for(unsigned x = 1; (in_channels > 4) && (x < width); x++) {
				memmove((WORD*)row + (size_t)x * 4, (WORD*)row + (size_t)x * in_channels, 4 * sizeof(WORD));
			}
			break;
		case ROWS_PREMULTIPLIED_8: UnpremultiplyRow8(row, width); break;
		case ROWS_PREMULTIPLIED_16: UnpremultiplyRow16((WORD*)row, width); break;
		case ROWS_PREMULTIPLIED_FLOAT: UnpremultiplyRowFloat((float*)row, width); break;
		default: break;
	}
}

/**
Tell whether a codestream has windowing margins, which jxrlib crops off the top of its rows
@param pWS Stream
@param offset Codestream position
@return Returns FALSE only for a codestream header without margins
*/
static BOOL
IsWindowed(WMPStream *pWS, size_t offset) {
	BYTE header[11];
	size_t pos = 0;
	BOOL windowed = TRUE;
	if(pWS->GetPos(pWS, &pos) == WMP_errSuccess) {
		if((pWS->SetPos(pWS, offset) == WMP_errSuccess) && (pWS->Read(pWS, header, sizeof(header)) == WMP_errSuccess)) {
			// WINDOWING_FLAG: bit 5 of the third byte after the signature
			windowed = (memcmp(header, "WMPHOTO", 7) != 0) || ((header[10] & 0x20) != 0);
		}
		pWS->SetPos(pWS, pos);
	}
	return windowed;
}

/**
Get the stride of a buffer holding rows of either pixel format
@param in_guid_format Decoded pixel format
@param out_guid_format Target pixel format
@param width Row width (in pixels)
@param cbStride Returned stride
@return Returns 0 if successful, returns ERR otherwise
*/
static ERR
GetConverterStride(const PKPixelFormatGUID &in_guid_format, const PKPixelFormatGUID &out_guid_format, int width, unsigned *cbStride) {
	PKPixelInfo pPIFrom;
	PKPixelInfo pPITo;

	pPIFrom.pGUIDPixFmt = &in_guid_format;
	ERR error_code = PixelFormatLookup(&pPIFrom, LOOKUP_FORWARD);
	if(error_code != WMP_errSuccess) {
		return error_code;
	}

	pPITo.pGUIDPixFmt = &out_guid_format;
	error_code = PixelFormatLookup(&pPITo, LOOKUP_FORWARD);
	if(error_code != WMP_errSuccess) {
		return error_code;
	}

	// 64-bit: a wide image overflows a 32-bit stride
	const size_t cbStrideFrom = (size_t)((pPIFrom.cbitUnit + 7) >> 3) * (size_t)width;
	const size_t cbStrideTo = (size_t)((pPITo.cbitUnit + 7) >> 3) * (size_t)width;
	// jxrlib counts rows in units of up to 4 bytes, rounding a stride down to them
	const size_t cbStrideMax = (MAX(cbStrideFrom, cbStrideTo) + 3) & ~(size_t)3;
	*cbStride = (unsigned)cbStrideMax;

	// the codec passes a stride as U32
	return (cbStrideMax > (size_t)UINT_MAX) ? WMP_errBufferOverflow : WMP_errSuccess;
}

/**
Swap red and blue of pixels decoded in the other color order
@param dib Decoded dib
@param out_guid_format Decoded pixel format
*/
static void
SwapRedBlueAsNeeded(FIBITMAP *dib, const PKPixelFormatGUID &out_guid_format) {
#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_BGR
	if(IsEqualGUID(out_guid_format, GUID_PKPixelFormat24bppRGB) || IsEqualGUID(out_guid_format, GUID_PKPixelFormat32bppRGB) || IsEqualGUID(out_guid_format, GUID_PKPixelFormat32bppRGBA)) {
		SwapRedBlue32(dib);
	}
#elif FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB
	if(IsEqualGUID(out_guid_format, GUID_PKPixelFormat24bppBGR) || IsEqualGUID(out_guid_format, GUID_PKPixelFormat32bppBGR) || IsEqualGUID(out_guid_format, GUID_PKPixelFormat32bppBGRA)) {
		SwapRedBlue32(dib);
	}
#endif
}

/**
Copy decoded pixels into the dib, in the pixel format they are decoded in
@param pDecoder Decoder handle
@param out_guid_format Decoded pixel format
@param dib Output dib
@param width Image width
@param height Image height
@return Returns 0 if successful, returns ERR otherwise
*/
static ERR
CopyPixels(PKImageDecode *pDecoder, PKPixelFormatGUID out_guid_format, FIBITMAP *dib, int width, int height) {
	ERR error_code = 0;	// error code as returned by the interface

	// image dimensions
	const PKRect rect = {0, 0, width, height};

	try {
		// get a pointer to dst pixel data
		BYTE *dib_bits = FreeImage_GetBits(dib);

		// get dst pitch (count of BYTE for stride)
		const unsigned cbStride = FreeImage_GetPitch(dib);

		// decode and copy bits to dst array
		error_code = pDecoder->Copy(pDecoder, &rect, dib_bits, cbStride);
		JXR_CHECK(error_code);

		// FreeImage DIB are upside-down relative to usual graphic conventions
		FreeImage_FlipVertical(dib);

		// post-processing ...
		// -------------------

		SwapRedBlueAsNeeded(dib, out_guid_format);

		return WMP_errSuccess;

	} catch(...) {
		return error_code;
	}
}

/**
Decode an image a band at a time, converting its pixel format if needed, and copy or turn each band into the dib
@param pDecoder Decoder handle
@param out_guid_format Target guid format
@param dib Output dib
@return Returns 0 if successful, returns ERR otherwise
*/
static ERR
CopyBandedPixels(PKImageDecode *pDecoder, const PKPixelFormatGUID &out_guid_format, FIBITMAP *dib) {
	PKFormatConverter *pConverter = NULL;	// pixel format converter
	ERR error_code = 0;	// error code as returned by the interface
	BYTE *pb = NULL;	// band buffer, also used for pixel format conversion

	// jxrlib flips rows left to right: a quarter turn, or upside down, is done here
	const ORIENTATION oOrientation = pDecoder->WMP.wmiI.oOrientation;
	const BOOL turn = (oOrientation >= O_RCW);
	const BOOL flip_v = (oOrientation == O_FLIPV) || (oOrientation == O_FLIPVH);
	pDecoder->WMP.wmiI.oOrientation = ((oOrientation == O_FLIPH) || (oOrientation == O_FLIPVH)) ? O_FLIPH : O_NONE;

	// upright image dimensions
	const unsigned width = turn ? FreeImage_GetHeight(dib) : FreeImage_GetWidth(dib);
	const unsigned height = turn ? FreeImage_GetWidth(dib) : FreeImage_GetHeight(dib);

	try {
		PKPixelFormatGUID in_guid_format;
		error_code = pDecoder->GetPixelFormat(pDecoder, &in_guid_format);
		JXR_CHECK(error_code);

		// a layout of FreeImage's is decoded as it is, its rows then transformed here; n-channel rows stand for RGB or CMYK
		const PKPixelFormatGUID &rows_format = GetRowsFormat(in_guid_format);
		const JXRRowLayout layout = GetRowLayout(rows_format, out_guid_format);
		const BOOL convert = (layout == ROWS_AS_DECODED) && !IsEqualGUID(out_guid_format, rows_format);
		PKPixelInfo inInfo;
		inInfo.pGUIDPixFmt = &in_guid_format;
		error_code = PixelFormatLookup(&inInfo, LOOKUP_FORWARD);
		JXR_CHECK(error_code);
		if(convert) {
			error_code = PKCodecFactory_CreateFormatConverter(&pConverter);
			JXR_CHECK(error_code);
			error_code = pConverter->Initialize(pConverter, pDecoder, NULL, out_guid_format);
			JXR_CHECK(error_code);
		}

		unsigned cbStride = 0;
		error_code = GetConverterStride(in_guid_format, out_guid_format, (int)width, &cbStride);
		JXR_CHECK(error_code);

		// margins would shift jxrlib's rows across bands: decode a windowed image in one band
		WMPStream *pWS = pDecoder->pStream;
		size_t image_pos = 0;
		error_code = pWS->GetPos(pWS, &image_pos);
		JXR_CHECK(error_code);
		const BOOL windowed = IsWindowed(pWS, image_pos) || (pDecoder->WMP.bHasAlpha && IsWindowed(pWS, pDecoder->WMP.wmiDEMisc.uAlphaOffset));
		const unsigned band = windowed ? height : MIN(height, BAND_ROWS);

		error_code = PKAllocAligned((void **) &pb, (size_t)cbStride * band, 128);
		JXR_CHECK(error_code);

		for(unsigned y = 0; y < height; y += band) {
			const unsigned rows = MIN(band, height - y);
			const PKRect rect = {0, (I32)y, (I32)width, (I32)rows};
			if(convert) {
				error_code = pConverter->Copy(pConverter, &rect, pb, cbStride);
			} else {
				error_code = pDecoder->Copy(pDecoder, &rect, pb, cbStride);
			}
			JXR_CHECK(error_code);
			if((y + rows < height) && (pDecoder->WMP.cLinesCropped != 0)) {
				// the next band's rows would land above it
				error_code = WMP_errFail;
				JXR_CHECK(error_code);
			}
			if(layout != ROWS_AS_DECODED) {
				for(unsigned r = 0; r < rows; r++) {
					TransformRow(pb + (size_t)r * cbStride, width, layout, (unsigned)inInfo.cChannel);
				}
			}
			if(turn) {
				for(unsigned r = 0; r < rows; r += BAND_ROWS) {
					TurnRows(pb + (size_t)r * cbStride, cbStride, y + r, y + MIN(rows, r + BAND_ROWS), dib, oOrientation);
				}
			} else {
				// scanlines run bottom-up
				const size_t line_size = FreeImage_GetLine(dib);
				for(unsigned r = y; r < y + rows; r++) {
					memcpy(FreeImage_GetScanLine(dib, flip_v ? r : height - 1 - r), pb + (size_t)(r - y) * cbStride, line_size);
				}
			}
		}

		PKFreeAligned((void **) &pb);
		PKFormatConverter_Release(&pConverter);

		SwapRedBlueAsNeeded(dib, out_guid_format);

		return WMP_errSuccess;

	} catch(...) {
		PKFreeAligned((void **) &pb);
		PKFormatConverter_Release(&pConverter);

		return error_code;
	}
}

// --------------------------------------------------------------------------

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	PKImageDecode *pDecoder = NULL;	// decoder interface
	ERR error_code = 0;				// error code as returned by the interface
	PKPixelFormatGUID guid_format;	// loaded pixel format (== input file pixel format if no conversion needed)
	
	FREE_IMAGE_TYPE image_type = FIT_UNKNOWN;	// input image type
	unsigned bpp = 0;							// input image bit depth
	FIBITMAP *dib = NULL;
	
	// get the I/O stream wrapper
	WMPStream *pDecodeStream = (WMPStream*)data;

	if(!handle || !pDecodeStream) {
		return NULL;
	}

	BOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;
	BOOL keep_cmyk = (flags & JXR_CMYK) == JXR_CMYK;

	try {
		int width, height;	// image dimensions (in pixels)

		// create a JXR decoder interface and initialize function pointers with *_WMP functions
		error_code = PKImageDecode_Create_WMP(&pDecoder);
		JXR_CHECK(error_code);

		// attach the stream to the decoder ...
		// ... then read the image container and the metadata
		error_code = pDecoder->Initialize(pDecoder, pDecodeStream);
		JXR_CHECK(error_code);

		// set decoder parameters
		SetDecoderParameters(pDecoder, flags);

		// get dst image format specifications
		unsigned red_mask = 0, green_mask = 0, blue_mask = 0;
		error_code = GetInputPixelFormat(pDecoder, keep_cmyk, &guid_format, &image_type, &bpp, &red_mask, &green_mask, &blue_mask);
		JXR_CHECK(error_code);

		// get image dimensions
		pDecoder->GetSize(pDecoder, &width, &height);

		// a codestream spends a bit or more on each 16 x 16 macroblock: an exact limit, so without PlausibleImageSize's margin
		if(!header_only && !PlausibleImageSize((UINT64)width * height * bpp / 8, CodestreamBytes(pDecoder), 256 * (UINT64)bpp / 64)) {
			throw FI_MSG_ERROR_CORRUPTED_IMAGE;
		}

		// allocate dst image
		{
			dib = FreeImage_AllocateHeaderT(header_only, image_type, width, height, bpp, red_mask, green_mask, blue_mask);
			if(!dib) {
				throw FI_MSG_ERROR_DIB_MEMORY;
			}
			if(FreeImage_GetBPP(dib) == 1) {
				// BD1WHITE1 as FIC_MINISBLACK; BD1BLACK1, where 1 is black, as FIC_MINISWHITE
				const BYTE zero = pDecoder->WMP.wmiSCP.bBlackWhite ? 255 : 0;
				RGBQUAD *pal = FreeImage_GetPalette(dib);
				pal[0].rgbRed = pal[0].rgbGreen = pal[0].rgbBlue = zero;
				pal[1].rgbRed = pal[1].rgbGreen = pal[1].rgbBlue = (BYTE)(255 - zero);
			}
		}

		// get image resolution
		{
			float resX, resY;	// image resolution (in dots per inch)
			// convert from English units, i.e. dots per inch to universal units, i.e. dots per meter
			pDecoder->GetResolution(pDecoder, &resX, &resY);
			if(pDecoder->WMP.wmiI.oOrientation >= O_RCW) {
				// turned a quarter, like the size
				const float res = resX;
				resX = resY;
				resY = res;
			}
			FreeImage_SetDotsPerMeterX(dib, (unsigned)(resX / 0.0254F + 0.5F));
			FreeImage_SetDotsPerMeterY(dib, (unsigned)(resY / 0.0254F + 0.5F));
		}

		// get metadata & ICC profile
		error_code = ReadMetadata(pDecoder, dib);
		JXR_CHECK(error_code);

		PKPixelFormatGUID in_guid_format;
		error_code = pDecoder->GetPixelFormat(pDecoder, &in_guid_format);
		JXR_CHECK(error_code);
		{
			const int layout = FindRowLayout(GetRowsFormat(in_guid_format), keep_cmyk);
			if((layout >= 0) && (s_FreeImageRowLayouts[layout].cmyk == CMYK_CONVERTED)) {
				// a CMYK profile does not describe the RGB pixels are loaded as
				FreeImage_DestroyICCProfile(dib);
			} else if((layout >= 0) && (s_FreeImageRowLayouts[layout].cmyk == CMYK_KEPT)) {
				FreeImage_GetICCProfile(dib)->flags |= FIICC_COLOR_IS_CMYK;
			}
		}

		if(header_only) {
			// header only mode ...

			// free the decoder
			pDecoder->Release(&pDecoder);
			assert(pDecoder == NULL);

			return dib;
		}

		// copy pixels into the dib, perform pixel conversion if needed
		if((pDecoder->WMP.wmiI.oOrientation < O_RCW) && IsEqualGUID(guid_format, in_guid_format)) {
			error_code = CopyPixels(pDecoder, guid_format, dib, width, height);
		} else {
			// jxrlib cannot turn an image a quarter, and converts pixels in a buffer: decode a band at a time
			error_code = CopyBandedPixels(pDecoder, guid_format, dib);
		}
		JXR_CHECK(error_code);

		// free the decoder
		pDecoder->Release(&pDecoder);
		assert(pDecoder == NULL);

		return dib;

	} catch (const char *message) {
		// unload the dib
		FreeImage_Unload(dib);
		dib = NULL;
		// free the decoder
		if(pDecoder) {
			pDecoder->Release(&pDecoder);
		}

		if(NULL != message) {
			FreeImage_OutputMessageProc(s_format_id, message);
		}
	}

	return NULL;
}

// ==========================================================
//	Save
// ==========================================================

/**
Configure compression parameters

ImageQuality  Q (BD==1)  Q (BD==8)   Q (BD==16)  Q (BD==32F) Subsample   Overlap
[0.0, 0.4]    8-IQ*5     (see table) (see table) (see table) 4:4:4       2
(0.4, 0.8)    8-IQ*5     (see table) (see table) (see table) 4:4:4       1
[0.8, 1.0)    8-IQ*5     (see table) (see table) (see table) 4:4:4       1
[1.0, 1.0]    1          1           1           1           4:4:4       0

@param wmiSCP Encoder parameters
@param pixelInfo Image specifications
@param width Image width in pixels
@param fltImageQuality Image output quality in [0..1), 1 means lossless
*/
static void
SetCompression(CWMIStrCodecParam *wmiSCP, const PKPixelInfo *pixelInfo, unsigned width, float fltImageQuality) {
    if(fltImageQuality < 1.0F) {
        // overlap
		if(fltImageQuality >= 0.5F) {
			wmiSCP->olOverlap = OL_ONE;
		} else {
			wmiSCP->olOverlap = OL_TWO;
		}
		// chroma sub-sampling
		if(fltImageQuality >= 0.5F || pixelInfo->uBitsPerSample > 8) {
			wmiSCP->cfColorFormat = YUV_444;
		} else {
			wmiSCP->cfColorFormat = YUV_420;
		}
		// the codec refuses 4:2:x with OL_TWO one macroblock wide
		if((wmiSCP->olOverlap == OL_TWO) &&
		   ((wmiSCP->cfColorFormat == YUV_420) || (wmiSCP->cfColorFormat == YUV_422)) &&
		   (((width + 15) >> 4) < 2)) {
			wmiSCP->olOverlap = OL_ONE;
		}

	    // bit depth
		if(pixelInfo->bdBitDepth == BD_1) {
			wmiSCP->uiDefaultQPIndex = (U8)(8 - 5.0F * fltImageQuality + 0.5F);
		}
		else {
			// remap [0.8, 0.866, 0.933, 1.0] to [0.8, 0.9, 1.0, 1.1]
            // to use 8-bit DPK QP table (0.933 == Photoshop JPEG 100)
            if(fltImageQuality > 0.8F && pixelInfo->bdBitDepth == BD_8 && wmiSCP->cfColorFormat != YUV_420 && wmiSCP->cfColorFormat != YUV_422) {
				fltImageQuality = 0.8F + (fltImageQuality - 0.8F) * 1.5F;
			}

            const int qi = (int) (10.0F * fltImageQuality);
            const float qf = 10.0F * fltImageQuality - (float)qi;
			
			const int *pQPs = 
				(wmiSCP->cfColorFormat == YUV_420 || wmiSCP->cfColorFormat == YUV_422) ?
				DPK_QPS_420[qi] :
				(pixelInfo->bdBitDepth == BD_8 ? DPK_QPS_8[qi] :
				(pixelInfo->bdBitDepth == BD_16 ? DPK_QPS_16[qi] :
				(pixelInfo->bdBitDepth == BD_16F ? DPK_QPS_16f[qi] :
				DPK_QPS_32f[qi])));
				
			wmiSCP->uiDefaultQPIndex = (U8) (0.5F + (float) pQPs[0] * (1.0F - qf) + (float) (pQPs + 6)[0] * qf);
			wmiSCP->uiDefaultQPIndexU = (U8) (0.5F + (float) pQPs[1] * (1.0F - qf) + (float) (pQPs + 6)[1] * qf);
			wmiSCP->uiDefaultQPIndexV = (U8) (0.5F + (float) pQPs[2] * (1.0F - qf) + (float) (pQPs + 6)[2] * qf);
            wmiSCP->uiDefaultQPIndexYHP = (U8) (0.5F + (float) pQPs[3] * (1.0F - qf) + (float) (pQPs + 6)[3] * qf);
			wmiSCP->uiDefaultQPIndexUHP = (U8) (0.5F + (float) pQPs[4] * (1.0F - qf) + (float) (pQPs + 6)[4] * qf);
			wmiSCP->uiDefaultQPIndexVHP = (U8) (0.5F + (float) pQPs[5] * (1.0F - qf) + (float) (pQPs + 6)[5] * qf);
		}
	} // fltImageQuality < 1.0F
    else {
		// lossless mode
		wmiSCP->uiDefaultQPIndex = 1;
	}
}

/**
Set encoder parameters
@param wmiSCP Encoder parameters
@param pixelInfo Image specifications
@param width Image width in pixels
@param flags FreeImage save flags
@param bHasAlpha TRUE if an alpha layer is present
*/
static void
SetEncoderParameters(CWMIStrCodecParam *wmiSCP, const PKPixelInfo *pixelInfo, unsigned width, int flags, BOOL bHasAlpha) {
	float fltImageQuality = 1.0F;

	// all values have been set to zero by the API
	// update default values for some attributes
    wmiSCP->cfColorFormat = YUV_444;		// color format
    wmiSCP->bdBitDepth = BD_LONG;			// internal bit depth
    wmiSCP->bfBitstreamFormat = SPATIAL;	// compressed image data in spatial order
    wmiSCP->bProgressiveMode = FALSE;		// sequential mode
    wmiSCP->olOverlap = OL_ONE;				// single level overlap processing 
	wmiSCP->cNumOfSliceMinus1H = 0;			// # of horizontal slices
	wmiSCP->cNumOfSliceMinus1V = 0;			// # of vertical slices
    wmiSCP->sbSubband = SB_ALL;				// keep all subbands
    wmiSCP->uAlphaMode = 0;					// 0:no alpha 1: alpha only else: something + alpha 
    wmiSCP->uiDefaultQPIndex = 1;			// quantization for grey or rgb layer(s), 1: lossless
    wmiSCP->uiDefaultQPIndexAlpha = 1;		// quantization for alpha layer, 1: lossless

	// process the flags
	// -----------------

	// progressive mode
	if((flags & JXR_PROGRESSIVE) == JXR_PROGRESSIVE) {
		// turn on progressive mode (instead of sequential mode)
		wmiSCP->bProgressiveMode = TRUE;
		// bProgressiveMode needs the FREQUENCY layout
		wmiSCP->bfBitstreamFormat = FREQUENCY;
	}

	// quality in [0.01 - 1.0), 1.0 means lossless - default is 0.80
	int quality = flags & 0x7F;
	if(quality == 0) {
		// defaut to 0.80
		fltImageQuality = 0.8F;
	} else if((flags & JXR_LOSSLESS) == JXR_LOSSLESS) {
		fltImageQuality = 1.0F;
	} else {
		quality = (quality >= 100) ? 100 : quality;
		fltImageQuality = quality / 100.0F;
	}
	SetCompression(wmiSCP, pixelInfo, width, fltImageQuality);

	// alpha compression
	if(bHasAlpha) {
		wmiSCP->uAlphaMode = 2;	// encode with a planar alpha channel
	}
}

// --------------------------------------------------------------------------

static BOOL DLL_CALLCONV
Save(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	BOOL bIsFlipped = FALSE;		// FreeImage DIB are upside-down relative to usual graphic conventions
	PKPixelFormatGUID guid_format;	// image format
	PKPixelInfo pixelInfo;			// image specifications
	BOOL bHasAlpha = FALSE;			// is alpha layer present ?

	PKImageEncode *pEncoder = NULL;		// encoder interface
	ERR error_code = 0;					// error code as returned by the interface

	// get the I/O stream wrapper
	WMPStream *pEncodeStream = (WMPStream*)data;

	if(!dib || !handle || !pEncodeStream) {
		return FALSE;
	}

	try {
		// get image dimensions
		unsigned width = FreeImage_GetWidth(dib);
		unsigned height = FreeImage_GetHeight(dib);

		// check JPEG-XR limits
		// (no minimum: the codec pads to 16x16 macroblocks)
		if((width == 0) || (height == 0)) {
			FreeImage_OutputMessageProc(s_format_id, "Unsupported image size: width x height = %d x %d", width, height);
			throw (const char*)NULL;
		}

		// get output pixel format
		error_code = GetOutputPixelFormat(dib, &guid_format, &bHasAlpha);
		JXR_CHECK(error_code);
		pixelInfo.pGUIDPixFmt = &guid_format;
		error_code = PixelFormatLookup(&pixelInfo, LOOKUP_FORWARD);
		JXR_CHECK(error_code);

		// create a JXR encoder interface and initialize function pointers with *_WMP functions
		error_code = PKImageEncode_Create_WMP(&pEncoder);
		JXR_CHECK(error_code);

		// attach the stream to the encoder and set all encoder parameters to zero ...
		error_code = pEncoder->Initialize(pEncoder, pEncodeStream, &pEncoder->WMP.wmiSCP, sizeof(CWMIStrCodecParam));
		JXR_CHECK(error_code);

		// ... then configure the encoder
		SetEncoderParameters(&pEncoder->WMP.wmiSCP, &pixelInfo, width, flags, bHasAlpha);
		// BD1BLACK1: a 1 is black
		pEncoder->WMP.wmiSCP.bBlackWhite = (pixelInfo.bdBitDepth == BD_1) && (FreeImage_GetColorType(dib) == FIC_MINISWHITE);

		// set pixel format
		pEncoder->SetPixelFormat(pEncoder, guid_format);

		// set image size
		pEncoder->SetSize(pEncoder, width, height);
		
		// set resolution (convert from universal units to English units)
		float resX = (float)(unsigned)(0.5F + 0.0254F * FreeImage_GetDotsPerMeterX(dib));
		float resY = (float)(unsigned)(0.5F + 0.0254F * FreeImage_GetDotsPerMeterY(dib));
		pEncoder->SetResolution(pEncoder, resX, resY);

		// set metadata
		error_code = WriteMetadata(pEncoder, dib);
		JXR_CHECK(error_code);

		// write metadata & pixels
		// -----------------------

		// dib coordinates are upside-down relative to usual conventions
		bIsFlipped = FreeImage_FlipVertical(dib);

		// get a pointer to dst pixel data
		BYTE *dib_bits = FreeImage_GetBits(dib);

		// get dst pitch (count of BYTE for stride)
		const INT64 cbStride = FreeImage_GetPitch(dib);

		// write metadata + pixels on output
		error_code = pEncoder->WritePixels(pEncoder, height, dib_bits, cbStride);
		if(error_code == WMP_errBufferOverflow) {
			FreeImage_OutputMessageProc(s_format_id,
				"Encoded image is too large for the JPEG XR container: the format stores image and alpha offsets as 32-bit values, so the output file cannot exceed 4 GB. Save at a lower quality, or without JXR_LOSSLESS, to bring the encoded size down.");
			throw (const char*)NULL;
		}
		JXR_CHECK(error_code);

		// recover dib coordinates
		FreeImage_FlipVertical(dib);

		// free the encoder
		pEncoder->Release(&pEncoder);
		assert(pEncoder == NULL);
		
		return TRUE;

	} catch (const char *message) {
		if(bIsFlipped) {
			// recover dib coordinates
			FreeImage_FlipVertical(dib);
		}
		if(pEncoder) {
			// free the encoder
			pEncoder->Release(&pEncoder);
			assert(pEncoder == NULL);
		}
		if(NULL != message) {
			FreeImage_OutputMessageProc(s_format_id, message);
		}
	}

	return FALSE;
}

// ==========================================================
//	 Init
// ==========================================================

void DLL_CALLCONV
InitJXR(Plugin *plugin, int format_id) {
	s_format_id = format_id;

	plugin->format_proc = Format;
	plugin->description_proc = Description;
	plugin->extension_proc = Extension;
	plugin->regexpr_proc = RegExpr;
	plugin->open_proc = Open;
	plugin->close_proc = Close;
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

