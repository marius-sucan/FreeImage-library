# mng-webp agent: findings (PluginMNG, MNGHelper, PluginJNG, PluginWebP, LibWebP patch), received 2026-10-19
Workspace $J/agents/mng-webp (src/, bin/ via build.sh, gen/, out/). Inputs: libmng MNGsuite-1.0 + 20030305 + allowed mngsuite (63 MNG), JNGsuite (30), TestAPI/WebP/data (6), libwebp-test-data (131). Oracle: Pillow 12.1.1 (libwebp 1.5.0).
Summary: 12 findings, 4 Medium, 8 Low. Only F1 is a regression vs main; F2-F4 are in new animation writers/playback.

## F1 MNG: the file's global tRNS is applied to embedded PNGs that must not inherit it
- Class: regression-vs-main (grey/RGB) + new-bug (indexed with own PLTE). Severity: Medium (spec-based; no suite MNG has a top-level tRNS). Confidence: high.
- Where: PluginMNG.cpp:1242-1245 (BuildPNGStream); f355509. MNG 1.0 §4.2.2: global tRNS inherited only with the global PLTE, only by PNGs with an empty PLTE.
- Evidence: `python3 -I src/gen_trns.py gen/trns`; `bin/px_{qpv,main} gen/trns/*.mng`: trns_grey qpv transparent=1, 255 -> (255,255,255,0), main opaque; trns_rgb qpv 32-bit white -> alpha 0, main 24-bit opaque; trns_ownplte qpv own colours + global alpha, main opaque with global colours; trns_emptyplte (control) qpv correct, main no alpha.
- Fix sketch: add the global tRNS only where the global PLTE is used.

## F2 Saving an animation opened with WEBP_PLAYBACK or MNG_PLAYBACK shifts the frames and grows the canvas
- Class: new-bug. Severity: Medium (file silently damaged on edit/Save As). Confidence: high.
- Where: pages tagged PluginWebP.cpp:628-651 (SetFrameMetadata, called 777-779), PluginMNG.cpp:1786-1817 (called 2203); writers use tags PluginWebP.cpp:704-710 / 1197-1206, PluginMNG.cpp:2262-2278; MultiPage.cpp:732 keeps the PLAYBACK bit in read_flags when reloading unchanged pages; ed09abe (WebP), f355509 (MNG).
- What: a playback page is the composited canvas but keeps the stored frame's FrameLeft/FrameTop/Disposal/Blend; on save the writer places each canvas-sized page at the stored offset: canvas grows, frames displaced, save returns TRUE. GIF_PLAYBACK pages carry only FrameTime (not affected).
- Evidence (bin/editpages_qpv, bin/t_saveas_qpv; Pillow decodes): a_lossless_alpha.webp 64x48 flags 0 -> 64x48 maxdiff 0; WEBP_PLAYBACK -> 92x50, warning "The frames need a 92 pixel wide canvas...", frames 1-4 maxdiff 255; d_mixed_minsize.webp 96x64 -> 144x66; a.mng 64x48 MNG_PLAYBACK delete page -> MHDR 92x66, pages at (4,2),(16,10),(28,18); Save As to WebP 104x74; GIF control 64x48.
- Fix sketch: tag composited pages as full-canvas frames (0,0, blend overwrite, disposal none), or strip PLAYBACK from read_flags in FreeImage_SavePages.

## F3 The MNG writer changes how converted animations play back
- Class: new-bug (main has no MNG writer). Severity: Medium. Confidence: high.
- Where: PluginMNG.cpp:1999-2000, 2273-2278, 1982-1991 (back[6]=1 at 1987), 2295-2299; no BlendMethod handling in Save/WriteMNG; ab7de6d.
- What: (a) DisposalMethod 2 becomes FRAM framing mode 3 = background over the whole canvas (§4.3.2), erasing earlier frames (optimised GIFs use disposal 2 constantly); disposal 3 written as "leave". (b) BlendMethod 1 (overwrite) ignored (MNG layers composite over -> trails). (c) page 0's advisory background colour written as mandatory BACK -> transparent areas opaque.
- Evidence: (a) gen/conv/sp.gif -> sp.mng: frame 2 GIF_PLAYBACK 576 opaque pixels, MNG_PLAYBACK 320 (frame 0 gone); WebP keeps frame 0. (b) Pillow f_kmax1.webp -> MNG: frames 2-5 maxdiff 255 on 255-1064 pixels. (c) transparent GIF -> MNG `BACK 000000 01`: (0,0,0,0) -> (0,0,0,255); 32-bit PNG with bKGD -> opaque white background.
- Fix sketch: zero-delay clipped background subframe for disposal 2; composited canvases for blend=1/disposal 3; BACK advisory or omitted.

