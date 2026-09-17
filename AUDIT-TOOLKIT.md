# Audit: Source/FreeImageToolkit/ — bug report

Branch `qpv` @ `bb98084`, audited 2026-09-17. Scope: the 14 files in
`Source/FreeImageToolkit/` (9,300 lines). **No files were modified by this audit.**

This is the companion to `AUDIT.md`, whose scope was the 75 files of
`Source/FreeImage/` and which did not reach this directory.

Confidence key:

- **CONFIRMED** — made to happen here, with the sanitizer output or the printed
  pixels quoted in the entry.
- **BY INSPECTION** — a defect in the text of the code that could not be driven from
  the public API on this machine; the entry says what stands in the way.

Every entry is also marked **upstream** or **local**:

- **upstream** — present unchanged in FreeImage 3.18.0. Verified by extracting
  `Source/FreeImageToolkit/*` from commit `6712d75` ("r1806 - v3.18.0 released") and
  diffing. Only seven of the fourteen files differ from 3.18.0 at all: `Resize.cpp`,
  `Rescale.cpp`, `Resize.h`, `Flip.cpp`, `Background.cpp`, `ClassicRotate.cpp` and
  `CopyPaste.cpp`. `BSplineRotate.cpp`, `Channels.cpp`, `Colors.cpp`, `Display.cpp`,
  `JPEGTransform.cpp`, `MultigridPoissonSolver.cpp` and `Filters.h` are byte-identical
  to 3.18.0.
- **local** — introduced on this branch; the entry names the commit.

## Summary

**40 findings.** Twelve of the fourteen files carry at least one (`Resize.h` and
`Filters.h` are clean); finding 40 is in `Source/Utilities.h` and is reported here
because `ClassicRotate.cpp` is what reaches it.

> **25 of the 26 confirmed findings are now fixed**, in 25 commits
> (`28287d1`..`9081915`). Each entry below carries the commit that fixed it. The one
> exception is finding 25, a documented limitation rather than a defect — see
> "Fixing pass" below for why, and for what the fixes were verified against.
**26 are CONFIRMED**, each reproduced here; 14 are BY INSPECTION.
**36 are upstream 3.18.0 defects; 4 are local.**

The four local ones are findings 3 and 4, both from `2bdea22`
("added FreeImage_RescaleRawBits()"); finding 23, from `5112d61`
("added parameter applyAlpha for FillBackgroundBitmap()"); and finding 28, an
incomplete widening in `0a8e25e`. The OpenMP work (`eef30db`, `d4bb86c`, `0bc96b8`)
produced **no** defect at all — see "What was checked and is correct" at the end,
including a byte-for-byte thread-count sweep.

The confirmed findings were re-run against a plain `-O2` build of the same sources as
well as the sanitized one. **Five of them are hard crashes with no sanitizer
involved** — findings 1, 2 (for the non-FIT_BITMAP types), 3, 5 and 7 — and two more
are silent: an empty rectangle returns a black image, and `FreeImage_RescaleRawBits`
walks 32 bytes past the caller's buffer without anything noticing. The table is under
"The same probes on an ordinary build".

| Class | Findings |
|---|---|
| **Heap buffer overflow, write** | 1 `wordspp`/`floatspp` use the rectangle width · 4b `RescaleRawBits` destination sized by the caller, bit depth chosen by the library · 5 multigrid `IRHO[-1]` |
| **Heap buffer overflow, read** | 2 `CWeightsTable` `Weights[-1]` on an empty rectangle |
| **Stack use after scope** | 6 `FillBackgroundBitmap`'s `blend` (8-, 24- and 32-bit) |
| **Null-pointer write / crash** | 3 `scale()` no longer checks its own allocation · 7 4-bit identity palette |
| **Memory leak** | 4a `RescaleRawBits` leaks its source header |
| **Silently wrong output** | 8 verticalFilter's stray `*= 0xFF` · 9 16-bit 565 horizontal offset · 10 `FI_RESCALE_TRUE_COLOR` inverts MINISWHITE · 11 4-bit `FlipHorizontal` at odd widths · 12 `Combine4`/`Paste`/`EnlargeCanvas` at odd x · 13 `IsVisualGreyscaleImage` tests entry 0 only · 14 4-bit `FillBackground` misses the last column · 16 `RotateEx` mangles FIT_FLOAT/UINT32/INT32 · 17 the skew filters add 0.5 to float samples · 18 `Paste` 555→565 · 19 `Invert` destroys the alpha channel |
| **Does nothing, reports success** | 15 `ApplyPaletteIndexMapping` on 1-bit, documented as supported · 22 `Set/GetComplexChannel` accept a channel they do not implement |
| **Depths refused that the code could handle** | 25 `Rotate` on 4-bit, 16-bit and FIT_INT16, even at exact right angles |
| **API / ABI** | 23 `FreeImage_FillBackground`'s new 4th parameter breaks the Delphi and VB6 wrappers · 24 `AllocateExT` uses a bitmap it has not checked |
| **Undefined behaviour, portability** | 40 `AssignPixel`'s unaligned `WORD`/`DWORD` accesses · 29 `int` overflow in the B-spline allocation · 28 32-bit row offsets in 64-bit `FreeImage_Copy` · 36/37 null-pointer arithmetic and a /256 blend |

The five I would fix first:

1. **6 (`Background.cpp:268`)** — `RGBQUAD blend` is declared inside an `if` block and
   `color_intl = &blend` on the next line outlives it. Every `FreeImage_FillBackground`,
   `FreeImage_AllocateEx(T)` and `FreeImage_EnlargeCanvas` call with
   `FI_COLOR_IS_RGBA_COLOR` and a partly transparent colour reads a dead stack object.
   Moving two declarations out of the block fixes it. Upstream, and it is the oldest
   and most widely reachable of these.
2. **1 (`Resize.cpp:1142`, `:1176`, `:1214`, `:1256`)** — `FreeImage_GetLine(src)` is
   divided by the *rectangle* width, so every `FreeImage_RescaleRect` of a rectangle
   narrower than the image writes past the end of the destination row for
   FIT_UINT16, FIT_RGB16, FIT_RGBA16, FIT_FLOAT, FIT_RGBF and FIT_RGBAF. One
   substitution: use `FreeImage_GetWidth(src)`.
3. **3 (`Resize.cpp:337`)** — restore the `if (!dst) return NULL;` that `2bdea22`
   deleted. `FreeImage_Rescale(dib, 100000, 100000, …)` segfaults today.
4. **4 (`Rescale.cpp:97`)** — `FreeImage_RescaleRawBits` leaks its source header on
   every successful call, reports success when `scale()` failed, and lets `scale()`
   pick a destination bit depth the caller never sized a buffer for.
5. **8 (`Resize.cpp:1340`)** — one stray `value *= 0xFF` makes every vertical upscale
   of a 1-bit image with a non-black/white palette saturate to 255.

### Findings by file

| file | lines | findings |
|---|---:|---|
| `Resize.cpp` | 2146 | 1, 2, 3, 7, 8, 9, 10, 26, 27 |
| `Rescale.cpp` | 260 | 4, 38 |
| `Background.cpp` | 900 | 6, 13, 14, 21, 23, 24, 30 |
| `CopyPaste.cpp` | 887 | 12, 18, 28, 35 |
| `ClassicRotate.cpp` | 916 | 17, 25, 34, 40 |
| `Colors.cpp` | 967 | 15, 19, 20, 39 |
| `BSplineRotate.cpp` | 730 | 16, 29 |
| `MultigridPoissonSolver.cpp` | 505 | 5, 33 |
| `JPEGTransform.cpp` | 623 | 31, 32 |
| `Display.cpp` | 230 | 36, 37 |
| `Channels.cpp` | 488 | 22 |
| `Flip.cpp` | 165 | 11 |
| `Resize.h` | 196 | — |
| `Filters.h` | 287 | — |

Finding 40 is in `Source/Utilities.h`; it is listed under `ClassicRotate.cpp` because
that is the file that reaches it and the file a fix would have to be tested against.

## Fixing pass

*Added 2026-09-17, after the report above was written and pushed as `38e5afb`.*

**25 of the 26 confirmed findings are fixed**, in 25 commits — one per finding,
`28287d1`..`9081915` on `qpv`. Each heading below carries the commit that fixed it, and
each commit message carries the before/after measurement. Eleven source files changed —
ten of the fourteen in this directory (`Resize.cpp`, `Rescale.cpp`, `Background.cpp`,
`CopyPaste.cpp`, `Colors.cpp`, `Channels.cpp`, `Flip.cpp`, `BSplineRotate.cpp`,
`ClassicRotate.cpp`, `MultigridPoissonSolver.cpp`) plus `Source/Utilities.h` — and four
language wrappers.

The 14 **BY INSPECTION** findings are not touched: they were the ones that could not be
driven from the public API here, so there is nothing to measure a fix against. They keep
their entries below.

**Finding 25 was deliberately not fixed.** `FreeImage_Rotate` refuses 4-bit and 16-bit
images, and that is what its own documentation says it does — "Rotates a 1-, 8-, 24- or
32-bit image" (`ClassicRotate.cpp:746`). Unlike every other confirmed finding it is
neither a crash, nor memory corruption, nor silently wrong output: the caller gets NULL
and can convert first. Making it work is new code, not a fix — 4 bpp needs its own
nibble-level permutation in all three of `Rotate90`/`Rotate180`/`Rotate270` (the generic
path computes `bytespp = GetLine / GetWidth`, which is 0 at 4 bpp), and 16 bpp needs the
source masks propagated into `FreeImage_AllocateT` in the same three places or the
rotated image loses its 555/565 identity. That is a feature request; it is left open.

### Two adjacent defects found while fixing

Both are described in the report under the finding whose fix exposed them, and both are
in the commit that fixes it:

