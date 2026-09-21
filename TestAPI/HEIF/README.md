# HEIF regression tests

Three standalone programs covering the HEIF plugin (`Source/FreeImage/PluginHEIF.cpp`,
built on the bundled libheif and libde265). Each prints a report and exits non-zero
on failure.

| test | what it covers |
|---|---|
| `decode` | Loads the 34 still-image files in `data/` (libheif's test images and fuzzing corpus, pillow-heif's synthetic images and six files derived from three of libheif's, see `data/README.md`) and checks everything the plugin decides: detection (an AVIF is left to the AVIF plugin, a HEIF carrying a codec FreeImage does not have is claimed and refused with a message), the bitmap type picked per pixel format (8-bit to 24/32-bit, 10/12-bit to `FIT_RGB16`/`FIT_RGBA16`, monochrome to 8-bit or `FIT_UINT16`), the geometry after the `clap`/`irot`/`imir` transforms and their direction, ICC/Exif/XMP (including the block a `mini` box stores without an `exif_tiff_header_offset`), the `thmb` thumbnail, the page count of multi-image files, header-only loads, memory streams, a truncated stream, and a pixel checksum per file. |
| `narrowio` | Streams a file through a `FreeImageIO` whose absolute seeks and tells refuse anything past a cap, and loads a HEIF that sits behind 777 bytes of junk with `FreeImage_LoadFromHandle`. The pixels must equal a plain `FreeImage_Load`. |
| `sequence` | Image sequences (animated HEIC), on the `seq-*` files of `data/`: the frame count, size and type, and the FIMD_ANIMATION tags of every page (`FrameTime` from the track's sample tables - libheif's own decode attaches each duration to a later frame - `Loop` from the edit list, the canvas); that a page is the same picture read in order, backwards, at random, twice, one per session or from memory, although libheif decodes a track forwards only; header-only pages; `HEIF_PLAYBACK` (32-bit, equal to the plain page converted); which pages a file gets (a thumbnail track written first, a file with a still image and a sequence, a frame padded by its encoder); an ICC profile in the sample entry; damage (a track that yields no frame, too many frames, a file cut short); and the frames rebuilt as an animated WebP through the page API. |

## Running

Build the library first (`make` in the repo root), then:

    make run            # decode + narrowio + sequence
    make asan-run       # the same, against a copy of the library whose plugin, libheif
                        # and libde265 objects are rebuilt with AddressSanitizer
    make narrowio-step  # the stepped seek, see below
    ./decode --png      # also writes every decoded page as fi_heif_<file>_<page>.png

Scratch files are written to `$HEIF_TEST_TMP`, or the current directory.
`make clean` removes them.

## The one branch that needs a special build

`FreeImageIO` seeks and tells with a `long`, which is 32-bit on Win64. Past 2 GB
the plugin cannot learn the file size and reaches offsets by rewinding and
stepping forward with `SEEK_CUR`. Where `long` is 64-bit that branch is
unreachable, so `PluginHEIF.cpp` takes an overridable bound:

    make narrowio-step

rebuilds `PluginHEIF.o` with `-DFI_HEIF_SEEK_STEP_MAX=4096` into a private copy
of the library and runs `narrowio` with a 4 KB cap on absolute seeks and tells.
Expected: `load -> ok`, `pixels -> exact`, one refused tell (the file size
becomes unknown) and a non-zero number of forward steps with `SEEK_CUR`. No
absolute seek is refused, because the plugin never attempts one past the bound.

## What "expected" looks like

- `decode` - one line per file in the form of the `EXPECTED` table, followed by
  the three transform checks reporting `exact` and `--- 0 failure(s) ---`. The
  `[HEIF] Cannot parse the file: ... Cannot read full meta box` lines are the
  truncated-stream check working. `avif32.heif` is reported as `detected as
  AVIF, not HEIF -> ok`, and `avc32.heif` as `refused -> ok` after a message
  naming the codec that is not built in - the only one left, now that JPEG,
  uncompressed and JPEG 2000 payloads all decode.
- `narrowio` - `pixels -> exact` twice, then the sequence read backwards with
  `10 of 10 pages exact` and its first frame from behind the junk `exact`.
- `sequence` - one line per file with its frame count and pixel checksum, the
  still image of `seq-with-still-heic.heic` winning, the damaged files failing as
  they should (`seq-corrupt.heics` in a few milliseconds of CPU: before the plugin's
  read budget libheif kept pushing samples into a decoder that never answered, as
  often as the looping track's edit list repeats it), and `--- 0 failure(s) ---`.
- `asan-run` - the same output, and no AddressSanitizer report.
