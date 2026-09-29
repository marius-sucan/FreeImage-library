# Maximum file size by format specification

This report covers every file type FreeImage registers: its 40 `FREE_IMAGE_FORMAT` values and the 43 camera-raw
extensions of the RAW plugin. For each one it gives the largest file the format's own specification allows. What
FreeImage itself can read or write is deliberately left out.

The specifications were researched online on 2026-09-29. Every limit that the summary marks *stated* was re-read in
the specification itself, and every derived number was recomputed.

## How to read it

Each limit is one of five kinds:

- **stated**: the specification gives the maximum in words.
- **field width**: no sentence gives a maximum, but a size or offset field is too narrow to describe or reach a
  larger file.
- **derived**: the file has no size or offset fields. Its size follows from header values (dimensions, bytes per
  pixel, worst-case compression), so the largest header values give the maximum.
- **unbounded**: there is no maximum. The specification allows unlimited chunks, segments, frames or boxes, or writes
  sizes as text numbers of any length.
- **fixed**: every file has the same size.

Units: 1 GiB = 2^30 bytes and 1 GB = 10^9 bytes, so 4 GiB = 4,294,967,296 bytes ≈ 4.29 GB. TiB, PiB and EiB are
2^40, 2^50 and 2^60 bytes.

## Summary

| FIF | Format | Extensions | Maximum file size | Kind |
|---|---|---|---|---|
| 0 `FIF_BMP` | Windows and OS/2 bitmap | bmp | 4,294,967,295 bytes (4 GiB − 1) | field width |
| 1 `FIF_ICO` | Windows icon | ico | 8,589,934,590 bytes (8 GiB − 2) | field width |
| 2 `FIF_JPEG` | JPEG / JFIF | jpg, jif, jpeg, jpe | none | unbounded |
| 3 `FIF_JNG` | JPEG Network Graphics | jng | none | unbounded |
| 4 `FIF_KOALA` | Koala Painter (C64) | koa | exactly 10,003 bytes | fixed |
| 5 `FIF_LBM` / `FIF_IFF` | IFF ILBM | iff, lbm | 2,147,483,654 bytes (2 GiB + 6) | field width |
| 6 `FIF_MNG` | Multiple-image Network Graphics | mng | none | unbounded |
| 7, 8, 11, 12, 14, 15 `FIF_PBM` … `FIF_PPMRAW` | Netpbm PBM, PGM, PPM, plain and raw | pbm, pgm, ppm | none | unbounded |
| 9 `FIF_PCD` | Kodak Photo CD | pcd | no published specification | — |
| 10 `FIF_PCX` | ZSoft PCX | pcx | 34,358,689,920 bytes (≈ 32 GiB) if its 16-bit fields are unsigned | derived |
| 13 `FIF_PNG` | PNG | png | none | unbounded |
| 16 `FIF_RAS` | Sun Raster | ras | 4,294,967,326 bytes (4 GiB + 30); ≈ 4 EiB for old-style files without a length | field width |
| 17 `FIF_TARGA` | Truevision TGA | tga, targa | 21,474,181,424 bytes (≈ 20 GiB) at 32 bits per pixel | derived |
| 18 `FIF_TIFF` | TIFF 6.0 | tif, tiff | 4,294,967,296 bytes (4 GiB); the BigTIFF extension 2^64 bytes | stated |
| 19 `FIF_WBMP` | WAP Wireless Bitmap | wap, wbmp, wbm | 2,305,843,008,676,823,052 bytes (≈ 2 EiB) under WAP-237; none under WAP-190 | derived |
| 20 `FIF_PSD` | Photoshop PSD and PSB | psd, psb | none in the specification (Photoshop Help: 2 GB for PSD) | unbounded |
| 21 `FIF_CUT` | Dr. Halo CUT | cut | 4,294,967,301 bytes (4 GiB + 5) | field width |
| 22 `FIF_XBM` | X11 bitmap | xbm | none | unbounded |
| 23 `FIF_XPM` | X11 pixmap | xpm | none | unbounded |
| 24 `FIF_DDS` | DirectDraw Surface | dds | ≈ 1.0 × 10^31 bytes (no practical limit) | derived |
| 25 `FIF_GIF` | GIF 87a / 89a | gif | none | unbounded |
| 26 `FIF_HDR` | Radiance RGBE | hdr | none | unbounded |
| 27 `FIF_FAXG3` | Raw CCITT Group 3 fax | g3 | none | unbounded |
| 28 `FIF_SGI` | SGI image | sgi, rgb, rgba, bw | uncompressed 562,924,184,011,262 bytes (≈ 512 TiB); RLE 2,147,745,789 bytes (≈ 2 GiB) | derived / field width |
| 29 `FIF_EXR` | OpenEXR | exr | ≈ 2^64 bytes (16 EiB) | field width |
| 30 `FIF_J2K` | JPEG 2000 codestream | j2k, j2c | none | unbounded |
| 31 `FIF_JP2` | JPEG 2000 JP2 file | jp2 | none | unbounded |
| 32 `FIF_PFM` | Portable FloatMap | pfm | none | unbounded |
| 33 `FIF_PICT` | Macintosh PICT | pct, pict, pic | version 1: a 32 KB picture plus the 512-byte header; version 2: none | stated |
| 34 `FIF_RAW` | Camera raw | 43 extensions | 4 GiB for most; DNG and Cine allow more (see Camera raw) | mixed |
| 35 `FIF_WEBP` | WebP | webp | 4,294,967,294 bytes (4 GiB − 2) | stated |
| 36 `FIF_JXR` | JPEG XR | jxr, wdp, hdp | 4,294,967,295 bytes (4 GiB − 1) | stated |
| 37 `FIF_AVIF` | AVIF still images and sequences | avif, avifs | none | unbounded |
| 38 `FIF_HEIF` | HEIF / HEIC still images and sequences | heic, heif, hif, heics, heifs | none | unbounded |
| 39 `FIF_APNG` | Animated PNG | apng, png | none | unbounded |

