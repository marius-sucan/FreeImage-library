// ==========================================================
// AVIF Loader
//
// AV1 Image File Format (AVIF) decoder, built on libavif (Source/LibAVIF)
// with dav1d (Source/LibDav1d) as the AV1 decoder.
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
//
// What this plugin does
// ---------------------
// - Loads still AVIF images and AVIF image sequences ('avis'); a sequence opens as
//   a multi-page bitmap, one page per frame, with the GIF-style "FrameTime" and
//   "Loop" tags in FIMD_ANIMATION.
// - 8-bit content becomes a 24-bit or (with alpha) 32-bit FIT_BITMAP; 10- and 12-bit
//   content becomes FIT_RGB16 / FIT_RGBA16, scaled to the full 16-bit range;
//   monochrome content without alpha becomes 8-bit greyscale or FIT_UINT16.
//   Premultiplied alpha is undone, so the pixels always carry straight alpha.
// - The transformative properties are applied in the order the HEIF/MIAF standards
//   require: clean aperture ('clap'), then rotation ('irot'), then mirroring ('imir').
//   Readers must honour them, unlike the advisory Exif orientation tag, which is
//   passed through untouched in the Exif metadata like every other plugin does.
// - ICC profiles, Exif (raw and parsed) and XMP are attached to the bitmap.
// - Decoding runs on all cores: dav1d's task threading plus libavif's threaded
//   YUV->RGB conversion.
// - Grid images, layered/progressive images and gain maps are handled by libavif;
//   only the base (color + alpha) image is surfaced.
//
// What it does not do
// -------------------
// - It cannot save: no AV1 encoder is bundled. FreeImage_FIFSupportsWriting(FIF_AVIF)
//   returns FALSE.
// - HDR content (PQ or HLG transfer characteristics) is returned as encoded, without
//   tone mapping; the CICP color description is not exposed.
// - HEIC (HEVC-coded HEIF) is a different codec: libavif does not read it, and
//   FreeImage_GetFileType() does not claim it.
//
// The whole file is streamed through FreeImageIO on demand (libavif's avifIO
// interface), so a header-only load (FIF_LOAD_NOPIXELS) reads the metadata boxes
// but not the compressed image data.
// ==========================================================

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "FreeImage.h"
#include "Utilities.h"

#include "../Metadata/FreeImageTag.h"

#include "../LibAVIF/include/avif/avif.h"

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ----------------------------------------------------------
//   Threads
// ----------------------------------------------------------

/** No point in more decoder threads than this for a single image */
#define FI_AVIF_MAX_THREADS 64

/**
Number of logical processors, for dav1d's decoding threads and libavif's
YUV->RGB conversion threads.
@return Returns a value between 1 and FI_AVIF_MAX_THREADS
*/
static int
GetProcessorCount() {
	int count = 1;
#ifdef _WIN32
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	count = (int)info.dwNumberOfProcessors;
#elif defined(_SC_NPROCESSORS_ONLN)
	const long n = sysconf(_SC_NPROCESSORS_ONLN);
	if(n > 0) {
		count = (int)n;
	}
#endif
	if(count < 1) {
		count = 1;
	}
	if(count > FI_AVIF_MAX_THREADS) {
		count = FI_AVIF_MAX_THREADS;
	}
	return count;
}

// ----------------------------------------------------------
//   avifIO over FreeImageIO
// ----------------------------------------------------------

/**
The largest offset a single absolute seek_proc call can express: seek_proc takes
a 'long', which is 32-bit on Win64. Overridable so the stepped path below can be
exercised where 'long' is 64-bit (see TestAPI/AVIF).
*/
#ifndef FI_AVIF_SEEK_STEP_MAX
#define FI_AVIF_SEEK_STEP_MAX LONG_MAX
#endif

/** read_proc takes an 'unsigned' size: read at most this much per call */
#define FI_AVIF_READ_CHUNK 0x40000000u

typedef struct tagAVIFStream {
	avifIO io;				//! libavif's view of the stream
	FreeImageIO *fio;		//! FreeImage I/O functions
	fi_handle handle;		//! FreeImage I/O handle
	long base;				//! stream position of the first AVIF byte
	BOOL size_known;		//! TRUE when 'size' is meaningful
	uint64_t size;			//! bytes from 'base' to the end of the stream (see size_known)
	BYTE *buffer;			//! the block handed to libavif by the last read
	size_t capacity;		//! bytes allocated for 'buffer'
} AVIFStream;

