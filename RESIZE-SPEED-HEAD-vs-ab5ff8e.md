# FreeImage_Rescale speed: HEAD against ab5ff8e, and against QPV's 29 August DLL

> The kernels of §8 (`1eb0175`, `06d69d9`) and the pass order for 8-, 24- and 32-bit images were replaced by `394d42a`,
> `59cf8ef` and `b5c8f48`: see RESIZE-SPEED-HUGE-IMAGES.md.

2026-09-27. HEAD is `b6e3af3` (`worktree-io64`), whose resize sources are the same as `qpv`'s and were last
changed by `d60316b`; the fix in §8 was committed after it, as `1eb0175` and `06d69d9`. The question: HEAD is about
25% slower than `ab5ff8e` on very large images, with any filter, on an 8-core machine. Which commit did it, and why?

## 1. Summary

1. **The reference you compared against is not `ab5ff8e`'s code.** You measured against QPV's `FreeImage-old.dll`.
   It was built 2026-08-29 21:01 UTC, shipped as QPV's `FreeImage.dll` in 6.3.00 (31 Aug), and was moved aside on 20 Sep.
   It is **`274f833`**, the tip of the unmerged resize rework (`ecb0b12`, `b2e2281`, `274f833`, branch
   `worktree-resize-parallel-rework`):
   - an MSVC build of `274f833` runs within 1% of it and gives the same pixels (§4);
   - `ab5ff8e`'s own code takes 55% longer.

   The rework forked from `qpv` at `33c3259` and **was never merged**; the branch was deleted later. Its commits are now referenced by no
   branch, tag, reflog or remote, so a `git gc` can delete them (§11, first item).
2. **What HEAD lacks is `274f833`'s SSE2 horizontal pass.** It transposes two source rows into a float tile and vectorises
   across the rows, keeping each tap sum in order, so its output is bit-exact.
   - Under MSVC it costs **0.31-0.34 ns per sample-tap**.
   - HEAD's kernels cost **0.55-0.61** there, whichever pass they run.
   - **No commit on `qpv` removed this kernel: `qpv` never had it.**
   - Against the 29 Aug DLL (MSVC, 8 threads, 16000x12000 -> 1920x1440), HEAD is **18-53% slower** with bicubic, b-spline,
     catmull-rom and lanczos3 (1.18-1.53). It is level with bilinear (0.94/0.99) and faster with box (0.63/0.72).
   - The gap grows with the reduction and shrinks with more threads. At 0.4x it is 0-11% at 8 threads and 18-34% at 1 thread; on the
     24000x18000 -> 1920x1440 reduction it is 65-78% at 1 thread (§5). That is why it shows on very large images shrunk to screen size.
3. **Against genuine `ab5ff8e` code, HEAD is faster, not slower.**
   - GCC: 0.53x the time over 126 large cases, never slower.
   - MSVC (placement-neutral builds, §7): 0.60-0.77x over the matrix (the 14 formats the old code resampled, 6 filters,
     up and down, 1 and 8 threads); the worst case is +1.4%.
   - **One commit on the `qpv` line makes MSVC builds slower: `d60316b`**, 13% slower on 32-bit Lanczos3 than its parent
     (32-bit box and 24-bit Lanczos3 within 1%, 24-bit box 13% faster).
   - Its pass-order rule assumes a horizontal tap costs two vertical taps; that was fitted with GCC, where the ratio is 1.56-1.75.
   - MSVC 14.29 compiles HEAD's vertical kernel to ~5.5 instructions per sample-tap against GCC's ~3.1. Its ratio is
     0.90-1.02, so "vertical first" saves nothing under MSVC, and loses on 32-bit (§6).
4. **Code placement.** On Intel 6th-10th gen Core (the JCC erratum), where the linker happens to put one 79-byte loop
   changes identical MSVC-built code by up to 36%. This is proved by the loop addresses in 12 builds (§7).
   - QPV's current `FreeImage.dll` (22 Sep) has its 24-bit loop in the bad place: 900 ms, against 674 ms for the same kernel placed clear.
   - Your 11th-gen+ machine does not have the erratum; QPV users on 6th-10th gen Intel do.
   - `/QIntel-jcc-erratum` removes the effect for 3.5-4.6 KB of code.
5. **The fix is committed on `worktree-io64`, and gives HEAD's output to the bit** (§8):
   - `1eb0175`: `274f833`'s horizontal kernel, ported to HEAD's banded passes. It is used on x86 and x64 with SSE2;
     other targets keep the old kernel.
   - `06d69d9`: an SSE2 vertical kernel for 8-bit samples, in MSVC builds only (GCC's own loop is faster).
   - Checked with the 485,333-check resize oracle under GCC and under MSVC, the 12,252-signature differential, and 54 MSVC
     output hashes.
   - Under MSVC, HEAD now needs **0.40-0.98 of your 29 Aug DLL's time** on 16000x12000 -> 1920x1440, depending on the filter
     (8 threads). The pass-order rule is unchanged; refitting it would gain up to 18% more on lanczos3 and 6-8% on the
     32-bit cubic filters (nothing on the 24-bit ones).
   - The rework's commits are kept on branch `resize-rework`.
6. **Not measured on your machine.** Every number here comes from a 4-core i7-7700T; the MSVC builds ran under Wine.
   `FreeImage-resize-speed-check.zip` runs the same builds on the 8-core machine in 30-45 minutes (§10).

## 2. How it was measured

**Machine.** Intel i7-7700T (Kaby Lake, 4 cores / 8 threads, 8 MB L3, microcode 0xf8), 16 GB, Linux 7.0. Not the 8-core
machine: every number here is from 4 cores. It is a 35 W part: back-to-back heavy runs drift ~6% slower as it heats, so
every run is preceded by 6 s idle, and the variants are interleaved with their order rotated per case and round.

**Builds.**
* **GCC 15.2**, `Makefile.gnu` flags (`-O3 -fopenmp`): the whole library at `ab5ff8e` (with `default(none)` read as
  `default(shared)`, which is what MSVC compiled) and at HEAD, plus Resize.o/Rescale.o of every one of the 22 commits,
  each compiled with its own headers and linked ahead of HEAD's library (checked: `ab5ff8e`'s objects in HEAD's
  library time the same as the whole `ab5ff8e` library, 603 vs 606 ms).
