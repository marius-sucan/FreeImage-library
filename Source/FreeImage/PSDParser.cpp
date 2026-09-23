// ==========================================================
// Photoshop Loader
//
// Design and implementation by
// - Hervé Drolon (drolon@infonie.fr)
// - Mihail Naydenov (mnaydenov@users.sourceforge.net)
// - Garrick Meeker (garrickmeeker@users.sourceforge.net)
//
// Based on LGPL code created and published by http://sourceforge.net/projects/elynx/
// Format is now publicly documented at:
// https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/
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
#include "PSDParser.h"

#include "../Metadata/FreeImageTag.h"

// --------------------------------------------------------------------------

// PSD signature (= '8BPS')
#define PSD_SIGNATURE	0x38425053
// Image resource block signature (= '8BIM')
#define PSD_RESOURCE	0x3842494D

// PSD color modes
#define PSDP_BITMAP			0
#define PSDP_GRAYSCALE		1
#define PSDP_INDEXED		2
#define PSDP_RGB			3
#define PSDP_CMYK			4
#define PSDP_MULTICHANNEL	7
#define PSDP_DUOTONE		8
#define PSDP_LAB			9

// PSD compression schemes
#define PSDP_COMPRESSION_NONE			0	//! Raw data
#define PSDP_COMPRESSION_RLE			1	//! RLE compression (same as TIFF packed bits)
#define PSDP_COMPRESSION_ZIP			2	//! ZIP compression without prediction
#define PSDP_COMPRESSION_ZIP_PREDICTION	3	//! ZIP compression with prediction

/**
PSD image resources
*/
enum {
	//! Obsolete - Photoshop 2.0
	PSDP_RES_RESOLUTION_INFO_V2		= 1000,
	//! ResolutionInfo structure
	PSDP_RES_RESOLUTION_INFO		= 1005,
	//! DisplayInfo structure
	PSDP_RES_DISPLAY_INFO			= 1007,
	//! IPTC-NAA record
	PSDP_RES_IPTC_NAA				= 1028,
	//! (Photoshop 4.0) Thumbnail resource for Photoshop 4.0 only
	PSDP_RES_THUMBNAIL_PS4			= 1033,
	//! (Photoshop 4.0) Copyright flag
	PSDP_RES_COPYRIGHT				= 1034,
	//! (Photoshop 5.0) Thumbnail resource (supersedes resource 1033)
	PSDP_RES_THUMBNAIL				= 1036,
	//! (Photoshop 5.0) Global Angle
	PSDP_RES_GLOBAL_ANGLE			= 1037,
	//! ICC profile
	PSDP_RES_ICC_PROFILE			= 1039,
	//! (Photoshop 6.0) Indexed Color Table Count; 2 bytes for the number of colors in table that are actually defined
	PSDP_RES_INDEXED_COLORS			= 1046,
	//! (Photoshop 6.0) Transparency Index. 2 bytes for the index of transparent color, if any.
	PSDP_RES_TRANSPARENCY_INDEX		= 1047,
	//! (Photoshop 7.0) EXIF data 1
	PSDP_RES_EXIF1					= 1058,
	//! (Photoshop 7.0) EXIF data 3
	PSDP_RES_EXIF3					= 1059,
	//! (Photoshop 7.0) XMP metadata
	PSDP_RES_XMP					= 1060,
	//! (Photoshop CS3) DisplayInfo structure
	PSDP_RES_DISPLAY_INFO_FLT		= 1077,
};

#define SAFE_DELETE_ARRAY(_p_) { if (NULL != (_p_)) { delete [] (_p_); (_p_) = NULL; } }

// --------------------------------------------------------------------------

template <int N>
class PSDGetValue {
public:
	static inline int get(const BYTE * iprBuffer) { return -1; } // error
};

template <>
class PSDGetValue<1> {
public:
	static inline BYTE get(const BYTE * iprBuffer) { return iprBuffer[0]; }
};

template <>
class PSDGetValue<2> {
public:
	static inline WORD get(const BYTE * iprBuffer) {
		// may be misaligned: memcpy, not a cast
		WORD v;
		memcpy(&v, iprBuffer, sizeof(v));
#ifndef FREEIMAGE_BIGENDIAN
		SwapShort(&v);
#endif
		return (int)v;
	}
};

template <>
class PSDGetValue<4> {
public:
	static inline DWORD get(const BYTE * iprBuffer) {
		DWORD v;
		memcpy(&v, iprBuffer, sizeof(v));
#ifndef FREEIMAGE_BIGENDIAN
		SwapLong(&v);
#endif
		return v;
	}
};

template <>
class PSDGetValue<8> {
public:
	static inline UINT64 get(const BYTE * iprBuffer) {
		UINT64 v;
		memcpy(&v, iprBuffer, sizeof(v));
#ifndef FREEIMAGE_BIGENDIAN
		SwapInt64(&v);
#endif
		return v;
	}
};

#define psdGetValue(PTR, SIZE)		PSDGetValue<SIZE>::get((PTR))
#define psdGetLongValue(PTR, SIZE)	PSDGetValue<SIZE>::get((PTR))

// --------------------------------------------------------------------------

static UINT64
psdReadSize(FreeImageIO *io, fi_handle handle, const psdHeaderInfo& header) {
	if(header._Version == 1) {
		BYTE Length[4];
		io->read_proc(Length, sizeof(Length), 1, handle);
		return psdGetLongValue(Length, sizeof(Length));
	} else {
		BYTE Length[8];
		io->read_proc(Length, sizeof(Length), 1, handle);
		return psdGetLongValue(Length, sizeof(Length));
	}
}

// --------------------------------------------------------------------------

template <int N>
class PSDSetValue {
public:
	static inline void set(BYTE * iprBuffer, int v) {} // error
};

template <>
class PSDSetValue<1> {
public:
	static inline void set(BYTE * iprBuffer, BYTE v) { iprBuffer[0] = v; }
};

template <>
class PSDSetValue<2> {
public:
	static inline void set(BYTE * iprBuffer, WORD v) {
#ifndef FREEIMAGE_BIGENDIAN
		SwapShort(&v);
#endif
		memcpy(iprBuffer, &v, sizeof(v));
	}
};

template <>
class PSDSetValue<4> {
public:
	static inline void set(BYTE * iprBuffer, DWORD v) {
#ifndef FREEIMAGE_BIGENDIAN
		SwapLong(&v);
#endif
		memcpy(iprBuffer, &v, sizeof(v));
	}
};

template <>
class PSDSetValue<8> {
public:
	static inline void set(BYTE * iprBuffer, UINT64 v) {
#ifndef FREEIMAGE_BIGENDIAN
		SwapInt64(&v);
#endif
		memcpy(iprBuffer, &v, sizeof(v));
	}
};

#define psdSetValue(PTR, SIZE, V)		PSDSetValue<SIZE>::set((PTR), (V))
#define psdSetLongValue(PTR, SIZE, V)	PSDSetValue<SIZE>::set((PTR), (V))

// --------------------------------------------------------------------------

static inline bool
psdWriteSize(FreeImageIO *io, fi_handle handle, const psdHeaderInfo& header, UINT64 v) {
	if(header._Version == 1) {
		BYTE Length[4];
		psdSetLongValue(Length, sizeof(Length), (DWORD)v);
		return (io->write_proc(Length, sizeof(Length), 1, handle) == 1);
	} else {
		BYTE Length[8];
		psdSetLongValue(Length, sizeof(Length), v);
		return (io->write_proc(Length, sizeof(Length), 1, handle) == 1);
	}
}

/**
Return Exif metadata as a binary read-only buffer.
The buffer is owned by the function and MUST NOT be freed by the caller.
*/
static BOOL
psd_write_exif_profile_raw(FIBITMAP *dib, BYTE **profile, unsigned *profile_size) {
    // marker identifying string for Exif = "Exif\0\0"
	// used by JPEG not PSD
    BYTE exif_signature[6] = { 0x45, 0x78, 0x69, 0x66, 0x00, 0x00 };

	FITAG *tag_exif = NULL;
	FreeImage_GetMetadata(FIMD_EXIF_RAW, dib, g_TagLib_ExifRawFieldName, &tag_exif);

	if(tag_exif) {
		const BYTE *tag_value = (BYTE*)FreeImage_GetTagValue(tag_exif);

		// verify the identifying string
		if(memcmp(exif_signature, tag_value, sizeof(exif_signature)) != 0) {
			// not an Exif profile
			return FALSE;
		}

		*profile = (BYTE*)tag_value + sizeof(exif_signature);
		*profile_size = (unsigned)FreeImage_GetTagLength(tag_exif) - sizeof(exif_signature);

		return TRUE;
	}

	return FALSE;
}

static BOOL
psd_set_xmp_profile(FIBITMAP *dib, const BYTE *dataptr, unsigned int datalen) {
	// create a tag
	FITAG *tag = FreeImage_CreateTag();
	if (tag) {
		FreeImage_SetTagID(tag, PSDP_RES_XMP);
		FreeImage_SetTagKey(tag, g_TagLib_XMPFieldName);
		FreeImage_SetTagLength(tag, (DWORD)datalen);
		FreeImage_SetTagCount(tag, (DWORD)datalen);
		FreeImage_SetTagType(tag, FIDT_ASCII);
		FreeImage_SetTagValue(tag, dataptr);

		// store the tag
		FreeImage_SetMetadata(FIMD_XMP, dib, FreeImage_GetTagKey(tag), tag);

		// destroy the tag
		FreeImage_DeleteTag(tag);
	}

	return TRUE;
}

