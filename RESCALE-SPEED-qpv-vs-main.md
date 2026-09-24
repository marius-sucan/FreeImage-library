# FreeImage_Rescale speed: qpv vs main

qpv `3e3323b` plus the pass-order change on branch `rescale-speed`, against origin/main `a976f39`, measured 2026-09-24.
Main's source is identical to `5af83c4` (only README.md differs). Speedup = main time / qpv time. Above 1.00x qpv is faster;
**bold** marks a case where qpv is more than 5% slower.

## Answer

- **Multi-threaded, qpv is 5.32x faster than main** (geometric mean over the 168 cases main can do): **6.50x on downscales,
  4.36x on upscales**, 2.70x to 14.5x, and **never slower**.
  - These runs use 6 threads: another process held a core and pre-empted 8-thread runs.
  - Before the pass-order change qpv measured 4.52x at 6 threads in this run, and 4.94x at 8 threads in the report's first
    run.
  - Main has no OpenMP pragmas at all, so it resizes on one thread on every compiler.
- **Per core (qpv forced to 1 thread), qpv is 1.83x faster**: 1.91x down, 1.77x up, 1.13x to 5.75x, and **never slower**.
  - The upscales that were 5-11% slower than main per core before the change are now faster: 1-bit 1.39x, 4-bit 1.53x,
    8-bit palette 1.40x and 16-bit 565 1.45x (means over the filters; 555 1.46x).
- **The pass-order change alone gives 1.26x at 1 thread and 1.18x at 6 threads** over those 168 cases, up to 2.0x. Over all
  228 filtered cases, including the types main cannot resize, it gives 1.24x and 1.16x.
  - At 1 thread no case is more than 5% slower than before.
  - At 6 threads 12 cases measure 5-14% slower. 11 of them run exactly the code they ran before, so that is noise. The
    twelfth, a FIT_DOUBLE reduction with BILINEAR, really is 12% slower there (about 9 ms against 8), and 1.32x faster
    at 1 thread.
- **Output:** 55 of the 168 comparable cases are byte-identical to main.
  - The other 113 are the cases whose pass order changed, except BOX enlargements, whose exact weights give the same
    pixels in either order.
  - In those 113, 13-31% of the samples differ by one step, at most two (float: the last bit or two).
  - The error against an exact resize is the same for both orders.
- **qpv only:** FILTER_NEAREST (0.5-105 ms at 6 threads), and FIT_INT16/UINT32/INT32/DOUBLE/COMPLEX. For these five types
  main returns an all-zero image, because its engine has no kernel for them.

## The pass order

A resize in both directions runs two passes, one per direction.
- **Main's rule:** filter horizontally first unless the width grows. It suited main's vertical pass, which walks down
  columns and was the costly one.
- **Why it no longer fits:** qpv's vertical pass reads rows for 8-bit greyscale, 24-bit, 32-bit and every non-FIT_BITMAP
  image, and per tap it is now cheaper than the horizontal pass.

`scaleInBands()` counts the taps of both orders in the two weight tables. With TX and TY the taps across one output row and
one output column:

    xy = 2 * TX * src_height + v2 * TY * dst_width
    yx = v1 * TY * src_width + 2 * TX * dst_height

- **Weights:** a horizontal tap counts 2, a row-blocked vertical tap 1, and a tap of the vertical kernels that still walk
  columns 2.5. Those kernels are 1- and 4-bit, palettes, 16-bit 555/565, and 8-bit to 24- or 32-bit.
- **v1 and v2:** the weights of the vertical pass that reads the source (yx) or the intermediate (xy).
- **The margin:** it keeps main's width rule unless the other order is cheaper by a margin, 1.1x, or 2x for 128-bit
  pixels (FIT_RGBAF, FIT_COMPLEX).

On uniform zooms, CATMULLROM (plain = 8/24/32-bit and the other plain-sample types):

| Zoom | plain | palette, 1/4-bit, 555/565 | RGBAF, COMPLEX |
|---|---|---|---|
| 0.4-0.7x down | vertical first | horizontal first (as before) | horizontal first (as before) |
| 0.8-0.9x down | horizontal first (as before) | horizontal first (as before) | horizontal first (as before) |
| 1.1-1.25x up | vertical first (as before) | horizontal first | vertical first (as before) |
| 1.5-2.5x up | horizontal first | horizontal first | vertical first (as before) |

Scored on 253 measured cases at 1 thread and 253 at 6: 19 formats at 0.4x and 2.5x; 8 formats at 0.5-0.8x and 1.25-2x;
16 other shapes (thumbnails, 8x, squashes, width-heavy and height-heavy shrinks, width-down/height-up and back).

| Rule | Mean loss vs the faster order, 1 / 6 threads | Worst loss, 1 / 6 threads | Mean vs main's rule, 1 / 6 threads | Worst vs main's rule, 1 / 6 threads |
|---|---:|---:|---:|---:|
| main's width rule | 24.4% / 20.4% | 3.61x / 3.03x | 1.00x / 1.00x | 1.00x / 1.00x |
| tap costs alone | 0.4% / 1.0% | 1.39x / 1.55x | 1.240x / 1.192x | **0.72x / 0.64x** |
| **with margins 1.1 / 2 (shipped)** | 0.9% / 0.9% | 1.23x / 1.27x | 1.233x / 1.193x | 1.00x / 1.00x |
| with margins 1.2 / 2 | 1.9% / 1.7% | 1.31x / 1.28x | 1.221x / 1.184x | 1.00x / 1.00x |

What the margins give up:
- **Mild zooms** (0.8-0.9x down, 1.1-1.25x up) of plain images keep main's order and give up measured gains of 2-26%.
  8-bit greyscale 1.1x up, for example, would be 5% faster at 1 thread and 12% at 6 threads with horizontal-first.
- **128-bit images keep main's order except in extreme cases.**
  - That margin hedges against the thread count, which the model does not have. On uniform zooms at 1 thread neither
    order wins on RGBAF (1.00-1.04x); at 6 threads vertical-first loses 5-34% on reductions, because on 16-byte pixels the row-blocked
    vertical pass is memory-bound.
  - It was fitted on RGBAF; COMPLEX is assumed to behave alike.
  - Squashes of 128-bit images, where vertical-first is 2-3x faster, are past the 2x margin and switch.
  - A shrinking COMPLEX image now takes main's order while FIT_DOUBLE may not, so COMPLEX no longer resizes bit-for-bit like
    two FIT_DOUBLE channels. The resize oracle skips those 399 comparisons and checks COMPLEX against its own reference.

The model's remaining misses:
- **Horizontal taps are cheaper than 2 when a pixel has many of them.** In width-heavy shrinks such as 4000x3000 ->
  400x1200-400x1300 (40 horizontal taps per pixel) the rule picks vertical-first for plain images. For 24-bit that is
  1-4% slower at 1 thread and 11-16% at 6. For 8-bit greyscale and FLOAT it is 16-23% faster at 1 thread and within 5% at 6.
- **FIT_DOUBLE reductions at 6 threads** (BILINEAR 12%): a milder form of the 128-bit memory limit.
- **1-bit reductions with CATMULLROM/LANCZOS3** (vertical-first would be 3-27% faster) keep main's order.

Lopsided shrinks of 4000x3000, CATMULLROM, ms, 1 thread / 6 threads (main is single-threaded):

| Shape | Format | main | qpv before | qpv now |
|---|---|---:|---:|---:|
| 3900x300 | RGBAF | 481 | 146 / 68 | 72 / 22.8 |
| 3900x300 | COMPLEX | n/a | 88 / 38 | 40 / 15.3 |
| 3900x300 | 24-bit | 271 | 190 / 53 | 65 / 18.4 |
| 3900x300 | FLOAT | 249 | 64 / 17.7 | 22 / 7.1 |
| 2000x300 | RGBAF | 304 | 101 / 31 | 76 / 24.2 |
| 2000x300 | COMPLEX | n/a | 56 / 18.5 | 39 / 14.0 |
| 2000x300 | 24-bit | 162 | 129 / 36 | 59 / 18.1 |
| 2000x300 | FLOAT | 141 | 42 / 11.2 | 20 / 6.9 |
| 400x2900 | RGBAF | 182 | 74 / 20 | 73 / 21 |
| 400x2900 | 24-bit | 102 | 97 / 24 | 93 / 28 |
| 400x1500 | RGBAF | 162 | 76 / 19 | 74 / 24 |
| 400x1500 | 24-bit | 97 | 91 / 25.5 | 90 / 25.9 |
| 400x1300 | 24-bit | 98 | 91 / 23.7 | 95 / 26.6 |
| 400x1300 | 8-bit grey | 46 | 43 / 10.2 | 37 / 10.7 |
| 400x1200 | 24-bit | 97 | 89 / 25.5 | 90 / 26.4 |
| 400x1200 | FLOAT | 81 | 39 / 9.0 | 31 / 9.2 |

In this table the builds' 6-thread runs went back to back, and the later ones measure slower. Where qpv before and now
run the same code, now measures about 7% slower on average, and up to 27% on single cases. Compare builds on the 1-thread
figures; the 6-thread figures in the misses above come from a run that alternated only two builds.

