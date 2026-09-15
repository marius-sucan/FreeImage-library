# Test images

All files but four come from libavif's test corpus (`tests/data/` of
https://github.com/AOMediaCodec/libavif at v1.4.2), where each of them is listed
with "License: same as libavif", i.e. the BSD 2-Clause license in
`Source/LibAVIF/LICENSE`. libavif's `tests/data/README.md` describes their
provenance in detail. `draw_points_idat_two_ipma.avif` is derived from
`draw_points_idat.avif` with `splitipma.py` (same license): its single `ipma`
box is split into two boxes with the same version and flags, one per item, which
is what link-u's cavif encoder writes and what upstream libavif refuses as
"Multiple Box[ipma] with a given pair of values of version and flags"; the
bundled libavif is patched to accept it, and the file must decode to exactly the
pixels of its source.

The other three come from **libheif** (`tests/data/` of
https://github.com/strukturag/libheif at v1.23.4, LGPL-3.0, the license in
`Source/LibHEIF/COPYING`, as for the files in `TestAPI/HEIF/data/`):
`simple_osm_tile_meta.avif`, `simple_osm_tile_alpha.avif` and
`mini_size_zero.avif`. All three replace the `meta` box with a
MinimizedImageBox, the compact header of a file branded `mif3`, which libavif
reads only when it is built with `AVIF_ENABLE_EXPERIMENTAL_MINI`. Despite the
name they hold no map data: each is a synthetic 256 x 256 tile (the size
OpenStreetMap serves) with the word "Red" drawn on it. The last two are the same
image, and so must decode to the same pixels.

A `mif3` file names no codec in its brands, so these also pin which plugin takes
one: the AVIF plugin claims a `mif3` file only when the FileTypeBox
minor_version is `avif`, and leaves the HEVC ones (`hevc32-mini.heif` in
`TestAPI/HEIF/data/`, minor version `heic`) to the HEIF plugin, which reads
them. FreeImage asks each plugin once and does not move on when the load then
fails, so a plugin that claims too much makes files unreadable rather than
merely mis-routed.

In short:

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
| `draw_points_idat_two_ipma.avif` | the same file with its `ipma` box split in two (same version and flags), see above |
| `circle_custom_properties.avif` | unknown item properties, which must be ignored |
| `simple_osm_tile_meta.avif` | MinimizedImageBox (`mif3`), 4:4:4, with an ICC profile, Exif and XMP |
| `simple_osm_tile_alpha.avif` | the same box layout with an alpha channel, no Exif or XMP |
| `mini_size_zero.avif` | the same file again with a `mini` box of size 0, i.e. one running to the end of the file |
