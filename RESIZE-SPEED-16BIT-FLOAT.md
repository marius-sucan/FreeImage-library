# FreeImage_Rescale: SSE2 and AVX2 kernels for RGB16, RGBA16, RGBF and RGBAF images, and their pass order

2026-09-28, branch `worktree-io64` on top of `748fc04`. Measured on the 4-core i7-7700T: GCC 15 builds natively, MSVC 14.29
(v142) builds under Wine with `/QIntel-jcc-erratum`, 10-megapixel sources at 6 threads (section 7 says why), checked on
2-megapixel ones.

## 1. Summary

1. **New kernels.** RGB16, RGBA16, RGBF and RGBAF images no longer go through the plain loops. They get the SSE2 and AVX2 kernels
   of 8-, 24- and 32-bit images, made generic over the sample type, and new *direct* kernels that convert every tap straight
   from the source. Each pass picks the faster kind by how many taps each source sample feeds (section 2). AVX2 is chosen at
   run time on x64, as for the byte kernels.
2. **Same pixels in either order.** Every sum adds the same taps in the same order in double, and multiplies and adds are
   never fused. The output equals HEAD's to the bit in either forced pass order, on GCC and MSVC and on both the SSE2 and
   the AVX2 path (section 6).
3. **Speed at 10 megapixels, 6 threads.** Geomean of the new time over HEAD's (below 1 is faster), each pass alone, over
   4 filters and 6 cases:

   | | horizontal | vertical |
   |---|---|---|
   | MSVC, AVX2 | 0.50 | 0.69 |
   | MSVC, SSE2 | 0.59 | 0.80 |
   | GCC, AVX2 | 0.55 | 0.79 |
   | GCC, SSE2 | 0.65 | 0.93 |

   Whole two-pass resizes, 384 cases per build, the filter passes timed, each build in the order it picks:
   0.55 MSVC AVX2, 0.69 MSVC SSE2, 0.64 GCC AVX2, 0.77 GCC SSE2. 16-bit images gain most (0.44-0.48 with MSVC AVX2).
   Float box and bilinear gain least: those passes already stream at the memory's bandwidth (section 3).
4. **Pass order.** The four types get their own costs in `scaleInBands`:
   - 16-bit samples: a horizontal tap costs as much as a vertical one, plus 4 per pixel the horizontal pass writes,
     with a margin of 1.1.
   - Float samples: a horizontal tap costs 0.6 of a vertical one, plus 1 per written pixel, with no margin.

   Measured against the faster order of each case, the loss falls from 2.05% to 0.78% (16-bit) and from 2.95% to 0.89%
   (float) at 10 MP. At 2 MP it falls from 1.70% to 0.52% and from 2.39% to 0.94% (section 5). Where the order changes,
   the image kept between the passes rounds differently, so the output changes too: 239 of the differential's 12,252
   signatures change (26 RGB16, 32 RGBA16, 111 RGBF and 70 RGBAF of 380 each). Each equals HEAD's code forced into the
   new order.
5. **Not faster everywhere.** With GCC's SSE2 path, RGBF box and bilinear resizes that write many pixels per tap (0.75x and
   up) are up to 1.35x slower than GCC's auto-vectorised plain loop. On MSVC AVX2, what Marius ships, 1 RGBF case of 96 is
   more than 5% slower (1.10x). Section 8 has the rest.

## 2. The kernels

### 2.1 Before

`HorizontalFilterSamples<T, SPP>` and `VerticalFilterSamples<T, SPP>` convert each sample to double and add its taps in
order. GCC vectorises both loops, two doubles at a time. MSVC vectorises only the vertical loop and runs the horizontal one a
sample at a time.

### 2.2 Now

- **Ring kernels**: the 8-, 24- and 32-bit ones, generic over the sample type.
  - Horizontally, 4 rows at a time: each source pixel is converted once into a ring of doubles, and a tap adds 12 or 16
    sums. With AVX2, two destination pixels share each load.
  - Vertically: each source row of a block is converted once.
  - What is new is only the conversion into the ring and the stores out of it: 16-bit samples widen through
    `_mm_cvtepi32_pd`, floats through `_mm_cvtps_pd`. 16-bit stores clamp and pack without SSE4.1.
