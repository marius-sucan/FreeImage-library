// FIQ_MCQUANT against a plain reimplementation of its algorithm (README.md), over synthetic images:
// odd and tiny sizes, 24 and 32 bits, palette sizes from 2 to 256 and reserved colors.

#include "FreeImage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef long long ll;

static unsigned rng = 12345;
static unsigned
rnd(void) {
	rng = rng * 1103515245u + 12345u;
	return rng >> 8;
}

// ---- the plain algorithm: pixels as R, G, B, top row first ----

typedef struct { int lo[3], hi[3]; ll pop; } Box;
static ll H[32][32][32];
static ll S[32][32][32][3];
static int T[32768];

static ll
box_sum(const Box *b, int k) {
	ll s = 0;
	for (int r = b->lo[0]; r <= b->hi[0]; r++)
		for (int g = b->lo[1]; g <= b->hi[1]; g++)
			for (int c = b->lo[2]; c <= b->hi[2]; c++) {
				const int level[4] = { 1, r, g, c };
				s += H[r][g][c] * level[k];
			}
	return s;
}

// the box shrunk to the cells that hold pixels
static void
shrink(Box *b) {
	for (int a = 0; a < 3; a++) {
		while (b->lo[a] < b->hi[a]) { Box t = *b; t.hi[a] = t.lo[a]; if (box_sum(&t, 0)) break; b->lo[a]++; }
		while (b->hi[a] > b->lo[a]) { Box t = *b; t.lo[a] = t.hi[a]; if (box_sum(&t, 0)) break; b->hi[a]--; }
	}
}

// the palette entry of a 5-bit cell: a color inside it, else the nearest to its center; in half units
static int
lookup(int pal[256][3], int n, int cell) {
	if (T[cell] >= 0) return T[cell];
	const int l[3] = { cell >> 10, (cell >> 5) & 31, cell & 31 };
	int best = -1, bd = 1 << 30;
	for (int pass = 0; pass < 2 && best < 0; pass++)
		for (int j = 0; j < n; j++) {
			if (pass == 0 && ((pal[j][0] >> 3) != l[0] || (pal[j][1] >> 3) != l[1] || (pal[j][2] >> 3) != l[2])) continue;
			int d = 0;
			for (int k = 0; k < 3; k++) d += (16 * l[k] + 7 - 2 * pal[j][k]) * (16 * l[k] + 7 - 2 * pal[j][k]);
			if (d < bd) { bd = d; best = j; }
		}
	return T[cell] = best;
}

