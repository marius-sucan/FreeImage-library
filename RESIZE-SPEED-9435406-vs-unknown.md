# FreeImage_Rescale: FreeImage-9435406.dll against FreeImage-unknown.dll, every pixel format at 56 megapixels

2026-09-27, branch `worktree-io64`. Your two MSVC builds from `old-dlls/`, timed in the same session on the same machine:
32 pixel formats (every image type and colour type the resize code handles) x 5 aspect ratios x 3 scale factors x
2 filters (B-spline, bilinear) = 960 cases per DLL, every source image 56.25 megapixels.

## 1. Summary

1. **`9435406` is faster in all 750 cases both DLLs can compute**: 1.65x on the geometric mean, from 1.075x to 3.95x. No
   colour type, image type, aspect ratio, direction, scale factor or filter favours `unknown`, and neither does any single
   case. The closest ones are the 1-bit greyscale images reduced x0.192 with B-spline, 1.075-1.096x in all ten of them (two
   formats, five shapes), well above the noise (0.8% median, section 9).
2. **Per `FREE_IMAGE_COLOR_TYPE`** of the source (section 4.1), `9435406` is faster by:

   | colour type | cases | upscale | downscale | all |
   |---|---|---|---|---|
   | FIC_MINISWHITE | 60 | 1.53x | 1.65x | **1.61x** |
   | FIC_MINISBLACK | 120 | 1.58x | 2.00x | **1.85x** |
   | FIC_RGB | 180 | 1.57x | 1.86x | **1.75x** |
   | FIC_PALETTE | 240 | 1.41x | 1.39x | **1.40x** |
   | FIC_RGBALPHA | 90 | 1.60x | 2.03x | **1.88x** |
   | FIC_CMYK | 60 | 1.57x | 1.95x | **1.82x** |

3. **Per image type** (section 4.2): the most for `FIT_FLOAT` (2.72x; 3.73x reduced x0.192), `FIT_RGBF` (2.19x), 8-bit greys
   (2.11x), 24- and 32-bit (2.09x) and `FIT_RGBAF` (1.97x); the least for 1-bit (1.29x), 4-bit (1.36x), 555/565 (1.42x) and
   8-bit palettes (1.44-1.48x).
4. **The aspect ratio hardly matters** (section 4.3): square 1.66x, wide 16:9 1.64x, ultra-wide 4:1 1.63x, tall 9:16 1.66x,
   very tall 1:4 1.67x. Within a format the five shapes stay within 7% of each other; the ultra-wide one gives `9435406` its
   smallest lead in 17 of the 25 formats.
5. **Downscaling gains more than upscaling** (section 4.4): 1.72x against 1.52x; by factor x1.6 1.52x, x0.48 1.70x, x0.192
   1.74x. Upscales gain less because allocating and clearing the 144-megapixel result, the same in both DLLs, takes
   34-67% of `9435406`'s upscale time (section 7). **The filter does not matter**: B-spline 1.64x, bilinear 1.66x.
6. **`unknown` cannot resize 7 of the 32 formats** (section 5). `FIT_INT16`, `FIT_UINT32`, `FIT_INT32`, `FIT_DOUBLE` and
   `FIT_COMPLEX` come back as an all-zero image of the right size, with nothing filtered and no error; for `FIT_DOUBLE` and
   `FIT_COMPLEX` upscaled, merely allocating and clearing its buffers takes `unknown` longer than the whole resize takes
   `9435406`. A 4-bit image whose palette is 0..15 or 15..0 (`FIC_MINISBLACK` / `FIC_MINISWHITE`) crashes it (access
   violation). `9435406` resizes all 32.
7. **Both return the same image type, bit depth and colour type** for the 25 formats both resize (section 5.3).
8. **The JCC erratum only trims the margins** (section 6). This CPU has it, your i7-11700 does not, and `unknown` was built
   without `/QIntel-jcc-erratum`. Against a padded build of `unknown`'s own code (same pixels), `9435406` leads by 1.60x
   instead of 1.63x over 114 cases. Three formats change noticeably: the leads on 4-bit greys (1.32x to 1.21x), the 8-bit
   grey palette (1.43x to 1.35x) and `FIT_RGBA16` (1.59x to 1.44x) shrink, and one case, 4-bit greys reduced x0.192 with
   B-spline, becomes a tie. On your CPU expect those margins rather than the ones measured here.
9. **`unknown` needs 34-44% more peak memory to upscale** (section 7): a full-size intermediate image, 1.6 times the source
   (4461 MiB against 3101 MiB for `FIT_RGBAF`); `9435406` passes bands of about 4 MB instead.
10. **Not measured on your machine.** A 4-core i7-7700T ran everything under Wine with 8 threads; your i7-11700 has 8 cores
    and 16 threads. Both have AVX2, so `9435406` runs its AVX2 kernels on both. Expect the same winner everywhere but the
    one tie of item 8, and similar ratios where the work is compute-bound; the upscales, dominated by writing
    144-megapixel images, depend more on the memory system.

## 2. The two DLLs

