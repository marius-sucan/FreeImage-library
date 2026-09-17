# Format-conformance review of the loader fixes

Range reviewed: `215257a..d883162` (29 code commits + 3 `AUDIT.md` commits), branch `qpv`.
Reviewed 2026-09-16. **No files were modified by this review.**

Section G, added later the same day, covers the 21 commits that fix AUDIT.md's
second-pass findings (`6083928..350b9f0`) — only the six of them that had to
choose between readings of a malformed file, plus the two stream-relative seek
fixes. Two residual entries in section D, D4 and D9, are closed by that range
and say so.

The question this report answers is narrower than "are the fixes correct": it is
*does each change still accept, and still decode identically, every file the
format's specification allows — and does it reject what the specification
forbids?* A bounds check that is memory-safe but rejects a conforming file is a
regression here, and one that is memory-safe but silently mis-decodes a
conforming file is a worse one.

## Method

For every commit: read the change against the normative description of the
format, then, where the verdict actually turned on it, check the primary source
and run the code.

Primary sources consulted:

| Format | Source |
|---|---|
| Radiance HDR | `src/common/resolu.c` from the LBNL Radiance tree (`str2resolu`, `fgetresolu`) |
| Sun raster | Encyclopedia of Graphics File Formats, *Sun Raster* |
| Dr. Halo CUT | Encyclopedia of Graphics File Formats, *Dr. Halo* |
| QuickDraw PICT | ImageMagick `coders/pict.c` `DecodeImage()`, as an independent implementation |
| Adobe PSD | Adobe Photoshop File Formats Specification (colour mode data; image resource IDs) |
| DDS | Microsoft `DDS_HEADER` / `DDS_PIXELFORMAT` |
| ILBM/PBM | IFF ILBM chunk and ByteRun1 rules |

Empirical work (all artefacts under `$CLAUDE_JOB_DIR/tmp`, nothing written into
the repository):

* Built `libfreeimage.a` from the tip of the range and drove it with the audit
  rig's `harness.c` plus a purpose-written pixel/palette probe.
* Hand-built conforming and edge-case files for HDR, DDS, RAS, CUT and ICO, and
  a PSD save/reload round-trip whose output was then parsed byte-by-byte against
  the Adobe layout.
* Re-ran the whole audit corpus (29 PoCs + 72 valid files) and joined it by
  filename against `.claude/audit/baseline.txt`, the pre-fix record.
  **52 entries appear in both. All 24 that were not crash/hang PoCs are
  unchanged — identical checksum, or the same "load failed" — and every valid
  file present in both lists is among those 24.** Each of the 28 differences is
  a PoC moving from crash / hang / sanitizer error to a clean rejection or a
  bounded decode, except `hdr_transposed.hdr` — see finding A1 — and two
  JPEG/JNG seeds whose "runtime error" was UBSan noise in the instrumented
  baseline build.

## Two notes on scope

* **There is no `PluginBMP.cpp` change in this range.** BMP is touched only
  indirectly, through `FreeImage_AllocateBitmap()` (commit `03fcfc5`). That
  commit deliberately preserves the negative-`biHeight` top-down DIB path that
  `PluginBMP` depends on, and the measurement confirms it: `topdown.bmp` (which
  carries `biHeight < 0`) and `bottomup.bmp` — the same image in the two row
  orders — decode to the *identical* checksum `8724dfc88026b403`, and
  `seed_00_01.BMP` matches its pre-fix checksum exactly. Verdict for BMP:
  **unaffected**.
* **"RLE"** is read here as the five run-length codecs the range actually
  touches — CUT, TARGA, IFF ByteRun1, PSD PackBits and PICT PackBits. The Sun
  raster `RT_BYTE_ENCODED` decoder and the BMP `BI_RLE4`/`BI_RLE8` decoders were
  **not** modified (the RAS commits changed the loop counters around the RLE
  reader, not the reader).

---

# Verdict summary

| # | Commit | Format | Verdict |
|---|---|---|---|
| 1 | `1666841` | HDR | **REGRESSION** — see A1. **Fixed in `04a8dcb`.** |
| 2 | `b926485` | DDS | Conformant, stricter |
| 3 | `45afab6` | DDS | Conformant, stricter |
| 4 | `305c4c8` | DDS | Conformant, stricter |
| 5 | `508a405` | ICO | Conformant, stricter |
| 6 | `5f2b599` | IFF | Conformant, stricter |
| 7 | `dabe04b` | XPM | Conformant, stricter |
| 8 | `03fcfc5` | (allocator) | Conformant, stricter — public API behaviour change |
| 9 | `9bd2f57` | DDS | Conformant — verified |
| 10 | `c1f2f66` | RAS | Conformant for conforming files — verified; opens a defective path, see D1 |
| 11 | `5cd7e6f` | CUT | Conformant — verified, and now exactly right on stream accounting |
| 12 | `5942c98` | PICT | Conformant — corroborated against ImageMagick |
| 13 | `d47ad42` | PSD | Conformant — verified against the Adobe layout |
| 14 | `fd65d1d` | PSD | Conformant — verified against the Adobe layout |
| 15 | `8b585c7` | IFF | Conformant |
| 16 | `aafe9c5` | TARGA | Conformant — preserves a deliberate leniency, see C7 |
| 17 | `2003de9` | TARGA | Conformant — defensive only |
| 18 | `8c8d529` | PSD | Conformant |
| 19 | `a4df0a0` | PSD | Conformant — portability only |
| 20 | `83b4785` | HDR | Conformant |
| 21 | `ffa77c9` | RAS | Conformant |
| 22 | `f052cff` | CUT | Conformant |
| 23 | `0691416` | WBMP | Conformant |
| 24 | `0f9e945` | XBM | Conformant |
| 25 | `17d467d` | XBM | Conformant |
| 26 | `215257a` | XPM | Conformant — safety only |
| 27 | `6e7bc25` | XPM | Conformant — safety only |
| 28 | `beec28f` | PICT | Conformant |
| 29 | `ae73c7a` | (memory I/O) | Conformant — not a format change |

