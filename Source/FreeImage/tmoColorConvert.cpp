// ==========================================================
// High Dynamic Range bitmap conversion routines
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
#include "ToneMapping.h"

// ----------------------------------------------------------
// Convert RGB to and from Yxy, same as in Reinhard et al. SIGGRAPH 2002
// References : 
// [1] Radiance Home Page [Online] http://radsite.lbl.gov/radiance/HOME.html
// [2] E. Reinhard, M. Stark, P. Shirley, and J. Ferwerda,  
//     Photographic Tone Reproduction for Digital Images, ACM Transactions on Graphics, 
//     21(3):267-276, 2002 (Proceedings of SIGGRAPH 2002). 
// [3] J. Tumblin and H.E. Rushmeier, 
//     Tone Reproduction for Realistic Images. IEEE Computer Graphics and Applications, 
//     13(6):42-48, 1993.
// ----------------------------------------------------------

/**
nominal CRT primaries 
*/
/*
static const float CIE_x_r = 0.640F;
static const float CIE_y_r = 0.330F;
static const float CIE_x_g = 0.290F;
static const float CIE_y_g = 0.600F;
static const float CIE_x_b = 0.150F;
static const float CIE_y_b = 0.060F;
static const float CIE_x_w = 0.3333F;	// use true white
static const float CIE_y_w = 0.3333F;
*/
/**
sRGB primaries
*/
static const float CIE_x_r = 0.640F;
static const float CIE_y_r = 0.330F;
static const float CIE_x_g = 0.300F;
static const float CIE_y_g = 0.600F;
static const float CIE_x_b = 0.150F;
static const float CIE_y_b = 0.060F;
static const float CIE_x_w = 0.3127F;	// Illuminant D65
static const float CIE_y_w = 0.3290F;

static const float CIE_D = ( CIE_x_r*(CIE_y_g - CIE_y_b) + CIE_x_g*(CIE_y_b - CIE_y_r) + CIE_x_b*(CIE_y_r - CIE_y_g) );
static const float CIE_C_rD = ( (1/CIE_y_w) * ( CIE_x_w*(CIE_y_g - CIE_y_b) - CIE_y_w*(CIE_x_g - CIE_x_b) + CIE_x_g*CIE_y_b - CIE_x_b*CIE_y_g) );
static const float CIE_C_gD = ( (1/CIE_y_w) * ( CIE_x_w*(CIE_y_b - CIE_y_r) - CIE_y_w*(CIE_x_b - CIE_x_r) - CIE_x_r*CIE_y_b + CIE_x_b*CIE_y_r) );
static const float CIE_C_bD = ( (1/CIE_y_w) * ( CIE_x_w*(CIE_y_r - CIE_y_g) - CIE_y_w*(CIE_x_r - CIE_x_g) + CIE_x_r*CIE_y_g - CIE_x_g*CIE_y_r) );

/**
RGB to XYZ (no white balance)
*/
static const float  RGB2XYZ[3][3] = {
	{ CIE_x_r*CIE_C_rD / CIE_D, 
	  CIE_x_g*CIE_C_gD / CIE_D, 
	  CIE_x_b*CIE_C_bD / CIE_D 
	},
	{ CIE_y_r*CIE_C_rD / CIE_D, 
	  CIE_y_g*CIE_C_gD / CIE_D, 
	  CIE_y_b*CIE_C_bD / CIE_D 
	},
	{ (1 - CIE_x_r-CIE_y_r)*CIE_C_rD / CIE_D,
	  (1 - CIE_x_g-CIE_y_g)*CIE_C_gD / CIE_D,
	  (1 - CIE_x_b-CIE_y_b)*CIE_C_bD / CIE_D
	}
};

