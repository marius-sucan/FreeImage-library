# Deep Audit Report: Bugs and Regressions in the `qpv` Branch (vs `origin/main`)

**Repository**: `FreeImage-library`  
**Compared Branches**: `origin/main` (`4cccd9aa7`) $\rightarrow$ `qpv` (`d497fb956`, 616 commits ahead)  
**Scope**: Full line-by-line audit of `Source/FreeImage/*`, `Source/FreeImageToolkit/*`, `Source/Metadata/*`, `Wrapper/*`, and build/project configurations.

> **Verified 2026-10-18 against `qpv` at `f5081b1`.** This report was written by another AI model and has been checked claim by claim: see [Verification results](#verification-results) below. Each finding now opens with its own **Verification** block; the original text under it is unchanged. The commit hashes above do not exist in this repository: the audited code is `d521037`, 616 commits ahead of `origin/main` (`a976f39`). The **Fixed** columns of the two tables there say what has been fixed since.

---

## Verification results

Checked on 2026-10-18 at `qpv` `f5081b1`, which has five code commits more than the audited `d521037` (a9262be, e461fc8, ce201d8, 59ea9d5, f5081b1). Each claim was read against the code at HEAD and in `origin/main` (found by function name, since the report's line numbers drift), and most were reproduced with small programs linked against a fresh build of HEAD: crafted files, AddressSanitizer on the file under test, fault injection (a failing `operator new`, `RLIMIT_FSIZE`, `--wrap`) and A/B runs against `main`'s code. The rigs and crafted files are in `.claude/scratch/qpv-report-verify/` (untracked; see its README.txt).

**Bottom line**
- Of the 44 findings (the report says 46), **11 hold as described, 24 hold in part and 9 do not hold at all**.
- No finding is Critical. One is **High**: PSD-01, two heap overflows from crafted PSD/PSB files, both inherited from `origin/main`. 8 are Medium, 26 Low and 9 None; the report had 3 Critical, 29 High and 12 Medium.
- Few of the confirmed parts are regressions caused by `qpv`: CORE-02, two parts of TMO-01, PNG-01 (1), HDR-01 (2), the Exif profile of JPEG-01 (2) and BUILD-01 (3). Most of the rest is upstream behaviour that `main` shares, or bugs in code `main` does not have.
- Checking the report turned up a **High regression it missed**: a 55-byte JNG made `FreeImage_Load` loop forever (2decc36), fixed since by e743010.
- Fixed since the report was written: JXR-01 (6) by ce201d8, then on 2026-10-18 both heap overflows of PSD-01 (2ec7065, bb2a0a8) and the JNG hang (e743010), and on 2026-10-19 every Medium finding: CORE-02 and CORE-03 (20d1ea9), CORE-06 (f89db21), CMS-01 (47e4902), TMO-01 (5cf28ba), PNM-01 (635a3c4), PLUG-01 (80b1503, d590b03, 4f20f92) and BUILD-01 (042b6f7), with two of the multi-page leaks and the DXT decode the report missed. Every other confirmed part is still open.

### Verdict per finding

**Verdict**: Confirmed = the bug is real as described (details may differ); Partly = some parts are real, others are not; Not a bug = no part holds. **Regression**: Yes = `qpv` broke what worked in `main`; No = the same in `main`; New code = a bug in code `main` does not have. **Fixed**: the commits that fixed it after the verification; No = still open; empty = nothing to fix.

| ID | Reported | Verdict | Regression? | Verified severity | Fixed | In short |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| CORE-01 | Critical | Partly | No | Low | No | A refused save deletes an existing file; `main` truncates it to 0 bytes, so no content is lost that `main` kept. |
| CORE-02 | Critical | **Confirmed** | **Yes** | Medium | **Yes** (20d1ea9) | Crash in `CacheFile::readFile` after one failed spool write (reproduced). |
| CORE-03 | High | **Confirmed** | No (`main` let `bad_alloc` escape) | Medium | **Yes** (20d1ea9) | Out of memory wedges the page cache until close; small leaks. |
| CORE-04 | High | Partly | No | Low | No | Windows only, and the spool keeps the new content. |
| CORE-05 | High | Not a bug | | None | | Cached pages come back as stored, by design. |
| CORE-06 | High | **Confirmed** | No | Medium | **Yes** (f89db21) | `DeletePage(0)` writes a GIF with no colour table at all. |
| CORE-07 | High | Partly | No | Low | No | `GetLine` wraps for rows of 512 MiB and more; upstream code. |
| CORE-08 | High | Not a bug | | None | | Zero-size dibs are refused on every path. |
| CORE-09 | Medium | Not a bug | | None | | Harmless; a NULL stream returns NULL. |
| CORE-10 | Medium | Partly | Deliberate | Low | No | The INT64 callbacks are a documented choice; the wrapper does not leak. |
| CMS-01 | Critical | **Confirmed** | New code | Medium | **Yes** (47e4902) | Device CMYK of a 16-bit min-is-white image is inverted; no memory corruption. |
| CMS-02 | High | Not a bug | | None | | Deliberate and documented; no inversion. |
| CMS-03 | High | Partly | New code | Low | No | Float frames of JPEG XL animations skip the profile conversion. |
| CMS-04 | High | Partly | No | Low | No | Rescale drops the profile of 16-bit min-is-white images. |
| CMS-05 | Medium | Not a bug | | None | | Upstream; the tag only duplicates the kept profile. |
| CMS-06 | High | **Confirmed** | New code | Low | No | `ConvertToLinear` inverts 16-bit min-is-white images. |
| CMS-07 | High | **Confirmed** | New code | Low | No | 8-bit PQ images get `FITM_NONE`. |
| CMS-08 | High | Partly | New code | Low | No | 4 of 6 profile corner cases are real. |
| TMO-01 | High | Partly | **Yes** (2 of 4 parts, 9b55043) | Medium | **Yes** (5cf28ba) | Black + one colour turns black or white; m > 1 inverts the tones. |
| TK-01 | High | Partly | No | Low | No | No over-read; 1-bit rotation replaces the palette. |
| TIFF-01 | High | Partly | No | Low | No | Multi-page thumbnails are mis-tagged but still reload. |
| TIFF-02 | High | Not a bug | | None | | libtiff refuses RowsPerStrip 0; no overflow. |
| PSD-01 | High | Partly | No | **High** | **Yes** (2ec7065, bb2a0a8) | Heap overflow WRITE (negative height) and READ (RLE table size wraps). |
| PSD-02 | Medium | Partly | No | Low | No | 6-byte over-read on a short Exif tag made by the caller. |
| BMP-01 | High | Partly | No (part 1 by design) | Low | No | Only the cut-file 0xFF case is wrong. |
| BMP-02 | High | Partly | Mixed | Low | No | RLE8 delta past the row end; OS/2 64-byte header. |
| PNG-01 | Medium | Partly | **Yes** (c637684) | Low | No | A cICP the loader cannot read silences gAMA. |
| APNG-01 | High | Partly | New code | Low | No | Grey-profiled frames are composited unconverted; multi-frame saves drop text. |
| MNG-01 | High | Partly | Mostly no | Low | No | JNG min-is-white colour type; header-only bpp of JNG with alpha. |
| MNG-02 | High | Partly | New code | Low | No | Salvage gap in a cut CRC; the writer forces BACK mandatory; a missing IEND swallows a frame. |
| GIF-01 | High | Partly | No | Low | No | Interlaced saves of 2 to 4 rows drop rows. |
| WEBP-01 | High | Partly | No | Low | No | A CMYK still save is written as BGRA. |
| JXR-01 | High | Not a bug | | None | Part 6 (ce201d8) | Misquoted; part 6 was fixed by ce201d8. |
| PNM-01 | High | **Confirmed** | No | Medium | **Yes** (635a3c4) | A 19-byte header gives a 2.6 GB blank image. |
| HDR-01 | High | Partly | **Yes** (part 2) | Low | No | Out of memory mid-file returns a partial image. |
| EXR-01 | Medium | **Confirmed** | No | Low | No | Narrow uncompressed files cut early are refused. |
| RAW-01 | High | Not a bug | | None | | No call site reads past the buffer. |
| AVIF-01 | Medium | Partly | New code | Low | No | Out of memory returns the untransformed image. |
| HEIF-01 | Medium | **Confirmed** | New code | Low | No | Sequences ignore their nclx colour description. |
| JPEG-01 | Medium | Partly | Mostly no | Low | No | Leaks on a failed save; greyscale loads keep RGB profiles. |
| J2K-01 | Medium | Not a bug | | None | | OpenJPEG already clamps every sample. |
| TGA-01 | High | **Confirmed** | No | Low | No | Only on non-default (RGB-order or big-endian) builds. |
| PLUG-01 | Medium | Partly | Mostly no | Medium | **Yes** (80b1503, d590b03, 4f20f92) | Unbounded allocations (RAS, IFF, WBMP, DXT); DXT decode; XBM X10 count. |
| BUILD-01 | Medium | **Confirmed** | Yes (part 3) | Medium | **Yes** (042b6f7) | MinGW build lacks the AVIF `mif3` flag; MSVC test project lacks a file. |

### Bugs the report missed

Found while checking the findings above.

| Where | Bug | Regression? | Severity | Fixed |
| :--- | :--- | :--- | :--- | :--- |
| `MNGHelper.cpp` `mng_ReadChunks` | A JNG (or an MNG holding one) whose alpha `IDAT` comes before the first `JDAT` and which ends at a chunk boundary makes `FreeImage_Load` loop forever. With no chunk header left and no JPEG data yet, the loop continues with the stale chunk name, and a stale `IDAT` is exempt from the bad-CRC throw. Reproduced with a 55-byte file, also with `FIF_LOAD_NOPIXELS` and through `FIF_MNG`. | Yes (2decc36) | High | **Yes** (e743010) |
| `MultiPage.cpp` | Out of memory: `FreeImage_SavePageToBlock` leaks its `FIMEMORY` when `writeFile` throws (about 512 KB for a 196 KB page), `FreeImage_LoadPageFromCache` leaks `compressed_data` when `readFile` throws, and a throw from `m_blocks.push_back` / `insert` after a page is stored leaves its blocks unreferenced until close. `RememberPageMetadata` silently drops the animation tags when its allocations fail. | New code (`main` had no `catch`) | Low | Partly (20d1ea9: the `FIMEMORY` and `compressed_data` leaks) |
| `PluginJXL.cpp` `AddFrame` | The animation writer converts a frame to the animation layout (`ConvertTo*`, which drops the profile) before `ConvertToFileProfile`, so a tagged frame not already in that layout is recoloured from the default space: an Adobe RGB 0.5 RGBF frame is written as 0.5001 linear instead of about 0.2178. Tested for RGBF only. | New code | Low | No |
| `PluginJPEG.cpp` `Save` | A failing output write longjmps out of `jpeg_write_scanlines` past `free(target)`: about 3 KB leaked per failed save. | No (same in `main`) | Low | No |
| `PluginDDS.cpp` `LoadDXT` | Widths that are not a multiple of 4 decode wrong: one block per block row is skipped, so the rows shift. | No (same in `main`) | Low | **Yes** (d590b03) |

### Errors in the report itself

- The commit hashes `4cccd9aa7` and `d497fb956` do not exist; the audited code is `d521037`. The summary table lists 44 findings, not 46.
- Quoted code that exists in no revision (HEAD, `d521037` or `main`): `bool CacheFile::cleanupMemCache()` with `if (!cleanupMemCache()) return NULL;` (CORE-02), the `HasPixels` form of `FreeImage_GetLine` (CORE-07), both checks of CORE-08, the 1-bit guard of TK-01, the `th_bpp` condition of TGA-01, `CopyICCProfile` (CMS-04), `BuildTables` / `m_tables` (CMS-06), `nBytes += oResource._Size` (PSD-01), `if (y == 0) throw` (HDR-01), the `IsWindowed` call (JXR-01) and `gets` (RAW-01). Functions named that do not exist: `SetDeviceOut` and `Combine` (CMS-01), `ValidateBitfields` (BMP-02), `SynthesizeExifProfile` (JPEG-01), `LoadStill` (HEIF-01).
- Labelled regressions although `main` behaves the same: CORE-01, CORE-04, CORE-06, CMS-04, CMS-05, TK-01, TIFF-01, PSD-02, GIF-01, and most of BMP-02 and JPEG-01. CORE-07's text blames a `qpv` change to `CalculateLine` that never happened. Three findings labelled regressions are not bugs at all: CORE-08, CORE-09 and JXR-01.
- Impacts claimed that do not occur: memory corruption (CMS-01, TK-01), infinite loops (TIFF-02, PSD-01 part 1), an unreadable file (BMP-02 part 1), a thumbnail ignored on reload (TIFF-01), metadata missing from header-only loads (AVIF-01).

---

## Executive Summary

The `qpv` branch introduces major architectural additions and rewrites over `origin/main`, including:
- 64-bit file and memory I/O (`FreeImageIO`, `MemoryIO`, `CacheFile`, `MultiPage`)
- Little CMS 2 color management (`ColorManagement.cpp`, `FIF_LOAD_DISPLAY_ICC`, CICP, soft proofing, CMYK separation, `TwinCache`)
- Linear-light conversions (`ConversionLinear.cpp`, `MustTonemap.cpp`) and SIMD-accelerated rescaling (`Resize.cpp`, `Rescale.cpp`)
- New and rewritten format plugins (`PluginJXL`, `PluginHEIF`, `PluginAVIF`, `PluginAPNG`, `PluginMNG`/`MNGHelper`, `PluginWebP`, `PluginTIFF`, `PSDParser`, `PluginBMP`, `PluginGIF`, `PluginJXR`, `PluginEXR`, `PluginRAW`)
- Partial-image recovery on truncated streams (`PartialImageWarning`, `PlausibleImageSize`)
- Dynamic bitmap views (`FreeImage_CreateView`) and out-of-line CMYK alpha planes (`FreeImage_GetCMYKAlpha`)

Across these changes, **46 verified bugs and regressions** (several encompassing multiple closely related sub-bugs) were identified. The table below summarizes all findings by severity and subsystem.

> *Verification: the summary table lists 44 findings, not 46. 11 hold as described, 24 in part and 9 not at all, and none is Critical; see [Verification results](#verification-results).*

---

## Summary Table of Findings (as reported, before verification)

| ID | Severity | Category | File(s) | Summary |
| :--- | :--- | :--- | :--- | :--- |
| **CORE-01** | **Critical** | Regression (Data Loss) | `Source/FreeImage/Plugin.cpp` | `FreeImage_Save` / `FreeImage_SaveU` truncates and deletes existing target files when called with invalid arguments (`dib == NULL`, header-only DIB, `FIF_UNKNOWN`, or read-only format) |
| **CORE-02** | **Critical** | Regression (Crash / NULL deref) | `Source/FreeImage/CacheFile.cpp` | `CacheFile::lockBlock` evicts and zeroes `m_current_block->data` when a prior `cleanupMemCache()` flush failed, causing an immediate `NULL` dereference in `readFile()` |
| **CORE-03** | **High** | Bug / State Corruption | `Source/FreeImage/CacheFile.cpp`, `MultiPage.cpp` | Unhandled `std::bad_alloc` inside `CacheFile::writeFile`, `lockBlock`, `allocateBlock`, and `FreeImage_SavePageToBlock` permanently wedges `CacheFile` (`m_current_block != NULL`) and leaks blocks |
| **CORE-04** | **High** | Regression (Data Loss on Windows) | `Source/FreeImage/MultiPage.cpp` | `FreeImage_CloseMultiBitmap` deletes the destination file before `renameFile`, destroying the original file if rename fails and leaking `.fispool` on Windows |
| **CORE-05** | **High** | Bug / Behavioral Mismatch | `Source/FreeImage/MultiPage.cpp` | Cached multipage blocks (`BLOCK_REFERENCE`) ignore `header->load_flags` (`FIF_LOAD_NOPIXELS`, `FIF_LOAD_DISPLAY_ICC`, `*_CMYK`), returning incompatible color spaces/bitmaps vs uncached pages |
| **CORE-06** | **High** | Regression (Color Corruption) | `Source/FreeImage/PluginGIF.cpp`, `MultiPage.cpp` | Calling `FreeImage_UnlockPage(bitmap, dib, TRUE)` on GIF frame `page > 0` or `FreeImage_DeletePage(bitmap, 0)` loses `"GlobalPalette"` and turns frames into a 256-level greyscale ramp |
| **CORE-07** | **High** | Bug (Integer Overflow / Heap Corruption) | `Source/FreeImage/BitmapAccess.cpp` | `FreeImage_GetLine` uses 32-bit `unsigned` math (`width * bpp + 7`) while `CalculateLine` uses 64-bit `unsigned long long`, overflowing on wide images and corrupting `FreeImage_GetPitch` / `FreeImage_GetScanLine` |
| **CORE-08** | **High** | Regression (`SIGFPE` Crash) | `Source/FreeImage/BitmapAccess.cpp`, `CopyPaste.cpp` | `FreeImage_CreateView` (`left == right` or `top == bottom`) and `FreeImage_AllocateHeaderForBits` allow 0-width/0-height bitmaps with `FreeImage_HasPixels() == TRUE`, causing divide-by-zero (`SIGFPE`) in `FreeImage_Copy` and `FreeImage_Paste` |
| **CORE-09** | **Medium** | Regression / Crash | `Source/FreeImage/FreeImageIO.cpp`, `MultiPage.cpp` | `_MemoryWriteProc` lacks a `size == 0 || count == 0` guard (extending `file_length` on 0-byte writes); `FreeImage_LoadMultiBitmapFromMemory` crashes on `stream == NULL` |
| **CORE-10** | **Medium** | Breaking ABI / Memory Leak | `Source/FreeImage.h`, `fipMultiPage.cpp` | `FI_SeekProc` and `FI_TellProc` changed `long` to `INT64` (breaking ABI on 32-bit and 64-bit Windows LLP64); `fipMultiPage::unlockPage` ignores `FreeImage_UnlockPage` failure and leaks `image._dib` |
| **CMS-01** | **Critical** | Bug (Memory Corruption / Inversion) | `Source/FreeImageToolkit/ColorManagement.cpp` | `ColorConverter::CreateTransform` skips creating `m_transform` when `m_device_out && in.Same(out)` even though `m_in_format != m_out_format`, reinterpreting `float*` pixels as `WORD*` in `FreeImage_ConvertToCMYK` and inverting `FIT_UINT16` `FIC_MINISWHITE` |
| **CMS-02** | **High** | Bug (Color / Palette Corruption) | `Source/FreeImageToolkit/ColorManagement.cpp` | `ColorConverter::SetSource` overwrites `m_px.model = MODEL_RGB` on 8-bit greyscale images carrying an RGB ICC profile, breaking `DefaultModel()`, corrupting `FIC_MINISBLACK`/`FIC_MINISWHITE` into `FIC_PALETTE` in `FreeImage_ApplyICCProfile`, and misrouting `ToDisplay` |
| **CMS-03** | **High** | Bug (Skipped Gamma / Model Conversion) | `Source/FreeImageToolkit/ColorManagement.cpp` | `ConvertToFileProfile` short-circuits via `TwinCache::IsTwin` (sRGB/Gray 2.2) without checking if `dib` is floating-point (default Linear sRGB/Gray 1.0) or if source/destination color models (`MODEL_RGB` vs `MODEL_GRAY`) differ |
| **CMS-04** | **High** | Regression (Dropped ICC Profile) | `Source/Utilities.h`, `Rescale.cpp`, `Conversion*.cpp` | `CopyICCProfile` compares `FreeImage_GetColorType(dst) == FreeImage_GetColorType(src)` before `FreeImage_CloneMetadata(dst, src)` copies `"PhotometricInterpretation"`, silently dropping ICC profiles on `FIT_UINT16` `FIC_MINISWHITE` images |
| **CMS-05** | **Medium** | Regression (Lost Metadata Tag) | `CopyPaste.cpp`, `Background.cpp`, `ClassicRotate.cpp` | `FreeImage_Copy`, `FreeImage_EnlargeCanvas`, and `FreeImage_Rotate` call `FreeImage_CloneMetadata` before `CopyICCProfile`/`FreeImage_CreateICCProfile`, which calls `FreeImage_DestroyICCProfile` and erases `FIMD_EXIF_MAIN` `"InterColorProfile"` |
| **CMS-06** | **High** | Bug (Inverted Linear Image) | `Source/FreeImage/ConversionLinear.cpp` | `FreeImage_ConvertToLinear` ignores `FIC_MINISWHITE` on `FIT_UINT16` images, producing a negative linear-light image |
| **CMS-07** | **High** | Bug (Unhandled 8-bit PQ Images) | `MustTonemap.cpp`, `ColorManagement.cpp` | 8-bit (`FIT_BITMAP`) PQ-encoded images are skipped by `ToDisplay` (`FIF_LOAD_DISPLAY_ICC`) so `FreeImage_MustTonemap` can handle them, but `MustTonemap` unconditionally returns `FITM_NONE` for `FIT_BITMAP` |
| **CMS-08** | **High** | Bug (Rejected/Wrong ICC & UnscaledBits in Linear Conversion) | `ConversionLinear.cpp`, `ColorManagement.cpp` | 4-bit greyscale and narrow-range transparent 8-bit greyscale `FIT_BITMAP` images return `FIC_PALETTE`, causing `AttachCICPProfile` to attach an RGB profile and `ConvertToLinear` to reject Gray ICC profiles; `ConvertToLinear` also rejects RGB ICC profiles on 8-bit greyscale, ignores `AToB0` LUTs on Gray profiles, and ignores `"UnscaledBits"` in `ConvertThroughProfile` |
| **TMO-01** | **High** | Regression (`NaN` / Black Image & Exploding Exponent `m`) | `tmoReinhard05.cpp`, `tmoColorConvert.cpp` | `FreeImage_TmoReinhard05Ex` divides by `log(maxLum) - log(minLum) == 0` on uniform-positive-luminance images, computes `k >> 1` and `m >> 1` when `Y == 0` pixels pull `Llav < minLum`, and does not guard `Cav` against `+Inf` |
| **TK-01** | **High** | Regression (Heap Over-Read & Palette Loss) | `Source/FreeImageToolkit/ClassicRotate.cpp` | `FreeImage_Rotate` overwrites custom 1-bit palettes on 90°/180°/270°/360° rotations, and `Rotate90`/`Rotate270` iterate `0..src_pitch` instead of `FreeImage_GetLine(src)`, reading past the heap buffer on 1-bit views and rotating padding bits into valid pixels |
| **TIFF-01** | **High** | Regression (Corrupted Thumbnail SubIFD) | `Source/FreeImage/PluginTIFF.cpp` | Saving a multipage TIFF page (`page >= 0`) that has a thumbnail writes `FILETYPE_PAGE` instead of `FILETYPE_REDUCEDIMAGE` on the thumbnail SubIFD |
| **TIFF-02** | **High** | Bug (Infinite Loop / Wrong IFD / 32-bit Overflow) | `Source/FreeImage/PluginTIFF.cpp` | `rowsperstrip == 0` causes infinite loops in strip-reading paths (`lines 2163, 2323, 2627`); `PageCount` leaves `tif` positioned at the last IFD; `LoadAsRBGA` can overflow 32-bit `tmsize_t` |
| **PSD-01** | **High** | Bug (Infinite Loop / OOB / Overflow) | `Source/FreeImage/PSDParser.cpp` | Negative `oResource._Size` causes an infinite loop in `ReadImageResources`; `_Height == 0` or negative dimensions are not rejected in `psdHeaderInfo::Read`; 32-bit `nChannels * nHeight * 4` overflows in RLE table allocation |
| **PSD-02** | **Medium** | Regression (Null Deref / Mutated Input) | `Source/FreeImage/PSDParser.cpp` | `psd_write_exif_profile_raw` dereferences `tag_value` without checking `NULL` or `length >= 6`; `psdThumbnail::Write` mutates caller's thumbnail in-place on `FREEIMAGE_COLORORDER_BGR` |
| **BMP-01** | **High** | Regression (Lost Transparency / False Alpha) | `Source/FreeImage/PluginBMP.cpp` | `CarriesAlpha` returns `FALSE` for 100% transparent (`alpha == 0`) 32-bit images, turning them 100% opaque on reload; truncated 32-bit `BI_RGB` BMPs with `0xFF` padding bytes are misclassified as transparent |
| **BMP-02** | **High** | Regression (Unreadable V5 16-bit BMP, RLE Bugs & OS/2 2.x Header Offset) | `Source/FreeImage/PluginBMP.cpp` | Saving a default 16-bit RGB555 DIB (`masks == 0`) with an ICC profile writes zero bitfield masks in the V5 header; 16-bit RGB565 without V5 omits `SwapLong` on big-endian; `DecodeRLE8` clamps `RLE_DELTA` to `width + 1`; `LoadPixelDataRLE4` overflows 32-bit `int` and leaves `RLE_DELTA` unclamped; `LoadOS22XBMP` fails to skip the remaining 24 bytes of a 64-byte OS/2 2.x header when `bfOffBits == 0` |
| **PNG-01** | **Medium** | Bug (Ignored Gamma Chunk & Narrow-Range Palette) | `Source/FreeImage/PluginPNG.cpp`, `MustTonemap.cpp` | `ConfigureDecoder` suppresses `gAMA` whenever `PNG_INFO_cICP` is present even when `cicp_rules` is `false`; `ExpandNarrowRange` fails to expand the palette on `FIF_LOAD_NOPIXELS` for 8-bit `FIC_MINISBLACK` PNGs and modifies pixels instead of palette on 8-bit `PNG_COLOR_TYPE_PALETTE` greyscale ramps |
| **APNG-01** | **High** | Bug (Lost Gray ICC / Stale Canvas Cache / `PLTE` Error) | `Source/FreeImage/PluginAPNG.cpp`, `MustTonemap.cpp` | Grayscale APNGs with a Gray ICC profile lose their profile on `APNG_PLAYBACK` and use the wrong curve in `APNG_LINEAR_BLEND`; `RenderFrame` ignores `flags` changes when reusing `info->canvas`; post-`IDAT` duplicate `PLTE` chunks break `libpng`; multi-frame save drops XMP/Comments/DateTime |
| **MNG-01** | **High** | Bug (Invalid JNG Save / Header Mismatch / Big-Endian) | `Source/FreeImage/MNGHelper.cpp`, `PluginMNG.cpp` | Saving 8-bit `FIC_MINISWHITE` to JNG writes a 3-channel `JHDR` with a 1-channel `JDAT` and drops Gray ICC profiles; `FIF_LOAD_NOPIXELS` on JNG with alpha returns 24/8-bit instead of 32-bit; `mng_WriteJNG` uses unconditional `SwapLong`; global `bKGD` shifts 8-bit values to `0` |
| **MNG-02** | **High** | Bug (`realloc` Leak, Unchecked CRC EOF, Advisory `BACK`, Missing `IEND`) | `Source/FreeImage/MNGHelper.cpp`, `PluginMNG.cpp` | `mng_ReadChunks` leaks memory on `realloc` failure and omits `+ 4` for the CRC in its EOF check (reading uninitialized `crc_file` and discarding decoded `JDAT` on trailing chunk CRC EOF); `ParseBACK` treats advisory `BACK`/`bKGD` as mandatory; `BuildPNGStream` copies 0-byte local `tRNS` chunks verbatim; `ScanEmbeddedStream` swallows remaining MNG frames if an embedded image lacks `IEND` |
| **GIF-01** | **High** | Regression (Corrupted Small Interlaced GIFs & OOB Write) | `Source/FreeImage/PluginGIF.cpp` | Interlaced GIF `Save` uses `if` instead of `while` when advancing interlace passes, dropping rows on images with `height <= 4`; `Load` writes 1 byte past `size` in `transparent_color <= size`; big-endian `Save` passes byte-swapped `width` to `CompressStart` |
| **WEBP-01** | **High** | Bug (Uncorrected CMYK Save & Stripped Animation Tags) | `Source/FreeImage/PluginWebP.cpp` | Still WebP `Save` (`!as_frame`) does not convert 32-bit CMYK or non-RGB ICC profiles (encoding CMYK bytes directly as BGRA); single-frame WebP animation metadata is stripped by `WebPMuxAssemble` (`MuxCleanup`) when frame covers canvas; 1-page multipage WebP with offset is saved as animation; truncated `VP8X`-only WebP emits misleading warning in `Open` |
| **JXR-01** | **High** | Regression (Broken Windowed Banded Decode & Overflow) | `Source/FreeImage/PluginJXR.cpp` | `CopyBandedPixels` passes current stream position `image_pos` instead of `uImageOffset` to `IsWindowed`, so `IsWindowed` always returns `FALSE`; `(UINT64)width * height * bpp / 8` can overflow 64 bits before `PlausibleImageSize` |
| **PNM-01** | **High** | Bug (Multi-GB Allocation / 0-Row Blank Image on EOF) | `Source/FreeImage/PluginPNM.cpp` | Raw PNM (`P4`/`P5`/`P6`) `catch` block short-circuits `(text == PNM_EOF) \|\| ((y > 0) && PlausibleImageSize(...))`, bypassing both `y > 0` and `PlausibleImageSize` on truncated headers |
| **HDR-01** | **High** | Regression (False Format Error & Masked OOM) | `Source/FreeImage/PluginHDR.cpp` | Calling `rgbe_ReadPixels_RLE(..., width, 1)` per row causes flat/old-RLE HDR files to fail if pixel 0 of any row `y > 0` has `R=2, G=2, B<128`, and misclassifies mid-image `malloc` OOM as a truncated file |
| **EXR-01** | **Medium** | Bug (Truncated Scanline EXR Rejected Early) | `Source/FreeImage/PluginEXR.cpp` | `CheckDataWindow` requires `chunks * 16 <= stream_bytes` across all chunks before `ReadEachBlock` runs, rejecting narrow scanline EXR files cut off mid-stream |
| **RAW-01** | **High** | Bug (Buffer Over-Read / Missing NUL Terminator) | `Source/FreeImage/PluginRAW.cpp` | `LibRaw_freeimage_datastream::gets` writes up to `length` bytes without NUL termination and returns `NULL` on last line without `\n`; `scanf_one` stops on NUL padding |
| **AVIF-01** | **Medium** | Bug (Silent Uncropped/Unrotated Image on OOM & Missing Header Metadata) | `Source/FreeImage/PluginAVIF.cpp` | `ApplyTransforms` silently returns the uncropped/unrotated `dib` when `FreeImage_Copy`/`FreeImage_Rotate` fails on OOM; `LoadHeader` (`FIF_LOAD_NOPIXELS`) omits `AttachMetadata` and multi-page frame seek |
| **HEIF-01** | **Medium** | Bug (Missing `nclx` Color Profile on Sequences) | `Source/FreeImage/PluginHEIF.cpp` | Animated HEIF (`heics`) track parser `ReadSampleEntry` ignores `"nclx"` `colr` boxes, unlike still HEIF (`LoadStill`) |
| **JPEG-01** | **Medium** | Regression (Memory Leak, Wrong ICC on Greyscale, IFD1 Abort, DPI Overwrite) | `Source/FreeImage/PluginJPEG.cpp`, `Exif.cpp` | `PluginJPEG::Save` `catch` block skips `jpeg_destroy_compress`; `JPEG_GREYSCALE` attaches RGB/CMYK ICC profiles to 8-bit greyscale output; `jpeg_read_exif_dir` returns `FALSE` instead of `TRUE` on truncated IFD1; `ReplaceWithRotated` overwrites swapped DPI on 90°/270° Exif rotation |
| **J2K-01** | **Medium** | Bug (Integer Wrap-Around on Wavelet Overshoot) | `Source/FreeImage/J2KHelper.cpp` | `SampleScale::operator()` returns `value` unclamped when `m_table.empty()` (`prec == 8`, `prec == 16`, or `J2K_UNSCALED`), wrapping negative or `> max` DWT values around `BYTE`/`WORD` |
| **TGA-01** | **High** | Bug (Corrupted Input DIB on Thumbnail Save) | `Source/FreeImage/PluginTARGA.cpp` | `PluginTARGA::Save` calls `SwapRedBlue32(dib)` / `swapShortPixels(dib)` on `dib` instead of `thumbnail` when writing the TGA 2.0 postage stamp on RGB / big-endian builds |
| **PLUG-01** | **Medium** | Bug (0-Row Blank Images & Missing `PlausibleImageSize`) | `PluginIFF.cpp`, `PluginPCX.cpp`, `PluginTARGA.cpp`, `PluginRAS.cpp`, `PluginWBMP.cpp`, `PluginDDS.cpp`, `PluginXBM.cpp` | Multiple plugins return a 100% blank `FIBITMAP` when `rows == 0` on EOF, lack `PlausibleImageSize` checks on uncompressed/DXT paths, or allow negative/overflowing dimensions (`PluginXBM`) |
| **BUILD-01** | **Medium** | Regression (Broken Source List Generators, MinGW `mif3`, MSVC Test Build) | `gensrclist.sh`, `genfipsrclist.sh`, `Makefile.mingw`, `Test.2017.vcxproj` | `gensrclist.sh`/`genfipsrclist.sh` reference deleted OpenEXR 2.2 dirs and omit `.cc` files; `Makefile.mingw` omits `-DAVIF_ENABLE_EXPERIMENTAL_MINI=1`; `Test.2017.vcxproj` omits `testToneMapping.cpp` |

---

## Detailed Findings

---

### 1. Core Library, I/O, `MultiPage`, `CacheFile`, and Wrappers

---

#### [CORE-01] `FreeImage_Save` and `FreeImage_SaveU` truncate and delete existing target files on invalid inputs

> **Verification (2026-10-18): Partly: the behaviour is real, the regression is not.** Verified severity **Low** (reported Critical).
>
> 1. `fopen(filename, "w+b")` runs before any argument check: **pre-existing**. `origin/main`'s `FreeImage_Save` opens the file the same way, so the existing content is already truncated to 0 bytes there.
> 2. `qpv` added `remove()` / `_wremove()` on failure: **true and deliberate** ("do not leave a partial image behind"; it also catches a failing `fclose`). It removes a file whose content `fopen` had already destroyed, so nothing is lost that `main` kept.
> 3. Tested at HEAD (`lead/core01.cpp`): a 14-byte file is gone after a refused save with a NULL dib, a `FIF_LOAD_NOPIXELS` dib, `FIF_UNKNOWN`, `FIF_AVIF` (read-only) and `FIT_COMPLEX` to GIF. `main` leaves a 0-byte file for the same calls.
>
> Still worth doing: check `fif`, `save_proc`, `HasPixels` and `SupportsExport*` before opening, or save to a temporary name and rename.

- **Severity**: **Critical** (Data Loss Regression vs `origin/main`)
- **Location**: `Source/FreeImage/Plugin.cpp:470-563`
- **Code**:
  ```cpp
  BOOL DLL_CALLCONV
  FreeImage_Save(FREE_IMAGE_FORMAT fif, FIBITMAP *dib, const char *filename, int flags) {
      ...
      FILE *handle = fopen(filename, "w+b");
      if (handle) {
          BOOL success = FreeImage_SaveToHandle(fif, dib, &io, (fi_handle)handle, flags);
          fclose(handle);
          if (!success) {
              remove(filename);
          }
          return success;
      }
      return FALSE;
  }
  ```
- **Root Cause & Impact**:
  1. `fopen(filename, "w+b")` immediately truncates `filename` to 0 bytes *before* `FreeImage_SaveToHandle` validates `FreeImage_HasPixels(dib)` (`Plugin.cpp:472`), `fif` range, or `node->m_plugin->save_proc != NULL` (`Plugin.cpp:481`).
  2. In `qpv`, `if (!success) remove(filename);` was added (`Plugin.cpp:531`, `_wremove(filename)` at `560`).
  3. Consequently, if an application calls `FreeImage_Save` pointing to an existing file on disk while passing `dib == NULL`, a header-only `FIBITMAP`, `FIF_UNKNOWN`, a read-only format (`FIF_HEIF`, `FIF_AVIF`, `FIF_RAW`), or an unsupported pixel type for that format, FreeImage **truncates and unlinks the existing file on disk**.
- **Recommended Fix**:
  Validate `FreeImage_HasPixels(dib)`, plugin existence, `node->m_plugin->save_proc != NULL`, and `SupportsExportType` / `SupportsExportBPP` **before** calling `fopen(filename, "w+b")`. Only call `remove(filename)` if the file did not exist prior to `fopen` (or write to a temporary file first).

---

#### [CORE-02] `CacheFile::lockBlock` self-evicts `m_current_block->data` after a single `cleanupMemCache` disk error, causing an immediate `NULL` dereference in `readFile`

> **Verification (2026-10-18): Confirmed.** Verified severity **Medium** (reported Critical). Regression: **yes** (`main` ignored spool write errors and always evicted, so its list never stayed over-full).
>
> - The quoted code is wrong: `cleanupMemCache()` is `void` and `lockBlock()` never checks it. The mechanism holds anyway: after one failed flush the memory list stays at `CACHE_SIZE + 1`; `lockBlock()` of a block already in memory does not move it to the front, so its own `cleanupMemCache()` call can flush and free the very block it returns.
> - Reproduced (`lead/core02.cpp`): `FreeImage_OpenMultiBitmap(FIF_TIFF, ..., keep_cache_in_memory = FALSE)`, 32 one-block pages, the 33rd append under `RLIMIT_FSIZE` (EFBIG, as on a full disk), the limit lifted, then `FreeImage_LockPage(0)`: SIGSEGV in `memcpy` in `CacheFile::readFile` (NULL source), called from `FreeImage_LoadPageFromCache` and `FreeImage_LockPage`. Without the refused write the page loads.
> - Needs an on-disk cache and a spool write that fails once and later succeeds (a full disk that is then freed).
>
> **Fixed (2026-10-19).** 20d1ea9: `lockBlock()` moves the block it locks to the front of the memory list, and `cleanupMemCache()` never flushes the locked block and flushes until the list is back to 32 blocks. The RLIMIT_FSIZE case now reads all 33 pages back and saves them; `CacheFile` driven alone with spool writes failing in random windows crashed in 16 of 40 seeds and is clean in all 40.

- **Severity**: **Critical** (Crash / `NULL` Pointer Dereference)
- **Location**: `Source/FreeImage/CacheFile.cpp:178-201`, `225-266`, `319-338`
- **Code**:
  ```cpp
  bool CacheFile::cleanupMemCache() {
      if (!m_keep_in_memory) {
          if (m_page_cache_mem.size() > CACHE_SIZE) {
              Block *old_block = m_page_cache_mem.back();
              ...
              if (FreeImage_fseek64(m_file, (INT64)old_block->nr * BLOCK_SIZE, SEEK_SET) != 0)
                  return false;
              ...
              delete[] old_block->data;
              old_block->data = NULL;
              m_page_cache_disk.push_front(old_block);
              m_page_cache_mem.pop_back();
          }
      }
      return true;
  }

  Block *CacheFile::lockBlock(int nr) {
      ...
      m_current_block = *(it->second);
      if (m_current_block->data == NULL) {
          ...
          m_page_cache_mem.push_front(m_current_block);
          m_page_map[nr] = m_page_cache_mem.begin();
      }
      if (!cleanupMemCache()) {
          return NULL;
      }
      return m_current_block;
  }
  ```
- **Root Cause & Impact**:
  1. When `m_page_cache_mem.size()` reaches `33` (`CACHE_SIZE + 1`) and `cleanupMemCache()` fails once (e.g., transient write failure), `m_page_cache_mem.size()` remains `33`.
  2. If `lockBlock(nr)` is subsequently called for the block at `m_page_cache_mem.back()`, `m_current_block->data` is already non-`NULL` (in memory). Notice that `lockBlock` does **not** move an already-in-memory block to the front of `m_page_cache_mem`.
  3. `lockBlock` then calls `cleanupMemCache()` at line 257. Because `m_page_cache_mem.size() > CACHE_SIZE` is still true (`33 > 32`), `cleanupMemCache()` flushes `m_page_cache_mem.back()` — **which is `m_current_block` itself** — and executes `delete[] old_block->data; old_block->data = NULL;`.
  4. `lockBlock` returns `m_current_block` with `m_current_block->data == NULL`!
  5. In `CacheFile::readFile` (`CacheFile.cpp:330`), `memcpy(data + s, block->data, copy_size)` immediately dereferences `block->data == NULL` and crashes.
- **Recommended Fix**:
  In `lockBlock`, ensure `cleanupMemCache()` never evicts `m_current_block` (e.g., splice `m_current_block` to `m_page_cache_mem.begin()` before calling `cleanupMemCache()`, or check `old_block != m_current_block`).

---

#### [CORE-03] `CacheFile` permanent wedging (`m_current_block != NULL`) and memory/block leaks when `std::bad_alloc` is thrown

> **Verification (2026-10-18): Confirmed (all four parts; details differ).** Verified severity **Medium** (reported High). Regression: **no**: `CacheFile` is the same in `main`, which has no `catch` at all, so there `std::bad_alloc` escaped the C API.
>
> Tested by failing the n-th `operator new` in the test program (196 runs, on a direct `CacheFile` driver and through the public API). No exception escaped and nothing crashed.
> 1. `writeFile` wedge: **confirmed**. A throw from `allocateBlock()` (CacheFile.cpp:367) while block 2 or later is locked leaves `m_current_block` set for good. Afterwards every `AppendPage` returns FALSE, `LockPage` of earlier cached pages returns NULL and `CloseMultiBitmap` returns FALSE. A new file is never written; an existing file is left untouched with all edits lost. The blocks stay in the cache lists and `close()` frees them: held, not leaked.
> 2. `lockBlock` reload: **confirmed** (only a disk cache with more than 32 blocks). `m_current_block` is set before `new BYTE[]`; a failure there wedges the cache the same way.
> 3. `allocateBlock`: **confirmed, broader**. A throw from `new BYTE[]` leaks the `Block`; a throw from `push_front` leaks the `Block` and its 64 KB (LSan).
> 4. `RememberPageMetadata` throwing after a stored page: **confirmed (differs)**. Only pages with `FIMD_ANIMATION` tags; the blocks are held until close, not leaked. `FreeImage_SavePageToBlock` has no `catch` of its own; its callers do.
>
> Related leaks the report missed are listed under "Bugs the report missed".
>
> **Fixed (2026-10-19).** 20d1ea9: `allocateBlock()` and `lockBlock()` change nothing until their allocations succeed, `writeFile()` allocates the next block after unlocking and frees the blocks it stored when memory runs out, `deleteFile()` follows the chain without reading blocks back, and `FreeImage_SavePageToBlock` / `FreeImage_UnlockPage` free a stored page whose animation tags cannot be remembered. With every allocation failed in turn, 54 of 76 `CacheFile` cases and 122 of 200 API cases wedged or leaked before, none after. Of the bugs the report missed, the `FIMEMORY` and `compressed_data` leaks are fixed too.

- **Severity**: **High** (State Corruption & Memory Leak)
- **Location**: `Source/FreeImage/CacheFile.cpp:204-218`, `230-237`, `352-375`; `Source/FreeImage/MultiPage.cpp:993-1012`, `1063-1084`, `1294-1320`
- **Root Cause & Impact**:
  `MultiPage.cpp` wraps `CacheFile` operations in `try { ... } catch (std::bad_alloc &)` blocks to recover from OOM, but `CacheFile` is not exception-safe:
  1. **`CacheFile::writeFile` (`CacheFile.cpp:353-370`)**: Calls `lockBlock(copy_alloc)` (setting `m_current_block != NULL`), then at line 367 calls `next_alloc = allocateBlock()` *before* `unlockBlock(copy_alloc)` at line 369. If `allocateBlock()` throws `std::bad_alloc` on the second or later block of a page, `unlockBlock` is never called. `m_current_block` remains non-`NULL` permanently, causing all future calls to `lockBlock` or `deleteBlock` to fail, and all blocks allocated since `stored_alloc` are leaked.
  2. **`CacheFile::lockBlock` (`CacheFile.cpp:230-237`)**: Sets `m_current_block = *(it->second);` *before* `m_current_block->data = new BYTE[BLOCK_SIZE];`. If `new` throws `std::bad_alloc`, `m_current_block` remains non-`NULL` with `data == NULL`.
  3. **`CacheFile::allocateBlock` (`CacheFile.cpp:205-206`)**: `Block *block = new Block;` followed by `block->data = new BYTE[BLOCK_SIZE];` leaks `block` if the second `new` throws.
  4. **`FreeImage_SavePageToBlock` (`MultiPage.cpp:993-1012`) & `FreeImage_UnlockPage` (`MultiPage.cpp:1294-1320`)**: If `FreeImage_RememberPageMetadata` throws `std::bad_alloc` after `header->m_cachefile.writeFile` has succeeded, the `catch` block does not call `header->m_cachefile.deleteFile(ref)` / `deleteFile(iPage)`, leaking the written cache blocks.
- **Recommended Fix**:
  Use `std::nothrow` or RAII guards inside `CacheFile`, assign `m_current_block` only after allocation succeeds, unlock the current block before allocating the next block in `writeFile`, and clean up `deleteFile(ref)` if `FreeImage_RememberPageMetadata` fails.

---

#### [CORE-04] `FreeImage_CloseMultiBitmap` on Windows deletes destination file before `renameFile`

> **Verification (2026-10-18): Partly: pre-existing, and now Windows-only.** Verified severity **Low** (reported High). Regression: **no**.
>
> 1. The quote drops the guard: the remove is inside `#ifdef _WIN32` (MultiPage.cpp:902-905). POSIX renames over the target atomically, and a failed rename keeps the original and drops the spool (tested with a failing `rename`). On Windows, a rename that fails after the remove leaves nothing at the path, but the spool holding the new content is kept and the message names both paths. `main` removed before renaming on every platform; `qpv` fixed POSIX in 6267d80.
> 2. A remove that fails (sharing violation) makes the rename fail and leaves the spool: **true, Windows-only, minor**; the original stays intact. Same in `main`.

- **Severity**: **High** (Data Loss Regression on Windows)
- **Location**: `Source/FreeImage/MultiPage.cpp:885-900`
- **Code**:
  ```cpp
  header->m_filename.removeFile();

  if (!spool_name.renameFile(header->m_filename)) {
      FreeImage_OutputMessageProc(header->fif, "Failed to replace destination file");
  #ifndef _WIN32
      spool_name.removeFile();
  #endif
      success = FALSE;
  }
  ```
- **Root Cause & Impact**:
  1. `header->m_filename.removeFile()` unconditionally deletes the user's original file before attempting `spool_name.renameFile(header->m_filename)`. If `renameFile` fails for any reason after `removeFile()` succeeded, the original file is already gone.
  2. Conversely, if `removeFile()` fails because another process holds `header->m_filename` open, `renameFile()` also fails and `#ifndef _WIN32` skips removing the `.fispool` temporary file, leaving orphaned `.fispool` files on disk.
- **Recommended Fix**:
  On Windows, use `ReplaceFileW` or `MoveFileExW(..., MOVEFILE_REPLACE_EXISTING)` so replacement is atomic and the original file is never unlinked before replacement succeeds.

---

#### [CORE-05] MultiPage cached (`BLOCK_REFERENCE`) vs uncached (`BLOCK_CONTINUEUS`) load-flag and color-space mismatch

> **Verification (2026-10-18): Not a bug (by design).** Verified severity **None** (reported High).
>
> A cached page is a bitmap the caller handed in (`AppendPage`, `InsertPage`, `UnlockPage(changed)`), and it comes back as stored. That is the documented design (`DISPLAY-ICC-PLAN.md` §2.2: "nothing: these are the app's own pixels", and §4.9, readable at `0abf265^`), and `TestAPI/ICC/profiles.c:1389` checks it.
> 1. Tested: a page loaded with flags 0 from a CMYK TIFF is RGB. Changed and unlocked, it re-locks as RGB with the digest it was handed in with. Only an appended CMYK page comes back CMYK, because it is the caller's own. `SavePages` writes the same bytes, so `LockPage` and the saved file agree.
> 2. `FIF_LOAD_DISPLAY_ICC`: a changed page is already display-referred and tagged; converting it again would be an identity.
> 3. `FIF_LOAD_NOPIXELS`: a header-only page cannot be stored (`UnlockPage(TRUE)` fails), so cached pages always have pixels, and the flag is documented as "not supported by all plugins".

- **Severity**: **High** (Behavioral Bug)
- **Location**: `Source/FreeImage/MultiPage.cpp:350-398`, `1189-1208`
- **Code**:
  ```cpp
  static int
  FreeImage_GetCacheLoadFlags(FREE_IMAGE_FORMAT fif) {
      switch (fif) {
          case FIF_TIFF: return TIFF_CMYK;
          case FIF_PSD:  return PSD_CMYK;
          case FIF_JPEG: return JPEG_CMYK;
          case FIF_JXL:  return JXL_CMYK;
          default:       return 0;
      }
  }
  ```
- **Root Cause & Impact**:
  - When `FreeImage_LockPage` loads an **uncached** page (`BLOCK_CONTINUEUS`, `MultiPage.cpp:1189-1208`), it passes `header->load_flags` to `load_proc` and, if `header->load_flags & FIF_LOAD_DISPLAY_ICC` is set, calls `ConvertToDisplayICC`.
  - When `FreeImage_LockPage` loads a **cached** page (`BLOCK_REFERENCE`, created by `FreeImage_AppendPage`, `FreeImage_InsertPage`, or `FreeImage_UnlockPage(..., TRUE)`), it calls `FreeImage_LoadPageFromCache` (`MultiPage.cpp:367`), which passes `FreeImage_GetCacheLoadFlags(header->cache_fif)` and **completely ignores `header->load_flags`**:
    1. If a CMYK multipage TIFF/PSD/JXL was opened with `flags = 0` (caller expects RGB pages) and a page is modified with `UnlockPage(..., TRUE)` or appended, locking that page again returns a `FIC_CMYK` bitmap instead of RGB.
    2. If the multipage bitmap was opened with `FIF_LOAD_DISPLAY_ICC`, locking a cached page skips `ConvertToDisplayICC`.
    3. If the multipage bitmap was opened with `FIF_LOAD_NOPIXELS`, locking a cached page still decodes full pixel data.
- **Recommended Fix**:
  Pass `header->load_flags & FIF_LOAD_NOPIXELS` and apply `FIF_LOAD_DISPLAY_ICC` (or CMYK-to-RGB conversion when `header->load_flags` did not request `*_CMYK`) in `FreeImage_LockPage` when loading from `FreeImage_LoadPageFromCache`.

---

#### [CORE-06] MultiPage GIF palette destruction on `FreeImage_UnlockPage(..., TRUE)` or `FreeImage_DeletePage(bitmap, 0)`

> **Verification (2026-10-18): Confirmed, pre-existing.** Verified severity **Medium** (reported High). Regression: **no** (`main` sets `GlobalPalette` on page 0 only and saves the same way).
>
> 1. Tags per page: **confirmed**. `NoLocalPalette` goes on every page, `GlobalPalette` on page 0 only (a deliberate choice, 14550e7).
> 2. `UnlockPage(changed)` on a page > 0: **confirmed (differs)**. The cached one-page GIF has no colour table, so re-locking that page in the same session gives a grey ramp. The saved file is intact, though: the page's animation tags are restored and frame 1 is written without a local table, using page 0's global one (checked with Pillow too).
> 3. `DeletePage(0)`: **confirmed**. The file is written with no global and no local colour table, and FreeImage and Pillow both decode every frame as grey.
>
> Fix hint: when page 0 has no `GlobalPalette`, `Save` writes the dib's own palette as the global table.
>
> **Fixed (2026-10-19).** f89db21: `Save` writes a first frame's own palette as the global table when the frame asks for the global table but has no `GlobalPalette` tag, and gives a frame without a local table one when the global table written does not hold its colors (they must agree where both define a color, and a frame with more colors may use none past the table's end). `DeletePage(0)`, `MovePage` to the front, a relocked changed page and a page saved alone keep their colors (Pillow agrees). 412 GIFs of the earlier corpora save byte for byte as before, except the 16 page-1 saves that had lost their colors.

- **Severity**: **High** (Color Corruption Regression)
- **Location**: `Source/FreeImage/PluginGIF.cpp:1176-1207`, `1303`, `1354`, `1463`, `1517`, `1670`
- **Root Cause & Impact**:
  1. In `PluginGIF::Load`, frames with `page > 0` that use the GIF Global Color Table have `"NoLocalPalette" = 1` set in `FIMD_ANIMATION` (`PluginGIF.cpp:1361`), whereas `"GlobalPalette"` is only attached when `page == 0` (`PluginGIF.cpp:1354`).
  2. When a user locks frame `page > 0`, modifies it, and calls `FreeImage_UnlockPage(bitmap, dib, TRUE)`, `FreeImage_SavePageToMemory` saves `dib` into the cache as a standalone single-page GIF (`page = 0`). In `PluginGIF::Save` (`PluginGIF.cpp:1463`, `1517`), `page == 0` looks for `"GlobalPalette"` (absent on `dib`), and then checks `"NoLocalPalette"` (which is `1` on `dib`!), so it writes **neither a Global Color Table nor a Local Color Table** into the cached GIF stream! When that frame is later locked or saved into the final GIF, `PluginGIF::Load` (`line 1205`) falls back to a 256-entry greyscale ramp, permanently destroying all colors on that frame.
  3. Similarly, if the user deletes frame 0 (`FreeImage_DeletePage(bitmap, 0)`) and closes/saves the multipage GIF, the new frame 0 was originally loaded as `page == 1` and lacks `"GlobalPalette"`, so the saved GIF has no Global Color Table and all remaining frames using the global palette turn greyscale. (Note that `qpv` already moved `LogicalWidth`, `LogicalHeight`, and `Loop` to be set on every frame at `PluginGIF.cpp:1303` for this exact reason, but left `"GlobalPalette"` inside `if (page == 0)` at line 1354.)
- **Recommended Fix**:
  Attach `"GlobalPalette"` to all frames (just like `LogicalWidth`/`LogicalHeight`/`Loop` at line 1303), or in `PluginGIF::Save` fall back to `FreeImage_GetPalette(dib)` whenever `page == 0` has `"NoLocalPalette" = 1` but no `"GlobalPalette"` tag.

---

#### [CORE-07] `FreeImage_GetLine` / `FreeImage_GetPitch` 32-bit unsigned multiplication overflow on wide bitmaps

> **Verification (2026-10-18): Partly: real, but upstream.** Verified severity **Low** (reported High). Regression: **no**.
>
> 1. `FreeImage_GetLine` multiplies in 32 bits: **confirmed**, and `GetPitch`, built from it, wraps too. `FIT_RGBAF` width 2^25, 32-bit width 2^27 and 8-bit width 2^29 all give `GetLine` = `GetPitch` = 0.
> 2. "`qpv` updated `CalculateLine` to 64-bit": **false**. `main`'s `CalculateLine` and `GetLine` are identical to HEAD's.
> 3. Such bitmaps can be allocated: **confirmed**, for rows of 512 MiB to 4 GiB.
> 4. "Memory corruption": **confirmed (differs)**. `GetScanLine` stays inside the buffer (the rows alias), but callers sizing from `GetLine` misbehave: `FreeImage_Copy` returns the wrong pixels, and `FreeImage_FlipHorizontal` reads before its 0-byte buffer (ASan heap-buffer-overflow READ; SIGSEGV without ASan). No write overflow was found.

- **Severity**: **High** (Integer Overflow / Heap Corruption)
- **Location**: `Source/FreeImage/BitmapAccess.cpp:1211-1222` vs `Source/Utilities.h:332-336`
- **Code**:
  ```cpp
  unsigned DLL_CALLCONV
  FreeImage_GetLine(FIBITMAP *dib) {
      return FreeImage_HasPixels(dib) ? ((FreeImage_GetWidth(dib) * FreeImage_GetBPP(dib)) + 7) / 8 : 0;
  }
  ```
- **Root Cause & Impact**:
  In `qpv`, `CalculateLine` (`Utilities.h:332`) was updated to `(((unsigned long long)width * bitdepth) + 7) / 8`, allowing `FreeImage_AllocateBitmap` (`BitmapAccess.cpp:233`) to allocate wide bitmaps on 64-bit platforms where `width * bpp + 7 > UINT_MAX` (for example, a `FIT_RGBAF` image with `bpp = 128` and `width = 33,554,432`, where `line = 536,870,912` fits in `unsigned`, but `width * 128 = 4,294,967,296` wraps to `0` in 32-bit `unsigned`). Because `FreeImage_GetLine` was not updated to call `CalculateLine`, `FreeImage_GetLine` and `FreeImage_GetPitch` return `0` (or a truncated value) for a bitmap whose buffer was allocated with the full 64-bit `CalculatePitch`, causing `FreeImage_GetScanLine` and callers to corrupt memory.
- **Recommended Fix**:
  Implement `FreeImage_GetLine` using `CalculateLine(FreeImage_GetWidth(dib), FreeImage_GetBPP(dib))`.

---

#### [CORE-08] Zero-dimension `FIBITMAP` with `FreeImage_HasPixels() == TRUE` via `FreeImage_CreateView` / `FreeImage_AllocateHeaderForBits` causes `SIGFPE`

> **Verification (2026-10-18): Not a bug.** Verified severity **None** (reported High).
>
> Neither quoted check exists in HEAD, `d521037` or `main`. `FreeImage_AllocateBitmap` rejects a zero width or height on every path, external bits included (BitmapAccess.cpp:314). So `CreateView(dib, x, y, x, y2)`, `AllocateHeaderForBits` and `ConvertFromRawBitsEx` with a zero dimension all return NULL (tested at 8 and 24 bpp), and `FreeImage_Copy` / `FreeImage_Paste` never see such a dib. `main` has the same check.

- **Severity**: **High** (`SIGFPE` Crash Regression)
- **Location**: `Source/FreeImage/BitmapAccess.cpp:258-263`, `Source/FreeImageToolkit/CopyPaste.cpp:803-829`, `453`, `535`
- **Code**:
  ```cpp
  // BitmapAccess.cpp:258
  if ((header_only == FALSE) && (ext_bits == NULL)) {
      if ((width == 0) || (height == 0)) {
          return NULL;
      }
  }
  ```
  ```cpp
  // CopyPaste.cpp:821 (FreeImage_CreateView)
  if ((left > right) || (right > FreeImage_GetWidth(dib)) || (top > bottom) || (bottom > FreeImage_GetHeight(dib))) {
      return NULL;
  }
  ```
- **Root Cause & Impact**:
  1. When `ext_bits != NULL` (`FreeImage_AllocateHeaderForBits`, used by `FreeImage_CreateView` and `FreeImage_ConvertFromRawBitsEx`), `FreeImage_AllocateBitmap` no longer rejects `width == 0` or `height == 0`, creating a `FIBITMAP` with `FreeImage_HasPixels(dib) == TRUE` and `width == 0` or `height == 0`.
  2. In `FreeImage_CreateView` (`CopyPaste.cpp:821`), the bounds check uses `left > right` and `top > bottom` instead of `left >= right` and `top >= bottom` (compare `FreeImage_Copy` at line 531, which checks `(left >= right) || (top >= bottom)`). Calling `FreeImage_CreateView(dib, x, y, x, y2)` returns a 0-width view with `FreeImage_HasPixels(view) == TRUE`.
  3. Passing that view to `FreeImage_Copy` (`CopyPaste.cpp:535`: `size_t scale = FreeImage_GetLine(src) / FreeImage_GetWidth(src);`) or `FreeImage_Paste` (`CopyPaste.cpp:453`) immediately triggers an integer division by zero (`SIGFPE`).
- **Recommended Fix**:
  Reject `(width == 0) || (height == 0)` whenever `header_only == FALSE` (including when `ext_bits != NULL`), and change `FreeImage_CreateView` to reject `(left >= right) || (top >= bottom)`.

---

#### [CORE-09] `_MemoryWriteProc` lacks a `size == 0 || count == 0` guard; `FreeImage_LoadMultiBitmapFromMemory` crashes on `stream == NULL`

> **Verification (2026-10-18): Not a bug.** Verified severity **None** (reported Medium).
>
> 1. `_MemoryWriteProc` with a zero size or count returns `count` and may grow the buffer, unlike `fwrite`: **pre-existing and harmless**. `main` behaves the same (HEAD also zero-fills a seek gap that `main` leaves uninitialised), and there is no memory-safety effect.
> 2. `FreeImage_LoadMultiBitmapFromMemory(fif, NULL, flags)` crashes: **false**. `_MemoryTellProc` returns -1 for a NULL handle (FreeImageIO.cpp:239), the page count is 0 and the call returns NULL (tested on 41 readable formats). Only a hand-built `FIMEMORY` with `data == NULL` crashes, which the API cannot produce, and `main` crashes on that too.

- **Severity**: **Medium** (Regression / Crash)
- **Location**: `Source/FreeImage/FreeImageIO.cpp:133-188`, `Source/FreeImage/MultiPage.cpp:1430-1460`
- **Root Cause & Impact**:
  1. `_MemoryWriteProc` (rewritten in `qpv` for 64-bit `FIMEMORYHEADER`) does not guard against `(size == 0) || (count == 0)` (unlike `_MemoryReadProc` at line 106). Calling `write_proc(buf, 0, 1, handle)` or `write_proc(buf, 1, 0, handle)`:
     - On a newly opened empty `FIMEMORY` (`data_length == 0`, `current_position == 0`), evaluates `0 >= 0` (`true`) and allocates 4,096 bytes.
     - After seeking past `file_length`, zero-fills from `file_length` to `current_position`, advances `file_length = current_position`, and returns `count` (which is non-zero when `size == 0, count > 0`).
  2. `FreeImage_LoadMultiBitmapFromMemory` (`MultiPage.cpp:1430`) does not check `if (!stream || !stream->data) return NULL;` before calling `FreeImage_OpenMultiBitmapFromHandle(&io, (fi_handle)stream, ...)`, which immediately calls `header->io.tell_proc(header->handle)` (`_MemoryTellProc`) and dereferences `stream->data` (`NULL`).
- **Recommended Fix**:
  Add `if ((size == 0) || (count == 0)) return 0;` at the start of `_MemoryWriteProc`, and validate `if (!stream || !stream->data) return NULL;` in `FreeImage_LoadMultiBitmapFromMemory`.

---

#### [CORE-10] Breaking ABI change in `FI_SeekProc` / `FI_TellProc` and memory leak in `fipMultiPage::unlockPage`

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported Medium).
>
> 1. `FI_SeekProc` / `FI_TellProc` take `INT64`: **true, deliberate and documented** (7de5c34: "only old Windows binaries with callbacks of their own would break: QPV and the wrappers implement none"; comment at `FreeImage.h:596`; the .NET, Delphi, VB6, C++/CLI and AHK wrappers were updated). It breaks only binaries that are not rebuilt and implement their own callbacks, on Win32, Win64 and 32-bit POSIX. On LP64 Linux `long` already is 64-bit.
> 2. `fipMultiPage::unlockPage` leaks: **false**. `FreeImage_UnlockPage` unloads the page on every FALSE return for a locked page (MultiPage.cpp:1239-1351). It leaves alone only a bitmap that is not a locked page, which is a caller error and the same in `main`. LSan is clean on the real wrapper.

- **Severity**: **Medium** (ABI Compatibility & Memory Leak)
- **Location**: `Source/FreeImage.h:598-599`, `Wrapper/FreeImagePlus/src/fipMultiPage.cpp:136-144`
- **Root Cause & Impact**:
  1. `FI_SeekProc` and `FI_TellProc` changed their offset/return type from `long` to `INT64`. On 32-bit platforms and 64-bit Windows (LLP64, where `sizeof(long) == 4` and `sizeof(INT64) == 8`), any existing C/C++/C#/Delphi/VB6 caller passing a standard `FreeImageIO` callback table with `long` signatures suffers stack/register corruption or truncated return values.
  2. `FreeImage_UnlockPage` was changed from `void` to `BOOL`, returning `FALSE` without freeing `dib` if saving the modified page fails (or on a read-only multibitmap when `changed == TRUE`). However, `fipMultiPage::unlockPage` (`fipMultiPage.cpp:136-144`) ignores the return value and unconditionally sets `image._dib = NULL; image._bHasChanged = FALSE;`, leaking the `FIBITMAP*`.

---

### 2. Color Management, Linear Light, Tone Mapping, Rescaling, and Toolkit

---

#### [CMS-01] `ColorConverter::CreateTransform`: Skipping Little CMS transform when `m_device_out && in.Same(out)` reinterprets `float*` scanlines as `WORD*` and inverts `FIT_UINT16` `FIC_MINISWHITE`

> **Verification (2026-10-18): Confirmed, but the impact differs.** Verified severity **Medium** (reported Critical). New `qpv` code (`main` has no colour management).
>
> - The shortcut at `ColorManagement.cpp:958` ignores the formats when the output is device CMYK, and `Same()` is an exact byte comparison. Device CMYK is what `FreeImage_ConvertToCMYK(dib, NULL, 0, 0)` selects. (`SetDeviceOut` and `Combine` do not exist; `FreeImage_ConvertCMYKToRGB` takes the format-checked branch and is fine.)
> - Scenario 2: **confirmed, broader**. A `FIT_UINT16` min-is-white image needs no profile at all, since the built-in source and device profiles are the same pointer. Samples 0/16384/49152/65535 give K = 65535/49151/16383/0, the reverse of the min-is-black equivalent (0/16384/49152/65535). With a Little CMS gray profile the result is right.
> - Scenario 1: **confirmed (differs)**. Only a float image carrying FreeImage's built-in sRGB bytes (RGBF/RGBAF) or built-in gray bytes (FLOAT) is affected; any other profile, Little CMS's own sRGB included, gets a transform. The float row is read as `WORD`s, which gives wrong pixels but stays in bounds: **no memory corruption** (ASan clean in `Separate`). The built-in gray uses the sRGB curve, not gamma 2.2.
>
> Fix hint: take the device shortcut only when the sample sizes match (`T_BYTES`) and the input has no `T_FLAVOR`.
>
> **Fixed (2026-10-19).** 47e4902: the device CMYK shortcut needs the stage's sample size and min-is-black samples. A min-is-white ramp of all 65536 values now separates exactly as its min-is-black twin, and float images tagged with the built-in profiles match their 16-bit twins within 1; 441 other layout, profile and intent cases are byte-identical.

- **Severity**: **Critical** (Memory/Pixel Corruption & Image Inversion)
- **Location**: `Source/FreeImageToolkit/ColorManagement.cpp:958`, `1224-1226`, `1262-1264`
- **Code**:
  ```cpp
  // ColorManagement.cpp:958
  bool CreateTransform() {
      ...
      if (!m_proof.handle && in.Same(out) && (m_device_out || (m_in_format == m_out_format))) {
          return true;
      }
      ...
  }
  ```
- **Root Cause & Impact**:
  When `m_device_out` is `true` (used by `Separate` / `FreeImage_ConvertToCMYK` and `Combine` / `FreeImage_ConvertCMYKToRGB` when device CMYK is used), line 958 skips creating `m_transform` (`m_transform == NULL`) whenever `!m_proof.handle && in.Same(out)` is true — **without checking `m_in_format == m_out_format`!**
  - **Scenario 1 (`FIT_RGBF`, `FIT_RGBAF`, or `FIT_FLOAT` with an embedded sRGB or Gray-2.2 ICC profile converted via `FreeImage_ConvertToCMYK(dib, NULL, 0, 0)`)**:
    1. `SetSource(dib)` sets `m_px.depth = 32` (`m_in_format = TYPE_RGB_FLT`, `TYPE_RGBA_FLT`, or `TYPE_GRAY_FLT` — 4 bytes per channel `float`).
    2. `SetDeviceOut(MODEL_CMYK)` (`line 898`) opens `m_device` with `DefaultProfile(m_px.model, (m_px.depth == 8) ? 8 : 16)`, which is `FICMS_PROFILE_SRGB` or `FICMS_PROFILE_GRAY` (`depth = 16`), and sets `m_out_format = TYPE_RGB_16` / `TYPE_GRAY_16` (2 bytes per channel `WORD`).
    3. Because `dib`'s embedded profile is also sRGB (or Gray 2.2), `in.Same(out)` is `true`!
    4. Line 958 sees `in.Same(out) && m_device_out` and returns `true` with `m_transform = NULL`!
    5. In `Separate` (`ColorManagement.cpp:1224-1226`), because `!m_transform` is true, `TransformLine` (`line 1064`) passes `const BYTE *intermediate = src` directly to `ToDeviceCMYK` -> `RGBToDeviceCMYK<WORD>((const WORD*)src, ...)` (`line 1020`).
    6. `RGBToDeviceCMYK<WORD>` reads the `float*` scanline as `const WORD*`, interpreting raw IEEE-754 float bits as 16-bit unsigned integers and reading only half the scanline!
  - **Scenario 2 (`FIT_UINT16` with `FIC_MINISWHITE` converted via `FreeImage_ConvertToCMYK(dib, NULL, 0, 0)`)**:
    1. `m_in_format` is `TYPE_GRAY_16_REV` (`T_FLAVOR(1)`), while `m_out_format` is `TYPE_GRAY_16` (`T_FLAVOR(0)`).
    2. Both `m_source` and `m_device` default to `FICMS_PROFILE_GRAY`, so `in.Same(out)` is `true` and `m_transform` is skipped (`NULL`).
    3. `RGBToDeviceCMYK<WORD>` executes `dst[3] = 65535 - src[0]` on un-reversed `MINISWHITE` samples (`0 = white`), producing a completely inverted (negative) CMYK image!
- **Recommended Fix**:
  Only skip transform creation when `m_in_format` and `m_out_format` share the same sample format/layout (differing at most in `T_EXTRA` when `m_device_out` is true, i.e., `((m_in_format & ~EXTRA_SH(7)) == m_out_format)`).

---

#### [CMS-02] `ColorConverter::SetSource` mutating `m_px.model = MODEL_RGB` for 8-bit greyscale images with an embedded RGB ICC profile breaks `FreeImage_ApplyICCProfile`, `FreeImage_ConvertToICCProfile`, and `ToDisplay`

> **Verification (2026-10-18): Not a bug.** Verified severity **None** (reported High).
>
> Switching an 8-bit grey image that carries an RGB profile to the RGB model is deliberate ("a grey palette may come with an RGB profile", ColorManagement.cpp:886-888; `TestAPI/ICC/convert.c` encodes the same rule), and Little CMS cannot link `TYPE_GRAY_8` to an RGB profile.
> 1. `FreeImage_ConvertToICCProfile(dib, NULL, 0, 0)` returns 24-bit RGB: true and documented ("palettes ... become 24- or 32-bit, or 8-bit grey"). "Attaches an sRGB profile" is false: the result is untagged.
> 2. `FreeImage_ApplyICCProfile` recolours the palette, so the type becomes `FIC_PALETTE`: true and documented ("a palette changes its colours"). The colours shown match `ConvertToICCProfile` exactly.
> 3. Min-is-white inversion: **false**. The table is built from `palette[i]`, and min-is-white and min-is-black images show identical greys.
> 4. Display path: **false**. `ToDisplay` picks the grey target before `SetSource`, and the in-place 8-bit path honours `reversed` (tested with sRGB and ProPhoto display profiles).

- **Severity**: **High** (Color / Palette Corruption)
- **Location**: `Source/FreeImageToolkit/ColorManagement.cpp:135-141`, `251-253`, `884-894`, `1135-1150`, `2160-2194`, `2231-2246`
- **Code**:
  ```cpp
  // ColorManagement.cpp:888 (SetSource)
  if ((model == m_px.model) || (indexed && (m_px.model == MODEL_GRAY) && (model == MODEL_RGB))) {
      m_px.model = model;
      m_source.handle = handle;
      m_source.model = model;
  ```
- **Root Cause & Impact**:
  When an 8-bit greyscale image (`LAYOUT_GRAY8`, `FIC_MINISBLACK` or `FIC_MINISWHITE`) has an embedded RGB ICC profile (which is common in PNG, JPEG, and TIFF files):
  1. **`FreeImage_ConvertToICCProfile(dib, NULL, 0, flags)` and `FreeImage_ApplyICCProfile(dib, NULL, 0, flags)` (`lines 2231-2246`)**:
     Both functions call `converter.SetSource(dib)` first and then pass `converter.DefaultModel()` to `SetDestination`. Because `SetSource` mutated `m_px.model` from `MODEL_GRAY` to `MODEL_RGB`, `converter.DefaultModel()` returns `MODEL_RGB` instead of `MODEL_GRAY`!
     - `FreeImage_ConvertToICCProfile(dib, NULL, 0, 0)` unexpectedly promotes the 8-bit greyscale image to 24-bit RGB and attaches an sRGB profile instead of converting it to default 8-bit greyscale (`FICMS_PROFILE_GRAY`).
     - `FreeImage_ApplyICCProfile(dib, NULL, 0, 0)` (or with an explicit RGB profile) sees `InPlaceLayout(m_px, MODEL_RGB) == LAYOUT_INDEXED` (`line 253`) and runs `RunTable` (`lines 1135-1150`), which **modifies the 256 palette entries while leaving the pixel indices untouched**! Because the palette is no longer a `0..255` linear ramp, `FreeImage_GetColorType(dib)` silently changes from `FIC_MINISBLACK`/`FIC_MINISWHITE` to **`FIC_PALETTE`** (and for `FIC_MINISWHITE` it feeds `i = 0..255` instead of `palette[i]` into the transform and writes the result to `palette[i]`, inverting the image!).
  2. **`ToDisplay` (`lines 2160-2194`)**:
     `AnalysePixels(dib, grey)` sets `grey = true` for `LAYOUT_GRAY8`, choosing `target = display.grey` (`MODEL_GRAY`). If the embedded RGB profile is not in `TwinCache` (or a custom display profile is active), `SetSource` sets `m_px.model = MODEL_RGB`. In `RunTable` (`line 1135`), `grey_in = (m_px.model == MODEL_GRAY)` is `false` while `grey_out` is `true`, so `RunTable` transforms `(i, i, i)` into `palette[i]` instead of transforming the pixel values, which again inverts `FIC_MINISWHITE` images because `RunTable` ignores the existing `255..0` palette ramp when `grey_in` is `false`!
- **Recommended Fix**:
  Do not mutate `m_px.model` inside `SetSource` for `LAYOUT_GRAY8`, or in `RunTable` read `const RGBQUAD *pal = FreeImage_GetPalette(dib)` as input when `m_px.layout == LAYOUT_GRAY8` and update the pixel bytes (resetting the palette to a standard `0..255` ramp) whenever `grey_out == true`.

---

#### [CMS-03] `ConvertToFileProfile`: `TwinCache` short-circuit ignores floating-point default linear profiles (`FICMS_PROFILE_LINEAR_SRGB` / `FICMS_PROFILE_LINEAR_GRAY`) and `MODEL_RGB` vs `MODEL_GRAY` mismatches

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported High). New `qpv` code.
>
> 1. Float frames skip the conversion: **confirmed, in both directions** (untagged linear RGBF against an sRGB file profile; sRGB-tagged RGBF against a NULL one). Only the JPEG XL animation writer passes float frames: an sRGB-tagged RGBAF page next to an untagged one is stored wrong (0.2140 instead of 0.5, and 0.5 instead of 0.2140). WebP, APNG and MNG never pass floats.
> 2. RGB vs grey twins: **false**. Every non-JPEG XL caller's target profile is RGB only, JPEG XL animations use RGB layouts, and grey into an sRGB twin is exact (R = G = B).

