// ==========================================================
// FreeImage 3 Test Script
//
// Float conversion and tone mapping tests
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

static const float s_testValues[] = { -2.0F, 0.0F, 0.25F, 1.0F, 5.0F, 15.0F, 50.0F, 1000.0F };
static const unsigned s_testValueCount = 8;

/** one-row image of s_testValues in every channel */
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

/** red (or only) channel of a one-row float image */
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

/** HDR scene sweeping luminance up to maxLum */
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
				// exact: all three channels are equal
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

			// the rest must match the clean render
			if(fabs(actual - expected) > 5.0) {
				return FALSE;
			}
		}
	}
	return TRUE;
}

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

static BOOL testToneMappingHandlesNegativeRadiance() {
	const FREE_IMAGE_TMO operators[3] = { FITMO_DRAGO03, FITMO_REINHARD05, FITMO_FATTAL02 };
	const unsigned width = 96, height = 96;

	for(int variant = 0; variant < 2; variant++) {
		for(int o = 0; o < 3; o++) {
			FIBITMAP *dib = FreeImage_AllocateT(FIT_RGBAF, width, height);
			if(!dib) return FALSE;

			// shapes from real JPEG XR files: deep negative floor, mixed signs
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

			if(white * 2 > total) {
				return FALSE;
			}
		}
	}
	return TRUE;
}

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
				// mixed signs, some with negative luminance
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

/** RGBF image with columns [0, black) black and the rest from value(x) */
static FIBITMAP* createBlackAndLit(unsigned width, unsigned height, unsigned black, float r, float g, float b, BOOL ramp) {
	FIBITMAP *dib = FreeImage_AllocateT(FIT_RGBF, width, height);
	if(!dib) return NULL;
	for(unsigned y = 0; y < height; y++) {
		FIRGBF *pixel = (FIRGBF*)FreeImage_GetScanLine(dib, y);
		for(unsigned x = 0; x < width; x++) {
			float scale = 0;
			if(x >= black) {
				// a log ramp 0.01 to 1 over the lit columns, or one color
				const float t = (width > black + 1) ? (float)(x - black) / (float)(width - black - 1) : 0;
				scale = ramp ? 0.01F * powf(100.0F, t) : 1.0F;
			}
			pixel[x].red   = r * scale;
			pixel[x].green = g * scale;
			pixel[x].blue  = b * scale;
		}
	}
	return dib;
}

static BOOL testReinhard05BlackAndOneColor() {
	const float colors[2][3] = { { 0.8F, 0.3F, 0.1F }, { 80.0F, 30.0F, 10.0F } };

	for(int c = 0; c < 2; c++) {
		FIBITMAP *dib = createBlackAndLit(32, 32, 16, colors[c][0], colors[c][1], colors[c][2], FALSE);
		if(!dib) return FALSE;
		FIBITMAP *dst = FreeImage_ToneMapping(dib, FITMO_REINHARD05);
		FreeImage_Unload(dib);
		if(!dst) return FALSE;

		// the lit half keeps its hue: neither black nor a white or grey
		const BYTE *bits = FreeImage_GetScanLine(dst, 0) + 3 * 20;
		const BOOL ok = (bits[FI_RGBA_RED] > bits[FI_RGBA_GREEN]) && (bits[FI_RGBA_GREEN] > bits[FI_RGBA_BLUE]);
		FreeImage_Unload(dst);
		if(!ok) return FALSE;
	}
	return TRUE;
}

static BOOL testReinhard05KeepsToneOrder() {
	// half black, then a log ramp: brighter input never maps darker
	for(unsigned black = 1; black <= 48; black += 15) {
		FIBITMAP *dib = createBlackAndLit(64, 4, black, 1, 1, 1, TRUE);
		if(!dib) return FALSE;
		FIBITMAP *dst = FreeImage_ToneMapping(dib, FITMO_REINHARD05);
		FreeImage_Unload(dib);
		if(!dst) return FALSE;

		const BYTE *bits = FreeImage_GetScanLine(dst, 0);
		BOOL ok = TRUE;
		for(unsigned x = black + 1; x < 64; x++) {
			if(bits[3 * x + FI_RGBA_RED] < bits[3 * (x - 1) + FI_RGBA_RED]) ok = FALSE;
		}
		// and the ramp is not one flat white
		if(bits[3 * black + FI_RGBA_RED] == bits[3 * 63 + FI_RGBA_RED]) ok = FALSE;
		FreeImage_Unload(dst);
		if(!ok) return FALSE;
	}
	return TRUE;
}