**One regression, in 29 commits** — fixed in `04a8dcb`, see A1. Twelve residual
non-conformances that the range did not introduce are listed in section D,
because two of them sit in code the fixes changed and one of them the fixes made
reachable.

---

# A. Regression (fixed)

## A1. HDR `1666841` — the `+X … +Y …` branch mis-decoded the very files it exists for

`Source/FreeImage/PluginHDR.cpp:267-277`. **Fixed in `04a8dcb`** — see
"After the fix" below.

The commit swapped the arguments of the second `sscanf` so that
`"+X <w> +Y <h>"` puts `w` in `width` and `h` in `height`:

```c
if(sscanf(buf,"-Y %d +X %d", &nHeight, &nWidth) < 2) {
    if(sscanf(buf,"+X %d +Y %d", &nWidth, &nHeight) < 2) {
```

That reads as the obviously right thing to do, and it is right about the
*reported dimensions*. It is wrong about what the pixel stream contains.

### What the format says

Radiance's `src/common/resolu.c` is the definition. `str2resolu()` sets the
`YMAJOR` flag only when the `X` token comes **after** the `Y` token:

```c
if (xndx > yndx) rp->rt |= YMAJOR;
```

and `fgetresolu()` derives the scanline geometry from that flag:

```c
if (rs.rt & YMAJOR) { *sl = rs.xr; *ns = rs.yr; }
else                { *sl = rs.yr; *ns = rs.xr; }
```

So for `"+X W +Y H"`: `xr = W`, `yr = H`, `YMAJOR` **unset**, therefore
**scanline length = H and scanline count = W**. The image is W wide and H tall,
but it is stored **column-major** — W scanlines of H pixels each. The `+X`-first
spelling is not an alternative way of writing the same layout; it *is* the flag
that says the layout is transposed.

### What the code did, and does

* **Before the fix:** `height = W`, `width = H`. The reader consumed W
  scanlines of H pixels — which is exactly the file's byte layout. The bitmap
  came out H×W: the transpose of the image, every pixel present and correctly
  associated — recoverable, modulo the axis sign flags FreeImage ignores in
  either version (D12), which leave the result flipped relative to standard
  orientation as well.
* **After the fix:** `width = W`, `height = H`. The reader consumes H scanlines
  of W pixels from a file that holds W scanlines of H. The dimensions are now
  right and the pixels are scrambled.

### Measured

Two encodings of the *same* 6×4 picture, one in each spelling, decoded by the
build at the tip of this range. (The X-major twin has to emit each column
bottom row first, because `+Y` means the Y axis ascends — see D12.)

```
=== (a) "-Y 4 +X 6"  (the standard, Y-major form)
6x4
r00: 10 11 12 13 14 15        <- correct
r01: 20 21 22 23 24 25
r02: 30 31 32 33 34 35
r03: 40 41 42 43 44 45

=== (b) "+X 6 +Y 4"  (the same picture, X-major)
6x4
r00: 10 20 30 40 11 21        <- scrambled
r01: 31 41 12 22 32 42
r02: 13 23 33 43 14 24
r03: 34 44 15 25 35 45
```

For the *adaptive RLE* form — which is what essentially every real Radiance file
larger than 8 pixels uses — it is worse than scrambled. `rgbe_ReadPixels_RLE()`
validates each scanline's declared length against `scanline_width`
(`PluginHDR.cpp:404`), so an X-major file is rejected outright:

```
--- xmajor_rle.hdr        ("+X 8 +Y 16", 8 scanlines of 16)
[FI] RGBE bad file format: wrong scanline width
load failed
--- xmajor_rle_as_old.hdr (identical bytes, labelled the pre-fix way)
loaded 16x8
```

That is a file the library used to load, and no longer loads.

### Why the existing test did not catch it

`.claude/audit/poc/hdr_transposed.hdr` is `+X 8 +Y 2`. Two columns is below the
`scanline_width < 8` threshold at `PluginHDR.cpp:386`, so RLE is disabled and the
file is a flat pixel stream; and for a flat FIT_RGBF image, 2×8 and 8×2 occupy
the same 192 bytes in the same order, so the harness checksum is *identical*
either way — `sum=6a285656bea13b03` before and after. The only thing that
changed in the corpus was the printed dimensions, which is precisely the half of
the defect the commit fixed.

### After the fix

Conformance allowed two remedies: decode and transpose, or reject the ordering
explicitly with a clear message. `04a8dcb` does the first. `rgbe_ReadHeader()`
now reports which axis was named first, and `Load()` reads the X-major form a
column at a time, scattering each column across the rows. Because `+Y` means
the Y axis ascends, scanline *x* holds column *x* from the bottom up — already
the dib's own row order — so only the transpose has to be undone.

The two twins above now decode to the same picture, and so does a 9×12 pair in
which *both* dimensions clear the 8-pixel threshold, so that both halves really
are run-length encoded:

```
--- p_flat_ymajor  ("-Y 4 +X 6")      --- p_flat_xmajor  ("+X 6 +Y 4")
r00: 10 11 12 13 14 15                r00: 10 11 12 13 14 15
r01: 20 21 22 23 24 25                r01: 20 21 22 23 24 25
r02: 30 31 32 33 34 35                r02: 30 31 32 33 34 35
r03: 40 41 42 43 44 45                r03: 40 41 42 43 44 45

p_rle_ymajor  loaded 9x12 sum=a60f7c4864e6a4e8
p_rle_xmajor  loaded 9x12 sum=a60f7c4864e6a4e8     <- was "load failed"
```

Nothing else moved: `leadenhall_market_4k.hdr` (4096×2048) still hashes to
`63683b0437402dbb`, an RGBF save/reload round-trip is exact, and over the whole
audit corpus — 104 entries — not one result changed.