At a glance:

- **Stated by the specification:** TIFF 4 GiB, JPEG XR 4 GiB − 1, WebP 4 GiB − 2 and PICT version 1 32 KB. Among the
  camera raws, DNG is 4 GiB unless it uses its 64-bit layout, and Cine version 0 is "4GB".
- **About 4 GiB, set by 32-bit fields:** BMP, Sun Raster, Dr. Halo CUT, SGI RLE (2 GiB if its offsets are signed)
  and most camera raws. ICO can reach 8 GiB; IFF stops at 2 GiB.
- **Larger ceilings:** TGA ≈ 20 GiB, PCX ≈ 32 GiB, SGI uncompressed ≈ 512 TiB, WBMP ≈ 2 EiB, OpenEXR and BigTIFF
  16 EiB, DDS ≈ 10^31 bytes.
- **No maximum:** PNG, APNG, MNG, JNG, GIF, JPEG, J2K, JP2, AVIF, HEIF, PSD/PSB, Radiance HDR, PFM, XBM, XPM, PNM,
  G3 and PICT version 2.
- **Fixed:** Koala, 10,003 bytes. **No specification:** Photo CD.

## Limits the specification states

### TIFF

- *TIFF Revision 6.0* (Aldus/Adobe, 3 June 1992), Section 2 "TIFF Structure", p. 13: "The largest possible TIFF
  file is 2\*\*32 bytes in length." Every offset is a LONG, a "32-bit (4-byte) unsigned integer" (p. 15). So the
  maximum is 4,294,967,296 bytes.
- BigTIFF is not part of TIFF 6.0. It is a de-facto extension, described in libtiff's *BigTIFF Design* as "based on
  a proposal by Steve Carlsen of Adobe". It uses version number 43 instead of 42, and its offsets and counts are
  64-bit. It states no maximum, but its offsets cannot reach past 2^64 bytes (16 EiB). DNG 1.6 and later explicitly
  allow it (see Camera raw).
