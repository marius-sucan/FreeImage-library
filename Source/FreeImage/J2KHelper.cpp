// ==========================================================
// JPEG2000 helpers
//
// Design and implementation by
// - Hervé Drolon (drolon@infonie.fr)
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
#include "../LibOpenJPEG/openjpeg.h"
#include "J2KHelper.h"
#include <limits.h>

// --------------------------------------------------------------------------

static OPJ_UINT64 
_LengthProc(J2KFIO_t *fio) {
	// length from the stream start; OpenJPEG needs it when Psot is 0
	fio->io->seek_proc(fio->handle, 0, SEEK_END);
	long end_pos = fio->io->tell_proc(fio->handle);
	fio->io->seek_proc(fio->handle, fio->start, SEEK_SET);
	return (end_pos > fio->start) ? (OPJ_UINT64)(end_pos - fio->start) : 0;
}

static OPJ_SIZE_T 
_ReadProc(void *p_buffer, OPJ_SIZE_T p_nb_bytes, void *p_user_data) {
	J2KFIO_t *fio = (J2KFIO_t*)p_user_data;
	OPJ_SIZE_T l_nb_read = fio->io->read_proc(p_buffer, 1, (unsigned)p_nb_bytes, fio->handle);
	if (!l_nb_read && p_nb_bytes) {
		fio->eof = TRUE;
	}
	return l_nb_read ? l_nb_read : (OPJ_SIZE_T)-1;
}

static OPJ_SIZE_T 
_WriteProc(void *p_buffer, OPJ_SIZE_T p_nb_bytes, void *p_user_data) {
	J2KFIO_t *fio = (J2KFIO_t*)p_user_data;  
	return fio->io->write_proc(p_buffer, 1, (unsigned)p_nb_bytes, fio->handle);
}

static OPJ_OFF_T 
_SkipProc(OPJ_OFF_T p_nb_bytes, void *p_user_data) {
	J2KFIO_t *fio = (J2KFIO_t*)p_user_data;
	if( fio->io->seek_proc(fio->handle, (long)p_nb_bytes, SEEK_CUR) ) {
		return -1;
	}
	return p_nb_bytes;
}

static OPJ_BOOL 
_SeekProc(OPJ_OFF_T p_nb_bytes, void *p_user_data) {
	J2KFIO_t *fio = (J2KFIO_t*)p_user_data;
	// positions are relative to where the stream started, not the file
	if( (p_nb_bytes < 0) || (p_nb_bytes > (OPJ_OFF_T)(LONG_MAX - fio->start)) ) {
		return OPJ_FALSE;
	}
	if( fio->io->seek_proc(fio->handle, fio->start + (long)p_nb_bytes, SEEK_SET) ) {
		return OPJ_FALSE;
	}
	return OPJ_TRUE;
}

// --------------------------------------------------------------------------

J2KFIO_t* 
opj_freeimage_stream_create(FreeImageIO *io, fi_handle handle, BOOL bRead) {
	if(!handle) {
		return NULL;
	}
	J2KFIO_t *fio = (J2KFIO_t*)malloc(sizeof(J2KFIO_t));
	if(fio) {
		fio->io = io;
		fio->handle = handle;
		const long start = io->tell_proc(handle);
		fio->start = (start > 0) ? start : 0;
		fio->eof = FALSE;

		opj_stream_t *l_stream = opj_stream_create(OPJ_J2K_STREAM_CHUNK_SIZE, bRead ? OPJ_TRUE : OPJ_FALSE);
		if (l_stream) {
			opj_stream_set_user_data(l_stream, fio, NULL);
			opj_stream_set_user_data_length(l_stream, _LengthProc(fio));
			opj_stream_set_read_function(l_stream, _ReadProc);
			opj_stream_set_write_function(l_stream, _WriteProc);
			opj_stream_set_skip_function(l_stream, _SkipProc);
			opj_stream_set_seek_function(l_stream, _SeekProc);
			fio->stream = l_stream;
			return fio;
		} else {
			free(fio);
		}
	}

	return NULL;		
}

void 
opj_freeimage_stream_destroy(J2KFIO_t* fio) {
	if(fio) {
		if(fio->stream) {
			opj_stream_destroy(fio->stream);
		}
		free(fio);
	}
}

