# JPEG XR: LibJXR and PluginJXR checked against ITU-T T.832

This report checks FreeImage's JPEG XR code against the JPEG XR standard. It asks two questions: does the code follow the specification, and which parts of the specification does it not implement? The code is `Source/LibJXR`, which is Microsoft's jxrlib with FreeImage's `// FreeImage:` patches, together with `Source/FreeImage/PluginJXR.cpp`. Both were taken at qpv 3edde65 and built the way FreeImage builds them (`-D__ANSI__ -DDISABLE_PERF_MEASUREMENT`, `REENTRANT_MODE`).

The report was written on 2026-10-12. **No source file was changed by this review.** R1, D1, E1 and D2 have been fixed since; their rows say how.

## The documents used

| Document | Role |
|---|---|
| ITU-T T.832 (06/2019), identical to ISO/IEC 29199-2, 4th edition | The specification checked against. Clause and line references use its text. |
| T.832 editions 03/2009, Cor. 2 (05/2011), 01/2012 and 08/2016 | Diffed to date each feature. Cor. 1 could not be downloaded. |
| ITU-T T.834 (10/2014 text, 01/2010 data attachment) | Conformance testing. A decoder conforms if its output **matches the T.835 reference decoder exactly** (§6.4). |
| ITU-T T.835 (08/2016), reference software 1.41 | The reference encoder and decoder. Built from the ITU zip. |

All four are free downloads from itu.int.

## How to read it

Every finding gets a verdict, a layer, a severity and a note on how it was proved.

**Verdict**
- **Deviation**: the code decodes a conformant file differently from the spec, or writes a file that breaks a "shall".
- **Missing**: the spec feature is not implemented.
- **Stricter**: the code refuses something the spec allows.
- **Looser**: the code accepts something the spec forbids.
- **Extension**: the behaviour is outside the spec.
- **Unclear**: the spec text and the reference software disagree.

**Layer**
- **core**: `Source/LibJXR/image`, the codec.
- **glue**: `Source/LibJXR/jxrgluelib`, the Annex A container and pixel formats.
- **plugin**: `PluginJXR.cpp`.

**Severity**
- **High**: wrong pixels in files that exist, or a memory error on a crafted file.
- **Medium**: an interoperability failure that real files can hit.
- **Low**: rare or forward-compatibility only.
- **None**: no practical effect.

**Evidence**
- **ref**: decoded by both decoders and compared with the T.835 reference decoder.
- **repro**: reproduced in a test program here.
- **code**: found by reading the code only.

## Summary

**The decoding core follows the specification closely.**
- **416 of the 463 files** in the official T.834 conformance attachment decode **bit-identically** to the T.835 reference decoder. Those files cover:
  - all 20 adaptive VLC tables and every QP corner case of QuantMap;
  - overlap modes 0, 1 and 2;
  - 1×1 up to 254×169 tiles, including hard tiles;
  - frequency mode;
  - index tables with reordered and shared packets;
  - windowing, LONG_WORD_FLAG and SHORT_HEADER_FLAG 0 and 1, and levels 4 to 255;
  - interleaved and planar alpha;
  - every Baseline and Main output format, including 3- to 8-channel images.
- Crafted streams also decode bit-exactly for TRIM_FLEXBITS 0–15, the red/blue flag, the four VLW_ESC forms, extra RESERVED_A bytes, SPATIAL_XFRM_SUBORDINATE, and the reserved fields jxrlib does ignore (RESERVED_E/F/G/H/L). RESERVED_C, RESERVED_D, RESERVED_I_BIT and RESERVED_J_BIT are not ignored (D7).
- jxrlib's own encoder output decodes bit-exactly in both decoders for **459 of 507** test files (407 before D1, E1 and D2 were fixed).

**The other 47 suite files:**
- **28** Chroma_Centering files differ. jxrlib ignores CHROMA_CENTERING when it upsamples 4:2:0 and 4:2:2 chroma. T.832 allows any upsampling filter; the T.834 test requires the nominal one.
- **19** Advanced-profile files are refused. They use output formats jxrlib does not implement: CMYKDIRECT, YCC at 10 or 16 bits, and YCC with alpha.

**Real deviations are outside what the suite covers.**

| | Finding | Severity | Status |
|---|---|---|---|
| R1 | A crafted codestream (YONLY declared as 16bppYCC422) **overflows the heap through `FreeImage_Load`**. | High | **fixed** |
| D1 | **Hard tiles:** column and row tile boundaries are swapped in overlap filtering, in both the decoder and the encoder. Any non-square tile grid decodes wrongly. | High, for such files | **fixed** |
| D2 | **Interleaved alpha output:** the colour plane's SCALED_FLAG and SHIFT_BITS are applied to the alpha plane, so jxrlib decodes its own lossless 4:2:0 RGBA files with wrong alpha. Alpha is also rounded with 4 where the spec says 3. | Medium | **fixed** |
| C1 | Every FreeImage alpha save writes ALPHA_BYTE_COUNT as an end offset. | Medium | open |
| C2 | A FreeImage save with Exif Copyright and XMP **is rejected by the reference decoder**: IFD tags are out of order. | Medium | open |
| C4 | Every FIT_RGBF save **is rejected by the reference decoder**: it uses a WIC pixel-format GUID that is not in the spec. | Medium | open |
| F1 | Black-is-one bilevel images (BD1BLACK1) load inverted. | Medium | open |
| R2 | An unchecked QP index in frequency mode reads out of bounds. | Medium | open |

