// ==========================================================
// FreeImage implementation
//
// Design and implementation by
// - Floris van den Berg (flvdberg@wxs.nl)
// - Hervé Drolon (drolon@infonie.fr)
// - Karl-Heinz Bussian (khbussian@moss.de)
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


#ifdef _WIN32
#include <windows.h>
#endif

#include "FreeImage.h"
#include "Utilities.h"

//----------------------------------------------------------------------

static const char *s_copyright = "This program uses FreeImage, a free, open source image library supporting all common bitmap formats. See http://freeimage.sourceforge.net for details";

//----------------------------------------------------------------------

#if defined(_WIN32) && !defined(__MINGW32__)
#ifndef FREEIMAGE_LIB

BOOL APIENTRY
DllMain(HANDLE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
	switch (ul_reason_for_call) {
		case DLL_PROCESS_ATTACH :
			FreeImage_Initialise(FALSE);
			break;

		case DLL_PROCESS_DETACH :
			FreeImage_DeInitialise();
			break;

		case DLL_THREAD_ATTACH :
		case DLL_THREAD_DETACH :
			break;
    }

    return TRUE;
}

#endif // FREEIMAGE_LIB

#else // !_WIN32 
#ifndef FREEIMAGE_LIB

void FreeImage_SO_Initialise() __attribute__((constructor));
void FreeImage_SO_DeInitialise() __attribute__((destructor));

void FreeImage_SO_Initialise() {
  FreeImage_Initialise(FALSE);
}

void FreeImage_SO_DeInitialise() {
  FreeImage_DeInitialise();
}
#endif // FREEIMAGE_LIB

#endif // _WIN32

//----------------------------------------------------------------------

const char * DLL_CALLCONV
FreeImage_GetVersion() {
	static char s_version[16];
	sprintf(s_version, "%d.%d.%d", FREEIMAGE_MAJOR_VERSION, FREEIMAGE_MINOR_VERSION, FREEIMAGE_RELEASE_SERIAL);
	return s_version;
}

const char * DLL_CALLCONV
FreeImage_GetCopyrightMessage() {
	return s_copyright;
}

//----------------------------------------------------------------------

BOOL DLL_CALLCONV
FreeImage_IsLittleEndian() {
	union {
		DWORD i;
		BYTE c[4];
	} u;
	u.i = 1;
	return (u.c[0] != 0);
}

//----------------------------------------------------------------------

static FreeImage_OutputMessageFunction freeimage_outputmessage_proc = NULL;
static FreeImage_OutputMessageFunctionStdCall freeimage_outputmessagestdcall_proc = NULL; 

void DLL_CALLCONV
FreeImage_SetOutputMessage(FreeImage_OutputMessageFunction omf) {
	freeimage_outputmessage_proc = omf;
}

void DLL_CALLCONV
FreeImage_SetOutputMessageStdCall(FreeImage_OutputMessageFunctionStdCall omf) {
	freeimage_outputmessagestdcall_proc = omf;
}

// ----------------------------------------------------------
// qpv: every message also goes to the debugger output ("qpv: fim: ...")
// ----------------------------------------------------------

// bounded append; the buffer is always terminated
static void
FreeImage_AppendMessage(char *message, int *length, int capacity, const char *text) {
	if (text == NULL) {
		text = "(null)";
	}
	while ((*text != '\0') && (*length < capacity - 1)) {
		message[(*length)++] = *text++;
	}
	message[*length] = '\0';
}

void DLL_CALLCONV
FreeImage_OutputMessageProc(int fif, const char *fmt, ...) {
	const int MSG_SIZE = 512; // 512 bytes should be more than enough for a short message

	if (fmt != NULL) {
		char message[MSG_SIZE];
		memset(message, 0, MSG_SIZE);

		// initialize the optional parameter list

		va_list arg;
		va_start(arg, fmt);

		// check the length of the format string

		int str_length = (int)( (strlen(fmt) > MSG_SIZE) ? MSG_SIZE : strlen(fmt) );

		// parse the format string and put the result in 'message'
		// (only %s, %d, %i, %u, %o, %x and %% are understood)

		int j = 0;

		for (int i = 0; i < str_length; ++i) {
			if (fmt[i] == '%') {
				if (i + 1 < str_length) {
					switch(tolower((unsigned char)fmt[i + 1])) {
						case '%' :
							FreeImage_AppendMessage(message, &j, MSG_SIZE, "%");
							break;

						case 'o' : // octal numbers
						{
							char tmp[16];

							_itoa(va_arg(arg, int), tmp, 8);

							FreeImage_AppendMessage(message, &j, MSG_SIZE, tmp);

							++i;

							break;
						}

						case 'i' : // decimal numbers
						case 'd' :
						{
							char tmp[16];

							_itoa(va_arg(arg, int), tmp, 10);

							FreeImage_AppendMessage(message, &j, MSG_SIZE, tmp);

							++i;

							break;
						}

						case 'u' : // unsigned decimal numbers
						{
							char tmp[16];

							sprintf(tmp, "%u", va_arg(arg, unsigned int));

							FreeImage_AppendMessage(message, &j, MSG_SIZE, tmp);

							++i;

							break;
						}

						case 'x' : // hexadecimal numbers
						{
							char tmp[16];

							_itoa(va_arg(arg, int), tmp, 16);

							FreeImage_AppendMessage(message, &j, MSG_SIZE, tmp);

							++i;

							break;
						}

						case 's' : // strings
						{
							FreeImage_AppendMessage(message, &j, MSG_SIZE, va_arg(arg, char*));

							++i;

							break;
						}
					};
				} else if (j < MSG_SIZE - 1) {
					message[j++] = fmt[i];
				}
			} else if (j < MSG_SIZE - 1) {
				message[j++] = fmt[i];
			};
		}

		// deinitialize the optional parameter list

		va_end(arg);

#ifdef _WIN32
		// mirror to the debugger (DebugView)

		{
			char line[MSG_SIZE + 64];
			int length = 0;
			const char *format = (fif >= 0) ? FreeImage_GetFormatFromFIF((FREE_IMAGE_FORMAT)fif) : NULL;

			FreeImage_AppendMessage(line, &length, (int)sizeof(line), "qpv: fim: ");
			if (format != NULL) {
				FreeImage_AppendMessage(line, &length, (int)sizeof(line), "[");
				FreeImage_AppendMessage(line, &length, (int)sizeof(line), format);
				FreeImage_AppendMessage(line, &length, (int)sizeof(line), "] ");
			}
			FreeImage_AppendMessage(line, &length, (int)sizeof(line), message);
			FreeImage_AppendMessage(line, &length, (int)sizeof(line), "\n");

			OutputDebugStringA(line);
		}
#endif

		// output the message to the user program

		if (freeimage_outputmessage_proc != NULL)
			freeimage_outputmessage_proc((FREE_IMAGE_FORMAT)fif, message);

		if (freeimage_outputmessagestdcall_proc != NULL)
			freeimage_outputmessagestdcall_proc((FREE_IMAGE_FORMAT)fif, message);
	}
}