int 
opj_freeimage_decode_threads(FreeImageIO *io, fi_handle handle, OPJ_CODEC_FORMAT format) {
	// OpenJPEG syncs its pool several times per tile, so a thread only pays off per 2 KiB of tile data
	const int cpus = opj_get_num_cpus();
	if(cpus < 2) {
		return 0;
	}
	OPJ_UINT64 threads = 0;
	const long start = io->tell_proc(handle);
	J2KFIO_t *fio = opj_freeimage_stream_create(io, handle, TRUE);
	if(fio) {
		opj_codec_t *codec = opj_create_decompress(format);
		opj_dparameters_t parameters;
		opj_set_default_decoder_parameters(&parameters);
		opj_image_t *image = NULL;
		if(codec && opj_setup_decoder(codec, &parameters) && opj_read_header(fio->stream, codec, &image)) {
			opj_codestream_info_v2_t *info = opj_get_cstr_info(codec);
			if(info) {
				const OPJ_UINT64 tiles = (OPJ_UINT64)info->tw * info->th;
				if(tiles) {
					threads = _LengthProc(fio) / tiles / 2048;
				}
				opj_destroy_cstr_info(&info);
			}
		}
		opj_image_destroy(image);
		opj_destroy_codec(codec);
		opj_freeimage_stream_destroy(fio);
	}
	io->seek_proc(handle, start, SEEK_SET);
	return (threads < 2) ? 0 : (int)MIN(threads, (OPJ_UINT64)cpus);
}

// --------------------------------------------------------------------------

/**
Divide an integer by a power of 2 and round upwards
@return Returns a divided by 2^b
*/
static int int_ceildivpow2(int a, int b) {
	return (a + (1 << b) - 1) >> b;
}