/**
Return XMP metadata as a binary read-only buffer.
The buffer is owned by the function and MUST NOT be freed by the caller.
*/
static BOOL
psd_get_xmp_profile(FIBITMAP *dib, BYTE **profile, unsigned *profile_size) {
	FITAG *tag_xmp = NULL;
	FreeImage_GetMetadata(FIMD_XMP, dib, g_TagLib_XMPFieldName, &tag_xmp);

	if(tag_xmp && (NULL != FreeImage_GetTagValue(tag_xmp))) {

		*profile = (BYTE*)FreeImage_GetTagValue(tag_xmp);
		*profile_size = (unsigned)FreeImage_GetTagLength(tag_xmp);

		return TRUE;
	}

	return FALSE;
}

// --------------------------------------------------------------------------

psdHeaderInfo::psdHeaderInfo() : _Version(-1), _Channels(-1), _Height(-1), _Width(-1), _BitsPerChannel(-1), _ColourMode(-1) {
}

psdHeaderInfo::~psdHeaderInfo() {
}

bool psdHeaderInfo::Read(FreeImageIO *io, fi_handle handle) {
	psdHeader header;

	const int n = (int)io->read_proc(&header, sizeof(header), 1, handle);
	if(!n) {
		return false;
	}

	// check the signature
	int nSignature = psdGetValue(header.Signature, sizeof(header.Signature));
	if (PSD_SIGNATURE == nSignature) {
		// check the version
		short nVersion = (short)psdGetValue( header.Version, sizeof(header.Version) );
		if (1 == nVersion || 2 == nVersion) {
			_Version = nVersion;
			// header.Reserved must be zero
			BYTE psd_reserved[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
			if(memcmp(header.Reserved, psd_reserved, 6) != 0) {
				FreeImage_OutputMessageProc(FIF_PSD, "Warning: file header reserved member is not equal to zero");
			}
			// read the header
			_Channels = (short)psdGetValue( header.Channels, sizeof(header.Channels) );
			_Height = psdGetValue( header.Rows, sizeof(header.Rows) );
			_Width = psdGetValue( header.Columns, sizeof(header.Columns) );
			_BitsPerChannel = (short)psdGetValue( header.Depth, sizeof(header.Depth) );
			_ColourMode = (short)psdGetValue( header.Mode, sizeof(header.Mode) );
			if (_Version == 1 && (_Width > 30000 || _Height > 30000)) {
				return false;
			}
			switch (_BitsPerChannel) {
				case 1:
				case 8:
				case 16:
				case 32:
					break;
				default:
					return false;
			}

			return true;
		}
	}

	return false;
}

bool psdHeaderInfo::Write(FreeImageIO *io, fi_handle handle) {
	psdHeader header;

	psdSetValue(header.Signature, sizeof(header.Signature), PSD_SIGNATURE);
	psdSetValue(header.Version, sizeof(header.Version), _Version);
	memset(header.Reserved, 0, sizeof(header.Reserved));
	psdSetValue(header.Channels, sizeof(header.Channels), _Channels);
	psdSetValue(header.Rows, sizeof(header.Rows), _Height);
	psdSetValue(header.Columns, sizeof(header.Columns), _Width);
	psdSetValue(header.Depth, sizeof(header.Depth), _BitsPerChannel);
	psdSetValue(header.Mode, sizeof(header.Mode), _ColourMode);
	return (io->write_proc(&header, sizeof(header), 1, handle) == 1);
}

// --------------------------------------------------------------------------

psdColourModeData::psdColourModeData() : _Length(-1), _plColourData(NULL) {
}

psdColourModeData::~psdColourModeData() {
	SAFE_DELETE_ARRAY(_plColourData);
}

bool psdColourModeData::Read(FreeImageIO *io, fi_handle handle) {
	if (0 < _Length) {
		SAFE_DELETE_ARRAY(_plColourData);
	}

	BYTE Length[4];
	io->read_proc(Length, sizeof(Length), 1, handle);

	_Length = psdGetValue( Length, sizeof(_Length) );
	if (0 < _Length) {
		_plColourData = new BYTE[_Length];
		io->read_proc(_plColourData, _Length, 1, handle);
	}

	return true;
}

bool psdColourModeData::Write(FreeImageIO *io, fi_handle handle) {
	BYTE Length[4];
	psdSetValue(Length, sizeof(Length), (DWORD)_Length);

	if(io->write_proc(Length, sizeof(Length), 1, handle) != 1) {
		return false;
	}
	if(0 < _Length) {
		if(io->write_proc(_plColourData, _Length, 1, handle) != 1) {
			return false;
		}
	}
	return true;
}

bool psdColourModeData::FillPalette(FIBITMAP *dib) {
	RGBQUAD *pal = FreeImage_GetPalette(dib);
	if(pal) {
		for (int i = 0; i < 256; i++) {
			pal[i].rgbRed	= _plColourData[i + 0*256];
			pal[i].rgbGreen = _plColourData[i + 1*256];
			pal[i].rgbBlue	= _plColourData[i + 2*256];
		}
		return true;
	}
	return false;
}

// --------------------------------------------------------------------------

psdImageResource::psdImageResource() : _plName (0) {
	Reset();
}

psdImageResource::~psdImageResource() {
	SAFE_DELETE_ARRAY(_plName);
}

void psdImageResource::Reset() {
	_Length = -1;
	memset( _OSType, '\0', sizeof(_OSType) );
	_ID = -1;
	SAFE_DELETE_ARRAY(_plName);
	_Size = -1;
}

bool psdImageResource::Write(FreeImageIO *io, fi_handle handle, int ID, int Size) {
	BYTE ShortValue[2], IntValue[4];

	_ID = ID;
	_Size = Size;
	psdSetValue((BYTE*)_OSType, sizeof(_OSType), PSD_RESOURCE);
	if(io->write_proc(_OSType, sizeof(_OSType), 1, handle) != 1) {
		return false;
	}
	psdSetValue(ShortValue, sizeof(ShortValue), _ID);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(ShortValue, sizeof(ShortValue), 0);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(IntValue, sizeof(IntValue), _Size);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}
	return true;
}

// --------------------------------------------------------------------------

psdResolutionInfo::psdResolutionInfo() : _widthUnit(-1), _heightUnit(-1), _hRes(-1), _vRes(-1), _hResUnit(-1), _vResUnit(-1) {
}

psdResolutionInfo::~psdResolutionInfo() {
}

int psdResolutionInfo::Read(FreeImageIO *io, fi_handle handle) {
	BYTE IntValue[4], ShortValue[2];
	int nBytes=0, n;

	// Horizontal resolution in pixels per inch.
	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_hRes = (short)psdGetValue(ShortValue, sizeof(_hRes) );

	// 1=display horizontal resolution in pixels per inch; 2=display horizontal resolution in pixels per cm.
	n = (int)io->read_proc(IntValue, sizeof(IntValue), 1, handle);
	nBytes += n * sizeof(IntValue);
	_hResUnit = psdGetValue(IntValue, sizeof(_hResUnit) );

	// Display width as 1=inches; 2=cm; 3=points; 4=picas; 5=columns.
	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_widthUnit = (short)psdGetValue(ShortValue, sizeof(_widthUnit) );

	// Vertical resolution in pixels per inch.
	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_vRes = (short)psdGetValue(ShortValue, sizeof(_vRes) );

	// 1=display vertical resolution in pixels per inch; 2=display vertical resolution in pixels per cm.
	n = (int)io->read_proc(IntValue, sizeof(IntValue), 1, handle);
	nBytes += n * sizeof(IntValue);
	_vResUnit = psdGetValue(IntValue, sizeof(_vResUnit) );

	// Display height as 1=inches; 2=cm; 3=points; 4=picas; 5=columns.
	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_heightUnit = (short)psdGetValue(ShortValue, sizeof(_heightUnit) );

	return nBytes;
}

bool psdResolutionInfo::Write(FreeImageIO *io, fi_handle handle) {
	BYTE IntValue[4], ShortValue[2];

	if (!psdImageResource().Write(io, handle, PSDP_RES_RESOLUTION_INFO, 16)) {
		return false;
	}

	// Horizontal resolution in pixels per inch.
	psdSetValue(ShortValue, sizeof(ShortValue), _hRes);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}

	// 1=display horizontal resolution in pixels per inch; 2=display horizontal resolution in pixels per cm.
	psdSetValue(IntValue, sizeof(IntValue), _hResUnit);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}

	// Display width as 1=inches; 2=cm; 3=points; 4=picas; 5=columns.
	psdSetValue(ShortValue, sizeof(ShortValue), _widthUnit);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}

	// Vertical resolution in pixels per inch.
	psdSetValue(ShortValue, sizeof(ShortValue), _vRes);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}

	// 1=display vertical resolution in pixels per inch; 2=display vertical resolution in pixels per cm.
	psdSetValue(IntValue, sizeof(IntValue), _vResUnit);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}

	// Display height as 1=inches; 2=cm; 3=points; 4=picas; 5=columns.
	psdSetValue(ShortValue, sizeof(ShortValue), _heightUnit);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}

	return true;
}

void psdResolutionInfo::GetResolutionInfo(unsigned &res_x, unsigned &res_y) {
	if(_hResUnit == 1) {
		// convert pixels / inch to pixel / m
		res_x = (unsigned) (_hRes / 0.0254000 + 0.5);
	} else if(_hResUnit == 2) {
		// convert pixels / cm to pixel / m
		res_x = (unsigned) (_hRes * 100.0 + 0.5);
	}

	if(_vResUnit == 1) {
		// convert pixels / inch to pixel / m
		res_y = (unsigned) (_vRes / 0.0254000 + 0.5);
	} else if(_vResUnit == 2) {
		// convert pixels / cm to pixel / m
		res_y = (unsigned) (_vRes * 100.0 + 0.5);
	}
}

// --------------------------------------------------------------------------

psdResolutionInfo_v2::psdResolutionInfo_v2() {
	_Channels = _Rows = _Columns = _Depth = _Mode = -1;
}

