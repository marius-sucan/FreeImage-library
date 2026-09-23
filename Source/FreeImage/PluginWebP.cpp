// ==========================================================
// Google WebP Loader & Writer
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

#include "../LibWebP/src/webp/decode.h"
#include "../LibWebP/src/webp/demux.h"
#include "../LibWebP/src/webp/encode.h"
#include "../LibWebP/src/webp/mux.h"

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ----------------------------------------------------------
//   Plugin state
// ----------------------------------------------------------

typedef struct {
	WebPMux *mux;
	WebPData bitstream;			//! whole file; mux and anim point into it
	WebPAnimDecoder *anim;		//! created on first use
	int anim_next;				//! next frame GetNext() returns; -1 = rewind
	FIBITMAP *cached_frame;
	int cached_page;			//! its page; -1 = none
	int frame_count;			//! 1 for a still image
	BOOL is_animation;
	int canvas_width;			//! VP8X canvas; 0 for a still image
	int canvas_height;
	int loop_count;

	// --- writing ---
	// Save() collects the frames, Close() writes them
	BOOL write;
	BOOL written;				//! Save() wrote a still image
	int out_pages;
	WebPData pending;			//! first page, held until a second arrives
	WebPMuxFrameInfo pending_info;
	int out_canvas_width;		//! canvas the pages asked for; 0 = none
	int out_canvas_height;
	int out_bound_width;		//! bounding box of the frames
	int out_bound_height;
	int out_loop;
} WebPPluginData;

static BOOL WebP_FinishAnimation(WebPPluginData *state, FreeImageIO *io, fi_handle handle);

static BOOL
WebP_SetAnimTag(FIBITMAP *dib, const char *key, WORD id, FREE_IMAGE_MDTYPE type, DWORD count, DWORD length, const void *value) {
	BOOL bResult = FALSE;
	FITAG *tag = FreeImage_CreateTag();
	if(tag) {
		FreeImage_SetTagKey(tag, key);
		FreeImage_SetTagID(tag, id);
		FreeImage_SetTagType(tag, type);
		FreeImage_SetTagCount(tag, count);
		FreeImage_SetTagLength(tag, length);
		FreeImage_SetTagValue(tag, value);
		// get the tag description
		TagLib& s = TagLib::instance();
		FreeImage_SetTagDescription(tag, s.getTagDescription(TagLib::ANIMATION, id));
		// store the tag
		bResult = FreeImage_SetMetadata(FIMD_ANIMATION, dib, key, tag);
		FreeImage_DeleteTag(tag);
	}
	return bResult;
}

static void
WebP_ClearFrameCache(WebPPluginData *state) {
	if(state->cached_frame != NULL) {
		FreeImage_Unload(state->cached_frame);
		state->cached_frame = NULL;
	}
	state->cached_page = -1;
}

// ----------------------------------------------------------
//   Helpers for the load function
// ----------------------------------------------------------

/**
Read the whole file into memory
*/
static BOOL
ReadFileToWebPData(FreeImageIO *io, fi_handle handle, WebPData * const bitstream) {
  uint8_t *raw_data = NULL;

  try {
	  // Read the input file and put it in memory
	  long start_pos = io->tell_proc(handle);
	  io->seek_proc(handle, 0, SEEK_END);
	  size_t file_length = (size_t)(io->tell_proc(handle) - start_pos);
	  io->seek_proc(handle, start_pos, SEEK_SET);
	  // one byte spare: a cut image chunk may need a pad byte
	  raw_data = (uint8_t*)malloc(file_length + 1);
	  if(!raw_data) {
		  throw FI_MSG_ERROR_MEMORY;
	  }
	  if(io->read_proc(raw_data, 1, (unsigned)file_length, handle) != file_length) {
		  throw "Error while reading input stream";
	  }
	  
	  // copy pointers (must be released later using free)
	  bitstream->bytes = raw_data;
	  bitstream->size = file_length;

	  return TRUE;

  } catch(const char *text) {
	  if(raw_data) {
		  free(raw_data);
	  }
	  memset(bitstream, 0, sizeof(WebPData));
	  if(NULL != text) {
		  FreeImage_OutputMessageProc(s_format_id, text);
	  }
	  return FALSE;
  }
}

static inline size_t
WebP_GetLE32(const uint8_t *p) {
	return (size_t)p[0] | ((size_t)p[1] << 8) | ((size_t)p[2] << 16) | ((size_t)p[3] << 24);
}