What the order change does to pixels:
- The image kept between the passes is rounded to the image type, so a changed order rounds different samples.
- It is not less accurate. On a 1000x750 <-> 400x300 photo-like image, both orders against an exact double-precision
  resize (rounded once, at the end):

| Format | Direction | Samples that differ | Largest difference | Mean error, horizontal first | Mean error, vertical first |
|---|---|---:|---:|---:|---:|
| 8-bit grey, 24-bit | down | 13.6-19.6% | 1 | 0.101-0.212 | 0.102-0.212 |
| 8-bit grey, 24-bit | up | 15.8-24.1% | 2 (negative-lobe filters), else 1 | 0.171-0.232 | 0.171-0.232 |
| UINT16, RGB16 | down | 13.5-19.7% | 1 | 0.102-0.212 | 0.102-0.212 |
| UINT16, RGB16 | up | 22.9-31.4% | 2 (negative-lobe filters), else 1 | 0.167-0.231 | 0.167-0.231 |
| FLOAT | down / up | 14.6-31.6% | 1.2e-7 | 0.44-1.00e-8 | 0.44-1.00e-8 |

Ranges run over the six filters. A BOX enlargement has exact weights, so its output never changes.

Verification of the change:
- **Only the order changes.** Of diffcases' 12,252 signatures (every layout, palettes, FI_RESCALE_TRUE_COLOR, source
  rectangles, RescaleRawBits, thumbnails), 9,633 are unchanged. 1,189 equal the old code forced to horizontal-first,
  1,430 equal it forced to vertical-first, and 0 are anything else.
- **Resize oracle** (`verify` with the same rule in its reference): 485,333 checks, 0 failures, the same hash at 1 and
  8 threads. It passes the same way:
  - with 1-byte bands (minimum 1 and 3 rows), whose diffcases output is identical to the stock build;
  - under ASan and UBSan with float-cast-overflow;
  - on sources whose rows pass 2^31 bytes.
- **Toolkit differential:** 462 of 8,202 keys differ, all of them resizes, thumbnails, and FreeImage_TmoFattal02(), which
  resizes internally.
- **TestAPI:** its log is unchanged. Only `mpages.tif` differs, because its pages are rescaled.
- **Windows:** the source builds for x64 and x86 with zig's clang and OpenMP, with no new warnings. MSVC is untested.

## Where the per-core time goes

Each pass timed on its own at 1 thread, before the order change (median of 3 interleaved runs, ms). The kernels are the same
now; only which one runs first changed.

| Pass | Case | main | qpv | speedup |
|---|---|---:|---:|---:|
| vertical, row-blocked kernel | 24-bit 4000x3000 -> 4000x1200, BICUBIC | 166.4 | 58.5 | 2.84x |
| vertical, row-blocked kernel | 8-bit grey, same | 78.6 | 19.2 | 4.09x |
| horizontal, downscale | 8-bit grey 4000x3000 -> 1600x3000, BICUBIC / LANCZOS3 | 42.8 / 61.5 | 45.0 / 66.4 | 0.95x / 0.93x |
| horizontal, downscale | 24-bit, same, CATMULLROM | 93.2 | 98.3 | 0.95x |
| horizontal, upscale | 8-bit grey 1600x3000 -> 4000x3000, BICUBIC / LANCZOS3 | 57.6 / 71.9 | 64.9 / 81.2 | 0.89x / 0.89x |
| horizontal, upscale | 24-bit, same, CATMULLROM / BSPLINE | 137.8 / 137.1 | 156.8 / 155.6 | 0.88x / 0.88x |
| vertical, old column kernel | 4-bit 1600x1200 -> 1600x3000, BICUBIC | 34.0 | 38.7 | 0.88x |
| vertical, old column kernel | 1-bit LANCZOS3 / palette CATMULLROM / 565 BSPLINE, same | 32.3 / 67.8 / 92.6 | 34.4 / 72.6 / 98.0 | 0.94x / 0.93x / 0.95x |

- The row-blocked vertical kernel carries the gain. The pass order now gives it the larger share of the work where that
  clearly pays.
- qpv's horizontal pass is still 5-12% slower per core than main's, and its copy of the column kernel 5-12% slower.
- Ruled out as causes of that, each by a rebuilt qpv that returns the same pixels:
  - OpenMP;
  - the ~4 MB bands;
  - the rounding store;
  - the `unsigned`/`INT64` mix in `getWeight(x, i)`: a hoisted weights pointer changed nothing and made the 8-bit horizontal
    kernel 20% slower.

  What is left is the code GCC generates. `perf` is blocked on this machine (`perf_event_paranoid` = 4).
- At several threads a large upscale has a floor. `FreeImage_AllocateT` zeroes the result on one thread
  (BitmapAccess.cpp:402), and allocating a 4000x3000 RGBAF or COMPLEX image alone takes 89 ms.

## Method

- **Builds.** Both use GCC 15.2 at `-O3`, with no `-march`.
  - main: built by the `.claude/scratch/regress` rig, stock flags plus the GCC 15 workarounds, without OpenMP.
  - qpv before the change: `git archive 3e3323b`, stock `Makefile.gnu` (`-O3 -fopenmp`).
  - qpv now: the same tree with the new `Resize.cpp`.
- **Cases.**
  - 19 formats: FIT_BITMAP 1-bit b/w, 4-bit grey, 8-bit grey, 8-bit colour palette, 16-bit 565 and 555, 24-bit and 32-bit
    RGBA, plus FIT_UINT16, INT16, UINT32, INT32, FLOAT, DOUBLE, COMPLEX, RGB16, RGBA16, RGBF and RGBAF.
  - All 7 filters.
  - Downscale 4000x3000 -> 1600x1200 (0.4x) and upscale 1600x1200 -> 4000x3000 (2.5x).
  - Content is photo-like: smooth shading, hard-edged blocks and 5% noise.
- **Timing.**
  - One process per case: 1 warm-up call, then at least 5 calls and at least 0.5 s.
  - Each call is the wall time of `FreeImage_Rescale`, allocating the result included. A run's value is the median of its
    calls.
- **Interleaving.** For each case main, qpv before and qpv now, each qpv at 1 and at 6 threads, ran back to back in a
  rotating order. This was repeated for 3 rounds, and the reported value is the median of the 3 run medians. The lopsided
  table is a separate run with the same method.
- **Noise.**
  - This job runs at nice 5 and cannot lower it. Another user's process (nice 0) held a core for much of the session,
    which is why multi-threaded runs use 6 threads.
  - Cases that run the same code in both builds measure 1.00x at 1 thread, and scatter up to ±14% at 6 threads.
- **Machine.** i7-7700T (4 cores / 8 threads, 2.9 GHz, 3.8 GHz turbo), `powersave` governor with turbo on, 15 GB RAM,
  Linux 7.0.
- **MSVC.** The threading picture carries over, since main has no OpenMP there either and qpv's Release builds enable it.
  The per-core kernel differences come from the compiler and may not.
- **Reproduce.** The rig and the raw data are in `.claude/scratch/rescale-bench/` on the benchmark machine, not in the repo.

## Headline numbers

Geometric mean over the cases main supports (BOX..LANCZOS3, every format main can resize):

| | qpv 6 threads vs main | qpv 1 thread vs main | order change, 6 threads | order change, 1 thread |
|---|---:|---:|---:|---:|
| Downscale (84 cases) | 6.50x (was 5.82x) | 1.91x (was 1.62x) | 1.12x | 1.18x |
| Upscale (84 cases) | 4.36x (was 3.51x) | 1.77x (was 1.31x) | 1.24x | 1.34x |
| Both directions (168 cases) | 5.32x (was 4.52x) | 1.83x (was 1.46x) | 1.18x | 1.26x |

Order change over all 228 filtered cases, including the formats main cannot resize: 1.16x at 6 threads, 1.24x at 1 thread.

- qpv 6 threads vs main: 2.70x (rgbaf up BOX) to 14.47x (float down BOX); 0 of 168 cases more than 5% slower than main.
- qpv 1 thread vs main: 1.13x (1bit down BSPLINE) to 5.75x (float down BOX); 0 of 168 cases more than 5% slower than main.
- Order change at 6 threads: 0.86x (4bit down CATMULLROM) to 1.95x (4bit up BOX); 12 of 228 cases more than 5% slower than before.
- Order change at 1 thread: 0.96x (4bit down BILINEAR) to 2.02x (4bit up BOX); 0 of 228 cases more than 5% slower than before.
- Pixels: 55 of 168 comparable cases identical to main, 113 differ (the ones whose pass order changed).

## Per format (geometric mean over the 6 filters)

