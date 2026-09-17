# Audit: do the toolkit functions work on `FreeImage_CreateView` results?

Branch `qpv` @ `b0825f8`, audited 2026-09-17 (`15ad866`). Companion to `AUDIT.md`
(`Source/FreeImage/`) and `AUDIT-TOOLKIT.md` (`Source/FreeImageToolkit/`); neither of
those looked at views.

> **All three findings are fixed**, in `ab712b0`, `9aedd43` and `93dc31e`. The audit
> itself changed no source file; see "The fixes" at the end for what changed, and for
> the evidence that no pixel of an ordinary bitmap moved.

## The claim under test

`Source/FreeImageToolkit/CopyPaste.cpp:769-776`, the doc comment on
`FreeImage_CreateView`:

> A dynamic view is a FreeImage bitmap with its own width and height, that,
> however, shares its bits with another FreeImage bitmap. [...] **All FreeImage
> operations, like saving, displaying and all the toolkit functions, when applied
> to the view, only affect the view's rectangular area.**

That sentence is two promises, and they were tested separately:

* **CONTAIN** — after `f(view)`, every pixel of the backing image outside the
  view's rectangle is unchanged.
* **RESULT** — `f(view)` produces the same pixels as `f(crop)`, where `crop` is the
  same rectangle extracted from the backing image one pixel at a time. The oracle
  deliberately does **not** go through `FreeImage_Copy`, which is itself under test.

## Verdict

> **27 of the 30 toolkit entry points that take a `FIBITMAP` honour the contract.
> Three break it: `FreeImage_Invert`, `FreeImage_FillBackground` and
> `FreeImage_FlipVertical`.** All three fail the same way, for the same reason, and
> only at 1 and 4 bits per pixel. **No function's result depends on a pixel outside
> its view**: every RESULT check passed, in all 3,330 cells.

All three defects are **upstream** FreeImage 3.18.0; one of them (`FlipVertical`) was
half-fixed on this branch in 2023 and the rest of it was still there. The three are
findings 1, 2 and 3 below. Finding 4 is the design question behind them, and the
fixes answer it the way it recommends.

| | |
|---|---|
| Entry points taking a `FIBITMAP` | 30 — all 30 exercised |
| Entry points taking no bitmap | 12 (`AllocateEx`, `AllocateExT`, `GetAdjustColorsLookupTable`, `RescaleRawBits`, 8 × `JPEGTransform*`) — not applicable |
| Cells run | 37 call shapes × 18 pixel formats × 5 view geometries = **3,330** |
| ok | 1,996 |
| inert (function rejects that pixel format) | 829 |
| n/a (function returns NULL for that format) | 475 |
| **CONTAIN failures** | **30** — 3 functions × 4 low-bpp formats × 3 ragged geometries, minus the 6 palette-only `Invert` cells |
| RESULT failures | 0 |
| AddressSanitizer / UBSan reports | 0 |

## What makes a view different

`FreeImage_CreateView` wraps a header around a pointer into the backing image
(`CopyPaste.cpp:848`, `FreeImage_AllocateHeaderForBits`). Two consequences:

1. **`FreeImage_GetPitch(view)` is the backing image's pitch**, which is larger than
   the pitch an ordinary bitmap of the view's width would have
   (`BitmapAccess.cpp:1148-1154` returns `external_pitch`). Code that recomputes the
   pitch instead of asking for it walks the wrong rows.
2. **The bytes past `FreeImage_GetLine(view)` in a row are the backing image's next
   pixels**, not padding. Code that writes a whole row of `pitch` bytes, or that
   treats the last byte of a row as its own, corrupts the neighbour.

`GetLine` is `(width * bpp + 7) / 8` — it **rounds up to a whole byte**. At 8 bpp and
above it is exact, so (2) cannot bite. At 1 and 4 bpp it is exact only when the width
is a multiple of 8 or of 2 respectively. `CreateView` constrains and enforces `left`
(a view must start on a byte boundary) but says nothing about `right` and enforces
nothing — so a view whose last byte is *shared* with the backing image is easy to
make and is accepted. That shared last byte is the whole of this audit.

