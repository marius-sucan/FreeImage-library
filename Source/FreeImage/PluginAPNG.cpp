// ==========================================================
// APNG (Animated PNG) Loader and Writer
//
// Design and implementation by
// - Marius Sucan
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

/*
An APNG is an ordinary PNG with three extra chunks: acTL says the file is animated,
one fcTL introduces every frame, and fdAT carries the frames after the first. The
frame data itself is PNG image data - the same zlib stream an IDAT holds, for an
image of the frame's own size, in the colour type and bit depth the file's single
IHDR declares.

That is the whole of it, and it is why this plugin parses the container itself and
leaves the pixels to the code that already reads PNG: a frame is turned back into a
standalone PNG in memory - signature, IHDR with the frame's width and height, the
file's palette and ancillary chunks, the frame's data as IDAT, IEND - and handed to
FIF_PNG. Every colour type, bit depth, transparency table, ICC profile and text
chunk PluginPNG.cpp understands therefore works here too, for free. The writer is
the same trick backwards: libpng compresses each frame as a standalone PNG and the
chunks of that PNG are split into the ancillary chunks (kept once, in the header)
and the IDAT payload (which becomes the frame's data).

Two things are worth knowing about the writer:

- It buffers. num_frames has to be written in acTL, which precedes the first frame,
  and the number of pages is only known once the last one has been saved - so the
  frames are compressed as they arrive and the file is written in Close(). The peak
  cost is the size of the file being written, which is what PluginWebP.cpp's mux
  already costs.
- An animation is written as 8-bit RGBA. Every frame of an APNG shares one IHDR, so
  the colour type has to be settled before the second frame is even seen; RGBA is
  the one choice that can hold whatever the remaining frames turn out to be. A
  single page is not an animation and is written as a plain PNG, with its palette,
  bit depth and metadata intact.

Frames that cover the whole canvas are reduced to the rectangle that differs from
the frame before them, which is what makes the output a fraction of the size of the
frames concatenated. It is exact, not an approximation: APNG_BLEND_OP_SOURCE
overwrites the rectangle it covers, so drawing only the pixels that changed leaves
the canvas identical to drawing all of them.

Reference: PNG Third Edition, 11.3.4 (Animation information), which standardised
https://wiki.mozilla.org/APNG_Specification
*/

#ifdef _MSC_VER
#pragma warning (disable : 4786) // identifier was truncated to 'number' characters
#endif

#include "FreeImage.h"
#include "Utilities.h"
#include "Plugin.h"

#include "../Metadata/FreeImageTag.h"

#undef PNG_Z_DEFAULT_COMPRESSION	// already used in ../LibPNG/pnglibconf.h

#include "../ZLib/zlib.h"
#include "../LibPNG/png.h"

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ==========================================================
// Format constants
// ==========================================================

#define APNG_DISPOSE_OP_NONE		0	//! leave the output buffer as it is
#define APNG_DISPOSE_OP_BACKGROUND	1	//! clear the frame's rectangle to transparent black
#define APNG_DISPOSE_OP_PREVIOUS	2	//! put back what was there before this frame drew

#define APNG_BLEND_OP_SOURCE		0	//! the frame overwrites its rectangle, alpha included
#define APNG_BLEND_OP_OVER			1	//! the frame is alpha-composited over the buffer

// The "DisposalMethod" tag of FIMD_ANIMATION carries GIF's numbering, which every
// FreeImage caller that walks an animation already speaks - see PluginGIF.cpp
#define GIF_DISPOSAL_UNSPECIFIED	0
#define GIF_DISPOSAL_LEAVE			1
#define GIF_DISPOSAL_BACKGROUND		2
#define GIF_DISPOSAL_PREVIOUS		3

// the largest a PNG chunk may be
#define APNG_MAX_CHUNK_LENGTH		0x7FFFFFFFUL

// ==========================================================
// Chunk model
// ==========================================================

/**
One frame: where it is drawn, how long it stays, what happens to it afterwards, and
its compressed image data - the concatenation of the IDAT or fdAT payloads that
carried it, which is exactly the zlib stream a standalone PNG of that frame holds.
*/
struct APNGFrame {
	DWORD width, height;
	DWORD x_offset, y_offset;
	WORD delay_num, delay_den;
	BYTE dispose_op, blend_op;
	std::vector<BYTE> data;

	APNGFrame() : width(0), height(0), x_offset(0), y_offset(0), delay_num(0), delay_den(100),
		dispose_op(APNG_DISPOSE_OP_NONE), blend_op(APNG_BLEND_OP_SOURCE) {
	}
};

/**
What Open() hands to the other entry points.
*/
struct APNGinfo {
	BOOL read;

	// ---------- reading ----------

	BYTE ihdr[13];					//! the file's IHDR payload, reused for every frame with its own size
	DWORD canvas_width;				//! the output buffer the frames are drawn on, from IHDR
	DWORD canvas_height;
	std::vector<BYTE> ancillary;	//! PLTE and the ancillary chunks, verbatim, replayed into every frame
	std::vector<APNGFrame> frames;
	DWORD num_plays;				//! 0 means loop forever
	BOOL animated;					//! FALSE for a plain PNG, which is read as a single page

	// APNG_PLAYBACK cache, the same shape as the one in PluginGIF.cpp: compositing a
	// frame needs the canvas the frame before it left behind, so keeping that canvas
	// turns walking an animation in order into one frame of work per frame instead of
	// replaying it from the start each time. previous_canvas is only filled for a
	// frame that disposes with APNG_DISPOSE_OP_PREVIOUS, which is the one case where
	// the canvas before the frame drew is needed again.
	FIBITMAP *canvas;
	FIBITMAP *previous_canvas;
	int canvas_page;				//! the frame `canvas` shows, -1 when there is none

	// ---------- writing ----------

	FIBITMAP *pending;				//! the first page, held until a second one proves this is an animation
	int pending_flags;				//! the save flags that page came with
	int out_pages;					//! how many pages Save() has taken
	std::vector<BYTE> out_ancillary;//! the ancillary chunks frame 0's encoder produced
	std::vector<APNGFrame> out_frames;
	DWORD out_width, out_height;	//! the canvas, grown as needed to hold every frame
	DWORD out_plays;
	FIBITMAP *out_previous;			//! the previous frame, whole canvas - NULL when it cannot be diffed against

	APNGinfo() : read(FALSE), canvas_width(0), canvas_height(0), num_plays(0), animated(FALSE),
		canvas(NULL), previous_canvas(NULL), canvas_page(-1),
		pending(NULL), pending_flags(0), out_pages(0), out_width(0), out_height(0), out_plays(0),
		out_previous(NULL) {
		memset(ihdr, 0, sizeof(ihdr));
	}

	~APNGinfo() {
		if(canvas) FreeImage_Unload(canvas);
		if(previous_canvas) FreeImage_Unload(previous_canvas);
		if(pending) FreeImage_Unload(pending);
		if(out_previous) FreeImage_Unload(out_previous);
	}
};

// ==========================================================
// Byte order and chunk helpers - PNG is big endian, whatever the host is
// ==========================================================

static inline DWORD
GetDWORD(const BYTE *p) {
	return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | (DWORD)p[3];
}

static inline WORD
GetWORD(const BYTE *p) {
	return (WORD)(((WORD)p[0] << 8) | (WORD)p[1]);
}

static inline void
PutDWORD(BYTE *p, DWORD value) {
	p[0] = (BYTE)(value >> 24);
	p[1] = (BYTE)(value >> 16);
	p[2] = (BYTE)(value >> 8);
	p[3] = (BYTE)value;
}