* `FreeImage_RescaleRawBits` silently did nothing when the source rectangle already had
  the destination size: `scale()`'s early exit hands back a freshly allocated bitmap
  rather than writing through the header wrapped around `dst_bits`, and the function
  returned 1 with the caller's buffer untouched (`c6f6e12`, finding 4e).
* `FillBackgroundBitmap` entered its alpha-blending block on `FI_COLOR_IS_RGBA_COLOR`
  alone, although `FI_COLOR_ALPHA_IS_INDEX` — which `FreeImage_AllocateExT` sets itself
  on the way in — says the same byte is a palette index. The blend overwrote the index
  with 0xFF, so `FreeImage_AllocateEx(w, h, 8, &grey, FI_COLOR_IS_RGBA_COLOR)` filled
  with white (`e567527`, finding 21).

### How the fixes were verified

* `.claude/audit/toolkit/tk.c` grew to 40 named probes. Every one was run before its
  fix and after it, and the before/after lines are quoted in the commit messages. The
  whole set runs clean under ASan + UBSan + LeakSanitizer; the only sanitizer output
  left anywhere is one pre-existing UBSan report inside `Source/LibJPEG/jdhuff.c:529`,
  which probe `Z` reaches by loading a JPEG and which is outside this directory.
* `.claude/audit/toolkit/bench.c` is a 2,281-case regression matrix: 23 source images
  (every bit depth and image type, plus the three palette shapes that used to crash or
  come out wrong)
  through rescale at six filters in five shapes, `RescaleRect` with offsets, rotate at
  twelve angles, flip, copy/paste/view, fill/allocate/enlarge with five colour options,
  invert/gamma/brightness/contrast/curve/histogram/colour-and-index mapping, channel
  extraction and insertion, composite, premultiply, the Poisson solver and two tone
  mappers. Each signature covers dimensions, type, bit depth, masks, colour type,
  transparency, palette and pixels.
* After every fix the matrix was re-run and diffed by key (`cmp.py` — `sort`/`join` are
  unusable here, they collate `.90` and `.-90` together). **Every commit's diff is
  either empty or exactly the rows that finding describes**, and the commit message
  names them.
* The whole pass was also measured end to end: `Source/` at `bb98084` was restored into
  a second build tree (`.claude/audit/stock_ref`) and the *current* matrix run against
  it. It does not finish — it segfaults 224 rows in, on the first 4-bit
  identity-palette rescale, which is finding 7. Of the 224 rows it does produce, 13
  differ from the fixed build: `rescale.truecolor.1white` (finding 10) and the twelve
  `rescale.up.1mid.*` / `rescale.vonly.1mid.*` over all six filters (finding 8). That no
  before/after total can be quoted for the rest is itself the result: the matrix already
  steers around the inputs that crash the unfixed library, and it still cannot get past
  the fourth source image.
* The OpenMP sweep — probe `Q` at `OMP_NUM_THREADS` = 1, 2, 4, 8 and 16 — is still
  byte-identical, and so is the whole 2,281-case matrix at 8 threads versus 1.
* Every changed `.cpp` was compiled with `-Wall -Wextra` against its pre-fix self: **no
  new warning class or count anywhere**, and two disappear — 14 `-Wswitch` in
  `Channels.cpp` and 2 `-Wconversion-null` in `Rescale.cpp`. All 91 translation units
  under `Source/FreeImage`, `Source/FreeImageToolkit`, `Source/Metadata` and
  `Source/FreeImageLib` still compile after the `Utilities.h` change.
* `AssignPixel`'s constant-size `memcpy` costs nothing: `objdump` of the rebuilt `-O3`
  `ClassicRotate.o` contains no call to `memcpy` at all.

---

# Resize.cpp / Rescale.cpp / Resize.h

## 1. `wordspp` and `floatspp` divide by the rectangle width, not the image width — CONFIRMED, upstream  **Fixed in `1505c3f`.**

`Resize.cpp:1142`, `:1176`, `:1214`, `:1256` (horizontalFilter) and `:1968`, `:2009`,
`:2055`, `:2105` (verticalFilter):

```cpp
// Calculate the number of words per pixel (1 for 16-bit, 3 for 48-bit or 4 for 64-bit)
const INT64 wordspp = (FreeImage_GetLine(src) / src_width) / sizeof(WORD);
```

`FreeImage_GetLine(src)` is the length of a whole source row. `src_width` is the width
of the *rectangle being rescaled*, which `FreeImage_RescaleRect` lets the caller make
narrower than the image. The quotient is then larger than the true samples-per-pixel,
and it is used both to step the source pointer **and** to step the destination:

```cpp
dst_bits[0] = (WORD)CLAMP<int>((int)(r + 0.5), 0, 0xFFFF);
…
dst_bits += wordspp;                  // Resize.cpp:1205
```

A 100-pixel-wide FIT_RGB16 image rescaled from a 50-pixel-wide rectangle gives
`wordspp = (600/50)/2 = 6` instead of 3, so twelve bytes are written per output pixel
into a six-byte-per-pixel row:

```
src 48bpp 100x8 line=600 pitch=600
wordspp the code computes = (600/50)/2 = 6   (correct: 3)
==1648110==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x746c131e05e0
WRITE of size 2 at 0x746c131e05e0 thread T0
    #0 CResizeEngine::horizontalFilter(...) Source/FreeImageToolkit/Resize.cpp:1202
    #3 CResizeEngine::scale(...) Source/FreeImageToolkit/Resize.cpp:409
    #4 FreeImage_RescaleRect Source/FreeImageToolkit/Rescale.cpp:78
0x746c131e05e0 is located 0 bytes after 1376-byte region [0x746c131e0080,0x746c131e05e0)
```

The same call shape overflows at `Resize.cpp:1166` for FIT_UINT16 and at `:1282` for
FIT_FLOAT/RGBF/RGBAF.

`verticalFilter` has the identical expression with `width` in place of `src_width`,
and it is reachable too — through the *yx* branch of `scale()` (`Resize.cpp:475`),
which passes the original source together with the rectangle width. Widening the same
100×8 FIT_RGB16 image from a 50-wide rectangle to 80:

```
==1649510==ERROR: AddressSanitizer: heap-buffer-overflow
WRITE of size 2 at 0x7917be3e06d0 thread T0
    #0 CResizeEngine::verticalFilter(...) Source/FreeImageToolkit/Resize.cpp:2042
    #3 CResizeEngine::scale(...) Source/FreeImageToolkit/Resize.cpp:475
    #4 FreeImage_RescaleRect Source/FreeImageToolkit/Rescale.cpp:78
```

The fix is `FreeImage_GetWidth(src)` in all eight places — that is what the quantity
means, and it is what `Colors.cpp:113` already uses for the same computation.

## 2. `CWeightsTable` reads `Weights[-1]` when the rectangle is empty — CONFIRMED, upstream  **Fixed in `71aa1b2`.**

`Resize.cpp:205-216`:

```cpp
// simplify the filter, discarding null weights at the right
int iTrailing = iRight - iLeft - 1;
while(m_WeightTable[u].Weights[iTrailing] == 0) {
    m_WeightTable[u].Right--;
    iTrailing--;
    if(m_WeightTable[u].Right == m_WeightTable[u].Left) {
        break;
    }
}
```

`FreeImage_RescaleRect` normalises and range-checks the rectangle but never rejects an
empty one: `src_left == src_right` passes `(src_left < 0) || (src_right > src_width)`.
`scale()` then calls `CWeightsTable(pFilter, dst_width, 0)`, where `dScale` is
`+inf`, `dCenter` is 0 and both `iLeft` and `iRight` clamp to 0. `iTrailing` is `-1`
and the loop reads `Weights[-1]` *before* it can take the break:

```
==1648118==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x6ea436be0098
READ of size 8 at 0x6ea436be0098 thread T0
    #0 CWeightsTable::CWeightsTable(...) Source/FreeImageToolkit/Resize.cpp:208
    #3 FreeImage_RescaleRect Source/FreeImageToolkit/Rescale.cpp:78
0x6ea436be0098 is located 8 bytes before 24-byte region [0x6ea436be00a0,0x6ea436be00b8)
```

Reproduced with `FreeImage_RescaleRect(src, 10, 16, 5, 0, 5, 16, FILTER_BILINEAR, 0)`
on both a 24-bit and a 48-bit source.

What happens next depends on the eight bytes the read lands on, because the loop
continues *while* it finds zeros:

* If they are non-zero, the loop body never runs. That is the ordinary case on glibc,
  where `Weights[-1]` is the malloc chunk's size field. `Right` stays equal to `Left`,
  every output pixel sums over an empty window, and `FreeImage_RescaleRect` returns a
  black image with no diagnostic. On the stock (non-sanitised) build of the same
  commit: `B: dst=0x5d60af68fa50  row0 = 0 0 0 0 0 0 0 0 0`.
* If they are zero, `m_WeightTable[u].Right--` wraps — `Right` is `unsigned` — so the
  `Right == Left` break can never fire afterwards, the walk continues backwards
  through the heap, and `getRightBoundary()` then hands the filter loop a bound near
  `0xFFFFFFFF`.

The non-FIT_BITMAP types do not get that far: `FreeImage_GetLine(src) / src_width` at
`Resize.cpp:1142` divides by the same zero. On the stock build,
`./tk_stock B2` (the 48-bit source) dies with `Floating point exception (core dumped)`.

Three things are wrong and all three should be fixed: `FreeImage_RescaleRect` and
`FreeImage_RescaleRawBits` should reject an empty rectangle; the trailing-zero trim
should test `iTrailing >= 0` as its loop condition rather than inferring it from
`Right == Left`; and `Right` should not be a type on which `--` can wrap past `Left`.

## 3. `scale()` no longer checks that it allocated a destination — CONFIRMED, **local** (`2bdea22`)  **Fixed in `28287d1`.**

`Resize.cpp:336-345`:

