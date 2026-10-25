# tiff-psd agent: findings (PluginTIFF, TIFFLogLuv, PluginG3, PSDParser, PluginPSD, XTIFF, LibTIFF4 patch), received 2026-10-19
Workspace W=$J/agents/tiff-psd (src/, bin/, out_*.txt, progress.md). Result: one Medium regression, five Low; ASan/UBSan/LSan no memory errors in TIFF/PSD code; all other differences are intended fixes checked against Pillow or an independent decoder.

## F1 Raw G3 fax: the libtiff reference file loads with 8024 extra blank rows
- Class: regression-vs-main. Severity: Medium. Confidence: high.
- Where: Source/FreeImage/PluginG3.cpp:149-170 (copyFaxFile loop); ab4fac5 (new loop) + 9eba884 (libtiff 4.7.2).
- What: with libtiff 4.7.2, after the end-of-page code and trailing bytes, the Fax3 1D decoder returns 1 ("row decoded") without consuming input (tif_rawcc stuck at 6) ~8000 times until an internal counter returns -1. qpv's loop stops only on a failed row that consumed nothing, so it keeps all those white rows. main's libtiff 4.6.0 returns -1 and rawcc=0 right after row 1102.
- Evidence: `W/bin/fitool_qpv load W/dl/pics/libtiffpic/g3test.g3 0` -> w=1728 h=9127; main -> h=1103 (= g3test.tif). qpv's first 1103 rows = main's; the other 8024 all white. W/src/g3loop.c isolation: qpv libtiff + qpv loop rows=9127; qpv libtiff + main loop runs away (the hang ab4fac5 fixed); main libtiff 1103 with either. Cutting the last 6 bytes -> 1102 rows in both.
- Fix sketch: also stop on the second consecutive successful row that leaves tif_rawcc unchanged (variant C validated: 1103 rows; 19-point truncation sweep + 7 synthetic files match variant B; trailing zero bytes still give 9 more blank rows than main, 516 vs 507).
- **Fixed** in 8a62554, where libtiff gives up EOL sync rather than by variant C alone: after each row the loader asks the decoder's mode (TIFFTAG_FAXMODE); once 4.7.2 decodes without EOLs after rows decoded with them (it switches when no EOL is left in the data), the page has ended: the row is written blank, as main's decoder ended a page, and the load stops. Variant C stays for data without any EOL, which 4.7.2 decodes from the first row on (main: 1 row): a second row in a row that reads no input stops it. g3test.g3 loads as 1728x1103, pixel for pixel g3test.tif and main, and trailing zero bytes give main's rows (507). 201 G3 files written with libtiff's encoder (EOL and byte-aligned codings; RTC, zero padding, random bytes; cuts; damage) and g3test.g3 cut by 0 to 11 bytes decode as on main byte for byte, but one cut whose partly decoded last row is the row before it repeated, as since ab4fac5; 3 of 61 EOL-less files with random bytes after them stop at 303 rows instead of 8492. testFaxG3 (TestAPI main suite) assembles the rows by hand, the end of g3test.g3 among the endings; the previous loader fails it.

