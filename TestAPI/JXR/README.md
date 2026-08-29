# JPEG XR regression tests

Five standalone programs covering the JXR plugin. Each prints a report and exits
non-zero on failure. They exist because the defects they cover all live in places
ordinary round-trip testing never reaches: error paths, unexercised save flags,
64-bit arithmetic, and the behaviour of `FreeImageIO`'s 32-bit `long` on LLP64.

| test | what it covers |
|---|---|
| `regress` | Round-trips 11 pixel formats x 3 quality settings and prints a size + checksum per output. **Diff the output before and after a change**: any difference on an existing combination is a regression. Also asserts which formats `JXR_LOSSLESS` round-trips bit-exactly. |
| `edge` | 11 formats x 8 tiny sizes x 5 quality settings, 440 round-trips - including the low-quality YUV 4:2:0 plus two-level-overlap path, which the codec rejects below two macroblocks of width. Also `JXR_PROGRESSIVE` and partial-file cleanup. Run under ASan. |
| `narrowio` | A `FreeImageIO` whose `tell_proc` cannot report a position past a small cap: the Win64 `long` wall, scaled down about 500,000x. The plugin must not depend on `tell_proc` at all. |
| `meta` | ICC + EXIF + XMP round-trip, covering `WriteMetadata`'s return value and `ReadProfile`. |
| `bigfile` | 46341 x 46341 (2.15 Gpx) saved lossless to a 2.17 GiB file and verified pixel by pixel on reload. Needs ~4.5 GB RAM, ~2.4 GB free space, ~2 minutes. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # regress, edge, narrowio, meta
    make asan run       # the same, with AddressSanitizer - worth it for edge and meta
    make bigfile-run    # the 2 GiB test, separately: it is slow and memory-hungry

Scratch files are written to `$JXR_TEST_TMP`, or the current directory.
`make clean` removes them.

## The one branch that needs a special build

`_jxr_io_SetPos` reaches offsets beyond `LONG_MAX` by rewinding and stepping
forward. Where `long` is 64-bit that branch is unreachable, so it would otherwise
ship to Windows untested. `PluginJXR.cpp` therefore takes an overridable bound:

    make narrowio-step

builds `PluginJXR.o` with `-DFI_JXR_SEEK_STEP_MAX=4096` into a private copy of the
library (the installed one is untouched) and runs `narrowio` with a matching cap on
absolute seeks. Expected: save ok, reload ok, pixels exact. Against the library as
it was before the fix, the save instead stops after 216 KB with no alpha plane.

## What "expected" looks like

- `regress` - byte-identical checksums to your recorded baseline; `exact` for every
  integer format under lossless. `not bit-exact (expected for this format)` on the
  16-bit packed and float formats is correct: JPEG XR's float pipeline works in a
  fixed-point internal space, and 16bpp 565/555 loses bits through the codec.
- `edge` - `440/440 round-trips ok`, progressive output differing from sequential,
  and no file left behind by a refused save.
- `narrowio` - `save -> ok`, `reload -> ok`, `pixels -> exact`, with `tell_proc
  refused 0 queries`.
- `meta` - ICC profile back identical, XMP present. `EXIF_MAIN` returning fewer tags
  than it started with is expected: the JXR container defines only a subset.
- `bigfile` - `crosses 2GiB=YES` and `exact round-trip over all 2.15 Gpx`.
