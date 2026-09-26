// ==========================================================
// MNG loader and writer
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

// Pages are the embedded PNG/JNG streams, decoded by FIF_PNG and FIF_JNG.
// Delta images and object-buffer chunks are skipped, not rendered.

#include "FreeImage.h"
#include "Utilities.h"
#include "Plugin.h"

#include "../Metadata/FreeImageTag.h"

#include <algorithm>
#include <new>
#include <vector>

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ----------------------------------------------------------

#define MNG_SIGNATURE_SIZE 8	// size of the signature

static const BYTE g_mng_signature[8] = { 138, 77, 78, 71, 13, 10, 26, 10 };
static const BYTE g_png_signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
static const BYTE g_jng_signature[8] = { 139, 74, 78, 71, 13, 10, 26, 10 };

#define GIF_DISPOSAL_UNSPECIFIED	0
#define GIF_DISPOSAL_LEAVE			1
#define GIF_DISPOSAL_BACKGROUND		2
#define GIF_DISPOSAL_PREVIOUS		3

/** Bits of the MHDR simplicity profile (MNG 1.0, 4.1.1). */
#define MNG_PROFILE_VALID			(1 << 0)
#define MNG_PROFILE_SIMPLE			(1 << 1)
#define MNG_PROFILE_COMPLEX			(1 << 2)
#define MNG_PROFILE_TRANSPARENCY	(1 << 3)
#define MNG_PROFILE_JNG				(1 << 4)
#define MNG_PROFILE_DELTA_PNG		(1 << 5)

#define MNG_CHUNK(a, b, c, d) \
	((DWORD)(((DWORD)(a) << 24) | ((DWORD)(b) << 16) | ((DWORD)(c) << 8) | (DWORD)(d)))

static const DWORD CHUNK_MHDR = MNG_CHUNK('M', 'H', 'D', 'R');
static const DWORD CHUNK_MEND = MNG_CHUNK('M', 'E', 'N', 'D');
static const DWORD CHUNK_IHDR = MNG_CHUNK('I', 'H', 'D', 'R');
static const DWORD CHUNK_IEND = MNG_CHUNK('I', 'E', 'N', 'D');
static const DWORD CHUNK_IDAT = MNG_CHUNK('I', 'D', 'A', 'T');
static const DWORD CHUNK_JDAT = MNG_CHUNK('J', 'D', 'A', 'T');
static const DWORD CHUNK_JHDR = MNG_CHUNK('J', 'H', 'D', 'R');
static const DWORD CHUNK_BASI = MNG_CHUNK('B', 'A', 'S', 'I');
static const DWORD CHUNK_DHDR = MNG_CHUNK('D', 'H', 'D', 'R');
static const DWORD CHUNK_FRAM = MNG_CHUNK('F', 'R', 'A', 'M');
static const DWORD CHUNK_DEFI = MNG_CHUNK('D', 'E', 'F', 'I');
static const DWORD CHUNK_BACK = MNG_CHUNK('B', 'A', 'C', 'K');
static const DWORD CHUNK_LOOP = MNG_CHUNK('L', 'O', 'O', 'P');
static const DWORD CHUNK_ENDL = MNG_CHUNK('E', 'N', 'D', 'L');
static const DWORD CHUNK_TERM = MNG_CHUNK('T', 'E', 'R', 'M');
static const DWORD CHUNK_SHOW = MNG_CHUNK('S', 'H', 'O', 'W');
static const DWORD CHUNK_SEEK = MNG_CHUNK('S', 'E', 'E', 'K');
static const DWORD CHUNK_PLTE = MNG_CHUNK('P', 'L', 'T', 'E');
static const DWORD CHUNK_tRNS = MNG_CHUNK('t', 'R', 'N', 'S');
static const DWORD CHUNK_gAMA = MNG_CHUNK('g', 'A', 'M', 'A');
static const DWORD CHUNK_cHRM = MNG_CHUNK('c', 'H', 'R', 'M');
static const DWORD CHUNK_sRGB = MNG_CHUNK('s', 'R', 'G', 'B');
static const DWORD CHUNK_iCCP = MNG_CHUNK('i', 'C', 'C', 'P');
static const DWORD CHUNK_pHYs = MNG_CHUNK('p', 'H', 'Y', 's');
static const DWORD CHUNK_bKGD = MNG_CHUNK('b', 'K', 'G', 'D');
static const DWORD CHUNK_PAST = MNG_CHUNK('P', 'A', 'S', 'T');
static const DWORD CHUNK_MAGN = MNG_CHUNK('M', 'A', 'G', 'N');
static const DWORD CHUNK_CLON = MNG_CHUNK('C', 'L', 'O', 'N');
static const DWORD CHUNK_DISC = MNG_CHUNK('D', 'I', 'S', 'C');
static const DWORD CHUNK_MOVE = MNG_CHUNK('M', 'O', 'V', 'E');
static const DWORD CHUNK_CLIP = MNG_CHUNK('C', 'L', 'I', 'P');

#define MNG_CHUNK_OVERHEAD	12

#define MNG_MAX_CHUNK_PAYLOAD	(64u * 1024u * 1024u)

#define MNG_INFINITE_ITERATIONS	0x7FFFFFFF

// 1 GiB at 32 bpp; MHDR may claim any canvas size
#define MNG_MAX_CANVAS_PIXELS	((UINT64)1 << 28)

// ----------------------------------------------------------
//   Reading big-endian values out of a chunk
// ----------------------------------------------------------

static inline DWORD
GetDWORD(const BYTE *p) {
	return ((DWORD)p[0] << 24) | ((DWORD)p[1] << 16) | ((DWORD)p[2] << 8) | (DWORD)p[3];
}

static inline LONG
GetLONG(const BYTE *p) {
	return (LONG)GetDWORD(p);
}

static inline WORD
GetWORD(const BYTE *p) {
	return (WORD)(((WORD)p[0] << 8) | (WORD)p[1]);
}

static inline void
PutWORD(BYTE *p, WORD value) {
	p[0] = (BYTE)(value >> 8);
	p[1] = (BYTE)(value);
}

static inline void
PutDWORD(BYTE *p, DWORD value) {
	p[0] = (BYTE)(value >> 24);
	p[1] = (BYTE)(value >> 16);
	p[2] = (BYTE)(value >> 8);
	p[3] = (BYTE)(value);
}

// ==========================================================
// The index Open() builds
// ==========================================================

// global chunks as of one point in the stream; MNG can redefine them
struct MNGGlobals {
	std::vector<BYTE> plte;
	std::vector<BYTE> trns;
	std::vector<BYTE> gama;
	std::vector<BYTE> chrm;
	std::vector<BYTE> srgb;
	std::vector<BYTE> iccp;
	std::vector<BYTE> phys;
};

struct MNGFrame {
	INT64 offset;			//! first chunk: IHDR, JHDR or BASI
	DWORD length;			//! through the CRC of IEND
	BOOL is_jng;
	BOOL is_basi;
	DWORD width, height;

	DWORD delay_ticks;		//! MHDR ticks
	LONG x, y;
	BOOL do_not_show;
	BOOL has_clip;
	LONG clip_left, clip_right, clip_top, clip_bottom;

	BYTE framing_mode;
	int subframe;
	BOOL restore_background; //! background layer drawn before it

	size_t globals;			//! index into MNGinfo::globals

	MNGFrame() : offset(0), length(0), is_jng(FALSE), is_basi(FALSE), width(0), height(0),
		delay_ticks(0), x(0), y(0), do_not_show(FALSE), has_clip(FALSE),
		clip_left(0), clip_right(0), clip_top(0), clip_bottom(0),
		framing_mode(1), subframe(0), restore_background(FALSE), globals(0) {
	}
};

struct MNGOutFrame {
	std::vector<BYTE> png;	//! IHDR..IEND, no signature
	DWORD width, height;
	LONG x, y;
	DWORD delay_ms;
	BYTE disposal;			//! GIF numbering
	BOOL has_alpha;

	MNGOutFrame() : width(0), height(0), x(0), y(0), delay_ms(0),
		disposal(GIF_DISPOSAL_LEAVE), has_alpha(FALSE) {
	}
};

struct MNGinfo {
	BOOL read;

	BOOL has_mhdr;
	DWORD canvas_width, canvas_height;
	DWORD ticks_per_second;
	DWORD nominal_layer_count, nominal_frame_count, nominal_play_time;
	DWORD simplicity;

	LONG loop_count;			//! 1 = once, 0 = forever
	BOOL has_background;
	RGBQUAD background;

	std::vector<MNGGlobals> globals;
	std::vector<MNGFrame> frames;

	BOOL complex_features;		//! delta image or object-buffer chunk seen
	BOOL warned;

	// MNG_PLAYBACK cache: the last composed canvas
	FIBITMAP *canvas;
	int canvas_page;			//! -1 when none

	// ---------- writing ----------

