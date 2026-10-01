# I/O layer tests

Six standalone programs for the file positions FreeImage carries: `FreeImageIO`'s
`seek_proc` and `tell_proc` take and return `INT64`, `FreeImage_Load()` and
`FreeImage_Save()` seek in 64 bits, memory streams hold what memory allows, an
image need not start at byte 0 of its stream, a save whose writes fail says so,
and a failed allocation breaks nothing.
Each prints a report and exits non-zero on failure.

| test | what it covers |
|---|---|
| `bigfile` | Writes two sparse BigTIFFs whose first page sits at 2.5 GB and 4.5 GB, with a second page behind it, and reads them with `FreeImage_Load`, `FreeImage_LoadFromHandle` (a `FreeImageIO` of the test's own), `FreeImage_OpenMultiBitmap` and `FreeImage_OpenMultiBitmapFromHandle`. Every pixel of both pages must come back. A small TIFF saved with `TIFF_BIGTIFF_FORMAT`, alone and as a two-page document, must be BigTIFF and load back; saved without it, classic TIFF. A 65536 x 65600 8-bit BMP, 4.3 GB, which its 32-bit size field cannot describe, must be refused before anything is written, with and without RLE (its rows overlap in a small buffer). 32-bit PSDs saved into a `FreeImageIO` that keeps nothing: 24000 x 24000, 2.3 GB of pixels, must be PSB, which Photoshop needs past 2 GB, and 20000 x 20000 stays PSD. Then a 4 x 1 RLE TGA with 4 GB after its pixels, whose read cache is sized from what follows, a 16 x 2 RLE SGI whose second row starts 3 GB into the file (its offsets are unsigned), and on 32-bit builds a WebP in a stream of 4 GB - 1 bytes, which must be refused before it is read. The files are sparse: 4.5 GB apparent, a few hundred KB on disk. |
| `memstream` | Wraps a 2.5 GB buffer holding a PNG with `FreeImage_OpenMemory` and loads it, seeks to its end and reads the last bytes; a 32-bit process takes the largest buffer over 2 GB it can reserve, or skips. On 64-bit builds it wraps 4.5 GB with `FreeImage_OpenMemory64` too, which `FreeImage_AcquireMemory64` reports and `FreeImage_AcquireMemory` refuses. The 64-bit exports on a stream of FreeImage's own: a seek to 5 GB (refused on 32-bit), `FreeImage_TellMemory` there (-1 where a `long` is 32-bit), and a size no buffer can have refused by `FreeImage_OpenMemory64`. `--grow` (64-bit) writes 64 bytes at 2 GB into a stream of FreeImage's own and reads them back (2 GB of RAM). |
| `bigsave` | Saves a 47000 x 47000 8-bit image as an uncompressed TIFF, 2.2 GB, and reloads its header: libtiff writes the IFD past 2 GB, and the file stays classic TIFF. `--4g` saves 65536 x 65600, 4.3 GB, which must come out as BigTIFF. `--full` reloads the pixels too. Needs 2.2 GB of RAM and of disk, 4.3 GB with `--4g`, and as much RAM again with `--full`. |
| `streams` | Images behind 777 bytes of other data: an ICO saved there (one page, and two through `FreeImage_SaveMultiBitmapToHandle`, which reads page 0 back while it writes page 1) and reloaded, a GIF whose logical screen must come from its own header (with and without `GIF_PLAYBACK`), a multi-page TIFF in a memory stream. A TGA thumbnail claiming more pixels than the file holds is dropped; a TGA written through a `FreeImageIO` whose positions read 5 GB more past the header leaves its thumbnail out, since the footer's offsets are 32-bit, instead of writing it over the pixels. `make asan-run` rebuilds the sources it exercises with AddressSanitizer. |
| `writefail` | Saves through a `FreeImageIO` whose writes stop after 100 bytes, then half way through the file, as on a full disk, in every writable format and through `FreeImage_SaveMultiBitmapToHandle`: each save must return FALSE, and return at all (a watchdog fails the test after 120 s). On Linux, not as root, `FreeImage_Save` to `/dev/full` must return FALSE too: its last bytes fail in `fclose()`. |
| `allocfail` | A model whose only tag was deleted must enumerate as empty. Then the 1st, 2nd, ... `malloc`, `calloc`, `realloc`, `new(std::nothrow)` or (for the multi-page and metadata cases) the throwing `new` of `libfreeimage.a` fails, one per forked child, until a run gets through with none failing (the first three are wrapped at link time, the operator `new`s replaced): creating, setting and cloning tags, `FreeImage_SetMetadata`, `FreeImage_Clone` and `FreeImage_CloneMetadata` must not crash, and no stored tag may lack its value; the multi-page mutators (`FreeImage_AppendPage`, `InsertPage`, `MovePage`, `DeletePage`, a changed `UnlockPage`) and `FreeImage_LoadMultiBitmapFromMemory` must leave the document consistent, not crash; a save, among them a lossy WebP save of an image with graded alpha, whose alpha plane goes through the lossless encoder, must return FALSE or a file that loads, and a load must not crash; color conversions - 8- and 16-bit, in place, to and from a CMYK press profile with black point compensation, a soft proof with a gamut check, a LUT profile with a Lab connection space, setting the display profile and `FIF_LOAD_DISPLAY_ICC` loads - must fail or give an image carrying the profile its pixels are in, and an APNG or animated WebP whose frames carry different profiles must be saved whole or not at all. The sweeps need `fork()` and GNU ld's `--wrap`: on Windows they are skipped. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run             # bigfile, memstream, streams, writefail, allocfail
    make asan-run        # streams, with AddressSanitizer
    make memstream-grow  # the 2 GB write
    make bigsave-run     # the 2.2 GB save
    make bigsave-4g-run  # the 4.3 GB save, as BigTIFF

Scratch files are written to `$IO_TEST_TMP`, or the current directory.
`make clean` removes them.

## What "expected" looks like

Every line `ok` and `--- 0 failure(s) ---`. On Windows the same programs build
with `zig cc -target x86_64-windows-gnu` (`zig c++` for allocfail) or MSVC against the static library;
`bigfile` compiled with `-DFI_TEST_OFF_T=long` against the header of FreeImage
3.18 or 3.19 shows the 2 GB wall those versions had there.
