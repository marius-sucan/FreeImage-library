// ==========================================================
// Upsampling / downsampling classes
//
// Design and implementation by
// - Hervé Drolon (drolon@infonie.fr)
// - Detlev Vendt (detlev.vendt@brillit.de)
// - Carsten Klein (cklein05@users.sourceforge.net)
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

#ifndef _RESIZE_H_
#define _RESIZE_H_

#include "FreeImage.h"
#include "Utilities.h"
#include "Filters.h" 

/**
  Filter weights table.<br>
  This class stores contribution information for an entire line (row or column).
*/
class CWeightsTable
{
/**
  Sampled filter weight table.<br>
  Contribution information for a single pixel
*/
typedef struct {
	/// Normalized weights of neighboring pixels
	double *Weights;
	/// Bounds of source pixels window
	unsigned Left, Right;
} Contribution;

private:
	/// Row (or column) of contribution weights 
	Contribution *m_WeightTable;
	/// Filter window size (of affecting source pixels) 
	unsigned m_WindowSize;
	/// Length of line (no. of rows / cols) 
	unsigned m_LineLength;
	/// Whether every allocation the constructor made succeeded
	BOOL m_bValid;

public:
	/** 
	Constructor<br>
	Allocate and compute the weights table
	@param pFilter Filter used for upsampling or downsampling
	@param uDstSize Length (in pixels) of the destination line buffer
	@param uSrcSize Length (in pixels) of the source line buffer
	*/
	CWeightsTable(CGenericFilter *pFilter, unsigned uDstSize, unsigned uSrcSize);

	/**
	Destructor<br>
	Destroy the weights table
	*/
	~CWeightsTable();

	BOOL isValid() const {
		return m_bValid;
	}

	/** Retrieve a filter weight, given source and destination positions
	@param dst_pos Pixel position in destination line buffer
	@param src_pos Pixel position in source line buffer
	@return Returns the filter weight
	*/
	double getWeight(unsigned dst_pos, unsigned src_pos) {
		return m_WeightTable[dst_pos].Weights[src_pos];
	}

	/** Retrieve left boundary of source line buffer
	@param dst_pos Pixel position in destination line buffer
	@return Returns the left boundary of source line buffer
	*/
	unsigned getLeftBoundary(unsigned dst_pos) {
		return m_WeightTable[dst_pos].Left;
	}

	/** Retrieve right boundary of source line buffer
	@param dst_pos Pixel position in destination line buffer
	@return Returns the right boundary of source line buffer
	*/
	unsigned getRightBoundary(unsigned dst_pos) {
		return m_WeightTable[dst_pos].Right;
	}
};

// ---------------------------------------------

/**
 CResizeEngine<br>
 This class performs filtered zoom. It scales an image to the desired dimensions with 
 any of the CGenericFilter derived filter class.<br>
 It works with FIT_BITMAP buffers, WORD buffers (FIT_UINT16, FIT_RGB16, FIT_RGBA16),
 float buffers (FIT_FLOAT, FIT_RGBF, FIT_RGBAF) and FIT_INT16, FIT_UINT32, FIT_INT32,
 FIT_DOUBLE and FIT_COMPLEX buffers.<br><br>

 <b>References</b> : <br>
 [1] Paul Heckbert, C code to zoom raster images up or down, with nice filtering. 
 UC Berkeley, August 1989. [online] http://www-2.cs.cmu.edu/afs/cs.cmu.edu/Web/People/ph/heckbert.html
 [2] Eran Yariv, Two Pass Scaling using Filters. The Code Project, December 1999. 
 [online] http://www.codeproject.com/bitmap/2_pass_scaling.asp

*/
class CResizeEngine
{
private:
	/// Pointer to the FIR / IIR filter
	CGenericFilter* m_pFilter;

public:

	/**
	Constructor
	@param filter FIR /IIR filter to be used
	*/
	CResizeEngine(CGenericFilter* filter):m_pFilter(filter) {}

	/// Destructor
	virtual ~CResizeEngine() {}

	/** Scale an image to the desired dimensions.

	src_left, src_top, src_width and src_height define the rectangle of the
	source image to be rescaled, like those of FreeImage_Copy. A resize in one
	direction is a single filtering pass; one in both directions runs the two
	passes over a band of rows at a time, see scaleInBands.

	@param src Pointer to the source image
	@param dst_width Destination image width
	@param dst_height Destination image height
	@param src_left Left boundary of the source rectangle to be scaled
	@param src_top Top boundary of the source rectangle to be scaled
	@param src_width Width of the source rectangle to be scaled
	@param src_height Height of the source rectangle to be scaled
	@return Returns the scaled image if successful, returns NULL otherwise
	*/
   FIBITMAP* scale(FIBITMAP *src, unsigned dst_width, unsigned dst_height, unsigned src_left, unsigned src_top, unsigned src_width, unsigned src_height, unsigned flags, BOOL rawBits, int dst_pitch, BYTE *dst_bits);

private:

	/**
	Resizes in both directions, keeping only a band of the image between the
	two passes: the passes alternate band by band, and the result is the
	same as with the whole image in between

	@param src Source image
	@param src_offset_x Left boundary of the source rectangle
	@param src_offset_y Bottom boundary of the source rectangle, in scanlines
	@param src_width Width of the source rectangle
	@param src_height Height of the source rectangle
	@param src_pal Palette for the first pass, NULL when the pixels are the values
	@param dst Destination image
	@param dst_width Destination image width
	@param dst_height Destination image height
	@param tmp_bpp Bit depth of the image between the passes
	@return Returns TRUE on success, FALSE if a weights table or the band could not be allocated
	*/
	BOOL scaleInBands(FIBITMAP * const src, const unsigned src_offset_x, const unsigned src_offset_y,
			const unsigned src_width, const unsigned src_height, const RGBQUAD * const src_pal,
			FIBITMAP * const dst, const unsigned dst_width, const unsigned dst_height, const unsigned tmp_bpp);

	/**
	Performs horizontal image filtering over a range of rows

	@param weightsTable Weights of the destination columns
	@param src Source image
	@param src_row First source scanline
	@param src_offset_x Left boundary of the source columns
	@param src_pal Source palette, NULL when the pixels are the values
	@param dst Destination image
	@param dst_row First destination scanline
	@param rows Number of rows
	@param dst_width Destination image width
	*/
	void horizontalFilter(CWeightsTable &weightsTable, FIBITMAP * const src, const unsigned src_row,
			const unsigned src_offset_x, const RGBQUAD * const src_pal,
			FIBITMAP * const dst, const unsigned dst_row, const unsigned rows, const unsigned dst_width);

	/**
	Performs vertical image filtering over a range of destination rows

	@param weightsTable Weights of the destination rows
	@param src Source image
	@param src_row_bias Source scanline of the weights' row 0
	@param src_offset_x Left boundary of the source columns
	@param src_pal Source palette, NULL when the pixels are the values
	@param dst Destination image
	@param dst_row_bias Destination scanline of the weights' row 0
	@param y_begin First destination row, as a row of the weights
	@param y_end End of the destination rows, as a row of the weights
	@param width Number of columns
	*/
	void verticalFilter(CWeightsTable &weightsTable, FIBITMAP * const src, const INT64 src_row_bias,
			const unsigned src_offset_x, const RGBQUAD * const src_pal,
			FIBITMAP * const dst, const INT64 dst_row_bias, const unsigned y_begin, const unsigned y_end, const unsigned width);
};

#endif //   _RESIZE_H_
