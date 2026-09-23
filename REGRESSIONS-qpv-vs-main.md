# qpv vs main: regression review (2026-09-22)

**Compared:** `origin/main` = `5af83c4` (plus 2 README-only commits) against `qpv` = `c67d625` (265 commits).
**Question:** what worked on main, for a valid input or an existing caller, and is broken or worse on qpv?
Fixed crashes on malformed files are not regressions. A stricter refusal of a *valid* file would be.

## Verdict

| # | Finding | Kind | Introduced by | Affects QPV? |
|---|---|---|---|---|
| R1 | Exif thumbnails are never loaded (`FreeImage_GetThumbnail` returns NULL) | **regression**, fixed in 1d9d70a | 6e64300 | no (QPV never calls GetThumbnail), but re-saved files lose their thumbnail |
| R2 | Fattal02 tone mapping darkens/shifts ordinary images and adds black speckles | **regression**, fixed in f072350 | 9b55043 | no (QPV uses Drago03/Reinhard05 only) |
| R3 | Fattal02 segfaults on a flat image smaller than 32 px (main returned NULL) | **regression** (crash), fixed in 48558a3 | 9b55043 | no |
| R4 | `CloseMultiBitmap` returns FALSE for an *untouched* read-write session of a writer-less format | **regression** (API result, deliberate), fixed in 3796059 | c1442f5 | no (QPV opens read-only) |
| R5 | `MakeThumbnail(hdr, N, TRUE)` ignores `convert` when the larger side is exactly N | **regression** (contract), fixed in f3efc44 | 3f0e1dc | no (QPV never calls it) |
| R6 | Tiled JPEG 2000 with small tiles decodes up to 1.6x (64 px tiles) / 6x (16 px tiles) slower | **performance**, fixed in 2417853 | 03c89f4 | yes, for such files |
| R7 | CMYK PSDs load with the K channel reversed (colours black, pure K white) and save it uninverted | **regression**, fixed in fd94d28 | 8464e74 (C5) | yes: every 8/16-bit CMYK PSD |
| R8 | A PSD with an unusable thumbnail fails to load; a good 1033 + bad 1036 thumbnail pair segfaults | **regression** (crash), fixed in 1f7cf71 | 6e64300 | yes, for such files |
| C1-C10 | Intended behaviour changes that existing callers can notice | needs a decision | various | yes: C4 (tone-mapping look), C4b (16-bit ConvertToType in combine), C8 (`-1` load flag) |

Nothing else in the differential runs came out worse on qpv for a valid input. R7 and R8 were found while fixing
the pre-existing bugs below: the corpus had no CMYK PSD, and R8 was listed as "plausible, not reproduced".

---

## Confirmed regressions

### R1. Exif thumbnails never load (fixed in 1d9d70a)
- **Where:** `Source/Metadata/Exif.cpp:774`, from the Fedora CVE-2021-33367 patch imported in `6e64300`:
  ```cpp
  if(de_addr+4 >= (BYTE*)(dwLength + ifd0th - tiffp)) { return TRUE; } // no thumbnail
  ```
  The left side is a real heap address and the right side is a small integer (profile length + offset) cast to a
  pointer, so the test is always true. `jpeg_read_exif_dir()` returns before ever reading IFD1.
- **Who is hit:** everything routed through `jpeg_read_exif_dir`: JPEG, WebP, PSD, HEIF and AVIF Exif blocks.
- **Evidence:**
  - Decode differential: 19 JPEGs and 1 WebP lose their thumbnail, for example `TestAPI/exif.jpg`, which is
    128x85 on main and has none on qpv. The main image and every metadata model hash are unchanged.
  - Re-save differential: on main, a loaded JPEG's thumbnail is carried into saved JPEG/PSD/TGA/TIFF/WebP files.
    On qpv those files come out without one.
  - This is also the root cause of `TestAPI/testAPI` aborting at `testThumbnail.cpp:132`.
    `AUDIT-MULTIPAGE.md` (removed in b73f0d4) called that abort pre-existing, but it only compared qpv against
    earlier qpv builds. On main, `GetThumbnail(exif.jpg)` works.