The remaining six spellings still do not parse at all; that is D12, and it is
untouched. The parse should really come from the two signs and the two axis
letters rather than two fixed format strings, which would close D12 as well.

---

# B. Conformant, but stricter than before

These reject a class of file that the format does not permit (or permits only
ambiguously). Each is defensible; each is listed so the tightening is a
decision on the record rather than a side effect.

## B1. DDS `b926485` — `dwRGBBitCount` restricted to {8, 16, 24, 32}

`PluginDDS.cpp:604-615`. The uncompressed `LoadRGB()` path now refuses any other
value. `FreeImage_Allocate()` silently coerces an unknown depth to 8, after
which the function's `bpp` and the bitmap's disagree; refusing is correct. Every
`DDPF_RGB`/`DDPF_LUMINANCE`/`DDPF_ALPHA` surface Microsoft documents uses one of
these four. A 4-bit or 2-bit DDS would now be refused — no such format exists in
the DDS pixel-format tables.

## B2. DDS `45afab6` — magic number and structure sizes checked in `Load()`

`PluginDDS.cpp:911-919`. `dwMagic == 'DDS '`, `DDS_HEADER.dwSize == 124` and
`DDS_PIXELFORMAT.dwSize == 32` are all *mandatory* per Microsoft's description
("This member must be set to 124" / "Structure size; set to 32"). `Validate()`
already made these checks but only runs from `FreeImage_GetFileType()`; an
application that names `FIF_DDS` from the extension bypassed it entirely.
Applying them in `Load()` makes the two entry points agree.

Caveat worth knowing: a handful of ancient tools emit `dwSize` fields that are
wrong. Such files were already rejected by `FreeImage_GetFileType()`, so this
does not narrow the set of files the library as a whole accepts — it only closes
the "caller picked the format itself" hole.

## B3. DDS `305c4c8` — surface size related to the bytes actually present

`PluginDDS.cpp:619-637`. `height > avail / fileLine` now rejects. The bound uses
the *unpadded* row length, so it is a lower bound and cannot reject a
`DDSD_PITCH` file with padded rows, mipmaps or cube faces (all of which make the
file larger, never smaller). Correct.

**Behaviour change:** a truncated but otherwise valid DDS now fails instead of
returning a partially decoded image. That is a deliberate trade; some viewers
prefer the partial image. It is the right default for a library.

## B4. ICO `508a405` — `biWidth <= 0 || biHeight <= 0` rejected

`PluginICO.cpp:310`. An icon stacks its XOR and AND masks, so `biHeight` is twice
the image height; unlike a BMP, the ICO format has no top-down form and a
negative `biHeight` is meaningless. Verified that the PNG ("Vista") icon entries
take an entirely separate path (`IsPNG()` → `FreeImage_LoadFromHandle(FIF_PNG,…)`
at `PluginICO.cpp:448`) and so cannot be caught by this check.

Measured: `biHeight = 8` on a 4×4 icon decodes correctly; `biHeight = -8` is now
refused (it previously produced a 4×2 bitmap and a 4 GiB read request).

## B5. IFF `5f2b599` — the FORM size is now trusted as a bound

`PluginIFF.cpp:258`: `if (ch_size > size) break;`. Per IFF, a FORM's `ckSize`
covers every subchunk including pad bytes, so a chunk claiming more than the
FORM has left is malformed. The pad-byte accounting that follows
(`PluginIFF.cpp:455-465`) is correct: odd-length chunks consume one extra byte
that is not counted in `ch_size` but *is* counted in the FORM's.

**Behaviour change:** a file whose FORM size is understated now stops at the
offending chunk instead of decoding it. Amiga-era writers were not always
careful with FORM sizes. No file in the corpus is affected (all four `.lbm`
seeds decode unchanged), but it is the one tightening here with a plausible
real-world false-positive.

## B6. XPM `dabe04b` — pixel rows shorter than `width * cpp` rejected

`PluginXPM.cpp:329`. XPM3 requires each pixel row to be exactly `width * cpp`
characters, so the test is a lower bound and cannot reject a conforming file.

Side effect worth naming: FreeImage's `ReadString()` reads to the first closing
quote and has never supported C string concatenation (`"aaaa" "bbbb",`). Such a
file used to read past the end of the short string; it is now rejected with a
message. Both are non-conformant handling — but a diagnosed failure beats an
out-of-bounds read.

## B7. `03fcfc5` — `FreeImage_Allocate*()` now returns NULL for a negative width

`BitmapAccess.cpp:309`. This is a **public API behaviour change**, not a format
one: `FreeImage_Allocate(-100, 100, 24)` used to return a 100-pixel-wide bitmap
and now returns NULL. That is the right call — silently substituting a different
size is what let a DDS with `dwWidth = 0xFFFFFFF0` and a Radiance file saying
`+X -1` each write a whole file into one scanline — but any caller that relied
on the `abs()` will now see an allocation failure.

The asymmetry with height is correct and necessary: `height < 0` is how a
top-down DIB is spelled and `PluginBMP` passes `biHeight` through unchanged
(`PluginBMP.cpp:541`, `:779`), so `abs()` has to stay there. Ruling out
`INT_MIN` first is also correct — `abs(INT_MIN)` is undefined.

---

# C. Conformant — verified

## C1. DDS `9bd2f57` — 16-bit surfaces read two bytes per pixel

`PluginDDS.cpp:672-675`. The distinction the commit draws is exactly the one the
format requires: a 16-bit surface is stored 2 bytes per pixel in the file and
expanded into a 24-bit bitmap in memory, so the read length must come from
`ddspf.dwRGBBitCount`, not from `FreeImage_GetBPP(dib)`. `DDSD_PITCH`'s
`dwPitchOrLinearSize` is "the number of bytes per scan line in an uncompressed
texture", so `delta = filePitch - fileLine` is the correct padding.