/**
XYZ to RGB (no white balance)
*/
static const float  XYZ2RGB[3][3] = {
	{(CIE_y_g - CIE_y_b - CIE_x_b*CIE_y_g + CIE_y_b*CIE_x_g) / CIE_C_rD,
	 (CIE_x_b - CIE_x_g - CIE_x_b*CIE_y_g + CIE_x_g*CIE_y_b) / CIE_C_rD,
	 (CIE_x_g*CIE_y_b - CIE_x_b*CIE_y_g) / CIE_C_rD
	},
	{(CIE_y_b - CIE_y_r - CIE_y_b*CIE_x_r + CIE_y_r*CIE_x_b) / CIE_C_gD,
	 (CIE_x_r - CIE_x_b - CIE_x_r*CIE_y_b + CIE_x_b*CIE_y_r) / CIE_C_gD,
	 (CIE_x_b*CIE_y_r - CIE_x_r*CIE_y_b) / CIE_C_gD
	},
	{(CIE_y_r - CIE_y_g - CIE_y_r*CIE_x_g + CIE_y_g*CIE_x_r) / CIE_C_bD,
	 (CIE_x_g - CIE_x_r - CIE_x_g*CIE_y_r + CIE_x_r*CIE_y_g) / CIE_C_bD,
	 (CIE_x_r*CIE_y_g - CIE_x_g*CIE_y_r) / CIE_C_bD
	}
};

/**
This gives approximately the following matrices : 

static const float RGB2XYZ[3][3] = { 
	{ 0.41239083F, 0.35758433F, 0.18048081F },
	{ 0.21263903F, 0.71516865F, 0.072192319F },
	{ 0.019330820F, 0.11919473F, 0.95053220F }
};
static const float XYZ2RGB[3][3] = { 
	{ 3.2409699F, -1.5373832F, -0.49861079F },
	{ -0.96924376F, 1.8759676F, 0.041555084F },
	{ 0.055630036F, -0.20397687F, 1.0569715F }
};
*/

// ----------------------------------------------------------

static const float EPSILON = 1e-06F;
static const float INF = 1e+10F;

/**
Convert in-place floating point RGB data to Yxy.<br>
On output, pixel->red == Y, pixel->green == x, pixel->blue == y
@param dib Input RGBF / Output Yxy image
@return Returns TRUE if successful, returns FALSE otherwise
*/
BOOL 
ConvertInPlaceRGBFToYxy(FIBITMAP *dib) {
	float result[3];

	if(FreeImage_GetImageType(dib) != FIT_RGBF)
		return FALSE;

	const unsigned width  = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	const unsigned pitch  = FreeImage_GetPitch(dib);
	
	BYTE *bits = (BYTE*)FreeImage_GetBits(dib);
	for(unsigned y = 0; y < height; y++) {
		FIRGBF *pixel = (FIRGBF*)bits;
		for(unsigned x = 0; x < width; x++) {
			result[0] = result[1] = result[2] = 0;
			for (int i = 0; i < 3; i++) {
				result[i] += RGB2XYZ[i][0] * pixel[x].red;
				result[i] += RGB2XYZ[i][1] * pixel[x].green;
				result[i] += RGB2XYZ[i][2] * pixel[x].blue;
			}
			const float W = result[0] + result[1] + result[2];
			const float Y = result[1];
			if(W > 0) { 
				pixel[x].red   = Y;			    // Y 
				pixel[x].green = result[0] / W;	// x 
				pixel[x].blue  = result[1] / W;	// y 	
			} else {
				pixel[x].red = pixel[x].green = pixel[x].blue = 0;
			}
		}
		// next line
		bits += pitch;
	}

	return TRUE;
}

/**
Convert in-place Yxy image to floating point RGB data.<br>
On input, pixel->red == Y, pixel->green == x, pixel->blue == y
@param dib Input Yxy / Output RGBF image
@return Returns TRUE if successful, returns FALSE otherwise
*/
BOOL 
ConvertInPlaceYxyToRGBF(FIBITMAP *dib) {
	float result[3];
	float X, Y, Z;

	if(FreeImage_GetImageType(dib) != FIT_RGBF)
		return FALSE;

	const unsigned width  = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	const unsigned pitch  = FreeImage_GetPitch(dib);

	BYTE *bits = (BYTE*)FreeImage_GetBits(dib);
	for(unsigned y = 0; y < height; y++) {
		FIRGBF *pixel = (FIRGBF*)bits;
		for(unsigned x = 0; x < width; x++) {
			Y = pixel[x].red;	        // Y 
			result[1] = pixel[x].green;	// x 
			result[2] = pixel[x].blue;	// y 
			if ((Y > EPSILON) && (result[1] > EPSILON) && (result[2] > EPSILON)) {
				X = (result[1] * Y) / result[2];
				Z = (X / result[1]) - X - Y;
			} else {
				X = Z = EPSILON;
			}
			pixel[x].red   = X;
			pixel[x].green = Y;
			pixel[x].blue  = Z;
			result[0] = result[1] = result[2] = 0;
			for (int i = 0; i < 3; i++) {
				result[i] += XYZ2RGB[i][0] * pixel[x].red;
				result[i] += XYZ2RGB[i][1] * pixel[x].green;
				result[i] += XYZ2RGB[i][2] * pixel[x].blue;
			}
			pixel[x].red   = result[0];	// R
			pixel[x].green = result[1];	// G
			pixel[x].blue  = result[2];	// B
		}
		// next line
		bits += pitch;
	}

	return TRUE;
}

