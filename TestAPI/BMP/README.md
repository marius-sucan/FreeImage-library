# BMP tests

A standalone program for the BMP loader's colour masks and alpha. Every file is
built in memory, so the suite carries no data. It prints a report and exits
non-zero on failure.

| test | what it covers |
|---|---|
| `masks` | 32-bit files: with `BI_RGB` the fourth byte, which the format calls unused, is alpha only when some pixel sets it, so a file holding 0 there loads opaque (40-byte, OS/2 1.x and 2.x headers) and FreeImage's own RGBA saves keep their alpha; with `BI_BITFIELDS` it is alpha only under an alpha mask, from a 56-byte header on (none in a 40- or 52-byte one), and an alpha mask that overlaps a colour counts as none. 32-bit masks in any order and width (R and B swapped, 10-10-10-2, 11-11-10) decode to 8 bits per channel, each value rounded to the nearest. 16-bit: 5-5-5 and 5-6-5 stay 16-bit; other masks become 24-bit, or 32-bit with an alpha mask (4-4-4-4, 1-5-5-5); an alpha mask beyond 16 bits is none. `BI_ALPHABITFIELDS`: four masks after a 40-byte header, 16- and 32-bit, bottom-up and top-down; refused at 8 bits. Every case loads header-only (`FIF_LOAD_NOPIXELS`) at the full load's bit depth. A cut file keeps the rows it holds and, without alpha, the rest is opaque; a file cut inside its masks loads blank as 5-5-5. Last, a 32-bit image saved by FreeImage loads back with its alpha, unless its alpha is 0 everywhere, which loads opaque. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run        # masks
    make asan-run   # masks, with PluginBMP.cpp rebuilt with AddressSanitizer and UBSan

## What "expected" looks like

Every line `ok` and `--- 0 failure(s) ---`. On Windows the program builds with
`zig cc -target x86_64-windows-gnu` or MSVC against the static library.