- Sources: <https://www.itu.int/itudoc/itu-t/com16/tiff-fx/docs/tiff6.pdf>,
  <https://libtiff.gitlab.io/libtiff/specification/bigtiff.html>

### WebP

- RFC 9649 *WebP Image Format* (IETF, November 2024), §2.4 "WebP File Header": "The maximum value of this field is
  2^32 minus 10 bytes, and thus the size of the whole file is at most 4 GiB minus 2 bytes." That is 4,294,967,294
  bytes. Google's *WebP Container Specification* has the same sentence.
- Pixels are capped separately. The VP8X canvas width and height are 24-bit, and "The product of Canvas Width and
  Canvas Height MUST be at most 2^32 - 1". Lossless images are at most 16384 × 16384.
- Trailing bytes are only discouraged: "The file SHOULD NOT contain any data after the data specified by File Size."
- Sources: <https://www.rfc-editor.org/rfc/rfc9649>, <https://developers.google.com/speed/webp/docs/riff_container>

### JPEG XR

- ITU-T T.832 (06/2019) | ISO/IEC 29199-2, Annex A.1, which covers the tag-based file format: "The value of
  FileSizeInBytes shall not exceed 2^32 − 1." That is 4,294,967,295 bytes.
- Microsoft's predecessor, the *HD Photo Feature Specification 1.0* (2006), §2.1, says the same: "the largest possible
  HD Photo file is 2^32–1 bytes in length. This limit will be addressed in a future update."
- The limit belongs to the .jxr container. The codestream itself has 64-bit length escapes, and JPEG XR stored in
  HEIF (T.832 Annex F) is not covered by it.
- Source: <https://www.itu.int/rec/T-REC-T.832>

### PICT

- *Inside Macintosh: Imaging With QuickDraw* (Apple, 1994), chapter 7, gives the version 1 limit twice:
  - p. 7-40: "Version 1 pictures are limited to 32 KB."
  - p. 7-5: "Version 2 and extended version 2 pictures can be much larger than the 32 KB limit imposed by the 2-byte
    picSize field."
- A PICT file is a 512-byte header followed by the picture. So a version 1 file is about 33 KB: 512 + 32,767 bytes,
  if 32 KB means the largest 2-byte picSize.
- A version 2 picture is an uncounted stream of opcodes that ends with OpEndPic. Nothing limits the number of opcodes,
  so a version 2 file has no maximum.
- Source: <https://developer.apple.com/library/archive/documentation/mac/pdf/ImagingWithQuickDraw.pdf>

## Limits set by a field width

### BMP

- Microsoft Learn, BITMAPFILEHEADER: `bfSize` is a DWORD, "The size, in bytes, of the bitmap file". A DWORD is "A
  32-bit unsigned integer", so the maximum is 4,294,967,295 bytes.
- No text lets `bfSize` be 0 or tells readers to ignore it. Other fields could point further (`bfOffBits` plus
  `biSizeImage`, or the BITMAPV5HEADER profile offset), but not inside a file whose `bfSize` is correct.
- OS/2 bitmaps have the same 32-bit field, but IBM's two references disagree on what it holds:
  - The *Multimedia Programming Reference* calls it the file size, as Windows does.
  - The *Presentation Manager Programming Reference* calls it the size of the header structure. Under that reading an
    OS/2 1.x bitmap is bounded only by its 16-bit width and height: 12,884,705,306 bytes at 24 bits per pixel. An
    OS/2 2.x bitmap is bounded by its 32-bit ones, at ≈ 48 EiB.
- Source: <https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-bitmapfileheader>

### ICO

- John Hornick, *Icons* (Microsoft, 1995):
  - Each directory entry has two DWORDs: `dwBytesInRes` ("How many bytes in this resource?") and `dwImageOffset`
    ("Where in the file is this image?").
  - `idCount` is a WORD.
  - Nothing records the size of the whole file.
