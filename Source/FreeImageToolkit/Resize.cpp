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

#include "Resize.h"

#if defined(_M_X64) || defined(_M_AMD64) || defined(__x86_64__) || defined(__SSE2__) || (defined(_M_IX86_FP) && (_M_IX86_FP >= 2))
#include <emmintrin.h>
#define FI_RESIZE_SSE2
#endif

// AVX2 kernels on x64, chosen at run time; not with MinGW's GCC, which misaligns spilled AVX registers (GCC bug 54412)
#if defined(FI_RESIZE_SSE2) && (defined(_M_X64) || defined(__x86_64__)) && !defined(_M_ARM64EC) && !(defined(__MINGW32__) && !defined(__clang__))
#include <immintrin.h>
#define FI_RESIZE_AVX2
#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
// MSVC gives the code around AVX intrinsics VEX encoding, so a function holding them stays out of its callers
#define FI_AVX2 __declspec(noinline)
#define FI_AVX2_INLINE __forceinline
#else
#include <cpuid.h>
#define FI_AVX2 __attribute__((target("avx2"), noinline))
#define FI_AVX2_INLINE inline __attribute__((target("avx2"), always_inline))
#endif
#endif

/**
Returns the color type of a bitmap. In contrast to FreeImage_GetColorType,
this function optionally supports a boolean OUT parameter, that receives TRUE,
if the specified bitmap is greyscale, that is, it consists of grey colors only.
Although it returns the same value as returned by FreeImage_GetColorType for all
image types, this extended function primarily is intended for palletized images,
since the boolean pointed to by 'bIsGreyscale' remains unchanged for RGB(A/F)
images. However, the outgoing boolean is properly maintained for palletized images,
as well as for any non-RGB image type, like FIT_UINTxx and FIT_DOUBLE, for example.
@param dib A pointer to a FreeImage bitmap to calculate the extended color type for
@param bIsGreyscale A pointer to a boolean, that receives TRUE, if the specified bitmap
is greyscale, that is, it consists of grey colors only. This parameter can be NULL.
@return the color type of the specified bitmap
*/
static FREE_IMAGE_COLOR_TYPE
GetExtendedColorType(FIBITMAP *dib, BOOL *bIsGreyscale) {
	const unsigned bpp = FreeImage_GetBPP(dib);
	const unsigned size = CalculateUsedPaletteEntries(bpp);
	const RGBQUAD * const pal = FreeImage_GetPalette(dib);
	FREE_IMAGE_COLOR_TYPE color_type = FIC_MINISBLACK;
	BOOL bIsGrey = TRUE;

	switch (bpp) {
		case 1:
		{
			for (unsigned i = 0; i < size; i++) {
				if ((pal[i].rgbRed != pal[i].rgbGreen) || (pal[i].rgbRed != pal[i].rgbBlue)) {
					color_type = FIC_PALETTE;
					bIsGrey = FALSE;
					break;
				}
			}
			if (bIsGrey) {
				if (pal[0].rgbBlue == 255 && pal[1].rgbBlue == 0) {
					color_type = FIC_MINISWHITE;
				} else if (pal[0].rgbBlue != 0 || pal[1].rgbBlue != 255) {
					color_type = FIC_PALETTE;
				}
			}
			break;
		}

		case 4:
		case 8:
		{
			for (unsigned i = 0; i < size; i++) {
				if ((pal[i].rgbRed != pal[i].rgbGreen) || (pal[i].rgbRed != pal[i].rgbBlue)) {
					color_type = FIC_PALETTE;
					bIsGrey = FALSE;
					break;
				}
				if (color_type != FIC_PALETTE && pal[i].rgbBlue != i) {
					if ((size - i - 1) != pal[i].rgbBlue) {
						color_type = FIC_PALETTE;
						if (!bIsGreyscale) {
							// exit loop if we're not setting
							// bIsGreyscale parameter
							break;
						}
					} else {
						color_type = FIC_MINISWHITE;
					}
				}
			}
			break;
		}

		default:
		{
			color_type = FreeImage_GetColorType(dib);
			bIsGrey = (color_type == FIC_MINISBLACK) ? TRUE : FALSE;
			break;
		}

	}
	if (bIsGreyscale) {
		*bIsGreyscale = bIsGrey;
	}

	return color_type;
}

/**
Returns a pointer to an RGBA palette, created from the specified bitmap.
The RGBA palette is a copy of the specified bitmap's palette, that, additionally
contains the bitmap's transparency information in the rgbReserved member
of the palette's RGBQUAD elements.
@param dib A pointer to a FreeImage bitmap to create the RGBA palette from.
@param buffer A pointer to the buffer to store the RGBA palette.
@return A pointer to the newly created RGBA palette or NULL, if the specified
bitmap is no palletized standard bitmap. If non-NULL, the returned value is
actually the pointer passed in parameter 'buffer'.
*/
static inline RGBQUAD *
GetRGBAPalette(FIBITMAP *dib, RGBQUAD * const buffer) {
	// clone the palette
	const unsigned ncolors = FreeImage_GetColorsUsed(dib);
	if (ncolors == 0) {
		return NULL;
	}
	memcpy(buffer, FreeImage_GetPalette(dib), ncolors * sizeof(RGBQUAD));
	// merge the transparency table
	const unsigned ntransp = MIN(ncolors, FreeImage_GetTransparencyCount(dib));
	const BYTE * const tt = FreeImage_GetTransparencyTable(dib);
	for (unsigned i = 0; i < ntransp; i++) {
		buffer[i].rgbReserved = tt[i];
	}
	for (unsigned i = ntransp; i < ncolors; i++) {
		buffer[i].rgbReserved = 255;
	}
	return buffer;
}

// rounds half away from zero and saturates; NaN gives lo
template <class T> static inline T
RoundSample(double value, double lo, double hi) {
	if ((value > lo) && (value < hi)) {
		// negated without a branch: noisy signs would mispredict one
		const INT64 magnitude = (INT64)(fabs(value) + 0.5);
		const INT64 negative = (value < 0);
		return (T)((magnitude ^ -negative) + negative);
	}
	return (T)((value >= hi) ? hi : lo);
}

static inline void StoreSample(BYTE *dst, double value) { *dst = RoundSample<BYTE>(value, 0.0, 255.0); }
static inline void StoreSample(WORD *dst, double value) { *dst = RoundSample<WORD>(value, 0.0, 65535.0); }
static inline void StoreSample(short *dst, double value) { *dst = RoundSample<short>(value, -32768.0, 32767.0); }
static inline void StoreSample(DWORD *dst, double value) { *dst = RoundSample<DWORD>(value, 0.0, 4294967295.0); }
static inline void StoreSample(LONG *dst, double value) { *dst = RoundSample<LONG>(value, -2147483648.0, 2147483647.0); }
static inline void StoreSample(float *dst, double value) { *dst = (float)value; }
static inline void StoreSample(double *dst, double value) { *dst = value; }

// a block of the vertical pass; BYTE and WORD sums stay far inside int, and saturating them there vectorises
template <class T> static inline void
StoreSamples(T *dst, const double *value, INT64 count) {
	for (INT64 k = 0; k < count; k++) {
		StoreSample(dst + k, value[k]);
	}
}
static inline void StoreSamples(BYTE *dst, const double *value, INT64 count) {
	for (INT64 k = 0; k < count; k++) {
		dst[k] = (BYTE)CLAMP<int>((int)(value[k] + 0.5), 0, 0xFF);
	}
}
static inline void StoreSamples(WORD *dst, const double *value, INT64 count) {
	for (INT64 k = 0; k < count; k++) {
		dst[k] = (WORD)CLAMP<int>((int)(value[k] + 0.5), 0, 0xFFFF);
	}
}

// samples per block of the vertical pass: its accumulators stay in L1
static const int VERTICAL_BLOCK = 256;

// the image between the two passes is held a band at a time, of about this many bytes: it stays in cache
static const size_t BAND_BYTES = 4 << 20;
// and of at least this many rows, to keep every thread busy
static const unsigned BAND_MIN_ROWS = 64;

// rows of a band: BAND_BYTES of them, never fewer than a window or BAND_MIN_ROWS, never more than rows
static unsigned
BandRows(unsigned width, unsigned bpp, unsigned window, unsigned rows) {
	const size_t line = MAX((size_t)1, ((size_t)width * bpp + 7) / 8);
	const size_t band = MAX(BAND_BYTES / line, (size_t)MAX(window, BAND_MIN_ROWS));
	return (unsigned)MIN(band, (size_t)rows);
}

// measured costs of a tap, in taps of the row-blocked vertical pass: horizontal, and vertical walking columns
static const double HORIZONTAL_TAP_COST = 2;
static const double COLUMN_TAP_COST = 2.5;

// measured costs in the byte kernels, in their vertical taps: a horizontal tap, and a pixel the horizontal pass writes
static const double BYTE_HORIZONTAL_TAP_COST = 0.8;
static const double BYTE_HORIZONTAL_PIXEL_COST = 4;

// how much cheaper the other order's taps must be to leave the width rule; more for 128-bit pixels, memory-bound vertically
static const double ORDER_MARGIN = 1.1;
static const double WIDE_ORDER_MARGIN = 2;

// taps of a pass over one row or column
static double
TotalTaps(CWeightsTable &weights, unsigned size) {
	double taps = 0;
	for (unsigned i = 0; i < size; i++) {
		taps += weights.getRightBoundary(i) - weights.getLeftBoundary(i);
	}
	return taps;
}

// cost of a vertical tap from src_bpp to dst_bpp: VerticalFilterSamples takes plain samples, the rest walk columns
static double
VerticalTapCost(FREE_IMAGE_TYPE type, unsigned src_bpp, const RGBQUAD *src_pal, unsigned dst_bpp) {
	const BOOL rows = (type != FIT_BITMAP) || (!src_pal && (src_bpp == dst_bpp) && ((src_bpp == 8) || (src_bpp == 24) || (src_bpp == 32)));
	return rows ? 1 : COLUMN_TAP_COST;
}

// whether every pass of either order runs the byte kernels: 8-, 24- or 32-bit samples without a palette, one depth throughout
static BOOL
ByteKernels(FREE_IMAGE_TYPE type, unsigned src_bpp, const RGBQUAD *src_pal, unsigned tmp_bpp, unsigned dst_bpp) {
	BOOL sse2 = FALSE;
#ifdef FI_RESIZE_SSE2
	sse2 = TRUE;
#endif
	return sse2 && (type == FIT_BITMAP) && !src_pal && (src_bpp == tmp_bpp) && (tmp_bpp == dst_bpp) && ((src_bpp == 8) || (src_bpp == 24) || (src_bpp == 32));
}

// horizontal pass for plain sample arrays: SPP samples per pixel, each filtered on its own
template <class T, int SPP> static void
HorizontalFilterSamples(CWeightsTable &weightsTable, FIBITMAP *const src, const unsigned src_row, const unsigned src_offset_x, FIBITMAP *const dst, const unsigned dst_row, const unsigned rows, const unsigned dst_width) {
	#pragma omp parallel for schedule(dynamic) default(shared)
	for (INT64 y = 0; y < rows; y++) {
		const T *const src_bits = (T *)FreeImage_GetScanLine(src, src_row + y) + (INT64)src_offset_x * SPP;
		T *dst_bits = (T *)FreeImage_GetScanLine(dst, dst_row + y);

		for (INT64 x = 0; x < dst_width; x++) {
			const INT64 iLeft = weightsTable.getLeftBoundary(x);
			const INT64 iLimit = weightsTable.getRightBoundary(x) - iLeft;
			const T *pixel = src_bits + iLeft * SPP;
			double value[SPP];
			for (int j = 0; j < SPP; j++) {
				value[j] = 0;
			}

			for (INT64 i = 0; i < iLimit; i++) {
				const double weight = weightsTable.getWeight(x, i);
				for (int j = 0; j < SPP; j++) {
					value[j] += weight * (double)pixel[j];
				}
				pixel += SPP;
			}

			for (int j = 0; j < SPP; j++) {
				StoreSample(dst_bits + j, value[j]);
			}
			dst_bits += SPP;
		}
	}
}

#ifdef FI_RESIZE_SSE2
#if defined(_MSC_VER) && !defined(__clang__)
#define FI_RESIZE_INLINE __forceinline
#else
#define FI_RESIZE_INLINE inline __attribute__((always_inline))
#endif

// rows the byte kernel filters together: 12 or 16 samples a tap keep 6 or 8 sums in flight
template <int SPP> struct ByteRows {
	enum { N = (SPP >= 2) ? 4 : 16 };
};

// pixels the ring holds besides a window: it converts this far ahead
static const INT64 RING_AHEAD = 128;
// bytes of a thread's ring beyond which the plain kernels run instead
static const INT64 RING_MAX_BYTES = 4 << 20;

#ifdef FI_RESIZE_AVX2
// whether the CPU runs AVX2 and the OS saves the YMM registers
static bool
DetectAVX2() {
	unsigned c1, b7, xcr0;
#if defined(_MSC_VER) && !defined(__clang__)
	int info[4];
	__cpuid(info, 0);
	if (info[0] < 7) {
		return false;
	}
	__cpuid(info, 1);
	c1 = (unsigned)info[2];
	__cpuidex(info, 7, 0);
	b7 = (unsigned)info[1];
	// OSXSAVE and AVX before XGETBV
	if ((c1 & (3u << 27)) != (3u << 27)) {
		return false;
	}
	xcr0 = (unsigned)_xgetbv(0);
#else
	unsigned a, b, c, d;
	if (__get_cpuid_max(0, NULL) < 7) {
		return false;
	}
	__cpuid(1, a, b, c, d);
	c1 = c;
	__cpuid_count(7, 0, a, b, c, d);
	b7 = b;
	if ((c1 & (3u << 27)) != (3u << 27)) {
		return false;
	}
	__asm__ __volatile__("xgetbv" : "=a"(xcr0), "=d"(d) : "c"(0));
#endif
	return ((xcr0 & 6) == 6) && ((b7 & (1u << 5)) != 0);
}

static bool
HasAVX2() {
	static const bool avx2 = DetectAVX2();
	return avx2;
}
#endif