- **Direct kernels**, new: every tap is converted straight from the source.
  - For these samples the conversion costs little, while the ring's transposition competes for the shuffle port and its
    4 rows for memory.
  - Horizontally they filter 4 rows at a time for 16-bit samples (compute-bound) and 2 for floats (which stream from
    memory). A pixel's channels sit in one AVX2 sum or two SSE2 sums per row.
  - Vertically they walk whole destination rows, 16 samples in registers through every tap, so each source row streams
    in one run. The ring's blocks of 256 samples had read 0.5-1 KB of each of about 64 rows per work item, which at
    1 thread made the vertical pass up to 1.5x slower than HEAD on box and bilinear.

Which one runs, from single passes timed at 48 MP (the thresholds are taps per source pixel or row):

| pass | SSE2 | AVX2 |
|---|---|---|
| horizontal | direct below 3.5 (box, bilinear), else the ring | direct below 3.5, else the ring |
| vertical | direct below 2.5, else the ring | always direct |

- A window too wide for a ring's 4 MB goes to the direct kernels, which have no limit, instead of the plain loops.
- The vertical ring of these samples holds blocks of up to 1024 samples in 128 KB, so each source row is read in runs of
  2-4 KB.
- Bytes keep every path they had.

### 2.3 The same output

- Conversions are exact.
- Float stores round as `(float)` does.
- 16-bit stores clamp `v + 0.5` to 0..65535 in double (NaN to 0), then truncate. For every sum a 16-bit source can
  produce, this equals `RoundSample<WORD>` (horizontal pass) and the clamped `(int)(v + 0.5)` (vertical pass).
- No FMA: GCC and clang compile the AVX2 functions for `avx2` only, and MSVC never fuses intrinsics. Checked on the GCC
  object and on the MSVC listing: VEX code only in functions named `*AVX2*`, no `vfmadd`, and no legacy SSE after a dirty
  YMM register in any function that runs.

## 3. Why float box and bilinear cannot gain much

At 48 MP, 8 threads, before the other user's job started:
- A horizontal RGBAF box reduction 8000 -> 2000 reads 768 MB. HEAD's GCC loop took 43 ms: 17.9 GB/s of reads.
- Bilinear, with the writes and their read-for-ownership, moves about 1.15 GB in the same time: 27 GB/s.
- That is most of what this machine's dual-channel DDR4-2400 delivers. A kernel that computes faster cannot finish sooner:
  the new ones take 1.0-1.05x there.
- At 1 thread, where the memory is not saturated, the direct kernels of these passes take 0.50-0.68x with AVX2 under GCC
  and 0.33-0.61x under MSVC; with SSE2, 0.81-1.15x and 0.58-1.01x. These were measured on 48 MP single passes with a
  4-row version of the float direct kernel; the final one filters 2 rows.
- The 8-core i7-11700 has twice the cores and 1.33x the bandwidth. These cases will gain even less there, and the
  16-bit and wide-filter ones about as much.

## 4. Speed

10-megapixel sources (3648 x 2736, 2736 x 3648 and 5920 x 1690), 6 threads, minimum of 3 calls after one warm-up.

### 4.1 Each pass alone

Single-pass resizes of 3648 x 2736 (to 0.25x, 0.5x and 1.6x on one axis), box, bilinear, b-spline and lanczos3. Time of
the pass (trace) over HEAD's; geomean, and range in brackets:

| pass | format | GCC SSE2 | GCC AVX2 | MSVC SSE2 | MSVC AVX2 |
|---|---|---|---|---|---|
| horizontal | rgb16 | 0.62 (0.36-0.95) | 0.45 (0.25-0.62) | 0.49 (0.28-0.83) | 0.39 (0.20-0.68) |
| horizontal | rgba16 | 0.42 (0.32-0.51) | 0.35 (0.27-0.44) | 0.44 (0.37-0.54) | 0.35 (0.31-0.48) |
| horizontal | rgbf | 0.71 (0.41-1.21) | 0.66 (0.27-1.03) | 0.76 (0.50-1.08) | 0.68 (0.40-1.03) |
| horizontal | rgbaf | 0.98 (0.88-1.06) | 0.87 (0.57-1.01) | 0.73 (0.49-1.00) | 0.68 (0.38-1.01) |
| horizontal | **all** | 0.65 (0.32-1.21) | 0.55 (0.25-1.03) | 0.59 (0.28-1.08) | 0.50 (0.20-1.03) |
| vertical | rgb16 | 0.88 (0.74-1.13) | 0.73 (0.58-0.93) | 0.69 (0.61-0.78) | 0.58 (0.49-0.76) |
| vertical | rgba16 | 0.89 (0.74-1.11) | 0.73 (0.61-0.91) | 0.70 (0.60-0.80) | 0.56 (0.47-0.73) |
| vertical | rgbf | 0.96 (0.70-1.08) | 0.85 (0.47-1.02) | 0.92 (0.78-1.01) | 0.82 (0.53-1.02) |
| vertical | rgbaf | 0.98 (0.79-1.08) | 0.85 (0.59-1.01) | 0.93 (0.80-1.02) | 0.84 (0.56-1.03) |
| vertical | **all** | 0.93 (0.70-1.13) | 0.79 (0.47-1.02) | 0.80 (0.60-1.02) | 0.69 (0.47-1.03) |