	std::vector<MNGOutFrame> out_frames;
	int out_flags;
	DWORD out_canvas_width;		//! LogicalWidth, grown to fit
	DWORD out_canvas_height;
	LONG out_loop;				//! 1 = once, 0 = forever
	BOOL out_has_background;
	RGBQUAD out_background;

	MNGinfo() : read(FALSE), has_mhdr(FALSE), canvas_width(0), canvas_height(0),
		ticks_per_second(0), nominal_layer_count(0), nominal_frame_count(0),
		nominal_play_time(0), simplicity(0), loop_count(1), has_background(FALSE),
		complex_features(FALSE), warned(FALSE), canvas(NULL), canvas_page(-1),
		out_flags(0), out_canvas_width(0), out_canvas_height(0), out_loop(1),
		out_has_background(FALSE) {
		background.rgbRed = background.rgbGreen = background.rgbBlue = 0;
		background.rgbReserved = 255;
		out_background.rgbRed = out_background.rgbGreen = out_background.rgbBlue = 0;
		out_background.rgbReserved = 255;
	}

	~MNGinfo() {
		if(canvas) {
			FreeImage_Unload(canvas);
		}
	}
};

static void
WarnComplex(MNGinfo *info) {
	if(info->warned) {
		return;
	}
	info->warned = TRUE;
	FreeImage_OutputMessageProc(s_format_id,
		"MNG: this file uses delta images or stored object buffers, which are not rendered. "
		"The embedded images are read as they are stored; frames that depend on a delta will differ from a full MNG viewer.");
}

// ==========================================================
// Walking the stream
// ==========================================================

static INT64
MNG_GetFileLength(FreeImageIO *io, fi_handle handle) {
	const INT64 start_pos = io->tell_proc(handle);
	io->seek_proc(handle, 0, SEEK_END);
	const INT64 file_length = io->tell_proc(handle);
	io->seek_proc(handle, start_pos, SEEK_SET);
	return file_length;
}

static BOOL
ReadChunkHeader(FreeImageIO *io, fi_handle handle, DWORD *length, DWORD *type) {
	BYTE header[8];
	if(io->read_proc(header, 1, 8, handle) != 8) {
		return FALSE;
	}
	*length = GetDWORD(&header[0]);
	*type = GetDWORD(&header[4]);
	return TRUE;
}

// *cut: the file ends inside the stream after some of its image data; the stream runs to the end of the file
static BOOL
ScanEmbeddedStream(FreeImageIO *io, fi_handle handle, INT64 start, INT64 file_length, DWORD *out_length, BOOL *cut) {
	io->seek_proc(handle, start, SEEK_SET);
	*cut = FALSE;
	BOOL has_data = FALSE;

	while(TRUE) {
		const INT64 pos = io->tell_proc(handle);
		DWORD length = 0, type = 0;
		const BOOL header = (pos >= 0) && (pos + 8 <= file_length) && ReadChunkHeader(io, handle, &length, &type);
		const BOOL data = header && ((type == CHUNK_IDAT) || (type == CHUNK_JDAT)) && (pos + 8 < file_length);
		if(!header || ((INT64)length > file_length) || (pos + 8 + (INT64)length + 4 > file_length)) {
			if(has_data || data) {
				*out_length = (DWORD)(file_length - start);
				*cut = TRUE;
				return TRUE;
			}
			return FALSE;
		}
		has_data |= data;
		io->seek_proc(handle, (INT64)length + 4, SEEK_CUR);

		if(type == CHUNK_IEND) {
			const INT64 end = io->tell_proc(handle);
			if(end <= start) {
				return FALSE;
			}
			*out_length = (DWORD)(end - start);
			return TRUE;
		}
	}
}

static BOOL
ReadBytesAt(FreeImageIO *io, fi_handle handle, INT64 offset, DWORD length, std::vector<BYTE>& out) {
	if(length == 0) {
		return FALSE;
	}
	try {
		out.resize(length);
	} catch(std::bad_alloc&) {
		return FALSE;
	}
	io->seek_proc(handle, offset, SEEK_SET);
	return (io->read_proc(&out[0], 1, length, handle) == length);
}

// ==========================================================
// The chunks that drive the animation
// ==========================================================

struct MNGFramingState {
	BYTE framing_mode;
	DWORD default_delay;	//! ticks
	DWORD current_delay;	//! for the upcoming subframe
	BOOL has_clip;
	LONG clip_left, clip_right, clip_top, clip_bottom;

	MNGFramingState() : framing_mode(1), default_delay(1), current_delay(1),
		has_clip(FALSE), clip_left(0), clip_right(0), clip_top(0), clip_bottom(0) {
	}
};

// FRAM (MNG 1.0, 4.3.2); optional fields are omitted as a group
static void
ParseFRAM(const BYTE *payload, DWORD length, MNGFramingState *state) {
	if(length == 0) {
		// empty FRAM: a subframe delimiter; one-shot values expire
		state->current_delay = state->default_delay;
		return;
	}

	const BYTE framing_mode = payload[0];
	if((framing_mode >= 1) && (framing_mode <= 4)) {
		state->framing_mode = framing_mode;
	}

	DWORD pos = 1;

	if(pos < length) {
		DWORD separator = pos;
		while((separator < length) && (payload[separator] != 0)) {
			separator++;
		}
		pos = (separator < length) ? separator + 1 : length;
	}

	if(pos + 4 > length) {
		state->current_delay = state->default_delay;
		return;
	}

	const BYTE change_delay = payload[pos];
	const BYTE change_timeout = payload[pos + 1];
	const BYTE change_clipping = payload[pos + 2];
	const BYTE change_sync = payload[pos + 3];
	pos += 4;

	DWORD delay = state->default_delay;
	BOOL got_delay = FALSE;
	if(change_delay && (pos + 4 <= length)) {
		delay = GetDWORD(&payload[pos]);
		pos += 4;
		got_delay = TRUE;
	}
	if(change_timeout && (pos + 4 <= length)) {
		pos += 4;
	}
	if(change_clipping && (pos + 17 <= length)) {
		const BYTE delta_type = payload[pos];
		const LONG left = GetLONG(&payload[pos + 1]);
		const LONG right = GetLONG(&payload[pos + 5]);
		const LONG top = GetLONG(&payload[pos + 9]);
		const LONG bottom = GetLONG(&payload[pos + 13]);
		pos += 17;

		if(delta_type == 1) {
			state->clip_left += left;
			state->clip_right += right;
			state->clip_top += top;
			state->clip_bottom += bottom;
		} else {
			state->clip_left = left;
			state->clip_right = right;
			state->clip_top = top;
			state->clip_bottom = bottom;
		}
		state->has_clip = TRUE;
	}
	(void)change_sync;

	if(got_delay) {
		state->current_delay = delay;
		if(change_delay == 2) {
			state->default_delay = delay;
		}
	} else {
		state->current_delay = state->default_delay;
	}
}

struct MNGObjectState {
	WORD id;
	LONG x, y;
	BOOL do_not_show;
	BOOL has_clip;
	LONG clip_left, clip_right, clip_top, clip_bottom;

	MNGObjectState() : id(0), x(0), y(0), do_not_show(FALSE), has_clip(FALSE),
		clip_left(0), clip_right(0), clip_top(0), clip_bottom(0) {
	}
};

// image stored under a nonzero DEFI id, for SHOW
struct MNGObject {
	WORD id;
	MNGFrame frame;
	BOOL do_not_show;

	MNGObject() : id(0), do_not_show(FALSE) {
	}
};

// SHOW mode 6/7 position, one per range
struct MNGShowCursor {
	WORD low, high;
	size_t next;
};

#define MNG_MAX_FRAMES	65536

// DEFI (MNG 1.0, 4.2.1): applies only the fields present
static void
ParseDEFI(const BYTE *payload, DWORD length, MNGObjectState *state) {
	if(length < 2) {
		return;
	}
	state->id = GetWORD(&payload[0]);
	if(length >= 3) {
		state->do_not_show = (payload[2] != 0) ? TRUE : FALSE;
	}
	// payload[3] (concrete) only matters to delta images
	if(length >= 12) {
		state->x = GetLONG(&payload[4]);
		state->y = GetLONG(&payload[8]);
	}
	if(length >= 28) {
		state->clip_left = GetLONG(&payload[12]);
		state->clip_right = GetLONG(&payload[16]);
		state->clip_top = GetLONG(&payload[20]);
		state->clip_bottom = GetLONG(&payload[24]);
		state->has_clip = TRUE;
	}
}

// BACK (MNG 1.0, 4.3.1); the background image id is ignored
static void
ParseBACK(const BYTE *payload, DWORD length, MNGinfo *info) {
	if(length < 6) {
		return;
	}
	info->background.rgbRed = (BYTE)(GetWORD(&payload[0]) >> 8);
	info->background.rgbGreen = (BYTE)(GetWORD(&payload[2]) >> 8);
	info->background.rgbBlue = (BYTE)(GetWORD(&payload[4]) >> 8);
	info->background.rgbReserved = 255;
	info->has_background = TRUE;
}

