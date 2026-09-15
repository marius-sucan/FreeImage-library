# OpenEXR regression tests

Three standalone programs covering the EXR plugin (`Source/FreeImage/PluginEXR.cpp`)
and the library behind it: the bundled OpenEXR (`Source/OpenEXR`), its C core
(`Source/OpenEXR/OpenEXRCore`) and the libdeflate that core compresses with
(`Source/LibDeflate`). Each prints a report and exits non-zero on failure. They
were written for the upgrade from OpenEXR 3.1.3 to 3.3.14 and are meant to be
run again at the next one.

| test | what it covers |
|---|---|
| `decode` | Loads the 14 files of `data/` and checks the bitmap type, depth and decoded pixels against a recorded table, plus format detection, a memory stream and a header-only load for each. The corpus is the point: it covers what FreeImage itself cannot write, so nothing else here reaches it - RLE, ZIPS, B44A, DWAA and DWAB compression, tiled and mipmapped layouts, 32-bit float channels, and a subsampled luminance/chroma file. |
| `regress` | Round-trips the three exportable pixel types (`FIT_FLOAT`, `FIT_RGBF`, `FIT_RGBAF`) across six sizes and twelve save flags, 216 cases, through a file and through a memory stream. Asserts that the lossless flags all reload to one checksum, that file and memory agree, and that every documented refusal is exactly the documented one. Encoded sizes are reported, not asserted: they legitimately move when the compressor does. |
| `robust` | Truncated prefixes, junk appended, single-byte corruptions, four-byte length fields set to nonsense, 64-byte regions wiped, empty and tiny buffers - 5284 damaged inputs across six files. They may load or be refused; they may not crash. Worth running under AddressSanitizer, which is what `make asan-run` is for. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # all three
    make asan-run       # rebuild OpenEXR, OpenEXRCore, libdeflate and the
                        # plugin with AddressSanitizer into a private copy of
                        # the library, and run the three against that

Scratch files are written to `$EXR_TEST_TMP`, or the current directory.
`make clean` removes them along with the ASan objects.

## The corpus

`data/` is generated, not collected: `make data` rebuilds it with `mkdata.cpp`,
which writes the files through the bundled OpenEXR itself - the only encoder
here that can produce them, since FreeImage's own `EXR_*` save flags reach
neither RLE, ZIPS, B44A, DWAA, DWAB nor the tiled and mipmapped layouts. The
files are one 64x48 picture, so the eight lossless variants must decode to the
same checksum as each other; that shared value is the strongest assertion in
`decode`, because a single compressor drifting shows up alone.

Do not regenerate it casually - the checksums recorded in `decode.c` are tied
to these exact bytes. `./decode --record` reprints the table when a change to
them is deliberate.

## Two things these tests pin down rather than check

**`EXR_LC` produces files FreeImage cannot read back.** The flag saves
luminance and chroma with the chroma subsampled; `PluginEXR` reads only RGB(A)
and Y layouts and rejects the result with "Unsupported color model:
A/BY/RY/Y". `data/fi_exr_yc.exr` and the `LC` rows in `regress` record that.
It predates OpenEXR 3.3 - the 3.1.3 the library shipped before behaved the
same - and fixing it means routing such files through `Imf::RgbaInputFile`,
which reconstructs RGB from the subsampled channels.

**A corrupted data window is only caught by the allocation failing.** No
FreeImage plugin sanity-checks a header's dimensions before asking for the
bitmap, so a damaged EXR claiming 65535x65535 leads to a tens-of-gigabytes
request that fails, after which the file is refused. That is why `asan-run`
passes `allocator_may_return_null=1`: ASan would otherwise abort on the
request instead of letting the library take its refusal path.