psdResolutionInfo_v2::~psdResolutionInfo_v2() {
}

int psdResolutionInfo_v2::Read(FreeImageIO *io, fi_handle handle) {
	BYTE ShortValue[2];
	int nBytes=0, n;

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Channels = (short)psdGetValue(ShortValue, sizeof(_Channels) );

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Rows = (short)psdGetValue(ShortValue, sizeof(_Rows) );

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Columns = (short)psdGetValue(ShortValue, sizeof(_Columns) );

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Depth = (short)psdGetValue(ShortValue, sizeof(_Depth) );

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Mode = (short)psdGetValue(ShortValue, sizeof(_Mode) );

	return nBytes;
}

bool psdResolutionInfo_v2::Write(FreeImageIO *io, fi_handle handle) {
	BYTE ShortValue[2];

	if(!psdImageResource().Write(io, handle, PSDP_RES_RESOLUTION_INFO_V2, 10))
		return false;

	psdSetValue(ShortValue, sizeof(ShortValue), _Channels);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(ShortValue, sizeof(ShortValue), _Rows);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(ShortValue, sizeof(ShortValue), _Columns);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(ShortValue, sizeof(ShortValue), _Depth);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(ShortValue, sizeof(ShortValue), _Mode);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}

	return true;
}

// --------------------------------------------------------------------------

psdDisplayInfo::psdDisplayInfo() {
	_Opacity = _ColourSpace = -1;
	for (unsigned n = 0; n < 4; ++n) {
		_Colour[n] = 0;
	}
	_Kind = 0;
	_padding = '0';
}

psdDisplayInfo::~psdDisplayInfo() {
}

int psdDisplayInfo::Read(FreeImageIO *io, fi_handle handle) {
	BYTE ShortValue[2];
	int nBytes=0, n;

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_ColourSpace = (short)psdGetValue(ShortValue, sizeof(_ColourSpace) );

	for (unsigned i = 0; i < 4; ++i) {
		n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
		nBytes += n * sizeof(ShortValue);
		_Colour[i] = (short)psdGetValue(ShortValue, sizeof(_Colour[i]) );
	}

	n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	nBytes += n * sizeof(ShortValue);
	_Opacity = (short)psdGetValue(ShortValue, sizeof(_Opacity) );
	if((_Opacity < 0) || (_Opacity > 100)) {
		throw "Invalid DisplayInfo::Opacity value";
	}

	BYTE c[1];
	n = (int)io->read_proc(c, sizeof(c), 1, handle);
	nBytes += n * sizeof(c);
	_Kind = (BYTE)psdGetValue(c, sizeof(c));

	n = (int)io->read_proc(c, sizeof(c), 1, handle);
	nBytes += n * sizeof(c);

	_padding = (BYTE)psdGetValue(c, sizeof(c));
	if(_padding != 0) {
		throw "Invalid DisplayInfo::Padding value";
	}

	return nBytes;
}

bool psdDisplayInfo::Write(FreeImageIO *io, fi_handle handle) {
	BYTE ShortValue[2];

	if(!psdImageResource().Write(io, handle, PSDP_RES_DISPLAY_INFO, 14))
		return false;

	psdSetValue(ShortValue, sizeof(ShortValue), _ColourSpace);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}
	for (unsigned i = 0; i < 4; ++i) {
		psdSetValue(ShortValue, sizeof(ShortValue), _Colour[i]);
		if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
			return false;
		}
	}
	psdSetValue(ShortValue, sizeof(ShortValue), _Opacity);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}
	BYTE c[1];
	psdSetValue(c, sizeof(c), _Kind);
	if(io->write_proc(c, sizeof(c), 1, handle) != 1) {
		return false;
	}
	psdSetValue(c, sizeof(c), 0);
	if(io->write_proc(c, sizeof(c), 1, handle) != 1) {
		return false;
	}

	return true;
}

// --------------------------------------------------------------------------

psdThumbnail::psdThumbnail() :
_Format(-1), _Width(-1), _Height(-1), _WidthBytes(-1), _Size(-1), _CompressedSize(-1), _BitPerPixel(-1), _Planes(-1), _dib(NULL), _owned(true) {
}

psdThumbnail::~psdThumbnail() {
	if (_owned) FreeImage_Unload(_dib);
}

void
psdThumbnail::Init() {
	if (_dib != NULL) {
		_Format = 1;
		_Width = (int)FreeImage_GetWidth(_dib);
		_Height = (int)FreeImage_GetHeight(_dib);
		_BitPerPixel = 24;
		_Planes = 1;
		_WidthBytes = (_Width * _BitPerPixel + 31) / 32 * 4;
		_Size = _WidthBytes * _Height * _Planes;
		_CompressedSize = _Size;
	}
}

int psdThumbnail::Read(FreeImageIO *io, fi_handle handle, int iResourceSize, bool isBGR) {
	BYTE ShortValue[2] = { 0 }, IntValue[4] = { 0 };

	// the thumbnail data follows a 28-byte header
	const int iTotalData = iResourceSize - 28;
	const long block_end = io->tell_proc(handle) + iResourceSize;

	io->read_proc(IntValue, sizeof(IntValue), 1, handle);
	_Format = psdGetValue(IntValue, sizeof(_Format) );

	io->read_proc(IntValue, sizeof(IntValue), 1, handle);
	_Width = psdGetValue(IntValue, sizeof(_Width) );

	io->read_proc(IntValue, sizeof(IntValue), 1, handle);
	_Height = psdGetValue(IntValue, sizeof(_Height) );

	io->read_proc(IntValue, sizeof(IntValue), 1, handle);
	_WidthBytes = psdGetValue(IntValue, sizeof(_WidthBytes) );

	io->read_proc(IntValue, sizeof(IntValue), 1, handle);
	_Size = psdGetValue(IntValue, sizeof(_Size) );

	io->read_proc(IntValue, sizeof(IntValue), 1, handle);
	_CompressedSize = psdGetValue(IntValue, sizeof(_CompressedSize) );

	io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	_BitPerPixel = (short)psdGetValue(ShortValue, sizeof(_BitPerPixel) );

	io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
	_Planes = (short)psdGetValue(ShortValue, sizeof(_Planes) );

	const long data_start = io->tell_proc(handle);

	if(_dib) {
		FreeImage_Unload(_dib);
		_dib = NULL;
	}

	// raw rows must fit in the resource and in the file
	io->seek_proc(handle, 0, SEEK_END);
	const INT64 avail = MIN((INT64)iTotalData, (INT64)(io->tell_proc(handle) - data_start));
	io->seek_proc(handle, data_start, SEEK_SET);

	if(_Format == 1) {
		// kJpegRGB thumbnail image
		_dib = FreeImage_LoadFromHandle(FIF_JPEG, io, handle);
		if(isBGR) {
			SwapRedBlue32(_dib);
		}
	}
	else if((_Format == 0) && (_BitPerPixel == 24) && (_Width > 0) && (_Height > 0)
		&& ((INT64)_WidthBytes >= (INT64)_Width * 3) && ((INT64)_WidthBytes * _Height <= avail)) {
		// kRawRGB thumbnail image
		_dib = FreeImage_Allocate(_Width, _Height, 24);
		if(_dib) {
			BYTE* dst_line_start = FreeImage_GetScanLine(_dib, _Height - 1);//<*** flipped
			std::vector<BYTE> line_start(_WidthBytes);
			const unsigned dstLineSize = FreeImage_GetPitch(_dib);
			for(unsigned h = 0; h < (unsigned)_Height; ++h, dst_line_start -= dstLineSize) {//<*** flipped
				io->read_proc(&line_start[0], _WidthBytes, 1, handle);
				memcpy(dst_line_start, &line_start[0], _Width * 3);
			}
#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_BGR
			SwapRedBlue32(_dib);
#endif
		}
	}
	// any other thumbnail is skipped, the image loads without it

	// the JPEG decoder reads ahead, so always resume at the end of the resource
	io->seek_proc(handle, block_end, SEEK_SET);

	return iResourceSize;
}

bool psdThumbnail::Write(FreeImageIO *io, fi_handle handle, bool isBGR) {
	BYTE ShortValue[2], IntValue[4];

	const long res_start_pos = io->tell_proc(handle);
	const int ID = isBGR ? PSDP_RES_THUMBNAIL_PS4 : PSDP_RES_THUMBNAIL;
	if(!psdImageResource().Write(io, handle, ID, 0))
		return false;

	psdSetValue(IntValue, sizeof(IntValue), _Format);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(IntValue, sizeof(IntValue), _Width);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(IntValue, sizeof(IntValue), _Height);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(IntValue, sizeof(IntValue), _WidthBytes);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(IntValue, sizeof(IntValue), _Size);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}
	const long compressed_pos = io->tell_proc(handle);
	psdSetValue(IntValue, sizeof(IntValue), _CompressedSize);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(ShortValue, sizeof(ShortValue), _BitPerPixel);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}
	psdSetValue(ShortValue, sizeof(ShortValue), _Planes);
	if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
		return false;
	}
	if(_Format == 1) {
		// kJpegRGB thumbnail image
		if(isBGR) {
			SwapRedBlue32(_dib);
		}
		const long start_pos = io->tell_proc(handle);
		FreeImage_SaveToHandle(FIF_JPEG, _dib, io, handle, JPEG_DEFAULT);
		const long current_pos = io->tell_proc(handle);
		_CompressedSize = (int)(current_pos - start_pos);
		io->seek_proc(handle, compressed_pos, SEEK_SET);
		psdSetValue(IntValue, sizeof(IntValue), _CompressedSize);
		if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
			return false;
		}
		io->seek_proc(handle, current_pos, SEEK_SET);
	}
	else {
		// kRawRGB thumbnail image
		// ### Unimplemented (should be trivial)
		_CompressedSize = 0;
	}

	int len = 28 + _CompressedSize;

	// Fix length of resource
	io->seek_proc(handle, res_start_pos + 8, SEEK_SET);
	psdSetValue(IntValue, sizeof(IntValue), len);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}
	io->seek_proc(handle, 0, SEEK_END);

	if((len % 2) != 0) {
		BYTE data[1];
		data[0] = 0;
		if(io->write_proc(data, sizeof(data), 1, handle) != 1) {
			return false;
		}
	}

	return true;
}

