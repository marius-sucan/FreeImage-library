# png-apng-gif agent: findings (PluginPNG, PluginAPNG, PluginGIF, LibPNG 1.6.58), received 2026-10-19
Workspace W=$J/agents/png-apng-gif (tools W/t, outputs W/out, generated W/gen, downloads W/dl: PngSuite 2017jul19, philip.html5.org APNG tests 000-060, Pillow 12.1.1 Tests/images). LibPNG stock 1.6.58, no FreeImage patches; pnglibconf.h keeps main's limits.
Summary: no regression vs main on any valid PNG, GIF or APNG. Problems are in the new APNG writer (F1, F2, F5), the new identification (F3), cut-file page counting (F4), one documented writer refusal (F6).

## F1 APNG writer stores GIF frames (any page without a BlendMethod tag) with blend_op SOURCE: transparent pixels punch holes
- Class: new-bug. Severity: Medium (High if QPV offers GIF->APNG conversion: transparency-optimised GIFs all break). Confidence: high.
- Where: PluginAPNG.cpp:81 (default blend_op = APNG_BLEND_OP_SOURCE), :1068-1071 (ReadFrameTags changes it only if "BlendMethod" exists), written :1335; 0f9863f. PluginGIF never sets BlendMethod; WebP (PluginWebP.cpp:699) and JXL (PluginJXL.cpp:2108) writers default to blend.
- Evidence: W/gen/d1/partial.gif (40x30; frame 1 20x10 at (5,5), left half transparent, disposal leave): `bin/ficonv_qpv -m 25 partial.gif 39 partial.apng.png`; `t/apng_oracle.py` -> `frame 1 ... dop 0 bop 0`; GIF_PLAYBACK vs APNG_PLAYBACK frame 1: 100 pixels differ (GIF [255 0 0 255], APNG [255 255 255 0]); independent decoder agrees with qpv's reader (file is wrong). fullopt.gif: 1184 of 1200 pixels transparent. Same pages saved as lossless WebP: 0 differing pixels.
- Fix sketch: pages with animation tags but no BlendMethod default to OVER (as WebP/JXL), or tag GIF frames with BlendMethod 0.

## F2 APNG writer stretches the contrast of 16-bit grey pages, each frame separately
- Class: new-bug. Severity: Medium. Confidence: high.
- Where: PluginAPNG.cpp:1124-1125 FreeImage_ConvertToStandardType(dib, TRUE); 0f9863f. The reader uses FreeImage_ConvertTo8Bits (high byte) at :754.
- What: scale_linear maps each frame's own min..max to 0..255 (ConversionType.cpp:102-140); flat frames clamp; animations flicker. Taken for any >= 2-page animation, or a single page with animation tags.
- Evidence: `bin/u16apng_qpv gen/d2/u16.png` (t/u16apng.c): page 0 gradient 1000..2008 (expected 3..7) saved as 0,85,170,255; page 1 flat 30000 (expected 117) saved as 255. Pillow reads the same.
- Fix sketch: FreeImage_ConvertTo8Bits(dib) (as RenderFrame), or write 16-bit APNG.

## F3 Animated PNGs identified as FIF_APNG (39); FreeImage_Validate(FIF_PNG) rejects them
- Class: compat-break (intentional, 0f9863f). Severity: Medium (agent). Confidence: high.
- Where: PluginPNG.cpp:335-357 (Validate FALSE when APNG_IsAnimatedStream, :348-354); PluginAPNG.cpp:264-295 (any acTL before IDAT); page list :494-515; ":399 num_frames is advisory".
- What: 54 of 419 Pillow test PNGs affected. GetFileType 39 vs 13; Validate(FIF_PNG) FALSE for a valid PNG; GetFIFFromFilename stays 13. Load(GetFileType(f)) on an APNG whose IDAT is not part of the animation returns the first fcTL frame, not the default image. Invalid num_frames files (conformance 047-050) play frames where the spec asks for the default image. Forcing FIF_PNG = main bit for bit on all 419.
- Evidence: `bin/validate_{qpv,main}` on dl/apngtests/002.png: 39 vs 13, Validate 0 vs 1; auto-load 002.png qpv green (anim frame) vs main red (default); 047.png qpv red, main green. W/out/apngauto/summary.txt.
- Fix sketch: document; optionally keep Validate(FIF_PNG) TRUE and choose inside GetFileType.

## F4 GIF and APNG count a frame cut before its first row: header-only describes it, full load returns NULL
- Class: new-bug. Severity: Low. Confidence: high.
- Where: PluginGIF.cpp:720 (Open keeps a frame once its first sub-block length byte is read) vs :1290-1294 (Load throws "frame holds too little data"); header-only branch :1217 never checks; a03b1f1, ca839e8, 42c553b. APNG: PluginAPNG.cpp:446-448, :485-492 keep a frame with any fdAT bytes; PNG loader refuses no decoded row (PluginPNG.cpp:722-725); cda7638.
- Evidence: gen/d5/cut3.gif `bin/nopix_qpv 25 0`: page 1 full NULL, header-only 60x40 4bpp; gen/d5a/cut6.png page 9 full NULL, header-only 60x45, APNG_PLAYBACK NULL. main GIF Open fails (0 pages); no APNG.
- Fix sketch: drop such frames when building the page list, or return the blank frame.