- **Suggested fix:** compare pointer to pointer, e.g. `if ((size_t)(de_addr - tiffp) + 4 > dwLength) return TRUE;`.
- **Fix (1d9d70a):** the IFD0 entry count, the link to the 1st IFD, its entry count and the thumbnail range are
  checked as 64-bit offsets, and so is the entry count the IFD loop reads first.
  - Re-enabling this block would otherwise have reopened two upstream overreads: the IFD1 count read and the
    32-bit wrap of `thOffset + thSize`.
  - ASan, calling `jpeg_read_exif_profile` on exact-size buffers: main's `Exif.cpp` overreads in all four crafted
    profiles, and pre-fix qpv's in one (an IFD0 at the profile's end). The fix overreads in none, and a
    thumbnail ending on the profile's last byte loads.
  - Decode differential: all 22 thumbnails main produces are reproduced pixel-exactly. Nothing else changed in
    2,261 files. HEIC/AVIF files without a native thumbnail now carry their Exif one; HEIF still prefers its native
    thumbnail, which is attached after the Exif block.
  - `testAPI` now runs to completion (exit 0) for the first time on qpv.

### R2. Fattal02 shifts ordinary images and adds black speckles (fixed in f072350)
- **Where:** `Source/FreeImage/tmoFattal02.cpp:433`. `LogLuminance()` now takes its minimum from
  `LuminanceRange()`, which returns a 0.1th-percentile minimum. Subtracting that positive minimum before
  `log(v+EPSILON)` pushes the darkest 0.1% of pixels onto the log cliff, where they turn black.
- **Evidence** (identical RGBF input fed to both builds):

  | Input | Mean, main → qpv | Channels off by >20 levels | Speckles, main → qpv |
  |---|---|---|---|
  | raccoon1-light (photo) | 209 → 180 | 81% | 25 → 955 |
  | Big_Dipper (photo) | 118 → 172 | 94% | 2 → 47 |
  | leadenhall_market_4k.hdr | 95 → 121 | 89% | 37 → 639 |
  | fi_exr_float.exr | 160 → 99 | 88% | 0 → 0 (dark, muddy) |

  The speckles are visible in `.claude/scratch/regress/pre2026/cmp_raccoon_fattal_crop.png`.
  - Restoring the absolute minimum makes the output byte-identical to main on all 13 photos.
  - That change leaves the four negative-radiance JXR files the percentile was written for untouched: after
    `ClampNegativeRGBF` their minimum is already 0.
- **Suggested fix:** use the absolute finite minimum, clamped at 0, and keep the robust maximum.
- **Fix (f072350):** `LuminanceRange()` (Fattal02 is its only caller) returns the darkest finite sample; the
  maximum stays robust. No clamp is needed, because `ClampNegativeRGBF` runs first.
  - The fix has a second half. A NaN pixel became a black sample in `ClampNegativeRGBF`, and with an absolute minimum that
    one pixel set the minimum and shifted the whole render: `testToneMappingSurvivesNonFiniteSamples` caught
    it. Fattal02 now turns NaN into +INF first, so like an INF it is skipped by the statistics and rendered black.
  - On each of 14 photos, under 0.05% of channels differ from main by more than 20 levels, and the speckle counts
    are main's again (5 vs 8,742 pre-fix on raccoon1-light).
  - Drago03, Reinhard05 and every JXR render are byte-identical to pre-fix qpv.
  - What still differs from main on HDR files (leadenhall) is the robust *maximum*, which is by design.

### R3. Fattal02 segfaults on a small flat image (fixed in 48558a3)
- **Repro:** `FreeImage_TmoFattal02()` on a 13x7 all-black FIT_RGBF image returns NULL on main and segfaults
  (exit 139) on qpv. The same happens at 31x31; at 40x40 qpv returns an image.
- **Cause:**
  - `nlevels` stays 0 below 32 px (`tmoFattal02.cpp:504`), so `PhiMatrix()` (`:243`) returns data from `malloc(0)`.
  - On main, a flat image threw before reaching that code. On qpv it no longer throws, so it gets there.
  - Non-flat images under 32 px crash in *both* builds; that part is an upstream bug.
- **Suggested fix:** return NULL when `nlevels == 0`.
- **Fix (48558a3):** exactly that.
  - Flat and non-flat images at 13x7, 31x31 and 31x400 return NULL. Main crashed on the non-flat ones and pre-fix
    qpv on both.
  - At 32 px and above the output is unchanged.

