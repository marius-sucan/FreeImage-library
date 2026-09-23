# AVIF regression tests

Two standalone programs covering the AVIF plugin (`Source/FreeImage/PluginAVIF.cpp`,
built on the bundled libavif and dav1d). Each prints a report and exits non-zero
on failure.

| test | what it covers |
|---|---|
| `decode` | Loads every file in `data/` (libavif's and libheif's corpora plus two files derived from them, see `data/README.md`) and checks everything the plugin decides: detection, the bitmap type picked per pixel format (8-bit to 24/32-bit, 10/12-bit to `FIT_RGB16`/`FIT_RGBA16`), the `clap`/`irot`/`imir` transforms, ICC/Exif/XMP, the page count of image sequences and the `FIMD_ANIMATION` tags on every one of their pages - including a file retimed so that each frame lasts a different, non-whole number of milliseconds, and a round trip that rebuilds it as an animated WebP through the page API to see what those tags are worth to a writer - the `AVIF_PLAYBACK` pages of a sequence against the pages loaded without it, what `FreeImage_CloseMultiBitmap()` returns for a document opened read-write in a format that has no writer, header-only loads, memory streams, a truncated stream, and a pixel checksum per file. One file is expected to be refused. |
| `narrowio` | Streams a file through a `FreeImageIO` whose absolute seeks and tells refuse anything past a cap, and loads an AVIF that sits behind 777 bytes of junk with `FreeImage_LoadFromHandle`. The pixels must equal a plain `FreeImage_Load`. |

## Running

Build the library first (`make` in the repo root), then:

    make run            # decode + narrowio
    make asan-run       # the same, with the plugin, libavif and dav1d rebuilt with
                        # AddressSanitizer and linked ahead of the library
    make narrowio-step  # the stepped seek, see below
    ./decode --png      # also writes every decoded page as fi_avif_<file>_<page>.png

Scratch files are written to `$AVIF_TEST_TMP`, or the current directory.
`make clean` removes them.

## The one branch that needs a special build

`FreeImageIO` seeks and tells with a `long`, which is 32-bit on Win64. Past 2 GB
the plugin cannot learn the file size and reaches offsets by rewinding and
stepping forward with `SEEK_CUR`. Where `long` is 64-bit that branch is
unreachable, so `PluginAVIF.cpp` takes an overridable bound:

    make narrowio-step

rebuilds `PluginAVIF.o` with `-DFI_AVIF_SEEK_STEP_MAX=4096` into a private copy
of the library and runs `narrowio` with a 4 KB cap on absolute seeks and tells.
Expected: `load -> ok`, `pixels -> exact`, one refused tell (the file size
becomes unknown) and a non-zero number of forward steps with `SEEK_CUR`. No
absolute seek is refused, because the plugin never attempts one past the bound.

## What "expected" looks like

- `decode` - one line per file in the form of the `EXPECTED` table, followed by
  `--- 0 failure(s) ---`. The `[AVIF] Cannot parse the file: Truncated data`
  lines are the truncated-stream check working, and the two
  `[AVIF] AVIF does not support writing` lines are the edited read-write session
  failing to save and the refused new document. `clap_irot_imir_non_essential.avif`
  is reported as `refused -> ok`: libavif rejects a `clap` that is not marked
  essential whatever the strictness setting, and the plugin passes that on.
  Each of the four image sequences also prints a `playback ->` line: its page
  count, and how far a played frame is allowed to be from the same frame loaded
  without the flag (nowhere for an 8-bit file, one level for a 12-bit one, which
  libavif and `FreeImage_ConvertTo32Bits()` quantize by different routes), and a
  `tags ->` line naming the frame description every page carries. The two
  `colors-animated-8bpc-variable-delays.avifs` lines before the table check the
  durations one by one, and that they survive being written out as an animated
  WebP - along with the disposal and the blend, which decide whether a frame
  replaces the canvas or is drawn onto it.
- `narrowio` - `pixels -> exact` twice.
- `asan-run` - the same output, and no AddressSanitizer report.
