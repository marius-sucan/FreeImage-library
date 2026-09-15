# JPEG 2000 regression tests

Three standalone programs covering the J2K and JP2 plugins (the bundled OpenJPEG
library, `Source/LibOpenJPEG`, and the glue in `Source/FreeImage/J2KHelper.cpp`).
Each prints a report and exits non-zero on failure. They were written for the
upgrade from the 2014 OpenJPEG snapshot to 2.5.4 and are meant to be run again
at the next upgrade.

| test | what it covers |
|---|---|
| `regress` | Round-trips 6 pixel formats x 14 sizes (1x1 to 257x129) x 4 rates x both containers through memory streams, 684 in all, plus a file round-trip per format. Prints size + checksum per output and asserts that every save and reload works and that rate 1 (lossless) reloads pixel-exact. Encoded checksums change legitimately when the encoder changes; the FAIL lines and the tally are the assertions. |
| `robust` | Truncated prefixes, junk appended, single-bit corruptions, wiped header regions, empty buffers: may load or be refused, must never crash. Also header-only loads (`FIF_LOAD_NOPIXELS`) and loads/saves through a `FreeImageIO` handle that does not start at offset 0. Run under ASan. |
| `corpus` | Decodes any JPEG 2000 files you give it, one line each (geometry, type, checksum of the pixel rows, decode time, messages) and can dump the pixels for the reference comparison below. Diff its output before and after a change. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run            # regress and robust
    make asan run       # the same, with AddressSanitizer - worth it for robust
    make corpus         # then: ./corpus [-h] [-m] [-o N] [-d DIR] files...

Scratch files are written to `$J2K_TEST_TMP`, or the current directory.
`make clean` removes them.

## What "expected" looks like

- `regress` - `684 round-trips, 0 failures`, with `exact` on every rate-1 line.
- `robust` - `0 failures`; truncated prefixes are all refused (OpenJPEG's strict
  mode: a partial codestream is an error, not a partial image) while the junk-
  appended copies decode exactly, and both offset tests say `ok`.
- `corpus` on the openjpeg-data conformance suite (below) - `0 failed`, and
  every file `identical` in the reference comparison except `zoo2.jp2`, where 2
  of 7.6M samples differ by 1 (9/7 wavelet rounding between builds).

## Comparing against the reference decoder

The oracle for pixel correctness is the same OpenJPEG version built on its own,
decoding the JPEG 2000 conformance files:

    git clone --depth 1 --branch v2.5.4 https://github.com/uclouvain/openjpeg.git
    cmake -S openjpeg -B openjpeg/build -DBUILD_SHARED_LIBS=OFF -DBUILD_CODEC=OFF && cmake --build openjpeg/build
    gcc -O2 -Iopenjpeg/src/lib/openjp2 -Iopenjpeg/build/src/lib/openjp2 refdec.c \
        openjpeg/build/bin/libopenjp2.a -lm -lpthread -o refdec

    git clone --depth 1 --filter=blob:none --sparse https://github.com/uclouvain/openjpeg-data.git
    git -C openjpeg-data sparse-checkout set input/conformance input/nonregression/htj2k

    mkdir fi ref
    ./corpus -d fi openjpeg-data/input/conformance/*.j2k openjpeg-data/input/conformance/*.j2c openjpeg-data/input/conformance/*.jp2
    for f in openjpeg-data/input/conformance/*.j2k openjpeg-data/input/conformance/*.j2c openjpeg-data/input/conformance/*.jp2; do
        ./refdec "$f" "ref/$(basename "$f").comps"; done
    python3 compcheck.py fi ref openjpeg-data/input/conformance/*.j2k openjpeg-data/input/conformance/*.j2c openjpeg-data/input/conformance/*.jp2

`refdec` writes every component the reference decodes, with no colour conversion
and with FreeImage's sign convention; `compcheck.py` (needs numpy) applies the
plugins' own rule for which components they keep - all of them when there are 1,
3 or 4 of equal sampling and precision, otherwise the first one - and compares
sample by sample. `input/nonregression` holds the fuzzer crash files and the HTJ2K
samples; feed those to `corpus` under ASan as well (they are the reason the old
snapshot's "decoded" HTJ2K output was noise, and why the upgrade exists).