| Format | Down: main ms | Down: 6T vs main | Down: 1T vs main | Down: order change 6T / 1T | Up: main ms | Up: 6T vs main | Up: 1T vs main | Up: order change 6T / 1T |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| FIT_BITMAP 1-bit b/w | 84.1 | 3.95x | 1.17x | 0.99x / 1.00x | 77.6 | 4.77x | 1.39x | 1.38x / 1.38x |
| FIT_BITMAP 4-bit grey | 81.7 | 3.93x | 1.18x | 0.91x / 1.00x | 84.5 | 5.26x | 1.53x | 1.57x / 1.70x |
| FIT_BITMAP 8-bit grey | 62.1 | 6.40x | 1.93x | 1.40x / 1.44x | 84.1 | 6.23x | 1.85x | 1.36x / 1.49x |
| FIT_BITMAP 8-bit colour palette | 150 | 5.01x | 1.31x | 0.99x / 1.01x | 193 | 3.88x | 1.40x | 1.34x / 1.48x |
| FIT_BITMAP 16-bit 565 | 151 | 4.89x | 1.29x | 1.01x / 1.00x | 214 | 4.08x | 1.45x | 1.37x / 1.50x |
| FIT_BITMAP 16-bit 555 | 154 | 4.86x | 1.30x | 0.97x / 1.01x | 216 | 4.00x | 1.46x | 1.34x / 1.51x |
| FIT_BITMAP 24-bit RGB | 140 | 5.92x | 1.72x | 1.20x / 1.27x | 196 | 4.01x | 1.49x | 1.22x / 1.28x |
| FIT_BITMAP 32-bit RGBA | 188 | 6.00x | 1.79x | 1.17x / 1.25x | 278 | 4.26x | 1.64x | 1.17x / 1.24x |
| FIT_UINT16 | 68.7 | 7.95x | 2.36x | 1.33x / 1.45x | 98.9 | 7.49x | 2.43x | 1.35x / 1.49x |
| FIT_INT16 | n/a | — | — | 1.19x / 1.30x | n/a | — | — | 1.14x / 1.27x |
| FIT_UINT32 | n/a | — | — | 1.19x / 1.25x | n/a | — | — | 1.10x / 1.21x |
| FIT_INT32 | n/a | — | — | 1.14x / 1.33x | n/a | — | — | 1.09x / 1.22x |
| FIT_FLOAT | 112 | 13.06x | 4.40x | 1.18x / 1.33x | 175 | 5.09x | 3.16x | 1.09x / 1.24x |
| FIT_DOUBLE | n/a | — | — | 1.08x / 1.27x | n/a | — | — | 1.02x / 1.14x |
| FIT_COMPLEX | n/a | — | — | 0.99x / 1.00x | n/a | — | — | 1.00x / 1.00x |
| FIT_RGB16 | 141 | 6.36x | 1.88x | 1.23x / 1.28x | 218 | 3.30x | 1.58x | 1.11x / 1.24x |
| FIT_RGBA16 | 270 | 8.69x | 2.42x | 1.34x / 1.41x | 336 | 3.72x | 1.77x | 1.18x / 1.30x |
| FIT_RGBF | 201 | 9.56x | 3.05x | 1.11x / 1.21x | 317 | 3.36x | 2.12x | 1.06x / 1.12x |
| FIT_RGBAF | 252 | 10.47x | 3.24x | 0.98x / 1.01x | 422 | 3.37x | 2.23x | 1.00x / 1.00x |

## Speedup matrix, qpv 6 threads vs main

**Downscale 4000x3000 -> 1600x1200 (0.4x)**

| Format | BOX | BICUBIC | BILINEAR | BSPLINE | CATMULLROM | LANCZOS3 |
|---|---:|---:|---:|---:|---:|---:|
| 1bit | 4.25x | 4.29x | 4.13x | 3.95x | 3.45x | 3.73x |
| 4bit | 4.55x | 3.57x | 3.88x | 3.92x | 3.69x | 4.01x |
| 8grey | 7.33x | 6.09x | 6.42x | 6.06x | 6.32x | 6.25x |
| 8pal | 5.96x | 4.99x | 5.46x | 4.74x | 4.66x | 4.41x |
| 16-565 | 5.29x | 4.80x | 5.01x | 5.00x | 4.78x | 4.48x |
| 16-555 | 5.31x | 5.18x | 4.62x | 4.83x | 4.97x | 4.33x |
| 24rgb | 7.25x | 5.80x | 6.75x | 5.49x | 5.58x | 4.97x |
| 32rgba | 7.14x | 5.60x | 7.19x | 5.53x | 5.82x | 5.04x |
| uint16 | 9.31x | 7.94x | 9.62x | 7.62x | 7.39x | 6.31x |
| int16 | n/a | n/a | n/a | n/a | n/a | n/a |
| uint32 | n/a | n/a | n/a | n/a | n/a | n/a |
| int32 | n/a | n/a | n/a | n/a | n/a | n/a |
| float | 14.47x | 12.48x | 12.92x | 12.57x | 14.12x | 11.98x |
| double | n/a | n/a | n/a | n/a | n/a | n/a |
| complex | n/a | n/a | n/a | n/a | n/a | n/a |
| rgb16 | 7.98x | 5.71x | 6.85x | 6.17x | 6.06x | 5.71x |
| rgba16 | 9.35x | 8.92x | 7.69x | 9.12x | 9.34x | 7.87x |
| rgbf | 11.06x | 10.40x | 9.07x | 9.64x | 9.82x | 7.76x |
| rgbaf | 11.21x | 10.18x | 12.30x | 10.10x | 10.53x | 8.81x |

**Upscale 1600x1200 -> 4000x3000 (2.5x)**

| Format | BOX | BICUBIC | BILINEAR | BSPLINE | CATMULLROM | LANCZOS3 |
|---|---:|---:|---:|---:|---:|---:|
| 1bit | 5.93x | 4.73x | 5.04x | 4.76x | 4.24x | 4.13x |
| 4bit | 6.16x | 4.96x | 5.95x | 5.37x | 4.86x | 4.47x |
| 8grey | 7.50x | 6.17x | 7.16x | 6.12x | 5.08x | 5.65x |
| 8pal | 3.75x | 3.99x | 3.78x | 3.89x | 3.96x | 3.92x |
| 16-565 | 3.86x | 4.21x | 3.94x | 3.99x | 4.11x | 4.37x |
| 16-555 | 3.75x | 4.08x | 3.83x | 4.01x | 4.39x | 3.95x |
| 24rgb | 3.98x | 3.95x | 4.08x | 4.01x | 4.01x | 4.04x |
| 32rgba | 4.12x | 4.20x | 4.22x | 4.25x | 4.28x | 4.50x |
| uint16 | 8.32x | 7.18x | 7.29x | 7.85x | 8.33x | 6.19x |
| int16 | n/a | n/a | n/a | n/a | n/a | n/a |
| uint32 | n/a | n/a | n/a | n/a | n/a | n/a |
| int32 | n/a | n/a | n/a | n/a | n/a | n/a |
| float | 3.99x | 5.47x | 4.74x | 5.51x | 5.34x | 5.71x |
| double | n/a | n/a | n/a | n/a | n/a | n/a |
| complex | n/a | n/a | n/a | n/a | n/a | n/a |
| rgb16 | 3.20x | 3.27x | 3.27x | 3.34x | 3.31x | 3.43x |
| rgba16 | 3.58x | 3.46x | 3.58x | 3.94x | 3.83x | 3.95x |
| rgbf | 2.77x | 3.68x | 3.03x | 3.57x | 3.48x | 3.75x |
| rgbaf | 2.70x | 3.57x | 2.86x | 3.72x | 3.69x | 3.86x |

## Speedup matrix, qpv 1 thread vs main

**Downscale 4000x3000 -> 1600x1200 (0.4x)**

| Format | BOX | BICUBIC | BILINEAR | BSPLINE | CATMULLROM | LANCZOS3 |
|---|---:|---:|---:|---:|---:|---:|
| 1bit | 1.20x | 1.15x | 1.22x | 1.13x | 1.17x | 1.14x |
| 4bit | 1.26x | 1.18x | 1.14x | 1.18x | 1.16x | 1.15x |
| 8grey | 2.11x | 1.86x | 2.00x | 1.87x | 1.90x | 1.85x |
| 8pal | 1.46x | 1.28x | 1.35x | 1.30x | 1.27x | 1.22x |
| 16-565 | 1.37x | 1.27x | 1.33x | 1.27x | 1.28x | 1.23x |
| 16-555 | 1.39x | 1.29x | 1.34x | 1.26x | 1.30x | 1.21x |
| 24rgb | 1.99x | 1.70x | 1.78x | 1.64x | 1.65x | 1.57x |
| 32rgba | 2.22x | 1.68x | 1.93x | 1.68x | 1.69x | 1.59x |
| uint16 | 3.00x | 2.17x | 2.71x | 2.17x | 2.15x | 2.10x |
| int16 | n/a | n/a | n/a | n/a | n/a | n/a |
| uint32 | n/a | n/a | n/a | n/a | n/a | n/a |
| int32 | n/a | n/a | n/a | n/a | n/a | n/a |
| float | 5.75x | 4.18x | 4.63x | 4.17x | 4.11x | 3.80x |
| double | n/a | n/a | n/a | n/a | n/a | n/a |
| complex | n/a | n/a | n/a | n/a | n/a | n/a |
| rgb16 | 2.31x | 1.79x | 2.00x | 1.80x | 1.78x | 1.69x |
| rgba16 | 2.80x | 2.48x | 2.34x | 2.47x | 2.47x | 2.04x |
| rgbf | 4.49x | 2.88x | 3.18x | 2.72x | 2.77x | 2.60x |
| rgbaf | 4.40x | 3.01x | 3.49x | 2.96x | 2.96x | 2.85x |

**Upscale 1600x1200 -> 4000x3000 (2.5x)**