/**
Move the stream to 'base + offset'. Offsets that do not fit in a 'long' are
reached by rewinding to the base and walking forward in steps that do.
*/
static BOOL
AVIF_SeekTo(AVIFStream *s, uint64_t offset) {
	const long span = (s->base <= FI_AVIF_SEEK_STEP_MAX) ? (FI_AVIF_SEEK_STEP_MAX - s->base) : 0;
	if(offset <= (uint64_t)span) {
		return (s->fio->seek_proc(s->handle, s->base + (long)offset, SEEK_SET) == 0) ? TRUE : FALSE;
	}
	if(s->fio->seek_proc(s->handle, s->base, SEEK_SET) != 0) {
		return FALSE;
	}
	uint64_t remaining = offset;
	while(remaining > 0) {
		const long step = (remaining > (uint64_t)FI_AVIF_SEEK_STEP_MAX) ? (long)FI_AVIF_SEEK_STEP_MAX : (long)remaining;
		if(s->fio->seek_proc(s->handle, step, SEEK_CUR) != 0) {
			return FALSE;
		}
		remaining -= (uint64_t)step;
	}
	return TRUE;
}

/**
libavif read callback. The contract (avif.h, avifIOReadFunc): the block stays valid
until the next read; an offset past the end is an error; an offset exactly at the
end yields an empty block; a range crossing the end is clipped to it.
*/
static avifResult
AVIF_ReadProc(avifIO *io, uint32_t readFlags, uint64_t offset, size_t size, avifROData *out) {
	AVIFStream *s = (AVIFStream*)io->data;

	if(readFlags != 0) {
		// no read flags are defined; refuse unknown ones like libavif's own readers do
		return AVIF_RESULT_IO_ERROR;
	}
	if(s->size_known) {
		if(offset > s->size) {
			return AVIF_RESULT_IO_ERROR;
		}
		if(size > s->size - offset) {
			size = (size_t)(s->size - offset);
		}
	}
	if(size == 0) {
		static const uint8_t empty = 0;
		out->data = &empty;
		out->size = 0;
		return AVIF_RESULT_OK;
	}
	if(size > s->capacity) {
		BYTE *buffer = (BYTE*)realloc(s->buffer, size);
		if(!buffer) {
			return AVIF_RESULT_OUT_OF_MEMORY;
		}
		s->buffer = buffer;
		s->capacity = size;
	}
	if(!AVIF_SeekTo(s, offset)) {
		return AVIF_RESULT_IO_ERROR;
	}

	size_t total = 0;
	while(total < size) {
		const size_t left = size - total;
		const unsigned chunk = (left > (size_t)FI_AVIF_READ_CHUNK) ? FI_AVIF_READ_CHUNK : (unsigned)left;
		const unsigned got = s->fio->read_proc(s->buffer + total, 1, chunk, s->handle);
		if(got == 0) {
			break;
		}
		total += got;
	}
	if((total < size) && s->size_known) {
		// the range was already clipped to the end: this is a read failure
		return AVIF_RESULT_IO_ERROR;
	}
	// with an unknown size a short read is the end of the stream, which the caller detects

	out->data = s->buffer;
	out->size = total;
	return AVIF_RESULT_OK;
}

// ----------------------------------------------------------
//   Decoder context (the 'data' of Open/Load/Close)
// ----------------------------------------------------------

typedef struct tagAVIFContext {
	AVIFStream stream;
	avifDecoder *decoder;
	int threads;
} AVIFContext;

/** Output bitmap format chosen for an image */
typedef struct tagAVIFOutput {
	FREE_IMAGE_TYPE type;
	unsigned bpp;
	avifRGBFormat format;	//! what libavif is asked to produce
	unsigned depth;			//! 8 or 16 bits per channel
} AVIFOutput;

static void
ReportError(const char *what, avifResult result, const avifDecoder *decoder) {
	const char *detail = decoder->diag.error;
	FreeImage_OutputMessageProc(s_format_id, "%s: %s%s%s", what, avifResultToString(result), detail[0] ? " - " : "", detail);
}

