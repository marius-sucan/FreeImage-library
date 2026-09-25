# HEIF regression tests

Three standalone programs covering the HEIF plugin (`Source/FreeImage/PluginHEIF.cpp`,
built on the bundled libheif and libde265), and a fourth that runs it over third-party
samples the repository does not carry. Each prints a report and exits non-zero on failure.

| test | what it covers |
|---|---|
| `decode` | Loads the 34 still-image files in `data/` (libheif's test images and fuzzing corpus, pillow-heif's synthetic images and six files derived from three of libheif's, see `data/README.md`) and checks everything the plugin decides: detection (an AVIF is left to the AVIF plugin, a HEIF carrying a codec FreeImage does not have is claimed and refused with a message), the bitmap type picked per pixel format (8-bit to 24/32-bit, 10/12-bit to `FIT_RGB16`/`FIT_RGBA16`, monochrome to 8-bit or `FIT_UINT16`), the geometry after the `clap`/`irot`/`imir` transforms and their direction, ICC/Exif/XMP (including the block a `mini` box stores without an `exif_tiff_header_offset`), the `thmb` thumbnail, the page count of multi-image files, header-only loads, memory streams, a truncated stream, and a pixel checksum per file. |
| `narrowio` | Streams a file and a sequence through a `FreeImageIO` of the test's own (64-bit positions through `fseeko`/`ftello`), loads a HEIF that sits behind 777 bytes of junk with `FreeImage_LoadFromHandle`, and one through a stream whose `SEEK_END` fails, whose length the plugin probes for. The pixels must equal a plain `FreeImage_Load`. |
| `sequence` | Image sequences (animated HEIC), on the `seq-*` files of `data/`: the frame count, size and type, and the FIMD_ANIMATION tags of every page (`FrameTime` from the track's sample tables - libheif's own decode attaches each duration to a later frame - `Loop` from the edit list, the canvas); that a page is the same picture read in order, backwards, at random, twice, one per session or from memory, although libheif decodes a track forwards only; header-only pages; `HEIF_PLAYBACK` (32-bit, equal to the plain page converted); which pages a file gets (a thumbnail track written first, a file with a still image and a sequence, a frame padded by its encoder); an ICC profile in the sample entry; damage (a track that yields no frame, too many frames, a file cut short); and the frames rebuilt as an animated WebP through the page API. |
| `thirdparty` | Files other HEIF writers produced, downloaded into `samples/` by `fetch_samples.sh` (see `samples/README.md` for their sources and terms): Nokia's example image sequences, bursts and collections, and the MPEG conformance suite's image-sequence files (C001, C026-C032, C036-C038, C041). Page counts, sizes, frame durations, loop counts, animation tags and a pixel checksum per file, each cross-checked against ffmpeg before it was pinned; every sequence is also read backwards, at random, header-only, played and from memory. |

## Running

Build the library first (`make` in the repo root), then:

    make run            # decode + narrowio + sequence
    make asan-run       # the same, with the plugin, libheif and libde265 rebuilt with
                        # AddressSanitizer and linked ahead of the library
    make thirdparty-run # fetch the third-party samples (about 12 MB) and run thirdparty
    make thirdparty-asan-run   # the same with the AddressSanitizer objects
    ./decode --png      # also writes every decoded page as fi_heif_<file>_<page>.png

Scratch files are written to `$HEIF_TEST_TMP`, or the current directory.
`make clean` removes them.

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
- `thirdparty` - one line per sample with its page count, size and pixel checksum, and
  `--- 0 failure(s) ---`. A missing sample is a failure that names `fetch_samples.sh`.