//---------------------------------------------------------------------------

psdICCProfile::psdICCProfile() : _ProfileSize(0), _ProfileData(NULL), _owned(true) {
}

psdICCProfile::~psdICCProfile() {
	clear();
}

void psdICCProfile::clear() { if (_owned) { SAFE_DELETE_ARRAY(_ProfileData); } else { _ProfileData = NULL; } _ProfileSize = 0;}

int psdICCProfile::Read(FreeImageIO *io, fi_handle handle, int size) {
	int nBytes = 0, n;

	clear();

	_ProfileData = new (std::nothrow) BYTE[size];
	if(NULL != _ProfileData) {
		n = (int)io->read_proc(_ProfileData, 1, size, handle);
		_ProfileSize = size;
		nBytes += n * sizeof(BYTE);
	}

	return nBytes;
}

bool psdICCProfile::Write(FreeImageIO *io, fi_handle handle) {
	if(!psdImageResource().Write(io, handle, PSDP_RES_ICC_PROFILE, _ProfileSize))
		return false;

	if(NULL != _ProfileData) {
		if(io->write_proc(_ProfileData, 1, _ProfileSize, handle) != _ProfileSize) {
			return false;
		}
		if((_ProfileSize % 2) != 0) {
			BYTE data[1];
			data[0] = 0;
			if(io->write_proc(data, sizeof(data), 1, handle) != 1) {
				return false;
			}
		}
	}

	return true;
}

//---------------------------------------------------------------------------

psdData::psdData() : _Size(0), _Data(NULL), _owned(true) {
}

psdData::~psdData() {
	clear();
}

void psdData::clear() { if (_owned) { SAFE_DELETE_ARRAY(_Data); } else { _Data = NULL; } _Size = 0;}

int psdData::Read(FreeImageIO *io, fi_handle handle, int size) {
	int nBytes = 0, n;

	clear();

	_Data = new (std::nothrow) BYTE[size];
	if(NULL != _Data) {
		n = (int)io->read_proc(_Data, 1, size, handle);
		_Size = (unsigned)size;
		nBytes += n * sizeof(BYTE);
	}

	return nBytes;
}

bool psdData::Write(FreeImageIO *io, fi_handle handle, int ID) {
	if(!psdImageResource().Write(io, handle, ID, _Size))
		return false;

	if(NULL != _Data) {
		if(io->write_proc(_Data, 1, _Size, handle) != _Size) {
			return false;
		}
		if((_Size % 2) != 0) {
			BYTE data[1];
			data[0] = 0;
			if(io->write_proc(data, sizeof(data), 1, handle) != 1) {
				return false;
			}
		}
	}

	return true;
}

//---------------------------------------------------------------------------

/**
Invert only color components, skipping Alpha/Black
(Can be useful as public/utility function)
*/
static
BOOL invertColor(FIBITMAP* dib) {
	FREE_IMAGE_TYPE type = FreeImage_GetImageType(dib);
	const unsigned Bpp = FreeImage_GetBPP(dib)/8;

	if((type == FIT_BITMAP && Bpp == 4) || type == FIT_RGBA16) {
		const unsigned width = FreeImage_GetWidth(dib);
		const unsigned height = FreeImage_GetHeight(dib);
		BYTE *line_start = FreeImage_GetScanLine(dib, 0);
		const unsigned pitch = FreeImage_GetPitch(dib);
		const unsigned triBpp = Bpp - (Bpp == 4 ? 1 : 2);

		for(unsigned y = 0; y < height; y++) {
			BYTE *line = line_start;

			for(unsigned x = 0; x < width; x++) {
				for(unsigned b=0; b < triBpp; ++b) {
					line[b] = ~line[b];
				}

				line += Bpp;
			}
			line_start += pitch;
		}

		return TRUE;
	}
	else {
		return FreeImage_Invert(dib);
	}
}

/**
Invert every channel, Black included (FreeImage_Invert leaves the fourth one alone)
*/
static
void invertCMYK(FIBITMAP* dib) {
	const FREE_IMAGE_TYPE type = FreeImage_GetImageType(dib);
	if((type != FIT_BITMAP) && (type != FIT_RGB16) && (type != FIT_RGBA16)) {
		return;
	}
	const unsigned lineSize = FreeImage_GetLine(dib);
	for(unsigned y = 0; y < FreeImage_GetHeight(dib); y++) {
		BYTE *line = FreeImage_GetScanLine(dib, y);
		for(unsigned x = 0; x < lineSize; x++) {
			line[x] = ~line[x];
		}
	}
}

//---------------------------------------------------------------------------

psdParser::psdParser() {
	_bThumbnailFilled = false;
	_bDisplayInfoFilled = false;
	_bResolutionInfoFilled = false;
	_bResolutionInfoFilled_v2 = false;
	_bCopyright = false;
	_GlobalAngle = 30;
	_ColourCount = -1;
	_TransparentIndex = -1;
	_fi_flags = 0;
	_fi_format_id = FIF_UNKNOWN;
}

psdParser::~psdParser() {
}

unsigned psdParser::GetChannelOffset(FIBITMAP* bitmap, unsigned c) const {
	unsigned channelOffset = c;
#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_BGR
	// Swap R/B indices for BGR -> RGB
	if(FreeImage_GetImageType(bitmap) == FIT_BITMAP &&
	   _headerInfo._ColourMode == PSDP_RGB &&
	   (c == 0 || c == 2)) {
		channelOffset = (2 - c);
	}
#endif
	return channelOffset;
}

bool psdParser::ReadLayerAndMaskInfoSection(FreeImageIO *io, fi_handle handle)	{
	bool bSuccess = true;

	UINT64 nTotalBytes = psdReadSize(io, handle, _headerInfo);

	// Hack to handle large PSB files without using fseeko().
	if (sizeof(long) < sizeof(UINT64)) {
		const long offset = 0x10000000;
		while (nTotalBytes > offset) {
			if (io->seek_proc(handle, offset, SEEK_CUR) != 0) {
				bSuccess = false;
				break;
			}
			nTotalBytes -= offset;
		}
	}
	if (bSuccess && nTotalBytes > 0) {
		if (io->seek_proc(handle, (long)nTotalBytes, SEEK_CUR) != 0)
			bSuccess = false;
	}

	return bSuccess;
}