**Not implemented:**
- CMYKDIRECT and most YCC output formats;
- interleaved alpha for n-channel and YCC images;
- more than 16 components;
- the PTM_COLOR_INFO, PROFILE_LEVEL_CONTAINER and COLOR_SPACE tags;
- multi-image files (previews and pages);
- Annex E's `image/jxr` media type in FreeImage;
- Annex F (JPEG XR stored in HEIF), and the JPX box-based file format.

**In Annex B terms**, jxrlib is a Main-profile decoder, with the exceptions of D3–D5. It is not an Advanced-profile decoder.

## 1. Method

**The reference decoder.** T.835 was compiled from the ITU zip. Its `.raw` output (the output-formatting result: interleaved, or planar for YUV and CMYKDIRECT) is the reference.

**The decoder under test.** `jxrdec.c` decodes through the glue exactly as PluginJXR does:
- `uAlphaMode` 2;
- the container's pixel format;
- a memory stream with FreeImage's EOS meaning ("data remains"), whose short reads copy what is there, as `FreeImage_ReadBytes` does.

Comparison is byte for byte. Only layout is normalised: T.835 drops pad channels and writes YUV444 as separate planes.

**Conformance data.** The 2010 T.834 attachment (463 `.jxr` files in 25 sets) was downloaded from ITU. The 2014 attachment (5 more sets, ~120 more files) is not downloadable, so its new sets were re-created with the T.835 encoder (§2.2).

**Crafted streams.** Header bits were patched with scripts. A T.835 encoder variant wrote interleaved alpha planes with their own BANDS_PRESENT. Lossless hard-tile files were checked against their source pixels.

**The encoder.** `jxrenc.c` drives jxrlib's `PKImageEncode_*` with every `CWMIStrCodecParam` setting. It produced a 507-file matrix of pixel format × options. Both decoders read every file, and the headers and containers were checked against the spec's rules.

**FreeImage itself.**
- `FreeImage_Load` ran on the whole suite, with HEAD's LibJXR and PluginJXR linked ahead of `Dist/libfreeimage.a`.
- Twelve `FreeImage_Save` outputs, one per image type, were decoded with T.835.

**Code review.** Six parallel reviews covered:
- headers (§§6, 8.1-8.6, Annex B);
- entropy coding (§§5.5-5.7, 8.7-8.12);
- reconstruction (§§9.4-9.9);
- output formatting (§9.10, Annex C);
- the container (Annexes A, E, F);
- the encoder (Annex D and the codestream "shall"s).

Each cited file:line. Every claim this report relies on was then re-checked by experiment, or is marked *code*.

## 2. Conformance results

### 2.1 T.834 attachment (2010), LibJXR against T.835

| Set | Files | Bit-exact | Differ | Refused |
|---|---|---|---|---|
| BasicAndOverlap 1x1 / 2x2 / 4x4 | 18 / 5 / 42 | all | | |
| BandsPresent 1x1 / 4x4 | 12 / 12 | all | | |
| Hard_Tile_Boundaries | 58 | 58 | | |
| Flags_And_Tiles / Index_Table_Use / Tile_Sizes | 7 / 6 / 28 | all | | |
| Entropy_Table_Coverage / MBLevel_QP_Coverage | 8 / 20 | all | | |
| Varied_QP / Special_QP / Varied_Internal_Color_Format | 12 / 7 / 8 | all | | |
| Output_Bitdepth_16 / Shift_Bits / Long_Word_Flag | 18 / 16 / 2 | all | | |
| Levels / Windowing / Tag_Based_Container | 30 / 8 / 3 | all | | |
| Output_Color_Format_Baseline / Main | 14 / 72 | all | | |
| Alpha_Interleaved | 7 | 7 | | |
| Chroma_Centering | 30 | 2 (centering 0) | 28 (up to 37 levels) | |
| Output_Color_Format_Advanced | 20 | 1 (24bppYCC444) | | 19 |
| **Total** | **463** | **416** | **28** | **19** |

**Through `FreeImage_Load`:** 408 files load and 55 are refused. The 55 are the 19 Advanced files and 36 n-channel files (5–8 channels, or 4 channels with alpha). Fourteen sampled loads match T.835 byte for byte, after FreeImage's BGR order: BGR/RGB, 8/16-bit gray, 48/64-bit, 128-bit float, interleaved alpha, hard tiles, windowing, levels and 4:2:0.