// 16 bytes to 8 pairs of doubles, pair j = bytes j and j + 8, by masks, shifts and 2^52 + b - 2^52: exact, and no shuffles
static FI_RESIZE_INLINE void
SplitBytes(__m128d *const pair, const __m128i v) {
	const __m128i m8 = _mm_set1_epi16(0x00FF);
	const __m128i m16 = _mm_set1_epi32(0x0000FFFF);
	const __m128i m32 = _mm_set_epi32(0, -1, 0, -1);
	const __m128i e = _mm_set_epi32(0x43300000, 0, 0x43300000, 0);
	const __m128d bias = _mm_set1_pd(4503599627370496.0);
	const __m128i w0 = _mm_and_si128(v, m8);
	const __m128i w1 = _mm_srli_epi16(v, 8);
	const __m128i d0 = _mm_and_si128(w0, m16);
	const __m128i d1 = _mm_and_si128(w1, m16);
	const __m128i d2 = _mm_srli_epi32(w0, 16);
	const __m128i d3 = _mm_srli_epi32(w1, 16);
	pair[0] = _mm_sub_pd(_mm_castsi128_pd(_mm_or_si128(_mm_and_si128(d0, m32), e)), bias);
	pair[1] = _mm_sub_pd(_mm_castsi128_pd(_mm_or_si128(_mm_and_si128(d1, m32), e)), bias);
	pair[2] = _mm_sub_pd(_mm_castsi128_pd(_mm_or_si128(_mm_and_si128(d2, m32), e)), bias);
	pair[3] = _mm_sub_pd(_mm_castsi128_pd(_mm_or_si128(_mm_and_si128(d3, m32), e)), bias);
	pair[4] = _mm_sub_pd(_mm_castsi128_pd(_mm_or_si128(_mm_srli_epi64(d0, 32), e)), bias);
	pair[5] = _mm_sub_pd(_mm_castsi128_pd(_mm_or_si128(_mm_srli_epi64(d1, 32), e)), bias);
	pair[6] = _mm_sub_pd(_mm_castsi128_pd(_mm_or_si128(_mm_srli_epi64(d2, 32), e)), bias);
	pair[7] = _mm_sub_pd(_mm_castsi128_pd(_mm_or_si128(_mm_srli_epi64(d3, 32), e)), bias);
}

// samples [k0, k1) of the rows to out[(k - k0) * ROWS + row]
template <int ROWS> static FI_RESIZE_INLINE void
TransposeBytes(double *const out, const BYTE *const *const line, const INT64 k0, const INT64 k1) {
	for (int j = 0; j < ROWS; j += 2) {
		const BYTE *const a = line[j];
		const BYTE *const b = line[j + 1];
		double *o = out + j;
		INT64 k = k0;
		for (; k + 8 <= k1; k += 8) {
			// samples k..k+7 of row a, then of row b: pair s is (a, b) of sample k + s
			__m128d pair[8];
			SplitBytes(pair, _mm_unpacklo_epi64(_mm_loadl_epi64((const __m128i *)(a + k)), _mm_loadl_epi64((const __m128i *)(b + k))));
			for (int s = 0; s < 8; s++) {
				_mm_store_pd(o + s * ROWS, pair[s]);
			}
			o += 8 * ROWS;
		}
		for (; k < k1; k++) {
			o[0] = a[k];
			o[1] = b[k];
			o += ROWS;
		}
	}
}

// n samples of a row to out: each 16 as SplitBytes pairs, the rest in order
static FI_RESIZE_INLINE void
ConvertByteRow(double *const out, const BYTE *const in, const INT64 n) {
	INT64 k = 0;
	for (; k + 16 <= n; k += 16) {
		__m128d pair[8];
		SplitBytes(pair, _mm_loadu_si128((const __m128i *)(in + k)));
		for (int s = 0; s < 8; s++) {
			_mm_store_pd(out + k + 2 * s, pair[s]);
		}
	}
	for (; k < n; k++) {
		out[k] = in[k];
	}
}

// 8 sums of SplitBytes pairs to 16 bytes in sample order, (int)(v + 0.5) saturated to 0..255 as the scalar kernels round
static FI_RESIZE_INLINE __m128i
PackPairSums(const __m128d *const sum) {
	const __m128d half = _mm_set1_pd(0.5);
	__m128i t[8];
	for (int m = 0; m < 8; m++) {
		t[m] = _mm_cvttpd_epi32(_mm_add_pd(sum[m], half));
	}
	const __m128i a = _mm_unpacklo_epi32(t[0], t[1]);
	const __m128i b = _mm_unpacklo_epi32(t[2], t[3]);
	const __m128i c = _mm_unpacklo_epi32(t[4], t[5]);
	const __m128i d = _mm_unpacklo_epi32(t[6], t[7]);
	const __m128i lo = _mm_packs_epi32(_mm_unpacklo_epi64(a, b), _mm_unpacklo_epi64(c, d));
	const __m128i hi = _mm_packs_epi32(_mm_unpackhi_epi64(a, b), _mm_unpackhi_epi64(c, d));
	return _mm_packus_epi16(lo, hi);
}

// 12 or 16 sums to bytes, (int)(v + 0.5) saturated to 0..255 as the scalar kernels round
template <int VALUES> static FI_RESIZE_INLINE void
PackSums(BYTE *const bytes, const __m128d *const sum) {
	const __m128d half = _mm_set1_pd(0.5);
	__m128i q[VALUES / 4];
	for (int v = 0; v < VALUES / 4; v++) {
		q[v] = _mm_unpacklo_epi64(_mm_cvttpd_epi32(_mm_add_pd(sum[2 * v], half)), _mm_cvttpd_epi32(_mm_add_pd(sum[2 * v + 1], half)));
	}
	_mm_storeu_si128((__m128i *)bytes, _mm_packus_epi16(_mm_packs_epi32(q[0], q[1]), _mm_packs_epi32(q[2], q[VALUES / 4 - 1])));
}

// the pixels [left, next) of a group of rows, as doubles, pixel p in slot p % capacity
struct ByteRing {
	double *ring;
	INT64 capacity, left, slot, next, next_slot;
};

// moves the ring to the window [iLeft, iRight), converting ahead up to end; returns the window's first slot
template <int SPP, int ROWS> static FI_RESIZE_INLINE INT64
MoveRing(ByteRing &r, const BYTE *const *const line, const INT64 iLeft, const INT64 iRight, const INT64 end) {
	r.slot += iLeft - r.left;
	while (r.slot >= r.capacity) {
		r.slot -= r.capacity;
	}
	r.left = iLeft;
	if (r.next < iLeft) {
		r.next = iLeft;
		r.next_slot = r.slot;
	}
	if (r.next < iRight) {
		const INT64 stop = MIN(MIN(end, iLeft + r.capacity), MAX(iRight, r.next + RING_AHEAD));
		while (r.next < stop) {
			const INT64 n = MIN(stop - r.next, r.capacity - r.next_slot);
			TransposeBytes<ROWS>(r.ring + r.next_slot * SPP * ROWS, line, r.next * SPP, (r.next + n) * SPP);
			r.next += n;
			r.next_slot += n;
			if (r.next_slot == r.capacity) {
				r.next_slot = 0;
			}
		}
	}
	return r.slot;
}

// a destination pixel's bytes, [channel][row], to the rows of its group
template <int SPP, int ROWS> static FI_RESIZE_INLINE void
ScatterBytes(BYTE *const *const out, const INT64 count, const INT64 x, const BYTE *const bytes) {
	for (INT64 r = 0; r < count; r++) {
		BYTE *const d = out[r] + x * SPP;
		for (int c = 0; c < SPP; c++) {
			d[c] = bytes[c * ROWS + r];
		}
	}
}

// the destination pixels of a group of rows, sums of two in SSE2 registers
template <int SPP> static FI_RESIZE_INLINE void
FilterGroup(CWeightsTable &weightsTable, ByteRing &r, const BYTE *const *const line, BYTE *const *const out, const INT64 count, const unsigned dst_width, const INT64 end) {
	enum { ROWS = ByteRows<SPP>::N, VALUES = SPP * ByteRows<SPP>::N };
	for (INT64 x = 0; x < dst_width; x++) {
		const INT64 iLeft = weightsTable.getLeftBoundary((unsigned)x);
		const INT64 taps = weightsTable.getRightBoundary((unsigned)x) - iLeft;
		const INT64 slot = MoveRing<SPP, ROWS>(r, line, iLeft, iLeft + taps, end);
		const double *const weights = weightsTable.getWeights((unsigned)x);
		__m128d sum[VALUES / 2];
		for (int v = 0; v < VALUES / 2; v++) {
			sum[v] = _mm_setzero_pd();
		}
		// the window runs to the end of the ring, then on from its start
		const double *pixel = r.ring + slot * VALUES;
		INT64 i = 0, stop = MIN(taps, r.capacity - slot);
		for (;;) {
			for (; i < stop; i++) {
				const __m128d w = _mm_set1_pd(weights[i]);
				for (int v = 0; v < VALUES / 2; v++) {
					sum[v] = _mm_add_pd(sum[v], _mm_mul_pd(w, _mm_load_pd(pixel + 2 * v)));
				}
				pixel += VALUES;
			}
			if (i == taps) {
				break;
			}
			pixel = r.ring;
			stop = taps;
		}
		BYTE bytes[16];
		PackSums<VALUES>(bytes, sum);
		ScatterBytes<SPP, ROWS>(out, count, x, bytes);
	}
}

#ifdef FI_RESIZE_AVX2
// 12 or 16 sums, in 3 or 4 AVX2 registers, to bytes as PackSums
template <int VALUES> static FI_AVX2_INLINE void
PackSumsAVX2(BYTE *const bytes, const __m256d *const sum) {
	const __m256d half = _mm256_set1_pd(0.5);
	__m128i q[VALUES / 4];
	for (int v = 0; v < VALUES / 4; v++) {
		q[v] = _mm256_cvttpd_epi32(_mm256_add_pd(sum[v], half));
	}
	_mm_storeu_si128((__m128i *)bytes, _mm_packus_epi16(_mm_packs_epi32(q[0], q[1]), _mm_packs_epi32(q[2], q[VALUES / 4 - 1])));
}

// FilterGroup two destination pixels at a time with AVX2: a pixel both windows read is loaded once for both sums
template <int SPP> static FI_AVX2 void
FilterGroupAVX2(CWeightsTable &weightsTable, ByteRing &r, const BYTE *const *const line, BYTE *const *const out, const INT64 count, const unsigned dst_width, const INT64 end) {
	enum { ROWS = ByteRows<SPP>::N, VALUES = SPP * ByteRows<SPP>::N };
	for (INT64 x = 0; x < dst_width; x += 2) {
		const INT64 x1 = MIN(x + 1, (INT64)dst_width - 1);
		const INT64 a0 = weightsTable.getLeftBoundary((unsigned)x), a1 = weightsTable.getRightBoundary((unsigned)x);
		const INT64 b0 = weightsTable.getLeftBoundary((unsigned)x1), b1 = (x1 > x) ? weightsTable.getRightBoundary((unsigned)x1) : b0;
		const INT64 stop = MAX(a1, b1);
		INT64 slot = MoveRing<SPP, ROWS>(r, line, a0, stop, end);
		const double *wa = weightsTable.getWeights((unsigned)x);
		const double *wb = weightsTable.getWeights((unsigned)x1);
		__m256d sa[VALUES / 4], sb[VALUES / 4];
		for (int v = 0; v < VALUES / 4; v++) {
			sa[v] = _mm256_setzero_pd();
			sb[v] = _mm256_setzero_pd();
		}
		// stretches where the windows reading a pixel stay the same and the ring does not wrap; each sum takes its taps in order
		for (INT64 p = a0; p < stop; ) {
			INT64 q = MIN(stop, p + (r.capacity - slot));
			if (b0 > p) {
				q = MIN(q, b0);
			}
			if (a1 > p) {
				q = MIN(q, a1);
			}
			if (b1 > p) {
				q = MIN(q, b1);
			}
			const double *pixel = r.ring + slot * VALUES;
			const bool in_a = (p < a1), in_b = (p >= b0) && (p < b1);
			if (in_a && in_b) {
				for (INT64 k = p; k < q; k++) {
					const __m256d wx = _mm256_broadcast_sd(wa++);
					const __m256d wy = _mm256_broadcast_sd(wb++);
					for (int v = 0; v < VALUES / 4; v++) {
						const __m256d s = _mm256_load_pd(pixel + 4 * v);
						sa[v] = _mm256_add_pd(sa[v], _mm256_mul_pd(wx, s));
						sb[v] = _mm256_add_pd(sb[v], _mm256_mul_pd(wy, s));
					}
					pixel += VALUES;
				}
			} else if (in_a) {
				for (INT64 k = p; k < q; k++) {
					const __m256d wx = _mm256_broadcast_sd(wa++);
					for (int v = 0; v < VALUES / 4; v++) {
						sa[v] = _mm256_add_pd(sa[v], _mm256_mul_pd(wx, _mm256_load_pd(pixel + 4 * v)));
					}
					pixel += VALUES;
				}
			} else if (in_b) {
				for (INT64 k = p; k < q; k++) {
					const __m256d wy = _mm256_broadcast_sd(wb++);
					for (int v = 0; v < VALUES / 4; v++) {
						sb[v] = _mm256_add_pd(sb[v], _mm256_mul_pd(wy, _mm256_load_pd(pixel + 4 * v)));
					}
					pixel += VALUES;
				}
			}
			slot += q - p;
			if (slot == r.capacity) {
				slot = 0;
			}
			p = q;
		}
		BYTE bytes[16];
		PackSumsAVX2<VALUES>(bytes, sa);
		ScatterBytes<SPP, ROWS>(out, count, x, bytes);
		if (x1 > x) {
			PackSumsAVX2<VALUES>(bytes, sb);
			ScatterBytes<SPP, ROWS>(out, count, x1, bytes);
		}
	}
}
#endif
#endif