- An image must start below 4 GiB and is at most 4 GiB − 1 long. So the file ends by (2^32 − 1) + (2^32 − 1) =
  8,589,934,590 bytes.
- The largest BMP-style entry (256 × 256 at 32 bits) is 270,376 bytes. Only a PNG-compressed entry, allowed since
  Windows Vista, can take a file much past 4 GiB.
- Source: <https://learn.microsoft.com/en-us/previous-versions/ms997538(v=msdn.10)>

### IFF ILBM

- *EA IFF 85* (Jerry Morrison, Electronic Arts, 1985) says: "An IFF file is just a single chunk of type FORM, LIST,
  or CAT." That chunk's size field is `LONG ckSize`, and LONG is "32 bits signed".
- An odd-length chunk is followed by a pad byte, which its parent's size counts. So a FORM's size is always even, at
  most 2^31 − 2. Adding the 8-byte chunk header gives 2,147,483,654 bytes.
- Some writers treat `ckSize` as unsigned, which allows about 4 GiB. The format's author says that is "not to the
  original specification".
- Sources: <https://wiki.amigaos.net/wiki/EA_IFF_85_Standard_for_Interchange_Format_Files>,
  <https://github.com/1fish2/IFF/blob/master/64-bit%20IFF.md>

### Sun Raster

- Sun's *rasterfile(5)*: the header is eight 32-bit C `int`s, followed by the colormap and then the image.
  - `ras_length` "contains the length in bytes of the image data".
  - `ras_maplength` holds the colormap's length.
- Both are signed, so each is at most 2^31 − 1. The file is at most 32 + 2 × (2^31 − 1) = 4,294,967,326 bytes.
- Old-style files are an exception: "files of type RT_OLD will usually have a 0 in the ras_length field". Such a file
  is bounded only by its 31-bit width and height, which gives about 4 EiB at 8 bits per pixel.
- Source: <https://docs.oracle.com/cd/E88353_01/html/E37852/rasterfile-5.html>

### Dr. Halo CUT

- No formal specification was published. The closest is Media Cybernetics' own informal *CUT File Format* note, on
  the CD-ROM that accompanies the *Encyclopedia of Graphics File Formats*.
- Width and height are 16-bit. Each scan line is a record with a 16-bit length that excludes itself. So the file is
  at most 6 + 65,535 × (2 + 65,535) = 4,294,967,301 bytes.
- Source:
  <https://archive.org/download/EncyclopediaOfGraphicsFileFormatsCompanionCd-rom/GFF_CD.ISO/FORMATS%2FDRHALO%2FSPEC%2FDR_HALO.TXT>

### OpenEXR

- *OpenEXR File Layout*: each offset-table entry is an "unsigned long" (8 bytes) giving "the distance, in bytes,
  between the start of the file and the start of the chunk". Each chunk's pixel-data size is a 32-bit int. So the
  last chunk starts below 2^64 and the file stays within about 2^64 bytes (16 EiB).
- *Technical Introduction*: "The OpenEXR file format places no fixed limit on image size, except that image width and
  height are represented by signed 32-bit integers".
- The layout document says it "does not define the OpenEXR file format". The reference library does, and it was not
  consulted.
- Source: <https://openexr.com/en/latest/OpenEXRFileLayout.html>

## Limits derived from the largest header values

### PCX

- ZSoft *PCX File Format Technical Reference Manual*, revision 5 (1991). The file has no size or offset fields. It is
  a 128-byte header, run-length-encoded scan lines and, for 256 colours, a 769-byte palette.
- Run-length encoding can at most double the data. A byte with its top two bits set must be written as a count/value
  pair: "IF the top two bits of X are 1's then count = 6 lowest bits of X".
- The maximum takes BytesPerLine at 65,534 (it "MUST be an EVEN number"), 65,536 lines and 4 planes, the most the
  manual describes. That gives 128 + 2 × 65,534 × 4 × 65,536 = 34,358,689,920 bytes.
