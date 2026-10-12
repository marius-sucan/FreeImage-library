# JPEG XL tests

Programs covering the JPEG XL plugin (`Source/FreeImage/PluginJXL.cpp`) and the libjxl it is
built on (`Source/LibJXL`, with highway and `Source/LibBrotli`). Each prints a report and exits
non-zero on failure.

| test | what it covers |
|---|---|
| `decode` | The files of `data/`: geometry, pixel type, ICC profile, CICP tag, Exif, XMP and animation tags of every page, against a recorded table; lossless files by checksum, lossy ones by PSNR against the patterns they were made from (`pattern.h`); header-only loads against full ones; pages locked out of order; `JXL_PLAYBACK`; every page of `test-jxl.jxl` against the page before it. |
| `regress` | Round trips of all nine exportable types at three sizes, lossless (bit exact, at several efforts) and lossy (PSNR, quality ordering); palettes; ICC profiles, the CICP tag, Exif and XMP; a 7-frame animation with offsets, all three disposals and both blends, checked against a GIF/APNG compositor, also lossy in ProPhoto (an ICC profile to libjxl); a single page; the page cache; refused types; NaN and infinity; a file 777 bytes into a stream and a handle without `SEEK_END`. |
| `robust` | Truncations, single-byte corruptions, wiped regions, appended junk and nonsense box sizes of `data/` files and of a lossy ProPhoto animation it writes - about 13000 damaged inputs, each loaded plainly, header-only and page by page. They may load or be refused; they may not crash. |
| `conformance.py` | The JPEG XL conformance suite (`fetch_conformance.py`, not kept here), every frame against its reference within the test's tolerances. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run                     # decode, regress, robust
    make asan-run                # the plugin, libjxl, highway and brotli built with AddressSanitizer
    python3 fetch_conformance.py && make conformance

Scratch files go to `$JXL_TEST_TMP`, or the current directory; `make clean` removes them.

## The corpus

`data/` was written by `mkdata.c` with libjxl 0.12.0, partly through FreeImage's writer and partly
through libjxl directly (orientation, layers, premultiplied alpha, a Brotli-compressed Exif box,
a bare codestream), except `test-jxl.jxl`: 22 lossy frames whose colours are an ICC profile, blended
over each other. It is kept, not regenerated: it is what the next libjxl must still decode.
Lossy decodes are compared by PSNR, not checksums, because libjxl's SIMD targets may round
differently.

## Conformance

All cases pass. Two compare differently from the reference, which is libjxl's raw output:
`alpha_premultiplied` is checked unpremultiplied, as FreeImage hands out straight alpha, and
`spot` only where no spot colour applies, as FreeImage renders spot colours into the image.
XYB images whose original colours are an ICC profile come back in that profile, so their
reference (linear sRGB) is converted to it before the comparison.
