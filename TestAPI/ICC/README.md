# Color management tests

Standalone programs covering `Source/FreeImageToolkit/ColorManagement.cpp` (the ICC
functions of `FreeImage.h`) and the Little CMS it is built on (`Source/LibLCMS2`).
Each prints a report and exits non-zero on failure.

| test | what it covers |
|---|---|
| `convert` | Every pixel layout FreeImage has (1-, 4- and 8-bit palettes with and without transparency, 8-bit grey ramps both ways round, 16-bit 555/565, 24/32-bit, 32-bit CMYK, FIT_UINT16 including min-is-white, FIT_RGB16, FIT_RGBA16, 16-bit CMYK, FIT_FLOAT, FIT_RGBF, FIT_RGBAF) × 6 embedded profiles (none, grey, RGB, CMYK, LUT-based RGB, junk) × 9 destinations × 3 intent/option sets, through `FreeImage_ConvertToICCProfile` and `FreeImage_ApplyICCProfile`, every pixel compared with a reference that calls Little CMS one pixel at a time. Then the layout, tag, CMYK flag and metadata of every result, the CICP tag a conversion removes, the in-place rules, `FreeImage_ConvertToCMYK`/`FreeImage_ConvertCMYKToRGB`, soft proofing, views, header-only and colorless images, profiles that are not device profiles, and 8-bit grey ramps, grey palettes and RGB greys against the tone curves themselves. `-quick` skips the images large enough to be converted on several threads; `-digest` prints one digest of every result. |
| `profiles` | The seven built-in profiles: fixed bytes (checksums), header, descriptions, color spaces, sRGB equal to Little CMS's own. Conversions against colorimetry computed without Little CMS, 16-bit round trips, floating-point values above 1. UTF-8 descriptions cut at every length. CMYK TIFF (8 and 16 bits), PSD (8 and 16 bits, with and without a profile) and JPEG, Adobe RGB PNG, TIFF, JPEG, WebP and JNG (with alpha too), grey PNG and JNG: the profile and the CMYK flag come back from the file, header-only too for JNG; a grey JNG writes no RGB profile. Camera JPEGs whose Exif says Adobe RGB (ColorSpace 2, or uncalibrated with the index R03) load with the Adobe RGB profile, header-only and as grey too, unless they hold a profile; sRGB, R98 and index-less ones, grey and CMYK files do not. Header-only loads describe the image a load returns: CMYK TIFF in strips, planes and tiles (8-bit, 16-bit, float, extra samples, 3 inks) and PSD (CMYK and CMYK with alpha, with and without a profile, multichannel, indexed, bitmap), with the thumbnail a CMYK PSD loaded as RGB keeps; with PSD_CMYK, a CMYK PSD without a profile loads as CMYK, its inks those of the file, and a 2-channel multichannel PSD loads grey without the CMYK flag. Tiled TIFF layouts FreeImage cannot copy are refused, and striped grey with float alpha loads its grey, contiguous or planar. CMYK pages (8- and 16-bit, with and without a profile) keep CMYK through the multi-page cache: appended, inserted, changed, saved to a file and to memory. |
| `robust` | lcms's malformed test profiles and 100 damaged copies (bytes changed, cut, tag table and header fields rewritten, regions wiped, bytes swapped) of 12 profiles, used as the embedded, destination and proofing profile of seven kinds of image. They may be refused; they may not crash. |
| `pillow.py` | The same conversions through Pillow's ImageCms, a Little CMS of another version: sRGB, Adobe RGB, ProPhoto, Display P3, grey, colord's FOGRA39 and SWOP press profiles, soft proofing. Optional: skipped without Pillow, numpy or colord's profiles. |
| `display` | `FIF_LOAD_DISPLAY_ICC`: 42 files (CMYK JPEG, TIFF and PSD with and without a profile, CMYK TIFF (8-bit, and 16-bit planar), JPEG XL and JPEG XR with an alpha, whose alpha has to be the one a load without the CMYK flag gives, RGB, RGBA, 16-bit, profiles sRGB or grey in all but name, grey, palettes, 555/565, float, a profile Little CMS cannot link, 2-page TIFF, GIF, APNG (24-bit and translucent) and MNG frames and playback, a PNG in an ICO, DNG) loaded with the flag through every loader (file, memory, handle, and the three multi-page openers; `LoadU` and `OpenMultiBitmapU` on Windows) must equal a load without it followed by the public conversion functions, for sRGB, Adobe RGB, Display P3, a LUT-based display and the detected one. Then: header-only loads are unchanged by the flag, for flags -1 too; PSD_LAB and RAW_UNPROCESSED; the 16-bit RAW output comes out encoded for display, its linear CICP tag gone; a changed page is saved in the display's colors and the others as the file holds them, except in an APNG, whose one profile, the display's, they are converted to; `FreeImage_ApplyDisplayICCProfile` on every fixture loaded without the flag gives the image and thumbnail the flag loads, refuses CMYK and 555/565 images untouched, header-only bitmaps and NULL, and leaves the PQ AVIF as it is; images tagged sRGB or grey in all but name keep their pixels on an sRGB display; the grey profile matches the display's greys; profiles that are sRGB in all but name become FreeImage's sRGB; the setter refuses what cannot be a display profile; loads on 8 threads while the display changes. `-x11` (`make x11`) tests the X11 detection against `xcbstub.c`, a stand-in libxcb. |
| `cicp` | Files that state their colors with code points alone load with the profile of those code points. AVIF and HEIF nclx boxes are patched to 19 primaries and transfer pairs (BT.2020, Display P3, DCI-P3, BT.470 M, BT.601, SMPTE 240M, generic film and EBU 3213-E primaries; BT.709, gamma 2.2 and 2.8, BT.601, SMPTE 240M, linear and BT.2020 curves; unspecified and reserved code points standing for sRGB's), and cICP chunks are put into grey, 16-bit grey, black and white, min-is-white, RGB, palette, 48-bit and RGBA PNGs; `draw_points_idat.avif` (1/4) and `jpeg32.heif` (6/6) are loaded as they come. Each profile's description, color space and cicp tag are checked, and its colors against a profile built from ITU-T H.273's tables and formulas; the pixels equal the unpatched file's, and header-only loads get the same profile. PQ, HLG, SMPTE 428, logarithmic and sRGB code points get no profile, and an AVIF's own ICC profile (colr prof) stays. A PNG's cICP chunk outranks its iCCP chunk when FreeImage reads the code points (a range flag of 0 or 1, a known tone curve and, but for grey, known primaries; libpng drops a nonzero matrix) and gives way to it otherwise: 22 such files in seven layouts load as the PNG of the winning chunk alone, pixels, profile and CICP tag, header-only too; without an iCCP chunk, unread code points stay in the CICP tag. A cICP chunk FreeImage reads with a range flag of 0 expands the samples to the full range (black at 16 and white at 235 times 2^(bits - 8)): 29 PNGs built chunk by chunk, every color type and bit depth, with tRNS keys and alphas, bKGD and iCCP chunks and cut image data, against the file without its cICP chunk expanded by the formula; the colors and the background change, alpha, a key's matches, the transparency table and a grey ramp do not; a keyed grey is expanded in its palette; samples of fewer than 8 bits, the full range and code points the iCCP outranks stay as stored. Every page of an AVIF animation carries the profile, locked or saved as TIFF without being locked. With `FIF_LOAD_DISPLAY_ICC`, loads and locked pages equal the plain ones converted by `FreeImage_ApplyDisplayICCProfile`, on a Display P3 and an sRGB display, a PNG with both chunks and a narrow-range one included. Saved, PNG keeps the profile as iCCP; JPEG XL writes the code points as their encoding, those of PNG, AVIF, a narrow-range HEIF, a narrow-range PNG and a PQ AVIF alike; an APNG with a cICP chunk saved as an animated JPEG XL keeps every frame's pixels. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # convert, profiles, robust, display, cicp
    make x11            # display -x11, against xcbstub/libxcb.so.1
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
- So are 8-bit conversions with grey on either side: Little CMS resamples them into 33
  points, up to 31 steps off next to black (linear grey to grey gives 6 for 12.7).
  `convert` holds every 8-bit grey path to the curves themselves, and `pillow.py` builds
  its grey transform without the tables too.
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