/**
Pick the FreeImage type for an image. Greyscale is only used for monochrome
content without alpha; everything else goes through RGB(A).
*/
static void
ChooseOutput(const avifImage *image, BOOL has_alpha, BOOL allow_grey, AVIFOutput *out) {
	const BOOL deep = (image->depth > 8) ? TRUE : FALSE;
	const BOOL grey = (allow_grey && (image->yuvFormat == AVIF_PIXEL_FORMAT_YUV400) && !has_alpha) ? TRUE : FALSE;

	out->depth = deep ? 16 : 8;
	if(grey) {
		out->type = deep ? FIT_UINT16 : FIT_BITMAP;
		out->bpp = deep ? 16 : 8;
		out->format = AVIF_RGB_FORMAT_GRAY;
	} else if(deep) {
		// FIRGB16 / FIRGBA16 are red, green, blue in memory whatever FREEIMAGE_COLORORDER says
		out->type = has_alpha ? FIT_RGBA16 : FIT_RGB16;
		out->bpp = has_alpha ? 64 : 48;
		out->format = has_alpha ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
	} else {
		out->type = FIT_BITMAP;
		out->bpp = has_alpha ? 32 : 24;
#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_BGR
		out->format = has_alpha ? AVIF_RGB_FORMAT_BGRA : AVIF_RGB_FORMAT_BGR;
#else
		out->format = has_alpha ? AVIF_RGB_FORMAT_RGBA : AVIF_RGB_FORMAT_RGB;
#endif
	}
}

static FIBITMAP *
AllocateOutput(BOOL header_only, const AVIFOutput *out, unsigned width, unsigned height) {
	FIBITMAP *dib = NULL;

	if((width == 0) || (height == 0) || (width > (unsigned)INT_MAX) || (height > (unsigned)INT_MAX)) {
		FreeImage_OutputMessageProc(s_format_id, "Unsupported image size: %u x %u", width, height);
		return NULL;
	}
	if((out->type == FIT_BITMAP) && (out->bpp >= 24)) {
		dib = FreeImage_AllocateHeaderT(header_only, FIT_BITMAP, (int)width, (int)height, (int)out->bpp, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	} else {
		dib = FreeImage_AllocateHeaderT(header_only, out->type, (int)width, (int)height, (int)out->bpp, 0, 0, 0);
	}
	if(!dib) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_DIB_MEMORY);
		return NULL;
	}
	if((out->type == FIT_BITMAP) && (out->bpp == 8)) {
		// greyscale ramp
		RGBQUAD *pal = FreeImage_GetPalette(dib);
		for(int i = 0; i < 256; i++) {
			pal[i].rgbRed = pal[i].rgbGreen = pal[i].rgbBlue = (BYTE)i;
			pal[i].rgbReserved = 0;
		}
	}
	return dib;
}

// ----------------------------------------------------------
//   Metadata
// ----------------------------------------------------------

static BOOL
SetMetadataLong(FREE_IMAGE_MDMODEL model, FIBITMAP *dib, const char *key, WORD id, LONG value) {
	FITAG *tag = FreeImage_CreateTag();
	if(!tag) {
		return FALSE;
	}
	FreeImage_SetTagKey(tag, key);
	FreeImage_SetTagID(tag, id);
	FreeImage_SetTagType(tag, FIDT_LONG);
	FreeImage_SetTagCount(tag, 1);
	FreeImage_SetTagLength(tag, 4);
	FreeImage_SetTagValue(tag, &value);
	const BOOL bResult = FreeImage_SetMetadata(model, dib, key, tag);
	FreeImage_DeleteTag(tag);
	return bResult;
}

/**
Offset of the TIFF header inside an Exif payload. ISO/IEC 23008-12 A.2.1 stores the
payload from the TIFF header on, but many writers keep JPEG's "Exif\0\0" prefix, and
libavif itself locates the header by searching, so search too.
@return Returns the offset, or 'size' when there is no TIFF header
*/
static size_t
FindTiffHeader(const BYTE *data, size_t size) {
	static const BYTE lsb_first[4] = { 0x49, 0x49, 0x2A, 0x00 };	// "II*\0"
	static const BYTE msb_first[4] = { 0x4D, 0x4D, 0x00, 0x2A };	// "MM\0*"
	for(size_t i = 0; i + 4 <= size; i++) {
		if((memcmp(data + i, lsb_first, 4) == 0) || (memcmp(data + i, msb_first, 4) == 0)) {
			return i;
		}
	}
	return size;
}