- **Severity**: **High** (Skipped Linear $\leftrightarrow$ sRGB & RGB $\leftrightarrow$ Gray Conversion)
- **Location**: `Source/FreeImageToolkit/ColorManagement.cpp:2490-2525`, `1837-1856`
- **Code**:
  ```cpp
  BOOL ConvertToFileProfile(FIBITMAP *dib, const void *profile, unsigned size, FIBITMAP **converted) {
      *converted = NULL;
      ...
      TwinCache &twins = GetTwinCache();
      if ((!own || twins.IsTwin(own, own_size)) && (!profile || twins.IsTwin(profile, size))) {
          return TRUE;
      }
  ```
- **Root Cause & Impact**:
  `TwinCache::IsTwin` (`line 1845`) returns `true` for profiles equivalent to **non-linear** `FICMS_PROFILE_SRGB` (`MODEL_RGB`) **or** `FICMS_PROFILE_GRAY` (`MODEL_GRAY`).
  1. **Floating-point images (`px.depth == 32`, `FIT_FLOAT`, `FIT_RGBF`, `FIT_RGBAF`)**:
     In `DefaultProfile(ColorModel model, unsigned depth)` (`line 523`), when `depth == 32`, the default profile when `own == NULL` or `profile == NULL` is **`FICMS_PROFILE_LINEAR_SRGB` or `FICMS_PROFILE_LINEAR_GRAY` (gamma 1.0)**, *not* sRGB!
     - If `dib` is an untagged 32-bit float image (`own == NULL`, meaning linear sRGB) and `profile` is an sRGB profile (`twins.IsTwin(profile, size) == true`), line 2512 evaluates `(!own) && twins.IsTwin(profile, size)` to `true` and returns `TRUE` (`*converted = NULL`), skipping the required Linear-to-sRGB conversion!
     - Conversely, if `dib` is a `FIT_RGBF` image carrying an embedded sRGB profile (`twins.IsTwin(own, own_size) == true`) and `profile == NULL` (requesting default linear sRGB for a float file format), line 2512 evaluates `twins.IsTwin(own, own_size) && (!profile)` to `true` and skips converting the sRGB float pixels to linear light!
  2. **Model mismatch (`MODEL_RGB` vs `MODEL_GRAY`)**:
     Because `TwinCache::IsTwin` returns `true` for both `FICMS_PROFILE_SRGB` and `FICMS_PROFILE_GRAY`, if `dib` is an untagged or sRGB 24/32-bit RGB image (`px.model == MODEL_RGB`) and the target file `profile` is a `MODEL_GRAY` sRGB-curve profile (`twins.IsTwin(profile, size) == true`), or vice versa, line 2512 returns `TRUE` with `*converted = NULL` instead of converting between RGB and Gray!