static inline void
WebP_PutLE32(uint8_t *p, size_t value) {
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

// a cut or damaged file: the RIFF ends after its last whole chunk, a cut image chunk keeps its bytes; FALSE if nothing changes
static BOOL
WebP_TrimToWholeChunks(WebPData *bitstream) {
	uint8_t *data = (uint8_t*)bitstream->bytes;
	const size_t size = bitstream->size;
	if((data == NULL) || (size < 20) || (memcmp(data, "RIFF", 4) != 0) || (memcmp(data + 8, "WEBP", 4) != 0)) {
		return FALSE;
	}
	size_t pos = 12;
	while(pos + 8 <= size) {
		const size_t room = size - pos - 8;
		const size_t length = WebP_GetLE32(data + pos + 4);
		if((length > room) || (length + (length & 1) > room)) {
			if((memcmp(data + pos, "VP8 ", 4) == 0) || (memcmp(data + pos, "VP8L", 4) == 0)) {
				// a whole payload lacking only its pad byte gets the spare one; a cut one keeps an even count, none made up
				const size_t have = (length <= room) ? length : (room & ~(size_t)1);
				WebP_PutLE32(data + pos + 4, have);
				if(have & 1) {
					data[size] = 0;
				}
				pos += 8 + have + (have & 1);
			}
			break;
		}
		pos += 8 + length + (length & 1);
	}
	if((pos <= 12) || ((pos == size) && (WebP_GetLE32(data + 4) == pos - 8))) {
		return FALSE;
	}
	WebP_PutLE32(data + 4, pos - 8);
	bitstream->size = pos;
	return TRUE;
}

// ----------------------------------------------------------
//   Helpers for the save function
// ----------------------------------------------------------

/**
Output function. Should return 1 if writing was successful.
data/data_size is the segment of data to write, and 'picture' is for
reference (and so one can make use of picture->custom_ptr).
*/
static int 
WebP_MemoryWriter(const BYTE *data, size_t data_size, const WebPPicture* const picture) {
	FIMEMORY *hmem = (FIMEMORY*)picture->custom_ptr;
	return data_size ? (FreeImage_WriteMemory(data, 1, (unsigned)data_size, hmem) == data_size) : 0;
}

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "WebP";
}

static const char * DLL_CALLCONV
Description() {
	return "Google WebP image format";
}

static const char * DLL_CALLCONV
Extension() {
	return "webp";
}

static const char * DLL_CALLCONV
RegExpr() {
	return NULL;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/webp";
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	BYTE riff_signature[4] = { 0x52, 0x49, 0x46, 0x46 };
	BYTE webp_signature[4] = { 0x57, 0x45, 0x42, 0x50 };
	BYTE signature[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

	io->read_proc(signature, 1, 12, handle);

	if(memcmp(riff_signature, signature, 4) == 0) {
		if(memcmp(webp_signature, signature + 8, 4) == 0) {
			return TRUE;
		}
	}

	return FALSE;
}

static BOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return (
		(depth == 24) || 
		(depth == 32)
		);
}

static BOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return (type == FIT_BITMAP) ? TRUE : FALSE;
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

static void * DLL_CALLCONV
Open(FreeImageIO *io, fi_handle handle, BOOL read) {
	WebPPluginData *state = (WebPPluginData*)calloc(1, sizeof(WebPPluginData));
	if(state == NULL) {
		return NULL;
	}
	state->anim_next = -1;
	state->cached_page = -1;
	state->frame_count = 1;

	if(read) {
		// read the input file and put it in memory
		if(!ReadFileToWebPData(io, handle, &state->bitstream)) {
			free(state);
			return NULL;
		}
		// create the MUX object; a cut or damaged file is read as far as its whole chunks go
		state->mux = WebPMuxCreate(&state->bitstream, 0);
		if((state->mux == NULL) && WebP_TrimToWholeChunks(&state->bitstream)) {
			state->mux = WebPMuxCreate(&state->bitstream, 0);
			if(state->mux != NULL) {
				FreeImage_OutputMessageProc(s_format_id, "Warning: the file is cut short or damaged; the frames it holds are kept");
			}
		}
		if(state->mux == NULL) {
			free((void*)state->bitstream.bytes);
			free(state);
			FreeImage_OutputMessageProc(s_format_id, "Failed to create mux object from file");
			return NULL;
		}

		uint32_t webp_flags = 0;
		if(WebPMuxGetFeatures(state->mux, &webp_flags) == WEBP_MUX_OK) {
			state->is_animation = (webp_flags & ANIMATION_FLAG) ? TRUE : FALSE;
		}
		if(state->is_animation) {
			int nframes = 0;
			WebPMuxAnimParams params;
			if((WebPMuxNumChunks(state->mux, WEBP_CHUNK_ANMF, &nframes) == WEBP_MUX_OK) && (nframes > 0)) {
				state->frame_count = nframes;
			}
			if(WebPMuxGetAnimationParams(state->mux, &params) == WEBP_MUX_OK) {
				state->loop_count = params.loop_count;
			}
			if(WebPMuxGetCanvasSize(state->mux, &state->canvas_width, &state->canvas_height) != WEBP_MUX_OK) {
				state->canvas_width = 0;
				state->canvas_height = 0;
			}
			// no canvas: a plain sequence of frames
			if((state->canvas_width <= 0) || (state->canvas_height <= 0)) {
				state->is_animation = FALSE;
			}
			// the demuxer is stricter than the mux: if it refuses, serve one image
			if(state->is_animation) {
				WebPDemuxer *demux = WebPDemux(&state->bitstream);
				if(demux == NULL) {
					state->is_animation = FALSE;
					state->frame_count = 1;
				} else {
					WebPDemuxDelete(demux);
				}
			}
		}
	} else {
		state->write = TRUE;
		// creates an empty mux object
		state->mux = WebPMuxNew();
		if(state->mux == NULL) {
			free(state);
			FreeImage_OutputMessageProc(s_format_id, "Failed to create empty mux object");
			return NULL;
		}
	}

	return state;
}