```cpp
   // allocate the dst image
   FIBITMAP *dst = NULL;
   if (rawBits==1) {
      dst = FreeImage_AllocateHeaderForBits(dst_bits, dst_pitch, image_type, …);
   } else {
      dst = FreeImage_AllocateT(image_type, dst_width, dst_height, dst_bpp, 0, 0, 0);
   }

	if (dst_bpp == 8) {
		RGBQUAD * const dst_pal = FreeImage_GetPalette(dst);
```

`git show 2bdea22 -- Source/FreeImageToolkit/Resize.cpp` shows exactly what was lost:

```diff
-	FIBITMAP *dst = FreeImage_AllocateT(image_type, dst_width, dst_height, dst_bpp, 0, 0, 0);
-	if (!dst) {
-		return NULL;
-	}
-
+   FIBITMAP *dst = NULL;
+   if (rawBits==1) {
```

`dst == NULL` then flows into `FreeImage_GetPalette` (harmless, returns NULL, but
`CREATE_GREYSCALE_PALETTE_REVERSE` would write through it for a MINISWHITE source),
into `horizontalFilter`/`verticalFilter` as `tmp`, and into `FreeImage_GetScanLine`,
which returns NULL for a NULL bitmap. The filters then write through it:

```
$ ./tk D        # FreeImage_Rescale(8x8 24-bit, 100000, 100000, FILTER_BOX)
AddressSanitizer:DEADLYSIGNAL
==1648131==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000002
==1648131==The signal is caused by a WRITE memory access.
    #0 CResizeEngine::horizontalFilter(...) Source/FreeImageToolkit/Resize.cpp:1088
    #3 CResizeEngine::scale(...) Source/FreeImageToolkit/Resize.cpp:499
    #5 FreeImage_Rescale Source/FreeImageToolkit/Rescale.cpp:93
```

A 100000×100000 destination is 30 GB, so this is the ordinary out-of-memory path of a
public function, not a contrived one. Restoring the three deleted lines fixes it.

## 4. `FreeImage_RescaleRawBits` — four defects in 65 lines — CONFIRMED, **local** (`2bdea22`)  **Fixed in `c6f6e12`.**

`Rescale.cpp:96-162`.

**4a. The source header is leaked on every successful call.** The function builds a
wrapper bitmap with `FreeImage_AllocateHeaderForBits` at `:99` and unloads it on each
of the three early-return paths — but not on the success path, which ends

```cpp
   FreeImage_Unload(dst);
   delete pFilter;
   return 1;
```

LeakSanitizer, on a single 16×8 call:

```
E: returned 1
==1648380==ERROR: LeakSanitizer: detected memory leaks
Direct leak of 8 byte(s) in 1 object(s) allocated from:
    #1 FreeImage_AllocateBitmap Source/FreeImage/BitmapAccess.cpp:390
    #2 FreeImage_AllocateHeaderForBits Source/FreeImage/BitmapAccess.cpp:491
    #3 FreeImage_RescaleRawBits Source/FreeImageToolkit/Rescale.cpp:99
Indirect leak of 416 byte(s) in 1 object(s) …
Indirect leak of 48 byte(s) in 1 object(s) …
```

472 bytes and three blocks per call. The function exists to be called in a loop.

**4b. `scale()` chooses the destination bit depth; the caller sized the buffer.**
`CResizeEngine::scale` promotes 16-bit FIT_BITMAP to 24 (`Resize.cpp:273-277`), and a
non-greyscale palette to 24 or 32 (`:249-252`). Nothing reconciles that with
`dst_pitch`/`dst_bits`, which the caller sized for the bit depth it passed in:

```
AA: dst buffer is 64 bytes (16 bpp); scale() will choose 24 bpp -> 96 bytes
==1648752==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x7232825e1201
WRITE of size 1 at 0x7232825e1201 thread T0
    #0 CResizeEngine::verticalFilter(...) Source/FreeImageToolkit/Resize.cpp:1912
    #4 FreeImage_RescaleRawBits Source/FreeImageToolkit/Rescale.cpp:156
0x7232825e1201 is located 1 bytes after 64-byte region [0x7232825e11c0,0x7232825e1200)
```

The call was `FreeImage_RescaleRawBits(sb, db, FIT_BITMAP, 16, 8, 32, 16, /*bpp*/16,
8, 4, 0, 0, 16, 8, FILTER_BOX)` with `db` a 64-byte (16-bpp) buffer. The raw-bits
entry point must either refuse a conversion or document that `dst_bits` has to be
sized for `scale()`'s choice; the header gives the caller no way to find that out.

**4c. It reports success when `scale()` failed.** `dst` is never tested; the function
returns `1` unconditionally at `:161`.

**4d. `return NULL` from a `BOOL` function**, at `:101`. Compiles, but the declared
type in `FreeImage.h:1119` is `BOOL`.

Two smaller notes on the same function: the RGBA masks are passed as
`FI_RGBA_RED_MASK/…` regardless of `bpp`, so a 16-bit source is never recognised as
555 or 565 (it is always treated as 555 by `IS_FORMAT_RGB565`); and the empty
rectangle of finding 2 is reachable here too.

## 7. A 4-bit image with an identity palette dereferences a NULL palette — CONFIRMED, upstream  **Fixed in `402d7fd`.**

`GetExtendedColorType` (`Resize.cpp:68-91`) classifies `pal[i].rgbBlue == i` as
greyscale, so `color_type` stays `FIC_MINISBLACK` and `scale()` never sets `src_pal`
(`:321-334`, which only runs for `FIC_PALETTE`). The 4-bit filter branches, however,
are written on the assumption printed in their own comment — "we always have got a palette for 4-bit images" — and index
`src_pal` unconditionally at `Resize.cpp:720`, `:752`, `:790`, `:1531`, `:1566`,
`:1606`:

```
L: color type = 1 (FIC_MINISBLACK=1 FIC_PALETTE=3)
Source/FreeImageToolkit/Resize.cpp:720:88: runtime error: load of null pointer of type 'BYTE'
==1648333==ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
    #0 CResizeEngine::horizontalFilter(...) Source/FreeImageToolkit/Resize.cpp:720
    #5 FreeImage_Rescale Source/FreeImageToolkit/Rescale.cpp:93
```

Trigger: `FreeImage_Allocate(16,16,4)`, `pal[i] = {i,i,i}`, then
`FreeImage_Rescale(dib, 8, 8, FILTER_BILINEAR)`. This is the same defect recorded
earlier in this repository's notes; it is still present at `bb98084`. The clean fix is
in `GetExtendedColorType`: a 4-bit palette whose entries are 0..15 is *not* the 8-bit
greyscale ramp the 8-bit branches assume, so either it should be reported as
`FIC_PALETTE`, or the 4-bit filter branches need the same `if (src_pal)` split the
1-bit and 8-bit branches already have.

## 8. `verticalFilter` multiplies an already-scaled palette value by 255 — CONFIRMED, upstream  **Fixed in `622124a`.**

`Resize.cpp:1316-1343`, the 1-bit source → 8-bit destination branch **that has a
palette**:

```cpp
if (src_pal) {
    // we have got a palette
    …
        value += (weightsTable.getWeight(y, i) * (double)*(BYTE *)&src_pal[pixel]);
        src_bits += src_pitch;
    }
    value *= 0xFF;                                   // <-- Resize.cpp:1340

    // clamp and place result in destination pixel
    *dst_bits = (BYTE)CLAMP<int>((int)(value + 0.5), 0, 0xFF);
```

`value` is already a weighted average of palette bytes, i.e. 0..255. Multiplying by
255 saturates every non-black sample. There are five `value *= 0xFF` sites in the
file; the other four (`:573`, `:639`, `:1370`, `:1443`) are all in "we do not have a
palette" branches, where `value` is a weighted average of *bits* and the multiply is
correct. `:1340` is the only one on a palette branch, and `horizontalFilter`'s
matching palette branch (`:528-551`) correctly has no multiply.

A 1-bit image whose palette is grey but not black/white — which is what makes
`GetExtendedColorType` return `FIC_PALETTE` with `bIsGreyscale` TRUE, and therefore
what makes `src_pal` non-NULL — shows it. Palette entries 64 and 192:

```
M: colour type=3
M: upscaled  row0 = 255 255 255 255 (bpp 8)      <-- verticalFilter ran first
M: downscaled row0 = 128 128 (bpp 8)             <-- horizontalFilter ran first
M: palette entries are 64 and 192, so every output sample must lie in [64,192]
```

The downscale (`FreeImage_Rescale(dib, 2, 2, …)`, the *xy* path) is right: 128 is the
average of 64 and 192. The upscale (`8, 8`, the *yx* path, which runs `verticalFilter`
first) is 255. Deleting line 1340 fixes it.

## 9. The 16-bit 565 horizontal offset is halved — CONFIRMED, upstream  **Fixed in `055eba2`.**

`Resize.cpp:995`:

```cpp
const WORD * const src_bits = (WORD *)FreeImage_GetScanLine(src, y + src_offset_y) + src_offset_x / sizeof(WORD);
```

against the 555 branch twelve lines below, `Resize.cpp:1028`:

```cpp
const WORD * const src_bits = (WORD *)FreeImage_GetScanLine(src, y + src_offset_y) + src_offset_x;
```

`src_offset_x` is a pixel index and `src_bits` is a `WORD *`, so the 555 form is the
correct one; `verticalFilter`'s 16-bit branch (`:1812`) also uses `+ src_offset_x`.
The 565 branch samples from column `src_left/2`.

Comparing `FreeImage_RescaleRect(src, 4, h, 8, 0, 16, h, FILTER_BOX, 0)` against
`FreeImage_Copy(src, 8, 0, 16, h)` followed by `FreeImage_Rescale(…, 4, h, …)`, which
must agree:

```
C: s565 IS_565=1  s555 IS_565=0
C: 565      RescaleRect(left=8)=31074f018802761b  Copy+Rescale=cd7d085f43c93f7b  DIFFER
C: 555      RescaleRect(left=8)=cd7d085f43c93f7b  Copy+Rescale=cd7d085f43c93f7b  same
C: 24bpp    RescaleRect(left=8)=e7cae1d89e4ec05b  Copy+Rescale=e7cae1d89e4ec05b  same
C: 8bpp     RescaleRect(left=8)=814f92fef8353843  Copy+Rescale=814f92fef8353843  same
```

