# WebP regression tests

Three standalone programs covering the WebP plugin
(`Source/FreeImage/PluginWebP.cpp`) and the libwebp it is built on
(`Source/LibWebP`). Each prints a report and exits non-zero on failure. They
were written for the upgrade from libwebp 1.2.1 to 1.6.0 and are meant to be
run again at the next one.

| test | what it covers |
|---|---|
| `decode` | Loads the six files of `data/` and checks geometry, depth, decoded pixels and attached metadata against a recorded table, plus format detection, a memory stream and a header-only load for each. |
| `regress` | Round-trips both exportable depths (24 and 32) through a file *and* a memory stream, across `WEBP_LOSSLESS`, the default quality and qualities 1/50/100, on a full-size picture and a 7x3 one. Asserts that lossless comes back bit exact and that the file and the stream agree; that an 8-bit image is refused rather than mangled; and that ICC, XMP and EXIF survive both lossy and lossless saves. Encoded sizes are reported, not asserted. |
| `robust` | Truncated prefixes, junk appended, single-byte corruptions, 64-byte regions wiped, and the RIFF and per-chunk length fields rewritten to nonsense - 6566 damaged inputs across the six files. They may load or be refused; they may not crash. Worth running under AddressSanitizer, which is what `make asan-run` is for. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # all three
    make asan-run       # rebuild the whole of Source/LibWebP and the plugin
                        # with AddressSanitizer into a private copy of the
                        # library, and run all three against that

Scratch files are written to `$WEBP_TEST_TMP`, or the current directory.
`make clean` removes them along with the ASan objects.

## The corpus

`data/` is **not** generated - it is kept. Every file in it was written by the
libwebp 1.2.1 that FreeImage bundled until the 1.6.0 upgrade, which is the last
thing that encoder will ever produce in this tree. The WebP bitstream is
normative, so any later libwebp has to decode these to exactly the pixels
recorded in `decode.c`; that is what makes them the cross-version oracle at the
next bump, and it is why they must not be regenerated. `./decode --record`
reprints the table when a change to it is deliberate.

The six cover VP8 (lossy), VP8L (lossless), an alpha channel through both, an
image with fully transparent regions, and the extended container with ICCP, XMP
and EXIF chunks attached.

## Two things that look like failures and are not

**Lossless output is not byte-stable across libwebp versions.** libwebp 1.5.0
stopped using floating point in the lossless encoder, so the same image encodes
to slightly different - usually slightly smaller - bytes than 1.2.1 produced.
What may not change is the decoded result, and that is what `regress` asserts.
Encoded sizes are printed so a drift is visible, never compared.

**RGB under a fully transparent pixel is not preserved.** `WebPConfig.exact`
defaults to 0 in every version of libwebp and the plugin never sets it, so the
encoder is free to put whatever compresses best where alpha is 0. Going from
1.2.1 to 1.6.0 changed 6704 such pixels in
`data/fi_webp_holes_lossless.webp` and not one visible one. `regress` therefore
checks the alpha channel and the visible pixels of that case, not a checksum.

## The bug this suite found

`regress` was what turned up a double free in `PluginWebP.cpp` that had been
there for as long as the plugin has: `EncodeImage` declares its `WebPPicture` on
the stack and only initializes it *after* the check that rejects an unsupported
image type, while the `catch` block calls `WebPPictureFree` on it whatever went
wrong. Saving anything that is not 24- or 32-bit - an 8-bit greyscale bitmap,
say - therefore handed `free()` two uninitialized stack words. It predates the
1.6.0 upgrade and reproduces on 1.2.1; it is fixed, and the "refusals" case in
`regress` is there so it stays fixed.