| DLL | SHA-256 | code | `FreeImage_GetVersion` | built with | `/QIntel-jcc-erratum` |
|---|---|---|---|---|---|
| `FreeImage-9435406.dll` | `020801a5...` | `9435406`, the tip of `worktree-io64` before the DLLs were added | 3.20.0 | MSVC 19.29.30158 (VS 2019 16.11), linker 14.29 | yes (`eae6e9b`) |
| `FreeImage-unknown.dll` | `7168c4f7...` | `274f833`, the tip of branch `resize-rework` (byte-identical to QPV's `FreeImage-old.dll` of 29 August) | 3.19.0 | the same compiler and linker builds (Rich header) | no |

Their resize code, from the merge base `33c3259`:

- `274f833` adds 5 commits: the weights in one block (`ecb0b12`), the vertical pass by rows instead of by columns, with
  working OpenMP pragmas (`b2e2281`), and an SSE2 horizontal kernel for 8-, 24- and 32-bit images (`274f833`).
- `9435406` adds 27, among them: the 17 September fixes (4-bit palettes, MINISWHITE, RawBits, offsets), filters for the five
  image types that were never filtered (`54dec18`), one template kernel for every plain-sample layout (`18e1a5f`,
  `79a9b43`), both passes over bands of about 4 MB instead of a full-size intermediate image (`9ee588d`), the pass order by
  tap cost (`d60316b`), ring kernels for 8-, 24- and 32-bit images (`394d42a`, `59cf8ef`) with their own pass order
  (`b5c8f48`), and AVX2 versions of those picked at run time (`2edd1da`).

## 3. Method

- **Machine**: Intel i7-7700T (Kaby Lake, 4 cores / 8 threads, 2.9-3.8 GHz, AVX2 and FMA, microcode 0xf8), 15 GB, Linux 7.0,
  `intel_pstate` powersave with balance_performance, turbo on. The DLLs ran under Wine 10.0 with QPV's `vcomp140.dll`
  (14.13.26020) and `OMP_NUM_THREADS=8`, all logical CPUs, as QPV runs them.
- **Bench**: `rmatrix.exe` (MSVC 19.29, `/O2 /MT /openmp`) loads the DLL named on its command line with `LoadLibrary`, makes
  the source image with that DLL's own `FreeImage_AllocateT` and times `FreeImage_Rescale(src, w, h, filter)`, the call
  QPV's `trFreeImage_Rescale` makes, from the call to its return. Unloading the result is not timed.
- **Images**: the 32 formats of section 3.1, each in 5 shapes of 56.25 megapixels: square 7500 x 7500, wide 16:9
  10000 x 5625, ultra-wide 4:1 15000 x 3750, tall 9:16 5625 x 10000 and very tall 1:4 3750 x 15000. Content: smooth
  shading, hard-edged 97 x 89 blocks and 5% noise, different per channel; the 1- and 4-bit images are ordered-dithered from
  the same values, the palette images index their palettes with them.
- **Operations**: both axes x1.6 (144 megapixels out: 12000 x 12000, 16000 x 9000, 24000 x 6000, 9000 x 16000,
  6000 x 24000), x0.48 (13 megapixels) and x0.192 (2.07 megapixels; the 16:9 image becomes 1920 x 1080), each with
  `FILTER_BSPLINE` and `FILTER_BILINEAR`. x1.6 rather than x2 keeps the largest case (`FIT_RGBAF`, `unknown`) at 4.5 GB on
  this 15 GB machine; an enlargement's cost per result pixel does not depend on the factor.
- **Order**: one Wine process per format, shape, DLL and round: the source, one warm-up call, then 3, 5 or 7 calls back to
  back per operation (x1.6, x0.48, x0.192). Two rounds per image, the DLLs alternating A B B A (B A A B for the next image),
  so that both see the same clocks, temperature and background; running all of one DLL and then the other would have given
  the second one a different machine. The calls are back to back because a pause before each one let the clocks drop
  (powersave governor): short calls then took up to 3x longer, erratically. Compared: the median of each case's 6, 10 or 14
  calls per DLL. The matrix ran from 13:21 to 15:15, the section 6 control from 15:15 to 15:53.
- **Checks**: the first result of every operation was compared with its source through 8 x 8 block means of every displayed
  channel (palettes and transparency resolved): every correct result is within 0.78% of the range (median 0.03%; the
  largest is the same case, and the same error, in both DLLs), `9435406`'s results for the five types `unknown` leaves
  blank within 0.05%; an all-zero result is reported as `blank`. Before each process the other processes on the machine
  had to use less than 0.3 cores for a second, and the CPU they used during each run was recorded (section 9).
- **Re-runs**: the 4 images whose two rounds disagreed by more than 20% on any operation got two more rounds, and so did one
  image whose first round a pause had split. The 3 processes during which other processes used more than 0.3 cores were
  dropped, their images having the later rounds.

### 3.1 The 32 formats

| format | image type | palette, content | `FreeImage_GetColorType` | result (both DLLs) |
|---|---|---|---|---|
| b1-black | FIT_BITMAP 1-bit | black, white | FIC_MINISBLACK | 8-bit FIC_MINISBLACK |
| b1-white | FIT_BITMAP 1-bit | white, black | FIC_MINISWHITE | 8-bit FIC_MINISWHITE |
| b1-pal | FIT_BITMAP 1-bit | 2 colours | FIC_PALETTE | 24-bit FIC_RGB |
| b1-trns | FIT_BITMAP 1-bit | 2 colours, alpha 255 and 96 | FIC_PALETTE | 32-bit FIC_RGBALPHA |
| b4-grey | FIT_BITMAP 4-bit | FreeImage's 16-grey ramp (0, 17, ... 255) | FIC_PALETTE | 8-bit FIC_MINISBLACK |
| b4-black | FIT_BITMAP 4-bit | greys 0..15 | FIC_MINISBLACK | 8-bit FIC_MINISBLACK (`unknown` crashes) |
| b4-white | FIT_BITMAP 4-bit | greys 15..0 | FIC_MINISWHITE | 8-bit FIC_MINISBLACK (`unknown` crashes) |
| b4-pal | FIT_BITMAP 4-bit | 16 colours | FIC_PALETTE | 24-bit FIC_RGB |
| b4-trns | FIT_BITMAP 4-bit | 16 colours, alpha 255..128 | FIC_PALETTE | 32-bit FIC_RGBALPHA |
| b8-black | FIT_BITMAP 8-bit | grey ramp | FIC_MINISBLACK | 8-bit FIC_MINISBLACK |
| b8-white | FIT_BITMAP 8-bit | reversed grey ramp | FIC_MINISWHITE | 8-bit FIC_MINISWHITE |
| b8-greypal | FIT_BITMAP 8-bit | 256 greys out of order | FIC_PALETTE | 8-bit FIC_MINISBLACK |
| b8-pal | FIT_BITMAP 8-bit | 256 colours | FIC_PALETTE | 24-bit FIC_RGB |
| b8-trns | FIT_BITMAP 8-bit | 256 colours, alpha 255..128 | FIC_PALETTE | 32-bit FIC_RGBALPHA |
| b16-555 | FIT_BITMAP 16-bit | RGB 5-5-5 | FIC_RGB | 24-bit FIC_RGB |
| b16-565 | FIT_BITMAP 16-bit | RGB 5-6-5 | FIC_RGB | 24-bit FIC_RGB |
| b24 | FIT_BITMAP 24-bit | | FIC_RGB | 24-bit FIC_RGB |
| b32-rgba | FIT_BITMAP 32-bit | alpha varies | FIC_RGBALPHA | 32-bit FIC_RGBALPHA |
| b32-rgb | FIT_BITMAP 32-bit | alpha 255 everywhere | FIC_RGB | 32-bit FIC_RGB |
| b32-cmyk | FIT_BITMAP 32-bit | ICC profile flagged CMYK | FIC_CMYK | 32-bit, reported FIC_RGBALPHA |
| uint16 | FIT_UINT16 | 0..65535 | FIC_MINISBLACK | FIT_UINT16 |
| int16 | FIT_INT16 | -32768..32767 | FIC_MINISBLACK | FIT_INT16 (`unknown`: blank) |
| uint32 | FIT_UINT32 | 0..4e9 | FIC_MINISBLACK | FIT_UINT32 (`unknown`: blank) |
| int32 | FIT_INT32 | -2e9..2e9 | FIC_MINISBLACK | FIT_INT32 (`unknown`: blank) |
| float | FIT_FLOAT | 0..1 | FIC_MINISBLACK | FIT_FLOAT |
| double | FIT_DOUBLE | 0..1 | FIC_MINISBLACK | FIT_DOUBLE (`unknown`: blank) |
| complex | FIT_COMPLEX | real and imaginary parts 0..1 | FIC_MINISBLACK | FIT_COMPLEX (`unknown`: blank) |
| rgb16 | FIT_RGB16 | | FIC_RGB | FIT_RGB16 |
| rgba16 | FIT_RGBA16 | | FIC_RGBALPHA | FIT_RGBA16 |
| rgba16-cmyk | FIT_RGBA16 | ICC profile flagged CMYK | FIC_CMYK | FIT_RGBA16, reported FIC_RGBALPHA |
| rgbf | FIT_RGBF | | FIC_RGB | FIT_RGBF |
| rgbaf | FIT_RGBAF | | FIC_RGBALPHA | FIT_RGBAF |

The colour type is what `FreeImage_GetColorType` returns for the source, the same in both DLLs for all 32. FreeImage's own
4-bit grey ramp is `FIC_PALETTE`, not `FIC_MINISBLACK`; only a 0..15 ramp is. `FIC_MINISBLACK` also covers every
single-channel image type other than `FIT_BITMAP`.

## 4. Results

In every table: "faster" names the DLL with the smaller geometric mean over the group, "by" is that geometric mean of
unknown's time / 9435406's time, the range the smallest and largest of those ratios, and the last two columns the median
of the group's per-case medians. Only the 750 cases both DLLs compute are counted; section 5 has the other 210.

### 4.1 By colour type

| colour type | cases | faster | by (geomean) | 9435406 wins | unknown wins | range | median ms 9435406 | median ms unknown |
|---|---|---|---|---|---|---|---|---|
| FIC_MINISWHITE | 60 | **9435406** | 1.61x | 60 | 0 | 1.08-2.57 | 91 | 107 |
| FIC_MINISBLACK | 120 | **9435406** | 1.85x | 120 | 0 | 1.08-3.95 | 81 | 145 |
| FIC_RGB | 180 | **9435406** | 1.75x | 180 | 0 | 1.26-3.08 | 161 | 305 |
| FIC_PALETTE | 240 | **9435406** | 1.40x | 240 | 0 | 1.20-1.66 | 190 | 273 |
| FIC_RGBALPHA | 90 | **9435406** | 1.88x | 90 | 0 | 1.34-2.70 | 221 | 391 |
| FIC_CMYK | 60 | **9435406** | 1.82x | 60 | 0 | 1.34-2.65 | 185 | 317 |

| colour type, direction | cases | faster | by (geomean) | 9435406 wins | unknown wins | range | median ms 9435406 | median ms unknown |
|---|---|---|---|---|---|---|---|---|
| FIC_MINISWHITE, up | 20 | **9435406** | 1.53x | 20 | 0 | 1.28-1.92 | 230 | 350 |
| FIC_MINISWHITE, down | 40 | **9435406** | 1.65x | 40 | 0 | 1.08-2.57 | 49 | 86 |
| FIC_MINISBLACK, up | 40 | **9435406** | 1.58x | 40 | 0 | 1.26-2.16 | 331 | 428 |
| FIC_MINISBLACK, down | 80 | **9435406** | 2.00x | 80 | 0 | 1.08-3.95 | 50 | 103 |
| FIC_RGB, up | 60 | **9435406** | 1.57x | 60 | 0 | 1.26-1.87 | 622 | 976 |
| FIC_RGB, down | 120 | **9435406** | 1.86x | 120 | 0 | 1.26-3.08 | 118 | 231 |
| FIC_PALETTE, up | 80 | **9435406** | 1.41x | 80 | 0 | 1.27-1.56 | 590 | 862 |
| FIC_PALETTE, down | 160 | **9435406** | 1.39x | 160 | 0 | 1.20-1.66 | 143 | 197 |
| FIC_RGBALPHA, up | 30 | **9435406** | 1.60x | 30 | 0 | 1.34-1.90 | 1137 | 1562 |
| FIC_RGBALPHA, down | 60 | **9435406** | 2.03x | 60 | 0 | 1.41-2.70 | 146 | 289 |
| FIC_CMYK, up | 20 | **9435406** | 1.57x | 20 | 0 | 1.34-1.88 | 795 | 1253 |
| FIC_CMYK, down | 40 | **9435406** | 1.95x | 40 | 0 | 1.45-2.65 | 129 | 261 |

### 4.2 By image type, bit depth and format

| image type | cases | faster | by (geomean) | 9435406 wins | unknown wins | range | median ms 9435406 | median ms unknown |
|---|---|---|---|---|---|---|---|---|
| FIT_BITMAP 1-bit | 120 | **9435406** | 1.29x | 120 | 0 | 1.08-1.58 | 197 | 270 |
| FIT_BITMAP 4-bit | 90 | **9435406** | 1.36x | 90 | 0 | 1.20-1.54 | 187 | 255 |
| FIT_BITMAP 8-bit | 150 | **9435406** | 1.69x | 150 | 0 | 1.32-2.57 | 127 | 186 |
| FIT_BITMAP 16-bit | 60 | **9435406** | 1.42x | 60 | 0 | 1.26-1.58 | 186 | 279 |
| FIT_BITMAP 24-bit | 30 | **9435406** | 2.09x | 30 | 0 | 1.65-2.49 | 98 | 217 |
| FIT_BITMAP 32-bit | 90 | **9435406** | 2.09x | 90 | 0 | 1.64-2.73 | 128 | 268 |
| FIT_UINT16 | 30 | **9435406** | 1.64x | 30 | 0 | 1.26-2.00 | 72 | 126 |
| FIT_FLOAT | 30 | **9435406** | 2.72x | 30 | 0 | 1.93-3.95 | 88 | 229 |
| FIT_RGB16 | 30 | **9435406** | 1.50x | 30 | 0 | 1.26-1.90 | 195 | 287 |
| FIT_RGBA16 | 60 | **9435406** | 1.59x | 60 | 0 | 1.34-2.15 | 256 | 374 |
| FIT_RGBF | 30 | **9435406** | 2.19x | 30 | 0 | 1.70-3.08 | 215 | 448 |
| FIT_RGBAF | 30 | **9435406** | 1.97x | 30 | 0 | 1.65-2.48 | 271 | 546 |

| image type, operation | cases | faster | by (geomean) | 9435406 wins | unknown wins | range | median ms 9435406 | median ms unknown |
|---|---|---|---|---|---|---|---|---|
| FIT_BITMAP 1-bit, up x1.6 | 40 | **9435406** | 1.35x | 40 | 0 | 1.27-1.45 | 435 | 601 |
| FIT_BITMAP 1-bit, down x0.48 | 40 | **9435406** | 1.34x | 40 | 0 | 1.22-1.58 | 144 | 198 |
| FIT_BITMAP 1-bit, down x0.192 | 40 | **9435406** | 1.19x | 40 | 0 | 1.08-1.41 | 106 | 129 |
| FIT_BITMAP 4-bit, up x1.6 | 30 | **9435406** | 1.39x | 30 | 0 | 1.31-1.50 | 574 | 813 |
| FIT_BITMAP 4-bit, down x0.48 | 30 | **9435406** | 1.43x | 30 | 0 | 1.33-1.54 | 164 | 244 |
| FIT_BITMAP 4-bit, down x0.192 | 30 | **9435406** | 1.27x | 30 | 0 | 1.20-1.42 | 105 | 141 |
| FIT_BITMAP 8-bit, up x1.6 | 50 | **9435406** | 1.57x | 50 | 0 | 1.39-1.95 | 239 | 363 |
| FIT_BITMAP 8-bit, down x0.48 | 50 | **9435406** | 1.82x | 50 | 0 | 1.47-2.52 | 66 | 107 |
| FIT_BITMAP 8-bit, down x0.192 | 50 | **9435406** | 1.70x | 50 | 0 | 1.32-2.57 | 45 | 64 |
| FIT_BITMAP 16-bit, up x1.6 | 20 | **9435406** | 1.46x | 20 | 0 | 1.40-1.51 | 606 | 885 |
| FIT_BITMAP 16-bit, down x0.48 | 20 | **9435406** | 1.49x | 20 | 0 | 1.42-1.58 | 186 | 279 |
| FIT_BITMAP 16-bit, down x0.192 | 20 | **9435406** | 1.32x | 20 | 0 | 1.26-1.47 | 124 | 165 |
| FIT_BITMAP 24-bit, up x1.6 | 10 | **9435406** | 1.73x | 10 | 0 | 1.65-1.84 | 431 | 755 |
| FIT_BITMAP 24-bit, down x0.48 | 10 | **9435406** | 2.21x | 10 | 0 | 2.03-2.35 | 98 | 217 |
| FIT_BITMAP 24-bit, down x0.192 | 10 | **9435406** | 2.40x | 10 | 0 | 2.28-2.49 | 46 | 111 |
| FIT_BITMAP 32-bit, up x1.6 | 30 | **9435406** | 1.77x | 30 | 0 | 1.64-1.90 | 544 | 967 |
| FIT_BITMAP 32-bit, down x0.48 | 30 | **9435406** | 2.10x | 30 | 0 | 1.87-2.28 | 128 | 268 |
| FIT_BITMAP 32-bit, down x0.192 | 30 | **9435406** | 2.46x | 30 | 0 | 2.00-2.73 | 61 | 146 |
| FIT_UINT16, up x1.6 | 10 | **9435406** | 1.30x | 10 | 0 | 1.26-1.34 | 351 | 450 |
| FIT_UINT16, down x0.48 | 10 | **9435406** | 1.77x | 10 | 0 | 1.68-1.87 | 72 | 126 |
| FIT_UINT16, down x0.192 | 10 | **9435406** | 1.90x | 10 | 0 | 1.79-2.00 | 34 | 65 |
| FIT_FLOAT, up x1.6 | 10 | **9435406** | 2.04x | 10 | 0 | 1.93-2.16 | 411 | 830 |
| FIT_FLOAT, down x0.48 | 10 | **9435406** | 2.64x | 10 | 0 | 2.54-2.69 | 88 | 229 |
| FIT_FLOAT, down x0.192 | 10 | **9435406** | 3.73x | 10 | 0 | 3.44-3.95 | 36 | 137 |
| FIT_RGB16, up x1.6 | 10 | **9435406** | 1.30x | 10 | 0 | 1.26-1.36 | 885 | 1139 |
| FIT_RGB16, down x0.48 | 10 | **9435406** | 1.48x | 10 | 0 | 1.42-1.58 | 195 | 287 |
| FIT_RGB16, down x0.192 | 10 | **9435406** | 1.75x | 10 | 0 | 1.64-1.90 | 97 | 167 |
| FIT_RGBA16, up x1.6 | 20 | **9435406** | 1.39x | 20 | 0 | 1.34-1.44 | 1125 | 1563 |
| FIT_RGBA16, down x0.48 | 20 | **9435406** | 1.53x | 20 | 0 | 1.41-1.62 | 256 | 374 |
| FIT_RGBA16, down x0.192 | 20 | **9435406** | 1.91x | 20 | 0 | 1.72-2.15 | 124 | 235 |
| FIT_RGBF, up x1.6 | 10 | **9435406** | 1.75x | 10 | 0 | 1.70-1.81 | 1128 | 1957 |
| FIT_RGBF, down x0.48 | 10 | **9435406** | 2.10x | 10 | 0 | 2.00-2.18 | 215 | 448 |
| FIT_RGBF, down x0.192 | 10 | **9435406** | 2.86x | 10 | 0 | 2.64-3.08 | 98 | 279 |
| FIT_RGBAF, up x1.6 | 10 | **9435406** | 1.68x | 10 | 0 | 1.65-1.72 | 1496 | 2495 |
| FIT_RGBAF, down x0.48 | 10 | **9435406** | 2.00x | 10 | 0 | 1.88-2.14 | 271 | 546 |
| FIT_RGBAF, down x0.192 | 10 | **9435406** | 2.29x | 10 | 0 | 2.05-2.48 | 136 | 313 |

| format | cases | faster | by (geomean) | 9435406 wins | unknown wins | range | median ms 9435406 | median ms unknown |
|---|---|---|---|---|---|---|---|---|
| b1-black | 30 | **9435406** | 1.23x | 30 | 0 | 1.08-1.39 | 113 | 132 |
| b1-white | 30 | **9435406** | 1.23x | 30 | 0 | 1.08-1.39 | 113 | 132 |
| b1-pal | 30 | **9435406** | 1.36x | 30 | 0 | 1.23-1.58 | 221 | 303 |
| b1-trns | 30 | **9435406** | 1.36x | 30 | 0 | 1.23-1.49 | 269 | 363 |
| b4-grey | 30 | **9435406** | 1.30x | 30 | 0 | 1.20-1.41 | 102 | 130 |
| b4-pal | 30 | **9435406** | 1.39x | 30 | 0 | 1.22-1.54 | 186 | 268 |
| b4-trns | 30 | **9435406** | 1.39x | 30 | 0 | 1.25-1.52 | 225 | 322 |
| b8-black | 30 | **9435406** | 2.11x | 30 | 0 | 1.62-2.57 | 37 | 87 |
| b8-white | 30 | **9435406** | 2.11x | 30 | 0 | 1.55-2.57 | 38 | 86 |
| b8-greypal | 30 | **9435406** | 1.44x | 30 | 0 | 1.33-1.54 | 66 | 101 |
| b8-pal | 30 | **9435406** | 1.47x | 30 | 0 | 1.32-1.61 | 154 | 241 |
| b8-trns | 30 | **9435406** | 1.48x | 30 | 0 | 1.32-1.66 | 190 | 295 |
| b16-555 | 30 | **9435406** | 1.43x | 30 | 0 | 1.26-1.58 | 189 | 281 |
| b16-565 | 30 | **9435406** | 1.41x | 30 | 0 | 1.26-1.51 | 185 | 272 |
| b24 | 30 | **9435406** | 2.09x | 30 | 0 | 1.65-2.49 | 98 | 217 |
| b32-rgba | 30 | **9435406** | 2.09x | 30 | 0 | 1.64-2.70 | 128 | 269 |
| b32-rgb | 30 | **9435406** | 2.09x | 30 | 0 | 1.66-2.73 | 128 | 264 |
| b32-cmyk | 30 | **9435406** | 2.08x | 30 | 0 | 1.70-2.65 | 129 | 270 |
| uint16 | 30 | **9435406** | 1.64x | 30 | 0 | 1.26-2.00 | 72 | 126 |
| float | 30 | **9435406** | 2.72x | 30 | 0 | 1.93-3.95 | 88 | 229 |
| rgb16 | 30 | **9435406** | 1.50x | 30 | 0 | 1.26-1.90 | 195 | 287 |
| rgba16 | 30 | **9435406** | 1.60x | 30 | 0 | 1.34-2.15 | 249 | 373 |
| rgba16-cmyk | 30 | **9435406** | 1.59x | 30 | 0 | 1.34-2.09 | 257 | 379 |
| rgbf | 30 | **9435406** | 2.19x | 30 | 0 | 1.70-3.08 | 215 | 448 |
| rgbaf | 30 | **9435406** | 1.97x | 30 | 0 | 1.65-2.48 | 271 | 546 |

`b32-rgba`, `b32-rgb` and `b32-cmyk` run the same code, and so do `rgba16` and `rgba16-cmyk`, `b8-black` and `b8-white`,
`b1-black` and `b1-white`: their rows agree within 0.01x, a fair picture of the noise in any one row.

### 4.3 By aspect ratio

| shape | cases | faster | by (geomean) | 9435406 wins | unknown wins | range | median ms 9435406 | median ms unknown |
|---|---|---|---|---|---|---|---|---|
| square 1:1 | 150 | **9435406** | 1.66x | 150 | 0 | 1.09-3.85 | 161 | 275 |
| wide 16:9 | 150 | **9435406** | 1.64x | 150 | 0 | 1.08-3.85 | 163 | 274 |
| ultra-wide 4:1 | 150 | **9435406** | 1.63x | 150 | 0 | 1.08-3.81 | 161 | 280 |
| tall 9:16 | 150 | **9435406** | 1.66x | 150 | 0 | 1.09-3.95 | 162 | 275 |
| very tall 1:4 | 150 | **9435406** | 1.67x | 150 | 0 | 1.09-3.93 | 160 | 275 |

| shape, operation | cases | faster | by (geomean) | 9435406 wins | unknown wins | range | median ms 9435406 | median ms unknown |
|---|---|---|---|---|---|---|---|---|
| square 1:1, up x1.6 | 50 | **9435406** | 1.52x | 50 | 0 | 1.29-2.10 | 554 | 882 |
| square 1:1, down x0.48 | 50 | **9435406** | 1.71x | 50 | 0 | 1.23-2.68 | 140 | 251 |
| square 1:1, down x0.192 | 50 | **9435406** | 1.75x | 50 | 0 | 1.09-3.85 | 92 | 141 |
| wide 16:9, up x1.6 | 50 | **9435406** | 1.51x | 50 | 0 | 1.26-2.05 | 536 | 885 |
| wide 16:9, down x0.48 | 50 | **9435406** | 1.68x | 50 | 0 | 1.23-2.69 | 141 | 245 |
| wide 16:9, down x0.192 | 50 | **9435406** | 1.73x | 50 | 0 | 1.08-3.85 | 93 | 135 |
| ultra-wide 4:1, up x1.6 | 50 | **9435406** | 1.51x | 50 | 0 | 1.26-1.99 | 555 | 884 |
| ultra-wide 4:1, down x0.48 | 50 | **9435406** | 1.68x | 50 | 0 | 1.23-2.62 | 147 | 248 |
| ultra-wide 4:1, down x0.192 | 50 | **9435406** | 1.72x | 50 | 0 | 1.08-3.81 | 93 | 136 |
| tall 9:16, up x1.6 | 50 | **9435406** | 1.52x | 50 | 0 | 1.28-2.13 | 541 | 882 |
| tall 9:16, down x0.48 | 50 | **9435406** | 1.72x | 50 | 0 | 1.22-2.64 | 139 | 249 |
| tall 9:16, down x0.192 | 50 | **9435406** | 1.76x | 50 | 0 | 1.09-3.95 | 92 | 141 |
| very tall 1:4, up x1.6 | 50 | **9435406** | 1.53x | 50 | 0 | 1.29-2.16 | 530 | 892 |
| very tall 1:4, down x0.48 | 50 | **9435406** | 1.72x | 50 | 0 | 1.22-2.68 | 139 | 253 |
| very tall 1:4, down x0.192 | 50 | **9435406** | 1.76x | 50 | 0 | 1.09-3.93 | 91 | 139 |

### 4.4 By direction, scale factor and filter

| direction | cases | faster | by (geomean) | 9435406 wins | unknown wins | range | median ms 9435406 | median ms unknown |
|---|---|---|---|---|---|---|---|---|
| upscale | 250 | **9435406** | 1.52x | 250 | 0 | 1.26-2.16 | 547 | 887 |
| downscale | 500 | **9435406** | 1.72x | 500 | 0 | 1.08-3.95 | 112 | 193 |

| operation | cases | faster | by (geomean) | 9435406 wins | unknown wins | range | median ms 9435406 | median ms unknown |
|---|---|---|---|---|---|---|---|---|
| up x1.6 | 250 | **9435406** | 1.52x | 250 | 0 | 1.26-2.16 | 547 | 887 |
| down x0.48 | 250 | **9435406** | 1.70x | 250 | 0 | 1.22-2.69 | 144 | 250 |
| down x0.192 | 250 | **9435406** | 1.74x | 250 | 0 | 1.08-3.95 | 92 | 138 |

| filter | cases | faster | by (geomean) | 9435406 wins | unknown wins | range | median ms 9435406 | median ms unknown |
|---|---|---|---|---|---|---|---|---|
| B-spline | 375 | **9435406** | 1.64x | 375 | 0 | 1.08-3.95 | 191 | 314 |
| bilinear | 375 | **9435406** | 1.66x | 375 | 0 | 1.10-3.81 | 137 | 237 |

| operation, filter | cases | faster | by (geomean) | 9435406 wins | unknown wins | range | median ms 9435406 | median ms unknown |
|---|---|---|---|---|---|---|---|---|
| up x1.6, B-spline | 125 | **9435406** | 1.52x | 125 | 0 | 1.26-2.16 | 592 | 963 |
| up x1.6, bilinear | 125 | **9435406** | 1.52x | 125 | 0 | 1.26-2.04 | 517 | 780 |
| down x0.48, B-spline | 125 | **9435406** | 1.69x | 125 | 0 | 1.22-2.69 | 180 | 296 |
| down x0.48, bilinear | 125 | **9435406** | 1.71x | 125 | 0 | 1.28-2.67 | 127 | 236 |
| down x0.192, B-spline | 125 | **9435406** | 1.73x | 125 | 0 | 1.08-3.95 | 115 | 168 |
| down x0.192, bilinear | 125 | **9435406** | 1.75x | 125 | 0 | 1.10-3.81 | 75 | 126 |

## 5. What `unknown` cannot do

### 5.1 Five image types come back blank

`274f833`'s filters have no case for `FIT_INT16`, `FIT_UINT32`, `FIT_INT32`, `FIT_DOUBLE` and `FIT_COMPLEX`: `scale()`
allocates the intermediate and the destination, runs filters that do nothing for these types and returns the destination,
all zeros, and `FreeImage_Rescale` gives no sign of it. `9435406` filters them (`54dec18`). Median ms over the 5 shapes,
`9435406` / `unknown`, for these five and the two 4-bit formats of 5.2 (`blank, N`: `unknown` returned an all-zero image
after N ms):

| format | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| b4-black | 320 / crash | 112 / crash | 92 / crash | 249 / crash | 72 / crash | 52 / crash |
| b4-white | 316 / crash | 111 / crash | 92 / crash | 248 / crash | 72 / crash | 51 / crash |
| int16 | 402 / blank, 213 | 88 / blank, 38 | 44 / blank, 13 | 346 / blank, 213 | 67 / blank, 38 | 29 / blank, 12 |
| uint32 | 534 / blank, 426 | 115 / blank, 75 | 54 / blank, 25 | 460 / blank, 426 | 91 / blank, 75 | 36 / blank, 24 |
| int32 | 498 / blank, 426 | 104 / blank, 75 | 44 / blank, 25 | 438 / blank, 425 | 86 / blank, 74 | 32 / blank, 24 |
| double | 717 / blank, 850 | 145 / blank, 150 | 55 / blank, 49 | 672 / blank, 849 | 136 / blank, 150 | 48 / blank, 48 |
| complex | 1405 / blank, 1785 | 218 / blank, 300 | 93 / blank, 98 | 1390 / blank, 1784 | 192 / blank, 299 | 75 / blank, 97 |

`unknown`'s time here is allocation alone: the result and a full-size intermediate, both cleared (1.9 GB for a
144-megapixel `FIT_DOUBLE` result, 3.7 GB for `FIT_COMPLEX`). In these medians, `9435406`'s real resize takes less time
than that for `FIT_DOUBLE` at x1.6 and x0.48 and for `FIT_COMPLEX` at every factor.