The 565 hash also differs from the 555 hash, which is the tell: the two sources hold
the same gradient and must rescale to the same 24-bit result.

`Resize.cpp:1146`, `:1180`, `:1218` and `:1260` divide `src_offset_x` the same way for
FIT_UINT16, FIT_RGB16, FIT_RGBA16 and the float types. There the correct expression is
`src_offset_x * wordspp` (which is what `verticalFilter:1974` uses), and the bug is
masked by finding 1, which crashes first.

## 10. `FI_RESCALE_TRUE_COLOR` inverts a FIC_MINISWHITE source — CONFIRMED, upstream  **Fixed in `52e2ca7`.**

`scale()` only repairs the MINISWHITE convention when the destination is palettised,
`Resize.cpp:345-350`:

```cpp
if (dst_bpp == 8) {
    RGBQUAD * const dst_pal = FreeImage_GetPalette(dst);
    if (color_type == FIC_MINISWHITE) {
        // build an inverted greyscale palette
        CREATE_GREYSCALE_PALETTE_REVERSE(dst_pal, 256);
    }
}
```

With `FI_RESCALE_TRUE_COLOR` (`Resize.cpp:265`) `dst_bpp` is 24, so nothing corrects
it; `src_pal` also stays NULL for MINISWHITE (`:321-334`), so the filters take the "we do
not have a palette" branch and emit the raw bit values as grey — which for MINISWHITE
is exactly backwards. A 1-bit image, white on the left half, black on the right:

```
R: colour type = 0 (FIC_MINISWHITE=0)
R:  8bpp out  row0 =   0   0 255 255                  <-- correct (reverse palette)
R: 24bpp out  row0 =   0   0   0   0 (first two pixels)
R: ConvertTo24Bits(src) row0 = 255 ...   0 (left half white, right half black)
```

The 24-bit output is black where the source is white.

## 26. `CWeightsTable`'s two allocations are unchecked — BY INSPECTION, upstream

`Resize.cpp:167-171`:

```cpp
m_WeightTable = (Contribution*)malloc(m_LineLength * sizeof(Contribution));
for(unsigned u = 0; u < m_LineLength; u++) {
    m_WeightTable[u].Weights = (double*)malloc(m_WindowSize * sizeof(double));
}
```

Neither result is tested, and the constructor has no way to report failure. For a
minifying Lanczos3 pass `m_WindowSize` is `2*ceil(3/dScale)+1`, so a 1×1 destination
from a large source asks for a window as wide as the source — the allocation sizes are
driven by the caller's ratio, not by a constant. The destructor `free()`s the same
pointers, so a partial construction also corrupts the heap. Not reproduced here: on
this machine the sizes needed to fail the allocation are also large enough that the
source bitmap cannot be allocated first.

## 27. Sub-byte horizontal offsets are silently truncated — BY INSPECTION, upstream

`horizontalFilter` does `src_offset_x >>= 3` for 1-bit (`Resize.cpp:527`, `:586`,
`:657`) and `>>= 1` for 4-bit (`:703`, `:734`, `:772`); `verticalFilter` does
`+ (src_offset_x >> 3)` (`:1310`) and `+ (src_offset_x >> 1)` (`:1506`) and then
indexes the source by the *destination* column `x`, not by `src_offset_x + x`. The
remainder is lost in every case, so `FreeImage_RescaleRect` on a 1-bit image with
`left` not a multiple of 8, or a 4-bit image with an odd `left`, samples the wrong
columns. `FreeImage_CreateView` documents exactly this restriction and rejects such
offsets (`CopyPaste.cpp:783-785`, `:819-832`); `FreeImage_RescaleRect` does not.

## 38. `FreeImage_MakeThumbnail` — dead code, and `<` where `<=` is meant — BY INSPECTION, upstream

`Rescale.cpp:169-176`:

```cpp
if(!FreeImage_HasPixels(dib) || (max_pixel_size <= 0)) return NULL;
…
if(max_pixel_size == 0) max_pixel_size = 1;

if((width < max_pixel_size) && (height < max_pixel_size)) {
```

The `== 0` test can never be true after the `<= 0` rejection. The `<` on the next
comparison means an image exactly `max_pixel_size` wide is rescaled to its own size
instead of being cloned. Neither is a correctness problem for callers; both are noise
that hides the intent.

---

# Background.cpp

## 6. `FillBackgroundBitmap` keeps a pointer to a dead stack object — CONFIRMED, upstream  **Fixed in `34fd6c0`.**

`Background.cpp:253-274`:

```cpp
		if (color->rgbReserved < 255) {
			…
			RGBQUAD bgcolor;
			…
			RGBQUAD blend;                                       // 268
			GetAlphaBlendedColor(&bgcolor, color_intl, &blend);  // 269
			color_intl = &blend;                                 // 270
		}                                                        // 271  <-- blend dies here
	}

	int index = (bpp <= 8) ? GetPaletteIndex(dib, color_intl, options, &color_type) : 0;   // 274
```

`blend` is scoped to the inner `if`. `color_intl` escapes it and is then read at
`:274` (8-bit, through `GetPaletteIndex`), `:317` (16-bit, `RGBQUAD_TO_WORD`), `:324`
(24-bit, `*((RGBTRIPLE *)color_intl)`) and `:332-334` (32-bit). Confirmed at all three
reachable depths — `supports_alpha` at `:237` excludes 16-bit — with a half
transparent fill colour:

```
AF: bpp=8  ==1648894==ERROR: AddressSanitizer: stack-use-after-scope
    #0 GetPaletteIndex Source/FreeImageToolkit/Background.cpp:101
    #1 FillBackgroundBitmap Source/FreeImageToolkit/Background.cpp:274
AF: bpp=24 ==1648897==ERROR: AddressSanitizer: stack-use-after-scope  READ of size 3
    #0 FillBackgroundBitmap Source/FreeImageToolkit/Background.cpp:324
AF: bpp=32 ==1648900==ERROR: AddressSanitizer: stack-use-after-scope  READ of size 1
    #0 FillBackgroundBitmap Source/FreeImageToolkit/Background.cpp:332
```

It also fires through `FreeImage_AllocateEx`, which is how most callers reach it:

```
==1648833==ERROR: AddressSanitizer: stack-use-after-scope
    #0 GetPaletteIndex Source/FreeImageToolkit/Background.cpp:93
    #1 FillBackgroundBitmap Source/FreeImageToolkit/Background.cpp:274
    #2 FreeImage_FillBackground Source/FreeImageToolkit/Background.cpp:438
    #3 FreeImage_AllocateExT Source/FreeImageToolkit/Background.cpp:631
    #4 FreeImage_AllocateEx Source/FreeImageToolkit/Background.cpp:712
```

This usually "works": at `-O0` the slot is not reused between `:271` and `:274`. It is
the kind of bug that changes behaviour when the compiler inlines differently or the
optimiser reuses the frame. Moving `bgcolor` and `blend` to the top of the function
fixes it, and costs nothing.

## 13. `IsVisualGreyscaleImage` examines palette entry 0 `ncolors` times — CONFIRMED, upstream  **Fixed in `f661d62`.**

`Background.cpp:42-49`:

```cpp
unsigned ncolors = FreeImage_GetColorsUsed(dib);
RGBQUAD *rgb = FreeImage_GetPalette(dib);
for (unsigned i = 0; i< ncolors; i++) {
    if ((rgb->rgbRed != rgb->rgbGreen) || (rgb->rgbRed != rgb->rgbBlue)) {
        return FALSE;
    }
}
return TRUE;
```

`i` is never used and `rgb` is never advanced. Any palettised image whose *first*
entry is grey is declared greyscale, whatever the other 255 entries hold.
`GetPaletteIndex` then converts the requested colour to a grey level and searches for
the nearest grey, so `FreeImage_FillBackground`, `FreeImage_AllocateExT` and
`FreeImage_EnlargeCanvas` pick the wrong palette index. With a palette of
{black, pure red, pure green, pure blue} and a request for pure red:

```
AG: palette[0] coloured -> FillBackground(red) picks index 1 (index 1 is pure red)
AG: palette[0] grey     -> FillBackground(red) picks index 0 (index 1 is pure red)
```

The only difference between the two runs is whether entry 0 happens to be grey. Fix:
`rgb[i]`, or `rgb++` in the loop body.

## 14. 4-bit `FillBackground` tests the wrong parity and misses the last column — CONFIRMED, upstream  **Fixed in `4065a7f`.**

`Background.cpp:302-310`:

```cpp
case 4: {
    unsigned bytes = (width / 2);
    memset(dst_bits, (index | (index << 4)), bytes);
    //if (bytes % 2) {
    if (bytes & 1) {
        dst_bits[bytes] &= 0x0F;
        dst_bits[bytes] |= (index << 4);
    }
    break;
}
```

The trailing half-byte exists when **`width`** is odd, not when `bytes` is. The two
agree only when `width % 4 == 3`:

```
I: w=1 ->  0   <-- NOT fully filled
I: w=2 -> 15 15
I: w=3 -> 15 15 15
I: w=4 -> 15 15 15 15
I: w=5 -> 15 15 15 15  0   <-- NOT fully filled
I: w=6 -> 15 15 15 15 15 15
I: w=7 -> 15 15 15 15 15 15 15
I: w=8 -> 15 15 15 15 15 15 15 15
I: w=9 -> 15 15 15 15 15 15 15 15  0   <-- NOT fully filled
```