static void DLL_CALLCONV
Close(FreeImageIO *io, fi_handle handle, void *data) {
	WebPPluginData *state = (WebPPluginData*)data;
	if(state == NULL) {
		return;
	}

	// write a collected animation; a still image was written by Save()
	if(state->write && !state->written && (state->out_pages > 0) && (io != NULL) && (handle != NULL)) {
		WebP_FinishAnimation(state, io, handle);
	}
	WebPDataClear(&state->pending);

	// both point into bitstream: free them first
	if(state->anim != NULL) {
		WebPAnimDecoderDelete(state->anim);
	}
	WebP_ClearFrameCache(state);
	if(state->mux != NULL) {
		WebPMuxDelete(state->mux);
	}
	if(state->bitstream.bytes != NULL) {
		free((void*)state->bitstream.bytes);
	}
	free(state);
}

// ----------------------------------------------------------

static int DLL_CALLCONV
PageCount(FreeImageIO *io, fi_handle handle, void *data) {
	WebPPluginData *state = (WebPPluginData*)data;
	return (state != NULL) ? state->frame_count : 0;
}

// ----------------------------------------------------------

/**
Decode a WebP image and returns a FIBITMAP image
@param webp_image Raw WebP image
@param flags FreeImage load flags
@return Returns a dib if successfull, returns NULL otherwise
*/
static FIBITMAP *
DecodeImage(WebPData *webp_image, int flags) {
	FIBITMAP *dib = NULL;

	const uint8_t* data = webp_image->bytes;	// raw image data
	const size_t data_size = webp_image->size;	// raw image size

    VP8StatusCode webp_status = VP8_STATUS_OK;

	BOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

	// Main object storing the configuration for advanced decoding
	WebPDecoderConfig decoder_config;
	// Output buffer
	WebPDecBuffer* const output_buffer = &decoder_config.output;
	// Features gathered from the bitstream
	WebPBitstreamFeatures* const bitstream = &decoder_config.input;

	try {
		// Initialize the configuration as empty
		// This function must always be called first, unless WebPGetFeatures() is to be called
		if(!WebPInitDecoderConfig(&decoder_config)) {
			throw "Library version mismatch";
		}

		// Retrieve features from the bitstream
		// The bitstream structure is filled with information gathered from the bitstream
		webp_status = WebPGetFeatures(data, data_size, bitstream);
		if(webp_status != VP8_STATUS_OK) {
			throw FI_MSG_ERROR_PARSING;
		}

		// Allocate output dib

		unsigned bpp = bitstream->has_alpha ? 32 : 24;	
		unsigned width = (unsigned)bitstream->width;
		unsigned height = (unsigned)bitstream->height;

		dib = FreeImage_AllocateHeader(header_only, width, height, bpp, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
		if(!dib) {
			throw FI_MSG_ERROR_DIB_MEMORY;
		}

		if(header_only) {
			WebPFreeDecBuffer(output_buffer);
			return dib;
		}

		// --- Set decoding options ---

		// use multi-threaded decoding
		decoder_config.options.use_threads = 1;
		// set output color space
		output_buffer->colorspace = bitstream->has_alpha ? MODE_BGRA : MODE_BGR;

		// ---

		// decode the input stream, taking 'config' into account. 
		
		webp_status = WebPDecode(data, data_size, &decoder_config);
		unsigned rows = height;
		if(webp_status != VP8_STATUS_OK) {
			// the rows before a cut or damage: fed in pieces, short of the last byte, the incremental decoder keeps each piece's rows
			rows = 0;
			WebPIDecoder *idec = WebPIDecode(NULL, 0, &decoder_config);
			if(idec != NULL) {
				for(size_t fed = 0; fed + 1 < data_size; ) {
					fed = MIN(fed + 4096, data_size - 1);
					const VP8StatusCode status = WebPIUpdate(idec, data, fed);
					int last_y = 0;
					if(WebPIDecGetRGB(idec, &last_y, NULL, NULL, NULL) != NULL) {
						rows = (unsigned)MAX(last_y, 0);
					}
					if(status != VP8_STATUS_SUSPENDED) {
						break;
					}
				}
				WebPIDelete(idec);
			}
			if(rows == 0) {
				throw FI_MSG_ERROR_PARSING;
			}
		}

		// fill the dib with the decoded data

		const BYTE *src_bitmap = output_buffer->u.RGBA.rgba;
		const unsigned src_pitch = (unsigned)output_buffer->u.RGBA.stride;

		switch(bpp) {
			case 24:
				for(unsigned y = 0; y < rows; y++) {
					const BYTE *src_bits = src_bitmap + y * src_pitch;						
					BYTE *dst_bits = (BYTE*)FreeImage_GetScanLine(dib, height-1-y);
					for(unsigned x = 0; x < width; x++) {
						dst_bits[FI_RGBA_BLUE]	= src_bits[0];	// B
						dst_bits[FI_RGBA_GREEN]	= src_bits[1];	// G
						dst_bits[FI_RGBA_RED]	= src_bits[2];	// R
						src_bits += 3;
						dst_bits += 3;
					}
				}
				break;
			case 32:
				for(unsigned y = 0; y < rows; y++) {
					const BYTE *src_bits = src_bitmap + y * src_pitch;						
					BYTE *dst_bits = (BYTE*)FreeImage_GetScanLine(dib, height-1-y);
					for(unsigned x = 0; x < width; x++) {
						dst_bits[FI_RGBA_BLUE]	= src_bits[0];	// B
						dst_bits[FI_RGBA_GREEN]	= src_bits[1];	// G
						dst_bits[FI_RGBA_RED]	= src_bits[2];	// R
						dst_bits[FI_RGBA_ALPHA]	= src_bits[3];	// A
						src_bits += 4;
						dst_bits += 4;
					}
				}
				break;
		}

		// Free the decoder
		WebPFreeDecBuffer(output_buffer);

		if(rows < height) {
			PartialImageWarning(s_format_id, rows, height);
		}

		return dib;

	} catch (const char *text) {
		if(dib) {
			FreeImage_Unload(dib);
		}
		WebPFreeDecBuffer(output_buffer);

		if(NULL != text) {
			FreeImage_OutputMessageProc(s_format_id, text);
		}

		return NULL;
	}
}

// composited canvas of frame 'page'; the caller owns it
static FIBITMAP *
DecodeCompositedFrame(WebPPluginData *state, int page) {
	if((state->cached_frame != NULL) && (state->cached_page == page)) {
		return FreeImage_Clone(state->cached_frame);
	}

	if(state->anim == NULL) {
		WebPAnimDecoderOptions options;
		if(!WebPAnimDecoderOptionsInit(&options)) {
			FreeImage_OutputMessageProc(s_format_id, "Library version mismatch");
			return NULL;
		}
		// BGRA; the FI_RGBA_* indices below keep big-endian right
		options.color_mode = MODE_BGRA;
		options.use_threads = 1;
		state->anim = WebPAnimDecoderNew(&state->bitstream, &options);
		if(state->anim == NULL) {
			FreeImage_OutputMessageProc(s_format_id, "Failed to create the animation decoder");
			return NULL;
		}
		state->anim_next = 0;
	}

	// forward-only decoder: rewind to go back
	if((state->anim_next < 0) || (page < state->anim_next)) {
		WebPAnimDecoderReset(state->anim);
		state->anim_next = 0;
	}

	uint8_t *frame_rgba = NULL;
	int timestamp = 0;
	while(state->anim_next <= page) {
		if(!WebPAnimDecoderHasMoreFrames(state->anim) || !WebPAnimDecoderGetNext(state->anim, &frame_rgba, &timestamp)) {
			// force a rewind on the next call
			state->anim_next = -1;
			FreeImage_OutputMessageProc(s_format_id, "Failed to decode animation frame %d", page);
			return NULL;
		}
		state->anim_next++;
	}

	// frame_rgba is valid until the next decoder call
	FIBITMAP *dib = FreeImage_Allocate(state->canvas_width, state->canvas_height, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	if(dib == NULL) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_DIB_MEMORY);
		return NULL;
	}
	for(int y = 0; y < state->canvas_height; y++) {
		const BYTE *src_bits = (const BYTE*)frame_rgba + (size_t)y * (size_t)state->canvas_width * 4;
		BYTE *dst_bits = (BYTE*)FreeImage_GetScanLine(dib, state->canvas_height - 1 - y);
		for(int x = 0; x < state->canvas_width; x++) {
			dst_bits[FI_RGBA_BLUE]	= src_bits[0];	// B
			dst_bits[FI_RGBA_GREEN]	= src_bits[1];	// G
			dst_bits[FI_RGBA_RED]	= src_bits[2];	// R
			dst_bits[FI_RGBA_ALPHA]	= src_bits[3];	// A
			src_bits += 4;
			dst_bits += 4;
		}
	}

	// cache it; a failure here is harmless
	WebP_ClearFrameCache(state);
	state->cached_frame = FreeImage_Clone(dib);
	if(state->cached_frame != NULL) {
		state->cached_page = page;
	}

	return dib;
}