static inline void
PutWORD(BYTE *p, WORD value) {
	p[0] = (BYTE)(value >> 8);
	p[1] = (BYTE)value;
}

/**
Append one chunk - length, type, data, CRC - to a buffer.
*/
static void
AppendChunk(std::vector<BYTE>& out, const char *type, const BYTE *data, size_t length) {
	BYTE header[8];
	PutDWORD(header, (DWORD)length);
	memcpy(header + 4, type, 4);
	out.insert(out.end(), header, header + 8);
	if(length > 0) {
		out.insert(out.end(), data, data + length);
	}

	uLong crc = crc32(0, (const Bytef*)type, 4);
	if(length > 0) {
		crc = crc32(crc, (const Bytef*)data, (uInt)length);
	}
	BYTE trailer[4];
	PutDWORD(trailer, (DWORD)crc);
	out.insert(out.end(), trailer, trailer + 4);
}

/**
Write one chunk straight to the output stream. The payload comes in two pieces so
that an fdAT can put its sequence number in front of the frame data without first
copying the two together.
*/
static BOOL
WriteChunk(FreeImageIO *io, fi_handle handle, const char *type, const BYTE *prefix, size_t prefix_length, const BYTE *data, size_t length) {
	const size_t total = prefix_length + length;
	if(total > APNG_MAX_CHUNK_LENGTH) {
		return FALSE;
	}

	BYTE header[8];
	PutDWORD(header, (DWORD)total);
	memcpy(header + 4, type, 4);
	if(io->write_proc(header, 1, 8, handle) != 8) {
		return FALSE;
	}

	uLong crc = crc32(0, (const Bytef*)type, 4);
	if(prefix_length > 0) {
		if(io->write_proc((void*)prefix, 1, (unsigned)prefix_length, handle) != prefix_length) {
			return FALSE;
		}
		crc = crc32(crc, (const Bytef*)prefix, (uInt)prefix_length);
	}
	if(length > 0) {
		if(io->write_proc((void*)data, 1, (unsigned)length, handle) != length) {
			return FALSE;
		}
		crc = crc32(crc, (const Bytef*)data, (uInt)length);
	}

	BYTE trailer[4];
	PutDWORD(trailer, (DWORD)crc);
	return (io->write_proc(trailer, 1, 4, handle) == 4) ? TRUE : FALSE;
}

/**
Read length bytes onto the end of a buffer, in bounded steps: a chunk header may
claim 2GB, and a file that claims it without holding it must not cost 2GB of memory
before the read fails.
@return Returns TRUE if all of it was there, and leaves the buffer untouched otherwise
*/
static BOOL
ReadBytes(FreeImageIO *io, fi_handle handle, std::vector<BYTE>& out, DWORD length) {
	const DWORD step = 64 * 1024;
	const size_t start = out.size();

	DWORD done = 0;
	while(done < length) {
		const DWORD n = MIN(step, length - done);
		out.resize(start + done + n);
		if(io->read_proc(&out[start + done], 1, n, handle) != n) {
			out.resize(start);
			return FALSE;
		}
		done += n;
	}
	return TRUE;
}

// ==========================================================
// Reading: parsing the container
// ==========================================================

/**
Is this stream an *animated* PNG? Read from the current position, which is left
somewhere in the middle of the file - both callers are reached through
FreeImage_ValidateFIF(), which puts it back.

This is what tells FIF_APNG and FIF_PNG apart: both read the same signature, and
the acTL chunk - which the format requires to come before IDAT - is the only thing
that says the file has more than one image in it.
*/
BOOL
APNG_IsAnimatedStream(FreeImageIO *io, fi_handle handle) {
	BYTE signature[8];

	if(io->read_proc(signature, 1, 8, handle) != 8) {
		return FALSE;
	}
	if(png_sig_cmp(signature, (png_size_t)0, 8) != 0) {
		return FALSE;
	}

	// acTL precedes IDAT, so however big the file is only its header is walked here
	for(int guard = 0; guard < 1024; guard++) {
		BYTE header[8];
		if(io->read_proc(header, 1, 8, handle) != 8) {
			return FALSE;
		}
		const DWORD length = GetDWORD(header);
		if(length > APNG_MAX_CHUNK_LENGTH - 4) {
			return FALSE;
		}
		if(memcmp(header + 4, "acTL", 4) == 0) {
			return TRUE;
		}
		if((memcmp(header + 4, "IDAT", 4) == 0) || (memcmp(header + 4, "IEND", 4) == 0)) {
			return FALSE;
		}
		if(io->seek_proc(handle, (long)(length + 4), SEEK_CUR) != 0) {
			return FALSE;
		}
	}

	return FALSE;
}

/**
Fill a frame from the 26 bytes of an fcTL payload, and check that it lies inside the
canvas - a frame that does not is what turns a decoder's compositing loop into an
out of bounds write.
*/
static BOOL
ParseFrameControl(const BYTE *payload, DWORD canvas_width, DWORD canvas_height, APNGFrame *frame) {
	frame->width    = GetDWORD(payload + 4);
	frame->height   = GetDWORD(payload + 8);
	frame->x_offset = GetDWORD(payload + 12);
	frame->y_offset = GetDWORD(payload + 16);
	frame->delay_num = GetWORD(payload + 20);
	frame->delay_den = GetWORD(payload + 22);
	frame->dispose_op = payload[24];
	frame->blend_op = payload[25];

	if((frame->width == 0) || (frame->height == 0)) {
		return FALSE;
	}
	// written this way round, neither sum can overflow
	if((frame->x_offset > canvas_width - frame->width) || (frame->y_offset > canvas_height - frame->height)) {
		return FALSE;
	}
	if(frame->width > canvas_width || frame->height > canvas_height) {
		return FALSE;
	}
	if(frame->dispose_op > APNG_DISPOSE_OP_PREVIOUS) {
		return FALSE;
	}
	if(frame->blend_op > APNG_BLEND_OP_OVER) {
		return FALSE;
	}
	// "if the denominator is 0, it is to be treated as if it were 100"
	if(frame->delay_den == 0) {
		frame->delay_den = 100;
	}
	return TRUE;
}