### 2.2 Re-created and crafted sets

| Set | Files | Result |
|---|---|---|
| Trim-Flexbits (T.834 2014 §7.29): noise spatial, noise frequency 2×2, natural image OVERLAP 2, TRIM 0–15 | 48 | 48 bit-exact |
| Red-Blue-Not-Swapped (§7.27): 555/565/101010, lossless and lossy, flag 0 and 1 | 12 | 12 bit-exact (but see D6) |
| Spatial_XFRM (§7.30): SPATIAL_XFRM_SUBORDINATE 1–7 patched in | 7 | output unchanged in both (ignored, as A.7.19 asks) |
| Reserved fields, VLW_ESC forms, PROFILE_LEVEL_INFO variants | 34 | see D7 and §8 |
| Interleaved alpha with its own BANDS_PRESENT (32bppBGRA) | 16 | 4 equal-band files decode with alpha ±1 (D2); 6 conformant files, where alpha has fewer bands, are refused (S1); 6 non-conformant files are refused. With D2 fixed, the 4 equal-band files match. |
| Interleaved alpha on 3–8 channel images | 6 | T.835 decodes; jxrlib fails (D12) |
| Hard tiles, T.835-encoded lossless: 8 geometries × soft/hard × OVERLAP 0/1/2 | 48 | soft tiles and OVERLAP 0 are exact in both. Hard tiles with OVERLAP 1/2 on non-square grids: jxrlib gets 1040–6381 pixels wrong, T.835 is exact (D1). With D1 fixed, jxrlib is exact on all 48. |
| jxrlib encoder matrix | 507 | 459 bit-exact; 22 jxrlib cannot read back (E2); 26 T.835 rejects (WIC GUIDs, C4). Before D1, E1 and D2 were fixed: 407 bit-exact, and 52 more files differed (47 hard-tiled, 5 interleaved alpha). |
| `FreeImage_Save`, one per type | 12 | T.835 decodes 9. It rejects both FIT_RGBF saves (C4) and the metadata save (C2). |

## 3. Findings: decoder core

