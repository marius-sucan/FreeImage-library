// ==========================================================
// Input/Output functions
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
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

// 64-bit off_t on 32-bit POSIX; must precede every include
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif

#include "FreeImage.h"
#include "Utilities.h"
#include "FreeImageIO.h"

// =====================================================================
// File IO functions
// =====================================================================

int
FreeImage_fseek64(FILE *file, INT64 offset, int origin) {
#ifdef _WIN32
	return _fseeki64(file, offset, origin);
#else
	return fseeko(file, (off_t)offset, origin);
#endif
}

INT64
FreeImage_ftell64(FILE *file) {
#ifdef _WIN32
	return _ftelli64(file);
#else
	return (INT64)ftello(file);
#endif
}

// static: names like _ReadProc must not leak out of the static library

static unsigned DLL_CALLCONV
_ReadProc(void *buffer, unsigned size, unsigned count, fi_handle handle) {
	return (unsigned)fread(buffer, size, count, (FILE *)handle);
}

static unsigned DLL_CALLCONV
_WriteProc(void *buffer, unsigned size, unsigned count, fi_handle handle) {
	return (unsigned)fwrite(buffer, size, count, (FILE *)handle);
}

static int DLL_CALLCONV
_SeekProc(fi_handle handle, INT64 offset, int origin) {
	return FreeImage_fseek64((FILE *)handle, offset, origin);
}

static INT64 DLL_CALLCONV
_TellProc(fi_handle handle) {
	return FreeImage_ftell64((FILE *)handle);
}

// ----------------------------------------------------------

void
SetDefaultIO(FreeImageIO *io) {
	io->read_proc  = _ReadProc;
	io->seek_proc  = _SeekProc;
	io->tell_proc  = _TellProc;
	io->write_proc = _WriteProc;
}

// =====================================================================
// Memory IO functions
// =====================================================================

// the most a memory stream holds: what size_t addresses, halved
static const INT64 FI_MEMORY_MAX = (INT64)(std::numeric_limits<size_t>::max() >> 1);

/**
The _MemoryReadProc function reads up to count items of size bytes from the input stream and stores them in buffer.
_MemoryReadProc returns the number of full items actually read,
which may be less than count if an error occurs or if the end of the file is encountered before reaching count.
If size or count is 0, _MemoryReadProc returns 0 and the buffer contents are unchanged

@param buffer
@param size
@param count
@param handle
@return
*/
unsigned DLL_CALLCONV
_MemoryReadProc(void *buffer, unsigned size, unsigned count, fi_handle handle) {
	if (!handle || !buffer || (size == 0) || (count == 0)) {
		return 0;
	}

	FIMEMORYHEADER *mem_header = (FIMEMORYHEADER*)(((FIMEMORY*)handle)->data);

	const UINT64 required_bytes = (UINT64)size * (UINT64)count;
	const INT64 remaining_bytes = mem_header->file_length - mem_header->current_position;

	if (remaining_bytes > 0) {
		if (required_bytes <= (UINT64)remaining_bytes) {
			// copy size bytes count times
			memcpy(buffer, (char*)mem_header->data + mem_header->current_position, (size_t)required_bytes);
			mem_header->current_position += (INT64)required_bytes;
			return count;
		}
		// if there isn't required_bytes bytes left to read, set pos to eof and return a short count
		memcpy(buffer, (char*)mem_header->data + mem_header->current_position, (size_t)remaining_bytes);
		mem_header->current_position = mem_header->file_length;
		return (unsigned)((UINT64)remaining_bytes / size);
	}

	// if size or count is 0, _MemoryReadProc returns 0 and the buffer contents are unchanged.
	return 0;
}

unsigned DLL_CALLCONV
_MemoryWriteProc(void *buffer, unsigned size, unsigned count, fi_handle handle) {
	if (!handle || !buffer) {
		return 0;
	}

	FIMEMORYHEADER *mem_header = (FIMEMORYHEADER*)(((FIMEMORY*)handle)->data);

	const UINT64 wanted = (UINT64)size * (UINT64)count;

	// a write that cannot end below FI_MEMORY_MAX is refused before any allocation
	if ((wanted >= (UINT64)FI_MEMORY_MAX) || (mem_header->current_position >= FI_MEMORY_MAX - (INT64)wanted)) {
		return 0;
	}

	const INT64 required_bytes = (INT64)wanted;
	const INT64 required_end = mem_header->current_position + required_bytes;

	// double the data block size if we need to
	while( required_end >= mem_header->data_length ) {
		INT64 newdatalen = 0;

		if( mem_header->data_length == 0 ) {
			// default to 4K if nothing yet
			newdatalen = 4096;
		} else if( mem_header->data_length >= FI_MEMORY_MAX / 2 ) {
			// cannot double: stop at the limit
			if( mem_header->data_length >= FI_MEMORY_MAX ) {
				return 0;
			}
			newdatalen = FI_MEMORY_MAX;
		} else {
			// double size
			newdatalen = mem_header->data_length * 2;
		}
		void *newdata = realloc(mem_header->data, (size_t)newdatalen);
		if(!newdata) {
			return 0;
		}
		mem_header->data = newdata;
		mem_header->data_length = newdatalen;
	}

	// a write past the end leaves a gap, which reads as zeros, as in a file
	if( mem_header->current_position > mem_header->file_length ) {
		memset((char *)mem_header->data + mem_header->file_length, 0, (size_t)(mem_header->current_position - mem_header->file_length));
	}

	memcpy((char *)mem_header->data + mem_header->current_position, buffer, (size_t)required_bytes);
	mem_header->current_position += required_bytes;

	if( mem_header->current_position > mem_header->file_length ) {
		mem_header->file_length = mem_header->current_position;
	}

	return count;
}

/**
The _MemorySeekProc function moves the file pointer (if any) associated with stream to a new location that is offset bytes from origin.
The next operation on the stream takes place at the new location. On a stream open for update, the next operation can be either a read or a write.
The argument origin must be one of the following constants, defined in STDIO.H:
	SEEK_CUR	Current position of file pointer.
	SEEK_END	End of file.
	SEEK_SET	Beginning of file.
You can use _MemorySeekProc to reposition the pointer anywhere in a file.
The pointer can also be positioned beyond the end of the file.

@param handle
@param offset
@param origin
@return If successful, returns 0. Otherwise, returns -1.
*/
int DLL_CALLCONV
_MemorySeekProc(fi_handle handle, INT64 offset, int origin) {
	if (!handle) {
		return -1;
	}

	FIMEMORYHEADER *mem_header = (FIMEMORYHEADER*)(((FIMEMORY*)handle)->data);

	INT64 base = 0;

	switch (origin) {
		case SEEK_CUR:
			base = mem_header->current_position;
			break;
		case SEEK_END:
			base = mem_header->file_length;
			break;
		default:
			// SEEK_SET
			break;
	}

	// the pointer can also be positioned beyond the end of the file
	if ((offset < -base) || (offset > FI_MEMORY_MAX - base)) {
		return -1;
	}

	mem_header->current_position = base + offset;
	return 0;
}

INT64 DLL_CALLCONV
_MemoryTellProc(fi_handle handle) {
	if (!handle) {
		return -1;
	}
	FIMEMORYHEADER *mem_header = (FIMEMORYHEADER*)(((FIMEMORY*)handle)->data);
	return mem_header->current_position;
}

// ----------------------------------------------------------

void
SetMemoryIO(FreeImageIO *io) {
	io->read_proc  = _MemoryReadProc;
	io->seek_proc  = _MemorySeekProc;
	io->tell_proc  = _MemoryTellProc;
	io->write_proc = _MemoryWriteProc;
}