/**
Walk the file once, collecting what the frames are made of.

A file whose animation chunks stop making sense is not thrown away: the animation
is abandoned and the default image - the one every PNG decoder sees - is kept and
served as a single page. That is what the format asks of a decoder, and what the
conformance suite's "invalid images" expect to happen to them.
*/
static BOOL
ParseStream(FreeImageIO *io, fi_handle handle, APNGinfo *info) {
	BYTE signature[8];

	if(io->read_proc(signature, 1, 8, handle) != 8) {
		return FALSE;
	}
	if(png_sig_cmp(signature, (png_size_t)0, 8) != 0) {
		return FALSE;
	}

	std::vector<BYTE> default_image;	//! the IDAT stream - a frame only if an fcTL came before it
	APNGFrame default_frame;			//! and that fcTL's contents
	BOOL have_ihdr = FALSE;
	BOOL have_actl = FALSE;
	BOOL seen_idat = FALSE;
	BOOL idat_is_frame = FALSE;
	BOOL broken = FALSE;				//! the animation chunks stopped adding up; the default image has not
	DWORD expected_seq = 0;				//! fcTL and fdAT share one sequence, "in order, with no gaps"
	int current = -1;					//! the frame fdAT payloads belong to

	for(;;) {
		BYTE header[8];
		if(io->read_proc(header, 1, 8, handle) != 8) {
			break;
		}
		const DWORD length = GetDWORD(header);
		const BYTE *type = header + 4;
		if(length > APNG_MAX_CHUNK_LENGTH) {
			break;
		}
		DWORD consumed = 0;				//! how much of the payload the branch below took

		if(memcmp(type, "IHDR", 4) == 0) {
			if(have_ihdr || (length != 13) || (io->read_proc(info->ihdr, 1, 13, handle) != 13)) {
				return FALSE;
			}
			consumed = 13;
			info->canvas_width = GetDWORD(info->ihdr);
			info->canvas_height = GetDWORD(info->ihdr + 4);
			if((info->canvas_width == 0) || (info->canvas_height == 0) ||
			   (info->canvas_width > 0x7FFFFFFF) || (info->canvas_height > 0x7FFFFFFF)) {
				return FALSE;
			}
			have_ihdr = TRUE;

		} else if(memcmp(type, "IDAT", 4) == 0) {
			if(!have_ihdr || !ReadBytes(io, handle, default_image, length)) {
				return FALSE;
			}
			consumed = length;
			seen_idat = TRUE;

		} else if(memcmp(type, "acTL", 4) == 0) {
			BYTE payload[8];
			// there is one acTL, and it comes before IDAT. A second one says the file
			// is not what it claims to be, so the animation goes; one that arrives
			// after IDAT is simply not an animation control chunk and is ignored.
			// Either way the default image is still there to be read.
			if((length == 8) && (io->read_proc(payload, 1, 8, handle) == 8)) {
				consumed = 8;
				if(have_actl) {
					broken = TRUE;
				} else if(!seen_idat) {
					// num_frames is advisory here: the fcTL chunks actually present are
					// what the frames are built from
					info->num_plays = GetDWORD(payload + 4);
					have_actl = TRUE;
				}
			}

		} else if(memcmp(type, "fcTL", 4) == 0) {
			BYTE payload[26];
			if(!have_ihdr) {
				return FALSE;
			}
			if((length != 26) || (io->read_proc(payload, 1, 26, handle) != 26)) {
				broken = TRUE;
			} else {
				consumed = 26;
				APNGFrame frame;
				if(GetDWORD(payload) != expected_seq) {
					// the sequence numbers are the format's own corruption check
					broken = TRUE;
				} else if(!ParseFrameControl(payload, info->canvas_width, info->canvas_height, &frame)) {
					broken = TRUE;
				} else if(!seen_idat) {
					// "the default image may be included as the first frame of the
					// animation by the presence of a single fcTL chunk before IDAT" -
					// and that frame is the default image, so it has to be all of it
					if(idat_is_frame || (frame.x_offset != 0) || (frame.y_offset != 0) ||
					   (frame.width != info->canvas_width) || (frame.height != info->canvas_height)) {
						broken = TRUE;
					} else {
						expected_seq++;
						default_frame = frame;
						idat_is_frame = TRUE;
					}
				} else {
					expected_seq++;
					info->frames.push_back(frame);
					current = (int)info->frames.size() - 1;
				}
			}

		} else if(memcmp(type, "fdAT", 4) == 0) {
			BYTE sequence[4];
			if((length < 4) || (current < 0) || (io->read_proc(sequence, 1, 4, handle) != 4)) {
				broken = TRUE;
			} else {
				consumed = 4;
				if(GetDWORD(sequence) != expected_seq) {
					broken = TRUE;
				} else {
					expected_seq++;
					if(!ReadBytes(io, handle, info->frames[current].data, length - 4)) {
						// the file ends inside the frame: the animation stops here, but
						// the default image was read long before this point
						broken = TRUE;
						break;
					}
					consumed = length;
				}
			}

		} else if(memcmp(type, "IEND", 4) == 0) {
			break;

		} else if((memcmp(type, "PLTE", 4) == 0) || ((type[0] & 0x20) != 0)) {
			// the palette and every ancillary chunk are copied verbatim, in the order
			// they appear, and replayed ahead of each frame's data: that is what gives
			// the frames their palette, transparency table, ICC profile and text
			const size_t start = info->ancillary.size();
			info->ancillary.insert(info->ancillary.end(), header, header + 8);
			if(!ReadBytes(io, handle, info->ancillary, length + 4)) {
				// a chunk the file promised and does not hold. Whatever came before it
				// is still good, and if that included the image, it is still readable
				info->ancillary.resize(start);
				break;
			}
			continue;	// the CRC came in with the rest of the chunk

		} else {
			// an unknown critical chunk: the format says a decoder that does not
			// understand one cannot claim to have read the file
			return FALSE;
		}

		// step over whatever the branch left of the payload, and the CRC
		if(io->seek_proc(handle, (long)(length - consumed) + 4, SEEK_CUR) != 0) {
			break;
		}
	}

	if(!have_ihdr || default_image.empty()) {
		return FALSE;
	}

	// a frame whose fdAT never arrived at the end of the file is simply not there;
	// one missing in the middle means the animation does not describe what it holds
	while(!info->frames.empty() && info->frames.back().data.empty()) {
		info->frames.pop_back();
	}
	for(size_t i = 0; i < info->frames.size(); i++) {
		if(info->frames[i].data.empty()) {
			broken = TRUE;
		}
	}

	info->animated = (have_actl && !broken && (idat_is_frame || !info->frames.empty())) ? TRUE : FALSE;

	if(info->animated) {
		if(idat_is_frame) {
			default_frame.data.swap(default_image);
			info->frames.insert(info->frames.begin(), default_frame);
		}
	} else {
		// a plain PNG, or one whose animation chunks were unusable: a single page,
		// holding the image every PNG decoder sees
		info->frames.clear();
		APNGFrame frame;
		frame.width = info->canvas_width;
		frame.height = info->canvas_height;
		frame.data.swap(default_image);
		info->frames.push_back(frame);
		info->num_plays = 0;
	}

	return TRUE;
}

// ==========================================================
// Reading: frames
// ==========================================================

/**
Turn one frame back into a standalone PNG and let FIF_PNG read it. Every colour
type, bit depth, transparency table, ICC profile and text chunk the PNG loader
handles therefore arrives here already handled.
@param info Plugin state
@param page Frame to decode
@param flags FreeImage load flags, passed through to the PNG loader
@return Returns a dib the caller owns, or NULL
*/
static FIBITMAP *
DecodeFrame(APNGinfo *info, int page, int flags) {
	const APNGFrame& frame = info->frames[page];
	if(frame.data.empty() || (frame.data.size() > APNG_MAX_CHUNK_LENGTH)) {
		// the IDAT built below carries all of it in one chunk, and a chunk has a
		// 31-bit length. Nothing that fits in memory gets near this
		return NULL;
	}

	static const BYTE signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

	std::vector<BYTE> png;
	png.reserve(8 + 25 + info->ancillary.size() + frame.data.size() + 12 + 12);
	png.insert(png.end(), signature, signature + 8);

	BYTE ihdr[13];
	memcpy(ihdr, info->ihdr, 13);
	PutDWORD(ihdr, frame.width);
	PutDWORD(ihdr + 4, frame.height);
	AppendChunk(png, "IHDR", ihdr, 13);

	png.insert(png.end(), info->ancillary.begin(), info->ancillary.end());

	AppendChunk(png, "IDAT", &frame.data[0], frame.data.size());
	AppendChunk(png, "IEND", NULL, 0);

	if(png.size() > 0xFFFFFFFFUL) {
		return NULL;
	}

	FIMEMORY *hmem = FreeImage_OpenMemory(&png[0], (DWORD)png.size());
	if(hmem == NULL) {
		return NULL;
	}
	FIBITMAP *dib = FreeImage_LoadFromMemory(FIF_PNG, hmem, flags);
	FreeImage_CloseMemory(hmem);

	return dib;
}