/**
Convert a OpenJPEG image to a FIBITMAP
@param format_id Plugin ID
@param image OpenJPEG image
@param header_only If TRUE, allocate a 'header only' FIBITMAP, otherwise allocate a full FIBITMAP
@return Returns the converted image if successful, returns NULL otherwise
*/
FIBITMAP* J2KImageToFIBITMAP(int format_id, const opj_image_t *image, BOOL header_only) {
	FIBITMAP *dib = NULL;

	try {
		// check the number of components
		int numcomps = image->numcomps;
		if (numcomps < 1) {
			throw FI_MSG_ERROR_CORRUPTED_IMAGE;
		}

		// compute image width and height

		//int w = int_ceildiv(image->x1 - image->x0, image->comps[0].dx);
		int wr = image->comps[0].w;
		int wrr = int_ceildivpow2(image->comps[0].w, image->comps[0].factor);
		
		//int h = int_ceildiv(image->y1 - image->y0, image->comps[0].dy);
		//int hr = image->comps[0].h;
		int hrr = int_ceildivpow2(image->comps[0].h, image->comps[0].factor);

		BOOL bIsValid = TRUE;
		for(int c = 0; c < numcomps - 1; c++) {
			if(	(image->comps[c].dx == image->comps[c+1].dx) && 
				(image->comps[c].dy == image->comps[c+1].dy) &&
				(image->comps[c].prec == image->comps[c+1].prec) ) {
				continue;
			} else {
				bIsValid = FALSE;
				break;
			}
		}
		bIsValid &= ((numcomps == 1) || (numcomps == 3) || (numcomps == 4));
		if(!bIsValid) {
			if(numcomps) {
				FreeImage_OutputMessageProc(format_id, "Warning: image contains %d greyscale components. Only the first will be loaded.\n", numcomps);
				numcomps = 1;
			} else {
				// unknown type
				throw FI_MSG_ERROR_UNSUPPORTED_FORMAT;
			}
		}

		// create a new DIB

		if(image->comps[0].prec <= 8) {
			switch(numcomps) {
				case 1:
					dib = FreeImage_AllocateHeader(header_only, wrr, hrr, 8);
					break;
				case 3:
					dib = FreeImage_AllocateHeader(header_only, wrr, hrr, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
					break;
				case 4:
					dib = FreeImage_AllocateHeader(header_only, wrr, hrr, 32, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
					break;
			}
		} else if(image->comps[0].prec <= 16) {
			switch(numcomps) {
				case 1:
					dib = FreeImage_AllocateHeaderT(header_only, FIT_UINT16, wrr, hrr);
					break;
				case 3:
					dib = FreeImage_AllocateHeaderT(header_only, FIT_RGB16, wrr, hrr);
					break;
				case 4:
					dib = FreeImage_AllocateHeaderT(header_only, FIT_RGBA16, wrr, hrr);
					break;
			}
		} else {
			throw FI_MSG_ERROR_UNSUPPORTED_FORMAT;
		}
		if(!dib) {
			throw FI_MSG_ERROR_DIB_MEMORY;
		}

		// "header only" FIBITMAP ?
		if(header_only) {
			return dib;
		}
		
		if(image->comps[0].prec <= 8) {
			if(numcomps == 1) {
				// 8-bit greyscale
				// ----------------------------------------------------------

				// build a greyscale palette
				
				RGBQUAD *pal = FreeImage_GetPalette(dib);
				for (int i = 0; i < 256; i++) {
					pal[i].rgbRed	= (BYTE)i;
					pal[i].rgbGreen = (BYTE)i;
					pal[i].rgbBlue	= (BYTE)i;
				}

				// load pixel data

				unsigned pixel_count = 0;

				for(int y = 0; y < hrr; y++) {		
					BYTE *bits = FreeImage_GetScanLine(dib, hrr - 1 - y);

					for(int x = 0; x < wrr; x++) {
						const unsigned pixel_pos = pixel_count / wrr * wr + pixel_count % wrr;

						int index = image->comps[0].data[pixel_pos];
						index += (image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0);

						bits[x] = (BYTE)index;

						pixel_count++;
					}
				}
			}
			else if(numcomps == 3) {

				// 24-bit RGB
				// ----------------------------------------------------------	
				
				// load pixel data

				unsigned pixel_count = 0;

				for(int y = 0; y < hrr; y++) {		
					BYTE *bits = FreeImage_GetScanLine(dib, hrr - 1 - y);

					for(int x = 0; x < wrr; x++) {
						const unsigned pixel_pos = pixel_count / wrr * wr + pixel_count % wrr;

						int r = image->comps[0].data[pixel_pos];
						r += (image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0);
						
						int g = image->comps[1].data[pixel_pos];
						g += (image->comps[1].sgnd ? 1 << (image->comps[1].prec - 1) : 0);
						
						int b = image->comps[2].data[pixel_pos];
						b += (image->comps[2].sgnd ? 1 << (image->comps[2].prec - 1) : 0);

						bits[FI_RGBA_RED]   = (BYTE)r;
						bits[FI_RGBA_GREEN] = (BYTE)g;
						bits[FI_RGBA_BLUE]  = (BYTE)b;
						bits += 3;

						pixel_count++;
					}
				}
			}
			else if(numcomps == 4) {

				// 32-bit RGBA
				// ----------------------------------------------------------	
				
				// load pixel data

				unsigned pixel_count = 0;

				for(int y = 0; y < hrr; y++) {		
					BYTE *bits = FreeImage_GetScanLine(dib, hrr - 1 - y);

					for(int x = 0; x < wrr; x++) {
						const unsigned pixel_pos = pixel_count / wrr * wr + pixel_count % wrr;

						int r = image->comps[0].data[pixel_pos];
						r += (image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0);
						
						int g = image->comps[1].data[pixel_pos];
						g += (image->comps[1].sgnd ? 1 << (image->comps[1].prec - 1) : 0);
						
						int b = image->comps[2].data[pixel_pos];
						b += (image->comps[2].sgnd ? 1 << (image->comps[2].prec - 1) : 0);

						int a = image->comps[3].data[pixel_pos];
						a += (image->comps[3].sgnd ? 1 << (image->comps[3].prec - 1) : 0);

						bits[FI_RGBA_RED]   = (BYTE)r;
						bits[FI_RGBA_GREEN] = (BYTE)g;
						bits[FI_RGBA_BLUE]  = (BYTE)b;
						bits[FI_RGBA_ALPHA] = (BYTE)a;
						bits += 4;

						pixel_count++;
					}
				}
			}
		}
		else if(image->comps[0].prec <= 16) {
			if(numcomps == 1) {
				// 16-bit greyscale
				// ----------------------------------------------------------

				// load pixel data

				unsigned pixel_count = 0;

				for(int y = 0; y < hrr; y++) {		
					WORD *bits = (WORD*)FreeImage_GetScanLine(dib, hrr - 1 - y);

					for(int x = 0; x < wrr; x++) {
						const unsigned pixel_pos = pixel_count / wrr * wr + pixel_count % wrr;

						int index = image->comps[0].data[pixel_pos];
						index += (image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0);

						bits[x] = (WORD)index;

						pixel_count++;
					}
				}
			}
			else if(numcomps == 3) {

				// 48-bit RGB
				// ----------------------------------------------------------	
				
				// load pixel data

				unsigned pixel_count = 0;

				for(int y = 0; y < hrr; y++) {		
					FIRGB16 *bits = (FIRGB16*)FreeImage_GetScanLine(dib, hrr - 1 - y);

					for(int x = 0; x < wrr; x++) {
						const unsigned pixel_pos = pixel_count / wrr * wr + pixel_count % wrr;

						int r = image->comps[0].data[pixel_pos];
						r += (image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0);
						
						int g = image->comps[1].data[pixel_pos];
						g += (image->comps[1].sgnd ? 1 << (image->comps[1].prec - 1) : 0);
						
						int b = image->comps[2].data[pixel_pos];
						b += (image->comps[2].sgnd ? 1 << (image->comps[2].prec - 1) : 0);

						bits[x].red   = (WORD)r;
						bits[x].green = (WORD)g;
						bits[x].blue  = (WORD)b;

						pixel_count++;
					}
				}
			}
			else if(numcomps == 4) {

				// 64-bit RGBA
				// ----------------------------------------------------------	
				
				// load pixel data

				unsigned pixel_count = 0;

				for(int y = 0; y < hrr; y++) {		
					FIRGBA16 *bits = (FIRGBA16*)FreeImage_GetScanLine(dib, hrr - 1 - y);

					for(int x = 0; x < wrr; x++) {
						const unsigned pixel_pos = pixel_count / wrr * wr + pixel_count % wrr;

						int r = image->comps[0].data[pixel_pos];
						r += (image->comps[0].sgnd ? 1 << (image->comps[0].prec - 1) : 0);
						
						int g = image->comps[1].data[pixel_pos];
						g += (image->comps[1].sgnd ? 1 << (image->comps[1].prec - 1) : 0);
						
						int b = image->comps[2].data[pixel_pos];
						b += (image->comps[2].sgnd ? 1 << (image->comps[2].prec - 1) : 0);

						int a = image->comps[3].data[pixel_pos];
						a += (image->comps[3].sgnd ? 1 << (image->comps[3].prec - 1) : 0);

						bits[x].red   = (WORD)r;
						bits[x].green = (WORD)g;
						bits[x].blue  = (WORD)b;
						bits[x].alpha = (WORD)a;

						pixel_count++;
					}
				}
			}
		}

		return dib;

	} catch(const char *text) {
		if(dib) FreeImage_Unload(dib);
		FreeImage_OutputMessageProc(format_id, text);
		return NULL;
	}

}

/**
Convert a FIBITMAP to a OpenJPEG image
@param format_id Plugin ID
@param dib FreeImage image
@param parameters Compression parameters
@return Returns the converted image if successful, returns NULL otherwise
*/
opj_image_t* FIBITMAPToJ2KImage(int format_id, FIBITMAP *dib, opj_cparameters_t *parameters) {
	int prec, numcomps, x, y, index;
	OPJ_COLOR_SPACE color_space;
	opj_image_cmptparm_t cmptparm[4];	// maximum of 4 components 
	opj_image_t *image = NULL;			// image to encode

	try {
		int w = FreeImage_GetWidth(dib);
		int h = FreeImage_GetHeight(dib);

		// get image characteristics
		FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib);

		if(image_type == FIT_BITMAP) {
			// standard image ...
			prec = 8;
			// the copy below reads 1, 3 or 4 bytes per pixel: 1-, 4- and 16-bit rows are shorter
			const unsigned bpp = FreeImage_GetBPP(dib);
			if((bpp != 8) && (bpp != 24) && (bpp != 32)) {
				throw FI_MSG_ERROR_UNSUPPORTED_FORMAT;
			}
			switch(FreeImage_GetColorType(dib)) {
				case FIC_MINISBLACK:
					numcomps = 1;
					color_space = OPJ_CLRSPC_GRAY;
					break;
				case FIC_RGB:
					if(FreeImage_GetBPP(dib) == 32) {
						// 32-bit image with a fully opaque layer
						numcomps = 4;
						color_space = OPJ_CLRSPC_SRGB;
					} else {
						// 24-bit image
						numcomps = 3;
						color_space = OPJ_CLRSPC_SRGB;
					}
					break;
				case FIC_RGBALPHA:
					numcomps = 4;
					color_space = OPJ_CLRSPC_SRGB;
					break;
				default:
					return NULL;
			}
		} else {
			// HDR image ...
			prec = 16;
			switch(image_type) {
				case FIT_UINT16:
					numcomps = 1;
					color_space = OPJ_CLRSPC_GRAY;
					break;
				case FIT_RGB16:
					numcomps = 3;
					color_space = OPJ_CLRSPC_SRGB;
					break;
				case FIT_RGBA16:
					numcomps = 4;
					color_space = OPJ_CLRSPC_SRGB;
					break;
				default:
					return NULL;
			}
		}

		// OpenJPEG needs min(w, h) >= 2^(numresolution - 1)
		{
			const int min_side = (w < h) ? w : h;
			int levels = 1;
			while( (levels < parameters->numresolution) && ((1 << levels) <= min_side) ) {
				levels++;
			}
			if(levels < parameters->numresolution) {
				parameters->numresolution = levels;
			}
		}

		// initialize image components
		memset(&cmptparm[0], 0, 4 * sizeof(opj_image_cmptparm_t));
		for(int i = 0; i < numcomps; i++) {
			cmptparm[i].dx = parameters->subsampling_dx;
			cmptparm[i].dy = parameters->subsampling_dy;
			cmptparm[i].w = w;
			cmptparm[i].h = h;
			cmptparm[i].prec = prec;
			cmptparm[i].sgnd = 0;
		}
		// create the image 
		image = opj_image_create(numcomps, &cmptparm[0], color_space);
		if(!image) {
			throw FI_MSG_ERROR_DIB_MEMORY;
		}

		// set image offset and reference grid 
		image->x0 = parameters->image_offset_x0;
		image->y0 = parameters->image_offset_y0;
		image->x1 = parameters->image_offset_x0 + (w - 1) *	parameters->subsampling_dx + 1;
		image->y1 = parameters->image_offset_y0 + (h - 1) *	parameters->subsampling_dy + 1;

		// set image data 
		if(prec == 8) {
			switch(numcomps) {
				case 1:
					index = 0;
					for(y = 0; y < h; y++) {
						BYTE *bits = FreeImage_GetScanLine(dib, h - 1 - y);
						for(x = 0; x < w; x++) {
							image->comps[0].data[index] = bits[x];
							index++;
						}
					}
					break;
				case 3:
					index = 0;
					for(y = 0; y < h; y++) {
						BYTE *bits = FreeImage_GetScanLine(dib, h - 1 - y);
						for(x = 0; x < w; x++) {
							image->comps[0].data[index] = bits[FI_RGBA_RED];
							image->comps[1].data[index] = bits[FI_RGBA_GREEN];
							image->comps[2].data[index] = bits[FI_RGBA_BLUE];
							bits += 3;
							index++;
						}
					}
					break;
				case 4:
					index = 0;
					for(y = 0; y < h; y++) {
						BYTE *bits = FreeImage_GetScanLine(dib, h - 1 - y);
						for(x = 0; x < w; x++) {
							image->comps[0].data[index] = bits[FI_RGBA_RED];
							image->comps[1].data[index] = bits[FI_RGBA_GREEN];
							image->comps[2].data[index] = bits[FI_RGBA_BLUE];
							image->comps[3].data[index] = bits[FI_RGBA_ALPHA];
							bits += 4;
							index++;
						}
					}
					break;
			}
		}
		else if(prec == 16) {
			switch(numcomps) {
				case 1:
					index = 0;
					for(y = 0; y < h; y++) {
						WORD *bits = (WORD*)FreeImage_GetScanLine(dib, h - 1 - y);
						for(x = 0; x < w; x++) {
							image->comps[0].data[index] = bits[x];
							index++;
						}
					}
					break;
				case 3:
					index = 0;
					for(y = 0; y < h; y++) {
						FIRGB16 *bits = (FIRGB16*)FreeImage_GetScanLine(dib, h - 1 - y);
						for(x = 0; x < w; x++) {
							image->comps[0].data[index] = bits[x].red;
							image->comps[1].data[index] = bits[x].green;
							image->comps[2].data[index] = bits[x].blue;
							index++;
						}
					}
					break;
				case 4:
					index = 0;
					for(y = 0; y < h; y++) {
						FIRGBA16 *bits = (FIRGBA16*)FreeImage_GetScanLine(dib, h - 1 - y);
						for(x = 0; x < w; x++) {
							image->comps[0].data[index] = bits[x].red;
							image->comps[1].data[index] = bits[x].green;
							image->comps[2].data[index] = bits[x].blue;
							image->comps[3].data[index] = bits[x].alpha;
							index++;
						}
					}
					break;
			}
		}

		return image;

	} catch (const char *text) {
		if(image) opj_image_destroy(image);
		FreeImage_OutputMessageProc(format_id, text);
		return NULL;
	}
}
