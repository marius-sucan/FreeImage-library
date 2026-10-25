# JPEG XL tests

Programs covering the JPEG XL plugin (`Source/FreeImage/PluginJXL.cpp`) and the libjxl it is
built on (`Source/LibJXL`, with highway and `Source/LibBrotli`). Each prints a report and exits
non-zero on failure.

| test | what it covers |
|---|---|
| `decode` | The files of `data/`: geometry, pixel type, ICC profile, CICP tag, Exif, XMP and animation tags of every page, against a recorded table; lossless files by checksum, lossy ones by PSNR against the patterns they were made from (`pattern.h`); header-only loads against full ones; the CMYK files with `JXL_CMYK` against their inks, the profile flagged CMYK; the XYB CMYK files cjxl writes against their inks' rendering through the profile and libjxl's own sRGB rendering, `JXL_CMYK` giving the same RGB image; pages locked out of order; `JXL_PLAYBACK`; every page of `test-jxl.jxl` against the page before it. |
| `regress` | Round trips of all nine exportable types at three sizes, lossless (bit exact, at several efforts) and lossy (PSNR, quality ordering); palettes; ICC profiles, the CICP tag, Exif and XMP; a 7-frame animation with offsets, all three disposals and both blends, checked against a GIF/APNG compositor, also lossy with profiles libjxl keeps as ICC (ProPhoto: whole frames; Instagram's c2: written as sRGB) and with float frames, their frame layout read through libjxl; CMYK of 8 and 16 bits with a CMYK profile written as CMYK (CMY samples and a black channel in that profile, never XYB, a level 10 codestream): back with `JXL_CMYK` bit exact lossless and within PSNR lossy, the profile kept, a plain load the profile's rendering, one page through the page cache still CMYK, an animation in RGB, without a profile its RGB rendering, float CMYK refused; a single page; the page cache; float samples of both signs, which libjxl refuses with its own predictors, lossless at every effort and through the page cache; refused types; NaN and infinity; a file 777 bytes into a stream and a handle without `SEEK_END`. |
| `robust` | Truncations, single-byte corruptions, wiped regions, appended junk and nonsense box sizes - about 15000 damaged inputs, each loaded plainly, header-only and page by page. They may load or be refused; they may not crash. Exif boxes too: crafted blocks (a TIFF header alone, an IFD cut 2 bytes short, maker notes ending the block, Canon camera settings as bytes and as shorts, whose tags are counted), one block cut at every length and read from every offset. |
| `conformance.py` | The JPEG XL conformance suite (`fetch_conformance.py`, not kept here), every frame against its reference within the test's tolerances. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run                     # decode, regress, robust
    make asan-run                # the plugin, libjxl, highway, brotli and the Exif reader built with AddressSanitizer
    python3 fetch_conformance.py && make conformance

Scratch files go to `$JXL_TEST_TMP`, or the current directory; `make clean` removes them.

## The corpus

`data/` was written by `mkdata.c` with libjxl 0.12.0, partly through FreeImage's writer and partly
through libjxl directly (orientation, layers, premultiplied alpha, a Brotli-compressed Exif box,
a bare codestream, lossy frames blended in ICC colours); the CMYK files carry a printer profile
Little CMS builds there. The exceptions are `test-jxl.jxl`: 22 lossy frames whose
colours are an ICC profile, blended over each other, kept, not regenerated: it is what the next
libjxl must still decode; and `cmyk8_xyb.jxl` and `cmyka8_xyb.jxl`, written by the official cjxl 0.12.0
release (built with skcms) at its defaults from `./mkdata press.icc cmyk8_xyb.pam cmyka8_xyb.pam`:
`cjxl cmyk8_xyb.pam data/cmyk8_xyb.jxl -x icc_pathname=press.icc`. Their XYB holds the image rendered
through the profile, K included; a libjxl built with Little CMS, as ours is, codes the CMY samples as
if they were sRGB instead. `./mkdata name...` writes only the files named.
Lossy decodes are compared by PSNR, not checksums, because libjxl's SIMD targets may round
differently.

## Conformance

All cases pass. Two compare differently from the reference, which is libjxl's raw output:
`alpha_premultiplied` is checked unpremultiplied, as FreeImage hands out straight alpha, and
`spot` only where no spot colour applies, as FreeImage renders spot colours into the image.
XYB images whose original colours are an ICC profile come back in that profile, so their
reference (linear sRGB) is converted to it before the comparison.
