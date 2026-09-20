// ==========================================================
// MNG loader
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

/**
A MNG is a container: a header, some chunks that say when and where things are
drawn, and one or more complete PNG or JNG datastreams embedded whole, minus
their signatures.  The pixels are therefore already handled - by PluginPNG.cpp
and PluginJNG.cpp - and what this plugin has to do is the container.

So it does the same thing PluginAPNG.cpp does for animated PNG.  Open() walks
the stream once and builds an index: for every embedded image, where its bytes
start, how many there are, and the animation state in force when it is drawn.
Load(page) turns one of those byte ranges back into a standalone PNG or JNG in
memory - signature, the global chunks the image inherits, its own chunks - and
hands it to FIF_PNG or FIF_JNG.  Every colour type, bit depth, palette,
transparency chunk and ICC profile those two already understand therefore works
here for free, and nothing in MNGHelper.cpp had to change: PluginJNG.cpp still
reaches it by exactly the route it always did.

One page per embedded image, as GIF, APNG and WebP do.  MNG_PLAYBACK = 2 asks
for the canvas a viewer would show at that frame instead of the rectangle the
file stores, mirroring GIF_PLAYBACK and APNG_PLAYBACK down to the tag names.
Each frame carries FrameTime (milliseconds), FrameLeft, FrameTop,
DisposalMethod and BlendMethod, and every page also carries LogicalWidth,
LogicalHeight and Loop.

What is covered.  MNG-VLC and MNG-LC in full: MHDR, the embedded PNG/JNG/BASI
images, global PLTE and tRNS and the other global ancillary chunks, FRAM with
its framing modes and its interframe delays, DEFI placement and clipping, BACK,
LOOP/ENDL, TERM, SHOW, MEND.

What is not.  The delta images of full MNG - DHDR and the object-buffer chunks
that go with it (PAST, MAGN, CLON, DISC, MOVE, CLIP applied to stored objects) -
are parsed well enough to be skipped and counted, never rendered.  Rendering
them means keeping every object buffer alive and replaying arbitrary edits onto
it, which is a different program from this one.  A file that uses them, or that
declares them in the MHDR simplicity profile, says so once through
FreeImage_OutputMessageProc and then reads as the images it does contain.
That is the whole difference between this and libmng, and it is a deliberate
one: silently returning a delta frame as though it were a whole picture would
be worse than saying it cannot be done.

References
  http://www.libpng.org/pub/mng/spec/    MNG 1.0, and the chunk layouts below
  http://www.libpng.org/pub/mng/spec/jng.html
  http://www.w3.org/TR/PNG/
*/

#include "FreeImage.h"
#include "Utilities.h"
#include "Plugin.h"

#include "../Metadata/FreeImageTag.h"

#include <new>
#include <vector>

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ----------------------------------------------------------

#define MNG_SIGNATURE_SIZE 8	// size of the signature