/**
Clear a rectangle of the canvas to fully transparent black.
*/
static void
ClearRegion(FIBITMAP *canvas, DWORD x, DWORD y, DWORD width, DWORD height) {
	const DWORD canvas_height = FreeImage_GetHeight(canvas);
	for(DWORD row = 0; row < height; row++) {
		BYTE *line = FreeImage_GetScanLine(canvas, canvas_height - 1 - (y + row));
		memset(line + (size_t)x * 4, 0, (size_t)width * 4);
	}
}

/**
Draw a 32-bit frame onto the 32-bit canvas at (x, y), the way the frame's blend_op
says to. Both bitmaps are bottom-up, the offsets are not: row 0 of the frame goes to
the row y of the canvas counted from its top.
*/
static void
CompositeFrame(FIBITMAP *canvas, FIBITMAP *frame, DWORD x, DWORD y, BYTE blend_op) {
	const DWORD canvas_height = FreeImage_GetHeight(canvas);
	const DWORD width = FreeImage_GetWidth(frame);
	const DWORD height = FreeImage_GetHeight(frame);

	for(DWORD row = 0; row < height; row++) {
		const BYTE *src = FreeImage_GetScanLine(frame, height - 1 - row);
		BYTE *dst = FreeImage_GetScanLine(canvas, canvas_height - 1 - (y + row)) + (size_t)x * 4;

		if(blend_op == APNG_BLEND_OP_SOURCE) {
			memcpy(dst, src, (size_t)width * 4);
			continue;
		}

		// "the frame should be composited onto the output buffer based on its alpha,
		// using a simple OVER operation". Everything below is that formula with both
		// sides multiplied by 255, so that it stays in integers
		for(DWORD col = 0; col < width; col++) {
			const unsigned sa = src[FI_RGBA_ALPHA];
			if(sa == 255) {
				memcpy(dst, src, 4);
			} else if(sa != 0) {
				const unsigned da = dst[FI_RGBA_ALPHA];
				const unsigned weight = da * (255 - sa);			// the background's share, x255
				const unsigned alpha = sa * 255 + weight;			// the result's alpha, x255
				if(alpha == 0) {
					dst[FI_RGBA_RED] = dst[FI_RGBA_GREEN] = dst[FI_RGBA_BLUE] = dst[FI_RGBA_ALPHA] = 0;
				} else {
					// rounded, not truncated: a frame blended over the one before it is
					// blended over the result again by the frame after, and half a level
					// dropped each time is a drift a long animation would show
					const unsigned half = alpha / 2;
					dst[FI_RGBA_RED]   = (BYTE)((src[FI_RGBA_RED]   * sa * 255 + dst[FI_RGBA_RED]   * weight + half) / alpha);
					dst[FI_RGBA_GREEN] = (BYTE)((src[FI_RGBA_GREEN] * sa * 255 + dst[FI_RGBA_GREEN] * weight + half) / alpha);
					dst[FI_RGBA_BLUE]  = (BYTE)((src[FI_RGBA_BLUE]  * sa * 255 + dst[FI_RGBA_BLUE]  * weight + half) / alpha);
					dst[FI_RGBA_ALPHA] = (BYTE)((alpha + 127) / 255);
				}
			}
			src += 4;
			dst += 4;
		}
	}
}

/**
Apply a frame's disposal to the canvas, which is what the frame after it starts from.
*/
static void
DisposeFrame(APNGinfo *info, int page) {
	const APNGFrame& frame = info->frames[page];

	switch(frame.dispose_op) {
		case APNG_DISPOSE_OP_BACKGROUND:
			ClearRegion(info->canvas, frame.x_offset, frame.y_offset, frame.width, frame.height);
			break;

		case APNG_DISPOSE_OP_PREVIOUS:
			if(info->previous_canvas != NULL) {
				FreeImage_Unload(info->canvas);
				info->canvas = info->previous_canvas;
				info->previous_canvas = NULL;
			}
			break;

		default:
			break;
	}
}

