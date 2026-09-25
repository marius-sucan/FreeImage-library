# Color management tests

Standalone programs covering `Source/FreeImageToolkit/ColorManagement.cpp` (the ICC
functions of `FreeImage.h`) and the Little CMS it is built on (`Source/LibLCMS2`).
Each prints a report and exits non-zero on failure.

| test | what it covers |
|---|---|
| `convert` | Every pixel layout FreeImage has (1-, 4- and 8-bit palettes with and without transparency, 8-bit grey ramps both ways round, 16-bit 555/565, 24/32-bit, 32-bit CMYK, FIT_UINT16 including min-is-white, FIT_RGB16, FIT_RGBA16, 16-bit CMYK, FIT_FLOAT, FIT_RGBF, FIT_RGBAF) × 6 embedded profiles (none, grey, RGB, CMYK, LUT-based RGB, junk) × 9 destinations × 3 intent/option sets, through `FreeImage_ConvertToICCProfile` and `FreeImage_ApplyICCProfile`, every pixel compared with a reference that calls Little CMS one pixel at a time. Then the layout, tag, CMYK flag and metadata of every result, the in-place rules, `FreeImage_ConvertToCMYK`/`FreeImage_ConvertCMYKToRGB`, soft proofing, views, header-only and colorless images, and profiles that are not device profiles. `-quick` skips the images large enough to be converted on several threads; `-digest` prints one digest of every result. |
| `profiles` | The seven built-in profiles: fixed bytes (checksums), header, descriptions, color spaces, sRGB equal to Little CMS's own. Conversions against colorimetry computed without Little CMS, 16-bit round trips, floating-point values above 1. UTF-8 descriptions cut at every length. CMYK TIFF (8 and 16 bits) and JPEG, Adobe RGB PNG, TIFF, JPEG and WebP, grey PNG: the profile and the CMYK flag come back from the file. Header-only loads describe the image a load returns: CMYK TIFF in strips, planes and tiles (8-bit, 16-bit, float, extra samples, 3 inks) and PSD (CMYK, CMYK with alpha, multichannel, indexed, bitmap), with the thumbnail a CMYK PSD loaded as RGB keeps. Tiled TIFF layouts FreeImage cannot copy are refused, and striped grey with float alpha loads its grey, contiguous or planar. |
| `robust` | lcms's malformed test profiles and 100 damaged copies (bytes changed, cut, tag table and header fields rewritten, regions wiped, bytes swapped) of 12 profiles, used as the embedded, destination and proofing profile of seven kinds of image. They may be refused; they may not crash. |
| `pillow.py` | The same conversions through Pillow's ImageCms, a Little CMS of another version: sRGB, Adobe RGB, ProPhoto, Display P3, grey, colord's FOGRA39 and SWOP press profiles, soft proofing. Optional: skipped without Pillow, numpy or colord's profiles. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # convert, profiles, robust
    make threads        # convert's digest on one thread and on eight must agree
    make asan-run       # Little CMS and ColorManagement.cpp rebuilt with AddressSanitizer
                        # and UndefinedBehaviorSanitizer, linked ahead of the library
    make pillow         # the Pillow cross-check

Scratch files are written to `$ICC_TEST_TMP`, or the current directory. `convert`
takes about a minute and a half, most of it spent building Little CMS transforms.

## How the reference works

`convert.c` restates the rules of the functions independently of the library: which
profile describes the source (the embedded one when its color space matches the pixels,
else sRGB, linear sRGB for float, grey sRGB or linear grey, device CMYK), which layout
the result has, when the in-place function may run. It then reads each source pixel
through the public pixel API in R, G, B order, converts it with its own Little CMS
transform, and compares. A channel-order, palette, row, alpha, threading or rule mistake
in the library shows up as a pixel mismatch.

The suite was checked with planted bugs: twenty mutations of `ColorManagement.cpp`
(channel order, rounding of device CMYK, nibble order, transparency, 555/565, reversed
ramps, the CMYK flag, alpha copy and opacity, layout rules, the mismatch policy, float
defaults, tagging, palettes, the identity shortcut, black point compensation, the
threaded rows) each make `convert` fail.

## Little CMS findings the tests pin

- 16-bit conversions are computed without Little CMS's precalculated tables
  (`cmsFLAGS_NOOPTIMIZE`): those err by up to about five 8-bit steps on plain
  matrix/TRC RGB conversions, more than 8-bit images, which take an exact path. The
  reference mirrors this; `profiles` round-trips 16-bit values through ProPhoto.
- A proofing profile whose header disagrees with its tags makes Little CMS 2.19 build a
  gamut-checking transform without its gamut table, and crash on the first pixel.
  `FreeImage_SoftProof` refuses such a profile; `robust` found it.
- UndefinedBehaviorSanitizer reports signed overflow in Little CMS's fixed-point
  interpolation (`cmsintrp.c`, `cmsopt.c`); the results are truncated to 16 bits, where
  wraparound gives the right value. `asan-run` exempts that check for Little CMS only.
- Perceptual CMYK to RGB differs from Little CMS 2.17 by up to 7 steps: 2.19 detects a
  different black point for these press profiles. `pillow.py` tolerates it.

## The data

`data/` holds five profiles from Little CMS 2.19.1's `testbed` (MIT license, like the
library): `test3.icc` (LUT-based RGB), `test5.icc` (v2 monitor), and the malformed
`bad.icc`, `bad_mpe.icc` and `toosmall.icc`. The press CMYK and grey 1.8 profiles are
built by `common.h` at run time.
