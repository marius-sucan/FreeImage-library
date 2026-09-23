# OpenEXR regression tests

Five standalone programs covering the EXR plugin (`Source/FreeImage/PluginEXR.cpp`)
and the library behind it: the bundled OpenEXR (`Source/OpenEXR`), its C core
(`Source/OpenEXR/OpenEXRCore`) and the libdeflate that core compresses with
(`Source/LibDeflate`). Each prints a report and exits non-zero on failure. They
were written for the upgrade from OpenEXR 3.1.3 to 3.3.14 and are meant to be
run again at the next one.

| test | what it covers |
|---|---|
| `decode` | Loads the 15 files of `data/` and checks the bitmap type, depth and decoded pixels against a recorded table, plus format detection, a memory stream and a header-only load for each. The corpus is the point: it covers what FreeImage itself cannot write, so nothing else here reaches it - RLE, ZIPS, B44A, DWAA and DWAB compression, tiled and mipmapped layouts, 32-bit float channels, a data window away from the origin, and a subsampled luminance/chroma file. |
| `regress` | Round-trips the three exportable pixel types (`FIT_FLOAT`, `FIT_RGBF`, `FIT_RGBAF`) across six sizes and twelve save flags, 216 cases, through a file and through a memory stream. Asserts that the lossless flags all reload to one checksum, that file and memory agree, and that every documented refusal is exactly the documented one. Encoded sizes are reported, not asserted: they legitimately move when the compressor does. |
| `robust` | Truncated prefixes, junk appended, single-byte corruptions, four-byte length fields set to nonsense, 64-byte regions wiped, empty and tiny buffers - 5284 damaged inputs across six files. They may load or be refused; they may not crash. Worth running under AddressSanitizer, which is what `make asan-run` is for. |
| `lc` | Saves a flat colour with `EXR_LC` at eight heights, as `FIT_RGBF` and as `FIT_RGBAF`, and checks *every row* of what comes back plus the alpha. Deliberately not a checksum test: a hash cannot tell a scanline the reader forgot to write from one it wrote, which is how the missing bottom row survived so long. |
| `bombs` | Patches the data window of a good file into shapes no file that size could hold - both dimensions huge, height only, width only, inverted, empty, corners at the extremes - and requires each to be refused with a message - from a header-only load as well as a full one, since the check runs before the allocation either way. Under `asan-run` it is the one test that does **not** get `allocator_may_return_null`, so an attempted allocation aborts rather than quietly returning NULL. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # all five
    make asan-run       # rebuild OpenEXR, OpenEXRCore, libdeflate and the
                        # plugin with AddressSanitizer, and run all five with
                        # those objects linked ahead of the library

Scratch files are written to `$EXR_TEST_TMP`, or the current directory.
`make clean` removes them along with the ASan objects.

## The corpus

`data/` is generated, not collected: `make data` rebuilds it with `mkdata.cpp`,
which writes the files through the bundled OpenEXR itself - the only encoder
here that can produce them, since FreeImage's own `EXR_*` save flags reach
neither RLE, ZIPS, B44A, DWAA, DWAB, the tiled and mipmapped layouts, nor a
data window away from the origin. The files are one 64x48 picture, so the nine
lossless variants - `fi_exr_offset.exr` among them, which places that picture at
(100, 200) - must decode to the same checksum as each other; that shared value is
the strongest assertion in `decode`, because a single compressor or a mishandled
data window then shows up alone.

Do not regenerate it casually - the checksums recorded in `decode.c` are tied
to these exact bytes. `./decode --record` reprints the table when a change to
them is deliberate.

## Two bugs these tests were written for

Both were found while upgrading OpenEXR from 3.1.3 to 3.3.14 and are older than
that upgrade; both are fixed, and the tests are here so they stay fixed.

**The luminance/chroma reader.** `EXR_LC` writes the chroma subsampled -
`Y/BY/RY` for an `FIT_RGBF` image, `A/BY/RY/Y` for an `FIT_RGBAF` one. Only the
three-channel form was recognised, so FreeImage refused to load back the
four-channel files it had written itself. And the chunk loop copied
`dw.max.y - dw.min.y` rows out of the `dw.max.y - dw.min.y + 1` it had read, so
the bottom scanline of *every* such image was left as the zeros
`FreeImage_AllocateHeaderT` had cleared it to. `lc` covers both; `decode` covers
the four-channel form through `data/fi_exr_yc.exr`.

**The data window.** It is a claim in the header and nothing checked it against
the file, so a single flipped byte could make the plugin ask for tens of
gigabytes - and on a machine that overcommits the allocation succeeds and
clearing the bitmap then walks all of it. `bombs` covers the check that now runs
first. Its control case matters as much as its bomb cases: the check is derived
from the length of the stream rather than from a fixed maximum size, precisely
so that a genuinely enormous image still loads.