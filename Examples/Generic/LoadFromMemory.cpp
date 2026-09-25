// ==========================================================
// Load From Memory Example
//
// Design and implementation by Floris van den Berg
//
// This file is part of FreeImage 3
//
// Use at own risk!
// ==========================================================
//
//  This example shows how to load a bitmap from memory
//  rather than from a file. To do this we make use of the
//  FreeImage_LoadFromHandle functions where we override
//  the i/o functions to simulate FILE* access in memory.
//
//  For seeking purposes the fi_handle passed to the i/o
//  functions contain the start of the data block where the
//  bitmap is stored.
//
// ==========================================================

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeImage.h"

// ----------------------------------------------------------

// read position; handle is the buffer start
fi_handle g_load_address;

// buffer size, for SEEK_END
long g_buffer_size;

// ----------------------------------------------------------

static unsigned DLL_CALLCONV
_ReadProc(void *buffer, unsigned size, unsigned count, fi_handle handle) {
	// stop at the end of the buffer; return whole items read
	BYTE *start = (BYTE *)handle;
	BYTE *position = (BYTE *)g_load_address;
	long remaining = g_buffer_size - (long)(position - start);

	if ((size == 0) || (remaining <= 0)) {
		return 0;
	}
	if ((unsigned long)remaining < (unsigned long)size * count) {
		count = (unsigned)((unsigned long)remaining / size);
	}

	memcpy(buffer, position, (size_t)size * count);

	g_load_address = position + (size_t)size * count;

	return count;
}

static unsigned DLL_CALLCONV
_WriteProc(void *buffer, unsigned size, unsigned count, fi_handle handle) {
	// there's not much use for saving the bitmap into memory now, is there?

	return size;
}

static int DLL_CALLCONV
_SeekProc(fi_handle handle, INT64 offset, int origin) {
	if (origin == SEEK_SET) {
		g_load_address = (BYTE *)handle + offset;
	} else if (origin == SEEK_END) {
		g_load_address = (BYTE *)handle + g_buffer_size + offset;
	} else {
		g_load_address = (BYTE *)g_load_address + offset;
	}

	return 0;
}

static INT64 DLL_CALLCONV
_TellProc(fi_handle handle) {
	assert((BYTE *)g_load_address >= (BYTE *)handle);

	return (INT64)((BYTE *)g_load_address - (BYTE *)handle);
}

// ----------------------------------------------------------

int 
main(int argc, char *argv[]) {
	const char *filename = (argc > 1) ? argv[1] : "images/sample.tif";

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_Initialise();
#endif // FREEIMAGE_LIB

	FreeImageIO io;

	io.read_proc  = _ReadProc;
	io.write_proc = _WriteProc;
	io.tell_proc  = _TellProc;
	io.seek_proc  = _SeekProc;

	// read the file into memory; any source will do

	FILE *file = fopen(filename, "rb");

	if (file != NULL) {
		fseek(file, 0, SEEK_END);
		long file_size = ftell(file);
		fseek(file, 0, SEEK_SET);

		BYTE *test = new BYTE[file_size];

		if (fread(test, 1, file_size, file) == (size_t)file_size) {
			// the i/o callbacks need the address and the length

			g_load_address = test;
			g_buffer_size = file_size;

			// detect the format, then convert

			FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromHandle(&io, (fi_handle)test, 0);

			g_load_address = test;

			FIBITMAP *dib = FreeImage_LoadFromHandle(fif, &io, (fi_handle)test);

			if (dib != NULL) {
				printf("%s : %u x %u, %u bpp\n", filename,
					FreeImage_GetWidth(dib), FreeImage_GetHeight(dib), FreeImage_GetBPP(dib));

				// don't forget to free the dib !
				FreeImage_Unload(dib);
			} else {
				printf("%s : could not be decoded\n", filename);
			}
		}

		delete [] test;
		fclose(file);
	} else {
		printf("%s : could not be opened\n", filename);
	}

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_DeInitialise();
#endif // FREEIMAGE_LIB

	return 0;
}