static BOOL testReinhard05BrightFlatImages() {
	const float values[3] = { 90.0F, 1000.0F, 12345.0F };

	for(int v = 0; v < 6; v++) {
		const float value = values[v % 3];
		FIBITMAP *dib = createBlackAndLit(16, 16, 0, value, value, value, FALSE);
		if(!dib) return FALSE;
		if(v >= 3) {
			// nearly flat: every other pixel two float steps brighter
			const float brighter = nextafterf(nextafterf(value, FLT_MAX), FLT_MAX);
			for(unsigned y = 0; y < 16; y++) {
				FIRGBF *pixel = (FIRGBF*)FreeImage_GetScanLine(dib, y);
				for(unsigned x = (y & 1); x < 16; x += 2) {
					pixel[x].red = pixel[x].green = pixel[x].blue = brighter;
				}
			}
		}
		FIBITMAP *dst = FreeImage_ToneMapping(dib, FITMO_REINHARD05);
		FreeImage_Unload(dib);
		if(!dst) return FALSE;
		// light in, light out
		const BOOL ok = (FreeImage_GetScanLine(dst, 8)[3 * 8] > 0);
		FreeImage_Unload(dst);
		if(!ok) return FALSE;
	}
	return TRUE;
}

/** mean of one channel of a 24-bit image, without the pixel at (0, 0) */
static double channelMean(FIBITMAP *dib, int channel) {
	double sum = 0;
	unsigned count = 0;
	for(unsigned y = 0; y < FreeImage_GetHeight(dib); y++) {
		const BYTE *bits = FreeImage_GetScanLine(dib, y);
		for(unsigned x = (y == 0) ? 1 : 0; x < FreeImage_GetWidth(dib); x++) {
			sum += bits[3 * x + channel];
			count++;
		}
	}
	return count ? (sum / count) : 0;
}

static BOOL testReinhard05InfiniteChannel() {
	FIBITMAP *clean = createScene(FIT_RGBF, 64, 64, 200.0);
	FIBITMAP *dib = createScene(FIT_RGBF, 64, 64, 200.0);
	if(!clean || !dib) return FALSE;
	((FIRGBF*)FreeImage_GetScanLine(dib, 0))[0].green = (float)HUGE_VAL;

	// the channel averages of the complete algorithm
	FIBITMAP *a = FreeImage_TmoReinhard05Ex(clean, 0, 0, 0.5, 0.5);
	FIBITMAP *b = FreeImage_TmoReinhard05Ex(dib, 0, 0, 0.5, 0.5);
	FreeImage_Unload(clean);
	FreeImage_Unload(dib);
	if(!a || !b) return FALSE;
	const BOOL ok = fabs(channelMean(a, FI_RGBA_GREEN) - channelMean(b, FI_RGBA_GREEN)) < 3.0;
	FreeImage_Unload(a);
	FreeImage_Unload(b);
	return ok;
}