// ----------------------------------------------------------
// Robust luminance statistics (finite samples, percentile extrema)
// ----------------------------------------------------------

#define TMO_HISTOGRAM_BINS	4096

/** the brightest 0.1% are outliers */
#define TMO_MAX_PERCENTILE	0.999

/** the darkest 0.1% are outliers */
#define TMO_MIN_PERCENTILE	0.001

/** [0..1] float to BYTE, clipped; NaN gives 0 (casting it is UB) */
static inline BYTE
ClampFloatToByte(float value) {
	const float scaled = 255.0F * value + 0.5F;
	if(scaled >= 255.0F) return 255;
	if(scaled > 0.0F)    return (BYTE)scaled;
	return 0;	// covers negative values, -INF and NaN
}

/** luminance statistics over the finite samples */
typedef struct tagLuminanceStats {
	float maxLum;		//! percentile ("robust") maximum luminance
	float minLumRobust;	//! percentile ("robust") minimum luminance
	float trueMaxLum;	//! absolute maximum luminance
	float minLum;		//! absolute minimum luminance
	float minPosLum;	//! smallest strictly positive luminance
	double sumLum;		//! sum of the luminance samples
	double sumLogLum;	//! sum of log(EPS + luminance)
	size_t count;		//! number of usable samples
} LuminanceStats;