bool psdParser::ReadImageResources(FreeImageIO *io, fi_handle handle, LONG length) {
	psdImageResource oResource;
	bool bSuccess = false;

	if(length > 0) {
		oResource._Length = length;
	} else {
		BYTE Length[4];
		int n = (int)io->read_proc(Length, sizeof(Length), 1, handle);

		oResource._Length = psdGetValue( Length, sizeof(oResource._Length) );
	}

	int nBytes = 0;
	int nTotalBytes = oResource._Length;

	while(nBytes < nTotalBytes) {
		int n = 0;
		oResource.Reset();

		n = (int)io->read_proc(oResource._OSType, sizeof(oResource._OSType), 1, handle);
		if(n != 1) {
			FreeImage_OutputMessageProc(_fi_format_id, "This file contains damaged data causing an unexpected end-of-file - stop reading resources");
			return false;
		}
		nBytes += n * sizeof(oResource._OSType);

		if( (nBytes % 2) != 0 ) {
			return false;
		}

		int nOSType = psdGetValue((BYTE*)&oResource._OSType, sizeof(oResource._OSType));

		if ( PSD_RESOURCE == nOSType ) {
			BYTE ID[2];
			n = (int)io->read_proc(ID, sizeof(ID), 1, handle);
			nBytes += n * sizeof(ID);

			oResource._ID = (short)psdGetValue( ID, sizeof(ID) );

			BYTE SizeOfName;
			n = (int)io->read_proc(&SizeOfName, sizeof(SizeOfName), 1, handle);
			nBytes += n * sizeof(SizeOfName);

			int nSizeOfName = psdGetValue( &SizeOfName, sizeof(SizeOfName) );
			if ( 0 < nSizeOfName ) {
				oResource._plName = new BYTE[nSizeOfName];
				n = (int)io->read_proc(oResource._plName, nSizeOfName, 1, handle);
				nBytes += n * nSizeOfName;
			}

			if ( 0 == (nSizeOfName % 2) ) {
				n = (int)io->read_proc(&SizeOfName, sizeof(SizeOfName), 1, handle);
				nBytes += n * sizeof(SizeOfName);
			}

			BYTE Size[4];
			n = (int)io->read_proc(Size, sizeof(Size), 1, handle);
			nBytes += n * sizeof(Size);

			oResource._Size = psdGetValue( Size, sizeof(oResource._Size) );

			if ( 0 != (oResource._Size % 2) ) {
				// resource data must be even
				oResource._Size++;
			}
			if ( 0 < oResource._Size ) {
				BYTE IntValue[4];
				BYTE ShortValue[2];

				switch( oResource._ID ) {
					case PSDP_RES_RESOLUTION_INFO_V2:
						// Obsolete - Photoshop 2.0
						_bResolutionInfoFilled_v2 = true;
						nBytes += _resolutionInfo_v2.Read(io, handle);
						break;

					// ResolutionInfo structure
					case PSDP_RES_RESOLUTION_INFO:
						_bResolutionInfoFilled = true;
						nBytes += _resolutionInfo.Read(io, handle);
						break;

					// DisplayInfo structure
					case PSDP_RES_DISPLAY_INFO:
						_bDisplayInfoFilled = true;
						nBytes += _displayInfo.Read(io, handle);
						break;

					// IPTC-NAA record
					case PSDP_RES_IPTC_NAA:
						nBytes += _iptc.Read(io, handle, oResource._Size);
						break;
					// (Photoshop 4.0) Copyright flag
					// Boolean indicating whether image is copyrighted. Can be set via Property suite or by user in File Info...
					case PSDP_RES_COPYRIGHT:
						n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
						nBytes += n * sizeof(ShortValue);
						_bCopyright = (1 == psdGetValue(ShortValue, sizeof(ShortValue)));
						break;

					// (Photoshop 4.0) Thumbnail resource for Photoshop 4.0 only
					case PSDP_RES_THUMBNAIL_PS4:
					// (Photoshop 5.0) Thumbnail resource (supersedes resource 1033)
					case PSDP_RES_THUMBNAIL:
					{
						_bThumbnailFilled = true;
						bool bBGR = (PSDP_RES_THUMBNAIL_PS4 == oResource._ID);
						nBytes += _thumbnail.Read(io, handle, oResource._Size, bBGR);
						break;
					}

					// (Photoshop 5.0) Global Angle
					// 4 bytes that contain an integer between 0 and 359, which is the global
					// lighting angle for effects layer. If not present, assumed to be 30.
					case PSDP_RES_GLOBAL_ANGLE:
						n = (int)io->read_proc(IntValue, sizeof(IntValue), 1, handle);
						nBytes += n * sizeof(IntValue);
						_GlobalAngle = psdGetValue(IntValue, sizeof(_GlobalAngle) );
						break;

					// ICC profile
					case PSDP_RES_ICC_PROFILE:
						nBytes += _iccProfile.Read(io, handle, oResource._Size);
						break;

					// (Photoshop 6.0) Indexed Color Table Count
					// 2 bytes for the number of colors in table that are actually defined
					case PSDP_RES_INDEXED_COLORS:
						n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
						nBytes += n * sizeof(ShortValue);
						_ColourCount = (short)psdGetValue(ShortValue, sizeof(ShortValue) );
						break;

					// (Photoshop 6.0) Transparency Index.
					// 2 bytes for the index of transparent color, if any.
					case PSDP_RES_TRANSPARENCY_INDEX:
						n = (int)io->read_proc(ShortValue, sizeof(ShortValue), 1, handle);
						nBytes += n * sizeof(ShortValue);
						_TransparentIndex = (short)psdGetValue(ShortValue, sizeof(ShortValue) );
						break;

					// (Photoshop 7.0) EXIF data 1
					case PSDP_RES_EXIF1:
						nBytes += _exif1.Read(io, handle, oResource._Size);
						break;
					// (Photoshop 7.0) EXIF data 3
					case PSDP_RES_EXIF3:
						nBytes += _exif3.Read(io, handle, oResource._Size);
						break;
					// (Photoshop 7.0) XMP metadata
					case PSDP_RES_XMP:
						nBytes += _xmp.Read(io, handle, oResource._Size);
						break;
					default:
					{
						// skip resource
						unsigned skip_length = MIN(oResource._Size, nTotalBytes - nBytes);
						io->seek_proc(handle, skip_length, SEEK_CUR);
						nBytes += skip_length;
					}
					break;
				}
			}
		}
  }

  if (nBytes == nTotalBytes) {
	  bSuccess = true;
  }

  return bSuccess;

}

// dst_line_end ends this scanline; lineSize counts source bytes
void psdParser::ReadImageLine(BYTE* dst, const BYTE* src, unsigned lineSize, unsigned dstBpp, unsigned bytes, const BYTE* dst_line_end) {
	switch (bytes) {
		case 4:
		{
			DWORD* d = (DWORD*)dst;
			const DWORD* s = (const DWORD*)src;
			dstBpp /= 4;
			while (lineSize > 0 && (const BYTE*)(d + 1) <= dst_line_end) {
				DWORD v = *s++;
#ifndef FREEIMAGE_BIGENDIAN
				SwapLong(&v);
#endif
				*d = v;
				d += dstBpp;
				lineSize -= 4;
			}
			break;
		}
		case 2:
		{
			WORD* d = (WORD*)dst;
			const WORD* s = (const WORD*)src;
			dstBpp /= 2;
			while (lineSize > 0 && (const BYTE*)(d + 1) <= dst_line_end) {
				WORD v = *s++;
#ifndef FREEIMAGE_BIGENDIAN
				SwapShort(&v);
#endif
				*d = v;
				d += dstBpp;
				lineSize -= 2;
			}
			break;
		}
		default:
			if (dstBpp == 1) {
				const unsigned room = (unsigned)(dst_line_end - dst);
				memcpy(dst, src, MIN(lineSize, room));
			} else {
				while (lineSize > 0 && dst < dst_line_end) {
					*dst = *src++;
					dst += dstBpp;
					lineSize--;
				}
			}
			break;
	}
}

void psdParser::UnpackRLE(BYTE* line, const BYTE* rle_line, BYTE* line_end, unsigned srcSize) {
	while (srcSize > 0 && line < line_end) {

		int len = *rle_line++;
		srcSize--;

		// NOTE len is signed byte in PackBits RLE

		if ( len < 128 ) { //<- MSB is not set
			// uncompressed packet

			// (len + 1) bytes of data are copied
			++len;

			const unsigned taken = (srcSize < (unsigned)len) ? srcSize : (unsigned)len;
			const size_t room = (size_t)(line_end - line);
			const size_t written = (taken < room) ? (size_t)taken : room;

			memcpy(line, rle_line, written);
			line += written;
			rle_line += taken;
			srcSize -= taken;
		}
		else if ( len > 128 ) { //< MSB is set
			// RLE compressed packet

			// One byte of data is repeated (–len + 1) times

			len ^= 0xFF; // same as (-len + 1) & 0xFF
			len += 2;    //

			if (srcSize < 1) {
				break;
			}

			const size_t room = (size_t)(line_end - line);
			const size_t written = ((size_t)len < room) ? (size_t)len : room;

			memset(line, *rle_line++, written);
			line += written;
			srcSize--;
		}
		else if ( 128 == len ) {
			// Do nothing
		}
	}//< rle_line

	// packets that stop short leave the rest of the line black, not stale
	if (line < line_end) {
		memset(line, 0, (size_t)(line_end - line));
	}
}