#ifdef FI_RESIZE_SSE2
// horizontal pass for 8-bit samples, ByteRows rows at a time, a lane each, taps in HorizontalFilterSamples' order; a ring converts each pixel once
template <int SPP, bool AVX2> static void
HorizontalFilterRing(CWeightsTable &weightsTable, FIBITMAP *const src, const unsigned src_row, const unsigned src_offset_x, FIBITMAP *const dst, const unsigned dst_row, const unsigned rows, const unsigned dst_width, const INT64 capacity) {
	enum { ROWS = ByteRows<SPP>::N, VALUES = SPP * ByteRows<SPP>::N };
	INT64 end = 0;
	for (unsigned x = 0; x < dst_width; x++) {
		end = MAX(end, (INT64)weightsTable.getRightBoundary(x));
	}
	const INT64 groups = ((INT64)rows + ROWS - 1) / ROWS;

	#pragma omp parallel default(shared)
	{
		void *const block = malloc((size_t)capacity * VALUES * sizeof(double) + 31);
		ByteRing r = { block ? (double *)(((size_t)block + 31) & ~(size_t)31) : NULL, capacity, 0, 0, 0, 0 };

		#pragma omp for schedule(dynamic)
		for (INT64 g = 0; g < groups; g++) {
			const INT64 y0 = g * ROWS;
			const INT64 count = MIN((INT64)ROWS, (INT64)rows - y0);
			if (!r.ring) {
				HorizontalFilterSamples<BYTE, SPP>(weightsTable, src, (unsigned)(src_row + y0), src_offset_x, dst, (unsigned)(dst_row + y0), (unsigned)count, dst_width);
				continue;
			}
			// a group short of rows filters its last row again and stores it once
			const BYTE *line[ROWS];
			BYTE *out[ROWS];
			for (int k = 0; k < ROWS; k++) {
				const INT64 y = y0 + MIN((INT64)k, count - 1);
				line[k] = FreeImage_GetScanLine(src, (int)(src_row + y)) + (INT64)src_offset_x * SPP;
				out[k] = FreeImage_GetScanLine(dst, (int)(dst_row + y));
			}
			r.left = r.slot = r.next = r.next_slot = 0;
#ifdef FI_RESIZE_AVX2
			if (AVX2) {
				FilterGroupAVX2<SPP>(weightsTable, r, line, out, count, dst_width, end);
				continue;
			}
#endif
			FilterGroup<SPP>(weightsTable, r, line, out, count, dst_width, end);
		}

		free(block);
	}
}
#endif

// horizontal pass for 8-bit samples: the ring kernels on x86 and x64, AVX2 where the CPU has it
template <int SPP> static void
HorizontalFilterBytes(CWeightsTable &weightsTable, FIBITMAP *const src, const unsigned src_row, const unsigned src_offset_x, FIBITMAP *const dst, const unsigned dst_row, const unsigned rows, const unsigned dst_width) {
#ifdef FI_RESIZE_SSE2
	const INT64 capacity = (INT64)weightsTable.getWindowSize() + RING_AHEAD;
#ifdef FI_RESIZE_AVX2
	// two windows, for two destination pixels at a time
	const INT64 capacity2 = capacity + weightsTable.getWindowSize();
	if (HasAVX2() && (capacity2 * SPP * ByteRows<SPP>::N * (INT64)sizeof(double) <= RING_MAX_BYTES)) {
		HorizontalFilterRing<SPP, true>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width, capacity2);
		return;
	}
#endif
	if (capacity * SPP * ByteRows<SPP>::N * (INT64)sizeof(double) <= RING_MAX_BYTES) {
		HorizontalFilterRing<SPP, false>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width, capacity);
		return;
	}
#endif
	HorizontalFilterSamples<BYTE, SPP>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
}

// vertical pass for plain sample arrays, row by row: every source row is read sequentially
template <class T, int SPP> static void
VerticalFilterSamples(CWeightsTable &weightsTable, FIBITMAP *const src, const INT64 src_row_bias, const unsigned src_offset_x, FIBITMAP *const dst, const INT64 dst_row_bias, const unsigned y_begin, const unsigned y_end, const unsigned width) {
	const INT64 src_pitch = FreeImage_GetPitch(src);
	const BYTE *const src_base = FreeImage_GetBits(src) + (INT64)src_offset_x * SPP * sizeof(T);
	const INT64 samples = (INT64)width * SPP;

	#pragma omp parallel for schedule(dynamic) default(shared)
	for (INT64 y = y_begin; y < y_end; y++) {
		const INT64 iLeft = weightsTable.getLeftBoundary(y);
		const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;
		const BYTE *const src_rows = src_base + (src_row_bias + iLeft) * src_pitch;
		T *const dst_bits = (T *)FreeImage_GetScanLine(dst, dst_row_bias + y);
		double value[VERTICAL_BLOCK];

		for (INT64 x0 = 0; x0 < samples; x0 += VERTICAL_BLOCK) {
			const INT64 count = MIN((INT64)VERTICAL_BLOCK, samples - x0);
			for (INT64 k = 0; k < count; k++) {
				value[k] = 0;
			}

			for (INT64 i = 0; i < iLimit; i++) {
				const double weight = weightsTable.getWeight(y, i);
				const T *const pixel = (const T *)(src_rows + i * src_pitch) + x0;
				for (INT64 k = 0; k < count; k++) {
					value[k] += weight * (double)pixel[k];
				}
			}

			StoreSamples(dst_bits + x0, value, count);
		}
	}
}

#ifdef FI_RESIZE_SSE2
// destination rows of a work item of the vertical byte kernel: neighbouring rows share most of their source rows
static const INT64 VERTICAL_RUN = 16;
// bytes of converted source rows a work item holds, a window of them: its block of samples follows
static const INT64 VERTICAL_RING_BYTES = 64 << 10;
// samples of a block: at least a cache line of each source row
static const INT64 VERTICAL_BLOCK_MIN = 64;
// taps per source row below which converting on every tap beats the ring
static const double VERTICAL_RING_USES = 1.5;

// samples of a block for a ring of capacity rows
static INT64
VerticalBlockSize(const INT64 capacity) {
	return CLAMP<INT64>((VERTICAL_RING_BYTES / (capacity * (INT64)sizeof(double))) & ~(INT64)15, VERTICAL_BLOCK_MIN, 256);
}

// the source rows [left, next) of a block, as doubles, row r in slot r % capacity; without a ring every tap converts
struct RowRing {
	double *ring;
	INT64 capacity, block, left, slot, next, next_slot;
};

// the samples [x0, x0 + n) of a work item: source row r at src + (row_bias + r) * pitch, destination row y of dst
struct SampleBlock {
	const BYTE *src;
	INT64 pitch, row_bias;
	FIBITMAP *dst;
	INT64 dst_row_bias, x0, n;
};

// moves the ring to the rows [iLeft, iRight), converting the new ones; returns iLeft's slot
static FI_RESIZE_INLINE INT64
MoveRowRing(RowRing &r, const SampleBlock &b, const INT64 iLeft, const INT64 iRight) {
	r.slot += iLeft - r.left;
	while (r.slot >= r.capacity) {
		r.slot -= r.capacity;
	}
	r.left = iLeft;
	if (r.next < iLeft) {
		r.next = iLeft;
		r.next_slot = r.slot;
	}
	for (; r.next < iRight; r.next++) {
		ConvertByteRow(r.ring + r.next_slot * r.block, b.src + (b.row_bias + r.next) * b.pitch, b.n);
		if (++r.next_slot == r.capacity) {
			r.next_slot = 0;
		}
	}
	return r.slot;
}

// a destination row of a block, every tap converting its 16 samples straight from the source
static FI_RESIZE_INLINE void
DirectRow(const SampleBlock &b, BYTE *const dst_bits, const INT64 iLeft, const INT64 taps, const double *const weights) {
	const BYTE *const rows = b.src + (b.row_bias + iLeft) * b.pitch;
	INT64 k = 0;
	for (; k + 16 <= b.n; k += 16) {
		__m128d sum[8];
		for (int m = 0; m < 8; m++) {
			sum[m] = _mm_setzero_pd();
		}
		const BYTE *pixel = rows + k;
		for (INT64 i = 0; i < taps; i++) {
			const __m128d w = _mm_set1_pd(weights[i]);
			__m128d pair[8];
			SplitBytes(pair, _mm_loadu_si128((const __m128i *)pixel));
			for (int m = 0; m < 8; m++) {
				sum[m] = _mm_add_pd(sum[m], _mm_mul_pd(w, pair[m]));
			}
			pixel += b.pitch;
		}
		_mm_storeu_si128((__m128i *)(dst_bits + k), PackPairSums(sum));
	}
	for (; k < b.n; k++) {
		const BYTE *pixel = rows + k;
		double value = 0;
		for (INT64 i = 0; i < taps; i++) {
			value += weights[i] * (double)*pixel;
			pixel += b.pitch;
		}
		dst_bits[k] = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
	}
}

// the samples [k, n) of a destination row, one at a time from the ring
static FI_RESIZE_INLINE void
RingTail(const RowRing &r, BYTE *const dst_bits, INT64 k, const INT64 n, const INT64 slot, const INT64 taps, const double *const weights) {
	for (; k < n; k++) {
		double value = 0;
		INT64 s = slot;
		for (INT64 i = 0; i < taps; i++) {
			value += weights[i] * r.ring[s * r.block + k];
			if (++s == r.capacity) {
				s = 0;
			}
		}
		dst_bits[k] = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
	}
}

// the destination rows [ya, yb) of a block, 16 sums in SSE2 registers through every tap
static FI_RESIZE_INLINE void
FilterBlock(CWeightsTable &weightsTable, RowRing &r, const SampleBlock &b, const INT64 ya, const INT64 yb) {
	r.left = r.next = weightsTable.getLeftBoundary((unsigned)ya);
	r.slot = r.next_slot = 0;
	for (INT64 y = ya; y < yb; y++) {
		const INT64 iLeft = weightsTable.getLeftBoundary((unsigned)y);
		const INT64 taps = weightsTable.getRightBoundary((unsigned)y) - iLeft;
		const double *const weights = weightsTable.getWeights((unsigned)y);
		BYTE *const dst_bits = FreeImage_GetScanLine(b.dst, (int)(b.dst_row_bias + y)) + b.x0;
		if (!r.ring) {
			DirectRow(b, dst_bits, iLeft, taps, weights);
			continue;
		}
		const INT64 slot = MoveRowRing(r, b, iLeft, iLeft + taps);
		INT64 k = 0;
		for (; k + 16 <= b.n; k += 16) {
			__m128d sum[8];
			for (int m = 0; m < 8; m++) {
				sum[m] = _mm_setzero_pd();
			}
			const double *row = r.ring + slot * r.block + k;
			const double *const end = r.ring + r.capacity * r.block + k;
			for (INT64 i = 0; i < taps; i++) {
				const __m128d w = _mm_set1_pd(weights[i]);
				for (int m = 0; m < 8; m++) {
					sum[m] = _mm_add_pd(sum[m], _mm_mul_pd(w, _mm_load_pd(row + 2 * m)));
				}
				row += r.block;
				if (row == end) {
					row = r.ring + k;
				}
			}
			_mm_storeu_si128((__m128i *)(dst_bits + k), PackPairSums(sum));
		}
		RingTail(r, dst_bits, k, b.n, slot, taps, weights);
	}
}

#ifdef FI_RESIZE_AVX2
// FilterBlock two destination rows at a time with AVX2: a source row both windows read is loaded once for both
static FI_AVX2 void
FilterBlockAVX2(CWeightsTable &weightsTable, RowRing &r, const SampleBlock &b, const INT64 ya, const INT64 yb) {
	r.left = r.next = weightsTable.getLeftBoundary((unsigned)ya);
	r.slot = r.next_slot = 0;
	for (INT64 y = ya; y < yb; y += 2) {
		const INT64 y1 = MIN(y + 1, yb - 1);
		const INT64 a0 = weightsTable.getLeftBoundary((unsigned)y), a1 = weightsTable.getRightBoundary((unsigned)y);
		const INT64 b0 = weightsTable.getLeftBoundary((unsigned)y1), b1 = (y1 > y) ? weightsTable.getRightBoundary((unsigned)y1) : b0;
		const double *const wa = weightsTable.getWeights((unsigned)y);
		const double *const wb = weightsTable.getWeights((unsigned)y1);
		BYTE *const da = FreeImage_GetScanLine(b.dst, (int)(b.dst_row_bias + y)) + b.x0;
		BYTE *const db = FreeImage_GetScanLine(b.dst, (int)(b.dst_row_bias + y1)) + b.x0;
		if (!r.ring) {
			DirectRow(b, da, a0, a1 - a0, wa);
			if (y1 > y) {
				DirectRow(b, db, b0, b1 - b0, wb);
			}
			continue;
		}
		const INT64 stop = MAX(a1, b1);
		const INT64 slot = MoveRowRing(r, b, a0, stop);
		INT64 k = 0;
		for (; k + 16 <= b.n; k += 16) {
			__m256d sa[4], sb[4];
			for (int v = 0; v < 4; v++) {
				sa[v] = _mm256_setzero_pd();
				sb[v] = _mm256_setzero_pd();
			}
			// stretches where the windows reading a row stay the same and the ring does not wrap; each sum takes its taps in order
			const double *xa = wa, *xb = wb;
			INT64 s = slot;
			for (INT64 p = a0; p < stop; ) {
				INT64 q = MIN(stop, p + (r.capacity - s));
				if (b0 > p) {
					q = MIN(q, b0);
				}
				if (a1 > p) {
					q = MIN(q, a1);
				}
				if (b1 > p) {
					q = MIN(q, b1);
				}
				const double *row = r.ring + s * r.block + k;
				const bool in_a = (p < a1), in_b = (p >= b0) && (p < b1);
				if (in_a && in_b) {
					for (INT64 i = p; i < q; i++) {
						const __m256d wx = _mm256_broadcast_sd(xa++);
						const __m256d wy = _mm256_broadcast_sd(xb++);
						for (int v = 0; v < 4; v++) {
							const __m256d t = _mm256_load_pd(row + 4 * v);
							sa[v] = _mm256_add_pd(sa[v], _mm256_mul_pd(wx, t));
							sb[v] = _mm256_add_pd(sb[v], _mm256_mul_pd(wy, t));
						}
						row += r.block;
					}
				} else if (in_a) {
					for (INT64 i = p; i < q; i++) {
						const __m256d wx = _mm256_broadcast_sd(xa++);
						for (int v = 0; v < 4; v++) {
							sa[v] = _mm256_add_pd(sa[v], _mm256_mul_pd(wx, _mm256_load_pd(row + 4 * v)));
						}
						row += r.block;
					}
				} else if (in_b) {
					for (INT64 i = p; i < q; i++) {
						const __m256d wy = _mm256_broadcast_sd(xb++);
						for (int v = 0; v < 4; v++) {
							sb[v] = _mm256_add_pd(sb[v], _mm256_mul_pd(wy, _mm256_load_pd(row + 4 * v)));
						}
						row += r.block;
					}
				}
				s += q - p;
				if (s == r.capacity) {
					s = 0;
				}
				p = q;
			}
			// each sum holds two SplitBytes pairs
			__m128d pair[8];
			for (int v = 0; v < 4; v++) {
				pair[2 * v] = _mm256_castpd256_pd128(sa[v]);
				pair[2 * v + 1] = _mm256_extractf128_pd(sa[v], 1);
			}
			_mm_storeu_si128((__m128i *)(da + k), PackPairSums(pair));
			if (y1 > y) {
				for (int v = 0; v < 4; v++) {
					pair[2 * v] = _mm256_castpd256_pd128(sb[v]);
					pair[2 * v + 1] = _mm256_extractf128_pd(sb[v], 1);
				}
				_mm_storeu_si128((__m128i *)(db + k), PackPairSums(pair));
			}
		}
		RingTail(r, da, k, b.n, slot, a1 - a0, wa);
		if (y1 > y) {
			INT64 s = slot + (b0 - a0);
			while (s >= r.capacity) {
				s -= r.capacity;
			}
			RingTail(r, db, k, b.n, s, b1 - b0, wb);
		}
	}
}
#endif

