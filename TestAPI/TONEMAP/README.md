# Tone mapping advice test

`musttonemap.c` covers `FreeImage_MustTonemap()` and `FreeImage_MustTonemapU()`
(`Source/FreeImage/MustTonemap.cpp`) and the `CICP` tag the PNG, AVIF, HEIF and RAW loaders attach.
It prints a report and exits non-zero on failure.

| part | what it covers |
|---|---|
| image types | NULL; 1- to 32-bit standard bitmaps; the scalar types (INT16, UINT32, INT32, DOUBLE, COMPLEX); RGBF and RGBAF, header-only too. |
| 16-bit images | UINT16, RGB16 and RGBA16 without a description, named as PNG, TIFF, PGM or camera RAW; 8-bit samples widened by shifting and by ×257; 12-bit samples stored unscaled; black; a dark widened 8-bit image; 2 MP of 12-bit samples whose one bright pixel the sample grid misses; 24 MP timing; header-only bitmaps; 16-bit CMYK. |
| the CICP tag | every kind of transfer code (PQ, HLG, linear, logarithmic, sRGB, BT.709, BT.2020, unspecified); the code points outrank the ICC profile; a camera RAW tagged with a display curve. |
| ICC profiles | the seven built-in profiles; Little CMS profiles with gamma 1.0, 2.8 and 5, a PQ tone curve with and without its cicp tag, cicp tags saying HLG and linear; a Lab profile; a damaged profile; colord's ECI-RGB v2 (L*), Rec709, Adobe RGB, ProPhoto and Gamma6500K profiles when installed. |
| grey float | FLOAT named as PFM, EXR or TIFF; within 0..1; one hot pixel in a million; 0.1% of the pixels above white; 4% negative; nothing but NaN; header-only. |
| PNG cICP | 16-bit PNGs with a gAMA of 1.0 and a cICP chunk: the tag, the pixels the file holds (cICP outranks gAMA), the verdict; header-only; a gAMA alone is still corrected. |
| sample files | `../AVIF/data` (PQ, sRGB, unspecified), `../HEIF/data` (10- and 12-bit without nclx, sRGB), `../../HDR-tests` (Radiance HDR, OpenEXR, JPEG XR, CR2 and ORF at 16 and 8 bits, a preview, a header-only Panasonic RAW, a CR2 stripped of its tag); missing files are skipped. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run

On Linux the `U` entry point classifies from the bitmap alone, since `FreeImage_GetFileTypeU()`
reads no Unicode file names there; the file-based checks use the ANSI entry point.

The test was checked with eight planted bugs, each caught: cICP no longer outranking gAMA, no
confirming pass after the sample grid, PQ read as a display curve, widened 8-bit samples never
detected, the RAW output left undescribed, a narrower linear band, and the HEIF and AVIF code points
not attached.
