# Test images

Three sources, all redistributable with their notices:

- **libheif** (https://github.com/strukturag/libheif at v1.23.4, LGPL-3.0, the
  license in `Source/LibHEIF/COPYING`): `clap_cropped.heic`,
  `conformance_window_padding.heic`, `rainbow-451x461.heic` and
  `with-alpha-512x512.heic` from `tests/data/`, `example.heic` from `examples/`
  (the MIT-licensed sample directory), and from `fuzzing/data/corpus/`:
  `colors-no-alpha.heic`, `colors-no-alpha-thumbnail.heic`,
  `colors-with-alpha.heic`, `colors-with-alpha-thumbnail.heic`, `hevc32.heif`,
  `hevc32-mini.heif`, `avif32.heif`, `avc32.heif`, `jpeg32.heif`, `j2k32.heif`
  and `unci32.heif`.
- **pillow-heif** (https://github.com/bigcat88/pillow_heif, BSD-3-Clause), the
  synthetic images of its `tests/images/heif/` directory: `L_8__128x128.heif`,
  `L_10__128x128.heif`, `L_12__128x128.heif`, `LA_8__128x128.heif`,
  `RGB_8__128x128.heif`, `RGB_10__128x128.heif`, `RGB_12__128x128.heif`,
  `RGBA_8__128x128.heif`, `RGBA_10__128x128.heif`, `RGBA_12__128x128.heif`,
  `L_xmp.heif` and `zPug_3.heic`.
- **derived here** from libheif's `rainbow-451x461.heic` (same license):
  `rainbow_irot0.heic`, `rainbow_irot1.heic`, `rainbow_imir0.heic` and
  `rainbow_imir1.heic`. Its 40-byte `clap` box is rewritten in place as an
  `irot` (angle 0 or 1) or `imir` (axis 0 or 1) box padded with zeros, so no
  offset in the file moves and the property keeps its `ipma` association.
  `rainbow_irot0` is therefore the uncropped image, and the other three are what
  libheif must make of a rotation and the two mirrorings of it.

| file | what it exercises |
|---|---|
| `clap_cropped.heic` | clean aperture: 256 x 256 coded, 64 x 64 shown |
| `conformance_window_padding.heic` | a 1 x 1 image whose HEVC stream is padded to 2 x 2, with Exif |
| `rainbow-451x461.heic` | ICC profile ('prof'), XMP, clap |
| `with-alpha-512x512.heic` | alpha channel, ICC profile |
| `example.heic` | a photo (1280 x 854) with a 320 x 212 'thmb' thumbnail and a second top-level image |
| `rainbow_irot0/1.heic`, `rainbow_imir0/1.heic` | rotation and mirroring, see above |
| `colors-*.heic` | 8-bit RGB / RGBA, with and without a thumbnail |
| `hevc32.heif` | HEVC item behind a generic 'mif1' brand list |
| `hevc32-mini.heif` | the compact 'mini' box layout (brand 'mif3') |
| `avif32.heif` | an AV1 payload: the AVIF plugin's file, not this one's |
| `avc32.heif`, `jpeg32.heif`, `j2k32.heif`, `unci32.heif` | HEIF with AVC, JPEG, JPEG 2000 and uncompressed payloads: detected as HEIF, refused with a message |
| `L_*`, `LA_8`, `RGB_*`, `RGBA_*` | monochrome, monochrome + alpha, RGB and RGBA at 8, 10 and 12 bits |
| `L_xmp.heif` | XMP on a monochrome image |
| `zPug_3.heic` | three top-level images (three pages), each with a thumbnail |