/** luminance stats of samples 'stride' floats apart (3: the Y of Yxy) */
static BOOL
GatherLuminanceStats(FIBITMAP *dib, unsigned samplesPerRow, unsigned stride, BOOL clampNegative, LuminanceStats *stats) {
	const unsigned width  = samplesPerRow;
	const unsigned height = FreeImage_GetHeight(dib);
	const unsigned pitch  = FreeImage_GetPitch(dib);

	memset(stats, 0, sizeof(LuminanceStats));

	float max_lum = -FLT_MAX, min_lum = FLT_MAX, min_pos_lum = FLT_MAX;
	double sum_lum = 0, sum_log_lum = 0;
	size_t count = 0, positive_count = 0;

	// first pass : min / max / sums, skipping non finite samples

	BYTE *bits = (BYTE*)FreeImage_GetBits(dib);
	for(unsigned y = 0; y < height; y++) {
		const float *pixel = (float*)bits;
		for(unsigned x = 0; x < width; x++) {
			const float sample = pixel[x * stride];
			if(!IsFiniteValue(sample)) {
				continue;
			}
			const float Y = (clampNegative && (sample < 0)) ? 0 : sample;

			if(Y > max_lum) max_lum = Y;
			if(Y < min_lum) min_lum = Y;
			if(Y > 0) {
				if(Y < min_pos_lum) min_pos_lum = Y;
				positive_count++;
			}
			// negative radiance is not light, and its log() is NaN
			const float lit = (Y > 0) ? Y : 0;
			sum_lum += lit;
			sum_log_lum += log(2.3e-5F + lit);	// contrast constant in Tumblin paper
			count++;
		}
		// next line
		bits += pitch;
	}

	if(count == 0) {
		// the image holds nothing but NaN / INF
		return FALSE;
	}

	stats->trueMaxLum   = max_lum;
	stats->minLum       = min_lum;
	stats->minPosLum    = (positive_count > 0) ? min_pos_lum : 0;
	stats->sumLum       = sum_lum;
	stats->sumLogLum    = sum_log_lum;
	stats->count        = count;
	stats->maxLum       = max_lum;
	stats->minLumRobust = min_lum;

	// second pass : percentile estimates

	if((positive_count < 2) || (stats->minPosLum >= max_lum)) {
		// nothing to reject
		return TRUE;
	}

	const double log_min = log((double)stats->minPosLum);
	const double log_max = log((double)max_lum);
	const double log_range = log_max - log_min;
	if(!(log_range > 0)) {
		return TRUE;
	}

	// log-spaced bins for positive samples, linear ones over [min_lum, 0]
	unsigned *histogram = (unsigned*)calloc(2 * TMO_HISTOGRAM_BINS, sizeof(unsigned));
	if(!histogram) {
		// out of memory : fall back on the absolute extrema
		return TRUE;
	}
	unsigned *neg_histogram = histogram + TMO_HISTOGRAM_BINS;
	const double neg_range = (min_lum < 0) ? -(double)min_lum : 0;

	bits = (BYTE*)FreeImage_GetBits(dib);
	for(unsigned y = 0; y < height; y++) {
		const float *pixel = (float*)bits;
		for(unsigned x = 0; x < width; x++) {
			const float raw = pixel[x * stride];
			if(!IsFiniteValue(raw)) {
				continue;
			}
			const float sample = (clampNegative && (raw < 0)) ? 0 : raw;
			if(sample > 0) {
				int bin = (int)(((log((double)sample) - log_min) / log_range) * (TMO_HISTOGRAM_BINS - 1) + 0.5);
				if(bin < 0) bin = 0;
				if(bin > TMO_HISTOGRAM_BINS - 1) bin = TMO_HISTOGRAM_BINS - 1;
				histogram[bin]++;
			} else if(neg_range > 0) {
				// bin 0 holds min_lum, the last bin holds the samples at 0
				int bin = (int)((((double)sample - (double)min_lum) / neg_range) * (TMO_HISTOGRAM_BINS - 1) + 0.5);
				if(bin < 0) bin = 0;
				if(bin > TMO_HISTOGRAM_BINS - 1) bin = TMO_HISTOGRAM_BINS - 1;
				neg_histogram[bin]++;
			}
		}
		// next line
		bits += pitch;
	}

	// every non positive sample sorts below every positive one
	const size_t non_positive = count - positive_count;

	// walk down from the brightest bin until the outlier budget is spent

	{
		// budget from lit samples only: black backdrops would inflate it
		const size_t budget = (size_t)((1.0 - TMO_MAX_PERCENTILE) * (double)positive_count);
		size_t accumulated = 0;
		int cut_bin = TMO_HISTOGRAM_BINS - 1;
		BOOL spent = FALSE;
		for(int bin = TMO_HISTOGRAM_BINS - 1; bin >= 0; bin--) {
			accumulated += histogram[bin];
			cut_bin = bin;
			if(accumulated > budget) {
				spent = TRUE;
				break;
			}
		}
		if(!spent) {
			// budget not spent: nothing to reject
			stats->maxLum = max_lum;
		} else {
			// upper edge of the bin that holds the percentile
			const double cut = exp(log_min + ((double)(cut_bin + 1) / (double)(TMO_HISTOGRAM_BINS - 1)) * log_range);
			float robust_max = (float)((cut < (double)max_lum) ? cut : (double)max_lum);
			if(!IsFiniteValue(robust_max) || (robust_max <= stats->minPosLum)) {
				// degenerate estimate : keep the absolute maximum
				robust_max = max_lum;
			}
			stats->maxLum = robust_max;
		}
	}

	// and up from the darkest bin

	{
		const size_t budget = (size_t)(TMO_MIN_PERCENTILE * (double)count);
		float robust_min = min_lum;

		if(non_positive > budget) {
			// the percentile lies among the samples at or below zero
			if(neg_range > 0) {
				size_t accumulated = 0;
				int cut_bin = 0;
				for(int bin = 0; bin < TMO_HISTOGRAM_BINS; bin++) {
					accumulated += neg_histogram[bin];
					cut_bin = bin;
					if(accumulated > budget) {
						break;
					}
				}
				robust_min = (float)((double)min_lum + ((double)cut_bin / (double)(TMO_HISTOGRAM_BINS - 1)) * neg_range);
			} else {
				// nothing below zero at all, so the dark end is made of zeros
				robust_min = min_lum;
			}
		} else {
			// it lies among the positive samples
			size_t accumulated = non_positive;
			int cut_bin = 0;
			for(int bin = 0; bin < TMO_HISTOGRAM_BINS; bin++) {
				accumulated += histogram[bin];
				cut_bin = bin;
				if(accumulated > budget) {
					break;
				}
			}
			// lower edge of the bin that holds the percentile
			const double cut = exp(log_min + ((double)cut_bin / (double)(TMO_HISTOGRAM_BINS - 1)) * log_range);
			robust_min = (float)cut;
		}

		if(!IsFiniteValue(robust_min) || (robust_min < min_lum) || (robust_min >= stats->maxLum)) {
			// degenerate estimate : keep the absolute minimum
			robust_min = min_lum;
		}
		stats->minLumRobust = robust_min;
	}

	free(histogram);

	return TRUE;
}

