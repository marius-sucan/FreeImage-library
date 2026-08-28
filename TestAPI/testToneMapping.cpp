// ==========================================================
// FreeImage 3 Test Script
//
// Regression tests for the floating point conversions and for the
// robustness of the tone mapping operators.
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

#include "TestSuite.h"

#include <math.h>
#include <float.h>

// ----------------------------------------------------------
// helpers
// ----------------------------------------------------------

/** Values a HDR pixel may legitimately take, including out of [0..1] ones. */
static const float s_testValues[] = { -2.0F, 0.0F, 0.25F, 1.0F, 5.0F, 15.0F, 50.0F, 1000.0F };
static const unsigned s_testValueCount = 8;

/** Build a one line image holding s_testValues in every colour channel. */
static FIBITMAP* createRamp(FREE_IMAGE_TYPE image_type) {
	FIBITMAP *dib = FreeImage_AllocateT(image_type, s_testValueCount, 1);
	if(!dib) return NULL;

	for(unsigned x = 0; x < s_testValueCount; x++) {
		const float value = s_testValues[x];
		switch(image_type) {
			case FIT_FLOAT:
				((float*)FreeImage_GetScanLine(dib, 0))[x] = value;
				break;
			case FIT_RGBF:
			{
				FIRGBF *pixel = (FIRGBF*)FreeImage_GetScanLine(dib, 0);
				pixel[x].red = pixel[x].green = pixel[x].blue = value;
				break;
			}
			case FIT_RGBAF:
			{
				FIRGBAF *pixel = (FIRGBAF*)FreeImage_GetScanLine(dib, 0);
				pixel[x].red = pixel[x].green = pixel[x].blue = value;
				pixel[x].alpha = 1.0F;
				break;
			}
			default:
				break;
		}
	}
	return dib;
}

/** Read back the red (or only) channel of a one line float image. */
static float sampleOf(FIBITMAP *dib, unsigned x) {
	switch(FreeImage_GetImageType(dib)) {
		case FIT_FLOAT:
			return ((float*)FreeImage_GetScanLine(dib, 0))[x];
		case FIT_RGBF:
			return ((FIRGBF*)FreeImage_GetScanLine(dib, 0))[x].red;
		case FIT_RGBAF:
			return ((FIRGBAF*)FreeImage_GetScanLine(dib, 0))[x].red;
		default:
			return 0;
	}
}

/** A synthetic HDR scene whose luminance sweeps over the given range. */
static FIBITMAP* createScene(FREE_IMAGE_TYPE image_type, unsigned width, unsigned height, double maxLum) {
	FIBITMAP *dib = FreeImage_AllocateT(image_type, width, height);
	if(!dib) return NULL;

	for(unsigned y = 0; y < height; y++) {
		for(unsigned x = 0; x < width; x++) {
			const double t = (double)x / (double)(width - 1);
			const double lum = 0.02 * pow(maxLum / 0.02, t);
			const float r = (float)lum;
			const float g = (float)(lum * 0.90);
			const float b = (float)(lum * 0.80);
			if(image_type == FIT_RGBF) {
				FIRGBF *pixel = (FIRGBF*)FreeImage_GetScanLine(dib, y);
				pixel[x].red = r; pixel[x].green = g; pixel[x].blue = b;
			} else {
				FIRGBAF *pixel = (FIRGBAF*)FreeImage_GetScanLine(dib, y);
				pixel[x].red = r; pixel[x].green = g; pixel[x].blue = b;
				pixel[x].alpha = 1.0F;
			}
		}
	}
	return dib;
}

/** Mean value of a 24-bit image, ignoring the pixel at (0, 0). */
static double meanByte(FIBITMAP *dib) {
	const unsigned width  = FreeImage_GetWidth(dib);
	const unsigned height = FreeImage_GetHeight(dib);
	double sum = 0;
	unsigned count = 0;

	for(unsigned y = 0; y < height; y++) {
		const BYTE *bits = FreeImage_GetScanLine(dib, y);
		for(unsigned x = 0; x < width * 3; x++) {
			if((y == 0) && (x < 3)) continue;	// the pixel that may hold an outlier
			sum += bits[x];
			count++;
		}
	}
	return count ? (sum / count) : 0;
}

/** Largest absolute difference between two 24-bit images of the same size. */
static int maxByteDifference(FIBITMAP *a, FIBITMAP *b) {
	const unsigned width  = FreeImage_GetWidth(a);
	const unsigned height = FreeImage_GetHeight(a);
	int worst = 0;

	for(unsigned y = 0; y < height; y++) {
		const BYTE *pa = FreeImage_GetScanLine(a, y);
		const BYTE *pb = FreeImage_GetScanLine(b, y);
		for(unsigned x = 0; x < width * 3; x++) {
			const int d = abs((int)pa[x] - (int)pb[x]);
			if(d > worst) worst = d;
		}
	}
	return worst;
}

