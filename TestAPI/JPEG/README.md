# JPEG regression tests

Three standalone programs covering the JPEG plugin
(`Source/FreeImage/PluginJPEG.cpp`), the lossless transforms
(`Source/FreeImageToolkit/JPEGTransform.cpp`) and the LibJPEG they are built on
(`Source/LibJPEG`). Each prints a report and exits non-zero on failure. They
were written for the upgrade from IJG libjpeg 9d to version 10 and are meant to
be run again at the next one.

| test | what it covers |
|---|---|
| `decode` | Loads the sixteen files of `data/` and checks geometry, depth, colour type, decoded pixels and attached metadata against a recorded table, plus format detection, a memory stream and a header-only load for each. Both IDCTs are recorded, since `JPEG_DEFAULT`/`JPEG_FAST` and `JPEG_ACCURATE` are different code. |
| `regress` | Round-trips the three exportable depths through a file *and* a memory stream across fourteen save modes, asserting the two agree byte for byte; checks that the depths JPEG cannot carry are refused and that 32-bit CMYK is not; records the quantisation tables actually emitted; and exercises all seven lossless transforms and `FreeImage_JPEGCrop` on an MCU-aligned image and a ragged one. |
| `robust` | Truncations, junk appended past EOI, single-byte corruptions, 64-byte regions wiped, rewritten segment lengths, and SOF dimensions set to zero or to values that would overflow a size computation - 55332 damaged inputs across eight files, each decoded with six load flag combinations, plus 120 truncated inputs through the lossless transform path. They may load or be refused; they may not crash. Worth running under AddressSanitizer, which is what `make asan-run` is for. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # all three
    make asan-run       # rebuild the whole of Source/LibJPEG, PluginJPEG.cpp
                        # and JPEGTransform.cpp with AddressSanitizer into a
                        # private copy of the library, and run all three
                        # against that

Scratch files are written to `$JPEG_TEST_TMP`, or the current directory.
`make clean` removes them along with the ASan objects.

## The corpus

`data/` is **not** generated - it is kept. Every file in it was entropy-coded
by the IJG libjpeg 9d that FreeImage bundled until the version 10 upgrade,
which is the last thing that encoder will ever produce in this tree. The JPEG
bitstream is normative, so any later libjpeg has to decode these to exactly the
pixels recorded in `decode.c`; that is what makes them the cross-version oracle
at the next bump, and it is why they must not be regenerated. `./decode
--record` reprints the table when a change to it is deliberate.

The sixteen cover the three subsampling factors plus 4:1:1, a progressive scan,
arithmetic coding, optimised Huffman tables, restart markers, greyscale, the
`JCS_RGB` colour space (no YCbCr transform at all), CMYK behind an Adobe APP14
marker, the two ends of the quality range, an image whose dimensions are not a
multiple of the MCU, a 1x1 image, and one carrying ICC, XMP, EXIF and IPTC.

`fi_jpeg_meta.jpg` is the only assembled one: it is `fi_jpeg_420.jpg` with the
APPn segments of `TestAPI/exif.jpg` spliced in ahead of the first table, so its
entropy-coded data is still 9d's. `decode.c` records the same pixel digest for
both files, which is what proves the splice left the image alone.

## What the 9d to 10 upgrade did and did not change

**Decoding did not move at all.** Every file in `data/` decodes to the same
bytes under 10 as under 9d, through both IDCTs, and so do the JPEG payloads
that reach libjpeg through LibTIFF, LibRawLite and LibHEIF. The two pristine
`djpeg` binaries agree too, which is the check that does not involve FreeImage
at all.

**Encoding moved, in exactly one place.** libjpeg 9e changed the sample
chrominance quantisation table's DC entry from 17 to 16 "for lossless support"
(`jcparam.c`); it arrives here because FreeImage skipped 9e and 9f and went
straight from 9d to 10. Nothing else in the encoder changed. So:

- a greyscale save is byte-identical to 9d's, at every quality - there is no
  chrominance table in the file. `regress` asserts the table count for this
  reason;
- a colour save is byte-identical at quality 87 and above, where the two bases
  scale to the same integer, and below quality 4, where both clamp;
- in between, a colour save differs. The file grows by between 1 and 42 bytes
  on the 256x192 test image - 0.02% to 0.6% - and the decoded picture moves by
  at most 2 levels out of 255 at the default quality, in the direction of the
  original: the finer chrominance DC step makes v10's output very slightly
  closer to the source, not further from it.

`regress` records the DC entry of each emitted table at six qualities rather
than checking a file checksum, so the next change of this kind names itself
instead of showing up as an opaque hash mismatch.

**JPEG-in-TIFF did not move either**, which is not obvious: `TIFF_JPEG` saves
go through the same encoder. FreeImage only ever writes JPEG-compressed TIFF as
RGB or greyscale, never as `PHOTOMETRIC_YCBCR`, so `tif_jpeg.c` calls
`jpeg_set_colorspace(JCS_UNKNOWN)` and every component is given quantisation
table 0 - the luminance one, which did not change. The chrominance table is
never written into a TIFF at all.
