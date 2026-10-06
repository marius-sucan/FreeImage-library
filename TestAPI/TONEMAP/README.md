# Tone mapping advice test

`musttonemap.c` covers `FreeImage_MustTonemap()`
(`Source/FreeImage/MustTonemap.cpp`), `FreeImage_ConvertToLinear()`
(`Source/FreeImage/ConversionLinear.cpp`) and the `CICP` tag the PNG, AVIF, HEIF and RAW loaders attach and the tone mapping operators rewrite.
It prints a report and exits non-zero on failure.

| part | what it covers |
|---|---|
| image types | NULL; 1- to 32-bit standard bitmaps; the scalar types (INT16, UINT32, INT32, DOUBLE, COMPLEX); RGBF and RGBAF, header-only too. |
| 16-bit images | UINT16, RGB16 and RGBA16 without a description, named as PNG, TIFF, PGM or camera RAW; 8-bit samples widened by shifting and by ×257; 12-bit samples stored unscaled; black; a dark widened 8-bit image; 2 MP of 12-bit samples whose one bright pixel the sample grid misses; 24 MP timing; header-only bitmaps; 16-bit CMYK. |
| the CICP tag | every kind of transfer code (PQ, HLG, linear, logarithmic, sRGB, BT.709, BT.2020, unspecified); PQ on RGBF and FLOAT images too; the code points outrank the ICC profile; a camera RAW tagged with a display curve. |
| unscaled samples | the `UnscaledBits` tag on RGB16, UINT16 and 8-bit images (and its outranking PQ), at the type's full width, on a float image; `SignificantBits`, the scaled samples' tag. |
| ICC profiles | the seven built-in profiles; Little CMS profiles with gamma 1.0, 2.8 and 5, a PQ tone curve with and without its cicp tag, cicp tags saying HLG and linear; a Lab profile; a damaged profile; colord's ECI-RGB v2 (L*), Rec709, Adobe RGB, ProPhoto and Gamma6500K profiles when installed. |
| grey float | FLOAT named as PFM, EXR or TIFF; within 0..1; one hot pixel in a million; 0.1% of the pixels above white; 4% negative; nothing but NaN; header-only. |
| PNG cICP | 16-bit PNGs with a gAMA of 1.0 and a cICP chunk: the tag, the pixels the file holds (cICP outranks gAMA), the verdict; header-only; a gAMA alone is still corrected. |
| `FreeImage_ConvertToLinear` | NULL, FIT_DOUBLE and header-only refused; nine curves on a row of grey levels (PQ, sRGB, BT.709, BT.2020, gamma 2.2 and 2.8, SMPTE 240M, logarithmic, linear), against formulas written out in the test; PQ 0.58 = SDR white and 1.0 = 49.26; HLG grey 0.75 = 1.0 and 1.0 = 4.93, a colour with BT.2020 and BT.709 weights, and to BT.709 primaries after the display step; untagged 16-bit, 24-bit and float images; the alpha of RGBA16 and 32-bit images; unscaled 12-bit samples; BT.2020 and Display P3 red in BT.709, grey kept grey, 20.0 through the matrix; linear sRGB, sRGB, gamma 2.2 and per-channel 1.8/2.2/2.6 profiles, the linear copy the result keeps, ProPhoto red in BT.709, 20.0 through a linear BT.2020 profile; grey profiles; a LUT-based profile (`../ICC/data/test3.icc`); 16-bit CMYK; `seine_hdr_rec2020.avif` tone mapped from linear light. |
| sample files | `../AVIF/data` (PQ, sRGB, unspecified), `../HEIF/data` (10- and 12-bit without nclx, sRGB), `../../HDR-tests` (Radiance HDR, OpenEXR, JPEG XR, CR2 and ORF at 16 and 8 bits, a preview, a header-only Panasonic RAW, a CR2 stripped of its tag); missing files are skipped. |
| tone mapped images | Drago03, Reinhard05 and Fattal02 on RGBF light untagged, tagged linear in BT.709 and in BT.2020 primaries, on RGB16 linear light and on a CR2 decoded at 16 bits: the output's CICP tag keeps the primaries and says sRGB (13/0/1), the input's is untouched, and `FreeImage_ConvertToLinear` reads the output through the sRGB curve. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then:

    make run


The test was checked with planted bugs, each caught: cICP no longer outranking gAMA, no confirming
pass after the sample grid, PQ read as a display curve, widened 8-bit samples never detected, the RAW
output left undescribed, a narrower linear band, the HEIF and AVIF code points not attached, the
`UnscaledBits` tag ignored, PQ floats not given 3, PQ's m1 and m2 swapped, no HLG display step,
BT.709 weights for BT.2020 light, the matrix transposed or applied before the HLG step, the unscaled
precision ignored, one tone curve for every channel, alpha over 256, the result keeping
`UnscaledBits`, and the tone mapped output keeping its input's linear CICP tag.