- **Recommended Fix**:
  Only use the `TwinCache` short-circuit when `px.depth != 32` (non-float image) **and** the source and destination color models match (`px.model == ModelOf(profile)`).

---

#### [CMS-04] `CopyICCProfile` silently drops the ICC profile on `FIT_UINT16` `FIC_MINISWHITE` images in `FreeImage_RescaleRect` and `FreeImage_ConvertTo*`

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported High). Regression: **no** (`main`'s `RescaleRect` never keeps a profile).
>
> The quoted `CopyICCProfile` is not HEAD's: BitmapAccess.cpp:746-771 copies the bytes itself, requires the same image type and depth, and never calls `FreeImage_CreateICCProfile`.
> 1. `FreeImage_Rescale` / `RescaleRect` / `MakeThumbnail` drop the profile of a `FIT_UINT16` min-is-white image: **confirmed (differs)**. `RescaleRect` copies the profile before `CloneMetadata` (Rescale.cpp:90), while the result is still typed min-is-black. Rotate, Clone, Copy and `ConvertToUINT16` keep the profile, as do min-is-black and 8-bit min-is-white images.
> 2. `ConvertTo*` drop it through `CopyICCProfile`: **false**. They never call it; those that drop profiles drop them for min-is-black too (upstream behaviour, documented).
>
> Fix hint: call `CloneMetadata` first. The order proposed under CMS-05 is the one that causes this.

- **Severity**: **High** (Regression: Silent ICC Profile Loss)
- **Location**: `Source/Utilities.h:769-793`, `Source/FreeImageToolkit/Rescale.cpp:89-95`, `Source/FreeImage/BitmapAccess.cpp:747-771`, `784-796`
- **Code**:
  ```cpp
  // Source/Utilities.h:769
  inline FREE_IMAGE_COLOR_TYPE
  ProfileColorType(FIBITMAP *dib) {
      FREE_IMAGE_COLOR_TYPE type = FreeImage_GetColorType(dib);
      return (type == FIC_RGBALPHA) ? FIC_RGB : type;
  }

  inline BOOL
  CopyICCProfile(FIBITMAP *dst, FIBITMAP *src) {
      FIICCPROFILE *profile = FreeImage_GetICCProfile(src);
      if (!profile || !profile->data) {
          return TRUE;
      }
      if (ProfileColorType(dst) == ProfileColorType(src)) {
          return FreeImage_CreateICCProfile(dst, profile->data, profile->size) != NULL;
      }
      return TRUE;
  }
  ```
  ```cpp
  // Source/FreeImageToolkit/Rescale.cpp:90
  CopyICCProfile(dst, src);
  if ((flags & FI_RESCALE_OMIT_METADATA) != FI_RESCALE_OMIT_METADATA) {
      FreeImage_CloneMetadata(dst, src);
  }
  ```
- **Root Cause & Impact**:
  1. `ProfileColorType(dib)` collapses `FIC_RGBALPHA` to `FIC_RGB`, but does **not** collapse `FIC_MINISWHITE` to `FIC_MINISBLACK` (even though both are monochrome/grayscale and use `icSigGrayData` ICC profiles!).
  2. For a `FIT_UINT16` image, `FreeImage_GetColorType` (`BitmapAccess.cpp:789-794`) inspects the `FIMD_EXIF_MAIN` `"PhotometricInterpretation"` metadata tag to return `FIC_MINISWHITE` vs `FIC_MINISBLACK`.
  3. In `FreeImage_RescaleRect` (`Rescale.cpp:90`), `CopyICCProfile(dst, src)` is called **before** `FreeImage_CloneMetadata(dst, src)` (and when `FI_RESCALE_OMIT_METADATA` is set, metadata is not cloned at all). At line 90, `dst` does not yet have `"PhotometricInterpretation"`, so `FreeImage_GetColorType(dst)` returns `FIC_MINISBLACK` while `FreeImage_GetColorType(src)` returns `FIC_MINISWHITE`.
  4. `ProfileColorType(dst) == ProfileColorType(src)` evaluates to `FALSE`, and **`CopyICCProfile` silently drops the Gray ICC profile on every rescaled `FIT_UINT16` `FIC_MINISWHITE` image** (as well as in `FreeImage_ConvertToUINT16`, `FreeImage_ConvertToFloat`, and `FreeImage_ConvertToStandardType` whenever `src` is `FIC_MINISWHITE` and `dst` is `FIC_MINISBLACK`).
- **Recommended Fix**:
  In `ProfileColorType(FIBITMAP *dib)` (`Source/Utilities.h:770` / `BitmapAccess.cpp:747`), treat `FIC_MINISWHITE` and `FIC_MINISBLACK` as equivalent (`if (type == FIC_MINISWHITE) return FIC_MINISBLACK;`).

---

#### [CMS-05] `FreeImage_Copy`, `FreeImage_EnlargeCanvas`, and `FreeImage_Rotate` erase `FIMD_EXIF_MAIN` `"InterColorProfile"` metadata

> **Verification (2026-10-18): Not a bug.** Verified severity **None** (reported Medium).
>
> 1. "`qpv` modified `FreeImage_DestroyICCProfile`": **false**. Removing the Exif `InterColorProfile` tag there is upstream code (r1806, 3.18.0; `main` BitmapAccess.cpp:1103).
> 2. `Copy` / `EnlargeCanvas` lose the tag: **pre-existing** (same order in `main`) and harmless. The ICC profile itself is kept and the tag only duplicates it; loaders strip it, and a stale copy is what would hurt (the TIFF writer writes it over the real profile: tested).
> 3. `Rotate`: **false**. The tag and the profile survive (tested at 90 and 30 degrees, and `RotateEx`).

- **Severity**: **Medium** (Regression: Lost Metadata Tag)
- **Location**: `Source/FreeImageToolkit/CopyPaste.cpp:599-617`, `Source/FreeImageToolkit/Background.cpp:888-906`, `Source/FreeImageToolkit/ClassicRotate.cpp:868-869`, `895-896`, `912-913`
- **Root Cause & Impact**:
  In `qpv`, `FreeImage_DestroyICCProfile` (`BitmapAccess.cpp:1190`) was modified to also delete the `"InterColorProfile"` tag from `FIMD_EXIF_MAIN`:
  ```cpp
  FreeImage_SetMetadata(FIMD_EXIF_MAIN, dib, "InterColorProfile", NULL);
  ```
  And `FreeImage_CreateICCProfile` calls `FreeImage_DestroyICCProfile(dib)` first (`BitmapAccess.cpp:1165`) — even when `src_profile->size == 0`!
  Because `FreeImage_Copy`, `FreeImage_EnlargeCanvas`, and `FreeImage_Rotate` call `FreeImage_CloneMetadata(dst, src)` **before** `FreeImage_CreateICCProfile` / `CopyICCProfile`, creating the ICC profile on `dst` immediately deletes the cloned `"InterColorProfile"` tag from `dst`'s `FIMD_EXIF_MAIN` metadata!
- **Recommended Fix**:
  Call `CopyICCProfile(dst, src)` before `FreeImage_CloneMetadata(dst, src)` (once `CMS-04` is fixed).

---

#### [CMS-06] `FreeImage_ConvertToLinear` ignores `FIC_MINISWHITE` on `FIT_UINT16` images

> **Verification (2026-10-18): Confirmed.** Verified severity **Low** (reported High). New `qpv` code.
>
> `BuildTables`, `m_tables` and `m_width` do not exist (the quote is invented), but the mechanism holds: `FIT_UINT16` maps to `IN_GREY16` and the colour type is never checked. A 16-bit min-is-white TIFF with samples 0/16384/32768/65535 linearises to 0/0.0509/0.2140/1.0, while its min-is-black equivalent gives 1.0/0.5225/0.2140/0. `FreeImage_ConvertToICCProfile` honours min-is-white. (Upstream's `ConvertTo8Bits` and `ConvertToStandardType` ignore it too.)

- **Severity**: **High** (Bug: Inverted Linear-Light Image)
- **Location**: `Source/FreeImage/ConversionLinear.cpp:259-277`, `398-400`, `588-591`, `614-627`
- **Code**:
  ```cpp
  // ConversionLinear.cpp:398
  case IN_GREY16:
      for (unsigned x = 0; x < m_width; x++) {
          ((float*)dst)[x] = m_tables.v1[((const WORD*)src)[x]];
      }
      break;
  ```
- **Root Cause & Impact**:
  For 1-, 4-, and 8-bit `FIT_BITMAP`, `FreeImage_ConvertToLinear` calls `FreeImage_ConvertTo24Bits(dib)` (`line 584`), which expands the palette and naturally accounts for `FIC_MINISWHITE`. For `FIT_UINT16`, however, `src` is `dib` itself (`layout = IN_GREY16`), and `ConversionLinear.cpp` never checks `FreeImage_GetColorType(dib) == FIC_MINISWHITE` (unlike `ColorManagement.cpp:163`). It indexes the LUT directly with the un-reversed `MINISWHITE` sample (`0 = white`, `65535 = black`), producing a negative (inverted) linear-light image.
- **Recommended Fix**:
  In `BuildTables` (`ConversionLinear.cpp:259`) or `ConvertRows` (`case IN_GREY16`), reverse the table or index `65535 - sample` when `m_type == FIT_UINT16 && FreeImage_GetColorType(src) == FIC_MINISWHITE`.

---

#### [CMS-07] 8-bit (`FIT_BITMAP`) PQ-encoded images are skipped by `ToDisplay` (`FIF_LOAD_DISPLAY_ICC`) but return `FITM_NONE` from `FreeImage_MustTonemap`

> **Verification (2026-10-18): Confirmed.** Verified severity **Low** (reported High). New `qpv` code.
>
> `ToDisplay` keeps every PQ-tagged `FIT_BITMAP` and 16-bit image unconverted (ColorManagement.cpp:2143-2159), and `MustTonemap` returns `FITM_NONE` for any `FIT_BITMAP` before looking at the transfer (MustTonemap.cpp:542). An 8-bit PNG with cICP 9/16/0/1 loaded with `FIF_LOAD_DISPLAY_ICC` keeps its pixels and gets `FITM_NONE`, although README says "PQ gives FITM_PQ". `FreeImage_ConvertToLinear` handles the image correctly. Producers: 8-bit PQ PNG, AVIF and HEIF files.

- **Severity**: **High** (Bug: Unhandled 8-bit PQ HDR Images)
- **Location**: `Source/FreeImage/MustTonemap.cpp:541-543` vs `Source/FreeImageToolkit/ColorManagement.cpp:2155-2159`
- **Code**:
  ```cpp
  // ColorManagement.cpp:2155 (ToDisplay)
  // so do PQ images, up to 10000 cd/m2: converted as display samples they would be clipped, and lose
  // the CICP tag FreeImage_MustTonemap and FreeImage_ConvertToLinear read them by
  if (IsPQEncoded(dib)) {
      return true;
  }
  ```
  ```cpp
  // MustTonemap.cpp:541 (MustTonemap)
  switch (FreeImage_GetImageType(dib)) {
      case FIT_BITMAP:
          return FITM_NONE;
  ```
- **Root Cause & Impact**:
  When an 8-bit PQ-encoded image (`FIT_BITMAP` with CICP transfer code 16, e.g. 8-bit PQ AVIF, HEIF, PNG, or JXL) is loaded with `FIF_LOAD_DISPLAY_ICC`, `ToDisplay` checks `IsPQEncoded(dib)` and intentionally skips converting it so that `FreeImage_MustTonemap` and `FreeImage_ConvertToLinear` can handle it.
  However, `MustTonemap` in `MustTonemap.cpp` returns `FITM_NONE` unconditionally for `FIT_BITMAP` without checking `TransferOf(dib, false) == TRANSFER_PQ` (even though `FreeImage_ConvertToLinear` supports `FIT_BITMAP`). Thus an 8-bit PQ image is neither converted by `FIF_LOAD_DISPLAY_ICC` nor flagged as `FITM_PQ` by `FreeImage_MustTonemap`.
- **Recommended Fix**:
  In `MustTonemap` (`MustTonemap.cpp:542`), check `if (TransferOf(dib, false) == TRANSFER_PQ) return FITM_PQ;` for `case FIT_BITMAP:`.

---

#### [CMS-08] Multiple ICC/CICP/UnscaledBits bugs in `ConversionLinear.cpp` and `AttachCICPProfile` (`ColorManagement.cpp`)

> **Verification (2026-10-18): Partly (4 of 6 parts).** Verified severity **Low** (reported High). New `qpv` code.
>
> - 1a. A 4-bit 0, 17, ..., 255 grey palette and a keyed narrow-range grey image are `FIC_PALETTE`: **true**, the upstream colour-type rule.
> - 1b. `AttachCICPProfile` gives them an RGB profile: **not a bug**. It is deliberate (the PNG writer keeps such images as palettes, whose embeddable profile must be RGB) and equivalent for neutral pixels.
> - 1c. `ConvertToLinear` rejects a GRAY profile on a 4-bit grey image: **confirmed** (136 maps to 0.2462 by the sRGB curve instead of 0.5333). The keyed narrow-range half is unreachable: narrow range implies a CICP tag, which outranks the profile.
> - 2. An RGB profile on an 8-bit min-is-black image is rejected: **confirmed** (a grey JPEG saved with an RGB profile reloads like that). The `MustTonemap` part holds only for `FIT_UINT16`.
> - 3. A GRAY profile with only an `AToB0` LUT falls back to the sRGB curve: **confirmed** (legal, rare).
> - 4. `ConvertThroughProfile` ignores `UnscaledBits`: **confirmed**. 12-bit unscaled RGB16 through a LUT profile (test3.icc) gives 0.0013/0.0026/0.0053 instead of 0.0512/0.2155/1.0; reachable with `J2K_UNSCALED` / `JP2_UNSCALED` and a LUT colr profile.

- **Severity**: **High** (Bug: Rejected/Mismatched ICC Profiles & UnscaledBits in Linear Conversion)
- **Location**: `Source/FreeImage/ConversionLinear.cpp:331-343`, `475-492`, `525-540`; `Source/FreeImageToolkit/ColorManagement.cpp:2576-2581`
- **Code**:
  ```cpp
  // ConversionLinear.cpp:337 (IsGreyPixels) & ColorManagement.cpp:2578 (AttachCICPProfile)
  case FIT_BITMAP: {
      const FREE_IMAGE_COLOR_TYPE color = FreeImage_GetColorType(dib);
      return (color == FIC_MINISBLACK) || (color == FIC_MINISWHITE);
  }
  ```
- **Root Cause & Impact**:
  1. **4-bit greyscale and narrow-range transparent 8-bit greyscale `FIT_BITMAP` images return `FIC_PALETTE`, breaking `IsGreyPixels` and `AttachCICPProfile`**:
     In `BitmapAccess.cpp:836-862`, `FreeImage_GetColorType(dib)` only returns `FIC_MINISBLACK` or `FIC_MINISWHITE` when `palette[i].rgbRed == i` or `ncolors - 1 - i`. A 4-bit greyscale image (whose 16 palette entries are `0, 17, 34, ..., 255`) or an 8-bit transparent greyscale image after `ExpandNarrowRange` (which expands the palette in-place, clamping `0..16 -> 0` and `235..255 -> 255`) has `FreeImage_GetColorType(dib) == FIC_PALETTE` even though every palette entry has `R == G == B`.
     - In `AttachCICPProfile` (`ColorManagement.cpp:2578-2579`), `grey` evaluates to `FALSE`, attaching an **RGB** CICP ICC profile (`"RGB "`) instead of a **Gray** CICP ICC profile (`"GRAY"`).
     - In `ConvertToLinear` (`ConversionLinear.cpp:337, 526`), `IsGreyPixels(dib)` returns `false`, so `if (profile && (space != (grey ? cmsSigGrayData : cmsSigRgbData)))` **rejects and closes a valid embedded `cmsSigGrayData` ICC profile** on a 4-bit greyscale or narrow-range transparent 8-bit greyscale image, falling back to `FromSRGB`.
  2. **`ConversionLinear.cpp:526` rejects embedded `cmsSigRgbData` ICC profiles on `FIC_MINISBLACK` / `FIC_MINISWHITE` `FIT_BITMAP` images**:
     Both `ColorManagement.cpp` (`line 887`) and `MustTonemap.cpp` (`TransferOf`, `line 372`) accept an embedded `cmsSigRgbData` profile on a greyscale `FIT_BITMAP`. However, `ConversionLinear.cpp:526` checks `space != (grey ? cmsSigGrayData : cmsSigRgbData)` and discards an embedded `cmsSigRgbData` profile when `grey` is `true` — even when `FreeImage_MustTonemap` returned `FITM_PQ` or `FITM_OPTIONAL` based on that exact profile, and even though line 584 expands `FIT_BITMAP` to 24/32-bit RGB before `ConvertRows`!
  3. **`ConversionLinear.cpp:534-540` silently falls back to `FromSRGB` for LUT-based `cmsSigGrayData` profiles**:
     For an RGB profile without matrix-shaper TRC tags, lines 541–555 delegate to `ConvertThroughProfile(dib, alpha)`. For a Gray profile (`profile && grey`) that uses an `AToB0` LUT rather than `cmsSigGrayTRCTag`, `cmsReadTag(profile, cmsSigGrayTRCTag)` returns `NULL` and lines 536–538 silently set `plan.curve = FromSRGB`, ignoring the profile's LUT.
  4. **`ConversionLinear.cpp:475-492` (`ConvertThroughProfile`) ignores `"UnscaledBits"` metadata**:
     When `dib` has `"UnscaledBits"` metadata (e.g. `J2K_UNSCALED` / `JP2_UNSCALED`) and a non-matrix-shaper (LUT) ICC profile, `ConvertToLinear` calls `ConvertThroughProfile(dib, alpha)`, which calls `FreeImage_ConvertToRGBF(dib)` / `FreeImage_ConvertToRGBAF(dib)`. Those divide samples by `255` or `65535` instead of `UnscaledMaximum(dib, maximum)` (`(1u << bits) - 1`), feeding unscaled dark values into the ICC transform.

---

#### [TMO-01] `tmoReinhard05.cpp` & `tmoColorConvert.cpp`: Division by zero (`log(maxLum) - log(minLum) == 0`), exploding contrast exponent `m`, and unguarded `+Inf` in `FreeImage_TmoReinhard05Ex`

> **Verification (2026-10-18): Partly: two real regressions.** Verified severity **Medium** (reported High). Regression: **yes for 1b and 2** (9b55043 made `minLum` skip zero pixels while `Llav` still counts them).
>
> - 1a. Uniform positive image (`maxLum == minLum`): **pre-existing**. HEAD and `main` agree: a sane grey up to L of about 69, all black from L = 86.7 in both.
> - 1b. Black background + one colour: **confirmed (differs), a regression**. HEAD gets `k = m = +inf`: the lit pixels turn white and lose their hue (f·L < 1) or the whole image turns black (f·L > 1). `main` gives a sane result.
> - 2. Zero pixels + a narrow positive range: **confirmed, a regression**. k and m grow past 1 (m from 0.81 to 44 as black covers 1/64 to 56/64 of a 0.5 to 0.8 ramp; every lit pixel is white once half the image is black). Also, with m > 1 the public `FreeImage_ToneMapping(FITMO_REINHARD05)` inverts the tone order (0.01 maps to 255, 1.0 to 145). The report's "k = 20..1000" is overstated (19 at 87.5 % black). Real photos take the other formula.
> - 3. +Inf poisons `Cav`: **pre-existing**, and `main` is worse (one +Inf sample blacks out the whole image for every parameter set). HEAD only zeroes that channel, and only when a ≠ 1 and c ≠ 0.
>
> **Fixed (2026-10-19).** 5cf28ba: the key's log average is taken over the strictly positive samples, as the minimum is; a range of 0.01% or less is flat and gives k = 0 (so a caller's contrast is kept), nearly flat images included; k cannot pass 1; the channel averages skip non-finite samples. All four parts give a picture now. Images without black pixels render byte for byte as before; images with large black areas render differently, since the key no longer counts black. Flooring the minimum at 2.3e-5 instead was rejected: one black or NaN pixel then moved the whole render by up to 17 levels.

- **Severity**: **High** (Regression: `NaN` / All-Black Output Image & Exploding Exponent `m`)
- **Location**: `Source/FreeImage/tmoReinhard05.cpp:40-44`, `94-107`, `140-156`, `228-235`; `Source/FreeImage/tmoColorConvert.cpp:285-288`, `564-584`
- **Code**:
  ```cpp
  // tmoColorConvert.cpp:575 (LuminanceFromY)
  *maxLum = stats.maxLum;
  // minimum positive luminance: callers take its log()
  *minLum = (stats.minPosLum > 0) ? stats.minPosLum : stats.maxLum;
  *Lav = (float)(stats.sumLum / (double)stats.count);
  *Llav = (float)exp(stats.sumLogLum / (double)stats.count);
  ```
  ```cpp
  // tmoReinhard05.cpp:96 (ToneMappingReinhard05)
  LuminanceFromY(Y, &maxLum, &minLum, &Lav, &Llav);
  k = (log(maxLum) - Llav) / (log(maxLum) - log(minLum));
  if(k < 0) {
      k = (log(maxLum) - log(Llav)) / (log(maxLum) - log(minLum));
      if(k < 0) m = 0.3F;
  }
  m = (m > 0) ? m : (float)(0.3 + 0.7 * pow(k, 1.4F));
  ```
- **Root Cause & Impact**:
  1. **Division by zero (`maxLum == minLum`) turns uniform or two-tone images completely black**:
     In `qpv`, `LuminanceFromY` (`tmoColorConvert.cpp:578`) was changed to set `*minLum = stats.minPosLum` (excluding `Y == 0` pixels). For any uniform non-black image — **or any two-tone image consisting of black (`Y == 0`) background pixels and a single constant-luminance foreground color (`Y == L > 0`)** — every positive pixel has luminance `L`, so `*maxLum == *minLum == L` and `log(maxLum) - log(minLum) == 0.0f`!
     At `tmoReinhard05.cpp:97`, dividing by `0.0f` produces `k = +Inf`, `-Inf`, or `NaN`. In IEEE-754 arithmetic, `if (k < 0)` is `false` for `+Inf` and `NaN`, so line 106 computes `m = +Inf` or `NaN`. Then `ToneMapReinhardSample` (`lines 40-44`) returns `0` for every pixel, turning the entire output image completely black!
  2. **Mismatch between `Llav` (includes `Y == 0` pixels) and `minLum` (excludes `Y == 0` pixels) causes `k >> 1` and `m >> 1`**:
     In `GatherLuminanceStats` (`tmoColorConvert.cpp:287`), `Y == 0` pixels are included in `sum_log_lum` as `log(2.3e-5F + 0)` and in `count`, pulling `Llav` down toward `2.3e-5F`. Meanwhile, `*minLum = stats.minPosLum` excludes `Y == 0` pixels. When an image has `Y == 0` pixels and positive pixels in a narrow range above `2.3e-5F` (e.g., `minPosLum = 0.5, maxLum = 0.8`), `Llav` is much smaller than `minLum`, making `log(maxLum) - log(Llav) >> log(maxLum) - log(minLum)`. This produces `k >> 1` (e.g., `k = 20..1000` instead of `k in [0, 1]`) and `m >> 1` (instead of `m in [0.3, 1.0)`), causing `pow(f * I_a, m)` to overflow/underflow.
  3. **Unguarded `+Inf` in `Cav` accumulation (`tmoReinhard05.cpp:140-156`)**:
     `ClampNegativeRGBF` (`line 228`) leaves `+Inf` channel values untouched. While `GatherLuminanceStats` skips `!IsFiniteValue(sample)`, the RGB channel-average loop in `ToneMappingReinhard05` (`lines 140-156`, executed when `a != 1 && c != 0`) sums `Cav[i] += *color` without checking `IsFiniteValue(*color)`, poisoning `Cav[i]` with `+Inf` and zeroing out channel `i` across the entire image.
- **Recommended Fix**:
  Guard `if (maxLum <= minLum || !(k >= 0.0F) || !isfinite(k)) { m = 0.3F; }`, clamp `k` to `[0.0F, 1.0F]`, and skip non-finite channel values in the `Cav` accumulation loop (`lines 140-156`).

---

#### [TK-01] `ClassicRotate.cpp`: `FreeImage_Rotate` overwrites custom 1-bit palettes on all rotations (including 360°), and `Rotate90`/`Rotate270` read past the heap buffer on 1-bit views

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported High). Regression: **no** (same loops, guard and palette code in `main`).
>
> 1. Over-read past the heap on 1-bit views, and padding bits rotated into pixels: **false**. The loops do run to `src_pitch`, but the destination bounds `break` comes before `*sbits` is read, so source bytes past the line are never touched. ASan is clean on views at the parent's last row and on a tightly wrapped buffer, while a mutant that reads before the `break` fires on the same inputs. Padding bits all 0 or all 1 give identical, exact rotations. The quoted guard `((x << 3) + b < dst_width)` exists in no revision.
> 2. The 1-bit palette is rebuilt as black/white: **confirmed**. Red/blue comes out white/black at 90, 180, 360 and -360 degrees, a black/grey palette is inverted, and the transparency table and background colour are dropped at 90/180/270. At ±360 they are kept (the report is wrong there); only the palette is replaced.

