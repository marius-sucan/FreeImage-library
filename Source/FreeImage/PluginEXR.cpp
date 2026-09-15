// ==========================================================
// EXR Loader and writer
//
// Design and implementation by 
// - Hervé Drolon (drolon@infonie.fr)
// - Mihail Naydenov (mnaydenov@users.sourceforge.net)
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

#ifdef _MSC_VER
// OpenEXR has many problems with MSVC warnings (why not just correct them ?), just ignore one of them
#pragma warning (disable : 4800) // ImfVersion.h - 'const int' : forcing value to bool 'true' or 'false' (performance warning)
#endif 

#include "../OpenEXR/Iex/Iex.h"
#include "../OpenEXR/OpenEXR/ImfIO.h"
#include "../OpenEXR/OpenEXR/ImfStdIO.h"
#include "../OpenEXR/OpenEXR/ImfOutputFile.h"
#include "../OpenEXR/OpenEXR/ImfInputFile.h"
#include "../OpenEXR/OpenEXR/ImfRgbaFile.h"
#include "../OpenEXR/OpenEXR/ImfChannelList.h"
#include "../OpenEXR/OpenEXR/ImfRgba.h"
#include "../OpenEXR/OpenEXR/ImfArray.h"
#include "../OpenEXR/OpenEXR/ImfPreviewImage.h"
#include "../OpenEXR/Imath/half.h"
#include "../OpenEXR/Imath/ImathInt64.h"


// ==========================================================
// Plugin Interface
// ==========================================================

static int s_format_id;

/**
How much larger than the file the uncompressed pixel data may claim to be
before the data window is treated as a lie.  Used by CheckDataWindow below.

Measured rather than guessed, against flat colour - the most compressible
content there is - at sizes from 512 to 16384 square, four half channels:

    codec   512     2048    4096    8192    16384
    DWAB    2494    9590    14012   17672   19806
    DWAA    605     2220    4071    6915    10574
    ZIP     350     680     803     876     932     (deflate tops out at 1032)
    PIZ     129     205     258

The ratio grows with the picture and then levels off: quadrupling the pixel
count from 8192 to 16384 square moved DWAB by a factor of 1.12, so ~20000:1 is
where the most compressible file any encoder can write ends up.  This leaves
better than six times that, and still refuses a data window that would need a
ratio in the tens of millions, which is what a corrupted one asks for.
*/
#define FI_EXR_MAX_COMPRESSION_RATIO 131072

// ----------------------------------------------------------

/**
FreeImage input stream wrapper
@see Imf_2_2::IStream
*/
class C_IStream : public Imf::IStream {
private:
    FreeImageIO *_io;
	fi_handle _handle;

public:
	C_IStream (FreeImageIO *io, fi_handle handle) : 
	  Imf::IStream(""), _io (io), _handle(handle) {
	}

	virtual bool read (char c[/*n*/], int n) {
		return ((unsigned)n == _io->read_proc(c, 1, n, _handle));
	}

	virtual uint64_t tellg() {
		return _io->tell_proc(_handle);
	}

	virtual void seekg(uint64_t pos) {
		_io->seek_proc(_handle, (unsigned)pos, SEEK_SET);
	}

	virtual void clear() {
	}
};

// ----------------------------------------------------------

/**
FreeImage output stream wrapper
@see Imf_2_2::OStream
*/
class C_OStream : public Imf::OStream {
private:
    FreeImageIO *_io;
	fi_handle _handle;

public:
	C_OStream (FreeImageIO *io, fi_handle handle) : 
	  Imf::OStream(""), _io (io), _handle(handle) {
	}

	virtual void write(const char c[/*n*/], int n) {
		if((unsigned)n != _io->write_proc((void*)&c[0], 1, n, _handle)) {
			Iex::throwErrnoExc();
		}
	}

	virtual uint64_t tellp() {
		return _io->tell_proc(_handle);
	}

	virtual void seekp(uint64_t pos) {
		_io->seek_proc(_handle, (unsigned)pos, SEEK_SET);
	}
};

// ----------------------------------------------------------


// ==========================================================
// Plugin Implementation
// ==========================================================