### R4. `CloseMultiBitmap` fails an untouched read-write session (fixed in 3796059)
- **Where:** `Source/FreeImage/MultiPage.cpp:495` (`c1442f5`). Opening a file with `read_only=FALSE` in a format
  that has no `save_proc` marks the session failed **at open time**.
- **Deliberate, but it reverses an earlier decision.** The message text at `:493` shows the failure is intended.
  Yet `28178b1`'s commit message said the opposite: an edit session on a writer-less format "is deliberately
  still allowed", and a caller that changes something "is told at the save".
- **Evidence:** `OpenMultiBitmap(fif, f, FALSE, FALSE, TRUE)` immediately followed by `CloseMultiBitmap` returns:
  - main: 1 for DDS, RAW (DNG), PCX and SGI;
  - qpv: 0, plus a "does not support writing" message. Nothing was changed and nothing was written.
- **Scope:** both `FreeImage_OpenMultiBitmap` and `OpenMultiBitmapU` share the helper at `:450`.
  - Not affected: `OpenMultiBitmapFromHandle` and `LoadMultiBitmapFromMemory` still close with 1 for the same files.
    So the .NET wrapper's stream loader (`FreeImageBitmap.LoadFromStream`, which throws on a FALSE close) is safe.
  - Not affected: QPV, which always opens read-only.
- **Suggested fix:** fail at close only when the session actually changed something (`header->changed`).
- **Fix (3796059):** the open-time flag is gone; the close path already fails a changed session on its own.
  - DDS, RAW, PCX, SGI, HEIF and AVIF: untouched and lock/unlock-only sessions close TRUE in both cache modes.
  - An edited session closes FALSE ("does not support writing") and leaves the file byte-identical, with no
    spool or cache file left behind.
  - Refused edits still report FALSE.
  - TIFF/GIF/ICO behaviour is unchanged.
  - `TestAPI/AVIF/decode` now asserts both halves.
  - The README changelog line was updated to match.

### R5. `MakeThumbnail(hdr, N, TRUE)` ignores `convert` at exactly N (fixed in f3efc44)
- **Where:** `Source/FreeImageToolkit/Rescale.cpp:206`. `3f0e1dc` changed `<` to `<=`, so an image whose larger
  side equals `max_pixel_size` takes the "return a clone" shortcut.
- **Evidence:** 100x50 source, max 100, `convert=TRUE`:
  - main returns 8-, 24- or 32-bit;
  - qpv returns the unconverted FIT_UINT16, RGB16, RGBA16, FLOAT, RGBF or RGBAF.
  - At 99 and 101 the builds agree. Main already skipped conversion for images *smaller* than max, which is an
    upstream inconsistency.
- For FIT_BITMAP input the change is an improvement: an exact clone instead of an identity resample.
- **Suggested fix:** apply `convert` in the clone path too.
- **Fix (f3efc44):** the clone takes the same conversion as a resampled thumbnail.
  - All six HDR types convert at 99, 100 and 101, including images *smaller* than `max_pixel_size`, as
    documented; main never converted those.
  - `convert=FALSE`, the types with no conversion (INT16, DOUBLE, ...) and exact-fit 1/4-bit bitmaps are still
    plain clones, and metadata is kept.

### R6. Tiled JPEG 2000 with small tiles got slower
- **Where:** `PluginJ2K.cpp:169/280` and `PluginJP2.cpp:169/280` (`03c89f4`) force
  `opj_codec_set_threads(opj_get_num_cpus())`. OpenJPEG 2.5.4 pays a per-tile threading cost.
- **Evidence** (4000x3000 RGB, 8 cores, FreeImage_Load):

  | Tiling | main | qpv | OpenJPEG 2.5.4, 1 thread / 8 threads |
  |---|---|---|---|
  | untiled | 1971 ms | 279 ms | 637 / 216 ms |
  | 1024 px tiles | 1778 ms | 368 ms | 705 / 284 ms |
  | 256 px tiles | 1839 ms | 494 ms | 778 / 452 ms |
  | 64 px tiles | 1726 ms | **2751 ms** | 1099 / **2689** ms |

  With 16 px tiles, 320x240 goes from 28 ms to 175 ms.