Widths ≡ 1 (mod 4) leave the rightmost column of every row untouched; a 1-pixel-wide
4-bit image is not filled at all. The mirror case, width ≡ 2 (mod 4), writes
`dst_bits[bytes]` one byte past the end of the *line* — that byte is inside the
scanline's alignment padding, so it is not a memory error, but it is not image data
either. `if (width & 1)` is the whole fix. (The 1-bit case immediately above,
`:286-299`, gets the same question right: it tests `width & 7`.)

## 21. `AllocateExT`'s 8-bit case leaves its substitute colour uninitialised — CONFIRMED, upstream  **Fixed in `e567527`.**

`Background.cpp:603-620`:

```cpp
case 8: {
    RGBQUAD *rgb = (RGBQUAD *)color;
    RGBQUAD *pal = FreeImage_GetPalette(bitmap);
    RGBQUAD rgbq;                              // <-- 607, indeterminate
    …
        if ((rgb->rgbRed == rgb->rgbGreen) && (rgb->rgbRed == rgb->rgbBlue)) {
            CREATE_GREYSCALE_PALETTE(pal, 256);
            rgbq.rgbReserved = rgb->rgbRed;    // only this member is set
            color = &rgbq;
```

The 1-bit and 4-bit cases twenty lines above both write `RGBQUAD rgbq = RGBQUAD();`
(`:541`, `:576`). The 8-bit one does not, and its `rgbRed`/`rgbGreen`/`rgbBlue` are
read by `GetAlphaBlendedColor` (`Background.cpp:196-198`) whenever the caller passes
`FI_COLOR_IS_RGBA_COLOR` with `0 < rgbReserved < 255`. AddressSanitizer does not
detect uninitialised reads at all — that is MemorySanitizer's job, and no MSan or
valgrind build was available here — so what the run below shows is the *call path*,
which is the same one finding 6 covers and which ASan does catch, a few statements
later, at the other end:

```
AD: ==1648833==ERROR: AddressSanitizer: stack-use-after-scope
    #0 GetPaletteIndex Source/FreeImageToolkit/Background.cpp:93
    #3 FreeImage_AllocateExT Source/FreeImageToolkit/Background.cpp:631
```

So the uninitialised read itself is BY INSPECTION; the call path that reaches it is
CONFIRMED. Adding `= RGBQUAD()` to `:607` matches the two cases beside it.

While tracing this, two neighbouring behaviours are worth writing down because they
look like bugs to a caller and are not covered elsewhere: with
`FI_COLOR_IS_RGBA_COLOR` and an alpha of 0, `FillBackgroundBitmap` returns TRUE
without filling anything (`:243-246`); and the blended colour always comes back with
`rgbReserved = 0xFF` (`:199`), which `GetPaletteIndex` — now running with the
implicit `FI_COLOR_ALPHA_IS_INDEX` that `:628` added — returns verbatim, so the fill
uses palette index 255 rather than the requested grey.

## 23. `FreeImage_FillBackground`'s new fourth parameter — CONFIRMED, **local** (`5112d61`)  **Fixed in `9081915`.**

The signature became

```c
DLL_API BOOL DLL_CALLCONV FreeImage_FillBackground(FIBITMAP *dib, const void *color,
        int options FI_DEFAULT(0), int applyAlpha FI_DEFAULT(0));   /* FreeImage.h:1153 */
```

Three consequences, none of them recorded anywhere in the tree:

**23a. The Delphi and VB6 wrappers no longer bind on Win32.** `DLL_CALLCONV` is
`__stdcall` there (`FreeImage.h:44`), so the decorated export name is derived from the
argument byte count and has moved from `@12` to `@16`. Both wrappers still ask for the
old one:

```
Wrapper/Delphi/src/FreeImage.pas:1631:   external FIDLL {$IFDEF WIN32}name '_FreeImage_FillBackground@12'{$ENDIF}
Wrapper/VB6/src/MFreeImage.bas:2196: Public Declare Function FreeImage_FillBackground Lib "FreeImage.dll" Alias "_FreeImage_FillBackground@12" ( _
```

That is a load-time failure, not a silent one. `Wrapper/AHK/freeimage-wrapper.ahk:450`
*was* updated and passes four arguments. (Win64 and Linux are unaffected: no name
decoration.)

**23b. It is a source break for C callers.** `FI_DEFAULT(x)` expands to nothing
outside C++ (`FreeImage.h:115`), so every existing three-argument C call now fails to
compile.

**23c. `applyAlpha` is an `int` assigned to a `BYTE`, and 0 is not a usable value.**
`Background.cpp:281-282` remaps 0 to 0xFF and `:335` truncates:

```
AE: applyAlpha=  0 -> pixel rgba = 10 20 30 255
AE: applyAlpha=  1 -> pixel rgba = 10 20 30 1
AE: applyAlpha=128 -> pixel rgba = 10 20 30 128
AE: applyAlpha=255 -> pixel rgba = 10 20 30 255
AE: applyAlpha=256 -> pixel rgba = 10 20 30 0
AE: applyAlpha=511 -> pixel rgba = 10 20 30 255
```

A fully transparent fill is unrequestable, and 256 quietly means 0. A `BYTE` parameter
with a separate `BOOL` would say what is meant; failing that, the range needs
documenting and clamping.

## 24. `FreeImage_AllocateExT` uses a bitmap it has not checked — CONFIRMED, upstream  **Fixed in `7388685`.**

`Background.cpp:521-528`:

```cpp
FIBITMAP *bitmap = FreeImage_AllocateT(type, width, height, bpp, red_mask, green_mask, blue_mask);

if (!color) {
    if ((palette) && (type == FIT_BITMAP) && (bpp <= 8)) {
        memcpy(FreeImage_GetPalette(bitmap), palette, FreeImage_GetColorsUsed(bitmap) * sizeof(RGBQUAD));
    }
    return bitmap;
}

if (bitmap != NULL) {
```

The `bitmap != NULL` guard is only on the *colour* path. With `color == NULL` and a
palette, a failed allocation reaches `memcpy(NULL, palette, 0)`:

```
==1648385==WARNING: AddressSanitizer failed to allocate 0x2540be9a0 bytes
Source/FreeImageToolkit/Background.cpp:525:10: runtime error: null pointer passed as argument 1, which is declared to never be null
N: AllocateExT(huge, color=NULL, palette!=NULL) = (nil)
```

Benign in practice — the length is 0 — but it is undefined behaviour, and it is
exactly the check that `FreeImage_Copy` and `FreeImage_CreateView` were given in this
repository (`CopyPaste.cpp:559-568`, `:860-868`) with the comment "memcpy() may not be
passed a NULL pointer, not even with a length of 0". The same reasoning applies here.

## 30. `FreeImage_EnlargeCanvas` can compute a non-positive size — BY INSPECTION, upstream

`Background.cpp:808-814`:

```cpp
	if (((left < 0) && (-left >= width)) || ((right < 0) && (-right >= width)) ||
		((top < 0) && (-top >= height)) || ((bottom < 0) && (-bottom >= height))) {
		return NULL;
	}

	unsigned newWidth = width + left + right;
	unsigned newHeight = height + top + bottom;
```

The guard rejects each side individually but not their sum, so `left = right = -9` on
a 10-wide image (with some other side positive, or the `FreeImage_Copy` shortcut at
`:798` takes over) yields `newWidth = -8`. The documentation at `:761` promises NULL
in this case. What actually saves it today is the `if (width < 0) return NULL;` added
to `FreeImage_AllocateBitmap` (`Source/FreeImage/BitmapAccess.cpp:309`) during the
previous audit:

```
O: EnlargeCanvas(left=-9,right=-9,top=+5) on a 10x10 = (nil)
```

Left as-is, the memcpy loop at `:870-874` would run with a negative `lineWidth`
(`:861`) converted to a huge `size_t`. The check belongs here too, next to the one that
is already written.

---

# CopyPaste.cpp

## 12. `Combine4` never shifts the source nibbles for an odd x — CONFIRMED, upstream  **Fixed in `120a452`.**

`CopyPaste.cpp:139-184`. `dst_bits` is positioned at byte `x >> 1`, the row is copied
straight across with `memcpy`, and the only concession to a half-byte destination
offset is a fix-up of the first and last byte:

```cpp
if (bOddStart) {
    buffer[0] = HINIBBLE(dst_bits[0]) + LOWNIBBLE(buffer[0]);
}
if (bOddEnd) {
    buffer[src_line - 1] = HINIBBLE(buffer[src_line - 1]) + LOWNIBBLE(dst_bits[src_line - 1]);
}
memcpy(dst_bits, buffer, src_line);
```

Nothing shifts the payload by a nibble, so the pasted image lands one pixel to the
left and the two end pixels are replaced by destination content. Pasting `1 2 3 4`
into an empty 8-pixel row at x = 1:

```
G: Paste(left=1) ok=1  dst = 0 2 3 0 0 0 0 0   (expected 0 1 2 3 4 0 0 0)
```

`FreeImage_Paste` returns TRUE. The public route most people will hit is
`FreeImage_EnlargeCanvas`, which for `bpp <= 4` goes through `FreeImage_Copy` +
`FreeImage_Paste` (`Background.cpp:830-850`):

```
V: left=1 -> 0 2 3 0 0   (expected 0 1 2 3 4)
V: left=2 -> 0 0 1 2 3 4   (expected 0 0 1 2 3 4)
```

An even border is fine; an odd one loses two pixels of a four-pixel image. The fix is
to shift the whole buffer by one nibble when `bOddStart`, i.e. to build the output row
as `(buffer[i] >> 4) | (buffer[i+1] << 4)`, and to write `src_line + 1` bytes.

## 18. `FreeImage_Paste` takes the 16-bit format from the destination only — CONFIRMED, upstream  **Fixed in `fb11ade`.**

`CopyPaste.cpp:677-682` derives `isRGB565` from `dst`'s masks, and `:685-686` skips
conversion whenever the two bit depths are equal:

```cpp
if(bpp_dst == bpp_src) {
    clone = src;
```

Two 16-bit images with *different* masks therefore reach `Combine16_565`, which
`memcpy`s the raw words (`:314-319`):