// ----------------------------------------------------------
// tests
// ----------------------------------------------------------

/**
Converting between two floating point formats must preserve the samples:
HDR data legitimately ranges outside of [0..1], and clamping it there (or at any
other fixed bound) silently destroys highlights.
*/
static BOOL testFloatConversionIsLossless() {
	FIBITMAP *src = NULL, *dst = NULL;
	const FREE_IMAGE_TYPE types[3] = { FIT_FLOAT, FIT_RGBF, FIT_RGBAF };

	for(int s = 0; s < 3; s++) {
		for(int d = 0; d < 3; d++) {
			src = createRamp(types[s]);
			if(!src) return FALSE;

			switch(types[d]) {
				case FIT_FLOAT: dst = FreeImage_ConvertToFloat(src); break;
				case FIT_RGBF:  dst = FreeImage_ConvertToRGBF(src);  break;
				default:        dst = FreeImage_ConvertToRGBAF(src); break;
			}
			if(!dst) { FreeImage_Unload(src); return FALSE; }

			for(unsigned x = 0; x < s_testValueCount; x++) {
				const float expected = s_testValues[x];
				const float actual   = sampleOf(dst, x);
				// a grey <-> colour conversion goes through a luminance weighting,
				// which is exact here because all three channels are equal
				if(fabs(actual - expected) > 1e-3 * (fabs(expected) + 1.0)) {
					FreeImage_Unload(src);
					FreeImage_Unload(dst);
					return FALSE;
				}
			}
			FreeImage_Unload(src); src = NULL;
			FreeImage_Unload(dst); dst = NULL;
		}
	}
	return TRUE;
}

/**
The same scene stored as RGBF and as RGBAF must tone map to the same picture:
the alpha channel carries no luminance, so dropping it may not change the result.
*/
static BOOL testToneMappingIgnoresAlpha() {
	const FREE_IMAGE_TMO operators[3] = { FITMO_DRAGO03, FITMO_REINHARD05, FITMO_FATTAL02 };
	const double ranges[3] = { 2.0, 50.0, 2000.0 };

	for(int o = 0; o < 3; o++) {
		for(int r = 0; r < 3; r++) {
			FIBITMAP *rgbf  = createScene(FIT_RGBF,  64, 64, ranges[r]);
			FIBITMAP *rgbaf = createScene(FIT_RGBAF, 64, 64, ranges[r]);
			if(!rgbf || !rgbaf) {
				if(rgbf) FreeImage_Unload(rgbf);
				if(rgbaf) FreeImage_Unload(rgbaf);
				return FALSE;
			}
			FIBITMAP *a = FreeImage_ToneMapping(rgbf,  operators[o]);
			FIBITMAP *b = FreeImage_ToneMapping(rgbaf, operators[o]);
			FreeImage_Unload(rgbf);
			FreeImage_Unload(rgbaf);
			if(!a || !b) {
				if(a) FreeImage_Unload(a);
				if(b) FreeImage_Unload(b);
				return FALSE;
			}
			const int worst = maxByteDifference(a, b);
			FreeImage_Unload(a);
			FreeImage_Unload(b);
			if(worst != 0) {
				return FALSE;
			}
		}
	}
	return TRUE;
}

/**
A single NaN or infinite sample - both of which do occur in HDR files - must not
be allowed to define the scene statistics and blank the whole frame.
*/
static BOOL testToneMappingSurvivesNonFiniteSamples() {
	const FREE_IMAGE_TMO operators[3] = { FITMO_DRAGO03, FITMO_REINHARD05, FITMO_FATTAL02 };
	const float poison[2] = { (float)HUGE_VAL, (float)(HUGE_VAL - HUGE_VAL) };	// +INF, NaN

	for(int o = 0; o < 3; o++) {
		FIBITMAP *clean = createScene(FIT_RGBAF, 64, 64, 200.0);
		if(!clean) return FALSE;
		FIBITMAP *reference = FreeImage_ToneMapping(clean, operators[o]);
		FreeImage_Unload(clean);
		if(!reference) return FALSE;
		const double expected = meanByte(reference);
		FreeImage_Unload(reference);

		for(int p = 0; p < 2; p++) {
			FIBITMAP *dib = createScene(FIT_RGBAF, 64, 64, 200.0);
			if(!dib) return FALSE;
			FIRGBAF *pixel = (FIRGBAF*)FreeImage_GetScanLine(dib, 0);
			pixel[0].red = pixel[0].green = pixel[0].blue = poison[p];

			FIBITMAP *dst = FreeImage_ToneMapping(dib, operators[o]);
			FreeImage_Unload(dib);
			if(!dst) return FALSE;
			const double actual = meanByte(dst);
			FreeImage_Unload(dst);

			// the rest of the picture must still look like the clean render
			if(fabs(actual - expected) > 5.0) {
				return FALSE;
			}
		}
	}
	return TRUE;
}