## Method

`./.claude/audit/views/vw.c`, built against the two existing audit trees
(`.claude/audit/stock2`, `-O3`; `.claude/audit/asan3`, ASan+UBSan). For every cell:

1. allocate a 40×20 backing image and fill it from a hash of `(x, y, sample)`;
2. `FreeImage_Clone` it as the reference;
3. `FreeImage_CreateView` the rectangle, and build the independent oracle crop;
4. run the function on the view and on the crop;
5. diff backing-image-vs-reference outside the rectangle (CONTAIN), and
   view-vs-crop (RESULT), pixel by pixel — never by whole bytes, so the padding
   bits of a 1- or 4-bit row can never fake a difference.

**Pixel formats (18):** 1 bpp and 4 bpp each with a colour palette and with a
greyscale ramp (the two take different branches — see finding 1), 8 bpp palettised,
8 bpp greyscale, 8 bpp with a transparency table, 16 bpp 555 and 565, 24, 32,
`FIT_UINT16`, `FIT_RGB16`, `FIT_RGBA16`, `FIT_FLOAT`, `FIT_RGBF`, `FIT_RGBAF`,
`FIT_COMPLEX`.

**Geometries (5):** `ragged` (8,3,29,15 — width 21, a right edge in the middle of a
byte at 1 and 4 bpp), `byte` (8,3,32,15 — width 24, every edge byte-aligned, but the
pitch still stressed), `full` (0,3,40,15 — full width, the control in which pitch is
*not* stressed), `corner` (0,0,21,12) and `tail` (8,8,29,20), which anchor the view
against the backing image's edges. Every non-`full` geometry has
`GetPitch(view) > natural pitch`, printed by `./vw -v` as a banner.

**The detectors were themselves tested.** `./vw -selfcheck` injects, by hand, each of
the three ways a function could break a view — one byte spilled past the right edge, a
row written at `pitch` width instead of `line` width, and a row walked at the natural
pitch instead of the view's — and requires the detector to fire, for all 18 formats ×
5 geometries. It also checks that a one-pixel result difference and a one-entry
palette difference are seen. `selfcheck: 0 failures`. Without this, a row of "ok"
would mean nothing; two real coverage holes were found and closed this way (a
degenerate test pattern that made 1-bit data a checkerboard, and greyscale palettes
that `FreeImage_GetColorType` did not actually call `FIC_MINISBLACK`).

**Reading the shared byte is not a defect; writing it is.** Several functions
`memcpy` a whole `GetLine` row into a scratch buffer — `FlipHorizontal`
(`Flip.cpp:50`), `Combine4` (`CopyPaste.cpp:162`), and `FlipVertical` itself — which
at a ragged edge does read the neighbour's half of the shared byte. That is harmless
as long as those bits are never used and never written back, and in all three cases
they are not: the pixels are then addressed individually. The line between the two is
the whole of findings 1-3, so the CONTAIN check tracks writes only.

**What the matrix did not run** *(at audit time — all four were added before the fixes
went in, and all four pass; see "The fixes")*. Every entry point was exercised with
the view as its *primary* bitmap argument. Four shapes were settled by inspection
rather than by a run: `FreeImage_Composite` with the view as the *background* (`bg`) rather than the
foreground, `FreeImage_SetChannel`/`SetComplexChannel` with the view as the *source*,
`FreeImage_Paste` with a view on both sides at once, and `FreeImage_Paste`'s
`alpha < 256` blending path. All four are per-row `FreeImage_GetScanLine` loops at
depths where `GetLine` is exact (`Display.cpp:106`, `Channels.cpp:255`, and the
`Combine8/16/24/32` blend loops), so the conclusion is unchanged — but it rests on
reading, not on measurement.

**One thing the CONTAIN check deliberately ignores:** the backing image's own row
padding. A write that lands only there is invisible to every FreeImage caller, so it
is not counted as a violation. Writes past the end of the backing buffer are a
different matter and are covered by the ASan build, which is clean.