- **Severity**: **High** (Regression: Heap Buffer Over-Read & Palette Corruption)
- **Location**: `Source/FreeImageToolkit/ClassicRotate.cpp:359`, `548`, `808-815`, `854-866`
- **Code**:
  ```cpp
  // ClassicRotate.cpp:359 (Rotate90, bpp == 1)
  for (INT64 x = 0; x < src_pitch; x++) {
      *(srcdisp + x) ? ...
  ```
  ```cpp
  // ClassicRotate.cpp:854 (FreeImage_Rotate)
  if (1 == bpp) {
      RGBQUAD *dst_pal = FreeImage_GetPalette(dst);
      if (FreeImage_GetColorType(src) == FIC_MINISBLACK) {
          dst_pal[0].rgbRed = dst_pal[0].rgbGreen = dst_pal[0].rgbBlue = 0;
          dst_pal[1].rgbRed = dst_pal[1].rgbGreen = dst_pal[1].rgbBlue = 255;
      } else { ... }
  }
  ```
- **Root Cause & Impact**:
  1. **Heap buffer over-read and padding-bit corruption in `Rotate90` and `Rotate270` (`lines 359, 548`)**:
     The inner byte loop for `bpp == 1` iterates `for (INT64 x = 0; x < src_pitch; x++)` instead of `(src_width + 7) / 8` (`FreeImage_GetLine(src)`):
     - If `src` is a 1-bit dynamic view created with `FreeImage_CreateView` (`src_pitch > FreeImage_GetLine(src)`), on the last row of the view `srcdisp + x` reads `src_pitch - FreeImage_GetLine(src)` bytes **past the end of the parent bitmap's heap buffer**!
     - Even on a normal 1-bit bitmap, if `src_width` is not a multiple of 8 (or `src_pitch > (src_width + 7) / 8`), if any trailing padding bits in the row are non-zero, `Rotate90` and `Rotate270` write them into valid destination rows (in `Rotate90`, `(src_height - y - 1) < dst_width` is always true, so the `((x << 3) + b < dst_width)` guard at line 365 checks the wrong dimension!).
  2. **1-bit custom palette, transparency, and background color destruction (`lines 854-866`)**:
     `FreeImage_Rotate` unconditionally overwrites `dst_pal[0..1]` with black and white, destroying any colored 2-entry palette (`FIC_PALETTE`) and omitting transparency table / background color copy on 1-bit images (even when `angle == 360.0` or `-360.0`, where `RotateAny` returned `FreeImage_Clone(src)`!).
