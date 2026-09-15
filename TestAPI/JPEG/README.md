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

There is also `oracle.c`, which is a tool rather than a test: `make oracle`
builds it, `run` ignores it, and it asserts nothing. It prints one diffable
line per operation - every load flag, every save flag, every transform, over
whatever directory of JPEGs you hand it - so that the same program built
against two different `libfreeimage.a` can be run twice and the reports
compared with `diff`. That is how this upgrade was checked, over a corpus
wider than `data/`, and it is what `decode.c` cannot do: `decode.c` compares
against a frozen table of these sixteen files, `oracle` compares two builds
over anything. Its header has the step-by-step for the next upgrade; the
important half is running it **before** the old library is replaced.

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
bytes under 10 as under 9d, through both IDCTs and all seven of the plugin's
load flag combinations, from a file and from a memory stream. So do the two
other places a JPEG stream actually reaches libjpeg in this tree: a
`TIFF_JPEG` image, through LibTIFF's `tif_jpeg.c`, and the JPEG payload of a
HEIF file, through libheif's decoder plugin. The two pristine `djpeg` binaries
agree on all sixteen files too, with `-dct int`, `fast` and `float`, which is
the check that does not involve FreeImage at all.

LibRawLite is the third library that compiles against these headers, and it
had to be checked differently, because none of its libjpeg call sites are
reachable from the files available here. LibRaw calls libjpeg in three places
- `src/decoders/dng.cpp` for a lossy-JPEG DNG, `kodak_decoders.cpp` for Kodak
raw, and `unpack_thumb.cpp` for a thumbnail it has to decode itself - and
FreeImage's RAW plugin reaches none of them: when the thumbnail it gets back is
not already a bitmap it hands the bytes to `FreeImage_LoadFromMemory(FIF_JPEG,
...)` (`PluginRAW.cpp`), so a JPEG thumbnail is decoded by the JPEG plugin, on
the path this suite already covers. The four DNGs of `TestAPI/RAW/data` contain
no JPEG stream at all - they are uncompressed, with an uncompressed preview.
What was verified for LibRaw is therefore that it compiles against the version
10 headers and that its four files decode identically before and after through
both the raw and the preview paths, which is the check that matters for the two
new struct fields. It is not a codec check, and nothing here claims it is.

**Encoding moved, in exactly one place.** libjpeg 9e changed the sample
chrominance quantisation table's DC entry from 17 to 16 "for lossless support"
(`jcparam.c`); it arrives here because FreeImage skipped 9e and 9f and went
straight from 9d to 10. Nothing else in the encoder changed. So:

- a greyscale save is byte-identical to 9d's at every one of the 101 quality
  values. There is no chrominance table in the file at all. `regress` asserts
  the table count for this reason;
- a colour save is byte-identical at 29 of the 101: everything from 87 up,
  where 17 and 16 scale to the same integer; 0 to 3, where both clamp to 255,
  the baseline maximum (`PluginJPEG.cpp` passes `force_baseline = TRUE`); and
  eleven scattered values in between - 64, 67, 70, 73, 76, 78, 79, 81, 82, 84
  and 85 - where the two bases happen to round together;
- at the other 72, a colour save differs. The file grows by between 1 and 42
  bytes on the 256x192 test image - 0.02% to 0.6% - and the decoded picture
  moves by at most 2 levels out of 255 at the default quality, and 16 at
  `JPEG_QUALITYBAD`. It moves towards the original rather than away from it:
  the finer chrominance DC step is the more accurate one.

That the quantisation table is the *only* encoder change is not taken on trust
from the change log. libjpeg 10 also rewrote `jchuff.c` and both DCTs, so the
two releases' own `cjpeg` binaries were run over the same image at every
quality from 0 to 100 in nine modes. In the seven that write a chrominance
table - the four subsampling factors, progressive, optimised Huffman and
arithmetic coding - the two encoders agree at exactly the same 25 qualities,
the same set in every mode. In the two that do not write one - greyscale, and
`JCS_RGB`, where there is no colour transform and all three components share
table 0 - they agree at all 101. One table entry explains every byte of
difference, and nothing else in the encoder moved.

(That sweep's 25 and FreeImage's 29 differ only because `cjpeg` leaves
`force_baseline` off and so allows quantisation values above 255, which keeps
qualities 0 to 3 apart; FreeImage clamps and they coincide.)

`regress` records the DC entry of each emitted table at six qualities rather
than checking a file checksum, so the next change of this kind names itself
instead of showing up as an opaque hash mismatch.

**JPEG-in-TIFF did not move either**, which is not obvious: `TIFF_JPEG` saves
go through the same encoder. FreeImage only ever writes JPEG-compressed TIFF as
RGB or greyscale, never as `PHOTOMETRIC_YCBCR`, so `tif_jpeg.c` calls
`jpeg_set_colorspace(JCS_UNKNOWN)` and every component is given quantisation
table 0 - the luminance one, which did not change. The chrominance table is
never written into a TIFF at all.