---

# 1. `FreeImage_Invert` inverts the backing image's pixels past the view's right edge — CONFIRMED, upstream  **Fixed in `ab712b0`.**

`Source/FreeImageToolkit/Colors.cpp:76-84`:

```cpp
} else {
    for(y = 0; y < height; y++) {
        BYTE *bits = FreeImage_GetScanLine(src, y);

        for (x = 0; x < FreeImage_GetLine(src); x++) {
            bits[x] = ~bits[x];
        }
    }
}
```

The loop runs over **bytes**, not pixels. On an ordinary bitmap the extra bits of the
last byte are padding and inverting them is harmless. On a view they are the backing
image's next pixels.

This is the non-palette branch (`Colors.cpp:68`), taken when `FreeImage_GetColorType`
is not `FIC_PALETTE` — i.e. for greyscale 1- and 4-bit images. (With a colour palette
`FreeImage_Invert` inverts the palette instead and never touches a pixel, which is why
the `1bpp`/`4bpp` rows pass and the `1grey`/`4grey` rows do not.)

```
FreeImage_Invert, 1 bpp (greyscale palette), backing image 40x4, view 21x4  line=3 pitch=8
          ........^^^^^^^^^^^^^^^^^^^^^...........   ^ = inside the view
  before  1010101010101010101010101010101010101010   row 3
          0101010101010101010101010101010101010101   row 2
          1010101010101010101010101010101010101010   row 1
          0101010101010101010101010101010101010101   row 0
  after   1010101001010101010101010101010110101010   row 3
          0101010110101010101010101010101001010101   row 2
          1010101001010101010101010101010110101010   row 1
          0101010110101010101010101010101001010101   row 0
  --> 12 pixels OUTSIDE the view changed
```

Columns 29, 30 and 31 — three pixels of the backing image, in each of four rows — are
inverted along with the view. At 4 bpp it is the single pixel in the last byte's low
nibble. 8 bpp and above are unaffected: `GetLine` is exact there.

3.18.0 (`6712d75:Source/FreeImageToolkit/Colors.cpp`) has this loop unchanged.

---

# 2. `FreeImage_FillBackground` replicates its first row over the backing image's pixels — CONFIRMED, upstream  **Fixed in `9aedd43`.**

`Source/FreeImageToolkit/Background.cpp:376-387`:

```cpp
// Then, copy the first scanline into all following scanlines.
// 'src_bits' is a pointer to the first scanline and is already
// set up correctly.
if (src_bits) {
    unsigned pitch = FreeImage_GetPitch(dib);
    unsigned bytes = FreeImage_GetLine(dib);
    dst_bits = src_bits + pitch;
    for (unsigned y = 1; y < height; y++) {
        memcpy(dst_bits, src_bits, bytes);
        dst_bits += pitch;
    }
}
```

`pitch` is right — it is asked for, not computed. `bytes` is the rounded-up row
length, so each `memcpy` carries the shared last byte of row 0 across every other row.

Note where the damage is *not*: the code that builds row 0 (`:312-346`) is
pixel-exact at both 1 and 4 bpp — it memsets `width / 8` (resp. `width / 2`) whole
bytes and then merges the remaining bits under a mask. So the first row of the view is
correct and only rows 1..h-1 corrupt the neighbour, which is exactly what the run
shows: the first differing pixel is always one row above the view's bottom row, never
in it.

```
FreeImage_FillBackground, 4 bpp (colour palette), backing image 40x4, view 21x4  line=11 pitch=20
          ........^^^^^^^^^^^^^^^^^^^^^...........   ^ = inside the view
  before  9e38d27c16b05af49e38d27c16b05af49e38d27c   row 3
          6b05af49e38d27c16b05af49e38d27c16b05af49   row 2
          38d27c16b05af49e38d27c16b05af49e38d27c16   row 1
          05af49e38d27c16b05af49e38d27c16b05af49e3   row 0
  after   9e38d27cddddddddddddddddddddd1f49e38d27c   row 3
          6b05af49ddddddddddddddddddddd1c16b05af49   row 2
          38d27c16ddddddddddddddddddddd19e38d27c16   row 1
          05af49e3ddddddddddddddddddddd16b05af49e3   row 0
  --> 3 pixels OUTSIDE the view changed
```