/**
Attach the ICC profile, the Exif block (raw and decoded) and the XMP packet.
All three are known after avifDecoderParse(), so this also serves header-only loads.
*/
static void
AttachMetadata(FIBITMAP *dib, const avifImage *image) {
	// ICC profile
	if((image->icc.size > 0) && image->icc.data && (image->icc.size <= (size_t)LONG_MAX)) {
		FreeImage_CreateICCProfile(dib, (void*)image->icc.data, (long)image->icc.size);
	}

	// Exif: the PSD helpers take the payload from its TIFF header, which is what libavif keeps
	if((image->exif.size > 0) && image->exif.data && (image->exif.size <= (size_t)UINT_MAX)) {
		const BYTE *exif = image->exif.data;
		const size_t offset = FindTiffHeader(exif, image->exif.size);
		if(offset < image->exif.size) {
			const unsigned length = (unsigned)(image->exif.size - offset);
			// the raw block, stored with the "Exif\0\0" prefix the JPEG and WebP writers expect
			psd_read_exif_profile_raw(dib, exif + offset, length);
			// the decoded tags (FIMD_EXIF_MAIN, FIMD_EXIF_EXIF, FIMD_EXIF_GPS, ...)
			psd_read_exif_profile(dib, exif + offset, length);
		}
	}

	// XMP
	if((image->xmp.size > 0) && image->xmp.data && (image->xmp.size <= (size_t)UINT_MAX)) {
		FITAG *tag = FreeImage_CreateTag();
		if(tag) {
			FreeImage_SetTagKey(tag, g_TagLib_XMPFieldName);
			FreeImage_SetTagLength(tag, (DWORD)image->xmp.size);
			FreeImage_SetTagCount(tag, (DWORD)image->xmp.size);
			FreeImage_SetTagType(tag, FIDT_ASCII);
			FreeImage_SetTagValue(tag, image->xmp.data);
			FreeImage_SetMetadata(FIMD_XMP, dib, FreeImage_GetTagKey(tag), tag);
			FreeImage_DeleteTag(tag);
		}
	}
}

/**
For image sequences: the frame duration and the loop count, with the meaning the
GIF plugin gives them ("FrameTime" in milliseconds; "Loop" counts plays, 0 = forever).
*/
static void
AttachAnimation(FIBITMAP *dib, avifDecoder *decoder, int page) {
	if(decoder->imageCount <= 1) {
		return;
	}
	avifImageTiming timing;
	if(avifDecoderNthImageTiming(decoder, (uint32_t)page, &timing) == AVIF_RESULT_OK) {
		double ms = timing.duration * 1000.0 + 0.5;
		if(ms < 0) {
			ms = 0;
		} else if(ms > (double)LONG_MAX) {
			ms = (double)LONG_MAX;
		}
		SetMetadataLong(FIMD_ANIMATION, dib, "FrameTime", ANIMTAG_FRAMETIME, (LONG)ms);
	}
	// libavif counts extra repetitions ('n' means n + 1 plays), negative = infinite or unknown
	const LONG loop = (decoder->repetitionCount < 0) ? 0 : (LONG)decoder->repetitionCount + 1;
	SetMetadataLong(FIMD_ANIMATION, dib, "Loop", ANIMTAG_LOOP, loop);
}

// ----------------------------------------------------------
//   Transformative properties
// ----------------------------------------------------------

/**
The clean aperture as a crop rectangle in pixels, or FALSE when there is none or
it is invalid.
*/
static BOOL
GetCleanAperture(const avifImage *image, avifDiagnostics *diag, avifCropRect *rect) {
	if(!(image->transformFlags & AVIF_TRANSFORM_CLAP)) {
		return FALSE;
	}
	if(!avifCropRectFromCleanApertureBox(rect, &image->clap, image->width, image->height, diag)) {
		FreeImage_OutputMessageProc(s_format_id, "Ignoring an invalid clean aperture ('clap') box");
		return FALSE;
	}
	if((rect->width == 0) || (rect->height == 0)) {
		return FALSE;
	}
	return TRUE;
}