// skip a zero-count LOOP up to its matching ENDL
static BOOL
SkipToMatchingENDL(FreeImageIO *io, fi_handle handle, INT64 file_length) {
	int depth = 1;

	while(depth > 0) {
		const INT64 chunk_start = io->tell_proc(handle);
		if((chunk_start < 0) || (chunk_start + MNG_CHUNK_OVERHEAD > file_length)) {
			return FALSE;
		}
		DWORD length = 0, type = 0;
		if(!ReadChunkHeader(io, handle, &length, &type)) {
			return FALSE;
		}
		const INT64 payload_start = chunk_start + 8;
		if(((INT64)length > file_length) || (payload_start + (INT64)length + 4 > file_length)) {
			return FALSE;
		}

		if(type == CHUNK_LOOP) {
			depth++;
		} else if(type == CHUNK_ENDL) {
			depth--;
		} else if(type == CHUNK_MEND) {
			// unclosed loop: leave MEND to the caller
			io->seek_proc(handle, chunk_start, SEEK_SET);
			return TRUE;
		}

		io->seek_proc(handle, payload_start + (INT64)length + 4, SEEK_SET);
	}

	return TRUE;
}

// modes 2 and 4: only a subframe's last layer keeps the delay
static void
ApplyFramingModes(MNGinfo *info) {
	const size_t count = info->frames.size();

	for(size_t i = 0; i < count; i++) {
		MNGFrame& frame = info->frames[i];
		if((frame.framing_mode != 2) && (frame.framing_mode != 4)) {
			continue;
		}
		const BOOL last_in_subframe = (i + 1 == count) ||
			(info->frames[i + 1].subframe != frame.subframe);
		if(!last_in_subframe) {
			frame.delay_ticks = 0;
		}
	}
}

static BOOL
AddLayer(MNGinfo *info, MNGFrame frame, const MNGFramingState& framing,
		 int subframe, BOOL *first_image) {
	if(info->frames.size() >= MNG_MAX_FRAMES) {
		return FALSE;
	}

	frame.delay_ticks = framing.current_delay;
	frame.framing_mode = framing.framing_mode;
	frame.subframe = subframe;
	frame.do_not_show = FALSE;

	// FRAM's layer clipping overrides DEFI's
	if(framing.has_clip) {
		frame.has_clip = TRUE;
		frame.clip_left = framing.clip_left;
		frame.clip_right = framing.clip_right;
		frame.clip_top = framing.clip_top;
		frame.clip_bottom = framing.clip_bottom;
	}

	// a background layer before the first image; modes 3 and 4 add more
	frame.restore_background = FALSE;
	if(*first_image) {
		frame.restore_background = TRUE;
	} else if(framing.framing_mode == 3) {
		frame.restore_background = TRUE;
	} else if(framing.framing_mode == 4) {
		frame.restore_background = info->frames.empty() ? TRUE :
			(info->frames.back().subframe != subframe) ? TRUE : FALSE;
	}
	*first_image = FALSE;

	info->frames.push_back(frame);
	return TRUE;
}

static MNGObject *
FindObject(std::vector<MNGObject>& objects, WORD id) {
	for(size_t i = 0; i < objects.size(); i++) {
		if(objects[i].id == id) {
			return &objects[i];
		}
	}
	return NULL;
}

// ==========================================================
// Building the index
// ==========================================================