/**
Converting an out of range float to 8 bits must clip, not wrap: a very bright
sample has to come out white, and casting it straight to an int is undefined.
*/
static BOOL testConvertToStandardTypeClips() {
	FIBITMAP *dib = FreeImage_AllocateT(FIT_FLOAT, 5, 1);
	if(!dib) return FALSE;

	float *bits = (float*)FreeImage_GetScanLine(dib, 0);
	bits[0] = 0.5F;
	bits[1] = 300.0F;
	bits[2] = 1e30F;
	bits[3] = (float)HUGE_VAL;
	bits[4] = -1e30F;

	FIBITMAP *dst = FreeImage_ConvertToStandardType(dib, FALSE);
	FreeImage_Unload(dib);
	if(!dst) return FALSE;

	const BYTE *out = FreeImage_GetScanLine(dst, 0);
	const BOOL ok = (out[0] == 1) && (out[1] == 255) && (out[2] == 255) && (out[3] == 255) && (out[4] == 0);
	FreeImage_Unload(dst);

	return ok;
}

/**
Wide gamut and scene referred HDR sources routinely store negative radiance:
a JPEG XR file can easily hold more negative colour samples than positive ones.
Such a file must still tone map to a usable picture. Reinhard05 in particular
divides by (colour + pow(intensity * adaptation, contrast)), which is a NaN for
a negative adaptation and diverges for a near zero denominator, so a negative
sample used to send the whole frame to white.
*/
static BOOL testToneMappingHandlesNegativeRadiance() {
	const FREE_IMAGE_TMO operators[3] = { FITMO_DRAGO03, FITMO_REINHARD05, FITMO_FATTAL02 };
	const unsigned width = 96, height = 96;

	for(int variant = 0; variant < 2; variant++) {
		for(int o = 0; o < 3; o++) {
			FIBITMAP *dib = FreeImage_AllocateT(FIT_RGBAF, width, height);
			if(!dib) return FALSE;

			// Two shapes taken from real JPEG XR files: a deep negative floor
			// over most of the frame, and channels of mixed sign everywhere.
			// The magnitude matters - a shallow floor does not reproduce.
			for(unsigned y = 0; y < height; y++) {
				FIRGBAF *pixel = (FIRGBAF*)FreeImage_GetScanLine(dib, y);
				for(unsigned x = 0; x < width; x++) {
					const double t = (double)x / (double)(width - 1);
					const float lit = (float)(0.02 * pow(18.0 / 0.02, t));
					if(variant == 0) {
						pixel[x].red   = (t < 0.66) ? -10.6F : lit;
						pixel[x].green = (t < 0.66) ? -10.6F : (float)(lit * 0.9);
						pixel[x].blue  = (t < 0.33) ? -10.6F : (float)(lit * 0.8);
					} else {
						pixel[x].red   = -0.103F;
						pixel[x].green = -0.103F;
						pixel[x].blue  = lit;
					}
					pixel[x].alpha = 1.0F;
				}
			}

			FIBITMAP *dst = FreeImage_ToneMapping(dib, operators[o]);
			FreeImage_Unload(dib);
			if(!dst) return FALSE;

			unsigned white = 0, total = 0;
			for(unsigned y = 0; y < height; y++) {
				const BYTE *bits = FreeImage_GetScanLine(dst, y);
				for(unsigned x = 0; x < width * 3; x++) {
					if(bits[x] == 255) white++;
					total++;
				}
			}
			FreeImage_Unload(dst);

			// a blown out frame is the failure mode this guards against
			if(white * 2 > total) {
				return FALSE;
			}
		}
	}
	return TRUE;
}