FIBITMAP* psdParser::ReadImageData(FreeImageIO *io, fi_handle handle) {
	if (handle == NULL) {
		return NULL;
	}

	bool header_only = (_fi_flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

	WORD nCompression = 0;
	if (io->read_proc(&nCompression, sizeof(nCompression), 1, handle) != 1) {
		return NULL;
	}

#ifndef FREEIMAGE_BIGENDIAN
	SwapShort(&nCompression);
#endif

	// PSDP_COMPRESSION_ZIP and PSDP_COMPRESSION_ZIP_PREDICTION
	// are only valid for layer data, not the composited data.
	if(nCompression != PSDP_COMPRESSION_NONE &&
	   nCompression != PSDP_COMPRESSION_RLE) {
		FreeImage_OutputMessageProc(_fi_format_id, "Unsupported compression %d", nCompression);
		return NULL;
	}

	const unsigned nWidth = _headerInfo._Width;
	const unsigned nHeight = _headerInfo._Height;
	const unsigned nChannels = _headerInfo._Channels;
	const unsigned depth = _headerInfo._BitsPerChannel;
	const unsigned bytes = (depth == 1) ? 1 : depth / 8;

	// channel(plane) line (BYTE aligned)
	const unsigned lineSize = (_headerInfo._BitsPerChannel == 1) ? (nWidth + 7) / 8 : nWidth * bytes;

	// A PSD image can have up to 56 channels. 
	if (nChannels > 56) {
		FreeImage_OutputMessageProc(_fi_format_id, "Unsupported number of channels %d", nChannels);
		return NULL;
	}

	if(nCompression == PSDP_COMPRESSION_RLE && depth > 16) {
		FreeImage_OutputMessageProc(_fi_format_id, "Unsupported RLE with depth %d", depth);
		return NULL;
	}

	// build output buffer

	FIBITMAP* bitmap = NULL;
	unsigned dstCh = 0;

	short mode = _headerInfo._ColourMode;

	if(mode == PSDP_MULTICHANNEL && nChannels < 3) {
		// CM
		mode = PSDP_GRAYSCALE; // C as gray, M as extra channel
	}

	bool needPalette = false;
	switch (mode) {
		case PSDP_BITMAP:
		case PSDP_DUOTONE:
		case PSDP_INDEXED:
		case PSDP_GRAYSCALE:
			dstCh = 1;
			switch(depth) {
				case 16:
				bitmap = FreeImage_AllocateHeaderT(header_only, FIT_UINT16, nWidth, nHeight, depth*dstCh);
				break;
				case 32:
				bitmap = FreeImage_AllocateHeaderT(header_only, FIT_FLOAT, nWidth, nHeight, depth*dstCh);
				break;
				default: // 1-, 8-
				needPalette = true;
				bitmap = FreeImage_AllocateHeader(header_only, nWidth, nHeight, depth*dstCh);
				break;
			}
			break;
		case PSDP_RGB:
		case PSDP_LAB:
		case PSDP_CMYK	:
		case PSDP_MULTICHANNEL	:
			// force PSDP_MULTICHANNEL CMY as CMYK
			dstCh = (mode == PSDP_MULTICHANNEL && !header_only) ? 4 : MIN<unsigned>(nChannels, 4);
			if(dstCh < 3) {
				throw "Invalid number of channels";
			}

			switch(depth) {
				case 16:
				bitmap = FreeImage_AllocateHeaderT(header_only, dstCh < 4 ? FIT_RGB16 : FIT_RGBA16, nWidth, nHeight, depth*dstCh);
				break;
				case 32:
				bitmap = FreeImage_AllocateHeaderT(header_only, dstCh < 4 ? FIT_RGBF : FIT_RGBAF, nWidth, nHeight, depth*dstCh);
				break;
				default:
				bitmap = FreeImage_AllocateHeader(header_only, nWidth, nHeight, depth*dstCh);
				break;
			}
			break;
		default:
			throw "Unsupported color mode";
			break;
	}
	if(!bitmap) {
		throw FI_MSG_ERROR_DIB_MEMORY;
	}

	// write thumbnail
	FreeImage_SetThumbnail(bitmap, _thumbnail.getDib());

	// @todo Add some metadata model

	if(header_only) {
		return bitmap;
	}

	// Load pixels data

	const unsigned dstChannels = dstCh;

	const unsigned dstBpp =  (depth == 1) ? 1 : FreeImage_GetBPP(bitmap)/8;
	const unsigned dstLineSize = FreeImage_GetPitch(bitmap);
	BYTE* const dst_first_line = FreeImage_GetScanLine(bitmap, nHeight - 1);//<*** flipped
	BYTE* line_start = new BYTE[lineSize]; //< fileline cache
   const unsigned dst_buffer_size = dstLineSize * nHeight;

	switch ( nCompression ) {
		case PSDP_COMPRESSION_NONE: // raw data
		{
			for(unsigned c = 0; c < nChannels; c++) {
				if(c >= dstChannels) {
					// @todo write extra channels
					break;
				}

				const unsigned channelOffset = GetChannelOffset(bitmap, c) * bytes;
				const unsigned limitLineSize = MIN(dstLineSize, lineSize);

				BYTE* dst_line_start = dst_first_line + channelOffset;
				if (channelOffset + lineSize > dst_buffer_size) {
					// Fix for CVE-2020-24295 from https://src.fedoraproject.org/rpms/freeimage/blob/f39/f/CVE-2020-24295.patch 
					throw "Invalid PSD image";
				}

				for(unsigned h = 0; h < nHeight; ++h, dst_line_start -= dstLineSize) {//<*** flipped
					const unsigned got = io->read_proc(line_start, 1, lineSize, handle);
					memset(line_start + got, 0, lineSize - got);
					ReadImageLine(dst_line_start, line_start, limitLineSize, dstBpp, bytes,
					              dst_line_start - channelOffset + dstLineSize);
				} //< h
			}//< ch

			SAFE_DELETE_ARRAY(line_start);

		}
		break;

		case PSDP_COMPRESSION_RLE: // RLE compression
		{

			// The RLE-compressed data is preceeded by a 2-byte line size for each row in the data,
			// store an array of these
			// Version 2 has 4-byte line sizes.

			// later use this array as DWORD rleLineSizeList[nChannels][nHeight];
			DWORD *rleLineSizeList = new (std::nothrow) DWORD[nChannels*nHeight];

			if(!rleLineSizeList) {
				FreeImage_Unload(bitmap);
				SAFE_DELETE_ARRAY(line_start);
				throw std::bad_alloc();
			}
			if(_headerInfo._Version == 1) {
				WORD *rleLineSizeList2 = new (std::nothrow) WORD[nChannels*nHeight];
				if(!rleLineSizeList2) {
					FreeImage_Unload(bitmap);
					SAFE_DELETE_ARRAY(line_start);
					throw std::bad_alloc();
				}
				// counts past the end of the file are 0
				const unsigned got = io->read_proc(rleLineSizeList2, 2, nChannels * nHeight, handle);
				memset(rleLineSizeList2 + got, 0, (nChannels * nHeight - got) * sizeof(WORD));
				for(unsigned index = 0; index < nChannels * nHeight; ++index) {
#ifndef FREEIMAGE_BIGENDIAN
					SwapShort(&rleLineSizeList2[index]);
#endif
					rleLineSizeList[index] = rleLineSizeList2[index];
				}
				SAFE_DELETE_ARRAY(rleLineSizeList2);
			} else {
				const unsigned got = io->read_proc(rleLineSizeList, 4, nChannels * nHeight, handle);
				memset(rleLineSizeList + got, 0, (nChannels * nHeight - got) * sizeof(DWORD));
#ifndef FREEIMAGE_BIGENDIAN
				for(unsigned index = 0; index < nChannels * nHeight; ++index) {
					SwapLong(&rleLineSizeList[index]);
				}
#endif
			}

			DWORD largestRLELine = 0;
			for(unsigned ch = 0; ch < nChannels; ++ch) {
				for(unsigned h = 0; h < nHeight; ++h) {
					const unsigned index = ch * nHeight + h;

					if(largestRLELine < rleLineSizeList[index]) {
						largestRLELine = rleLineSizeList[index];
					}
				}
			}

			BYTE* rle_line_start = new (std::nothrow) BYTE[largestRLELine];
			if(!rle_line_start) {
				FreeImage_Unload(bitmap);
				SAFE_DELETE_ARRAY(line_start);
				SAFE_DELETE_ARRAY(rleLineSizeList);
				throw std::bad_alloc();
			}

			// Read the RLE data
			for (unsigned ch = 0; ch < nChannels; ch++) {
				if(ch >= dstChannels) {
					// @todo write to extra channels
					break;
				}
				const BYTE* const line_end = line_start + lineSize;

				const unsigned channelOffset = GetChannelOffset(bitmap, ch) * bytes;

				BYTE* dst_line_start = dst_first_line + channelOffset;
				for(unsigned h = 0; h < nHeight; ++h, dst_line_start -= dstLineSize) {//<*** flipped
					const unsigned index = ch * nHeight + h;

					// - read and uncompress line -

					// only the bytes the file holds are unpacked
					const unsigned rleLineSize = io->read_proc(rle_line_start, 1, rleLineSizeList[index], handle);

					// - write line to destination -

					UnpackRLE(line_start, rle_line_start, line_start + lineSize, rleLineSize);
					ReadImageLine(dst_line_start, line_start, lineSize, dstBpp, bytes,
					              dst_line_start - channelOffset + dstLineSize);
				}//< h
			}//< ch

			SAFE_DELETE_ARRAY(line_start);
			SAFE_DELETE_ARRAY(rleLineSizeList);
			SAFE_DELETE_ARRAY(rle_line_start);
		}
		break;

		/*
		 * If layer data is ever supported, do something like this:
		 * Compressed size comes from layer info section.
		 * Prediction means unzip, then each pixel value is a delta from the previous pixel.
		 * Horizontally only.
		 */
		/*
		case PSDP_COMPRESSION_ZIP: // ZIP without prediction
		case PSDP_COMPRESSION_ZIP_PREDICTION: // ZIP with prediction
			{
				BYTE *compressed = NULL;
				size_t compressedSize = 0;
				BYTE *uncompressed = new (std::nothrow) BYTE[nHeight * lineSize];
				if(!uncompressed) {
					FreeImage_Unload(bitmap);
					SAFE_DELETE_ARRAY(line_start);
					throw std::bad_alloc();
				}
				DWORD size = FreeImage_ZLibGUnzip(uncompressed, nHeight * lineSize, compressed, compressedSize);
			}
			break;
		*/
		default: // Unknown format
			break;

	}

	// --- Further process the bitmap ---

	if((mode == PSDP_CMYK || mode == PSDP_MULTICHANNEL)) {
		// CMYK values are "inverted", invert them back

		if(mode == PSDP_MULTICHANNEL) {
			invertColor(bitmap);
		} else {
			invertCMYK(bitmap);
		}

		if((_fi_flags & PSD_CMYK) == PSD_CMYK) {
			// keep as CMYK

			if(mode == PSDP_MULTICHANNEL) {
				//### we force CMY to be CMYK, but CMY has no ICC.
				// Create empty profile and add the flag.
				FreeImage_CreateICCProfile(bitmap, NULL, 0);
				FreeImage_GetICCProfile(bitmap)->flags |= FIICC_COLOR_IS_CMYK;
			}
		}
		else {
			// convert to RGB

			ConvertCMYKtoRGBA(bitmap);

			// The ICC Profile is no longer valid
			_iccProfile.clear();

			// remove the pending A if not present in source
			if(nChannels == 4 || nChannels == 3 ) {
				FIBITMAP* t = RemoveAlphaChannel(bitmap);
				if(t) {
					FreeImage_Unload(bitmap);
					bitmap = t;
				} // else: silently fail
			}
		}
	}
	else if ( mode == PSDP_LAB && !((_fi_flags & PSD_LAB) == PSD_LAB)) {
		ConvertLABtoRGB(bitmap);
	}
	else {
		if (needPalette && FreeImage_GetPalette(bitmap)) {

			if(mode == PSDP_BITMAP) {
				CREATE_GREYSCALE_PALETTE_REVERSE(FreeImage_GetPalette(bitmap), 2);
			}
			else if(mode == PSDP_INDEXED) {
				if(!_colourModeData._plColourData || _colourModeData._Length != 768 || _ColourCount < 0) {
					FreeImage_OutputMessageProc(_fi_format_id, "Indexed image has no palette. Using the default grayscale one.");
				} else {
					_colourModeData.FillPalette(bitmap);
				}
			}
			// GRAYSCALE, DUOTONE - use default grayscale palette
		}
	}

	return bitmap;
}

bool psdParser::WriteLayerAndMaskInfoSection(FreeImageIO *io, fi_handle handle)	{
	// Short section with no layers.
	BYTE IntValue[4];

	UINT64 size;
	if(_headerInfo._Version == 1) {
		size = 8;
	} else {
		size = 12;
	}
	// Length of whole info.
	if(!psdWriteSize(io, handle, _headerInfo, size)) {
		return false;
	}
	// Length of layers info section.
	if(!psdWriteSize(io, handle, _headerInfo, 0)) {
		return false;
	}
	// Length of global layer mask info section.  Always 4 bytes.
	psdSetValue(IntValue, sizeof(IntValue), 0);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}
	// Additional layer information.
	return true;
}

