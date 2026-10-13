# JPEG XR regression tests

Nine standalone programs covering the JXR plugin. Each prints a report and exits
non-zero on failure. They exist because the defects they cover all live in places
ordinary round-trip testing never reaches: error paths, unexercised save flags,
64-bit arithmetic, and a `FreeImageIO` of the caller's own.

| test | what it covers |
|---|---|
| `regress` | Round-trips 11 pixel formats x 3 quality settings and prints a size + checksum per output. **Diff the output before and after a change**: any difference on an existing combination is a regression. Also asserts which formats `JXR_LOSSLESS` round-trips bit-exactly. |
| `edge` | 11 formats x 8 tiny sizes x 5 quality settings, 440 round-trips - including the low-quality YUV 4:2:0 plus two-level-overlap path, which the codec rejects below two macroblocks of width. Each file must be in a pixel format of T.832 Table A.6 and load back with the type and depth saved. Also `JXR_PROGRESSIVE` and partial-file cleanup. Run under ASan. |
| `narrowio` | A lossless save through `FreeImage_SaveToHandle` and the reload through `FreeImage_LoadFromHandle`, with a `FreeImageIO` of the test's own (64-bit positions through `fseeko`/`ftello`). |
| `meta` | ICC + EXIF + XMP round-trip, covering `WriteMetadata`'s return value and `ReadProfile`. |
| `tags` | The 14 Exif tags the container holds as descriptive metadata (strings, the ratings, the UTF-16 XPTitle, the two-SHORT PageNumber), each alone, all together, all together with XMP, IPTC, Exif, GPS and an ICC profile, and only those short enough to sit in their IFD entry, saved lossless: the pixels and the tags must come back, and every file list its IFD entries in ascending tag order (T.832 A.7.2). A tag of a type the container does not take is left out, the others kept. Then a TIFF with a PageNumber, `Wrapper/FreeImagePlus/test/test.tif` (or argv[1]). |
| `robust` | Some 6,500 damaged files made from six saved ones: descriptive tags of another type or count (and a PageNumber of one SHORT, as older release builds wrote it), every container pixel format, every codestream colour format and bit depth, internal colour format, alpha flag (also with each bands-present value) and size, a grey codestream declared as 16bppYCC422 or 12bppYCC420, sizes the data cannot hold, cuts, and random damage. Each may load or not, but never crash, abort or leak; the declared YCC is refused, and a size the data cannot hold is refused before allocating. The seeds with alpha must carry ALPHA_BYTE_COUNT as the alpha codestream's length. The 1-bit seed declared BD1BLACK1, where 1 is black, must show the same picture through inverted bits as FIC_MINISWHITE, and a FIC_MINISWHITE image save as BD1BLACK1 and load back as it was. Run under ASan too. |
| `orient` | Every pixel format saved losslessly, its container orientation set to each of the 8 values: the image loads as the upright one flipped, or turned a quarter clockwise and then flipped, with width, height and resolution swapped on a quarter turn, header-only loads too. Images taller than the 128 rows a quarter turn decodes at a time, lossless and lossy, cover the band edges. |
| `convert` | `data/`: lossless files in pixel formats or layouts FreeImage cannot write, made by jxrlib from values exact in each: 8x140 64bppRGBHalf, 64bppRGBFixedPoint, 32bppRGBE and 128bppRGBFloat (loaded as RGBF) and 64bppRGBAHalf with planar alpha (RGBAF); 9x140, an odd width, 32bppRGB101010 (RGB16), 32bppBGR and 32bppRGB (24-bit, the padding dropped), 32bppRGBA, premultiplied 32bppPBGRA, 32bppPRGBA, 64bppPRGBA and 128bppPRGBAFloat (straight alpha), and 32bppCMYK, 64bppCMYK, 40bppCMYKAlpha and 80bppCMYKAlpha (RGB, alpha kept, their CMYK profile dropped; with JXR_CMYK their CMYK samples, the alpha dropped, the profile kept and marked CMYK); crop.jxr, 40x140 with planar alpha cropped by jxrlib at 16,32, its image plane keeping a 16-row margin its alpha plane lacks; hardtiles.jxr, 96x140 with two levels of overlap and hard tiles 2 macroblocks wide and 5 and 4 high; and interleaved alpha: ileave420.jxr (grey 4:2:0 colour scaled, its lossless alpha not), ileave16.jxr (64bppRGBA, SHIFT_BITS 3) and ileave_q.jxr (lossy), which must match the reference decoder's output in ileave_q.ref. Each loads the values it was made from at every container orientation, a header-only load the same type and size. Never regenerate `data/`. |
| `bigfile` | 46341 x 46341 (2.15 Gpx) saved lossless to a 2.17 GiB file and verified pixel by pixel on reload. Needs ~4.5 GB RAM, ~2.4 GB free space, ~2 minutes. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # regress, edge, narrowio, meta, tags, robust, orient, convert
    make asan run       # the same, with AddressSanitizer - worth it for edge, meta and robust
    make bigfile-run    # the 2 GiB test, separately: it is slow and memory-hungry

Scratch files are written to `$JXR_TEST_TMP`, or the current directory.
`make clean` removes them.

## What "expected" looks like

- `regress` - byte-identical checksums to your recorded baseline; `exact` for every
  integer format under lossless. `not bit-exact (expected for this format)` on the
  16-bit packed and float formats is correct: JPEG XR's float pipeline works in a
  fixed-point internal space, and 16bpp 565/555 loses bits through the codec.
- `edge` - `440/440 round-trips ok`, progressive output differing from sequential,
  and no file left behind by a refused save.
- `narrowio` - `save -> ok`, `reload -> ok`, `pixels -> exact`.
- `meta` - ICC profile back identical, XMP present. `EXIF_MAIN` returning fewer tags
  than it started with is expected: the JXR container defines only a subset.
- `tags` - `0 failures`.
- `robust` - `0 failures`; no AddressSanitizer report under `make asan run`.
- `orient` - `0 failures`.
- `convert` - `0 failures`.
- `bigfile` - `crosses 2GiB=YES` and `exact round-trip over all 2.15 Gpx`.
