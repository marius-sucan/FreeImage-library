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
| C1 | `FreeImage_FillBackground` had a 4th parameter: C callers broke, Win32 export `@16`, stale x64 register | **ABI break**, fixed in e96b0fe | 5112d61 | yes: its new-image fill; apply `qpv.patch` |
| C7 | Cut or malformed files that main decoded (as garbage) were refused | **stricter**, fixed in fdf4e87..24b8aa7: every loader but RAW, HEIF and AVIF now keeps what a cut or damaged file holds | various | yes: any damaged or partly downloaded file |
| C2-C6, C8-C10 | Intended behaviour changes that existing callers can notice | needs a decision | various | yes: C4 (tone-mapping look), C4b (16-bit ConvertToType in combine), C8 (`-1` load flag) |

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

- **C1. `FreeImage_FillBackground` gained a 4th parameter** (`5112d61`, `Background.cpp:430`). **Fixed in e96b0fe.**
  - C source that calls it with 3 arguments no longer compiles (`FI_DEFAULT` is empty in C).
  - Win32: there is no `.def` file, so the stdcall export changes from `_FreeImage_FillBackground@12` to `@16`.
    Old 32-bit binaries fail to load.
  - Win64 and SysV: an old binary leaves garbage in the 4th argument register. If it is non-zero, the
    `FI_COLOR_IS_RGBA_COLOR` blend is skipped; if it is 1..255, it also becomes the 32-bit fill alpha.
  - **Fix:** the 3.18 prototype and the `@12` export are back. The option `FI_COLOR_SET_ALPHA` (0x08) replaces
    `applyAlpha`: nothing is blended and a 32-bit image gets `rgbReserved` as its alpha, 0 included.
    `AllocateEx` and `EnlargeCanvas` pass it through. The AHK, Delphi, VB6 and .NET wrappers are back on 3 parameters.
  - Verified: every 3-argument fill is byte-identical to before (26,880 cases); the option equals `applyAlpha=A` for
    A in 1..255 (23,040 cases). FreeImage 3.18 ignores the bit and fills the exact RGB, opaque (also on its real
    x86/x64 DLLs under Wine). Among the 93 objects that include `FreeImage.h`, only `Background.o` changes.
  - **QPV:** apply `qpv.patch` (repo root): its wrapper copy and the new-image fill (`…, 8` instead of `…, 1, -1`).
    The DLL and the script must ship together. At 0% opacity a new image now gets the chosen RGB, where it stayed
    black before.
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
- **C7. Stricter refusals, all of malformed files.** **Fixed in fdf4e87..24b8aa7.** Main produced garbage for
  these; qpv returned NULL:
  - truncated SGI, PCD and Koala;
  - 2-bpp ICO (main decoded them as 8-bit garbage);
  - XPM rows shorter than the declared width;
  - DDS with a bad magic or a negative width;
  - an unterminated HDR header line.

  Also, `OpenMultiBitmap` on a file with no readable page now returns NULL instead of a 0-page handle.

  Every loader but RAW, HEIF and AVIF now keeps what a cut or damaged file holds, as the PNG loader does since
  f61c4bf:
  - The rows, blocks of lines or frames decoded before the cut or the damage load, and the rest is zero. A warning
    says what was kept ("N of M rows decoded, the rest is blank", or "… interlace passes complete …"); DebugView
    gets it on Windows.
  - NULL only when no row could be decoded. Nothing that earlier qpv loaded is refused. A file of which main
    showed only garbage, with not one real row, stays refused. Pixels change for a file earlier qpv loaded in one
    case: a cut RAS image. Past the cut, earlier builds repeated the last byte read (RLE) or left uninitialised
    heap bytes (raw, so `LockPage` could disagree with `Load`); that part is now zero.
  - A file whose header claims far more than its bytes can hold is still refused (`PlausibleImageSize`: never
    below 64 MB, above that each format's best-case expansion), so a 100-byte file cannot allocate gigabytes.
  - SGI and PSD store alpha as a plane after the colours, so a file cut before it gets opaque alpha and the
    decoded colours show. A JNG cut inside its alpha data drops the alpha for the same reason.
  - A cut interlaced GIF fills its missing rows from the passes decoded, like an Adam7 PNG.
  - Only FreeImage's own sources changed (`Source/FreeImage/`, `Source/Utilities.h`; `TestAPI/APNG/robust.c`
    for two expectations). No bundled library was touched: OpenJPEG's strict mode is switched off through its
    API, and WebP uses libwebp's incremental decoder.

  The C7 cases: SGI (a639a01; each RLE row keeps its own offset), PCD (b79bb14), Koala (c7da09a; loads once its
  colour data starts), ICO (3758910; 2-bit icons load as 4-bit with their 4 colours), XPM (1ad8ada; a short row
  keeps its pixels), DDS (f33e02a; a wrong magic loads when both header sizes are right, and wrong sizes when the
  magic is), HDR (3388793; a long header line keeps its start). Two stay as they are:
  - A DDS width of 2^31 or more (the "negative" one): the field is a DWORD, and no such image has pixels to show.
  - `OpenMultiBitmap` on a file with no readable page returns NULL: there is nothing to keep.

  The same now holds for BMP (≤ 8-bit and RLE), CUT, EXR (block by block), GIF, IFF, J2K/JP2, JNG, MNG, APNG,
  PCX, PFM, PNM, PSD, RAS, TGA, TIFF (every load path), WBMP, WebP and XBM. Main refused most of these too.
  Not changed:
  - JPEG, JXR and G3 already returned what they decoded.
  - RAW, HEIF and AVIF: LibRaw throws at the end of the data, and libheif and libavif decode whole items with
    codecs that give all or nothing. Salvaging them would mean changing those libraries.
  - A cut animated WebP keeps its whole frames only, because libwebp composites whole frames.
  - A frame that the file ends inside and that decodes no row is still a page, and fails to lock (APNG, GIF,
    MNG); a single-frame file like that is refused.

  Found with it:
  - Interlaced GIF frames 2-4 rows tall lost every row but the first (1e3db05; main too).
  - LibJXR reads 4 bytes past `gSignificantRunBin` (`segdec.c:352`, `DecodeSignificantRun`) on every cut or
    damaged JXR copy tried, 23 of 23 under ASan; the intact files are clean. It is in the bundled library, same
    in main and qpv, and not fixed here.
  - The size bound stops a huge, mostly blank image from being *returned*, but most loaders allocate before
    they read the data. The 114-byte `audit/crashes/bmp_da39a3ee5e6b.bmp` (17 x 2^31 rows) allocated and zeroed
    8 GB for 9-17 s before it was refused, in main too. **Fixed in 0fe40ec** for BMP:
    - A raster over 64 MB is checked against the pixel data the file holds before anything is allocated (one
      seek to the end, only for such rasters). An RLE stream that ends with its end-of-bitmap code still loads
      however sparse it is.
    - The rule now covers 16-, 24- and 32-bit BMPs too: one over 64 MB holding less than 1/64 of its pixels is
      refused (a 75 MB 24-bit image cut to 1% loaded 49 rows; cut to 2% it loads).
    - 11 crafted files over every BMP path now take 0.01 s and 7 MB instead of 4-9 s and 3.5-8.8 GB.
    - The other loaders still allocate first.

  Verified:
  - 3,299 cut and damaged copies of 41 kinds of file (cut at 5-95% and one byte short, 16 bytes overwritten at
    40% and 70%): qpv before the fix (9add96c) returned an image for 1,517, and now does for 2,372. None that
    loaded before is refused. In 10, 9add96c matched the intact file in a few more rows. In each case it kept
    stale buffer bytes or repeated the last value where the file ended, and on these smooth test images that
    happened to be right. Those pixels are now blank.
  - The 2,341-file intact corpus decodes as before, apart from 18 audit PoCs: 8 C7 files now load, 7 only gained
    the warning, and the 3 RAS files above.
  - Speed: 38 images of 3000x2000, in every format whose loader changed, decode in the same time (4,370 vs
    4,383 ms in total). JP2 varied by ±10% between runs, and J2K with the same codestream was faster. The
    bounds add no seek to the end of the file (SGI's is in its cut branch). Seven loaders (BMP RLE, CUT, HDR,
    PFM, PNM, XBM, XPM) read their position once at the start of the pixel data, as PNG does (under a
    microsecond). GIF counts the bytes it reads rather than asking per frame, and BMP RLE8 reads its position a
    second time only for a short image.
  - ASan: all 5,784 cut, damaged and intact files, except the 8 GB BMP above, which the OOM killer stopped.
    Nothing in FreeImage's code; the LibJXR over-read above.
    The only failed allocation is `audit/poc/xpm_nulldib.xpm` asking for 2e9 x 2e9 pixels: the request fails
    at once and the file is refused, as before.
  - Every TestAPI suite passes: APNG, MNG, WebP, J2K, JPEG, JXR, EXR, RAW, HEIF, AVIF and testAPI. The WebP
    robustness test decodes 4,400 of its 6,566 damaged inputs (9add96c: 2,094), and the six crafted bombs are
    refused.
  - With `glibc.malloc.perturb=165` every cut and damaged copy decodes exactly as without it, so no blank part
    holds uninitialised memory.
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