static BOOL
ParseStream(FreeImageIO *io, fi_handle handle, INT64 start, MNGinfo *info) {
	const INT64 file_length = MNG_GetFileLength(io, handle);
	if(file_length <= start + MNG_SIGNATURE_SIZE) {
		return FALSE;
	}
	io->seek_proc(handle, start + MNG_SIGNATURE_SIZE, SEEK_SET);

	MNGFramingState framing;
	MNGObjectState object;
	MNGGlobals globals;

	// a DEFI can name an id before an image is stored under it
	std::vector<MNGObjectState> defined;
	std::vector<MNGObject> objects;
	std::vector<MNGShowCursor> cursors;

	size_t globals_index = (size_t)-1;

	int subframe = 0;
	BOOL first_image = TRUE;
	BOOL seen_mend = FALSE;

	// an outer LOOP becomes the loop count; images are not repeated
	int loop_depth = 0;
	BOOL have_outer_loop = FALSE;
	DWORD outer_loop_count = 1;
	size_t outer_loop_first = 0, outer_loop_last = 0;

	BOOL have_term = FALSE;
	DWORD term_iterations = 1;

	std::vector<BYTE> payload;

	while(!seen_mend) {
		const INT64 chunk_start = io->tell_proc(handle);
		if((chunk_start < 0) || (chunk_start + MNG_CHUNK_OVERHEAD > file_length)) {
			break;
		}

		DWORD length = 0, type = 0;
		if(!ReadChunkHeader(io, handle, &length, &type)) {
			break;
		}
		const INT64 payload_start = chunk_start + 8;
		if(((INT64)length > file_length) || (payload_start + (INT64)length + 4 > file_length)) {
			FreeImage_OutputMessageProc(s_format_id,
				"MNG: a chunk claims %u bytes, which runs past the end of the file", length);
			break;
		}
		const INT64 next_chunk = payload_start + (INT64)length + 4;

		// skip embedded streams whole, so their PLTE is not taken as global
		if((type == CHUNK_IHDR) || (type == CHUNK_JHDR) || (type == CHUNK_BASI) || (type == CHUNK_DHDR)) {
			DWORD stream_length = 0;
			BOOL stream_cut = FALSE;
			if(!ScanEmbeddedStream(io, handle, chunk_start, file_length, &stream_length, &stream_cut)) {
				FreeImage_OutputMessageProc(s_format_id,
					"MNG: an embedded image has no IEND - the file ends inside it");
				break;
			}
			if(stream_cut) {
				FreeImage_OutputMessageProc(s_format_id,
					"MNG: the file ends inside an embedded image, which is decoded as far as it goes");
			}

			if(type == CHUNK_DHDR) {
				info->complex_features = TRUE;
				WarnComplex(info);
			} else {
				MNGFrame frame;
				frame.offset = chunk_start;
				frame.length = stream_length;
				frame.is_jng = (type == CHUNK_JHDR) ? TRUE : FALSE;
				frame.is_basi = (type == CHUNK_BASI) ? TRUE : FALSE;

				// IHDR, BASI and JHDR all open with a 4-byte width and height
				if(length >= 8) {
					if(!ReadBytesAt(io, handle, payload_start, (length < 16) ? length : 16, payload)) {
						break;
					}
					frame.width = GetDWORD(&payload[0]);
					frame.height = GetDWORD(&payload[4]);
					// filter 64 (intrapixel differencing): libpng lacks it
					if(!frame.is_jng && (length >= 13) && (payload[11] == 64)) {
						info->complex_features = TRUE;
						WarnComplex(info);
					}
				}

				frame.x = object.x;
				frame.y = object.y;
				if(object.has_clip) {
					frame.has_clip = TRUE;
					frame.clip_left = object.clip_left;
					frame.clip_right = object.clip_right;
					frame.clip_top = object.clip_top;
					frame.clip_bottom = object.clip_bottom;
				}

				if(globals_index == (size_t)-1) {
					info->globals.push_back(globals);
					globals_index = info->globals.size() - 1;
				}
				frame.globals = globals_index;

				if(object.id != 0) {
					MNGObject *stored = FindObject(objects, object.id);
					if(!stored) {
						if(objects.size() < MNG_MAX_FRAMES) {
							objects.push_back(MNGObject());
							stored = &objects.back();
							stored->id = object.id;
						}
					}
					if(stored) {
						stored->frame = frame;
						stored->do_not_show = object.do_not_show;
					}
				}

				// hidden objects become pages only when shown
				if(!object.do_not_show) {
					if(!AddLayer(info, frame, framing, subframe, &first_image)) {
						FreeImage_OutputMessageProc(s_format_id,
							"MNG: the file names more than %d layers", MNG_MAX_FRAMES);
						break;
					}
					if(have_outer_loop && (loop_depth > 0)) {
						outer_loop_last = info->frames.size();
					}
				}
			}

			io->seek_proc(handle, chunk_start + (INT64)stream_length, SEEK_SET);
			continue;
		}

		const BOOL want_payload =
			(type == CHUNK_MHDR) || (type == CHUNK_FRAM) || (type == CHUNK_DEFI) ||
			(type == CHUNK_BACK) || (type == CHUNK_LOOP) || (type == CHUNK_ENDL) ||
			(type == CHUNK_TERM) || (type == CHUNK_SHOW) || (type == CHUNK_PLTE) ||
			(type == CHUNK_tRNS) ||
			(type == CHUNK_gAMA) || (type == CHUNK_cHRM) || (type == CHUNK_sRGB) ||
			(type == CHUNK_iCCP) || (type == CHUNK_pHYs) || (type == CHUNK_bKGD);

		payload.clear();
		if(want_payload && (length > 0)) {
			if(length > MNG_MAX_CHUNK_PAYLOAD) {
				FreeImage_OutputMessageProc(s_format_id,
					"MNG: refusing a %u byte chunk payload", length);
				break;
			}
			if(!ReadBytesAt(io, handle, payload_start, length, payload)) {
				break;
			}
		}
		const BYTE *data = payload.empty() ? NULL : &payload[0];

		// CRC-check the chunks that steer parsing; drop a bad one and go on
		if(want_payload) {
			BYTE type_bytes[5];
			PutDWORD(type_bytes, type);
			type_bytes[4] = '\0';

			DWORD crc = FreeImage_ZLibCRC32(0, type_bytes, 4);
			if(length > 0) {
				crc = FreeImage_ZLibCRC32(crc, &payload[0], length);
			}

			std::vector<BYTE> crc_bytes;
			if(!ReadBytesAt(io, handle, payload_start + (INT64)length, 4, crc_bytes)) {
				break;
			}
			if(crc != GetDWORD(&crc_bytes[0])) {
				FreeImage_OutputMessageProc(s_format_id,
					"MNG: the %s chunk has a bad CRC and is ignored - the file is damaged",
					(const char*)type_bytes);
				io->seek_proc(handle, next_chunk, SEEK_SET);
				continue;
			}
		}

		if(type == CHUNK_MHDR) {
			if(length >= 28) {
				info->has_mhdr = TRUE;
				info->canvas_width = GetDWORD(&data[0]);
				info->canvas_height = GetDWORD(&data[4]);
				info->ticks_per_second = GetDWORD(&data[8]);
				info->nominal_layer_count = GetDWORD(&data[12]);
				info->nominal_frame_count = GetDWORD(&data[16]);
				info->nominal_play_time = GetDWORD(&data[20]);
				info->simplicity = GetDWORD(&data[24]);

				// bit 0 off: the profile says nothing
				if((info->simplicity & MNG_PROFILE_VALID) &&
				   (info->simplicity & (MNG_PROFILE_COMPLEX | MNG_PROFILE_DELTA_PNG))) {
					info->complex_features = TRUE;
					WarnComplex(info);
				}
			} else {
				FreeImage_OutputMessageProc(s_format_id,
					"MNG: the MHDR chunk is %u bytes instead of 28", length);
			}
		} else if(type == CHUNK_MEND) {
			seen_mend = TRUE;
		} else if(type == CHUNK_FRAM) {
			subframe++;
			ParseFRAM(data, length, &framing);
		} else if(type == CHUNK_DEFI) {
			// defaults fill omitted fields only for a new id
			if(length >= 2) {
				const WORD id = GetWORD(&data[0]);
				MNGObjectState state;
				state.clip_right = (LONG)info->canvas_width;
				state.clip_bottom = (LONG)info->canvas_height;

				size_t slot = defined.size();
				for(size_t i = 0; i < defined.size(); i++) {
					if(defined[i].id == id) {
						state = defined[i];
						slot = i;
						break;
					}
				}
				state.id = id;
				ParseDEFI(data, length, &state);

				if(slot < defined.size()) {
					defined[slot] = state;
				} else if(defined.size() < MNG_MAX_FRAMES) {
					defined.push_back(state);
				}
				object = state;
			}
		} else if(type == CHUNK_SHOW) {
			// SHOW: modes 0/2/4/6 display, 1/3/5/7 only change visibility
			WORD first_id = 1, last_id = 0xFFFF;
			BYTE show_mode = 0;
			if(length >= 2) {
				first_id = GetWORD(&data[0]);
				last_id = first_id;
			}
			if(length >= 4) {
				last_id = GetWORD(&data[2]);
			}
			if(length >= 5) {
				show_mode = data[4];
			}

			const WORD low = MIN(first_id, last_id);
			const WORD high = MAX(first_id, last_id);

			// in reverse order if last_image < first_image
			std::vector<size_t> range;
			for(size_t i = 0; i < objects.size(); i++) {
				if((objects[i].id >= low) && (objects[i].id <= high)) {
					range.push_back(i);
				}
			}
			if(last_id < first_id) {
				std::reverse(range.begin(), range.end());
			}

			if((show_mode == 6) || (show_mode == 7)) {
				// modes 6/7 step one object per SHOW; one cursor per range
				size_t slot = cursors.size();
				for(size_t i = 0; i < cursors.size(); i++) {
					if((cursors[i].low == low) && (cursors[i].high == high)) {
						slot = i;
						break;
					}
				}
				if(slot == cursors.size()) {
					MNGShowCursor cursor;
					cursor.low = low;
					cursor.high = high;
					cursor.next = 0;
					cursors.push_back(cursor);
				}

				if(!range.empty()) {
					const size_t chosen = cursors[slot].next % range.size();
					for(size_t i = 0; i < range.size(); i++) {
						objects[range[i]].do_not_show = (i == chosen) ? FALSE : TRUE;
					}
					cursors[slot].next = chosen + 1;

					if(show_mode == 6) {
						if(!AddLayer(info, objects[range[chosen]].frame, framing,
									 subframe, &first_image)) {
							FreeImage_OutputMessageProc(s_format_id,
								"MNG: the file names more than %d layers", MNG_MAX_FRAMES);
							break;
						}
						if(have_outer_loop && (loop_depth > 0)) {
							outer_loop_last = info->frames.size();
						}
					}
				}
			} else {
				// modes 3 and 5 change visibility without displaying, and 1 hides
				const BOOL displays = (show_mode == 0) || (show_mode == 2) || (show_mode == 4);

				for(size_t i = 0; i < range.size(); i++) {
					MNGObject& stored = objects[range[i]];
					switch(show_mode) {
						case 0:
						case 3: stored.do_not_show = FALSE; break;
						case 1: stored.do_not_show = TRUE; break;
						case 4:
						case 5: stored.do_not_show = stored.do_not_show ? FALSE : TRUE; break;
						default: break;	// 2 displays without changing the flag
					}
					if(displays && !stored.do_not_show) {
						if(!AddLayer(info, stored.frame, framing, subframe, &first_image)) {
							FreeImage_OutputMessageProc(s_format_id,
								"MNG: the file names more than %d layers", MNG_MAX_FRAMES);
							break;
						}
						if(have_outer_loop && (loop_depth > 0)) {
							outer_loop_last = info->frames.size();
						}
					}
				}
			}
		} else if(type == CHUNK_BACK) {
			ParseBACK(data, length, info);
		} else if(type == CHUNK_TERM) {
			// TERM 3 repeats; anything else plays once
			if((length >= 10) && (data[0] == 3)) {
				have_term = TRUE;
				term_iterations = GetDWORD(&data[6]);
			} else if(length >= 1) {
				have_term = TRUE;
				term_iterations = 1;
			}
		} else if(type == CHUNK_LOOP) {
			DWORD iterations = 1;
			if(length >= 5) {
				iterations = GetDWORD(&data[1]);
			}
			if(iterations == 0) {
				io->seek_proc(handle, next_chunk, SEEK_SET);
				if(!SkipToMatchingENDL(io, handle, file_length)) {
					break;
				}
				continue;
			}
			loop_depth++;
			if(loop_depth == 1) {
				have_outer_loop = TRUE;
				outer_loop_count = iterations;
				outer_loop_first = info->frames.size();
				outer_loop_last = info->frames.size();
			}
		} else if(type == CHUNK_ENDL) {
			if(loop_depth > 0) {
				loop_depth--;
			}
		} else if(type == CHUNK_SEEK) {
			// SEEK undefines object attributes, not the images
			object = MNGObjectState();
			defined.clear();
			cursors.clear();
		} else if((type == CHUNK_PAST) || (type == CHUNK_MAGN) || (type == CHUNK_CLON) ||
				  (type == CHUNK_DISC) || (type == CHUNK_MOVE) || (type == CHUNK_CLIP)) {
			// object-buffer chunks: not rendered
			info->complex_features = TRUE;
			WarnComplex(info);
		} else if(type == CHUNK_PLTE) {
			globals.plte.assign(payload.begin(), payload.end());
			globals_index = (size_t)-1;
		} else if(type == CHUNK_tRNS) {
			globals.trns.assign(payload.begin(), payload.end());
			globals_index = (size_t)-1;
		} else if(type == CHUNK_gAMA) {
			globals.gama.assign(payload.begin(), payload.end());
			globals_index = (size_t)-1;
		} else if(type == CHUNK_cHRM) {
			globals.chrm.assign(payload.begin(), payload.end());
			globals_index = (size_t)-1;
		} else if(type == CHUNK_sRGB) {
			globals.srgb.assign(payload.begin(), payload.end());
			globals_index = (size_t)-1;
		} else if(type == CHUNK_iCCP) {
			globals.iccp.assign(payload.begin(), payload.end());
			globals_index = (size_t)-1;
		} else if(type == CHUNK_pHYs) {
			globals.phys.assign(payload.begin(), payload.end());
			globals_index = (size_t)-1;
		} else if(type == CHUNK_bKGD) {
			// a global bKGD stands in for BACK when there is no BACK
			if(!info->has_background && (length >= 6)) {
				info->background.rgbRed = (BYTE)(GetWORD(&data[0]) >> 8);
				info->background.rgbGreen = (BYTE)(GetWORD(&data[2]) >> 8);
				info->background.rgbBlue = (BYTE)(GetWORD(&data[4]) >> 8);
				info->background.rgbReserved = 255;
				info->has_background = TRUE;
			}
		}

		io->seek_proc(handle, next_chunk, SEEK_SET);
	}

	// TERM wins over LOOP
	if(have_term) {
		info->loop_count = (term_iterations >= MNG_INFINITE_ITERATIONS) ? 0 : (LONG)term_iterations;
	} else if(have_outer_loop && (outer_loop_first == 0) && (outer_loop_last == info->frames.size())
			  && !info->frames.empty()) {
		info->loop_count = (outer_loop_count >= MNG_INFINITE_ITERATIONS) ? 0 : (LONG)outer_loop_count;
	}

	// nothing ever shown: hand over the stored images in order
	if(info->frames.empty() && !objects.empty()) {
		for(size_t i = 0; i < objects.size(); i++) {
			MNGFrame frame = objects[i].frame;
			frame.delay_ticks = 0;
			frame.do_not_show = FALSE;
			frame.restore_background = (i == 0) ? TRUE : FALSE;
			info->frames.push_back(frame);
		}
		FreeImage_OutputMessageProc(s_format_id,
			"MNG: no image in this file is ever displayed; its %d stored image(s) are "
			"read in the order they were defined", (int)info->frames.size());
	}

	ApplyFramingModes(info);

	if(!info->has_mhdr) {
		// no MHDR: size the canvas to fit the images
		for(size_t i = 0; i < info->frames.size(); i++) {
			const MNGFrame& frame = info->frames[i];
			const DWORD right = (DWORD)MAX((LONG)0, frame.x + (LONG)frame.width);
			const DWORD bottom = (DWORD)MAX((LONG)0, frame.y + (LONG)frame.height);
			info->canvas_width = MAX(info->canvas_width, right);
			info->canvas_height = MAX(info->canvas_height, bottom);
		}
	}

	return !info->frames.empty();
}