| ID | Clause | Finding | Evidence | Verdict, severity |
|---|---|---|---|---|
| D1 | 8.3.4, 9.9.3, 9.9.6 | **Hard tiles, axes swapped.** The overlap filter compares the macroblock *column* with `uiTileY[]` (row boundaries, counted by `cNumOfSliceMinus1H`) and the *row* with `uiTileX[]` (`strInvTransform.c:1201-1229`). Everywhere else `uiTileX` holds column positions (`strdec.c:3238`, `strcodec.c:150`). So filtering is suppressed on the wrong macroblock lines; with tiles of 1, 2 and 4 MB the errors run along x≈16 and y≈16. The encoder has the identical code (`strFwdTransform.c:510-537`, E1). | ref, repro. T.835-encoded lossless files 6, 7 or 10 MB wide: jxrlib wrong in 1040–6381 pixels, T.835 exact. A jxrlib copy with the 4 conditions swapped back matches T.835 on 41 hard-tiled streams, in both directions. Square grids (the only kind in the suite) hide it. | Deviation, **high** for hard-tiled files with overlap. FreeImage never writes tiles. **Fixed** 2026-10-12: columns are tracked by `uiTileX`, rows by `uiTileY`. The 41 probe streams and the 48-file matrix of §2.2 match T.835; the suite results are unchanged. |
| D2 | 9.10.5-9.10.6, Tables 188/189 | **Interleaved alpha output** (`outputMBRowAlpha`, `strdec.c:806-898`). (a) The guard at 808 tests `WMISCP.cfColorFormat`, the *internal* format, so the 8-bit RGBA alpha that `outputMBRow` wrote correctly is written again. (b) The rounding term is 4 (832, 852, 862, 872, 882) where the spec gives 3 for BD8, BD16S, BD16F, BD32S and BD32F. (c) It uses the primary plane's SCALED_FLAG, SHIFT_BITS, LEN_MANTISSA and EXP_BIAS (816, 822-823), not the alpha plane's own. (d) The BD16 bias ignores SHIFT_BITS (842). | ref, repro. (b): alpha +1 in about 2–19% of alpha samples, on T.835 and on jxrlib files. (c): jxrlib's own lossless 4:2:0 RGBA file (colour scaled, alpha not) gives alpha errors up to **111** in jxrlib; T.835 is exact. (d): harness, SHIFT_BITS 3 makes every alpha 65535. Planar alpha, which FreeImage writes, is unaffected. | Deviation, **medium**. **Fixed** 2026-10-13: `outputMBRowAlpha` alone formats the alpha plane, at every bit depth, with the alpha plane's own header and the spec's rounding and bias; the dead guard of (a) is gone. The harness's alpha cases, the 5 lossy matrix files, the 4 equal-band crafted files and jxrlib's lossless 4:2:0 RGBA file now match T.835. BD16F alpha beyond ±32767 still differs, as the primary plane does (D11). |
| D3 | 8.7.2, 8.7.9, 8.7.19 | TRIM_FLEXBITS is only stored in the primary plane's coding contexts (`strdec.c:275, 296`), so it is never applied to an interleaved alpha plane. The encoder does trim alpha. | repro: jxrlib cannot decode its own spatial interleaved-alpha + trim file (alpha error 255). | Deviation, low |
| D4 | 9.7.3 Table 144, 9.8.3 | A tile with USE_LP_QP_FLAG copies the LP quantizer structs, including LP's chroma shift (`strdec.c:187-192`, `strcodec.c:906`). With SCALED_FLAG=1, chroma HP is therefore dequantized at half the spec's step. | ref: a T.835 encoder patched to write tile-level USE_LP_QP gives 12074 of 12288 samples wrong (PSNR 40.7 → 19.9 dB); one remap call fixes it. The suite's only USE_LP_QP tile is unscaled. | Deviation, low (no common encoder writes it) |
| D5 | 8.7.2 Table 39 | With interleaved alpha in spatial mode, tile headers are read as DCp, DCa, LPp, LPa, HPp, HPa; the spec order is DCp, LPp, HPp, DCa, LPa, HPa (`strdec.c:309-323`; the encoder matches). This only matters when both planes carry tile-level QPs. | code. T.835 itself crashes on such streams. | Deviation, low |
| D6 | 8.3.16, Table 185, Tables 196-198, A.7.18 NOTE 3 | **RED_BLUE_NOT_SWAPPED_FLAG packing.** The normative text puts the decoded R in the most significant field when the flag is 0, and in the least significant when it is 1. jxrlib packs the opposite way for both values (`strdec.c:1693-1755`). | ref: **T.835 does exactly the same.** It is bit-identical with jxrlib on 17 crafted files with both flag values and on the suite's three packed files, and its writer says "put r into the MSB as in DIP" (the Microsoft porting kit). jxrlib therefore passes T.834. jxrlib's encoder, and so FreeImage's 555/565 saves, always writes flag 0 with red taken from the low field. Every implementation tested decodes that correctly, but a decoder written from the text alone would show R and B swapped. Changing either side alone would break interoperability with WIC, T.835 and older FreeImage files. | Unclear (spec against every implementation); no change advised |
| D7 | 8.3.5, 8.3.15, 8.4.17, 8.4.19 | **Reserved fields are not ignored.** HARD_TILING_FLAG and RESERVED_C are read as one 4-bit "subversion" accepting only 0, 1 and 9 (`strdec.c:3169-3181`). RESERVED_C=0 without hard tiling selects the HD Photo 1.0 transforms and decodes different pixels. Every other value except 1 is refused, and so is RESERVED_C=0 with hard tiling. RESERVED_D is read as "tile stretch" (3200, 3243). RESERVED_I_BIT and RESERVED_J_BIT are read as "use DC/LP QP" (3092, 3107). | repro: with each bit set on a lossless file, T.835's output is unchanged while jxrlib refuses or decodes differently. Conformant streams never set these. RESERVED_C=0 is useful HD Photo compatibility. | Deviation, low (forward compatibility) |
| D8 | 8.4.11-8.4.12 | NUM_COMPONENTS_EXTENDED_MINUS16 is not implemented (`MAX_CHANNELS 16`). NUM_COMPONENTS_MINUS1=15 is parsed as 16 channels plus 4 reserved bits instead of the 12-bit extension (`strdec.c:3056-3057`). | code and review rig. Annex A has no pixel format with more than 8 channels. | Missing, none for files |
| D9 | 8.4.6, 8.4.9, 9.10.3 | CHROMA_CENTERING_X/Y are parsed (`strdec.c:3040-3047`) and never used. `interpolateUV` (541-633) always applies the centering-0 filter (Table 181 row 0), vertical first and then horizontal, exactly as the spec does for centering 0. | ref: 28 suite files differ. T.832 9.10.3.2 lets a decoder use any upsampling; T.834 §6.4 requires the nominal filter for the test. jxrlib's encoder writes centering 0. | Deviation for T.834 only, low |
| D10 | Table 182 | YONLY→YUV444 output never writes the chroma planes (they should be 0 plus bias). | code and harness | Deviation, low |
| D11 | 9.10.7 | BD16F magnitude is masked with 0x7FFF instead of clamped (`strdec.c:532-538`). The RGBE exponent is cast to U8 instead of clipped (441-456). EXP_BIAS is passed as plain `char` to `pixel2float` (489), so negative biases are wrong where `char` is unsigned (ARM, PPC). | harness. Only out-of-range values or unusual biases are affected. | Deviation, low |
| D12 | Table 176, 8.3.18, B.2.4 | Output formats that are not implemented; see §7. CMYKDIRECT is commented out of `COLORFORMAT` (`windowsmediaphoto.h:150`). The glue's format table (`JXRGlue.c:112-212`) has no 10/16-bit YCC, YCC fixed point or YCC+alpha. `outputMBRowAlpha` refuses interleaved alpha for anything but RGB and CMYK (`strdec.c:827`). | ref: 19 suite files and 6 crafted ones | Missing, medium for Advanced-profile files |