// vertical pass for 8-bit samples by blocks of samples and runs of rows, taps in VerticalFilterSamples' order; a ring converts rows feeding several taps once
template <bool AVX2> static void
VerticalFilterRing(CWeightsTable &weightsTable, const BYTE *const src, const INT64 src_pitch, const INT64 src_row_bias, FIBITMAP *const dst, const INT64 dst_row_bias, const unsigned y_begin, const unsigned y_end, const INT64 samples, const INT64 capacity) {
	const INT64 block = VerticalBlockSize(capacity);
	const INT64 blocks = (samples + block - 1) / block;
	const INT64 items = blocks * (((INT64)y_end - (INT64)y_begin + VERTICAL_RUN - 1) / VERTICAL_RUN);
	double taps_total = 0;
	INT64 span_end = 0;
	for (unsigned y = y_begin; y < y_end; y++) {
		taps_total += weightsTable.getRightBoundary(y) - weightsTable.getLeftBoundary(y);
		span_end = MAX(span_end, (INT64)weightsTable.getRightBoundary(y));
	}
	const BOOL direct = (y_begin < y_end) && (taps_total < VERTICAL_RING_USES * (double)(span_end - (INT64)weightsTable.getLeftBoundary(y_begin)));

	#pragma omp parallel default(shared)
	{
		void *const mem = direct ? NULL : malloc((size_t)(capacity * block) * sizeof(double) + 31);
		RowRing r = { mem ? (double *)(((size_t)mem + 31) & ~(size_t)31) : NULL, capacity, block, 0, 0, 0, 0 };

		#pragma omp for schedule(dynamic)
		for (INT64 item = 0; item < items; item++) {
			const INT64 x0 = (item % blocks) * block;
			const SampleBlock b = { src + x0, src_pitch, src_row_bias, dst, dst_row_bias, x0, MIN(block, samples - x0) };
			const INT64 ya = (INT64)y_begin + (item / blocks) * VERTICAL_RUN;
			const INT64 yb = MIN((INT64)y_end, ya + VERTICAL_RUN);
#ifdef FI_RESIZE_AVX2
			if (AVX2) {
				FilterBlockAVX2(weightsTable, r, b, ya, yb);
				continue;
			}
#endif
			FilterBlock(weightsTable, r, b, ya, yb);
		}

		free(mem);
	}
}
#endif

// vertical pass for 8-bit samples: the ring kernels on x86 and x64, AVX2 where the CPU has it
template <int SPP> static void
VerticalFilterBytes(CWeightsTable &weightsTable, FIBITMAP *const src, const INT64 src_row_bias, const unsigned src_offset_x, FIBITMAP *const dst, const INT64 dst_row_bias, const unsigned y_begin, const unsigned y_end, const unsigned width) {
#ifdef FI_RESIZE_SSE2
	const BYTE *const src_bits = FreeImage_GetBits(src) + (INT64)src_offset_x * SPP;
	const INT64 src_pitch = FreeImage_GetPitch(src);
	const INT64 samples = (INT64)width * SPP;
	const INT64 window = weightsTable.getWindowSize();
#ifdef FI_RESIZE_AVX2
	// two windows, for two destination rows at a time
	if (HasAVX2() && (2 * window * VerticalBlockSize(2 * window) * (INT64)sizeof(double) <= RING_MAX_BYTES)) {
		VerticalFilterRing<true>(weightsTable, src_bits, src_pitch, src_row_bias, dst, dst_row_bias, y_begin, y_end, samples, 2 * window);
		return;
	}
#endif
	if (window * VerticalBlockSize(window) * (INT64)sizeof(double) <= RING_MAX_BYTES) {
		VerticalFilterRing<false>(weightsTable, src_bits, src_pitch, src_row_bias, dst, dst_row_bias, y_begin, y_end, samples, window);
		return;
	}
#endif
	VerticalFilterSamples<BYTE, SPP>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
}

// a nearest-neighbour resize to this many pixels or more shares its rows between threads; waking them costs more below
static const UINT64 NEAREST_PARALLEL_PIXELS = 1 << 14;

// the rows a nearest-neighbour resize reads and writes
struct NearestFrame {
	FIBITMAP *src;
	unsigned src_offset_y;
	unsigned src_height;
	FIBITMAP *dst;
	unsigned dst_width;
	unsigned dst_height;
};

// the source row from the left edge of the source rectangle, as it is
struct NearestCopy {
	size_t offset;
	unsigned width;
	unsigned bpp;
	NearestCopy(size_t o, unsigned w, unsigned b) : offset(o), width(w), bpp(b) {}
	void operator()(BYTE *dst, const BYTE *src) const {
		CopyRowPixels(dst, src + offset, width, bpp);
	}
};

// pixels of B bytes, destination column x copied from source column cols[x]
template <int B> struct NearestPixels {
	const unsigned *cols;
	unsigned width;
	NearestPixels(const unsigned *c, unsigned w) : cols(c), width(w) {}
	void operator()(BYTE *dst, const BYTE *src) const {
		for (unsigned x = 0; x < width; x++) {
			memcpy(dst, src + (size_t)cols[x] * B, B);
			dst += B;
		}
	}
};

// 1- or 4-bit pixels, packed from the high bits; the last byte keeps its bits past the row
template <int BPP> struct NearestPacked {
	const unsigned *cols;
	unsigned width;
	NearestPacked(const unsigned *c, unsigned w) : cols(c), width(w) {}
	void operator()(BYTE *dst, const BYTE *src) const {
		const unsigned per_byte = 8 / BPP;
		for (unsigned x = 0; x < width; x += per_byte) {
			const unsigned n = MIN(per_byte, width - x);
			unsigned byte = 0;
			for (unsigned k = 0; k < n; k++) {
				const unsigned c = cols[x + k];
				byte = (byte << BPP) | ((src[c / per_byte] >> ((per_byte - 1 - c % per_byte) * BPP)) & ((1 << BPP) - 1));
			}
			const unsigned bits = n * BPP;
			*dst = (BYTE)((*dst & (0xFF >> bits)) | (byte << (8 - bits)));
			dst++;
		}
	}
};

// 1-, 4- or 8-bit indices written as the first B bytes of their palette entries
template <int BPP, int B> struct NearestEntries {
	const unsigned *cols;
	unsigned width;
	const RGBQUAD *pal;
	NearestEntries(const unsigned *c, unsigned w, const RGBQUAD *p) : cols(c), width(w), pal(p) {}
	void operator()(BYTE *dst, const BYTE *src) const {
		const unsigned per_byte = 8 / BPP;
		for (unsigned x = 0; x < width; x++) {
			const unsigned c = cols[x];
			const unsigned index = (src[c / per_byte] >> ((per_byte - 1 - c % per_byte) * BPP)) & ((1 << BPP) - 1);
			memcpy(dst, &pal[index], B);
			dst += B;
		}
	}
};

// fills every row of dst; a row with the same source row as the row this thread wrote just before is copied from it
template <class GATHER> static void
NearestRows(const NearestFrame &frame, const GATHER &gather) {
	const unsigned bpp = FreeImage_GetBPP(frame.dst);
	const INT64 rows = frame.dst_height;
	const bool threaded = (rows > 1) && ((UINT64)frame.dst_width * frame.dst_height >= NEAREST_PARALLEL_PIXELS);

	#pragma omp parallel default(shared) if(threaded)
	{
		INT64 last = -1;
		unsigned last_src = 0;

		#pragma omp for schedule(static)
		for (INT64 y = 0; y < rows; y++) {
			// the source row under this row's centre, counted from the top
			const UINT64 from_top = ((2 * (UINT64)(rows - 1 - y) + 1) * frame.src_height) / (2 * (UINT64)rows);
			const unsigned src_row = frame.src_offset_y + frame.src_height - 1 - (unsigned)from_top;
			BYTE *const dst_bits = FreeImage_GetScanLine(frame.dst, (int)y);

			if ((y > 0) && (last == y - 1) && (last_src == src_row)) {
				CopyRowPixels(dst_bits, FreeImage_GetScanLine(frame.dst, (int)(y - 1)), frame.dst_width, bpp);
			} else {
				gather(dst_bits, FreeImage_GetScanLine(frame.src, src_row));
			}
			last = y;
			last_src = src_row;
		}
	}
}

// copies each pixel from the source pixel under its centre, in the source's format or as its 24/32-bit palette entry
static BOOL
ScaleNearest(FIBITMAP *const src, const unsigned src_offset_x, const unsigned src_offset_y, const unsigned src_width, const unsigned src_height, FIBITMAP *const dst, const unsigned dst_width, const unsigned dst_height) {
	const unsigned src_bpp = FreeImage_GetBPP(src);
	const unsigned dst_bpp = FreeImage_GetBPP(dst);
	const NearestFrame frame = { src, src_offset_y, src_height, dst, dst_width, dst_height };

	if (dst_bpp == src_bpp) {
		if (src_bpp <= 8) {
			// the indices keep their meaning
			memcpy(FreeImage_GetPalette(dst), FreeImage_GetPalette(src), FreeImage_GetColorsUsed(src) * sizeof(RGBQUAD));
			FreeImage_SetTransparencyTable(dst, FreeImage_GetTransparencyTable(src), FreeImage_GetTransparencyCount(src));
			FreeImage_SetTransparent(dst, FreeImage_IsTransparent(src));
		}
		const UINT64 left_bits = (UINT64)src_offset_x * src_bpp;
		if ((dst_width == src_width) && ((left_bits & 7) == 0)) {
			NearestRows(frame, NearestCopy((size_t)(left_bits >> 3), dst_width, dst_bpp));
			return TRUE;
		}
	}

	// the source column under each destination column's centre
	unsigned *const cols = (unsigned *)calloc(dst_width, sizeof(unsigned));
	if (!cols) {
		return FALSE;
	}
	for (unsigned x = 0; x < dst_width; x++) {
		cols[x] = src_offset_x + (unsigned)(((2 * (UINT64)x + 1) * src_width) / (2 * (UINT64)dst_width));
	}

	BOOL bResult = TRUE;
	if (dst_bpp != src_bpp) {
		RGBQUAD pal_buffer[256];
		const RGBQUAD *const pal = (dst_bpp == 32) ? GetRGBAPalette(src, pal_buffer) : FreeImage_GetPalette(src);
		switch (src_bpp * 100 + dst_bpp) {
			case 124:
				NearestRows(frame, NearestEntries<1, 3>(cols, dst_width, pal));
				break;
			case 132:
				NearestRows(frame, NearestEntries<1, 4>(cols, dst_width, pal));
				break;
			case 424:
				NearestRows(frame, NearestEntries<4, 3>(cols, dst_width, pal));
				break;
			case 432:
				NearestRows(frame, NearestEntries<4, 4>(cols, dst_width, pal));
				break;
			case 824:
				NearestRows(frame, NearestEntries<8, 3>(cols, dst_width, pal));
				break;
			case 832:
				NearestRows(frame, NearestEntries<8, 4>(cols, dst_width, pal));
				break;
			default:
				bResult = FALSE;
				break;
		}
	} else {
		switch (src_bpp) {
			case 1:
				NearestRows(frame, NearestPacked<1>(cols, dst_width));
				break;
			case 4:
				NearestRows(frame, NearestPacked<4>(cols, dst_width));
				break;
			case 8:
				NearestRows(frame, NearestPixels<1>(cols, dst_width));
				break;
			case 16:
				NearestRows(frame, NearestPixels<2>(cols, dst_width));
				break;
			case 24:
				NearestRows(frame, NearestPixels<3>(cols, dst_width));
				break;
			case 32:
				NearestRows(frame, NearestPixels<4>(cols, dst_width));
				break;
			case 48:
				NearestRows(frame, NearestPixels<6>(cols, dst_width));
				break;
			case 64:
				NearestRows(frame, NearestPixels<8>(cols, dst_width));
				break;
			case 96:
				NearestRows(frame, NearestPixels<12>(cols, dst_width));
				break;
			case 128:
				NearestRows(frame, NearestPixels<16>(cols, dst_width));
				break;
			default:
				bResult = FALSE;
				break;
		}
	}

	free(cols);
	return bResult;
}

// --------------------------------------------------------------------------

