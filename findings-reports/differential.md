# differential: findings (qpv f1a2a67 vs main a976f39)

Empirical backbone: the same inputs through main-rel and qpv-rel, with Pillow 12.1.1 as a third reader, every observable
property compared. Workspace W = `/home/marius/.claude/jobs/d912fdd7/tmp/agents/differential`; harness sources in `W/src/`,
binaries in `W/bin/`, raw JSONL in `W/out/`, CSV tables in `W/out/csv/` (loads.csv, differences.csv, pixel_compare.csv,
saves.csv, reloads.csv, conversions.csv, display_icc.csv, offset_loads.csv, seek_end_refused.csv, asan_qpv.csv).

Corpus (2209 files): TestAPI data and top-level images, JXR-tests, bmpsuite-2.8, PSD samples, heif-seq samples, JXL
conformance inputs, MNG suite; downloaded PngSuite 2017, libtiff pics-3.8.0, Pillow 12.1.1 Tests/images (1058 files),
54 openexr-images; 406 files written by Pillow (each writer x modes x 1x1/3x5/257x129); generated raw G3 streams, BMP
alpha/V5 files, grey-ICC and CICP samples.

Runs: flag 0 load in both libs (+ LoadFromMemory, + OpenMultiBitmap/LockPage of every page); every format flag; every flag
| FIF_LOAD_NOPIXELS; pixel dumps compared in a common RGBA space against main and Pillow; loads from memory at offset 13;
loads through a FreeImageIO refusing SEEK_END; MALLOC_PERTURB_ 85 vs 170; 4-thread stress (rel and ASan); qpv ASan/UBSan/LSan
sweep; FIF_LOAD_DISPLAY_ICC sweep; save matrix (27 writers x 25 bitmap types x 3 sizes x common flags x {no metadata,
ICC+comment+background}, 5950 jobs per lib), every output reloaded by main, qpv and Pillow; SaveToMemory vs Save bytes;
multi-page copies (GIF, TIFF, APNG, WebP, MNG); conversion matrix (25 types x 30 ConvertTo* calls); capability matrix.

## Findings

### D1 Raw G3 fax files decode with up to 8192 blank rows appended (libtiff's g3test.g3: 1103 -> 9127 rows)
- Class: regression-vs-main
- Severity: Medium
- Where: Source/FreeImage/PluginG3.cpp:150-170 (copyFaxFile; the only stop, at 153-154, is a *failed* row that consumed
  nothing); caused by the LibTIFF 4.7.2 upgrade 9eba884 together with ab4fac5 ("G3: a damaged fax file could hang the loader").
- What: libtiff 4.7.2's Fax3 decoder, once the data ends in fill bits, keeps returning 1 ("row decoded") while consuming no
  input until its eofReachedCount reaches EOF_REACHED_COUNT_THRESHOLD = 8192 (Source/LibTIFF4/tif_fax3.c:49, checked at :297);
  libtiff 4.5 (main) returned -1 and consumed the tail. libtiff's own readers stop at ImageLength, but the raw-G3 loop reads
  until the data runs out, so every no-progress "row" becomes a blank scanline. Main's loop on libtiff 4.7.2 would never end;
  ab4fac5 added a stop only for failed rows.
- Trigger: FreeImage_Load(FIF_FAXG3, "g3test.g3", 0) (libtiff pics-3.8.0); any raw G3 stream that ends in fill bits.
- Evidence: `W/bin/g3repro_{main,qpv} g3test.g3` (src/g3repro.c): main 1728x1103 (pixels identical to libtiff's
  g3test.tif, hash 992c3265e2c7c5e6 in both), qpv 1728x9127; qpv's top 1103 rows equal main's, rows 1103-9126 all white.
  Pillow-written 1728x50 G3 stream + g3test's 7-byte tail (gen/g3/pil_50_tail_g3tail.g3): main 51 rows, qpv 8242 (= 50 + 8192);
  + 4 zero bytes: main 51, qpv 53; clean streams with or without RTC: identical. 2x2 matrix (src/g3loop.cpp, the plugin loop
  rebuilt against each tree's libtiff): 4.5+main loop 1103, 4.5+qpv loop 1103, 4.7.2+main loop never ends (10873 failed
  rows at a 20000 cap), 4.7.2+qpv loop 9127 (from row 1102: ok=1, 0 bytes consumed).
- Confidence: high
- Fix sketch: in copyFaxFile stop whenever a row consumed no input (`tifin->tif_rawcc == lastcc`), whatever `ok` is; checked
  with the harness: g3test.g3 -> 1102 rows, the tail variant -> 51.