- The overall picture is a large speed-up; only small-tile files regress.
- **Suggested fix:** enable threads only when tiles are large (e.g. tile area ≥ 256x256, readable after
  `opj_read_header`), or cap the thread count.
- **Fix (2417853):** the loaders read the main header once and give OpenJPEG one thread per 2 KiB of compressed
  data per tile, capped at the CPU count, and no pool below 4 KiB.
  - Why that rule: OpenJPEG 2.5.4 syncs its pool once per tile for the code-blocks and twice per resolution
    level and component for the wavelet. Tile area alone does not predict the cost; heavily compressed lossy
    tiles lose even at 256 px. A one-thread pool is slower than none.
  - Measured on 94 generated files (32 px to untiled, lossless and lossy, 1/3 components, 8/16 bits, 32/64-px
    code-blocks): no file decodes slower than without a pool. The set takes 13.7 s against 21.4 s with every
    CPU and 28.7 s with none.
  - The 64-px file now loads in 1.0 s, faster than main's 1.7 s. The tiniest case (a 2000x1500 grey file, 32-px tiles, 50:1
    lossy) is still 1.28x main (53 vs 41 ms): OpenJPEG 2.5.4's inline decode is slower per tile than the 2014 snapshot,
    and the header peek costs about 1.3 µs per tile.
  - Pixels are unchanged in all 1068 J2K/JP2 test files; TestAPI/J2K passes.

### R7. CMYK PSDs load and save with K reversed (fixed in fd94d28)
- **Where:** `PSDParser.cpp` undoes PSD's inverted CMYK storage with `FreeImage_Invert()`, on load and on save.
  Since `8464e74` (C5) `FreeImage_Invert` leaves the fourth channel of 32-bit and RGBA16 images alone, and for
  CMYK that channel is K.
- **Evidence:** a hand-built 8x2 CMYK PSD with pure and 50% C, M, Y and K. Main decodes Y as 255,255,0 and K as
  0,0,0; qpv decoded every colour as 0,0,0 and pure K as 255,255,255, at 8 and 16 bits. A known-ink bitmap saved
  as CMYK PSD had its K plane written uninverted.
- **Fix:** `invertCMYK()` in PSDParser flips every 8/16-bit sample, as `FreeImage_Invert` did on main; floats
  are left alone, as before. The public `FreeImage_Invert` keeps C5. Decodes now equal main exactly.
- No other caller inside the library depends on a C-list change: C1's internal 3-argument FillBackground calls
  keep main's path, C3 has no internal callers, and C4b's `ConvertToStandardType(…, TRUE)` is used only by the
  new APNG plugin and MakeThumbnail (R5).

### R8. A bad PSD thumbnail fails the load or crashes (fixed in 1f7cf71)
- **Where:** `psdThumbnail::Read`, the Fedora CVE-2020-24293 check from `6e64300`. It throws when
  `WidthBytes < Width * bpp / 8`, which aborts the whole PSD (header-only loads too). It fires even for JPEG
  thumbnails, which never use WidthBytes, and for a thumbnail resource shorter than its 28-byte header.
  It also ran after `_dib` was unloaded but not cleared. A good 1033 thumbnail followed by a bad 1036 one left a
  dangling pointer that the destructor freed again: qpv segfaulted where main loads.
- **Fix:** an unusable thumbnail is skipped and the image loads.
  - JPEG thumbnails load as on main.
  - A raw thumbnail must be 24-bit with WidthBytes ≥ Width × 3, and its rows must fit in the resource and in the
    file, so the CVE's over-read stays blocked.
  - The reader now also resumes at the true end of the resource. Main and qpv both stopped 28 bytes early and
    could parse a fake resource out of the JPEG data.

---

## Behaviour changes that need a decision (intended, but callers can notice)

- **C1. `FreeImage_FillBackground` gained a 4th parameter** (`5112d61`, `Background.cpp:430`).
  - C source that calls it with 3 arguments no longer compiles (`FI_DEFAULT` is empty in C).
  - Win32: there is no `.def` file, so the stdcall export changes from `_FreeImage_FillBackground@12` to `@16`.
    Old 32-bit binaries fail to load.
  - Win64 and SysV: an old binary leaves garbage in the 4th argument register. If it is non-zero, the
    `FI_COLOR_IS_RGBA_COLOR` blend is skipped; if it is 1..255, it also becomes the 32-bit fill alpha.
  - A separate `FreeImage_FillBackgroundEx` would have kept the ABI.