CWeightsTable::CWeightsTable(CGenericFilter *pFilter, unsigned uDstSize, unsigned uSrcSize) {
	double dWidth;
	double dFScale;
	const double dFilterWidth = pFilter->GetWidth();

	// scale factor
	const double dScale = double(uDstSize) / double(uSrcSize);

	m_WeightTable = NULL;
	m_WindowSize = 0;
	m_LineLength = 0;
	m_bValid = FALSE;

	if((uDstSize == 0) || (uSrcSize == 0)) {
		return;
	}

	if(dScale < 1.0) {
		// minification
		dWidth = dFilterWidth / dScale; 
		dFScale = dScale; 
	} else {
		// magnification
		dWidth = dFilterWidth; 
		dFScale = 1.0; 
	}

	// allocate a new line contributions structure
	//
	// window size is the number of sampled pixels
	// in double, capped at uSrcSize: int overflows
	const double dWindow = 2.0 * ceil(dWidth) + 1.0;
	m_WindowSize = (dWindow >= (double)uSrcSize) ? uSrcSize : (unsigned)dWindow;
	if(m_WindowSize == 0) {
		m_WindowSize = 1;
	}
	// length of dst line (no. of rows / cols) 
	m_LineLength = uDstSize; 

	 // allocate list of contributions 
	// calloc: overflow-checked, NULL Weights let the destructor run
	m_WeightTable = (Contribution*)calloc(m_LineLength, sizeof(Contribution));
	if(!m_WeightTable) {
		m_LineLength = 0;
		return;
	}
	for(unsigned u = 0; u < m_LineLength; u++) {
		// allocate contributions for every pixel
		m_WeightTable[u].Weights = (double*)malloc(m_WindowSize * sizeof(double));
		if(!m_WeightTable[u].Weights) {
			return;
		}
	}

	// offset for discrete to continuous coordinate conversion
	const double dOffset = (0.5 / dScale);

	for(unsigned u = 0; u < m_LineLength; u++) {
		// scan through line of contributions

		// inverse mapping (discrete dst 'u' to continous src 'dCenter')
		const double dCenter = (double)u / dScale + dOffset;

		// find the significant edge points that affect the pixel
		// clamp in double, then convert: the ratio is unbounded
		const double dLeft = dCenter - dWidth + 0.5;
		const double dRight = dCenter + dWidth + 0.5;
		const int iLeft = (dLeft <= 0.0) ? 0
				: ((dLeft >= (double)uSrcSize) ? (int)uSrcSize : (int)dLeft);
		const int iRight = (dRight <= 0.0) ? 0
				: ((dRight >= (double)uSrcSize) ? (int)uSrcSize : (int)dRight);

		m_WeightTable[u].Left = iLeft; 
		m_WeightTable[u].Right = iRight;

		double dTotalWeight = 0;  // sum of weights (initialized to zero)
		for(int iSrc = iLeft; iSrc < iRight; iSrc++) {
			// calculate weights
			const double weight = dFScale * pFilter->Filter(dFScale * ((double)iSrc + 0.5 - dCenter));
			// assert((iSrc-iLeft) < m_WindowSize);
			m_WeightTable[u].Weights[iSrc-iLeft] = weight;
			dTotalWeight += weight;
		}
		if((dTotalWeight > 0) && (dTotalWeight != 1)) {
			// normalize weight of neighbouring points
			for(int iSrc = iLeft; iSrc < iRight; iSrc++) {
				// normalize point
				m_WeightTable[u].Weights[iSrc-iLeft] /= dTotalWeight; 
			}
		}

		// simplify the filter, discarding null weights at the right
		{
			int iTrailing = iRight - iLeft - 1;
			while((iTrailing >= 0) && (m_WeightTable[u].Weights[iTrailing] == 0)) {
				m_WeightTable[u].Right--;
				iTrailing--;
			}
		}

	} // next dst pixel

	m_bValid = TRUE;
}

CWeightsTable::~CWeightsTable() {
	if(m_WeightTable) {
		for(unsigned u = 0; u < m_LineLength; u++) {
			// free contributions for every pixel
			free(m_WeightTable[u].Weights);
		}
		// free list of pixels contributions
		free(m_WeightTable);
	}
}

// --------------------------------------------------------------------------