Measured on three hand-built RGB565 files — no `DDSD_PITCH`, `DDSD_PITCH` with
pitch == row bytes, and `DDSD_PITCH` with 4 bytes of row padding — all three
decode to the identical, correct pixels:

```
r00: 00 04 08 | 00 08 08 | 00 0c 08 | 00 10 08     (B G R, green = (x+1)*4, red = (y+1)*8)
```

See D2 for the one case in this expression that is still unguarded.

## C2. RAS `c1f2f66` — the full-palette file loads at last

`PluginRAS.cpp:320-351`. The old code shrank `numcolors` when the map was short
and **threw on everything else**, so `maplength == 3 * 2^depth` — the exact shape
every conforming palettised Sun raster has — landed in the `else throw`. No
standard 256-colour `.ras` could be opened. Removing the throw is correct, and
the `depth <= 8` guard on `1 << depth` is necessary (shifting by 32 is UB, and
32-bit surfaces reach the same switch).

Measured: an 8-bit RAS with a 768-byte `RMT_EQUAL_RGB` map now loads with the
planes read correctly — `pal[0] = 10 20 30`, `pal[1] = 11 21 31` from
planar red/green/blue planes; `ras_full_palette.ras` goes from `load failed` to
a correct decode. The new seek past unread colormap bytes is right: the spec
puts the pixel data immediately after `ras_maplength` bytes.

The over-long-map case this opens is a genuine defect — **D1**.

## C3. CUT `5cd7e6f` — the row bound also makes the stream accounting exact

`PluginCUT.cpp:180`. The Dr. Halo format is: a 6-byte header (`WORD Width`,
`WORD Height`, `WORD Reserved`), then for each scan line a 16-bit word giving the
encoded length, the RLE packets, and a `0x00` row terminator.

FreeImage's `CUTHEADER` is `{WORD width; WORD height; LONG dummy;}` = **8** bytes,
so the header read swallows the 6-byte header *plus the first row's length word*.
That is why the decode loop can ignore length words entirely and still stay in
step, and why it reads two bytes after each row terminator — the comment calling
them "two useless bytes [that] paint shop pro adds" is wrong; they are the next
row's mandatory length word.

With `--y < 0 → break`, the loop reads exactly `height - 1` length words after the
header, for `height` total. That is exactly right. Before the fix the last row's
terminator consumed two bytes that do not exist and walked `bits` below the
allocation.

Measured on a hand-built spec-conforming 4×3 CUT (length word before every row):

```
4x3 bpp=8
r00: 0a 0b 0c 0d
r01: 14 15 16 17
r02: 1e 1f 20 21     <- exactly the 10,11,12,13 / 20..23 / 30..33 encoded
```

## C4. PICT `5942c98` — `expandBuf8()` counts source bytes, and the caller now agrees

`PluginPICT.cpp:754-763`. The commit's central claim is that the unpacked branch
must pass `rowBytes`, not `width`. Three independent witnesses agree:

* **ImageMagick** `coders/pict.c` `DecodeImage()`: `if (bytes_per_line < 8)` →
  unpacked; `if (bytes_per_line > 250)` → 16-bit scanline length else one byte;
  `if (bits_per_pixel <= 8) bytes_per_line &= 0x7fff;`; and for the unpacked case
  it reads `number_pixels` = `bytes_per_line` bytes per row. Identical to
  FreeImage's structure, and it reads **rowBytes** bytes per row.
* **In-tree**: `SkipBits()` at `PluginPICT.cpp:432` skips `rowBytes*height` for
  the `rowBytes < 8` case — FreeImage's own code already says an unpacked row is
  `rowBytes` bytes.
* The old code's own internal contradiction: for bpp 1/2/4 the main loops
  consumed `width` *source bytes* while the "leftover pixels" blocks below them
  treated `width` as a *pixel* count. It could not be right both ways.

The RLE unit accounting after the rewrite is also correct. `PixelPerRLEUnit` is
8/4/2/1/1 for bpp 1/2/4/8/16 (`PluginPICT.cpp:726-747`), so one source byte
expands to `unit` destination pixels, the repeat loop steps by `k * unit`, and
`advanceRow(dst, len * unit, dst_end)` matches what was written. `j += len *
pkpixsize + 1` correctly counts source bytes plus the flag byte.

Clamping at `dst_end` also correctly subsumes the deleted "leftover pixels"
blocks: the spare bits in a row's last source byte are padding, which is what
QuickDraw means them to be. And continuing to *consume* the source after the row
is full is what keeps the following rows in step — the right choice.

The corpus PoCs `pict_expandbuf8.pct` and `pict_unpackbits.pct` go from
heap-buffer-overflow to bounded decodes; no valid PICT changed.

## C5. PSD `d47ad42` + `fd65d1d` — the written file now matches the Adobe layout

Parsing a PSD written by the build at the tip of this range, field by field:

```
psd_indexed.psd   sig=8BPS ver=1 channels=1 4x2 bpc=8 mode=2 (Indexed)
  colour mode data length=768                   <- spec: 768 for Indexed
  image resources section length=68
     8BIM id=1005 size=16     (ResolutionInfo)
     8BIM id=1007 size=14     (DisplayInfo)
     8BIM id=1046 size=2 -> count=256           <- Indexed Color Table Count
  section ends exactly at declared end: True

psd_1bpp.psd      sig=8BPS ver=1 channels=1 8x2 bpc=1 mode=0 (Bitmap)
  colour mode data length=0                     <- spec: only Indexed/Duotone carry one
  image resources section length=54
     8BIM id=1005 size=16
     8BIM id=1007 size=14
  section ends exactly at declared end: True
```

Everything checks out against the specification:

* The colour mode data block is 768 bytes for Indexed and absent for Bitmap.
  Adobe: *"Only indexed color and duotone have color mode data. For indexed
  color images, the length will be 768."* The old code allocated
  `GetColorsUsed() * 3` bytes and indexed it with a hard-coded stride of 256, so
  a 1-bpp save allocated six bytes and wrote at offsets 256, 257, 512 and 513 —
  a heap overflow on the ordinary path.
* `PSDP_RES_INDEXED_COLORS` is 1046 = 0x0416, which is the correct ID for
  *(Photoshop 6.0) Indexed Color Table Count*, not 1047 (Transparency Index).
* `psdImageResource::Write()` emits the empty Pascal name as two zero bytes
  (`PSDParser.cpp:459-463`), which is the required length-0 string plus its pad
  byte; the reader's `if (0 == (nSizeOfName % 2)) read one more`
  (`PSDParser.cpp:1163`) is the matching rule.
* The image-resources section length is back-patched from the final stream
  position (`PSDParser.cpp:2280-2286`), so the new resource is covered by the
  declared length. Verified: the parse lands exactly on the declared end.
* `psdColourModeData::Write()` now emits the length big-endian via `psdSetValue`.
  Previously it wrote the host-order `int`, so every indexed PSD the library
  produced declared a 0x00030000-byte colour table.

Round-trip verified: an 8-bpp bitmap with palette `(3i, 5i, 7i)` comes back with
`pal[3] = 09 0f 15` and its pixels intact; a 1-bpp save no longer overruns.

There is a pre-existing polarity defect in Bitmap mode that these fixes made
reachable — **D3**.

## C6. PSD `a4df0a0` — unaligned access

`PSDParser.cpp:110-150`, `:190-220`. `memcpy` into a local is the portable
spelling of an unaligned load and compiles to the same instruction where the
hardware allows one. The byte-swap logic is untouched, so no decoded value
changes on any platform; the PSD header genuinely does place 4-byte fields at
offsets 14 and 18, so the old casts were misaligned on every file. `PSDSetValue`
losing its bogus `const` is a straight correctness fix.

## C7. TARGA `aafe9c5` + `2003de9`

`PluginTARGA.cpp:651`, `:673`, `:603`.

The per-pixel `y >= height` test is the real bound; the pre-existing linear
estimate at `:637` *under*-counts (it ignores the `pitch - line_size` cost of
each row boundary a packet crosses), so it can never falsely reject a valid
file — it only falsely accepts, which is why the inner test was needed. Correct
reasoning, correctly implemented.

**A leniency worth naming explicitly:** TGA 2.0 says a run-length packet should
never encode pixels from more than one scan line. FreeImage's
`x >= line_size → y++` inside the packet loops accepts packets that do cross
rows. The fix keeps that leniency and merely bounds it. That is the right
decision for a reader — plenty of encoders violate the rule — but it is a
deliberate deviation from the standard and should stay documented.

The `sz < file_pixel_size` floor is purely defensive: it only ever *raises* the
IOCache size, so it cannot change how any file decodes. It closes a real hole —
`IOCache::getBytes()` hands back a raw pointer and assumes `count` bytes are
behind it, and the file chooses the cache size.

## C8. IFF `8b585c7` — PackBits bounded at the padded row length

`PluginIFF.cpp:312`: `unsigned line = FreeImage_GetLine(dib) + 1 & ~1;`.
Operator precedence makes that `(GetLine + 1) & ~1`, i.e. the row length rounded
up to an even number of bytes — which is exactly the PBM row padding ILBM
requires, so pad bytes are consumed rather than leaked into the next row. And
`line <= pitch` always holds (rounding to 2 never exceeds rounding to 4), so the
clamp writes stay inside the allocated scanline.

Consuming the literal bytes of an over-long packet while declining to write them
is the correct choice: it keeps the following rows in step. Treating
`rle_count == 128` as a no-op is what PackBits specifies.

## C9. RAS `ffa77c9` — `WORD` counters widened

`PluginRAS.cpp:199-207`. `ras_width`/`ras_height`/`ras_depth` are 32-bit fields;
16-bit counters truncated the line length and made the row and column loops wrap
at 65536 and never terminate. Widening them to `unsigned` matches the header.
Verified: `ras_x_overflow.ras` (100000 wide) and `ras_y_overflow.ras` (70000
tall) go from UBSan error / hang to correct decodes.

## C10. CUT `f052cff`, XPM `215257a`/`6e7bc25`, XBM `0f9e945`/`17d467d`, WBMP `0691416`, PICT `beec28f`, PSD `8c8d529`, HDR `83b4785`, MemoryIO `ae73c7a`

All safety/robustness fixes with no bearing on how a conforming file is
interpreted. Points checked:

* **WBMP** — `sizeParamIdent = (b & 0x70) >> 4` is bounded by 7 and
  `sizeParamValue = (b & 0x0F)` by 15, so `BYTE Ident[8]` and `BYTE Value[16]`
  cannot be overrun; the WBMP type-3 extension header defines those widths in
  exactly those bit fields. The two added `read_proc` checks close a two-byte
  denial of service (the field is primed with `0x80` and `read_proc` leaves it
  untouched past EOF).
* **XBM** — `readLine(str, n, …)` writes the terminator at `str[n]`, so
  `MAX_LINE - 1` is the correct argument for `char line[MAX_LINE]`; it also
  makes the `strlen(line) == MAX_LINE - 1` over-long-line check reachable for
  the first time. `readChar()` returning `EOF` is checked at all five call sites
  before `hex_table[]` is indexed (`PluginXBM.cpp:202`, `:208`, `:215`, `:232`, `:243`), so `EOF == -1` never becomes a negative array index.
* **PSD `UnpackRLE`** — clamping the source as well as the destination is
  required by PackBits (a packet may claim more than the compressed line holds),
  and the `srcSize -= len` underflow it fixes was what defeated the loop's own
  guard.