- Two readings change that figure:
  - The manual never says whether its 16-bit "integers" are signed. Read as signed, the file tops out at
    8,589,410,432 bytes (8 GiB).
  - The decoding rule also accepts a count of 0, which outputs nothing. If that is legal the file is unbounded. The
    manual's own encoder never writes it.
- Source: <http://qzx.com/pc-gpe/pcx.txt> (transcription of ZSoft's manual)

### Truevision TGA

- *Truevision TGA File Format Specification, Version 2.0* (1989–1991). There is no size field. Width and height are
  16-bit, the ID field is at most 255 bytes, and the colour map has at most 65,535 entries.
- At 32 bits per pixel:
  - Uncompressed, a file is 17,179,345,199 bytes (≈ 16 GiB).
  - Run-length encoded, the worst case is a 1-byte packet header for every pixel: 21,474,181,424 bytes (≈ 20 GiB).
- The pixel-depth byte formally allows up to 255 bits ("other pixel depths could be used"). That would give
  141,731,692,844 bytes.
- The TGA 2.0 extension area, developer area and scan-line table are reached through 32-bit offsets and sit after the
  image data. A file that uses them must keep its header, colour map and image below 4 GiB, which caps it at
  8,589,934,616 bytes.
- Source: <https://archive.org/download/tgaffs/tgaffs.pdf>

### SGI

- *The SGI Image File Format, Version 1.00* (Paul Haeberli, Silicon Graphics). The header is 512 bytes. Width, height
  and channel count are 16-bit, with 1 or 2 bytes per channel.
- Uncompressed ("verbatim"): 512 + 65,535³ × 2 = 562,924,184,011,262 bytes (≈ 512 TiB).
- RLE: each scan line is found through a 4-byte offset in a start table, declared "long". The last line must start
  below 2^31 and is at most 262,142 bytes, so the file is at most 2,147,745,789 bytes. The document's sample code
  declares the offsets `unsigned long`; read that way, the limit is 4,295,229,437 bytes.
- Source: <https://paulbourke.net/dataformats/sgirgb/sgiversion.html>

### DDS

- Microsoft Learn, *Programming Guide for DDS*. The file is a magic number, a 124-byte header and an optional 20-byte
  DX10 header, then the surfaces back to back. There are no offsets and no file-size field. `dwPitchOrLinearSize` is
  optional, and the guide calls it unreliable.
- The header fields are wide:
  - Width, height, depth and mip count are DWORDs.
  - The DX10 array size is a UINT.
  - The widest pixel format is 16 bytes.
- The largest describable file is a cube-map array of 2^32 − 1 cubes, each face (2^32 − 1)² pixels with a full mip
  chain. That is about 1.0 × 10^31 bytes, which in practice means no limit.
- Direct3D's texture limits (16,384 pixels, 2,048 array slices in Direct3D 11) are API limits, documented separately.
- Source: <https://learn.microsoft.com/en-us/windows/win32/direct3ddds/dx-graphics-dds-pguide>

### WBMP

- The original, WAP-190 *Wireless Application Environment Specification* (WAP Forum, 2000), stores width and height
  as multi-byte integers of any length ("h and k are arbitrary integers"). Under it there is no maximum.
- Its successor, WAP-237 *WAE Defined Media Type Specification* (2001), makes them "mb_u_int32 32 bit unsigned
  integer". A type 0 image, at 1 bit per pixel with rows padded to a byte, is then at most 12 + 2^29 × (2^32 − 1) =
  2,305,843,008,676,823,052 bytes (≈ 2 EiB).
- WAP-237 also allows "at most 15 animated images following the main image". If each is as large as the main image,
  the file can reach 36,893,488,138,829,168,652 bytes (≈ 32 EiB).
- Sources: <https://stuff.mit.edu/afs/sipb/contrib/doc/specs/protocol/wap/WAP-190-WAESpec-20000329-a.pdf>,
  <https://www.openmobilealliance.org/tech/affiliates/wap/wap-237-waemt-20010515-a.pdf>

## Fixed size

### Koala Painter