| Format | BOX | BICUBIC | BILINEAR | BSPLINE | CATMULLROM | LANCZOS3 |
|---|---:|---:|---:|---:|---:|---:|
| 1bit | 1.68x | 1.35x | 1.45x | 1.34x | 1.31x | 1.24x |
| 4bit | 1.89x | 1.45x | 1.57x | 1.48x | 1.46x | 1.36x |
| 8grey | 2.13x | 1.76x | 1.96x | 1.77x | 1.79x | 1.72x |
| 8pal | 1.51x | 1.38x | 1.44x | 1.38x | 1.37x | 1.33x |
| 16-565 | 1.52x | 1.42x | 1.47x | 1.43x | 1.46x | 1.42x |
| 16-555 | 1.53x | 1.40x | 1.49x | 1.43x | 1.47x | 1.43x |
| 24rgb | 1.65x | 1.44x | 1.56x | 1.44x | 1.43x | 1.43x |
| 32rgba | 1.81x | 1.58x | 1.66x | 1.62x | 1.61x | 1.57x |
| uint16 | 2.69x | 2.49x | 2.49x | 2.30x | 2.37x | 2.24x |
| int16 | n/a | n/a | n/a | n/a | n/a | n/a |
| uint32 | n/a | n/a | n/a | n/a | n/a | n/a |
| int32 | n/a | n/a | n/a | n/a | n/a | n/a |
| float | 3.15x | 3.16x | 3.21x | 3.12x | 3.10x | 3.21x |
| double | n/a | n/a | n/a | n/a | n/a | n/a |
| complex | n/a | n/a | n/a | n/a | n/a | n/a |
| rgb16 | 1.65x | 1.57x | 1.65x | 1.53x | 1.58x | 1.51x |
| rgba16 | 1.95x | 1.72x | 1.80x | 1.73x | 1.74x | 1.68x |
| rgbf | 2.12x | 2.09x | 2.24x | 2.11x | 2.09x | 2.07x |
| rgbaf | 2.16x | 2.28x | 2.12x | 2.29x | 2.36x | 2.20x |

## qpv-only cases

FILTER_NEAREST does not exist on main; it keeps the source pixel format and has one pass, so the order change does not touch it. Main cannot resize FIT_INT16/UINT32/INT32/DOUBLE/COMPLEX (it returns an all-zero image).

| Format | Dir | NEAREST 6T ms | NEAREST 1T ms | vs main BOX (6T) |
|---|---|---:|---:|---:|
| 1bit | down | 0.86 | 2.39 | 46.15x |
| 4bit | down | 1.38 | 4.05 | 30.07x |
| 8grey | down | 0.54 | 1.59 | 64.07x |
| 8pal | down | 0.54 | 1.60 | 161.70x |
| 16-565 | down | 0.64 | 1.83 | 145.73x |
| 16-555 | down | 0.64 | 1.82 | 146.27x |
| 24rgb | down | 1.13 | 2.40 | 72.90x |
| 32rgba | down | 1.45 | 2.38 | 80.46x |
| uint16 | down | 0.63 | 1.82 | 71.57x |
| int16 | down | 0.64 | 1.83 | n/a |
| uint32 | down | 1.48 | 2.36 | n/a |
| int32 | down | 1.47 | 2.35 | n/a |
| float | down | 1.46 | 2.36 | 47.00x |
| double | down | 2.94 | 3.78 | n/a |
| complex | down | 5.88 | 6.81 | n/a |
| rgb16 | down | 2.24 | 3.35 | 37.56x |
| rgba16 | down | 2.93 | 3.77 | 50.92x |
| rgbf | down | 4.50 | 5.34 | 27.24x |
| rgbaf | down | 5.88 | 6.63 | 28.10x |
| 1bit | up | 2.19 | 6.04 | 25.08x |
| 4bit | up | 3.63 | 10.5 | 16.83x |
| 8grey | up | 1.77 | 4.30 | 34.50x |
| 8pal | up | 1.77 | 4.28 | 79.91x |
| 16-565 | up | 2.67 | 5.19 | 57.83x |
| 16-555 | up | 2.69 | 5.18 | 57.65x |
| 24rgb | up | 20.1 | 21.8 | 7.20x |
| 32rgba | up | 26.5 | 27.4 | 7.70x |
| uint16 | up | 2.67 | 5.21 | 25.99x |
| int16 | up | 2.74 | 5.23 | n/a |
| uint32 | up | 27.3 | 28.1 | n/a |
| int32 | up | 27.9 | 29.0 | n/a |
| float | up | 28.1 | 28.5 | 4.53x |
| double | up | 56.2 | 54.3 | n/a |
| complex | up | 105 | 111 | n/a |
| rgb16 | up | 40.6 | 43.2 | 4.08x |
| rgba16 | up | 53.7 | 53.3 | 4.68x |
| rgbf | up | 80.1 | 79.3 | 2.93x |
| rgbaf | up | 104 | 110 | 3.00x |

| Format | Dir | BOX | BICUBIC | BILINEAR | BSPLINE | CATMULLROM | LANCZOS3 |
|---|---|---:|---:|---:|---:|---:|---:|
| int16 | down | 7.07 / 24.5 | 14.2 / 44.8 | 9.53 / 31.6 | 12.8 / 45.9 | 13.9 / 45.1 | 17.7 / 59.5 |
| int16 | up | 15.2 / 49.8 | 20.2 / 68.9 | 16.7 / 56.3 | 20.0 / 69.7 | 22.0 / 71.3 | 27.1 / 85.4 |
| uint32 | down | 6.78 / 22.4 | 15.4 / 48.3 | 9.04 / 31.1 | 15.3 / 48.4 | 13.7 / 48.5 | 19.6 / 66.3 |
| uint32 | up | 35.0 / 62.4 | 41.7 / 89.6 | 39.9 / 72.8 | 43.4 / 88.4 | 40.7 / 89.2 | 49.5 / 107 |
| int32 | down | 6.35 / 22.7 | 11.6 / 41.9 | 9.88 / 29.3 | 14.2 / 42.0 | 12.9 / 41.8 | 17.7 / 53.9 |
| int32 | up | 36.6 / 67.2 | 41.2 / 86.8 | 37.8 / 72.8 | 43.2 / 86.2 | 41.6 / 85.7 | 47.2 / 104 |
| double | down | 6.75 / 15.1 | 10.9 / 31.6 | 9.13 / 20.8 | 10.3 / 31.6 | 10.9 / 31.6 | 15.0 / 42.6 |
| double | up | 59.4 / 68.0 | 61.8 / 87.1 | 59.2 / 73.6 | 59.9 / 86.1 | 62.4 / 87.6 | 66.1 / 98.8 |
| complex | down | 15.1 / 26.8 | 15.9 / 53.9 | 14.6 / 35.2 | 16.7 / 54.3 | 16.0 / 55.5 | 25.7 / 75.4 |
| complex | up | 115 / 127 | 111 / 159 | 117 / 142 | 111 / 158 | 113 / 154 | 123 / 179 |

(cells: 6 threads / 1 thread, ms)

## Every case

Median of the per-run medians over the rounds; ms per FreeImage_Rescale call, allocation of the result included. old = qpv before the pass-order change, new = after.

**Downscale 4000x3000 -> 1600x1200 (0.4x)**

