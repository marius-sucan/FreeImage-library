# qpv vs main: regression review (2026-09-22)

**Compared:** `origin/main` = `5af83c4` (plus 2 README-only commits) against `qpv` = `c67d625` (265 commits).
**Question:** what worked on main, for a valid input or an existing caller, and is broken or worse on qpv?
Fixed crashes on malformed files are not regressions. A stricter refusal of a *valid* file would be.

## Verdict

| # | Finding | Kind | Commit | Affects QPV? |
|---|---|---|---|---|
| R1 | Exif thumbnails are never loaded (`FreeImage_GetThumbnail` returns NULL) | **regression** | 6e64300 | no (QPV never calls GetThumbnail), but re-saved files lose their thumbnail |
| R2 | Fattal02 tone mapping darkens/shifts ordinary images and adds black speckles | **regression** | 9b55043 | no (QPV uses Drago03/Reinhard05 only) |
| R3 | Fattal02 segfaults on a flat image smaller than 32 px (main returned NULL) | **regression** (crash) | 9b55043 | no |
| R4 | `CloseMultiBitmap` returns FALSE for an *untouched* read-write session of a writer-less format | **regression** (API result, deliberate) | c1442f5 | no (QPV opens read-only) |
| R5 | `MakeThumbnail(hdr, N, TRUE)` ignores `convert` when the larger side is exactly N | **regression** (contract) | 3f0e1dc | no (QPV never calls it) |
| R6 | Tiled JPEG 2000 with small tiles decodes up to 1.6x (64 px tiles) / 6x (16 px tiles) slower | **performance** | 03c89f4 | yes, for such files |
| C1-C10 | Intended behaviour changes that existing callers can notice | needs a decision | various | yes: C4 (tone-mapping look), C4b (16-bit ConvertToType in combine), C8 (`-1` load flag) |

Nothing else in the differential runs came out worse on qpv for a valid input.

---

## Confirmed regressions

### R1. Exif thumbnails never load
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

### R2. Fattal02 shifts ordinary images and adds black speckles
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

### R3. Fattal02 segfaults on a small flat image
- **Repro:** `FreeImage_TmoFattal02()` on a 13x7 all-black FIT_RGBF image returns NULL on main and segfaults
  (exit 139) on qpv. The same happens at 31x31; at 40x40 qpv returns an image.
- **Cause:**
  - `nlevels` stays 0 below 32 px (`tmoFattal02.cpp:504`), so `PhiMatrix()` (`:243`) returns data from `malloc(0)`.
  - On main, a flat image threw before reaching that code. On qpv it no longer throws, so it gets there.
  - Non-flat images under 32 px crash in *both* builds; that part is an upstream bug.
- **Suggested fix:** return NULL when `nlevels == 0`.

### R4. `CloseMultiBitmap` fails an untouched read-write session
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

### R5. `MakeThumbnail(hdr, N, TRUE)` ignores `convert` at exactly N
- **Where:** `Source/FreeImageToolkit/Rescale.cpp:206`. `3f0e1dc` changed `<` to `<=`, so an image whose larger
  side equals `max_pixel_size` takes the "return a clone" shortcut.
- **Evidence:** 100x50 source, max 100, `convert=TRUE`:
  - main returns 8-, 24- or 32-bit;
  - qpv returns the unconverted FIT_UINT16, RGB16, RGBA16, FLOAT, RGBF or RGBAF.
  - At 99 and 101 the builds agree. Main already skipped conversion for images *smaller* than max, which is an
    upstream inconsistency.
- For FIT_BITMAP input the change is an improvement: an exact clone instead of an identity resample.
- **Suggested fix:** apply `convert` in the clone path too.

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
  - JP2 is 3.7-7x faster, except R6.
- **Plausible, not reproduced:** `PSDParser.cpp:799` (`6e64300` + `4b82cff`). The thumbnail `WidthBytes` check
  throws and aborts the whole PSD load, even for JPEG thumbnails, which never use WidthBytes. Only spec-violating
  files can hit it.
- **Robustness.**
  - Loading from a stream that does not start at byte 0: qpv newly handles RAW, ICO, HEIF and AVIF; nothing got worse.
  - Repeated Initialise/DeInitialise cycles work.
  - Main's JNG leak (~155 B per load) is gone.
  - The HEIF path (new, so not a regression) shows possible growth of ~15-25 B per test cycle, i.e. a few bytes per
    decode (30/150/600 cycles). AVIF growth is a constant warm-up.
  - ISOBMFF files that are not images (MP4, MOV, M4A, 3GP, CR3-style) stay FIF_UNKNOWN.

## Pre-existing bugs found along the way (same in both builds, not regressions)

- **IPTC in TIFF writes heap garbage.** After a TIFF round trip, a 1-byte IPTC `Province-State` comes back as
  88-100 bytes of heap memory, different on every run. Heap contents end up in saved files.
- **`ICO_MAKEALPHA` on an 8-bit icon with `biClrUsed=16`** produces random alpha. PIL reads the same icon as
  fully opaque.
- **Writers given a bit depth they do not support** read past the image buffer:
  - TGA RLE aborts;
  - XPM indexes a 2-entry palette with packed bytes;
  - PNG given 555/565 input reads past its last row.
- **Loading from a stream offset:** JXR, EXR, TIFF and MNG/JNG fail, and SGI decodes wrongly.
- **Fattal02 crashes** on any non-flat image under 32 px (upstream `PhiMatrix`).
- **A 913-byte PCX** makes both builds allocate 4.3 GB.
- **`audit/crashes/png_PluginPNG.cpp-790.png`** aborts both builds.

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
