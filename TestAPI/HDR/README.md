# Radiance HDR tests

`rgbe` saves known RGBF pixels as a Radiance HDR file in memory and loads them back. RGBE holds no negative samples:
they come back as 0, as Radiance's `setcolr()` writes them, and the other samples within the 8 bits of mantissa the
shared exponent leaves below the largest one.

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then `make run`.
`LIBFI=<path>/libfreeimage.a make run` runs the test against another build.