/**
Apply 'clap', 'irot' and 'imir' to a decoded bitmap, in that order
(ISO/IEC 23000-22, 7.3.6.7).
@return Returns the transformed bitmap; the input is released when replaced
*/
static FIBITMAP *
ApplyTransforms(FIBITMAP *dib, const avifImage *image, avifDiagnostics *diag) {
	avifCropRect rect;

	// clean aperture: the pixels are RGB here, so this is the 4:4:4 crop the standard asks for
	if(GetCleanAperture(image, diag, &rect)) {
		if((rect.x != 0) || (rect.y != 0) || (rect.width != image->width) || (rect.height != image->height)) {
			FIBITMAP *cropped = FreeImage_Copy(dib, (int)rect.x, (int)rect.y, (int)(rect.x + rect.width), (int)(rect.y + rect.height));
			if(cropped) {
				FreeImage_Unload(dib);
				dib = cropped;
			} else {
				FreeImage_OutputMessageProc(s_format_id, "Could not apply the clean aperture (%u x %u at %u, %u)", rect.width, rect.height, rect.x, rect.y);
			}
		}
	}

	// rotation: 'irot' counts quarter turns anti-clockwise, as does FreeImage_Rotate's angle
	if(image->transformFlags & AVIF_TRANSFORM_IROT) {
		const int quarter_turns = image->irot.angle & 3;
		if(quarter_turns != 0) {
			FIBITMAP *rotated = FreeImage_Rotate(dib, 90.0 * quarter_turns, NULL);
			if(rotated) {
				FreeImage_Unload(dib);
				dib = rotated;
			} else {
				FreeImage_OutputMessageProc(s_format_id, "Could not apply the %d degree rotation ('irot')", 90 * quarter_turns);
			}
		}
	}

	// mirroring: axis 0 exchanges top and bottom, axis 1 left and right (ISO/IEC 23008-12:2022, 6.5.12)
	if(image->transformFlags & AVIF_TRANSFORM_IMIR) {
		if(image->imir.axis == 0) {
			FreeImage_FlipVertical(dib);
		} else {
			FreeImage_FlipHorizontal(dib);
		}
	}

	return dib;
}

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "AVIF";
}

static const char * DLL_CALLCONV
Description() {
	return "AV1 Image File Format";
}

static const char * DLL_CALLCONV
Extension() {
	return "avif,avifs";
}

static const char * DLL_CALLCONV
RegExpr() {
	return NULL;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/avif";
}