### 5.2 4-bit greys 0..15 crash

A 4-bit image whose palette is exactly 0..15 (`FIC_MINISBLACK`) or 15..0 (`FIC_MINISWHITE`) takes an access violation in
`unknown`, in its first call for each of the 5 shapes (Wine exit code 5): `274f833`'s 4-bit filters read the palette without
checking it, and its `scale()` passes none for those two colour types. `402d7fd` fixed it on 17 September, and `9435406` has
the fix. FreeImage's loaders give 4-bit greys the 0, 17, ... 255 ramp instead (`b4-grey`, `FIC_PALETTE`), which both DLLs
resize, so few real files reach this.

### 5.3 The same result formats

For all 25 formats both DLLs resize, they return the same image type, bit depth and colour type (section 3.1, last
column). One thing both get wrong the same way: the result of a CMYK source (`b32-cmyk`, `rgba16-cmyk`) loses the CMYK
flag of the ICC profile, so `FreeImage_GetColorType` reports it as `FIC_RGBALPHA`; the pixels are the resized CMYK values.

## 6. The JCC erratum

On Intel's Skylake-family cores (the 6th- to 10th-generation desktop parts, this i7-7700T among them) with the 2019
microcode (here 0xf8), a jump that crosses or ends on a 32-byte boundary keeps its loop out of the decoded-uop cache, so the
loop runs from the slower legacy decoders; your 11th-generation i7-11700 is not affected. `9435406` is built with
`/QIntel-jcc-erratum`, which pads such jumps, and `unknown` is not, so `unknown` may be losing time here that it would not
lose on your machine.