- **Recommended Fix**:
  In `Rotate90` and `Rotate270`, bound the `bpp == 1` x-loop to `(src_width + 7) / 8` and check `((x << 3) + (7 - b)) < src_width` (`Rotate90` at line 365 currently checks `< dst_width`, which is `src_height`!). In `FreeImage_Rotate`, copy the 2-entry palette, transparency table, and background color from `src` when `angle` is a multiple of 90°.

---

### 3. Format Plugins — Part 1 (`TIFF`, `PSD`, `BMP`, `PNG`, `APNG`, `MNG`/`JNG`, `GIF`, `WebP`)

---

#### [TIFF-01] `PluginTIFF.cpp`: Saving a multipage TIFF page with a thumbnail tags the thumbnail SubIFD as `FILETYPE_PAGE` instead of `FILETYPE_REDUCEDIMAGE`

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported High). Regression: **no** (same branch in `main`).
>
> - The mis-tag is real: a multi-page save passes `page >= 0` for the thumbnail SubIFD too, which gets `NewSubfileType` 2 plus PageNumber and PageName instead of 1 (dumped; single-page saves write 1).
> - The impact is false: `ReadThumbnail` never looks at the subfile type ("subfiletype" appears only on the save side), and the page-0 thumbnail reloads through `FreeImage_Load` and `LockPage(0)`. Pages 1 and later lose theirs through the unrelated `thumbnailCount` guard, whatever their tag.

- **Severity**: **High** (Regression: Corrupted Thumbnail SubIFD in Multipage TIFFs)
- **Location**: `Source/FreeImage/PluginTIFF.cpp:2842`, `2991-3002`, `3039-3044`, `3217-3239`
- **Code**:
  ```cpp
  // PluginTIFF.cpp:2991 (SaveOneTIFF)
  if (page >= 0) {
      TIFFSetField(out, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
      //amaan: add page number and page name
      TIFFSetField(out, TIFFTAG_PAGENUMBER, (uint16_t)page, (uint16_t)page_count);
      char page_name[30];
      sprintf(page_name, "Page %d", page + 1);
      TIFFSetField(out, TIFFTAG_PAGENAME, page_name);
  } else {
      // is it a thumbnail ? 
      TIFFSetField(out, TIFFTAG_SUBFILETYPE, (ifd == 0) ? 0 : FILETYPE_REDUCEDIMAGE);
  }
  ```
- **Root Cause & Impact**:
  In `Save` (`PluginTIFF.cpp:3217-3239`), `SaveOneTIFF` is called in a loop for `ifd = 0` (main page) and `ifd = 1` (thumbnail) with the same `page` parameter. When saving a multipage TIFF (`page >= 0`), `if (page >= 0)` executes for `ifd == 1` as well, writing `FILETYPE_PAGE`, `TIFFTAG_PAGENUMBER`, and `TIFFTAG_PAGENAME` instead of `FILETYPE_REDUCEDIMAGE` on the thumbnail SubIFD! When the multipage TIFF is later loaded, `ReadThumbnail` (`line 1743`) checks `if (subfiletype == FILETYPE_REDUCEDIMAGE)` and ignores the thumbnail.
- **Recommended Fix**:
  Check `if (ifd > 0) { TIFFSetField(out, TIFFTAG_SUBFILETYPE, FILETYPE_REDUCEDIMAGE); } else if (page >= 0) { ... }`.

---

#### [TIFF-02] `PluginTIFF.cpp`: `rowsperstrip == 0` causes infinite loops in strip readers; `PageCount` leaves active directory at last IFD; `LoadAsRBGA` 32-bit overflow

> **Verification (2026-10-18): Not a bug.** Verified severity **None** (reported High).
>
> 1. `RowsPerStrip = 0`: libtiff refuses it while reading the directory (tif_dir.c:371, `goto badvalue32`), so the open fails before any strip loop. Crafted files on the strip, CMYK, LogLuv and RGBA paths return NULL at once.
> 2. `PageCount` position: single-image loads (page -1) never call it, and multi-page loads always seek with `TIFFSetDirectory` (tested both ways on a 2-page file).
> 3. `LoadAsRBGA` overflow: `tmsize_t` is `int64_t` on every build, and libtiff checks its own products.

- **Severity**: **High** (Infinite Loop / Bug)
- **Location**: `Source/FreeImage/PluginTIFF.cpp:1160-1174`, `1809`, `1824`, `1856`, `1936`, `2163`, `2323`, `2627`
- **Root Cause & Impact**:
  1. **`rowsperstrip == 0` infinite loop**: `Load` reads `TIFFGetField(tif, TIFFTAG_ROWSPERSTRIP, &rowsperstrip)` (`line 1856`) instead of `TIFFGetFieldDefaulted`. While line 2299 checks `(row == 0) ? 1 : row`, the strip loops in `LoadAsCMYK` (`line 2163`), `LoadAsGenericStrip` (`line 2323`), and `LoadAsLogLuv` (`line 2627`) step `for (uint32_t y = 0; y < height; y += rowsperstrip)`. If a TIFF explicitly sets `TIFFTAG_ROWSPERSTRIP = 0`, those loops never advance `y` and spin forever.
  2. **`PageCount` directory position (`lines 1160-1174, 1824`)**: `PageCount` iterates `do { nr_ifd++; } while (TIFFReadDirectory(tif) != 0);` without resetting `TIFFSetDirectory(tif, 0)`. Meanwhile, `Load` (`line 1824`) only calls `TIFFSetDirectory(tif, (uint16_t)page)` when `page != -1 && page >= 0`.
  3. **`LoadAsRBGA` 32-bit overflow (`line 1936`)**: `raster = (uint32_t*)_TIFFcalloc((tmsize_t)width * height, sizeof(uint32_t))` can overflow 32-bit `tmsize_t` / `uint32_t` index arithmetic inside `TIFFReadRGBAImage` when `width * height > UINT32_MAX / 4`.
- **Recommended Fix**:
  Sanitize `if (rowsperstrip == 0) rowsperstrip = height;` right after `TIFFGetField(tif, TIFFTAG_ROWSPERSTRIP, &rowsperstrip)` (or use `TIFFGetFieldDefaulted`), and reset `TIFFSetDirectory(tif, 0)` in `PageCount` / `Load`.

---

#### [PSD-01] `PSDParser.cpp`: Infinite loop on negative `oResource._Size`, missing `<= 0` dimension checks in `psdHeaderInfo::Read`, and 32-bit overflow in RLE line-size table allocations

> **Verification (2026-10-18): Partly: two real memory-safety bugs, both pre-existing.** Verified severity **High**. Regression: **no** (`main`'s header read, `nHeight` and RLE table are identical).
>
> 1. A negative resource size loops forever: **false**. The real loop never adds `_Size` (sizes of 0 or less are skipped); -1, -2, -1000000 and 0xFFFFFFFF return at once.
> 2. Missing dimension checks: **confirmed (differs)**. A zero width or height fails cleanly, but a **negative** height passes: `_Height` is signed and the only bound is `> 30000` for version 1. `const unsigned nHeight` (PSDParser.cpp:1578) then becomes about 4.29e9 while the allocator takes the absolute value, and the load ends in a **heap-buffer-overflow WRITE** in `ReadImageLine` (PSDParser.cpp:1488) called from `ReadImageData` (1736). The file is 104 bytes; this was re-run independently with an ASan build of `PSDParser.cpp`. PSB widths over 300000 are not enforced but stay bounded.
> 3. RLE line-size table: **confirmed**. `new (std::nothrow) DWORD[nChannels*nHeight]` (1754) multiplies in 32 bits; 56 channels × 77,000,000 rows wraps to 17,032,704 entries and the largest-line scan reads past them (**heap-buffer-overflow READ**, ASan).
>
> Fix hint: reject `_Width <= 0 || _Height <= 0` (and the PSB limits) in `psdHeaderInfo::Read`; size the table as `(size_t)nChannels * nHeight`.
>
> **Fixed (2026-10-18).** 2ec7065: `psdHeaderInfo::Read` refuses a width or height below 1 (part 2). bb2a0a8: an RLE image whose line-size table would have more than `UINT_MAX / 4` entries is refused before anything is allocated (part 3), and the writer, whose products wrap the same way from 268,435,457 RGBA rows, saves such an image uncompressed. The PSB limit of 300000 is not enforced, since FreeImage writes larger PSBs itself. Both crafted files now fail cleanly under ASan, and 2865 PSD and PSB files load as before.

- **Severity**: **High** (Infinite Loop / Heap Corruption / OOB)
- **Location**: `Source/FreeImage/PSDParser.cpp:337-343`, `1277`, `1301-1314`, `1679`, `1754`, `1763`, `1772`, `1787`, `2205-2209`
- **Root Cause & Impact**:
  1. **Infinite loop in `ReadImageResources` (`lines 1277, 1301-1314`)**: `oResource._Size` is a signed 32-bit `int`. If a PSD resource block has `_Size == -1`, `if ((oResource._Size % 2) != 0) oResource._Size++;` increments it to `0`, and `nBytes += oResource._Size` never advances `nBytes`, trapping `while (nBytes < nTotalBytes)` in an infinite loop (or moving `nBytes` backwards if `_Size < -1`).
  2. **Missing dimension validation in `psdHeaderInfo::Read` (`lines 337-343, 1679`)**: `_Width` and `_Height` are signed `int`s read directly from the file. For PSB (`_Version == 2`), `Read` does not enforce the `<= 300000` specification limit or `_Width > 0 && _Height > 0`. If `_Height == 0` and `_Channels == 2` (`FIT_BITMAP` with `_BitPerChannel == 8`), `FreeImage_AllocateHeader` succeeds when `header_only` is true or reaches `FreeImage_GetScanLine(bitmap, nHeight - 1)` (`0xFFFFFFFF`).
  3. **32-bit overflow in RLE table allocation (`lines 1754, 1787, 2205`)**: `new (std::nothrow) BYTE[nChannels * nHeight * 4]` computes `nChannels * nHeight * 4` in 32-bit `unsigned`, which can wrap around on large PSB dimensions and cause a heap buffer overflow.
- **Recommended Fix**:
  Reject `oResource._Size < 0` in `psdImageResource::Read`, enforce `_Width > 0 && _Height > 0 && _Width <= max_dim && _Height <= max_dim` in `psdHeaderInfo::Read`, and cast `nChannels * nHeight * 4` to `size_t` with overflow checks.

---

#### [PSD-02] `PSDParser.cpp`: Unchecked `tag_value` in `psd_write_exif_profile_raw` and in-place mutation of caller's thumbnail in `psdThumbnail::Write`

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported Medium). Regression: **no**.
>
> 1. `psd_write_exif_profile_raw` compares 6 bytes before checking the tag: **confirmed**. An `ExifRaw` tag of 3 bytes saved as PSD gives an ASan heap-buffer-overflow READ of 6 bytes (PSDParser.cpp:251). It is the same in `main`, and `jpeg_write_exif_profile_raw` (PluginJPEG.cpp:947) has the same order. It needs a short tag made by the caller: the loaders check the "Exif\0\0" signature.
> 2. `psdThumbnail::Write` mutates the caller's thumbnail and seeks to `SEEK_END`: **false**. The swap depends on the `isBGR` argument, which `Save` always passes as `false` (tested: bytes unchanged), and `SEEK_END` resumes after the thumbnail just written.