## F5 APNG writer: when frame 0 does not cover the canvas, the default image (IDAT) is fully transparent
- Class: new-bug (interop). Severity: Low. Confidence: high.
- Where: PluginAPNG.cpp:1308-1320 (first_is_default false -> CompressBackdrop :1257-1268 writes a blank IDAT not in the animation); 0f9863f.
- Evidence: gen/d13/small0.gif (60x40 screen, frame 0 30x20 at (10,5)) -> `ficonv_qpv -m 25 small0.gif 39 small0.apng.png`: chunks IHDR sRGB gAMA cHRM pHYs acTL IDAT(32) fcTL fdAT fcTL fdAT IEND; `fidump_main -f 13`: 60x40 max alpha 0; Pillow n_frames 3 default_image True.
- Fix sketch: write frame 0 padded on a transparent canvas as IDAT with a full-canvas fcTL.

## F6 PNG writer now refuses FIT_INT16, FIT_FLOAT, FIT_RGBF and 16-bit 555/565
- Class: compat-break (intentional, 85e8c87; README says INT16 must be converted first). Severity: Low. Confidence: high.
- Where: PluginPNG.cpp:1099-1106.
- What: main TRUE for all; main wrote garbage for 555/565/FLOAT/RGBF and over-read for 16-bit bitmaps (ASan heap-buffer-overflow READ 111 in png_write_row) -> refusing is a fix. FIT_INT16: main wrote a valid 16-bit grey PNG of the raw two's-complement bits (Pillow reads I;16 102..65535); qpv returns FALSE.
- Evidence: bin/pngsave_{qpv,main}, W/out/pngsave.txt.

## Pre-existing (same in main)
- GIF writer: interlaced frames 2-4 rows tall lose every row but the first (PluginGIF.cpp:1767 `if` vs reader fix 1e3db05 `while` :1265); 23 frames in a 150-file battery, identical in both.
- GIF LZW decoder UB on corrupt minimum code size (`1 << m_minCodeSize` :257, `(1 << m_codeSize) - 1` :523); no memory error.
- Header-only PNG loads lack tEXt/iTXt after IDAT (both; inherent).
## Outside scope (agent notes)
- WebP multi-page writer: AppendPage of 8-bit palettised pages fails ("WebP cannot encode this image"), yet CloseMultiBitmap returns TRUE and no file is written (W/gen/d1, partial.webp missing). -> mng-webp scope.
- main's OpenMultiBitmap on plain PNG may not honour FIF_LOAD_NOPIXELS like qpv (MultiPage territory).

## Checked, no issue found (condensed)
- PNG loads (FIF_PNG forced; flags 0, PNG_IGNOREGAMMA, NOPIXELS): 176 PngSuite files bit-identical to main (type, bpp, palette, tRNS, bKGD, pHYs, metadata, pixels) except xcsn0g01 (IDAT CRC, salvaged by design); 419 Pillow PNGs identical except 4 damaged files now salvaged.
- libpng 1.6.58 vs 1.6.39: same limits; gAMA/sRGB/iCCP/cICP behaviour as intended; narrow-range expansion; header-only = full on synthetic cICP files.
- PNG salvage: byte-flip and zero sweeps: wherever main loads, qpv returns the identical image; main aborts 8-10 times per file on IEND damage (double free of row_pointers, main PluginPNG.cpp:790 after :766); qpv ASan clean; salvaged rows exact.
- PNG saves (1/4/8-bit, palettes, tRNS, 24/32, UINT16/RGB16/RGBA16, interlaced): TRUE in both, Pillow decodes source pixels; text/XMP/tIME/pHYs/bKGD round-trip; ICC: same profiles embedded; where 1.6.39 failed the save qpv saves without the profile and warns; failed writes stop the save.
- APNG reading: APNG_PLAYBACK = independent oracle within ±1 on philip 000-038 and Pillow apng/*; LINEAR_BLEND exact vs oracle on 11 files; delays, num_plays, all dispose/blend combos correct; order-independent; header-only = full on 115 files.
- APNG writing: APNG->APNG round trips identical; Delete/Move/InsertPage correct; mixed page types ASan/leak clean; ICC conversion to frame 0's profile correct (LittleCMS reference). A GRAY first-frame profile is dropped silently (no warning).
- GIF reading: 38 Pillow GIFs + TestAPI sample.gif raw frames/palettes identical to main; playback differs only where main is wrong (no-GCE frames read 'I' as flags; junk after last frame); 150 random animations match a reference decoder exactly (main: 23 raw + 120 playback differences); off-screen frames clipped correctly; cache order-independent; header-only = full; corruption sweeps: raw frames identical wherever main loads (main crashes on several variants).
- GIF writing: re-saves decode to same pixels/rects/disposal/delays (except pre-existing interlace bug); cross-build files agree; LZW encoder output byte-identical to main on 120 random images up to 3000x2000; Pillow decodes all except 3 short-interlaced.
- Large valid files: 16000x16000 uniform PNG and 12000x12000 uniform GIF load as in main; PlausibleImageSize applies only to cut GIF frames (margin 4096 x 64).
- Thread safety: no new global state in the three plugins.
