# bmp-ico-tga agent: findings (PluginBMP, ICO, TARGA, DDS, PNM, PFM, HDR + Utilities.h helpers), received 2026-10-19
Workspace $J/agents/bmp-ico-tga (tools/, data/, bin/fil_{qpv,main} + ASan). Two Low regressions (repros main loads, qpv refuses; Pillow loads F1's files); two compat breaks already reported by differential, cited with extra evidence.

## F1 Valid sparse RLE BMPs over 64 MB are refused unless the file ends exactly on the end-of-bitmap code
- Class: regression-vs-main. Severity: Low (only sparse RLE files: rows skipped with EOL/delta codes, raster > 64 MB, and trailing bytes after EOB or RLE4 without EOB). Confidence: high.
- Where: Source/FreeImage/PluginBMP.cpp:599-623 (PixelDataCanFill), called at 853, 1092, 1311; 0fe40ec ("BMP: a huge image claimed by a few bytes is refused before allocation").
- What: before allocating, a raster over 64 MB must satisfy raster <= data x 128 x 64 (RLE8) or data x 64 x 64 (RLE4); otherwise accepted only if the last 4 bytes are `?? ?? 00 01` or `00 01 00 00`. Skipped rows cost 2 bytes each regardless of width; any trailing bytes (1- or 3-byte pad, junk, more data in the stream) refuse it; RLE4 with no EOB refused (main and Pillow load it). In a V5 file with the ICC profile after the pixels (qpv's own writer layout, spec-recommended) the EOB test can never pass (new bug; main can't open V5). qpv's own BMP_SAVE_RLE writer output (uniform 20000x4000, 641080 bytes, ends 00 00 00 01) reloads fine.
- Evidence (`bin/fil_qpv <file>` vs `bin/fil_main`; generator tools/repro_f1.py):
  data/f1/f1b_rle8_eob.bmp: qpv 60000x2000 = main = Pillow
  data/f1/f1b_rle8_eob_pad1.bmp: qpv NULL "The file holds too little data for its size"; main 60000x2000 same pixels; Pillow ok
  pad2 loads in both; pad3 qpv NULL, main loads
  data/f1/f1_rle4_noeob.bmp: qpv NULL; main 60000x4000; Pillow L 60000x4000
  data/big/rle4_eol_pad1_30000x9000.bmp: qpv NULL, main loads; data/big/rle8_eol_junk_20000x4000.bmp (8 trailing bytes): qpv NULL, main loads
  data/big/v5rle8_eol_prof_60000x2000.bmp (3048-byte sRGB profile): qpv NULL (main cannot open V5)
- Fix sketch: when the ratio check fails for RLE, walk the RLE command stream (cheap) and accept if it reaches EOB or the last row; or skip trailing bytes before the EOB test and accept a missing EOB.
- **Fixed** in 9a29a60: when the ratio check fails, the RLE data is decoded without the image (DecodeRLE8 with no dib, CountRowsRLE4) and the file is refused only when the load would keep no row. The 19 refused files load, with main's pixels where main loads them, the V5 one with its profile.

## F2 The new size checks assume SEEK_END works; on a stream that refuses it, BMP, uncompressed TGA and DDS images over 64 MB are refused
- Class: regression-vs-main. Severity: Low (custom FreeImageIO whose seek_proc fails on SEEK_END: pipes, network, archives; file and memory I/O fine). Confidence: high.
- Where: PluginBMP.cpp:606-609 (PixelDataCanFill, 0fe40ec) + LoadEmbeddedProfile :783 (a57a9d9; profile dropped on such streams); PluginTARGA.cpp:762 and 826-829 (eof feeds PlausibleImageSize, 8fc4ac5); PluginDDS.cpp:612-621 (f33e02a), 778-788 (d590b03). None checks the seek_proc(handle, 0, SEEK_END) result; when it fails, end = current position, data = 0, every raster > 64 MB refused. PluginPNM.cpp:321 does it right (`if(io->seek_proc(handle, 0, SEEK_END) == 0)`). TGA RLE already failed in main without SEEK_END.
- Evidence (`NO_SEEK_END=1 bin/fil_<build> -o 0 <file>`): data/rleself/u24.bmp qpv NULL "too little data", main 5000x5000; data/rleself/u24.tga qpv NULL "Image data corrupted", main 5000x5000; data/big3/dds_dxt1_8190x8190.dds qpv NULL, main 8188x8188 (main crops DXT); tga_rle24_9000x8000.tga NULL in both (pre-existing).
- Fix sketch: run the plausibility test only when SEEK_END succeeds and the end lies beyond the pixel-data offset; read the BMP profile without the end bound otherwise.
- **Fixed** in cf3ba85, not as sketched: PlausibleImageData() and StreamHoldsByte() (Utilities.h) ask whether the stream holds the last byte a bound needs (a seek that lands there, a byte read) instead of seeking to its end, so the bounds keep working on such streams; skipping them let the 192-byte audit DDS reach the crash below. The BMP profile is checked the same way; the RLE TGA cache takes 64 KB where the end is unknown (main failed there too). The helpers fit other-plugins.md OP-2 (RAS, IFF, PCX, WBMP, XBM).

## Found while fixing F1 and F2
- DDS LoadRGB: an uncompressed row over 2 GB (32-bit, width over 536870911) overflowed `const int fileLine`, and the first short read then memset with it as a size: a 36 MB file of 536870916 x 1, enough for its size bound, segfaulted through any stream. A regression of f33e02a (main ignores the short read). **Fixed** in 2419866.
- TGA Load neither checked nor cleared its 18-byte header, footer and extension area (main too): a shorter file loaded as FIF_TARGA decoded leftover stack bytes (0-byte files came back as images). **Fixed** in 03b8e6e.
- FreeImage_GetLine() multiplied width by bpp in 32 bits (main too): a row of 512 MB or more reported a short line and pitch (536870916 x 32-bit: 16 bytes instead of 2147483664), so GetScanLine's rows overlapped, and code that sizes rows in 64 bits disagreed with it: FlipVertical and FlipHorizontal overran their buffers, Rotate asserted. **Fixed** in 1a321c4: computed in 64 bits; rows over 2 GB are refused at allocation.
- TGA RLE read cache (open): a pixel split by the end of a cut file reads as its bytes plus zeros, or as zeros, depending on the cache size (a refill after a short read zeroes what is left); a memory stream and one without SEEK_END size it differently, so 1 cut file of 5,393 differs in that pixel.

## Cited (already reported)
- differential C1 = my D1: main can't read the V5 headers qpv writes (32-bit alpha not all 0xFF/0, or any RGB ICC; a57a9d9, 68437f9); main's CheckBitmapInfoHeader rejects every biSize != 40. Pillow reads all 48 qpv-written BMPs in the matrix (1/4/8/16/24/32-bit, views, RLE8, ICC at 1/8/16/24/32) with matching pixels; qpv round-trips a 21012-byte AdobeRGB1998 profile (full + header-only).
- differential C3: PFM row order (00abb5e): netpbm PFM page says rows are "ordered bottom to top"; data/pnm/pfgrey_vert.pfm: Pillow and qpv put 103 on top, main 100.

## Pre-existing (identical in main)
- PBM: 1-bit saves inverted (P4 and P1; Pillow sees inversion); P1 with no whitespace between digits fails full load (header-only succeeds).
- TGA: colour maps with cm_first_entry > 0 indexed from 0; 16-bit alpha bit ignored; cut colour map leaves malloc'd bytes in the palette; 16-bit grey+alpha (type 3) header-only OK but full load fails; PluginTARGA.cpp:605 misaligned load (UBSan).
- ICO: 16-bit 565 icons come back with 555 colours (Pillow refuses them); with ICO_MAKEALPHA header-only reports original bpp vs full 32-bit; a 126-byte ICO claiming 20000x20000x32 allocates 1.6 GB and takes 4.5 s in both (no size check for standard icons).
- DDS: DXT3/DXT5 differ from Pillow by the same amounts in main; A1R5G5B5 and A4R4G4B4 lose alpha; A8B8G8R8 R/B swapped.
- BMP: LoadPixelDataRLE4 computes pixels + height * width in int (RLE4 > 2^31 px fail in both).
## Outside scope (agent notes)
- FreeImageIO seek/tell INT64 API/ABI break (tool needed -DFIL_INT64_IO).
- OpenMultiBitmap now fails on a file with no readable page; main reported pages=1 for a non-ICO opened as ICO.

## Checked, no issue found (condensed)
- BMP Suite 2.8 (g/q/b/x): qpv matches every reference main matches and fixes the rest (V2-V5, OS/2 2.x, zero 4th byte, unusual masks); remaining differences only under fully transparent reference pixels; no ASan/UBSan on b/.
- 32-bit alpha: BI_RGB real alpha kept; all-zero 4th byte opaque; 40-byte BI_BITFIELDS with three masks: 4th byte now ignored (as Pillow; intended 8722330); V3+ alpha masks and BI_ALPHABITFIELDS honoured. 16-bit non-555/565 masks now load as 24/32-bit (main returned wrong colours).
- Save matrix both writers (1/4/8 with/without transparency, 555/565, 24, 32 with varying/opaque/zero/binary alpha, views, BMP_SAVE_RLE): every qpv file loads exactly in qpv and Pillow; profiles round-trip; padding zero; main cannot read RLE8 at all (incl. its own output data/bmpsave_main_1/b8.bmp).
- Lead h (large, highly compressible valid files), pixels = main: BMP run-encoded RLE8 9000x8000 and 20000x10000, RLE4 16000x9000 and 30000x9000; TGA RLE 9000x8000 at 8-bit cmap/grey/16/24 (footer)/32 (top-left), 65535x1100x32; HDR RLE 9000x8000 + "+X w +Y h"; DDS DXT1 8190x8190, DXT5 8190x4094 (qpv full size, main crops), uncompressed 6000x4000x32 and 9000x8000x16; ICO PNG entries 12000x12000 and 9000x9000. Only F1's sparse case fails.
- ICO: Pillow ICOs (PNG and BMP entries) = Pillow; short palettes = Pillow (main misreads); 2-bit icons exact; save matrix at 32/256 px alpha exact; 256x256 16-bit saved as correct 24-bit PNG (main wrong colours); multi-page create/append/reorder byte-identical to main and readable by Pillow; stream offsets and memory work (main NULL at offsets).
- TGA: Pillow L/P/RGB/RGBA raw+RLE top/bottom, hand-made 15/16-bit, right-to-left, cmap 15/16/24/32, ID, footer: identical in qpv and main (flags 0 and TARGA_LOAD_RGB888); saves raw and RLE byte-identical to main; 1/4-bit saves refused (main wrote garbage or asserted); thumbnails round-trip; stream offsets work.
- DDS: DXT1/3/5 at odd sizes 1x1..37x23 and with mipmaps: correct size, DXT1 = Pillow exactly (main NULL below 4 px, crops to multiples of 4); uncompressed R5G6B5/X1R5G5B5/R8G8B8/A8R8G8B8/X8R8G8B8 same in both.
- HDR: "+X w +Y h" column-major matches Radiance resolu.c/resolu.h; column-major files (flat and RLE) = same picture written row-major (main transposes); header-only = full.
- PNM P1-P6 (maxval 15..65535, comments, one-line/tab headers, two images, no final newline) = or better than main, = Pillow at 8 bits; PFM endianness from scale sign, Pf greyscale.
- 706 truncated variants under qpv ASan+UBSan: only the pre-existing PluginTARGA.cpp:605.
- Header-only consistent apart from pre-existing cases. LLP64/Win32 read of scope files: nothing found.
