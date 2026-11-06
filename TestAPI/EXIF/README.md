# Exif orientation tests

`orientation` checks the orientation of images that load upright. The JPEG plugin turns the pixels with
`JPEG_EXIFROTATE`; the HEIF and AVIF plugins apply `clap`, `irot` and `imir`; libjxl applies the codestream's
orientation. Their Exif tag, raw Exif block and XMP `tiff:Orientation` then say 1, because the JPEG, WebP and JPEG XL
writers copy the raw block and the packet, and a viewer that honours them would turn the image again.

| part | what it covers |
|---|---|
| JPEG | Orientations 1 to 9 spliced into `../JPEG/data/fi_jpeg_420.jpg`, little- and big-endian, SHORT, LONG and BYTE entries, XMP as an attribute and as an element. A load without `JPEG_EXIFROTATE` keeps them; full and header-only loads with it say 1 when they turn the image and keep the values otherwise; a JPEG saved from the turned image loads with `JPEG_EXIFROTATE` at the size it was saved. |
| WebP, JPEG XL | An unturned Orientation 6 saved as WebP comes back as 6, since WebP has no orientation of its own; saved as JPEG XL, whose codestream orientation is then 1, it comes back as 1. |
| HEIF, AVIF | `../HEIF/data/hevc32-mini-exif.heif` and `../AVIF/data/paris_icc_exif_xmp.avif` with their Exif Orientation patched in memory, and `../HEIF/data/rainbow_irot1.heic` with its XMP orientation patched: 1 in full and header-only loads, at the size of the unpatched file. |
| RAW | The metadata of a header-only `JPEG_EXIFROTATE` load cloned onto another bitmap, as PluginRAW gives a decode the Exif data of its JPEG preview. |

## Running

Build the library first (`make -f Makefile.gnu dist` in the repo root), then `make run`.
`LIBFI=<path>/libfreeimage.a make run` runs the test against another build.