- On AVX2, 4 of 192 cases are more than 2% slower than HEAD, at most 1.03x (float box and bilinear).
- On SSE2, 15 of 192, at most 1.21x (a GCC RGBF bilinear enlargement, 19 -> 23 ms). The rest are 16-bit vertical box
  passes of 4-5 ms.
- GCC's baseline is stronger than MSVC's (it vectorises the horizontal loop), so the same kernels gain more under MSVC.

### 4.2 Whole resizes

384 two-pass resizes per build: 4 formats, the 3 shapes, box, bilinear, b-spline and lanczos3, and 8 scales (0.1, 0.25,
0.5, 0.75, 1.25 and 1.6 on both axes, 0.3 x 0.6 and 0.6 x 0.3). HEAD runs in the order its rule picks, the new code in the
order the new rule picks. Time of both passes over HEAD's; geomean, and range in brackets:

| images | GCC SSE2 | GCC AVX2 | MSVC SSE2 | MSVC AVX2 |
|---|---|---|---|---|
| rgb16 | 0.76 (0.47-1.12) | 0.60 (0.36-0.94) | 0.62 (0.41-0.95) | 0.48 (0.32-0.78) |
| rgba16 | 0.61 (0.39-0.98) | 0.50 (0.31-0.93) | 0.57 (0.44-0.85) | 0.44 (0.34-0.78) |
| rgbf | 0.86 (0.53-1.35) | 0.73 (0.38-1.16) | 0.85 (0.50-1.14) | 0.70 (0.35-1.10) |
| rgbaf | 0.89 (0.54-1.14) | 0.75 (0.43-1.12) | 0.76 (0.50-1.05) | 0.63 (0.34-1.04) |
| reductions | 0.78 (0.39-1.35) | 0.62 (0.31-1.16) | 0.70 (0.41-1.14) | 0.54 (0.32-1.10) |
| enlargements | 0.75 (0.44-1.27) | 0.67 (0.40-1.12) | 0.68 (0.45-1.11) | 0.58 (0.37-1.04) |
| box | 0.87 (0.46-1.21) | 0.78 (0.40-1.16) | 0.78 (0.44-1.08) | 0.69 (0.34-1.10) |
| bilinear | 0.90 (0.53-1.35) | 0.72 (0.38-1.16) | 0.77 (0.49-1.14) | 0.60 (0.36-0.94) |
| b-spline | 0.72 (0.45-1.12) | 0.59 (0.39-0.96) | 0.65 (0.42-1.05) | 0.51 (0.35-0.83) |
| lanczos3 | 0.64 (0.39-0.97) | 0.49 (0.31-0.79) | 0.60 (0.41-0.83) | 0.44 (0.32-0.73) |
| **all** | **0.77** | **0.64** | **0.69** | **0.55** |

- **Whole calls** include `FreeImage_AllocateT` of the result: 0.82 GCC SSE2, 0.71 GCC AVX2, 0.82 MSVC SSE2,
  0.72 MSVC AVX2.
- **Why passes are the headline for MSVC:** under Wine every MSVC call spends about 22 ms outside the passes, HEAD and
  new alike. That is the allocation and page faulting of the result through Wine's heap, and it varies by up to 18-27 ms
  between calls of the same case. The same RGBF resize timed 20.5 ms (HEAD) against 31 ms (new) in the matrix, and
  30.6 against 20.7 in a fresh process. The passes themselves repeat within a few percent.

## 5. Pass order

### 5.1 Data

- **What was timed:** the new code, every two-pass case above in both forced orders (`FI_RESCALE_ORDER`, calls
  alternating), GCC and MSVC, SSE2 and AVX2. That makes 1536 pairs at 10 MP, plus 512 pairs on a 1600 x 1200 source.
- **Model:** the costs `scaleInBands` compares are those of the byte kernels, `(h * TX + p * dst_width) * src_height +
  TY * dst_width` against `TY * src_width + (h * TX + p * dst_width) * dst_height`, with a margin over the width rule.
