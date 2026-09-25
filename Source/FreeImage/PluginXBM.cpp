// ==========================================================
// XBM Loader
//
// Design and implementation by
// - Hervé Drolon <drolon@infonie.fr>
// part of the code adapted from the netPBM package (xbmtopbm.c)
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
//
//
// ==========================================================

#include "FreeImage.h"
#include "Utilities.h"

// ==========================================================
// Internal functions
// ==========================================================

#define MAX_LINE	512

static const char *ERR_XBM_SYNTAX	= "Syntax error";
static const char *ERR_XBM_LINE		= "Line too long";
static const char *ERR_XBM_DECL		= "Unable to find a line in the file containing the start of C array declaration (\"static char\" or whatever)";
static const char *ERR_XBM_EOFREAD	= "EOF / read error";
static const char *ERR_XBM_WIDTH	= "Invalid width";
static const char *ERR_XBM_HEIGHT	= "Invalid height";
static const char *ERR_XBM_MEMORY	= "Out of memory";

/**
Get a string from a stream. 
Read the string from the current stream to the first newline character. 
The result stored in str is appended with a null character.
@param str Storage location for data
@param n Maximum number of characters to read 
@param io Pointer to the FreeImageIO structure
@param handle Handle to the stream
@return Returns str. NULL is returned to indicate an error or an end-of-file condition.
*/
static char* 
readLine(char *str, int n, FreeImageIO *io, fi_handle handle) {
	char c = 0;
	int count, i = 0;
	do {
		count = io->read_proc(&c, 1, 1, handle);
		str[i++] = c;
	} while((c != '\n') && (i < n));
	if(count <= 0)
		return NULL;
	str[i] = '\0';
	return str;
}

/**
Get a char from the stream
@param io Pointer to the FreeImageIO structure
@param handle Handle to the stream
@return Returns the next character in the stream
*/
static int 
readChar(FreeImageIO *io, fi_handle handle) {
	BYTE c = 0;
	if(io->read_proc(&c, 1, 1, handle) != 1)
		return EOF;
	return c;
}