- **Severity**: **Medium** (Crash / Unexpected Input Mutation)
- **Location**: `Source/FreeImage/PSDParser.cpp:248-258`, `872-875`, `900`
- **Root Cause & Impact**:
  1. `psd_write_exif_profile_raw` (`line 254`) calls `memcmp(exif_signature, tag_value, 6)` without checking `tag_value != NULL` or `FreeImage_GetTagLength(tag_exif) >= 6`.
  2. `psdThumbnail::Write` (`line 873`) calls `SwapRedBlue32(_dib)` directly on `_dib` without restoring it when `_owned == false` (`Init` attached the caller's `dib`'s thumbnail directly), mutating the caller's in-memory thumbnail on `FREEIMAGE_COLORORDER_BGR` builds, and seeks to `SEEK_END` instead of `current_pos` at line 900.

---

#### [BMP-01] `PluginBMP.cpp`: 32-bit BMP transparency regressions in `CarriesAlpha` and `SetAlphaFromUnusedByte`

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported High).
>
> 1. An all-zero alpha reloads opaque: **by design**, the trade-off accepted in 8722330 (BMP spec conformance item L1; ImageMagick, stb, WIC and Firefox read such files the same way). Even a V5 file then writes a zero alpha mask.
> 2. `CarriesAlpha` ignores `FreeImage_IsTransparent`: **not a bug**. For 32-bit images, transparency follows the colour type and the alpha bytes, not the `SetTransparent` flag, throughout the library.
> 3. A cut 32-bit `BI_RGB` file whose unused byte is 0xFF reads as transparent: **confirmed (minor)**. `SetAlphaFromUnusedByte` also scans the zero-filled rows that were never decoded; this is `qpv`'s salvage code.

- **Severity**: **High** (Regression: Lost Transparency & False Transparency on Truncated BMPs)
- **Location**: `Source/FreeImage/PluginBMP.cpp:742-755`, `1013-1022`, `1775-1795`, `1828`
- **Code**:
  ```cpp
  static BOOL
  CarriesAlpha(FIBITMAP *dib) {
      if (FreeImage_GetBPP(dib) != 32) return FALSE;
      BOOL zero = TRUE, full = TRUE;
      for (unsigned y = 0; y < FreeImage_GetHeight(dib); y++) {
          const BYTE *p = FreeImage_GetScanLine(dib, y) + FI_RGBA_ALPHA;
          for (unsigned x = 0; x < FreeImage_GetWidth(dib); x++, p += 4) {
              if (*p != 0x00) zero = FALSE;
              if (*p != 0xFF) full = FALSE;
              if (!zero && !full) return TRUE;
          }
      }
      return FALSE;
  }
  ```
- **Root Cause & Impact**:
  1. **100% transparent 32-bit images become 100% opaque on save + reload**: When saving a completely transparent 32-bit RGBA image (all alpha bytes are `0x00` and `FreeImage_IsTransparent(dib) == TRUE`), `CarriesAlpha(dib)` returns `FALSE` because `zero` remains `TRUE`! `Save` then writes `BI_RGB` (or a V5 header with `bV5AlphaMask = 0`). When that file is loaded back, `SetAlphaFromUnusedByte` (`line 742`) sees all `0x00` alpha bytes, calls `SetOpaque(dib)`, and **overwrites every pixel's alpha byte with `0xFF`**, turning a 100% transparent image into a 100% opaque image!
  2. **Explicitly opaque 32-bit images (`FreeImage_SetTransparent(dib, FALSE)`) with non-constant XRGB padding bytes**: `CarriesAlpha(dib)` ignores `FreeImage_IsTransparent(dib)` and returns `TRUE`, writing a V5 alpha mask (`0xFF000000`) when an ICC profile is present.
  3. **Truncated 32-bit `BI_RGB` BMP with `0xFF` padding bytes (`line 1016`)**: If an opaque 32-bit `BI_RGB` BMP whose writer stored `0xFF` in the unused 4th byte is truncated (`rows < abs(height)`), the decoded rows have `0xFF` while the undecoded zero-initialized rows have `0x00`. `SetAlphaFromUnusedByte` sees mixed `0x00` and `0xFF` values and treats the image as transparent instead of calling `SetOpaque(dib)`.
- **Recommended Fix**:
  In `CarriesAlpha(dib)`, check `if (!FreeImage_IsTransparent(dib)) return FALSE;` first, and if `v5` is active, preserve alpha even when `zero == TRUE`. In `SetAlphaFromUnusedByte`, only inspect the `rows` actually read from the file (`0..rows - 1`).

---

#### [BMP-02] `PluginBMP.cpp`: Saving a default 16-bit RGB555 DIB (`masks == 0`) with an ICC profile writes zero bitfield masks; 16-bit RGB565 omits big-endian swap; `DecodeRLE8` / `LoadPixelDataRLE4` RLE bugs; `LoadOS22XBMP` 64-byte header offset bug

> **Verification (2026-10-18): Partly; the impacts are overstated.** Verified severity **Low** (reported High).
>
> 1. A 16-bit image with an ICC profile writes zero V5 masks: the masks are 0, but "unreadable" is **false**. There is no `ValidateBitfields`: zero colour masks mean the default 555 layout (PluginBMP.cpp:967) and the file reloads. `main` writes zero masks too.
> 2. Missing big-endian swap of the non-V5 masks: **pre-existing**, big-endian only.
> 3. `DecodeRLE8` delta clamp `width + 1`: **confirmed**, and worse than stated. A delta past the end of the row followed by a run returns early, and on row 0 the whole image comes back NULL ("Error decoding RLE8"). Malformed streams only; `qpv`'s rewritten decoder.
> 4. `LoadPixelDataRLE4` `int` arithmetic and unclamped delta: **pre-existing and guarded**. The loop-top and per-write clamps keep it in bounds (ASan clean); a real overflow needs an allocation over 2 GB, which fails first.
> 5. OS/2 2.x 64-byte header with `bfOffBits == 0`: **confirmed, pre-existing**. Only 40 header bytes are read and the seek check uses 40, so the other 24 are decoded as pixels (tested). Affects such files whose `bfOffBits` is 54 or less.

- **Severity**: **High** (Regression: Unreadable Saved BMP, RLE Bugs, and Corrupted OS/2 2.x BMP Load)
- **Location**: `Source/FreeImage/PluginBMP.cpp:328`, `359`, `382-384`, `512`, `519-521`, `541-543`, `1162`, `1204`, `1224`, `1259`, `1829-1842`, `1930-1950`
- **Root Cause & Impact**:
  1. **Unreadable V5 16-bit BMP (`lines 1829-1842`)**: When saving a 16-bit DIB (`dst_bpp == 16`) that has an ICC profile (`v5 == TRUE`), line 1829 forces `bit_fields = TRUE` and writes `FreeImage_GetRedMask(dib)`, `FreeImage_GetGreenMask(dib)`, `FreeImage_GetBlueMask(dib)` into the V5 header. However, `FreeImage_Allocate(w, h, 16)` (with default masks `0, 0, 0`) has `GetRedMask(dib) == 0`. `Save` therefore writes `BI_BITFIELDS` with `0, 0, 0` masks, which `LoadWindowsBMP` (`line 958`: `ValidateBitfields`) immediately rejects as an invalid BMP!
  2. **Missing big-endian byte-swap when saving 16-bit RGB565 without V5 header (`lines 1930-1950`)**: When `!v5 && bit_fields`, lines 1931–1940 write the three `DWORD` masks without `#ifdef FREEIMAGE_BIGENDIAN SwapLong(&d); #endif`, whereas `LoadWindowsBMP` (`line 956`) byte-swaps them on big-endian machines.
  3. **`DecodeRLE8` off-by-one delta clamp (`line 512, 519-521, 541-543`)**: In `RLE_DELTA`, `bits` is clamped via `bits = MIN(bits + delta_x, (INT64)width + 1);` instead of `width`. If `bits == width + 1` and the next opcode is a run (`status_byte > 0`), `count = MIN(status_byte, width - bits)` evaluates to `-1 < 0`, hitting `if (count < 0) return scanline;` and aborting the RLE8 decode prematurely.
  4. **`LoadPixelDataRLE4` 32-bit `int` overflow and unclamped `RLE_DELTA` (`lines 328, 359, 382-384`)**: Computes `pixels + height * width` and `scanline * width` in 32-bit signed `int` (overflowing when `width * height > INT_MAX`), and does not clamp `bits += delta_x` to `width` in `RLE_DELTA`.
  5. **`LoadOS22XBMP` header size and file offset bug (`lines 1162, 1204, 1224, 1259`)**:
     - `bitmap_bits_offset` is `offset_in_file + bitmapfileheader.bfOffBits`, yet lines 1162, 1224, and 1259 omit `offset_in_file` on the right-hand side and compare against `sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + (used_colors * 3)` (`40` instead of `bih.biSize`, which can be `64` for OS/2 2.x headers).
     - For 16/24/32-bit OS/2 2.x BMPs where `bfOffBits == 0` (`bitmap_bits_offset == offset_in_file`) and `bih.biSize == 64`, `LoadOS22XBMP` only reads 40 bytes of `bih` (`sizeof(BITMAPINFOHEADER)` at line 1082) and never seeks past the remaining 24 bytes of the 64-byte OS/2 2.x header before reading pixel data, decoding the last 24 bytes of the header as pixels!
- **Recommended Fix**:
  Default V5 16-bit `0, 0, 0` masks to `FI16_555_RED_MASK`, `FI16_555_GREEN_MASK`, `FI16_555_BLUE_MASK`; byte-swap the `!v5 && bit_fields` masks on `FREEIMAGE_BIGENDIAN`; clamp `bits` to `width` in `DecodeRLE8` and `LoadPixelDataRLE4`; use `INT64` in `LoadPixelDataRLE4`; and seek to `offset_in_file + sizeof(BITMAPFILEHEADER) + bih.biSize` before reading the palette/pixels in `LoadOS22XBMP`.

---

#### [PNG-01] `PluginPNG.cpp` & `MustTonemap.cpp`: `ConfigureDecoder` suppresses `gAMA` even when CICP code points are unspecified; `ExpandNarrowRange` fails on `FIF_LOAD_NOPIXELS` for 8-bit `FIC_MINISBLACK` and modifies pixels instead of palette on 8-bit `PNG_COLOR_TYPE_PALETTE` greyscale ramps

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported Medium).
>
> 1. A cICP chunk the loader cannot read (code points 2/2) suppresses `gAMA`: **confirmed**, a change from `main` (c637684). libpng flags any cICP, `ConfigureDecoder` counts it as a colour description, and `ReadsCICP` then rejects it: gAMA 1.0 + cICP 2/2/0/1 loads as 64/128/192/255, untagged, instead of 136/186/224/255. PNG 3 lets cICP outrank gAMA only "if understood by the decoder". The cICP precedence work kept "gAMA off for any cICP" on purpose, so this one needs a decision. Fix hint, if wanted: count only a readable cICP.
> 2. `ExpandNarrowRange` on header-only loads and grey-ramp palettes: **false**. Header-only and full loads agree in every case tested (8-bit grey, identity grey `PLTE`, with and without `tRNS`), and remapping the indices of an identity palette gives exactly the colours a palette remap would.

- **Severity**: **Medium** (Bug: Ignored Gamma Chunk & Narrow-Range Palette Bug)
- **Location**: `Source/FreeImage/PluginPNG.cpp:587-591`, `810-811`, `976-980`; `Source/FreeImage/MustTonemap.cpp:183`, `194-209`
- **Code**:
  ```cpp
  // PluginPNG.cpp:587 (ConfigureDecoder)
  BOOL described = png_get_valid(png_ptr, info_ptr, PNG_INFO_iCCP) || png_get_valid(png_ptr, info_ptr, PNG_INFO_sRGB);
  #ifdef PNG_INFO_cICP
  described = described || png_get_valid(png_ptr, info_ptr, PNG_INFO_cICP);
  #endif
  if (png_get_valid(png_ptr, info_ptr, PNG_INFO_gAMA) && !described) {
  ```
  ```cpp
  // MustTonemap.cpp:183, 194 (ExpandNarrowRange)
  const unsigned height = FreeImage_HasPixels(dib) ? FreeImage_GetHeight(dib) : 0;
  ...
  if ((bpp == 8) && (FreeImage_GetColorType(dib) == FIC_MINISBLACK) && !FreeImage_IsTransparent(dib)) {
      for (unsigned y = 0; y < height; y++) { ... }
  } else if (bpp <= 8) {
      RGBQUAD *palette = FreeImage_GetPalette(dib);
  ```
- **Root Cause & Impact**:
  1. **Ignored `gAMA` chunk when `cICP` has unspecified code points (`PluginPNG.cpp:587-591` vs `810-811`)**: A PNG `cICP` chunk with `colour_primaries = 2` and `transfer_function = 2` (unspecified) sets `PNG_INFO_cICP` in `libpng`, causing `ConfigureDecoder` to skip `png_set_gamma`. Later at line 811, `ReadsCICP` returns `cicp_rules = FALSE`, so neither an ICC profile nor `gAMA` gamma correction is applied.
  2. **`ExpandNarrowRange` on `FIF_LOAD_NOPIXELS` and 8-bit palettized greyscale ramps (`PluginPNG.cpp:976-980`, `MustTonemap.cpp:183, 194-209`)**:
     For an 8-bit `PNG_COLOR_TYPE_PALETTE` PNG whose `PLTE` chunk happens to be a 256-entry `0..255` greyscale ramp (so `FreeImage_GetColorType(dib) == FIC_MINISBLACK`): on a `FIF_LOAD_NOPIXELS` load (`height == 0`), `ExpandNarrowRange(dib)` enters the `FIC_MINISBLACK` pixel loop (`0..height=0`) and leaves the palette unexpanded, whereas on a full pixel load it modifies the pixel indices instead of the `PLTE` palette!

---

#### [APNG-01] `PluginAPNG.cpp`: Grayscale APNG with Gray ICC profile loses profile on `APNG_PLAYBACK` and uses wrong curve in `APNG_LINEAR_BLEND`; `RenderFrame` ignores `flags` changes; post-`IDAT` duplicate `PLTE` breaks load; multi-frame save drops metadata

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported High). New `qpv` code.
>
> 1. A grey ICC profile is lost in `APNG_PLAYBACK`: **confirmed (differs)**. Dropping it is right (a GRAY profile cannot tag RGBA; `CopyColorDescription` checks the source profile, not the destination), but the grey frames are composited without being converted through their profile, so a gamma 1.0 grey shows up to 73 levels off (gamma 1.8: 19; an sRGB-curve grey: 0). MNG playback converts every layer. The sRGB curve for `APNG_LINEAR_BLEND` is documented. Only third-party files are affected: FreeImage's APNG writer keeps RGB profiles only.
> 2. `RenderFrame` ignores changed flags: **false**. The flags are fixed per open handle.
> 3. A `PLTE` after `IDAT` is routed ahead of it: true, but such a file is invalid anyway. libpng rejects the duplicate ("PLTE: duplicate", not "Too many PLTE chunks") with or without FreeImage's routing, which exists for files whose only `PLTE` comes late.
> 4. Multi-frame saves drop comments, XMP and `tIME`: **confirmed**. `CompressFrame` writes only `pHYs` and `iCCP`/`sRGB`; a lone untagged page, saved as a plain PNG, keeps them.

- **Severity**: **High** (Multiple Bugs in New `PluginAPNG`)
- **Location**: `Source/FreeImage/PluginAPNG.cpp:459`, `580-618`, `716-725`, `747-785`, `929-945`; `Source/FreeImage/MustTonemap.cpp:136-139`
- **Root Cause & Impact**:
  1. **Gray ICC profile lost on `APNG_PLAYBACK` and `APNG_LINEAR_BLEND`**:
     - `APNG_PLAYBACK` always composites onto a 32-bit RGBA canvas (`FIC_RGBALPHA`). In `CopyColorDescription` (`MustTonemap.cpp:136-139`), `memcmp(icc->data + 16, "RGB ", 4) == 0` rejects `"GRAY"` ICC profiles when `dst` is 32-bit RGBA, silently dropping the grayscale APNG's ICC profile!
     - In `LinearTables` (`PluginAPNG.cpp:594`), `CopyColorDescription(ramp, Description(info, flags))` also rejects the `"GRAY"` ICC profile when copying onto the 24-bit RGB `ramp`, so `APNG_LINEAR_BLEND` blends using the sRGB curve instead of the Gray ICC profile's tone curve!
  2. **`RenderFrame` canvas cache ignores `flags` changes (`lines 716-725`)**:
     When loading frames via a multipage handle, `RenderFrame` reuses `info->canvas`, `info->description`, and `info->linear` across calls even if `flags` changed (e.g., `APNG_LINEAR_BLEND` or `PNG_IGNOREGAMMA` toggled).
  3. **Post-`IDAT` duplicate `PLTE` chunk placed in `info->ancillary` (`line 459`)**:
     `std::vector<BYTE>& chunks = (seen_idat && (memcmp(type, "PLTE", 4) != 0)) ? info->trailing : info->ancillary;` places any post-`IDAT` `PLTE` chunk into `info->ancillary` (before `IDAT`), injecting two `PLTE` chunks into every synthesized frame PNG and causing `libpng` to abort with `"Too many PLTE chunks"`.
  4. **Multi-frame APNG save drops `FIMD_COMMENTS`, `FIMD_XMP`, and Exif `DateTime` (`lines 929-945`)**:
     `CompressFrame` only writes `pHYs` and `iCCP`/`sRGB`, dropping `FIMD_COMMENTS`, `FIMD_XMP`, and `DateTime` whenever a multi-frame or animation-tagged APNG is saved.

---

#### [MNG-01] `MNGHelper.cpp` & `PluginMNG.cpp`: Invalid JNG save for 8-bit `FIC_MINISWHITE`, `FIF_LOAD_NOPIXELS` BPP mismatch on JNG with alpha, unconditional `SwapLong` in `mng_WriteJNG`, and global `bKGD` 8-bit shift bug

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported High).
>
> 1. Min-is-white saved as JNG: **confirmed**. The `JHDR` says colour type 10 over a 1-component JPEG (pre-existing: `main` does the same), and the grey profile is left out with a warning (new). FreeImage reloads the file correctly; other decoders may not.
> 2. `FIF_LOAD_NOPIXELS` on a JNG with alpha reports 24 bpp where a full load gives 32: **confirmed, pre-existing**.
> 3. `SwapLong` in `mng_WriteJNG`: **pre-existing**, big-endian only.
> 4. Top-level `bKGD` shifted `>> 8`: **not a bug**. MNG 1.0 §4.6: "In the MNG top level, all of these chunks are written as though for 16-bit RGBA PNG datastreams."

- **Severity**: **High** (Multiple Bugs in JNG/MNG)
- **Location**: `Source/FreeImage/MNGHelper.cpp:1141`, `1249`, `1347-1358`, `1387-1392`, `1407`; `Source/FreeImage/PluginMNG.cpp:501-513`, `1046-1050`
- **Root Cause & Impact**:
  1. **Saving 8-bit `FIC_MINISWHITE` to JNG produces a broken file (`MNGHelper.cpp:1347-1358`, `1407`)**:
     `mng_WriteJNG` only checks `color_type == FIC_MINISBLACK` when choosing `MNG_COLORTYPE_JPEGGRAY` (`8`). For an 8-bit `FIC_MINISWHITE` image, it sets `jng_color_type = MNG_COLORTYPE_JPEGCOLOR` (`10`, 3-channel YCbCr), rejects any attached `"GRAY"` ICC profile at line 1407, and then calls `FreeImage_SaveToMemory(FIF_JPEG, dib, ...)` which writes a **1-channel grayscale JPEG** into `JDAT`!
  2. **`FIF_LOAD_NOPIXELS` on a JNG with an alpha plane returns 24-bit/8-bit instead of 32-bit (`MNGHelper.cpp:1141`, `1249`)**:
     `mng_ReadChunks` skips the alpha channel when `header_only == TRUE` and returns the 24-bit or 8-bit `FIF_JPEG` header instead of a 32-bit header.
  3. **Unconditional `SwapLong` in `mng_WriteJNG` (`MNGHelper.cpp:1387-1392`)**:
     Uses `SwapLong` instead of `mng_SwapLong` (which is a no-op on `FREEIMAGE_BIGENDIAN`), writing little-endian `JHDR` dimensions on big-endian architectures.
  4. **Global `bKGD` in MNG shifts 8-bit values `>> 8` to `0` (`PluginMNG.cpp:501-513`, `1046-1050`)**:
     `ParseBACK` unconditionally shifts 2-byte color fields right by 8 bits (`GetWORD(&payload[0]) >> 8`). While MNG `BACK` always stores 16-bit values, PNG/MNG `bKGD` stores an 8-bit value in the low byte (`0x00RR`), so routing global `CHUNK_bKGD` through `ParseBACK` at line 1047 zeroes out the background color.