Column 29 becomes `1` in rows 1, 2 and 3 — row 0's value, dragged along inside the
shared byte. Row 0 itself is untouched outside the view.

`FreeImage_AllocateEx`/`AllocateExT` reach the same loop, but they always hand it a
bitmap they have just allocated, so only `FreeImage_FillBackground` can be given a
view. 3.18.0 has this loop unchanged.

This is a different site from `AUDIT-TOOLKIT.md` finding 14, which was the *first*
scanline's 4-bit parity test and is fixed (`4065a7f`).

---

# 3. `FreeImage_FlipVertical` still swaps the backing image's pixels — CONFIRMED, upstream, half-fixed locally  **Fixed in `93dc31e`.**

`Source/FreeImageToolkit/Flip.cpp:160-167`:

```cpp
for(size_t y = 0; y < height/2; y++) {

    memcpy(Mid, From + line_s, line);
    memcpy(From + line_s, From + line_t, line);
    memcpy(From + line_t, Mid, line);
    line_s += pitch;
    line_t -= pitch;
}
```

Commit `3a57bb4` ("fixed FreeImage_FlipVertical() to work nicely with image objects
created via FreeImage_CreateView() by having it respect the width of the source")
changed these three `memcpy` lengths from `pitch` to `line`. That was the right
direction and it is most of the fix — but `line` is still rounded up to a whole byte,
so the last byte of each swapped row is still shared with the backing image.

How much `3a57bb4` bought, on the same 21-wide 1-bit view:

| | pixels outside the view changed |
|---|---|
| 3.18.0, `memcpy(..., pitch)` | **64** |
| current, `memcpy(..., line)` | **12** |

```
FreeImage_FlipVertical, 1 bpp (greyscale palette), backing image 40x4, view 21x4  line=3 pitch=8
          ........^^^^^^^^^^^^^^^^^^^^^...........   ^ = inside the view
  after   1010101001010101010101010101010110101010   row 3
          0101010110101010101010101010101001010101   row 2
          1010101001010101010101010101010110101010   row 1
          0101010110101010101010101010101001010101   row 0
  --> 12 pixels OUTSIDE the view changed

FreeImage_FlipVertical as 3.18.0 writes it (memcpy of `pitch`, not `line`)
  after   0101010101010101010101010101010101010101   row 3
          1010101010101010101010101010101010101010   row 2
          0000000001010101010101010101010101010101   row 1
          0101010110101010101010101010101010101010   row 0
  --> 64 pixels OUTSIDE the view changed
```

(The upstream figure is produced by running 3.18.0's loop verbatim against the same
view, in `repro.c`; it is not a claim about an upstream build.)

`FreeImage_FlipHorizontal` is **not** affected. It reads a whole `line` into a scratch
buffer but writes back one pixel at a time at every depth (`Flip.cpp:54-58` for 1 bpp,
`:70-79` for 4 bpp), so the bits it does not own are never written.

---

# 4. `FreeImage_CreateView` constrains `left` but not `right` — the design question

`CopyPaste.cpp:793-795`:

> Since the memory block shared by the backing image and the view must start at a
> byte boundary, the value of parameter `left` must be a multiple of 8 for 1-bit
> images and a multiple of 2 for 4-bit images.

and `:828-842` enforces exactly that, returning `NULL` otherwise. Nothing is said
about `right`, and nothing is enforced. The three findings above are all the same
consequence: at 1 and 4 bpp a view whose `right` is not on a byte boundary **shares
its last byte** with the backing image, and three functions write that byte whole.

Two ways out:

* **Fix the three functions** (recommended). Each needs the same thing the 1- and
  4-bit branches of `FillBackgroundBitmap` already do for row 0: copy
  `GetLine - 1` whole bytes and merge the final byte under a mask covering only
  `width % (8 / bpp)` pixels. A single shared helper — "copy/modify `width` pixels of
  a row, leaving the rest of the final byte alone" — would serve all three, and would
  also make `FlipVertical`'s scratch buffer honest.
* **Document and enforce a `right` constraint**, symmetrically with `left`. This is a
  one-line change in `CreateView` but it would start returning `NULL` for rectangles
  that are accepted today, and it makes views second-class at exactly the depths where
  they are most useful. It also does not match the promise quoted at the top, which is
  unconditional.

Until either is done, the safe rule for callers is: **at 1 bpp make the view's width a
multiple of 8; at 4 bpp make it even.** The `byte` geometry in the matrix is that rule,
and all 37 call shapes pass in it, at every pixel format.

---

# What was verified to work

Every one of these was run against a view with a stressed pitch (`GetPitch(view)`
is 1.67× to 2× the natural pitch for the view's width), in five geometries and
every pixel format the function accepts, and matched the independent oracle pixel for
pixel while leaving the backing image alone:

| | |
|---|---|
| `Background.cpp` | `EnlargeCanvas` |
| `BSplineRotate.cpp` | `RotateEx` |
| `Channels.cpp` | `GetChannel`, `SetChannel`, `GetComplexChannel`, `SetComplexChannel` |
| `ClassicRotate.cpp` | `Rotate` — arbitrary angle, and 90/180/270, which is the only case 1 bpp accepts |
| `Colors.cpp` | `AdjustCurve`, `AdjustGamma`, `AdjustBrightness`, `AdjustContrast`, `AdjustColors`, `GetHistogram`, `ApplyColorMapping`, `SwapColors`, `ApplyPaletteIndexMapping`, `SwapPaletteIndices` |
| `CopyPaste.cpp` | `Copy` (including a rectangle flush against the ragged right edge), `Paste` (view as destination, including a paste flush against the ragged edge; and view as source), `CreateView` (a view of a view) |
| `Display.cpp` | `Composite`, `PreMultiplyWithAlpha` |
| `Flip.cpp` | `FlipHorizontal` |
| `MultigridPoissonSolver.cpp` | `MultigridPoissonSolver` |
| `Rescale.cpp` / `Resize.cpp` | `Rescale` (down and up), `RescaleRect`, `MakeThumbnail` |

Three of these deserve a specific note, because the code looks dangerous and is not:

* **`Resize.cpp`** is 2,345 lines of explicit pitch arithmetic, and all of it is
  correct on views: every source access goes through `FreeImage_GetScanLine` plus an
  offset, or through `FreeImage_GetBits` plus `src_offset_y * FreeImage_GetPitch(src)`
  (`:1499`, `:1701`, `:1828`, `:2009`, `:2083`, `:2122`, `:2171`, …). The pitch is
  always asked for, never recomputed.
* **`MultigridPoissonSolver.cpp:37` and `:44`** do `memcpy`/`memset` of
  `GetHeight * GetPitch` — a flat whole-buffer operation that would be badly wrong on
  a view. They are only ever handed the square `FIT_FLOAT` grids the solver allocates
  itself; the caller's `Laplacian` reaches the solver only through
  `FreeImage_Paste` (`:509`), which is view-clean.
* **`CopyPaste.cpp`'s `Combine1` and `Combine4`** write the destination one bit and
  one nibble at a time (`:75-79`, `:163-176`), so `FreeImage_Paste` into a view with a
  ragged right edge is safe even when the pasted rectangle ends exactly on it. The
  `Combine8/16/24/32` and `CombineSameType` fast paths `memcpy` `GetLine(src)` bytes,
  which is exact at those depths.

## Two notes that are not defects

**Palette operations on a palettised view do not touch the backing image at all.**
A view gets a private copy of the palette (`CopyPaste.cpp:869-877`), so
`FreeImage_Invert`, `FreeImage_AdjustCurve` and `FreeImage_ApplyColorMapping` on a
1-, 4- or 8-bit palettised view rewrite *the view's* palette and leave both the shared
pixels and the backing image's palette exactly as they were. That satisfies the
contract, but vacuously: a caller who makes a view in order to invert one region of a
palettised image gets nothing, with a `TRUE` return. It is a consequence of the
documented private-palette semantics, not a bug, but it is worth knowing.

**`FreeImage_GetHistogram` accepts a `FIT_FLOAT` image.** `FreeImage_GetBPP` of a
`FIT_FLOAT` bitmap is 32, so the `(bpp == 24) || (bpp == 32)` branch
(`Colors.cpp:392`) reads floats as if they were BGRA bytes. That is wrong, but it is
wrong identically on a view and on an ordinary bitmap — it is not a view defect and is
out of this audit's scope.

---

# The fixes

Three commits, one per finding, on top of the audit (`15ad866`):

| commit | finding | what changed |
|---|---|---|
| `ab712b0` | 1 | `Colors.cpp` `FreeImage_Invert`, plus the three helpers in `Source/Utilities.h` |
| `9aedd43` | 2 | `Background.cpp` `FillBackgroundBitmap`'s row-replication loop |
| `93dc31e` | 3 | `Flip.cpp` `FreeImage_FlipVertical`'s row swap |

74 inserted lines and 5 deleted, over four files. The shape of all three is the same,
and it is what finding 4 recommends: stop at the last byte a row wholly owns, then
merge the pixels left over in the next one under a mask. `Source/Utilities.h` carries
the rule, next to `CalculateLine()` whose rounding is what causes it:

```cpp
inline unsigned CalculateWholeRowBytes(const unsigned width, const unsigned bitdepth);
inline BYTE     CalculateRowTailMask  (const unsigned width, const unsigned bitdepth);
inline void     CopyRowPixels(BYTE *dst, const BYTE *src, unsigned width, unsigned bitdepth);
```

`CalculateRowTailMask` is 0 above 4 bpp, so `CopyRowPixels` degenerates to the plain
`memcpy` those call sites already did, and nothing above 4 bpp changes at all. None of
the three names existed anywhere in `Source/` beforehand.

`FreeImage_Invert` does not use `CopyRowPixels` — it has no source row. It inverts the
whole bytes with `~` as before and then the remaining pixels with `bits[whole] ^= tail`,
since inverting bits under a mask is an XOR with it.

## What the fixes were verified against

**The view contract now holds without exception.** The matrix was first extended by the
four call shapes this report had settled by inspection — `Composite` with the view as
the *background*, `SetChannel` with the view as the *source*, `Paste` with a view on
both sides, and `Paste`'s `alpha < 256` blending path — and its containment rule for
PRODUCE tests was tightened from "no pixel outside the rectangle changed" to "no pixel
of the backing image changed at all", since those functions only read their source.
That makes it 41 call shapes × 18 pixel formats × 5 geometries = **3,690 cells**:

| | before the fixes | after |
|---|---|---|
| rows failing CONTAIN | 30 | **0** |
| rows failing RESULT | 0 | 0 |
| cells that ran the function | 2,226 | 2,226 |
| inert / n-a | 829 / 635 | 829 / 635 |
| `./vw -selfcheck` | 0 failures | 0 failures |
| ASan + UBSan reports | 0 | 0 |

The 30 failures were all on the containment axis: those cells produced the *right*
pixels inside the view and damaged the backing image on the way, so the count of
cells that ran the function is the same on both sides.

`-selfcheck` still passing after the fixes is the part that matters: it proves the
detectors are still live, so "0 failing rows" means the defects are gone rather than
the test having gone blind.

Each finding's own reproduction:

| | before | after |
|---|---|---|
| `Invert`, 1 bpp / 4 bpp | 12 / 4 pixels outside the view changed | **0 / 0** |
| `FillBackground`, 1 bpp / 4 bpp | 6 / 3 | **0 / 0** |
| `FlipVertical`, 1 bpp / 4 bpp | 12 / 4 | **0 / 0** |
| `FlipVertical` as 3.18.0 writes it | 64 | 64 (unchanged — it is a simulation, not the library) |

**No pixel of an ordinary bitmap moved.** The fixes change what those three functions
leave in a row's *padding*, which matters because file writers emit it. `pad.c` runs
all three over 5,400 ordinary-bitmap shapes — widths 1..40, heights 1/2/3/7/17, nine
pixel formats — against a build with the fixes and a build without, and compares two
signatures per case:

```
cases                                    : 5400
PIXEL signatures changed (must be 0)     : 0
byte signatures changed                  : 636
  ...on a byte-aligned row (must be 0)   : 0
  ...depths involved                     : ['1', '4']
  ...operations involved                 : ['fill', 'flipv', 'invert']
```

Every byte-level difference is at 1 or 4 bpp, on a row that does not end on a byte
boundary, in one of the three functions changed. Nothing else moves.

**The toolkit regression matrix agrees.** `.claude/audit/toolkit/bench` hashes every
`FreeImage_GetLine()` byte, padding included. 58 of its 2,281 cases move, and they are
exactly `invert.*` (4), `fill.*` (45) and `flipv.*` (9) at 1 and 4 bpp on its 29×17
images — a width that ends mid-byte at both depths. No other row moves. That shape is
one of the 5,400 above, so its pixels are covered by the comparison. `baseline.txt` was
regenerated; `.claude/audit/toolkit/README.md` records why.

**The library still builds.** A full `make` from clean: exit 0, 0 errors, and the same
49 warnings as before, none of them in the four changed files. `ClassicRotate.cpp` and
`PluginHDR.cpp`, which carry warnings of their own and include `Utilities.h`, produce
identical warning counts compiled against the old and the new header. `-Wall -Wextra`
on the three changed sources: 0 warnings, before and after. The reproductions were
re-run against `Dist/libfreeimage.a` from that build, not only against the audit trees.

---

# The rig

`.claude/audit/views/`, untracked like the rest of `.claude/audit/`.

| file | what it is |
|---|---|
| `vw.c` | the matrix: 37 call shapes × 18 pixel formats × 5 geometries. `./vw` prints only failing rows; `./vw -v` prints every cell plus a geometry banner; `k=`, `g=`, `t=` narrow it |
| `vw.c -selfcheck` | injects each failure mode by hand and requires the detector to fire. **Run this before believing a clean matrix.** |
| `repro.c` | the three findings as standalone before/after pixel dumps, plus 3.18.0's `FlipVertical` loop run against a view for comparison |
| `build.sh` | builds `vw` against `.claude/audit/stock2`; `build.sh asan` also builds `vw_asan` against `.claude/audit/asan3` |
| `pad.c` | 5,400 ordinary-bitmap shapes, a pixel-only and a byte-level signature each. Run against a build with and without the fixes: the pixel one must not move |
| `rb.sh` | recompiles the named sources into both audit trees, verifies the archive member, and relinks every driver here plus the toolkit rig's `bench` |
| `run-stock.txt`, `run-asan.txt` | the two runs behind the numbers above |
| `prefix-run.txt`, `postfix-run.txt` | the 41-shape matrix either side of the fixes |

    cd .claude/audit/views
    ./build.sh asan
    OMP_NUM_THREADS=1 ./vw -selfcheck        # must print "selfcheck: 0 failures"
    OMP_NUM_THREADS=1 ./vw
    OMP_NUM_THREADS=1 ASAN_OPTIONS=detect_leaks=0:allocator_may_return_null=1 ./vw_asan

The ASan driver must be linked with `-static-libasan`; without it the runtime does not
come first in the library list and refuses to start (gcc 15). The stock and sanitized
runs produce identical failing rows, and ASan+UBSan report nothing — which is the
point of finding 1-3: all three write *inside* the backing image's allocation, so no
sanitizer can see them. Only a pixel-level containment check can.