// ==========================================================
// Turning a frame back into a file
// ==========================================================

static void
AppendChunk(std::vector<BYTE>& out, DWORD type, const BYTE *data, DWORD length) {
	BYTE header[8];
	PutDWORD(&header[0], length);
	PutDWORD(&header[4], type);

	DWORD crc = FreeImage_ZLibCRC32(0, &header[4], 4);
	if(length > 0) {
		crc = FreeImage_ZLibCRC32(crc, (BYTE*)data, length);
	}

	out.insert(out.end(), header, header + 8);
	if(length > 0) {
		out.insert(out.end(), data, data + length);
	}

	BYTE crc_bytes[4];
	PutDWORD(crc_bytes, crc);
	out.insert(out.end(), crc_bytes, crc_bytes + 4);
}

static void
AppendGlobalChunk(std::vector<BYTE>& out, DWORD type, const std::vector<BYTE>& payload) {
	AppendChunk(out, type, payload.empty() ? NULL : &payload[0], (DWORD)payload.size());
}

struct MNGChunkRef {
	DWORD type;
	const BYTE *payload;
	DWORD length;
	const BYTE *raw;
	DWORD raw_length;
	BOOL cut;	//! the stream ends inside it: length and raw_length count what is there
};

static BOOL
SplitChunks(const std::vector<BYTE>& stream, std::vector<MNGChunkRef>& out) {
	size_t pos = 0;
	while(pos + 8 <= stream.size()) {
		MNGChunkRef ref;
		ref.length = GetDWORD(&stream[pos]);
		ref.type = GetDWORD(&stream[pos + 4]);
		ref.raw = &stream[pos];
		ref.cut = FALSE;
		if((ref.length > stream.size()) || (pos + 12 + (size_t)ref.length > stream.size())) {
			// a cut stream keeps its last chunk as far as it goes; the PNG loader decodes that
			if(out.empty()) {
				return FALSE;
			}
			ref.raw_length = (DWORD)(stream.size() - pos);
			ref.length = MIN(ref.length, ref.raw_length - 8);
			ref.cut = TRUE;
		} else {
			ref.raw_length = ref.length + MNG_CHUNK_OVERHEAD;
		}
		ref.payload = (ref.length > 0) ? &stream[pos + 8] : NULL;
		out.push_back(ref);
		pos += ref.raw_length;
	}
	return !out.empty();
}

// splice in inherited globals; an empty PLTE means the global one
static BOOL
BuildPNGStream(const MNGinfo *info, const MNGFrame& frame, const std::vector<BYTE>& raw,
			   std::vector<BYTE>& out) {
	std::vector<MNGChunkRef> chunks;
	if(!SplitChunks(raw, chunks)) {
		return FALSE;
	}
	if(chunks[0].length < 13) {
		return FALSE;
	}
	const MNGGlobals& globals = info->globals[frame.globals];
	const BYTE colour_type = chunks[0].payload[9];

	out.insert(out.end(), g_png_signature, g_png_signature + 8);

	// BASI starts with a 13-byte IHDR
	AppendChunk(out, CHUNK_IHDR, chunks[0].payload, 13);

	// these precede PLTE; the image's own copy wins
	BOOL has_local_gama = FALSE, has_local_chrm = FALSE, has_local_srgb = FALSE;
	BOOL has_local_iccp = FALSE, has_local_phys = FALSE, has_local_trns = FALSE;
	BOOL has_local_plte = FALSE;
	for(size_t i = 0; i < chunks.size(); i++) {
		const DWORD type = chunks[i].type;
		if(type == CHUNK_gAMA) has_local_gama = TRUE;
		else if(type == CHUNK_cHRM) has_local_chrm = TRUE;
		else if(type == CHUNK_sRGB) has_local_srgb = TRUE;
		else if(type == CHUNK_iCCP) has_local_iccp = TRUE;
		else if(type == CHUNK_pHYs) has_local_phys = TRUE;
		else if(type == CHUNK_tRNS) has_local_trns = TRUE;
		else if((type == CHUNK_PLTE) && (chunks[i].length > 0)) has_local_plte = TRUE;
	}

	if(!has_local_iccp && !globals.iccp.empty()) {
		AppendGlobalChunk(out, CHUNK_iCCP, globals.iccp);
	}
	if(!has_local_srgb && !globals.srgb.empty()) {
		AppendGlobalChunk(out, CHUNK_sRGB, globals.srgb);
	}
	if(!has_local_gama && !globals.gama.empty()) {
		AppendGlobalChunk(out, CHUNK_gAMA, globals.gama);
	}
	if(!has_local_chrm && !globals.chrm.empty()) {
		AppendGlobalChunk(out, CHUNK_cHRM, globals.chrm);
	}

	BOOL wrote_plte = FALSE;
	BOOL wrote_trns = FALSE;
	BOOL wrote_pre_idat = FALSE;

	for(size_t i = 1; i < chunks.size(); i++) {
		const MNGChunkRef& chunk = chunks[i];

		if(chunk.type == CHUNK_IEND) {
			continue;	// written last, once
		}

		if(chunk.type == CHUNK_PLTE) {
			if((chunk.length == 0) && !globals.plte.empty()) {
				AppendGlobalChunk(out, CHUNK_PLTE, globals.plte);
			} else {
				out.insert(out.end(), chunk.raw, chunk.raw + chunk.raw_length);
			}
			wrote_plte = TRUE;
			continue;
		}

		if((chunk.type == CHUNK_IDAT) && !wrote_pre_idat) {
			wrote_pre_idat = TRUE;
			// an inherited PLTE, tRNS and pHYs go before IDAT
			if(!wrote_plte && !has_local_plte && (colour_type == 3) && !globals.plte.empty()) {
				AppendGlobalChunk(out, CHUNK_PLTE, globals.plte);
				wrote_plte = TRUE;
			}
			if(!wrote_trns && !has_local_trns && !globals.trns.empty()) {
				AppendGlobalChunk(out, CHUNK_tRNS, globals.trns);
				wrote_trns = TRUE;
			}
			if(!has_local_phys && !globals.phys.empty()) {
				AppendGlobalChunk(out, CHUNK_pHYs, globals.phys);
			}
		}

		if(chunk.type == CHUNK_tRNS) {
			wrote_trns = TRUE;
		}

		out.insert(out.end(), chunk.raw, chunk.raw + chunk.raw_length);
	}

	// a cut stream ends where the file does
	if(!chunks.back().cut) {
		AppendChunk(out, CHUNK_IEND, NULL, 0);
	}
	return TRUE;
}

static BOOL
BuildJNGStream(const std::vector<BYTE>& raw, std::vector<BYTE>& out) {
	if(raw.size() < MNG_CHUNK_OVERHEAD) {
		return FALSE;
	}
	out.insert(out.end(), g_jng_signature, g_jng_signature + 8);
	out.insert(out.end(), raw.begin(), raw.end());
	return TRUE;
}