---

#### [MNG-02] `MNGHelper.cpp` & `PluginMNG.cpp`: Memory leaks on `realloc` failure, unchecked EOF on chunk CRC, advisory `BACK` treated as mandatory, 0-byte local `tRNS` breaking `libpng`, and missing `IEND` swallowing frames

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported High). Checking it turned up a High hang the report missed (see "Bugs the report missed").
>
> - 1a. `realloc` leaks: **pre-existing**, out of memory only (chunk lengths are bounded by the file size).
> - 1b. The EOF check ignores the CRC: **confirmed (differs)**. A JNG cut 0 to 2 bytes into the CRC of a chunk after `JDAT` returns NULL ("bad CRC") instead of salvaging; cuts inside the payload or after the chunk salvage. `crc_file` is uninitialised only for the first chunk and can only force a mismatch. `main` returns NULL for every cut, so this is a gap in 2decc36's salvage.
> - 2. Advisory `BACK` treated as mandatory: **confirmed (differs)**. Using an advisory background when reading is allowed (§4.3.1: "Applications can use them if they choose to"), but the writer always sets `mandatory_background = 1` from the page's background colour, so a transparent PNG with a `bKGD` chunk, re-saved as MNG, plays back opaque.
> - 3. A 0-length local `tRNS`: **not a bug**. MNG 1.0 signals inheritance only with an empty `PLTE`, and libpng ignores a 0-length `tRNS` as benign.
> - 4. A missing `IEND` swallows frames: **confirmed (differs)**. The scan runs to the next `IEND`, not to the end of the file, and the next image is lost (1 page instead of 2). Malformed files only.

- **Severity**: **High** (Memory Leak, Uninitialized Read, and Spec Bugs in MNG/JNG)
- **Location**: `Source/FreeImage/MNGHelper.cpp:868`, `949`, `969`, `978`, `1038`, `1077`; `Source/FreeImage/PluginMNG.cpp:304-335`, `501-513`, `1194`, `1251-1255`, `1576`, `1987`
- **Root Cause & Impact**:
  1. **`mng_ReadChunks` memory leaks on `realloc` failure and unchecked EOF on chunk CRC (`MNGHelper.cpp:868, 949, 969, 978, 1038, 1077`)**:
     - At lines 969, 1038, and 1077, `ptr = (BYTE*)realloc(ptr, size)` overwrites `ptr` with `NULL` if `realloc` fails, leaking the previously allocated buffer when `throw` jumps to the `catch` block.
     - At line 949, `((INT64)mLength > (INT64)(mLOF - Offset))` checks whether the chunk payload fits in the file, but omits the 4-byte CRC (`+ 4`). If a file ends inside the 4-byte CRC of a chunk, `io->read_proc(&crc_file, 1, sizeof(crc_file), handle)` at line 978 fails and leaves `crc_file` (declared uninitialized at line 868) with uninitialized stack bytes. Furthermore, if that truncated CRC belongs to a trailing metadata chunk (e.g. `tEXt` or `pHYs`) after `JDAT`, line 988 throws `NULL` and discards the already-read JPEG image in `hJpegMemory` instead of salvaging it.
  2. **Advisory `BACK` / `bKGD` treated as mandatory background (`PluginMNG.cpp:501-513, 1576, 1987`)**:
     `ParseBACK` ignores `payload[6]` (the MNG `mandatory_background` flag), setting `info->has_background = TRUE` even for advisory backgrounds (`payload[6] == 0`). This causes `MNG_PLAYBACK` (`FillBackground`, line 1576) and `Save` (`WriteMNG`, line 1987, which writes `back[6] = 1`) to replace transparent canvas backgrounds with an opaque background.
  3. **0-length local `tRNS` chunk copied verbatim into standalone PNG stream (`PluginMNG.cpp:1194, 1251-1255`)**:
     MNG 1.0 (Section 4.2.3) allows an embedded PNG to contain a 0-length `tRNS` chunk to reference the global `tRNS` chunk (just like a 0-length `PLTE` chunk). `BuildPNGStream` handles `chunk.length == 0` for `CHUNK_PLTE` (`line 1226`), but for `CHUNK_tRNS` it sets `has_local_trns = TRUE` even when `chunk.length == 0` and copies the 0-byte `tRNS` chunk into the standalone PNG stream, which `libpng` rejects as an invalid `tRNS` chunk length.
  4. **`ScanEmbeddedStream` consumes remainder of MNG file if an embedded image lacks `IEND` (`PluginMNG.cpp:304-335`)**:
     If a malformed MNG has an embedded `IHDR` + `IDAT` (`has_data = TRUE`) that is missing its `IEND` chunk before subsequent MNG chunks (`FRAM`, `IHDR`, `MEND`), `ScanEmbeddedStream` seeks past all subsequent chunks until EOF and sets `*out_length = file_length - start`, swallowing all remaining frames into a single frame.

---