```
W: Paste 555 -> 565 ok=1  dst[0]=0x7c00 (555 red 0x7c00, 565 red 0xf800)
```

Pure red in 555 is stored unchanged into a 565 bitmap, where that bit pattern is a
dark green. `FreeImage_Paste` returns TRUE. `Combine16_555` and `Combine16_565` check
only `FreeImage_GetBPP(...) != 16` (`:241`, `:301`) and never look at the masks of
either image.

## 28. `FreeImage_Copy`'s 1-bit and 4-bit row offsets are still 32-bit — BY INSPECTION, **local** (`0a8e25e`)

`CopyPaste.cpp:571-601`. The commit widened `src_width`, `dst_width`, `dst_line`,
`dst_pitch` and `src_pitch` to `INT64`, and the 4-bit loop counters with them, but the
row offsets kept their original type:

```cpp
	if (bpp == 1) {
		BOOL value;
		unsigned y_src, y_dst;                 // 574

		for (int y = 0; y < dst_height; y++) { // 577  - still int
			y_src = y * src_pitch;             // INT64 product truncated into unsigned
```

and the same at `:590`. `y * src_pitch` is computed in 64 bits and then truncated, so a
1-bit or 4-bit image whose pixel data exceeds 4 GiB copies from and to the wrong rows.
The `bpp >= 8` branch below (`:607-611`) does the arithmetic in `INT64` throughout and
is correct. Not reproduced: a 4 GiB 1-bit bitmap is 34 gigapixels.

## 35. `FreeImage_CreateView`'s `left < 0` and `top < 0` tests are dead — BY INSPECTION, upstream

`CopyPaste.cpp:812`: both parameters are `unsigned`. The condition is harmless because
`right > width` covers the real case, but it is what a reader checks first.

---

# Flip.cpp

## 11. 4-bit `FreeImage_FlipHorizontal` reverses bytes, including the pad nibble — CONFIRMED, upstream  **Fixed in `8dd1c58`.**

`Flip.cpp:65-76`:

```cpp
case 4 :
{
    for(unsigned c = 0; c < line; c++) {
        bits[c] = new_bits[line - c - 1];

        BYTE nibble = (bits[c] & 0xF0) >> 4;

        bits[c] = bits[c] << 4;
        bits[c] |= nibble;
    }
}
```

Reversing whole bytes and then swapping nibbles is a correct pixel reversal only when
the row holds an even number of pixels. With an odd width the last byte's low nibble
is padding, and reversing brings it to the front:

```
F: before 1 2 3 4 5
F: after  0 5 4 3 2    (expected 5 4 3 2 1)
```

Every pixel is displaced one position and the first is lost. The other depths handle
this correctly because they work in pixel units (`:82`, `:92`, `:107`); the 1-bit case
uses `x` and `width - 1 - x` explicitly (`:55-60`) and is right.

## Notes on `Flip.cpp` that are *not* bugs

`bytespp = FreeImage_GetLine(src) / FreeImage_GetWidth(src)` at `:40` is 0 for 1- and
4-bit images, but neither of those cases uses it. `0xff7f >> (new_x & 0x7)` at `:60`
looks like a typo for `~(0x80 >> …)` and is not: truncation to `BYTE` makes the two
identical for every shift 0..7. The `?:` with assignments in both arms parses as
intended in C++ (it would not in C). `FreeImage_FlipVertical`'s switch to copying
`line` rather than `pitch` bytes (`3a57bb4`) is correct and is what makes it work on
`FreeImage_CreateView` results.

---

# ClassicRotate.cpp

## 17. The skew filters add 0.5 to float samples — CONFIRMED, upstream  **Fixed in `5d670e6`.**

`ClassicRotate.cpp:96-98` (and `:233-235` in `VerticalSkewT`):

```cpp
for(INT64 j = 0; j < samples; j++) {
    pxlLeft[j] = static_cast<T>(pxlBkg[j] + (pxlSrc[j] - pxlBkg[j]) * weight + 0.5);
}
```

The `+ 0.5` is round-to-nearest for the integer instantiations, `T = BYTE` and
`T = WORD`. `HorizontalSkew`/`VerticalSkew` also instantiate the template with
`T = float` for FIT_FLOAT, FIT_RGBF and FIT_RGBAF (`:166-170`, `:305-309`), where it
is a half-unit bias on data whose whole range is typically 0..1. Most of it cancels in
the running `pxlLeft - pxlOldLeft` difference; what does not cancel is the first
sample of each row or column, where `pxlOldLeft` comes from the background and carries
no bias, and the leftover pixel written past the end of the skew (`:122`, `:259`).
Rotating a *uniform* 0.125 FIT_FLOAT image by 10° over a black background:

```
Y: uniform 0.125 float image rotated 10 deg -> range [-0.828695 .. 1.04419]  (background 0)
```

The output must lie in [0, 0.125]. It spans [-0.83, +1.04] — eight times the input
value, and negative, on an image with no variation at all to interpolate.

## 25. `FreeImage_Rotate` refuses 4-bit and 16-bit images even at exact right angles — CONFIRMED, upstream

`ClassicRotate.cpp:839-891` dispatches on `bpp == 1` and `bpp == 8 || 24 || 32`, and
falls through to `return NULL` for everything else. `Rotate90`, `Rotate180` and
`Rotate270` are pure pixel permutations that need only `bytespp`, and `Rotate180`
already has a working 1-bit path, so nothing about 4-bit or 16-bit is actually hard:

```
AC:   1 bpp  Rotate(45)=NULL  Rotate(90)=ok
AC:   4 bpp  Rotate(45)=NULL  Rotate(90)=NULL
AC:   8 bpp  Rotate(45)=ok    Rotate(90)=ok
AC:  16 bpp  Rotate(45)=NULL  Rotate(90)=NULL
AC:  24 bpp  Rotate(45)=ok    Rotate(90)=ok
AC:  32 bpp  Rotate(45)=ok    Rotate(90)=ok
AC: UINT16  bpp=16  Rotate(45)=ok
AC: INT16   bpp=16  Rotate(45)=NULL
```

FIT_INT16 is the odd one out among the integer types: `HorizontalSkewT<WORD>` would
handle it, and `AssignPixel` names it in the `case 2` comment, but neither
`HorizontalSkew`/`VerticalSkew` (`:161-165`, `:300-304`) nor `FreeImage_Rotate`'s
switch (`:892-897`) lists it.

**Left unfixed**, alone among the confirmed findings — see "Fixing pass" above. It is
the only one that is neither a crash, nor memory corruption, nor silently wrong output,
and the refusal is what the function's own documentation describes. Supporting these
depths is new code rather than a repair.

## 40. `AssignPixel` does unaligned 16- and 32-bit accesses — CONFIRMED, upstream  **Fixed in `4506d9a`.**

`Source/Utilities.h:333-348` — outside this directory, but `ClassicRotate.cpp` is its
main caller and is where UBSan reports it:

```
Source/Utilities.h:338:38: runtime error: load of misaligned address 0x6ed1c4be05cb for type 'const WORD', which requires 2 byte alignment
Source/Utilities.h:338:36: runtime error: store to misaligned address 0x6ed1c4be0823 for type 'WORD', which requires 2 byte alignment
Source/Utilities.h:347:37: runtime error: store to misaligned address 0x6ef1c4be0e26 for type 'DWORD', which requires 4 byte alignment
Source/Utilities.h:347:39: runtime error: load of misaligned address 0x6ef1c4be0b76 for type 'const DWORD', which requires 4 byte alignment
```

`case 3` (24-bit) moves two bytes as a `WORD` and `case 6` (FIT_RGB16) moves four as a
`DWORD`; both are at a 3- or 6-byte stride, so two of every three pixels are
misaligned. It is free on x86 and it is undefined behaviour everywhere. The tree ships
`Makefile.solaris` (SPARC traps) and `Makefile.iphone`; `memcpy` compiles to the same
instructions on x86 and is correct on both.

## 34. The skew gap-fill loops have no destination bound — BY INSPECTION, upstream

`ClassicRotate.cpp:80-90` and `:215-227` write `iOffset` pixels of background into
`dst` with no comparison against `dst_width`/`dst_height`. `Rotate45` is the only
caller and its shear offsets are bounded by the destination it allocated, so this is
not reachable today; it is worth a bound because these are template functions with a
`bkcolor` the caller supplies.

## Residual notes from the INT64 widening (`bbb140a`) — BY INSPECTION, **local**

The widening is correct where it matters, but three `int` narrowings were left inside
it: `iXPos < (int)dst_width` (`:101`), `iYPos < (int)dst_height` (`:238`, `:255`,
`:263`, `:268`), `div((int)y, 8)` (`:352`, `:540`) and
`src_width + unsigned(...)` (`:653`, `:685`, `:716`). None of them can be reached with
a real bitmap — every one needs a dimension above `INT_MAX` — but they mean the
function is not actually 64-bit clean, which is what the commit set out to make it.

---

# Colors.cpp

## 15. `FreeImage_ApplyPaletteIndexMapping` does nothing for 1-bit images — CONFIRMED, upstream  **Fixed in `f8bed84`.**

`Colors.cpp:886-889`:

```cpp
	switch (bpp) {
		case 1: {

			return result;
		}
```

`result` is 0. The documentation immediately above (`:839-840`, `:945-946`) says the
function and its `FreeImage_SwapPaletteIndices` wrapper apply "on a 1-, 4- or 8-bit
palletized image":

```
P: 1-bit SwapPaletteIndices changed 0 pixels; row = 0 1 0 1 0 1 0 1   (expected 1 0 1 0 1 0 1 0)
```

The return value at least reports 0, so a caller that checks it is not misled — but it
is reported the same way as "the colour was not present", which is not the same thing.

## 19. `FreeImage_Invert` destroys the alpha channel — CONFIRMED, upstream  **Fixed in `8464e74`.**