The control: `274f833` and `9435406` compiled here with one compiler for all three builds (MSVC 19.29.30159, one build
after the DLLs' 30158) and the vcxproj's Release|x64 flags, as static benches (a DLL's `DllMain` would initialise
plugins not built here): `274f833` once without and once with `/QIntel-jcc-erratum`, `9435406` with it. These three and
the two DLLs ran in the same session, in rotating order, 2 rounds, over 19 formats, one per code path both DLLs have (no
same-code duplicates, nothing `unknown` cannot resize), x 6 operations at 16:9 = 114 cases. In every one of them the two
`274f833` builds and `unknown.dll` give byte-identical results, and so do the `9435406` build and `9435406.dll`.

| ratio | cases | geomean | range | over 1.05 | under 0.95 |
|---|---|---|---|---|---|
| unpadded / padded, 274f833 static (the padding alone, in the build made here) | 114 | 1.028 | 0.94-1.26 | 29 | 2 |
| unknown.dll / padded 274f833 static (what unknown.dll loses to a JCC-safe build of its own code) | 114 | 1.016 | 0.90-1.21 | 18 | 1 |
| unknown.dll / unpadded 274f833 static (DLL against static build, both unpadded) | 114 | 0.988 | 0.78-1.19 | 14 | 22 |
| 9435406.dll / padded 9435406 static (DLL against static build, both padded) | 114 | 0.998 | 0.92-1.05 | 1 | 4 |
| unknown.dll / 9435406.dll (the matrix comparison, this session) | 114 | 1.629 | 1.08-3.94 | 114 | 0 |
| padded 274f833 static / padded 9435406 static (both JCC-safe) | 114 | 1.601 | 1.01-3.98 | 113 | 0 |
| padded 274f833 static / 9435406.dll | 114 | 1.603 | 1.00-3.92 | 113 | 0 |

- The static build stands in for the DLL: `9435406.dll` against its padded static build, 0.998 on average (0.92-1.05).
- Padding alone moves `274f833`'s kernels by up to 26% on this CPU, depending on where each loop lands: in the build made
  here the unpadded 1-bit greys, 8-bit transparent palette, float and RGBF formats lose 8-12%, others nothing.
- `unknown.dll` loses 1.6% on average to the padded build of its own code, but 6-11% on three formats: 4-bit greys,
  the 8-bit grey palette and `FIT_RGBA16`.

| format | unpadded / padded 274f833 | unknown.dll / padded 274f833 | 9435406.dll speed-up over unknown.dll | over padded 274f833 |
|---|---|---|---|---|
| b1-black | 1.102 | 0.989 | 1.22x | 1.23x |
| b1-pal | 0.991 | 1.004 | 1.35x | 1.35x |
| b1-trns | 0.997 | 0.998 | 1.36x | 1.36x |
| b4-grey | 1.005 | 1.092 | 1.32x | 1.21x |
| b4-pal | 1.057 | 1.011 | 1.39x | 1.38x |
| b4-trns | 0.976 | 0.993 | 1.42x | 1.43x |
| b8-black | 1.031 | 0.999 | 2.08x | 2.08x |
| b8-greypal | 1.007 | 1.063 | 1.43x | 1.35x |
| b8-pal | 1.038 | 1.005 | 1.45x | 1.44x |
| b8-trns | 1.077 | 1.005 | 1.47x | 1.46x |
| b16-565 | 0.988 | 0.977 | 1.41x | 1.44x |
| b24 | 1.040 | 1.002 | 2.08x | 2.08x |
| b32-rgba | 1.016 | 1.004 | 2.10x | 2.09x |
| uint16 | 0.995 | 1.015 | 1.64x | 1.62x |
| float | 1.124 | 1.025 | 2.71x | 2.64x |
| rgb16 | 1.006 | 1.026 | 1.53x | 1.49x |
| rgba16 | 1.004 | 1.105 | 1.59x | 1.44x |
| rgbf | 1.082 | 1.009 | 2.19x | 2.17x |
| rgbaf | 1.014 | 0.994 | 1.96x | 1.97x |

| operation, filter | unpadded / padded 274f833 | unknown.dll / padded 274f833 | 9435406.dll over unknown.dll | over padded 274f833 |
|---|---|---|---|---|
| up x1.6, B-spline | 1.014 | 1.016 | 1.50x | 1.47x |
| up x1.6, bilinear | 1.020 | 1.014 | 1.51x | 1.48x |
| down x0.48, B-spline | 1.029 | 1.016 | 1.67x | 1.64x |
| down x0.48, bilinear | 1.038 | 1.012 | 1.69x | 1.67x |
| down x0.192, B-spline | 1.042 | 1.029 | 1.69x | 1.65x |
| down x0.192, bilinear | 1.027 | 1.010 | 1.74x | 1.72x |

The closest case with both sides padded: `b4-grey` reduced x0.192 with B-spline, 92.5 ms for padded `274f833` against 92.1 ms
for `9435406.dll`, where `unknown.dll` takes 112.3 ms (measured at 16:9; in the matrix the other four shapes of that case
give the same ratio as 16:9, 1.20-1.21x). So on a CPU without the erratum, `9435406` should still win every case but that
one, a tie; everywhere else the winner does not depend on the padding.

## 7. Memory and allocation

**Allocation.** Every call allocates its result (and `unknown` also its intermediate), and `FreeImage_AllocateT` clears the
memory. Timed on its own (5 calls each, both DLLs, which run the same code and agree within 2%):

| pixel | a 144-megapixel result | `FreeImage_AllocateT` | writing it once more | freeing it | x1.6 in 9435406 | allocation share | x1.6 in unknown | unknown's intermediate (16:9) |
|---|---|---|---|---|---|---|---|---|
| b8-black, 1 byte | 144 MB | 62 ms | 6 ms | 8 ms | 184 ms | 34% | 328 ms | 38 ms |
| b24, 3 bytes | 432 MB | 188 ms | 17 ms | 26 ms | 431 ms | 44% | 755 ms | 118 ms |
| b32-rgba, 4 bytes | 576 MB | 250 ms | 22 ms | 35 ms | 548 ms | 46% | 961 ms | 157 ms |
| uint16, 2 bytes | 288 MB | 125 ms | 11 ms | 17 ms | 351 ms | 36% | 450 ms | 78 ms |
| float, 4 bytes | 576 MB | 251 ms | 22 ms | 35 ms | 411 ms | 61% | 830 ms | 157 ms |
| rgb16, 6 bytes | 864 MB | 375 ms | 33 ms | 52 ms | 885 ms | 42% | 1139 ms | 235 ms |
| rgba16, 8 bytes | 1152 MB | 500 ms | 44 ms | 69 ms | 1137 ms | 44% | 1562 ms | 314 ms |
| rgbf, 12 bytes | 1728 MB | 749 ms | 66 ms | 103 ms | 1128 ms | 66% | 1957 ms | 469 ms |
| rgbaf, 16 bytes | 2304 MB | 998 ms | 88 ms | 136 ms | 1496 ms | 67% | 2495 ms | 621 ms |

The time goes into faulting in fresh pages: writing the same image once more takes a tenth of it. Both DLLs pay it for the
result, one thread doing all of it inside `FreeImage_AllocateT`, which is why the x1.6 upscales gain less than the
reductions: 34-67% of `9435406`'s upscale time is that allocation. `unknown` pays it again for its intermediate. An untested
idea for later: the result need not be cleared, since the resize writes every pixel; its threads would then fault the
pages in as they write.

**Peak memory**, whole process (source, result, intermediate, bench), 16:9 source, B-spline, one call:

| format | source | x1.6 result | peak 9435406 | peak unknown | unknown more by | x0.192: peak 9435406 | peak unknown |
|---|---|---|---|---|---|---|---|
| b8-black | 54 MiB | 137 MiB | 230 MiB | 308 MiB | 78 MiB (+34%) | 89 MiB | 96 MiB |
| b24 | 161 MiB | 412 MiB | 612 MiB | 861 MiB | 249 MiB (+41%) | 205 MiB | 228 MiB |
| b32-rgba | 215 MiB | 549 MiB | 803 MiB | 1138 MiB | 335 MiB (+42%) | 260 MiB | 294 MiB |
| uint16 | 107 MiB | 275 MiB | 421 MiB | 585 MiB | 164 MiB (+39%) | 145 MiB | 162 MiB |
| float | 215 MiB | 549 MiB | 803 MiB | 1138 MiB | 335 MiB (+42%) | 260 MiB | 294 MiB |
| rgb16 | 322 MiB | 824 MiB | 1187 MiB | 1692 MiB | 505 MiB (+43%) | 371 MiB | 426 MiB |
| rgba16 | 429 MiB | 1099 MiB | 1570 MiB | 2246 MiB | 676 MiB (+43%) | 484 MiB | 558 MiB |
| rgbf | 644 MiB | 1648 MiB | 2338 MiB | 3353 MiB | 1015 MiB (+43%) | 709 MiB | 822 MiB |
| rgbaf | 858 MiB | 2197 MiB | 3101 MiB | 4461 MiB | 1360 MiB (+44%) | 928 MiB | 1085 MiB |

`unknown`'s extra is its full-size intermediate: source width x result height, 1.6 times the source for x1.6. `9435406`
keeps only bands of about 4 MB between the passes (`9ee588d`).

## 8. Why `9435406` is faster

From the code, not measured pass by pass:

- **8-bit greys, 24- and 32-bit** (2.1x): `9435406` runs both passes in `HorizontalFilterBytes` / `VerticalFilterBytes`: each
  source pixel is converted once into a ring of doubles (`394d42a`, `59cf8ef`), two destination pixels or rows are made per
  sweep of the ring, in AVX2 on this CPU and on yours (`2edd1da`), in the pass order these kernels make cheaper (`b5c8f48`).
  `274f833` has an SSE2 horizontal kernel that filters two rows at once from a tile of converted samples, and a vertical
  loop over rows (`verticalFilterRows`) that converts every source sample again for every tap.
- **`FIT_FLOAT`, `FIT_RGBF`, `FIT_RGBAF`** (2.0-2.7x) and **`FIT_UINT16`, `FIT_RGB16`, `FIT_RGBA16`** (1.5-1.6x):
  `274f833` filters horizontally a pixel at a time (for the three float types in one loop that reads the channel count at run
  time) and vertically with `verticalFilterRows`. `9435406` runs template kernels with the sample type and channel count
  fixed at compile time and a row-blocked vertical pass (`18e1a5f`), in the order the tap costs favour (`d60316b`). The
  float reductions gain most because the orders differ: at x0.192, `274f833` filters horizontally first (its rule: the width
  shrinks), over all source rows with its slowest loop; `9435406` filters vertically first.
- **1- and 4-bit, palettes, 555/565** (1.2-1.5x): the first pass still reads the source a pixel at a time through the
  palette or the bit fields, in both; in `9435406` the second pass then runs on the 8-, 24- or 32-bit intermediate with the
  byte kernels above. Half of the work got faster, so the gain is smaller.
- **Every format**: bands of about 4 MB instead of a full-size intermediate that `274f833` allocates, clears and writes on
  every call (section 7).

## 9. How much to trust these numbers

- **Identical code, different format**: the 360 pairs of case medians of same-code formats (section 4.2) differ by 0.8% at
  the median, 3.1% at the 90th percentile, 9.3% at most.
- **Round against round**: the 1842 case medians of round 1 differ from round 2's by 0.8% at the median, 4.8% at the 90th
  percentile, 30.5% at most (`unknown`, `b1-pal` 4:1 x0.192, an image that then got two more rounds); 26 differ by more
  than 10%.
- **Minimum instead of median**: comparing the fastest call of each DLL instead of the median changes no winner.
- **Other processes** used 0.05 cores during the median run, 0.11 at the 95th percentile and 0.24 at most, after the 3 busy
  processes were dropped; the gate held runs back for 40 s in all.
- **Calls**: 6, 10 and 14 per case and DLL (x1.6, x0.48, x0.192), 9 to 28 in the images that got more rounds.
- The lead is at least 7.5% in every case, and 20% or more (rounded) in all but the 20 1-bit greyscale cases reduced x0.192
  (1.075-1.126x); within that group the five shapes agree within 3%, so this small lead is real.
- The five shapes of a format give nearly the same ratio; with large noise they would not.
- Wine runs the DLLs as native x64 code; it translates the system calls (memory, threads, timer), which both DLLs make alike.
  Allocating fresh memory (section 7) is the one part where Windows itself could be faster or slower than Wine on Linux, and
  it is the same for both DLLs.

## Appendix A. Every case

Median ms of every case, `9435406` / `unknown`, the faster in bold.

**b1-black**: FIT_BITMAP 1-bit, FIC_MINISBLACK; result 8-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **336** / 437 | **124** / 152 | **104** / 113 | **258** / 349 | **77** / 100 | **57** / 64 |
| 16:9 10000 x 5625 | **332** / 441 | **122** / 153 | **104** / 112 | **252** / 345 | **77** / 100 | **57** / 64 |
| 4:1 15000 x 3750 | **341** / 433 | **123** / 152 | **104** / 112 | **253** / 352 | **78** / 100 | **57** / 63 |
| 9:16 5625 x 10000 | **331** / 433 | **123** / 150 | **104** / 114 | **265** / 358 | **78** / 101 | **57** / 65 |
| 1:4 3750 x 15000 | **332** / 457 | **123** / 155 | **104** / 113 | **259** / 348 | **78** / 101 | **57** / 64 |

**b1-white**: FIT_BITMAP 1-bit, FIC_MINISWHITE; result 8-bit FIC_MINISWHITE

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **332** / 441 | **123** / 153 | **103** / 113 | **255** / 344 | **77** / 100 | **57** / 64 |
| 16:9 10000 x 5625 | **333** / 432 | **123** / 151 | **103** / 112 | **252** / 349 | **77** / 100 | **57** / 63 |
| 4:1 15000 x 3750 | **335** / 428 | **124** / 153 | **104** / 112 | **253** / 352 | **78** / 101 | **57** / 64 |
| 9:16 5625 x 10000 | **338** / 434 | **123** / 152 | **104** / 112 | **253** / 342 | **77** / 102 | **57** / 64 |
| 1:4 3750 x 15000 | **333** / 447 | **124** / 151 | **104** / 113 | **256** / 346 | **78** / 101 | **58** / 65 |

**b1-pal**: FIT_BITMAP 1-bit, FIC_PALETTE; result 24-bit FIC_RGB

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **702** / 948 | **250** / 351 | **192** / 246 | **552** / 769 | **165** / 242 | **111** / 144 |
| 16:9 10000 x 5625 | **710** / 929 | **250** / 353 | **191** / 242 | **534** / 765 | **169** / 247 | **115** / 144 |
| 4:1 15000 x 3750 | **734** / 936 | **253** / 351 | **192** / 236 | **554** / 745 | **165** / 255 | **112** / 151 |
| 9:16 5625 x 10000 | **702** / 946 | **256** / 348 | **191** / 244 | **546** / 755 | **166** / 247 | **113** / 146 |
| 1:4 3750 x 15000 | **692** / 923 | **249** / 349 | **191** / 236 | **529** / 753 | **163** / 258 | **108** / 152 |

**b1-trns**: FIT_BITMAP 1-bit, FIC_PALETTE; result 32-bit FIC_RGBALPHA

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **896** / 1181 | **307** / 422 | **231** / 293 | **677** / 985 | **205** / 305 | **137** / 194 |
| 16:9 10000 x 5625 | **885** / 1183 | **310** / 420 | **231** / 294 | **662** / 951 | **218** / 294 | **139** / 174 |
| 4:1 15000 x 3750 | **916** / 1161 | **316** / 426 | **233** / 288 | **667** / 967 | **208** / 303 | **139** / 185 |
| 9:16 5625 x 10000 | **891** / 1160 | **305** / 421 | **231** / 283 | **671** / 966 | **204** / 291 | **136** / 184 |
| 1:4 3750 x 15000 | **886** / 1179 | **305** / 425 | **230** / 284 | **676** / 969 | **201** / 298 | **133** / 185 |

**b4-grey**: FIT_BITMAP 4-bit, FIC_PALETTE; result 8-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **317** / 422 | **112** / 150 | **92** / 111 | **247** / 344 | **72** / 101 | **51** / 62 |
| 16:9 10000 x 5625 | **316** / 424 | **111** / 148 | **91** / 110 | **248** / 333 | **72** / 101 | **51** / 63 |
| 4:1 15000 x 3750 | **316** / 423 | **112** / 150 | **92** / 111 | **247** / 340 | **72** / 101 | **52** / 62 |
| 9:16 5625 x 10000 | **314** / 411 | **111** / 150 | **91** / 111 | **246** / 334 | **72** / 101 | **51** / 62 |
| 1:4 3750 x 15000 | **321** / 423 | **112** / 149 | **92** / 110 | **248** / 325 | **72** / 101 | **52** / 63 |

**b4-black**: FIT_BITMAP 4-bit, FIC_MINISBLACK; result 8-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | 320 / crash | 111 / crash | 91 / crash | 250 / crash | 72 / crash | 51 / crash |
| 16:9 10000 x 5625 | 321 / crash | 111 / crash | 92 / crash | 247 / crash | 72 / crash | 52 / crash |
| 4:1 15000 x 3750 | 320 / crash | 112 / crash | 92 / crash | 249 / crash | 72 / crash | 52 / crash |
| 9:16 5625 x 10000 | 315 / crash | 112 / crash | 91 / crash | 248 / crash | 72 / crash | 52 / crash |
| 1:4 3750 x 15000 | 315 / crash | 112 / crash | 92 / crash | 250 / crash | 72 / crash | 52 / crash |

**b4-white**: FIT_BITMAP 4-bit, FIC_MINISWHITE; result 8-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | 314 / crash | 111 / crash | 92 / crash | 250 / crash | 72 / crash | 51 / crash |
| 16:9 10000 x 5625 | 316 / crash | 111 / crash | 91 / crash | 246 / crash | 72 / crash | 51 / crash |
| 4:1 15000 x 3750 | 320 / crash | 112 / crash | 92 / crash | 248 / crash | 72 / crash | 52 / crash |
| 9:16 5625 x 10000 | 313 / crash | 112 / crash | 92 / crash | 248 / crash | 72 / crash | 51 / crash |
| 1:4 3750 x 15000 | 316 / crash | 111 / crash | 92 / crash | 248 / crash | 72 / crash | 52 / crash |

**b4-pal**: FIT_BITMAP 4-bit, FIC_PALETTE; result 24-bit FIC_RGB

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **649** / 887 | **215** / 309 | **157** / 193 | **517** / 732 | **147** / 226 | **92** / 127 |
| 16:9 10000 x 5625 | **648** / 892 | **216** / 310 | **157** / 192 | **513** / 735 | **147** / 221 | **94** / 125 |
| 4:1 15000 x 3750 | **665** / 878 | **219** / 312 | **159** / 195 | **515** / 749 | **151** / 225 | **95** / 127 |
| 9:16 5625 x 10000 | **626** / 896 | **214** / 312 | **157** / 193 | **523** / 734 | **147** / 226 | **93** / 131 |
| 1:4 3750 x 15000 | **629** / 893 | **215** / 311 | **157** / 193 | **505** / 716 | **146** / 222 | **91** / 120 |

**b4-trns**: FIT_BITMAP 4-bit, FIC_PALETTE; result 32-bit FIC_RGBALPHA

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **811** / 1137 | **264** / 375 | **187** / 248 | **634** / 928 | **178** / 262 | **115** / 154 |
| 16:9 10000 x 5625 | **807** / 1123 | **266** / 373 | **188** / 241 | **636** / 925 | **186** / 269 | **121** / 154 |
| 4:1 15000 x 3750 | **843** / 1111 | **272** / 374 | **189** / 240 | **639** / 927 | **182** / 268 | **121** / 151 |
| 9:16 5625 x 10000 | **807** / 1124 | **262** / 375 | **187** / 247 | **629** / 941 | **178** / 271 | **115** / 163 |
| 1:4 3750 x 15000 | **801** / 1119 | **262** / 377 | **187** / 241 | **638** / 935 | **180** / 268 | **117** / 152 |

**b8-black**: FIT_BITMAP 8-bit, FIC_MINISBLACK; result 8-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **184** / 351 | **39** / 96 | **21** / 53 | **178** / 296 | **35** / 77 | **17** / 37 |
| 16:9 10000 x 5625 | **196** / 349 | **40** / 96 | **21** / 53 | **183** / 297 | **35** / 76 | **17** / 37 |
| 4:1 15000 x 3750 | **202** / 353 | **41** / 97 | **21** / 53 | **187** / 306 | **37** / 77 | **18** / 38 |
| 9:16 5625 x 10000 | **194** / 355 | **38** / 96 | **21** / 53 | **178** / 295 | **34** / 77 | **17** / 37 |
| 1:4 3750 x 15000 | **184** / 358 | **39** / 97 | **21** / 53 | **175** / 297 | **35** / 77 | **17** / 37 |

**b8-white**: FIT_BITMAP 8-bit, FIC_MINISWHITE; result 8-bit FIC_MINISWHITE

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **184** / 352 | **39** / 96 | **21** / 52 | **179** / 303 | **35** / 77 | **17** / 37 |
| 16:9 10000 x 5625 | **197** / 349 | **40** / 97 | **21** / 53 | **184** / 300 | **36** / 77 | **17** / 37 |
| 4:1 15000 x 3750 | **208** / 359 | **41** / 96 | **21** / 53 | **189** / 293 | **37** / 77 | **18** / 37 |
| 9:16 5625 x 10000 | **186** / 350 | **39** / 96 | **21** / 53 | **178** / 304 | **35** / 77 | **17** / 37 |
| 1:4 3750 x 15000 | **186** / 356 | **39** / 96 | **21** / 53 | **175** / 297 | **35** / 77 | **17** / 37 |

**b8-greypal**: FIT_BITMAP 8-bit, FIC_PALETTE; result 8-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **259** / 369 | **76** / 117 | **56** / 75 | **219** / 310 | **55** / 84 | **34** / 47 |
| 16:9 10000 x 5625 | **264** / 377 | **76** / 117 | **56** / 75 | **221** / 310 | **55** / 85 | **34** / 47 |
| 4:1 15000 x 3750 | **264** / 377 | **77** / 118 | **57** / 75 | **220** / 311 | **56** / 84 | **34** / 47 |
| 9:16 5625 x 10000 | **257** / 367 | **76** / 117 | **56** / 75 | **219** / 308 | **55** / 84 | **34** / 46 |
| 1:4 3750 x 15000 | **260** / 377 | **76** / 118 | **56** / 76 | **219** / 307 | **56** / 84 | **34** / 47 |

**b8-pal**: FIT_BITMAP 8-bit, FIC_PALETTE; result 24-bit FIC_RGB

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **591** / 843 | **180** / 275 | **125** / 167 | **478** / 704 | **127** / 205 | **78** / 110 |
| 16:9 10000 x 5625 | **606** / 842 | **184** / 279 | **125** / 166 | **478** / 722 | **128** / 206 | **73** / 110 |
| 4:1 15000 x 3750 | **598** / 846 | **183** / 281 | **126** / 166 | **483** / 736 | **128** / 207 | **75** / 110 |
| 9:16 5625 x 10000 | **574** / 821 | **179** / 279 | **124** / 163 | **481** / 709 | **127** / 199 | **74** / 104 |
| 1:4 3750 x 15000 | **585** / 837 | **180** / 278 | **123** / 173 | **476** / 713 | **127** / 204 | **78** / 105 |

**b8-trns**: FIT_BITMAP 8-bit, FIC_PALETTE; result 32-bit FIC_RGBALPHA

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **723** / 1085 | **229** / 336 | **152** / 208 | **589** / 912 | **153** / 253 | **98** / 139 |
| 16:9 10000 x 5625 | **726** / 1069 | **226** / 337 | **153** / 216 | **582** / 898 | **153** / 243 | **98** / 130 |
| 4:1 15000 x 3750 | **735** / 1076 | **230** / 338 | **154** / 210 | **588** / 903 | **156** / 244 | **99** / 136 |
| 9:16 5625 x 10000 | **730** / 1066 | **223** / 336 | **155** / 207 | **593** / 922 | **152** / 253 | **97** / 144 |
| 1:4 3750 x 15000 | **710** / 1077 | **224** / 339 | **154** / 217 | **613** / 935 | **154** / 254 | **99** / 142 |

**b16-555**: FIT_BITMAP 16-bit, FIC_RGB; result 24-bit FIC_RGB

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **673** / 1007 | **221** / 329 | **158** / 202 | **555** / 812 | **155** / 242 | **94** / 137 |
| 16:9 10000 x 5625 | **690** / 965 | **222** / 320 | **158** / 204 | **538** / 780 | **154** / 232 | **96** / 128 |
| 4:1 15000 x 3750 | **687** / 974 | **224** / 318 | **158** / 205 | **561** / 802 | **157** / 238 | **96** / 135 |
| 9:16 5625 x 10000 | **675** / 958 | **220** / 325 | **157** / 201 | **535** / 773 | **157** / 240 | **95** / 135 |
| 1:4 3750 x 15000 | **662** / 978 | **220** / 321 | **157** / 197 | **531** / 777 | **155** / 244 | **95** / 133 |

**b16-565**: FIT_BITMAP 16-bit, FIC_RGB; result 24-bit FIC_RGB

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **693** / 1005 | **215** / 317 | **152** / 193 | **539** / 795 | **151** / 229 | **93** / 125 |
| 16:9 10000 x 5625 | **674** / 962 | **219** / 314 | **152** / 203 | **535** / 794 | **155** / 225 | **95** / 126 |
| 4:1 15000 x 3750 | **693** / 1007 | **220** / 315 | **153** / 193 | **549** / 790 | **152** / 230 | **94** / 127 |
| 9:16 5625 x 10000 | **668** / 963 | **215** / 314 | **152** / 193 | **527** / 796 | **151** / 229 | **93** / 125 |
| 1:4 3750 x 15000 | **651** / 982 | **215** / 314 | **152** / 193 | **528** / 789 | **152** / 229 | **95** / 122 |

**b24**: FIT_BITMAP 24-bit, FIC_RGB; result 24-bit FIC_RGB

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **454** / 801 | **103** / 241 | **50** / 124 | **415** / 686 | **92** / 192 | **42** / 96 |
| 16:9 10000 x 5625 | **473** / 804 | **105** / 242 | **51** / 125 | **416** / 695 | **94** / 191 | **42** / 96 |
| 4:1 15000 x 3750 | **448** / 803 | **102** / 238 | **52** / 127 | **406** / 685 | **92** / 196 | **42** / 98 |
| 9:16 5625 x 10000 | **446** / 820 | **102** / 238 | **50** / 125 | **406** / 710 | **91** / 192 | **41** / 97 |
| 1:4 3750 x 15000 | **463** / 841 | **102** / 240 | **50** / 125 | **407** / 678 | **92** / 193 | **42** / 97 |

**b32-rgba**: FIT_BITMAP 32-bit, FIC_RGBALPHA; result 32-bit FIC_RGBALPHA

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **578** / 1060 | **133** / 295 | **63** / 163 | **534** / 874 | **119** / 236 | **54** / 123 |
| 16:9 10000 x 5625 | **565** / 1062 | **135** / 294 | **63** / 162 | **513** / 869 | **121** / 241 | **57** / 128 |
| 4:1 15000 x 3750 | **563** / 1068 | **142** / 296 | **63** / 167 | **502** / 868 | **126** / 237 | **56** / 124 |
| 9:16 5625 x 10000 | **573** / 1041 | **130** / 297 | **62** / 168 | **517** / 872 | **116** / 247 | **54** / 129 |
| 1:4 3750 x 15000 | **592** / 1038 | **131** / 291 | **62** / 163 | **516** / 884 | **115** / 237 | **53** / 126 |

**b32-rgb**: FIT_BITMAP 32-bit, FIC_RGB; result 32-bit FIC_RGB

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **592** / 1033 | **133** / 295 | **63** / 165 | **527** / 878 | **118** / 237 | **53** / 126 |
| 16:9 10000 x 5625 | **570** / 1052 | **135** / 287 | **63** / 164 | **506** / 883 | **121** / 238 | **54** / 121 |
| 4:1 15000 x 3750 | **570** / 1035 | **142** / 295 | **64** / 165 | **516** / 891 | **127** / 237 | **55** / 122 |
| 9:16 5625 x 10000 | **565** / 1059 | **130** / 296 | **62** / 169 | **517** / 875 | **116** / 242 | **53** / 130 |
| 1:4 3750 x 15000 | **574** / 1075 | **131** / 295 | **62** / 166 | **525** / 892 | **116** / 236 | **54** / 132 |

**b32-cmyk**: FIT_BITMAP 32-bit, FIC_CMYK; result 32-bit FIC_RGBALPHA

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **583** / 1054 | **132** / 297 | **64** / 162 | **525** / 893 | **119** / 249 | **58** / 132 |
| 16:9 10000 x 5625 | **581** / 1035 | **135** / 295 | **63** / 160 | **522** / 903 | **121** / 240 | **56** / 130 |
| 4:1 15000 x 3750 | **555** / 1040 | **142** / 296 | **63** / 164 | **508** / 898 | **127** / 237 | **60** / 121 |
| 9:16 5625 x 10000 | **579** / 1032 | **131** / 292 | **62** / 163 | **514** / 889 | **117** / 240 | **54** / 129 |
| 1:4 3750 x 15000 | **573** / 1068 | **131** / 294 | **62** / 164 | **516** / 893 | **116** / 237 | **55** / 126 |

**uint16**: FIT_UINT16, FIC_MINISBLACK; result 16-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **382** / 491 | **81** / 138 | **41** / 77 | **321** / 421 | **61** / 114 | **26** / 52 |
| 16:9 10000 x 5625 | **370** / 489 | **83** / 139 | **42** / 77 | **318** / 423 | **62** / 114 | **27** / 53 |
| 4:1 15000 x 3750 | **377** / 487 | **84** / 141 | **43** / 77 | **335** / 423 | **63** / 116 | **27** / 53 |
| 9:16 5625 x 10000 | **368** / 476 | **81** / 137 | **41** / 76 | **326** / 420 | **61** / 114 | **26** / 52 |
| 1:4 3750 x 15000 | **372** / 496 | **82** / 139 | **42** / 77 | **317** / 421 | **61** / 114 | **26** / 53 |

**int16**: FIT_INT16, FIC_MINISBLACK; result 16-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | 399 / blank | 87 / blank | 44 / blank | 346 / blank | 67 / blank | 30 / blank |
| 16:9 10000 x 5625 | 402 / blank | 88 / blank | 44 / blank | 350 / blank | 67 / blank | 29 / blank |
| 4:1 15000 x 3750 | 416 / blank | 89 / blank | 45 / blank | 360 / blank | 68 / blank | 30 / blank |
| 9:16 5625 x 10000 | 401 / blank | 87 / blank | 44 / blank | 346 / blank | 66 / blank | 29 / blank |
| 1:4 3750 x 15000 | 407 / blank | 88 / blank | 44 / blank | 346 / blank | 67 / blank | 29 / blank |

**uint32**: FIT_UINT32, FIC_MINISBLACK; result 32-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | 539 / blank | 116 / blank | 55 / blank | 460 / blank | 91 / blank | 36 / blank |
| 16:9 10000 x 5625 | 534 / blank | 114 / blank | 54 / blank | 452 / blank | 91 / blank | 36 / blank |
| 4:1 15000 x 3750 | 538 / blank | 118 / blank | 56 / blank | 456 / blank | 94 / blank | 37 / blank |
| 9:16 5625 x 10000 | 531 / blank | 115 / blank | 54 / blank | 461 / blank | 91 / blank | 36 / blank |
| 1:4 3750 x 15000 | 517 / blank | 114 / blank | 54 / blank | 464 / blank | 91 / blank | 36 / blank |

**int32**: FIT_INT32, FIC_MINISBLACK; result 32-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | 500 / blank | 104 / blank | 45 / blank | 438 / blank | 86 / blank | 32 / blank |
| 16:9 10000 x 5625 | 496 / blank | 103 / blank | 44 / blank | 438 / blank | 86 / blank | 32 / blank |
| 4:1 15000 x 3750 | 517 / blank | 107 / blank | 46 / blank | 443 / blank | 88 / blank | 32 / blank |
| 9:16 5625 x 10000 | 498 / blank | 104 / blank | 44 / blank | 436 / blank | 83 / blank | 32 / blank |
| 1:4 3750 x 15000 | 486 / blank | 104 / blank | 44 / blank | 444 / blank | 86 / blank | 31 / blank |

**float**: FIT_FLOAT, FIC_MINISBLACK; result 32-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **430** / 903 | **94** / 253 | **42** / 162 | **377** / 770 | **82** / 207 | **30** / 109 |
| 16:9 10000 x 5625 | **434** / 887 | **95** / 254 | **42** / 162 | **397** / 773 | **79** / 207 | **30** / 105 |
| 4:1 15000 x 3750 | **463** / 894 | **96** / 253 | **43** / 164 | **387** / 769 | **80** / 207 | **31** / 105 |
| 9:16 5625 x 10000 | **424** / 906 | **95** / 251 | **42** / 166 | **380** / 760 | **78** / 207 | **30** / 105 |
| 1:4 3750 x 15000 | **429** / 928 | **94** / 252 | **41** / 163 | **380** / 770 | **78** / 207 | **29** / 112 |

**double**: FIT_DOUBLE, FIC_MINISBLACK; result 64-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | 717 / blank | 145 / blank | 55 / blank | 672 / blank | 136 / blank | 48 / blank |
| 16:9 10000 x 5625 | 723 / blank | 147 / blank | 56 / blank | 700 / blank | 140 / blank | 49 / blank |
| 4:1 15000 x 3750 | 744 / blank | 150 / blank | 60 / blank | 702 / blank | 144 / blank | 52 / blank |
| 9:16 5625 x 10000 | 683 / blank | 144 / blank | 54 / blank | 661 / blank | 136 / blank | 48 / blank |
| 1:4 3750 x 15000 | 678 / blank | 145 / blank | 54 / blank | 657 / blank | 135 / blank | 48 / blank |

**complex**: FIT_COMPLEX, FIC_MINISBLACK; result 128-bit FIC_MINISBLACK

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | 1405 / blank | 218 / blank | 93 / blank | 1390 / blank | 192 / blank | 78 / blank |
| 16:9 10000 x 5625 | 1417 / blank | 221 / blank | 93 / blank | 1400 / blank | 198 / blank | 75 / blank |
| 4:1 15000 x 3750 | 1413 / blank | 232 / blank | 100 / blank | 1415 / blank | 208 / blank | 76 / blank |
| 9:16 5625 x 10000 | 1367 / blank | 217 / blank | 92 / blank | 1365 / blank | 188 / blank | 74 / blank |
| 1:4 3750 x 15000 | 1358 / blank | 209 / blank | 91 / blank | 1317 / blank | 188 / blank | 73 / blank |

**rgb16**: FIT_RGB16, FIC_RGB; result 48-bit FIC_RGB

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **949** / 1230 | **220** / 317 | **117** / 192 | **780** / 1042 | **163** / 257 | **75** / 142 |
| 16:9 10000 x 5625 | **969** / 1223 | **223** / 317 | **118** / 196 | **777** / 1055 | **169** / 253 | **75** / 140 |
| 4:1 15000 x 3750 | **965** / 1220 | **219** / 317 | **116** / 197 | **806** / 1044 | **164** / 254 | **75** / 137 |
| 9:16 5625 x 10000 | **943** / 1231 | **220** / 319 | **116** / 195 | **827** / 1058 | **172** / 255 | **78** / 137 |
| 1:4 3750 x 15000 | **948** / 1224 | **218** / 318 | **115** / 194 | **782** / 1047 | **168** / 255 | **75** / 135 |

**rgba16**: FIT_RGBA16, FIC_RGBALPHA; result 64-bit FIC_RGBALPHA

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **1244** / 1666 | **290** / 408 | **160** / 275 | **1005** / 1437 | **213** / 337 | **95** / 200 |
| 16:9 10000 x 5625 | **1249** / 1686 | **283** / 425 | **149** / 266 | **1019** / 1444 | **211** / 337 | **96** / 193 |
| 4:1 15000 x 3750 | **1242** / 1688 | **283** / 423 | **154** / 288 | **1014** / 1459 | **209** / 339 | **95** / 204 |
| 9:16 5625 x 10000 | **1237** / 1676 | **288** / 427 | **158** / 275 | **1017** / 1449 | **215** / 335 | **93** / 190 |
| 1:4 3750 x 15000 | **1246** / 1677 | **284** / 426 | **150** / 277 | **1036** / 1432 | **211** / 337 | **94** / 191 |

**rgba16-cmyk**: FIT_RGBA16, FIC_CMYK; result 64-bit FIC_RGBALPHA

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **1243** / 1705 | **292** / 423 | **160** / 277 | **1032** / 1442 | **216** / 338 | **98** / 199 |
| 16:9 10000 x 5625 | **1240** / 1679 | **284** / 427 | **155** / 283 | **1012** / 1439 | **230** / 339 | **99** / 195 |
| 4:1 15000 x 3750 | **1259** / 1712 | **286** / 427 | **158** / 278 | **1018** / 1461 | **213** / 336 | **99** / 194 |
| 9:16 5625 x 10000 | **1254** / 1674 | **285** / 419 | **157** / 275 | **1008** / 1441 | **211** / 337 | **94** / 190 |
| 1:4 3750 x 15000 | **1214** / 1687 | **286** / 420 | **150** / 274 | **1026** / 1442 | **210** / 336 | **92** / 192 |

**rgbf**: FIT_RGBF, FIC_RGB; result 96-bit FIC_RGB

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **1177** / 2064 | **230** / 492 | **116** / 330 | **1068** / 1863 | **197** / 396 | **78** / 233 |
| 16:9 10000 x 5625 | **1194** / 2058 | **228** / 498 | **122** / 324 | **1091** / 1859 | **197** / 397 | **81** / 232 |
| 4:1 15000 x 3750 | **1191** / 2116 | **234** / 502 | **122** / 327 | **1086** / 1875 | **202** / 404 | **86** / 232 |
| 9:16 5625 x 10000 | **1168** / 2049 | **228** / 493 | **111** / 323 | **1046** / 1842 | **188** / 400 | **76** / 234 |
| 1:4 3750 x 15000 | **1165** / 2038 | **228** / 492 | **114** / 330 | **1022** / 1850 | **187** / 397 | **76** / 232 |

**rgbaf**: FIT_RGBAF, FIC_RGBALPHA; result 128-bit FIC_RGBALPHA

| shape | up x1.6 B-spline | down x0.48 B-spline | down x0.192 B-spline | up x1.6 bilinear | down x0.48 bilinear | down x0.192 bilinear |
|---|---|---|---|---|---|---|
| 1:1 7500 x 7500 | **1566** / 2652 | **313** / 599 | **162** / 369 | **1417** / 2385 | **237** / 490 | **105** / 260 |
| 16:9 10000 x 5625 | **1563** / 2640 | **318** / 598 | **173** / 374 | **1430** / 2372 | **236** / 494 | **108** / 260 |
| 4:1 15000 x 3750 | **1562** / 2689 | **322** / 608 | **183** / 374 | **1429** / 2389 | **241** / 490 | **108** / 260 |
| 9:16 5625 x 10000 | **1563** / 2621 | **311** / 598 | **170** / 366 | **1417** / 2380 | **233** / 489 | **104** / 259 |
| 1:4 3750 x 15000 | **1574** / 2602 | **302** / 601 | **170** / 366 | **1384** / 2354 | **227** / 486 | **110** / 260 |

## Appendix B. The rig

Not committed; in `.claude/scratch/resize-matrix/` of this worktree:

- `rmatrix.cpp`: the bench (`rmatrix_matrix.cpp` is the exact source of the matrix runs); `driver.py`: the matrix, resumable;
  `results.jsonl`: every process with its output lines, wall time and the other processes' CPU use; `analyze.py`: the tables.
- `jcc/`: `extract.sh`, `jccbuild.py` (the static builds), `jccdriver.py`, `jcc.jsonl`, `jcc_analyze.py`.
- `allocbench.cpp`, `alloc_run.sh`, `memwatch.py`, `memtable.sh`, `memtables.py`: section 7.
