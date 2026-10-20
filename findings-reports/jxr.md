# jxr agent: findings (PluginJXR.cpp + Source/LibJXR), received 2026-10-19
Workspace $J/agents/jxr. Oracle: ITU-T T.835 (08/2016) JPEG XR reference software (Picture Elements implementation, not jxrlib), sources dl/t835/, build t835build/jpegxr; decodes to TIFF/raw, encodes every T.832 format incl. tiles, hard tiles, alpha modes, windowing, internal colour formats, band dropping.
Harness: src/fi.c (bin/fi_{qpv,main,qasan,masan}), src/interop.py (save/load matrix), src/t835fmts.py, chk_fmts.py, chk_dir.py, orient.py + setorient.py (orientation 0-7), settag.py (PTM_COLOR_INFO), asan_sweep.sh, src/{uni,bigsave,meta,rating,xmpsave,memio,hardenc,viewsave}.c.
Summary: qpv's decoding is right in every case checked against the reference and fixes many main bugs (wrong pixels, heap overflows, asserts); no memory error in new code; three behaviour changes; one pre-existing severe save bug remains.

## JXR-1 FIT_RGBF saves are now 128bppRGBFloat files, which main and FreeImage 3.18 load as RGBAF with alpha 0 (fully transparent)
- Class: compat-break (intentional; commit message says so). Severity: Medium. Confidence: high.
- Where: Source/LibJXR/jxrgluelib/JXRGlueJxr.c:380-392 (pTwins maps 96bppRGBFloat to 128bppRGBFloat), 014b15a. Reader side main PluginJXR.cpp:215 ({ BD_32F, 128, FIT_RGBAF }).
- What: every upstream FreeImage up to main maps 128-bit float to FIT_RGBAF and takes the padding float (jxrlib writes 0) as alpha: QPV's FIT_RGBF JXR saves look blank in apps built on FreeImage 3.18 or main. Upside: T.835 (conforming readers) now read them; it refused main's 96bppRGBFloat files. Related: 1-bit FIC_MINISWHITE now saved as BD1BLACK1 (e969ea0); main/3.18 show them inverted.
- Evidence: `SIZES=64x48 FLAGS=0x64,0 python3 -I src/interop.py`: `rgbf 64x48 flags=64 by qpv qpv:t11/96/ct2 ... main:t12/128/ct4 ... ref:max=0.00012207`; main's 4th channel of interop/rgbf_64x48_64_qpv.jxr min 0.0 max 0.0; main-saved RGBF loads in main t11/96 but T.835 refuses (`_jxrc_image_pixelformat: Assertion '0' failed`); 014b15a message acknowledges older readers show the saves transparent.
- Fix sketch: policy choice: keep 96bppRGBFloat for FreeImage-reader interop, or document the change.

## JXR-2 A save now fails when jxrlib cannot rewrite the image's XMP <dc:format>; main saved the image
- Class: regression-vs-main. Severity: Low (non-conformant XMP, rare). Confidence: high.
- Where: PluginJXR.cpp:2229-2230 (`error_code = WriteMetadata(...); JXR_CHECK(error_code);`), from 73d3287; failure in JXRGlueJxr.c:1129/1131 PKImageEncode_SetXMPMetadata_WMP (replaces dc:format with image/vnd.ms-photo; WMP_errFail when the closing tag is missing or non-text precedes </dc:format>).
- What: main ignored WriteMetadata's result (wrote the image, dropped XMP and following Exif/GPS); qpv aborts the save with the misleading "Invalid instruction - please contact the FreeImage team" and removes the file.
- Evidence: `bin/xmpsave_{qpv,main} out.jxr {1,2}` (src/xmpsave.c; XMP `<dc:format><rdf:Alt><rdf:li>image/jpeg</rdf:li></rdf:Alt></dc:format>` or unclosed `<dc:format>image/jpeg`): qpv `MSG[36]: Invalid instruction ... save=0`, no file; main save=1, 255-byte file that reloads in qpv. Mode 0 (plain dc:format) saves in both.
- Fix sketch: on SetXMPMetadata failure retry without the dc:format rewrite, or warn and skip XMP; fail only on OOM.

## JXR-3 Loaded type/bpp/colour type changed for many pixel formats; JXR_CMYK on load shares bit 0x4 with JXR_LOSSLESS and quality values
- Class: compat-break (intentional; all corrections). Severity: Low. Confidence: high.
- Where: PluginJXR.cpp:282-305 (s_FreeImageRowLayouts), 463-512 (GetNativePixelFormat), 1880-1881 (keep_cmyk); commits 2add680, eac7ad0, 0bcee4a, 3fae37c, e969ea0, e053de3.
- Changes (main -> qpv): 32bppBGR/32bppRGB 32-bit FIC_RGBALPHA alpha 0 -> 24-bit; 32bppRGBE raw bytes as 32-bit -> FIT_RGBF; 128bppRGBFloat RGBAF alpha 0 -> RGBF; 32/64bppCMYK raw CMYK labelled RGBA with profile -> RGB 24/48, CMYK profile dropped (CMYK only with JXR_CMYK); premultiplied labelled straight -> un-premultiplied; 32bppRGBA and 3-channel n-component R/B swapped -> correct; 24bppYCC444 without PTM_COLOR_INFO shown as RGB -> refused; BD1BLACK1 MINISBLACK (inverted) -> FIC_MINISWHITE.
- Flag overlap: JXR_CMYK (0x4) is a bit of JXR_LOSSLESS (0x64) and many quality values; a caller passing save flags to Load (no-op in main) now gets CMYK as FIC_CMYK. Multi-page cache uses separate flags (MultiPage.cpp:376/439).
- Evidence: `python3 -I src/cmp_all.py data/*.jxr` vs T.835 decodes in t835out/, src/chk_cmyk.py, chk_premul.py, chk_misc.py, RGBE check: qpv maxdiff=0 vs reference on all; e.g. `bgr32 main type=1 bpp=32 ct=4 ... extra-ch min=0 max=0`; RGBE qpv `vs m*2^(E-136): 0.0`, main `raw bytes vs ref RGBE 63`.
- Fix sketch: release notes; optionally give JXR_CMYK a bit above 0x7F.