static const char * DLL_CALLCONV
Format() {
	return "EXR";
}

static const char * DLL_CALLCONV
Description() {
	return "ILM OpenEXR";
}

static const char * DLL_CALLCONV
Extension() {
	return "exr";
}

static const char * DLL_CALLCONV
RegExpr() {
	return NULL;
}

static const char * DLL_CALLCONV
MimeType() {
	return "image/x-exr";
}

static BOOL DLL_CALLCONV
Validate(FreeImageIO *io, fi_handle handle) {
	BYTE exr_signature[] = { 0x76, 0x2F, 0x31, 0x01 };
	BYTE signature[] = { 0, 0, 0, 0 };

	io->read_proc(signature, 1, 4, handle);
	return (memcmp(exr_signature, signature, 4) == 0);
}

static BOOL DLL_CALLCONV
SupportsExportDepth(int depth) {
	return FALSE;
}

static BOOL DLL_CALLCONV 
SupportsExportType(FREE_IMAGE_TYPE type) {
	return (
		(type == FIT_FLOAT) ||
		(type == FIT_RGBF)  ||
		(type == FIT_RGBAF)
	);
}

static BOOL DLL_CALLCONV
SupportsNoPixels() {
	return TRUE;
}

// --------------------------------------------------------------------------

/**
Refuse a data window the file cannot possibly hold.

The data window is a claim made by the header, and nothing checks it against
the file: FreeImage_AllocateHeaderT takes a damaged or hostile one at face
value and asks the system for whatever it says, which for a single flipped
byte can be tens of gigabytes.

OpenEXR has a limit of its own, but Header::sanityCheck() reads it from
exr_set_default_maximum_image_size(), which is process-global state that a
library has no business setting, and the per-context
ContextInitializer::setMaxImageSize() is not wired through to it yet (see the
TODO in ImfHeader.cpp).  So the check belongs here, where it can also use
something OpenEXR does not have to hand: the length of the stream.

Both tests below are derived from the file rather than from a fixed maximum
size, so a genuinely enormous image still loads.

@param header Header of the file being loaded
@param width Data window width, already known to fit an int
@param height Data window height, already known to fit an int
@param stream_bytes Length of the stream, or 0 when it could not be measured
@throw Iex::InputExc when the file is too small for the picture it describes
*/
static void
CheckDataWindow(const Imf::Header& header, int width, int height, long stream_bytes) {
	if(stream_bytes <= 0) {
		// the stream would not say how long it is: nothing to compare against
		return;
	}

	// 1. A scanline image is stored in chunks of getCompressionNumScanlines()
	// rows.  Every chunk costs 8 bytes in the chunk offset table plus an 8 byte
	// chunk header (its y coordinate and its data size), so a file shorter than
	// 16 bytes per chunk cannot hold the number of rows it claims.  Exact: no
	// valid file can fail this.  Tiled images are left to the second test.
	if(!header.hasTileDescription()) {
		const int lines_per_chunk = Imf::getCompressionNumScanlines(header.compression());
		if(lines_per_chunk > 0) {
			const INT64 chunks = ((INT64)height + lines_per_chunk - 1) / lines_per_chunk;
			const INT64 needed = chunks * 16;
			if(needed > (INT64)stream_bytes) {
				THROW (Iex::InputExc, "Invalid data window: the header describes " << width << " x " << height
					<< " pixels, whose chunk table alone needs " << needed << " bytes, but the file holds only "
					<< stream_bytes << " bytes");
			}
		}
	}

	// 2. For any image, tiled or not, compare the uncompressed pixel data the
	// header describes against the size of the file.
	double bytes_per_pixel = 0;
	for (Imf::ChannelList::ConstIterator i = header.channels().begin(); i != header.channels().end(); ++i) {
		const Imf::Channel &channel = i.channel();
		const double sample_bytes = (channel.type == Imf::HALF) ? 2.0 : 4.0;
		const double x_sampling = (channel.xSampling > 0) ? (double)channel.xSampling : 1.0;
		const double y_sampling = (channel.ySampling > 0) ? (double)channel.ySampling : 1.0;
		// a subsampled channel keeps one sample per xSampling x ySampling pixels
		bytes_per_pixel += sample_bytes / (x_sampling * y_sampling);
	}
	// in double, because the product overflows every integer type long before
	// it becomes implausible, and no precision is needed at this magnitude
	const double raw_bytes = (double)width * (double)height * bytes_per_pixel;
	if(raw_bytes > (double)stream_bytes * FI_EXR_MAX_COMPRESSION_RATIO) {
		THROW (Iex::InputExc, "Invalid data window: the header describes " << width << " x " << height
			<< " pixels, i.e. " << (INT64)(raw_bytes / 1048576.0) << " MB of pixel data, which a file of "
			<< stream_bytes << " bytes cannot hold");
	}
}