- *KoalaPainter Owner's Manual for your C-64* (Koala Technologies, 1984), "Picture Format": an 8,000-byte bitmap, two
  1,000-byte colour memories and 1 background byte, loaded at $6000–$8710.
- The picture is saved as a Commodore program file, which adds a 2-byte load address. Every file is therefore 10,003
  bytes.
- Source: <https://archive.org/details/koala-painter-owners-manual-for-your-c-64-9-84>

## No maximum

### PNG and APNG

- W3C *PNG Specification (Third Edition)*, June 2025, caps each chunk and each dimension:
  - A chunk's length "shall not exceed 2^31-1 bytes".
  - Width and height are at most 2^31 − 1.
- But "There may be multiple IDAT chunks", even of zero length, and text chunks may repeat freely. So a PNG file has
  no maximum.
- APNG is part of the third edition. Its animation chunks share a 31-bit sequence number, so there are at most 2^31
  of them (at most 2^30 frames). Ordinary chunks carry no sequence number and still repeat freely, so an APNG file
  has no maximum either.
- Source: <https://www.w3.org/TR/png-3/>

### MNG and JNG

- *MNG Format Version 1.0* (PNG Development Group, 2001) uses PNG's chunk structure. A file is "a sequence of zero or
  more single frames", and a frame count of 2^31 − 1 means infinite. No maximum.
- JNG (chapter 5 of MNG 1.0) caps the image at 65,535 × 65,535: width and height are "range 0..65535". But "there
  may be multiple JDAT chunks", so the file has no maximum.
- Source: <http://www.libpng.org/pub/mng/spec/>

### GIF

- CompuServe *GIF 89a* (1990): screen and image dimensions are 16-bit. Image data goes in sub-blocks of 0 to 255
  bytes, repeated as needed, and "An unlimited number of images may be present per Data Stream." No maximum.
- Source: <https://www.w3.org/Graphics/GIF/spec-gif89a.txt>

### JPEG

- ITU-T T.81 (1992) | ISO/IEC 10918-1 caps the image at 65,535 × 65,535 pixels and each marker segment at 65,535
  bytes. Three rules still leave the file without a maximum:
  - Table and miscellaneous segments "may be present in any order and with no limit on the number of segments"
    (B.2.4).
  - "Any marker may optionally be preceded by any number of fill bytes" (B.1.1.2).
  - Entropy-coded data has no length field.
- JFIF (ITU-T T.871) adds no file limit. Exif limits only its own APP1 segment, which "shall not exceed the 64
  KBytes".
- Source: <https://www.w3.org/Graphics/JPEG/itu-t81.pdf>

### JPEG 2000: J2K and JP2

- Codestream, ITU-T T.800 (06/2019) | ISO/IEC 15444-1, Annex A. Tile-parts alone are capped at 65,535 tiles × 255
  tile-parts × (2^32 − 1) bytes (≈ 64 PiB). Two rules remove any maximum:
  - COM segments are "Repeatable as many times as desired" in the main header, which has no length.
  - The last tile-part need not give its length: "Only the last tile-part in the codestream may contain a 0 for
    Psot. If the Psot is 0, this tile-part is assumed to contain all data until the EOC marker."
- JP2 file, Annex I. A box length is 32-bit, or 64-bit (XLBox) when LBox is 1. LBox = 0 means "this box contains all
  bytes up to the end of the file", so there is no maximum.
- Source: <https://www.itu.int/rec/T-REC-T.800>

### AVIF and HEIF

- Both are ISO base media files (ISO/IEC 14496-12), and neither has a maximum:
  - A box size is 32-bit, or 64-bit ("largesize").
  - A size of 0 means "this box is the last one in the file, and its contents extend to the end of the file".
  - An item extent of length 0 likewise runs to the end of the file, and media-data boxes may repeat.
- 2^64 − 1 bytes is the cap on one explicitly sized box, not on the file.
- HEIF (ISO/IEC 23008-12) and AVIF (AOM *AV1 Image File Format* 1.2.0, October 2025) add no size limit.
- Their profile limits cap the pixels of one coded image, not the file. Examples are AVIF Baseline 8,192 × 4,352,
  AVIF Advanced 16,384 × 8,704, and the HEVC levels.