// GIF convention: FrameTime in ms, DisposalMethod 1 = leave, 2 = background
static void
SetFrameMetadata(FIBITMAP *dib, const WebPPluginData *state, const WebPMuxFrameInfo *webp_frame, int page) {
	LONG duration = (LONG)webp_frame->duration;
	WORD left = (WORD)webp_frame->x_offset;
	WORD top = (WORD)webp_frame->y_offset;
	BYTE disposal = (webp_frame->dispose_method == WEBP_MUX_DISPOSE_BACKGROUND) ? 2 : 1;
	BYTE blend = (webp_frame->blend_method == WEBP_MUX_NO_BLEND) ? 1 : 0;

	WebP_SetAnimTag(dib, "FrameTime", ANIMTAG_FRAMETIME, FIDT_LONG, 1, 4, &duration);
	WebP_SetAnimTag(dib, "FrameLeft", ANIMTAG_FRAMELEFT, FIDT_SHORT, 1, 2, &left);
	WebP_SetAnimTag(dib, "FrameTop", ANIMTAG_FRAMETOP, FIDT_SHORT, 1, 2, &top);
	WebP_SetAnimTag(dib, "DisposalMethod", ANIMTAG_DISPOSALMETHOD, FIDT_BYTE, 1, 1, &disposal);
	WebP_SetAnimTag(dib, "BlendMethod", ANIMTAG_BLENDMETHOD, FIDT_BYTE, 1, 1, &blend);

	// on every frame, so deleting page 0 keeps the canvas
	if(state->is_animation) {
		WORD logicalwidth = (WORD)state->canvas_width;
		WORD logicalheight = (WORD)state->canvas_height;
		LONG loop = (LONG)state->loop_count;
		WebP_SetAnimTag(dib, "LogicalWidth", ANIMTAG_LOGICALWIDTH, FIDT_SHORT, 1, 2, &logicalwidth);
		WebP_SetAnimTag(dib, "LogicalHeight", ANIMTAG_LOGICALHEIGHT, FIDT_SHORT, 1, 2, &logicalheight);
		WebP_SetAnimTag(dib, "Loop", ANIMTAG_LOOP, FIDT_LONG, 1, 4, &loop);
	}
}