- **Fit:** constants fitted per sample size on the pooled compilers and ISAs.
- **One rule for every processor:** an image resizes to the same pixels on every machine, and the per-compiler and
  per-ISA losses stay within 0.5% of each other.

### 5.2 Rules compared

Mean loss against the faster order of each case, the worst case, and the pairs lost by more than 5%:

| sample size | rule | 10 MP mean | 10 MP worst | 10 MP >5% | 2 MP mean | 2 MP worst |
|---|---|---|---|---|---|---|
| 16-bit | HEAD's (2 x horizontal taps, margin 1.1; 2 for RGBAF) | 2.05% | 1.46 | 88 / 768 | 1.70% | 1.29 |
| 16-bit | **new: 1, +4 per pixel, margin 1.1** | **0.78%** | 1.39 | 33 / 768 | **0.52%** | 1.08 |
| 16-bit | byte rule (0.8, +4, no margin) | 1.37% | 1.39 | 56 / 768 | 0.76% | 1.28 |
| 16-bit | width rule | 5.76% | 1.68 | 193 / 768 | 5.92% | 1.58 |
| float | HEAD's | 2.95% | 1.49 | 143 / 768 | 2.39% | 1.54 |
| float | **new: 0.6, +1 per pixel, no margin** | **0.89%** | 1.43 | 41 / 768 | **0.94%** | 1.30 |
| float | byte rule | 3.84% | 1.55 | 195 / 768 | 2.61% | 1.39 |
| float | width rule | 2.17% | 1.43 | 104 / 768 | 2.37% | 1.54 |

- Per compiler and ISA, the new rules lose 0.47-0.99% (16-bit) and 0.56-1.12% (float) at 10 MP.
- The neighbourhoods, and their cliffs:
  - 16-bit: the best 12 of 45 neighbours checked (a tap of 0.9-1.1, 3-5 per pixel, a margin of 1.08-1.3) lose
    0.78-1.16%. A tap of 0.8 loses 1.25%, 1.2 loses 2.5%, 6 per pixel 1.8%, and a margin of 1.05 or 1.0 2.6%.
  - Float: a tap of 0.55-0.75, 0-1 per pixel and a margin of 1-1.02 lose 0.86-0.89%; a tap of 0.5, or a margin of 1.05,
    loses 2.2%.
- They also hold when the orders are compared on pass times alone (1.01% and 1.43%, the best fits there: 0.86% and 1.43%).
- The new rule takes another order than HEAD's in 24% of the 16-bit cases, 31% of RGBAF and 53% of RGBF.

### 5.3 What it picks

The orders the new rules take on the matrix (the same at 10 and 2 MP, and for every shape; xy = horizontal first):

| scale | 16-bit: box, bilinear | 16-bit: b-spline, lanczos3 | float: box | float: bilinear | float: b-spline, lanczos3 |
|---|---|---|---|---|---|
| 0.1 | yx | xy | xy | xy | xy |
| 0.25 | yx | b-spline yx, lanczos3 xy | xy | xy | xy |
| 0.5 | yx | yx (lanczos3 xy on 2 of 6) | yx | xy | xy |
| 0.75 | yx | xy | yx | xy | xy |
| 0.3 x 0.6 | xy | xy | xy | xy | xy |
| 0.6 x 0.3 | yx | yx | yx | yx | yx |
| 1.25 | xy | yx | xy | xy | yx |
| 1.6 | xy | xy | xy | xy | yx |

- **16-bit, compute-bound:** a horizontal tap costs about a vertical one, and each pixel the horizontal pass writes adds
  4. Reductions filter vertically first unless the filter is wide and the scale mild or strong.
- **Float, which streams from memory:** a horizontal tap costs 0.6 of a vertical one, since it reads 12 or 16 contiguous
  bytes while a vertical tap reads another row. Most reductions filter horizontally first, enlargements only with box and
  bilinear.

### 5.4 The worst misses, re-timed

The seven largest losses of the new rules, re-timed with 45 calls per order (minimum ms of whole calls, and of the passes
alone):