// --------------------------------------------------------------------------

static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
	bool bUseRgbaInterface = false;
	FIBITMAP *dib = NULL;	

	if(!handle) {
		return NULL;
	}

	try {
		BOOL header_only = (flags & FIF_LOAD_NOPIXELS) == FIF_LOAD_NOPIXELS;

		// save the stream starting point
		const long stream_start = io->tell_proc(handle);

		// measure the stream, so that CheckDataWindow below can tell whether the
		// file is big enough to hold the picture its header describes.  Zero means
		// the size could not be established - a handle whose tell_proc is a 32-bit
		// long, for instance - and the checks are then skipped rather than guessed.
		long stream_bytes = 0;
		if(io->seek_proc(handle, 0, SEEK_END) == 0) {
			const long stream_end = io->tell_proc(handle);
			if(stream_end > stream_start) {
				stream_bytes = stream_end - stream_start;
			}
		}
		io->seek_proc(handle, stream_start, SEEK_SET);

		// wrap the FreeImage IO stream
		C_IStream istream(io, handle);

		// open the file
		Imf::InputFile file(istream);

		// get file info			
		const Imath::Box2i &dataWindow = file.header().dataWindow();
		// the difference of two ints does not fit an int, so widen before subtracting:
		// OpenEXR only guarantees the corners are within +/- INT_MAX/2
		const INT64 window_width  = (INT64)dataWindow.max.x - (INT64)dataWindow.min.x + 1;
		const INT64 window_height = (INT64)dataWindow.max.y - (INT64)dataWindow.min.y + 1;
		if((window_width <= 0) || (window_height <= 0) || (window_width > INT_MAX) || (window_height > INT_MAX)) {
			THROW (Iex::InputExc, "Invalid data window: " << window_width << " x " << window_height << " pixels");
		}
		const int width  = (int)window_width;
		const int height = (int)window_height;

		//const Imf::Compression &compression = file.header().compression();

		const Imf::ChannelList &channels = file.header().channels();

		// check the number of components and check for a coherent format

		std::string exr_color_model;
		Imf::PixelType pixel_type = Imf::HALF;
		FREE_IMAGE_TYPE image_type = FIT_UNKNOWN;
		int components = 0;
		bool bMixedComponents = false;

		for (Imf::ChannelList::ConstIterator i = channels.begin(); i != channels.end(); ++i) {
			components++;
			if(components == 1) {
				exr_color_model += i.name();
				pixel_type = i.channel().type;
			} else {
				exr_color_model += "/";
				exr_color_model += i.name();
				if (i.channel().type != pixel_type) {
					bMixedComponents = true;
				}
			}
		}

		if(bMixedComponents) {
			bool bHandled = false;
			// we may have a RGBZ or RGBAZ image ... 
			if(components > 4) {
				if(channels.findChannel("R") && channels.findChannel("G") && channels.findChannel("B") && channels.findChannel("A")) {
					std::string msg = "Warning: converting color model " + exr_color_model + " to RGBA color model";
					FreeImage_OutputMessageProc(s_format_id, msg.c_str());
					bHandled = true;
				}
			}
			else if(components > 3) {
				if(channels.findChannel("R") && channels.findChannel("G") && channels.findChannel("B")) {
					std::string msg = "Warning: converting color model " + exr_color_model + " to RGB color model";
					FreeImage_OutputMessageProc(s_format_id, msg.c_str());
					bHandled = true;
				}
			}
			if(!bHandled) {
				THROW (Iex::InputExc, "Unable to handle mixed component types (color model = " << exr_color_model << ")");
			} 
		}

		switch(pixel_type) {
			case Imf::UINT:
				THROW (Iex::InputExc, "Unsupported format: UINT");
				break;
			case Imf::HALF:
			case Imf::FLOAT:
			default:
				break;
		}

		// check for supported image color models
		// --------------------------------------------------------------

		if(channels.findChannel("Y") && channels.findChannel("BY") && channels.findChannel("RY")) {
			// Luminance and chroma, the chroma normally subsampled: Y/BY/RY, or
			// A/BY/RY/Y once the image has an alpha channel - which is exactly what
			// SaveAsEXR_LC writes for a RGBAF image (Imf::WRITE_YCA).  Only
			// Imf::RgbaInputFile puts RGB back together out of these, so the low
			// level interface further down is not used for them.
			//
			// Recognised by channel name rather than by channel count: until
			// 2026-09-15 only the three channel form was, and the four channel one
			// fell through to "Unsupported color model: A/BY/RY/Y" - so FreeImage
			// refused to read back the files its own EXR_LC flag had written.
			bUseRgbaInterface = true;
			if(channels.findChannel("A")) {
				image_type = FIT_RGBAF;
				components = 4;
			} else {
				image_type = FIT_RGBF;
				components = 3;
			}
		} else if((components == 1) || (components == 2)) {				
			// if the image is gray-alpha (YA), ignore the alpha channel
			if((components == 1) && channels.findChannel("Y")) {
				image_type = FIT_FLOAT;
				components = 1;
			} else {
				std::string msg = "Warning: loading color model " + exr_color_model + " as Y color model";
				FreeImage_OutputMessageProc(s_format_id, msg.c_str());
				image_type = FIT_FLOAT;
				// ignore the other channel
				components = 1;
			}
		} else if(components == 3) {
			if(channels.findChannel("R") && channels.findChannel("G") && channels.findChannel("B")) {
				image_type = FIT_RGBF;
			}
		} else if(components >= 4) {
			if(channels.findChannel("R") && channels.findChannel("G") && channels.findChannel("B")) {
				if(channels.findChannel("A")) {
					if(components > 4) {
						std::string msg = "Warning: converting color model " + exr_color_model + " to RGBA color model";
						FreeImage_OutputMessageProc(s_format_id, msg.c_str());
					}
					image_type = FIT_RGBAF;
					// ignore other layers if there is more than one alpha layer
					components = 4;
				} else {
					std::string msg = "Warning: converting color model " + exr_color_model + " to RGB color model";
					FreeImage_OutputMessageProc(s_format_id, msg.c_str());

					image_type = FIT_RGBF;
					// ignore other channels
					components = 3;					
				}
			}
		}

		if(image_type == FIT_UNKNOWN) {
			THROW (Iex::InputExc, "Unsupported color model: " << exr_color_model);
		}

		// the data window is only a claim until it has been checked against the file
		CheckDataWindow(file.header(), width, height, stream_bytes);

		// allocate a new dib
		dib = FreeImage_AllocateHeaderT(header_only, image_type, width, height, 0);
		if(!dib) THROW (Iex::NullExc, FI_MSG_ERROR_MEMORY);

		// try to load the preview image
		// --------------------------------------------------------------

		if(file.header().hasPreviewImage()) {
			const Imf::PreviewImage& preview = file.header().previewImage();
			const unsigned thWidth = preview.width();
			const unsigned thHeight = preview.height();
			
			FIBITMAP* thumbnail = FreeImage_Allocate(thWidth, thHeight, 32);
			if(thumbnail) {
				const Imf::PreviewRgba *src_line = preview.pixels();
				BYTE *dst_line = FreeImage_GetScanLine(thumbnail, thHeight - 1);
				const unsigned dstPitch = FreeImage_GetPitch(thumbnail);
				
				for (unsigned y = 0; y < thHeight; ++y) {
					const Imf::PreviewRgba *src_pixel = src_line;
					RGBQUAD* dst_pixel = (RGBQUAD*)dst_line;
					
					for(unsigned x = 0; x < thWidth; ++x) {
						dst_pixel->rgbRed = src_pixel->r;
						dst_pixel->rgbGreen = src_pixel->g;
						dst_pixel->rgbBlue = src_pixel->b;
						dst_pixel->rgbReserved = src_pixel->a;				
						src_pixel++;
						dst_pixel++;
					}
					src_line += thWidth;
					dst_line -= dstPitch;
				}
				FreeImage_SetThumbnail(dib, thumbnail);
				FreeImage_Unload(thumbnail);
			}
		}

		if(header_only) {
			// header only mode
			return dib;
		}

		// load pixels
		// --------------------------------------------------------------

		const BYTE *bits = FreeImage_GetBits(dib);			// pointer to our pixel buffer
		const size_t bytespp = sizeof(float) * components;	// size of our pixel in bytes
		const unsigned pitch = FreeImage_GetPitch(dib);		// size of our yStride in bytes

		Imf::PixelType pixelType = Imf::FLOAT;	// load as float data type;
		
		if(bUseRgbaInterface) {
			// use the RGBA interface (used when loading RY BY Y images )

			const int chunk_size = 16;

			BYTE *scanline = (BYTE*)bits;

			// re-open using the RGBA interface
			io->seek_proc(handle, stream_start, SEEK_SET);
			Imf::RgbaInputFile rgbaFile(istream);

			// read the file in chunks
			Imath::Box2i dw = dataWindow;
			Imf::Array2D<Imf::Rgba> chunk(chunk_size, width);
			while (dw.min.y <= dw.max.y) {
				// how many rows this pass covers: the last chunk is a short one.
				// Until 2026-09-15 the copy below ran to (dw.max.y - dw.min.y), one
				// row short of the (dw.max.y - dw.min.y + 1) that were read, so the
				// bottom scanline of every Y/BY/RY image was left as the zeros
				// FreeImage_AllocateHeaderT had cleared it to.
				const int rows = MIN(chunk_size, dw.max.y - dw.min.y + 1);
				// read a chunk
				rgbaFile.setFrameBuffer (&chunk[0][0] - dw.min.x - dw.min.y * width, 1, width);
				rgbaFile.readPixels (dw.min.y, dw.min.y + rows - 1);
				// fill the dib
				for(int y = 0; y < rows; y++) {
					const Imf::Rgba *half_rgba = chunk[y];
					if(image_type == FIT_RGBAF) {
						FIRGBAF *pixel = (FIRGBAF*)scanline;
						for(int x = 0; x < width; x++) {
							// convert from half to float
							pixel[x].red = half_rgba[x].r;
							pixel[x].green = half_rgba[x].g;
							pixel[x].blue = half_rgba[x].b;
							pixel[x].alpha = half_rgba[x].a;
						}
					} else {
						FIRGBF *pixel = (FIRGBF*)scanline;
						for(int x = 0; x < width; x++) {
							// convert from half to float
							pixel[x].red = half_rgba[x].r;
							pixel[x].green = half_rgba[x].g;
							pixel[x].blue = half_rgba[x].b;
						}
					}
					// next line
					scanline += pitch;
				}
				// next chunk
				dw.min.y += rows;
			}

		} else {
			// use the low level interface
			// build a frame buffer (i.e. what we want on output)
			Imf::FrameBuffer frameBuffer;
			if(components == 1) {
				frameBuffer.insert ("Y",			// name
					Imf::Slice::Make(pixelType,	// type
					bits,									// base
					dataWindow,							// data window
					bytespp,								// xStride
					pitch,								// yStride
					1, 1,									// x/y sampling
					0.0));								// fillValue
			} else if((components == 3) || (components == 4)) {

				const char *channel_name[4] = { "R", "G", "B", "A" };
				for(int c = 0; c < components; c++) {
					frameBuffer.insert (
						channel_name[c],					// name
						Imf::Slice::Make(pixelType,	// type
						bits + c * sizeof(float),		// base
						dataWindow,							// data window
						bytespp,								// xStride
						pitch,								// yStride
						1, 1,									// x/y sampling
						0.0));								// fillValue
				}
			}

			// read the file
			file.setFrameBuffer(frameBuffer);
			file.readPixels(dataWindow.min.y, dataWindow.max.y);
		}

		// lastly, flip dib lines
		FreeImage_FlipVertical(dib);

	}
	catch(Iex::BaseExc & e) {
		if(dib != NULL) {
			FreeImage_Unload(dib);
		}
		FreeImage_OutputMessageProc(s_format_id, e.what());
		return NULL;
	}

	return dib;
}