/** PNG signature, prefixed to an embedded IHDR..IEND to make it a file again */
static const BYTE g_png_signature[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
/** JNG signature, likewise for an embedded JHDR..IEND */
static const BYTE g_jng_signature[8] = { 139, 74, 78, 71, 13, 10, 26, 10 };

/** The disposal methods the animation metadata is written in, which are GIF's.
PluginGIF.cpp and PluginAPNG.cpp spell them out the same way. */
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

/** A chunk type as a single comparable number. */
#define MNG_CHUNK(a, b, c, d) \
	((DWORD)(((DWORD)(a) << 24) | ((DWORD)(b) << 16) | ((DWORD)(c) << 8) | (DWORD)(d)))

static const DWORD CHUNK_MHDR = MNG_CHUNK('M', 'H', 'D', 'R');
static const DWORD CHUNK_MEND = MNG_CHUNK('M', 'E', 'N', 'D');
static const DWORD CHUNK_IHDR = MNG_CHUNK('I', 'H', 'D', 'R');
static const DWORD CHUNK_IEND = MNG_CHUNK('I', 'E', 'N', 'D');
static const DWORD CHUNK_IDAT = MNG_CHUNK('I', 'D', 'A', 'T');
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

/** A chunk header is 4 bytes of length plus 4 of type; a chunk also has a
4-byte CRC after its data. */
#define MNG_CHUNK_OVERHEAD	12

/** No sane MNG-level chunk is anywhere near this big, and refusing to allocate
for a bogus length is cheaper than discovering it later. */
#define MNG_MAX_CHUNK_PAYLOAD	(64u * 1024u * 1024u)

/** The spec caps iteration counts at 2^31-1, which means "forever". */
#define MNG_INFINITE_ITERATIONS	0x7FFFFFFF

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
PutDWORD(BYTE *p, DWORD value) {
	p[0] = (BYTE)(value >> 24);
	p[1] = (BYTE)(value >> 16);
	p[2] = (BYTE)(value >> 8);
	p[3] = (BYTE)(value);
}

// ==========================================================
// The index Open() builds
// ==========================================================

/**
The global chunks in force at a point in the stream.  MNG lets these be
redefined between images, so an image inherits whatever was last declared
before it rather than whatever the file ends with - which is why a frame
records which snapshot it belongs to instead of pointing at one global copy.
*/
struct MNGGlobals {
	std::vector<BYTE> plte;		//! payload only, without length/type/CRC
	std::vector<BYTE> trns;
	std::vector<BYTE> gama;
	std::vector<BYTE> chrm;
	std::vector<BYTE> srgb;
	std::vector<BYTE> iccp;
	std::vector<BYTE> phys;
};

/**
One embedded image: where its bytes are, and everything the container says
about how it is drawn.
*/
struct MNGFrame {
	long offset;			//! file offset of its first chunk (IHDR, JHDR or BASI)
	DWORD length;			//! bytes from there through the CRC of its IEND
	BOOL is_jng;			//! JHDR..IEND rather than IHDR..IEND
	BOOL is_basi;			//! BASI..IEND, whose first 13 payload bytes are an IHDR
	DWORD width, height;

	DWORD delay_ticks;		//! interframe delay in force, in MHDR ticks
	LONG x, y;				//! where DEFI puts it on the canvas
	BOOL do_not_show;		//! DEFI said it is not potentially visible
	BOOL has_clip;
	LONG clip_left, clip_right, clip_top, clip_bottom;

	BYTE framing_mode;		//! the FRAM framing mode in force
	int subframe;			//! which run between FRAM chunks it belongs to
	BOOL restore_background;//! a background layer is drawn immediately before it

	size_t globals;			//! index into MNGinfo::globals

	MNGFrame() : offset(0), length(0), is_jng(FALSE), is_basi(FALSE), width(0), height(0),
		delay_ticks(0), x(0), y(0), do_not_show(FALSE), has_clip(FALSE),
		clip_left(0), clip_right(0), clip_top(0), clip_bottom(0),
		framing_mode(1), subframe(0), restore_background(FALSE), globals(0) {
	}
};

/**
What Open() hands to the other entry points.
*/
struct MNGinfo {
	BOOL read;

	BOOL has_mhdr;
	DWORD canvas_width, canvas_height;
	DWORD ticks_per_second;
	DWORD nominal_layer_count, nominal_frame_count, nominal_play_time;
	DWORD simplicity;

	LONG loop_count;			//! plays: 1 is once, 0 is forever, as GIF and APNG spell it
	BOOL has_background;
	RGBQUAD background;

	std::vector<MNGGlobals> globals;
	std::vector<MNGFrame> frames;

	BOOL complex_features;		//! a delta image or an object-buffer chunk was met
	BOOL warned;				//! the message about them has been issued once

	// MNG_PLAYBACK cache, the same shape as the one in PluginAPNG.cpp: compositing a
	// frame needs the canvas the frame before it left behind, so keeping that canvas
	// turns walking an animation in order into one frame of work per frame instead of
	// replaying it from the start each time.
	FIBITMAP *canvas;
	int canvas_page;			//! the frame `canvas` shows, -1 when there is none

	MNGinfo() : read(FALSE), has_mhdr(FALSE), canvas_width(0), canvas_height(0),
		ticks_per_second(0), nominal_layer_count(0), nominal_frame_count(0),
		nominal_play_time(0), simplicity(0), loop_count(1), has_background(FALSE),
		complex_features(FALSE), warned(FALSE), canvas(NULL), canvas_page(-1) {
		background.rgbRed = background.rgbGreen = background.rgbBlue = 0;
		background.rgbReserved = 255;
	}

	~MNGinfo() {
		if(canvas) {
			FreeImage_Unload(canvas);
		}
	}
};

/**
Say once, and only once, that this file uses something no one here renders.
*/
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

/**
The length of the stream, from the current position restored afterwards.
*/
static long
MNG_GetFileLength(FreeImageIO *io, fi_handle handle) {
	const long start_pos = io->tell_proc(handle);
	io->seek_proc(handle, 0, SEEK_END);
	const long file_length = io->tell_proc(handle);
	io->seek_proc(handle, start_pos, SEEK_SET);
	return file_length;
}

/**
Read a chunk's length and type at the current position.
@return TRUE if 8 bytes were there to read
*/
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

/**
Measure an embedded datastream: from the first byte of its IHDR, JHDR, BASI or
DHDR chunk through the CRC of the IEND that closes it.  The stream position is
left at the byte after that IEND.
@return TRUE if a complete datastream was found
*/
static BOOL
ScanEmbeddedStream(FreeImageIO *io, fi_handle handle, long start, long file_length, DWORD *out_length) {
	io->seek_proc(handle, start, SEEK_SET);

	while(TRUE) {
		const long pos = io->tell_proc(handle);
		if((pos < 0) || (pos + MNG_CHUNK_OVERHEAD > file_length)) {
			return FALSE;
		}

		DWORD length = 0, type = 0;
		if(!ReadChunkHeader(io, handle, &length, &type)) {
			return FALSE;
		}
		// the 4-byte CRC follows the payload
		if((length > (DWORD)file_length) || (pos + 8 + (long)length + 4 > file_length)) {
			return FALSE;
		}
		io->seek_proc(handle, (long)length + 4, SEEK_CUR);

		if(type == CHUNK_IEND) {
			const long end = io->tell_proc(handle);
			if(end <= start) {
				return FALSE;
			}
			*out_length = (DWORD)(end - start);
			return TRUE;
		}
	}
}

/**
Read `length` bytes at `offset`.
*/
static BOOL
ReadBytesAt(FreeImageIO *io, fi_handle handle, long offset, DWORD length, std::vector<BYTE>& out) {
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

/**
The FRAM parameters in force while the stream is walked.  FRAM is a state
machine, not a per-frame record: a chunk can change a parameter for the
upcoming subframe only, or change it and reset the default with it.
*/
struct MNGFramingState {
	BYTE framing_mode;
	DWORD default_delay;	//! ticks, and 1 tick is the spec's default rate
	DWORD current_delay;	//! what the upcoming subframe uses
	BOOL has_clip;
	LONG clip_left, clip_right, clip_top, clip_bottom;

	MNGFramingState() : framing_mode(1), default_delay(1), current_delay(1),
		has_clip(FALSE), clip_left(0), clip_right(0), clip_top(0), clip_bottom(0) {
	}
};

/**
Parse a FRAM chunk (MNG 1.0, 4.3.2).

Everything after the framing mode is optional, and the optional parts are
omitted as a group, so the chunk has to be read by what is left rather than by
fixed offsets:

	framing_mode      1 byte
	subframe_name     0..n bytes of Latin-1, then
	separator         1 NUL - both omitted together with everything after them
	change_interframe_delay           1 byte   0 no, 1 this subframe, 2 also the default
	change_timeout_and_termination    1 byte   0..8
	change_layer_clipping_boundaries  1 byte   0 no, 1 this subframe, 2 also the default
	change_sync_id_list               1 byte
	interframe_delay  4 bytes, present only if change_interframe_delay is nonzero
	timeout           4 bytes, present only if change_timeout is nonzero
	clipping          1 byte delta type then 4 signed longs, if change_clipping is nonzero
	sync ids          4 bytes each, to the end of the chunk
*/
static void
ParseFRAM(const BYTE *payload, DWORD length, MNGFramingState *state) {
	if(length == 0) {
		// "An empty FRAM chunk is just a subframe delimiter": it changes nothing,
		// but the one-shot parameters of the subframe it closes do not survive it.
		state->current_delay = state->default_delay;
		return;
	}

	const BYTE framing_mode = payload[0];
	if((framing_mode >= 1) && (framing_mode <= 4)) {
		state->framing_mode = framing_mode;
	}

	DWORD pos = 1;

	// the subframe name, if any, up to its NUL separator
	if(pos < length) {
		DWORD separator = pos;
		while((separator < length) && (payload[separator] != 0)) {
			separator++;
		}
		// no separator means the name ran to the end and nothing follows it
		pos = (separator < length) ? separator + 1 : length;
	}

	// with no change bytes, the defaults come back for the upcoming subframe
	if(pos + 4 > length) {
		state->current_delay = state->default_delay;
		return;
	}

	const BYTE change_delay = payload[pos];
	const BYTE change_timeout = payload[pos + 1];
	const BYTE change_clipping = payload[pos + 2];
	const BYTE change_sync = payload[pos + 3];
	pos += 4;

	// A delay the chunk promised but did not carry leaves the default alone -
	// the alternative is to invent a number for it.
	DWORD delay = state->default_delay;
	BOOL got_delay = FALSE;
	if(change_delay && (pos + 4 <= length)) {
		delay = GetDWORD(&payload[pos]);
		pos += 4;
		got_delay = TRUE;
	}
	if(change_timeout && (pos + 4 <= length)) {
		// the timeout says how long to wait for the event that ends a
		// non-deterministic subframe; nothing here waits for events
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
			// "determined by adding the FRAM data to the values from the
			// previous subframe"
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
	(void)change_sync;	// the sync id list only matters to a decoder that waits on one

	if(got_delay) {
		state->current_delay = delay;
		if(change_delay == 2) {
			state->default_delay = delay;
		}
	} else {
		state->current_delay = state->default_delay;
	}
}

/**
The DEFI placement in force while the stream is walked.
*/
struct MNGObjectState {
	LONG x, y;
	BOOL do_not_show;
	BOOL has_clip;
	LONG clip_left, clip_right, clip_top, clip_bottom;

	MNGObjectState() : x(0), y(0), do_not_show(FALSE), has_clip(FALSE),
		clip_left(0), clip_right(0), clip_top(0), clip_bottom(0) {
	}
};

/**
Parse a DEFI chunk (MNG 1.0, 4.2.1), which is 2, 3, 4, 12 or 28 bytes: an
object id, then do_not_show, then the concrete flag, then a location, then
clipping boundaries.  "If any field is omitted, all subsequent fields must
also be omitted" - and an omitted field is not a field set to zero, so a
2-byte DEFI leaves the location where the last one put it.
*/
static void
ParseDEFI(const BYTE *payload, DWORD length, MNGObjectState *state) {
	if(length < 2) {
		return;
	}
	if(length >= 3) {
		state->do_not_show = (payload[2] != 0) ? TRUE : FALSE;
	}
	// payload[3] is the concrete flag, which only matters to a delta image
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

/**
Parse a BACK chunk (MNG 1.0, 4.3.1): 6, 7, 9 or 10 bytes of 16-bit RGB, a
mandatory flag, and a background image id this plugin has no object buffers
to look up.
*/
static void
ParseBACK(const BYTE *payload, DWORD length, MNGinfo *info) {
	if(length < 6) {
		return;
	}
	// the samples are 16 bit; FreeImage's background colour is 8
	info->background.rgbRed = (BYTE)(GetWORD(&payload[0]) >> 8);
	info->background.rgbGreen = (BYTE)(GetWORD(&payload[2]) >> 8);
	info->background.rgbBlue = (BYTE)(GetWORD(&payload[4]) >> 8);
	info->background.rgbReserved = 255;
	info->has_background = TRUE;
}

/**
A LOOP whose iteration_count is zero runs its body no times at all: "Upon
encountering a LOOP chunk whose iteration_count is zero, decoders simply skip
chunks until the matching ENDL chunk is found, and resume processing with the
chunk immediately following it".  The images inside such a loop are therefore
not part of the animation, and not pages either.

The stream is positioned just after the LOOP chunk on entry, and just after its
ENDL on success.  Loops nest, so this counts rather than matches: the spec says
the nest level "should be used as a sanity check but is not required", and a
count cannot be fooled by a file that numbers its levels oddly.
*/
static BOOL
SkipToMatchingENDL(FreeImageIO *io, fi_handle handle, long file_length) {
	int depth = 1;

	while(depth > 0) {
		const long chunk_start = io->tell_proc(handle);
		if((chunk_start < 0) || (chunk_start + MNG_CHUNK_OVERHEAD > file_length)) {
			return FALSE;
		}
		DWORD length = 0, type = 0;
		if(!ReadChunkHeader(io, handle, &length, &type)) {
			return FALSE;
		}
		const long payload_start = chunk_start + 8;
		if((length > (DWORD)file_length) || (payload_start + (long)length + 4 > file_length)) {
			return FALSE;
		}

		if(type == CHUNK_LOOP) {
			depth++;
		} else if(type == CHUNK_ENDL) {
			depth--;
		} else if(type == CHUNK_MEND) {
			// the loop is never closed; stop here rather than run off the end,
			// and leave MEND to be read by the caller
			io->seek_proc(handle, chunk_start, SEEK_SET);
			return TRUE;
		}

		io->seek_proc(handle, payload_start + (long)length + 4, SEEK_SET);
	}

	return TRUE;
}

/**
Framing modes 2 and 4 associate the interframe delay "only with the final layer
in the subframe.  A zero interframe delay is associated with the other layers",
so the delay recorded against every layer while walking the stream has to come
back off all but the last of each subframe.  Modes 1 and 3 associate it with
every foreground layer and are already right.
*/
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

// ==========================================================
// Building the index
// ==========================================================

/**
Walk the whole stream once and record every embedded image with the animation
state in force when it is drawn.
*/
static BOOL
ParseStream(FreeImageIO *io, fi_handle handle, MNGinfo *info) {
	const long file_length = MNG_GetFileLength(io, handle);
	if(file_length <= MNG_SIGNATURE_SIZE) {
		return FALSE;
	}
	io->seek_proc(handle, MNG_SIGNATURE_SIZE, SEEK_SET);

	MNGFramingState framing;
	MNGObjectState object;
	MNGGlobals globals;

	// `globals` is only copied into the list when an image actually needs it, so a
	// file that redefines its palette between every frame costs one snapshot per
	// distinct state and a file that never redefines it costs exactly one.
	size_t globals_index = (size_t)-1;

	int subframe = 0;
	BOOL first_image = TRUE;
	BOOL seen_mend = FALSE;

	// LOOP/ENDL.  The images are not repeated in the file and are not repeated
	// here either: the iteration count of a loop that encloses the whole sequence
	// becomes the animation's loop count instead.  The spec expects exactly this
	// of "MNG editors that extract a series of PNG or JNG files".
	int loop_depth = 0;
	BOOL have_outer_loop = FALSE;
	DWORD outer_loop_count = 1;
	size_t outer_loop_first = 0, outer_loop_last = 0;

	BOOL have_term = FALSE;
	DWORD term_iterations = 1;

	std::vector<BYTE> payload;

	while(!seen_mend) {
		const long chunk_start = io->tell_proc(handle);
		if((chunk_start < 0) || (chunk_start + MNG_CHUNK_OVERHEAD > file_length)) {
			break;
		}

		DWORD length = 0, type = 0;
		if(!ReadChunkHeader(io, handle, &length, &type)) {
			break;
		}
		const long payload_start = chunk_start + 8;
		if((length > (DWORD)file_length) || (payload_start + (long)length + 4 > file_length)) {
			FreeImage_OutputMessageProc(s_format_id,
				"MNG: a chunk claims %u bytes, which runs past the end of the file", length);
			break;
		}
		const long next_chunk = payload_start + (long)length + 4;

		// An embedded datastream is measured and stepped over whole; its own chunks
		// are none of this loop's business, which is also what keeps a PLTE inside an
		// image from being mistaken for a global one.
		if((type == CHUNK_IHDR) || (type == CHUNK_JHDR) || (type == CHUNK_BASI) || (type == CHUNK_DHDR)) {
			DWORD stream_length = 0;
			if(!ScanEmbeddedStream(io, handle, chunk_start, file_length, &stream_length)) {
				FreeImage_OutputMessageProc(s_format_id,
					"MNG: an embedded image has no IEND - the file ends inside it");
				break;
			}

			if(type == CHUNK_DHDR) {
				// a delta image: counted and skipped, never rendered
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
					// filter method 64 is MNG's intrapixel differencing, which libpng
					// does not implement
					if(!frame.is_jng && (length >= 13) && (payload[11] == 64)) {
						info->complex_features = TRUE;
						WarnComplex(info);
					}
				}

				frame.delay_ticks = framing.current_delay;
				frame.x = object.x;
				frame.y = object.y;
				frame.do_not_show = object.do_not_show;
				frame.framing_mode = framing.framing_mode;
				frame.subframe = subframe;

				// Whose clipping wins: DEFI's is the object's own, FRAM's is the
				// layer's, and the layer's is the one a viewer applies.
				if(framing.has_clip) {
					frame.has_clip = TRUE;
					frame.clip_left = framing.clip_left;
					frame.clip_right = framing.clip_right;
					frame.clip_top = framing.clip_top;
					frame.clip_bottom = framing.clip_bottom;
				} else if(object.has_clip) {
					frame.has_clip = TRUE;
					frame.clip_left = object.clip_left;
					frame.clip_right = object.clip_right;
					frame.clip_top = object.clip_top;
					frame.clip_bottom = object.clip_bottom;
				}

				// "Regardless of the framing mode, encoders must insert a background
				// layer ... ahead of the first image layer in the datastream", and
				// modes 3 and 4 insert more of them.
				if(first_image) {
					frame.restore_background = TRUE;
				} else if(framing.framing_mode == 3) {
					frame.restore_background = TRUE;
				} else if(framing.framing_mode == 4) {
					frame.restore_background = info->frames.empty() ? TRUE :
						(info->frames.back().subframe != subframe) ? TRUE : FALSE;
				}
				first_image = FALSE;

				if(globals_index == (size_t)-1) {
					info->globals.push_back(globals);
					globals_index = info->globals.size() - 1;
				}
				frame.globals = globals_index;

				info->frames.push_back(frame);
				if(have_outer_loop && (loop_depth > 0)) {
					outer_loop_last = info->frames.size();
				}
			}

			io->seek_proc(handle, chunk_start + (long)stream_length, SEEK_SET);
			continue;
		}

		// everything else is a MNG-level chunk whose payload may matter
		const BOOL want_payload =
			(type == CHUNK_MHDR) || (type == CHUNK_FRAM) || (type == CHUNK_DEFI) ||
			(type == CHUNK_BACK) || (type == CHUNK_LOOP) || (type == CHUNK_ENDL) ||
			(type == CHUNK_TERM) || (type == CHUNK_PLTE) || (type == CHUNK_tRNS) ||
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

				// The profile is only meaningful when it says it is; bit 0 off means
				// "the absence of any features is unspecified", not "none are used".
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
			ParseDEFI(data, length, &object);
		} else if(type == CHUNK_BACK) {
			ParseBACK(data, length, info);
		} else if(type == CHUNK_TERM) {
			// TERM 3 repeats the whole datastream; anything else is a single pass
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
				// data[0] is the nest level, which the depth count below tracks
				// for itself
				iterations = GetDWORD(&data[1]);
			}
			if(iterations == 0) {
				// "Upon encountering a LOOP chunk whose iteration_count is zero,
				// decoders simply skip chunks until the matching ENDL chunk is
				// found": the images inside are not part of the animation.
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
			// SEEK restores the state saved at the last SAVE; with no object buffers
			// to restore, resetting the placement is the part that matters here
			object = MNGObjectState();
		} else if((type == CHUNK_PAST) || (type == CHUNK_MAGN) || (type == CHUNK_CLON) ||
				  (type == CHUNK_DISC) || (type == CHUNK_MOVE) || (type == CHUNK_CLIP)) {
			// object-buffer chunks: they edit images this plugin does not keep
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

	// How many times the whole thing plays.  TERM describes the datastream and so
	// wins over a LOOP, which might only enclose part of it.
	if(have_term) {
		info->loop_count = (term_iterations >= MNG_INFINITE_ITERATIONS) ? 0 : (LONG)term_iterations;
	} else if(have_outer_loop && (outer_loop_first == 0) && (outer_loop_last == info->frames.size())
			  && !info->frames.empty()) {
		info->loop_count = (outer_loop_count >= MNG_INFINITE_ITERATIONS) ? 0 : (LONG)outer_loop_count;
	}

	ApplyFramingModes(info);

	if(!info->has_mhdr) {
		// a MNG with no MHDR is malformed, but if it held images they are still
		// images; give them a canvas big enough to hold them
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

/**
Append a chunk: length, type, payload, CRC.
*/
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

/**
One chunk of an embedded datastream.
*/
struct MNGChunkRef {
	DWORD type;
	const BYTE *payload;
	DWORD length;
	const BYTE *raw;
	DWORD raw_length;
};

/**
Split an embedded datastream into its chunks.
*/
static BOOL
SplitChunks(const std::vector<BYTE>& stream, std::vector<MNGChunkRef>& out) {
	size_t pos = 0;
	while(pos + MNG_CHUNK_OVERHEAD <= stream.size()) {
		MNGChunkRef ref;
		ref.length = GetDWORD(&stream[pos]);
		ref.type = GetDWORD(&stream[pos + 4]);
		if((ref.length > stream.size()) || (pos + 12 + (size_t)ref.length > stream.size())) {
			return FALSE;
		}
		ref.payload = (ref.length > 0) ? &stream[pos + 8] : NULL;
		ref.raw = &stream[pos];
		ref.raw_length = ref.length + MNG_CHUNK_OVERHEAD;
		out.push_back(ref);
		pos += ref.raw_length;
	}
	return !out.empty();
}

/**
Rebuild a standalone PNG from an embedded IHDR..IEND or BASI..IEND, splicing in
the global chunks the image inherits.

The only substitution the MNG spec asks for is the palette: an embedded image
whose own PLTE is empty uses the global one, and one that carries a real
palette keeps it.  The rest of the global chunks - gAMA, cHRM, sRGB, iCCP,
pHYs, tRNS - are added only where the image has none of its own, and at a point
in the stream where PNG allows them.
*/
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

	// BASI's first 13 payload bytes are an IHDR; the samples that follow only
	// matter when there is no IDAT, which is handled by the caller.
	AppendChunk(out, CHUNK_IHDR, chunks[0].payload, 13);

	// These must all precede PLTE, and the image's own copy always wins.
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
				// the empty PLTE that means "use the global palette"
				AppendGlobalChunk(out, CHUNK_PLTE, globals.plte);
			} else {
				out.insert(out.end(), chunk.raw, chunk.raw + chunk.raw_length);
			}
			wrote_plte = TRUE;
			continue;
		}

		if((chunk.type == CHUNK_IDAT) && !wrote_pre_idat) {
			wrote_pre_idat = TRUE;
			// A palette image that carries no PLTE at all still inherits the
			// global one, and tRNS and pHYs have to land before the image data.
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

	AppendChunk(out, CHUNK_IEND, NULL, 0);
	return TRUE;
}

/**
Rebuild a standalone JNG from an embedded JHDR..IEND.  A JNG carries everything
it needs, so this is the signature and the chunks as they stand - and handing
it to FIF_JNG is what makes the JPEG-plus-alpha path in MNGHelper.cpp do the
work, exactly as it does for a .jng file.
*/
static BOOL
BuildJNGStream(const std::vector<BYTE>& raw, std::vector<BYTE>& out) {
	if(raw.size() < MNG_CHUNK_OVERHEAD) {
		return FALSE;
	}
	out.insert(out.end(), g_jng_signature, g_jng_signature + 8);
	out.insert(out.end(), raw.begin(), raw.end());
	return TRUE;
}

/**
Is there any IDAT in this datastream?  A BASI without one is a solid colour.
*/
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

/**
A BASI with no IDAT: "sixteen-bit {red, green, blue, alpha} values that are
used to fill the entire basis object when the IDAT chunk is not present".
*/
static FIBITMAP *
CreateBASIFill(const std::vector<BYTE>& raw, const MNGFrame& frame, int flags) {
	std::vector<MNGChunkRef> chunks;
	if(!SplitChunks(raw, chunks) || (chunks[0].length < 13)) {
		return NULL;
	}
	const BYTE *payload = chunks[0].payload;
	const DWORD length = chunks[0].length;

	// "If the color samples are omitted, zeroes will be used", and an omitted
	// alpha means opaque.
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

/**
Decode one page as the file stores it.
*/
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

	FIMEMORY *hmem = FreeImage_OpenMemory(&stream[0], (DWORD)stream.size());
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

/**
Fill the whole canvas with the background: the BACK colour when the file gives
one, and otherwise transparent, so that an application can put its own scene
behind a MNG whose images do not cover the frame.
*/
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

/**
Draw a 32-bit frame onto the 32-bit canvas at (x, y), compositing it over what
is already there the way MNG says a layer is composited, and clipped to the
layer clipping boundaries when the file sets any.

FreeImage stores the bottom line first, so a MNG y - which counts down from the
top - is turned round here rather than everywhere else.
*/
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
		// "The left and top boundaries are inclusive, while the right and bottom
		// boundaries are exclusive."
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

/**
Play the animation up to `page` and return the canvas as it then looks.
*/
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

	// Walking the animation in order costs one frame of work per frame; jumping
	// backwards, or to a canvas of a different size, starts again from the top.
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

/**
Turn an interframe delay in MHDR ticks into milliseconds.

"When this field is zero, the length of a tick is infinite, and decoders will
ignore any attempt to define interframe delay" - a file with one frame is
supposed to say ticks_per_second = 0, and it gets no delay at all.
*/
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

	// GIF's numbering, which is what FIMD_ANIMATION is written in. A background
	// layer drawn ahead of the *next* frame is the same thing as this frame
	// disposing to the background.
	BYTE disposal = GIF_DISPOSAL_LEAVE;
	if((page + 1 < (int)info->frames.size()) && info->frames[page + 1].restore_background) {
		disposal = GIF_DISPOSAL_BACKGROUND;
	}
	// a MNG layer is composited over what is beneath it, never replacing it
	BYTE blend = 0;

	SetAnimTag(dib, "FrameTime", ANIMTAG_FRAMETIME, FIDT_LONG, 1, 4, &duration);
	SetAnimTag(dib, "FrameLeft", ANIMTAG_FRAMELEFT, FIDT_SHORT, 1, 2, &left);
	SetAnimTag(dib, "FrameTop", ANIMTAG_FRAMETOP, FIDT_SHORT, 1, 2, &top);
	SetAnimTag(dib, "DisposalMethod", ANIMTAG_DISPOSALMETHOD, FIDT_BYTE, 1, 1, &disposal);
	SetAnimTag(dib, "BlendMethod", ANIMTAG_BLENDMETHOD, FIDT_BYTE, 1, 1, &blend);

	// The canvas and the loop count describe the file rather than any one frame,
	// so every frame is told about them, exactly as PluginAPNG.cpp and
	// PluginGIF.cpp do.
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
	BYTE mng_signature[8] = { 138, 77, 78, 71, 13, 10, 26, 10 };
	BYTE signature[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

	io->read_proc(&signature, 1, MNG_SIGNATURE_SIZE, handle);

	return (memcmp(mng_signature, signature, MNG_SIGNATURE_SIZE) == 0) ? TRUE : FALSE;
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

static void * DLL_CALLCONV
Open(FreeImageIO *io, fi_handle handle, BOOL read) {
	if(!read) {
		// there is no MNG writer: FreeImage_Save() and the save at
		// FreeImage_CloseMultiBitmap() both report that for themselves
		return NULL;
	}

	MNGinfo *info = new(std::nothrow) MNGinfo;
	if(!info) {
		return NULL;
	}
	info->read = TRUE;

	io->seek_proc(handle, 0, SEEK_SET);
	if(!Validate(io, handle)) {
		delete info;
		return NULL;
	}

	if(!ParseStream(io, handle, info)) {
		delete info;
		return NULL;
	}

	return info;
}

static void DLL_CALLCONV
Close(FreeImageIO *io, fi_handle handle, void *data) {
	MNGinfo *info = (MNGinfo*)data;
	if(info) {
		delete info;
	}
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
	// FreeImage_Load() asks for page -1, meaning "the image"; for an animation
	// that is its first frame
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
	plugin->save_proc = NULL;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = SupportsICCProfiles;
	plugin->supports_no_pixels_proc = SupportsNoPixels;
}