/**
Every operator clips negative radiance to zero on its working copy, so an image
that carries negative samples must tone map exactly like the same image with
those samples already clipped. Drago03 is the strict case: its bias function is
pow(x, 0.234), which returns a NaN for a negative x, and its Pade approximation
of log(x+1) has a pole at x = -1.5. Both used to be reachable, and speckled the
result with black pixels - about 1% of the frame on real JPEG XR files.
*/
static BOOL testToneMappingClipsNegativeRadiance() {
	const FREE_IMAGE_TMO operators[3] = { FITMO_DRAGO03, FITMO_REINHARD05, FITMO_FATTAL02 };
	const unsigned width = 64, height = 64;

	for(int o = 0; o < 3; o++) {
		FIBITMAP *raw     = FreeImage_AllocateT(FIT_RGBAF, width, height);
		FIBITMAP *clipped = FreeImage_AllocateT(FIT_RGBAF, width, height);
		if(!raw || !clipped) {
			if(raw) FreeImage_Unload(raw);
			if(clipped) FreeImage_Unload(clipped);
			return FALSE;
		}

		for(unsigned y = 0; y < height; y++) {
			FIRGBAF *pr = (FIRGBAF*)FreeImage_GetScanLine(raw, y);
			FIRGBAF *pc = (FIRGBAF*)FreeImage_GetScanLine(clipped, y);
			for(unsigned x = 0; x < width; x++) {
				const double t = (double)x / (double)(width - 1);
				const double u = (double)y / (double)(height - 1);
				const float lit = (float)(0.02 * pow(18.0 / 0.02, t));
				// mixed signs, including pixels whose *luminance* is negative,
				// which is what reaches Drago03's bias function
				float c[3];
				c[0] = (u < 0.5) ? -0.5F : lit;
				c[1] = (u < 0.7) ? -0.5F : (float)(lit * 0.9);
				c[2] = lit;
				for(int i = 0; i < 3; i++) {
					const float v = c[i];
					((float*)&pr[x])[i] = v;
					((float*)&pc[x])[i] = (v > 0) ? v : 0;
				}
				pr[x].alpha = 1.0F;
				pc[x].alpha = 1.0F;
			}
		}

		FIBITMAP *a = FreeImage_ToneMapping(raw, operators[o]);
		FIBITMAP *b = FreeImage_ToneMapping(clipped, operators[o]);
		FreeImage_Unload(raw);
		FreeImage_Unload(clipped);
		if(!a || !b) {
			if(a) FreeImage_Unload(a);
			if(b) FreeImage_Unload(b);
			return FALSE;
		}
		const int worst = maxByteDifference(a, b);
		FreeImage_Unload(a);
		FreeImage_Unload(b);
		if(worst != 0) {
			return FALSE;
		}
	}
	return TRUE;
}

/**
A flat image carries no gradient and no dynamic range, but it is still a valid
input: every operator must hand back a picture rather than nothing, and a black
scene must come out black rather than as a division by log10(1) = 0.
*/
static BOOL testToneMappingHandlesFlatImages() {
	const FREE_IMAGE_TMO operators[3] = { FITMO_DRAGO03, FITMO_REINHARD05, FITMO_FATTAL02 };
	const float values[3] = { 0.0F, -1.0F, 0.5F };
	const unsigned width = 64, height = 64;

	for(int o = 0; o < 3; o++) {
		for(int v = 0; v < 3; v++) {
			FIBITMAP *dib = FreeImage_AllocateT(FIT_RGBF, width, height);
			if(!dib) return FALSE;
			for(unsigned y = 0; y < height; y++) {
				FIRGBF *pixel = (FIRGBF*)FreeImage_GetScanLine(dib, y);
				for(unsigned x = 0; x < width; x++) {
					pixel[x].red = pixel[x].green = pixel[x].blue = values[v];
				}
			}

			FIBITMAP *dst = FreeImage_ToneMapping(dib, operators[o]);
			FreeImage_Unload(dib);
			if(!dst) {
				return FALSE;
			}

			if(values[v] <= 0) {
				// no light in, no light out
				for(unsigned y = 0; y < height; y++) {
					const BYTE *bits = FreeImage_GetScanLine(dst, y);
					for(unsigned x = 0; x < width * 3; x++) {
						if(bits[x] != 0) {
							FreeImage_Unload(dst);
							return FALSE;
						}
					}
				}
			}
			FreeImage_Unload(dst);
		}
	}
	return TRUE;
}

// ----------------------------------------------------------

void testToneMapping() {
	printf("testToneMapping ...\n");

	BOOL bResult = testFloatConversionIsLossless();
	assert(bResult);

	bResult = testToneMappingIgnoresAlpha();
	assert(bResult);

	bResult = testToneMappingSurvivesNonFiniteSamples();
	assert(bResult);

	bResult = testConvertToStandardTypeClips();
	assert(bResult);

	bResult = testToneMappingHandlesNegativeRadiance();
	assert(bResult);

	bResult = testToneMappingClipsNegativeRadiance();
	assert(bResult);

	bResult = testToneMappingHandlesFlatImages();
	assert(bResult);
}