## 4. Findings: robustness on non-conformant or crafted input

| ID | Finding | Evidence | Severity |
|---|---|---|---|
| R1 | Internal YONLY with OUTPUT_CLR_FMT YUV422 is forbidden (Tables 179 and 183) but not rejected. `outputNChannel` walks the full width over offset tables built for half width. That gives a **heap-buffer-overflow read** of `pOffsetX` (`strdec.c:662`) followed by a write through the garbage offset. It is reachable through `FreeImage_Load` with a 16bppYCC422 container: FreeImage maps that GUID to a 32-bit bitmap, and `ValidateOutputLayout` passes YONLY. | repro: ASan heap-buffer-overflow; the release build segfaults on a 640×480 file and loads garbage at 64×48. | **High**. **Fixed** 2026-10-12: `ValidateOutputLayout` refuses YUV420 or YUV422 output from a YONLY plane. The other pairs Table 179 forbids still load: jxrlib decodes a YONLY plane as half or float RGB bit-identically to T.835. |
| R2 | In frequency mode LP_QP_INDEX is not range-checked (`segdec.c:796`; spatial mode checks at 1035, frequency-mode HP at 1136). A reserved QPINDEX_REF reads past the tile's quantizer array in `dequantizeMacroblock` (`strPredQuantDec.c:75`). | repro by review (ASan heap-buffer-overflow READ) | Medium |
| R3 | A bad INDEX_TABLE_STARTCODE is detected but the error is dropped (`StrIODecInit`, `strdec.c:2653`), and the uninitialised offset table is then used. | code; the decode then fails at Copy | Low |
| R4 | Reserved BANDS_PRESENT 5–15 decode silently to garbage (`strdec.c:2840`). Illegal run lengths overrun scan arrays inside their struct (`segdec.c:508`). Reserved COMPONENT_MODE 3 is treated as INDEPENDENT (`strdec.c:90`). The 8th signature byte is not checked (3163). None of 8.3.21-8.3.30's constraints (tile partition, margin parity, width ≥ 32 with 4:2:x and OVERLAP 2, format pairs) are validated, apart from the YUV420/YUV422 output pairs since the R1 fix. | code, repro | Low |
| S1 | Stricter: FreeImage's 3edde65 refuses an interleaved alpha plane whose BANDS_PRESENT differs from the primary's (`strdec.c:3644`). Under 8.4.4 an alpha plane with *fewer* bands is conformant. | ref: 6 such T.835-made files; T.835 decodes them, jxrlib refuses. This was a deliberate trade-off against the NULL-quantizer crash. No known encoder writes such files. | Stricter, low |
| S2 | Stricter, acceptable: RESERVED_B≠1 is refused (8.3.3 reserves it for incompatible streams). Width and height are I32 in the glue (≤ 2³¹−1, against 2³² at level 255). The glue rejects the whole file when a core tag has a count other than 1, where Table A.4 says ignore the entry. | code, repro | None |

## 5. Findings: encoder core

FreeImage's save path avoids every item in this table except where noted. It uses no tiles, YUV444 or YUV420 only, planar alpha, and the glue's legal pixel-format pairs.