/**
Read an XBM file into a buffer
@param io Pointer to the FreeImageIO structure
@param handle Handle to the stream
@param widthP (return value) Pointer to the bitmap width
@param heightP (return value) Pointer to the bitmap height
@param dataP (return value) Pointer to the bitmap buffer
@param rowsP (return value) Rows read before an error in the data; the rest of the buffer is zero
@return Returns NULL if OK, returns an error message otherwise
*/
static const char* 
readXBMFile(FreeImageIO *io, fi_handle handle, int *widthP, int *heightP, char **dataP, int *rowsP) {
	char line[MAX_LINE], name_and_type[MAX_LINE];
	char* ptr;
	char* t;
	int version = 0;
	int raster_length, v;
	int bytes, bytes_per_line, padding;
	int c1, c2, value1, value2;
	int hex_table[256];
	BOOL found_declaration;
	/* in scanning through the bitmap file, we have found the first
	 line of the C declaration of the array (the "static char ..."
	 or whatever line)
	 */
	BOOL eof;	// we've encountered end of file while searching file
	const char *error = NULL;

	*widthP = *heightP = -1;
	*rowsP = 0;

	found_declaration = FALSE;    // haven't found it yet; haven't even looked
	eof = FALSE;                  // haven't encountered end of file yet 
	
	while(!found_declaration && !eof) {

		// n counts characters: readLine() writes the NUL at str[n]
		if( readLine(line, MAX_LINE - 1, io, handle) == NULL) {
			eof = TRUE;
		}
		else {
			if( strlen( line ) == MAX_LINE - 1 )
				return( ERR_XBM_LINE );
			if( sscanf(line, "#define %s %d", name_and_type, &v) == 2 ) {
				if( ( t = strrchr( name_and_type, '_' ) ) == NULL )
					t = name_and_type;
				else
					t++;
				if ( ! strcmp( "width", t ) )
					*widthP = v;
				else if ( ! strcmp( "height", t ) )
					*heightP = v;
				continue;
			}

			if( sscanf( line, "static short %s = {", name_and_type ) == 1 ) {
				version = 10;
				found_declaration = TRUE;
			}
			else if( sscanf( line, "static char %s = {", name_and_type ) == 1 ) {
				version = 11;
				found_declaration = TRUE;
			}
			else if(sscanf(line, "static unsigned char %s = {", name_and_type ) == 1 ) {
				version = 11;
				found_declaration = TRUE;
			}
		}
	}

	if(!found_declaration) 
		return( ERR_XBM_DECL );

	if(*widthP == -1 )
		return( ERR_XBM_WIDTH );
	if( *heightP == -1 )
		return( ERR_XBM_HEIGHT );

	padding = 0;
	if ( ((*widthP % 16) >= 1) && ((*widthP % 16) <= 8) && (version == 10) )
		padding = 1;

	bytes_per_line = (*widthP + 7) / 8 + padding;

	raster_length =  bytes_per_line * *heightP;
	*dataP = (char*) malloc( raster_length );
	if ( *dataP == (char*) 0 )
		return( ERR_XBM_MEMORY );

	// initialize hex_table
	for ( c1 = 0; c1 < 256; c1++ ) {
		hex_table[c1] = 256;
	}
	hex_table['0'] = 0;
	hex_table['1'] = 1;
	hex_table['2'] = 2;
	hex_table['3'] = 3;
	hex_table['4'] = 4;
	hex_table['5'] = 5;
	hex_table['6'] = 6;
	hex_table['7'] = 7;
	hex_table['8'] = 8;
	hex_table['9'] = 9;
	hex_table['A'] = 10;
	hex_table['B'] = 11;
	hex_table['C'] = 12;
	hex_table['D'] = 13;
	hex_table['E'] = 14;
	hex_table['F'] = 15;
	hex_table['a'] = 10;
	hex_table['b'] = 11;
	hex_table['c'] = 12;
	hex_table['d'] = 13;
	hex_table['e'] = 14;
	hex_table['f'] = 15;

	ptr = *dataP;
	const INT64 data_start = io->tell_proc(handle);

	if(version == 10) {
		for( bytes = 0; bytes < raster_length; bytes += 2 ) {
			while( ( c1 = readChar(io, handle) ) != 'x' ) {
				if ( c1 == EOF ) {
					error = ERR_XBM_EOFREAD;
					goto data_end;
				}
			}

			c1 = readChar(io, handle);
			c2 = readChar(io, handle);
			if( c1 == EOF || c2 == EOF ) {
				error = ERR_XBM_EOFREAD;
				goto data_end;
			}
			value1 = ( hex_table[c1] << 4 ) + hex_table[c2];
			if ( value1 >= 256 ) {
				error = ERR_XBM_SYNTAX;
				goto data_end;
			}
			c1 = readChar(io, handle);
			c2 = readChar(io, handle);
			if( c1 == EOF || c2 == EOF ) {
				error = ERR_XBM_EOFREAD;
				goto data_end;
			}
			value2 = ( hex_table[c1] << 4 ) + hex_table[c2];
			if ( value2 >= 256 ) {
				error = ERR_XBM_SYNTAX;
				goto data_end;
			}
			*ptr++ = (char)value2;
			if ( ( ! padding ) || ( ( bytes + 2 ) % bytes_per_line ) )
				*ptr++ = (char)value1;
		}
	}
	else {
		for(bytes = 0; bytes < raster_length; bytes++ ) {
			/*
			** skip until digit is found
			*/
			for( ; ; ) {
				c1 = readChar(io, handle);
				if ( c1 == EOF ) {
					error = ERR_XBM_EOFREAD;
					goto data_end;
				}
				value1 = hex_table[c1];
				if ( value1 != 256 )
					break;
			}
			/*
			** loop on digits
			*/
			for( ; ; ) {
				c2 = readChar(io, handle);
				if ( c2 == EOF ) {
					error = ERR_XBM_EOFREAD;
					goto data_end;
				}
				value2 = hex_table[c2];
				if ( value2 != 256 ) {
					value1 = (value1 << 4) | value2;
					if ( value1 >= 256 ) {
						error = ERR_XBM_SYNTAX;
						goto data_end;
					}
				}
				else if ( c2 == 'x' || c2 == 'X' ) {
					if ( value1 == 0 )
						continue;
					error = ERR_XBM_SYNTAX;
					goto data_end;
				}
				else break;
			}
			*ptr++ = (char)value1;
		}
	}

data_end:
	// a cut or damaged file keeps the bytes before the damage; the rest is zero (white)
	if (ptr < *dataP + raster_length) {
		memset(ptr, 0, (*dataP + raster_length) - ptr);
	}
	*rowsP = (bytes_per_line > 0) ? (int)(ptr - *dataP) / bytes_per_line : 0;
	// nothing is kept of a huge claim a few bytes long
	if (error && !PlausibleImageSize((UINT64)raster_length, (UINT64)(io->tell_proc(handle) - data_start), 1)) {
		*rowsP = 0;
	}

	return error;
}

// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "XBM";
}

static const char * DLL_CALLCONV
Description() {
	return "X11 Bitmap Format";
}

static const char * DLL_CALLCONV
Extension() {
	return "xbm";
}

static const char * DLL_CALLCONV
RegExpr() {
	return NULL;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/x-xbitmap";
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	char magic[8];
	if(readLine(magic, 7, io, handle)) {
		if(strcmp(magic, "#define") == 0)
			return TRUE;
	}
	return FALSE;
}

static BOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return FALSE;
}

static BOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return FALSE;
}

// ----------------------------------------------------------

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	char *buffer = NULL;
	int width, height;
	FIBITMAP *dib = NULL;

	try {

		// load the bitmap data
		int rows = 0;
		const char* error = readXBMFile(io, handle, &width, &height, &buffer, &rows);
		// Microsoft doesn't implement throw between functions :(
		if(error) {
			// a cut or damaged file keeps its complete rows
			if(!buffer || rows <= 0) throw (char*)error;
			FreeImage_OutputMessageProc(s_format_id, error);
		}


		// allocate a new dib
		dib = FreeImage_Allocate(width, height, 1);
		if(!dib) throw (char*)ERR_XBM_MEMORY;

		// write the palette data
		RGBQUAD *pal = FreeImage_GetPalette(dib);
		pal[0].rgbRed = pal[0].rgbGreen = pal[0].rgbBlue = 0;
		pal[1].rgbRed = pal[1].rgbGreen = pal[1].rgbBlue = 255;

		// copy the bitmap
		BYTE *bP = (BYTE*)buffer;
		for(int y = 0; y < height; y++) {
			BYTE count = 0;
			BYTE mask = 1;
			BYTE *bits = FreeImage_GetScanLine(dib, height - 1 - y);

			for(int x = 0; x < width; x++) {
				if(count >= 8) {
					bP++;
					count = 0;
					mask = 1;
				}
				if(*bP & mask) {
					// Set bit(x, y) to 0
					bits[x >> 3] &= (0xFF7F >> (x & 0x7));
				} else {
					// Set bit(x, y) to 1
					bits[x >> 3] |= (0x80 >> (x & 0x7));
				}
				count++;
				mask <<= 1;
			}
			bP++;
		}

		if(error) {
			PartialImageWarning(s_format_id, MIN(rows, height), height);
		}

		free(buffer);
		return dib;

	} catch(const char *text) {
		if(buffer)	free(buffer);
		if(dib)		FreeImage_Unload(dib);
		FreeImage_OutputMessageProc(s_format_id, text);
		return NULL;
	}
}


// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitXBM(Plugin *plugin, int format_id) {
    s_format_id = format_id;

	plugin->format_proc = Format;
	plugin->description_proc = Description;
	plugin->extension_proc = Extension;
	plugin->regexpr_proc = RegExpr;
	plugin->open_proc = NULL;
	plugin->close_proc = NULL;
	plugin->pagecount_proc = NULL;
	plugin->pagecapability_proc = NULL;
	plugin->load_proc = Load;
	plugin->save_proc = NULL;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = NULL;
}