* **MSVC 14.29.30133 (toolset v142, the one `FreeImage.2017.vcxproj` names)** + Windows SDK 10.0.19041, installed with
  msvc-wine and run under Wine 10, with FreeImage.2017.vcxproj's Release|x64 compiler settings: `/Ox /Ob2 /Oi /Ot /GL
  /GF /MT /GS- /W3 /Zp16 /Oy /openmp /EHsc /fp:precise`, `/LTCG`. The 43 core sources the resize path needs plus
  Resize.cpp/Rescale.cpp of each commit, linked statically into the benchmark (a DLL's DllMain would initialise every
  plugin, whose libraries are not built here); `vcomp140.dll` is the one QPV ships (14.13.26020). The MSVC build of
  `ab5ff8e` gives the same output hash as GCC's.
* **The same MSVC builds again with `/QIntel-jcc-erratum`** (§7): they take the code layout out of the comparison.

**Benchmarks.** `bench_large` times `FreeImage_Rescale` end to end on a generated photo-like image (smooth shading,
hard-edged blocks, noise); 1 warm-up, 3 timed calls, median. Traced builds of `ab5ff8e`, `79a9b43`, `9ee588d` and HEAD
add timers around `horizontalFilter`/`verticalFilter` (`FI_RESIZE_TRACE`), and HEAD's traced build also takes
`FI_RESCALE_ORDER=xy|yx` to force the pass order without changing anything else. The matrix of the earlier report
(`rescale-bench`: 19 formats x 7 filters, 4000x3000 <-> 1600x1200) is rerun too (§9).

**What could not be done here.** No 8-core machine; hardware counters are closed to unprivileged users
(`perf_event_paranoid` = 4); Windows threading is Wine's. The last one was measured: an empty `omp parallel for`
region costs 17 us with vcomp under Wine and 3 us with libgomp, and HEAD enters ~40 regions per call, under 1 ms
of a 600-3000 ms resize.

## 3. Every change to the resize code since ab5ff8e

`git log ab5ff8e..HEAD -- Source/FreeImageToolkit/Resize.cpp Resize.h Rescale.cpp Filters.h` lists 22 commits (no merges;
HEAD `b6e3af3` on `worktree-io64` has the same resize sources as `qpv`). Only four of them change code that runs in a
two-pass resize of a 24- or 32-bit image; the rest fix other paths, or only comments and encoding.

| commit | date | what it did | touches the 24/32-bit two-pass path? |
|---|---|---|---|
| `0bc96b8` | 08-29 | `default(none)` -> `default(shared)` on the 42 pragmas, `-fopenmp` in the makefiles | no: MSVC already compiled `default(none)` as shared |
| `28287d1` | 09-17 | `scale()`: NULL check after allocating the destination | no |
| `1505c3f` | 09-17 | UINT16/RGB16/RGBA16/float: samples per pixel from the image, not the rectangle | no |
| `055eba2` | 09-17 | horizontalFilter: pixel offset no longer divided by the sample size (16-bit, float types) | no |
| `71aa1b2` | 09-17 | empty rescale rectangle: no `Weights[-1]`, no division by zero | no (weights-table trim guard only) |
| `c6f6e12` | 09-17 | `FreeImage_RescaleRawBits`: leak, overwrite, false success | no |
| `402d7fd` | 09-17 | 4-bit images whose palette looks like a ramp keep their palette | no |
| `622124a` | 09-17 | 1-bit -> 8-bit vertical pass scaled by 255 twice | no |
| `52e2ca7` | 09-17 | `FI_RESCALE_TRUE_COLOR` inverted `FIC_MINISWHITE` | no |
| `2877e63` | 09-17 | CWeightsTable: window size in double, mallocs checked | no (same weights) |
| `cf6d13e` | 09-17 | RescaleRect: sub-byte left offset of 1/4-bit images | no |
| `3f0e1dc`, `f3efc44` | 09-17, 09-22 | MakeThumbnail at exactly `max_pixel_size` | no |
| `9988ebf`, `5d0098d` | 09-22, 09-23 | comments; Latin-1 -> UTF-8 | no (token-identical) |
| `ddea696` | 09-23 | types the filters did not handle are refused | no |
| `54dec18` | 09-23 | INT16/UINT32/INT32/DOUBLE/COMPLEX resampled: `HorizontalFilterSamples` / `VerticalFilterSamples` templates added | no (new types only) |
| `18e1a5f` | 09-23 | UINT16/RGB16/RGBA16/FLOAT/RGBF/RGBAF moved to the templates | no (other types) |
| **`79a9b43`** | 09-23 | **8-bit grey, 24- and 32-bit moved to the templates**: row-blocked vertical pass (256 samples, accumulators in memory) replaces the column-walking one; horizontal pass stores through `RoundSample` | **yes** |
| **`9ee588d`** | 09-23 | **both passes run over ~4 MB bands of rows** instead of a full-size temporary image; weights built once | **yes** |
| `c89c660` | 09-24 | `FILTER_NEAREST = -1` | only a branch in `scale()` |
| **`d60316b`** | 09-25 | **squash of `1cfe331` + `7fa470f` (branch rescale-speed): the pass order is chosen by a tap-cost model** (`HORIZONTAL_TAP_COST 2`, `COLUMN_TAP_COST 2.5`, `ORDER_MARGIN 1.1`, `WIDE_ORDER_MARGIN 2`) instead of "horizontal first unless the width grows" | **yes** |

## 4. Your reference DLL is the unmerged rework

Same machine and settings, 8 threads, 24-bit 16000x12000 -> 1920x1440 lanczos3:

| build | ms | output hash |
|---|---|---|
| QPV `FreeImage-old.dll` (built 2026-08-29 21:01 UTC) | 431.6 | `9cd4214b4cb48ff8` |
| MSVC build of `274f833` (rework tip) | 433.6 | `9cd4214b4cb48ff8` |
| MSVC build of `90ffece` (`274f833`'s parent: rework without the SSE2 pass) | 671.4 | `9cd4214b4cb48ff8` |
| MSVC build of `ab5ff8e` (padded) | 671.6 | `9cd4214b4cb48ff8` |
| QPV `FreeImage.dll` (22 Sep, built from `qpv`) | 900.1 | `9cd4214b4cb48ff8` |
| HEAD (padded) | 600.9 | `0e5b0e097c1fe3d3` |

* The rework never changes a pixel: the MSVC and GCC builds of `274f833` match `ab5ff8e` in all 16 checked cases
  (4 shapes x 4 filters, odd sizes included). HEAD's hash differs because `d60316b` filters these vertically first: the
  image between the passes is rounded to 8 bits, so 13-31% of samples move by one step (this is documented in that commit).
* History: the rework is 9 commits on `33c3259` (2026-08-29): `ecb0b12` (contiguous weights), `b2e2281` (row-major
  vertical pass), `96e43cb` + its revert `f35bb47` (float sums), three commits of notes, and `274f833` (SSE2 horizontal
  pass). `qpv` continued from the same parent with `dbb117f`, `73d3287`, `17d98ed`, `0bc96b8` and never merged it.
  The September kernel work (`79a9b43`, `9ee588d`, `d60316b`) was done on `qpv`, on top of `ab5ff8e`'s kernels, and
  never had `274f833`.
* `git for-each-ref --contains 274f833` and `git reflog --all` are empty, and `origin` has no such branch.

## 5. HEAD against the rework

### 5.1 By filter, one large reduction

MSVC, 8 threads, 16000x12000 -> 1920x1440 (s = 0.12), median ms. The two QPV DLLs are your real builds, the others
are built here with `/QIntel-jcc-erratum`:

| filter | QPV 29 Aug DLL | QPV 22 Sep DLL | 274f833 (rework) | ab5ff8e | HEAD | HEAD / 29 Aug DLL | HEAD / ab5ff8e |
|---|---|---|---|---|---|---|---|
| 24rgb box | 194 | 249 | 190 | 206 | 122 | **0.63** | 0.59 |
| 24rgb bicubic | 335 | 634 | 331 | 478 | 408 | **1.22** | 0.85 |
| 24rgb bilinear | 232 | 378 | 230 | 293 | 218 | **0.94** | 0.74 |
| 24rgb b-spline | 343 | 638 | 338 | 487 | 405 | **1.18** | 0.83 |
| 24rgb catmull-rom | 338 | 636 | 336 | 479 | 399 | **1.18** | 0.83 |
| 24rgb lanczos3 | 438 | 901 | 429 | 673 | 605 | **1.38** | 0.90 |
| 32rgba box | 238 | 268 | 246 | 269 | 170 | **0.72** | 0.63 |
| 32rgba bicubic | 434 | 595 | 438 | 598 | 570 | **1.31** | 0.95 |
| 32rgba bilinear | 302 | 375 | 299 | 374 | 299 | **0.99** | 0.80 |
| 32rgba b-spline | 431 | 611 | 423 | 609 | 578 | **1.34** | 0.95 |
| 32rgba catmull-rom | 430 | 598 | 428 | 599 | 570 | **1.33** | 0.95 |
| 32rgba lanczos3 | 551 | 840 | 546 | 839 | 846 | **1.53** | 1.01 |

### 5.2 Why the gap depends on the filter and the ratio

The time goes into the pass that runs over the full source ("the big pass"). The cost of one sample-tap in that pass,
at 1 thread, 24000x18000 -> 1920x1440 lanczos3 (7.77 G sample-taps at 24 bits, 10.36 G at 32), in ns:

| kernel doing the big pass | MSVC 24-bit | MSVC 32-bit | GCC 24-bit | GCC 32-bit |
|---|---|---|---|---|
| ab5ff8e, hand-written horizontal | 0.58 | 0.54 | 0.56 | 0.62 |
| 274f833, SSE2 horizontal (rework) | 0.34 | 0.31 | 0.30 | 0.29 |
| 9ee588d, template horizontal | 0.61 | 0.54 | 0.56 | 0.63 |
| HEAD, template vertical (rule: vertical first) | 0.60 | 0.61 | 0.36 | 0.36 |
| HEAD forced horizontal first, template horizontal | 0.61 | 0.55 | - | - |
| HEAD + SSE2 vertical | 0.48 | 0.46 | 0.48 | 0.47 |

* The rework's big pass is its SSE2 horizontal kernel, at 0.31-0.34 ns under MSVC. HEAD's big pass is its vertical kernel
  at 0.60, and forcing HEAD horizontal-first only moves it onto a template horizontal kernel at 0.55-0.61. Under MSVC, HEAD
  has no kernel as fast as the rework's.
* Wide filters mean many taps per output, so the tap cost dominates; with box and bilinear the fixed costs dominate
  (stores, bands, the rework's full-size temporary image and page faults, the tile transposition), and there HEAD's band
  structure wins. A stronger reduction also means more taps per output.
* More threads shrink the gap, because the rework allocates and faults in a full-size temporary image on every call.
  On the standard matrix (0.4x, §9), HEAD against the rework on the cubic filters and lanczos3 is 1.00-1.11 at
  8 threads, but 1.18-1.34 at 1 thread. At 0.12x it is 1.18-1.53 at 8 threads; at 0.08x and 1 thread it is 1.65
  (24-bit) and 1.78 (32-bit).

## 6. HEAD against `ab5ff8e`: the `qpv` history

### 6.1 GCC: no regression anywhere

GCC 15.2, whole libraries, 8 threads, time of HEAD / time of `ab5ff8e`:

| format | case | BOX | BICUBIC | BILINEAR | BSPLINE | CATMULLROM | LANCZOS3 |
|---|---|---|---|---|---|---|---|
| 24rgb | 24000x16000 → 2400x1600 | 0.47 | 0.58 | 0.55 | 0.57 | 0.56 | 0.62 |
| 24rgb | 16000x12000 → 1920x1440 | 0.45 | 0.59 | 0.54 | 0.58 | 0.59 | 0.60 |
| 24rgb | 12000x9000 → 3000x2250 | 0.40 | 0.57 | 0.57 | 0.65 | 0.55 | 0.64 |
| 24rgb | 12000x9000 → 6000x4500 | 0.43 | 0.54 | 0.52 | 0.68 | 0.56 | 0.59 |
| 24rgb | 12000x9000 → 9000x6750 | 0.49 | 0.56 | 0.53 | 0.57 | 0.55 | 0.60 |
| 24rgb | 8000x6000 → 12000x9000 | 0.57 | 0.64 | 0.58 | 0.63 | 0.61 | 0.62 |
| 24rgb | 6000x4500 → 12000x9000 | 0.61 | 0.66 | 0.64 | 0.66 | 0.65 | 0.66 |
| 32rgba | 24000x16000 → 2400x1600 | 0.53 | 0.58 | 0.54 | 0.61 | 0.63 | 0.64 |
| 32rgba | 16000x12000 → 1920x1440 | 0.50 | 0.63 | 0.54 | 0.58 | 0.61 | 0.68 |
| 32rgba | 12000x9000 → 3000x2250 | 0.42 | 0.57 | 0.52 | 0.56 | 0.55 | 0.61 |
| 32rgba | 12000x9000 → 6000x4500 | 0.49 | 0.60 | 0.53 | 0.61 | 0.59 | 0.65 |
| 32rgba | 12000x9000 → 9000x6750 | 0.48 | 0.54 | 0.51 | 0.56 | 0.58 | 0.61 |
| 32rgba | 8000x6000 → 12000x9000 | 0.54 | 0.60 | 0.59 | 0.62 | 0.61 | 0.64 |
| 32rgba | 6000x4500 → 12000x9000 | 0.61 | 0.66 | 0.64 | 0.65 | 0.66 | 0.72 |
| 8grey | 24000x16000 → 2400x1600 | 0.33 | 0.52 | 0.46 | 0.52 | 0.50 | 0.52 |
| 8grey | 16000x12000 → 1920x1440 | 0.38 | 0.56 | 0.49 | 0.55 | 0.58 | 0.58 |
| 8grey | 12000x9000 → 3000x2250 | 0.37 | 0.47 | 0.37 | 0.61 | 0.52 | 0.52 |
| 8grey | 12000x9000 → 6000x4500 | 0.28 | 0.41 | 0.34 | 0.39 | 0.43 | 0.42 |
| 8grey | 12000x9000 → 9000x6750 | 0.38 | 0.44 | 0.38 | 0.43 | 0.42 | 0.47 |
| 8grey | 8000x6000 → 12000x9000 | 0.47 | 0.47 | 0.48 | 0.46 | 0.45 | 0.49 |
| 8grey | 6000x4500 → 12000x9000 | 0.49 | 0.47 | 0.52 | 0.48 | 0.48 | 0.48 |

Geomean HEAD / `ab5ff8e` over the 126 cases: 0.534; the worst case is 0.72.

### 6.2 MSVC, every commit

Median ms, 8 threads, 16000x12000 -> 1920x1440, two rounds. Each cell reads *as built / with `/QIntel-jcc-erratum`*
(the second number takes code placement out, §7; its first round overlapped an analysis job and is left out):

| commit | date | 24-bit BOX | 24-bit Lanczos3 | 32-bit BOX | 32-bit Lanczos3 |
|---|---|---|---|---|---|
| `ab5ff8e` | 04-02 | 233 / **206** | 805 / **674** | 266 / **265** | 840 / **840** |
| `0bc96b8` | 08-29 | 234 / **208** | 802 / **674** | 267 / **264** | 839 / **842** |
| `28287d1` | 09-17 | 250 / **206** | 907 / **673** | 267 / **266** | 846 / **842** |
| `1505c3f` | 09-17 | 207 / **206** | 674 / **670** | 270 / **265** | 860 / **840** |
| `055eba2` | 09-17 | 207 / **208** | 673 / **676** | 270 / **270** | 864 / **841** |
| `71aa1b2` | 09-17 | 206 / **208** | 673 / **673** | 291 / **269** | 971 / **849** |
| `c6f6e12` | 09-17 | 209 / **206** | 674 / **672** | 291 / **264** | 948 / **837** |
| `402d7fd` | 09-17 | 234 / **209** | 805 / **672** | 265 / **265** | 840 / **846** |
| `622124a` | 09-17 | 234 / **207** | 806 / **671** | 267 / **267** | 847 / **866** |
| `52e2ca7` | 09-17 | 233 / **208** | 811 / **672** | 265 / **268** | 842 / **844** |
| `2877e63` | 09-17 | 252 / **207** | 916 / **675** | 268 / **261** | 839 / **840** |
| `cf6d13e` | 09-17 | 207 / **207** | 672 / **673** | 295 / **266** | 976 / **860** |
| `3f0e1dc` | 09-17 | 206 / **207** | 675 / **672** | 298 / **266** | 951 / **841** |
| `9988ebf` | 09-22 | 208 / **208** | 673 / **672** | 294 / **269** | 951 / **838** |
| `f3efc44` | 09-22 | 207 / **207** | 679 / **673** | 291 / **267** | 951 / **845** |
| `5d0098d` | 09-23 | 207 / **206** | 673 / **676** | 293 / **265** | 956 / **838** |
| `ddea696` | 09-23 | 209 / **207** | 673 / **674** | 271 / **284** | 863 / **842** |
| `54dec18` | 09-23 | 250 / **207** | 903 / **673** | 268 / **266** | 843 / **860** |
| `18e1a5f` | 09-23 | 207 / **206** | 673 / **673** | 296 / **264** | 951 / **841** |
| `79a9b43` | 09-23 | 205 / **196** | 661 / **659** | 249 / **249** | 818 / **819** |
| `9ee588d` | 09-23 | 140 / **140** | 610 / **612** | 172 / **170** | 753 / **747** |
| `c89c660` | 09-24 | 140 / **143** | 611 / **610** | 173 / **171** | 765 / **753** |
| `d60316b` | 09-25 | 122 / **123** | 600 / **604** | 175 / **170** | 859 / **846** |

With placement neutralised, 19 of the 22 commits stay within run-to-run noise: mostly ±1%, and up to +6% for a few single
runs (`622124a`, `ddea696`, `54dec18`) that the earlier round did not repeat. Three steps remain:

| commit | what it changed | step under MSVC (padded) |
|---|---|---|
| `79a9b43` | row-blocked vertical pass for 8/24/32-bit | -2% to -7% |
| `9ee588d` | the passes over ~4 MB bands of rows | -8% to -31% |
| `d60316b` | pass order by a tap-cost model | box -13%, 24-bit lanczos3 -1%, **32-bit lanczos3 +13%** |

The key commits again, three rounds, JCC-padded MSVC and GCC (step against the previous column):

| MSVC (padded) / GCC | ab5ff8e | 79a9b43 | 9ee588d | d60316b = HEAD | HEAD + SSE2 vertical |
|---|---|---|---|---|---|
| MSVC 24rgb box | 206 | 197 (-5%) | 141 (-29%) | 123 (-13%) | 82 |
| MSVC 24rgb lanczos3 | 672 | 658 (-2%) | 608 (-8%) | 602 (-1%) | 418 |
| MSVC 32rgba box | 266 | 248 (-7%) | 171 (-31%) | 170 (-1%) | 113 |
| MSVC 32rgba lanczos3 | 841 | 818 (-3%) | 748 (-8%) | 846 (+13%) | 571 |
| GCC 24rgb box | 169 | 145 (-14%) | 110 (-24%) | 76 (-31%) | 72 |
| GCC 24rgb lanczos3 | 604 | 553 (-8%) | 524 (-5%) | 367 (-30%) | 394 |
| GCC 32rgba box | 229 | 193 (-16%) | 145 (-25%) | 105 (-28%) | 96 |
| GCC 32rgba lanczos3 | 810 | 750 (-7%) | 707 (-6%) | 516 (-27%) | 537 |

### 6.3 Why `d60316b` is slower under MSVC

`scaleInBands` (d60316b) compares

    cost_xy = 2 * TX * src_h + 1 * TY * dst_w        (horizontal pass first)
    cost_yx = 1 * TY * src_w + 2 * TX * dst_h        (vertical pass first)

where TX and TY are the tap totals of the two weight tables. For a reduction it leaves the width rule (horizontal
first) when `cost_xy > 1.1 * cost_yx`. For a proportional reduction by s, the tap totals are TX = k * src_w and
TY = k * src_h (k = taps per unit of source, the same on both axes), so

    cost_yx / cost_xy = (1 + 2s) / (2 + s)

This is below 1/1.1 for every **s < 0.75**, whatever the filter: every plain 8/24/32-bit reduction to less than three
quarters of the size goes vertical first. Write rho for (cost of a horizontal tap) / (cost of a vertical tap) in place
of the hard-coded 2. The time ratio of the big passes is then

    t_yx / t_xy = (1 + rho * s) / (rho + s)

| rho | where | t_yx / t_xy at s = 0.08 | at s = 0.12 | at s = 0.5 |
|---|---|---|---|---|
| 2.00 | what `d60316b` assumes | 0.56 | 0.59 | 0.80 |
| 1.75 | GCC 15.2, 32-bit, measured | 0.62 | 0.65 | 0.83 |
| 1.56 | GCC 15.2, 24-bit, measured | 0.69 | 0.71 | 0.86 |
| 1.02 | MSVC 14.29, 24-bit, measured | 0.98 | 0.99 | 0.99 |
| 0.90 | MSVC 14.29, 32-bit, measured | **1.09** | **1.09** | 1.04 |
| 0.77 | what a 25% loss would take | 1.25 | 1.23 | 1.09 |

The rho values come from the per-pass table in 5.2 (template horizontal kernel against template vertical kernel, 1 thread,
JCC-padded). The model ignores the fixed costs (stores, bands, the small second pass), so it predicts only the big
pass. For 32-bit lanczos3 at 24000x18000 -> 1920x1440 (s = 0.08) it gives 1.093; the same padded MSVC binary
measures **1.098** (`FI_RESCALE_ORDER` unset against `=xy`, 1 thread).

**The two kernels, as each compiler builds them.**

* **Horizontal** (one destination pixel = a dot product along a source row). `ab5ff8e`'s hand-written 24-bit loop and HEAD's
  `HorizontalFilterSamples<BYTE,3>` compile to the same inner loop under MSVC 14.29, instruction for instruction:

      movsd  xmm2,[r9+rax*8]                                        ; the weight
      movzx  eax,byte [rcx+2] / movd xmm0,eax / cvtdq2pd xmm0,xmm0  ; x3 channels
      mulsd  xmm0,xmm2 / addsd xmm3,xmm0                            ; x3, sums in xmm3..xmm5
      add rcx,3 / cmp rdx,r10 / jl

  The template's `double value[SPP]` stays in registers. The only difference is the per-pixel store: HEAD's `RoundSample`
  takes three `comisd` and two branches per sample, against a `cvttsd2si` and a `cmovg`.
* **Vertical** (`VerticalFilterSamples<BYTE,SPP>`, from `79a9b43`): for each destination row, for each block of 256
  samples, for each tap, `value[k] += w * (double)src[k]`.
  * GCC 15.2 `-O3` vectorises it 16 samples at a time from one 16-byte load (`movdqu`, `punpck{l,h}bw/wd`, `cvtdq2pd`,
    `pshufd`) **and unrolls two taps per pass**, so the 256 sums are loaded and stored once per two taps: about
    **3.1 instructions per sample-tap**.
  * MSVC 14.29 `/Ox` vectorises it 2 samples at a time from a **2-byte load** (`movzx eax,word`, `movd`, `punpcklbw`,
    `punpcklwd`, `cvtdq2pd`) and **loads and stores the sums on every tap** (`movups`, `mulpd`, `addpd`, `movups`):
    about **5.5 instructions per sample-tap**.

The per-pass table in 5.2 gives the costs these produce, at 1 thread and placement-neutral. For HEAD's two template
kernels (horizontal / vertical):
* GCC: 0.56 / 0.36 ns at 24 bits, 0.63 / 0.36 at 32, so **rho = 1.56 and 1.75**;
* MSVC: 0.61 / 0.60 ns at 24 bits, 0.55 / 0.61 at 32, so **rho = 1.02 and 0.90**.

`d60316b` hard-codes rho = 2.

Other measurements agree:
* Within one binary, forcing the order changes only the order. HEAD's MSVC build with `FI_RESCALE_ORDER=xy` against
  its own rule takes 0.88-0.98x the time on 32-bit lanczos3 at every size from 8000x6000 to 32000x24000 (8 threads).
* GCC is the opposite: `d60316b` makes GCC builds 1.37-1.45x faster on the same cases (table above).
* The commits' own messages say "MSVC untested", and the fit was made on 4000x3000 sources at 1 and 6 threads.

## 7. Code placement: the JCC erratum

Intel's 2019 microcode for Skylake-derived cores (6th-10th gen Core, this i7-7700T included) stops caching decoded
instructions for any 32-byte block where a jump crosses or ends on the boundary. A hot loop whose closing
`cmp`/`jl` sits there runs from the legacy decoders. MSVC does not pad for this unless given `/QIntel-jcc-erratum`;
GCC's 16-byte loop alignment happened to keep every GCC kernel loop here clear.

The 24-bit horizontal tap loop of `ab5ff8e`'s kernel is 79 bytes. Where it starts decides everything:

| build | loop start mod 32 | closing `cmp`/`jl` | 24-bit lanczos3, as built | with `/QIntel-jcc-erratum` |
|---|---|---|---|---|
| `ab5ff8e`, `0bc96b8`, `402d7fd` | 17 | ends on a boundary | 802-805 ms | 672-674 ms |
| `28287d1`, `2877e63`, `54dec18` | 17 | ends on a boundary | 903-916 ms | 673-675 ms |
| `1505c3f`, `cf6d13e`, `18e1a5f` | 1 | clear | 672-674 ms | 670-673 ms |
| `79a9b43` (other kernel) | 16 | clear | 661 ms | 659 ms |
| QPV `FreeImage.dll`, 22 Sep | 17 | ends on a boundary | **900 ms** | - |

(The 903-916 builds have a second hot loop in a bad place.) So QPV's shipped 22 Sep DLL is 34% slower than the same
kernel placed clear, on every Intel 6th-10th gen machine. Between two separately linked MSVC builds on such a CPU, a
25% difference proves nothing about the code. Your 11th-gen+ machine does not have the erratum, but this explains
why comparisons made elsewhere can disagree.

## 8. The fix

### 8.1 What was committed

* **`1eb0175`**: `HorizontalFilterBytes<SPP>` for 8-bit greyscale, 24-bit and 32-bit, `274f833`'s kernel adapted to the
  bands' row ranges, and `CWeightsTable::getWindowSize()`. It compiles where SSE2 is (`_M_X64`, `_M_AMD64`,
  `__x86_64__`, `__SSE2__`, `_M_IX86_FP >= 2`). Elsewhere, and for windows wider than its 512-pixel tile,
  `HorizontalFilterSamples` runs as before.
* **`06d69d9`**: `VerticalStrips` for 8-bit samples, compiled for MSVC only. GCC builds keep their block loop, which GCC
  vectorises better (0.36 against 0.48 ns per sample-tap).
* The pass-order rule is unchanged.

Gates on the committed code:
* The resize oracle: 485,333 checks, 0 failures, at 1 and 8 threads. Under GCC it gives HEAD's hash `5951cf7c6828346f`;
  its MSVC build gives HEAD's MSVC hash `c3349ec02ebc32b6` (the two CRTs' `sin()` differ in the last bit, which moves the
  lanczos3 weights).
* The 12,252-signature differential is identical to HEAD's, also with bands of 1 and 3 rows.
* ASan and UBSan are clean, 54 MSVC output hashes equal HEAD's, and aarch64 and x86 compile with HEAD's warnings.

Against your reference, MSVC with `/QIntel-jcc-erratum`, 8 threads, 16000x12000 -> 1920x1440, median ms:

| filter | QPV 29 Aug DLL | rework 274f833 | HEAD before (d60316b) | HEAD now (06d69d9) | now / 29 Aug DLL | now / before |
|---|---|---|---|---|---|---|
| 24rgb box | 204 | 186 | 123 | 81 | **0.40** | 0.66 |
| 24rgb bicubic | 340 | 337 | 400 | 271 | **0.80** | 0.68 |
| 24rgb bilinear | 235 | 231 | 216 | 138 | **0.59** | 0.64 |
| 24rgb b-spline | 336 | 340 | 407 | 263 | **0.78** | 0.65 |
| 24rgb catmull-rom | 337 | 342 | 416 | 260 | **0.77** | 0.62 |
| 24rgb lanczos3 | 443 | 439 | 606 | 391 | **0.88** | 0.65 |
| 32rgba box | 241 | 241 | 169 | 114 | **0.47** | 0.67 |
| 32rgba bicubic | 436 | 437 | 574 | 357 | **0.82** | 0.62 |
| 32rgba bilinear | 307 | 304 | 302 | 193 | **0.63** | 0.64 |
| 32rgba b-spline | 433 | 428 | 573 | 362 | **0.84** | 0.63 |
| 32rgba catmull-rom | 438 | 443 | 570 | 360 | **0.82** | 0.63 |
| 32rgba lanczos3 | 551 | 555 | 845 | 538 | **0.98** | 0.64 |

The committed code against HEAD before it, 8 threads, median ms ("+1" = `1eb0175`, "+1+2" = `06d69d9`; one round,
so single GCC cells can be off by 10-20%: the strong-reduction box and bilinear cases were rerun three times and gave GCC 0.94-0.99,
MSVC 0.97-1.01 for "+1"):

| case | format | filter | MSVC HEAD | MSVC +1 | MSVC +1+2 | +1/HEAD | +1+2/HEAD | GCC HEAD | GCC +1 | GCC +1+2 | +1/HEAD | +1+2/+1 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 12000x9000 -> 10000x7500 | 24rgb | box | 410 | 355 | 312 | 0.86 | 0.76 | 340 | 313 | 308 | 0.92 | 0.99 |
| 12000x9000 -> 10000x7500 | 24rgb | bilinear | 497 | 411 | 366 | 0.83 | 0.74 | 383 | 364 | 360 | 0.95 | 0.99 |
| 12000x9000 -> 10000x7500 | 24rgb | lanczos3 | 838 | 656 | 556 | 0.78 | 0.66 | 643 | 562 | 546 | 0.87 | 0.97 |
| 12000x9000 -> 10000x7500 | 32rgba | box | 512 | 444 | 400 | 0.87 | 0.78 | 456 | 384 | 391 | 0.84 | 1.02 |
| 12000x9000 -> 10000x7500 | 32rgba | bilinear | 599 | 531 | 458 | 0.89 | 0.76 | 531 | 445 | 443 | 0.84 | 1.00 |
| 12000x9000 -> 10000x7500 | 32rgba | lanczos3 | 1069 | 872 | 717 | 0.82 | 0.67 | 868 | 681 | 682 | 0.78 | 1.00 |
| 12000x9000 -> 10000x7500 | 8grey | box | 175 | 157 | 146 | 0.90 | 0.84 | 136 | 129 | 131 | 0.95 | 1.02 |
| 12000x9000 -> 10000x7500 | 8grey | bilinear | 203 | 182 | 164 | 0.89 | 0.81 | 154 | 146 | 146 | 0.95 | 1.00 |
| 12000x9000 -> 10000x7500 | 8grey | lanczos3 | 328 | 282 | 241 | 0.86 | 0.73 | 255 | 218 | 219 | 0.85 | 1.00 |
| 16000x12000 -> 1920x1440 | 24rgb | box | 124 | 123 | 85 | 0.99 | 0.68 | 73 | 83 | 74 | 1.13 | 0.90 |
| 16000x12000 -> 1920x1440 | 24rgb | bilinear | 220 | 208 | 137 | 0.94 | 0.62 | 132 | 128 | 124 | 0.97 | 0.97 |
| 16000x12000 -> 1920x1440 | 24rgb | lanczos3 | 602 | 578 | 397 | 0.96 | 0.66 | 369 | 347 | 345 | 0.94 | 0.99 |
| 16000x12000 -> 1920x1440 | 32rgba | box | 169 | 178 | 118 | 1.05 | 0.70 | 102 | 101 | 100 | 1.00 | 0.99 |
| 16000x12000 -> 1920x1440 | 32rgba | bilinear | 301 | 296 | 193 | 0.98 | 0.64 | 180 | 211 | 170 | 1.17 | 0.81 |
| 16000x12000 -> 1920x1440 | 32rgba | lanczos3 | 845 | 815 | 541 | 0.96 | 0.64 | 508 | 476 | 477 | 0.94 | 1.00 |
| 16000x12000 -> 1920x1440 | 8grey | box | 44 | 46 | 32 | 1.04 | 0.72 | 26 | 30 | 26 | 1.13 | 0.87 |
| 16000x12000 -> 1920x1440 | 8grey | bilinear | 75 | 74 | 51 | 0.98 | 0.68 | 47 | 47 | 51 | 1.00 | 1.10 |
| 16000x12000 -> 1920x1440 | 8grey | lanczos3 | 212 | 204 | 143 | 0.96 | 0.67 | 127 | 127 | 127 | 1.00 | 1.00 |
| 6000x4500 -> 12000x9000 | 24rgb | box | 390 | 350 | 303 | 0.90 | 0.78 | 338 | 322 | 318 | 0.95 | 0.99 |
| 6000x4500 -> 12000x9000 | 24rgb | bilinear | 453 | 403 | 339 | 0.89 | 0.75 | 372 | 360 | 359 | 0.97 | 1.00 |
| 6000x4500 -> 12000x9000 | 24rgb | lanczos3 | 745 | 636 | 489 | 0.85 | 0.66 | 560 | 515 | 505 | 0.92 | 0.98 |
| 6000x4500 -> 12000x9000 | 32rgba | box | 484 | 443 | 377 | 0.91 | 0.78 | 447 | 407 | 405 | 0.91 | 0.99 |
| 6000x4500 -> 12000x9000 | 32rgba | bilinear | 567 | 515 | 423 | 0.91 | 0.75 | 523 | 454 | 454 | 0.87 | 1.00 |
| 6000x4500 -> 12000x9000 | 32rgba | lanczos3 | 952 | 842 | 634 | 0.88 | 0.67 | 767 | 657 | 653 | 0.86 | 1.00 |
| 6000x4500 -> 12000x9000 | 8grey | box | 174 | 161 | 144 | 0.93 | 0.83 | 122 | 123 | 122 | 1.01 | 0.99 |
| 6000x4500 -> 12000x9000 | 8grey | bilinear | 198 | 181 | 157 | 0.91 | 0.79 | 139 | 132 | 146 | 0.95 | 1.10 |
| 6000x4500 -> 12000x9000 | 8grey | lanczos3 | 296 | 270 | 222 | 0.91 | 0.75 | 209 | 198 | 194 | 0.94 | 0.98 |

12000x9000 -> 10000x7500: MSVC +1/HEAD 0.85 (0.78-0.90), +1+2/HEAD 0.75 (0.66-0.84); GCC +1/HEAD 0.88 (0.78-0.95), +2/+1 1.00 (0.97-1.02)
16000x12000 -> 1920x1440: MSVC +1/HEAD 0.98 (0.94-1.05), +1+2/HEAD 0.67 (0.62-0.72); GCC +1/HEAD 1.03 (0.94-1.17), +2/+1 0.95 (0.81-1.10)
6000x4500 -> 12000x9000: MSVC +1/HEAD 0.90 (0.85-0.93), +1+2/HEAD 0.75 (0.66-0.83); GCC +1/HEAD 0.93 (0.86-1.01), +2/+1 1.00 (0.98-1.10)

### 8.2 The prototype, and the pass order

The prototype had the same two kernels, the vertical one for every compiler:

* **`274f833`'s horizontal kernel**, ported to HEAD's banded row ranges for 8-bit, 24-bit and 32-bit (`HorizontalFilterBytes<C>`;
  `CWeightsTable` gains `getWindowSize()`; windows wider than its 512-pixel tile fall back to the template).
* **An SSE2 vertical kernel**: 16 samples at a time, their sums in registers through all taps (`VerticalStrips`).

It passed the same oracle and 108 output hashes (GCC, MSVC, padded or not, rule/xy/yx forced), and it is what
the forced orders below were measured with. Its patch (`.claude/scratch/resize-regress/resize-sse2-kernels.patch`)
is superseded by the two commits.

MSVC (padded) and GCC, 8 threads, 16000x12000 -> 1920x1440, median ms:

| filter | rework (274f833) | HEAD | HEAD + SSE2 vertical | HEAD + both, rule order | HEAD + both, horizontal first | best / rework | GCC HEAD | GCC HEAD + both, best |
|---|---|---|---|---|---|---|---|---|
| 24rgb box | 186 | 127 | 86 | 83 | 123 | **0.45** | 74 | 72 |
| 24rgb bicubic | 343 | 404 | 277 | 263 | 270 | **0.77** | 245 | 221 |
| 24rgb bilinear | 235 | 212 | 145 | 136 | 167 | **0.58** | 130 | 127 |
| 24rgb b-spline | 341 | 403 | 299 | 261 | 261 | **0.76** | 249 | 224 |
| 24rgb catmull-rom | 348 | 403 | 272 | 262 | 264 | **0.75** | 250 | 229 |
| 24rgb lanczos3 | 443 | 604 | 422 | 396 | 352 | **0.80** | 370 | 317 |
| 32rgba box | 242 | 170 | 122 | 115 | 156 | **0.48** | 101 | 95 |
| 32rgba bicubic | 428 | 570 | 381 | 358 | 335 | **0.78** | 344 | 321 |
| 32rgba bilinear | 312 | 306 | 197 | 197 | 211 | **0.63** | 185 | 180 |
| 32rgba b-spline | 431 | 613 | 390 | 362 | 334 | **0.78** | 353 | 315 |
| 32rgba catmull-rom | 439 | 575 | 380 | 361 | 331 | **0.75** | 350 | 318 |
| 32rgba lanczos3 | 560 | 850 | 578 | 540 | 443 | **0.79** | 518 | 454 |

* With the better order per filter, HEAD with both kernels takes **0.45-0.80x the time of your 29 Aug DLL** under MSVC. The
  better order is vertical first for box and bilinear (few taps; the SSE2 vertical kernel wins) and horizontal first
  for the cubic filters and lanczos3 (the rework's kernel wins). HEAD's current rule sends all of them vertical first,
  so it gets 0.45-0.96x. **The rule has to be refitted to these kernels**, with a per-sample term for the tile transposition,
  and measured under MSVC as well as GCC.
* Under GCC the SSE2 vertical kernel is not a gain on its own: 0.48 against 0.36 ns per sample-tap at 1 thread, because
  GCC already unrolls two taps and converts 16 bytes per load, so `06d69d9` compiles it for MSVC only; unrolled the
  way GCC does, it could serve both compilers.
* If you want the least risk instead: keep the width rule for 8/24/32-bit under MSVC (horizontal first on every
  reduction). With `274f833`'s kernel that is the "horizontal first" column; box and bilinear then give up 7-48%
  against the better order.

## 9. The rescale-bench matrix, rerun

The earlier report's matrix (`rescale-bench/bench.cpp`): 19 formats x 7 filters, 4000x3000 -> 1600x1200 (down) and
1600x1200 -> 4000x3000 (up), at least 5 calls and 500 ms per case, one round, with the variants interleaved. `ab5ff8e`
and the rework returned a blank image for FIT_INT16, FIT_UINT32, FIT_INT32, FIT_DOUBLE and FIT_COMPLEX (resampled only
since `54dec18`), and had no FILTER_NEAREST, so those cases are left out: 14 formats x 6 filters remain. MSVC means the
JCC-padded builds.

| threads | direction | cases | GCC HEAD / ab5ff8e | MSVC HEAD / ab5ff8e | MSVC HEAD / rework | worst MSVC HEAD / ab5ff8e | worst MSVC HEAD / rework |
|---|---|---|---|---|---|---|---|
| 1 | down | 84 | 0.51 (max 0.82) | 0.60 | 0.82 | 0.93 (32rgba LANCZOS3) | 1.34 (32rgba LANCZOS3) |
| 1 | up | 84 | 0.56 (max 0.72) | 0.67 | 0.86 | 1.01 (32rgba LANCZOS3) | 1.30 (24rgb LANCZOS3) |
| 8 | down | 84 | 0.50 (max 0.80) | 0.60 | 0.71 | 0.97 (4bit LANCZOS3) | 1.12 (4bit LANCZOS3) |
| 8 | up | 84 | 0.66 (max 0.80) | 0.77 | 0.87 | 0.97 (1bit LANCZOS3) | 1.11 (24rgb LANCZOS3) |

<details><summary>1 thread, 4000x3000 -> 1600x1200 (ms, median)</summary>

| format | filter | GCC ab5ff8e | GCC HEAD | MSVC ab5ff8e | MSVC rework | MSVC HEAD | HEAD/ab5ff8e (MSVC) | HEAD/rework (MSVC) |
|---|---|---|---|---|---|---|---|---|
| 1bit | NEAREST | - | 2.4 | - | - | 2.9 | - | - |
| 1bit | BOX | 48.8 | 33.8 | 47.2 | 33.5 | 35.5 | 0.75 | 1.06 |
| 1bit | BICUBIC | 118.4 | 88.3 | 116.7 | 95.7 | 98.9 | 0.85 | 1.03 |
| 1bit | BILINEAR | 73.6 | 50.8 | 69.9 | 51.5 | 54.9 | 0.79 | 1.07 |
| 1bit | BSPLINE | 120.8 | 88.4 | 114.4 | 94.3 | 97.6 | 0.85 | 1.04 |
| 1bit | CATMULLROM | 120.8 | 88.3 | 116.7 | 96.2 | 99.2 | 0.85 | 1.03 |
| 1bit | LANCZOS3 | 169.9 | 127.5 | 157.0 | 134.7 | 142.5 | 0.91 | 1.06 |
| 4bit | NEAREST | - | 4.1 | - | - | 4.9 | - | - |
| 4bit | BOX | 46.3 | 33.7 | 42.9 | 30.9 | 35.1 | 0.82 | 1.14 |
| 4bit | BICUBIC | 104.6 | 83.3 | 105.5 | 85.1 | 90.0 | 0.85 | 1.06 |
| 4bit | BILINEAR | 65.8 | 49.7 | 63.8 | 46.9 | 52.5 | 0.82 | 1.12 |
| 4bit | BSPLINE | 104.8 | 83.5 | 104.1 | 85.1 | 90.0 | 0.86 | 1.06 |
| 4bit | CATMULLROM | 104.2 | 83.3 | 105.2 | 85.4 | 89.9 | 0.85 | 1.05 |
| 4bit | LANCZOS3 | 145.0 | 118.4 | 146.4 | 124.1 | 127.5 | 0.87 | 1.03 |
| 8grey | NEAREST | - | 1.6 | - | - | 2.1 | - | - |
| 8grey | BOX | 38.3 | 16.6 | 38.3 | 27.9 | 23.7 | 0.62 | 0.85 |
| 8grey | BICUBIC | 80.3 | 37.9 | 80.7 | 50.5 | 54.7 | 0.68 | 1.08 |
| 8grey | BILINEAR | 53.4 | 23.8 | 55.4 | 35.1 | 34.5 | 0.62 | 0.98 |
| 8grey | BSPLINE | 80.6 | 37.7 | 81.2 | 50.5 | 54.7 | 0.67 | 1.08 |
| 8grey | CATMULLROM | 79.8 | 37.9 | 81.4 | 50.4 | 54.7 | 0.67 | 1.08 |
| 8grey | LANCZOS3 | 107.7 | 54.7 | 108.5 | 68.8 | 76.3 | 0.70 | 1.11 |
| 8pal | NEAREST | - | 1.6 | - | - | 2.1 | - | - |
| 8pal | BOX | 93.5 | 57.9 | 99.0 | 69.4 | 64.3 | 0.65 | 0.93 |
| 8pal | BICUBIC | 182.1 | 136.1 | 185.0 | 152.7 | 147.3 | 0.80 | 0.96 |
| 8pal | BILINEAR | 123.1 | 85.5 | 125.1 | 95.7 | 91.0 | 0.73 | 0.95 |
| 8pal | BSPLINE | 182.3 | 135.5 | 185.4 | 152.5 | 147.5 | 0.80 | 0.97 |
| 8pal | CATMULLROM | 182.4 | 131.3 | 180.0 | 152.1 | 148.1 | 0.82 | 0.97 |
| 8pal | LANCZOS3 | 247.7 | 188.5 | 243.8 | 209.8 | 204.9 | 0.84 | 0.98 |
| 16-565 | NEAREST | - | 1.8 | - | - | 3.2 | - | - |
| 16-565 | BOX | 100.2 | 68.6 | 109.9 | 80.1 | 75.0 | 0.68 | 0.94 |
| 16-565 | BICUBIC | 177.5 | 133.5 | 216.2 | 183.4 | 178.7 | 0.83 | 0.97 |
| 16-565 | BILINEAR | 129.1 | 93.1 | 149.4 | 115.2 | 110.9 | 0.74 | 0.96 |
| 16-565 | BSPLINE | 177.9 | 132.8 | 216.7 | 183.0 | 177.8 | 0.82 | 0.97 |
| 16-565 | CATMULLROM | 178.3 | 132.6 | 216.0 | 177.8 | 178.4 | 0.83 | 1.00 |
| 16-565 | LANCZOS3 | 240.9 | 183.7 | 286.8 | 252.1 | 246.5 | 0.86 | 0.98 |
| 16-555 | NEAREST | - | 1.8 | - | - | 3.2 | - | - |
| 16-555 | BOX | 102.7 | 68.8 | 111.1 | 81.1 | 76.1 | 0.69 | 0.94 |
| 16-555 | BICUBIC | 181.7 | 134.8 | 222.3 | 188.0 | 183.9 | 0.83 | 0.98 |
| 16-555 | BILINEAR | 132.8 | 95.2 | 151.8 | 117.6 | 113.4 | 0.75 | 0.96 |
| 16-555 | BSPLINE | 182.1 | 134.6 | 220.6 | 182.4 | 183.8 | 0.83 | 1.01 |
| 16-555 | CATMULLROM | 182.7 | 134.1 | 221.7 | 187.9 | 183.5 | 0.83 | 0.98 |
| 16-555 | LANCZOS3 | 237.4 | 185.7 | 293.9 | 259.4 | 255.2 | 0.87 | 0.98 |
| 24rgb | NEAREST | - | 2.4 | - | - | 4.6 | - | - |
| 24rgb | BOX | 88.5 | 41.7 | 96.9 | 66.2 | 59.2 | 0.61 | 0.89 |
| 24rgb | BICUBIC | 163.0 | 97.5 | 180.3 | 116.4 | 146.3 | 0.81 | 1.26 |
| 24rgb | BILINEAR | 114.5 | 60.0 | 126.6 | 82.6 | 89.6 | 0.71 | 1.08 |
| 24rgb | BSPLINE | 168.1 | 97.5 | 180.5 | 116.4 | 146.5 | 0.81 | 1.26 |
| 24rgb | CATMULLROM | 168.5 | 97.2 | 181.7 | 116.5 | 146.4 | 0.81 | 1.26 |
| 24rgb | LANCZOS3 | 226.7 | 136.5 | 234.4 | 150.5 | 199.9 | 0.85 | 1.33 |
| 32rgba | NEAREST | - | 2.4 | - | - | 5.3 | - | - |
| 32rgba | BOX | 122.3 | 52.8 | 111.4 | 83.1 | 75.3 | 0.68 | 0.91 |
| 32rgba | BICUBIC | 214.7 | 127.1 | 209.6 | 149.3 | 183.5 | 0.88 | 1.23 |
| 32rgba | BILINEAR | 153.2 | 78.0 | 143.3 | 107.0 | 110.8 | 0.77 | 1.04 |
| 32rgba | BSPLINE | 214.8 | 127.8 | 209.6 | 149.5 | 183.9 | 0.88 | 1.23 |
| 32rgba | CATMULLROM | 213.8 | 127.6 | 209.9 | 149.6 | 183.5 | 0.87 | 1.23 |
| 32rgba | LANCZOS3 | 281.0 | 178.2 | 278.3 | 193.1 | 257.8 | 0.93 | 1.34 |
| uint16 | NEAREST | - | 1.8 | - | - | 3.1 | - | - |
| uint16 | BOX | 42.9 | 15.2 | 48.7 | 30.4 | 23.3 | 0.48 | 0.76 |
| uint16 | BICUBIC | 75.5 | 34.5 | 94.5 | 66.4 | 47.0 | 0.50 | 0.71 |
| uint16 | BILINEAR | 53.2 | 21.0 | 66.1 | 44.1 | 31.5 | 0.48 | 0.71 |
| uint16 | BSPLINE | 75.4 | 34.3 | 94.3 | 64.6 | 47.0 | 0.50 | 0.73 |
| uint16 | CATMULLROM | 73.9 | 34.2 | 94.4 | 66.5 | 47.0 | 0.50 | 0.71 |
| uint16 | LANCZOS3 | 103.3 | 49.2 | 121.1 | 90.1 | 64.3 | 0.53 | 0.71 |
| float | NEAREST | - | 2.4 | - | - | 5.3 | - | - |
| float | BOX | 68.0 | 12.1 | 78.7 | 48.8 | 19.1 | 0.24 | 0.39 |
| float | BICUBIC | 129.1 | 30.6 | 174.2 | 122.9 | 39.8 | 0.23 | 0.32 |
| float | BILINEAR | 89.1 | 19.0 | 106.6 | 70.8 | 26.1 | 0.24 | 0.37 |
| float | BSPLINE | 129.3 | 30.7 | 179.7 | 123.3 | 39.8 | 0.22 | 0.32 |
| float | CATMULLROM | 129.0 | 30.8 | 180.7 | 122.9 | 39.8 | 0.22 | 0.32 |
| float | LANCZOS3 | 181.5 | 43.7 | 248.7 | 176.4 | 54.8 | 0.22 | 0.31 |
| rgb16 | NEAREST | - | 3.4 | - | - | 7.8 | - | - |
| rgb16 | BOX | 86.1 | 37.3 | 112.2 | 74.3 | 58.6 | 0.52 | 0.79 |
| rgb16 | BICUBIC | 161.0 | 90.7 | 195.0 | 148.2 | 122.4 | 0.63 | 0.83 |
| rgb16 | BILINEAR | 111.2 | 54.2 | 139.8 | 98.8 | 80.5 | 0.58 | 0.81 |
| rgb16 | BSPLINE | 160.8 | 90.3 | 195.9 | 148.4 | 122.1 | 0.62 | 0.82 |
| rgb16 | CATMULLROM | 159.9 | 88.0 | 195.0 | 148.5 | 122.0 | 0.63 | 0.82 |
| rgb16 | LANCZOS3 | 214.9 | 122.3 | 253.7 | 199.7 | 164.4 | 0.65 | 0.82 |
| rgba16 | NEAREST | - | 3.8 | - | - | 9.8 | - | - |
| rgba16 | BOX | 147.4 | 52.5 | 132.7 | 87.7 | 71.6 | 0.54 | 0.82 |
| rgba16 | BICUBIC | 338.9 | 137.0 | 227.9 | 177.2 | 150.5 | 0.66 | 0.85 |
| rgba16 | BILINEAR | 183.4 | 78.7 | 162.8 | 116.9 | 97.5 | 0.60 | 0.83 |
| rgba16 | BSPLINE | 339.8 | 137.7 | 227.3 | 177.6 | 150.9 | 0.66 | 0.85 |
| rgba16 | CATMULLROM | 341.1 | 137.5 | 227.9 | 173.0 | 149.7 | 0.66 | 0.86 |
| rgba16 | LANCZOS3 | 388.0 | 185.3 | 295.5 | 238.7 | 204.6 | 0.69 | 0.86 |
| rgbf | NEAREST | - | 5.5 | - | - | 14.6 | - | - |
| rgbf | BOX | 113.7 | 28.2 | 133.9 | 101.6 | 46.1 | 0.34 | 0.45 |
| rgbf | BICUBIC | 213.3 | 84.1 | 325.5 | 251.5 | 98.8 | 0.30 | 0.39 |
| rgbf | BILINEAR | 146.5 | 49.8 | 189.8 | 139.3 | 63.6 | 0.33 | 0.46 |
| rgbf | BSPLINE | 214.4 | 83.6 | 326.3 | 251.4 | 95.6 | 0.29 | 0.38 |
| rgbf | CATMULLROM | 213.0 | 83.1 | 323.6 | 251.4 | 98.6 | 0.30 | 0.39 |
| rgbf | LANCZOS3 | 287.4 | 115.7 | 384.9 | 294.4 | 134.9 | 0.35 | 0.46 |
| rgbaf | NEAREST | - | 6.8 | - | - | 18.9 | - | - |
| rgbaf | BOX | 132.0 | 38.3 | 169.4 | 121.6 | 65.9 | 0.39 | 0.54 |
| rgbaf | BICUBIC | 221.9 | 94.6 | 332.0 | 253.1 | 146.0 | 0.44 | 0.58 |
| rgbaf | BILINEAR | 154.4 | 58.1 | 229.2 | 165.7 | 92.0 | 0.40 | 0.56 |
| rgbaf | BSPLINE | 232.9 | 95.8 | 328.3 | 243.6 | 145.7 | 0.44 | 0.60 |
| rgbaf | CATMULLROM | 232.8 | 95.3 | 329.8 | 251.8 | 145.5 | 0.44 | 0.58 |
| rgbaf | LANCZOS3 | 322.8 | 147.6 | 445.7 | 339.4 | 200.4 | 0.45 | 0.59 |

</details>

<details><summary>1 thread, 1600x1200 -> 4000x3000 (ms, median)</summary>

| format | filter | GCC ab5ff8e | GCC HEAD | MSVC ab5ff8e | MSVC rework | MSVC HEAD | HEAD/ab5ff8e (MSVC) | HEAD/rework (MSVC) |
|---|---|---|---|---|---|---|---|---|
| 1bit | NEAREST | - | 6.1 | - | - | 8.0 | - | - |
| 1bit | BOX | 56.5 | 33.1 | 61.2 | 53.1 | 41.7 | 0.68 | 0.79 |
| 1bit | BICUBIC | 91.0 | 63.5 | 103.9 | 88.7 | 83.3 | 0.80 | 0.94 |
| 1bit | BILINEAR | 67.5 | 44.8 | 74.1 | 66.0 | 56.2 | 0.76 | 0.85 |
| 1bit | BSPLINE | 91.4 | 64.2 | 100.8 | 87.7 | 82.7 | 0.82 | 0.94 |
| 1bit | CATMULLROM | 91.0 | 64.1 | 104.1 | 89.8 | 82.8 | 0.80 | 0.92 |
| 1bit | LANCZOS3 | 119.7 | 86.4 | 138.1 | 112.2 | 113.9 | 0.83 | 1.02 |
| 4bit | NEAREST | - | 10.5 | - | - | 13.9 | - | - |
| 4bit | BOX | 65.3 | 33.3 | 67.6 | 52.6 | 41.6 | 0.62 | 0.79 |
| 4bit | BICUBIC | 102.7 | 61.3 | 109.4 | 84.6 | 81.4 | 0.74 | 0.96 |
| 4bit | BILINEAR | 76.5 | 45.0 | 81.0 | 63.7 | 56.7 | 0.70 | 0.89 |
| 4bit | BSPLINE | 102.5 | 63.3 | 109.4 | 84.8 | 81.4 | 0.74 | 0.96 |
| 4bit | CATMULLROM | 103.0 | 63.5 | 106.2 | 84.6 | 79.3 | 0.75 | 0.94 |
| 4bit | LANCZOS3 | 130.5 | 85.2 | 141.8 | 105.4 | 109.0 | 0.77 | 1.03 |
| 8grey | NEAREST | - | 4.4 | - | - | 8.4 | - | - |
| 8grey | BOX | 64.8 | 29.4 | 65.7 | 49.0 | 42.0 | 0.64 | 0.86 |
| 8grey | BICUBIC | 100.8 | 51.8 | 103.2 | 71.9 | 75.6 | 0.73 | 1.05 |
| 8grey | BILINEAR | 76.6 | 37.1 | 78.5 | 57.7 | 53.9 | 0.69 | 0.93 |
| 8grey | BSPLINE | 101.1 | 51.4 | 103.3 | 72.1 | 74.6 | 0.72 | 1.04 |
| 8grey | CATMULLROM | 101.8 | 51.2 | 103.1 | 71.9 | 75.4 | 0.73 | 1.05 |
| 8grey | LANCZOS3 | 130.6 | 66.1 | 130.7 | 84.6 | 96.7 | 0.74 | 1.14 |
| 8pal | NEAREST | - | 4.4 | - | - | 8.4 | - | - |
| 8pal | BOX | 158.8 | 94.7 | 152.8 | 112.7 | 100.8 | 0.66 | 0.89 |
| 8pal | BICUBIC | 223.0 | 152.8 | 229.4 | 163.9 | 187.0 | 0.81 | 1.14 |
| 8pal | BILINEAR | 180.3 | 114.7 | 180.0 | 130.6 | 131.9 | 0.73 | 1.01 |
| 8pal | BSPLINE | 217.4 | 152.8 | 227.4 | 163.5 | 187.1 | 0.82 | 1.14 |
| 8pal | CATMULLROM | 223.9 | 152.5 | 230.5 | 166.4 | 189.5 | 0.82 | 1.14 |
| 8pal | LANCZOS3 | 275.6 | 196.2 | 288.5 | 201.8 | 249.4 | 0.86 | 1.24 |
| 16-565 | NEAREST | - | 5.4 | - | - | 14.1 | - | - |
| 16-565 | BOX | 174.0 | 102.0 | 165.9 | 130.3 | 108.5 | 0.65 | 0.83 |
| 16-565 | BICUBIC | 250.6 | 164.0 | 244.8 | 199.9 | 203.4 | 0.83 | 1.02 |
| 16-565 | BILINEAR | 197.3 | 121.7 | 193.4 | 154.0 | 139.8 | 0.72 | 0.91 |
| 16-565 | BSPLINE | 250.5 | 162.3 | 254.0 | 200.2 | 203.6 | 0.80 | 1.02 |
| 16-565 | CATMULLROM | 249.4 | 163.1 | 253.4 | 193.7 | 203.5 | 0.80 | 1.05 |
| 16-565 | LANCZOS3 | 309.3 | 205.8 | 318.9 | 246.4 | 265.1 | 0.83 | 1.08 |
| 16-555 | NEAREST | - | 5.3 | - | - | 14.3 | - | - |
| 16-555 | BOX | 174.2 | 103.5 | 167.0 | 130.2 | 111.9 | 0.67 | 0.86 |
| 16-555 | BICUBIC | 249.2 | 163.6 | 254.5 | 199.6 | 205.7 | 0.81 | 1.03 |
| 16-555 | BILINEAR | 197.1 | 120.8 | 195.8 | 153.5 | 140.4 | 0.72 | 0.91 |
| 16-555 | BSPLINE | 249.8 | 163.5 | 255.4 | 201.1 | 206.7 | 0.81 | 1.03 |
| 16-555 | CATMULLROM | 250.0 | 158.4 | 256.2 | 199.5 | 205.2 | 0.80 | 1.03 |
| 16-555 | LANCZOS3 | 310.8 | 204.9 | 321.6 | 245.5 | 268.3 | 0.83 | 1.09 |
| 24rgb | NEAREST | - | 22.3 | - | - | 20.9 | - | - |
| 24rgb | BOX | 162.8 | 89.6 | 160.0 | 113.5 | 112.2 | 0.70 | 0.99 |
| 24rgb | BICUBIC | 223.0 | 147.1 | 228.8 | 164.4 | 200.2 | 0.88 | 1.22 |
| 24rgb | BILINEAR | 184.3 | 108.7 | 182.8 | 126.7 | 142.0 | 0.78 | 1.12 |
| 24rgb | BSPLINE | 225.1 | 146.8 | 229.0 | 164.2 | 193.9 | 0.85 | 1.18 |
| 24rgb | CATMULLROM | 224.7 | 146.9 | 229.2 | 164.2 | 199.8 | 0.87 | 1.22 |
| 24rgb | LANCZOS3 | 280.3 | 185.1 | 285.8 | 199.0 | 258.4 | 0.90 | 1.30 |
| 32rgba | NEAREST | - | 27.6 | - | - | 25.5 | - | - |
| 32rgba | BOX | 211.4 | 113.7 | 179.5 | 148.6 | 146.7 | 0.82 | 0.99 |
| 32rgba | BICUBIC | 306.1 | 190.0 | 258.5 | 210.5 | 249.4 | 0.96 | 1.18 |
| 32rgba | BILINEAR | 245.8 | 139.8 | 200.0 | 168.6 | 178.5 | 0.89 | 1.06 |
| 32rgba | BSPLINE | 306.8 | 188.5 | 259.0 | 210.8 | 249.2 | 0.96 | 1.18 |
| 32rgba | CATMULLROM | 307.5 | 189.4 | 259.6 | 210.1 | 249.6 | 0.96 | 1.19 |
| 32rgba | LANCZOS3 | 371.1 | 239.8 | 316.9 | 248.2 | 321.2 | 1.01 | 1.29 |
| uint16 | NEAREST | - | 5.3 | - | - | 14.2 | - | - |
| uint16 | BOX | 70.0 | 26.3 | 81.1 | 57.0 | 47.5 | 0.59 | 0.83 |
| uint16 | BICUBIC | 98.6 | 45.9 | 122.9 | 94.9 | 72.8 | 0.59 | 0.77 |
| uint16 | BILINEAR | 75.9 | 32.4 | 95.0 | 70.0 | 56.2 | 0.59 | 0.80 |
| uint16 | BSPLINE | 98.2 | 45.8 | 123.6 | 95.5 | 72.7 | 0.59 | 0.76 |
| uint16 | CATMULLROM | 98.0 | 46.4 | 123.7 | 95.7 | 72.6 | 0.59 | 0.76 |
| uint16 | LANCZOS3 | 123.2 | 57.7 | 153.6 | 118.0 | 89.4 | 0.58 | 0.76 |
| float | NEAREST | - | 27.4 | - | - | 25.3 | - | - |
| float | BOX | 115.5 | 39.7 | 118.7 | 85.9 | 44.3 | 0.37 | 0.52 |
| float | BICUBIC | 194.1 | 59.6 | 210.8 | 145.3 | 64.3 | 0.30 | 0.44 |
| float | BILINEAR | 138.0 | 45.8 | 146.3 | 106.4 | 50.7 | 0.35 | 0.48 |
| float | BSPLINE | 195.9 | 59.5 | 211.5 | 149.8 | 64.0 | 0.30 | 0.43 |
| float | CATMULLROM | 196.1 | 59.5 | 210.3 | 149.4 | 62.5 | 0.30 | 0.42 |
| float | LANCZOS3 | 230.3 | 72.9 | 265.1 | 197.9 | 79.2 | 0.30 | 0.40 |
| rgb16 | NEAREST | - | 41.2 | - | - | 39.0 | - | - |
| rgb16 | BOX | 168.4 | 98.8 | 188.3 | 142.9 | 120.5 | 0.64 | 0.84 |
| rgb16 | BICUBIC | 231.5 | 148.8 | 259.7 | 216.4 | 191.1 | 0.74 | 0.88 |
| rgb16 | BILINEAR | 189.6 | 112.7 | 212.1 | 171.4 | 149.8 | 0.71 | 0.87 |
| rgb16 | BSPLINE | 231.0 | 148.6 | 251.6 | 216.0 | 191.3 | 0.76 | 0.89 |
| rgb16 | CATMULLROM | 231.3 | 148.1 | 260.0 | 216.4 | 191.1 | 0.74 | 0.88 |
| rgb16 | LANCZOS3 | 280.9 | 184.3 | 316.4 | 266.6 | 234.1 | 0.74 | 0.88 |
| rgba16 | NEAREST | - | 53.0 | - | - | 49.7 | - | - |
| rgba16 | BOX | 253.1 | 132.2 | 220.1 | 172.9 | 160.0 | 0.73 | 0.93 |
| rgba16 | BICUBIC | 363.0 | 207.9 | 298.4 | 253.7 | 234.3 | 0.79 | 0.92 |
| rgba16 | BILINEAR | 287.3 | 156.8 | 241.1 | 198.6 | 182.8 | 0.76 | 0.92 |
| rgba16 | BSPLINE | 363.8 | 209.8 | 292.4 | 247.3 | 234.7 | 0.80 | 0.95 |
| rgba16 | CATMULLROM | 355.8 | 208.5 | 298.1 | 253.2 | 233.6 | 0.78 | 0.92 |
| rgba16 | LANCZOS3 | 425.7 | 261.9 | 365.8 | 313.8 | 285.6 | 0.78 | 0.91 |
| rgbf | NEAREST | - | 79.0 | - | - | 74.6 | - | - |
| rgbf | BOX | 208.4 | 107.1 | 223.6 | 180.4 | 114.3 | 0.51 | 0.63 |
| rgbf | BICUBIC | 312.7 | 163.7 | 369.9 | 291.4 | 165.0 | 0.45 | 0.57 |
| rgbf | BILINEAR | 240.8 | 119.9 | 277.2 | 220.3 | 130.4 | 0.47 | 0.59 |
| rgbf | BSPLINE | 311.1 | 163.9 | 371.6 | 296.4 | 165.7 | 0.45 | 0.56 |
| rgbf | CATMULLROM | 305.7 | 163.3 | 359.2 | 296.2 | 164.1 | 0.46 | 0.55 |
| rgbf | LANCZOS3 | 376.8 | 190.7 | 467.4 | 373.9 | 200.8 | 0.43 | 0.54 |
| rgbaf | NEAREST | - | 105.2 | - | - | 99.5 | - | - |
| rgbaf | BOX | 228.7 | 143.2 | 275.9 | 224.7 | 154.6 | 0.56 | 0.69 |
| rgbaf | BICUBIC | 333.4 | 198.5 | 444.0 | 354.3 | 239.3 | 0.54 | 0.68 |
| rgbaf | BILINEAR | 246.7 | 162.4 | 333.6 | 267.5 | 186.5 | 0.56 | 0.70 |
| rgbaf | BSPLINE | 345.0 | 198.7 | 444.1 | 354.6 | 239.5 | 0.54 | 0.68 |
| rgbaf | CATMULLROM | 344.4 | 199.1 | 444.1 | 353.3 | 238.2 | 0.54 | 0.67 |
| rgbaf | LANCZOS3 | 407.8 | 242.7 | 553.7 | 503.6 | 284.6 | 0.51 | 0.57 |

</details>

<details><summary>8 threads, 4000x3000 -> 1600x1200 (ms, median)</summary>

| format | filter | GCC ab5ff8e | GCC HEAD | MSVC ab5ff8e | MSVC rework | MSVC HEAD | HEAD/ab5ff8e (MSVC) | HEAD/rework (MSVC) |
|---|---|---|---|---|---|---|---|---|
| 1bit | NEAREST | - | 0.7 | - | - | 0.9 | - | - |
| 1bit | BOX | 12.9 | 8.2 | 17.0 | 13.3 | 13.6 | 0.80 | 1.02 |
| 1bit | BICUBIC | 32.0 | 22.7 | 34.6 | 29.4 | 30.1 | 0.87 | 1.02 |
| 1bit | BILINEAR | 19.6 | 13.0 | 22.6 | 18.6 | 18.9 | 0.84 | 1.02 |
| 1bit | BSPLINE | 31.9 | 22.7 | 34.3 | 29.3 | 29.9 | 0.87 | 1.02 |
| 1bit | CATMULLROM | 32.0 | 22.7 | 34.7 | 29.5 | 30.1 | 0.87 | 1.02 |
| 1bit | LANCZOS3 | 46.5 | 33.9 | 47.3 | 41.9 | 42.7 | 0.90 | 1.02 |
| 4bit | NEAREST | - | 1.1 | - | - | 1.9 | - | - |
| 4bit | BOX | 10.8 | 7.8 | 16.0 | 12.3 | 13.2 | 0.82 | 1.07 |
| 4bit | BICUBIC | 26.1 | 20.7 | 30.2 | 25.7 | 27.5 | 0.91 | 1.07 |
| 4bit | BILINEAR | 15.8 | 12.1 | 20.3 | 16.4 | 17.9 | 0.88 | 1.09 |
| 4bit | BSPLINE | 26.1 | 20.8 | 30.4 | 26.4 | 27.4 | 0.90 | 1.04 |
| 4bit | CATMULLROM | 26.1 | 20.7 | 30.3 | 26.0 | 27.4 | 0.90 | 1.05 |
| 4bit | LANCZOS3 | 37.7 | 30.3 | 40.0 | 34.6 | 38.8 | 0.97 | 1.12 |
| 8grey | NEAREST | - | 0.4 | - | - | 1.8 | - | - |
| 8grey | BOX | 9.1 | 4.1 | 14.8 | 11.7 | 10.5 | 0.71 | 0.89 |
| 8grey | BICUBIC | 20.5 | 9.8 | 24.3 | 17.5 | 18.1 | 0.75 | 1.04 |
| 8grey | BILINEAR | 13.1 | 5.7 | 17.8 | 13.5 | 13.0 | 0.73 | 0.96 |
| 8grey | BSPLINE | 20.5 | 9.8 | 24.2 | 17.5 | 18.1 | 0.75 | 1.04 |
| 8grey | CATMULLROM | 20.5 | 9.8 | 24.2 | 17.5 | 18.0 | 0.75 | 1.03 |
| 8grey | LANCZOS3 | 28.9 | 15.3 | 31.6 | 23.2 | 25.0 | 0.79 | 1.07 |
| 8pal | NEAREST | - | 0.4 | - | - | 1.8 | - | - |
| 8pal | BOX | 20.3 | 12.9 | 37.3 | 30.3 | 21.2 | 0.57 | 0.70 |
| 8pal | BICUBIC | 40.6 | 31.8 | 58.0 | 52.0 | 43.1 | 0.74 | 0.83 |
| 8pal | BILINEAR | 26.4 | 18.9 | 44.0 | 37.7 | 28.5 | 0.65 | 0.76 |
| 8pal | BSPLINE | 40.6 | 31.7 | 58.0 | 51.7 | 42.9 | 0.74 | 0.83 |
| 8pal | CATMULLROM | 40.6 | 31.7 | 58.4 | 52.0 | 42.9 | 0.73 | 0.82 |
| 8pal | LANCZOS3 | 57.2 | 45.7 | 74.4 | 68.1 | 59.1 | 0.79 | 0.87 |
| 16-565 | NEAREST | - | 0.5 | - | - | 3.4 | - | - |
| 16-565 | BOX | 22.3 | 15.0 | 41.0 | 33.7 | 24.7 | 0.60 | 0.73 |
| 16-565 | BICUBIC | 38.4 | 30.0 | 65.7 | 59.2 | 50.2 | 0.76 | 0.85 |
| 16-565 | BILINEAR | 28.6 | 21.3 | 48.6 | 42.2 | 33.3 | 0.68 | 0.79 |
| 16-565 | BSPLINE | 38.3 | 30.0 | 65.0 | 58.9 | 50.0 | 0.77 | 0.85 |
| 16-565 | CATMULLROM | 38.4 | 30.1 | 65.2 | 59.5 | 50.2 | 0.77 | 0.84 |
| 16-565 | LANCZOS3 | 56.6 | 42.9 | 83.7 | 78.1 | 69.0 | 0.82 | 0.88 |
| 16-555 | NEAREST | - | 0.5 | - | - | 3.4 | - | - |
| 16-555 | BOX | 22.5 | 15.0 | 41.1 | 34.2 | 25.1 | 0.61 | 0.73 |
| 16-555 | BICUBIC | 38.7 | 29.9 | 66.4 | 59.9 | 51.0 | 0.77 | 0.85 |
| 16-555 | BILINEAR | 28.8 | 21.3 | 49.4 | 43.0 | 33.8 | 0.68 | 0.79 |
| 16-555 | BSPLINE | 38.8 | 29.8 | 66.0 | 59.9 | 51.0 | 0.77 | 0.85 |
| 16-555 | CATMULLROM | 38.6 | 29.9 | 66.6 | 59.8 | 51.1 | 0.77 | 0.85 |
| 16-555 | LANCZOS3 | 56.6 | 45.2 | 85.4 | 79.4 | 70.5 | 0.82 | 0.89 |
| 24rgb | NEAREST | - | 1.0 | - | - | 5.2 | - | - |
| 24rgb | BOX | 19.4 | 9.8 | 37.0 | 30.6 | 21.4 | 0.58 | 0.70 |
| 24rgb | BICUBIC | 38.3 | 24.3 | 57.9 | 43.3 | 43.6 | 0.75 | 1.01 |
| 24rgb | BILINEAR | 25.1 | 14.6 | 43.8 | 34.9 | 28.8 | 0.66 | 0.82 |
| 24rgb | BSPLINE | 38.2 | 24.3 | 58.1 | 43.6 | 43.6 | 0.75 | 1.00 |
| 24rgb | CATMULLROM | 38.3 | 24.3 | 57.7 | 43.2 | 43.5 | 0.75 | 1.01 |
| 24rgb | LANCZOS3 | 53.9 | 35.3 | 73.8 | 54.2 | 59.8 | 0.81 | 1.10 |
| 32rgba | NEAREST | - | 1.4 | - | - | 7.1 | - | - |
| 32rgba | BOX | 24.9 | 13.5 | 45.2 | 39.4 | 26.3 | 0.58 | 0.67 |
| 32rgba | BICUBIC | 48.9 | 32.0 | 70.0 | 56.2 | 54.8 | 0.78 | 0.97 |
| 32rgba | BILINEAR | 32.2 | 19.3 | 53.5 | 45.2 | 35.7 | 0.67 | 0.79 |
| 32rgba | BSPLINE | 48.5 | 32.0 | 69.5 | 56.6 | 54.8 | 0.79 | 0.97 |
| 32rgba | CATMULLROM | 48.6 | 32.0 | 69.6 | 56.4 | 54.6 | 0.79 | 0.97 |
| 32rgba | LANCZOS3 | 66.7 | 46.0 | 88.8 | 69.3 | 75.1 | 0.85 | 1.08 |
| uint16 | NEAREST | - | 0.5 | - | - | 3.4 | - | - |
| uint16 | BOX | 11.3 | 3.8 | 23.3 | 17.7 | 11.9 | 0.51 | 0.67 |
| uint16 | BICUBIC | 18.3 | 8.6 | 33.0 | 25.5 | 17.9 | 0.54 | 0.70 |
| uint16 | BILINEAR | 12.8 | 5.4 | 25.7 | 20.2 | 13.8 | 0.54 | 0.68 |
| uint16 | BSPLINE | 18.3 | 8.6 | 32.9 | 25.3 | 17.9 | 0.55 | 0.71 |
| uint16 | CATMULLROM | 18.3 | 8.6 | 33.0 | 25.5 | 17.9 | 0.54 | 0.70 |
| uint16 | LANCZOS3 | 27.5 | 13.0 | 41.2 | 32.3 | 23.7 | 0.57 | 0.73 |
| float | NEAREST | - | 1.4 | - | - | 7.1 | - | - |
| float | BOX | 16.9 | 4.1 | 42.3 | 33.5 | 14.9 | 0.35 | 0.44 |
| float | BICUBIC | 31.7 | 7.9 | 63.2 | 49.6 | 19.5 | 0.31 | 0.39 |
| float | BILINEAR | 20.5 | 5.8 | 47.7 | 38.3 | 16.4 | 0.34 | 0.43 |
| float | BSPLINE | 31.7 | 7.9 | 63.2 | 49.2 | 19.3 | 0.31 | 0.39 |
| float | CATMULLROM | 31.7 | 7.9 | 63.0 | 49.4 | 19.4 | 0.31 | 0.39 |
| float | LANCZOS3 | 44.7 | 11.9 | 78.8 | 61.6 | 24.7 | 0.31 | 0.40 |
| rgb16 | NEAREST | - | 2.1 | - | - | 11.0 | - | - |
| rgb16 | BOX | 20.1 | 9.4 | 55.5 | 48.1 | 25.4 | 0.46 | 0.53 |
| rgb16 | BICUBIC | 38.2 | 23.2 | 76.1 | 67.3 | 41.8 | 0.55 | 0.62 |
| rgb16 | BILINEAR | 25.6 | 14.1 | 61.9 | 53.9 | 30.6 | 0.50 | 0.57 |
| rgb16 | BSPLINE | 38.2 | 23.2 | 76.3 | 67.2 | 41.7 | 0.55 | 0.62 |
| rgb16 | CATMULLROM | 38.2 | 23.2 | 76.1 | 67.4 | 41.6 | 0.55 | 0.62 |
| rgb16 | LANCZOS3 | 52.6 | 33.6 | 92.7 | 81.8 | 54.2 | 0.59 | 0.66 |
| rgba16 | NEAREST | - | 2.8 | - | - | 14.6 | - | - |
| rgba16 | BOX | 48.3 | 14.3 | 70.3 | 62.0 | 31.9 | 0.45 | 0.51 |
| rgba16 | BICUBIC | 85.2 | 32.9 | 94.6 | 85.6 | 53.7 | 0.57 | 0.63 |
| rgba16 | BILINEAR | 57.0 | 21.0 | 78.0 | 69.6 | 38.9 | 0.50 | 0.56 |
| rgba16 | BSPLINE | 85.3 | 32.8 | 94.2 | 85.7 | 53.5 | 0.57 | 0.62 |
| rgba16 | CATMULLROM | 84.8 | 32.8 | 94.5 | 86.2 | 53.5 | 0.57 | 0.62 |
| rgba16 | LANCZOS3 | 100.2 | 47.2 | 113.8 | 103.7 | 69.6 | 0.61 | 0.67 |
| rgbf | NEAREST | - | 4.3 | - | - | 21.9 | - | - |
| rgbf | BOX | 52.7 | 10.6 | 97.0 | 88.8 | 34.5 | 0.36 | 0.39 |
| rgbf | BICUBIC | 74.8 | 21.0 | 139.1 | 125.5 | 45.9 | 0.33 | 0.37 |
| rgbf | BILINEAR | 59.6 | 15.5 | 107.4 | 97.5 | 38.2 | 0.36 | 0.39 |
| rgbf | BSPLINE | 74.8 | 21.0 | 138.4 | 124.9 | 45.7 | 0.33 | 0.37 |
| rgbf | CATMULLROM | 75.2 | 21.0 | 138.8 | 125.0 | 45.6 | 0.33 | 0.37 |
| rgbf | LANCZOS3 | 91.8 | 30.2 | 156.5 | 139.6 | 56.6 | 0.36 | 0.41 |
| rgbaf | NEAREST | - | 5.8 | - | - | 29.7 | - | - |
| rgbaf | BOX | 70.4 | 14.2 | 127.3 | 114.2 | 43.9 | 0.34 | 0.38 |
| rgbaf | BICUBIC | 90.2 | 23.8 | 166.2 | 146.8 | 63.4 | 0.38 | 0.43 |
| rgbaf | BILINEAR | 70.5 | 14.7 | 138.5 | 126.1 | 48.7 | 0.35 | 0.39 |
| rgbaf | BSPLINE | 89.5 | 23.7 | 165.5 | 147.7 | 63.2 | 0.38 | 0.43 |
| rgbaf | CATMULLROM | 90.0 | 23.8 | 165.3 | 146.2 | 63.3 | 0.38 | 0.43 |
| rgbaf | LANCZOS3 | 102.6 | 35.0 | 193.5 | 170.6 | 79.0 | 0.41 | 0.46 |

</details>

<details><summary>8 threads, 1600x1200 -> 4000x3000 (ms, median)</summary>

| format | filter | GCC ab5ff8e | GCC HEAD | MSVC ab5ff8e | MSVC rework | MSVC HEAD | HEAD/ab5ff8e (MSVC) | HEAD/rework (MSVC) |
|---|---|---|---|---|---|---|---|---|
| 1bit | NEAREST | - | 1.7 | - | - | 3.1 | - | - |
| 1bit | BOX | 13.7 | 8.5 | 28.4 | 25.2 | 22.6 | 0.80 | 0.90 |
| 1bit | BICUBIC | 24.3 | 16.6 | 36.5 | 34.1 | 33.2 | 0.91 | 0.97 |
| 1bit | BILINEAR | 16.3 | 11.1 | 30.6 | 27.9 | 25.9 | 0.85 | 0.93 |
| 1bit | BSPLINE | 24.3 | 16.6 | 36.0 | 33.6 | 33.3 | 0.93 | 0.99 |
| 1bit | CATMULLROM | 24.3 | 16.6 | 36.3 | 33.9 | 33.4 | 0.92 | 0.98 |
| 1bit | LANCZOS3 | 32.4 | 23.4 | 44.0 | 41.7 | 42.7 | 0.97 | 1.02 |
| 4bit | NEAREST | - | 3.0 | - | - | 7.5 | - | - |
| 4bit | BOX | 15.1 | 8.5 | 29.3 | 25.1 | 22.5 | 0.77 | 0.90 |
| 4bit | BICUBIC | 25.6 | 15.9 | 38.4 | 32.6 | 32.4 | 0.84 | 0.99 |
| 4bit | BILINEAR | 17.8 | 10.8 | 32.5 | 27.4 | 25.7 | 0.79 | 0.94 |
| 4bit | BSPLINE | 25.7 | 15.9 | 38.4 | 32.7 | 32.3 | 0.84 | 0.99 |
| 4bit | CATMULLROM | 25.7 | 15.9 | 38.5 | 32.6 | 32.2 | 0.84 | 0.99 |
| 4bit | LANCZOS3 | 33.8 | 22.4 | 46.5 | 40.0 | 41.2 | 0.89 | 1.03 |
| 8grey | NEAREST | - | 1.5 | - | - | 10.3 | - | - |
| 8grey | BOX | 15.1 | 7.7 | 28.6 | 24.0 | 22.7 | 0.79 | 0.94 |
| 8grey | BICUBIC | 25.7 | 12.6 | 36.6 | 29.6 | 30.0 | 0.82 | 1.01 |
| 8grey | BILINEAR | 17.7 | 9.3 | 31.3 | 25.4 | 24.9 | 0.80 | 0.98 |
| 8grey | BSPLINE | 25.7 | 12.5 | 36.5 | 29.5 | 29.9 | 0.82 | 1.01 |
| 8grey | CATMULLROM | 25.7 | 12.6 | 36.6 | 29.4 | 29.9 | 0.82 | 1.02 |
| 8grey | LANCZOS3 | 33.6 | 17.5 | 43.6 | 35.1 | 37.1 | 0.85 | 1.06 |
| 8pal | NEAREST | - | 1.5 | - | - | 10.3 | - | - |
| 8pal | BOX | 46.8 | 35.2 | 68.6 | 63.1 | 51.5 | 0.75 | 0.82 |
| 8pal | BICUBIC | 63.8 | 49.5 | 89.9 | 75.1 | 72.5 | 0.81 | 0.97 |
| 8pal | BILINEAR | 51.8 | 39.6 | 74.1 | 66.6 | 58.0 | 0.78 | 0.87 |
| 8pal | BSPLINE | 63.6 | 49.5 | 88.6 | 75.4 | 72.0 | 0.81 | 0.96 |
| 8pal | CATMULLROM | 64.2 | 49.6 | 88.9 | 75.0 | 73.0 | 0.82 | 0.97 |
| 8pal | LANCZOS3 | 77.7 | 59.9 | 105.0 | 86.0 | 91.3 | 0.87 | 1.06 |
| 16-565 | NEAREST | - | 2.5 | - | - | 20.8 | - | - |
| 16-565 | BOX | 50.3 | 36.7 | 73.1 | 65.8 | 54.2 | 0.74 | 0.82 |
| 16-565 | BICUBIC | 69.8 | 51.9 | 96.3 | 84.0 | 77.2 | 0.80 | 0.92 |
| 16-565 | BILINEAR | 58.2 | 41.5 | 79.1 | 71.7 | 61.4 | 0.78 | 0.86 |
| 16-565 | BSPLINE | 69.7 | 51.4 | 95.2 | 82.9 | 76.5 | 0.80 | 0.92 |
| 16-565 | CATMULLROM | 69.5 | 51.7 | 94.5 | 82.9 | 76.6 | 0.81 | 0.92 |
| 16-565 | LANCZOS3 | 85.0 | 62.7 | 112.0 | 96.0 | 95.6 | 0.85 | 1.00 |
| 16-555 | NEAREST | - | 2.5 | - | - | 20.9 | - | - |
| 16-555 | BOX | 50.9 | 36.9 | 72.5 | 67.0 | 54.1 | 0.75 | 0.81 |
| 16-555 | BICUBIC | 69.6 | 51.7 | 95.3 | 82.2 | 77.1 | 0.81 | 0.94 |
| 16-555 | BILINEAR | 56.9 | 41.6 | 80.0 | 70.8 | 61.2 | 0.77 | 0.87 |
| 16-555 | BSPLINE | 69.8 | 51.5 | 94.9 | 82.7 | 77.1 | 0.81 | 0.93 |
| 16-555 | CATMULLROM | 69.7 | 51.4 | 95.5 | 82.6 | 77.6 | 0.81 | 0.94 |
| 16-555 | LANCZOS3 | 84.8 | 62.2 | 112.2 | 96.0 | 96.0 | 0.86 | 1.00 |
| 24rgb | NEAREST | - | 20.4 | - | - | 31.8 | - | - |
| 24rgb | BOX | 47.3 | 35.7 | 69.6 | 62.4 | 54.3 | 0.78 | 0.87 |
| 24rgb | BICUBIC | 65.1 | 48.9 | 89.6 | 73.6 | 74.9 | 0.84 | 1.02 |
| 24rgb | BILINEAR | 51.9 | 39.4 | 75.9 | 66.0 | 60.8 | 0.80 | 0.92 |
| 24rgb | BSPLINE | 65.1 | 48.5 | 89.3 | 74.2 | 75.1 | 0.84 | 1.01 |
| 24rgb | CATMULLROM | 65.2 | 48.6 | 89.6 | 74.1 | 75.2 | 0.84 | 1.01 |
| 24rgb | LANCZOS3 | 80.1 | 58.8 | 104.8 | 84.2 | 93.3 | 0.89 | 1.11 |
| 32rgba | NEAREST | - | 26.8 | - | - | 42.9 | - | - |
| 32rgba | BOX | 63.3 | 46.8 | 86.5 | 80.5 | 70.4 | 0.81 | 0.87 |
| 32rgba | BICUBIC | 85.4 | 64.4 | 111.1 | 96.2 | 96.7 | 0.87 | 1.01 |
| 32rgba | BILINEAR | 69.2 | 52.8 | 94.1 | 85.5 | 77.9 | 0.83 | 0.91 |
| 32rgba | BSPLINE | 84.3 | 64.5 | 110.8 | 96.1 | 96.2 | 0.87 | 1.00 |
| 32rgba | CATMULLROM | 84.9 | 64.5 | 111.1 | 96.0 | 96.4 | 0.87 | 1.00 |
| 32rgba | LANCZOS3 | 102.0 | 78.4 | 129.2 | 109.7 | 118.4 | 0.92 | 1.08 |
| uint16 | NEAREST | - | 2.5 | - | - | 21.1 | - | - |
| uint16 | BOX | 18.5 | 7.1 | 44.9 | 38.2 | 32.3 | 0.72 | 0.85 |
| uint16 | BICUBIC | 24.5 | 11.9 | 53.7 | 46.2 | 38.1 | 0.71 | 0.82 |
| uint16 | BILINEAR | 19.8 | 8.7 | 47.8 | 41.3 | 34.4 | 0.72 | 0.83 |
| uint16 | BSPLINE | 24.5 | 11.9 | 53.6 | 46.3 | 38.2 | 0.71 | 0.83 |
| uint16 | CATMULLROM | 24.5 | 11.9 | 53.8 | 46.5 | 38.2 | 0.71 | 0.82 |
| uint16 | LANCZOS3 | 30.1 | 16.4 | 61.5 | 53.6 | 44.0 | 0.72 | 0.82 |
| float | NEAREST | - | 26.9 | - | - | 43.1 | - | - |
| float | BOX | 45.4 | 30.0 | 81.1 | 72.0 | 50.6 | 0.62 | 0.70 |
| float | BICUBIC | 62.1 | 32.8 | 103.5 | 88.0 | 54.8 | 0.53 | 0.62 |
| float | BILINEAR | 50.7 | 30.6 | 86.7 | 77.2 | 52.3 | 0.60 | 0.68 |
| float | BSPLINE | 62.3 | 32.7 | 103.7 | 87.2 | 54.1 | 0.52 | 0.62 |
| float | CATMULLROM | 61.9 | 32.6 | 103.5 | 87.9 | 54.5 | 0.53 | 0.62 |
| float | LANCZOS3 | 72.8 | 36.7 | 119.9 | 98.8 | 59.2 | 0.49 | 0.60 |
| rgb16 | NEAREST | - | 39.6 | - | - | 63.9 | - | - |
| rgb16 | BOX | 66.5 | 50.8 | 112.8 | 106.1 | 84.0 | 0.74 | 0.79 |
| rgb16 | BICUBIC | 79.8 | 63.3 | 132.6 | 123.3 | 100.7 | 0.76 | 0.82 |
| rgb16 | BILINEAR | 67.5 | 54.0 | 118.7 | 111.5 | 88.9 | 0.75 | 0.80 |
| rgb16 | BSPLINE | 80.1 | 63.2 | 133.0 | 123.5 | 99.6 | 0.75 | 0.81 |
| rgb16 | CATMULLROM | 79.8 | 63.5 | 133.8 | 123.8 | 101.3 | 0.76 | 0.82 |
| rgb16 | LANCZOS3 | 93.9 | 73.1 | 147.4 | 138.9 | 112.5 | 0.76 | 0.81 |
| rgba16 | NEAREST | - | 53.1 | - | - | 85.2 | - | - |
| rgba16 | BOX | 105.2 | 67.9 | 126.2 | 130.7 | 110.3 | 0.87 | 0.84 |
| rgba16 | BICUBIC | 132.2 | 88.8 | 161.9 | 153.3 | 128.7 | 0.79 | 0.84 |
| rgba16 | BILINEAR | 112.6 | 75.6 | 143.4 | 136.2 | 115.4 | 0.81 | 0.85 |
| rgba16 | BSPLINE | 132.4 | 88.8 | 160.8 | 154.0 | 128.7 | 0.80 | 0.84 |
| rgba16 | CATMULLROM | 132.5 | 89.1 | 160.6 | 153.3 | 128.4 | 0.80 | 0.84 |
| rgba16 | LANCZOS3 | 155.8 | 104.1 | 174.9 | 163.9 | 144.8 | 0.83 | 0.88 |
| rgbf | NEAREST | - | 78.6 | - | - | 113.8 | - | - |
| rgbf | BOX | 126.1 | 84.0 | 168.2 | 160.3 | 125.6 | 0.75 | 0.78 |
| rgbf | BICUBIC | 151.2 | 93.2 | 197.2 | 185.7 | 132.5 | 0.67 | 0.71 |
| rgbf | BILINEAR | 134.4 | 85.3 | 175.1 | 165.0 | 126.8 | 0.72 | 0.77 |
| rgbf | BSPLINE | 151.2 | 92.5 | 197.8 | 184.1 | 131.9 | 0.67 | 0.72 |
| rgbf | CATMULLROM | 150.8 | 93.2 | 197.4 | 183.8 | 132.1 | 0.67 | 0.72 |
| rgbf | LANCZOS3 | 168.4 | 102.7 | 222.5 | 203.8 | 145.3 | 0.65 | 0.71 |
| rgbaf | NEAREST | - | 105.8 | - | - | 135.2 | - | - |
| rgbaf | BOX | 161.7 | 111.5 | 199.1 | 190.8 | 147.4 | 0.74 | 0.77 |
| rgbaf | BICUBIC | 188.7 | 118.4 | 241.7 | 221.6 | 164.6 | 0.68 | 0.74 |
| rgbaf | BILINEAR | 164.4 | 112.0 | 208.0 | 191.4 | 149.3 | 0.72 | 0.78 |
| rgbaf | BSPLINE | 188.9 | 118.7 | 241.6 | 221.2 | 164.5 | 0.68 | 0.74 |
| rgbaf | CATMULLROM | 188.0 | 118.6 | 240.1 | 222.5 | 164.7 | 0.69 | 0.74 |
| rgbaf | LANCZOS3 | 203.4 | 129.1 | 266.5 | 247.7 | 181.6 | 0.68 | 0.73 |

</details>

HEAD is never more than 1.4% slower than `ab5ff8e` in any case of the matrix, under either compiler. Against the
rework:
* HEAD is far ahead on the 16-bit, float, palette and 555/565 formats (0.37-0.89 at 8 threads), which the rework did
  not vectorise.
* It is 2-12% behind on 1- and 4-bit.
* It is behind on the 8/24/32-bit formats with the cubic filters and lanczos3, where `274f833`'s kernel applies.

## 10. Checking it on the 8-core machine

The direct test is the branch: build `FreeImage.dll` from `worktree-io64` (`06d69d9` or later) the way you always do, and time
it against `FreeImage-old.dll`.

`.claude/scratch/resize-regress/FreeImage-resize-speed-check.zip` (1.6 MB) holds six MSVC builds of this rig:
`ab5ff8e`, the rework, HEAD, HEAD + SSE2 vertical, HEAD + both kernels (all JCC-padded), and HEAD unpadded, with
QPV's `vcomp140.dll`. `run.bat` (or `driver.exe`) runs 5 shapes x 6 filters x 8 configurations, including HEAD and
HEAD + both forced horizontal first. It writes `results.txt` and per-pass `trace_*.txt`. The driver was tested under
Wine here. These results decide what holds on the 8-core, 16-thread machine:
* `rework` against `head`: the gap you saw, per filter;
* `head_unpadded` against `head`: whether placement matters on that CPU (it should not on 11th gen+);
* `headboth` against `headboth_xy`: which order each filter wants there, the input for refitting the rule.

## 11. Recommendations

1. **Protect the rework commits.** Done: branch `resize-rework` at `274f833`, pushed to origin.
2. **Merge the horizontal kernel.** Done in `1eb0175`.
3. **Refit the pass-order rule** to the kernels that exist, per compiler, on large images and on the 8-core machine.
   The present constants are GCC-only and wrong for MSVC. Include the fixed per-pixel costs, or box and bilinear will
   be sent the wrong way.
4. **Take the SSE2 vertical kernel for MSVC builds.** Done in `06d69d9` (unrolling it the way GCC does would let GCC use it too).
5. **Add `/QIntel-jcc-erratum`** to the Release configurations of `FreeImage.2017.vcxproj` (C/C++ > Code Generation >
   "Enable Intel JCC Erratum Mitigation"). The 22 Sep DLL QPV ships loses 34% to it on older Intel CPUs.
6. When comparing builds, compare them on the same hardware with the same toolset, and don't read anything into a
   difference between two unpadded MSVC builds on a 6th-10th gen Intel CPU.

## 12. Ruled out along the way

* **Band overhead.** An empty `omp parallel for` region costs 17 us with vcomp under Wine and 3 us with libgomp. HEAD
  enters about 40 regions per call, under 1 ms of a 600-3000 ms resize.
* **Thread scaling and SMT.** On the i7-7700T, 8 threads are no faster than 4 for the MSVC kernels (SMT gives them
  nothing). HEAD's ratio to `ab5ff8e` does not worsen with more threads (32-bit lanczos3: 1.02 at 1 thread, 0.95 at
  8), and not with size either (8000 to 32000 pixels wide). Memory-traffic counters are closed here
  (`perf_event_paranoid` 4), so a memory-bound 8-core case is not excluded by measurement, only by the absence of any size trend.
* **The rest of the library.** Between `ab5ff8e` and HEAD only `03fcfc5` touched `BitmapAccess.cpp`, and only its
  argument checks. `ab5ff8e`'s resize objects linked into HEAD's library time the same as `ab5ff8e`'s whole library
  (603 vs 606 ms). The vcxproj compiler settings did not change.
* **Codegen of HEAD's horizontal template.** MSVC keeps `double value[SPP]` in registers, and its tap loop is instruction
  for instruction the same as `ab5ff8e`'s hand-written one. Only the per-pixel store is heavier (`RoundSample`).

## 13. Rig

Everything is in `.claude/scratch/resize-regress/` (untracked):
* **MSVC:** installed in `.claude/scratch/msvc/` (msvc-wine, MSVC 14.29.30133 + SDK 10.0.19041, 2.6 GB). Run it with
  the AHK rig's Wine, with a persistent wineserver (`bin/wineserver-persistent`); otherwise each run costs ~20 s.
* **Libraries and objects:** `build_head.sh`, `build_base.sh` (GCC libraries), `extract_commits.sh` + `build_commits.sh`
  (per-commit objects), `msvc_build.py common|bench|variant <sha>` (MSVC; `OUT=.../msvc-jcc
  FLAGS_EXTRA=/QIntel-jcc-erratum` for the padded builds).
* **Benchmarks:** `bench_large.cpp` and `rbench.cpp` (the earlier matrix program). `run_sweep.py`, `trace_matrix.py` and
  `run_matrix.py` interleave the variants with `PAUSE=6`. Without the pause the i7-7700T drifts 6% slower over
  back-to-back runs.
* **Variants and analysis:** `make_trace.py` / `make_order.py` add the pass timers and the `FI_RESCALE_ORDER` override;
  `make_sse.py` / `make_hsse.py` build the prototypes. `jcc_h24.py` finds the tap loops in an executable and reports their
  32-byte placement.
* **Data:** `sweep1.tsv` (GCC), `msvc1.tsv`, `bisect_msvc.tsv`, `bisect_jcc.tsv`, `key8.tsv`, `size8.tsv`,
  `ladder.tsv`, `pass1.tsv`, `rework8.tsv`, `fix8.tsv`, `matrix.tsv`.