All fixed, one commit each (2026-09-22; the BMP, PSD and TIFF writers and the PNG loader on 2026-09-23).
Several turned out wider than first recorded.

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
  - BMP (63bbdda): non-bitmap types became 16- to 128-bit BMPs; UINT16/INT16 got all-zero colour masks.
  - PSD (f0d5bc7): UINT32, INT32, DOUBLE and COMPLEX became single-channel RGB files that FreeImage cannot
    load; 4- and 16-bit bitmaps were already refused, but silently. The declared FIT_FLOAT had the same layout
    and did not reload either; it is now 32-bit grayscale (080c325), which psd-tools reads with the same values.
  - TIFF (23cc6cf): 555/565 bitmaps got their tags but no image data. A thumbnail the writer cannot store is
    now skipped with the thumbnail warning, and the image is still saved.
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
- **A cut or damaged PNG was refused**, however much of it had been decoded.
  - Damaged after its image data (83061ac): no IEND, a cut or bad-CRC IEND, garbage or a cut chunk after the last
    IDAT, a missing last IDAT CRC. All 6,224 such copies of 778 PNGs now load pixel-identical to the intact file,
    `png_PluginPNG.cpp-790.png` included.
  - Cut inside its image data (f61c4bf): the rows decoded so far load and the rest is zero; an interlaced image
    fills the missing pixels from coarser Adam7 passes. libpng drops a read piece of up to 8 KiB that the stream
    cuts short, so the PNG is read once more with that IDAT shortened to the bytes present. All 8,112 cuts match
    an oracle that inflates the bytes present with libpng's row loop. Decoded bytes zlib still holds when the
    input runs out stay lost (at most 1,025 here).
  - The error is still reported, then a warning says what was kept; DebugView gets both on Windows.
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
- **BMP RLE saves wrote uninitialised heap bytes** (0bef08b). The pad byte after an odd-length absolute run was
  skipped instead of written, so the same image saved twice gave different files.