// a tag of the wrong type counts as absent
static BOOL
WebP_GetAnimTag(FIBITMAP *dib, const char *key, FREE_IMAGE_MDTYPE type, LONG *value) {
	FITAG *tag = NULL;

	if(!FreeImage_GetMetadata(FIMD_ANIMATION, dib, key, &tag) || (tag == NULL)) {
		return FALSE;
	}
	if(FreeImage_GetTagType(tag) != type) {
		return FALSE;
	}
	const void *v = FreeImage_GetTagValue(tag);
	if(v == NULL) {
		return FALSE;
	}

	switch(type) {
		case FIDT_BYTE:
			*value = (LONG)*(const BYTE*)v;
			return TRUE;
		case FIDT_SHORT:
			*value = (LONG)*(const WORD*)v;
			return TRUE;
		case FIDT_LONG:
			*value = (LONG)*(const DWORD*)v;
			return TRUE;
		default:
			return FALSE;
	}
}

// FrameTime marks a page of an animation
static BOOL
WebP_IsFrame(FIBITMAP *dib) {
	LONG value = 0;
	return WebP_GetAnimTag(dib, "FrameTime", FIDT_LONG, &value);
}

// inverse of SetFrameMetadata()
static void
WebP_ReadFrameInfo(FIBITMAP *dib, WebPMuxFrameInfo *frame) {
	LONG value = 0;

	memset(frame, 0, sizeof(*frame));
	frame->id = WEBP_CHUNK_ANMF;
	frame->dispose_method = WEBP_MUX_DISPOSE_NONE;
	frame->blend_method = WEBP_MUX_BLEND;

	if(WebP_GetAnimTag(dib, "FrameTime", FIDT_LONG, &value)) {
		frame->duration = (int)value;
	}
	// WebP offsets are even; the mux rounds down too
	if(WebP_GetAnimTag(dib, "FrameLeft", FIDT_SHORT, &value)) {
		frame->x_offset = ((int)value) & ~1;
	}
	if(WebP_GetAnimTag(dib, "FrameTop", FIDT_SHORT, &value)) {
		frame->y_offset = ((int)value) & ~1;
	}
	// GIF numbering: 2 = background, else leave
	if(WebP_GetAnimTag(dib, "DisposalMethod", FIDT_BYTE, &value)) {
		frame->dispose_method = (value == 2) ? WEBP_MUX_DISPOSE_BACKGROUND : WEBP_MUX_DISPOSE_NONE;
	}
	if(WebP_GetAnimTag(dib, "BlendMethod", FIDT_BYTE, &value)) {
		frame->blend_method = (value == 1) ? WEBP_MUX_NO_BLEND : WEBP_MUX_BLEND;
	}
}

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	WebPPluginData *state = NULL;
	WebPMux *mux = NULL;
	WebPMuxFrameInfo webp_frame = { 0 };	// raw image
	WebPData color_profile;	// ICC raw data
	WebPData xmp_metadata;	// XMP raw data
	WebPData exif_metadata;	// EXIF raw data
	FIBITMAP *dib = NULL;
	WebPMuxError error_status;

	if(!handle) {
		return NULL;
	}

	try {
		// get the MUX object
		state = (WebPPluginData*)data;
		if(!state || !state->mux) {
			throw (1);
		}
		mux = state->mux;

		// FreeImage_Load passes -1
		if(page == -1) {
			page = 0;
		}
		if((page < 0) || (page >= state->frame_count)) {
			throw (1);
		}

		const BOOL header_only = ((flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS) ? TRUE : FALSE;
		const BOOL playback = (state->is_animation && ((flags & WEBP_PLAYBACK) == WEBP_PLAYBACK)) ? TRUE : FALSE;

		// gets the feature flags from the mux object
		uint32_t webp_flags = 0;
		error_status = WebPMuxGetFeatures(mux, &webp_flags);
		if(error_status != WEBP_MUX_OK) {
			throw (1);
		}

		// get image data
		error_status = WebPMuxGetFrame(mux, page + 1, &webp_frame);

		if(error_status == WEBP_MUX_OK) {
			if(playback) {
				dib = header_only ?
					FreeImage_AllocateHeader(TRUE, state->canvas_width, state->canvas_height, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK) :
					DecodeCompositedFrame(state, page);
			} else {
				// decode the data (can be limited to the header if flags uses FIF_LOAD_NOPIXELS)
				dib = DecodeImage(&webp_frame.bitstream, flags);
			}
			if(!dib) {
				throw (1);
			}

			if(state->is_animation) {
				SetFrameMetadata(dib, state, &webp_frame, page);
			}

			// get ICC profile
			if(webp_flags & ICCP_FLAG) {
				error_status = WebPMuxGetChunk(mux, "ICCP", &color_profile);
				if(error_status == WEBP_MUX_OK) {
					FreeImage_CreateICCProfile(dib, (void*)color_profile.bytes, (long)color_profile.size);
				}
			}

			// get XMP metadata
			if(webp_flags & XMP_FLAG) {
				error_status = WebPMuxGetChunk(mux, "XMP ", &xmp_metadata);
				if(error_status == WEBP_MUX_OK) {
					// create a tag
					FITAG *tag = FreeImage_CreateTag();
					if(tag) {
						FreeImage_SetTagKey(tag, g_TagLib_XMPFieldName);
						FreeImage_SetTagLength(tag, (DWORD)xmp_metadata.size);
						FreeImage_SetTagCount(tag, (DWORD)xmp_metadata.size);
						FreeImage_SetTagType(tag, FIDT_ASCII);
						FreeImage_SetTagValue(tag, xmp_metadata.bytes);
						
						// store the tag
						FreeImage_SetMetadata(FIMD_XMP, dib, FreeImage_GetTagKey(tag), tag);

						// destroy the tag
						FreeImage_DeleteTag(tag);
					}
				}
			}

			// get Exif metadata
			if(webp_flags & EXIF_FLAG) {
				error_status = WebPMuxGetChunk(mux, "EXIF", &exif_metadata);
				if(error_status == WEBP_MUX_OK) {
					// read the Exif raw data as a blob
					jpeg_read_exif_profile_raw(dib, exif_metadata.bytes, (unsigned)exif_metadata.size);
					// read and decode the Exif data
					jpeg_read_exif_profile(dib, exif_metadata.bytes, (unsigned)exif_metadata.size);
				}
			}
		}

		WebPDataClear(&webp_frame.bitstream);

		return dib;

	} catch(int) {
		WebPDataClear(&webp_frame.bitstream);
		return NULL;
	}
}