FIBITMAP* CResizeEngine::scale(FIBITMAP *src, unsigned dst_width, unsigned dst_height, unsigned src_left, unsigned src_top, unsigned src_width, unsigned src_height, unsigned flags, BOOL rawBits, int dst_pitch, BYTE *dst_bits) {

	const FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(src);
	const unsigned src_bpp = FreeImage_GetBPP(src);

	// determine the image's color type
	BOOL bIsGreyscale = FALSE;
	FREE_IMAGE_COLOR_TYPE color_type;
	if (src_bpp <= 8) {
		color_type = GetExtendedColorType(src, &bIsGreyscale);
		if (src_bpp == 4) {
			// 4-bit always needs its palette: only it says what an index means
			color_type = FIC_PALETTE;
		}
	} else {
		color_type = FIC_RGB;
	}

	// determine the required bit depth of the destination image
	unsigned dst_bpp;
	unsigned dst_bpp_s1 = 0;
	if (!m_pFilter) {
		// nearest copies pixels; only FI_RESCALE_TRUE_COLOR changes the format, to what the filters return with it
		dst_bpp = src_bpp;
		if ((src_bpp <= 8) && ((flags & FI_RESCALE_TRUE_COLOR) == FI_RESCALE_TRUE_COLOR)) {
			dst_bpp = FreeImage_IsTransparent(src) ? 32 : 24;
		}
	} else if (color_type == FIC_PALETTE && !bIsGreyscale) {
		// non greyscale FIC_PALETTE images require a high-color destination
		// image (24- or 32-bits depending on the image's transparent state)
		dst_bpp = FreeImage_IsTransparent(src) ? 32 : 24;
	} else if (src_bpp <= 8) {
		// greyscale images require an 8-bit destination image
		// (or a 32-bit image if the image is transparent);
		// however, if flag FI_RESCALE_TRUE_COLOR is set, we will return
		// a true color (24 bpp) image
		if (FreeImage_IsTransparent(src)) {
			dst_bpp = 32;
			// additionally, for transparent images we always need a
			// palette including transparency information (an RGBA palette)
			// so, set color_type accordingly
			color_type = FIC_PALETTE;
		} else {
			dst_bpp = ((flags & FI_RESCALE_TRUE_COLOR) == FI_RESCALE_TRUE_COLOR) ? 24 : 8;
			// in any case, we use a fast 8-bit temporary image for the
			// first filter operation (stage 1, either horizontal or
			// vertical) and implicitly convert to 24 bpp (if requested
			// by flag FI_RESCALE_TRUE_COLOR) during the second filter
			// operation
			dst_bpp_s1 = 8;
		}
	} else if (src_bpp == 16 && image_type == FIT_BITMAP) {
		// 16-bit 555 and 565 RGB images require a high-color destination
		// image (fixed to 24 bits, since 16-bit RGBs don't support
		// transparency in FreeImage)
		dst_bpp = 24;
	} else {
		// bit depth remains unchanged for all other images
		dst_bpp = src_bpp;
	}

	// make 'stage 1' bpp a copy of the destination bpp if it
	// was not explicitly set
	if (dst_bpp_s1 == 0) {
		dst_bpp_s1 = dst_bpp;
	}

	if (rawBits && (dst_bpp != src_bpp)) {
		// the caller's buffer is laid out for src_bpp
		return NULL;
	}

	// early exit if destination size is equal to source size
	if ((src_width == dst_width) && (src_height == dst_height)) {
		FIBITMAP *out = src;
		FIBITMAP *tmp = src;
		if ((src_width != FreeImage_GetWidth(src)) || (src_height != FreeImage_GetHeight(src))) {
			out = FreeImage_Copy(tmp, src_left, src_top, src_left + src_width, src_top + src_height);
			tmp = out;
		}
		if (src_bpp != dst_bpp) {
			switch (dst_bpp) {
				case 8:
					out = FreeImage_ConvertToGreyscale(tmp);
					break;
				case 24:
					out = FreeImage_ConvertTo24Bits(tmp);
					break;
				case 32:
					out = FreeImage_ConvertTo32Bits(tmp);
					break;
				default:
					break;
			}
			if (tmp != src) {
				FreeImage_Unload(tmp);
				tmp = NULL;
			}
		}

		return (out != src) ? out : FreeImage_Clone(src);
	}

	// the filters handle these image types only: any other would come out blank
	switch (image_type) {
		case FIT_BITMAP:
		case FIT_UINT16:
		case FIT_INT16:
		case FIT_UINT32:
		case FIT_INT32:
		case FIT_FLOAT:
		case FIT_DOUBLE:
		case FIT_COMPLEX:
		case FIT_RGB16:
		case FIT_RGBA16:
		case FIT_RGBF:
		case FIT_RGBAF:
			break;
		default:
			return NULL;
	}

	RGBQUAD pal_buffer[256];
	RGBQUAD *src_pal = NULL;

	// provide the source image's palette to the rescaler for
	// FIC_PALETTE type images (this includes palletized greyscale
	// images with an unordered palette as well as transparent images)
	// also MINISWHITE when dst has no palette to carry the inversion
	if ((color_type == FIC_PALETTE) || ((color_type == FIC_MINISWHITE) && (dst_bpp != 8))) {
		if (dst_bpp == 32) {
			// a 32-bit destination image signals transparency, so
			// create an RGBA palette from the source palette
			src_pal = GetRGBAPalette(src, pal_buffer);
		} else {
			src_pal = FreeImage_GetPalette(src);
		}
	}

   // allocate the dst image
   FIBITMAP *dst = NULL;
   if (rawBits==1) {
      // dib = FreeImage_AllocateHeader(header_only, header.is_width, header.is_height, pixel_bits, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
      dst = FreeImage_AllocateHeaderForBits(dst_bits, dst_pitch, image_type, dst_width, dst_height, dst_bpp, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
   } else {
      dst = FreeImage_AllocateT(image_type, dst_width, dst_height, dst_bpp, FreeImage_GetRedMask(src), FreeImage_GetGreenMask(src), FreeImage_GetBlueMask(src));
   }
   if (!dst) {
      return NULL;
   }

	if (dst_bpp == 8) {
		RGBQUAD * const dst_pal = FreeImage_GetPalette(dst);
		if (color_type == FIC_MINISWHITE) {
			// build an inverted greyscale palette
			CREATE_GREYSCALE_PALETTE_REVERSE(dst_pal, 256);
		} 
		/*
		else {
			// build a default greyscale palette
			// Currently, FreeImage_AllocateT already creates a default
			// greyscale palette for 8 bpp images, so we can skip this here.
			CREATE_GREYSCALE_PALETTE(dst_pal, 256);
		}
		*/
	}

	// calculate x and y offsets; since FreeImage uses bottom-up bitmaps, the
	// value of src_offset_y is measured from the bottom of the image
	const unsigned src_offset_x = src_left;
	const unsigned src_offset_y = FreeImage_GetHeight(src) - src_height - src_top;

	BOOL bResult;
	if (!m_pFilter) {
		bResult = ScaleNearest(src, src_offset_x, src_offset_y, src_width, src_height, dst, dst_width, dst_height);
	} else if (src_height == dst_height) {
		CWeightsTable weightsTable(m_pFilter, dst_width, src_width);
		bResult = weightsTable.isValid();
		if (bResult) {
			horizontalFilter(weightsTable, src, src_offset_y, src_offset_x, src_pal, dst, 0, src_height, dst_width);
		}
	} else if (src_width == dst_width) {
		CWeightsTable weightsTable(m_pFilter, dst_height, src_height);
		bResult = weightsTable.isValid();
		if (bResult) {
			verticalFilter(weightsTable, src, src_offset_y, src_offset_x, src_pal, dst, 0, 0, dst_height, dst_width);
		}
	} else {
		bResult = scaleInBands(src, src_offset_x, src_offset_y, src_width, src_height, src_pal, dst, dst_width, dst_height, dst_bpp_s1);
	}

	if (!bResult) {
		FreeImage_Unload(dst);
		return NULL;
	}
	return dst;
}

BOOL CResizeEngine::scaleInBands(FIBITMAP *const src, const unsigned src_offset_x, const unsigned src_offset_y, const unsigned src_width, const unsigned src_height, const RGBQUAD *const src_pal, FIBITMAP *const dst, const unsigned dst_width, const unsigned dst_height, const unsigned tmp_bpp) {
	CWeightsTable weightsX(m_pFilter, dst_width, src_width);
	CWeightsTable weightsY(m_pFilter, dst_height, src_height);
	if (!weightsX.isValid() || !weightsY.isValid()) {
		return FALSE;
	}
	const FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(src);

	// horizontal first unless the width grows, or the other order when its taps are clearly cheaper; the byte kernels take the cheaper one
	const double taps_x = TotalTaps(weightsX, dst_width), taps_y = TotalTaps(weightsY, dst_height);
	double cost_xy, cost_yx, margin;
	if (ByteKernels(image_type, FreeImage_GetBPP(src), src_pal, tmp_bpp, FreeImage_GetBPP(dst))) {
		cost_xy = (BYTE_HORIZONTAL_TAP_COST * taps_x + BYTE_HORIZONTAL_PIXEL_COST * dst_width) * src_height + taps_y * dst_width;
		cost_yx = taps_y * src_width + (BYTE_HORIZONTAL_TAP_COST * taps_x + BYTE_HORIZONTAL_PIXEL_COST * dst_width) * dst_height;
		margin = 1;
	} else {
		cost_xy = HORIZONTAL_TAP_COST * taps_x * src_height + VerticalTapCost(image_type, tmp_bpp, NULL, FreeImage_GetBPP(dst)) * taps_y * dst_width;
		cost_yx = VerticalTapCost(image_type, FreeImage_GetBPP(src), src_pal, tmp_bpp) * taps_y * src_width + HORIZONTAL_TAP_COST * taps_x * dst_height;
		margin = (FreeImage_GetBPP(src) >= 128) ? WIDE_ORDER_MARGIN : ORDER_MARGIN;
	}
	const BOOL xy = (dst_width <= src_width) ? (cost_xy <= margin * cost_yx) : (cost_yx > margin * cost_xy);

	if (xy) {
		// xy filtering: a destination row reads a window of filtered rows, and neighbouring windows overlap
		unsigned window = 0;
		for (unsigned y = 0; y < dst_height; y++) {
			window = MAX(window, weightsY.getRightBoundary(y) - weightsY.getLeftBoundary(y));
		}
		FIBITMAP *tmp = FreeImage_AllocateT(image_type, dst_width, BandRows(dst_width, tmp_bpp, window, src_height), tmp_bpp, 0, 0, 0);
		if (!tmp) {
			return FALSE;
		}
		const unsigned capacity = FreeImage_GetHeight(tmp);
		const size_t pitch = FreeImage_GetPitch(tmp);

		// filtered rows [held_first, held_last) of the source rectangle, from the start of tmp
		unsigned held_first = 0, held_last = 0;
		for (unsigned y0 = 0; y0 < dst_height; ) {
			// as many destination rows as their filtered rows fit in tmp; the right boundaries are not monotonic
			unsigned first = weightsY.getLeftBoundary(y0), last = weightsY.getRightBoundary(y0), y1 = y0 + 1;
			for (; y1 < dst_height; y1++) {
				const unsigned f = MIN(first, weightsY.getLeftBoundary(y1));
				const unsigned l = MAX(last, weightsY.getRightBoundary(y1));
				if (l - f > capacity) {
					break;
				}
				first = f;
				last = l;
			}
			// rows the previous band filtered move to the start instead of being filtered again
			unsigned kept = 0;
			if ((first >= held_first) && (first < held_last)) {
				kept = held_last - first;
				if (first > held_first) {
					memmove(FreeImage_GetBits(tmp), FreeImage_GetScanLine(tmp, first - held_first), kept * pitch);
				}
			}
			if (first + kept < last) {
				horizontalFilter(weightsX, src, src_offset_y + first + kept, src_offset_x, src_pal, tmp, kept, last - first - kept, dst_width);
				kept = last - first;
			}
			held_first = first;
			held_last = first + kept;

			verticalFilter(weightsY, tmp, -(INT64)first, 0, NULL, dst, 0, y0, y1, dst_width);
			y0 = y1;
		}
		FreeImage_Unload(tmp);
	} else {
		// yx filtering: each filtered row feeds one destination row
		FIBITMAP *tmp = FreeImage_AllocateT(image_type, src_width, BandRows(src_width, tmp_bpp, 1, dst_height), tmp_bpp, 0, 0, 0);
		if (!tmp) {
			return FALSE;
		}
		const unsigned capacity = FreeImage_GetHeight(tmp);
		for (unsigned y0 = 0; y0 < dst_height; y0 += capacity) {
			const unsigned y1 = MIN(dst_height, y0 + capacity);
			verticalFilter(weightsY, src, src_offset_y, src_offset_x, src_pal, tmp, -(INT64)y0, y0, y1, src_width);
			horizontalFilter(weightsX, tmp, 0, 0, NULL, dst, y0, y1 - y0, dst_width);
		}
		FreeImage_Unload(tmp);
	}

	return TRUE;
}

void CResizeEngine::horizontalFilter(CWeightsTable &weightsTable, FIBITMAP *const src, unsigned src_row, unsigned src_offset_x, const RGBQUAD *const src_pal, FIBITMAP *const dst, unsigned dst_row, unsigned rows, unsigned dst_width) {

   // step through rows
   switch(FreeImage_GetImageType(src)) {
      case FIT_BITMAP:
      {
         switch(FreeImage_GetBPP(src)) {
            case 1:
            {
               switch(FreeImage_GetBPP(dst)) {
                  case 8:
                  {
                     // transparently convert the 1-bit non-transparent greyscale image to 8 bpp
                     // keep the bit remainder of the left edge
                     const INT64 src_bit_offset = src_offset_x & 0x07;
                     src_offset_x >>= 3;
                     if (src_pal) {
                        // we have got a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 y = 0; y < rows; y++) {
                           // scale each row
                           const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                           BYTE * const dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                           for (INT64 x = 0; x < dst_width; x++) {
                              // loop through row
                              const INT64 iLeft = weightsTable.getLeftBoundary(x);      // retrieve left boundary
                              const INT64 iRight = weightsTable.getRightBoundary(x);   // retrieve right boundary
                              double value = 0;

                              for (INT64 i = iLeft; i < iRight; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 const INT64 isrc = i + src_bit_offset;
                                 const INT64 pixel = (src_bits[isrc >> 3] & (0x80 >> (isrc & 0x07))) != 0;
                                 value += (weightsTable.getWeight(x, i - iLeft) * (double)*(BYTE *)&src_pal[pixel]);
                              }

                              // clamp and place result in destination pixel
                              dst_bits[x] = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                           }
                        }
                     } else {
                        // we do not have a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 y = 0; y < rows; y++) {
                           // scale each row
                           const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                           BYTE * const dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                           for (INT64 x = 0; x < dst_width; x++) {
                              // loop through row
                              const INT64 iLeft = weightsTable.getLeftBoundary(x);      // retrieve left boundary
                              const INT64 iRight = weightsTable.getRightBoundary(x);   // retrieve right boundary
                              double value = 0;

                              for (INT64 i = iLeft; i < iRight; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 const INT64 isrc = i + src_bit_offset;
                                 const INT64 pixel = (src_bits[isrc >> 3] & (0x80 >> (isrc & 0x07))) != 0;
                                 value += (weightsTable.getWeight(x, i - iLeft) * (double)pixel);
                              }
                              value *= 0xFF;

                              // clamp and place result in destination pixel
                              dst_bits[x] = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                           }
                        }
                     }
                  }
                  break;

                  case 24:
                  {
                     // transparently convert the non-transparent 1-bit image to 24 bpp
                     // keep the bit remainder of the left edge
                     const INT64 src_bit_offset = src_offset_x & 0x07;
                     src_offset_x >>= 3;
                     if (src_pal) {
                        // we have got a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 y = 0; y < rows; y++) {
                           // scale each row
                           const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                           BYTE *dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                           for (INT64 x = 0; x < dst_width; x++) {
                              // loop through row
                              const INT64 iLeft = weightsTable.getLeftBoundary(x);    // retrieve left boundary
                              const INT64 iRight = weightsTable.getRightBoundary(x);  // retrieve right boundary
                              double r = 0, g = 0, b = 0;

                              for (INT64 i = iLeft; i < iRight; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 const double weight = weightsTable.getWeight(x, i - iLeft);
                                 const INT64 isrc = i + src_bit_offset;
                                 const INT64 pixel = (src_bits[isrc >> 3] & (0x80 >> (isrc & 0x07))) != 0;
                                 const BYTE * const entry = (BYTE *)&src_pal[pixel];
                                 r += (weight * (double)entry[FI_RGBA_RED]);
                                 g += (weight * (double)entry[FI_RGBA_GREEN]);
                                 b += (weight * (double)entry[FI_RGBA_BLUE]);
                              }

                              // clamp and place result in destination pixel
                              dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                              dst_bits += 3;
                           }
                        }
                     } else {
                        // we do not have a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 y = 0; y < rows; y++) {
                           // scale each row
                           const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                           BYTE *dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                           for (INT64 x = 0; x < dst_width; x++) {
                              // loop through row
                              const INT64 iLeft = weightsTable.getLeftBoundary(x);    // retrieve left boundary
                              const INT64 iRight = weightsTable.getRightBoundary(x);  // retrieve right boundary
                              double value = 0;

                              for (INT64 i = iLeft; i < iRight; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 const INT64 isrc = i + src_bit_offset;
                                 const INT64 pixel = (src_bits[isrc >> 3] & (0x80 >> (isrc & 0x07))) != 0;
                                 value += (weightsTable.getWeight(x, i - iLeft) * (double)pixel);
                              }
                              value *= 0xFF;

                              // clamp and place result in destination pixel
                              const BYTE bval = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_RED]   = bval;
                              dst_bits[FI_RGBA_GREEN]   = bval;
                              dst_bits[FI_RGBA_BLUE]   = bval;
                              dst_bits += 3;
                           }
                        }
                     }
                  }
                  break;

                  case 32:
                  {
                     // transparently convert the transparent 1-bit image to 32 bpp; 
                     // we always have got a palette here
                     // keep the bit remainder of the left edge
                     const INT64 src_bit_offset = src_offset_x & 0x07;
                     src_offset_x >>= 3;
                     #pragma omp parallel for schedule(dynamic) default(shared)
                     for (INT64 y = 0; y < rows; y++) {
                        // scale each row
                        const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                        BYTE *dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                        for (INT64 x = 0; x < dst_width; x++) {
                           // loop through row
                           const INT64 iLeft = weightsTable.getLeftBoundary(x);    // retrieve left boundary
                           const INT64 iRight = weightsTable.getRightBoundary(x);  // retrieve right boundary
                           double r = 0, g = 0, b = 0, a = 0;

                           for (INT64 i = iLeft; i < iRight; i++) {
                              // scan between boundaries
                              // accumulate weighted effect of each neighboring pixel
                              const double weight = weightsTable.getWeight(x, i - iLeft);
                              const INT64 isrc = i + src_bit_offset;
                              const INT64 pixel = (src_bits[isrc >> 3] & (0x80 >> (isrc & 0x07))) != 0;
                              const BYTE * const entry = (BYTE *)&src_pal[pixel];
                              r += (weight * (double)entry[FI_RGBA_RED]);
                              g += (weight * (double)entry[FI_RGBA_GREEN]);
                              b += (weight * (double)entry[FI_RGBA_BLUE]);
                              a += (weight * (double)entry[FI_RGBA_ALPHA]);
                           }

                           // clamp and place result in destination pixel
                           dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_ALPHA]   = (BYTE)CLAMP<int>((int)(a + 0.5), 0, 0xFF);
                           dst_bits += 4;
                        }
                     }
                  }
                  break;
               }
            }
            break;

            case 4:
            {
               switch(FreeImage_GetBPP(dst)) {
                  case 8:
                  {
                     // transparently convert the non-transparent 4-bit greyscale image to 8 bpp; 
                     // we always have got a palette for 4-bit images
                     // keep the nibble remainder of the left edge
                     const INT64 src_nibble_offset = src_offset_x & 0x01;
                     src_offset_x >>= 1;
                     #pragma omp parallel for schedule(dynamic) default(shared)
                     for (INT64 y = 0; y < rows; y++) {
                        // scale each row
                        const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                        BYTE * const dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                        for (INT64 x = 0; x < dst_width; x++) {
                           // loop through row
                           const INT64 iLeft = weightsTable.getLeftBoundary(x);    // retrieve left boundary
                           const INT64 iRight = weightsTable.getRightBoundary(x);  // retrieve right boundary
                           double value = 0;

                           for (INT64 i = iLeft; i < iRight; i++) {
                              // scan between boundaries
                              // accumulate weighted effect of each neighboring pixel
                              const INT64 isrc = i + src_nibble_offset;
                              const INT64 pixel = isrc & 0x01 ? src_bits[isrc >> 1] & 0x0F : src_bits[isrc >> 1] >> 4;
                              value += (weightsTable.getWeight(x, i - iLeft) * (double)*(BYTE *)&src_pal[pixel]);
                           }

                           // clamp and place result in destination pixel
                           dst_bits[x] = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                        }
                     }
                  }
                  break;

                  case 24:
                  {
                     // transparently convert the non-transparent 4-bit image to 24 bpp; 
                     // we always have got a palette for 4-bit images
                     // keep the nibble remainder of the left edge
                     const INT64 src_nibble_offset = src_offset_x & 0x01;
                     src_offset_x >>= 1;
                     #pragma omp parallel for schedule(dynamic) default(shared)
                     for (INT64 y = 0; y < rows; y++) {
                        // scale each row
                        const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                        BYTE *dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                        for (INT64 x = 0; x < dst_width; x++) {
                           // loop through row
                           const INT64 iLeft = weightsTable.getLeftBoundary(x);    // retrieve left boundary
                           const INT64 iRight = weightsTable.getRightBoundary(x);  // retrieve right boundary
                           double r = 0, g = 0, b = 0;

                           for (INT64 i = iLeft; i < iRight; i++) {
                              // scan between boundaries
                              // accumulate weighted effect of each neighboring pixel
                              const double weight = weightsTable.getWeight(x, i - iLeft);
                              const INT64 isrc = i + src_nibble_offset;
                              const INT64 pixel = isrc & 0x01 ? src_bits[isrc >> 1] & 0x0F : src_bits[isrc >> 1] >> 4;
                              const BYTE * const entry = (BYTE *)&src_pal[pixel];
                              r += (weight * (double)entry[FI_RGBA_RED]);
                              g += (weight * (double)entry[FI_RGBA_GREEN]);
                              b += (weight * (double)entry[FI_RGBA_BLUE]);
                           }

                           // clamp and place result in destination pixel
                           dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                           dst_bits += 3;
                        }
                     }
                  }
                  break;

                  case 32:
                  {
                     // transparently convert the transparent 4-bit image to 32 bpp; 
                     // we always have got a palette for 4-bit images
                     // keep the nibble remainder of the left edge
                     const INT64 src_nibble_offset = src_offset_x & 0x01;
                     src_offset_x >>= 1;
                     #pragma omp parallel for schedule(dynamic) default(shared)
                     for (INT64 y = 0; y < rows; y++) {
                        // scale each row
                        const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                        BYTE *dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                        for (INT64 x = 0; x < dst_width; x++) {
                           // loop through row
                           const INT64 iLeft = weightsTable.getLeftBoundary(x);    // retrieve left boundary
                           const INT64 iRight = weightsTable.getRightBoundary(x);  // retrieve right boundary
                           double r = 0, g = 0, b = 0, a = 0;

                           for (INT64 i = iLeft; i < iRight; i++) {
                              // scan between boundaries
                              // accumulate weighted effect of each neighboring pixel
                              const double weight = weightsTable.getWeight(x, i - iLeft);
                              const INT64 isrc = i + src_nibble_offset;
                              const INT64 pixel = isrc & 0x01 ? src_bits[isrc >> 1] & 0x0F : src_bits[isrc >> 1] >> 4;
                              const BYTE * const entry = (BYTE *)&src_pal[pixel];
                              r += (weight * (double)entry[FI_RGBA_RED]);
                              g += (weight * (double)entry[FI_RGBA_GREEN]);
                              b += (weight * (double)entry[FI_RGBA_BLUE]);
                              a += (weight * (double)entry[FI_RGBA_ALPHA]);
                           }

                           // clamp and place result in destination pixel
                           dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_ALPHA]   = (BYTE)CLAMP<int>((int)(a + 0.5), 0, 0xFF);
                           dst_bits += 4;
                        }
                     }
                  }
                  break;
               }
            }
            break;

            case 8:
            {
               switch(FreeImage_GetBPP(dst)) {
                  case 8:
                  {
                     // scale the 8-bit non-transparent greyscale image
                     // into an 8 bpp destination image
                     if (src_pal) {
                        // we have got a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 y = 0; y < rows; y++) {
                           // scale each row
                           const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                           BYTE * const dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                           for (INT64 x = 0; x < dst_width; x++) {
                              // loop through row
                              const INT64 iLeft = weightsTable.getLeftBoundary(x);            // retrieve left boundary
                              const INT64 iLimit = weightsTable.getRightBoundary(x) - iLeft;   // retrieve right boundary
                              const BYTE * const pixel = src_bits + iLeft;
                              double value = 0;

                              // for(i = iLeft to iRight)
                              for (INT64 i = 0; i < iLimit; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 value += (weightsTable.getWeight(x, i) * (double)*(BYTE *)&src_pal[pixel[i]]);
                              }

                              // clamp and place result in destination pixel
                              dst_bits[x] = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                           }
                        }
                     } else {
                        // we do not have a palette
                        HorizontalFilterBytes<1>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
                     }
                  }
                  break;

                  case 24:
                  {
                     // transparently convert the non-transparent 8-bit image to 24 bpp
                     if (src_pal) {
                        // we have got a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 y = 0; y < rows; y++) {
                           // scale each row
                           const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                           BYTE *dst_bits = FreeImage_GetScanLine(dst, dst_row + y);
                           for (INT64 x = 0; x < dst_width; x++) {
                              // loop through row
                              const INT64 iLeft = weightsTable.getLeftBoundary(x);            // retrieve left boundary
                              const INT64 iLimit = weightsTable.getRightBoundary(x) - iLeft;   // retrieve right boundary
                              const BYTE * const pixel = src_bits + iLeft;
                              double r = 0, g = 0, b = 0;

                              // for(i = iLeft to iRight)
                              for (INT64 i = 0; i < iLimit; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 const double weight = weightsTable.getWeight(x, i);
                                 const BYTE *const entry = (BYTE *)&src_pal[pixel[i]];
                                 r += (weight * (double)entry[FI_RGBA_RED]);
                                 g += (weight * (double)entry[FI_RGBA_GREEN]);
                                 b += (weight * (double)entry[FI_RGBA_BLUE]);
                              }

                              // clamp and place result in destination pixel
                              dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                              dst_bits += 3;
                           }
                        }
                     } else {
                        // we do not have a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 y = 0; y < rows; y++) {
                           // scale each row
                           const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                           BYTE *dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                           for (INT64 x = 0; x < dst_width; x++) {
                              // loop through row
                              const INT64 iLeft = weightsTable.getLeftBoundary(x);            // retrieve left boundary
                              const INT64 iLimit = weightsTable.getRightBoundary(x) - iLeft;   // retrieve right boundary
                              const BYTE * const pixel = src_bits + iLeft;
                              double value = 0;

                              // for(i = iLeft to iRight)
                              for (INT64 i = 0; i < iLimit; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 const double weight = weightsTable.getWeight(x, i);
                                 value += (weight * (double)pixel[i]);
                              }

                              // clamp and place result in destination pixel
                              const BYTE bval = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_RED]   = bval;
                              dst_bits[FI_RGBA_GREEN]   = bval;
                              dst_bits[FI_RGBA_BLUE]   = bval;
                              dst_bits += 3;
                           }
                        }
                     }
                  }
                  break;

                  case 32:
                  {
                     // transparently convert the transparent 8-bit image to 32 bpp; 
                     // we always have got a palette here
                     #pragma omp parallel for schedule(dynamic) default(shared)
                     for (INT64 y = 0; y < rows; y++) {
                        // scale each row
                        const BYTE * const src_bits = FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                        BYTE *dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                        for (INT64 x = 0; x < dst_width; x++) {
                           // loop through row
                           const INT64 iLeft = weightsTable.getLeftBoundary(x);            // retrieve left boundary
                           const INT64 iLimit = weightsTable.getRightBoundary(x) - iLeft;   // retrieve right boundary
                           const BYTE * const pixel = src_bits + iLeft;
                           double r = 0, g = 0, b = 0, a = 0;

                           // for(i = iLeft to iRight)
                           for (INT64 i = 0; i < iLimit; i++) {
                              // scan between boundaries
                              // accumulate weighted effect of each neighboring pixel
                              const double weight = weightsTable.getWeight(x, i);
                              const BYTE * const entry = (BYTE *)&src_pal[pixel[i]];
                              r += (weight * (double)entry[FI_RGBA_RED]);
                              g += (weight * (double)entry[FI_RGBA_GREEN]);
                              b += (weight * (double)entry[FI_RGBA_BLUE]);
                              a += (weight * (double)entry[FI_RGBA_ALPHA]);
                           }

                           // clamp and place result in destination pixel
                           dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_ALPHA]   = (BYTE)CLAMP<int>((int)(a + 0.5), 0, 0xFF);
                           dst_bits += 4;
                        }
                     }
                  }
                  break;
               }
            }
            break;

            case 16:
            {
               // transparently convert the 16-bit non-transparent image to 24 bpp
               if (IS_FORMAT_RGB565(src)) {
                  // image has 565 format
                  #pragma omp parallel for schedule(dynamic) default(shared)
                  for (INT64 y = 0; y < rows; y++) {
                     // scale each row
                     const WORD * const src_bits = (WORD *)FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                     BYTE *dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                     for (INT64 x = 0; x < dst_width; x++) {
                        // loop through row
                        const INT64 iLeft = weightsTable.getLeftBoundary(x);            // retrieve left boundary
                        const INT64 iLimit = weightsTable.getRightBoundary(x) - iLeft;   // retrieve right boundary
                        const WORD *pixel = src_bits + iLeft;
                        double r = 0, g = 0, b = 0;

                        // for(i = iLeft to iRight)
                        for (INT64 i = 0; i < iLimit; i++) {
                           // scan between boundaries
                           // accumulate weighted effect of each neighboring pixel
                           const double weight = weightsTable.getWeight(x, i);
                           r += (weight * (double)((*pixel & FI16_565_RED_MASK) >> FI16_565_RED_SHIFT));
                           g += (weight * (double)((*pixel & FI16_565_GREEN_MASK) >> FI16_565_GREEN_SHIFT));
                           b += (weight * (double)((*pixel & FI16_565_BLUE_MASK) >> FI16_565_BLUE_SHIFT));
                           pixel++;
                        }

                        // clamp and place result in destination pixel
                        dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(((r * 0xFF) / 0x1F) + 0.5), 0, 0xFF);
                        dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(((g * 0xFF) / 0x3F) + 0.5), 0, 0xFF);
                        dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(((b * 0xFF) / 0x1F) + 0.5), 0, 0xFF);
                        dst_bits += 3;
                     }
                  }
               } else {
                  // image has 555 format
                  #pragma omp parallel for schedule(dynamic) default(shared)
                  for (INT64 y = 0; y < rows; y++) {
                     // scale each row
                     const WORD * const src_bits = (WORD *)FreeImage_GetScanLine(src, src_row + y) + src_offset_x;
                     BYTE *dst_bits = FreeImage_GetScanLine(dst, dst_row + y);

                     for (INT64 x = 0; x < dst_width; x++) {
                        // loop through row
                        const INT64 iLeft = weightsTable.getLeftBoundary(x);            // retrieve left boundary
                        const INT64 iLimit = weightsTable.getRightBoundary(x) - iLeft;   // retrieve right boundary
                        const WORD *pixel = src_bits + iLeft;
                        double r = 0, g = 0, b = 0;

                        // for(i = iLeft to iRight)
                        for (INT64 i = 0; i < iLimit; i++) {
                           // scan between boundaries
                           // accumulate weighted effect of each neighboring pixel
                           const double weight = weightsTable.getWeight(x, i);
                           r += (weight * (double)((*pixel & FI16_555_RED_MASK) >> FI16_555_RED_SHIFT));
                           g += (weight * (double)((*pixel & FI16_555_GREEN_MASK) >> FI16_555_GREEN_SHIFT));
                           b += (weight * (double)((*pixel & FI16_555_BLUE_MASK) >> FI16_555_BLUE_SHIFT));
                           pixel++;
                        }

                        // clamp and place result in destination pixel
                        dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(((r * 0xFF) / 0x1F) + 0.5), 0, 0xFF);
                        dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(((g * 0xFF) / 0x1F) + 0.5), 0, 0xFF);
                        dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(((b * 0xFF) / 0x1F) + 0.5), 0, 0xFF);
                        dst_bits += 3;
                     }
                  }
               }
            }
            break;

            case 24:
               HorizontalFilterBytes<3>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
               break;

            case 32:
               HorizontalFilterBytes<4>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
               break;
         }
      }
      break;

      case FIT_UINT16:
         HorizontalFilterSamples<WORD, 1>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
         break;

      case FIT_RGB16:
         HorizontalFilterSamples<WORD, 3>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
         break;

      case FIT_RGBA16:
         HorizontalFilterSamples<WORD, 4>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
         break;

      case FIT_FLOAT:
         HorizontalFilterSamples<float, 1>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
         break;

      case FIT_RGBF:
         HorizontalFilterSamples<float, 3>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
         break;

      case FIT_RGBAF:
         HorizontalFilterSamples<float, 4>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
         break;

      case FIT_INT16:
         HorizontalFilterSamples<short, 1>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
         break;

      case FIT_UINT32:
         HorizontalFilterSamples<DWORD, 1>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
         break;

      case FIT_INT32:
         HorizontalFilterSamples<LONG, 1>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
         break;

      case FIT_DOUBLE:
         HorizontalFilterSamples<double, 1>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
         break;

      case FIT_COMPLEX:
         // real and imaginary parts: the weights are real
         HorizontalFilterSamples<double, 2>(weightsTable, src, src_row, src_offset_x, dst, dst_row, rows, dst_width);
         break;
   }
}