/**
Set the preview image using the dib embedded thumbnail
*/
static BOOL
SetPreviewImage(FIBITMAP *dib, Imf::Header& header) {
	if(!FreeImage_GetThumbnail(dib)) {
		return FALSE;
	}
	FIBITMAP* thumbnail = FreeImage_GetThumbnail(dib);

	if((FreeImage_GetImageType(thumbnail) != FIT_BITMAP) || (FreeImage_GetBPP(thumbnail) != 32)) {
		// invalid thumbnail - ignore it
		FreeImage_OutputMessageProc(s_format_id, FI_MSG_WARNING_INVALID_THUMBNAIL);
	} else {
		const unsigned thWidth = FreeImage_GetWidth(thumbnail);
		const unsigned thHeight = FreeImage_GetHeight(thumbnail);
		
		Imf::PreviewImage preview(thWidth, thHeight);

		// copy thumbnail to 32-bit RGBA preview image
		
		const BYTE* src_line = FreeImage_GetScanLine(thumbnail, thHeight - 1);
		Imf::PreviewRgba* dst_line = preview.pixels();
		const unsigned srcPitch = FreeImage_GetPitch(thumbnail);
		
		for (unsigned y = 0; y < thHeight; y++) {
			const RGBQUAD* src_pixel = (RGBQUAD*)src_line;
			Imf::PreviewRgba* dst_pixel = dst_line;
			
			for(unsigned x = 0; x < thWidth; x++) {
				dst_pixel->r = src_pixel->rgbRed;
				dst_pixel->g = src_pixel->rgbGreen;
				dst_pixel->b = src_pixel->rgbBlue;
				dst_pixel->a = src_pixel->rgbReserved;
				
				src_pixel++;
				dst_pixel++;
			}
			
			src_line -= srcPitch;
			dst_line += thWidth;
		}
		
		header.setPreviewImage(preview);
	}

	return TRUE;
}