| build | case | filter | xy | yx | rule takes | loss (passes) | in the matrix |
|---|---|---|---|---|---|---|---|
| MSVC SSE2 | rgbf 2736x3648 -> 2052x2736 | lanczos3 | 60.3 | 60.4 | xy | 1.00 (1.00) | 1.43 |
| MSVC AVX2 | rgbf 3648x2736 -> 1824x1368 | b-spline | 44.2 | 46.8 | xy | 1.00 (1.00) | 1.35 |
| MSVC SSE2 | rgbaf 5920x1690 -> 2960x845 | b-spline | 45.6 | 50.0 | xy | 1.00 (1.00) | 1.35 |
| GCC AVX2 | rgbaf 5920x1690 -> 3552x507 | bilinear | 12.5 | 16.3 | yx | **1.31** (1.38) | 1.32 |
| MSVC SSE2 | rgba16 3648x2736 -> 4560x3420 | bilinear | 104.0 | 102.3 | xy | 1.02 (1.06) | 1.39 |
| MSVC AVX2 | rgbaf 1600x1200 -> 2560x1920 | b-spline | 51.6 | 52.0 | yx | 1.01 (1.04) | 1.30 |
| MSVC SSE2 | rgbf 1600x1200 -> 2560x1920 | lanczos3 | 46.0 | 45.0 | yx | 1.00 (1.04) | 1.27 |

- Six were the Wine allocation noise of section 4.2: near-ties, or the order the rule takes.
- One is real: RGBAF bilinear 0.6 x 0.3 reductions, where horizontal-first is faster in every build (by 2%, 32%, 2% and
  14%). The float rule is fitted on RGBF and RGBAF together, and RGBF prefers vertical-first on the same case in three of
  the four builds.

## 6. Verification

On the final kernels, before the rule change:
- warnings as HEAD (11 with `-Wall -Wextra`; 4 on aarch64, x86 and x64 Linux and Windows with zig clang);
- the resize oracle, `verify5.cpp` (485,333 checks), gives HEAD's hash on GCC (`041cca732abc5b44`) and MSVC
  (`f9ef8449cb22d09d`), both paths, 1 and 8 threads, and passes under ASan and UBSan on both paths;
- the differential's 12,252 signatures (every layout, palettes and TRUE_COLOR included) are unchanged: at 1 and 4 threads,
  on the SSE2 path, and with bands of 1 and 3 rows;
- output hashes against HEAD in both forced orders on GCC and MSVC, both paths: 2016 small cases, 756 with windows beyond
  the rings' 4 MB (which now go to the direct kernels), and 96 at 48 MP. 0 mismatches.

With the rule change:
- `verify6.cpp` is `verify5.cpp` with the new costs of the four types in its `orderXY()`. It passes all 485,333 checks
  under GCC (hash `2ed9b4a4bc90c093`) and MSVC (`d0de2b901fd9ff82`), both paths, at 1 and 8 threads.
- The differential changes in 239 signatures, all of RGB16, RGBA16, RGBF or RGBAF images. Each equals HEAD's code forced
  into the new order: 192 into horizontal-first, 47 into vertical-first. Every other layout is unchanged. The same holds at
  1 and 4 threads, on the SSE2 path, and with bands of 1 and 3 rows.
- In both forced orders the output is still HEAD's, on 2016 small cases.

## 7. How it was measured

- **6 threads.** My jobs run at nice 5. Another user's nice-0 job (0.4-0.8 cores) was busy all morning and pre-empted one of
  8 OpenMP threads, and every barrier then waited for it. The same 4.6 ms vertical pass took 147 ms in another call, and a
  3-hour 8-thread matrix at 48 MP had to be thrown away. At 6 threads the same cases repeated within 1-4%.
- **10 megapixels** at your request, after the 48 MP single-pass runs that set the kernel thresholds. Those ran before the
  other job started (foreign load 0.07-0.18 cores).
- The load gate held a run back while other processes used more than 1.5 cores.
- The builds' order rotated group by group, and the two orders alternated call by call.

## 8. Not done, and limits

- **RGBF box and bilinear on GCC's SSE2 path**: 26 of 96 whole resizes more than 5% slower than HEAD, up to 1.35x.
  - With few taps per pixel, GCC's auto-vectorised plain loop costs less per written pixel than the direct kernel's
    3-channel loads and stores.
  - AVX2: 7 of 96 with GCC (up to 1.16x) and 1 with MSVC (1.10x). The SSE2 path runs only on processors without AVX2.
- **Second passes.** The ring-or-direct thresholds were fitted on single passes streaming from memory. In a two-pass
  resize the second pass reads the 4 MB band from cache, where the ring's saving counts for more. The order rule absorbs
  what the kernels do, but the second pass's kernel choice was not tuned separately.
- **FIT_UINT16 and FIT_FLOAT** (one channel) keep the plain loops; the kernels would take them with 16 rows.
- **32-bit x86** builds compile the SSE2 kernels (8 vector registers: sums spill); not timed.
- **Nothing ran on the 8-core i7-11700**, and the MSVC builds ran under Wine.