// --------------------------------------------------------------------------

/**
Encode a FIBITMAP to a WebP image
@param hmem Memory output stream, containing on return the encoded image
@param dib The FIBITMAP to encode
@param flags FreeImage save flags
@return Returns TRUE if successfull, returns FALSE otherwise
*/
static BOOL
EncodeImage(FIMEMORY *hmem, FIBITMAP *dib, int flags) {
	WebPPicture picture;	// Input buffer
	WebPConfig config;		// Coding parameters

	BOOL bIsFlipped = FALSE;

	// zeroed so the catch block can always free it
	memset(&picture, 0, sizeof(picture));

	try {
		const unsigned width = FreeImage_GetWidth(dib);
		const unsigned height = FreeImage_GetHeight(dib);
		const unsigned bpp = FreeImage_GetBPP(dib);
		const unsigned pitch = FreeImage_GetPitch(dib);

		// check image type
		FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib);

		if( !((image_type == FIT_BITMAP) && ((bpp == 24) || (bpp == 32))) )  {
			throw FI_MSG_ERROR_UNSUPPORTED_FORMAT;
		}

		// check format limits
		if(MAX(width, height) > WEBP_MAX_DIMENSION) {
			FreeImage_OutputMessageProc(s_format_id, "Unsupported image size: width x height = %d x %d", width, height);
			return FALSE;
		}

		// Initialize output I/O
		if(WebPPictureInit(&picture) == 1) {
			picture.writer = WebP_MemoryWriter;
			picture.custom_ptr = hmem;
			picture.width = (int)width;
			picture.height = (int)height;
		} else {
			throw "Couldn't initialize WebPPicture";
		}

		// --- Set encoding parameters ---

		// Initialize encoding parameters to default values
		WebPConfigInit(&config);

		// quality/speed trade-off (0=fast, 6=slower-better)
		config.method = 6;

		if((flags & WEBP_LOSSLESS) == WEBP_LOSSLESS) {
			// lossless encoding
			config.lossless = 1;
			picture.use_argb = 1;
		} else if((flags & 0x7F) > 0) {
			// lossy encoding
			config.lossless = 0;
			// quality is between 1 (smallest file) and 100 (biggest) - default to 75
			config.quality = (float)(flags & 0x7F);
			if(config.quality > 100) {
				config.quality = 100;
			}
		}

		// validate encoding parameters
		if(WebPValidateConfig(&config) == 0) {
			throw "Failed to initialize encoder";
		}

		// --- Perform encoding ---
		
		// Invert dib scanlines
		bIsFlipped = FreeImage_FlipVertical(dib);


		// convert dib buffer to output stream

		const BYTE *bits = FreeImage_GetBits(dib);

#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_BGR
		switch(bpp) {
			case 24:
				WebPPictureImportBGR(&picture, bits, pitch);
				break;
			case 32:
				WebPPictureImportBGRA(&picture, bits, pitch);
				break;
		}
#else
		switch(bpp) {
			case 24:
				WebPPictureImportRGB(&picture, bits, pitch);
				break;
			case 32:
				WebPPictureImportRGBA(&picture, bits, pitch);
				break;
		}

#endif // FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_BGR

		if(!WebPEncode(&config, &picture)) {
			throw "Failed to encode image";
		}

		WebPPictureFree(&picture);

		if(bIsFlipped) {
			// invert dib scanlines
			FreeImage_FlipVertical(dib);
		}

		return TRUE;

	} catch (const char* text) {

		WebPPictureFree(&picture);

		if(bIsFlipped) {
			// invert dib scanlines
			FreeImage_FlipVertical(dib);
		}

		if(NULL != text) {
			FreeImage_OutputMessageProc(s_format_id, text);
		}
	}

	return FALSE;
}