* **HDR `83b4785`** — `%d` must land in an `int`; writing straight into the
  `unsigned` outputs let `+X -1` through as 4294967295. Correct.
* **MemoryIO `ae73c7a`** — the 64-bit product is the right fix. The concrete
  old failure is `size = 0x10000, count = 0x10001`: the 32-bit product wrapped
  to 65536, so the function copied 64 KiB and returned a count claiming 4 GiB.
  Zero-size reads still return 0 (there is an early guard at
  `FreeImageIO.cpp:86`), so the memory backend and the stdio backend continue to
  agree — checked: `FreeImage_ReadMemory(buf, 0, 3, m)` → 0, `fread(buf, 0, 3,
  fp)` → 0.

---

# D. Residual non-conformance

None of these were introduced by the range. Three are here because the fixes
touched, depend on, or exposed them; the rest are recorded because they are in
the same decoders and a conformance review that omitted them would be
misleading.

## D1. RAS — an over-long colormap reads green and blue out of the red plane

`PluginRAS.cpp:320-351`. **This path is newly reachable because of `c1f2f66`.**

EGFF: for `RMT_EQUAL_RGB` *"the colors are separated into three planes, stored in
RGB order, with each plane being one-third the size of the ColorMapLength
value."* Each plane is `maplength / 3` bytes. FreeImage reads `3 * numcolors`
bytes contiguously and slices them at `numcolors`:

```c
r = (BYTE*)malloc(3 * numcolors);
g = r + numcolors;  b = g + numcolors;
io->read_proc(r, 3 * numcolors, 1, handle);
```

That is correct only when `numcolors == maplength / 3`. It holds for the short
map (`numcolors` is set to `maplength / 3`) and for the exact map
(`3 * 2^depth == maplength`) — the two normal cases, and both decode correctly.
It fails when `maplength > 3 * 2^depth`: `numcolors` stays at `2^depth`, and the
"green" and "blue" bytes are taken from inside the red plane. The new seek then
discards the real green and blue planes.

Measured on a 1-bit RAS carrying a full 768-byte map (planes `0x10…`, `0x20…`,
`0x30…`):

```
16x2 bpp=1
pal: [0]=101214 [1]=111315      <- should be [0]=102030 [1]=112131
```

Before the fix this file threw "Invalid palette"; it now loads with wrong
colours. Writing a full 256-entry map on a low-depth image is unusual but not
forbidden.

**Fix:** read `numcolors` bytes, skip `maplength/3 - numcolors`, three times.

## D2. DDS — `delta` is still unguarded against `filePitch < fileLine`

`PluginDDS.cpp:675`. Now that `delta = filePitch - fileLine` is computed against
the right row length, nothing stops it going negative. The DDS description makes
pitch the row length, so `dwPitchOrLinearSize < fileLine` is malformed — and the
common real-world spelling of that is `DDSD_PITCH` set with the field left at 0.

Measured on a 4×4 32-bit DDS with `DDSD_PITCH` and `dwPitchOrLinearSize = 0`:

```
r00: 10 10 10 10 11 11 11 11 12 12 12 12 13 13 13 13
r01: 10 10 10 10 11 11 11 11 12 12 12 12 13 13 13 13     <- every row is row 0
r02: 10 10 10 10 11 11 11 11 12 12 12 12 13 13 13 13
r03: 10 10 10 10 11 11 11 11 12 12 12 12 13 13 13 13
```

The seek winds the stream back a full row each time. Silently wrong output, and
on a large declared height it is also a long backwards-seek loop.

**Fix:** `if (delta < 0) { /* reject, or treat as delta = 0 */ }`.

## D3. PSD — Bitmap mode is written without the format's polarity

`PSDParser.cpp:1707` vs `:1829` (`WriteImageData`). **Made reachable by `d47ad42`** — before
that commit, saving 1-bpp overran the heap, so this was masked by a worse bug.

FreeImage's PSD *reader* applies `CREATE_GREYSCALE_PALETTE_REVERSE(pal, 2)` for
`PSDP_BITMAP`, i.e. it takes bit 0 to mean white and bit 1 to mean black.
That is the correct PSD convention — ImageMagick's `coders/psd.c` expands the
same data with `*pixels++ = (pixel >> 7) & 0x01 ? 0U : 255U;`, a set bit
becoming black. `WriteImageData()` applies no such inversion: it copies
FreeImage's indices verbatim. The reader is right and the writer is wrong.

Measured — an 8×1 1-bpp bitmap saved with `pal[0] = black, pal[1] = white` and
reloaded:

```
wrote bits=f0 pal0=00 pal1=ff   ->   read bits=f0 pal0=ff pal1=00
```

The bits round-trip; the palette inverts. A 1-bpp image saved as PSD and
reloaded comes back as its photographic negative, and Photoshop will show it
inverted too.

**Fix:** the writer's real defect is that it ignores the *sense* of the source
palette. A `FIC_MINISWHITE` 1-bpp source (`pal[0]` white) already round-trips
correctly, because bit 1 means black on both sides; only `FIC_MINISBLACK`
sources — FreeImage's usual sense, and what the measurement above used — come
back negated. So: invert on write when
`FreeImage_GetColorType(dib) == FIC_MINISBLACK`, and leave `FIC_MINISWHITE`
alone.

## D4. RAS — a 32-bit surface with an odd width consumes a phantom pad byte — **fixed, `7c56d90`**

`PluginRAS.cpp:379-385`. `fill` is derived from `linelength % 2` where
`linelength = header.width`, but a 32-bit row is `width * 4` bytes, which is
always even — Sun raster pads rows to 16 bits, so no pad byte exists. (For
24-bit it happens to work: `width * 3` is odd exactly when `width` is.)

Measured on a 3×2 32-bit RAS — row 1 is displaced by one byte:

```
r00: 10 20 30 ff | 11 20 30 ff | 12 20 30 ff      <- correct
r01: 20 30 ff 20 | 20 30 ff 21 | 20 30 30 22      <- shifted by 1
```