/**
Get the maximum, minimum and average luminance.<br>
On input, pixel->red == Y, pixel->green == x, pixel->blue == y
@param Yxy Source Yxy image to analyze
@param maxLum Maximum luminance
@param minLum Minimum luminance
@param worldLum Average luminance (world adaptation luminance)
@return Returns TRUE if successful, returns FALSE otherwise
*/
BOOL 
LuminanceFromYxy(FIBITMAP *Yxy, float *maxLum, float *minLum, float *worldLum) {
	if(FreeImage_GetImageType(Yxy) != FIT_RGBF)
		return FALSE;

	LuminanceStats stats;

	// Y of Yxy: stride 3, negatives read as 0
	if(!GatherLuminanceStats(Yxy, FreeImage_GetWidth(Yxy), 3, TRUE, &stats)) {
		return FALSE;
	}

	// maximum luminance
	*maxLum = stats.maxLum;
	// minimum luminance
	*minLum = stats.minLum;
	// world adaptation luminance
	*worldLum = (float)exp(stats.sumLogLum / (double)stats.count);

	return TRUE;
}

/**
Clamp RGBF image highest values to display white, 
then convert to 24-bit RGB
*/
FIBITMAP* 
ClampConvertRGBFTo24(FIBITMAP *src) {
	if(FreeImage_GetImageType(src) != FIT_RGBF)
		return FALSE;

	const unsigned width  = FreeImage_GetWidth(src);
	const unsigned height = FreeImage_GetHeight(src);

	FIBITMAP *dst = FreeImage_Allocate(width, height, 24, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	if(!dst) return NULL;

	const unsigned src_pitch  = FreeImage_GetPitch(src);
	const unsigned dst_pitch  = FreeImage_GetPitch(dst);

	BYTE *src_bits = (BYTE*)FreeImage_GetBits(src);
	BYTE *dst_bits = (BYTE*)FreeImage_GetBits(dst);

	for(unsigned y = 0; y < height; y++) {
		const FIRGBF *src_pixel = (FIRGBF*)src_bits;
		BYTE *dst_pixel = (BYTE*)dst_bits;
		for(unsigned x = 0; x < width; x++) {
			// not CLAMP(): it passes NaN, and casting NaN is UB
			dst_pixel[FI_RGBA_RED]   = ClampFloatToByte(src_pixel[x].red);
			dst_pixel[FI_RGBA_GREEN] = ClampFloatToByte(src_pixel[x].green);
			dst_pixel[FI_RGBA_BLUE]  = ClampFloatToByte(src_pixel[x].blue);
			dst_pixel += 3;
		}
		src_bits += src_pitch;
		dst_bits += dst_pitch;
	}

	return dst;
}

/**
Extract the luminance channel L from a RGBF image. 
Luminance is calculated from the sRGB model (RGB2XYZ matrix) 
using a D65 white point : 
L = ( 0.2126 * r ) + ( 0.7152 * g ) + ( 0.0722 * b )
Reference : 
A Standard Default Color Space for the Internet - sRGB. 
[online] http://www.w3.org/Graphics/Color/sRGB
*/
FIBITMAP*  
ConvertRGBFToY(FIBITMAP *src) {
	if(FreeImage_GetImageType(src) != FIT_RGBF)
		return FALSE;

	const unsigned width  = FreeImage_GetWidth(src);
	const unsigned height = FreeImage_GetHeight(src);

	FIBITMAP *dst = FreeImage_AllocateT(FIT_FLOAT, width, height);
	if(!dst) return NULL;

	const unsigned src_pitch  = FreeImage_GetPitch(src);
	const unsigned dst_pitch  = FreeImage_GetPitch(dst);

	
	BYTE *src_bits = (BYTE*)FreeImage_GetBits(src);
	BYTE *dst_bits = (BYTE*)FreeImage_GetBits(dst);

	for(unsigned y = 0; y < height; y++) {
		const FIRGBF *src_pixel = (FIRGBF*)src_bits;
		float  *dst_pixel = (float*)dst_bits;
		for(unsigned x = 0; x < width; x++) {
			const float L = LUMA_REC709(src_pixel[x].red, src_pixel[x].green, src_pixel[x].blue);
			dst_pixel[x] = (L > 0) ? L : 0;
		}
		// next line
		src_bits += src_pitch;
		dst_bits += dst_pitch;
	}

	return dst;
}

/**
Get the maximum, minimum, average luminance and log average luminance from a Y image
@param dib Source Y image to analyze
@param maxLum Maximum luminance
@param minLum Minimum luminance
@param Lav Average luminance
@param Llav Log average luminance (also known as 'world adaptation luminance')
@return Returns TRUE if successful, returns FALSE otherwise
@see ConvertRGBFToY, FreeImage_TmoReinhard05Ex
*/
BOOL 
LuminanceFromY(FIBITMAP *dib, float *maxLum, float *minLum, float *Lav, float *Llav) {
	if(FreeImage_GetImageType(dib) != FIT_FLOAT)
		return FALSE;

	LuminanceStats stats;

	if(!GatherLuminanceStats(dib, FreeImage_GetWidth(dib), 1, FALSE, &stats)) {
		return FALSE;
	}

	// maximum luminance
	*maxLum = stats.maxLum;
	// minimum positive luminance: callers take its log()
	*minLum = (stats.minPosLum > 0) ? stats.minPosLum : stats.maxLum;
	// average luminance
	*Lav = (float)(stats.sumLum / (double)stats.count);
	// average log luminance, a.k.a. world adaptation luminance
	*Llav = (float)exp(stats.sumLogLum / (double)stats.count);

	return TRUE;
}

/**
Luminance range of Y, outliers excluded
@return Returns TRUE if successful, returns FALSE otherwise
*/
BOOL
LuminanceRange(FIBITMAP *Y, float *maxLum, float *minLum) {
	if(FreeImage_GetImageType(Y) != FIT_FLOAT)
		return FALSE;

	LuminanceStats stats;

	if(!GatherLuminanceStats(Y, FreeImage_GetWidth(Y), 1, FALSE, &stats)) {
		return FALSE;
	}
	*maxLum = stats.maxLum;
	*minLum = stats.minLumRobust;

	return TRUE;
}

/**
Clip negative radiance and NaN to zero, in place
@return Returns TRUE if successful, returns FALSE otherwise
*/
BOOL
ClampNegativeRGBF(FIBITMAP *dib) {
	if(FreeImage_GetImageType(dib) != FIT_RGBF)
		return FALSE;

	const unsigned width  = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	const unsigned pitch  = FreeImage_GetPitch(dib);

	BYTE *bits = (BYTE*)FreeImage_GetBits(dib);
	for(unsigned y = 0; y < height; y++) {
		float *pixel = (float*)bits;
		for(unsigned x = 0; x < width * 3; x++) {
			// NaN fails this test too
			if(!(pixel[x] > 0)) {
				pixel[x] = 0;
			}
		}
		// next line
		bits += pitch;
	}

	return TRUE;
}

/**
Robust range of all RGBF samples, outliers excluded
@return Returns TRUE if successful, returns FALSE otherwise
*/
BOOL
RGBFRobustRange(FIBITMAP *dib, float *maxValue, float *minValue) {
	if(FreeImage_GetImageType(dib) != FIT_RGBF)
		return FALSE;

	LuminanceStats stats;

	// three interleaved floats per pixel, all of them scanned
	if(!GatherLuminanceStats(dib, FreeImage_GetWidth(dib) * 3, 1, FALSE, &stats)) {
		return FALSE;
	}
	*maxValue = stats.maxLum;
	*minValue = stats.minLumRobust;

	return TRUE;
}
// --------------------------------------------------------------------------

static void findMaxMinPercentile(FIBITMAP *Y, float minPrct, float *minLum, float maxPrct, float *maxLum) {
	int x, y;
	int width = FreeImage_GetWidth(Y);
	int height = FreeImage_GetHeight(Y);
	int pitch = FreeImage_GetPitch(Y);

	// reserve(), not the sizing constructor: push_back appends
	std::vector<float> vY;
	vY.reserve((size_t)width * height);

	BYTE *bits = (BYTE*)FreeImage_GetBits(Y);
	for(y = 0; y < height; y++) {
		float *pixel = (float*)bits;
		for(x = 0; x < width; x++) {
			if((pixel[x] != 0) && IsFiniteValue(pixel[x])) {
				vY.push_back(pixel[x]);
			}
		}
		bits += pitch;
	}

	if(vY.empty()) {
		*minLum = 0;
		*maxLum = 0;
		return;
	}

	// a percentile of exactly 1 would index one past the end
	size_t min_index = (size_t)(minPrct * vY.size());
	size_t max_index = (size_t)(maxPrct * vY.size());
	if(min_index >= vY.size()) min_index = vY.size() - 1;
	if(max_index >= vY.size()) max_index = vY.size() - 1;

	std::nth_element(vY.begin(), vY.begin() + min_index, vY.end());
	*minLum = vY[min_index];
	std::nth_element(vY.begin(), vY.begin() + max_index, vY.end());
	*maxLum = vY[max_index];
}

/**
Clipping function<br>
Remove any extremely bright and/or extremely dark pixels 
and normalize between 0 and 1. 
@param Y Input/Output image
@param minPrct Minimum percentile
@param maxPrct Maximum percentile
*/
void 
NormalizeY(FIBITMAP *Y, float minPrct, float maxPrct) {
	int x, y;
	float maxLum, minLum;

	if(minPrct > maxPrct) {
		// swap values
		float t = minPrct; minPrct = maxPrct; maxPrct = t;
	}
	if(minPrct < 0) minPrct = 0;
	if(maxPrct > 1) maxPrct = 1;

	int width = FreeImage_GetWidth(Y);
	int height = FreeImage_GetHeight(Y);
	int pitch = FreeImage_GetPitch(Y);

	// find max & min luminance values
	if((minPrct > 0) || (maxPrct < 1)) {
		maxLum = 0, minLum = 0;
		findMaxMinPercentile(Y, minPrct, &minLum, maxPrct, &maxLum);
	} else {
		maxLum = -1e20F, minLum = 1e20F;
		BYTE *bits = (BYTE*)FreeImage_GetBits(Y);
		for(y = 0; y < height; y++) {
			const float *pixel = (float*)bits;
			for(x = 0; x < width; x++) {
				const float value = pixel[x];
				if(!IsFiniteValue(value)) continue;
				maxLum = (maxLum < value) ? value : maxLum;	// max Luminance in the scene
				minLum = (minLum < value) ? minLum : value;	// min Luminance in the scene
			}
			// next line
			bits += pitch;
		}
	}
	if(!(maxLum > minLum)) return;

	// normalize to range 0..1 
	const float divider = maxLum - minLum;
	BYTE *bits = (BYTE*)FreeImage_GetBits(Y);
	for(y = 0; y < height; y++) {
		float *pixel = (float*)bits;
		for(x = 0; x < width; x++) {
			if(!IsFiniteValue(pixel[x])) {
				// neither comparison below would catch a NaN
				pixel[x] = EPSILON;
				continue;
			}
			pixel[x] = (pixel[x] - minLum) / divider;
			if(pixel[x] <= 0) pixel[x] = EPSILON;
			if(pixel[x] > 1) pixel[x] = 1;
		}
		// next line
		bits += pitch;
	}
}