- One exception exists only in draft: HEIF's low-overhead `mini` box (brand `mif3`, Amendment 2:2026) has size fields
  that cap its payload at about 773 MiB.
- Sources: <https://aomediacodec.github.io/av1-avif/v1.2.0.html>, <https://www.iso.org/standard/89035.html>

### PSD and PSB

- The *Adobe Photoshop File Formats Specification* (November 2019) gives no file-size limit. Its limits are on the
  image:
  - 1–56 channels.
  - 1, 8, 16 or 32 bits per channel.
  - 30,000 pixels per side for PSD ("PSB max of 300,000").
- Why nothing caps the file:
  - Section lengths are 4 bytes; PSB uses 8 for the layer and mask information.
  - The final image-data section has no length field, and its ZIP compression has no size bound.
  - Without ZIP, a PSD tops out at about 214 GB.
- The familiar limit comes from Adobe's product documentation, not the specification. Photoshop Help says: "PSD
  supports files up to 2GB. For larger files, use Large Document Format (PSB)."
- Sources: <https://www.adobe.com/devnet-apps/photoshop/fileformatashtml/>,
  <https://helpx.adobe.com/photoshop/desktop/save-and-export/export-files-to-different-formats/photoshop-file-formats-overview.html>

### Radiance HDR

- Greg Ward, *Radiance File Formats* (LBNL): a text header of any length, a resolution string such as "-Y N +X M"
  with plain decimal numbers, then the scan lines. The run-length scheme is used only for scan lines of 8 to 32,767
  pixels, and other scan lines are written flat. No maximum.
- Source: <https://radsite.lbl.gov/radiance/refer/filefmts.pdf>

### PFM

- There is no official specification. Netpbm describes several PFM variants, "none of them authoritatively
  documented". The usual reference, Paul Debevec's page, gives a text header with decimal width and height, then
  4-byte floats. No maximum.
- Source: <https://www.pauldebevec.com/Research/HDR/PFM/>

### XBM, XPM and PNM

- XBM, *Xlib — C Language X Interface* §16.9: C source with decimal `#define` width and height and a byte array, with
  no ranges. The X protocol's 16-bit pixmap sizes limit servers, not files.
- XPM, *XPM Manual* 3.4i (Arnaud Le Hors, 1996): decimal width, height, colour count and characters per pixel "(so
  there is no limit on the number of colors)", plus free-form extensions.
- PNM, Netpbm's PBM, PGM and PPM pages:
  - Width and height are decimal numbers with no bound, and maxval is below 65,536.
  - A raw file is "a sequence of one or more" images.
  - In a plain file, samples are ASCII numbers "of arbitrary size", separated by any amount of white space.
- None of the three has a maximum.
- Sources: <https://www.x.org/releases/current/doc/libX11/libX11/libX11.html>, <https://www.x.org/docs/XPM/xpm.pdf>,
  <https://netpbm.sourceforge.net/doc/ppm.html>

### Raw CCITT Group 3 fax

- A .g3 file is a headerless ITU-T T.4 stream. That is a convention: T.4 defines no file. Scan lines are at most
  14,592 pixels wide, but T.30 lets a page be "Unlimited" in length, and nothing limits the number of pages. No
  maximum.
- Sources: <https://www.itu.int/rec/T-REC-T.4>, <https://www.itu.int/rec/T-REC-T.30>

### Kodak Photo CD

- Kodak never published the Image Pac format; it licensed it instead. The *Encyclopedia of Graphics File Formats*
  says "Kodak will not divulge information on the format that would enable developers to directly access the image
  data."
- Kodak's own material gives the fixed resolutions: up to 2,048 × 3,072 in the .pcd file, with 4,096 × 6,144 in
  separate Image Pac Extension files.