static int
plain_quantize(const BYTE *rgb, int w, int h, int colors, int reserve, const RGBQUAD *reserved, BYTE *out, int pal[256][3]) {
	const int count = colors - reserve;
	int n = 0;
	if (count > 0) {
		memset(H, 0, sizeof(H));
		memset(S, 0, sizeof(S));
		for (int i = 0; i < w * h; i++) {
			const BYTE *p = rgb + 3 * i;
			H[p[0] >> 3][p[1] >> 3][p[2] >> 3]++;
			for (int k = 0; k < 3; k++) S[p[0] >> 3][p[1] >> 3][p[2] >> 3][k] += p[k];
		}
		Box bx[256];
		n = 1;
		for (int a = 0; a < 3; a++) { bx[0].lo[a] = 0; bx[0].hi[a] = 31; }
		bx[0].pop = box_sum(&bx[0], 0);
		shrink(&bx[0]);
		while (n < count) {
			int best = -1;
			ll bs = 0;
			for (int i = 0; i < n; i++) {
				int e = 0;
				for (int a = 0; a < 3; a++) if (bx[i].hi[a] - bx[i].lo[a] > e) e = bx[i].hi[a] - bx[i].lo[a];
				if (bx[i].pop * e > bs) { bs = bx[i].pop * e; best = i; }
			}
			if (best < 0) break;
			Box b = bx[best];
			int ax = 0;
			for (int a = 1; a < 3; a++) if (b.hi[a] - b.lo[a] > b.hi[ax] - b.lo[ax]) ax = a;
			int m = (int)(box_sum(&b, ax + 1) / b.pop);
			if (m >= b.hi[ax]) m = b.hi[ax] - 1;
			Box lo = b, up = b;
			lo.hi[ax] = m;
			up.lo[ax] = m + 1;
			lo.pop = box_sum(&lo, 0);
			up.pop = box_sum(&up, 0);
			shrink(&lo);
			shrink(&up);
			bx[best] = up;
			bx[n++] = lo;
		}
		for (int i = 1; i < n; i++) {
			Box k = bx[i];
			int j = i - 1;
			while (j >= 0 && bx[j].pop < k.pop) { bx[j + 1] = bx[j]; j--; }
			bx[j + 1] = k;
		}
		for (int i = 0; i < n; i++) {
			const Box *b = &bx[i];
			const int one = b->lo[0] == b->hi[0] && b->lo[1] == b->hi[1] && b->lo[2] == b->hi[2];
			for (int a = 0; a < 3; a++) {
				if (one) {
					// a one-cell box: the rounded mean color of its pixels
					const ll c = H[b->lo[0]][b->lo[1]][b->lo[2]], sum = S[b->lo[0]][b->lo[1]][b->lo[2]][a];
					pal[i][a] = (int)((2 * sum + c) / (2 * c));
				} else {
					const int l = (int)(box_sum(b, a + 1) / b->pop);
					pal[i][a] = (l * 510 + 31) / 62;
				}
			}
		}
	}
	// the reserved colors take the last entries; copies of the first color fill the gap before them
	for (int i = n; i < count; i++)
		for (int a = 0; a < 3; a++) pal[i][a] = pal[0][a];
	for (int i = 0; i < reserve; i++) {
		pal[count + i][0] = reserved[i].rgbRed;
		pal[count + i][1] = reserved[i].rgbGreen;
		pal[count + i][2] = reserved[i].rgbBlue;
	}
	n = count + reserve;
	memset(T, -1, sizeof(T));
	int *cur = calloc((size_t)(w + 2) * 3, sizeof(int)), *nxt = calloc((size_t)(w + 2) * 3, sizeof(int));
	for (int y = 0; y < h; y++) {
		const int ltr = y & 1, step = ltr ? 1 : -1;
		for (int i = 0; i < w; i++) {
			const int x = ltr ? i : w - 1 - i;
			int v[3];
			for (int k = 0; k < 3; k++) {
				const int a = cur[(x + 1) * 3 + k];
				int q = a >= 0 ? a / 16 : -((-a + 15) / 16);
				int s = rgb[3 * (y * w + x) + k] + q;
				v[k] = s < 0 ? 0 : (s > 255 ? 255 : s);
			}
			const int c = lookup(pal, n, ((v[0] >> 3) << 10) | ((v[1] >> 3) << 5) | (v[2] >> 3));
			out[y * w + x] = (BYTE)c;
			for (int k = 0; k < 3; k++) {
				const int e = v[k] - pal[c][k];
				const int xn = x + step, xb = x - step;
				if (xn >= 0 && xn < w) { cur[(xn + 1) * 3 + k] += 7 * e; nxt[(xn + 1) * 3 + k] += e; }
				if (xb >= 0 && xb < w) nxt[(xb + 1) * 3 + k] += 3 * e;
				nxt[(x + 1) * 3 + k] += 5 * e;
			}
		}
		int *t = cur; cur = nxt; nxt = t;
		memset(nxt, 0, (size_t)(w + 2) * 3 * sizeof(int));
	}
	free(cur);
	free(nxt);
	return n;
}

// ---- the cases ----

static void
fill(BYTE *rgb, int w, int h, int kind) {
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++) {
			BYTE *p = rgb + 3 * (y * w + x);
			switch (kind) {
				case 0: p[0] = (BYTE)rnd(); p[1] = (BYTE)rnd(); p[2] = (BYTE)rnd(); break;
				case 1: p[0] = (BYTE)(x * 255 / (w > 1 ? w - 1 : 1)); p[1] = (BYTE)(y * 255 / (h > 1 ? h - 1 : 1)); p[2] = (BYTE)((x + y) * 127 / (w + h)); break;
				case 2: { static const BYTE c[3][3] = { {250, 10, 10}, {12, 200, 40}, {30, 30, 220} }; memcpy(p, c[(x / 3 + y) % 3], 3); break; }
				case 3: p[0] = 128; p[1] = 128; p[2] = 128; break;
				case 4: { const BYTE v = ((x + y) & 1) ? 255 : 0; p[0] = v; p[1] = (BYTE)(255 - v); p[2] = v; break; }
				case 5: { const int g = (x * 7 + y * 3) & 255; p[0] = p[1] = p[2] = (BYTE)g; break; }
				case 6: { static const BYTE c[3][3] = { {87, 87, 87}, {88, 83, 84}, {90, 90, 90} }; memcpy(p, c[(x / 4 + y / 3) % 3], 3); break; }
				default: p[0] = 253; p[1] = 254; p[2] = 255; break;
			}
		}
}

