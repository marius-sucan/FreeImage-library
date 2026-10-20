# tonemap-metadata: qpv vs main findings (2026-10-19)

Scope: HDR / tone mapping / float+linear conversions (ConversionLinear.cpp, MustTonemap.cpp, tmoColorConvert.cpp,
tmoReinhard05.cpp, tmoFattal02.cpp, tmoDrago03.cpp, ToneMapping.cpp/.h, ConversionRGBF/RGBAF/Float.cpp,
MultigridPoissonSolver.cpp, plus ConversionType.cpp's float paths) and Source/Metadata/*.
Workspace: /home/marius/.claude/jobs/d912fdd7/tmp/agents/tonemap-metadata (W below). Programs in W/src, built by W/build.sh
against qpv-rel/main-rel (+ qpv-asan/main-asan). Real HDR inputs: the public OpenEXR sample images (W/dl/openexr).

Summary: no regression in metadata reading/writing or in the tag API (qpv only fixes main bugs there). The tone mapping
robustness work is sound for NaN/INF/negative/flat/tiny inputs, but its 0.1% percentile white point changes renders of
ordinary HDR photos (F1, F2: Fattal02 ~2x brighter and noisier; renders and thumbnails jump when a light source crosses
0.1% of the frame). New qpv code: ConvertToLinear clips float HDR through table-based ICC curves (F3); the Exif parser
over-reads the exact-size buffers the new HEIF/AVIF/JXL loaders pass (F4, ASan); smaller contract and edge issues F5-F9.

Independence note: early on, a broad `grep -rn MustTonemap .` printed a few lines of QPV_BUGS_AND_REGRESSIONS_REPORT.md
(I did not open the file). Finding F5 overlaps one of those lines (8-bit PQ -> FITM_NONE); it is reported only because I
reproduced it independently. Nothing else below came from that file.

### F1 Fattal02 renders of ordinary HDR photos are ~2x brighter, with amplified noise, than main
- Class: regression-vs-main (side effect of an intended robustness change)
- Severity: Medium
- Where: Source/FreeImage/tmoFattal02.cpp:433 (`LuminanceRange(H, &maxLum, &minLum)`) -> tmoColorConvert.cpp:596
  LuminanceRange -> GatherLuminanceStats (tmoColorConvert.cpp:257, maximum = 99.9th percentile of the lit samples,
  TMO_MAX_PERCENTILE at :227, budget/cut at :369-392). Introduced by 9b55043 (squash of 2026-08-28).
- What: LogLuminance normalises luminance to [0..100] with the percentile maximum and clips the brightest 0.1% to 100
  before the gradient-domain compression. On real HDR photos whose light sources (candle flames, lamps, glints) cover
  < 0.1% of the frame, those highlights are flattened before the compression (and, presumably, the smaller solved range
  lets the final NormalizeY push the rest up): the picture comes out roughly twice as bright and the dark-region noise is strongly amplified (visible as coloured
  speckle over the backgrounds of StillLife and PrismsLenses). The squash commit claims "at most 1.9% of channels move
  by more than 20" on well-formed images; on these photos 57-89% do.
- Trigger: FreeImage_ToneMapping(dib, FITMO_FATTAL02, 0, 0) / FreeImage_TmoFattal02(dib, 0.5, 0.85) on StillLife.exr,
  CandleGlass.exr, PrismsLenses.exr (openexr-images ScanLines/).
- Evidence (negatives pre-clipped to 0 in the test so both builds get identical non-negative RGBF input;
  `TM_POS=1 W/bin/tm_{qpv,main} file <exr> ...`, then `python3 -I W/src/cmp.py W/out/oe_qpv W/out/oe_main -pos__`):
  mean level qpv vs main (mean |diff|, % channels off by >20), fattal_def:
  StillLife 81.8 vs 42.7 (39.0, 59.7%); CandleGlass 64.9 vs 38.4 (26.5, 77.2%); PrismsLenses 59.0 vs 28.0 (31.0, 58.3%);
  with (0.6,0.9): CandleGlass 61.4 vs 30.1 (88.5%). Desk 95.7 vs 95.3, MtTamWest 127.9 vs 123.1, Tree 121.4 vs 120.6
  (photos whose bright area exceeds 0.1%) stay close. Side-by-sides: W/view/sl_fattal.png, cg_fattal.png, pl_fattal.png.
  Root cause confirmed: W/variant/tmoFattal02_truemax.cpp (qpv file with the absolute finite maximum restored in
  LogLuminance, linked ahead of qpv-rel) gives mean |diff| 0.001 vs main on all nine renders.
- Confidence: high (measurement and root cause); medium on "worse" (no ground truth for a tone mapper, but the noise
  amplification is plain in the side-by-sides)
- Fix sketch: use the absolute finite maximum (stats.trueMaxLum) in LuminanceRange for Fattal02, keeping only the
  non-finite filtering; or raise the outlier budget well below what real light sources occupy (e.g. 1e-6).

### F2 Percentile white point: renders jump when a light source crosses 0.1% of the frame; thumbnails no longer match
- Class: regression-vs-main (side effect of an intended robustness change)
- Severity: Medium
- Where: tmoColorConvert.cpp:227 TMO_MAX_PERCENTILE 0.999 and :365-394 (robust maximum), used by LuminanceFromYxy (:457,
  Drago03 Lmax), LuminanceFromY (:569, Reinhard05 key), RGBFRobustRange (:645, Reinhard05 output normalisation,
  tmoReinhard05.cpp:196) and LuminanceRange (Fattal02, F1). Introduced by 9b55043.
- What: main takes the scene maximum as the white point, so the render varies smoothly with the content. qpv treats the
  brightest 0.1% of lit samples as outliers; a light source that covers 0.099% is ignored (scene rendered ~85-90 levels
  brighter, the light clipped) while one covering 0.101% is honoured. The same image therefore renders very differently at
  different sizes: FreeImage_MakeThumbnail (rescale, then Drago03) and a downscaled preview disagree with the full-size
  render, because resampling changes the fraction of bright pixels. Small light sources (the sun in a landscape or
  panorama, lamps, specular glints) are exactly the < 0.1% case.
- Trigger: RGBF scene of ordinary content (0.02..0.9) plus a 2000-luminance square covering N ppm of the pixels
  (W/src/tm.c scene `sunppm<N>`, W/src/thumb.c); FreeImage_TmoDrago03(dib, 2.2, 0), FreeImage_TmoReinhard05(dib, 0, 0),
  FreeImage_MakeThumbnail(dib, 200, TRUE).
- Evidence: `W/bin/tm_{qpv,main} synth sunppm<N> 512x384 -` mean level, qpv | main:
  drago_def N=800: 196.7 | 106.9; N=950: 195.5 | 106.8; N=1050: 106.8 | 106.8.
  rein_def N=950: 157.2 | 80.0; N=1050: 77.2 | 80.0. fattal_def N=950: 190.4 | 128.5; N=1050: 128.5 | 128.5.
  `W/bin/thumb_qpv` (1600x1200): ppm=800: Drago full 196.7, MakeThumbnail(200) 113.0, Rescale(400x300)+Drago 113.0,
  Reinhard full 158.4 vs 400px 81.1; ppm=600: full 197.2 vs thumbnail 113.6. `W/bin/thumb_main`: full 106.9,
  thumbnail 106.7, 400px 106.8 for every N. Root cause confirmed: W/variant/tmoColorConvert_truemax.cpp (robust maximum
  replaced by the absolute one) makes Drago03 and Fattal02 match main at every N and removes the jump in Reinhard05.
  The cut also bites the natural tail of smooth content: a lognormal scene with no discrete light (scene `dim`,
  sigma 0.8) renders Drago03 152.7 vs 136.6 (mean |diff| 16.2) and, with exposure +2, 8.6% of channels clipped to 255
  vs 0.5% in main; Reinhard05 66.0 vs 53.4 (23% of channels off by > 20). Scene `sky` (sun 0.06% + glints): Drago03 124.7
  vs 112.1, Fattal02 (0.6,0.9) 89.8 vs 69.3. On the 7 OpenEXR photos main can load, Drago03 stays within ~7 levels mean.
- Confidence: high
- Fix sketch: keep NaN/INF filtering but use the absolute finite maximum (or a much smaller budget / a soft knee), so
  the white point varies continuously with the content.

### F3 ConvertToLinear clips float samples above 1.0 when the ICC tone curve is a sampled 'curv' table
- Class: new-bug
- Severity: Medium
- Where: Source/FreeImage/ConversionLinear.cpp:66-71 (Plan::Apply -> cmsEvalToneCurveFloat) with the TRCs read at
  :535 (grey) and :545-547 (RGB); introduced by 460ecc4.
- What: For FLOAT/RGBF/RGBAF input with a matrix-shaper (or grey) profile, each sample goes through
  cmsEvalToneCurveFloat. For a tabulated curve (curv with > 1 entries) Little CMS evaluates through its 16-bit table,
  which saturates the input and output to [0..1]; parametric curves (para, curv with 0/1 entry) extrapolate. So HDR
  values above 1.0 are clipped to exactly 1.0 for profiles with table TRCs (the common v2 "sRGB IEC61966-2.1" profile has
  1024-entry curv tables), hue is lost (4,2,1 -> 1,1,1), a profile mixing curve kinds shifts hue per channel, and the
  tone mapper fed with the result renders brighter input darker. FreeImage_MustTonemap returns FITM_REQUIRED for these
  images; see "Scope of exposure" for which call flow reaches ConvertToLinear.
- Trigger: RGBF image with values up to 4.0 and an RGB profile whose rTRC/gTRC/bTRC are curv tables; saved and reloaded
  as float TIFF; FreeImage_ConvertToLinear(dib, 0).
- Evidence: `W/bin/ftiff_qpv W/icc/curvN_srgb1024.icc out.tif`: reload type=11 icc=2476 MustTonemap=2; input (4.0,2.0,1.0)
  -> linear 1.0000 1.0000 1.0000; x=31 (1.97) -> 1.0000; Reinhard05 of the result: red 255 (x=16), 222, 220, 218 (x=63),
  brighter input darker. Same file with the same sRGB curve as para type 3 (W/icc/para3_srgb.icc): 25.3141 4.9538 1.0000
  and a monotonic render 163/221/244/255. `W/bin/lin_qpv icc W/icc/*.icc` on an RGBF ramp -0.5..1.5: curvN_srgb1024 and
  curv2_lin give 1.0000 at x=1.25 and 1.5; para0/curv1 gamma 2.2 give 1.63/2.44; mixed_rgb.icc (para, curv-gamma,
  curv-table) gives 1.5000, 2.4393, 1.0000 at x=1.5. Not applicable to main (new API).
- Confidence: high
- Fix sketch: for samples outside [0,1] on a tabulated curve, extend it (e.g. pow with cmsEstimateGamma of the curve, or
  the end slope), or rebuild the table as a parametric/segmented float curve before evaluating floats.
- Scope of exposure: the TestAPI v2 sRGB-style profile test5.icc (rTRC/gTRC/bTRC = curv with 1024 entries, the layout of
  the widespread "sRGB IEC61966-2.1" v2 profile) clips the same way (x=1.25 and 1.5 -> 1.0000), and so does a LUT-only
  profile (test3.icc, through ConvertThroughProfile -> FreeImage_ConvertToICCProfile). FreeImage's own built-in profiles
  use para curves and are not affected (fi_srgb: x=1.5 -> 2.5371). Callers hit it when they follow the header comment at
  FreeImage.h:842-843 / the AHK wrapper ("the tone mapping operators take linear light: FreeImage_ConvertToLinear() gives
  it"); the FITM_REQUIRED text at FreeImage.h:848 alone would send RGBF straight to FreeImage_ToneMapping.
- Note (low confidence, not verified): Photoshop's 32-bit files store linear light while embedding the document's normal
  profile; applying the profile's TRC to such float data at all would darken them.

### F4 Exif parser over-reads exact-size buffers handed in by the new HEIF/AVIF/JXL loaders
- Class: new-bug (new callers of a pre-existing weakness; the PSD path fails the same way in main)
- Severity: Medium
- Where: Source/Metadata/Exif.cpp:1352-1391 psd_read_exif_profile (memcmp of 4 bytes at :1370 and ReadUint32(pbProfile+4)
  at :1384 with no `length >= 8` check); Exif.cpp:633 IFD bound check `(ifdp - tiffp) + 12 * nde > dwLength` omits the
  2-byte entry count, so the last entry may extend 2 bytes past the buffer (read at :534 via :759). New callers:
  PluginHEIF.cpp:619 (a7aa443), PluginAVIF.cpp:285 (77840ab), PluginJXL.cpp:834 (4b3d2f4): FindTiffHeader accepts a
  TIFF signature in the last 4..7 bytes (HEIF/AVIF), JXL passes any length >= 1.
- What: In main these routines saw JPEG APP1 data (inside libjpeg's marker pool, so slack bytes follow), PSD resources and
  JPEG XR Exif/GPS IFD blocks (jpegxr_read_exif_profile, offset 0).
  qpv now passes exactly-sized heap buffers from HEIF (malloc(size)), JXL (std::vector) and AVIF (libavif buffer); a crafted
  or truncated Exif block reads up to 4 bytes past the allocation. Out-of-bounds read only (values can end up in tags);
  a crash needs the buffer to end at a page boundary.
- Trigger: HEIF whose Exif item payload ends with "II*\0" in its last 4..7 bytes; JXL whose Exif box tiff_header_offset
  leaves 1..3 bytes; HEIF/PSD whose IFD0 entry table ends exactly at the buffer end.
- Evidence (W/bin/loadany_{qpvasan,mainasan} 0 <file>; crafted by W/heif/*.heic, W/jxl/*.jxl, W/gen/*.psd):
  short_exif_4/6/7.heic (qpv-asan): heap-buffer-overflow READ of size 1, #1 ReadUint32 Exif.cpp:146, #2 psd_read_exif_profile
  Exif.cpp:1384, #3 AttachExif PluginHEIF.cpp:619. jxl_exif_left1/3.jxl: READ of size 4 in memcmp, psd_read_exif_profile
  Exif.cpp:1370, AttachMetadata PluginJXL.cpp:834. ifd_overrun.heic: READ in processExifTag Exif.cpp:534 <-
  jpeg_read_exif_dir Exif.cpp:759 <- psd_read_exif_profile :1391 <- PluginHEIF.cpp:619. short_exif_8.heic loads cleanly.
  AVIF: not crafted; same FindTiffHeader pattern over libavif's exact-size exif buffer (PluginAVIF.cpp:281-285), so
  affected by code reading. main has no HEIF/JXL/AVIF plugin; psd_exif4.psd, psd_exif6.psd and psd_ifd_overrun.psd overflow in main-asan too
  (Exif.cpp:1351 / :756), so the PSD part is pre-existing.
- Confidence: high
- Fix sketch: `if(!data || length < 8) return FALSE;` in psd_read_exif_profile (and check dwProfileLength >= 8 after the
  signature in jpeg_read_exif_profile); make the IFD check `(ifdp - tiffp) + 2 + 12 * nde > dwLength`.

### F5 FreeImage_MustTonemap answers FITM_NONE for 8-bit PQ images (overlaps a line glimpsed in the earlier report)
- Class: new-bug (contract)
- Severity: Low
- Where: Source/FreeImage/MustTonemap.cpp:541-543 (`case FIT_BITMAP: return FITM_NONE;`, c637684; FITM_PQ added by 460ecc4
  only for 16-bit and float); doc FreeImage.h:849 and README ("FITM_PQ (3)").
- What: A standard bitmap tagged PQ (CICP transfer 16) gets FITM_NONE, "show it as it is", although FreeImage_ConvertToLinear
  decodes it as PQ and FIF_LOAD_DISPLAY_ICC deliberately leaves PQ images unconverted for the tone mappers (aef91f4). A caller
  following MustTonemap displays the raw PQ code values (washed out). 16-bit PQ gets FITM_PQ.
- Trigger: 8-bit RGB or grey PNG with a cICP chunk 9/16/0/1 (W/gen/pq8.png, pq8grey.png, made by W/src/mkpng.py).
- Evidence: `W/bin/mt_qpv W/gen/*.png`: pq8.png type=1 bpp=24 MustTonemap=0, with FIF_LOAD_DISPLAY_ICC (Display P3 display)
  still 0 with pixel 160 unchanged and cicp 9/16/0/1 kept; pq16.png MustTonemap=3; synthetic FIT_BITMAP tagged PQ -> 0, RGB16
  tagged PQ -> 3. `W/bin/lin_qpv file W/gen/pq8.png`: ConvertToLinear decodes PQ (cicp 9/8/0/1, BT.2020 profile).
  Not applicable to main (new API).
- Confidence: high
- Fix sketch: for FIT_BITMAP return FITM_PQ when TransferOf(dib, false) == TRANSFER_PQ, else FITM_NONE.

### F6 JPEG_EXIFROTATE keeps the Exif thumbnail unrotated on a rotated image
- Class: new-bug
- Severity: Low
- Where: Source/Metadata/Exif.cpp:1034-1045 ReplaceWithRotated, line 1041 `FreeImage_SetThumbnail(rotated, FreeImage_GetThumbnail(*dib))`;
  c978dd6. The header-only path (Exif.cpp RotateExif, NOPIXELS) keeps it too.
- What: Orientations 3/5/6/7/8 now carry the original thumbnail over to the turned image without turning it; main dropped
  it. (Main already left the thumbnail unflipped for orientations 2 and 4, which flip in place; qpv extends that
  inconsistency to the rotated cases.) A caller showing FreeImage_GetThumbnail of an EXIFROTATE load gets a sideways thumbnail next to an upright image
  (the Orientation tag still says 6 in both builds).
- Trigger: a JPEG with Orientation 6 and an Exif thumbnail, loaded with JPEG_EXIFROTATE (W/gen/canon40d_orient6.jpg: the
  public Canon_40D.jpg sample with its Orientation patched to 6).
- Evidence: `W/bin/rot_qpv W/gen/canon40d_orient6.jpg`: flags=0x0008 image 68x100 thumb=yes 68x46; flags=0x8008 image
  68x100 thumb 68x46. `W/bin/rot_main`: flags=0x0008 image 68x100 thumb=no (and icc=0).
- Confidence: high
- Fix sketch: rotate/flip the thumbnail with the same operation (or drop it as before), and consider setting Orientation to 1.

### F7 FI_LINEAR_SRGB_PRIMARIES is silently ignored for primaries without a chromaticity entry
- Class: new-bug (edge)
- Severity: Low
- Where: Source/FreeImage/ConversionLinear.cpp:354-364 PlanFromCode (460ecc4).
- What: For CICP primaries FreeImage has no chromaticities for (10 = SMPTE ST 428 XYZ, reserved 3, 13-21, 23+) the flag
  converts nothing, keeps the samples and tags the result (P, 8, 0, 1) with no profile; no message, non-NULL result.
- Trigger: RGB16 tagged CICP 10/13/0/1, FreeImage_ConvertToLinear(dib, FI_LINEAR_SRGB_PRIMARIES).
- Evidence: `W/bin/lin_qpv curves`: rgb16_P10_flag cicp=10/8/0/1 icc=0 with samples identical to the unconverted P2 case
  (0,1,0 / 0.000301,0.991,0.000151); P9/P11/P12/P22 are converted.
- Confidence: high
- Fix sketch: build the XYZ case (identity to XYZ) or return NULL / emit a message when the flag cannot be honoured.

### F8 Float conversions no longer clamp; ConvertToStandardType's linear scaling seeds from the data
- Class: compat-break (intended, b2ec031 and 9b55043)
- Severity: Low
- Where: ConversionRGBF.cpp:200-230, ConversionRGBAF.cpp:205-235, ConversionFloat.cpp:157-178; ConversionType.cpp:104-127.
- What: FLOAT->RGBF, FLOAT->RGBAF, RGBAF->RGBF, RGBF->RGBAF, RGBF/RGBAF->FLOAT keep values outside [0..1] (main clamped);
  ConvertToStandardType(TRUE) maps [data min, data max] to [0, 255] (main used [min(min,255), max(max,0)]). Internal callers
  checked: tone mappers (intended: RGBAF and RGBF now tone map identically; main clamped RGBAF, e.g. fi_exr_b44 Drago 177.5
  qpv vs 194.5 main, RGBF copies agree within 0.5), MakeThumbnail (RGBAF->RGBF; ConvertToStandardType for INT16/UINT32/
  INT32/FLOAT/DOUBLE), PSD grey float + alpha, JXL save, ConversionLinear: none gets a worse result. External callers that
  relied on [0..1] output (e.g. their own v*255 cast) can overflow.
- Evidence: `W/bin/fconv_{qpv,main}` values (2, -0.5, NaN, 0.25): qpv keeps 2 -0.5 nan 0.25 in all six conversions, main
  gives 1 0 nan 0.25; INT16 [-500..-100] -> std: qpv 0 64 191 255, main 0 51 153 204; UINT16 [1000..60000]: qpv 0 82 169 255,
  main 3 84 170 255.
- Confidence: high
- Fix sketch: document in README / FreeImage.h; nothing to fix internally.

### F9 FreeImage_MustTonemap changed signature days after it was added; FreeImage_MustTonemapU removed
- Class: compat-break (qpv-internal API churn; not vs main)
- Severity: Low
- Where: Source/FreeImage.h:1117, MustTonemap.cpp:579-582; 4cdd46d (2026-10-03) replaced `const char *filename` by
  `FREE_IMAGE_FORMAT sourceFmt` and deleted the exported FreeImage_MustTonemapU added in c637684 (2026-09-30).
- What: Binaries built against the 09-30 DLL pass a char pointer where an int format is now read (FIF garbage; NULL now means
  FIF_BMP rather than "unknown") and fail to bind MustTonemapU. The AHK wrapper was updated (passes "Int", FIF).
- Evidence: `git show 4cdd46d` (header and definition diff).
- Confidence: high
- Fix sketch: none needed if no DLL built between 2026-09-30 and 2026-10-03 shipped to other callers (then the item is
  moot); otherwise keep MustTonemapU / the old signature as a shim.

## Outside my scope
- Little CMS TetrahedralInterp16 signed-integer overflow (UBSan, cmsintrp.c:845) reached through FreeImage_ConvertToICCProfile
  from ConvertToLinear's LUT-profile path on a mutated test3.icc (W/out/iccfuzz/test3.icc.fail.143): vendored lcms / colormgmt.
- TIFF save of IPTC: main writes a 69-byte IPTC/NAA blob that reads back with 0 IPTC tags; qpv writes 273 bytes that round-trip
  (qpv fix, PluginTIFF).
- main segfaults loading Blobbies.exr (negative data window); qpv loads it (EXR plugin).
- rgb64half.jxr / rgbe.jxr load differently in main and qpv (JXR plugin); MakeThumbnail of float images smaller than the
  requested size returns float in main, 24-bit in qpv (Rescale).

## Checked, no issue found
- Transfer functions of ConvertToLinear against reference formulas (W/src/chk_curves.py): sRGB, BT.709/601/2020 (0.3% at
  the knee from the rounded BT.709 constants only), gamma 2.2/2.8, SMPTE 240M, linear, log100/log316, PQ (/203), HLG incl.
  BT.2100 display step (0.75 -> 1.0005; BT.2020 vs BT.709 weights), SMPTE 428; 16-bit and float input, negative signals.
  H.273 chromaticity table and the lcms parametric parameters (GetCICPToneCurve) by reading. ICC TRCs curv 0/1/n entries
  and para types 0-4 on 16-bit input match lcms evaluation.
- ICC fuzzing of ConvertToLinear (both flags), MustTonemap and Drago03 (DescribeToneMapped -> GetToneMappedProfile,
  TwinCache) under qpv ASan/UBSan: 19 seed profiles (generated curv/para variants, TestAPI/ICC/data incl. bad/toosmall/
  bad_mpe/test3/test5) x 1250 mutations each (truncation, byte flips, extreme tag offsets/sizes, curv counts, para
  types, swapped tag signatures; two runs, seeds 0-249 and 250-1249, both ALLDONE) x 7 image types: no memory error or
  leak; one UBSan report, the lcms one under "Outside my scope".
- Tone-mapped output profiles/tags (580cdba, 3eb811e, 6ba52f6, 4128ed3): BT.2020 PQ AVIF made linear keeping its primaries and
  tone mapped carries cicp 9/13/0/1 + a BT.2020/sRGB-curve profile; colour-managed to sRGB it matches the sRGB-primaries
  path within 0.5 levels mean for all three operators; sRGB-primaries input gets no profile; MakeThumbnail of RGBF/RGBAF keeps
  comments, XMP and resolution.
- Tone mapping NaN/Inf/negative/black/flat/ramp/tiny inputs: qpv gives pictures where main gave black/white/NULL/crash
  (main SIGSEGV/ASan heap overflow in Fattal02 below 32 px and in the Poisson solver at 2x2; qpv returns NULL / works);
  qpv never returns NULL where main succeeded (Fattal02 at 1, 2, 3, 4, 5, 8, 16, 31, 32 and 33 px square, 31x200, 200x31,
  32x1, 64x2; Drago03/Reinhard05/Poisson at 1x1, 2x2, 3x3, 5x5, 1x7, 9x1); qpv ASan/UBSan clean on all
  synthetic scenes. Drago03/Reinhard05 on the 7 OpenEXR photos main can load: within ~7 / ~11 levels mean of main once main's
  negative-radiance breakage is removed (worst: Reinhard05Ex(0, 0.7, 1, 0) on CandleGlass and StillLife, mean |diff| ~11,
  12-24% of channels off by > 20); with negatives left in, main is the broken one (Desk: hazy with blown-out pixels).
- Reinhard05 5cf28ba key change, Drago03 black-scene path, ClampConvertRGBFTo24 NaN handling, findMaxMinPercentile
  (dead in practice: NormalizeY(…, 0, 1) only), MultigridPoissonSolver type/size checks.
- MustTonemap verdicts for every image type, untagged/PQ/HLG/linear tags, header-only loads (documented to skip statistics),
  FITM_ERROR for NULL, FITM_UNSCALED precedence; ExpandNarrowRange memory safety by reading; thread-safety of new globals
  (mutex-guarded caches, per-call lcms contexts, OpenMP rows read-only).
- Metadata read: 33 camera JPEGs (exif-samples: maker notes of Canon/Nikon/Fuji/Olympus/Pentax/Sony/Panasonic/..., GPS,
  interop, malformed tests) + 37 TestAPI JPEG/TIFF/PNG/WebP/JXR/DNG files: tag-by-tag dumps (key, id, type, count, length,
  value hash, TagToString, thumbnail, ICC, resolution) identical between main and qpv, except intended RAW CICP tags/profiles.
- Metadata write: JPEG/TIFF/PNG/JXR/WebP saves of 4 Exif/IPTC/XMP sources by each build read back identically by both
  readers; files are the same metadata-wise except the TIFF IPTC fix above.
- Tag API in normal conditions: set/get/count/iterate, delete tag/missing tag/absent model, delete model, Clone,
  CloneMetadata, empty ASCII tags, SetTagValue count mismatch keep the old value: same as main; FindFirstMetadata on an
  emptied model (main: non-NULL handle with a garbage tag) and SetMetadata/CloneTag of a tag without a value (main: SEGV in
  memcpy, FreeImageTag.cpp:134) are fixed in qpv. IPTC return value change and XTIFF NULL-value skip are harmless (callers
  ignore the result). JPEG_EXIFROTATE now keeps the ICC profile (main lost it) and header-only sizes follow the rotation.
- Exif thumbnail bounds (1d9d70a, CVE fixes in 6e64300/c8c9132): checks are correct and only reject malformed data.