/**
Build the canvas as a viewer shows it at a given frame, rather than the rectangle
the file stores. Playing an animation in order costs one frame of work per frame,
because the canvas the last call produced is kept; asking for a frame behind it
means starting over from a transparent canvas, exactly as PluginGIF.cpp behaves.
@return Returns a 32-bit dib the caller owns, or NULL
*/
static FIBITMAP *
RenderFrame(APNGinfo *info, int page, int flags) {
	if((info->canvas != NULL) && (info->canvas_page == page)) {
		return FreeImage_Clone(info->canvas);
	}

	int start;
	if((info->canvas != NULL) && (info->canvas_page >= 0) && (info->canvas_page < page)) {
		start = info->canvas_page + 1;
	} else {
		// "at the beginning of each play the output buffer shall be completely
		// initialized to a fully transparent black rectangle"
		if(info->canvas != NULL) {
			FreeImage_Unload(info->canvas);
		}
		if(info->previous_canvas != NULL) {
			FreeImage_Unload(info->previous_canvas);
			info->previous_canvas = NULL;
		}
		info->canvas_page = -1;
		info->canvas = FreeImage_Allocate(info->canvas_width, info->canvas_height, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
		if(info->canvas == NULL) {
			return NULL;
		}
		start = 0;
	}

	for(int i = start; i <= page; i++) {
		// the canvas holds frame i-1 fully rendered; stepping past it means disposing of it
		if(i > 0) {
			DisposeFrame(info, i - 1);
		}

		FIBITMAP *raw = DecodeFrame(info, i, flags & ~(APNG_PLAYBACK | FIF_LOAD_NOPIXELS));
		if(raw == NULL) {
			info->canvas_page = -1;		// do not hand out a half drawn canvas later
			return NULL;
		}
		FIBITMAP *frame32 = FreeImage_ConvertTo32Bits(raw);
		FreeImage_Unload(raw);
		if(frame32 == NULL) {
			info->canvas_page = -1;
			return NULL;
		}

		const APNGFrame& frame = info->frames[i];
		if((FreeImage_GetWidth(frame32) != frame.width) || (FreeImage_GetHeight(frame32) != frame.height)) {
			// the fcTL and the frame's own IHDR disagree: compositing it would write
			// outside the canvas
			FreeImage_Unload(frame32);
			info->canvas_page = -1;
			return NULL;
		}

		if(frame.dispose_op == APNG_DISPOSE_OP_PREVIOUS) {
			if(info->previous_canvas != NULL) {
				FreeImage_Unload(info->previous_canvas);
			}
			info->previous_canvas = FreeImage_Clone(info->canvas);
		}

		CompositeFrame(info->canvas, frame32, frame.x_offset, frame.y_offset, (i == 0) ? APNG_BLEND_OP_SOURCE : frame.blend_op);
		FreeImage_Unload(frame32);

		info->canvas_page = i;
	}

	return FreeImage_Clone(info->canvas);
}

/**
Attach one FIMD_ANIMATION tag, description included, the way PluginGIF.cpp does.
*/
static BOOL
SetAnimTag(FIBITMAP *dib, const char *key, WORD id, FREE_IMAGE_MDTYPE type, DWORD count, DWORD length, const void *value) {
	BOOL bResult = FALSE;
	FITAG *tag = FreeImage_CreateTag();
	if(tag) {
		FreeImage_SetTagKey(tag, key);
		FreeImage_SetTagID(tag, id);
		FreeImage_SetTagType(tag, type);
		FreeImage_SetTagCount(tag, count);
		FreeImage_SetTagLength(tag, length);
		FreeImage_SetTagValue(tag, value);
		TagLib& s = TagLib::instance();
		FreeImage_SetTagDescription(tag, s.getTagDescription(TagLib::ANIMATION, id));
		bResult = FreeImage_SetMetadata(FIMD_ANIMATION, dib, key, tag);
		FreeImage_DeleteTag(tag);
	}
	return bResult;
}

/**
Read one tag, and only accept it if it is of the type it is supposed to be - the
same guard PluginGIF.cpp puts in front of the animation tags it reads back.
*/
static BOOL
GetAnimTag(FIBITMAP *dib, const char *key, FREE_IMAGE_MDTYPE type, FITAG **tag) {
	if(FreeImage_GetMetadata(FIMD_ANIMATION, dib, key, tag)) {
		if((FreeImage_GetTagType(*tag) == type) && (FreeImage_GetTagValue(*tag) != NULL)) {
			return TRUE;
		}
	}
	return FALSE;
}

/**
Describe a frame with the tags a caller that already walks GIF or WebP animations
reads: FrameTime is milliseconds, and DisposalMethod uses GIF's numbering.
*/
static void
SetFrameMetadata(FIBITMAP *dib, const APNGinfo *info, int page) {
	const APNGFrame& frame = info->frames[page];

	// delay_num/delay_den are a fraction of a second; round to the nearest millisecond
	LONG duration = (LONG)(((DWORD)frame.delay_num * 1000 + frame.delay_den / 2) / frame.delay_den);
	WORD left = (WORD)MIN(frame.x_offset, (DWORD)0xFFFF);
	WORD top = (WORD)MIN(frame.y_offset, (DWORD)0xFFFF);
	BYTE disposal = (frame.dispose_op == APNG_DISPOSE_OP_BACKGROUND) ? GIF_DISPOSAL_BACKGROUND :
					(frame.dispose_op == APNG_DISPOSE_OP_PREVIOUS) ? GIF_DISPOSAL_PREVIOUS : GIF_DISPOSAL_LEAVE;
	BYTE blend = (frame.blend_op == APNG_BLEND_OP_SOURCE) ? 1 : 0;

	SetAnimTag(dib, "FrameTime", ANIMTAG_FRAMETIME, FIDT_LONG, 1, 4, &duration);
	SetAnimTag(dib, "FrameLeft", ANIMTAG_FRAMELEFT, FIDT_SHORT, 1, 2, &left);
	SetAnimTag(dib, "FrameTop", ANIMTAG_FRAMETOP, FIDT_SHORT, 1, 2, &top);
	SetAnimTag(dib, "DisposalMethod", ANIMTAG_DISPOSALMETHOD, FIDT_BYTE, 1, 1, &disposal);
	SetAnimTag(dib, "BlendMethod", ANIMTAG_BLENDMETHOD, FIDT_BYTE, 1, 1, &blend);

	if(page == 0) {
		WORD logicalwidth = (WORD)MIN(info->canvas_width, (DWORD)0xFFFF);
		WORD logicalheight = (WORD)MIN(info->canvas_height, (DWORD)0xFFFF);
		LONG loop = (LONG)info->num_plays;
		SetAnimTag(dib, "LogicalWidth", ANIMTAG_LOGICALWIDTH, FIDT_SHORT, 1, 2, &logicalwidth);
		SetAnimTag(dib, "LogicalHeight", ANIMTAG_LOGICALHEIGHT, FIDT_SHORT, 1, 2, &logicalheight);
		SetAnimTag(dib, "Loop", ANIMTAG_LOOP, FIDT_LONG, 1, 4, &loop);
	}
}

// ==========================================================
// Writing: compressing a frame
// ==========================================================

static void
error_handler(png_structp png_ptr, const char *error) {
	FreeImage_OutputMessageProc(s_format_id, error);
	png_longjmp(png_ptr, 1);
}

static void
warning_handler(png_structp png_ptr, const char *warning) {
	(png_structp)png_ptr;
	(char*)warning;
}

static void
_WriteVectorProc(png_structp png_ptr, png_bytep data, png_size_t size) {
	std::vector<BYTE> *out = (std::vector<BYTE>*)png_get_io_ptr(png_ptr);
	try {
		out->insert(out->end(), data, data + size);
	} catch(...) {
		png_error(png_ptr, "Out of memory");
	}
}

static void
_FlushVectorProc(png_structp png_ptr) {
	(png_structp)png_ptr;
}

/**
Compress a 32-bit dib as a standalone RGBA PNG in memory. The metadata is only
asked for on the first frame, whose chunks become the animation's own header.
@return Returns TRUE if successful, returns FALSE otherwise
*/
static BOOL
CompressFrame(FIBITMAP *dib, int flags, BOOL with_metadata, std::vector<BYTE>& png) {
	png_structp png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, error_handler, warning_handler);
	if(png_ptr == NULL) {
		return FALSE;
	}
	png_infop info_ptr = png_create_info_struct(png_ptr);
	if(info_ptr == NULL) {
		png_destroy_write_struct(&png_ptr, (png_infopp)NULL);
		return FALSE;
	}

	// everything that lives across the setjmp below is either a plain value or owned
	// by the caller: png_longjmp would step over the destructor of anything else
	if(setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		return FALSE;
	}

	png_set_write_fn(png_ptr, &png, _WriteVectorProc, _FlushVectorProc);

	const png_uint_32 width = (png_uint_32)FreeImage_GetWidth(dib);
	const png_uint_32 height = (png_uint_32)FreeImage_GetHeight(dib);

	png_set_IHDR(png_ptr, info_ptr, width, height, 8, PNG_COLOR_TYPE_RGBA,
		PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);

	// the same compression controls PluginPNG.cpp offers
	const int zlib_level = flags & 0x0F;
	if((zlib_level >= 1) && (zlib_level <= 9)) {
		png_set_compression_level(png_ptr, zlib_level);
	} else if((flags & PNG_Z_NO_COMPRESSION) == PNG_Z_NO_COMPRESSION) {
		png_set_compression_level(png_ptr, Z_NO_COMPRESSION);
	}
	png_set_compression_strategy(png_ptr, Z_FILTERED);
	png_set_filter(png_ptr, 0, PNG_FILTER_NONE | PNG_FILTER_SUB | PNG_FILTER_UP | PNG_FILTER_PAETH);

	if(with_metadata) {
		const png_uint_32 res_x = (png_uint_32)FreeImage_GetDotsPerMeterX(dib);
		const png_uint_32 res_y = (png_uint_32)FreeImage_GetDotsPerMeterY(dib);
		if((res_x > 0) && (res_y > 0)) {
			png_set_pHYs(png_ptr, info_ptr, res_x, res_y, PNG_RESOLUTION_METER);
		}
		FIICCPROFILE *iccProfile = FreeImage_GetICCProfile(dib);
		if(iccProfile->size && iccProfile->data) {
			png_set_option(png_ptr, PNG_SKIP_sRGB_CHECK_PROFILE, 1);
			png_set_iCCP(png_ptr, info_ptr, "Embedded Profile", 0, (png_const_bytep)iccProfile->data, iccProfile->size);
		}
	}

	png_write_info(png_ptr, info_ptr);

#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_BGR
	png_set_bgr(png_ptr);
#endif

	for(png_uint_32 k = 0; k < height; k++) {
		png_write_row(png_ptr, FreeImage_GetScanLine(dib, height - k - 1));
	}

	png_write_end(png_ptr, info_ptr);
	png_destroy_write_struct(&png_ptr, &info_ptr);

	return TRUE;
}