| ID | Finding | Evidence | Severity |
|---|---|---|---|
| E1 | Hard tiles use the same transposed boundaries as D1. The narrow-tile check (hard tiles, 4:2:x, OVERLAP 2) also tests tile heights against the image width (`strenc.c:1252-1260`): it accepts 1-MB tile columns and rejects legal 1-MB tile rows. | ref: jxrlib's lossless hard-tile files decode with 1093–6495 bytes wrong in T.835. The same files from an axis-fixed jxrlib decode exactly in T.835. | Deviation, medium (API only). **Fixed** 2026-10-12 with D1: the encoder's hard-tiled files decode in T.835 exactly as in jxrlib, lossless ones to their source, and the narrow-tile check tests tile widths. Files the old jxrlib wrote with hard tiles on a non-square grid now decode as T.835 decodes them, with that encoder's error along the tile edges. |
| E2 | jxrlib writes files it cannot decode itself: interleaved alpha with trim (D3), or on n-channel images (D12), and before the D2 fix with 4:2:0; and 12bppYCC420 / 16bppYCC422, which FreeImage's build refuses (`ValidateOutputLayout`, `strdec.c:3480-3482`). | ref: 22 of the matrix files | Deviation, low |
| E3 | Combinations forbidden by Tables 179/183 are accepted from the API: CMYK output with internal YUV444, which is the trap of the default `cfColorFormat`; RGBE or float RGB with internal YONLY; YUV422 output with internal 4:2:0. The width ≥ 32 check for 4:2:x with OVERLAP 2 runs before YCC input forces the internal format (`strenc.c:1198` vs 1280-1283). Odd YUV420 width and height are not refused. | ref: T.835 asserts or crashes on the CMYK and RGBE ones; jxrlib refuses its own CMYK one. | Deviation, low (API only) |
| E4 | The writer for 16 components puts NUM_COMPONENTS_MINUS1=15 plus 4 zero bits and no extension field (`strenc.c:776-777`), which misparses under 8.4.12. VLW_ESC's 8-byte form writes bits 32–47 twice (`strenc.c:523-534`); only codestreams over 4 GiB are affected, and PluginJXR refuses those. OVERLAP_MODE 3 is not range-checked. | code, review rig | Deviation, none in practice |
| E5 | Empty FLEXBITS packets (≤ 4 bytes, only in frequency mode) are dropped, and their index entry is written as escape 0xFF, i.e. offset 0 (`strenc.c:590, 607`). A decoder then parses tile 0's first packet as a TILE_FLEXBITS that reads zero bits. Lossy `JXR_PROGRESSIVE` saves routinely do this. | ref: T.835 decodes such files bit-identically. It is legal packet sharing in effect (T.834 §7.20 shares packets), though not literally "the offset of the n-th packet" (8.5.3). | Unclear, none |
| ok | Conformant: GDI_SIGNATURE and reserved fields; INDEX_TABLE_PRESENT_FLAG; SHORT_HEADER_FLAG exactly when sizes fit; LONG_WORD_FLAG always 1 (always valid, so Sub-baseline output is impossible); PROFILE_LEVEL_INFO 111/255 when there is no index table, inferred 111/255 (B.1) otherwise; CHROMA_CENTERING 0, matching its co-sited down-sampler; SHIFT_BITS, LEN_MANTISSA and EXP_BIAS defaults; QP syntax; EXP_BIAS written as two's complement i(8). | code, ref | |

## 6. Findings: container (glue) and FreeImage plugin

| ID | Clause | Finding | Evidence | Severity |
|---|---|---|---|---|
| C1 | A.7.30 | ALPHA_BYTE_COUNT is written as ALPHA_OFFSET + count (`JXRGlueJxr.c:749`), which equals the file size. This hits every planar-alpha file, i.e. every FreeImage RGBA, RGBA16 and RGBAF save. The official suite files carry the true count. The one T.835-encoded planar-alpha file checked has the same end offset, and T.835's decoder ignores the count, as jxrlib's does. Readers that bound the alpha read by the count go past EOF. | repro: all 189 planar-alpha matrix files and all 4 FreeImage alpha saves | Deviation, medium |
| C2 | A.7.2 | IFD tags are not written in ascending order. The static `wmpDEs[]` table puts 0x8298 (Copyright) and 0x9C9B (Caption) before 0x02BC (XMP), 0x83BB, 0x8769, 0x8773 and 0x8825 (`JXRGlueJxr.c:314`). A FreeImage save carrying Exif Copyright and XMP, i.e. any re-saved photo that has both, is **rejected by T.835** (`ifd_tag > ifd_tag_prev`). | repro: tags written 0x13B, 0x8298, **0x2BC**, 0x8773, … | Deviation, medium |
| C3 | A.7.5 | Out-of-line data (descriptive strings, XMP, IPTC, ICC) starts at odd offsets; only the Exif and GPS IFDs are aligned. | repro: XMP and ICC at odd offsets | Deviation, low |
| C4 | A.7.18 | Pixel-format GUIDs outside Table A.6. **Every FIT_RGBF save** uses WIC's 96bppRGBFloat {E3FED78F-…} (`PluginJXR.cpp:524-525`). On RGB colour-order builds, 32-bit saves use 32bppRGBA (`PluginJXR.cpp:501`). The codestream is fine; the conformant choices are 128bppRGBFloat and 32bppBGRA. | repro: **T.835 rejects** both RGBF saves ("_jxrc_image_pixelformat") | Deviation, medium |
| C5 | Table A.4, A.7.3 | `ParsePFDEntry` ignores ELEMENT_TYPE for the core tags (`JXRGlueJxr.c:1598-1665`). A BYTE or USHORT value with non-zero padding is misread, and reserved types are used instead of ignored. The suite's IMAGE_BAND_PRESENCE entries carry non-zero padding, but jxrlib does not use that tag. | code | Deviation, low |
| C6 | A.7.17, A.7.21, A.7.22 | COLOR_SPACE, PTM_COLOR_INFO and PROFILE_LEVEL_CONTAINER are neither read nor written. BT.2020 and PQ/HLG signalling is lost. Each unknown tag, including these and PADDING_DATA, prints `Unrecognized WMPTag` to stderr from inside the library (`JXRGlueJxr.c:1784`). | repro | Missing, low |
| C7 | A.6.3, A.7.20 | Only the first IFD is read (A.6.3 allows that), so there are no previews or pages. A file whose first IFD is a preview loads as the preview. | code | Missing, low |
| C8 | A.7.29 | A.7.29 says the separate alpha codestream "shall" have ALPHA_IMAGE_PLANE_FLAG=1. Every file observed writes 0, as 8.3.18 implies: jxrlib, T.835, and all 36 planar-alpha files of the official suite. | repro | Unclear (likely spec erratum) |
| F1 | 8.3.20, Table 199 | **BD1BLACK1 loads inverted.** jxrlib's output keeps the file's convention, which is correct, but PluginJXR always installs a black=0 palette (`PluginJXR.cpp:1655-1658`) and ignores `wmiSCP.bBlackWhite`. | ref, repro: FreeImage's bits equal T.835's raw bits for both suite BlackWhite files, under one palette | Deviation, medium |
| F2 | A.7.18 | n-channel images with 5–8 channels, or 4 with alpha, are refused (36 suite files), although jxrlib decodes them bit-exactly. 3- and 4-channel images and 24bppYCC444 load as raw samples into RGB(A) bitmaps; this is the known open item. | ref | Missing, medium |
| F3 | Annex E | `MimeType` returns "image/vnd.ms-photo" (`PluginJXR.cpp:1080`), so `FreeImage_GetFIFFromMime("image/jxr")` fails. The glue also rewrites an XMP `dc:format` to that type. | code | Deviation, low |
| F4 | Annex F, T.801 | JPEG XR stored in HEIF ('jxr1' items, brands jxrA and jxrS) and JPX-boxed JPEG XR are not supported. Such files are identified as FIF_HEIF or FIF_JP2 and then fail. | code | Missing, low |
| — | (not a spec issue) | FreeImage's planar alpha codestream ignores the save options. The alpha encoder parameters are copied before `SetEncoderParameters` runs (`JXRGlueJxr.c:770-771`), so alpha is always lossless, spatial and OVERLAP 0, whatever the quality or `JXR_PROGRESSIVE`. | repro | — |

