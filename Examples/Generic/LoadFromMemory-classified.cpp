// ==========================================================
// Classified FreeImageIO handler
//
// Design and implementation by
// - schickb (schickb@hotmail.com)
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

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "FreeImage.h"

// DLL_CALLCONV on declarations and definitions alike

class MemIO : public FreeImageIO {
public :
    MemIO( BYTE *data, long size ) : _start(data), _cp(data), _size(size) {
        read_proc  = _ReadProc;
        write_proc = _WriteProc;
        tell_proc  = _TellProc;
        seek_proc  = _SeekProc;
    }

    void Reset() {
		_cp = _start;
	}

    static unsigned DLL_CALLCONV _ReadProc(void *buffer, unsigned size, unsigned count, fi_handle handle);
    static unsigned DLL_CALLCONV _WriteProc(void *buffer, unsigned size, unsigned count, fi_handle handle);
    static int DLL_CALLCONV _SeekProc(fi_handle handle, INT64 offset, int origin);
	static INT64 DLL_CALLCONV _TellProc(fi_handle handle);

private:
    BYTE * const _start;
    BYTE *_cp;
    long _size;			// buffer size, for SEEK_END
};


unsigned DLL_CALLCONV
MemIO::_ReadProc(void *buffer, unsigned size, unsigned count, fi_handle handle) {
    MemIO *memIO = (MemIO*)handle;

    // stop at the end of the buffer; return whole items read
    long remaining = memIO->_size - (memIO->_cp - memIO->_start);

    if ((size == 0) || (remaining <= 0)) {
        return 0;
    }
    if ((unsigned long)remaining < (unsigned long)size * count) {
        count = (unsigned)((unsigned long)remaining / size);
    }

    memcpy(buffer, memIO->_cp, (size_t)size * count);

    memIO->_cp += (size_t)size * count;

    return count;
}

unsigned DLL_CALLCONV
MemIO::_WriteProc(void *buffer, unsigned size, unsigned count, fi_handle handle) {
    // this one only reads
    assert(false);
    return size;
}

int DLL_CALLCONV
MemIO::_SeekProc(fi_handle handle, INT64 offset, int origin) {
    MemIO *memIO = (MemIO*)handle;

    if (origin == SEEK_SET) {
        memIO->_cp = memIO->_start + offset;
    } else if (origin == SEEK_END) {
        memIO->_cp = memIO->_start + memIO->_size + offset;
    } else {
        memIO->_cp = memIO->_cp + offset;
    }

    return 0;
}

INT64 DLL_CALLCONV
MemIO::_TellProc(fi_handle handle) {
    MemIO *memIO = (MemIO*)handle;

    return (INT64)(memIO->_cp - memIO->_start);
}

// ----------------------------------------------------------
// the MemIO is both the FreeImageIO and the handle
// ----------------------------------------------------------

int
main(int argc, char *argv[]) {
	const char *filename = (argc > 1) ? argv[1] : "images/sample.tif";

	// call this ONLY when linking with FreeImage as a static library
#ifdef FREEIMAGE_LIB
	FreeImage_Initialise();
#endif // FREEIMAGE_LIB

	FILE *file = fopen(filename, "rb");

	if (file != NULL) {
		fseek(file, 0, SEEK_END);
		long size = ftell(file);
		fseek(file, 0, SEEK_SET);

		BYTE *data = new BYTE[size];

		if (fread(data, 1, size, file) == (size_t)size) {
			MemIO memIO(data, size);

			FREE_IMAGE_FORMAT fif = FreeImage_GetFileTypeFromHandle(&memIO, (fi_handle)&memIO, 0);

			// rewind after the format probe
			memIO.Reset();

			FIBITMAP *fbmp = FreeImage_LoadFromHandle(fif, &memIO, (fi_handle)&memIO);

			if (fbmp != NULL) {
				printf("%s : %u x %u, %u bpp\n", filename,
					FreeImage_GetWidth(fbmp), FreeImage_GetHeight(fbmp), FreeImage_GetBPP(fbmp));

				FreeImage_Unload(fbmp);
			} else {
				printf("%s : could not be decoded\n", filename);
			}
		}

		delete [] data;
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