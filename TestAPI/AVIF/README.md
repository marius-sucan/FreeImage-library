# AVIF regression tests

Two standalone programs covering the AVIF plugin (`Source/FreeImage/PluginAVIF.cpp`,
built on the bundled libavif and dav1d). Each prints a report and exits non-zero
on failure.

| test | what it covers |
|---|---|
| `decode` | Loads the 16 files in `data/` (libavif's own corpus plus one derived from it, see `data/README.md`) and checks everything the plugin decides: detection, the bitmap type picked per pixel format (8-bit to 24/32-bit, 10/12-bit to `FIT_RGB16`/`FIT_RGBA16`), the `clap`/`irot`/`imir` transforms, ICC/Exif/XMP, the `FrameTime`/`Loop` tags and page count of image sequences, header-only loads, memory streams, a truncated stream, and a pixel checksum per file. One file is expected to be refused. |
| `narrowio` | Streams a file through a `FreeImageIO` whose absolute seeks and tells refuse anything past a cap, and loads an AVIF that sits behind 777 bytes of junk with `FreeImage_LoadFromHandle`. The pixels must equal a plain `FreeImage_Load`. |

## Running

Build the library first (`make` in the repo root), then:

    make run            # decode + narrowio
    make asan-run       # the same, against a copy of the library whose plugin, libavif
                        # and dav1d objects are rebuilt with AddressSanitizer
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
  lines are the truncated-stream check working. `clap_irot_imir_non_essential.avif`
  is reported as `refused -> ok`: libavif rejects a `clap` that is not marked
  essential whatever the strictness setting, and the plugin passes that on.
- `narrowio` - `pixels -> exact` twice.
- `asan-run` - the same output, and no AddressSanitizer report.