#### [GIF-01] `PluginGIF.cpp`: Interlaced GIF `Save` drops rows when `height <= 4`, `Load` has an off-by-one out-of-bounds write on `transparent_color == size`, and big-endian `Save` passes byte-swapped `width` to `CompressStart`

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported High). Regression: **no**.
>
> 1. Interlaced saves of 2 to 4 row images drop rows: **confirmed, pre-existing** (`main`'s `Save` has the same `if`; `qpv` fixed only `Load`). It is reachable when the "Interlaced" animation tag is 1, e.g. when a loaded interlaced GIF is saved again: heights 2, 3 and 4 keep one row; heights 1, 5 and 6 are fine.
> 2. `transparent_color <= size` writes out of bounds: **false**. `table` is `BYTE[256]`, `size` is at most 256 and only `size` entries are used. The only effect: a transparent index equal to the palette size gives an all-opaque table.
> 3. A byte-swapped width reaches `CompressStart`: **confirmed, big-endian only**, pre-existing.

- **Severity**: **High** (Regression: Corrupted Interlaced GIFs & OOB Write)
- **Location**: `Source/FreeImage/PluginGIF.cpp:1408-1415`, `1484-1489`, `1693`, `1703-1726`
- **Code**:
  ```cpp
  // PluginGIF.cpp:1721 (Save)
  if (interlaced) {
      y += g_GifInterlaceIncrement[interlacepass];
      if (y >= output_height && ++interlacepass < GIF_INTERLACE_PASSES) {
          y = g_GifInterlaceOffset[interlacepass];
      }
  }
  ```
  ```cpp
  // PluginGIF.cpp:1408 (Load)
  int size = 1 << bpp;
  if (transparent_color <= size) {
      BYTE table[256];
      memset(table, 0xFF, size);
      table[transparent_color] = 0;
      FreeImage_SetTransparencyTable(dib, table, size);
  }
  ```
- **Root Cause & Impact**:
  1. **Interlaced GIF `Save` drops rows when `output_height` is `2`, `3`, or `4` (`lines 1721-1725`)**:
     In `Load` (`line 1262`), pass advancement was fixed to use `while (y >= height && ++interlacepass < GIF_INTERLACE_PASSES)`, but `Save` (`line 1723`) still uses `if`!
     When `output_height == 2`, `3`, or `4`:
     - Pass 0 writes `y = 0`, then `y += 8` (`y = 8 >= output_height`).
     - The `if` advances `interlacepass` to `1` and sets `y = g_GifInterlaceOffset[1] = 4`.
     - Because `y = 4 >= output_height`, the outer `while (y < output_height)` loop **immediately terminates** without advancing to pass 2 (`y = 2`) or pass 3 (`y = 1`, `3`), writing only 1 scanline into the GIF stream!
  2. **Off-by-one out-of-bounds write (`line 1409`)**:
     `if (transparent_color <= size)` instead of `< size` writes `table[size] = 0` one byte past the `size` initialized entries when `bpp == 1` (`size = 2, transparent_color = 2`) or `bpp == 4` (`size = 16, transparent_color = 16`).
  3. **Big-endian `Save` bug (`lines 1484-1489, 1693`)**:
     At line 1487 (`#ifdef FREEIMAGE_BIGENDIAN`), `SwapShort(&width)` mutates local variable `width` in-place and never swaps it back; at line 1693, `stringtable->CompressStart(bpp, width)` receives the byte-swapped `width`!

---

#### [WEBP-01] `PluginWebP.cpp`: Still WebP `Save` (`!as_frame`) does not convert 32-bit CMYK or non-RGB ICC profiles; single-frame animation metadata is stripped by `WebPMuxAssemble`; 1-page multipage WebP with offset saved as animation; misleading warning on `VP8X`-only truncated WebP

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported High).
>
> 1. A still WebP save writes 32-bit CMYK as BGRA: **confirmed, pre-existing** (`main`'s still path is the same). K becomes alpha and the CMYK profile is written. It needs an explicit `*_CMYK` load flag. Untagged pages of a multi-page WebP take the same path; only pages with animation tags are converted.
> 2. One full-canvas frame loses `ANIM`/`ANMF`: true, but libwebp's `MuxCleanup` does that on purpose, and one frame's duration and loop count show nothing.
> 3. One page with an offset is written as an animation: **not a bug**. A still image cannot hold an offset or a larger canvas; the file reloads as one page with all its tags.
> 4. A warning on a cut `VP8X`-only file: **false**. libwebp refuses a mux without an image, so only the parsing error is printed.

- **Severity**: **High** (Bug: Corrupted CMYK WebP Save & Animation Metadata Bugs)
- **Location**: `Source/FreeImage/PluginWebP.cpp:307-312`, `762-825`, `1076-1118`, `1142-1191`, `1222-1234`
- **Root Cause & Impact**:
  1. **Still WebP `Save` on 32-bit CMYK (`lines 1144-1157` vs `1161-1191`)**:
     In the animation path (`as_frame == TRUE`, lines 1161–1191), `Save` checks `PNG_IsEmbeddableProfile(..., TRUE)` and calls `ConvertToFileProfile` so that a 32-bit `FIC_CMYK` bitmap (or a bitmap with a non-RGB ICC profile) is converted to RGB before encoding. In the still-image path (`!as_frame`, lines 1144–1157), `Save` neither checks `FreeImage_GetColorType(dib) == FIC_CMYK` nor calls `ConvertToFileProfile`: a 32-bit CMYK `FIBITMAP` is encoded directly as raw BGRA pixels with its CMYK ICC profile attached to the WebP container!
  2. **Single-frame WebP animation tags stripped (`lines 1076-1118`)**:
     When saving a single-page WebP that carries `FrameLeft`/`FrameTop`/`FrameTime`/`Loop` metadata with `FrameLeft == 0`, `FrameTop == 0`, and frame dimensions equal to canvas dimensions, `Save` uses `WebPMuxSetAnimationParams` + `WebPMuxPushFrame`. Inside `WebPMuxAssemble` (`Source/LibWebP/src/mux/muxedit.c:565-587`), `MuxCleanup` converts any 1-frame mux whose frame covers the canvas back into a plain non-animated WebP, discarding `ANIM`/`ANMF` (`Loop`, `FrameTime`, `DisposalMethod`, and `BlendMethod`)!
  3. **1-page multipage WebP with offset saved as animation despite comment (`lines 1076-1118, 1222-1234`)**:
     If a 1-page multipage WebP is saved with an untagged image that has `FrameLeft > 0` or `LogicalWidth > width`, `state->pending` is held with the comment `// hold the first frame: alone it is a still image`, yet `WebP_FinishAnimation` unconditionally calls `WebP_FlushPending(state)` (pushing it as an `ANMF` animation frame) instead of checking `state->out_pages == 1`.
  4. **Truncated WebP containing only non-image chunks (`VP8X`/`ICCP`) emits misleading partial-image warning in `Open` (`lines 307-312, 762-825`)**:
     If a WebP file is cut off after `VP8X` (before `VP8 ` or `VP8L`), `WebP_TrimToWholeChunks` and `WebPMuxCreate` succeed on the image-less container, emitting `"Warning: the file is cut short or damaged; the frames it holds are kept"` in `Open` before `Load` fails with `NULL`.

---

### 4. Format Plugins — Part 2 (`JXR`, `JXL`, `HEIF`, `AVIF`, `EXR`, `RAW`, `JPEG`, `Exif`, `J2K`/`JP2`, `PNM`, `HDR`, `TARGA`, `DDS`, `XBM`, `IFF`, `PCX`, `RAS`, `WBMP`)

---

#### [JXR-01] `PluginJXR.cpp`: `CopyBandedPixels` passes current stream position `image_pos` instead of `uImageOffset` to `IsWindowed`, breaking windowed JXR banded decoding

> **Verification (2026-10-18): Not a bug (part 6 fixed since).** Verified severity **None** (reported High).
>
> - 1 to 4. The quoted call does not exist. The real one is `IsWindowed(pWS, image_pos) || (bHasAlpha && IsWindowed(pWS, uAlphaOffset))` (PluginJXR.cpp:1783), and the stream sits at the codestream start by design (`ReadContainer` ends with `SetPos(uImageOffset)`, and jxrlib's own `Copy` relies on it). A signature mismatch returns TRUE, which gives the safe full-height band, not FALSE. Instrumented: a windowed file shows `GetPos == uImageOffset`, a full-height band and all 8 orientations exact; forcing FALSE makes the same file fail. The bands are 128 rows, not 64.
> - 5. `(UINT64)width * height * bpp / 8` can wrap: true but harmless. When it wraps the raster is at least 2^61 bytes, and the allocation right after is refused.
> - 6. An out-of-memory `cmyk_alpha` plane is ignored: **fixed since** by ce201d8 (JPEG XR and JPEG XL loads now fail; tested).

- **Severity**: **High** (Regression: Broken Windowed JXR Decoding & 64-bit Overflow)
- **Location**: `Source/FreeImage/PluginJXR.cpp:1750-1792`, `1909`, `1980-1988`
- **Code**:
  ```cpp
  // PluginJXR.cpp:1780
  size_t image_pos = 0;
  if (pWS->GetPos(pWS, &image_pos) == WMP_errSuccess && !IsWindowed(pWS, image_pos)) {
      if ( pDecoder->WMP.wmiSCP_Alpha.uAlphaMode == 2 ) {
          if (IsWindowed(pWS, (size_t)pDecoder->WMP.wmiDEMisc.uAlphaOffset)) {
              band_height = height;
          }
      }
      ...
  ```
- **Root Cause & Impact**:
  1. `IsWindowed(pWS, offset)` (`lines 1750-1767`) seeks to `offset`, checks for the 8-byte `"WMPHOTO\0"` codestream signature, and inspects the `bWindowing` bit in the codestream header.
  2. However, at line 1781, `CopyBandedPixels` passes `image_pos` (the current stream position *after* `pDecoder->Initialize` has already parsed the main and alpha codestream headers!) instead of `(size_t)pDecoder->WMP.wmiDEMisc.uImageOffset`!
  3. Because `image_pos` does not point to the start of the `"WMPHOTO\0"` codestream, `memcmp(buf, "WMPHOTO", 8)` inside `IsWindowed(pWS, image_pos)` **always fails and returns `FALSE`**!
  4. As a result, any windowed JXR image with `height > 64` that takes the `CopyBandedPixels` path is decoded in 64-row bands instead of full-frame (`band_height = height`), triggering the jxrlib `cRow == 0` windowed band failure that `IsWindowed` was written to prevent!
  5. Additionally, at line 1909, `PlausibleImageSize(io, handle, base_pos, (UINT64)width * height * bpp / 8, 550)` can overflow 64-bit `UINT64` before calling `PlausibleImageSize` when `width` and `height` are large 32-bit integers and `bpp` is up to 128.
  6. At lines 1980-1988 (and `PluginJXL.cpp:1308-1317`), if `FreeImage_Allocate(width, height, 8 * bytes)` fails (OOM) while extracting the `cmyk_alpha` plane for `JXR_CMYK | FIF_LOAD_DISPLAY_ICC`, the error is silently ignored instead of returning `NULL`.
- **Recommended Fix**:
  Replace `IsWindowed(pWS, image_pos)` at line 1781 with `IsWindowed(pWS, (size_t)pDecoder->WMP.wmiDEMisc.uImageOffset)`.

---

#### [PNM-01] `PluginPNM.cpp`: Raw PBM/PGM/PPM (`P4`, `P5`, `P6`) bypasses `y > 0` and `PlausibleImageSize` on EOF, allocating multi-GB bitmaps from a 15-byte header

> **Verification (2026-10-18): Confirmed (details differ), pre-existing.** Verified severity **Medium** (reported High). Regression: **no** (`main` allocates the same and returns the same blank image, without the warning; HEAD is also faster on such files).
>
> - No PNM variant checks the size before allocating: "unlike ASCII" is wrong (a 19-byte P3 header also allocates 2.6 GB before it fails). Raw files then keep the bitmap at EOF on purpose ("raw data always kept what it read", a156574).
> - `P6\n30000 30000\n255\n` (19 bytes, not 15) takes 2.6 GB and returns a blank 30000×30000 image with "0 of 30000 rows"; P5 takes 887 MB and P4 (15 bytes) 118 MB.
>
> Fix hint: bound raw rasters by the bytes left in the file before allocating, as BMP does since 0fe40ec, and return NULL when nothing was read.
>
> **Fixed (2026-10-19).** 635a3c4: before allocating, a raster over 64 MB needs at least 1/64 of its raw bytes in the file (1/128 for ASCII), as BMP does; every crafted header stays at about 8 MB. A small header without data still gives a blank image, as decided for PLUG-01 (1).

- **Severity**: **High** (Bug: DoS Allocation & 0-Row Blank Image on Truncated Header)
- **Location**: `Source/FreeImage/PluginPNM.cpp:365`, `571-592`
- **Code**:
  ```cpp
  // PluginPNM.cpp:571
  } catch (const char *text) {
      ...
      if (dib && reading_pixels && ((text == PNM_EOF) || ((y > 0) && PlausibleImageSize(io, handle, base_pos, (UINT64)width * height * bpp / 8, 1)))) {
          PartialImageWarning(s_format_id, y, height);
          return dib;
      }
  ```
- **Root Cause & Impact**:
  1. Unlike ASCII PNM (`P1`/`P2`/`P3`), the raw PNM (`P4`/`P5`/`P6`) loading branches do not call `PlausibleImageSize` before `FreeImage_AllocateHeader` (`line 365`).
  2. When reading pixels, raw PNM throws `PNM_EOF` on EOF. In the `catch` handler at line 579, the condition is `((text == PNM_EOF) || ((y > 0) && PlausibleImageSize(...)))`.
  3. Because `text == PNM_EOF` is on the left side of `||`, raw PNM files bypass **both** `y > 0` and `PlausibleImageSize`!
  4. A 15-byte truncated file `P6\n30000 30000\n255\n` allocates a 2.7 GB `FIBITMAP`, fails on row `y = 0` with `PNM_EOF`, logs `PartialImageWarning(s_format_id, 0, 30000)`, and returns a 2.7 GB all-black bitmap instead of `NULL`!
- **Recommended Fix**:
  Require `(y > 0) && PlausibleImageSize(...)` for both `PNM_EOF` and ASCII EOF errors, and check `PlausibleImageSize(..., 1)` before allocating raw PNM (`P4`/`P5`/`P6`) bitmaps.

---

#### [HDR-01] `PluginHDR.cpp`: Calling `rgbe_ReadPixels_RLE(..., width, 1)` per row breaks flat HDR files when pixel 0 of any row has `R=2, G=2, B<128` and masks mid-image `malloc` OOM as a partial image

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported High).
>
> 1. Reading row by row breaks flat files whose rows start with (2, 2, B < 128): **false premise**. `main` also reads one row per call, and Radiance's own `freadcolrs` tests every scanline the same way; no conforming writer produces such a pixel (the largest mantissa is at least 128). HEAD keeps the rows before the bad one, where `main` returned NULL.
> 2. Out of memory mid-file is reported as a cut file: **confirmed, new on `qpv`**. Failing the per-row `malloc` on row 1 or 2 returns a partial image where `main` returned NULL. This contradicts the out-of-memory-fails rule of 59ea9d5 and ce201d8.

- **Severity**: **High** (Regression: False Format Error on Valid Flat HDRs & Masked OOM)
- **Location**: `Source/FreeImage/PluginHDR.cpp:406-428`, `678-733`
- **Code**:
  ```cpp
  // PluginHDR.cpp:718
  for (unsigned y = 0; y < height; y++) {
      FIRGBF *scanline = (FIRGBF*)FreeImage_GetScanLine(dib, height - 1 - y);
      if (!rgbe_ReadPixels_RLE(io, handle, (float*)scanline, width, 1)) {
          if (y == 0) {
              throw FI_MSG_ERROR_PARSING;
          }
          PartialImageWarning(s_format_id, y, height);
          break;
      }
  }
  ```
- **Root Cause & Impact**:
  In `origin/main`, `rgbe_ReadPixels_RLE` was called once with `num_scanlines = height`. When pixel 0 of row 0 was not a new-RLE marker (`rgbe[0] != 2 || rgbe[1] != 2 || (rgbe[2] & 0x80)`), it fell back to `rgbe_ReadPixels(..., scanline_width * num_scanlines)` for the entire rest of the image in one call.
  In `qpv`, `Load` calls `rgbe_ReadPixels_RLE(io, handle, (float*)scanline, width, 1)` once per scanline (`num_scanlines = 1`):
  1. For an uncompressed (flat) or old-RLE Radiance `.hdr` file, `rgbe_ReadPixels_RLE` now checks `(rgbe[0] != 2) || (rgbe[1] != 2) || (rgbe[2] & 0x80)` at pixel 0 of **every** row `y`! If pixel 0 of any row $y$ happens to have mantissa bytes `R == 2`, `G == 2`, and `B < 128` (and `(B << 8 | E) != width`), `rgbe_ReadPixels_RLE` mistakes that pixel for a corrupt RLE header, logs `"RGBE bad file format: wrong scanline width"`, and aborts (or truncates) the image!
  2. `rgbe_ReadPixels_RLE` allocates `malloc(sizeof(BYTE) * 4 * scanline_width)` on every single row (`line 423`). If `malloc` fails (OOM) on row $y > 0$, `rgbe_ReadPixels_RLE` returns `FALSE`, and `Load` treats the OOM as a truncated file (`PartialImageWarning`) instead of returning `NULL`!
- **Recommended Fix**:
  Pre-allocate the 4-byte scanline buffer once before the loop, and once a file is determined to be non-new-RLE on row 0, call `rgbe_ReadPixels` directly for all subsequent rows.

---

#### [EXR-01] `PluginEXR.cpp`: `CheckDataWindow` rejects truncated scanline EXR files before `ReadEachBlock` can salvage them

> **Verification (2026-10-18): Confirmed.** Verified severity **Low** (reported Medium). Regression: **no** (`main` refuses every cut EXR).
>
> The `chunks * 16 <= stream_bytes` bound (PluginEXR.cpp:205) holds only for complete files. A 1×4000 FLOAT `EXR_NONE` file cut at 10 to 75 % of its data is refused ("chunk table alone needs 64000 bytes") and salvaged at 90 %; a 2×4000 RGB file is refused below 50 %; ZIP-compressed and 64-pixel-wide files salvage at every cut.
>
> Fix hint: bound by the header plus 8 bytes per chunk, since the offset table precedes all chunk data.

- **Severity**: **Medium** (Bug in Partial-EXR Recovery)
- **Location**: `Source/FreeImage/PluginEXR.cpp:200-210`, `590-615`
- **Root Cause & Impact**:
  `ReadEachBlock` was added in `qpv` to salvage partial scanline EXR files that were cut off mid-stream. However, `CheckDataWindow` (`line 205`) checks `if ((UINT64)chunks * 16 > stream_bytes) throw Iex::InputExc(...)`. Because `chunks` is the total number of scanline blocks in the full image height, if a narrow scanline EXR (e.g., `width <= 4`) is truncated mid-file, `stream_bytes` can be smaller than `chunks * 16`, throwing `InputExc` before `ReadEachBlock` is ever reached.

---

#### [RAW-01] `PluginRAW.cpp`: `LibRaw_freeimage_datastream::gets` lacks NUL termination on long lines and fails on EOF without `\n`; `scanf_one` fails on NUL padding

> **Verification (2026-10-18): Not a bug in effect.** Verified severity **None** (reported High).
>
> 1. `gets` can leave a full buffer without a NUL: the deviation is real and **pre-existing** (`main` has the same `memset` and up-to-length loop; the quoted code is not HEAD's), but no LibRaw call site reads past it. The ten `fgets` callers terminate their buffers or compare with `strncmp` and short literals, and `parse_mos` never calls it.
> 2. At EOF without a newline: `NULL` is returned, but the buffer keeps what was read. Only `parse_rollei` checks, and a valid Rollei header ends with a newline.
> 3. `scanf_one` and NUL padding: **false**. None of LibRaw's own streams skips a leading NUL; FreeImage's is more lenient than theirs.

- **Severity**: **High** (Buffer Over-Read / Parsing Bug)
- **Location**: `Source/FreeImage/PluginRAW.cpp:168-196`
- **Code**:
  ```cpp
  char* gets(char *buffer, int length) {
      if (!buffer || (length <= 0)) return NULL;
      for (int i = 0; i < length; i++) {
          if (_io->read_proc(&buffer[i], 1, 1, _handle) != 1) {
              return NULL;
          }
          if (buffer[i] == '\n') {
              if ((i + 1) < length) {
                  buffer[i + 1] = 0;
              }
              return buffer;
          }
      }
      return buffer;
  }
  ```
- **Root Cause & Impact**:
  1. Standard `fgets(s, n, stream)` reads at most `n - 1` characters and **always** NUL-terminates `s`. `LibRaw_freeimage_datastream::gets` reads up to `length` characters and only writes a NUL byte if `buffer[i] == '\n'` **and** `(i + 1) < length`! If a line is `>= length - 1` bytes long, `buffer` is returned **without a NUL terminator**, causing `strstr`/`sscanf`/`strcmp` inside LibRaw (`parse_mos`, etc.) to read past the stack buffer!
  2. If the last line of the stream ends at EOF without a trailing `\n`, `read_proc` returns `0` and `gets` discards the characters already read and returns `NULL`.
  3. In `scanf_one` (`line 188`), `while ((c > 0) && isspace(c))` stops when `c == 0`, whereas Leaf/Mamiya MOS metadata blocks pad fields with NUL (`'\0'`) bytes (which LibRaw's native `scanf_one` skips via `c == 0 || isspace(c)`).
- **Recommended Fix**:
  Read at most `length - 1` bytes in `gets`, always NUL-terminate `buffer`, return `buffer` on EOF if `i > 0`, and skip `c == 0` in `scanf_one`'s leading loop.

---

#### [AVIF-01] `PluginAVIF.cpp`: `ApplyTransforms` silently returns an uncropped/unrotated image on OOM; `LoadHeader` omits Exif/XMP metadata and multi-page frame seek

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported Medium). New `qpv` code.
>
> 1. Out of memory in the `clap` or `irot` step returns the untransformed image: **confirmed**, though not silently (a "Could not apply the ... rotation" warning is printed). With the rotation's allocation refused, a 256×512 image comes back 512×256, while a header-only load reports the rotated size. This contradicts the out-of-memory-fails rule.
> 2. `LoadHeader` omits metadata and the page seek: **false**. `AttachMetadata` is called in `LoadHeader` (PluginAVIF.cpp:627) and has been since the plugin's first commit; header-only and decoded pages agree in 44 page pairs (size, type, ICC, Exif, XMP, CICP, FrameTime).

- **Severity**: **Medium** (Bug in New `PluginAVIF`)
- **Location**: `Source/FreeImage/PluginAVIF.cpp:371-411`, `605-631`
- **Root Cause & Impact**:
  1. In `ApplyTransforms` (`lines 382-387, 396-401`), if `FreeImage_Copy` (`clap` crop) or `FreeImage_Rotate` (`irot` rotation) returns `NULL` due to out-of-memory, `ApplyTransforms` logs an error message and leaves `dib` pointing to the uncropped/unrotated image instead of unloading `dib` and returning `NULL`.
  2. In `LoadHeader` (`FIF_LOAD_NOPIXELS`, `lines 605-631`), `AttachMetadata(decoder->image, dib)` is never called (so Exif and XMP metadata are missing on header-only loads) and `decoder->image` is read without seeking to `page` on multi-page AVIFs.

---

#### [HEIF-01] `PluginHEIF.cpp`: Animated HEIF (`heics`) sequences ignore `nclx` (`colr`) CICP color descriptions

> **Verification (2026-10-18): Confirmed.** Verified severity **Low** (reported Medium). New `qpv` code.
>
> `ReadSampleEntry` reads a `colr` box only when it is `prof` or `rICC`, and no code attaches CICP to sequence frames. A sequence whose sample entry carries nclx 12/13/6 (Display P3) loads with no CICP tag and no profile, so P3, BT.2020, PQ and HLG frames are taken as sRGB. Stills get theirs (the still path is `AttachCICP`; there is no `LoadStill`). libheif's track decoder ignores that box too.

- **Severity**: **Medium** (Bug in New `PluginHEIF`)
- **Location**: `Source/FreeImage/PluginHEIF.cpp:1170-1185` vs `1717-1765`
- **Root Cause & Impact**:
  For still HEIF images (`LoadStill`, `lines 1717-1765`), `PluginHEIF` reads both ICC (`prof`/`rICC`) and CICP (`nclx`) color descriptions via `heif_image_handle_get_raw_color_profile` and `heif_image_handle_get_nclx_color_profile`. However, for animated HEIF image sequences (`ReadSampleEntry`, `lines 1170-1185`), the `"colr"` box parser only handles `"prof"` and `"rICC"` and ignores `"nclx"`.

---

#### [JPEG-01] `PluginJPEG.cpp` & `Exif.cpp`: `Save` leaks `cinfo` on `malloc` failure; `JPEG_GREYSCALE` attaches RGB/CMYK ICC profiles to 8-bit greyscale output; `jpeg_read_exif_dir` aborts on truncated IFD1; `ReplaceWithRotated` overwrites swapped DPI

> **Verification (2026-10-18): Partly.** Verified severity **Low** (reported Medium).
>
> 1. `Save` leaks the libjpeg object when a `malloc` fails: **confirmed, pre-existing** (`Save` is the same as `main`'s; about 99 KB per failed save). Also, not in the report: a failing output write longjmps past `free(target)` (about 3 KB per save, reachable on a full disk), same in `main`.
> 2. `JPEG_GREYSCALE` keeps an RGB or CMYK profile. An embedded RGB profile: **pre-existing** (same in `main`). The Adobe RGB profile made for Exif ColorSpace 2 (`IsExifAdobeRGB`; there is no `SynthesizeExifProfile`) is attached too: **new on `qpv`**. CMYK: unreachable, `JPEG_GREYSCALE` on a CMYK file fails on both.
> 3. `jpeg_read_exif_dir` returns FALSE on a bad IFD1: **not a bug in effect**. The IFD0, Exif, GPS and Interop tags are stored by then and every caller ignores the return value, so only the thumbnail is lost, as the comment says. Same in `main`.
> 4. `ReplaceWithRotated` undoes Rotate's DPI swap: **false premise**. `FreeImage_Rotate` never swaps DPI (there is no `DotsPerMeter` in ClassicRotate.cpp, at HEAD or in `main`), and both builds give the same, unswapped values. What is real: a 90° or 270° Exif rotation does not swap the X and Y resolution (pre-existing).

- **Severity**: **Medium** (Regressions in JPEG & Exif)
- **Location**: `Source/FreeImage/PluginJPEG.cpp:776-787`, `1261-1264`, `1319-1324`, `1753-1758`; `Source/Metadata/Exif.cpp:803-808`, `1034-1045`
- **Root Cause & Impact**:
  1. **`PluginJPEG::Save` memory leak (`PluginJPEG.cpp:1753-1758`)**: When `malloc(target)` throws `"Error: Out of memory"` at lines 1636, 1659, 1695, or 1723, `catch (const char *text)` returns `FALSE` without calling `jpeg_destroy_compress(&cinfo)`, leaking all memory pools allocated by `libjpeg`.
  2. **`JPEG_GREYSCALE` attaches RGB/CMYK ICC profile to 8-bit greyscale DIB (`PluginJPEG.cpp:776-787`, `1261-1264`, `1319-1324`)**: When a color RGB or CMYK JPEG is decoded with `JPEG_GREYSCALE`, `read_icc_profile` and `SynthesizeExifProfile` still attach the 3-channel RGB or 4-channel CMYK ICC profile to the 1-channel 8-bit greyscale `dib`.
  3. **`jpeg_read_exif_dir` aborts all Exif loading on truncated IFD1 (`Exif.cpp:803-808`)**:
     ```cpp
     if (remaining >= dwLength) {
         // bad IFD1 directory, ignore it
         return FALSE;
     }
     ```
     Despite the comment `"bad IFD1 directory, ignore it"`, it returns `FALSE` instead of `TRUE` (and uses `>=` instead of `>`), causing the caller to treat the entire Exif block as invalid when only the optional IFD1 thumbnail directory is out of bounds.
  4. **`ReplaceWithRotated` overwrites swapped X/Y DPI (`Exif.cpp:1034-1045`)**:
     `FreeImage_Rotate(*dib, 90)` or `270` (`ClassicRotate.cpp:870-871`) already clones metadata and swaps `DotsPerMeterX` and `DotsPerMeterY` on `rotated`. Then `ReplaceWithRotated` calls `FreeImage_CloneMetadata(rotated, *dib)` a second time (`line 1039`), which copies `ResolutionX`/`ResolutionY` from the unrotated `*dib` back onto `rotated`, undoing the DPI swap!

---

#### [J2K-01] `J2KHelper.cpp`: `SampleScale::operator()` fails to clamp out-of-range DWT values when `m_table.empty()`, causing integer wrap-around

> **Verification (2026-10-18): Not a bug.** Verified severity **None** (reported Medium).
>
> OpenJPEG 2.5.4 clamps every sample after the inverse wavelet transform, 5-3 and 9-7 alike, to [0, 2^prec - 1] or to the signed range (`opj_tcd_dc_level_shift_decode`, tcd.c:2328-2352). Lossy encodes at rates 8, 30 and 100 (8 and 16-bit, signed and unsigned, 12-bit scaled and unscaled) never left the range, and FreeImage's samples matched OpenJPEG's in 18 of 18 cases. `main` casts the same way.

- **Severity**: **Medium** (Bug: Pixel Wrap-Around on Wavelet Overshoot)
- **Location**: `Source/FreeImage/J2KHelper.cpp:184-207`
- **Code**:
  ```cpp
  OPJ_INT32 operator()(OPJ_INT32 value) const {
      if (m_table.empty()) return value;
      if (value <= 0) return 0;
      if (value >= (OPJ_INT32)m_table.size()) return m_table.back();
      return m_table[value];
  }
  ```
- **Root Cause & Impact**:
  When `prec == 8`, `prec == 16`, or `J2K_UNSCALED` is used, `m_table` is empty (`m_table.empty() == true`), and `SampleScale::operator()` returns `value` without clamping it to `[0, (1 << prec) - 1]`. Because OpenJPEG's 9-7 irreversible wavelet transform (or slightly damaged codestreams) can produce `OPJ_INT32` values such as `-1` or `256` (for 8-bit) / `65536` (for 16-bit), the caller (`(T)scale(src[x])`) casts `-1` to `BYTE` (`255`) or `256` to `BYTE` (`0`), causing bright white pixels to wrap to black and dark black pixels to wrap to white!
- **Recommended Fix**:
  Store `m_max = (1 << dst_bits) - 1` in `SampleScale` and always clamp `value` to `[0, m_max]`.

---

#### [TGA-01] `PluginTARGA.cpp`: `Save` swaps channels/endianness on `dib` instead of `thumbnail` when writing a TGA 2.0 postage stamp

> **Verification (2026-10-18): Confirmed, but only on non-default builds; pre-existing.** Verified severity **Low**, and **None** on default builds (reported High).
>
> `SwapRedBlue32(dib)` / `swapShortPixels(dib)` act on the caller's dib after the main image is written and leave the thumbnail unswapped, but that code is compiled only under `FREEIMAGE_COLORORDER_RGB` or on big-endian hosts. RGB order is the default only on big-endian hosts and no makefile or project defines either macro, so x86-64 and ARM64 Linux, Windows and macOS builds are unaffected ("macOS/Linux RGB builds" is wrong). An 8-bit dib with a 24-bit thumbnail cannot occur (equal bpp is required). Same in `main`; it was left open by decision in the earlier qpv-vs-main review.

- **Severity**: **High** (Bug: Corrupts Caller's `dib` and Writes Wrong Thumbnail Order)
- **Location**: `Source/FreeImage/PluginTARGA.cpp:1668-1673`
- **Code**:
  ```cpp
  #if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB
      if (th_bpp == 24 || th_bpp == 32) {
          SwapRedBlue32(dib);
      }
  #endif
  #ifdef FREEIMAGE_BIGENDIAN
      if (th_bpp == 16) {
          swapShortPixels(dib);
      }
  #endif
  ```
- **Root Cause & Impact**:
  At lines 1668 and 1673, `SwapRedBlue32(dib)` and `swapShortPixels(dib)` are called on `dib` instead of `thumbnail`! On `FREEIMAGE_COLORORDER_RGB` (macOS/Linux RGB builds) or `FREEIMAGE_BIGENDIAN`:
  1. If `dib` has a different BPP than `th_bpp` (e.g., `dib` is 8-bit palettized and `thumbnail` is 24-bit), `SwapRedBlue32(dib)` fails, leaving `thumbnail` unswapped; if `dib` is 24/32-bit, `SwapRedBlue32(dib)` mutates the caller's main `dib` in-place while leaving `thumbnail` unswapped!
- **Recommended Fix**:
  Pass `thumbnail` instead of `dib` to `SwapRedBlue32(thumbnail)` and `swapShortPixels(thumbnail)`.

---

#### [PLUG-01] Smaller Plugins (`PluginIFF`, `PluginPCX`, `PluginTARGA`, `PluginRAS`, `PluginWBMP`, `PluginDDS`, `PluginXBM`): 0-row partial images on EOF, missing `PlausibleImageSize`, and XBM dimension/padding bugs

> **Verification (2026-10-18): Partly.** Verified severity **Medium** (reported Medium). Mostly **pre-existing**.
>
> 1. A file that ends right after its header returns a blank image (IFF, PCX, TGA, RAS, WBMP): **true, pre-existing and deliberate** (f3b5313: "still returned when it holds no row at all, as before"). `main` returns the same bitmaps, without a warning.
> 2. No size bound before allocating: **confirmed, broader, pre-existing**. RAS (raw and RLE), IFF (raw and RLE), WBMP and DDS DXT allocate from the header: 48 bytes give 391 MB (IFF), 32 bytes 519 MB (RAS), 8 bytes 519 MB (WBMP) and 136 bytes 519 MB (DXT); IFF can reach about 12.9 GB. PCX and TGA are bounded at HEAD, where `main` allocated 261 MB and 1 GB.
>    DXT `width &= ~3`: **confirmed (differs), pre-existing**. 1×1 to 3×3 DXT files return NULL, and, worse, any width that is not a multiple of 4 decodes wrong (block rows shift: a 6×8 file comes out 4×8 with wrong rows).
> 3. XBM: (a) the dimension checks are **pre-existing**; zero and negative sizes return NULL at HEAD (`main` hangs on some). A wrapping `int` product combined with a 4 GB allocation was not tested and is likely real. (b) The X10 row count: **confirmed (differs)**, `qpv` code. Valid files load; cut files with padding report too few rows, or NULL after one row (`main` hangs on them).
>
> **Fixed (2026-10-19).** Part 2: 80b1503 bounds RAS, IFF and WBMP before allocating; d590b03 bounds DXT the same way and decodes DXT images of any size (243 crafted sizes exact, Pillow-encoded files as Pillow decodes them). Part 3: 4f20f92 refuses zero and negative XBM sizes, sizes the buffer in 64 bits with the same bound, and counts the X10 rows as they are stored. Part 1 stays as decided.

- **Severity**: **Medium** (Bugs in Partial-Image Handling & Input Validation)
- **Location**:
  - `Source/FreeImage/PluginIFF.cpp:545-555`, `597`
  - `Source/FreeImage/PluginPCX.cpp:518-524`
  - `Source/FreeImage/PluginTARGA.cpp:959-975`, `1354-1359`
  - `Source/FreeImage/PluginRAS.cpp:419-424`, `514-518`
  - `Source/FreeImage/PluginWBMP.cpp:259-277`
  - `Source/FreeImage/PluginDDS.cpp:796-834`
  - `Source/FreeImage/PluginXBM.cpp:153-167`, `230-233`, `284`
- **Root Cause & Impact**:
  1. **0-row blank images returned on immediate EOF**: `PluginIFF`, `PluginPCX`, `PluginTARGA`, `PluginRAS`, and `PluginWBMP` do not check `if (rows == 0) { FreeImage_Unload(dib); return NULL; }` when a stream ends immediately after the header, returning a 100% blank bitmap with `PartialImageWarning(s_format_id, 0, height)`.
  2. **Missing `PlausibleImageSize`**: `PluginRAS` (`rle == FALSE`), `PluginIFF` (`compression == 1`), `PluginWBMP`, and `PluginDDS` (`LoadDXT`) do not call `PlausibleImageSize` before allocating bitmaps from header dimensions. Additionally, `LoadDXT` (`PluginDDS.cpp:797`) rounds `width &= ~3; height &= ~3;`, turning valid 1x1, 2x2, or 3x3 DXT mipmaps into 0-dimension allocations instead of rounding block count up via `(width + 3) / 4`.
  3. **`PluginXBM.cpp` negative/zero dimensions, 32-bit overflow, and X10 row undercount (`lines 153-167, 230-233, 284`)**:
     - Line 230 only checks `if (*widthP == -1 || *heightP == -1)`, allowing negative (`<= -2`) or `0` dimensions from `#define ..._width` / `_height`, and computes `bytes_per_line * *heightP` (`line 258`) in 32-bit `int`.
     - Line 284 increments `(*rowsP)++` inside `if ((bytes == bytes_per_line - 1) && !padding)` for X10 (`version == 10`) format, which never executes when `padding == 1` (`bytes_per_line` is odd), leaving `*rowsP == 0` and failing every valid X10 XBM with `width % 16 in [1..8]`.

---

### 5. Build Scripts & Project Files

---

#### [BUILD-01] Outdated source list generators (`gensrclist.sh`, `genfipsrclist.sh`), missing `-DAVIF_ENABLE_EXPERIMENTAL_MINI=1` in `Makefile.mingw`, and missing `testToneMapping.cpp` in `Test.2017.vcxproj`

> **Verification (2026-10-18): Confirmed (details differ).** Verified severity **Medium** (reported Medium).
>
> 1. `gensrclist.sh` / `genfipsrclist.sh` are stale: **confirmed**. They name the deleted OpenEXR 2.2 directories and lack `OpenEXR/OpenEXR`, `OpenEXRCore` and `LibDeflate`. "`.cc` files are dropped" is **false**: the `egrep` pattern is unanchored. Nothing runs these scripts; `Makefile.srcs` is maintained by hand.
> 2. `Makefile.mingw` lacks `-DAVIF_ENABLE_EXPERIMENTAL_MINI=1`: **confirmed (differs)**. Every other makefile and `LibAVIF.2017.vcxproj` (not `FreeImage.2017.vcxproj`) define it. Without it libavif rejects the `mif3` brand, so `FIF_AVIF` does not claim the file and `FIF_HEIF` does, then fails ("AV1" not built in). Simulated: the three corpus `mif3` files fail; HEAD loads them as AVIF.
> 3. `TestAPI/Test.2017.vcxproj` lacks `testToneMapping.cpp`: **confirmed**. `MainTestSuite.cpp:115` calls `testToneMapping()`, so an MSVC build fails to link; `TestAPI/Makefile` builds `*.cpp` and is fine.
>
> **Fixed (2026-10-19).** 042b6f7: `Makefile.mingw` defines `AVIF_ENABLE_EXPERIMENTAL_MINI=1`, `TestAPI/Test.2017.vcxproj` compiles `testToneMapping.cpp`, and the generators' `DIRLIST` names the OpenEXR 3.3 directories (for the record; the `.srcs` files stay hand-maintained).

- **Severity**: **Medium** (Build System Regressions)
- **Location**: `gensrclist.sh`, `genfipsrclist.sh`, `Makefile.mingw:45`, `TestAPI/Test.2017.vcxproj`
- **Root Cause & Impact**:
  1. **`gensrclist.sh` & `genfipsrclist.sh`**: Still reference deleted OpenEXR 2.2 directories (`Source/OpenEXR/Half`, `Source/OpenEXR/IlmImf`, `Source/OpenEXR/IexMath`), omit `Source/OpenEXR/OpenEXR`, `Source/OpenEXR/OpenEXRCore`, and `Source/LibDeflate`, and only match `\.(c|cpp)` in `egrep` — dropping all `.cc` source files in `Source/LibHEIF`, `Source/LibDe265`, and `Source/LibJXL` if regenerated.
  2. **`Makefile.mingw:45`**: Omits `-DAVIF_ENABLE_EXPERIMENTAL_MINI=1` (which is present in `Makefile.gnu:48`, `Makefile.osx:54`, and `FreeImage.2017.vcxproj`). Because `PluginAVIF.cpp:481` claims `mif3` files in `Validate`, MinGW builds recognize `mif3` AVIF files in `Validate` but fail to decode them inside `libavif`.
  3. **`TestAPI/Test.2017.vcxproj`**: `TestAPI/MainTestSuite.cpp:115` calls `testToneMapping()`, defined in `TestAPI/testToneMapping.cpp`, which was added to `TestAPI/Makefile` but omitted from `TestAPI/Test.2017.vcxproj`, causing an unresolved external symbol linker error in Visual Studio builds.
