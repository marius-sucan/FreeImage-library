# toolkit agent: findings (qpv f1a2a67 vs main a976f39), received 2026-10-19
Workspace: $J/agents/toolkit (src/ programs, bin/, out/, data/); progress.md has harness paths.

## TK-1 FreeImage_Invert no longer inverts the K channel of CMYK images
- Class: regression-vs-main. Severity: Medium. Confidence: high.
- Where: Source/FreeImageToolkit/Colors.cpp:102 (`samples = (bytespp == 4) ? 3 : bytespp`, used :107) and :124 (`wordspp == 4 ? 3`), from 8464e74 ("FreeImage_Invert turned an opaque RGBA image fully transparent").
- What: the alpha-keeping fix keys on 4 samples per pixel; CMYK images (TIFF_CMYK/PSD_CMYK/JXR_CMYK/JXL_CMYK loads, 32-bit FIT_BITMAP or FIT_RGBA16 with FIICC_COLOR_IS_CMYK) keep K in that slot and have no alpha. qpv inverts C,M,Y only; main inverted all four inks (as Pillow ImageChops.invert does).
- Evidence: `bin/d1_{qpv,main} data/d1/cmyk.tif out.tif`:
  qpv 32-bit CMYK (10,20,30,40) -> (245,235,225,40); RGBA16 (1000,2000,3000,4000) -> (...,4000); TIFF_CMYK file top-left 200 100 50 250 -> 55 155 205 250 (≠ PIL invert)
  main -> (245,235,225,215); RGBA16 -> (...,61535); file -> 55 155 205 5 (= PIL invert)
  Real file kimageformats__cmyka-8bits.psd with PSD_CMYK: centre pixel qpv 255 255 255 0, main 255 255 255 255.
- Fix sketch: keep the 4th sample only when not CMYK (check FreeImage_GetICCProfile(src)->flags & FIICC_COLOR_IS_CMYK), same for wordspp.

## TK-2 In-place lossless JPEG transforms now always delete data appended after the JPEG
- Class: compat-break (intended change, unintended side effect). Severity: Low (agent's rating). Confidence: high.
- Where: Source/FreeImageToolkit/JPEGTransform.cpp truncateInPlaceStdIO (:396-416), truncateInPlaceMemory (:418-428); called after every in-place JPEGTransform(U)/JPEGCrop(U)/JPEGTransformCombined(U) (:541/:561/:581/:601/:621/:641) and ...FromMemory (:687). From 281eba3.
- What: the commit removes stale entropy-coded bytes after the new EOI (main left them: privacy leak after crop). But it truncates at the new EOI, so anything appended after the source EOI (Google/Samsung Motion Photo MP4, end-located trailers) is always removed. main kept the trailer at the same end-relative position whenever the output was not longer (every crop, shrinking rotations/flips); it corrupted it when the output grew. Separate-output transforms drop trailers in both; outputs byte-identical (96 cases).
- Evidence: `python3 -I src/d2_run.py` (6 TestAPI JPEGs + 5128-byte `ftypmp42` trailer):
  test.jpg+trailer rot90: qpv out=46049 trailer_kept=False | main out=51295 trailer_kept=True
  test.jpg+trailer crop: qpv 21685 False | main 51295 True; fi_jpeg_odd.jpg rot90/fliph/rot180/crop/mem-*: qpv False | main True
  exif.jpg+trailer rot90 (grows): qpv False | main False (overwritten)
- Fix sketch: record where the source JPEG ended; copy [old_end, old_size) after the new EOI, then truncate.

## TK-3 FreeImage_Allocate/AllocateT/AllocateHeader(T)/AllocateEx(T) return NULL for a negative width
- Class: compat-break (intentional, 03fcfc5). Severity: Low. Confidence: high.
- Where: Source/FreeImage/BitmapAccess.cpp:304-306 (FreeImage_AllocateBitmap). main applied abs() to width (still does for height, :312).
- Evidence: harness out/{alloc,allocex}.{qpv,main}.txt: qpv `alloc|x|-1x5|d0 b24|NULL`, main `t1 b24 1x5`; AllocateExT(FIT_FLOAT,-5,3) NULL vs 5x3.
- Fix sketch: document, or restore abs(width) with the INT_MIN guard.

## Checked, no issue found (condensed)
- FreeImage_Rotate: 11,900 cases (bm1/8/24/32, UINT16, RGB16, RGBA16, FLOAT, RGBF, RGBAF; many angles; bkcolor): integer types byte-identical to main; floats differ at non-right angles only (5d670e6 bias fix: constant 0.5 stays 0.5 in qpv, main -0.397..1.371); ICC/CMYK kept (21c47b7, a6da159); NaN/Inf angle -> NULL (main hangs on Inf).
- OpenMP rotation: 240 large rotations identical at 1/3/8 threads, repeated; 1-bit 90/270 serial.
- RotateEx: 8/24/32 identical to main; refuses FLOAT/UINT32/INT32 (32be4a9).
- CreateView contract: 15 in-place ops x 5 views x 17 types: qpv 0 outside-pixel changes (main 79 failures, 15 FlipVertical crashes).
- Sub-byte (1/4-bit) reference checks: 541, qpv 0 failures (main 67).
- AdjustCurve/Brightness/Contrast/Gamma/GetHistogram identical; AdjustColors identity TRUE; ApplyColorMapping count as documented.
- FillBackground/AllocateEx(T)/EnlargeCanvas: differences only from intended fixes (/255 rounding, ALPHA_IS_INDEX, SET_ALPHA, 4-bit odd width, IsVisualGreyscaleImage, INT_MAX borders refused).
- Composite (/255 rounding only), channels (complex-channel refusals intended).
- Conversions (ConvertTo4/8/555/565/24/32, Greyscale, StandardType, ConvertToType, UINT16/RGB16/RGBA16, Threshold, Dither, quantizers, RawBits, Clone, GetMemorySize; 17 types x 9 variants x 5 sizes): identical except intended CONVERT_TO_BYTE min/max scaling, NaN/Inf skipping, and ColorQuantizeEx NULL reserve palette (main crashes 135 times).
- BitmapAccess: cmyk_alpha handled in Clone/Unload/CreateView/header-only; CopyICCProfile; RemoveAlphaChannel; FindFirstMetadata on emptied model (main crashes); SetMetadata self-tag (main heap-use-after-free).
- Header-only bitmaps through 37 entry points: no crashes.
- ASan/UBSan qpv: no new errors in scope.
## Pre-existing in main too
- ConversionType.cpp:138 INT32 span > 2^31 signed overflow; ConvertToStandardType/ConvertToType NULL deref on header-only non-bitmap (:110-112); ConvertTo4Bits leak (Conversion4.cpp:150) / clone (:245); Halftoning RAND signed overflow; LFPQuantizer misaligned loads.
## Outside scope (noted by agent)
- ConversionRGBF.cpp:115 ConvertToRGBF on 16-bit 555/565 FIT_BITMAP: heap-buffer-overflow in both (pre-existing).
- Float conversions to RGBF/RGBAF/Float differ for HDR/NaN inputs (clamps removed) -> tonemap agent.
- FREEIMAGEHEADER grew by cmyk_alpha pointer: only code hard-coding offsets inside dib->data affected (ABI note).