static BOOL testReinhard05IgnoresOneBlackPixel() {
	// one black sample does not move the key of the whole image
	const unsigned width = 256, height = 64;
	FIBITMAP *clean = createScene(FIT_RGBF, width, height, 200.0);
	FIBITMAP *dib = createScene(FIT_RGBF, width, height, 200.0);
	if(!clean || !dib) return FALSE;
	FIRGBF *pixel = (FIRGBF*)FreeImage_GetScanLine(dib, 0);
	pixel[0].red = pixel[0].green = pixel[0].blue = 0;

	BOOL ok = TRUE;
	for(int complete = 0; ok && (complete < 2); complete++) {
		FIBITMAP *a = complete ? FreeImage_TmoReinhard05Ex(clean, 0, 0, 0.5, 0.5) : FreeImage_ToneMapping(clean, FITMO_REINHARD05);
		FIBITMAP *b = complete ? FreeImage_TmoReinhard05Ex(dib, 0, 0, 0.5, 0.5) : FreeImage_ToneMapping(dib, FITMO_REINHARD05);
		ok = (a != NULL) && (b != NULL);
		double sum = 0;
		unsigned count = 0;
		for(unsigned y = 0; ok && y < height; y++) {
			const BYTE *pa = FreeImage_GetScanLine(a, y);
			const BYTE *pb = FreeImage_GetScanLine(b, y);
			for(unsigned x = (y == 0) ? 3 : 0; x < width * 3; x++) {
				sum += abs((int)pa[x] - (int)pb[x]);
				count++;
			}
		}
		// the rest of the image renders as without it
		if(ok && (sum / count > 0.5)) ok = FALSE;
		if(a) FreeImage_Unload(a);
		if(b) FreeImage_Unload(b);
	}
	FreeImage_Unload(clean);
	FreeImage_Unload(dib);
	return ok;
}

/** ordinary light, 0.02 to 0.9, with a square lamp at 2000 over 'ppm' millionths of the pixels */
static FIBITMAP* createLampScene(unsigned width, unsigned height, unsigned ppm) {
	FIBITMAP *dib = FreeImage_AllocateT(FIT_RGBF, width, height);
	if(!dib) return NULL;
	for(unsigned y = 0; y < height; y++) {
		FIRGBF *pixel = (FIRGBF*)FreeImage_GetScanLine(dib, y);
		for(unsigned x = 0; x < width; x++) {
			const float level = (float)(0.02 + 0.88 * (0.5 + 0.5 * sin(x * 0.05) * cos(y * 0.07)));
			pixel[x].red = level;
			pixel[x].green = level * 0.9F;
			pixel[x].blue = level * 0.7F;
		}
	}
	const unsigned area = (unsigned)((double)width * height * ppm / 1e6 + 0.5);
	const unsigned side = (unsigned)ceil(sqrt((double)area));
	for(unsigned i = 0; i < area; i++) {
		FIRGBF *pixel = (FIRGBF*)FreeImage_GetScanLine(dib, height / 2 + i / side) + width / 2 + i % side;
		pixel->red = pixel->green = pixel->blue = 2000.0F;
	}
	return dib;
}

/** mean of a 24-bit image */
static double meanLevel(FIBITMAP *dib) {
	double sum = 0;
	for(unsigned y = 0; y < FreeImage_GetHeight(dib); y++) {
		const BYTE *bits = FreeImage_GetScanLine(dib, y);
		for(unsigned x = 0; x < FreeImage_GetWidth(dib) * 3; x++) {
			sum += bits[x];
		}
	}
	return sum / ((double)FreeImage_GetWidth(dib) * FreeImage_GetHeight(dib) * 3);
}

static BOOL testLampSizeChangesRenderSmoothly(FREE_IMAGE_TMO tmo) {
	// a lamp of 0.09% and one of 0.11% of the frame render alike
	FIBITMAP *small = createLampScene(512, 384, 900);
	FIBITMAP *large = createLampScene(512, 384, 1100);
	FIBITMAP *a = small ? FreeImage_ToneMapping(small, tmo) : NULL;
	FIBITMAP *b = large ? FreeImage_ToneMapping(large, tmo) : NULL;
	const BOOL ok = a && b && (fabs(meanLevel(a) - meanLevel(b)) < 20);
	if(small) FreeImage_Unload(small);
	if(large) FreeImage_Unload(large);
	if(a) FreeImage_Unload(a);
	if(b) FreeImage_Unload(b);
	return ok;
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

	bResult = testReinhard05BlackAndOneColor();
	assert(bResult);

	bResult = testReinhard05KeepsToneOrder();
	assert(bResult);

	bResult = testReinhard05BrightFlatImages();
	assert(bResult);

	bResult = testReinhard05InfiniteChannel();
	assert(bResult);

	bResult = testReinhard05IgnoresOneBlackPixel();
	assert(bResult);

	bResult = testLampSizeChangesRenderSmoothly(FITMO_FATTAL02);
	assert(bResult);
}