## F2 1-bit PSD save ignores the palette: a min-is-black image is saved inverted
- Class: new-bug (main's 1-bit PSD save produced unreadable files). Severity: Low. Confidence: high.
- Where: PSDParser.cpp:2456 (colourMode = PSDP_BITMAP), raw/RLE writers 2253-2301 copy index bits unchanged; reader assumes 1 = black (CREATE_GREYSCALE_PALETTE_REVERSE 2048-2049); d47ad42 / a4df0a0.
- Evidence: `W/bin/fisave_qpv 1 1 37 23 20 0 W/psd/save1/qpv1_mb.psd mb`, read back MINISWHITE with identical bits -> every pixel inverted (GIMP/ImageMagick/Photoshop/FreeImage all read 1 = black). main's file unreadable by qpv, main, Pillow.
- Fix sketch: invert bits on save when palette index 0 is darker than index 1.

## F3 TIFF: ink sets other than CMYK are refused, even with TIFF_CMYK
- Class: compat-break (intended, ef0f638). Severity: Low. Confidence: high.
- Where: PluginTIFF.cpp:1903-1907.
- Evidence: W/tgen/inkset2_cmyk8.tif (photometric 5, spp 4, inkset 2): qpv "TIFF images of inks other than CMYK are not supported" NULL for flags 0/1/0x8000; main BITMAP/24 RGB, BITMAP/32 CMYK with TIFF_CMYK, header OK; Pillow CMYK.
- Fix sketch: with TIFF_CMYK return the raw inks; otherwise warn and convert as CMYK.
- **Fixed** in 410fd41, as sketched: the first four inks load as CMYK, raw with TIFF_CMYK, converted to RGB without it and with a warning (display loads warn too). The reason for the refusal is met instead: NumberOfInks, which libtiff also takes from InkNames, says which samples are inks, the ExtraSamples tag describes the others from the first sample past the inks (libtiff pads its list to the samples past four colours, keeping the file's entries first), and without the tag inks other than CMYK have no alpha. inkset2_cmyk8.tif loads as on main, pixel for pixel, with flags 0 and 1; 2- to 4-ink files load exactly as their InkSet 1 twins, display loads too. TestAPI/ICC profiles: 15 files of 4 to 6 inks; of 1808 TIFFs on disk only inkset2_cmyk8.tif loads otherwise.

## F4 TIFF_CMYK/PSD_CMYK with an RGB ICC profile attached now writes RGBA
- Class: compat-break (intended, d95a478). Severity: Low. Confidence: high.
- Where: SaveAsCMYK ColorManagement.cpp:2351-2366; PluginTIFF.cpp:2919/2944; PSDParser.cpp:2445/2474. Channel bytes relabelled RGBA (TIFF also swaps B/R; K becomes alpha). main wrote CMYK.
- Evidence: `fisave_qpv 1 32 37 23 18 1 W/icc/qpv_cmyk_rgb.tif icc=W/icc/rgb.icc` -> warning, Pillow RGBA; main -> CMYK. Same for PSD. No ICC or CMYK ICC: CMYK in both.

## F5 TIFF: any ENOMEM seen while opening or reading fails the whole load
- Class: compat-break (59ea9d5). Severity: Low. Confidence: high (mechanism), low frequency.
- Where: PluginTIFF.cpp:1137-1148 (Open), 1173-1180 (PageCount), 1843-1850 (page select), 2780. errno cleared before each call (stale values harmless), but any ENOMEM raised during the call fails the load: (a) a caller's read_proc leaving errno=ENOMEM on success; (b) libtiff failing to allocate an optional tag and recovering.
- Evidence: (a) `W/bin/errnoio_qpv W/tmp/sample.tif 1` -> "Memory allocation failed", NULL, multipage NULL; main 16x16 pages=3. (b) `python3 -I W/src/bigtag.py W/bigtag/bigxmp.tif 1200000000` (sparse 1.2 GB with XMP tag); `(ulimit -v 900000; fitool_qpv load ...)` -> NULL; main 16x16.
- Fix sketch: treat ENOMEM as fatal only when libtiff also fails or a pixel-relevant tag is missing.
- **Fixed** in e8ab30c. (a) The TIFF I/O glue keeps errno as it was around the caller's callbacks. (b) FreeImage's libtiff allocators count, per thread, the failed reallocs of more than 16 MB (how libtiff reads a tag's data, which it then leaves out) apart from all other failed allocations; a directory read that succeeded fails only for those others, or for an ENOMEM set outside the allocators. libtiff copies a custom tag's value by realloc too and keeps the tag without its value when that fails: such tags are unset (FreeImage copied an ICC profile from NULL). The page count clears errno before each directory. Both evidence cases load as on main. TestAPI/IO allocfail: through the callback, a file, a three-page document and a damaged strip; a 20 MB ICC profile libtiff cannot read, or reads and cannot copy, is left out; a 15 MB one fails the load. The TIFF sweeps keep their counts (1545, 60).

## F6 Header-only load succeeds for tiled planar 16-bit/float grey + extra-sample TIFFs that the full load refuses
- Class: new-bug. Severity: Low. Confidence: high.
- Where: PluginTIFF.cpp:2722-2770 (LoadAsGreyExtra; refusal 2745-2749 only when !header_only); ec337d1, f4bb70e.
- Evidence: `fitool_qpv load W/tgen/grey16x1_tp.tif 0x8000` -> RGBA16 64bpp; flags 0 -> "Separated tiled TIFF images are not supported" NULL; main NULL for both. Same greyf32x2_tp.tif, white16x1_tp.tif.

## The forwarded OOM lead (tif_dir.c:1524 via PluginTIFF.cpp:1905)
- Verdict: pre-existing crash class shared with main, not a qpv regression. libtiff (4.6.0 and 4.7.2) leaves a custom tag entry with value NULL when the value allocation fails (tif_dir.c:833-839 / 819-825); a later TIFFGetField dereferences it (:1524).
- The :1905 site needs an allocator that fails without setting errno (as the colormgmt harness); with glibc/MSVC ENOMEM qpv's Open check refuses first (W/bin/tiffoom_qpv_asan, SETERRNO=1: 240/350 points, no crash; SETERRNO=0: SEGV).
- Still reachable with errno set via the Exif sub-directory: qpv SEGV tif_dir.c:1524 <- XTIFF.cpp:363 <- tiff_read_exif_profile (PluginTIFF.cpp:899) (EXIFONLY=1, W/oom/exif_noicc{2,3}.tif); main SEGVs earlier (tif_dir.c:707 in TIFFReadEXIFDirectory) and in TIFFReadDirectoryFindFieldInfo (tif_dirread.c:5227) during TIFFClientOpen, the _TIFFMergeFields failure qpv's tif_dirinfo.c:651 patch fixes (patch verified).
- Gaps: errno checks don't cover TIFFReadEXIFDirectory (897) or the three TIFFSetDirectory(tiff, cur_dir) re-reads (903, 1749, 1784).
- Fix sketch: errno = 0 before 897 and skip tiff_read_exif_tags on ENOMEM, or a libtiff patch dropping the tag entry on value allocation failure.
- OOM sweeps with errno set + LSan: no bad_alloc escapes, no leaks.
- Since e8ab30c (F5), the directory reads it checks count libtiff's failed allocations by their NULL, errno or not, and fail for any but a big tag's: W/src/tiffoom.cpp with SETERRNO=0 on W/oom/cmyk_none_icc.tif runs its 240 points without the :1905 SEGV (93c7883: signal 11). TIFFReadEXIFDirectory and the cur_dir re-reads are not among those reads.

## Pre-existing (same or worse in main)
- Win32 only (code reasoning): YCbCr/Lab loader buffer size (tmsize_t)width*height (PluginTIFF.cpp:1961) wraps on Win32 -> heap overflow in TIFFReadRGBAImage; main has the same wrap.
- TIFF Photoshop tag DisplayInfo opacity > 100 fails the whole load in both (throw escapes ReadThumbnail) (W/oom/ps_badopacity.tif).
- 1/4-bit single-sample PlanarConfig=2 TIFFs: qpv refuses with a wrong "bitspersample = 8" message (PluginTIFF.cpp:1893-1899, 6e64300); main aborts in AssignPixel assert (Utilities.h:356), release build would loop forever; Pillow reads them.
- Indexed PSDs without resource 1046 get a grey palette (both). YCbCr/Lab loader returns premultiplied RGBA (both). 565 TIFF saves: main wrote unreadable files, qpv returns FALSE (23cc6cf).
- Out of memory, a CMYK TIFF loaded as RGB whose 24-bit copy cannot be allocated comes back 32-bit RGBA, opaque, with "Cannot allocate memory for buffer. CMYK image converted to RGB + pending Alpha" (FinishCMYK; main the same): W/src/tiffoom.cpp on W/oom/cmyk_none_icc.tif, allocation points 32-34, before and after F5. Noticed while checking F5, not fixed.
## Outside scope (agent notes)
- LibJPEG shift UB jdhuff.c:529/549, jidctint.c:232 (thumbnails, JPEG-in-TIFF). FreeImageTag.cpp:139 memcpy NULL src with 0 length (both).

## Checked, no issue found (condensed)
- Loads: libtiff pics-3.8.0 (~60), ~110 Pillow TIFFs (incl. crash/oom), 298 generated layouts (RGB/grey/MINISWHITE/palette/CMYK at 8/16/32/half; ExtraSamples variants; strip/planar/tile/tile-planar; BigTIFF; multi-page; Deflate; missing SPP): intended fixes confirmed vs Pillow (16-bit colormaps 256/256 vs main 74; associated alpha unpremultiplied; tiled CMYK; ICC+Exif use-after-free in main; ExtraSamples alpha; 16-bit grey+alpha; half-float RGBA; planar float grey; CMYK+extra; missing SPP).
- Saves: TIFF 24 types x 10 flag sets read back pixel-identical in qpv and main, Pillow reads all it supports; 5-page multi-page both ways; IPTC/Exif/XMP/resolution round-trip (main writes only a quarter of IPTC bytes); PSD 18 types x 4 flag sets read back identically in both; refused PSD types are ones main wrote unreadable.
- Damaged/OOM: MALLOC_PERTURB_ on 712 truncated files: 0 differences; ASan/UBSan/LSan 712 truncated x 4 flags + 717 intact x 5 flags: clean.
- Header-only consistent with full loads (flags 0, TIFF_CMYK, PSD_CMYK, PSD_LAB) except F6 and the 8-bit transparency table (inherent).
- Metadata: XTIFF checks, IPTC by byte count (incl. byte-swapped LONG), Exif, XMP, ICC+Exif, resolution.
- PSD: 55 samples + 194 generated files vs independent decoder (W/src/psddec.py): exact (CMYK ±1); alpha selection matches spec and GIMP/Krita/ImageMagick; PSD_CMYK = Pillow CMYK; PSD_LAB unchanged.
- Large compressible files: 30000² 1-bit G3/G4/PackBits, 16000² 8-bit Deflate/LZW/PackBits TIFFs, 10000² RGB PSD/PSB RLE load in qpv (main fails the 16000² 8-bit TIFFs "Image is corrupted").
- G3 hang fix works; truncated row counts = main; only F1.
