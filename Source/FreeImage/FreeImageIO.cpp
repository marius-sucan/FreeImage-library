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

#include "FreeImage.h"
#include "Utilities.h"
#include "FreeImageIO.h"

// =====================================================================
// File IO functions
// =====================================================================

// static: these four are the stdio implementations SetDefaultIO installs, they
// are used nowhere but in this file and are declared in no header. Without it
// they have external linkage, and four names as ordinary as _ReadProc and
// _TellProc sit in the static library waiting to capture any program that
// happens to define its own - which is exactly what they did to
// Examples/Generic/LoadFromMemory.cpp, whose FreeImageIO then read the caller's
// memory buffer with fread() and crashed.

static unsigned DLL_CALLCONV 
_ReadProc(void *buffer, unsigned size, unsigned count, fi_handle handle) {
	return (unsigned)fread(buffer, size, count, (FILE *)handle);
}

static unsigned DLL_CALLCONV 
_WriteProc(void *buffer, unsigned size, unsigned count, fi_handle handle) {
	return (unsigned)fwrite(buffer, size, count, (FILE *)handle);
}

static int DLL_CALLCONV
_SeekProc(fi_handle handle, long offset, int origin) {
	return fseek((FILE *)handle, offset, origin);
}

static long DLL_CALLCONV
_TellProc(fi_handle handle) {
	return ftell((FILE *)handle);
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

	// size * count has to be computed in a type that can hold it.  As
	// "(int)size * count" it was evaluated in 32 bits and stored in an int, so
	// any request of 2 GiB or more landed on a negative value and fell out of
	// the test below - a silent short read that looked like end of file.  A
	// memory stream is bounded by file_length, which is an int, so a request
	// that large cannot be served at all: refuse it deliberately rather than
	// leave it to the arithmetic.
	const UINT64 required_bytes = (UINT64)size * (UINT64)count;
	const int remaining_bytes = mem_header->file_length - mem_header->current_position;

	if ((required_bytes <= (UINT64)std::numeric_limits<int>::max()) && (remaining_bytes > 0)) {
		if ((int)required_bytes <= remaining_bytes) {
			// copy size bytes count times
			memcpy(buffer, (char*)mem_header->data + mem_header->current_position, (size_t)required_bytes);
			mem_header->current_position += (int)required_bytes;
			return count;
		}
		else {
			// if there isn't required_bytes bytes left to read, set pos to eof and return a short count
			memcpy(buffer, (char*)mem_header->data + mem_header->current_position, (size_t)remaining_bytes);
			mem_header->current_position = mem_header->file_length;
			return (unsigned)(remaining_bytes / size);
		}
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

	// the cast to long used to happen AFTER a 32-bit multiply, so size = count =
	// 0x10000 gave required_bytes == 0: nothing was copied and the function
	// still returned count, reporting a write that never happened
	const UINT64 wanted = (UINT64)size * (UINT64)count;

	if (wanted > (UINT64)std::numeric_limits<int>::max()) {
		return 0;
	}

	const long required_bytes = (long)wanted;

	// double the data block size if we need to
	while( (mem_header->current_position + required_bytes) >= mem_header->data_length ) {
		long newdatalen = 0;

		// if we are at or above 1G, we cant double without going negative
		if( mem_header->data_length & 0x40000000 ) {
			// max 2G
			if( mem_header->data_length == 0x7FFFFFFF ) {
				return 0;
			}
			newdatalen = 0x7FFFFFFF;
		} else if( mem_header->data_length == 0 ) {
			// default to 4K if nothing yet
			newdatalen = 4096;
		} else {
			// double size
			newdatalen = mem_header->data_length << 1;
		}
		void *newdata = realloc(mem_header->data, newdatalen);
		if(!newdata) {
			return 0;
		}
		mem_header->data = newdata;
		mem_header->data_length = newdatalen;
	}

	memcpy((char *)mem_header->data + mem_header->current_position, buffer, required_bytes);
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
_MemorySeekProc(fi_handle handle, long offset, int origin) {
	if (!handle) {
		return -1;
	}

	FIMEMORYHEADER *mem_header = (FIMEMORYHEADER*)(((FIMEMORY*)handle)->data);

	// you can use _MemorySeekProc to reposition the pointer anywhere in a file
	// the pointer can also be positioned beyond the end of the file

	switch (origin) { //0 to filelen-1 are 'inside' the file
		default:
		case SEEK_SET: //can fseek() to 0-7FFFFFFF always
			if ((offset >= 0) && (offset <= std::numeric_limits<int>::max())) {
				// the 64-bit long offset may overflow when casted to int
				mem_header->current_position = offset;
				return 0;
			}
			break;

		case SEEK_CUR:
			if (((mem_header->current_position + offset) >= 0) && ((mem_header->current_position + offset) <= std::numeric_limits<int>::max())) {
				// the 64-bit result may overflow when casted to int
				mem_header->current_position += offset;
				return 0;
			}
			break;

		case SEEK_END:
			if (((mem_header->file_length + offset) >= 0) && ((mem_header->file_length + offset) <= std::numeric_limits<int>::max())) {
				// the 64-bit result may overflow when casted to int
				mem_header->current_position = mem_header->file_length + offset;
				return 0;
			}
			break;
	}

	return -1;
}

long DLL_CALLCONV 
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