/**
Does the FileTypeBox held in 'ftyp' (the whole box, 'size' bytes) name 'brand', either as
its major brand or among its compatible brands? Mirrors libavif's avifFileTypeHasBrand.
*/
static BOOL
ftypHasBrand(const BYTE *ftyp, unsigned size, const char *brand) {
	if(memcmp(ftyp + 8, brand, 4) == 0) {
		// major_brand
		return TRUE;
	}
	// compatible_brands[], which follows the 4-byte minor_version
	for(unsigned offset = 16; offset + 4 <= size; offset += 4) {
		if(memcmp(ftyp + offset, brand, 4) == 0) {
			return TRUE;
		}
	}
	return FALSE;
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	// An AVIF file opens with a FileTypeBox ('ftyp') whose brands name AVIF.
	// libavif checks the brands, given the whole box: fetch it by its declared size.
	BYTE buffer[4096];

	if(io->read_proc(buffer, 1, 8, handle) != 8) {
		return FALSE;
	}
	if(memcmp(buffer + 4, "ftyp", 4) != 0) {
		return FALSE;
	}
	const unsigned box_size = ((unsigned)buffer[0] << 24) | ((unsigned)buffer[1] << 16) | ((unsigned)buffer[2] << 8) | (unsigned)buffer[3];
	// 16 = box header + major brand + minor version; anything beyond the buffer is not an image's ftyp
	if((box_size < 16) || (box_size > sizeof(buffer))) {
		return FALSE;
	}
	if(io->read_proc(buffer + 8, 1, box_size - 8, handle) != box_size - 8) {
		return FALSE;
	}
	avifROData data;
	data.data = buffer;
	data.size = box_size;
	if(!avifPeekCompatibleFileType(&data)) {
		return FALSE;
	}
	// 'mif3' is a structural brand, not a codec brand: it says the file replaces the
	// MetaBox with a MinimizedImageBox, and says nothing about what codec the image is
	// coded in. The codec is named by the FileTypeBox minor_version, or spelled out inside
	// the box. libavif accepts only AV1 and refuses everything else once it parses the box,
	// so claiming every 'mif3' file here would take HEVC ones (hevc32-mini.heif, minor
	// version 'heic') away from the HEIF plugin, which does read them: FreeImage asks each
	// plugin once and does not move on when the load then fails.
	// libavif writes 'avif' as the minor_version of the files it produces, and requires it
	// for any file that does not spell the codec out, so that is the test. The brand is
	// only ever the deciding one when neither codec brand is present.
	if(!ftypHasBrand(buffer, box_size, "avif") && !ftypHasBrand(buffer, box_size, "avis")) {
		if(memcmp(buffer + 12, "avif", 4) != 0) {
			return FALSE;
		}
	}
	return TRUE;
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

static void DLL_CALLCONV
Close(FreeImageIO *io, fi_handle handle, void *data) {
	AVIFContext *ctx = (AVIFContext*)data;
	if(ctx) {
		if(ctx->decoder) {
			// the decoder does not own our avifIO (io.destroy is NULL), so this only releases libavif's state
			avifDecoderDestroy(ctx->decoder);
		}
		free(ctx->stream.buffer);
		free(ctx);
	}
}

/**
Create the decoder and parse the container. Everything but the AV1 payloads is
read here: dimensions, depth, alpha, transforms, metadata, frame count.
*/
static void * DLL_CALLCONV
Open(FreeImageIO *io, fi_handle handle, BOOL read) {
	if(!read) {
		// this plugin cannot write
		return NULL;
	}

	AVIFContext *ctx = (AVIFContext*)calloc(1, sizeof(AVIFContext));
	if(!ctx) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_MEMORY);
		return NULL;
	}

	// --- the stream ---

	AVIFStream *s = &ctx->stream;
	s->fio = io;
	s->handle = handle;
	s->base = io->tell_proc(handle);
	if(s->base < 0) {
		s->base = 0;
	}
	// the stream length, when tell_proc can express it (it cannot beyond 2 GB where 'long' is 32-bit)
	if(io->seek_proc(handle, 0, SEEK_END) == 0) {
		const long end = io->tell_proc(handle);
		if(end >= s->base) {
			s->size = (uint64_t)(end - s->base);
			s->size_known = TRUE;
		}
	}
	io->seek_proc(handle, s->base, SEEK_SET);

	s->io.read = AVIF_ReadProc;
	s->io.sizeHint = s->size_known ? s->size : 0;
	s->io.persistent = AVIF_FALSE;
	s->io.data = s;
	// io.destroy stays NULL: the decoder must not free a struct it does not own

	// --- the decoder ---

	ctx->threads = GetProcessorCount();
	ctx->decoder = avifDecoderCreate();
	if(!ctx->decoder) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_MEMORY);
		Close(io, handle, ctx);
		return NULL;
	}
	avifDecoder *decoder = ctx->decoder;
	decoder->maxThreads = ctx->threads;
	// FreeImage's own allocation is the limit, not libavif's 16384 x 16384 default: gigapixel images are in scope
	decoder->imageSizeLimit = UINT32_MAX;
	decoder->imageDimensionLimit = 0;
	// lenient: accept files from encoders that skipped the 'pixi' or the alpha 'ispe' property
	// (libheif <= 1.11 among them), and ignore rather than reject an invalid 'clap' (see ApplyTransforms)
	decoder->strictFlags = AVIF_STRICT_DISABLED;
	avifDecoderSetIO(decoder, &s->io);

	avifResult result = avifDecoderParse(decoder);
	if(result == AVIF_RESULT_INVALID_EXIF_PAYLOAD) {
		// a broken Exif block should cost the metadata, not the image
		ReportError("Ignoring the Exif metadata", result, decoder);
		decoder->ignoreExif = AVIF_TRUE;
		result = avifDecoderParse(decoder);
	}
	if(result != AVIF_RESULT_OK) {
		ReportError("Cannot parse the file", result, decoder);
		Close(io, handle, ctx);
		return NULL;
	}

	return ctx;
}

static int DLL_CALLCONV
PageCount(FreeImageIO *io, fi_handle handle, void *data) {
	AVIFContext *ctx = (AVIFContext*)data;
	if(!ctx || !ctx->decoder) {
		return 0;
	}
	return (ctx->decoder->imageCount > 0) ? ctx->decoder->imageCount : 1;
}

