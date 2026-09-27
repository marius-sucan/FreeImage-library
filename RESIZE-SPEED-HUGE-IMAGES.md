# FreeImage_Rescale on a 129300 x 86102 image: why a13e209 took twice as long, and the fix

2026-09-27, branch `worktree-io64`. The case: a 32-bit 129300 x 86102 image (11133 megapixels, 44.5 GB) reduced to
1820 x 1059 with FILTER_BSPLINE, on an 8-core i7-11700, with DLLs built by Visual Studio 2019 16.11.43. Your timings:

| DLL | resize code | time |
|---|---|---|
| `FreeImage-unknown.dll` | `274f833` (summary, 1) | 6.55 s |
| `ab5ff8e`, `a27b34e`, `54dec18`, `b549f4d` | | 8.25-8.29 s |
| `d60316b`, `b6e3af3` | | 9.14-9.15 s |
| `a13e209` | | 18.35 s |

## 1. Summary

1. **`FreeImage-unknown.dll` is `274f833`.** It is byte-identical (SHA-256 `7168c4f7...`) to QPV's `FreeImage-old.dll` of
   29 August, which the previous report traced to `274f833`, the tip of the resize rework that never reached `qpv` (branch
   `resize-rework`). It filters horizontally first with that branch's SSE2 horizontal kernel. `3790a75` (a GIF fix) is not it.
2. **Why `a13e209` doubled.** From `d60316b` on, the pass-order rule sends this reduction vertically first, so the vertical
   pass runs over the whole source: rows of 129300 pixels (517 KB), a window of 327 of them per destination row. In `a13e209`
   that pass is `06d69d9`'s MSVC-only `VerticalStrips`: 16 samples at a time through all 327 taps, 16 bytes of a different
   row on every tap. Here it is 1.31-1.37x slower than `b6e3af3`'s loop per core (1 to 4 threads), and hyper-threading hides
   it at 8 threads (0.96x); your 16 threads show 2.0x. The exact factor was not reproduced here (section 2).
3. **The fix is three commits** on `worktree-io64`, each with the same output to the bit except where the third changes the
   pass order:
   - `C1` = `394d42a` (horizontal): each thread converts every source pixel once into a ring of doubles, 16, 4 or 4
     rows at a time.
   - `C2` = `59cf8ef` (vertical): the same with rows, one SSE2 kernel for GCC and MSVC; `VerticalStrips` is gone.
   - `C3` = `b5c8f48` (pass order): 8-, 24- and 32-bit images take the order these kernels make cheaper; this case goes
     horizontal first again.
4. **Result on the cut case** (section 2), MSVC builds: 1218 ms against 1852 ms for your `unknown` DLL in the same round
   (0.66x), 2655 ms for your `a13e209` DLL (0.46x) and 2817 ms for your `b6e3af3` DLL (0.43x). GCC builds: 1109 ms
   against 1677 ms for `b6e3af3` (0.66x). Across 30 cases of 8-, 24- and 32-bit images the series takes 0.55x
   `b6e3af3`'s time on average under MSVC (0.35-0.83x) and 0.70x under GCC (0.54-0.97x), and is never slower than
   `a13e209` by more than 5% (section 4).