/**
Split what CompressFrame() produced: the image data, which becomes the frame's own,
and everything around it, which is the animation's header. IHDR and IEND are the
container's and are written by the assembler.
@return Returns TRUE if successful, returns FALSE otherwise
*/
static BOOL
SplitPNG(const std::vector<BYTE>& png, std::vector<BYTE> *ancillary, std::vector<BYTE>& data) {
	size_t pos = 8;		// past the signature

	while(pos + 8 <= png.size()) {
		const DWORD length = GetDWORD(&png[pos]);
		if((length > APNG_MAX_CHUNK_LENGTH) || (pos + 12 + length > png.size())) {
			return FALSE;
		}
		const BYTE *type = &png[pos + 4];

		if(memcmp(type, "IDAT", 4) == 0) {
			data.insert(data.end(), png.begin() + (pos + 8), png.begin() + (pos + 8 + length));
		} else if(memcmp(type, "IEND", 4) == 0) {
			return !data.empty();
		} else if(memcmp(type, "IHDR", 4) != 0) {
			if(ancillary != NULL) {
				ancillary->insert(ancillary->end(), png.begin() + pos, png.begin() + (pos + 12 + length));
			}
		}

		pos += 12 + length;
	}

	return FALSE;
}

/**
The rectangle in which two same-sized 32-bit frames differ.
@return Returns FALSE when they are identical
*/
static BOOL
DifferenceRegion(FIBITMAP *current, FIBITMAP *previous, DWORD *out_x, DWORD *out_y, DWORD *out_width, DWORD *out_height) {
	const DWORD width = FreeImage_GetWidth(current);
	const DWORD height = FreeImage_GetHeight(current);

	DWORD left = width, right = 0, top = height, bottom = 0;

	for(DWORD row = 0; row < height; row++) {
		// walked in top-down order so that top and bottom read the way they are named
		const DWORD *src = (const DWORD*)FreeImage_GetScanLine(current, height - 1 - row);
		const DWORD *ref = (const DWORD*)FreeImage_GetScanLine(previous, height - 1 - row);
		if(memcmp(src, ref, (size_t)width * 4) == 0) {
			continue;
		}
		if(row < top) {
			top = row;
		}
		bottom = row;
		for(DWORD col = 0; col < left; col++) {
			if(src[col] != ref[col]) {
				left = col;
				break;
			}
		}
		for(DWORD col = width; col > right; col--) {
			if(src[col - 1] != ref[col - 1]) {
				right = col;
				break;
			}
		}
	}

	if(right <= left) {
		return FALSE;
	}

	*out_x = left;
	*out_y = top;
	*out_width = right - left;
	*out_height = bottom - top + 1;

	return TRUE;
}

/**
Read what a caller can say about a frame through FIMD_ANIMATION: where it goes on
the canvas, how long it lasts, and what happens to it afterwards. The defaults are
the ones that make an untagged bitmap a plain frame of a simple animation.
@return Returns TRUE if the image describes itself as a frame of one at all
*/
static BOOL
ReadFrameTags(FIBITMAP *dib, APNGFrame *frame) {
	FITAG *tag = NULL;
	BOOL tagged = FALSE;

	frame->width = FreeImage_GetWidth(dib);
	frame->height = FreeImage_GetHeight(dib);
	frame->delay_num = 100;
	frame->delay_den = 1000;

	if(GetAnimTag(dib, "FrameLeft", FIDT_SHORT, &tag)) {
		frame->x_offset = *(WORD*)FreeImage_GetTagValue(tag);
		tagged = TRUE;
	}
	if(GetAnimTag(dib, "FrameTop", FIDT_SHORT, &tag)) {
		frame->y_offset = *(WORD*)FreeImage_GetTagValue(tag);
		tagged = TRUE;
	}
	if(GetAnimTag(dib, "FrameTime", FIDT_LONG, &tag)) {
		// the tag is milliseconds, and a denominator of 1000 stores them exactly
		const LONG delay = *(LONG*)FreeImage_GetTagValue(tag);
		frame->delay_num = (WORD)((delay < 0) ? 0 : MIN(delay, (LONG)0xFFFF));
		tagged = TRUE;
	}
	if(GetAnimTag(dib, "DisposalMethod", FIDT_BYTE, &tag)) {
		const BYTE disposal = *(BYTE*)FreeImage_GetTagValue(tag);
		frame->dispose_op = (disposal == GIF_DISPOSAL_BACKGROUND) ? APNG_DISPOSE_OP_BACKGROUND :
							(disposal == GIF_DISPOSAL_PREVIOUS) ? APNG_DISPOSE_OP_PREVIOUS : APNG_DISPOSE_OP_NONE;
		tagged = TRUE;
	}
	if(GetAnimTag(dib, "BlendMethod", FIDT_BYTE, &tag)) {
		// the tag is GIF's way round: 1 means do not blend
		frame->blend_op = (*(BYTE*)FreeImage_GetTagValue(tag) != 0) ? APNG_BLEND_OP_SOURCE : APNG_BLEND_OP_OVER;
		tagged = TRUE;
	}
	if(FreeImage_GetMetadata(FIMD_ANIMATION, dib, "LogicalWidth", &tag) ||
	   FreeImage_GetMetadata(FIMD_ANIMATION, dib, "LogicalHeight", &tag) ||
	   FreeImage_GetMetadata(FIMD_ANIMATION, dib, "Loop", &tag)) {
		tagged = TRUE;
	}

	return tagged;
}

/**
The canvas and the loop count, which only the first frame carries.
*/
static void
ReadAnimationTags(FIBITMAP *dib, const APNGFrame *frame, DWORD *width, DWORD *height, DWORD *plays) {
	FITAG *tag = NULL;

	*width = frame->width;
	*height = frame->height;
	if(GetAnimTag(dib, "LogicalWidth", FIDT_SHORT, &tag)) {
		*width = *(WORD*)FreeImage_GetTagValue(tag);
	}
	if(GetAnimTag(dib, "LogicalHeight", FIDT_SHORT, &tag)) {
		*height = *(WORD*)FreeImage_GetTagValue(tag);
	}
	if(GetAnimTag(dib, "Loop", FIDT_LONG, &tag)) {
		const LONG loop = *(LONG*)FreeImage_GetTagValue(tag);
		*plays = (loop > 0) ? (DWORD)loop : 0;
	}
}