## F4 The WebP animation writer moves frames with odd offsets by one pixel
- Class: new-bug (rounding intended per a48037f). Severity: Medium for converted animations. Confidence: high.
- Where: PluginWebP.cpp:704-710 (& ~1); disposal 711-714 (GIF disposal 3 written as none); a48037f.
- Evidence: odd.gif (frames at (5,3),(17,11),...) -> odd.webp: offsets read back 4,2; WEBP_PLAYBACK vs GIF_PLAYBACK every frame differs (maxdiff 255, 355-920 px); shifting by (1,1) -> identical.
- Fix sketch: pad odd-offset frames with a transparent column/row and blend over at the even offset.

## F5 FreeImage_Save(FIF_WEBP) of a frame-tagged bitmap writes an animated WebP that still-image decoders refuse
- Class: compat-break (intended, a48037f "Behaviour change"). Severity: Low. Confidence: high.
- Where: PluginWebP.cpp:1142 (as_frame), 1076-1118. Any 24/32-bit bitmap with FrameTime written as a 1-frame animation on its LogicalWidth x LogicalHeight canvas; libwebp collapses to a still only if the frame covers the canvas; else WebPDecode* (dwebp, simple API readers) refuse. main wrote a still.
- Evidence: load d_mixed_minsize.webp (first frame 16x12 on 96x64), FreeImage_Save(FIF_WEBP, WEBP_LOSSLESS): qpv `WebPGetFeatures 96x64 has_animation=1; WebPDecodeRGBA -> FAILED`; main `16x12 has_animation=0; decoded`.

## F6 MNG_PLAYBACK (0x2) reaches the JPEG decoder as JPEG_ACCURATE
- Class: new-bug. Severity: Low. Confidence: high.
- Where: PluginMNG.cpp:1715 passes flags to DecodeFrame; 1478 FreeImage_LoadFromMemory(FIF_JNG, hmem, flags); f355509. APNG strips its playback flags (PluginAPNG.cpp:1529).
- Evidence: MNGCOL1.mng playback canvas differs from the raw page by up to 3 levels on 5958 of 38240 px; extracted JNG hash differs with flags 0 vs 2.
- Fix sketch: DecodeFrame uses flags & ~MNG_PLAYBACK.

## F7 WebP writer fails the whole save for loop counts > 65535 or frame times outside 0..2^24-1 ms
- Class: new-bug. Severity: Low. Confidence: high.
- Where: PluginWebP.cpp:1107-1115 (+1217-1219), 701-703, 1062/1241; a48037f. Values go straight to libwebp mux; every page's cache store fails; AppendPage FALSE with misleading "WebP cannot encode this image"; CloseMultiBitmap TRUE, no file.
- Evidence: MNG with TERM 3,0,0,100000 -> Loop=100000 -> "Failed to set the animation parameters", appends 0, close 1, no file; FrameTime -5 or 0x7FFFFFFF -> "Failed to add a frame".
- Fix sketch: clamp loop and duration.

## F8 WebP writer frames untagged pages differently from the MNG (and APNG) writers
- Class: new-bug. Severity: Low. Confidence: high.
- Where: WebP PluginWebP.cpp:693-703 (duration 0) + calloc'd out_loop (0 = forever); MNG PluginMNG.cpp:2260 (100 ms), out_loop(1) at 248; APNG 100 ms (PluginAPNG.cpp:1045-1046).
- Evidence: bin/t_untagged_qpv: WebP time=0 loop=0; MNG time=100 loop=1.

## F9 An allocation failure while encoding a WebP frame can give a successful save of a wrong image
- Class: new-bug for the animation path; still-image path pre-existing (main identical code, main crashes on the same sweep). Severity: Low. Confidence: high.
- Where: PluginWebP.cpp:911 (FreeImage_FlipVertical result unchecked), 919-935 (WebPPictureImport* results ignored).
- Evidence: bin/oomsave_qpv: webp24/32 n=5 TRUE with vertically flipped output; webp24 n=7 TRUE with an 88-byte garbage file; webpanim n=166/434/722 Close TRUE, frame 0 flipped (backtrace FlipVertical <- EncodeImage <- WebP_EncodeToData <- Save). main aborts free(): invalid pointer on the still path.