This sits inside the `linelength`/`fill` block that `ffa77c9` rewrote, so it is
worth folding into the same area: `fill` should be computed from the row's byte
length, not from the pixel count.

Done in `7c56d90`: `linelength` now holds what its name says — `width * depth/8`,
or `(width + 7) / 8` at 1 bpp — and `fill` follows from its parity. The file
above decodes to `0d 0e 0f 0c | 11 12 13 10 | 15 16 17 14`. Controls checked at
the other depths and parities: 24 bpp odd width, 8 bpp odd width (which does
carry a pad byte, and still consumes it), 32 bpp even width, 1 bpp width 5.

## D5. ICO — `biHeight` is not required to be even

`PluginICO.cpp:311-313`. `height = bmih.biHeight / 2` truncates an odd value, and
the XOR read (`height * pitch`) and the AND-mask read that follows it
sequentially then both land in the wrong place. Measured: `biHeight = 7` on a 4×4
icon silently yields a 4×3 bitmap. The format requires `biHeight == 2 * height`.
One more clause on the check at `:310` would close it.

## D6. PSD — an indexed file without resource 1046 loses its palette

`PSDParser.cpp:1710`: the reader refuses to use the colour table unless
`_ColourCount >= 0`, i.e. unless image resource 1046 is present. The
specification makes the 768-byte colour mode data authoritative and 1046 merely
the count of entries that are *defined*. A conforming indexed PSD that omits
1046 is read with a default greyscale palette. `fd65d1d` makes FreeImage's own
files round-trip, which is the important half, but does not relax the reader.

## D7. PICT — the `rowBytes == 0` fallback is wrong for sub-8-bpp

`PluginPICT.cpp:720-722`: `if (rowBytes == 0) rowBytes = pixwidth;` where
`pixwidth` is the pixel count (doubled for 16 bpp). For 1/2/4 bpp the real row
length is `ceil(width * bpp / 8)` rounded to even, so the substituted value is
8×/4×/2× too large. `5942c98` did not introduce this, but it did make
`expandBuf8()`'s source-byte count depend on `rowBytes`, so a wrong `rowBytes`
now mis-consumes the stream where before it was merely ignored.

Only reachable through `DecodeOp9a()` (`:846-857`), which passes `rowBytes = 0`;
opcode 0x9A is `DirectBitsRect` and is 16- or 32-bit by definition, so a 1/2/4-bpp
arrival there is already a malformed file. Memory-safe after the fix — the
`dst_end` clamp holds. Low severity, but the fallback should compute
`((width * pixelSize + 7) / 8 + 1) & ~1`.

## D8. IFF — the ILBM branch does not consume over-long literal packets

`PluginIFF.cpp:397-406`. The interlaced path clamps the *write* when a literal
packet exceeds `src_size` but reads only the clamped count, leaving the rest of
the packet in the stream and misaligning everything after it. `8b585c7` fixed
exactly this asymmetry in the PBM branch and left the ILBM one as it was. The
loop is bounded by `src_size` so it terminates, but the decode is wrong.

## D9. RAS — `RMT_RAW` allocates a file-controlled size unchecked — **fixed, `dcdb5d1`**

`PluginRAS.cpp:362-364`: `malloc(header.maplength)` with no NULL check, then
`read_proc` through the result. `ras_maplength` is a 32-bit file field, so this
is a 4 GiB allocation request followed by a NULL dereference. Adjacent to the
colormap code `c1f2f66` changed but in a different `case`. (The block reads the
colormap purely to skip it; a `seek_proc` would be both safer and faster.)

Done in `dcdb5d1`, by the second half of that parenthesis: the allocation is gone
and the plugin seeks past the colormap, in steps that fit a `long` on a 32-bit
build. A 34-byte file's largest request drops from 4294967295 bytes to the 4096
the library itself asks for. The other three `malloc`s in the same plugin are
checked in the same commit.

## D10. DDS — dead variable

`PluginDDS.cpp:673`: `const int line = CalculateLine(width, FreeImage_GetBPP(dib));`
is no longer referenced after `9bd2f57` replaced its uses with `fileLine`. It is
a `-Wunused-variable` warning and, more to the point, a reader's trap: it looks
as though the bitmap line length still participates. Delete it.

## D11. CUT — scan-line length words are ignored

`PluginCUT.cpp:170-196`. The decoder relies solely on the `0x00` row terminator
and blindly skips the two-byte length word. A conforming decoder would use the
length to bound each row, which would also make the decoder robust against a row
that never terminates. Works on well-formed files; the fix bounds it safely.

## D12. HDR — only two of the eight legal resolution strings parse at all

`PluginHDR.cpp:267-270`. Radiance allows `{-Y,+Y} × {-X,+X}` in either order —
eight spellings, encoding `YMAJOR`, `YDECR` and `XDECR`. FreeImage hard-codes two
`sscanf` format strings, so `+Y 512 +X 768`, `-Y 512 -X 768` and the rest fail
with "missing image size specifier", and the sign flags (which say whether rows
run top-to-bottom and columns left-to-right) are never consulted. Parsing the
two signs and the two axis letters, then flipping/transposing accordingly, would
fix A1 and this at once.

---

# E. Recommended follow-ups, in order

1. ~~**HDR `+X … +Y …` (A1)**~~ — **done, `04a8dcb`**: the form is now read a
   column at a time and transposed into the bitmap. It was the only regression
   in the range.
2. **RAS over-long colormap (D1)** — read each plane at its own stride
   (`maplength / 3`). The path is new as of `c1f2f66` and produces wrong colours.
3. **PSD Bitmap polarity (D3)** — invert on write. Newly reachable now that
   1-bpp save works, and it makes every 1-bpp PSD the library writes a negative.