static int
run_case(int w, int h, int bpp, int kind, int colors, int reserve) {
	BYTE *rgb = malloc((size_t)w * h * 3), *ref = malloc((size_t)w * h);
	fill(rgb, w, h, kind);
	FIBITMAP *dib = FreeImage_Allocate(w, h, bpp, FI_RGBA_RED_MASK, FI_RGBA_GREEN_MASK, FI_RGBA_BLUE_MASK);
	for (int y = 0; y < h; y++) {
		BYTE *line = FreeImage_GetScanLine(dib, h - 1 - y);
		for (int x = 0; x < w; x++) {
			BYTE *p = line + x * (bpp / 8);
			p[FI_RGBA_RED] = rgb[3 * (y * w + x)];
			p[FI_RGBA_GREEN] = rgb[3 * (y * w + x) + 1];
			p[FI_RGBA_BLUE] = rgb[3 * (y * w + x) + 2];
			if (bpp == 32) p[FI_RGBA_ALPHA] = (BYTE)rnd();
		}
	}
	RGBQUAD reserved[256];
	for (int i = 0; i < reserve; i++) {
		reserved[i].rgbRed = (BYTE)rnd(); reserved[i].rgbGreen = (BYTE)rnd(); reserved[i].rgbBlue = (BYTE)rnd(); reserved[i].rgbReserved = 0;
	}
	if (kind == 6) {
		// colors inside one cell at different and at equal distances from its center, and one nearer to a center
		// than the computed color inside that cell
		static const BYTE fixed[5][3] = { {80, 80, 80}, {84, 83, 84}, {83, 83, 83}, {84, 84, 84}, {91, 91, 91} };
		for (int i = 0; i < reserve && i < 5; i++) {
			reserved[i].rgbRed = fixed[i][0]; reserved[i].rgbGreen = fixed[i][1]; reserved[i].rgbBlue = fixed[i][2];
		}
	}
	int pal[256][3];
	const int n = plain_quantize(rgb, w, h, colors, reserve, reserved, ref, pal);
	FIBITMAP *q = (colors == 256 && reserve == 0 && (kind & 1)) ? FreeImage_ColorQuantize(dib, FIQ_MCQUANT)
		: FreeImage_ColorQuantizeEx(dib, FIQ_MCQUANT, colors, reserve, reserve ? reserved : NULL);
	int fail = 0;
	if (!q || FreeImage_GetBPP(q) != 8 || FreeImage_GetWidth(q) != (unsigned)w || FreeImage_GetHeight(q) != (unsigned)h) {
		fail = 1;
	} else {
		const RGBQUAD *qp = FreeImage_GetPalette(q);
		for (int i = 0; i < n && !fail; i++)
			if (qp[i].rgbRed != pal[i][0] || qp[i].rgbGreen != pal[i][1] || qp[i].rgbBlue != pal[i][2]) fail = 2;
		for (int y = 0; y < h && !fail; y++) {
			const BYTE *line = FreeImage_GetScanLine(q, h - 1 - y);
			for (int x = 0; x < w; x++)
				if (line[x] != ref[y * w + x]) { fail = 3; break; }
		}
	}
	if (fail) {
		printf("FAIL %dx%d %d-bit kind %d colors %d reserve %d: %s\n", w, h, bpp, kind, colors, reserve,
			fail == 1 ? "no result" : fail == 2 ? "palette differs" : "indices differ");
	}
	FreeImage_Unload(q);
	FreeImage_Unload(dib);
	free(rgb);
	free(ref);
	return fail != 0;
}

int
main(void) {
	static const int sizes[][2] = { {1, 1}, {1, 7}, {7, 1}, {2, 2}, {3, 5}, {4, 3}, {5, 3}, {8, 8}, {16, 16}, {33, 17},
		{64, 1}, {1, 64}, {100, 75}, {257, 129}, {300, 300} };
	static const int palettes[][2] = { {256, 0}, {2, 0}, {16, 0}, {100, 0}, {255, 0}, {256, 1}, {256, 5}, {16, 16}, {2, 1} };
	int cases = 0, failed = 0;
	for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++)
		for (int bpp = 24; bpp <= 32; bpp += 8)
			for (int kind = 0; kind < 6; kind++)
				for (unsigned p = 0; p < sizeof(palettes) / sizeof(palettes[0]); p++) {
					failed += run_case(sizes[s][0], sizes[s][1], bpp, kind, palettes[p][0], palettes[p][1]);
					cases++;
				}
	// larger images, 24- and 32-bit
	for (int kind = 0; kind < 6; kind++) {
		failed += run_case(1031, 517, kind & 1 ? 32 : 24, kind, 256, 0);
		cases++;
	}
	// a palette color inside a cell wins it, reserved colors too
	static const int inside[][2] = { {256, 0}, {256, 5}, {16, 5}, {8, 5} };
	for (unsigned p = 0; p < sizeof(inside) / sizeof(inside[0]); p++) {
		failed += run_case(37, 23, 24, 6, inside[p][0], inside[p][1]);
		cases++;
	}
	// one color over more than 2^24 pixels, whose 32-bit sums must move to 64 bits in between
	failed += run_case(5000, 3400, 24, 7, 256, 0);
	cases++;
	printf("%d cases, %d failed\n", cases, failed);
	return failed != 0;
}
