# JPEG XR regression tests

Ten standalone programs covering the JXR plugin. Each prints a report and exits
non-zero on failure. They exist because the defects they cover all live in places
ordinary round-trip testing never reaches: error paths, unexercised save flags,
64-bit arithmetic, and a `FreeImageIO` of the caller's own.

| test | what it covers |
|---|---|
| `regress` | Round-trips 11 pixel formats and 8- and 16-bit CMYK x 3 quality settings and prints a size + checksum per output. **Diff the output before and after a change**: any difference on an existing combination is a regression. Also asserts which formats `JXR_LOSSLESS` round-trips bit-exactly, CMYK back as CMYK with `JXR_CMYK`. |
| `edge` | 13 formats, CMYK of 8 and 16 bits among them, x 8 tiny sizes x 5 quality settings, 520 round-trips - including the low-quality YUV 4:2:0 plus two-level-overlap path, which the codec rejects below two macroblocks of width. Each file must be in a pixel format of T.832 Table A.6, CMYK in 32bppCMYK or 64bppCMYK, and load back with the type and depth saved, CMYK as CMYK with `JXR_CMYK`. Also `JXR_PROGRESSIVE`, partial-file cleanup, and float CMYK, which T.832 lacks, refused. Run under ASan. |
| `narrowio` | A lossless save through `FreeImage_SaveToHandle` and the reload through `FreeImage_LoadFromHandle`, with a `FreeImageIO` of the test's own (64-bit positions through `fseeko`/`ftello`). |
| `meta` | ICC + EXIF + XMP round-trip, covering `WriteMetadata`'s return value and `ReadProfile`. |
| `tags` | The 14 Exif tags the container holds as descriptive metadata (strings, the ratings, the UTF-16 XPTitle, the two-SHORT PageNumber), each alone, all together, all together with XMP, IPTC, Exif, GPS and an ICC profile, and only those short enough to sit in their IFD entry, saved lossless: the pixels and the tags must come back, and every file list its IFD entries in ascending tag order (T.832 A.7.2), the values stored past them at even offsets (A.7.5). A tag of a type the container does not take is left out, the others kept. A file whose IFD also holds entries that change nothing, COLOR_SPACE 0xFFFF, IMAGE_TYPE, an unspecified PTM_COLOR_INFO, PROFILE_LEVEL_CONTAINER, PADDING_DATA and a private one, must load its pixels without writing to stderr. Then a TIFF with a PageNumber, `Wrapper/FreeImagePlus/test/test.tif` (or argv[1]). |
| `robust` | Some 6,500 damaged files made from six saved ones: descriptive tags of another type or count (and a PageNumber of one SHORT, as older release builds wrote it), every container pixel format, every codestream colour format and bit depth, internal colour format, alpha flag (also with each bands-present value) and size, a grey codestream declared as 16bppYCC422 or 12bppYCC420, sizes the data cannot hold, cuts, and random damage. Each may load or not, but never crash, abort or leak; the declared YCC is refused, and a size the data cannot hold is refused before allocating. The seeds with alpha must carry ALPHA_BYTE_COUNT as the alpha codestream's length. The 1-bit seed declared BD1BLACK1, where 1 is black, must show the same picture through inverted bits as FIC_MINISWHITE, and a FIC_MINISWHITE image save as BD1BLACK1 and load back as it was. Run under ASan too. |
| `orient` | Every pixel format saved losslessly, its container orientation set to each of the 8 values: the image loads as the upright one flipped, or turned a quarter clockwise and then flipped, with width, height and resolution swapped on a quarter turn, header-only loads too. Images taller than the 128 rows a quarter turn decodes at a time, lossless and lossy, cover the band edges. |
| `convert` | `data/`: lossless files in pixel formats or layouts FreeImage cannot write, made by jxrlib from values exact in each: 8x140 64bppRGBHalf, 64bppRGBFixedPoint, 32bppRGBE and 128bppRGBFloat (loaded as RGBF) and 64bppRGBAHalf with planar alpha (RGBAF); 9x140, an odd width, 32bppRGB101010 (RGB16), 32bppBGR and 32bppRGB (24-bit, the padding dropped), 32bppRGBA, premultiplied 32bppPBGRA, 32bppPRGBA, 64bppPRGBA and 128bppPRGBAFloat (straight alpha), and 32bppCMYK, 64bppCMYK, 40bppCMYKAlpha and 80bppCMYKAlpha (RGB, alpha kept, their CMYK profile dropped; with JXR_CMYK their CMYK samples, the alpha dropped, the profile kept and marked CMYK); crop.jxr, 40x140 with planar alpha cropped by jxrlib at 16,32, its image plane keeping a 16-row margin its alpha plane lacks; hardtiles.jxr, 96x140 with two levels of overlap and hard tiles 2 macroblocks wide and 5 and 4 high; and interleaved alpha: ileave420.jxr (grey 4:2:0 colour scaled, its lossless alpha not), ileave16.jxr (64bppRGBA, SHIFT_BITS 3) and ileave_q.jxr (lossy), which must match the reference decoder's output in ileave_q.ref; and lpqp.jxr, 32x140 in frequency mode with two LP QPs per tile and an LP QP index per macroblock, held to lpqp.ref. Each loads the values it was made from at every container orientation, a header-only load the same type and size; with `FIF_LOAD_DISPLAY_ICC` on an sRGB display the CMYK files, whose profiles cannot be linked, load the same pixels, alpha included. lpqp_bad.jxr, the same with the reserved LP QP index 2 in one macroblock, must be refused, its header still loading. n-channel images, 9x140 with planar alpha last: 24bpp3Channels, 32bpp3ChannelsAlpha and 64bpp3ChannelsAlpha load as RGB(A), channel 0 red; 32bpp4Channels, 40bpp4ChannelsAlpha, 48bpp5ChannelsAlpha and 128bpp8Channels as the CMYK files do, from their first four channels. 24bppYCC444 without PTM_COLOR_INFO must be refused, header only too. Never regenerate `data/`. |
| `colour` | COLOR_SPACE and PTM_COLOR_INFO (T.832 A.7.17, A.7.21) added to lossless saves and `data/` files. Without an ICC profile in the file, PTM_COLOR_INFO's code points become the CICP tag and, unless they describe sRGB, PQ, HLG or nothing, a profile of them, grey for grey images; COLOR_SPACE 1 alone the CICP tag of sRGB. Only unsigned RGB and grey of 8, 10 and 16 bits take either; entries of another type or count, and the conformance suite's all-unspecified ones, change nothing; a file's own profile wins. Header-only loads describe the same. With FULL_RANGE_FLAG 0, RGB by matrix 0 and grey by a luma matrix load expanded from the narrow range (A.7.21.5), 8- and 16-bit, alpha untouched; other matrices, and files with a profile, keep their samples. Each profile converts to FreeImage's sRGB as an independent reference does (the spec's curves, RP 177 matrices, Bradford adaptation): every primaries with the BT.709 curve, every curve with BT.2020 primaries and as grey, within one step at 8 bits. `FIF_LOAD_DISPLAY_ICC` on an sRGB and a Display P3 display gives what `FreeImage_ApplyICCProfile` gives, and leaves PQ alone. Eight threads load one profile at once. `data/ycc444.jxr` with PTM_COLOR_INFO loads as 24-bit RGB through each matrix FreeImage converts (BT.709, FCC, BT.601 625 and 525, SMPTE 240M, BT.2020, YCgCo), full and narrow range, with RESERVED_K set too: A.7.21.5's forward equations take the loaded RGB back to the file's samples within half a code value, on 300 pixels or more that are not clipped; a quarter turn gives the same pixels turned, an RGB profile in the file is kept and a grey one dropped, and matrices 0, 2, 3, 10 (BT.2020 constant luminance) and 14 (ICtCp), and no PTM_COLOR_INFO, are refused with their message, header only too. |
| `bigfile` | 46341 x 46341 (2.15 Gpx) saved lossless to a 2.17 GiB file and verified pixel by pixel on reload. Needs ~4.5 GB RAM, ~2.4 GB free space, ~2 minutes. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # regress, edge, narrowio, meta, tags, robust, orient, convert, colour
    make asan run       # the same, with AddressSanitizer - worth it for edge, meta and robust
    make bigfile-run    # the 2 GiB test, separately: it is slow and memory-hungry

Scratch files are written to `$JXR_TEST_TMP`, or the current directory.
`make clean` removes them.

## What "expected" looks like

- `regress` - byte-identical checksums to your recorded baseline; `exact` for every
  integer format under lossless. `not bit-exact (expected for this format)` on the
  16-bit packed and float formats is correct: JPEG XR's float pipeline works in a
  fixed-point internal space, and 16bpp 565/555 loses bits through the codec.
- `edge` - `520/520 round-trips ok`, progressive output differing from sequential,
  and no file left behind by a refused save.
- `narrowio` - `save -> ok`, `reload -> ok`, `pixels -> exact`.
- `meta` - ICC profile back identical, XMP present. `EXIF_MAIN` returning fewer tags
  than it started with is expected: the JXR container defines only a subset.
- `tags` - `0 failures`.
- `robust` - `0 failures`; no AddressSanitizer report under `make asan run`.
- `orient` - `0 failures`.
- `convert` - `0 failures`.
- `colour` - `0 failures`.
- `bigfile` - `crosses 2GiB=YES` and `exact round-trip over all 2.15 Gpx`.