/// Performs vertical image filtering
void CResizeEngine::verticalFilter(CWeightsTable &weightsTable, FIBITMAP *const src, INT64 src_row_bias, unsigned src_offset_x, const RGBQUAD *const src_pal, FIBITMAP *const dst, INT64 dst_row_bias, unsigned y_begin, unsigned y_end, unsigned width) {

   // step through columns
   switch(FreeImage_GetImageType(src)) {
      case FIT_BITMAP:
      {
         const INT64 dst_pitch = FreeImage_GetPitch(dst);
         BYTE * const dst_base = FreeImage_GetBits(dst) + (dst_row_bias + y_begin) * dst_pitch;

         switch(FreeImage_GetBPP(src)) {
            case 1:
            {
               const INT64 src_pitch = FreeImage_GetPitch(src);
               const BYTE * const src_base = FreeImage_GetBits(src) + (src_offset_x >> 3);
               // src_base is byte-aligned: carry the dropped bits
               const INT64 src_bit_offset = src_offset_x & 0x07;

               switch(FreeImage_GetBPP(dst)) {
                  case 8:
                  {
                     // transparently convert the 1-bit non-transparent greyscale image to 8 bpp
                     if (src_pal) {
                        // we have got a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 x = 0; x < width; x++) {
                           // work on column x in dst
                           BYTE *dst_bits = dst_base + x;
                           const INT64 index = (x + src_bit_offset) >> 3;
                           const INT64 mask = 0x80 >> ((x + src_bit_offset) & 0x07);

                           // scale each column
                           for (INT64 y = y_begin; y < y_end; y++) {
                              // loop through column
                              const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                              const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                              const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + index;
                              double value = 0;

                              for (INT64 i = 0; i < iLimit; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 const INT64 pixel = (*src_bits & mask) != 0;
                                 value += (weightsTable.getWeight(y, i) * (double)*(BYTE *)&src_pal[pixel]);
                                 src_bits += src_pitch;
                              }
                              // already 0..255: averaged palette bytes, not bits

                              // clamp and place result in destination pixel
                              *dst_bits = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                              dst_bits += dst_pitch;
                           }
                        }
                     } else {
                        // we do not have a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 x = 0; x < width; x++) {
                           // work on column x in dst
                           BYTE *dst_bits = dst_base + x;
                           const INT64 index = (x + src_bit_offset) >> 3;
                           const INT64 mask = 0x80 >> ((x + src_bit_offset) & 0x07);

                           // scale each column
                           for (INT64 y = y_begin; y < y_end; y++) {
                              // loop through column
                              const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                              const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                              const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + index;
                              double value = 0;

                              for (INT64 i = 0; i < iLimit; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 value += (weightsTable.getWeight(y, i) * (double)((*src_bits & mask) != 0));
                                 src_bits += src_pitch;
                              }
                              value *= 0xFF;

                              // clamp and place result in destination pixel
                              *dst_bits = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                              dst_bits += dst_pitch;
                           }
                        }
                     }
                  }
                  break;

                  case 24:
                  {
                     // transparently convert the non-transparent 1-bit image to 24 bpp
                     if (src_pal) {
                        // we have got a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 x = 0; x < width; x++) {
                           // work on column x in dst
                           BYTE *dst_bits = dst_base + x * 3;
                           const INT64 index = (x + src_bit_offset) >> 3;
                           const INT64 mask = 0x80 >> ((x + src_bit_offset) & 0x07);

                           // scale each column
                           for (INT64 y = y_begin; y < y_end; y++) {
                              // loop through column
                              const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                              const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                              const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + index;
                              double r = 0, g = 0, b = 0;

                              for (INT64 i = 0; i < iLimit; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 const double weight = weightsTable.getWeight(y, i);
                                 const INT64 pixel = (*src_bits & mask) != 0;
                                 const BYTE * const entry = (BYTE *)&src_pal[pixel];
                                 r += (weight * (double)entry[FI_RGBA_RED]);
                                 g += (weight * (double)entry[FI_RGBA_GREEN]);
                                 b += (weight * (double)entry[FI_RGBA_BLUE]);
                                 src_bits += src_pitch;
                              }

                              // clamp and place result in destination pixel
                              dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                              dst_bits += dst_pitch;
                           }
                        }
                     } else {
                        // we do not have a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 x = 0; x < width; x++) {
                           // work on column x in dst
                           BYTE *dst_bits = dst_base + x * 3;
                           const INT64 index = (x + src_bit_offset) >> 3;
                           const INT64 mask = 0x80 >> ((x + src_bit_offset) & 0x07);

                           // scale each column
                           for (INT64 y = y_begin; y < y_end; y++) {
                              // loop through column
                              const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                              const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                              const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + index;
                              double value = 0;

                              for (INT64 i = 0; i < iLimit; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 value += (weightsTable.getWeight(y, i) * (double)((*src_bits & mask) != 0));
                                 src_bits += src_pitch;
                              }
                              value *= 0xFF;

                              // clamp and place result in destination pixel
                              const BYTE bval = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_RED]   = bval;
                              dst_bits[FI_RGBA_GREEN]   = bval;
                              dst_bits[FI_RGBA_BLUE]   = bval;
                              dst_bits += dst_pitch;
                           }
                        }
                     }
                  }
                  break;

                  case 32:
                  {
                     // transparently convert the transparent 1-bit image to 32 bpp; 
                     // we always have got a palette here
                     #pragma omp parallel for schedule(dynamic) default(shared)
                     for (INT64 x = 0; x < width; x++) {
                        // work on column x in dst
                        BYTE *dst_bits = dst_base + x * 4;
                        const INT64 index = (x + src_bit_offset) >> 3;
                        const INT64 mask = 0x80 >> ((x + src_bit_offset) & 0x07);

                        // scale each column
                        for (INT64 y = y_begin; y < y_end; y++) {
                           // loop through column
                           const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                           const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                           const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + index;
                           double r = 0, g = 0, b = 0, a = 0;

                           for (INT64 i = 0; i < iLimit; i++) {
                              // scan between boundaries
                              // accumulate weighted effect of each neighboring pixel
                              const double weight = weightsTable.getWeight(y, i);
                              const INT64 pixel = (*src_bits & mask) != 0;
                              const BYTE * const entry = (BYTE *)&src_pal[pixel];
                              r += (weight * (double)entry[FI_RGBA_RED]);
                              g += (weight * (double)entry[FI_RGBA_GREEN]);
                              b += (weight * (double)entry[FI_RGBA_BLUE]);
                              a += (weight * (double)entry[FI_RGBA_ALPHA]);
                              src_bits += src_pitch;
                           }

                           // clamp and place result in destination pixel
                           dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_ALPHA]   = (BYTE)CLAMP<int>((int)(a + 0.5), 0, 0xFF);
                           dst_bits += dst_pitch;
                        }
                     }
                  }
                  break;
               }
            }
            break;

            case 4:
            {
               const INT64 src_pitch = FreeImage_GetPitch(src);
               const BYTE *const src_base = FreeImage_GetBits(src) + (src_offset_x >> 1);
               // and the dropped nibble
               const INT64 src_nibble_offset = src_offset_x & 0x01;

               switch(FreeImage_GetBPP(dst)) {
                  case 8:
                  {
                     // transparently convert the non-transparent 4-bit greyscale image to 8 bpp; 
                     // we always have got a palette for 4-bit images
                     #pragma omp parallel for schedule(dynamic) default(shared)
                     for (INT64 x = 0; x < width; x++) {
                        // work on column x in dst
                        BYTE *dst_bits = dst_base + x;
                        const INT64 index = (x + src_nibble_offset) >> 1;

                        // scale each column
                        for (INT64 y = y_begin; y < y_end; y++) {
                           // loop through column
                           const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                           const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                           const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + index;
                           double value = 0;

                           for (INT64 i = 0; i < iLimit; i++) {
                              // scan between boundaries
                              // accumulate weighted effect of each neighboring pixel
                              const INT64 pixel = (x + src_nibble_offset) & 0x01 ? *src_bits & 0x0F : *src_bits >> 4;
                              value += (weightsTable.getWeight(y, i) * (double)*(BYTE *)&src_pal[pixel]);
                              src_bits += src_pitch;
                           }

                           // clamp and place result in destination pixel
                           *dst_bits = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                           dst_bits += dst_pitch;
                        }
                     }
                  }
                  break;

                  case 24:
                  {
                     // transparently convert the non-transparent 4-bit image to 24 bpp; 
                     // we always have got a palette for 4-bit images
                     #pragma omp parallel for schedule(dynamic) default(shared)
                     for (INT64 x = 0; x < width; x++) {
                        // work on column x in dst
                        BYTE *dst_bits = dst_base + x * 3;
                        const INT64 index = (x + src_nibble_offset) >> 1;

                        // scale each column
                        for (INT64 y = y_begin; y < y_end; y++) {
                           // loop through column
                           const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                           const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                           const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + index;
                           double r = 0, g = 0, b = 0;

                           for (INT64 i = 0; i < iLimit; i++) {
                              // scan between boundaries
                              // accumulate weighted effect of each neighboring pixel
                              const double weight = weightsTable.getWeight(y, i);
                              const INT64 pixel = (x + src_nibble_offset) & 0x01 ? *src_bits & 0x0F : *src_bits >> 4;
                              const BYTE *const entry = (BYTE *)&src_pal[pixel];
                              r += (weight * (double)entry[FI_RGBA_RED]);
                              g += (weight * (double)entry[FI_RGBA_GREEN]);
                              b += (weight * (double)entry[FI_RGBA_BLUE]);
                              src_bits += src_pitch;
                           }

                           // clamp and place result in destination pixel
                           dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                           dst_bits += dst_pitch;
                        }
                     }
                  }
                  break;

                  case 32:
                  {
                     // transparently convert the transparent 4-bit image to 32 bpp; 
                     // we always have got a palette for 4-bit images
                     #pragma omp parallel for schedule(dynamic) default(shared)
                     for (INT64 x = 0; x < width; x++) {
                        // work on column x in dst
                        BYTE *dst_bits = dst_base + x * 4;
                        const INT64 index = (x + src_nibble_offset) >> 1;

                        // scale each column
                        for (INT64 y = y_begin; y < y_end; y++) {
                           // loop through column
                           const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                           const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                           const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + index;
                           double r = 0, g = 0, b = 0, a = 0;

                           for (INT64 i = 0; i < iLimit; i++) {
                              // scan between boundaries
                              // accumulate weighted effect of each neighboring pixel
                              const double weight = weightsTable.getWeight(y, i);
                              const INT64 pixel = (x + src_nibble_offset) & 0x01 ? *src_bits & 0x0F : *src_bits >> 4;
                              const BYTE *const entry = (BYTE *)&src_pal[pixel];
                              r += (weight * (double)entry[FI_RGBA_RED]);
                              g += (weight * (double)entry[FI_RGBA_GREEN]);
                              b += (weight * (double)entry[FI_RGBA_BLUE]);
                              a += (weight * (double)entry[FI_RGBA_ALPHA]);
                              src_bits += src_pitch;
                           }

                           // clamp and place result in destination pixel
                           dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_ALPHA]   = (BYTE)CLAMP<int>((int)(a + 0.5), 0, 0xFF);
                           dst_bits += dst_pitch;
                        }
                     }
                  }
                  break;
               }
            }
            break;

            case 8:
            {
               const INT64 src_pitch = FreeImage_GetPitch(src);
               const BYTE *const src_base = FreeImage_GetBits(src) + src_offset_x;

               switch(FreeImage_GetBPP(dst)) {
                  case 8:
                  {
                     // scale the 8-bit non-transparent greyscale image into an 8 bpp destination image
                     if (src_pal) {
                        // we have got a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 x = 0; x < width; x++) {
                           // work on column x in dst
                           BYTE *dst_bits = dst_base + x;

                           // scale each column
                           for (INT64 y = y_begin; y < y_end; y++) {
                              // loop through column
                              const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                              const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                              const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + x;
                              double value = 0;

                              for (INT64 i = 0; i < iLimit; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 value += (weightsTable.getWeight(y, i) * (double)*(BYTE *)&src_pal[*src_bits]);
                                 src_bits += src_pitch;
                              }

                              // clamp and place result in destination pixel
                              *dst_bits = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                              dst_bits += dst_pitch;
                           }
                        }
                     } else {
                        // we do not have a palette
                        VerticalFilterBytes<1>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
                     }
                  }
                  break;

                  case 24:
                  {
                     // transparently convert the non-transparent 8-bit image to 24 bpp
                     if (src_pal) {
                        // we have got a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 x = 0; x < width; x++) {
                           // work on column x in dst
                           BYTE *dst_bits = dst_base + x * 3;

                           // scale each column
                           for (INT64 y = y_begin; y < y_end; y++) {
                              // loop through column
                              const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                              const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                              const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + x;
                              double r = 0, g = 0, b = 0;

                              for (INT64 i = 0; i < iLimit; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 const double weight = weightsTable.getWeight(y, i);
                                 const BYTE * const entry = (BYTE *)&src_pal[*src_bits];
                                 r += (weight * (double)entry[FI_RGBA_RED]);
                                 g += (weight * (double)entry[FI_RGBA_GREEN]);
                                 b += (weight * (double)entry[FI_RGBA_BLUE]);
                                 src_bits += src_pitch;
                              }

                              // clamp and place result in destination pixel
                              dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                              dst_bits += dst_pitch;
                           }
                        }
                     } else {
                        // we do not have a palette
                        #pragma omp parallel for schedule(dynamic) default(shared)
                        for (INT64 x = 0; x < width; x++) {
                           // work on column x in dst
                           BYTE *dst_bits = dst_base + x * 3;

                           // scale each column
                           for (INT64 y = y_begin; y < y_end; y++) {
                              // loop through column
                              const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                              const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                              const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + x;
                              double value = 0;

                              for (INT64 i = 0; i < iLimit; i++) {
                                 // scan between boundaries
                                 // accumulate weighted effect of each neighboring pixel
                                 value += (weightsTable.getWeight(y, i) * (double)*src_bits);
                                 src_bits += src_pitch;
                              }

                              // clamp and place result in destination pixel
                              const BYTE bval = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
                              dst_bits[FI_RGBA_RED]   = bval;
                              dst_bits[FI_RGBA_GREEN]   = bval;
                              dst_bits[FI_RGBA_BLUE]   = bval;
                              dst_bits += dst_pitch;
                           }
                        }
                     }
                  }
                  break;

                  case 32:
                  {
                     // transparently convert the transparent 8-bit image to 32 bpp; 
                     // we always have got a palette here
                     #pragma omp parallel for schedule(dynamic) default(shared)
                     for (INT64 x = 0; x < width; x++) {
                        // work on column x in dst
                        BYTE *dst_bits = dst_base + x * 4;

                        // scale each column
                        for (INT64 y = y_begin; y < y_end; y++) {
                           // loop through column
                           const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                           const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                           const BYTE *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + x;
                           double r = 0, g = 0, b = 0, a = 0;

                           for (INT64 i = 0; i < iLimit; i++) {
                              // scan between boundaries
                              // accumulate weighted effect of each neighboring pixel
                              const double weight = weightsTable.getWeight(y, i);
                              const BYTE * const entry = (BYTE *)&src_pal[*src_bits];
                              r += (weight * (double)entry[FI_RGBA_RED]);
                              g += (weight * (double)entry[FI_RGBA_GREEN]);
                              b += (weight * (double)entry[FI_RGBA_BLUE]);
                              a += (weight * (double)entry[FI_RGBA_ALPHA]);
                              src_bits += src_pitch;
                           }

                           // clamp and place result in destination pixel
                           dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(r + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(g + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(b + 0.5), 0, 0xFF);
                           dst_bits[FI_RGBA_ALPHA]   = (BYTE)CLAMP<int>((int)(a + 0.5), 0, 0xFF);
                           dst_bits += dst_pitch;
                        }
                     }
                  }
                  break;
               }
            }
            break;

            case 16:
            {
               // transparently convert the 16-bit non-transparent image to 24 bpp
               const INT64 src_pitch = FreeImage_GetPitch(src) / sizeof(WORD);
               const WORD *const src_base = (WORD *)FreeImage_GetBits(src) + src_offset_x;

               if (IS_FORMAT_RGB565(src)) {
                  // image has 565 format
                  #pragma omp parallel for schedule(dynamic) default(shared)
                  for (INT64 x = 0; x < width; x++) {
                     // work on column x in dst
                     BYTE *dst_bits = dst_base + x * 3;

                     // scale each column
                     for (INT64 y = y_begin; y < y_end; y++) {
                        // loop through column
                        const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                        const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                        const WORD *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + x;
                        double r = 0, g = 0, b = 0;

                        for (INT64 i = 0; i < iLimit; i++) {
                           // scan between boundaries
                           // accumulate weighted effect of each neighboring pixel
                           const double weight = weightsTable.getWeight(y, i);
                           r += (weight * (double)((*src_bits & FI16_565_RED_MASK) >> FI16_565_RED_SHIFT));
                           g += (weight * (double)((*src_bits & FI16_565_GREEN_MASK) >> FI16_565_GREEN_SHIFT));
                           b += (weight * (double)((*src_bits & FI16_565_BLUE_MASK) >> FI16_565_BLUE_SHIFT));
                           src_bits += src_pitch;
                        }

                        // clamp and place result in destination pixel
                        dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(((r * 0xFF) / 0x1F) + 0.5), 0, 0xFF);
                        dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(((g * 0xFF) / 0x3F) + 0.5), 0, 0xFF);
                        dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(((b * 0xFF) / 0x1F) + 0.5), 0, 0xFF);
                        dst_bits += dst_pitch;
                     }
                  }
               } else {
                  // image has 555 format
                  #pragma omp parallel for schedule(dynamic) default(shared)
                  for (INT64 x = 0; x < width; x++) {
                     // work on column x in dst
                     BYTE *dst_bits = dst_base + x * 3;

                     // scale each column
                     for (INT64 y = y_begin; y < y_end; y++) {
                        // loop through column
                        const INT64 iLeft = weightsTable.getLeftBoundary(y);            // retrieve left boundary
                        const INT64 iLimit = weightsTable.getRightBoundary(y) - iLeft;   // retrieve right boundary
                        const WORD *src_bits = src_base + (src_row_bias + iLeft) * src_pitch + x;
                        double r = 0, g = 0, b = 0;

                        for (INT64 i = 0; i < iLimit; i++) {
                           // scan between boundaries
                           // accumulate weighted effect of each neighboring pixel
                           const double weight = weightsTable.getWeight(y, i);
                           r += (weight * (double)((*src_bits & FI16_555_RED_MASK) >> FI16_555_RED_SHIFT));
                           g += (weight * (double)((*src_bits & FI16_555_GREEN_MASK) >> FI16_555_GREEN_SHIFT));
                           b += (weight * (double)((*src_bits & FI16_555_BLUE_MASK) >> FI16_555_BLUE_SHIFT));
                           src_bits += src_pitch;
                        }

                        // clamp and place result in destination pixel
                        dst_bits[FI_RGBA_RED]   = (BYTE)CLAMP<int>((int)(((r * 0xFF) / 0x1F) + 0.5), 0, 0xFF);
                        dst_bits[FI_RGBA_GREEN]   = (BYTE)CLAMP<int>((int)(((g * 0xFF) / 0x1F) + 0.5), 0, 0xFF);
                        dst_bits[FI_RGBA_BLUE]   = (BYTE)CLAMP<int>((int)(((b * 0xFF) / 0x1F) + 0.5), 0, 0xFF);
                        dst_bits += dst_pitch;
                     }
                  }
               }
            }
            break;

            case 24:
               VerticalFilterBytes<3>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
               break;

            case 32:
               VerticalFilterBytes<4>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
               break;
         }
      }
      break;

      case FIT_UINT16:
         VerticalFilterSamples<WORD, 1>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
         break;

      case FIT_RGB16:
         VerticalFilterSamples<WORD, 3>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
         break;

      case FIT_RGBA16:
         VerticalFilterSamples<WORD, 4>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
         break;

      case FIT_FLOAT:
         VerticalFilterSamples<float, 1>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
         break;

      case FIT_RGBF:
         VerticalFilterSamples<float, 3>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
         break;

      case FIT_RGBAF:
         VerticalFilterSamples<float, 4>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
         break;

      case FIT_INT16:
         VerticalFilterSamples<short, 1>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
         break;

      case FIT_UINT32:
         VerticalFilterSamples<DWORD, 1>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
         break;

      case FIT_INT32:
         VerticalFilterSamples<LONG, 1>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
         break;

      case FIT_DOUBLE:
         VerticalFilterSamples<double, 1>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
         break;

      case FIT_COMPLEX:
         VerticalFilterSamples<double, 2>(weightsTable, src, src_row_bias, src_offset_x, dst, dst_row_bias, y_begin, y_end, width);
         break;
   }
}