| Format | Filter | main | old 1T | new 1T | new 1T vs main | old 6T | new 6T | new 6T vs main | Output | Pixels vs main |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| 1bit | NEAREST | — | 2.36 | 2.39 | — | 0.86 | 0.86 | — | 1-bit | main: n/a |
| 1bit | BOX | 39.8 | 32.8 | 33.2 | 1.20x | 9.12 | 9.37 | 4.25x | 8-bit | identical |
| 1bit | BICUBIC | 101 | 86.2 | 87.5 | 1.15x | 25.6 | 23.5 | 4.29x | 8-bit | identical |
| 1bit | BILINEAR | 61.2 | 51.0 | 50.1 | 1.22x | 14.7 | 14.8 | 4.13x | 8-bit | identical |
| 1bit | BSPLINE | 101 | 87.4 | 89.0 | 1.13x | 25.6 | 25.5 | 3.95x | 8-bit | identical |
| 1bit | CATMULLROM | 100 | 87.2 | 85.9 | 1.17x | 25.6 | 29.0 | 3.45x | 8-bit | identical |
| 1bit | LANCZOS3 | 143 | 126 | 126 | 1.14x | 38.1 | 38.4 | 3.73x | 8-bit | identical |
| 4bit | NEAREST | — | 4.06 | 4.05 | — | 1.37 | 1.38 | — | 4-bit | main: n/a |
| 4bit | BOX | 41.4 | 32.8 | 32.9 | 1.26x | 9.10 | 9.11 | 4.55x | 8-bit | identical |
| 4bit | BICUBIC | 97.6 | 83.1 | 82.4 | 1.18x | 24.0 | 27.3 | 3.57x | 8-bit | identical |
| 4bit | BILINEAR | 59.4 | 50.0 | 52.2 | 1.14x | 13.3 | 15.3 | 3.88x | 8-bit | identical |
| 4bit | BSPLINE | 96.4 | 83.5 | 81.9 | 1.18x | 22.1 | 24.6 | 3.92x | 8-bit | identical |
| 4bit | CATMULLROM | 95.6 | 82.6 | 82.2 | 1.16x | 22.3 | 25.9 | 3.69x | 8-bit | identical |
| 4bit | LANCZOS3 | 134 | 118 | 116 | 1.15x | 31.8 | 33.4 | 4.01x | 8-bit | identical |
| 8grey | NEAREST | — | 1.54 | 1.59 | — | 0.54 | 0.54 | — | 8-bit | main: n/a |
| 8grey | BOX | 34.6 | 24.7 | 16.4 | 2.11x | 6.79 | 4.72 | 7.33x | 8-bit | differs |
| 8grey | BICUBIC | 70.5 | 53.3 | 38.0 | 1.86x | 15.6 | 11.6 | 6.09x | 8-bit | differs |
| 8grey | BILINEAR | 47.2 | 34.6 | 23.6 | 2.00x | 9.21 | 7.34 | 6.42x | 8-bit | differs |
| 8grey | BSPLINE | 70.0 | 53.1 | 37.4 | 1.87x | 14.7 | 11.5 | 6.06x | 8-bit | differs |
| 8grey | CATMULLROM | 71.0 | 53.2 | 37.3 | 1.90x | 18.2 | 11.2 | 6.32x | 8-bit | differs |
| 8grey | LANCZOS3 | 100 | 78.4 | 54.2 | 1.85x | 23.7 | 16.0 | 6.25x | 8-bit | differs |
| 8pal | NEAREST | — | 1.59 | 1.60 | — | 0.54 | 0.54 | — | 8-bit | main: n/a |
| 8pal | BOX | 86.8 | 59.3 | 59.4 | 1.46x | 14.3 | 14.6 | 5.96x | 24-bit | identical |
| 8pal | BICUBIC | 171 | 134 | 134 | 1.28x | 34.1 | 34.3 | 4.99x | 24-bit | identical |
| 8pal | BILINEAR | 114 | 86.8 | 84.1 | 1.35x | 21.2 | 20.8 | 5.46x | 24-bit | identical |
| 8pal | BSPLINE | 173 | 132 | 133 | 1.30x | 33.9 | 36.5 | 4.74x | 24-bit | identical |
| 8pal | CATMULLROM | 172 | 135 | 135 | 1.27x | 36.6 | 36.8 | 4.66x | 24-bit | identical |
| 8pal | LANCZOS3 | 229 | 191 | 188 | 1.22x | 52.2 | 52.0 | 4.41x | 24-bit | identical |
| 16-565 | NEAREST | — | 1.84 | 1.83 | — | 0.64 | 0.64 | — | 16-bit | main: n/a |
| 16-565 | BOX | 92.7 | 67.9 | 67.7 | 1.37x | 17.8 | 17.5 | 5.29x | 24-bit | identical |
| 16-565 | BICUBIC | 167 | 131 | 131 | 1.27x | 35.1 | 34.7 | 4.80x | 24-bit | identical |
| 16-565 | BILINEAR | 125 | 95.1 | 93.9 | 1.33x | 24.9 | 24.9 | 5.01x | 24-bit | identical |
| 16-565 | BSPLINE | 166 | 131 | 131 | 1.27x | 35.1 | 33.1 | 5.00x | 24-bit | identical |
| 16-565 | CATMULLROM | 166 | 130 | 130 | 1.28x | 34.7 | 34.7 | 4.78x | 24-bit | identical |
| 16-565 | LANCZOS3 | 223 | 181 | 182 | 1.23x | 47.5 | 49.8 | 4.48x | 24-bit | identical |
| 16-555 | NEAREST | — | 1.77 | 1.82 | — | 0.63 | 0.64 | — | 16-bit | main: n/a |
| 16-555 | BOX | 93.2 | 67.5 | 67.3 | 1.39x | 17.0 | 17.6 | 5.31x | 24-bit | identical |
| 16-555 | BICUBIC | 171 | 131 | 132 | 1.29x | 32.7 | 33.0 | 5.18x | 24-bit | identical |
| 16-555 | BILINEAR | 126 | 98.7 | 93.7 | 1.34x | 25.1 | 27.3 | 4.62x | 24-bit | identical |
| 16-555 | BSPLINE | 168 | 133 | 133 | 1.26x | 34.7 | 34.8 | 4.83x | 24-bit | identical |
| 16-555 | CATMULLROM | 173 | 132 | 133 | 1.30x | 32.8 | 34.7 | 4.97x | 24-bit | identical |
| 16-555 | LANCZOS3 | 226 | 185 | 186 | 1.21x | 51.8 | 52.1 | 4.33x | 24-bit | identical |
| 24rgb | NEAREST | — | 2.39 | 2.40 | — | 1.09 | 1.13 | — | 24-bit | main: n/a |
| 24rgb | BOX | 82.2 | 54.3 | 41.3 | 1.99x | 16.4 | 11.3 | 7.25x | 24-bit | differs |
| 24rgb | BICUBIC | 162 | 120 | 95.2 | 1.70x | 31.3 | 28.0 | 5.80x | 24-bit | differs |
| 24rgb | BILINEAR | 107 | 76.8 | 59.9 | 1.78x | 21.3 | 15.8 | 6.75x | 24-bit | differs |
| 24rgb | BSPLINE | 159 | 120 | 96.9 | 1.64x | 31.4 | 28.9 | 5.49x | 24-bit | differs |
| 24rgb | CATMULLROM | 158 | 124 | 95.7 | 1.65x | 34.0 | 28.3 | 5.58x | 24-bit | differs |
| 24rgb | LANCZOS3 | 214 | 166 | 136 | 1.57x | 45.1 | 43.0 | 4.97x | 24-bit | differs |
| 32rgba | NEAREST | — | 2.38 | 2.38 | — | 1.40 | 1.45 | — | 32-bit | main: n/a |
| 32rgba | BOX | 116 | 68.1 | 52.4 | 2.22x | 18.3 | 16.3 | 7.14x | 32-bit | differs |
| 32rgba | BICUBIC | 211 | 155 | 125 | 1.68x | 44.8 | 37.7 | 5.60x | 32-bit | differs |
| 32rgba | BILINEAR | 148 | 95.6 | 76.6 | 1.93x | 27.5 | 20.5 | 7.19x | 32-bit | differs |
| 32rgba | BSPLINE | 211 | 157 | 126 | 1.68x | 42.9 | 38.2 | 5.53x | 32-bit | differs |
| 32rgba | CATMULLROM | 210 | 155 | 124 | 1.69x | 43.5 | 36.1 | 5.82x | 32-bit | differs |
| 32rgba | LANCZOS3 | 278 | 213 | 175 | 1.59x | 59.4 | 55.1 | 5.04x | 32-bit | differs |
| uint16 | NEAREST | — | 1.83 | 1.82 | — | 0.64 | 0.63 | — | UINT16 | main: n/a |
| uint16 | BOX | 45.1 | 22.7 | 15.0 | 3.00x | 6.28 | 4.84 | 9.31x | UINT16 | differs |
| uint16 | BICUBIC | 74.1 | 48.6 | 34.1 | 2.17x | 12.6 | 9.34 | 7.94x | UINT16 | differs |
| uint16 | BILINEAR | 57.9 | 31.1 | 21.3 | 2.71x | 8.65 | 6.02 | 9.62x | UINT16 | differs |
| uint16 | BSPLINE | 73.6 | 48.4 | 33.9 | 2.17x | 12.7 | 9.66 | 7.62x | UINT16 | differs |
| uint16 | CATMULLROM | 72.7 | 48.3 | 33.7 | 2.15x | 12.8 | 9.84 | 7.39x | UINT16 | differs |
| uint16 | LANCZOS3 | 102 | 70.5 | 48.5 | 2.10x | 20.8 | 16.2 | 6.31x | UINT16 | differs |
| int16 | NEAREST | — | 1.82 | 1.83 | — | 0.64 | 0.64 | — | INT16 | main: n/a |
| int16 | BOX | — | 30.9 | 24.5 | — | 8.49 | 7.07 | — | INT16 | main: blank |
| int16 | BICUBIC | — | 59.6 | 44.8 | — | 17.3 | 14.2 | — | INT16 | main: blank |
| int16 | BILINEAR | — | 40.7 | 31.6 | — | 10.5 | 9.53 | — | INT16 | main: blank |
| int16 | BSPLINE | — | 59.7 | 45.9 | — | 15.7 | 12.8 | — | INT16 | main: blank |
| int16 | CATMULLROM | — | 58.5 | 45.1 | — | 15.7 | 13.9 | — | INT16 | main: blank |
| int16 | LANCZOS3 | — | 78.0 | 59.5 | — | 22.2 | 17.7 | — | INT16 | main: blank |
| uint32 | NEAREST | — | 2.35 | 2.36 | — | 1.49 | 1.48 | — | UINT32 | main: n/a |
| uint32 | BOX | — | 28.9 | 22.4 | — | 8.66 | 6.78 | — | UINT32 | main: blank |
| uint32 | BICUBIC | — | 59.3 | 48.3 | — | 17.0 | 15.4 | — | UINT32 | main: blank |
| uint32 | BILINEAR | — | 40.6 | 31.1 | — | 11.0 | 9.04 | — | UINT32 | main: blank |
| uint32 | BSPLINE | — | 60.4 | 48.4 | — | 16.3 | 15.3 | — | UINT32 | main: blank |
| uint32 | CATMULLROM | — | 59.1 | 48.5 | — | 18.6 | 13.7 | — | UINT32 | main: blank |
| uint32 | LANCZOS3 | — | 82.3 | 66.3 | — | 22.8 | 19.6 | — | UINT32 | main: blank |
| int32 | NEAREST | — | 2.39 | 2.35 | — | 1.50 | 1.47 | — | INT32 | main: n/a |
| int32 | BOX | — | 28.7 | 22.7 | — | 7.96 | 6.35 | — | INT32 | main: blank |
| int32 | BICUBIC | — | 56.3 | 41.9 | — | 14.7 | 11.6 | — | INT32 | main: blank |
| int32 | BILINEAR | — | 39.1 | 29.3 | — | 10.7 | 9.88 | — | INT32 | main: blank |
| int32 | BSPLINE | — | 55.1 | 42.0 | — | 15.2 | 14.2 | — | INT32 | main: blank |
| int32 | CATMULLROM | — | 56.3 | 41.8 | — | 14.0 | 12.9 | — | INT32 | main: blank |
| int32 | LANCZOS3 | — | 73.3 | 53.9 | — | 20.0 | 17.7 | — | INT32 | main: blank |
| float | NEAREST | — | 2.36 | 2.36 | — | 1.50 | 1.46 | — | FLOAT | main: n/a |
| float | BOX | 68.8 | 16.0 | 12.0 | 5.75x | 5.15 | 4.75 | 14.47x | FLOAT | differs |
| float | BICUBIC | 126 | 39.2 | 30.2 | 4.18x | 11.4 | 10.1 | 12.48x | FLOAT | differs |
| float | BILINEAR | 86.6 | 26.8 | 18.7 | 4.63x | 8.53 | 6.71 | 12.92x | FLOAT | differs |
| float | BSPLINE | 127 | 39.8 | 30.4 | 4.17x | 11.5 | 10.1 | 12.57x | FLOAT | differs |
| float | CATMULLROM | 127 | 39.7 | 31.0 | 4.11x | 11.5 | 9.00 | 14.12x | FLOAT | differs |
| float | LANCZOS3 | 164 | 57.4 | 43.1 | 3.80x | 16.1 | 13.7 | 11.98x | FLOAT | differs |
| double | NEAREST | — | 3.73 | 3.78 | — | 3.04 | 2.94 | — | DOUBLE | main: n/a |
| double | BOX | — | 21.0 | 15.1 | — | 7.84 | 6.75 | — | DOUBLE | main: blank |
| double | BICUBIC | — | 38.7 | 31.6 | — | 12.0 | 10.9 | — | DOUBLE | main: blank |
| double | BILINEAR | — | 27.4 | 20.8 | — | 8.07 | 9.13 | — | DOUBLE | main: blank |
| double | BSPLINE | — | 39.3 | 31.6 | — | 11.2 | 10.3 | — | DOUBLE | main: blank |
| double | CATMULLROM | — | 39.1 | 31.6 | — | 11.9 | 10.9 | — | DOUBLE | main: blank |
| double | LANCZOS3 | — | 52.8 | 42.6 | — | 17.3 | 15.0 | — | DOUBLE | main: blank |
| complex | NEAREST | — | 6.82 | 6.81 | — | 6.02 | 5.88 | — | COMPLEX | main: n/a |
| complex | BOX | — | 27.1 | 26.8 | — | 14.8 | 15.1 | — | COMPLEX | main: blank |
| complex | BICUBIC | — | 54.8 | 53.9 | — | 17.0 | 15.9 | — | COMPLEX | main: blank |
| complex | BILINEAR | — | 35.7 | 35.2 | — | 13.4 | 14.6 | — | COMPLEX | main: blank |
| complex | BSPLINE | — | 53.9 | 54.3 | — | 16.7 | 16.7 | — | COMPLEX | main: blank |
| complex | CATMULLROM | — | 54.5 | 55.5 | — | 17.1 | 16.0 | — | COMPLEX | main: blank |
| complex | LANCZOS3 | — | 75.1 | 75.4 | — | 23.5 | 25.7 | — | COMPLEX | main: blank |
| rgb16 | NEAREST | — | 3.40 | 3.35 | — | 2.30 | 2.24 | — | RGB16 | main: n/a |
| rgb16 | BOX | 84.3 | 47.5 | 36.5 | 2.31x | 13.9 | 10.6 | 7.98x | RGB16 | differs |
| rgb16 | BICUBIC | 159 | 113 | 88.6 | 1.79x | 31.5 | 27.8 | 5.71x | RGB16 | differs |
| rgb16 | BILINEAR | 109 | 69.9 | 54.7 | 2.00x | 19.0 | 16.0 | 6.85x | RGB16 | differs |
| rgb16 | BSPLINE | 159 | 113 | 88.3 | 1.80x | 33.4 | 25.7 | 6.17x | RGB16 | differs |
| rgb16 | CATMULLROM | 159 | 114 | 89.3 | 1.78x | 32.8 | 26.3 | 6.06x | RGB16 | differs |
| rgb16 | LANCZOS3 | 213 | 160 | 126 | 1.69x | 43.8 | 37.3 | 5.71x | RGB16 | differs |
| rgba16 | NEAREST | — | 3.76 | 3.77 | — | 3.05 | 2.93 | — | RGBA16 | main: n/a |
| rgba16 | BOX | 149 | 76.7 | 53.3 | 2.80x | 21.4 | 15.9 | 9.35x | RGBA16 | differs |
| rgba16 | BICUBIC | 335 | 192 | 135 | 2.48x | 47.2 | 37.6 | 8.92x | RGBA16 | differs |
| rgba16 | BILINEAR | 179 | 106 | 76.7 | 2.34x | 30.1 | 23.3 | 7.69x | RGBA16 | differs |
| rgba16 | BSPLINE | 333 | 192 | 135 | 2.47x | 47.4 | 36.5 | 9.12x | RGBA16 | differs |
| rgba16 | CATMULLROM | 335 | 194 | 136 | 2.47x | 50.5 | 35.8 | 9.34x | RGBA16 | differs |
| rgba16 | LANCZOS3 | 386 | 256 | 189 | 2.04x | 72.2 | 49.0 | 7.87x | RGBA16 | differs |
| rgbf | NEAREST | — | 5.34 | 5.34 | — | 4.41 | 4.50 | — | RGBF | main: n/a |
| rgbf | BOX | 123 | 29.6 | 27.3 | 4.49x | 11.4 | 11.1 | 11.06x | RGBF | differs |
| rgbf | BICUBIC | 236 | 101 | 82.0 | 2.88x | 26.8 | 22.7 | 10.40x | RGBF | differs |
| rgbf | BILINEAR | 155 | 61.4 | 48.8 | 3.18x | 17.7 | 17.1 | 9.07x | RGBF | differs |
| rgbf | BSPLINE | 223 | 100 | 81.7 | 2.72x | 26.5 | 23.1 | 9.64x | RGBF | differs |
| rgbf | CATMULLROM | 223 | 98.7 | 80.6 | 2.77x | 27.8 | 22.7 | 9.82x | RGBF | differs |
| rgbf | LANCZOS3 | 294 | 138 | 113 | 2.60x | 40.2 | 38.0 | 7.76x | RGBF | differs |
| rgbaf | NEAREST | — | 6.67 | 6.63 | — | 5.92 | 5.88 | — | RGBAF | main: n/a |
| rgbaf | BOX | 165 | 37.6 | 37.6 | 4.40x | 14.6 | 14.7 | 11.21x | RGBAF | identical |
| rgbaf | BICUBIC | 277 | 96.1 | 92.3 | 3.01x | 27.5 | 27.2 | 10.18x | RGBAF | identical |
| rgbaf | BILINEAR | 198 | 56.5 | 56.8 | 3.49x | 15.9 | 16.1 | 12.30x | RGBAF | identical |
| rgbaf | BSPLINE | 275 | 92.5 | 92.9 | 2.96x | 27.3 | 27.2 | 10.10x | RGBAF | identical |
| rgbaf | CATMULLROM | 276 | 93.1 | 93.2 | 2.96x | 27.4 | 26.2 | 10.53x | RGBAF | identical |
| rgbaf | LANCZOS3 | 374 | 132 | 131 | 2.85x | 37.0 | 42.4 | 8.81x | RGBAF | identical |