`Colors.cpp:93-103` computes `bytespp` from the line length and inverts every byte of
every pixel; `:113-122` does the same in `WORD` units. For 32-bit FIT_BITMAP and
FIT_RGBA16 that includes the alpha sample:

```
S: 32bpp after Invert: px0 rgba = 245 235 225 0  (alpha was 255)
S: RGBA16 after Invert: px0 = 65435 65335 65235 0  (alpha was 65535)
```

An opaque image becomes fully transparent. Whether that is the intended reading of
"inverts each pixel data" is arguable; what is not arguable is that
`FreeImage_Composite` and every saver will now treat the result as invisible, and that
the documentation says nothing about it. FIT_RGBF/RGBAF are rejected outright
(`:125-128`), so there is no float precedent to be consistent with.

## 20. `FreeImage_AdjustColors` reports failure when there is nothing to do — CONFIRMED, upstream  **Fixed in `dff3559`.**

`Colors.cpp:626-629`:

```cpp
if (FreeImage_GetAdjustColorsLookupTable(LUT, brightness, contrast, gamma, invert)) {
    return FreeImage_AdjustCurve(dib, LUT, FICC_RGB);
}
return FALSE;
```

`FreeImage_GetAdjustColorsLookupTable` returns *the number of adjustments applied*,
and returns 0 for the default arguments (`:508-515`). The documented contract of
`FreeImage_AdjustColors` is "Returns TRUE on success, FALSE otherwise (e.g. when the
bitdepth of the source dib cannot be handled)":

```
T: AdjustColors(0,0,1.0,FALSE) = 0 (nothing was wrong)
T: AdjustColors(10,0,1.0,FALSE) = 1
```

## 39. `FreeImage_ApplyColorMapping` counts palette entries, not pixels — BY INSPECTION, upstream

`Colors.cpp:681-706`, the 1/4/8-bit case, increments `result` once per changed
*palette entry*. The documentation at `:643-644` and `:664` says "the total number of
pixels changed", which is what the 16-, 24- and 32-bit cases below do count. A caller
cannot tell the two apart from the return value.

## Notes on `Colors.cpp` and `Channels.cpp` that are *not* bugs

`(dst_type != FIC_RGB) && (dst_type != FIC_RGBALPHA) || (src_type != FIC_MINISBLACK)`
(`Channels.cpp:215`) and `(src_bpp != 8) || (dst_bpp != 24) && (dst_bpp != 32)`
(`:227`) draw `-Wparentheses` but both group the way the author meant.
`SET_HI_NIBBLE` (`Colors.cpp:32`) does not mask its argument while `SET_LO_NIBBLE`
does; the `|=` into a `BYTE` truncates to the same result. The 4-bit `skip_last` /
`max_x` logic at `:891-896` correctly skips the padding nibble of an odd-width row.

---

# Channels.cpp

## 22. `Get`/`SetComplexChannel` succeed for channels they do not implement — CONFIRMED, upstream  **Fixed in `afd7861`.**

`FreeImage_GetComplexChannel` (`Channels.cpp:362-434`) allocates the FIT_DOUBLE result
*before* the `switch (channel)`, and the switch has no `default`. A request for, say,
`FICC_RED` therefore returns a valid, zero-filled image rather than NULL.
`FreeImage_SetComplexChannel` (`:443-488`) has the mirror problem: its switch handles
only `FICC_REAL` and `FICC_IMAG`, and it returns TRUE regardless:

```
X: MAG -> 0x75e5c01e0090 first = 5 (expected 5)
X: RED -> 0x75e5c01e00b0 first = 0  (a non-NULL all-zero image)
X: SetComplexChannel(FICC_MAG) returned 1
```

`FreeImage_GetComplexChannel` on a non-FIT_COMPLEX image is handled correctly — `dst`
stays NULL and `FreeImage_CloneMetadata(NULL, src)` is a no-op — but it gets there by
falling off the end of the `if`, not by a check.

---

# Display.cpp

## 36. The alpha blends divide by 256, not 255 — BY INSPECTION, upstream

`Display.cpp:169-172` and `Background.cpp:193-198`:

```cpp
not_alpha = (BYTE)~alpha;
cp_bits[FI_RGBA_BLUE] = (BYTE)((alpha * (WORD)fgc.rgbBlue + not_alpha * (WORD)bkc.rgbBlue) >> 8);
```

`~alpha` is `255 - alpha`, so the weights sum to 255 and the shift divides by 256.
`FreeImage_Composite` special-cases `alpha == 0` and `alpha == 255` (`:155-166`), which
hides the worst of it, but `GetAlphaBlendedColor` does not: it is called only with
`0 < alpha < 255` and is always one level dark. `FreeImage_PreMultiplyWithAlpha`
fifteen lines below gets it right — `(alpha * c + 127) / 255` (`:222-224`) — so the
correct form is already in the file.

## 37. `FreeImage_Composite` computes a scanline pointer for a background it may not have — BY INSPECTION, upstream

`Display.cpp:106`:

```cpp
BYTE *bg_bits = FreeImage_GetScanLine(bg, y);
```

`bg` is allowed to be NULL (the checkerboard case). `FreeImage_GetScanLine` returns
NULL for it, and `bg_bits` is then advanced by `bg_bits += 3` once per pixel
(`:176`) — pointer arithmetic on a null pointer, which is undefined, although it is
never dereferenced (`:137` guards the read). This build did not flag it; it costs one
`if` to make it well-defined.

---

# BSplineRotate.cpp

## 16. `FreeImage_RotateEx` dispatches on bit depth alone — CONFIRMED, upstream  **Fixed in `32be4a9`.**

`BSplineRotate.cpp:659-722`:

```cpp
bpp = FreeImage_GetBPP(dib);

if(bpp == 8) { … }
if((bpp == 24) || (bpp == 32)) { … }
```

`FreeImage_GetImageType` is never consulted. FIT_UINT32, FIT_INT32 and FIT_FLOAT all
report 32 bits per pixel, so each is taken apart into four "colour channels", each
byte plane is B-spline rotated as if it were 8-bit greyscale, and the result is
reassembled as a 32-bit FIT_BITMAP:

```
K: src type=6 bpp=32
K: RotateEx returned type=1 bpp=32 (a FIT_FLOAT input became this)

AC: UINT32  bpp=32  RotateEx=ok  <-- TYPE CHANGED
AC: INT32   bpp=32  RotateEx=ok  <-- TYPE CHANGED
AC: FLOAT   bpp=32  RotateEx=ok  <-- TYPE CHANGED
```

The pixels are meaningless (the exponent and mantissa bytes of a float are
interpolated independently), the image type silently changes, and the caller gets no
error. The documentation (`:638`) says "Input dib (8, 24 or 32-bit)", which is exactly
the ambiguity: it means 8/24/32-bit **FIT_BITMAP**. One
`FreeImage_GetImageType(dib) != FIT_BITMAP` test at the top fixes all three.

## 29. `malloc(width * height * sizeof(double))` overflows `int` — BY INSPECTION, upstream

`BSplineRotate.cpp:538-539` and `:568`:

```cpp
int width = FreeImage_GetWidth(dib);
int height = FreeImage_GetHeight(dib);
…
ImageRasterArray = (double*)malloc(width * height * sizeof(double));
```

`width * height` is an `int` product; the promotion to `size_t` happens afterwards.
At 2^31 pixels it overflows — signed overflow, so formally undefined, and in practice
either a negative value (the allocation fails, which is handled) or, at exactly
2^32 pixels, zero (the allocation succeeds and the copy loop at `:574-581` writes
32 GB into it). The threshold needs a source image of 2 GB or more, which is why this
is BY INSPECTION: nothing on this machine could hold both it and the `double` array.
`(size_t)width * height * sizeof(double)` is the fix, with a check afterwards.

---

# MultigridPoissonSolver.cpp

## 5. A 2×2 input indexes the grid array at −1 — CONFIRMED, upstream  **Fixed in `e68d76f`.**

`MultigridPoissonSolver.cpp:330-357`:

```cpp
int nn = n;
// check grid size and grid levels
while (nn >>= 1) ng++;
if (n != 1 + (1L << ng)) { … throw(1); }
…
    _CREATE_ARRAY_GRID_(IRHO, ng);        // malloc(ng * sizeof(FIBITMAP*))
…
nn = n/2 + 1;
ngrid = ng - 2;                           // 350

// allocate storage for r.h.s. on grid (ng - 2) ...
IRHO[ngrid] = FreeImage_AllocateT(FIT_FLOAT, nn, nn);   // 353
```

`FreeImage_MultigridPoissonSolver` rounds the larger dimension up to `2^j + 1`
(`:473-480`), so an image whose larger side is 2 gives `n = 3` and `ng = 1`. The four
grid arrays are then one element long and `ngrid` is **−1**:

```
==1648327==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x767be29e00a8
WRITE of size 8 at 0x767be29e00a8 thread T0
    #0 fmg_mglin Source/FreeImageToolkit/MultigridPoissonSolver.cpp:353
    #1 FreeImage_MultigridPoissonSolver Source/FreeImageToolkit/MultigridPoissonSolver.cpp:490
0x767be29e00a8 is located 8 bytes before 8-byte region [0x767be29e00b0,0x767be29e00b8)
allocated by thread T0 here:
    #1 fmg_mglin Source/FreeImageToolkit/MultigridPoissonSolver.cpp:343
```

A `FIBITMAP *` is written eight bytes before the block, and `_FREE_ARRAY_GRID_` will
not free it, so it leaks as well. Execution then continues to
`fmg_solve(IU[0], IRHO[0])` at `:376` with `IRHO[0]` still NULL — the allocation went
to `IRHO[-1]` — and `fmg_solve` dereferences the result of
`FreeImage_GetScanLine(NULL, 1)` at `:123-124`.

`ng == 1` is the only failing case: `n = 5` gives `ng = 2` and `ngrid = 0`, which is
in range. Rejecting `ng < 2` alongside the existing `ng > NGMAX` check at `:337` is
enough. Reached from `FreeImage_TmoFattal02` as well as directly.