// the caller owns 'out'
static BOOL
WebP_EncodeToData(FIBITMAP *dib, int flags, WebPData *out) {
	FIMEMORY *hmem = FreeImage_OpenMemory();
	BYTE *data = NULL;
	DWORD size = 0;
	BOOL bResult = FALSE;

	memset(out, 0, sizeof(*out));

	if(hmem == NULL) {
		return FALSE;
	}
	if(EncodeImage(hmem, dib, flags) && FreeImage_AcquireMemory(hmem, &data, &size) && (size > 0)) {
		uint8_t *copy = (uint8_t*)WebPMalloc(size);
		if(copy != NULL) {
			memcpy(copy, data, size);
			out->bytes = copy;
			out->size = size;
			bResult = TRUE;
		}
	}
	FreeImage_CloseMemory(hmem);

	return bResult;
}

// once per file, from the first frame
static BOOL
WebP_SetMetadataChunks(WebPMux *mux, FIBITMAP *dib) {
	const int copy_data = 1;

	{
		FIICCPROFILE *iccProfile = FreeImage_GetICCProfile(dib);
		if(iccProfile->size && iccProfile->data) {
			WebPData icc_profile;
			icc_profile.bytes = (uint8_t*)iccProfile->data;
			icc_profile.size = (size_t)iccProfile->size;
			if(WebPMuxSetChunk(mux, "ICCP", &icc_profile, copy_data) != WEBP_MUX_OK) {
				return FALSE;
			}
		}
	}
	{
		FITAG *tag = NULL;
		if(FreeImage_GetMetadata(FIMD_XMP, dib, g_TagLib_XMPFieldName, &tag)) {
			WebPData xmp_profile;
			xmp_profile.bytes = (uint8_t*)FreeImage_GetTagValue(tag);
			xmp_profile.size = (size_t)FreeImage_GetTagLength(tag);
			if(WebPMuxSetChunk(mux, "XMP ", &xmp_profile, copy_data) != WEBP_MUX_OK) {
				return FALSE;
			}
		}
	}
	{
		FITAG *tag = NULL;
		if(FreeImage_GetMetadata(FIMD_EXIF_RAW, dib, g_TagLib_ExifRawFieldName, &tag)) {
			WebPData exif_profile;
			exif_profile.bytes = (uint8_t*)FreeImage_GetTagValue(tag);
			exif_profile.size = (size_t)FreeImage_GetTagLength(tag);
			if(WebPMuxSetChunk(mux, "EXIF", &exif_profile, copy_data) != WEBP_MUX_OK) {
				return FALSE;
			}
		}
	}
	return TRUE;
}

static BOOL
WebP_AssembleAndWrite(WebPMux *mux, FreeImageIO *io, fi_handle handle) {
	WebPData output_data = { 0 };
	BOOL bResult = FALSE;

	if(WebPMuxAssemble(mux, &output_data) != WEBP_MUX_OK) {
		FreeImage_OutputMessageProc(s_format_id, "Failed to create webp output file");
	} else if(io->write_proc((void*)output_data.bytes, 1, (unsigned)output_data.size, handle) != output_data.size) {
		FreeImage_OutputMessageProc(s_format_id, "Failed to write webp output file");
	} else {
		bResult = TRUE;
	}

	WebPDataClear(&output_data);

	return bResult;
}

// push the held first frame; no-op once pushed
static BOOL
WebP_FlushPending(WebPPluginData *state) {
	WebPMuxError error_status;

	if(state->pending.bytes == NULL) {
		return TRUE;
	}

	state->pending_info.bitstream = state->pending;
	error_status = WebPMuxPushFrame(state->mux, &state->pending_info, 1 /* copy */);

	WebPDataClear(&state->pending);
	memset(&state->pending_info, 0, sizeof(state->pending_info));

	if(error_status != WEBP_MUX_OK) {
		FreeImage_OutputMessageProc(s_format_id, "Failed to add a frame to the animation");
		return FALSE;
	}
	return TRUE;
}