### D2 FIF_LOAD_DISPLAY_ICC and the ICC conversion API ignore the grey profile of grey+alpha images
- Class: new-bug (the PSD plain-load profile drop is regression-vs-main on metadata, intended per 263ead3)
- Severity: Medium
- Where: Source/FreeImageToolkit/ColorManagement.cpp:885-897 (ColorConverter::SetSource drops an embedded GRAY profile
  when the pixels are RGB(A): "ICC: the embedded profile does not match the pixels and is ignored", :896). Grey+alpha PNGs
  load as 32-bit RGBA carrying the grey iCCP (identical in main: bpp 32, icc 912, same pixel hash); grey+alpha PSDs load as RGBA and drop the profile outright
  (Source/FreeImage/PSDParser.cpp:2060-2073, commit 263ead3).
- What: a greyscale image with transparency and a non-sRGB grey profile (Photoshop's default "Dot Gain 20%", "Dot Gain 15%",
  "Gray Gamma 2.2") is shown under FIF_LOAD_DISPLAY_ICC exactly as a plain load, i.e. as sRGB, while the same image without
  alpha is converted through its profile. FreeImage_ConvertToICCProfile / ApplyDisplayICCProfile take the same path. For
  PSD the plain load also loses the profile main kept (on main's 8-bit grey result). The AHK wrapper exposes
  FIF_LOAD_DISPLAY_ICC and ApplyDisplayICCProfile for QPV's display path.
- Trigger: gen/greyicc/la_dotgain.png (Pillow LA PNG with the Dot Gain 20% profile taken from
  corpus/psd/agpsd__read_grayscale-alpha_src.psd); corpus/psd/agpsd__read_grayscale-alpha_src.psd,
  kimageformats__testcard_graya8.psd, oovpsd__grayscale8bit_transparent.psd.
- Evidence: probe_qpv l_dotgain.png (no alpha) 0 vs 0x4000: max 23, mean 14.6, mid-grey 128 -> 149; la_dotgain.png
  0 vs 0x4000: identical pixels plus the message above; la_dotgain.tif / l_dotgain.tif: converted (128 -> 149).
  bin/iccconv_qpv (src/iccconv.c, FreeImage_ConvertToICCProfile(dib, NULL, 0, 0)): la_dotgain.png UNCHANGED (128 -> 128),
  l_dotgain.png converted (128 -> 149). The three PSDs: plain load icc=0 in qpv vs 912/408/912-byte GRAY profile in main;
  0x4000 output identical to the plain load.
- Confidence: high
- Fix sketch: for a GRAY profile on RGB(A) pixels with R=G=B, convert through an RGB profile built from the grey TRC
  (or convert the grey channel before expanding to RGBA); keep the PSD profile for that path.

### C1 32-bit BMPs with alpha and BMPs with an ICC profile written by qpv cannot be read by stock FreeImage
- Class: compat-break (intended: a57a9d9 "alpha is saved where every reader finds it", 68437f9 "an image's ICC profile is
  saved in a V5 header")
- Severity: Low
- Where: Source/FreeImage/PluginBMP.cpp:1818-1830 (`v5 = alpha || embed`, BITMAPV5HEADER, BI_BITFIELDS + alpha mask, 'MBED')
- What: main wrote a 40-byte BITMAPINFOHEADER BI_RGB for every BMP; qpv writes a 124-byte V5 header for 32-bit images whose
  alpha is not all 255 and for any image with an RGB profile. main's reader (FreeImage 3.19 and older) rejects V4/V5 headers
  ("Invalid file format"); Pillow and qpv read them, and they follow the spec.
- Trigger: FreeImage_Save(FIF_BMP, 32-bit image with varying alpha) or any bitmap carrying an RGB ICC profile; open in main.
- Evidence: save matrix (out/save.jsonl, out/reload_analysis.txt): 34 qpv BMPs unreadable by main because of the V5 header
  (28 with an ICC profile, every bitmap type; 6 b32 with alpha, all sizes, flags 0 and BMP_SAVE_RLE). Header dump:
  qpv BMP_b32_3x5_f0_m0.bmp hdr 124, comp 3, masks ff0000/ff00/ff/ff000000, 'sRGB'; BMP_b24_3x5_f0_m7.bmp hdr 124 'MBED'
  profsize 588; opaque 32-bit and untagged images keep the 40-byte header and load in main. (12 RLE8 BMPs are also
  unreadable by main, but main cannot read the identical RLE8 data it writes itself: pre-existing.)
  Repro: `W/bin/save_qpv DIR 0 b32 3 5 0 0` (ok=1), then `W/bin/probe_main DIR/BMP_b32_3x5_f0_m0.bmp 0` ->
  "MSG fif=0: Invalid file format", ok=0; `W/bin/probe_qpv` on the same file -> ok=1 bpp=32 ctype=4.
- Confidence: high
- Fix sketch: none needed if intended; document that QPV-saved BMPs with alpha or a profile need a V4/V5-capable reader.

### C2 TIFF_CMYK / PSD_CMYK no longer write CMYK when the 32/64-bit image carries an RGB profile
- Class: compat-break (intended: d95a478 "TIFF, PSD: TIFF_CMYK and PSD_CMYK save an image with an RGB profile as RGB")
- Severity: Low
- Where: Source/FreeImageToolkit/ColorManagement.cpp:2351 (SaveAsCMYK); callers PluginTIFF.cpp:2919/2944, PSDParser.cpp:2445/2474
- What: main relabels the four channels as C,M,Y,K whenever the flag is passed (dropping the profile); qpv writes RGB(A)
  with the profile and a warning when the profile is not CMYK. A caller who filled the channels with CMYK but left an sRGB
  profile on the bitmap (e.g. inherited from the source) now gets an RGB file.
- Evidence: save matrix TIFF b32/b32o/rgba16, flags 0x1, meta 7: main reload icc=0 (CMYK), qpv reload icc=588 (RGB); PSD same.
  Repro: `ICC=W/gen/srgb.icc W/bin/save_{main,qpv} DIR 18 b32 3 5 0x1 7`; Pillow reads main's file as mode CMYK with the
  588-byte sRGB profile embedded (a self-contradictory file), qpv's as RGBA with that profile.
- Confidence: high
- Fix sketch: document; or honour the explicit flag and drop the RGB profile instead.

### C3 PFM row order: main-written PFMs load upside down in qpv (and qpv-written ones in main)
- Class: compat-break (intended spec fix: 00abb5e "PFM: images are saved and loaded bottom row first")
- Severity: Low
- Where: Source/FreeImage/PluginPFM.cpp:296/331 (read), :418 (write, bottom row first)
- What: PFM stores the bottom row first; main read and wrote top row first. qpv follows the spec, so every PFM written by
  main or older FreeImage loads flipped in qpv, and qpv's PFMs load flipped in older FreeImage.
- Evidence: Pillow Tests/images/hopper.pfm, hopper_be.pfm, gen/pillow/pfm_F_*.pfm: qpv == Pillow, main flipped (vertical flip
  gives max 0); save matrix PFM f32/rgbf: main->qpv and qpv->main flipped, Pillow agrees with qpv.
  Repro: `W/bin/probe_main W/out/save/qpv/PFM_f32_3x5_f0_m0.pfm 0` pix=9b6001922e8540de vs `W/bin/probe_qpv` pix=4363f8442e0aa45a
  (the dumps differ by a vertical flip only).
- Confidence: high
- Fix sketch: none (spec fix); a note in the release text for users of FreeImage-written PFMs.

### C4 GetFileType returns FIF_APNG (39) for animated PNGs, and the default load is the first animation frame
- Class: compat-break (intended: 0f9863f "PluginAPNG: read and write animated PNG (FIF_APNG = 39)")
- Severity: Low
- Where: Source/FreeImage/PluginPNG.cpp:345-353 (Validate refuses acTL-before-IDAT); PluginAPNG.cpp:506-515 (a default
  image without an fcTL is not a frame), :1508-1510 (page -1 -> 0)
- What: callers comparing `fif == FIF_PNG`, or passing GetFileType's result to Load, see a change for animated PNGs: the
  default load is the first fcTL frame, not the IDAT default image main returned when the default image is not part of the
  animation (APNG-spec behaviour). FreeImage_Load(FIF_PNG, ...) still returns exactly main's image.
- Evidence: 56 corpus files ft PNG -> APNG; forced FIF_PNG (-f 13) equals main on 54/54 Pillow apng tests; 21 of them give a
  different page-0 image under FIF_APNG (e.g. blend_op_over.png: main and Pillow frame 0 = red default image, qpv page 0 green).
  Repro: `W/bin/probe_qpv P/apng/blend_op_over.png 0` -> ft=39 pix=4b061a6145a60383; `W/bin/probe_qpv -f 13 ...` and
  `W/bin/probe_main ...` -> pix=939235bdff064383 (main ft=13).
- Confidence: high
- Fix sketch: none if intended; document for callers that hard-code FIF_PNG.

### C5 JPEG 2000 samples of 1-7 and 9-15 bits are scaled by default
- Class: compat-break (intended, documented: e6f4f0b; J2K_UNSCALED / JP2_UNSCALED restore main's values)
- Severity: Low
- Where: Source/FreeImage/J2KHelper.cpp:181-190 (SampleScale), :236
- Evidence: Pillow 9bit.j2k: main raw max 511, qpv default 65535 (matches Pillow's I;16), qpv with J2K_UNSCALED byte-identical
  to main; 16bit.cropped.j2k unchanged. Repro: `W/bin/probe_qpv P/9bit.j2k 0` pix=78e1e8251d142fa1, `... 9bit.j2k 1` and
  `W/bin/probe_main P/9bit.j2k 0` pix=d8ed9919593339b7.
- Confidence: high

### C6 FreeImage_Allocate and the other Allocate* entry points return NULL for a negative width
- Class: compat-break (intended: 03fcfc5 "FreeImage_Allocate: a negative width was turned into a positive one")
- Severity: Low
- Where: Source/FreeImage/BitmapAccess.cpp:304 in the shared FreeImage_AllocateBitmap (:300), which Allocate, AllocateT,
  AllocateHeader, AllocateHeaderT and AllocateHeaderForBits all call (Allocate and AllocateT tested)
- What: main took abs(width), so Allocate(-5, 5, 24) returned a 5x5 bitmap; qpv returns NULL. Negative heights (top-down
  convention) still give abs(height) in both. Callers that relied on the old leniency now get NULL.
- Evidence: W/bin/negalloc_{main,qpv} (src/negalloc.c): main "Allocate(-5,5): ok 5x5", qpv "Allocate(-5,5): NULL";
  Allocate(5,-5) ok 5x5 in both.
- Confidence: high
- Fix sketch: none if intended; mention in the release notes.

## Triage of the main/qpv differences (no further findings)
- Pass 1: 4310 load records. 5 files main loads and qpv refuses: all deliberately invalid. lpqp_bad.jxr has a reserved LP QP
  index and ycc444.jxr is YCC without PTM_COLOR_INFO; qpv's TestAPI/JXR/convert.c requires both to be refused. 01r_00.pcx and
  pcx_overrun.bin are Pillow overrun tests that must raise. expected_to_read.jp2 is a Pillow header-error test: OpenJPEG
  2.5.4 says "JP2H box missing", and Pillow refuses it too.
- 103 files qpv loads and main does not: 63 match Pillow within 2 levels. 36 have no Pillow opinion: the bmpsuite q files
  match bmpsuite's reference PNGs, and the rest are MNG composition, EXR YC, JXR fixtures, RAW mono and salvaged truncated
  files. 1 is a 5-5-5-1 BITFIELDS BMP whose alpha Pillow ignores. 4 are damaged bmpsuite RLE files salvaged differently
  from Pillow (main fails them).
- 94 pixel differences: 21 APNG page-0 semantics (C4). 3 PSDs whose 4th channel is a named saved selection: per Photoshop
  semantics qpv shows them opaque (667ed86), while main and Pillow show alpha. Grey/CMYK PSD merged alpha now applied.
  PFM row order (C3). PCD DIV4/DIV16 sizes were swapped in main. GIF playback matches Pillow, main does not. Tiled CMYK JPEG
  TIFF: qpv within 1 level of Pillow, main off by 217 mean. 16-bit RGBa TIFF un-premultiplied (qpv close to Pillow).
  16->8-bit palette rounding (qpv == Pillow). ICO palette (qpv == Pillow). Leaf DNG white balance (qpv decodes like its
  AsShotNeutral twin). EXR YC bottom row (22c7abc). JXR premultiplied/BGR/CMYK fixtures (qpv's tests). libtiff ThunderScan
  right-edge pixels of text.tif (0.4%, upstream libtiff change). D1.
- Header-only vs full load: every non-colour-type inconsistency in qpv also exists in main; the qpv-only ones are 32-bit
  images whose alpha a header cannot scan (FIC_RGBALPHA vs FIC_RGB).

## Unreproduced observation
- UBSan once reported "load of value 7, which is not a valid value for type 'bool'" in vendored libjxl
  (Source/LibJXL/lib/jxl/dec_xyb.h:36, OutputEncodingInfo copy via PluginJXL LoadCMYK) during the 4-thread ASan run; not
  reproduced single-threaded or in a JXL-only 4-thread rerun; outputs were deterministic. Upstream code; Low if real.

## Pre-existing in main (fixed in qpv)
- main aborts or crashes: WebP save of unsupported types (free(): invalid pointer), TGA RLE save of 1-bit (assert),
  JXR crop.jxr and ileave420.jxr, header-only truncated_exif_dpi.jpg, EXR Blobbies/Spirals, J2K/JP2 on SEEK_END-refusing
  handles.
- main fails or garbles: the 1-, 8- and 16-bit PSDs its own writer produces (unreadable by main, qpv and Pillow), its own
  RLE8 BMPs, Pillow's L/P TIFFs, 128bppRGBFloat JXR (padding read as alpha), 1-bit MINISWHITE JXR (photometric ignored),
  saves of types it claims not to support (469 accepted, all decoded wrong or not at all).

## Checked, no issue found
- PNG: forced FIF_PNG equals main on all 54 Pillow APNG tests; PngSuite and Pillow PNGs identical including
  PNG_IGNOREGAMMA; PNG saves byte-identical to main for all 232 successful jobs.
- BMP vs bmpsuite references: qpv matches every reference main matches and fixes the rest (V4/V5, BITFIELDS 32-bit, OS/2 v2).
  Real alpha in BI_RGB 32-bit kept; all-zero alpha made opaque.
- GIF: frames, GIF_LOAD256, GIF_PLAYBACK vs Pillow (qpv fixes 2 files). Multi-page copy byte-identical to main. Per-page
  LogicalWidth/LogicalHeight/Loop tags now on every page (convention change, writer unaffected).
- TIFF: libtiff pics and Pillow TIFFs, TIFF_CMYK; only fixes (Pillow L/P TIFFs main calls corrupt, corrupt ICC in main,
  RGBa, tiled CMYK JPEG); multi-page copies.
- JPEG: all load flags identical to main; JPEG_EXIFROTATE now keeps the ICC profile; encoder within 0.02 mean of main
  (libjpeg 10 DQT).
- Other formats: PSD (spec alpha semantics, CMYK alpha, thumbnails, header-only). PCX/SGI/TGA/PNM/XBM/XPM/WBMP loads and
  saves identical to main. ICO (MAKEALPHA fix). PCD (DIV4/DIV16 fix). WebP (lossy/lossless identical, playback).
  JXR (lossless exact, lossy identical). EXR (54 public files: only the YC fix; 2 main crashes). RAW (linear sRGB profile
  and CICP on RAW_DEFAULT, embedded profile on RAW_PREVIEW, none on RAW_DISPLAY; preview rotation; mono DNGs).
- New formats (AVIF/HEIF/JXL/APNG/MNG playback): LoadFromMemory == Load, header == full, LockPage(0) == Load, deterministic.
- Robustness and memory: LoadFromMemory == Load for all qpv loads. Offset loads from memory: qpv 1892/1892 identical,
  main 481 differences. SEEK_END-refusing handle: no file main loads that qpv fails; qpv 147 fails vs main 173 plus 55 main
  aborts; 3 qpv salvage differences only where main fails. MALLOC_PERTURB_ 85 vs 170: 0 differences over 4364 runs.
  4 threads x 138 files x {0, DISPLAY_ICC}: 0 mismatches, 0 ASan errors. qpv ASan/UBSan/LSan sweep over the corpus
  (flag 0 with memory and multi-page, and header-only): 0 reports from FreeImage code (4027 clean runs; the 39 runs with
  harness UB rerun with the fixed probe bin/probe_qpvasan2: clean; all 298 harness-leak runs leaked exactly 1 allocation,
  the probe's own read buffer).
- FIF_LOAD_DISPLAY_ICC sweep: no crash, no failure where the plain load works, no size change; NOPIXELS output equals the
  plain header-only load (1861/1861); 79 conversions; ignored profiles only on fixtures/fuzz files or D2.
- Saves: no save main accepts and qpv refuses produced a usable file in main (all 469 decoded wrong or not at all); no
  lossless round trip worse in qpv; every qpv-only save round-trips; Pillow reads every qpv file whose main counterpart it
  reads; SaveToMemory == Save bytes in qpv (main differs in 150); the writer never modifies its source; qpv's
  APNG/WebP/MNG animations keep frames, loop and delays in Pillow; ICC/comment/background/dpm kept as in main or better.
- Capability matrix: identical for FIF 0-36 apart from BMP/JP2 ICC, MNG writing, GIF/MNG header-only. Conversions:
  36/2250 differ, all fixes (float clamp b2ec031, odd-width min/max, UINT32 >= 2^31). CICP-only PNGs: pixels equal main,
  tags attached, P3 profile synthesised.