## 7. Parts of the specification that are not implemented

| Feature | Clause | Since | jxrlib | FreeImage |
|---|---|---|---|---|
| CMYKDIRECT output, with or without alpha | 8.3.19, Table 176 | 2009 | missing | refused |
| YCC output at BD10/BD16, YCC444 fixed point, YCC with alpha (16 pixel formats) | Table 176, A.6 | 2009 | missing from the glue table | refused |
| 12bppYCC420, 16bppYCC422 output | Table 176 | 2009 | the core can produce it, the glue cannot | refused |
| Interleaved alpha on NCOMPONENT or YCC | 8.3.18 | 2009 | missing | — |
| More than 16 components | 8.4.12 | 2009 | missing | — |
| CHROMA_CENTERING in upsampling | 8.4.6, 9.10.3 | 2009 | ignored (allowed) | — |
| PROFILE_LEVEL_INFO validation and real profile signalling | 8.6, Annex B | 2009 | skipped on read (allowed); always writes 111/255 | — |
| PTM_COLOR_INFO | A.7.21 | 2009 (code points extended 2019) | missing | — |
| PROFILE_LEVEL_CONTAINER, COLOR_SPACE | A.7.22, A.7.17 | 2009 | missing | — |
| Multiple images per file (preview, pages) | A.6.3, A.7.20 | 2009 | missing | no multipage |
| Media type image/jxr | Annex E | 2016 | — | uses image/vnd.ms-photo |
| Storage in HEIF (ISO/IEC 23008-12) | Annex F | 2019 | missing | missing |
| JPX box-based file format | T.801 Amd., T.834 §7.31 | 2012 | missing | missing |

**Edition history**, for the "jxrlib predates it" question:
- Most features above are in the 2009 first edition.
- PREMULTIPLIED_ALPHA_FLAG is in Cor. 2 (2011).
- RED_BLUE_NOT_SWAPPED_FLAG first appears in the 2012 edition among the editions checked.
- The 2016 edition only adds Annex E.
- The 2019 edition adds Annex F and the PTM_COLOR_INFO code points for BT.2020, BT.2100 PQ/HLG and ICtCp, with presence rules.

## 8. Verified conformant

Beyond the suite results in §2:

- **CODED_IMAGE, 8.2:**
  - SubsequentBytes up to 300 extra RESERVED_A bytes, two PROFILE_LEVEL_INFO entries, and every VLW_ESC form (16-bit, 0xFB, 0xFC, escape) are skipped correctly.
  - An unknown PROFILE_IDC is ignored. T.835 asserts on it.