- **Saving a multi-bitmap opened with `FIF_LOAD_NOPIXELS`** (8bff0ee). The pages reached the writers without
  pixels: PNG, PSD and TIFF crashed and TGA wrote an empty image, so deleting a page from a TIFF opened
  header-only crashed in `CloseMultiBitmap`. The pages are now reloaded with pixels.
- **TestAPI's ASan runs used the uninstrumented code** (f324a79). `asan-lib` added the ASan objects to a copy of
  the library under new names, and the linker took the originals: APNG, EXR, JPEG, MNG and RAW ran no
  instrumented plugin or decoder code, WebP some. AVIF and HEIF named their decoder objects `.._.._Source_...`,
  which `asan-obj/*.o` skips, so only their plugin was instrumented. The objects are now linked ahead of the
  unchanged library, restricted to `Makefile.srcs` sources and built with `Makefile.gnu`'s `-D` flags. The AVIF
  suite had lost `AVIF_ENABLE_EXPERIMENTAL_MINI`, which showed once libavif ran instrumented. All eight
  `asan-run` pass with no report and the same output as `run`.

- **BMPs with a V2 to V5 or OS/2 2.x header did not load** (4fe0c6f), in main too: `CheckBitmapInfoHeader`
  (upstream r1836) accepted only the 40-byte header, although the loaders handle the larger ones. Among them
  are 32-bit BMPs with alpha, such as `/usr/share/pixmaps/debian-logo.bmp`, which now matches Pillow. The
  reopened paths had bugs of their own, fixed with it:
  - 16/24/32-bit `BI_RGB` files took the masks in a larger header, which count only with `BI_BITFIELDS`.
  - OS/2 2.x read its palette at an absolute position (wrong for a file not at byte 0) and skipped a padded one.
  - A cut OS/2 palette, 1.x too, repeated its last entry or showed stack bytes; the rest now stays grey.
  - 88 crafted files over every header size, bit depth and compression decode identically to their 40-byte
    twins and match Pillow. They load the same from a stream offset and header-only, and are clean under ASan.

Found, not fixed (each needs a decision, or is out of scope):
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
- **Third round (`refuse/`):**
  - `probe.cpp`: every type per writer, header-only multi-page saves, TIFF thumbnails, mixed multi-page TIFF.
  - `wdiff` with `cmp_w.py`: supported cases write the same bytes, unsupported ones are refused.
  - `gen_png` + `gen_trunc.py` + `pngload` + `cmp_png.py`: 10,747 damaged copies of 778 PNGs; `pngasan` loads
    them all in one ASan/LSan process, linked with `TestAPI/MNG/asan-obj/*.o` ahead of `Dist/libfreeimage.a`.
  - `partial/`: `gen_cuts.py` cuts inside the image data, `zoracle` gives libpng's stopping point, `partcheck`
    compares pixel by pixel, `offpng` loads at a stream offset, `view` draws a contact sheet.
  - `psdfloat` + `psdcheck.py`: psd-tools (installed in `refuse/pylib`) as an independent PSD reader.