## F10 MNG: signed overflow with huge DEFI/FRAM values
- Class: new-bug (UB). Severity: Low (output benign: blank canvases). Confidence: high.
- Where: PluginMNG.cpp:1608 (right = left + frame_width), 1086, 421-424; f355509. UBSan reports at 1608, 1086, 421-422; no ASan error.

## F11 MNG: a BASI image with one empty IDAT is not filled
- Class: new-bug. Severity: Low. Confidence: high.
- Where: PluginMNG.cpp:1294-1305 (HasImageData counts an empty IDAT), 1465. §4.2.5: no IDAT or a single empty IDAT -> fill. gen/basi/basi_noidat.mng 8x8 fill OK; basi_emptyidat.mng NULL (main NULL for both).

## F12 Writers read a 2-byte value from a FIDT_SHORT animation tag without checking its length
- Class: new-bug (same pattern pre-exists in main's GIF writer PluginGIF.cpp:1099-1100). Severity: Low (caller-made tag). Confidence: high.
- Where: PluginWebP.cpp:674 (WebP_GetAnimTag); PluginMNG.cpp:2268, 2271, 2285, 2288. ASan heap-buffer-overflow READ 2 (src/t_weird.c case 4: FrameLeft SHORT count 0).

## Pre-existing (same in main)
- JNG with alpha: header-only reports 24/8-bit, full load 32-bit (mng_ReadChunks skips alpha when header-only); now visible through MNG pages too since MNG advertises SupportsNoPixels.
## Outside scope (agent notes)
- APNG_PLAYBACK pages carry stored offsets like F2 (PluginAPNG.cpp:842-843). MultiPage.cpp:732 keeps playback flags when re-reading pages (F2's second half).
- Multi-page closes re-encode every page with the close flags: an edited lossless WebP animation closed with flags 0 becomes lossy q75 (maxdiff 95-232).
- FreeImage_CloneMetadata skips FIMD_ANIMATION (main BitmapAccess.cpp:1294, upstream): ConvertTo32Bits + CloneMetadata drops frame tags.
- CloseMultiBitmap returns TRUE when every AppendPage failed.
- libjpeg UBSan shifts (jdhuff.c, jidctint.c).

## Checked, no issue found (condensed)
- WebP still loading: 137 files identical to main (dims, bpp, pixels, ICC, dpm, metadata counts); FreeImage_Load of 8 animated WebPs returns main's first raw frame; header-only = full.
- WebP mutation differential: 3024 loads: zero cases where main loads and qpv fails/differs; qpv salvages 1581 more; ASan clean.
- WebP still saves (24/32 x ICC/EXIF/XMP x flags 0,1,50,100,127,0x100,0x132): decode in Pillow and cross-decode; lossless exact; lossy same size as main; VP8X flags and metadata chunks identical.
- 8-bit pages to WebP (lead from png-apng-gif): AppendPage FALSE with a message; Close TRUE and writes nothing; existing target untouched; main aborts (free(): invalid pointer). Not a bug per agent (but see "CloseMultiBitmap returns TRUE when every AppendPage failed").
- WEBP_PLAYBACK vs Pillow on 7 animations: zero visible-pixel differences; durations/loops equal; header-only = full; ASan clean.
- WebP animation writing round-trips offsets (even), durations, disposal, loop; mixed profiles converted to frame 0's (maxdiff 0/3 vs lcms); deleting page 0 keeps canvas; edited page keeps timing.
- MNG/JNG loading: 63 MNG + 30 JNG match main except intended changes (hidden objects skipped; DUTCH1 and PNGBGxxx now load); every page full vs header-only and forward vs backward agree; ASan/UBSan/LSan clean (except libjpeg UB).
- MNG/JNG mutation differential: 1134 MNG + 702 JNG loads: no main-loads/qpv-fails case; bad-CRC control chunks now ignored (more lenient).
- TERM/LOOP/tEXt behaviour per spec; MNG writer chunks well formed and load in main as first frame; JNG cross-load identical between builds; failing writes: all WebP/MNG/JNG writes use write_proc(buf,1,n,h), byte-count callbacks behave the same; partial sinks fail saves at every cut.
- OOM sweeps: MNG animation close (408 allocations), JNG saves fail cleanly; WebP only F9.
- Large compressible: 8000x8000 lossless WebP (2.5 KB) and 62 KB 8000x8000 MNG load identically in both.
- LibWebP a4b340d patch (vp8l_enc.c:1120) correct. JNG stream offset (PluginJNG.cpp:122) correct.