static BOOL
HasImageData(const std::vector<BYTE>& raw) {
	std::vector<MNGChunkRef> chunks;
	if(!SplitChunks(raw, chunks)) {
		return FALSE;
	}
	for(size_t i = 0; i < chunks.size(); i++) {
		if(chunks[i].type == CHUNK_IDAT) {
			return TRUE;
		}
	}
	return FALSE;
}

// BASI without IDAT: fill with its 16-bit RGBA
static FIBITMAP *
CreateBASIFill(const std::vector<BYTE>& raw, const MNGFrame& frame, int flags) {
	std::vector<MNGChunkRef> chunks;
	if(!SplitChunks(raw, chunks) || (chunks[0].length < 13)) {
		return NULL;
	}
	const BYTE *payload = chunks[0].payload;
	const DWORD length = chunks[0].length;

	// omitted samples are 0; omitted alpha is opaque
	BYTE red = 0, green = 0, blue = 0, alpha = 255;
	if(length >= 19) {
		red = (BYTE)(GetWORD(&payload[13]) >> 8);
		green = (BYTE)(GetWORD(&payload[15]) >> 8);
		blue = (BYTE)(GetWORD(&payload[17]) >> 8);
	}
	if(length >= 21) {
		alpha = (BYTE)(GetWORD(&payload[19]) >> 8);
	}

	const BOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

	// same bound as the canvas
	if(!header_only &&
	   ((UINT64)frame.width * (UINT64)frame.height > MNG_MAX_CANVAS_PIXELS)) {
		FreeImage_OutputMessageProc(s_format_id,
			"MNG: refusing to fill a %ux%u BASI image", frame.width, frame.height);
		return NULL;
	}

	FIBITMAP *dib = FreeImage_AllocateHeader(header_only, (int)frame.width, (int)frame.height, 32,
		FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	if(!dib) {
		return NULL;
	}
	if(!header_only) {
		for(unsigned y = 0; y < FreeImage_GetHeight(dib); y++) {
			BYTE *line = FreeImage_GetScanLine(dib, y);
			for(unsigned x = 0; x < FreeImage_GetWidth(dib); x++) {
				line[FI_RGBA_RED] = red;
				line[FI_RGBA_GREEN] = green;
				line[FI_RGBA_BLUE] = blue;
				line[FI_RGBA_ALPHA] = alpha;
				line += 4;
			}
		}
	}
	return dib;
}

static FIBITMAP *
DecodeFrame(FreeImageIO *io, fi_handle handle, MNGinfo *info, int page, int flags) {
	if((page < 0) || (page >= (int)info->frames.size())) {
		return NULL;
	}
	const MNGFrame& frame = info->frames[page];

	std::vector<BYTE> raw;
	if(!ReadBytesAt(io, handle, frame.offset, frame.length, raw)) {
		FreeImage_OutputMessageProc(s_format_id, "MNG: cannot read the bytes of frame %d", page);
		return NULL;
	}

	std::vector<BYTE> stream;
	if(frame.is_jng) {
		if(!BuildJNGStream(raw, stream)) {
			return NULL;
		}
	} else {
		if(frame.is_basi && !HasImageData(raw)) {
			return CreateBASIFill(raw, frame, flags);
		}
		if(!BuildPNGStream(info, frame, raw, stream)) {
			FreeImage_OutputMessageProc(s_format_id, "MNG: frame %d is not a readable image", page);
			return NULL;
		}
	}

	FIMEMORY *hmem = FreeImage_OpenMemory64(&stream[0], (UINT64)stream.size());
	if(!hmem) {
		return NULL;
	}
	FIBITMAP *dib = FreeImage_LoadFromMemory(frame.is_jng ? FIF_JNG : FIF_PNG, hmem, flags);
	FreeImage_CloseMemory(hmem);

	return dib;
}

// ==========================================================
// MNG_PLAYBACK: the canvas a viewer would show
// ==========================================================

// BACK colour, or transparent when there is none
static void
FillBackground(FIBITMAP *canvas, const MNGinfo *info) {
	RGBQUAD colour;
	colour.rgbRed = info->has_background ? info->background.rgbRed : 0;
	colour.rgbGreen = info->has_background ? info->background.rgbGreen : 0;
	colour.rgbBlue = info->has_background ? info->background.rgbBlue : 0;
	colour.rgbReserved = info->has_background ? 255 : 0;

	const unsigned width = FreeImage_GetWidth(canvas);
	const unsigned height = FreeImage_GetHeight(canvas);
	for(unsigned y = 0; y < height; y++) {
		BYTE *line = FreeImage_GetScanLine(canvas, y);
		for(unsigned x = 0; x < width; x++) {
			line[FI_RGBA_RED] = colour.rgbRed;
			line[FI_RGBA_GREEN] = colour.rgbGreen;
			line[FI_RGBA_BLUE] = colour.rgbBlue;
			line[FI_RGBA_ALPHA] = colour.rgbReserved;
			line += 4;
		}
	}
}

// composite over, clipped; MNG y counts down from the top
static void
CompositeFrame(FIBITMAP *canvas, FIBITMAP *frame, const MNGFrame& info) {
	const int canvas_width = (int)FreeImage_GetWidth(canvas);
	const int canvas_height = (int)FreeImage_GetHeight(canvas);
	const int frame_width = (int)FreeImage_GetWidth(frame);
	const int frame_height = (int)FreeImage_GetHeight(frame);

	// the destination rectangle, in top-down coordinates
	int left = info.x;
	int top = info.y;
	int right = left + frame_width;
	int bottom = top + frame_height;

	if(info.has_clip) {
		// left/top inclusive, right/bottom exclusive
		left = MAX(left, (int)info.clip_left);
		top = MAX(top, (int)info.clip_top);
		right = MIN(right, (int)info.clip_right);
		bottom = MIN(bottom, (int)info.clip_bottom);
	}
	left = MAX(left, 0);
	top = MAX(top, 0);
	right = MIN(right, canvas_width);
	bottom = MIN(bottom, canvas_height);

	for(int y = top; y < bottom; y++) {
		const int source_y = y - info.y;
		if((source_y < 0) || (source_y >= frame_height)) {
			continue;
		}
		const BYTE *src = FreeImage_GetScanLine(frame, (unsigned)(frame_height - 1 - source_y));
		BYTE *dst = FreeImage_GetScanLine(canvas, (unsigned)(canvas_height - 1 - y));

		for(int x = left; x < right; x++) {
			const int source_x = x - info.x;
			if((source_x < 0) || (source_x >= frame_width)) {
				continue;
			}
			const BYTE *s = src + source_x * 4;
			BYTE *d = dst + x * 4;

			const unsigned alpha = s[FI_RGBA_ALPHA];
			if(alpha == 255) {
				d[FI_RGBA_RED] = s[FI_RGBA_RED];
				d[FI_RGBA_GREEN] = s[FI_RGBA_GREEN];
				d[FI_RGBA_BLUE] = s[FI_RGBA_BLUE];
				d[FI_RGBA_ALPHA] = 255;
			} else if(alpha > 0) {
				const unsigned dst_alpha = d[FI_RGBA_ALPHA];
				const unsigned out_alpha = alpha + dst_alpha * (255 - alpha) / 255;
				if(out_alpha > 0) {
					d[FI_RGBA_RED] = (BYTE)((s[FI_RGBA_RED] * alpha +
						d[FI_RGBA_RED] * dst_alpha * (255 - alpha) / 255) / out_alpha);
					d[FI_RGBA_GREEN] = (BYTE)((s[FI_RGBA_GREEN] * alpha +
						d[FI_RGBA_GREEN] * dst_alpha * (255 - alpha) / 255) / out_alpha);
					d[FI_RGBA_BLUE] = (BYTE)((s[FI_RGBA_BLUE] * alpha +
						d[FI_RGBA_BLUE] * dst_alpha * (255 - alpha) / 255) / out_alpha);
				}
				d[FI_RGBA_ALPHA] = (BYTE)out_alpha;
			}
		}
	}
}

static FIBITMAP *
RenderFrame(FreeImageIO *io, fi_handle handle, MNGinfo *info, int page, int flags) {
	if((page < 0) || (page >= (int)info->frames.size())) {
		return NULL;
	}

	const unsigned width = info->canvas_width ? info->canvas_width : info->frames[page].width;
	const unsigned height = info->canvas_height ? info->canvas_height : info->frames[page].height;
	if(!width || !height) {
		return NULL;
	}
	if((flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS) {
		return FreeImage_AllocateHeader(TRUE, (int)width, (int)height, 32,
			FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	}

	if((UINT64)width * (UINT64)height > MNG_MAX_CANVAS_PIXELS) {
		FreeImage_OutputMessageProc(s_format_id,
			"MNG: refusing to compose a %ux%u canvas - read the pages without "
			"MNG_PLAYBACK to get the images themselves", width, height);
		return NULL;
	}

	// reuse the cached canvas when moving forward
	int start = 0;
	if(info->canvas && (info->canvas_page >= 0) && (info->canvas_page < page) &&
	   (FreeImage_GetWidth(info->canvas) == width) && (FreeImage_GetHeight(info->canvas) == height)) {
		start = info->canvas_page + 1;
	} else {
		if(info->canvas) {
			FreeImage_Unload(info->canvas);
			info->canvas = NULL;
		}
		info->canvas_page = -1;
		info->canvas = FreeImage_Allocate((int)width, (int)height, 32,
			FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
		if(!info->canvas) {
			return NULL;
		}
		FillBackground(info->canvas, info);
	}

	for(int i = start; i <= page; i++) {
		const MNGFrame& frame = info->frames[i];

		if(frame.restore_background && (i > 0)) {
			FillBackground(info->canvas, info);
		}

		if(!frame.do_not_show) {
			FIBITMAP *dib = DecodeFrame(io, handle, info, i, flags & ~FIF_LOAD_NOPIXELS);
			if(dib) {
				FIBITMAP *frame32 = (FreeImage_GetBPP(dib) == 32 &&
					FreeImage_GetImageType(dib) == FIT_BITMAP) ? dib : FreeImage_ConvertTo32Bits(dib);
				if(frame32) {
					CompositeFrame(info->canvas, frame32, frame);
					if(frame32 != dib) {
						FreeImage_Unload(frame32);
					}
				}
				FreeImage_Unload(dib);
			}
		}
		info->canvas_page = i;
	}

	return FreeImage_Clone(info->canvas);
}

// ==========================================================
// Animation metadata
// ==========================================================

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

// ticks_per_second 0: infinite tick, no delays
static LONG
DelayToMilliseconds(const MNGinfo *info, DWORD ticks) {
	if(info->ticks_per_second == 0) {
		return 0;
	}
	const double ms = (double)ticks * 1000.0 / (double)info->ticks_per_second;
	if(ms >= 2147483647.0) {
		return 2147483647;
	}
	return (LONG)(ms + 0.5);
}

static void
SetFrameMetadata(FIBITMAP *dib, const MNGinfo *info, int page) {
	const MNGFrame& frame = info->frames[page];

	LONG duration = DelayToMilliseconds(info, frame.delay_ticks);
	WORD left = (WORD)MIN(MAX(frame.x, (LONG)0), (LONG)0xFFFF);
	WORD top = (WORD)MIN(MAX(frame.y, (LONG)0), (LONG)0xFFFF);

	// a background layer before the next frame = dispose to background
	BYTE disposal = GIF_DISPOSAL_LEAVE;
	if((page + 1 < (int)info->frames.size()) && info->frames[page + 1].restore_background) {
		disposal = GIF_DISPOSAL_BACKGROUND;
	}
	// MNG layers always blend over
	BYTE blend = 0;

	SetAnimTag(dib, "FrameTime", ANIMTAG_FRAMETIME, FIDT_LONG, 1, 4, &duration);
	SetAnimTag(dib, "FrameLeft", ANIMTAG_FRAMELEFT, FIDT_SHORT, 1, 2, &left);
	SetAnimTag(dib, "FrameTop", ANIMTAG_FRAMETOP, FIDT_SHORT, 1, 2, &top);
	SetAnimTag(dib, "DisposalMethod", ANIMTAG_DISPOSALMETHOD, FIDT_BYTE, 1, 1, &disposal);
	SetAnimTag(dib, "BlendMethod", ANIMTAG_BLENDMETHOD, FIDT_BYTE, 1, 1, &blend);

	// file-level tags go on every frame
	{
		WORD logicalwidth = (WORD)MIN(info->canvas_width, (DWORD)0xFFFF);
		WORD logicalheight = (WORD)MIN(info->canvas_height, (DWORD)0xFFFF);
		LONG loop = info->loop_count;
		SetAnimTag(dib, "LogicalWidth", ANIMTAG_LOGICALWIDTH, FIDT_SHORT, 1, 2, &logicalwidth);
		SetAnimTag(dib, "LogicalHeight", ANIMTAG_LOGICALHEIGHT, FIDT_SHORT, 1, 2, &logicalheight);
		SetAnimTag(dib, "Loop", ANIMTAG_LOOP, FIDT_LONG, 1, 4, &loop);
	}
}

// ==========================================================
// Writing
// ==========================================================

static BOOL
GetAnimTag(FIBITMAP *dib, const char *key, FREE_IMAGE_MDTYPE type, FITAG **tag) {
	if(FreeImage_GetMetadata(FIMD_ANIMATION, dib, key, tag) && *tag) {
		if((FreeImage_GetTagType(*tag) == type) && FreeImage_GetTagValue(*tag)) {
			return TRUE;
		}
	}
	return FALSE;
}

// PNG datastream without its 8-byte signature
static BOOL
EncodeFrame(FIBITMAP *dib, int flags, std::vector<BYTE>& out) {
	FIMEMORY *hmem = FreeImage_OpenMemory(NULL, 0);
	if(!hmem) {
		return FALSE;
	}

	BOOL bResult = FreeImage_SaveToMemory(FIF_PNG, dib, hmem, flags);
	if(bResult) {
		BYTE *data = NULL;
		DWORD size = 0;
		if(FreeImage_AcquireMemory(hmem, &data, &size) && data && (size > 8)) {
			try {
				out.assign(data + 8, data + size);
			} catch(std::bad_alloc&) {
				bResult = FALSE;
			}
		} else {
			bResult = FALSE;
		}
	}

	FreeImage_CloseMemory(hmem);
	return bResult;
}

static BOOL
WriteChunk(FreeImageIO *io, fi_handle handle, DWORD type, const BYTE *data, DWORD length) {
	BYTE header[8];
	PutDWORD(&header[0], length);
	PutDWORD(&header[4], type);

	DWORD crc = FreeImage_ZLibCRC32(0, &header[4], 4);
	if(length > 0) {
		crc = FreeImage_ZLibCRC32(crc, (BYTE*)data, length);
	}

	BYTE crc_bytes[4];
	PutDWORD(crc_bytes, crc);

	if(io->write_proc(header, 1, 8, handle) != 8) {
		return FALSE;
	}
	if((length > 0) && (io->write_proc((void*)data, 1, length, handle) != length)) {
		return FALSE;
	}
	return (io->write_proc(crc_bytes, 1, 4, handle) == 4);
}

// 1000 ticks/s; FRAM mode 3 = dispose to background, mode 1 = leave
static BOOL
WriteMNG(FreeImageIO *io, fi_handle handle, MNGinfo *info) {
	const size_t count = info->out_frames.size();
	if(count == 0) {
		return TRUE;	// no pages: write nothing
	}

	// grow the canvas to hold every frame
	DWORD canvas_width = info->out_canvas_width;
	DWORD canvas_height = info->out_canvas_height;
	BOOL has_alpha = FALSE;
	DWORD play_time = 0;

	for(size_t i = 0; i < count; i++) {
		const MNGOutFrame& frame = info->out_frames[i];
		const LONG right = frame.x + (LONG)frame.width;
		const LONG bottom = frame.y + (LONG)frame.height;
		if((right > 0) && ((DWORD)right > canvas_width)) {
			canvas_width = (DWORD)right;
		}
		if((bottom > 0) && ((DWORD)bottom > canvas_height)) {
			canvas_height = (DWORD)bottom;
		}
		if(frame.has_alpha) {
			has_alpha = TRUE;
		}
		// saturate at 2^31-1 (spec cap)
		play_time = (frame.delay_ms > MNG_INFINITE_ITERATIONS - play_time)
			? MNG_INFINITE_ITERATIONS : play_time + frame.delay_ms;
	}
	if(!canvas_width || !canvas_height) {
		return FALSE;
	}

	// VALID | SIMPLE (+ transparency); never the complex or delta bits
	DWORD simplicity = MNG_PROFILE_VALID | MNG_PROFILE_SIMPLE;
	if(has_alpha) {
		simplicity |= MNG_PROFILE_TRANSPARENCY;
	}

	// background layers: one first, one per clearing frame
	DWORD layers = (DWORD)count + 1;
	for(size_t i = 1; i < count; i++) {
		if(info->out_frames[i - 1].disposal == GIF_DISPOSAL_BACKGROUND) {
			layers++;
		}
	}

	if(io->write_proc((void*)g_mng_signature, 1, 8, handle) != 8) {
		return FALSE;
	}

	{
		BYTE mhdr[28];
		PutDWORD(&mhdr[0], canvas_width);
		PutDWORD(&mhdr[4], canvas_height);
		PutDWORD(&mhdr[8], 1000);			// a tick is a millisecond
		PutDWORD(&mhdr[12], layers);
		PutDWORD(&mhdr[16], (DWORD)count);
		PutDWORD(&mhdr[20], play_time);
		PutDWORD(&mhdr[24], simplicity);
		if(!WriteChunk(io, handle, CHUNK_MHDR, mhdr, 28)) {
			return FALSE;
		}
	}

	// no TERM means play once
	if(info->out_loop != 1) {
		BYTE term[10];
		term[0] = 3;						// repeat the datastream
		term[1] = 0;						// then show the last frame
		PutDWORD(&term[2], 0);				// no delay before repeating
		PutDWORD(&term[6], (info->out_loop <= 0) ? MNG_INFINITE_ITERATIONS
												 : (DWORD)info->out_loop);
		if(!WriteChunk(io, handle, CHUNK_TERM, term, 10)) {
			return FALSE;
		}
	}

	if(info->out_has_background) {
		BYTE back[7];
		back[0] = info->out_background.rgbRed;   back[1] = info->out_background.rgbRed;
		back[2] = info->out_background.rgbGreen; back[3] = info->out_background.rgbGreen;
		back[4] = info->out_background.rgbBlue;  back[5] = info->out_background.rgbBlue;
		back[6] = 1;						// the colour is mandatory
		if(!WriteChunk(io, handle, CHUNK_BACK, back, 7)) {
			return FALSE;
		}
	}

	// DEFI only when the position changes (default: the origin)
	LONG placed_x = 0, placed_y = 0;

	for(size_t i = 0; i < count; i++) {
		const MNGOutFrame& frame = info->out_frames[i];

		const BYTE framing_mode =
			((i > 0) && (info->out_frames[i - 1].disposal == GIF_DISPOSAL_BACKGROUND)) ? 3 : 1;

		{
			BYTE fram[10];
			fram[0] = framing_mode;
			fram[1] = 0;					// the subframe is nameless: just the separator
			fram[2] = 2;					// change the delay, and the default with it
			fram[3] = 0;					// no change to the timeout
			fram[4] = 0;					// no change to the clipping boundaries
			fram[5] = 0;					// no change to the sync id list
			PutDWORD(&fram[6], frame.delay_ms);
			if(!WriteChunk(io, handle, CHUNK_FRAM, fram, 10)) {
				return FALSE;
			}
		}

		if((frame.x != placed_x) || (frame.y != placed_y)) {
			BYTE defi[12];
			PutWORD(&defi[0], 0);			// object 0: not kept after it is drawn
			defi[2] = 0;					// potentially visible
			defi[3] = 0;					// abstract: nothing deltas against it
			PutDWORD(&defi[4], (DWORD)frame.x);
			PutDWORD(&defi[8], (DWORD)frame.y);
			if(!WriteChunk(io, handle, CHUNK_DEFI, defi, 12)) {
				return FALSE;
			}
			placed_x = frame.x;
			placed_y = frame.y;
		}

		const DWORD size = (DWORD)frame.png.size();
		if(io->write_proc((void*)&frame.png[0], 1, size, handle) != size) {
			return FALSE;
		}
	}

	return WriteChunk(io, handle, CHUNK_MEND, NULL, 0);
}

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "MNG";
}

static const char * DLL_CALLCONV
Description() {
	return "Multiple-image Network Graphics";
}

static const char * DLL_CALLCONV
Extension() {
	return "mng";
}

static const char * DLL_CALLCONV
RegExpr() {
	return NULL;
}

static const char * DLL_CALLCONV
MimeType() {
	return "video/x-mng";
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	BYTE signature[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	io->read_proc(&signature, 1, MNG_SIGNATURE_SIZE, handle);

	return (memcmp(g_mng_signature, signature, MNG_SIGNATURE_SIZE) == 0) ? TRUE : FALSE;
}

// same as PluginPNG.cpp: frames are PNG-encoded
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
	MNGinfo *info = new(std::nothrow) MNGinfo;
	if(!info) {
		return NULL;
	}

	if(!read) {
		// writing: pages arrive in Save(), Close() builds the file
		info->read = FALSE;
		return info;
	}

	info->read = TRUE;

	// the MNG need not start at byte 0 of the stream
	const INT64 start = io->tell_proc(handle);
	if(!Validate(io, handle)) {
		delete info;
		return NULL;
	}

	if(!ParseStream(io, handle, start, info)) {
		delete info;
		return NULL;
	}

	return info;
}

static void DLL_CALLCONV
Close(FreeImageIO *io, fi_handle handle, void *data) {
	MNGinfo *info = (MNGinfo*)data;
	if(!info) {
		return;
	}

	// MHDR needs the final canvas, so the file is written here
	if(!info->read && !info->out_frames.empty()) {
		try {
			if(!WriteMNG(io, handle, info)) {
				FreeImage_OutputMessageProc(s_format_id, "Failed to write the output file");
			}
		} catch(std::bad_alloc&) {
			FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_MEMORY);
		}
	}

	delete info;
}

static int DLL_CALLCONV
PageCount(FreeImageIO *io, fi_handle handle, void *data) {
	MNGinfo *info = (MNGinfo*)data;
	return info ? (int)info->frames.size() : 0;
}

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	MNGinfo *info = (MNGinfo*)data;
	if(!info || info->frames.empty()) {
		return NULL;
	}
	// page -1 (FreeImage_Load): the first frame
	if(page < 0) {
		page = 0;
	}
	if(page >= (int)info->frames.size()) {
		return NULL;
	}

	const BOOL playback = (flags & MNG_PLAYBACK) == MNG_PLAYBACK;

	FIBITMAP *dib = playback ? RenderFrame(io, handle, info, page, flags)
							 : DecodeFrame(io, handle, info, page, flags);
	if(!dib) {
		return NULL;
	}

	SetFrameMetadata(dib, info, page);

	if(info->has_background) {
		FreeImage_SetBackgroundColor(dib, &info->background);
	}

	return dib;
}


static BOOL DLL_CALLCONV
Save(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	MNGinfo *info = (MNGinfo*)data;
	if(!info || !dib || info->read) {
		return FALSE;
	}

	// refuse here: Close() returns void
	{
		const FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib);
		if(!FreeImage_HasPixels(dib) || !SupportsExportType(image_type) ||
		   ((image_type == FIT_BITMAP) && !SupportsExportDepth(FreeImage_GetBPP(dib)))) {
			FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_UNSUPPORTED_FORMAT);
			return FALSE;
		}
	}

	try {
		std::vector<BYTE> png;

		if(!EncodeFrame(dib, flags, png)) {
			FreeImage_OutputMessageProc(s_format_id, "Failed to compress a frame");
			return FALSE;
		}

		if(info->out_frames.size() >= MNG_MAX_FRAMES) {
			FreeImage_OutputMessageProc(s_format_id,
				"MNG: refusing to write more than %d frames", MNG_MAX_FRAMES);
			return FALSE;
		}

		const BOOL is_first = info->out_frames.empty();

		info->out_frames.push_back(MNGOutFrame());
		MNGOutFrame& frame = info->out_frames.back();
		frame.png.swap(png);

		frame.width = FreeImage_GetWidth(dib);
		frame.height = FreeImage_GetHeight(dib);
		frame.has_alpha = (FreeImage_GetBPP(dib) == 32) || FreeImage_IsTransparent(dib);

		// default for untagged pages, as in PluginAPNG.cpp
		frame.delay_ms = 100;

		FITAG *tag = NULL;
		if(GetAnimTag(dib, "FrameTime", FIDT_LONG, &tag)) {
			const LONG delay = *(LONG*)FreeImage_GetTagValue(tag);
			frame.delay_ms = (delay > 0) ? (DWORD)delay : 0;
		}
		if(GetAnimTag(dib, "FrameLeft", FIDT_SHORT, &tag)) {
			frame.x = *(WORD*)FreeImage_GetTagValue(tag);
		}
		if(GetAnimTag(dib, "FrameTop", FIDT_SHORT, &tag)) {
			frame.y = *(WORD*)FreeImage_GetTagValue(tag);
		}
		if(GetAnimTag(dib, "DisposalMethod", FIDT_BYTE, &tag)) {
			// GIF_DISPOSAL_PREVIOUS is written as LEAVE
			const BYTE disposal = *(BYTE*)FreeImage_GetTagValue(tag);
			frame.disposal = (disposal == GIF_DISPOSAL_BACKGROUND) ? GIF_DISPOSAL_BACKGROUND
																   : GIF_DISPOSAL_LEAVE;
		}

		// the first page sets canvas and loop count
		if(is_first) {
			info->out_flags = flags;

			if(GetAnimTag(dib, "LogicalWidth", FIDT_SHORT, &tag)) {
				info->out_canvas_width = *(WORD*)FreeImage_GetTagValue(tag);
			}
			if(GetAnimTag(dib, "LogicalHeight", FIDT_SHORT, &tag)) {
				info->out_canvas_height = *(WORD*)FreeImage_GetTagValue(tag);
			}
			if(GetAnimTag(dib, "Loop", FIDT_LONG, &tag)) {
				const LONG loop = *(LONG*)FreeImage_GetTagValue(tag);
				info->out_loop = (loop > 0) ? loop : 0;
			}

			RGBQUAD background;
			if(FreeImage_GetBackgroundColor(dib, &background)) {
				info->out_background = background;
				info->out_has_background = TRUE;
			}
		}

		return TRUE;

	} catch(std::bad_alloc&) {
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_MEMORY);
		return FALSE;
	}
}


// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitMNG(Plugin *plugin, int format_id) {
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