void psdParser::WriteImageLine(BYTE* dst, const BYTE* src, unsigned lineSize, unsigned srcBpp, unsigned bytes) {
	switch (bytes) {
	case 4:
		{
			DWORD* d = (DWORD*)dst;
			const DWORD* s = (const DWORD*)src;
			srcBpp /= 4;
			while (lineSize > 0) {
				DWORD v = *s;
#ifndef FREEIMAGE_BIGENDIAN
				SwapLong(&v);
#endif
				*d++ = v;
				s += srcBpp;
				lineSize -= 4;
			}
			break;
		}
	case 2:
		{
			WORD* d = (WORD*)dst;
			const WORD* s = (const WORD*)src;
			srcBpp /= 2;
			while (lineSize > 0) {
				WORD v = *s;
#ifndef FREEIMAGE_BIGENDIAN
				SwapShort(&v);
#endif
				*d++ = v;
				s += srcBpp;
				lineSize -= 2;
			}
			break;
		}
	default:
		if (srcBpp == 1) {
			memcpy(dst, src, lineSize);
		} else {
			while (lineSize > 0) {
				*dst++ = *src;
				src += srcBpp;
				lineSize--;
			}
		}
		break;
	}
}

unsigned psdParser::PackRLE(BYTE* line_start, const BYTE* src_line, unsigned srcSize) {
	BYTE* line = line_start;
	while (srcSize > 0) {
		if(srcSize >= 2 && src_line[0] == src_line[1]) {
			int len = 2;
			while(len < 127 && len < (int)srcSize && src_line[0] == src_line[len])
				len++;
			*line++ = (BYTE)((-len + 1) & 0xFF);
			*line++ = src_line[0];
			src_line += len;
			srcSize -= len;
		} else {
			// uncompressed packet
			// (len + 1) bytes of data are copied
			int len = 1;
			while(len < 127 && len < (int)srcSize &&
				  (len+2 >= (int)srcSize || // check to switch to a run instead
				   src_line[len] != src_line[len+1] ||
				   src_line[len] != src_line[len+2]))
				len++;
			*line++ = (BYTE)(len - 1);
			for(int i=0; i < len; i++) {
				*line++ = *src_line;
				src_line++;
			}
			srcSize -= len;
		}
	}
	return (unsigned)(line - line_start);
}

bool psdParser::WriteImageData(FreeImageIO *io, fi_handle handle, FIBITMAP* dib) {
	if (handle == NULL) {
		return false;
	}

	// frees the inverted CMYK copy on every return
	struct DibHolder {
		FIBITMAP *dib;
		~DibHolder() { if(dib) FreeImage_Unload(dib); }
	} cmyk = { NULL };

	if (_headerInfo._ColourMode == PSDP_CMYK) {
		// CMYK values must be "inverted"
		cmyk.dib = FreeImage_Clone(dib);
		if (cmyk.dib == NULL) {
			return false;
		}
		dib = cmyk.dib;
		invertCMYK(dib);
	}

	int nCompression = PSDP_COMPRESSION_RLE;
	if(_headerInfo._BitsPerChannel > 8) {
		// RLE is nearly useless for 16-bit, as it only looks at 8-bit data for runs.
		nCompression = PSDP_COMPRESSION_NONE;
	}
	if((_fi_flags & PSD_NONE) == PSD_NONE) {
		nCompression = PSDP_COMPRESSION_NONE;
	} else if((_fi_flags & PSD_RLE) == PSD_RLE) {
		nCompression = PSDP_COMPRESSION_RLE;
		if (_headerInfo._BitsPerChannel > 16) {
			nCompression = PSDP_COMPRESSION_NONE;
		}
	}

	WORD CompressionValue = nCompression;
#ifndef FREEIMAGE_BIGENDIAN
	SwapShort(&CompressionValue);
#endif

	if(io->write_proc(&CompressionValue, sizeof(CompressionValue), 1, handle) != 1) {
		return false;
	}

	const unsigned nWidth = _headerInfo._Width;
	const unsigned nHeight = _headerInfo._Height;
	const unsigned nChannels = _headerInfo._Channels;
	const unsigned depth = _headerInfo._BitsPerChannel;
	const unsigned bytes = (depth == 1) ? 1 : depth / 8;

	// channel(plane) line (BYTE aligned)
	const unsigned lineSize = (_headerInfo._BitsPerChannel == 1) ? (nWidth + 7) / 8 : nWidth * bytes;

	const unsigned srcBpp =  (depth == 1) ? 1 : FreeImage_GetBPP(dib)/8;
	const unsigned srcLineSize = FreeImage_GetPitch(dib);
	BYTE* const src_first_line = FreeImage_GetScanLine(dib, nHeight - 1);//<*** flipped
	std::vector<BYTE> line(lineSize); //< fileline cache
	BYTE* line_start = &line[0];

	switch ( nCompression ) {
		case PSDP_COMPRESSION_NONE: // raw data
		{
			for(unsigned c = 0; c < nChannels; c++) {
				const unsigned channelOffset = GetChannelOffset(dib, c) * bytes;

				BYTE* src_line_start = src_first_line + channelOffset;
				for(unsigned h = 0; h < nHeight; ++h, src_line_start -= srcLineSize) {//<*** flipped
					WriteImageLine(line_start, src_line_start, lineSize, srcBpp, bytes);
					if(io->write_proc(line_start, lineSize, 1, handle) != 1) {
						return false;
					}
				} //< h
			}//< ch
		}
		break;

		case PSDP_COMPRESSION_RLE: // RLE compression
		{
			// The RLE-compressed data is preceeded by a 2-byte line size for each row in the data,
			// store an array of these
			// Version 2 has 4-byte line sizes.

			// later use this array as WORD rleLineSizeList[nChannels][nHeight];
			// Every 127 bytes needs a length byte.
			std::vector<BYTE> rle_line(lineSize + ((lineSize + 126) / 127)); //< RLE buffer
			BYTE* rle_line_start = &rle_line[0];
			std::vector<DWORD> rleLineSizeList(nChannels*nHeight, 0);
			const long offsets_pos = io->tell_proc(handle);
			if(_headerInfo._Version == 1) {
				if(io->write_proc(&rleLineSizeList[0], nChannels*nHeight*2, 1, handle) != 1) {
					return false;
				}
			} else {
				if(io->write_proc(&rleLineSizeList[0], nChannels*nHeight*4, 1, handle) != 1) {
					return false;
				}
			}
			for(unsigned c = 0; c < nChannels; c++) {
				const unsigned channelOffset = GetChannelOffset(dib, c) * bytes;

				BYTE* src_line_start = src_first_line + channelOffset;
				for(unsigned h = 0; h < nHeight; ++h, src_line_start -= srcLineSize) {//<*** flipped
					WriteImageLine(line_start, src_line_start, lineSize, srcBpp, bytes);
					unsigned len = PackRLE(rle_line_start, line_start, lineSize);
					rleLineSizeList[c * nHeight + h] = len;
					if(io->write_proc(rle_line_start, len, 1, handle) != 1) {
						return false;
					}
				}
			}
			// Fix length of resource
			io->seek_proc(handle, offsets_pos, SEEK_SET);
			if(_headerInfo._Version == 1) {
				std::vector<WORD> rleLineSizeList2(nChannels*nHeight);
				for(unsigned index = 0; index < nChannels * nHeight; ++index) {
					rleLineSizeList2[index] = (WORD)rleLineSizeList[index];
#ifndef FREEIMAGE_BIGENDIAN
					SwapShort(&rleLineSizeList2[index]);
#endif
				}
				if(io->write_proc(&rleLineSizeList2[0], nChannels*nHeight*2, 1, handle) != 1) {
					return false;
				}
			} else {
#ifndef FREEIMAGE_BIGENDIAN
				for(unsigned index = 0; index < nChannels * nHeight; ++index) {
					SwapLong(&rleLineSizeList[index]);
				}
#endif
				if(io->write_proc(&rleLineSizeList[0], nChannels*nHeight*4, 1, handle) != 1) {
					return false;
				}
			}
			io->seek_proc(handle, 0, SEEK_END);
		}
		break;

		case PSDP_COMPRESSION_ZIP: // ZIP without prediction
		case PSDP_COMPRESSION_ZIP_PREDICTION: // ZIP with prediction
		{
		}
		break;

		default: // Unknown format
			break;
	}

	return true;
}