**Upscale 1600x1200 -> 4000x3000 (2.5x)**

| Format | Filter | main | old 1T | new 1T | new 1T vs main | old 6T | new 6T | new 6T vs main | Output | Pixels vs main |
|---|---|---:|---:|---:|---:|---:|---:|---:|---|---|
| 1bit | NEAREST | — | 6.03 | 6.04 | — | 2.19 | 2.19 | — | 1-bit | main: n/a |
| 1bit | BOX | 54.8 | 49.4 | 32.6 | 1.68x | 15.0 | 9.24 | 5.93x | 8-bit | identical |
| 1bit | BICUBIC | 84.0 | 86.7 | 62.1 | 1.35x | 24.4 | 17.8 | 4.73x | 8-bit | differs |
| 1bit | BILINEAR | 63.9 | 60.3 | 44.1 | 1.45x | 17.5 | 12.7 | 5.04x | 8-bit | differs |
| 1bit | BSPLINE | 84.9 | 84.5 | 63.2 | 1.34x | 24.3 | 17.8 | 4.76x | 8-bit | differs |
| 1bit | CATMULLROM | 83.5 | 86.3 | 63.5 | 1.31x | 24.7 | 19.7 | 4.24x | 8-bit | differs |
| 1bit | LANCZOS3 | 105 | 113 | 84.7 | 1.24x | 33.5 | 25.3 | 4.13x | 8-bit | differs |
| 4bit | NEAREST | — | 10.5 | 10.5 | — | 3.63 | 3.63 | — | 4-bit | main: n/a |
| 4bit | BOX | 61.2 | 65.3 | 32.4 | 1.89x | 19.3 | 9.92 | 6.16x | 8-bit | identical |
| 4bit | BICUBIC | 90.4 | 103 | 62.3 | 1.45x | 26.1 | 18.2 | 4.96x | 8-bit | differs |
| 4bit | BILINEAR | 69.6 | 76.7 | 44.2 | 1.57x | 22.1 | 11.7 | 5.95x | 8-bit | differs |
| 4bit | BSPLINE | 91.4 | 102 | 61.8 | 1.48x | 26.0 | 17.0 | 5.37x | 8-bit | differs |
| 4bit | CATMULLROM | 91.5 | 102 | 62.7 | 1.46x | 26.1 | 18.9 | 4.86x | 8-bit | differs |
| 4bit | LANCZOS3 | 113 | 128 | 83.5 | 1.36x | 34.8 | 25.3 | 4.47x | 8-bit | differs |
| 8grey | NEAREST | — | 4.27 | 4.30 | — | 1.76 | 1.77 | — | 8-bit | main: n/a |
| 8grey | BOX | 61.0 | 44.7 | 28.6 | 2.13x | 12.9 | 8.14 | 7.50x | 8-bit | identical |
| 8grey | BICUBIC | 89.7 | 76.2 | 51.0 | 1.76x | 19.2 | 14.5 | 6.17x | 8-bit | differs |
| 8grey | BILINEAR | 70.4 | 54.6 | 35.9 | 1.96x | 15.4 | 9.82 | 7.16x | 8-bit | differs |
| 8grey | BSPLINE | 89.7 | 75.4 | 50.8 | 1.77x | 19.2 | 14.7 | 6.12x | 8-bit | differs |
| 8grey | CATMULLROM | 91.3 | 73.8 | 51.1 | 1.79x | 19.0 | 18.0 | 5.08x | 8-bit | differs |
| 8grey | LANCZOS3 | 113 | 93.7 | 65.5 | 1.72x | 27.1 | 19.9 | 5.65x | 8-bit | differs |
| 8pal | NEAREST | — | 4.29 | 4.28 | — | 1.77 | 1.77 | — | 8-bit | main: n/a |
| 8pal | BOX | 141 | 147 | 93.7 | 1.51x | 51.7 | 37.7 | 3.75x | 24-bit | identical |
| 8pal | BICUBIC | 206 | 220 | 150 | 1.38x | 72.2 | 51.7 | 3.99x | 24-bit | differs |
| 8pal | BILINEAR | 163 | 169 | 114 | 1.44x | 55.6 | 43.1 | 3.78x | 24-bit | differs |
| 8pal | BSPLINE | 205 | 220 | 149 | 1.38x | 70.4 | 52.7 | 3.89x | 24-bit | differs |
| 8pal | CATMULLROM | 205 | 222 | 149 | 1.37x | 69.3 | 51.7 | 3.96x | 24-bit | differs |
| 8pal | LANCZOS3 | 259 | 276 | 194 | 1.33x | 86.7 | 66.1 | 3.92x | 24-bit | differs |
| 16-565 | NEAREST | — | 5.20 | 5.19 | — | 2.67 | 2.67 | — | 16-bit | main: n/a |
| 16-565 | BOX | 155 | 160 | 102 | 1.52x | 54.7 | 40.1 | 3.86x | 24-bit | identical |
| 16-565 | BICUBIC | 228 | 240 | 161 | 1.42x | 74.1 | 54.3 | 4.21x | 24-bit | differs |
| 16-565 | BILINEAR | 179 | 182 | 122 | 1.47x | 61.1 | 45.3 | 3.94x | 24-bit | differs |
| 16-565 | BSPLINE | 228 | 242 | 159 | 1.43x | 73.4 | 57.0 | 3.99x | 24-bit | differs |
| 16-565 | CATMULLROM | 231 | 240 | 159 | 1.46x | 78.2 | 56.1 | 4.11x | 24-bit | differs |
| 16-565 | LANCZOS3 | 289 | 292 | 204 | 1.42x | 95.3 | 66.2 | 4.37x | 24-bit | differs |
| 16-555 | NEAREST | — | 5.27 | 5.18 | — | 2.74 | 2.69 | — | 16-bit | main: n/a |
| 16-555 | BOX | 155 | 161 | 102 | 1.53x | 54.5 | 41.3 | 3.75x | 24-bit | identical |
| 16-555 | BICUBIC | 229 | 242 | 163 | 1.40x | 78.7 | 56.1 | 4.08x | 24-bit | differs |
| 16-555 | BILINEAR | 179 | 183 | 121 | 1.49x | 60.9 | 46.8 | 3.83x | 24-bit | differs |
| 16-555 | BSPLINE | 231 | 243 | 162 | 1.43x | 79.5 | 57.7 | 4.01x | 24-bit | differs |
| 16-555 | CATMULLROM | 238 | 250 | 162 | 1.47x | 78.6 | 54.3 | 4.39x | 24-bit | differs |
| 16-555 | LANCZOS3 | 291 | 293 | 203 | 1.43x | 90.3 | 73.5 | 3.95x | 24-bit | differs |
| 24rgb | NEAREST | — | 22.2 | 21.8 | — | 20.3 | 20.1 | — | 24-bit | main: n/a |
| 24rgb | BOX | 145 | 115 | 87.7 | 1.65x | 48.0 | 36.4 | 3.98x | 24-bit | identical |
| 24rgb | BICUBIC | 207 | 185 | 144 | 1.44x | 64.5 | 52.5 | 3.95x | 24-bit | differs |
| 24rgb | BILINEAR | 167 | 136 | 107 | 1.56x | 50.2 | 41.0 | 4.08x | 24-bit | differs |
| 24rgb | BSPLINE | 208 | 185 | 145 | 1.44x | 61.6 | 52.0 | 4.01x | 24-bit | differs |
| 24rgb | CATMULLROM | 210 | 184 | 147 | 1.43x | 62.6 | 52.4 | 4.01x | 24-bit | differs |
| 24rgb | LANCZOS3 | 260 | 230 | 182 | 1.43x | 75.8 | 64.3 | 4.04x | 24-bit | differs |
| 32rgba | NEAREST | — | 27.5 | 27.4 | — | 27.0 | 26.5 | — | 32-bit | main: n/a |
| 32rgba | BOX | 204 | 143 | 113 | 1.81x | 57.0 | 49.6 | 4.12x | 32-bit | identical |
| 32rgba | BICUBIC | 295 | 227 | 186 | 1.58x | 80.6 | 70.2 | 4.20x | 32-bit | differs |
| 32rgba | BILINEAR | 230 | 173 | 139 | 1.66x | 64.0 | 54.5 | 4.22x | 32-bit | differs |
| 32rgba | BSPLINE | 301 | 231 | 186 | 1.62x | 82.5 | 70.8 | 4.25x | 32-bit | differs |
| 32rgba | CATMULLROM | 299 | 230 | 186 | 1.61x | 81.9 | 69.8 | 4.28x | 32-bit | differs |
| 32rgba | LANCZOS3 | 371 | 288 | 236 | 1.57x | 102 | 82.4 | 4.50x | 32-bit | differs |
| uint16 | NEAREST | — | 5.25 | 5.21 | — | 2.68 | 2.67 | — | UINT16 | main: n/a |
| uint16 | BOX | 69.3 | 39.3 | 25.8 | 2.69x | 11.7 | 8.33 | 8.32x | UINT16 | identical |
| uint16 | BICUBIC | 111 | 67.7 | 44.7 | 2.49x | 21.6 | 15.5 | 7.18x | UINT16 | differs |
| uint16 | BILINEAR | 80.4 | 48.7 | 32.2 | 2.49x | 14.3 | 11.0 | 7.29x | UINT16 | differs |
| uint16 | BSPLINE | 107 | 66.5 | 46.2 | 2.30x | 19.2 | 13.6 | 7.85x | UINT16 | differs |
| uint16 | CATMULLROM | 107 | 67.8 | 45.4 | 2.37x | 19.2 | 12.9 | 8.33x | UINT16 | differs |
| uint16 | LANCZOS3 | 132 | 84.8 | 59.0 | 2.24x | 23.8 | 21.3 | 6.19x | UINT16 | differs |
| int16 | NEAREST | — | 5.30 | 5.23 | — | 2.69 | 2.74 | — | INT16 | main: n/a |
| int16 | BOX | — | 62.0 | 49.8 | — | 17.5 | 15.2 | — | INT16 | main: blank |
| int16 | BICUBIC | — | 89.5 | 68.9 | — | 24.7 | 20.2 | — | INT16 | main: blank |
| int16 | BILINEAR | — | 70.8 | 56.3 | — | 18.7 | 16.7 | — | INT16 | main: blank |
| int16 | BSPLINE | — | 89.2 | 69.7 | — | 23.0 | 20.0 | — | INT16 | main: blank |
| int16 | CATMULLROM | — | 88.5 | 71.3 | — | 24.6 | 22.0 | — | INT16 | main: blank |
| int16 | LANCZOS3 | — | 111 | 85.4 | — | 28.8 | 27.1 | — | INT16 | main: blank |
| uint32 | NEAREST | — | 27.9 | 28.1 | — | 27.7 | 27.3 | — | UINT32 | main: n/a |
| uint32 | BOX | — | 74.8 | 62.4 | — | 38.7 | 35.0 | — | UINT32 | main: blank |
| uint32 | BICUBIC | — | 108 | 89.6 | — | 44.8 | 41.7 | — | UINT32 | main: blank |
| uint32 | BILINEAR | — | 88.0 | 72.8 | — | 42.5 | 39.9 | — | UINT32 | main: blank |
| uint32 | BSPLINE | — | 109 | 88.4 | — | 46.8 | 43.4 | — | UINT32 | main: blank |
| uint32 | CATMULLROM | — | 109 | 89.2 | — | 48.0 | 40.7 | — | UINT32 | main: blank |
| uint32 | LANCZOS3 | — | 129 | 107 | — | 55.2 | 49.5 | — | UINT32 | main: blank |
| int32 | NEAREST | — | 28.5 | 29.0 | — | 26.9 | 27.9 | — | INT32 | main: n/a |
| int32 | BOX | — | 79.8 | 67.2 | — | 39.1 | 36.6 | — | INT32 | main: blank |
| int32 | BICUBIC | — | 107 | 86.8 | — | 45.5 | 41.2 | — | INT32 | main: blank |
| int32 | BILINEAR | — | 89.3 | 72.8 | — | 40.5 | 37.8 | — | INT32 | main: blank |
| int32 | BSPLINE | — | 108 | 86.2 | — | 46.3 | 43.2 | — | INT32 | main: blank |
| int32 | CATMULLROM | — | 106 | 85.7 | — | 46.2 | 41.6 | — | INT32 | main: blank |
| int32 | LANCZOS3 | — | 124 | 104 | — | 52.7 | 47.2 | — | INT32 | main: blank |
| float | NEAREST | — | 28.7 | 28.5 | — | 27.2 | 28.1 | — | FLOAT | main: n/a |
| float | BOX | 127 | 48.9 | 40.4 | 3.15x | 32.8 | 31.8 | 3.99x | FLOAT | identical |
| float | BICUBIC | 190 | 75.8 | 60.2 | 3.16x | 38.6 | 34.8 | 5.47x | FLOAT | differs |
| float | BILINEAR | 147 | 54.2 | 45.9 | 3.21x | 33.5 | 31.1 | 4.74x | FLOAT | differs |
| float | BSPLINE | 190 | 75.9 | 60.8 | 3.12x | 38.3 | 34.4 | 5.51x | FLOAT | differs |
| float | CATMULLROM | 187 | 76.9 | 60.2 | 3.10x | 38.1 | 34.9 | 5.34x | FLOAT | differs |
| float | LANCZOS3 | 232 | 92.6 | 72.3 | 3.21x | 44.3 | 40.7 | 5.71x | FLOAT | differs |
| double | NEAREST | — | 53.0 | 54.3 | — | 52.7 | 56.2 | — | DOUBLE | main: n/a |
| double | BOX | — | 74.2 | 68.0 | — | 58.5 | 59.4 | — | DOUBLE | main: blank |
| double | BICUBIC | — | 100.0 | 87.1 | — | 64.1 | 61.8 | — | DOUBLE | main: blank |
| double | BILINEAR | — | 86.6 | 73.6 | — | 58.0 | 59.2 | — | DOUBLE | main: blank |
| double | BSPLINE | — | 97.3 | 86.1 | — | 61.8 | 59.9 | — | DOUBLE | main: blank |
| double | CATMULLROM | — | 99.4 | 87.6 | — | 64.5 | 62.4 | — | DOUBLE | main: blank |
| double | LANCZOS3 | — | 115 | 98.8 | — | 70.6 | 66.1 | — | DOUBLE | main: blank |
| complex | NEAREST | — | 112 | 111 | — | 106 | 105 | — | COMPLEX | main: n/a |
| complex | BOX | — | 127 | 127 | — | 113 | 115 | — | COMPLEX | main: blank |
| complex | BICUBIC | — | 161 | 159 | — | 118 | 111 | — | COMPLEX | main: blank |
| complex | BILINEAR | — | 138 | 142 | — | 116 | 117 | — | COMPLEX | main: blank |
| complex | BSPLINE | — | 155 | 158 | — | 112 | 111 | — | COMPLEX | main: blank |
| complex | CATMULLROM | — | 155 | 154 | — | 111 | 113 | — | COMPLEX | main: blank |
| complex | LANCZOS3 | — | 183 | 179 | — | 122 | 123 | — | COMPLEX | main: blank |
| rgb16 | NEAREST | — | 41.5 | 43.2 | — | 39.8 | 40.6 | — | RGB16 | main: n/a |
| rgb16 | BOX | 166 | 123 | 100 | 1.65x | 62.4 | 51.9 | 3.20x | RGB16 | identical |
| rgb16 | BICUBIC | 230 | 182 | 146 | 1.57x | 76.1 | 70.4 | 3.27x | RGB16 | differs |
| rgb16 | BILINEAR | 189 | 141 | 115 | 1.65x | 64.3 | 57.8 | 3.27x | RGB16 | differs |
| rgb16 | BSPLINE | 227 | 185 | 149 | 1.53x | 76.5 | 68.0 | 3.34x | RGB16 | differs |
| rgb16 | CATMULLROM | 233 | 187 | 148 | 1.58x | 74.7 | 70.3 | 3.31x | RGB16 | differs |
| rgb16 | LANCZOS3 | 278 | 228 | 185 | 1.51x | 88.2 | 81.0 | 3.43x | RGB16 | differs |
| rgba16 | NEAREST | — | 52.5 | 53.3 | — | 53.1 | 53.7 | — | RGBA16 | main: n/a |
| rgba16 | BOX | 251 | 165 | 129 | 1.95x | 80.7 | 70.1 | 3.58x | RGBA16 | identical |
| rgba16 | BICUBIC | 357 | 269 | 207 | 1.72x | 114 | 103 | 3.46x | RGBA16 | differs |
| rgba16 | BILINEAR | 285 | 205 | 158 | 1.80x | 92.6 | 79.4 | 3.58x | RGBA16 | differs |
| rgba16 | BSPLINE | 361 | 272 | 208 | 1.73x | 109 | 91.5 | 3.94x | RGBA16 | differs |
| rgba16 | CATMULLROM | 360 | 270 | 207 | 1.74x | 112 | 93.9 | 3.83x | RGBA16 | differs |
| rgba16 | LANCZOS3 | 432 | 341 | 257 | 1.68x | 138 | 109 | 3.95x | RGBA16 | differs |
| rgbf | NEAREST | — | 80.0 | 79.3 | — | 79.6 | 80.1 | — | RGBF | main: n/a |
| rgbf | BOX | 235 | 117 | 111 | 2.12x | 88.8 | 84.6 | 2.77x | RGBF | identical |
| rgbf | BICUBIC | 348 | 192 | 167 | 2.09x | 103 | 94.8 | 3.68x | RGBF | differs |
| rgbf | BILINEAR | 268 | 125 | 120 | 2.24x | 87.2 | 88.5 | 3.03x | RGBF | differs |
| rgbf | BSPLINE | 341 | 190 | 161 | 2.11x | 100 | 95.4 | 3.57x | RGBF | differs |
| rgbf | CATMULLROM | 335 | 190 | 161 | 2.09x | 106 | 96.3 | 3.48x | RGBF | differs |
| rgbf | LANCZOS3 | 408 | 221 | 197 | 2.07x | 119 | 109 | 3.75x | RGBF | differs |
| rgbaf | NEAREST | — | 111 | 110 | — | 113 | 104 | — | RGBAF | main: n/a |
| rgbaf | BOX | 312 | 143 | 145 | 2.16x | 112 | 116 | 2.70x | RGBAF | identical |
| rgbaf | BICUBIC | 456 | 203 | 200 | 2.28x | 126 | 128 | 3.57x | RGBAF | identical |
| rgbaf | BILINEAR | 344 | 160 | 162 | 2.12x | 120 | 120 | 2.86x | RGBAF | identical |
| rgbaf | BSPLINE | 463 | 195 | 202 | 2.29x | 127 | 125 | 3.72x | RGBAF | identical |
| rgbaf | CATMULLROM | 464 | 200 | 196 | 2.36x | 131 | 126 | 3.69x | RGBAF | identical |
| rgbaf | LANCZOS3 | 538 | 245 | 245 | 2.20x | 138 | 139 | 3.86x | RGBAF | identical |

## Consistency checks

- Output hash stable across rounds: yes
- qpv before the change: 168 of 168 comparable cases identical to main.
- Crashes / NULL results: none
- Round-to-round spread (max/min of the per-run medians): median 1.048, worst 1.578