4. **DDS negative `delta` (D2)** — one comparison; the `DDSD_PITCH` with
   `dwPitchOrLinearSize = 0` case is common enough to matter.
5. ~~**RAS 32-bit odd-width `fill` (D4)**~~ — **done, `7c56d90`**.
6. Lower priority: D5 (ICO even height), D8 (ILBM literal consumption),
   ~~D9 (`RMT_RAW` malloc)~~ — **done, `dcdb5d1`** — D10 (dead variable),
   D7 (PICT `rowBytes` fallback), D6 (PSD resource 1046), D11, D12.

D4 and D9 are the only two the second-pass range closed. D1, D2, D3, D5, D6, D7,
D8, D10, D11 and D12 stand exactly as written above — none of the 21 fixes went
near them, and D1's measurement was re-run unchanged.

# F. Reproducing

The library was built from the tip of the range with the stock `make -f
Makefile.gnu`. The probes and generated test files are throwaway and live in the
job's scratch directory, not in the repository; each one is described inline
above in enough detail to rebuild it. The corpus comparison is

```
.claude/audit/check.sh                      # with H pointed at a stock-build harness
diff <(join baseline.txt) <(join now.txt)   # joined by filename
```

against `.claude/audit/baseline.txt`, which is the pre-fix record.

---

# G. Addendum: the second pass's 21 fixes, `6083928..350b9f0`

This report reviewed `215257a..d883162`. The 21 commits that followed fix
AUDIT.md's second-pass findings, and six of them had to choose between readings
of a malformed file that the format does not settle. Recorded here for the same
reason as everything above: a decode decision is a conformance decision.

Each was checked the same way — the reproducer, its well-formed control, and the
72-file valid corpus, which is byte-identical across all 21 commits.

## G1. RAS `7c56d90` — the pad byte follows the row, so the row's length decides it

D4, above.

## G2. SGI `b598bf4` — an opcode with a zero run length is skipped

The SGI RLE opcode `0x80` gives a run of no pixels. Two readings are defensible:
the format's own (`pixel = count & 0x7f; if (!pixel) break;` — end of scanline)
and PackBits' (a no-op; FreeImage's PICT decoder already comments "Special case:
repeat value of 0. Apple says ignore."). The old code produced neither — the
counter went to -1 and every later byte of the stream came out as a literal
pixel.

Skip was taken. This decoder reaches every row through the RLE offset table and
writes exactly `width` pixels per row, so "end of scanline" would leave the rest
of the row with no defined content — trading a decode bug for a disclosure. And
the loop that fetches the opcode already skipped a bare `0x00`, the other value
whose low seven bits are zero, so skipping is what the code was already doing for
half the cases. A row encoded `0x82 11 22 | 0x80 | 0x82 33 44` now decodes to
`11 22 33 44`, identical to the same row written as one literal run.

## G3. PCX `0b2c4d2` — a run length of zero emits nothing

The PCX repeat count is 1..63 and the format has no meaning for 0. `count` was a
`BYTE` and `count--` made it 255, so one `0xC0` byte replaced a row with 256
copies of one value. netpbm's `pcxtoppm` uses `while (count-- > 0)` on an `int`
and ImageMagick's `coders/pcx.c` the same: both emit nothing. So does FreeImage
now, and the reproducer decodes byte for byte the same as a control carrying no
`0xC0` packet.

Skipping a packet means the decode must be able to make progress without writing,
which needs the same commit's other half: the two buffer refills discarded their
result, so a truncated file went on decoding the previous refill — and on the
first one, the uninitialised heap the buffer is `malloc`'d from. What the stream
cannot supply is zeroed, which makes a truncated PCX decode the same way twice
and makes the loop provably terminate.

## G4. ICO `b522371` — 2 bpp leaves the whitelist

The CVE-2020-24292 bit-depth whitelist admitted 2, which
`FreeImage_AllocateBitmap` has no case for and rounds up to 8 — so the bitmap was
8 bpp while `line` and `pitch` had been computed for 2, and a quarter of the rows
were ever written. Windows icons are 1, 4, 8, 16, 24 or 32 bpp; 2 is not a depth
the format defines, and it is off the list rather than added to the allocator.

## G5. PNM `5f2f20a` — an ASCII sample is held to `maxval`

The PNM formats require every sample to be `<= maxval`. Nothing enforced it, and
the scaling that follows is `255 * level` in an `int` and
`65535 * (double)level` cast to a `WORD` — overflow and an out-of-range float
conversion respectively, both undefined. Samples are clamped to the declared
`maxval` at the one place they are read. Conforming files are unaffected: ASCII
greymaps and pixmaps at `maxval` 255 and 65535 decode to the same bytes as
before.

## G6. KOALA `b527e6e` — a short file is refused, not zero-filled

`koala_t` is 10001 bytes and every one of them becomes a pixel. A file that does
not carry a whole image was decoded from the stack; it is refused now rather than
padded, because a KOALA file has exactly one length and a shorter one is not a
picture of anything. (The same commit masks `image.background` to a nibble, as
every other branch of the colour switch does: a background byte of `0x35` was
packed as `0x75`, two different palette indices for one colour.)

## G7. Stream-relative seeks — PCX `9e64484`, ICO `350b9f0`

Not a format choice but a container one. An image's internal offsets — a PCX's
palette and header lengths, an ICO's directory position and each
`dwImageOffset` — count from the start of the image, not of the file it is
embedded in. Both plugins seeked to them absolutely, so neither was readable
through a handle positioned inside a larger stream. Both now add the position the
load started at. Verified by decoding the same files at stream offset 0 and 64
and comparing the pixels.

One case is deliberately left: PCX's `seek_proc(handle, -769L, SEEK_END)`. "The
last 769 bytes of the file" is the format's own definition of where an 8-bpp PCX
keeps its palette, not an assumption the plugin makes, and an embedded PCX with
data after it gives no way to work out where the palette really is.
