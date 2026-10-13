# BMP tests

Standalone programs for the BMP loader's colour masks, alpha and ICC profiles, and
the writer's alpha. Every file is built in memory, so the suite carries no data.
Each prints a report and exits non-zero on failure.

| test | what it covers |
|---|---|
| `masks` | 32-bit files: with `BI_RGB` the fourth byte, which the format calls unused, is alpha only when some pixel sets it, so a file holding 0 there loads opaque (40-byte, OS/2 1.x and 2.x headers) and FreeImage's own RGBA saves keep their alpha; with `BI_BITFIELDS` it is alpha only under an alpha mask, from a 56-byte header on (none in a 40- or 52-byte one), and an alpha mask that overlaps a colour counts as none. 32-bit masks in any order and width (R and B swapped, 10-10-10-2, 11-11-10) decode to 8 bits per channel, each value rounded to the nearest. 16-bit: 5-5-5 and 5-6-5 stay 16-bit; other masks become 24-bit, or 32-bit with an alpha mask (4-4-4-4, 1-5-5-5); an alpha mask beyond 16 bits is none. `BI_ALPHABITFIELDS`: four masks after a 40-byte header, 16- and 32-bit, bottom-up and top-down; refused at 8 bits. Every case loads header-only (`FIF_LOAD_NOPIXELS`) at the full load's bit depth. A cut file keeps the rows it holds and, without alpha, the rest is opaque; a file cut inside its masks loads blank as 5-5-5. Pixels after a gap: an OS/2 1.x header's 24- and 32-bit pixels are read where `bfOffBits` says. Last, FreeImage's own 32-bit saves: an image whose alpha says something is written with a `BITMAPV5HEADER` (`BI_BITFIELDS`, alpha mask `FF000000`, sRGB, no profile) and loads back unchanged; opaque, never-set (0 everywhere) and CMYK images keep the 40-byte `BI_RGB` header. |
| `icc` | `BITMAPV5HEADER` files with an embedded ICC profile (after the pixels, as in the format): the profile is attached in full and header-only loads, also to a palette image, and one padded in the file is cut to its own size. Not attached, the image loading as usual: a linked profile (a file name, never opened), `sRGB` with profile bytes present, an offset past the file or into the header, a size below an ICC header or of 4 GB, a grey, CMYK or damaged profile, a V4 header. With the display set to sRGB, `FIF_LOAD_DISPLAY_ICC` converts an Adobe RGB file's pixels (or a palette image's palette), exactly as `FreeImage_ApplyDisplayICCProfile()` does, and leaves an sRGB-tagged file's pixels alone. `FreeImage_FIFSupportsICCProfiles(FIF_BMP)` is TRUE, and a saved image carries no profile. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run        # masks, icc
    make asan-run   # both, with PluginBMP.cpp rebuilt with AddressSanitizer and UBSan

## What "expected" looks like

Every line `ok` and `--- 0 failure(s) ---`. On Windows the programs build with
`zig cc -target x86_64-windows-gnu` or MSVC against the static library.
