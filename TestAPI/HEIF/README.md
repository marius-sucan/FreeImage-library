# HEIF regression tests

Two standalone programs covering the HEIF plugin (`Source/FreeImage/PluginHEIF.cpp`,
built on the bundled libheif and libde265). Each prints a report and exits non-zero
on failure.

| test | what it covers |
|---|---|
| `decode` | Loads the 32 files in `data/` (libheif's test images and fuzzing corpus, pillow-heif's synthetic images and four files derived from one of libheif's, see `data/README.md`) and checks everything the plugin decides: detection (an AVIF is left to the AVIF plugin, a HEIF carrying a codec FreeImage does not have is claimed and refused with a message), the bitmap type picked per pixel format (8-bit to 24/32-bit, 10/12-bit to `FIT_RGB16`/`FIT_RGBA16`, monochrome to 8-bit or `FIT_UINT16`), the geometry after the `clap`/`irot`/`imir` transforms and their direction, ICC/Exif/XMP, the `thmb` thumbnail, the page count of multi-image files, header-only loads, memory streams, a truncated stream, and a pixel checksum per file. |
| `narrowio` | Streams a file through a `FreeImageIO` whose absolute seeks and tells refuse anything past a cap, and loads a HEIF that sits behind 777 bytes of junk with `FreeImage_LoadFromHandle`. The pixels must equal a plain `FreeImage_Load`. |

## Running

Build the library first (`make` in the repo root), then:

    make run            # decode + narrowio
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
  AVIF, not HEIF -> ok`; `avc32.heif`, `jpeg32.heif`, `j2k32.heif` and
  `unci32.heif` as `refused -> ok`, each after a message naming the codec that
  is not built in.
- `narrowio` - `pixels -> exact` twice.
- `asan-run` - the same output, and no AddressSanitizer report.