/**
Take one page of the animation: normalize it, place it, shrink it to what actually
changed, compress it, and keep it until Close() writes the file.
@return Returns TRUE if successful, returns FALSE otherwise
*/
static BOOL
AddFrame(APNGinfo *info, FIBITMAP *dib, int flags, BOOL is_first) {
	// every frame of an APNG shares one IHDR, so they all have to be the same kind of
	// image; 8-bit RGBA is the one that can hold any of them
	FIBITMAP *frame32 = FreeImage_ConvertTo32Bits(dib);
	if(frame32 == NULL) {
		// 16-bit greyscale and the like have to come down to a standard type first
		FIBITMAP *standard = FreeImage_ConvertToStandardType(dib, TRUE);
		if(standard != NULL) {
			frame32 = FreeImage_ConvertTo32Bits(standard);
			FreeImage_Unload(standard);
		}
	}
	if(frame32 == NULL) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_UNSUPPORTED_FORMAT);
		return FALSE;
	}

	APNGFrame frame;
	ReadFrameTags(dib, &frame);
	frame.width = FreeImage_GetWidth(frame32);
	frame.height = FreeImage_GetHeight(frame32);

	if(is_first) {
		ReadAnimationTags(dib, &frame, &info->out_width, &info->out_height, &info->out_plays);
	}
	// the canvas is written in Close(), so a frame that does not fit inside it can
	// still enlarge it rather than be rejected
	info->out_width = MAX(info->out_width, frame.x_offset + frame.width);
	info->out_height = MAX(info->out_height, frame.y_offset + frame.height);

	// A frame that covers the whole canvas and simply overwrites it is the common
	// case, and it does not have to be stored whole: SOURCE overwrites the rectangle
	// it covers and nothing else, so storing only the pixels that differ from the
	// frame before leaves the canvas identical. This is exact, not an approximation.
	FIBITMAP *encoded = frame32;
	const BOOL replaces_canvas =
		(frame.x_offset == 0) && (frame.y_offset == 0) &&
		(frame.blend_op == APNG_BLEND_OP_SOURCE) && (frame.dispose_op == APNG_DISPOSE_OP_NONE) &&
		(frame.width == info->out_width) && (frame.height == info->out_height);

	if(replaces_canvas && !is_first && (info->out_previous != NULL) &&
	   (FreeImage_GetWidth(info->out_previous) == frame.width) && (FreeImage_GetHeight(info->out_previous) == frame.height)) {
		DWORD x = 0, y = 0, width = 0, height = 0;
		if(DifferenceRegion(frame32, info->out_previous, &x, &y, &width, &height)) {
			if((width < frame.width) || (height < frame.height)) {
				FIBITMAP *cropped = FreeImage_Copy(frame32, (int)x, (int)y, (int)(x + width), (int)(y + height));
				if(cropped != NULL) {
					encoded = cropped;
					frame.x_offset = x;
					frame.y_offset = y;
					frame.width = width;
					frame.height = height;
				}
			}
		} else {
			// nothing moved: the format has no way to say "no change", so say it with
			// a single pixel drawn over itself
			FIBITMAP *cropped = FreeImage_Copy(frame32, 0, 0, 1, 1);
			if(cropped != NULL) {
				encoded = cropped;
				frame.width = 1;
				frame.height = 1;
			}
		}
	}

	BOOL bResult = FALSE;
	{
		std::vector<BYTE> png;
		if(CompressFrame(encoded, flags, is_first, png)) {
			bResult = SplitPNG(png, is_first ? &info->out_ancillary : NULL, frame.data);
		}
	}

	if(encoded != frame32) {
		FreeImage_Unload(encoded);
	}

	if(!bResult) {
		FreeImage_Unload(frame32);
		return FALSE;
	}

	// keep this frame whole, so that the next one can be diffed against it - but only
	// while every frame so far has left the canvas showing exactly its own pixels
	if(info->out_previous != NULL) {
		FreeImage_Unload(info->out_previous);
		info->out_previous = NULL;
	}
	if(replaces_canvas) {
		info->out_previous = frame32;
	} else {
		FreeImage_Unload(frame32);
	}

	try {
		info->out_frames.push_back(APNGFrame());
	} catch(std::bad_alloc&) {
		return FALSE;
	}
	APNGFrame& stored = info->out_frames.back();
	stored.width = frame.width;
	stored.height = frame.height;
	stored.x_offset = frame.x_offset;
	stored.y_offset = frame.y_offset;
	stored.delay_num = frame.delay_num;
	stored.delay_den = frame.delay_den;
	stored.dispose_op = frame.dispose_op;
	stored.blend_op = frame.blend_op;
	stored.data.swap(frame.data);

	return TRUE;
}

/**
Compress an empty canvas, for the default image of an animation whose first frame
does not cover one. Such a file needs an IDAT all the same - every PNG does - and a
fully transparent one is what the animation starts from anyway.
@return Returns TRUE if successful, returns FALSE otherwise
*/
static BOOL
CompressBackdrop(DWORD width, DWORD height, int flags, std::vector<BYTE>& data) {
	FIBITMAP *dib = FreeImage_Allocate(width, height, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	if(dib == NULL) {
		return FALSE;
	}
	std::vector<BYTE> png;
	const BOOL bResult = CompressFrame(dib, flags, FALSE, png) ? SplitPNG(png, NULL, data) : FALSE;
	FreeImage_Unload(dib);
	return bResult;
}

/**
Write the animation: the header, the control chunks and the frames, in the one order
the format allows.

The first frame's data goes in IDAT where it can, so that a decoder that knows
nothing of APNG still sees the first frame rather than nothing. It only can when
that frame covers the whole canvas, because the fcTL that introduces the default
image has to describe the whole of it; a first frame placed somewhere smaller gets
a transparent canvas as the default image and goes into an fdAT like the rest.
@return Returns TRUE if successful, returns FALSE otherwise
*/
static BOOL
WriteAnimation(FreeImageIO *io, fi_handle handle, APNGinfo *info, int flags) {
	static const BYTE signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };

	if(io->write_proc((void*)signature, 1, 8, handle) != 8) {
		return FALSE;
	}

	BYTE ihdr[13];
	PutDWORD(ihdr, info->out_width);
	PutDWORD(ihdr + 4, info->out_height);
	ihdr[8] = 8;						// bit depth
	ihdr[9] = PNG_COLOR_TYPE_RGBA;
	ihdr[10] = PNG_COMPRESSION_TYPE_BASE;
	ihdr[11] = PNG_FILTER_TYPE_BASE;
	ihdr[12] = PNG_INTERLACE_NONE;
	if(!WriteChunk(io, handle, "IHDR", NULL, 0, ihdr, 13)) {
		return FALSE;
	}

	if(!info->out_ancillary.empty()) {
		const size_t size = info->out_ancillary.size();
		if(io->write_proc(&info->out_ancillary[0], 1, (unsigned)size, handle) != size) {
			return FALSE;
		}
	}

	BYTE actl[8];
	PutDWORD(actl, (DWORD)info->out_frames.size());
	PutDWORD(actl + 4, info->out_plays);
	if(!WriteChunk(io, handle, "acTL", NULL, 0, actl, 8)) {
		return FALSE;
	}

	const APNGFrame& first = info->out_frames[0];
	const BOOL first_is_default = (first.x_offset == 0) && (first.y_offset == 0) &&
		(first.width == info->out_width) && (first.height == info->out_height);

	if(!first_is_default) {
		std::vector<BYTE> backdrop;
		if(!CompressBackdrop(info->out_width, info->out_height, flags, backdrop)) {
			return FALSE;
		}
		if(!WriteChunk(io, handle, "IDAT", NULL, 0, &backdrop[0], backdrop.size())) {
			return FALSE;
		}
	}

	DWORD sequence = 0;
	for(size_t i = 0; i < info->out_frames.size(); i++) {
		const APNGFrame& frame = info->out_frames[i];

		BYTE fctl[26];
		PutDWORD(fctl, sequence++);
		PutDWORD(fctl + 4, frame.width);
		PutDWORD(fctl + 8, frame.height);
		PutDWORD(fctl + 12, frame.x_offset);
		PutDWORD(fctl + 16, frame.y_offset);
		PutWORD(fctl + 20, frame.delay_num);
		PutWORD(fctl + 22, frame.delay_den);
		fctl[24] = frame.dispose_op;
		fctl[25] = frame.blend_op;
		if(!WriteChunk(io, handle, "fcTL", NULL, 0, fctl, 26)) {
			return FALSE;
		}

		if((i == 0) && first_is_default) {
			if(!WriteChunk(io, handle, "IDAT", NULL, 0, &frame.data[0], frame.data.size())) {
				return FALSE;
			}
		} else {
			BYTE prefix[4];
			PutDWORD(prefix, sequence++);
			if(!WriteChunk(io, handle, "fdAT", prefix, 4, &frame.data[0], frame.data.size())) {
				return FALSE;
			}
		}
	}

	return WriteChunk(io, handle, "IEND", NULL, 0, NULL, 0);
}

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "APNG";
}