## Pre-existing (in main too), severe enough to mention
- JXR save fails with "File I/O error" whenever an inline IFD value of type ASCII/BYTE/UNDEFINED is exactly 4 bytes (e.g. EXIF_MAIN Make "DJI"/"HTC"/"LGE", 1-char XPTitle): WriteWmpDE calls pWS->Write(pWS, pad, 0) (JXRMeta.c:823) and FreeImage's _jxr_io_Write returns WMP_errFileIO for cb == 0 (PluginJXR.cpp:59-69). Repro `bin/rating_{qpv,main} out.jxr 4` (Make "DJI" + Model): both `MSG[36]: File I/O error save=0`; main leaves a broken 158-byte file, qpv none. Fix: return success for cb == 0. (a14b20b fixed neighbouring cases, not this one.)
- UBSan shift-exponent at strdec.c:476/480/484 (inverseConvertRGBE, >> (iShift + 1) with iShift up to 255): main-asan reports the same lines on fuzz inputs 2fe4e6338e81, 8c3b413f1eb9, f919a29585f1; no FreeImage patch touches it; on input_2_* main additionally hits an ASan global-buffer-overflow, qpv does not.
- Other jxrlib UB noise in both builds (left shift of negatives, misaligned U32 loads, shift by 32 at strcodec.c:990/993).
## Outside scope (agent notes)
- Source/Metadata/Exif.cpp:1222 tiff_write_ifd reads ifd_size bytes from the 1-byte stack variable empty_byte (FreeImage_WriteMemory(&empty_byte, 1, ifd_size, hmem)): ASan stack-buffer-overflow when any image with EXIF_EXIF tags is saved as JXR (e.g. SaveToMemory); same in main (Exif.cpp:1189).
- Header-only with FIF_LOAD_DISPLAY_ICC reports FIC_CMYK while the full load returns RGB (documented).
- CICP tag on JXR YCC loads keeps the file's matrix (e.g. 1) on pixels that are now RGB (AVIF/HEIF same convention; PluginAPNG.cpp:1117 tests matrix == 0).

## Checked, no issue found (condensed)
- Pixel mapping: 40 files (TestAPI/JXR/data, JXR-tests) + ~150 T.835-encoded files (raw formats 3-34 at 8/10/16 bits, alpha modes 0/1/2, grey/RGB/RGBA fixed/half/float, internal YOnly/YUV420/422/444, lossless and q20/30): wherever qpv loads, pixels = reference after documented conversion. Everything main loads loads in qpv except deliberate refusals (YCC444 without PTM_COLOR_INFO; lpqp_bad.jxr, non-conformant, main heap-overflows on it).
- YCC444 + PTM_COLOR_INFO matrices 1,4,5,6,7,8,9 full/narrow match H.273 exactly; 0/2/10 refused with message. Narrow-range expansion correct.
- Interop matrix (15 types x 7 sizes x 8 flag sets): qpv files load in main with identical pixels except JXR-1; main files load in qpv; T.835 reads every qpv file. Saves < 16x16 and OL_TWO step-down fine; JXR_PROGRESSIVE real frequency order; CreateView saves exact.
- Metadata round-trips (ICC, EXIF_MAIN/EXIF, GPS, IPTC, XMP, Rating, PageNumber, XPTitle, ...), also at memory-stream offsets; qpv IFDs sorted with even offsets (T.835 parses them; rejects main's).
- Banded decode and orientation: orientations 0-7 on ~70 base files at many sizes: every result = numpy transform of the upright load; main cannot load quarter turns.
- Large images: 10000x8000 32-bit and 12000x9000 24-bit (~300 MB) saves exact, decode identically in main; Win64 offsets OK.
- Damaged-file gate: PlausibleImageSize (>= 1 bit per 256 px on rasters > 64 MB) never refuses valid codestreams (DCONLY uniform 10000x10000: 48862 bytes vs 48828 minimum).
- Header-only: 574 file x flag pairs = full load.
- LibJXR patches: interleaved alpha = T.835 (main off by 1 / garbles 16-bit); 64bppRGBHalf/FixedPoint row start + RGBE in-place = T.835; hard tiles with unequal tile counts = T.835 (main tile-edge errors up to 255; flip side: files from unpatched jxrlib's encoder with such tiles now decode with artifacts identical to T.835; only direct API users can make them); last-macroblock-row guard OK; 1635 ASan/UBSan/LSan runs clean beyond pre-existing jxrlib UB; I/O wrapper offsets OK (memory streams and non-zero offsets work in qpv, fail in main).