- **C2. Animated PNGs are now `FIF_APNG` (39), not `FIF_PNG` (13)** (`0f9863f`, `PluginPNG.cpp:289`).
  - Code that tests `fif == FIF_PNG` misses them. Single-frame APNGs stay FIF_PNG.
  - For an APNG whose IDAT default image is *not* part of the animation, `FreeImage_Load(GetFileType(f), f, 0)`
    returned the default image on main. On qpv it returns animation frame 0 (verified with a crafted file).
- **C3. `FreeImage_MovePage(bm, target, source)` semantics changed** (`d79f90e`).
  - Main (upstream) moved the page at `target` in front of the page at `source`.
  - qpv moves `source` to `target`, as documented.
  - Callers written against upstream's actual behaviour will now reorder differently. QPV does not call it.
- **C4. Tone-mapping and float-conversion look** (`9b55043`, `b2ec031`). This one is visible in QPV, which
  exposes Reinhard05 and uses Drago03 for RGBF/RGBAF.
  - Reinhard05 now normalises over a 0.1% percentile range. On ordinary photos it is more saturated and
    contrasty: 6 of 13 photos move more than 10% of their channels by more than 20 levels (see `cmp_photos.png`).
  - On leadenhall: Drago 108 → 119, Reinhard 57 → 66 mean (about 27-28 dB PSNR against main).
  - RGBAF/FLOAT input is no longer clipped to [0,1] before mapping.
  - JXR files that main blew out to white are now fixed.
- **C4b. `ConvertToType(…, FIT_BITMAP, TRUE)` / `ConvertToStandardType(…, TRUE)` now stretch from the true
  minimum** (`9b55043`), as documented.
  - Main started its minimum at 255, so an image whose darkest value exceeds 255 came out nearly flat:
    UINT16 in [30000,30500] maps to 251..255 on main and 0..255 on qpv.
  - Wide-range data moves by a few levels only ([1000,60000]: 3..255 on main, 0..255 on qpv).
  - `ConvertTo8Bits` (>>8) is unchanged.
  - **Affects QPV** only in its combine-into-multi-page path (`quick-picto-viewer.ahk:62831`), for
    UINT16/INT16/UINT32/FLOAT pages. Its display path uses `ConvertTo*Bits`, which is unchanged.
- **C5. `FreeImage_Invert` leaves alpha alone** (`8464e74`, `Colors.cpp:101/123`). Upstream inverts alpha too.
  PSDParser relied on the old behaviour for K; that internal use is R7, now fixed.
- **C6. `FreeImage_Save`/`SaveU` delete the file when the plugin fails** (`Plugin.cpp:483/507`).
  - Main left a 0-byte or partial file behind.
  - The writer matrix found no plugin that returns FALSE after writing a readable file, so no valid output is
    lost that way.
- **C7. Stricter refusals, all of malformed files.** Main produced garbage for these; qpv returns NULL:
  - truncated SGI, PCD and Koala;
  - 2-bpp ICO (main decoded them as 8-bit garbage);
  - XPM rows shorter than the declared width;
  - DDS with a bad magic or a negative width;
  - an unterminated HDR header line.

  Also, `OpenMultiBitmap` on a file with no readable page now returns NULL instead of a 0-page handle.
- **C8. Flags that used to be ignored now act.**
  - GIF honours `FIF_LOAD_NOPIXELS` (main decoded the pixels anyway).
  - `MNG_PLAYBACK` (2) and `WEBP_PLAYBACK` (1) now return a composited 32-bit canvas.
  - QPV's `loadArgs := -1` sets every bit, so it now gets header-only / 32-bit results for GIF/MNG/WebP/APNG.
    That is presumably what it wants.
- **C9. The multi-page cache file is named `doc.tif.<pid>.<rand>.ficache`**, where main used `doc.ficache` next to
  the file. This is the intended fix for the .tif/.tiff collision.