- **Orientation:** SPATIAL_XFRM_SUBORDINATE is ignored in Annex A files, and SPATIAL_XFRM_PRIMARY (or O_NONE) is used, as A.7.19 asks. PluginJXR applies quarter turns in Table 21's order.
- **PREMULTIPLIED_ALPHA_FLAG** is not used; PIXEL_FORMAT decides, as Annex A makes authoritative. FreeImage un-premultiplies the P formats.
- **Ignore rules that are honoured:** RED_BLUE_NOT_SWAPPED_FLAG on non-packed formats, RESERVED_E/F/G/H/L, and the byte-alignment bits.
- **Index tables:** offsets relative to CODED_TILES, reordered packets, shared packets, and escape entries read as offset 0; the index table is required for frequency mode and tiling.
- **Entropy coding** (review against the spec's tables, script-checked): all VLC tables, deltaDisc tables, adaptive scan orders and totals reset, adaptive normalization, CBPLP/CBPHP prediction, and the per-tile and per-16-MB reset points.
- **Reconstruction:** remapping, DC/LP/HP prediction (no prediction across tile boundaries), QuantMap for SCALED_FLAG 0 and 1, and every inverse transform and overlap operator are textually identical to Tables 150-175. Decoding uses 32-bit arithmetic regardless of LONG_WORD_FLAG, which 8.3.12 allows.
- **The FreeImage-specific patches** do not change conformant decoding. The container-codestream format check (`JXRGlueJxr.c:1909-1911`) enforces Annex A's BPC and Colour rule. The only stricter patches are S1 and the subsampled-YCC refusal.

## 9. Notes on the specification and the reference software

**Probable errata in T.832:**
- C8, the alpha codestream's ALPHA_IMAGE_PLANE_FLAG.
- Line 7891, the soft-tile right edge of first-level overlap, uses `[4]` where symmetry with line 7741 needs `[3]`. It is in every edition.
- Table 144 has `j <= NumLPQPs` where `<` is meant.
- The 4:2:0/4:2:2 hard-tile corner loops (`Tx < NumTileCols-1`) skip the last internal tile edge. jxrlib treats every edge and matches T.835.

**T.835 1.41 caveats, which matter when using it as the oracle:**
- It crashes or asserts on interleaved alpha combined with tile-level QPs or with trim flexbits.
- It reads EXP_BIAS unsigned and asserts it is ≤ 127, although the spec declares i(8).
- `-C`/`-R` segfault; use `-U`.
- It packs red and blue like jxrlib (D6).
- It is lenient on container and codestream "shall"s. It decoded files with end-offset ALPHA_BYTE_COUNT, odd YUV420 width, 1-MB hard tiles with 4:2:x and OVERLAP 2, and OVERLAP_MODE 3 without a warning. "T.835 accepts it" proves little for C1, C3 and E3.

## 10. Suggested fixes, by value

| Order | Fix |
|---|---|
| 1 | R1: refuse INTERNAL/OUTPUT pairs outside Table 179 in `ImageStrDecInit`, so YONLY can no longer be declared as YCC422. **Done** 2026-10-12 for the YUV420 and YUV422 output rows only: the other forbidden pairs decode bit-identically in T.835. |
| 2 | R2: check `iQIndexLP < cNumQPLP` in frequency mode (`segdec.c:796`), as spatial mode already does. |
| 3 | D1 and E1: swap `uiTileX`/`uiTileY` and `cNumOfSliceMinus1V`/`H` in the four tests of `strInvTransform.c` and the matching tests of `strFwdTransform.c` and `strenc.c`. All 41 probe streams then match T.835. **Done** 2026-10-12. |
| 4 | C2 and C3: sort IFD entries by tag before writing, and pad out-of-line data to even offsets. |
| 5 | C4: save FIT_RGBF as 128bppRGBFloat (the glue has the converter). |
| 6 | C1: write the true ALPHA_BYTE_COUNT. |
| 7 | F1: build the palette from `bBlackWhite`, or invert the bits on load. |
| 8 | D2-D4: rounding 3 in `outputMBRowAlpha`, the alpha plane's own parameters, fix the guard; TRIM_FLEXBITS on the alpha contexts; re-remap the copied HP quantizers. D2 **done** 2026-10-13. |
| 9 | F2: load n-channel images, and refuse or convert the raw 3/4-channel and YCC444 cases. |

## 11. Reproducing

The rig is in `.claude/scratch/jxr-spec` (untracked):
- **Drivers:** `rig/src/jxrdec.c`, `jxrenc.c`, `fiload.c`, `fisave.c`.
- **Builds:** `scripts/build_t835.sh`, `build_libjxr.sh`, `build_fi.sh`.
- **Running and comparing:** `scripts/run_suite.sh SUITE OUT` decodes with both decoders; `scripts/compare.py OUT` compares.
- **Header dumpers:** `jxrhdr.py`, `tilehdr.py`, `scan_headers.py`.
- **Crafting:** `craft1.sh`, `mkreserved.py`, `hard_matrix.sh`, `enc_matrix.py`.
- **Checks of the fixes:** `fixes/`, described in its `README.txt`.
- **ITU material** (not committed; ITU's licence forbids redistribution): T.832 at https://www.itu.int/rec/T-REC-T.832, the T.834 data at https://www.itu.int/rec/T-REC-T.834-201001-S, and the T.835 software at https://www.itu.int/rec/T-REC-T.835-201608-I.
