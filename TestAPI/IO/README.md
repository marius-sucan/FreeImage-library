# I/O layer tests

Four standalone programs for the file positions FreeImage carries: `FreeImageIO`'s
`seek_proc` and `tell_proc` take and return `INT64`, `FreeImage_Load()` and
`FreeImage_Save()` seek in 64 bits, memory streams hold what memory allows, and an
image need not start at byte 0 of its stream.
Each prints a report and exits non-zero on failure.

| test | what it covers |
|---|---|
| `bigfile` | Writes two sparse BigTIFFs whose first page sits at 2.5 GB and 4.5 GB, with a second page behind it, and reads them with `FreeImage_Load`, `FreeImage_LoadFromHandle` (a `FreeImageIO` of the test's own), `FreeImage_OpenMultiBitmap` and `FreeImage_OpenMultiBitmapFromHandle`. Every pixel of both pages must come back. The files are sparse: 4.5 GB apparent, a few hundred KB on disk. |
| `memstream` | Wraps a 2.5 GB buffer holding a PNG with `FreeImage_OpenMemory` and loads it, seeks to its end and reads the last bytes. `--grow` writes 64 bytes at 2 GB into a stream of FreeImage's own and reads them back (2 GB of RAM). |
| `bigsave` | Saves a 47000 x 47000 8-bit image as an uncompressed TIFF, 2.2 GB, and reloads its header: libtiff writes the IFD past 2 GB. `--full` reloads the pixels too. Needs 2.2 GB of RAM and of disk (4.4 GB of RAM with `--full`). |
| `streams` | Images behind 777 bytes of other data: an ICO saved there (one page, and two through `FreeImage_SaveMultiBitmapToHandle`, which reads page 0 back while it writes page 1) and reloaded, a GIF whose logical screen must come from its own header (with and without `GIF_PLAYBACK`), a multi-page TIFF in a memory stream. A TGA thumbnail claiming more pixels than the file holds is dropped; a TGA written through a `FreeImageIO` whose positions read 5 GB more past the header leaves its thumbnail out, since the footer's offsets are 32-bit, instead of writing it over the pixels. `make asan-run` rebuilds the sources it exercises with AddressSanitizer. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run             # bigfile, memstream, streams
    make asan-run        # streams, with AddressSanitizer
    make memstream-grow  # the 2 GB write
    make bigsave-run     # the 2.2 GB save

Scratch files are written to `$IO_TEST_TMP`, or the current directory.
`make clean` removes them.

## What "expected" looks like

Every line `ok` and `--- 0 failure(s) ---`. On Windows the same programs build
with `zig cc -target x86_64-windows-gnu` (or MSVC) against the static library;
`bigfile` compiled with `-DFI_TEST_OFF_T=long` against the header of FreeImage
3.18 or 3.19 shows the 2 GB wall those versions had there.