- **C10. Build and deployment.**
  - MSVC toolset is now v142 (VS2017 can no longer build it unmodified).
  - The Release FreeImage.dll imports `VCOMP140.DLL`. QPV ships it.
  - Linux: `libfreeimage.so` needs `libgomp.so.1`, and static-lib users must link with `-fopenmp`.
  - Compiler requirements: libheif needs C++20 and OpenEXR needs C++17.
  - `libfreeimage.so` no longer exports libwebp's 129 `WebP*`/`VP8*` functions (`fadba4c`). A program that took
    libwebp from libfreeimage.so would fail to load.

---

## Checked and not regressed

- **API/ABI.**
  - FIF values 0..36 and every existing flag value are unchanged.
  - No `FreeImage_*` export was removed; 5 were added.
  - The `Plugin` struct is unchanged.
  - The plugin table is purely additive: no extension, MIME type or capability was lost.
  - The shared library links with `--no-undefined`.
- **Sources.** The Latin-1 files are intact. MSVC project file lists match `Makefile.srcs` (differences are
  deliberate: AVX files, win32cond).
- **Decoding** (2,261 files: repo test data, audit seeds/PoCs, scratch samples, 1,500 system images, 487
  PIL-generated variants).
  - Every file that changed outcome was malformed, apart from new-format support (HEIF, AVIF, APNG, MNG
    animation, WebP animation) and the APNG retyping in C2.
  - Pixel changes on real files are codec drift only:
    - DWAA/DWAB EXR: at most 0.006 absolute difference.
    - LibRaw 0.22 in RAW_DISPLAY, RAW_PREVIEW and RAW_HALFSIZE: ≥ 92 dB PSNR.
  - No file crashes or hangs only on qpv. One wallpaper timeout was machine load; it loads in 226 ms on both.
- **Load flags QPV uses** (1,748 files): JPEG_EXIFROTATE, RAW_PREVIEW/RAW_DISPLAY, `-1`, and the multi-page
  playback flags. The only differences are C8 and codec drift. EXIFROTATE turns an orientation-6 320x240 into
  240x320 in both builds.