/**
Header-only load: the bitmap after the transforms, without decoding.
*/
static FIBITMAP *
LoadHeader(avifDecoder *decoder, int page) {
	const avifImage *image = decoder->image;
	AVIFOutput out;
	avifCropRect rect;

	ChooseOutput(image, decoder->alphaPresent, TRUE, &out);

	// the transforms change the size: the clean aperture first, then an odd number of quarter turns
	unsigned width = image->width;
	unsigned height = image->height;
	if(GetCleanAperture(image, &decoder->diag, &rect)) {
		width = rect.width;
		height = rect.height;
	}
	if((image->transformFlags & AVIF_TRANSFORM_IROT) && (image->irot.angle & 1)) {
		const unsigned t = width;
		width = height;
		height = t;
	}

	FIBITMAP *dib = AllocateOutput(TRUE, &out, width, height);
	if(dib) {
		AttachMetadata(dib, image);
		AttachAnimation(dib, decoder, page);
	}
	return dib;
}

/**
Decode frame 'page' into a new bitmap.
*/
static FIBITMAP *
LoadPixels(AVIFContext *ctx, int page) {
	avifDecoder *decoder = ctx->decoder;
	AVIFOutput out;
	avifRGBImage rgb;

	avifResult result = avifDecoderNthImage(decoder, (uint32_t)page);
	if(result != AVIF_RESULT_OK) {
		ReportError("Cannot decode the image", result, decoder);
		return NULL;
	}
	const avifImage *image = decoder->image;
	const BOOL has_alpha = (image->alphaPlane != NULL) ? TRUE : FALSE;

	// first choice: greyscale for monochrome content; if libavif has no direct path for it, go through colour
	BOOL allow_grey = TRUE;
	FIBITMAP *dib = NULL;
	for(;;) {
		ChooseOutput(image, has_alpha, allow_grey, &out);
		dib = AllocateOutput(FALSE, &out, image->width, image->height);
		if(!dib) {
			return NULL;
		}

		avifRGBImageSetDefaults(&rgb, image);
		rgb.format = out.format;
		rgb.depth = out.depth;
		rgb.chromaUpsampling = AVIF_CHROMA_UPSAMPLING_BEST_QUALITY;
		// FreeImage keeps straight alpha: libavif undoes the premultiplication when the file has it
		rgb.alphaPremultiplied = AVIF_FALSE;
		rgb.maxThreads = ctx->threads;
		rgb.pixels = FreeImage_GetBits(dib);
		rgb.rowBytes = FreeImage_GetPitch(dib);

		result = avifImageYUVToRGB(image, &rgb);
		if(result == AVIF_RESULT_OK) {
			break;
		}
		FreeImage_Unload(dib);
		dib = NULL;
		if((result == AVIF_RESULT_NOT_IMPLEMENTED) && (out.format == AVIF_RGB_FORMAT_GRAY)) {
			allow_grey = FALSE;
			continue;
		}
		ReportError("Cannot convert the image to RGB", result, decoder);
		return NULL;
	}

	// libavif wrote the rows top-down, FreeImage keeps them bottom-up
	FreeImage_FlipVertical(dib);

	dib = ApplyTransforms(dib, image, &decoder->diag);

	AttachMetadata(dib, image);
	AttachAnimation(dib, decoder, page);

	return dib;
}

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	AVIFContext *ctx = (AVIFContext*)data;
	if(!ctx || !ctx->decoder) {
		return NULL;
	}
	if(page < 0) {
		page = 0;
	}
	if(page >= ctx->decoder->imageCount) {
		FreeImage_OutputMessageProc(s_format_id, "Page %d does not exist: the file has %d image(s)", page, ctx->decoder->imageCount);
		return NULL;
	}

	const BOOL header_only = ((flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS) ? TRUE : FALSE;
	if(header_only) {
		return LoadHeader(ctx->decoder, page);
	}
	return LoadPixels(ctx, page);
}

// ==========================================================
//	 Init
// ==========================================================

void DLL_CALLCONV
InitAVIF(Plugin *plugin, int format_id) {
	s_format_id = format_id;

	plugin->format_proc = Format;
	plugin->description_proc = Description;
	plugin->extension_proc = Extension;
	plugin->regexpr_proc = RegExpr;
	plugin->open_proc = Open;
	plugin->close_proc = Close;
	plugin->pagecount_proc = PageCount;
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