/**
Save using EXR_LC compression (works only with RGB[A]F images)
*/
static BOOL 
SaveAsEXR_LC(C_OStream& ostream, FIBITMAP *dib, Imf::Header& header, int width, int height) {
	int x, y;
	Imf::RgbaChannels rgbaChannels;

	try {

		FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib);

		// convert from float to half
		Imf::Array2D<Imf::Rgba> pixels(height, width);
		switch(image_type) {
			case FIT_RGBF:
				rgbaChannels = Imf::WRITE_YC;
				for(y = 0; y < height; y++) {
					FIRGBF *src_bits = (FIRGBF*)FreeImage_GetScanLine(dib, height - 1 - y);
					for(x = 0; x < width; x++) {
						Imf::Rgba &dst_bits = pixels[y][x];
						dst_bits.r = src_bits[x].red;
						dst_bits.g = src_bits[x].green;
						dst_bits.b = src_bits[x].blue;
					}
				}
				break;
			case FIT_RGBAF:
				rgbaChannels = Imf::WRITE_YCA;
				for(y = 0; y < height; y++) {
					FIRGBAF *src_bits = (FIRGBAF*)FreeImage_GetScanLine(dib, height - 1 - y);
					for(x = 0; x < width; x++) {
						Imf::Rgba &dst_bits = pixels[y][x];
						dst_bits.r = src_bits[x].red;
						dst_bits.g = src_bits[x].green;
						dst_bits.b = src_bits[x].blue;
						dst_bits.a = src_bits[x].alpha;
					}
				}
				break;
			default:
				THROW (Iex::IoExc, "Bad image type");
				break;
		}

		// write the data
		Imf::RgbaOutputFile file(ostream, header, rgbaChannels);
		file.setFrameBuffer (&pixels[0][0], 1, width);
		file.writePixels (height);

		return TRUE;

	} catch(Iex::BaseExc & e) {
		FreeImage_OutputMessageProc(s_format_id, e.what());

		return FALSE;
	}

}

