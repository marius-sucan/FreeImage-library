# Test images

All files come from libavif's test corpus (`tests/data/` of
https://github.com/AOMediaCodec/libavif at v1.4.2), where each of them is listed
with "License: same as libavif", i.e. the BSD 2-Clause license in
`Source/LibAVIF/LICENSE`. libavif's `tests/data/README.md` describes their
provenance in detail; in short:

| file | what it exercises |
|---|---|
| `white_1x1.avif` | the smallest possible image |
| `alpha_noispe.avif` | alpha item without an `ispe` property (strict mode would reject it) |
| `clap_irot_imir_non_essential.avif` | clean aperture, rotation and mirroring, all applied |
| `clop_irot_imor.avif` | the same pixels with `clap`/`imir` renamed to unknown boxes: only the rotation applies |
| `abc_color_irot_alpha_irot.avif` | rotation with a correctly associated alpha item |
| `abc_color_irot_alpha_NOirot.avif` | rotation with the alpha item missing the `irot` association |
| `colors-animated-8bpc.avif` | 8-bit image sequence |
| `colors-animated-8bpc-alpha-exif-xmp.avif` | 8-bit sequence with alpha, Exif and XMP |
| `colors-animated-12bpc-keyframes-0-2-3.avif` | 12-bit sequence with non-keyframe frames |
| `paris_icc_exif_xmp.avif` | ICC profile, Exif and XMP on a photo |
| `sofa_grid1x5_420.avif` | 1x5 grid image, 4:2:0 |
| `seine_hdr_rec2020.avif` | 10-bit HDR (PQ, Rec. 2020) still |
| `color_grid_alpha_nogrid.avif` | grid color item with a non-grid alpha item |
| `draw_points_idat.avif` | image data stored in an `idat` box |
| `circle_custom_properties.avif` | unknown item properties, which must be ignored |