static const char * DLL_CALLCONV
Description() {
	return "Animated Portable Network Graphics";
}

static const char * DLL_CALLCONV
Extension() {
	return "apng,png";
}

static const char * DLL_CALLCONV
RegExpr() {
	return NULL;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/apng";
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	return APNG_IsAnimatedStream(io, handle);
}

static BOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return (
			(depth == 1) ||
			(depth == 4) ||
			(depth == 8) ||
			(depth == 24) ||
			(depth == 32)
		);
}

static BOOL DLL_CALLCONV
SupportsExportType(FREE_IMAGE_TYPE type) {
	return (
		(type == FIT_BITMAP) ||
		(type == FIT_UINT16) ||
		(type == FIT_RGB16) ||
		(type == FIT_RGBA16)
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

// ----------------------------------------------------------

static void * DLL_CALLCONV
Open(FreeImageIO *io, fi_handle handle, BOOL read) {
	APNGinfo *info = new(std::nothrow) APNGinfo;
	if(info == NULL) {
		return NULL;
	}
	info->read = read;

	if(read) {
		BOOL bResult = FALSE;
		try {
			bResult = ParseStream(io, handle, info);
		} catch(std::bad_alloc&) {
			bResult = FALSE;
		}
		if(!bResult) {
			FreeImage_OutputMessageProc(s_format_id, "Invalid or corrupted PNG file");
			delete info;
			return NULL;
		}
	}

	return info;
}

static void DLL_CALLCONV
Close(FreeImageIO *io, fi_handle handle, void *data) {
	APNGinfo *info = (APNGinfo*)data;
	if(info == NULL) {
		return;
	}

	// the file is written here: acTL has to state how many frames follow it, and that
	// is only known once the last page has been saved
	if(!info->read) {
		try {
			if(info->pending != NULL) {
				APNGFrame described;
				if(!ReadFrameTags(info->pending, &described)) {
					// an ordinary image, saved once. Writing it as a plain PNG keeps its
					// palette, bit depth and metadata, which an RGBA animation of one
					// frame would have flattened for nothing
					if(!FreeImage_SaveToHandle(FIF_PNG, info->pending, io, handle, info->pending_flags)) {
						FreeImage_OutputMessageProc(s_format_id, "Failed to write the output file");
					}
				} else if(!AddFrame(info, info->pending, info->pending_flags, TRUE)) {
					// but an image that says how long it lasts and where it goes is a
					// frame, and saying so is the only way those survive the round trip
					// FreeImage_AppendPage() makes of every page through its cache
					FreeImage_OutputMessageProc(s_format_id, "Failed to write the output file");
				}
				FreeImage_Unload(info->pending);
				info->pending = NULL;
			}
			if(!info->out_frames.empty()) {
				if(!WriteAnimation(io, handle, info, info->pending_flags)) {
					FreeImage_OutputMessageProc(s_format_id, "Failed to write the output file");
				}
			}
		} catch(std::bad_alloc&) {
			FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_MEMORY);
		}
	}

	delete info;
}

// ----------------------------------------------------------

static int DLL_CALLCONV
PageCount(FreeImageIO *io, fi_handle handle, void *data) {
	APNGinfo *info = (APNGinfo*)data;
	return (info != NULL) ? (int)info->frames.size() : 0;
}

// ----------------------------------------------------------

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	APNGinfo *info = (APNGinfo*)data;
	if((info == NULL) || !info->read) {
		return NULL;
	}

	// FreeImage_Load asks for page -1, meaning the one image it expects
	if(page == -1) {
		page = 0;
	}
	if((page < 0) || (page >= (int)info->frames.size())) {
		return NULL;
	}

	try {
		const BOOL header_only = ((flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS) ? TRUE : FALSE;
		// APNG_PLAYBACK asks for the canvas a viewer shows at this frame rather than
		// the rectangle the file stores. A plain PNG has nothing to composite, so the
		// flag does nothing to one and such a file loads exactly as it would as a PNG.
		const BOOL playback = (info->animated && ((flags & APNG_PLAYBACK) == APNG_PLAYBACK)) ? TRUE : FALSE;

		FIBITMAP *dib = NULL;
		if(playback) {
			dib = header_only ?
				FreeImage_AllocateHeader(TRUE, info->canvas_width, info->canvas_height, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK) :
				RenderFrame(info, page, flags);
		} else {
			dib = DecodeFrame(info, page, flags & ~APNG_PLAYBACK);
		}
		if(dib == NULL) {
			return NULL;
		}

		if(info->animated) {
			SetFrameMetadata(dib, info, page);
		}

		return dib;

	} catch(std::bad_alloc&) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_MEMORY);
		return NULL;
	}
}

// ----------------------------------------------------------

static BOOL DLL_CALLCONV
Save(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	APNGinfo *info = (APNGinfo*)data;
	if((info == NULL) || (dib == NULL) || info->read) {
		return FALSE;
	}

	// What cannot be written is refused here, where there is still something to
	// return FALSE to. The file itself is only assembled in Close(), which returns
	// void - so a page accepted now and found impossible then would leave
	// FreeImage_Save() answering TRUE over a file it would otherwise have removed.
	// The test is the plugin's own: a lone page is written by the PNG writer, whose
	// depths these are, and a page of an animation goes through ConvertTo32Bits(),
	// which takes all of them.
	{
		const FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib);
		if(!FreeImage_HasPixels(dib) || !SupportsExportType(image_type) ||
		   ((image_type == FIT_BITMAP) && !SupportsExportDepth(FreeImage_GetBPP(dib)))) {
			FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_UNSUPPORTED_FORMAT);
			return FALSE;
		}
	}

	try {
		// The first page is held rather than compressed: a lone one is written as the
		// plain PNG it is, and it is the arrival of a second that turns the output
		// into an animation. Which page number this is does not matter - they arrive
		// in order - so the pages are counted here rather than trusted.
		if(info->out_pages == 0) {
			info->pending = FreeImage_Clone(dib);
			if(info->pending == NULL) {
				FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_DIB_MEMORY);
				return FALSE;
			}
			info->pending_flags = flags;
			info->out_pages = 1;
			return TRUE;
		}

		if(info->pending != NULL) {
			FIBITMAP *first = info->pending;
			info->pending = NULL;
			const BOOL bResult = AddFrame(info, first, info->pending_flags, TRUE);
			FreeImage_Unload(first);
			if(!bResult) {
				return FALSE;
			}
		}

		info->out_pages++;
		return AddFrame(info, dib, flags, FALSE);

	} catch(std::bad_alloc&) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_MEMORY);
		return FALSE;
	}
}

// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitAPNG(Plugin *plugin, int format_id) {
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