## 33. The solver does not check its input type or any of its results — BY INSPECTION, upstream

`MultigridPoissonSolver.cpp:466-490`. `FreeImage_HasPixels` is the only validation;
the function then builds a FIT_FLOAT square and calls
`FreeImage_Paste(I, Laplacian, 1, 1, 255)` without looking at the return value. For a
Laplacian that is not FIT_FLOAT, `FreeImage_Paste` refuses (`CopyPaste.cpp:663-667`)
and the solver runs on an all-zero image, returning a plausible-looking result derived
from nothing. `fmg_mglin`'s `BOOL` return at `:490` is discarded too.

---

# JPEGTransform.cpp

## 31. An in-place transform never truncates the file — BY INSPECTION, upstream

`JPEGTransform.cpp:333-335`:

```cpp
if(src_handle == dst_handle) {
    dst_io->seek_proc(dst_handle, stream_start, SEEK_SET);
}
```

`FreeImage_JPEGTransform(file, file, …)` opens one `FILE*` in `"r+b"`
(`:397-400`), rewinds it and writes the transformed stream over the original. A
transform that trims partial edge MCUs produces a shorter file, and the tail of the
old one is left in place after `EOI`. Decoders stop at `EOI`, so the image still
loads, but the file carries bytes of the previous image and its size is wrong.

## 32. `getMemIO` does not validate `src_stream` — BY INSPECTION, upstream

`JPEGTransform.cpp:587-609` checks `dst_stream` (and refuses a read-only user buffer)
but passes `src_stream` straight through to `*src_handle`. `FreeImage_JPEGTransform*
FromMemory(NULL, …)` reaches the memory `read_proc` with a NULL handle.

## Note on `ls_jpeg_error_exit` — upstream, shared with PluginJPEG

`JPEGTransform.cpp:58-71` returns to libjpeg when `msg_parm.i[0] == 13`, which
libjpeg's contract forbids ("Control must NOT return to the caller"), and otherwise
throws a C++ exception through libjpeg's C frames after having already called
`jpeg_destroy`. The double destroy is safe — `jpeg_destroy` clears `cinfo->mem` and
the `catch` at `:356` re-entering it is a no-op — and the unwind works on the
platforms this tree builds on. It is identical to `PluginJPEG.cpp`'s handler, so it is
noted here rather than reported: changing one without the other would be worse.

---

## Method

Everything was read first — all 9,300 lines, with each of the seven locally modified
files diffed against its FreeImage 3.18.0 original — and run afterwards.

The library came from `.claude/audit/asan3/libfreeimage.a`, the ASan+UBSan build left
behind by the previous audit; its `Source/FreeImageToolkit/*.cpp` and `*.h` were
compared byte for byte against the working tree first, and are identical. The probes
are a single driver, `.claude/audit/toolkit/tk.c`, built with

```sh
cd .claude/audit/asan3
gcc -std=gnu99 -g -O0 -fsanitize=address,undefined -fno-omit-frame-pointer \
    -I. -ISource -o ../toolkit/tk ../toolkit/tk.c libfreeimage.a \
    -lstdc++ -lpthread -lm -fopenmp
```

and run as `./tk <probe>` with
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 OMP_NUM_THREADS=1`. Probe `E` needs
`detect_leaks=1`; probe `N` needs
`max_allocation_size_mb=512:allocator_may_return_null=1`. Each probe builds its own
bitmaps in memory — no image files are involved, and nothing is written into the
repository.

| probe | finding |
|---|---|
| `A` | 1, FIT_RGB16, through `horizontalFilter` |
| `A2` | 1, the same defect through `verticalFilter` |
| `B`, `B2` | 2 |
| `C <type>` | 1 and 9; `24bpp` and `8bpp` are the controls |
| `D` | 3 |
| `E` | 4a |
| `AA` | 4b |
| `AB` | control for 4b — an 8-bit raw-bits rescale is correct |
| `F` | 11 |
| `G`, `V` | 12 |
| `H`, `AG` | 13 |
| `I` | 14 |
| `J` | 5 |
| `K`, `AC` | 16, 25 |
| `L` | 7 |
| `M` | 8 |
| `N` | 24 |
| `O` | 30 |
| `P` | 15 |
| `Q` | thread-count sweep (below) |
| `R` | 10 |
| `S` | 19 |
| `T` | 20 |
| `U` | 37 |
| `W` | 18 |
| `X` | 22 |
| `Y` | 17 |
| `Z` | scope check for `testThumbnail` (below) |
| `AD`, `AF8`, `AF24`, `AF32` | 6, 21 |
| `AE` | 23c |

### The same probes on an ordinary build

`.claude/audit/stock2/libfreeimage.a` is a plain `-O2` build of the same sources
(its `Source/FreeImageToolkit/*` was compared byte for byte against the working tree
too). Linking `tk.c` against it instead answers "what does a user actually see":

| probe | finding | stock build |
|---|---|---|
| `A`, `A2` | 1 | **SIGSEGV** |
| `B` | 2, FIT_BITMAP | returns a silently black image |
| `B2` | 2, FIT_RGB16 | **SIGFPE** — integer division by zero at `Resize.cpp:1142` |
| `D` | 3 | **SIGSEGV** |
| `E` | 4a | silent — 472 bytes per call |
| `AA` | 4b | silent heap corruption (32 bytes past a 64-byte block) |
| `J` | 5 | **SIGSEGV** |
| `L` | 7 | **SIGSEGV** |

Five of the eight are hard crashes without any sanitizer. The two silent ones —
the black image and the raw-bits overflow — are the ones to worry about.

## What was checked and is correct

**The OpenMP work is deterministic.** Every parallelised function was run at 1, 2, 4,
8 and 16 threads and the pixel hashes compared. All five runs are identical, for a
downscale and an upscale with Lanczos3, for `FreeImage_Rotate` at 90°, 180°, 270° and
33° on a 257×129 24-bit image, and for the three right-angle rotations of a 1-bit
image of the same size:

```
T=1  Q down=64e77778a193ad62 up=c754173782f06a40 rot90=82e0750399bca6b4 rot180=2de6b65778ac03b4 rot270=480816bd015285b4 rot33=e6300866a7a59878 b90=4222011ac0d4e2f1 b180=66562dc1046645da b270=4443e4c5b8d48469
T=16 Q down=64e77778a193ad62 up=c754173782f06a40 rot90=82e0750399bca6b4 rot180=2de6b65778ac03b4 rot270=480816bd015285b4 rot33=e6300866a7a59878 b90=4222011ac0d4e2f1 b180=66562dc1046645da b270=4443e4c5b8d48469
```

The two 1-bit paths that *were* races — `Rotate90` and `Rotate270`, where eight
consecutive source rows `|=` into the same destination byte — no longer carry a
pragma and now carry a comment saying why (`ClassicRotate.cpp:348-349`, `:538-539`).
`Rotate180`'s 1-bit pragma is safe: each iteration owns one destination scanline.

**`FreeImage_RescaleRect` with a left offset is correct for 8-bit, 24-bit and 16-bit
555** — see the control lines in finding 9. The defect is confined to 565 and to the
non-FIT_BITMAP types.

**`FreeImage_RescaleRawBits` on an 8-bit source is correct**, including the palette:
`FreeImage_AllocateHeaderForBits` gives it a greyscale ramp, so a uniform index
survives the round trip (`AB: dst row0 = 200 200 200 200 200 200 200 200`).

**`testAPI`'s abort at `TestAPI/testThumbnail.cpp:132` is not a toolkit bug.**
`FreeImage_MakeThumbnail`, the only `Source/FreeImageToolkit` function in that area,
works:

```
Z: load=0x758a60fe0070
Z: GetThumbnail=(nil)
Z: MakeThumbnail(100)=0x758a60fe3650 100x67
```

`testLoadThumbnail` fails because `FreeImage_GetThumbnail` returns NULL, and
`TestAPI/exif.jpg` does carry a thumbnail — its Exif IFD1 holds
`JPEGInterchangeFormat = 1038` and `JPEGInterchangeFormatLength = 3662`. The failure
is in the Exif/JPEG thumbnail-extraction path (`Source/Metadata/`, `PluginJPEG.cpp`),
outside this audit's scope.

**`FreeImage_EnlargeCanvas` currently returns NULL for a collapsing rectangle** —
see finding 30. It is defended by a check in `Source/FreeImage/BitmapAccess.cpp`, not
by one of its own.

**Not bugs, checked and dismissed:** the `0xff7f >>` idiom and the `?:`-with-assignment
in `Flip.cpp` and `CopyPaste.cpp`; the `transparent_table` indexing in
`Display.cpp:120` (`BitmapAccess.cpp:94` declares it `BYTE[256]`, pre-filled with
0xFF, so a palette index can never run off it); `InitialAntiCausalCoefficient`'s
`c[DataLength - 2]` (`BSplineRotate.cpp:213`), unreachable at `DataLength == 1`
because `ConvertToInterpolationCoefficients` returns early at `:91`; the `Width == 1`
division guard in `InterpolatedValue` (`:478`); `sprintf(crop, …)` into `char[64]`
(`JPEGTransform.cpp:146`, 47 bytes worst case); and `fmg_restrict`'s boundary loops,
which are correctly bounded by the coarse grid.

`Filters.h` has nothing to report. Two things in it look wrong and are not:
`CBoxFilter::Filter` returns 1.0 for `fabs(dVal) <= m_dWidth` inclusive, so a sample
landing exactly on a box edge is counted at both ends — but `CWeightsTable` normalises
by the weight sum immediately afterwards (`Resize.cpp:197-203`), so the result is
still a partition of unity; and `CCatmullRomFilter::Filter` is the one filter that
does not call `fabs` first, because it spells out both halves of the kernel and
evaluates to the same value either way (at `dVal = -2` it is 0, as it must be).