- **Writing** (9,982 cases per build: 20 formats × 23 pixel types × 7 sizes × flags).
  - No case where main saves and qpv refuses.
  - No case where main reproduces the source and qpv does not.
  - Files cross-load both ways, except where main's reader was broken: BMP RLE8, EXR LC.
  - JPEG PSNR is identical and sizes are within ±10 bytes (libjpeg 10's chroma table); J2K lossy is within 0.13 dB.
  - Lossless WebP is exact wherever alpha > 0.
- **Metadata.** A round trip through 10 formats preserves Exif, IPTC, XMP, ICC, DPI and comments the same way in
  both builds. The only exceptions are the R1 thumbnail and the pre-existing IPTC-in-TIFF garbage listed below.
- **Multi-page flows.** TIFF/GIF/ICO create, append, insert, delete, move, modify, memory load and memory save
  behave the same apart from C3 and C9.
  - qpv's GIF writer now sizes the logical screen to fit all frames; main wrote 32x24 with frames up to 64x40.
  - Single-page formats now lock their page; main returned NULL.
  - `GIF_PLAYBACK` random access matches main exactly for every lock order tried: 0..n, a single deep page,
    backwards, repeats, and 400 → 10 on a 448-frame GIF.
  - QPV's own animation-writing sequence (a `FrameTime` FIDT_LONG tag per page, then append) round-trips the
    same delays, loop and disposal in both builds. qpv's GIF writer sizes the logical screen to fit all frames.
- **Toolkit** (1,005 cases + 24 real-HDR cases per build).
  - All 719 differing keys trace to intentional fixes, apart from R2, R3, R5, C4 and C5.
  - OpenMP runs at 1, 4 and 8 threads are byte-identical (7,635 keys plus 324 HDR keys), so there are no races.
  - `RescaleRect` (12 QPV calls) now equals `Rescale(Copy())` in 110/110 cases; main differed in 24 and crashed
    in 25. The new pixels are the corrected ones.
- **Performance.**
  - PNG, JPEG and TIFF decode times are identical in controlled runs.
  - `GetFileType` costs 0.1 ms on unknown files in both builds.
  - JP2 is 3.7-7x faster, except R6 (now fixed).
- The `PSDParser.cpp:799` thumbnail check once listed here as plausible is R8.
- **Robustness.**
  - Loading from a stream that does not start at byte 0: qpv newly handles RAW, ICO, HEIF and AVIF; nothing got worse.
  - Repeated Initialise/DeInitialise cycles work.
  - Main's JNG leak (~155 B per load) is gone.
  - HEIF/AVIF do not leak. LeakSanitizer is clean for load, thumbnail, header-only, memory and multi-page cycles.
    The heap growth seen earlier is a one-time warm-up: about 0.5 KB for a still image and 5.5 KB for a
    sequence, the same after 30, 150 or 600 cycles.
  - ISOBMFF files that are not images (MP4, MOV, M4A, 3GP, CR3-style) stay FIF_UNKNOWN.

## Pre-existing bugs found along the way (same in both builds, not regressions)

All fixed, one commit each (2026-09-22). Several turned out wider than first recorded.

- **IPTC in TIFF writes heap garbage** (1d9dc2f). libtiff 4 counts RichTIFFIPTC in bytes; FreeImage still used
  libtiff 3's LONG count.
  - Writing stored only a quarter of the IPTC block: exif.jpg kept 5 of 18 tags.
  - Reading parsed up to three buffer-lengths of heap past the tag. That was the "Province-State" garbage.
  - Big-endian TIFFs with IPTC byte-swapped 4x the buffer in place, a heap overflow write, and aborted.
  - Now: byte counts both ways. Adobe's LONG-typed tag in MM files is detected by the reversed 0x1C marker.
- **`ICO_MAKEALPHA` with `biClrUsed=16`** (d8287db). The palette read ignored `biClrUsed`, so pixels and mask were
  read from the wrong place. Colours were wrong too (125 of 128 pixels), not only alpha. Missing mask bytes now
  count as opaque.
- **Writers given input they cannot store.** Each writer below now refuses what its `SupportsExportType` /
  `SupportsExportDepth` do not declare:
  - TGA (007fd32): RLE aborted, and 1/4-bit wrote empty rows.
  - XPM (d123545): 1/4-bit packed bytes were read as indices, and 16/32-bit pixels at a 3-byte stride.
  - PNG (85e8c87): 555/565 read past the last row, and floats and 32-bit integers were half-read.
    **Behaviour change:** FIT_INT16, which used to round-trip bit-exactly as unsigned 16-bit grey, must now be
    converted by the caller.
  - J2K/JP2 (706232c): 1-bit grey and 555/565 read past their rows.
  - ICO (25908ca): 256x256 16-bit icons went through PNG; they are now stored as 24-bit.
- **Loading from a stream offset.**
  - Fixed as listed: SGI (2b91121), TIFF (74aacfe), EXR (1b4dacf), JXR (0eaf4ed) and MNG/JNG (e65017c). TIFF, EXR
    and JXR also *save* correctly at an offset.
  - Found with them:
    - TGA (d0f87e7): 16-bit pixels, the TGA 2.0 footer and thumbnail, and the writer's offsets.
    - PICT (e1ef615): detection and opcode alignment.
    - The multi-page layer (6f43e81): `OpenMultiBitmapFromHandle` / `LoadMultiBitmapFromMemory` rewound to byte 0,
      which broke every multi-page format.
  - With the image at byte 777 of a memory stream, 2,084 corpus files decode identically to a plain load. The two
    left are RAS PoCs (see below). All 61 loadable multi-page samples match page for page.
- **Fattal02 crashes** on any non-flat image under 32 px: fixed by R3's 48558a3.
- **A 913-byte PCX allocates 4.3 GB** (cb0682c). bytes_per_line must now hold the width, and the data must reach
  2/63 of the raster. Found with it:
  - Uncompressed PCX decoded every row after the first wrongly (6845350).
  - Odd-width 16-colour PCX lost its last column (1d77c3d).
  - 1-pixel-wide or -tall PCX, including Pillow's 1x1 files, was refused (3ea9b2e).
- **`audit/crashes/png_PluginPNG.cpp-790.png`** (16adb5f): a double free. libpng longjmps out of `png_read_end`
  (bad IEND CRC) after `row_pointers` was freed, and a non-volatile local held the stale pointer.
- **Deleting a page from Pillow's mixed-mode multi-page TIFF** (daa370c). The real bug: every 8-bit grey or
  palette TIFF without a SamplesPerPixel tag failed to load ("Image is corrupted"), which includes all of
  Pillow's L and P TIFFs; 44 more corpus files now decode. Found with it: 16-bit colormaps written as v << 8
  (Pillow, libtiff tools) came out one level low (8f2ae4b).
- **`audit/poc/psd_unpackrle.psd` from memory** (7b53576). Short RLE lines, truncated count tables and short raw
  rows left heap memory in the image. They now decode as zeros.

Also found while fixing, and fixed:
- **PSD writer.**
  - 16-bit RLE saves overflowed the PackBits buffer on the heap (726d12e).
  - Every RLE save leaked the line-size table, and failed writes leaked buffers and the CMYK copy (4356041).
- **TGA** (8fc4ac5). TGA has no magic number, so a fuzz file (libheif's `github_46_2.heic`) parsed as a
  28777x28786 TGA. It allocated 3.2 GB, and rows past EOF showed uninitialised heap memory. The data must now
  reach one RLE packet per 128 pixels, and rows past EOF are zero.

Found, not fixed (each needs a decision, or is out of scope):
- The BMP, PSD and TIFF writers still accept types they do not declare (BMP non-bitmap types, PSD
  DOUBLE/INT32/UINT32, TIFF 555/565) and write visibly wrong images. This is memory-safe; refusing, like TGA/PNG
  now do, or converting is a policy choice.
- A PNG cut right after its image data (no IEND) is refused, although every pixel was decoded.
- `audit/poc/ras_huge_maplength.ras` and `f08_ras_hugemap.ras` (colour map past EOF) decode as zeros from a file
  but NULL from memory. A seek past EOF succeeds on a file and fails on a memory stream.
- JPEG's `Load` reads `dib` after a longjmp. It works only because `RotateExif(&dib)` keeps it in memory; no
  leak or crash reproduces.
- The TGA writer's thumbnail block swaps colour order on `dib` instead of `thumbnail` on RGB-order builds (not
  x86).
- TestAPI/JXR's `meta` test needs `TestAPI/raw_exif.jpg`, which only `testAPI` creates.

## Not covered

- MSVC builds were not compiled here. The Windows-only paths (LoadU/SaveU `_wremove`, OpenMultiBitmapU, the
  `OutputDebugString` mirror) were read, not run against a main-built DLL.
- No real CR3 file, and few real-world J2K or huge TIFF files, were available.

## Rig (untracked, `.claude/scratch/regress/`)

- **Builds:** `main/` and `qpv/`, from `git archive`. Main needs the GCC 15 flags in `build-main.sh`.
- **Harnesses:**
  - `fidiff.cpp`: decode, plus `dump` mode.
  - `wdiff.cpp`: writer.
  - `flagdiff.cpp`: load flags.
  - `mptest.cpp`: multi-page flows.
  - `resave.cpp`: metadata round trip.
  - `offload.cpp`: stream offset.
  - `leak.cpp`: heap growth.
  - `jp2bench.sh`: tiled JP2 timing.
- **Comparison scripts:** `cmp_decode.py`, `cmp_writer*.py`.
- **Run outputs:** `runs/`.
- **Toolkit differential:** `toolkit/`.
- **Early-commit probes:** `pre2026/`.
- **Second round (the fixes above):**
  - `j2kgrid/`: 94-file thread benchmark.
  - `pcx/`, `psd/`, `pngerr/`, `iptc/`, `tifspp/`, `icoclr/`: hand-built inputs per fix.
  - `rebuild.sh` and `bedit.py`: byte-safe edits of the Latin-1 sources.
  - Uninitialised reads were exposed with `GLIBC_TUNABLES=glibc.malloc.perturb`, and in-plugin overflows with a
    single ASan-compiled TU linked ahead of the archive.
  - Final check: the whole 2,261-file corpus against the post-R5 build. 57 files changed, all as listed above:
    44 more files decode, the PCX bomb, the PNG abort and the TGA fuzz file are now clean refusals, and the ICO,
    PCX and PSD corrections. TestAPI's format suites and `testAPI` pass.