// widen the canvas to cover every frame: WebPMuxAssemble refuses overflow
static BOOL
WebP_FinishAnimation(WebPPluginData *state, FreeImageIO *io, fi_handle handle) {
	int width = state->out_canvas_width;
	int height = state->out_canvas_height;

	if(!WebP_FlushPending(state)) {
		return FALSE;
	}

	if(state->out_bound_width > width) {
		if(width > 0) {
			FreeImage_OutputMessageProc(s_format_id,
				"The frames need a %d pixel wide canvas, not the %d the pages asked for",
				state->out_bound_width, width);
		}
		width = state->out_bound_width;
	}
	if(state->out_bound_height > height) {
		if(height > 0) {
			FreeImage_OutputMessageProc(s_format_id,
				"The frames need a %d pixel tall canvas, not the %d the pages asked for",
				state->out_bound_height, height);
		}
		height = state->out_bound_height;
	}

	if((width > 0) && (height > 0)) {
		if(WebPMuxSetCanvasSize(state->mux, width, height) != WEBP_MUX_OK) {
			FreeImage_OutputMessageProc(s_format_id, "Failed to set the animation canvas size");
			return FALSE;
		}
	}
	{
		WebPMuxAnimParams params;
		params.bgcolor = 0;					// transparent
		params.loop_count = state->out_loop;
		if(WebPMuxSetAnimationParams(state->mux, &params) != WEBP_MUX_OK) {
			FreeImage_OutputMessageProc(s_format_id, "Failed to set the animation parameters");
			return FALSE;
		}
	}

	return WebP_AssembleAndWrite(state->mux, io, handle);
}

static BOOL DLL_CALLCONV
Save(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	WebPPluginData *state = (WebPPluginData*)data;
	WebPData bitstream = { 0 };
	BOOL bResult = FALSE;

	if(!dib || !handle || !state || !state->mux) {
		return FALSE;
	}

	// refuse here: Close() cannot report a failure

	if(!FreeImage_HasPixels(dib)) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_UNSUPPORTED_FORMAT);
		return FALSE;
	}

	if((FreeImage_GetImageType(dib) != FIT_BITMAP) || !SupportsExportDepth(FreeImage_GetBPP(dib))) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_UNSUPPORTED_FORMAT);
		return FALSE;
	}

	const BOOL as_frame = (page >= 0) || WebP_IsFrame(dib);

	if(!as_frame) {
		if(!WebP_EncodeToData(dib, flags, &bitstream)) {
			goto done;
		}
		if(WebPMuxSetImage(state->mux, &bitstream, 1 /* copy */) != WEBP_MUX_OK) {
			goto done;
		}
		if(!WebP_SetMetadataChunks(state->mux, dib)) {
			goto done;
		}
		bResult = WebP_AssembleAndWrite(state->mux, io, handle);
		state->written = TRUE;
		goto done;
	}

	// --- a frame of an animation ---

	if(!WebP_EncodeToData(dib, flags, &bitstream)) {
		goto done;
	}

	{
		WebPMuxFrameInfo frame;
		WebP_ReadFrameInfo(dib, &frame);

		{
			const int right = frame.x_offset + (int)FreeImage_GetWidth(dib);
			const int bottom = frame.y_offset + (int)FreeImage_GetHeight(dib);
			if(right > state->out_bound_width) {
				state->out_bound_width = right;
			}
			if(bottom > state->out_bound_height) {
				state->out_bound_height = bottom;
			}
		}

		// canvas and loop may come from any page
		{
			LONG value = 0;
			if(WebP_GetAnimTag(dib, "LogicalWidth", FIDT_SHORT, &value) && ((int)value > state->out_canvas_width)) {
				state->out_canvas_width = (int)value;
			}
			if(WebP_GetAnimTag(dib, "LogicalHeight", FIDT_SHORT, &value) && ((int)value > state->out_canvas_height)) {
				state->out_canvas_height = (int)value;
			}
			if((state->out_pages == 0) && WebP_GetAnimTag(dib, "Loop", FIDT_LONG, &value)) {
				state->out_loop = (int)value;
			}
		}

		if(state->out_pages == 0) {
			// hold the first frame: alone it is a still image
			if(!WebP_SetMetadataChunks(state->mux, dib)) {
				goto done;
			}

			state->pending = bitstream;
			state->pending_info = frame;
			memset(&bitstream, 0, sizeof(bitstream));	// owned by the state
			state->out_pages = 1;
			bResult = TRUE;
			goto done;
		}

		if(!WebP_FlushPending(state)) {
			goto done;
		}

		frame.bitstream = bitstream;
		if(WebPMuxPushFrame(state->mux, &frame, 1 /* copy */) != WEBP_MUX_OK) {
			FreeImage_OutputMessageProc(s_format_id, "Failed to add a frame to the animation");
			goto done;
		}
		state->out_pages++;
		bResult = TRUE;
	}

done:
	WebPDataClear(&bitstream);
	return bResult;
}

// ==========================================================
//	 Init
// ==========================================================

void DLL_CALLCONV
InitWEBP(Plugin *plugin, int format_id) {
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
	plugin->save_proc = Save;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = SupportsICCProfiles;
	plugin->supports_no_pixels_proc = SupportsNoPixels;
}

