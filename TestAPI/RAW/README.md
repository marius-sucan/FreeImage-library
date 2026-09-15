# RAW regression tests

Three standalone programs covering the RAW plugin
(`Source/FreeImage/PluginRAW.cpp`) and the LibRaw it is built on
(`Source/LibRawLite`). Each prints a report and exits non-zero on failure. They
were written for the upgrade from LibRaw 0.21.1 to 0.22.2 and are meant to be
run again at the next one.

| test | what it covers |
|---|---|
| `decode` | Loads the four files of `data/` through all six of the plugin's paths - the default 16-bit load, `RAW_DISPLAY`, `RAW_PREVIEW`, `RAW_UNPROCESSED`, `RAW_HALFSIZE` and `FIF_LOAD_NOPIXELS` - and checks geometry, depth, decoded pixels, the ICC profile and the `Raw.*` metadata against a recorded table. Plus format detection for each. |
| `regress` | The plugin rather than the decoder: every path loaded from a file *and* from a memory stream, required to agree exactly; the relations between paths (header-only matches the full load and carries no pixels, half size is half, `RAW_DISPLAY` is the 16-bit image at 8 bits); `RAW_PREVIEW` using an embedded preview where there is one and falling back to a decode where there is not; the active-area margin and the `Raw.Frame.*` keys that describe it; the embedded colour profile; the Bayer pattern; and that RAW is read-only. |
| `robust` | Truncated prefixes, junk appended, single-byte corruptions, 32-bit words set to `0xFFFFFFFF`, 64-byte regions wiped, and a dense sweep over the first kilobyte where the TIFF header and both IFDs live - 12747 damaged inputs across the four files, plus degenerate buffers. They may load or be refused; they may not crash. Worth running under AddressSanitizer, which is what `make asan-run` is for. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # all three
    make asan-run       # rebuild the whole of Source/LibRawLite and the plugin
                        # with AddressSanitizer into a private copy of the
                        # library, and run all three against that

Nothing is written to disk: `robust` feeds every damaged buffer through a
memory stream, and no test saves anything.

## The corpus

`data/` **is** generated - `make data` reproduces it byte for byte from
`mkdata.py`. That is the opposite of `TestAPI/WebP`, and for a reason: FreeImage
never writes a RAW file, so nothing about these fixtures depends on the LibRaw
version being replaced, and there is no old encoder whose output has to be
preserved. What must not drift silently is the table in `decode.c`, which is
the oracle. Its values were recorded from LibRaw 0.21.1 and 0.22.2 reproduces
every one of them. `./decode --record` reprints it when a change is deliberate.

The four files are small uncompressed DNGs - a preview image in IFD0 and an
uncompressed CFA field in a SubIFD, which is the layout LibRaw takes apart with
its TIFF parser. A DNG carries TIFF magic and no RAW-specific signature, so
`Validate()` cannot shortcut and has to open the file through
`LibRaw_freeimage_datastream`, which is the FreeImage code worth exercising.
Between them they cover:

- `fi_raw_rggb.dng` - RGGB, a 48x32 embedded preview, a 516-byte ICC profile,
  and a four-pixel `ActiveArea` margin so the processed image (88x56) is
  smaller than the CFA field (96x64);
- `fi_raw_bggr.dng` - the other common Bayer phase, to catch a decoder that
  hard-codes one;
- `fi_raw_nopreview.dng` - no embedded preview, so `RAW_PREVIEW` has to fall
  back to decoding;
- `fi_raw_odd.dng` - odd dimensions (70x46) and no margin, for off-by-one in
  the row loops.

They are deliberately tiny. The decoders that 0.22 adds - Panasonic encoding 8,
Sony YCC, OM System 14-bit - need real files from those cameras, which are tens
of megabytes each and do not belong in this repository; they were used to check
the upgrade by hand and the results are in the commit message.

## Two things that look like failures and are not

**The demosaiced output is not stable across LibRaw versions.** Only
`RAW_UNPROCESSED` returns the sensor data as it was read; every other path runs
LibRaw's post-processing, whose colour tables and interpolation change between
releases. On real camera files the 0.21.1 to 0.22.2 upgrade left the Bayer
field identical and moved the demosaiced pixels. It did not move them for the
files in `data/`, which is why `decode` can check them at all - a synthetic DNG
with a flat colour matrix gives the post-processor nothing version-specific to
do. If a future upgrade does move them, that is a change to record, not a bug.

**A file the plugin used to load may start being refused.** LibRaw returns
`LIBRAW_FILE_UNSUPPORTED` for formats it recognises but cannot decode, and
0.22.2 recognises more of them than 0.21.1 did. A Nikon Z 8 file in
high-efficiency mode used to come back as a bitmap of streaks over black, with
"data corrupted" written to stderr; it is now refused outright. Refusing is the
correct behaviour and `FreeImage_Load` returning NULL is how it surfaces.

## A limitation these tests found, and do not cover

`LibRaw_freeimage_datastream` cannot read a stream that does not start at byte
zero. Its constructor measures the size from the handle's current position, but
its `seek()` passes the offset to `seek_proc` with `SEEK_SET` unchanged, so
LibRaw's absolute offsets land in the wrong place and the file is not
recognised. `FreeImage_LoadFromHandle` is otherwise documented to start from
wherever the handle happens to be. This predates the 0.22.2 upgrade - the
wrapper is unchanged by it - so there is no test for it here; it is written
down so the next person does not have to rediscover it.