static BOOL DLL_CALLCONV
Save(FreeImageIO *io, FIBITMAP *dib, fi_handle handle, int page, int flags, void *data) {
	const char *channel_name[4] = { "R", "G", "B", "A" };
	BOOL bIsFlipped = FALSE;
	half *halfData = NULL;

	if(!dib || !handle) return FALSE;

	try {
		// check for EXR_LC compression and verify that the format is RGB
		if((flags & EXR_LC) == EXR_LC) {
			FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib);
			if(((image_type != FIT_RGBF) && (image_type != FIT_RGBAF)) || ((flags & EXR_FLOAT) == EXR_FLOAT)) {
				THROW (Iex::IoExc, "EXR_LC compression is only available with RGB[A]F images");
			}
			if((FreeImage_GetWidth(dib) % 2) || (FreeImage_GetHeight(dib) % 2)) {
				THROW (Iex::IoExc, "EXR_LC compression only works when the width and height are a multiple of 2");
			}
		}

		// wrap the FreeImage IO stream
		C_OStream ostream(io, handle);

		// compression
		Imf::Compression compress;
		if((flags & EXR_NONE) == EXR_NONE) {
			// no compression
			compress = Imf::NO_COMPRESSION;
		} else if((flags & EXR_ZIP) == EXR_ZIP) {
			// zlib compression, in blocks of 16 scan lines
			compress = Imf::ZIP_COMPRESSION;
		} else if((flags & EXR_PIZ) == EXR_PIZ) {
			// piz-based wavelet compression
			compress = Imf::PIZ_COMPRESSION;
		} else if((flags & EXR_PXR24) == EXR_PXR24) {
			// lossy 24-bit float compression
			compress = Imf::PXR24_COMPRESSION;
		} else if((flags & EXR_B44) == EXR_B44) {
			// lossy 44% float compression
			compress = Imf::B44_COMPRESSION;
		} else {
			// default value
			compress = Imf::PIZ_COMPRESSION;
		}

		// create the header
		int width  = FreeImage_GetWidth(dib);
		int height = FreeImage_GetHeight(dib);
		int dx = 0, dy = 0;

		Imath::Box2i dataWindow (Imath::V2i (0, 0), Imath::V2i (width - 1, height - 1));
		Imath::Box2i displayWindow (Imath::V2i (-dx, -dy), Imath::V2i (width - dx - 1, height - dy - 1));

		Imf::Header header = Imf::Header(displayWindow, dataWindow, 1, 
			Imath::V2f(0,0), 1, 
			Imf::INCREASING_Y, compress);        		

		// handle thumbnail
		SetPreviewImage(dib, header);
		
		// check for EXR_LC compression
		if((flags & EXR_LC) == EXR_LC) {
			return SaveAsEXR_LC(ostream, dib, header, width, height);
		}

		// output pixel type
		Imf::PixelType pixelType;
		if((flags & EXR_FLOAT) == EXR_FLOAT) {
			pixelType = Imf::FLOAT;	// save as float data type
		} else {
			// default value
			pixelType = Imf::HALF;	// save as half data type
		}

		// check the data type and number of channels
		int components = 0;
		FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib);
		switch(image_type) {
			case FIT_FLOAT:
				components = 1;
				// insert luminance channel
				header.channels().insert ("Y", Imf::Channel(pixelType));
				break;
			case FIT_RGBF:
				components = 3;
				for(int c = 0; c < components; c++) {
					// insert R, G and B channels
					header.channels().insert (channel_name[c], Imf::Channel(pixelType));
				}
				break;
			case FIT_RGBAF:
				components = 4;
				for(int c = 0; c < components; c++) {
					// insert R, G, B and A channels
					header.channels().insert (channel_name[c], Imf::Channel(pixelType));
				}
				break;
			default:
				THROW (Iex::ArgExc, "Cannot save: invalid data type.\nConvert the image to float before saving as OpenEXR.");
		}

		// build a frame buffer (i.e. what we have on input)
		Imf::FrameBuffer frameBuffer;

		BYTE *bits = NULL;	// pointer to our pixel buffer
		size_t bytespp = 0;	// size of our pixel in bytes
		size_t bytespc = 0;	// size of our pixel component in bytes
		unsigned pitch = 0;	// size of our yStride in bytes


		if(pixelType == Imf::HALF) {
			// convert from float to half
			halfData = new(std::nothrow) half[width * height * components];
			if(!halfData) {
				THROW (Iex::NullExc, FI_MSG_ERROR_MEMORY);
			}

			for(int y = 0; y < height; y++) {
				float *src_bits = (float*)FreeImage_GetScanLine(dib, height - 1 - y);
				half *dst_bits = halfData + y * width * components;
				for(int x = 0; x < width; x++) {
					for(int c = 0; c < components; c++) {
						dst_bits[c] = src_bits[c];
					}
					src_bits += components;
					dst_bits += components;
				}
			}
			bits = (BYTE*)halfData;
			bytespc = sizeof(half);
			bytespp = sizeof(half) * components;
			pitch = sizeof(half) * width * components;
		} else if(pixelType == Imf::FLOAT) {
			// invert dib scanlines
			bIsFlipped = FreeImage_FlipVertical(dib);
		
			bits = FreeImage_GetBits(dib);
			bytespc = sizeof(float);
			bytespp = sizeof(float) * components;
			pitch = FreeImage_GetPitch(dib);
		}

		if(image_type == FIT_FLOAT) {
			frameBuffer.insert ("Y",	// name
				Imf::Slice (pixelType,	// type
				(char*)(bits),			// base
				bytespp,				// xStride
				pitch));				// yStride
		} else if((image_type == FIT_RGBF) || (image_type == FIT_RGBAF)) {			
			for(int c = 0; c < components; c++) {
				char *channel_base = (char*)(bits) + c*bytespc;
				frameBuffer.insert (channel_name[c],// name
					Imf::Slice (pixelType,			// type
					channel_base,					// base
					bytespp,	// xStride
					pitch));	// yStride
			}
		}

		// write the data
		Imf::OutputFile file (ostream, header);
		file.setFrameBuffer (frameBuffer);
		file.writePixels (height);

		if(halfData != NULL) {
			delete[] halfData;
		}
		if(bIsFlipped) {
			// invert dib scanlines
			FreeImage_FlipVertical(dib);
		}

		return TRUE;

	} catch(Iex::BaseExc & e) {
		if(halfData != NULL) {
			delete[] halfData;
		}
		if(bIsFlipped) {
			// invert dib scanlines
			FreeImage_FlipVertical(dib);
		}

		FreeImage_OutputMessageProc(s_format_id, e.what());

		return FALSE;
	}	
}

// ==========================================================
//   Init
// ==========================================================

void DLL_CALLCONV
InitEXR(Plugin *plugin, int format_id) {
	s_format_id = format_id;

	// initialize the OpenEXR library
	// note that this OpenEXR function produce so called "false memory leaks"
	// see http://lists.nongnu.org/archive/html/openexr-devel/2013-11/msg00000.html
	Imf::staticInitialize();

	plugin->format_proc = Format;
	plugin->description_proc = Description;
	plugin->extension_proc = Extension;
	plugin->regexpr_proc = RegExpr;
	plugin->open_proc = NULL;
	plugin->close_proc = NULL;
	plugin->pagecount_proc = NULL;
	plugin->pagecapability_proc = NULL;
	plugin->load_proc = Load;
	plugin->save_proc = Save;
	plugin->validate_proc = Validate;
	plugin->mime_proc = MimeType;
	plugin->supports_export_bpp_proc = SupportsExportDepth;
	plugin->supports_export_type_proc = SupportsExportType;
	plugin->supports_icc_profiles_proc = NULL;
	plugin->supports_no_pixels_proc = SupportsNoPixels;
}