- It also gives a typical size: "the final Image Pac file size ranges between 4.5 and 6.5 MB". That is a typical
  range, not a maximum.
- Source: <https://web.archive.org/web/20000303141834/http://www.kodak.com/US/en/digital/dlc/book2/chapter4/imgpacp2.shtml>

## Camera raw (`FIF_RAW`)

Four of the 43 extensions have a public specification. The rest are vendor formats.

Public technical references (ExifTool, the UK National Archives' PRONOM, the Archive Team wiki) show that most of
them are TIFF files. TIFF files inherit TIFF 6.0's stated 2^32 bytes, which TIFF/EP (ISO 12234-2) repeats: "The
largest possible TIFF file is 2\*\*32 bytes in length." That inheritance is an inference from their structure, not a
vendor statement.

| Extensions | Format | Public specification | Maximum file size |
|---|---|---|---|
| dng | Adobe DNG 1.7.1 (2023) | yes | 4 GiB, stated; 2^64 bytes with the optional 64-bit (BigTIFF) layout |
| cine | Vision Research Phantom Cine (2011) | yes | version 0: "4GB", stated; version 1: image offsets are 64-bit, other pointers 32-bit |
| crw | Canon CIFF 1.0 revision 4 (1997) | yes | about 4 GiB (32-bit heap offsets and lengths) |
| x3f | Sigma/Foveon FOVb 2.x (2004) | partly; later versions undocumented | about 4 GiB (32-bit directory offsets) |
| 3fr, arw, cr2, dcr, erf, fff, iiq, k25, kdc, mef, mos, nef, nrw, pef, sr2, srf, srw | Vendor TIFF variants | no | 4 GiB (TIFF) |
| orf, raw, rw2, rwl | Olympus, Panasonic, Leica: TIFF with another magic number | no | about 4 GiB (32-bit offsets) |
| raf, mrw, ia | Fujifilm, Minolta, Sinar: own block layouts with 32-bit offsets or lengths | no | about 4 GiB |
| cs1 | Sinar CaptureShop, PSD-based | no | none (as PSD) |
| bay, bmq, cap, dc2, drf, dsc, kc2, mdc, ptx, pxn, qtk, rdc, rwz, sti | Undocumented | no | unknown |

- DNG allows the 64-bit layout (DNG Specification 1.6 and later, "64-bit Format"): "TIFF format was designed using
  32-bit values to store file offsets, which limits the maximum size of a TIFF file to 4 gigabytes. There is a
  64-bit extension of TIFF (known as BigTIFF) which does not have this file size limitation. The DNG format allows
  this same 64-bit extension." Adobe recommends the 64-bit form only "when the resulting file would be larger than 4
  gigabytes".
- Cine version 0 is stated at 4GB: "Version 0 had the array of pointers to images on 32 bits and it was limited to
  maximum 4GB file size. Current version is 1 and the format supports files bigger than 4GB." Version 1's image
  offsets are signed 64-bit, so images must start below 2^63 bytes, and "All other file pointers remained at 32
  bits."
- "About 4 GiB" rows cap only where the last block starts. That block's own 32-bit length could in principle carry
  the file towards 8 GiB, and no specification addresses this.
- ExifTool calls iiq TIFF-based, but the file starts with Phase One's own header, so its row is the least certain.
- The kdc row covers Kodak's TIFF-based EasyShare files. The older DC40/DC50/DC120 files with the same extension are
  undocumented.
- Sources:
  - DNG: <https://helpx.adobe.com/camera-raw/desktop/dng-and-file-formats/digital-negative.html>
  - TIFF/EP: <https://www.iso.org/standard/29377.html>
  - CIFF: <http://xyrion.org/ciff/CIFFspecV1R04.pdf>
  - Cine: <https://phantomhighspeed.my.salesforce-sites.com/servlet/fileField?id=0BE1N000000kD2i>
  - X3F: <https://libopenraw.freedesktop.org/formats/x3f/x3f-raw-format.pdf>
  - ExifTool: <https://exiftool.org/#supported>