FIBITMAP* psdParser::Load(FreeImageIO *io, fi_handle handle, int s_format_id, int flags) {
	FIBITMAP *Bitmap = NULL;

	_fi_flags = flags;
	_fi_format_id = s_format_id;

	try {
		if (NULL == handle) {
			throw("Cannot open file");
		}

		if (!_headerInfo.Read(io, handle)) {
			throw("Error in header");
		}

		if (!_colourModeData.Read(io, handle)) {
			throw("Error in ColourMode Data");
		}

		if (!ReadImageResources(io, handle)) {
			throw("Error in Image Resource");
		}

		if (!ReadLayerAndMaskInfoSection(io, handle)) {
			throw("Error in Mask Info");
		}

		Bitmap = ReadImageData(io, handle);
		if (NULL == Bitmap) {
			throw("Error in Image Data");
		}

		// set resolution info
		if(NULL != Bitmap) {
			unsigned res_x = 2835;	// 72 dpi
			unsigned res_y = 2835;	// 72 dpi
			if (_bResolutionInfoFilled) {
				_resolutionInfo.GetResolutionInfo(res_x, res_y);
			}
			FreeImage_SetDotsPerMeterX(Bitmap, res_x);
			FreeImage_SetDotsPerMeterY(Bitmap, res_y);
		}

		// set ICC profile
		if(NULL != _iccProfile._ProfileData) {
			FreeImage_CreateICCProfile(Bitmap, _iccProfile._ProfileData, _iccProfile._ProfileSize);
			if ((flags & PSD_CMYK) == PSD_CMYK) {
				short mode = _headerInfo._ColourMode;
				if((mode == PSDP_CMYK) || (mode == PSDP_MULTICHANNEL)) {
					FreeImage_GetICCProfile(Bitmap)->flags |= FIICC_COLOR_IS_CMYK;
				}
			}
		}

		// Metadata
		if(NULL != _iptc._Data) {
			read_iptc_profile(Bitmap, _iptc._Data, _iptc._Size);
		}
		if(NULL != _exif1._Data) {
			psd_read_exif_profile(Bitmap, _exif1._Data, _exif1._Size);
			psd_read_exif_profile_raw(Bitmap, _exif1._Data, _exif1._Size);
		} else if(NULL != _exif3._Data) {
			// Assume that we only want one Exif resource.
			psd_read_exif_profile(Bitmap, _exif3._Data, _exif3._Size);
			psd_read_exif_profile_raw(Bitmap, _exif3._Data, _exif3._Size);
		}

		// XMP metadata
		if(NULL != _xmp._Data) {
			psd_set_xmp_profile(Bitmap, _xmp._Data, _xmp._Size);
		}

	} catch(const char *text) {
		FreeImage_OutputMessageProc(s_format_id, text);
	}
	catch(const std::exception& e) {
		FreeImage_OutputMessageProc(s_format_id, "%s", e.what());
	}

	return Bitmap;
}

bool psdParser::Save(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	if (!dib || !handle) {
		return false;
	}

	_fi_flags = flags;

	const FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib);

	const unsigned width = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	const unsigned bitsperpixel = FreeImage_GetBPP(dib);

	const FIICCPROFILE* iccProfile = FreeImage_GetICCProfile(dib);

	// setup out-variables based on dib and flag options

	unsigned bitspersample;
	unsigned samplesperpixel;
	short colourMode = PSDP_RGB;

	if(image_type == FIT_BITMAP) {
		// standard image: 1-, 4-, 8-, 16-, 24-, 32-bit
		if(bitsperpixel == 32) {
			// 32-bit images : check for CMYK or alpha transparency

			if((((iccProfile->flags & FIICC_COLOR_IS_CMYK) == FIICC_COLOR_IS_CMYK) || ((flags & PSD_CMYK) == PSD_CMYK))) {
				colourMode = PSDP_CMYK;
			}
			samplesperpixel = 4;
		} else if(bitsperpixel == 24) {
			samplesperpixel = 3;
		} else if(bitsperpixel == 8) {
			samplesperpixel = 1;
			colourMode = PSDP_INDEXED;
		} else if(bitsperpixel == 1) {
			samplesperpixel = 1;
			colourMode = PSDP_BITMAP;
		} else {
			return false;
		}
		bitspersample = bitsperpixel / samplesperpixel;
	} else if(image_type == FIT_UINT16 || image_type == FIT_INT16) {
		// Grayscale
		samplesperpixel = 1;
		bitspersample = bitsperpixel / samplesperpixel;
		colourMode = PSDP_GRAYSCALE;
	} else if(image_type == FIT_RGB16) {
		// 48-bit RGB
		samplesperpixel = 3;
		bitspersample = bitsperpixel / samplesperpixel;
	} else if(image_type == FIT_RGBA16) {
		// 64-bit RGBA
		samplesperpixel = 4;
		bitspersample = bitsperpixel / samplesperpixel;
		if((((iccProfile->flags & FIICC_COLOR_IS_CMYK) == FIICC_COLOR_IS_CMYK) || ((flags & PSD_CMYK) == PSD_CMYK))) {
			colourMode = PSDP_CMYK;
		}
	} else if(image_type == FIT_RGBF) {
		// 96-bit RGBF
		samplesperpixel = 3;
		bitspersample = bitsperpixel / samplesperpixel;
	} else if (image_type == FIT_RGBAF) {
		// 128-bit RGBAF
		samplesperpixel = 4;
		bitspersample = bitsperpixel / samplesperpixel;
	} else {
		// special image type (int, long, double, ...)
		samplesperpixel = 1;
		bitspersample = bitsperpixel;
	}

	_headerInfo._Version = (((flags & PSD_PSB) == PSD_PSB) || width > 30000 || height > 30000) ? 2 : 1;
	_headerInfo._Channels = samplesperpixel;
	_headerInfo._Height = height;
	_headerInfo._Width = width;
	_headerInfo._BitsPerChannel = bitsperpixel / samplesperpixel;
	_headerInfo._ColourMode = colourMode;
	if(!_headerInfo.Write(io, handle)) return false;

	_colourModeData._Length = 0;
	_colourModeData._plColourData = NULL;

	// indexed only: 768 bytes, 256 reds, then greens, then blues
	if (colourMode == PSDP_INDEXED) {
		RGBQUAD *pal = FreeImage_GetPalette(dib);

		if (pal != NULL) {
			const unsigned used = MIN(FreeImage_GetColorsUsed(dib), (unsigned)256);

			_colourModeData._Length = 768;
			_colourModeData._plColourData = new BYTE[768];
			memset(_colourModeData._plColourData, 0, 768);

			for(unsigned i = 0; i < used; i++ ) {
				_colourModeData._plColourData[i + 0*256] = pal[i].rgbRed;
				_colourModeData._plColourData[i + 1*256] = pal[i].rgbGreen;
				_colourModeData._plColourData[i + 2*256] = pal[i].rgbBlue;
			}
		}
	}

	if (!_colourModeData.Write(io, handle)) {
		return false;
	}

	BYTE IntValue[4];
	const long res_start_pos = io->tell_proc(handle);
	psdSetValue(IntValue, sizeof(IntValue), 0);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}

	_resolutionInfo._hRes = (short) (0.5 + 0.0254 * FreeImage_GetDotsPerMeterX(dib));
	_resolutionInfo._hResUnit = 1; // inches
	_resolutionInfo._widthUnit = 1; // inches
	_resolutionInfo._vRes = (short) (0.5 + 0.0254 * FreeImage_GetDotsPerMeterY(dib));
	_resolutionInfo._vResUnit = 1; // inches
	_resolutionInfo._heightUnit = 1; // inches
	if (!_resolutionInfo.Write(io, handle)) {
		return false;
	}

	// psdResolutionInfo_v2 is obsolete - Photoshop 2.0

	_displayInfo._ColourSpace = (colourMode == PSDP_CMYK) ? 2 : 0;
	memset(_displayInfo._Colour, 0, sizeof(_displayInfo._Colour));
	_displayInfo._Opacity = 100;
	_displayInfo._Kind = 0;
	_displayInfo._padding = 0;
	if (!_displayInfo.Write(io, handle)) {
		return false;
	}

	// (Photoshop 6.0) Indexed Color Table Count
	if (colourMode == PSDP_INDEXED) {
		psdImageResource res;
		BYTE ShortValue[2];

		if (!res.Write(io, handle, PSDP_RES_INDEXED_COLORS, (int)sizeof(ShortValue))) {
			return false;
		}

		psdSetValue(ShortValue, sizeof(ShortValue), (WORD)MIN(FreeImage_GetColorsUsed(dib), (unsigned)256));

		if(io->write_proc(ShortValue, sizeof(ShortValue), 1, handle) != 1) {
			return false;
		}
	}

	if(GetThumbnail() == NULL) {
		_thumbnail._owned = false;
		_thumbnail._dib = FreeImage_GetThumbnail(dib);
	}
	if(GetThumbnail() != NULL) {
		_thumbnail.Init();
		if (!_thumbnail.Write(io, handle, false)) {
			return false;
		}
	}

	if(iccProfile != NULL && iccProfile->size > 0) {
		_iccProfile.clear();
		_iccProfile._owned = false;
		_iccProfile._ProfileSize = iccProfile->size;
		_iccProfile._ProfileData = (BYTE*)iccProfile->data;
		if (!_iccProfile.Write(io, handle)) {
			return false;
		}
	}

	if(write_iptc_profile(dib, &_iptc._Data, &_iptc._Size)) {
		if (!_iptc.Write(io, handle, PSDP_RES_IPTC_NAA)) {
			return false;
		}
	}

	if(psd_write_exif_profile_raw(dib, &_exif1._Data, &_exif1._Size)) {
		_exif1._owned = false;
		if (!_exif1.Write(io, handle, PSDP_RES_EXIF1)) {
			return false;
		}
	}

	if(psd_get_xmp_profile(dib, &_xmp._Data, &_xmp._Size)) {
		_xmp._owned = false;
		if (!_xmp.Write(io, handle, PSDP_RES_XMP)) {
			return false;
		}
	}

	// Fix length of resources
	const long current_pos = io->tell_proc(handle);
	psdSetValue(IntValue, sizeof(IntValue), (int)(current_pos - res_start_pos - 4));
	io->seek_proc(handle, res_start_pos, SEEK_SET);
	if(io->write_proc(IntValue, sizeof(IntValue), 1, handle) != 1) {
		return false;
	}
	io->seek_proc(handle, current_pos, SEEK_SET);

	if (!WriteLayerAndMaskInfoSection(io, handle)) {
		return false;
	}
	if (!WriteImageData(io, handle, dib)) {
		return false;
	}

	return true;
}