5. **Not measured on your machine.** Every number here comes from a 4-core i7-7700T; the MSVC builds ran under Wine. The case
   was cut to 8000 source rows, 129300 x 8000 -> 1820 x 98: the same row width, ratios and filter windows (287 and 327 taps),
   4.1 GB instead of 44.5 GB. Projected from the ratios, the final code should take about 4.1-4.4 s (6.55 s times the 0.62-0.68
   of `unknown`'s time the final code took in the rounds here) on your machine.

## 2. What each DLL does

### 2.1 Your DLLs on the cut case

Your DLLs under Wine, 8 threads, minimum of 3 calls:

| DLL | cut case, ms | / unknown | your full case | / unknown |
|---|---|---|---|---|
| `unknown` (`274f833`) | 1684 | 1.00 | 6.55 s | 1.00 |
| `b549f4d` | 2302 | 1.37 | 8.29 s | 1.27 |
| `d60316b` | 2650 | 1.57 | 9.14 s | 1.40 |
| `b6e3af3` | 2689 | 1.60 | 9.15 s | 1.40 |
| `a13e209` | 2679 | 1.59 | 18.35 s | 2.80 |

The ranking matches yours except `a13e209`, level with `b6e3af3` here at 8 threads (2.2).

- `274f833` (unknown), and `b549f4d` and older: horizontal first (the width rule). The rework's SSE2 kernel is 1.37x faster
  than the plain one here, 1.27x on your machine.
- `d60316b`, `b6e3af3`, `a13e209`: vertical first, by `d60316b`'s tap-cost rule, which counts a horizontal tap as two vertical
  ones (GCC's kernels before these commits).
- Hashes: `unknown` and `b549f4d` give `e16fea2c0ac402f7`, the three vertical-first DLLs `97d9f0964609a290`: the image between
  the passes rounds differently in the two orders, as `d60316b` documented.

### 2.2 Why `a13e209` is slower than `b6e3af3`

The same code in both except the kernels of `1eb0175` (horizontal, the small pass here) and `06d69d9` (vertical, the big one).
Threads, `a13e209` / `b6e3af3`, minimum ms:

| threads | b6e3af3 | a13e209 | ratio |
|---|---|---|---|
| 1 | 10136 | 13895 | 1.37 |
| 2 | 5059 | 6733 | 1.33 |
| 4 | 2619 | 3426 | 1.31 |
| 8 | 2729 | 2627 | 0.96 |

`b6e3af3`'s vertical loop is compute-bound: 4 threads on 4 cores give everything, hyper-threading nothing. `VerticalStrips` walks
327 rows 517 KB apart for every 16 samples, one cache line and one page per tap: it waits on memory, and a second thread per
core fills those waits. With 8 cores and 16 threads sharing the same DRAM, that loop has less to hide behind; the 2.0x you
measured fits that, but this machine cannot show it.

## 3. The fix

All three commits touch `Source/FreeImageToolkit/Resize.cpp` (and `Resize.h`: `CWeightsTable::getWeights()`), 8-bit
greyscale, 24-bit and 32-bit FIT_BITMAP images without a palette, x86 and x64 (SSE2, which both compilers enable there by
default). Other image types and other targets keep their kernels. The taps of every sum are added in the same order as before,
in double, without FMA, so the output is the same to the bit.

### 3.1 `C1`: the horizontal pass converts every source pixel once

`274f833`'s kernel (ported as `1eb0175`) transposes two rows into a tile of 512 floats for each run of destination pixels whose
windows fit in it, converting a source pixel again for every run it falls in, and converting to double again on every tap. At
129300 -> 1820 a window is 287 pixels wide, so a tile holds 4 destination pixels. A window over 512 pixels (reductions beyond
about 120x with b-spline) fell back to the plain loop.

Now each thread keeps a ring of doubles covering its rows' current window and 128 pixels ahead, and converts each source pixel
of its rows once. The conversion splits 16 bytes into (row, row) pairs with masks and shifts and turns them into doubles by
2^52 + b - 2^52, which is exact and uses no shuffles; a tap is then one multiply and one add per two samples, read from L1.
Rows go 16, 4 or 4 at a time (8-, 24-, 32-bit), so 6 to 8 sums are in flight, more than the latency of an add needs. A ring
over 4 MB per thread (windows of about 40000-65000 pixels) leaves the pass to the plain loop.

### 3.2 `C2`: the vertical pass does the same with rows, for both compilers

`VerticalStrips` (MSVC only) is gone, and so is GCC's auto-vectorised block loop for these images. A work item takes a block of
64-256 samples and 16 destination rows; each source row it reads is converted once into a ring (a window of rows, 64 KB, at
least a cache line of every row), and 16 sums stay in registers through every tap. Where each row feeds fewer than 1.5 taps
(box reductions) the ring costs more than it saves, and the taps convert their 16 samples straight from the source instead.
The same intrinsics compile under GCC and MSVC, so both builds now run the same vertical code.

### 3.3 `C3`: the pass order weighs what these kernels cost

`d60316b`'s rule counts a horizontal tap as two vertical ones: GCC's kernels at the time. With both passes on the new kernels a
horizontal tap costs about 0.8 of a vertical one, and every pixel the horizontal pass writes about 4 taps more (its sums are
packed and scattered to up to 16 rows). When both passes run these kernels, `scaleInBands` now compares

    cost_xy = (0.8 * TX + 4 * dst_width) * src_height + TY * dst_width
    cost_yx = TY * src_width + (0.8 * TX + 4 * dst_width) * dst_height

(TX, TY: the tap totals of the two weight tables) and takes the cheaper order, with no margin. Every other layout keeps
`d60316b`'s model.

The two constants were fitted on 156 cases timed in both orders under both compilers, with `C1` and `C2` (before `C2`'s
direct path for box reductions, see 6): 13 shapes
(uniform reductions from 0.75x down to 129300 -> 1820, enlargements 1.25-4x, and four anisotropic ones), box, bilinear,
b-spline and lanczos3, 8-, 24- and 32-bit. Loss against the faster order of each case (geometric mean over 312 timings):

| rule | mean loss | cases > 5% | worst |
|---|---|---|---|
| this one | 0.37% | 7 | 1.39 (a near-tie, see below) |
| `d60316b` | 7.1% | 78 | 2.08 |
| width rule (horizontal first unless the width grows) | 7.2% | 74 | 2.38 |
| always horizontal first | 10.0% | 70 | 2.38 |

The neighbourhood is flat (0.75-0.85 and 3-6 stay within 0.2%). The worst losses re-timed with 45 calls each are near-ties or
cases where the compilers disagree (32-bit bilinear 4000 -> 400: GCC prefers horizontal first by 8%, MSVC vertical first by
5%). On the 129300-wide shapes horizontal first is 1.11-1.74x faster in all 24 timings; the costs the rule compares favour it by
about 1.2x, while `d60316b` took the other order. There the vertical pass over 517 KB rows touches about 1500 pages per work item
(or, in the old kernels, per destination row), beyond what the TLB holds.

## 4. Results

Each build in the order it picks (`xy` = horizontal first), 8 threads, minimum of 3 calls, JCC-padded MSVC builds. `+C1`,
`+C2`, `+C3` are the three commits in turn; `a13e209` is HEAD before them. One GCC cell (8-bit box 4000x3000 ->
2000x1500, `+C3`) was a hiccup of 7 ms and is the re-timed 3.9 ms, the same as `+C2` (45 calls).

MSVC, 8 threads, minimum ms of 3 (the order each build picks, xy = horizontal first):

| format | case | filter | b6e3af3 | a13e209 | +C1 | +C2 | +C3 | final / b6e3af3 | final / a13e209 |
|---|---|---|---|---|---|---|---|---|---|
| 32rgba | 129300x8000 -> 1820x98 | b-spline | 2937 yx | 2600 yx | 2614 yx | 1888 yx | 1218 xy | **0.41** | 0.47 |
| 24rgb | 129300x8000 -> 1820x98 | b-spline | 2255 yx | 2008 yx | 2009 yx | 1352 yx | 888 xy | **0.39** | 0.44 |
| 8grey | 129300x8000 -> 1820x98 | b-spline | 684 yx | 648 yx | 659 yx | 422 yx | 271 xy | **0.40** | 0.42 |
| 32rgba | 16000x12000 -> 1920x1440 | box | 170 yx | 113 yx | 113 yx | 116 yx | 117 yx | **0.69** | 1.03 |
| 32rgba | 16000x12000 -> 1920x1440 | b-spline | 578 yx | 360 yx | 353 yx | 246 yx | 232 xy | **0.40** | 0.64 |
| 32rgba | 16000x12000 -> 1920x1440 | lanczos3 | 863 yx | 538 yx | 526 yx | 340 yx | 306 xy | **0.35** | 0.57 |
| 24rgb | 16000x12000 -> 1920x1440 | box | 124 yx | 82 yx | 79 yx | 84 yx | 85 yx | **0.68** | 1.04 |
| 24rgb | 16000x12000 -> 1920x1440 | b-spline | 407 yx | 262 yx | 250 yx | 179 yx | 178 xy | **0.44** | 0.68 |
| 24rgb | 16000x12000 -> 1920x1440 | lanczos3 | 607 yx | 392 yx | 382 yx | 244 yx | 237 xy | **0.39** | 0.61 |
| 8grey | 16000x12000 -> 1920x1440 | box | 44 yx | 31 yx | 31 yx | 33 yx | 33 yx | **0.74** | 1.05 |
| 8grey | 16000x12000 -> 1920x1440 | b-spline | 142 yx | 93 yx | 87 yx | 65 yx | 63 xy | **0.44** | 0.68 |
| 8grey | 16000x12000 -> 1920x1440 | lanczos3 | 211 yx | 142 yx | 135 yx | 91 yx | 86 xy | **0.41** | 0.61 |
| 32rgba | 4000x3000 -> 2000x1500 | box | 33 yx | 27 yx | 25 yx | 26 yx | 26 yx | **0.79** | 0.98 |
| 32rgba | 4000x3000 -> 2000x1500 | b-spline | 64 yx | 43 yx | 40 yx | 35 yx | 35 yx | **0.55** | 0.81 |
| 32rgba | 4000x3000 -> 2000x1500 | lanczos3 | 86 yx | 56 yx | 53 yx | 43 yx | 44 yx | **0.51** | 0.78 |
| 24rgb | 4000x3000 -> 2000x1500 | box | 27 yx | 21 yx | 20 yx | 21 yx | 21 yx | **0.79** | 0.98 |
| 24rgb | 4000x3000 -> 2000x1500 | b-spline | 50 yx | 34 yx | 31 yx | 28 yx | 28 yx | **0.56** | 0.82 |
| 24rgb | 4000x3000 -> 2000x1500 | lanczos3 | 68 yx | 45 yx | 41 yx | 34 yx | 34 yx | **0.50** | 0.77 |
| 8grey | 4000x3000 -> 2000x1500 | box | 13 yx | 11 yx | 10 yx | 10 yx | 10 yx | **0.83** | 0.96 |
| 8grey | 4000x3000 -> 2000x1500 | b-spline | 21 yx | 16 yx | 14 yx | 12 yx | 12 yx | **0.59** | 0.78 |
| 8grey | 4000x3000 -> 2000x1500 | lanczos3 | 28 yx | 21 yx | 19 yx | 16 yx | 16 yx | **0.58** | 0.78 |
| 32rgba | 2000x1500 -> 8000x6000 | box | 175 xy | 136 xy | 132 xy | 134 xy | 136 xy | **0.78** | 1.00 |
| 32rgba | 2000x1500 -> 8000x6000 | b-spline | 270 xy | 193 xy | 186 xy | 153 xy | 158 xy | **0.59** | 0.82 |
| 32rgba | 2000x1500 -> 8000x6000 | lanczos3 | 348 xy | 232 xy | 225 xy | 176 xy | 184 xy | **0.53** | 0.79 |
| 24rgb | 2000x1500 -> 8000x6000 | box | 134 xy | 102 xy | 101 xy | 102 xy | 102 xy | **0.76** | 1.00 |
| 24rgb | 2000x1500 -> 8000x6000 | b-spline | 207 xy | 147 xy | 142 xy | 128 xy | 124 xy | **0.60** | 0.84 |
| 24rgb | 2000x1500 -> 8000x6000 | lanczos3 | 270 xy | 178 xy | 167 xy | 133 xy | 136 xy | **0.50** | 0.76 |
| 8grey | 2000x1500 -> 8000x6000 | box | 51 xy | 44 xy | 43 xy | 47 xy | 41 xy | **0.80** | 0.93 |
| 8grey | 2000x1500 -> 8000x6000 | b-spline | 84 xy | 64 xy | 55 xy | 49 xy | 52 xy | **0.62** | 0.82 |
| 8grey | 2000x1500 -> 8000x6000 | lanczos3 | 105 xy | 71 xy | 77 xy | 59 xy | 54 xy | **0.52** | 0.76 |

geomean final/b6e3af3 0.553 (worst 0.83, best 0.35); final/a13e209 0.765 (worst 1.05)
per commit: C1/a13e209 0.956 (0.87-1.08), C2/C1 0.846 (0.64-1.11), C3/C2 0.946 (0.64-1.08)

GCC, 8 threads, minimum ms of 3 (the order each build picks, xy = horizontal first):

| format | case | filter | b6e3af3 | a13e209 | +C1 | +C2 | +C3 | final / b6e3af3 | final / a13e209 |
|---|---|---|---|---|---|---|---|---|---|
| 32rgba | 129300x8000 -> 1820x98 | b-spline | 1677 yx | 1703 yx | 1698 yx | 1775 yx | 1109 xy | **0.66** | 0.65 |
| 24rgb | 129300x8000 -> 1820x98 | b-spline | 1318 yx | 1324 yx | 1323 yx | 1371 yx | 859 xy | **0.65** | 0.65 |
| 8grey | 129300x8000 -> 1820x98 | b-spline | 398 yx | 401 yx | 398 yx | 419 yx | 264 xy | **0.66** | 0.66 |
| 32rgba | 16000x12000 -> 1920x1440 | box | 101 yx | 105 yx | 98 yx | 100 yx | 98 yx | **0.97** | 0.93 |
| 32rgba | 16000x12000 -> 1920x1440 | b-spline | 346 yx | 321 yx | 316 yx | 237 yx | 219 xy | **0.63** | 0.68 |
| 32rgba | 16000x12000 -> 1920x1440 | lanczos3 | 516 yx | 474 yx | 464 yx | 331 yx | 295 xy | **0.57** | 0.62 |
| 24rgb | 16000x12000 -> 1920x1440 | box | 73 yx | 73 yx | 73 yx | 70 yx | 71 yx | **0.97** | 0.98 |
| 24rgb | 16000x12000 -> 1920x1440 | b-spline | 248 yx | 234 yx | 226 yx | 184 yx | 168 xy | **0.68** | 0.72 |
| 24rgb | 16000x12000 -> 1920x1440 | lanczos3 | 369 yx | 348 yx | 335 yx | 255 yx | 234 xy | **0.63** | 0.67 |
| 8grey | 16000x12000 -> 1920x1440 | box | 26 yx | 26 yx | 25 yx | 25 yx | 25 yx | **0.95** | 0.98 |
| 8grey | 16000x12000 -> 1920x1440 | b-spline | 83 yx | 82 yx | 77 yx | 61 yx | 56 xy | **0.67** | 0.69 |
| 8grey | 16000x12000 -> 1920x1440 | lanczos3 | 128 yx | 127 yx | 116 yx | 87 yx | 79 xy | **0.62** | 0.62 |
| 32rgba | 4000x3000 -> 2000x1500 | box | 17 yx | 15 yx | 14 yx | 13 yx | 13 yx | **0.74** | 0.85 |
| 32rgba | 4000x3000 -> 2000x1500 | b-spline | 37 yx | 31 yx | 28 yx | 22 yx | 22 yx | **0.60** | 0.72 |
| 32rgba | 4000x3000 -> 2000x1500 | lanczos3 | 54 yx | 43 yx | 39 yx | 30 yx | 30 yx | **0.54** | 0.69 |
| 24rgb | 4000x3000 -> 2000x1500 | box | 12 yx | 12 yx | 11 yx | 10 yx | 10 yx | **0.78** | 0.81 |
| 24rgb | 4000x3000 -> 2000x1500 | b-spline | 28 yx | 25 yx | 21 yx | 18 yx | 18 yx | **0.63** | 0.71 |
| 24rgb | 4000x3000 -> 2000x1500 | lanczos3 | 40 yx | 34 yx | 30 yx | 24 yx | 24 yx | **0.59** | 0.68 |
| 8grey | 4000x3000 -> 2000x1500 | box | 5 yx | 5 yx | 4 yx | 4 yx | 4 yx | **0.74** | 0.76 |
| 8grey | 4000x3000 -> 2000x1500 | b-spline | 11 yx | 10 yx | 8 yx | 7 yx | 7 yx | **0.58** | 0.65 |
| 8grey | 4000x3000 -> 2000x1500 | lanczos3 | 17 yx | 15 yx | 12 yx | 10 yx | 10 yx | **0.55** | 0.65 |
| 32rgba | 2000x1500 -> 8000x6000 | box | 168 xy | 158 xy | 153 xy | 140 xy | 139 xy | **0.83** | 0.88 |
| 32rgba | 2000x1500 -> 8000x6000 | b-spline | 231 xy | 212 xy | 202 xy | 166 xy | 165 xy | **0.72** | 0.78 |
| 32rgba | 2000x1500 -> 8000x6000 | lanczos3 | 272 xy | 252 xy | 241 xy | 189 xy | 189 xy | **0.69** | 0.75 |
| 24rgb | 2000x1500 -> 8000x6000 | box | 126 xy | 125 xy | 116 xy | 105 xy | 106 xy | **0.84** | 0.85 |
| 24rgb | 2000x1500 -> 8000x6000 | b-spline | 171 xy | 164 xy | 155 xy | 128 xy | 130 xy | **0.76** | 0.79 |
| 24rgb | 2000x1500 -> 8000x6000 | lanczos3 | 209 xy | 195 xy | 183 xy | 149 xy | 150 xy | **0.72** | 0.77 |
| 8grey | 2000x1500 -> 8000x6000 | box | 45 xy | 45 xy | 41 xy | 37 xy | 37 xy | **0.81** | 0.81 |
| 8grey | 2000x1500 -> 8000x6000 | b-spline | 62 xy | 60 xy | 53 xy | 47 xy | 45 xy | **0.72** | 0.75 |
| 8grey | 2000x1500 -> 8000x6000 | lanczos3 | 77 xy | 73 xy | 65 xy | 53 xy | 54 xy | **0.70** | 0.74 |

geomean final/b6e3af3 0.699 (worst 0.97, best 0.54); final/a13e209 0.744 (worst 0.98)
per commit: C1/a13e209 0.922 (0.77-1.01), C2/C1 0.863 (0.71-1.05), C3/C2 0.935 (0.62-1.01)

MSVC's `a13e209` is ahead only on box reductions of large images (up to 5%): its `VerticalStrips` converts on every
tap, as the new kernel's direct path does, a little faster.

## 5. Verification

Gates on the final code, and on each commit where it matters:

- **Same output as HEAD, each commit on its own** (`C1`, `C2`): 60 regular cases (10 shapes, every filter) and 24 huge-window
  cases (windows up to 300000 pixels, through the 4 MB fallback) give HEAD's hashes under GCC and MSVC;
  and each gives the old oracle's HEAD hash `5951cf7c6828346f` (485,333 checks, 1 and 8 threads) and the differential's
  12,252 signatures unchanged.
- **The final kernels in either forced order** give `a13e209`'s output: 168 cases under each compiler.
- **The resize oracle** (`verify4.cpp` with its `orderXY()` mirroring the new rule as `verify5.cpp`): 485,333 checks, 0
  failures, under GCC at 1 and 8 threads (hash `041cca732abc5b44`), with ASan and UBSan at -O0, and under MSVC 14.29 at 1 and 8
  threads (`f9ef8449cb22d09d`; the two CRTs' `sin()` differ in the last bit, which moves the lanczos3 weights).
- **The differential** (12,252 signatures over every layout, palettes and TRUE_COLOR included): 12,023 unchanged; the other 229
  are the 8-, 24- and 32-bit cases whose order `C3` flips, and each equals HEAD's code forced into the new order. The same at 1
  and 4 threads, and with bands of 1 and 3 rows.
- **Warnings**: GCC -Wall -Wextra as HEAD (11); zig clang for aarch64, x86 Linux, x64 and x86 Windows as HEAD (4 each). The
  32-bit x86 builds compile the SSE2 kernels too; their 8 sums need more than the 8 xmm registers, so they spill: correct, not
  timed.
- **testAPI and the toolkit dump** (`tkd`, 8202 keys), final code against HEAD: testAPI exits 0 with the same log and the
  same 25 output files; every `tkd` key is unchanged.

## 6. Left alone

- **Your machine.** The 2.0x of `a13e209` was not reproduced (1.31-1.37x per core here, hidden at 8 threads), and nothing here
  ran on an 8-core CPU or on the full 44.5 GB image.
- **AVX2.** The horizontal tap loop is now bound by the two SSE2 multiply/add ports: 0.5 cycles per sample-tap. 256-bit
  multiplies and adds (no FMA, which would change the output) would halve that on AVX2 CPUs like yours, about 1.4-1.6x on the
  big pass by estimate. It needs runtime CPU dispatch, and under MSVC a separately compiled file or care with VEX encoding;
  not started.
- **`/QIntel-jcc-erratum`.** Not added to the project: it removes the up-to-36% code-placement swings of the previous report on
  Intel 6th-10th gen, and does nothing for your 11th gen. A one-line `AdditionalOptions` if you want it for QPV's users.
- **Other image types** (16-bit, float, palette, 1- and 4-bit) keep their kernels and `d60316b`'s GCC-fitted order rule.
- **The fit's data** was taken before `C2` gained its direct path for box reductions; that path makes vertical-first box
  reductions about 12% faster, in cases where the rule already takes that order.
