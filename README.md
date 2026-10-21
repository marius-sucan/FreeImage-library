What is FreeImage ?
-----------------------------------------------------------------------------
FreeImage is an Open Source library project for developers who would like to support popular graphics image formats like PNG, BMP, JPEG, TIFF and others as needed by today's multimedia applications.

FreeImage is easy to use, fast, multithreading safe, and cross-platform (works with Windows, Linux and Mac OS X).

Thanks to its ANSI C interface, FreeImage is usable in many languages including C, C++, VB, C#, Delphi, Java and also in common scripting languages such as Perl, Python, PHP, TCL, Lua or Ruby.

The library comes in two versions: a binary DLL distribution that can be linked against any WIN32/WIN64 C/C++ compiler and a source distribution.
Workspace files for Microsoft Visual Studio are provided, as well as makefiles for Linux, Mac OS X and other systems.

--------
This was initially a clone of https://sourceforge.net/p/freeimage/svn/ .

FreeImage is currently sporadically maintained by Hervé Drolon on SourceForge. It is licensed under the GNU General Public License, version 2.0 (GPLv2) or version 3.0 (GPLv3), and the FreeImage Public License (FIPL). More details on the project homepage: https://freeimage.sourceforge.io/ .

--------

This branch of the repository is used to compile the FreeImage.DLL used in Quick Picto Viewer. It brings the following changes:

## Changes and fixes

### New formats and animations

- added AVIF loading (FIF_AVIF=37) with the bundled libavif 1.4.2 and dav1d 1.5.4;
- added HEIC/HEIF loading (FIF_HEIF=38, extensions heic/heif/hif) with the bundled libheif 1.23.5 and libde265 1.1.3, animated HEIF image sequences included;
- added JPEG XL reading and writing (FIF_JXL=40) with the bundled libjxl 0.12.0, animations included; CMYK images with a CMYK profile are saved as CMYK;
- added APNG reading and writing (FIF_APNG=39, extensions apng/png) on top of LibPNG, animations included;
  - added the APNG_LINEAR_BLEND load flag: with APNG_PLAYBACK, translucent APNG frames blend in linear light;
- added full support for animated WebP files, reading and writing, with an example file; an animation of one frame is saved as a still image;
- added full support for MNG animations (FIF_MNG=6), reading and writing;
  - every frame loads as a page, not only the first, header-only too; MNG_PLAYBACK composites the frames as a viewer shows them;
  - frame delays, positions, clipping, hidden images, the background and the loop count are read from the MHDR, FRAM, DEFI, SHOW, BACK, TERM and LOOP chunks; BASI images and solid fills load;
  - frames inherit the file's global PLTE, tRNS and pHYs chunks and its gAMA, cHRM, sRGB, iCCP or cICP color space, as the spec orders; JNG files and frames keep their ICC profile;
  - saves write PNG frames with their delays, positions, disposal, blending, loop count, background and ICC profile, and play as the animation they come from;

### Color management and CMYK

- added color management by bundling Little CMS 2.19.1, with several new exported functions: see [Color management API](#color-management-api);
- added the FIF_LOAD_DISPLAY_ICC load flag: images load in the screen's colors, CMYK images included;
- AVIF, HEIF, PNG and JPEG XR files that state their colors with code points (nclx, cICP, PTM_COLOR_INFO) instead of an ICC profile load with a profile of those colors;
  - 8-bit JPEG XR YCC 4:4:4 files load as RGB through PTM_COLOR_INFO's matrix;
  - a PNG's cICP chunk outranks its iCCP chunk, as PNG 3 orders them;
  - narrow-range PNG and JPEG XR files load expanded to the full range;
- the PNG, APNG, AVIF, HEIF, JPEG XL and JPEG XR loaders attach the file's ITU-T H.273 code points (color primaries, transfer characteristics, matrix coefficients, full range flag) as the FIMD_CUSTOM tag "CICP" of 4 FIDT_BYTE values;
  - the RAW loader describes its 16-bit output the same way: sRGB primaries, linear transfer;
  - a color conversion that changes the pixels removes the tag;
- fixed the shadows of 8-bit grey images in color conversions, FIF_LOAD_DISPLAY_ICC included;
- fixed CMYK pages added to or changed in multi-page files being stored as RGB;
- fixed bugs related to CMYK support in TIFF, JPEG and PSD files;

### HDR and tone mapping

- added FreeImage_MustTonemap(): whether an image needs tone mapping to be displayed: FITM_NONE (0), FITM_OPTIONAL (1), FITM_REQUIRED (2), FITM_PQ (3), FITM_UNSCALED (4), or FITM_ERROR (-1) for a NULL or unknown bitmap;
  - bitmaps tagged "UnscaledBits" return FITM_UNSCALED;
  - PQ gives FITM_PQ, whose curve FreeImage_ConvertToLinear() undoes before the tone mapping;
  - 16-bit images are taken as encoded for display unless they are linear (camera RAW at 16 bits, a linear profile) or steeper than a display curve (FITM_OPTIONAL), or use no more than 12 of their 16 bits (FITM_OPTIONAL);
  - floating-point RGB bitmaps always return FITM_REQUIRED;
- added FreeImage_ConvertToLinear();
- the results of the tone mappers carry a profile of the image's colors, for color management;
- fixed the tone-mapping algorithms misbehaving with extreme values, flat images and large black areas;
- fixed Fattal02 making photos with small light sources too bright and grainy;
- fixed Fattal02 letting a few bright pixels, such as stars or lamps, darken the whole picture;
- fixed Drago03 and Reinhard05 renders jumping in brightness with the size of a small light source or a black area, thumbnails included;
- fixed FreeImage_ConvertToLinear() clipping light above white when the ICC profile's tone curves are tables;
- fixed Reinhard05 crushing the shadows of many HDR photos;

### Resizing and rotation

- FreeImage_Rescale() is faster for 8-bit greyscale, 24-bit and 32-bit images and has a lower peak memory usage; its kernels use SSE2 and AVX2 instructions when available;
- the image resizer and rotation are multi-threaded with OpenMP, which the makefiles now enable too; build with `make OPENMP=0` for a single-threaded library, see README.linux;
- added FILTER_NEAREST (-1) to FreeImage_Rescale(): nearest-neighbor resampling, the fastest filter;
- FreeImage_Rescale() can now resample FIT_INT16, FIT_UINT32, FIT_INT32, FIT_DOUBLE and FIT_COMPLEX images with every filter;
- added FreeImage_RescaleRawBits();
- fixed FreeImage_Rescale() and FreeImage_Rotate() dropping the ICC profile and returning CMYK images as RGBA;

### Large images and files

- added FreeImage_OpenMemory64(), FreeImage_AcquireMemory64(), FreeImage_SeekMemory64() and FreeImage_TellMemory64(): memory streams with UINT64 sizes and INT64 positions; the old exports are unchanged;
- added the TIFF_BIGTIFF_FORMAT (0x20000) save flag, to write BigTIFF files over 4 GB in size;
- fixed the FreeImage I/O layer's 2 GB file limit on Windows, and memory streams being limited to 2 GB buffers;
- fixed the JXR and BMP decoders and encoders on images over 1300 mgpx;
- fixed FreeImage_Copy() crashing, and FreeImage_Rescale() and FreeImage_Rotate() failing, on very large images [over 5000 mgpx]; FreeImage_Rescale() no longer screws up the colors;
- fixed TIFF saves past 4 GB returning TRUE with a file that cannot be opened, and multi-page TIFFs crossing 4 GB losing the page that crossed it;
- fixed PSD saves whose pixels pass 2 GB being written as version 1 PSD, which Photoshop reads up to 2 GB: they are written as PSB, as PSD_PSB asks;
- fixed BMP saves over 4 GB writing a broken file and returning TRUE: such a save is refused;
- fixed MNG files holding an embedded image of 4 GB or more being misread past it, and MNG frames of 4 GB or more failing to save;
- fixed JNG saves of 4 GB or more writing no image data yet returning TRUE, and JNG alpha layers of 4 GB or more being dropped on load;
- fixed multi-page bitmaps refusing a page of more than 2 GiB once encoded;

### Loading and saving

- a cut or damaged APNG, PNG, BMP, CUT, DDS, EXR, GIF, HDR, ICO, IFF, J2K, JP2, JPEG, JNG, JXR, Koala, MNG, PCD, PCX, PFM, PNM, PSD, RAS, SGI, TGA, TIFF, WBMP, WebP, XBM or XPM file loads the rows, blocks or frames decoded before the damage, the rest remains blank; a warning message, mirrored to DebugView, says what was kept;
- the TGA, XPM, PNG, ICO, J2K, JP2, BMP, PSD and TIFF writers return FALSE for image types and bit depths they do not support, instead of writing garbage; a FIT_INT16 image must now be converted before it is saved as PNG;
- fixed file saves returning TRUE when writing failed;
- fixed PNG, APNG, BMP, PSD and XPM saves failing when a FreeImageIO write_proc returns the bytes it wrote instead of the items;
- fixed loading from a stream that does not start at byte zero in the RAW, SGI, TGA, PICT, TIFF, EXR, JXR, MNG, JNG and GIF plugins, and in multi-page documents opened from a handle or memory;
- fixed TIFF files saved to memory carrying a byte of uninitialized heap memory: a write past the end of a memory stream leaves zeros, as in a file;
- fixed FreeImage_SaveMultiBitmapToMemory() writing into, and crashing on, a memory stream that wraps the caller's buffer; it returns FALSE, as FreeImage_SaveToMemory() does;

### Multi-page files

- added FreeImage_OpenMultiBitmapU(), which takes a wchar_t file name and path;
- FreeImage_AppendPage(), FreeImage_InsertPage(), FreeImage_DeletePage() and FreeImage_UnlockPage() now return TRUE or FALSE;
- FreeImage_CloseMultiBitmap() returns FALSE only when the file could not be written, when a document opened with read_only=0 was changed in a format that has no writer, such as AVIF or HEIF, or when a CMYK page would lose its alpha channel; that file is left as it was, and an unchanged document closes with TRUE;
- fixed every multi-page document being parsed twice over, once to count its pages and again to read them;
- fixed FreeImage_LockPage() getting slower the further into an animated GIF it went; reading an n-frame file from beginning to end cost O(n^2);
- fixed multi-page GIF saving with variable frame dimensions: the logical screen is enlarged to fit every frame; fixed a stack buffer overflow in the LZW encoder;
- fixed GIF frames drawn with the global color table turning grey when the first frame was deleted or moved, or when they were changed or saved on their own;
- fixed multi-page documents crashing, or refusing every later change, after the disk or the memory ran out while a page was cached;
- fixed FreeImage_LockPage() returning NULL for a J2K, JP2 or JPEG XR image locked a second time, or first locked after the document was saved;
- fixed animations opened with GIF_PLAYBACK, APNG_PLAYBACK, MNG_PLAYBACK or WEBP_PLAYBACK playing differently once edited or saved in another format;

### Security

- applied the fixes of the [Fedora F39 repository](https://src.fedoraproject.org/rpms/freeimage/tree/f39) for CVE-2020-24292, CVE-2020-24293, CVE-2020-24295, CVE-2021-33367, CVE-2021-40263, CVE-2021-40266, CVE-2023-47995 and CVE-2023-47997, which fix buffer overflows in PluginICO.cpp, PSDParser.cpp and PluginTIFF.cpp;
- fixed out-of-bounds accesses in PluginBMP, PluginPSD, PluginMNG and PluginPICT;
- fixed an integer wraparound and a segmentation fault in Exif.cpp;
- fixed out-of-bounds reads of short or crafted Exif metadata;
- fixed crashes on crafted files whose text ends up in a warning or error message, such as XPM color names and EXR channel names;

### BMP

- BMP files load and save the ICC profile a V5 header embeds; images without one are saved as before;
- added loading of BI_ALPHABITFIELDS files, at 16 and 32 bits, and of 24-bit BI_BITFIELDS files as GIMP 2.10 writes them;
- fixed BMPs with a V2, V3, V4, V5 (such as 32-bit BMPs with alpha) or OS/2 2.x header not loading;
- fixed 32-bit BMPs loading fully transparent, and 16- and 32-bit BMPs with uncommon color masks loading with wrong colors or without their alpha channel;
- fixed RLE8 BMPs failing to load, FreeImage's own BMP_SAVE_RLE saves among them, also when a run passes the row end;
- fixed 16-, 24- and 32-bit OS/2 1.x BMPs reading their pixels from the wrong offset;
- fixed BMPs saved with transparency showing opaque in Windows, web browsers and other programs;
- fixed saved BMPs stating wrong sizes in their headers, and carrying a view's parent pixels in their row padding;

### TIFF

- fixed TIFF files with both an ICC profile and Exif data crashing on load, or loading with a damaged color profile;
- fixed CMYK TIFFs with an alpha channel, and some RGB TIFFs with extra channels, failing to load;
- fixed TIFF extra channels that are not alpha, such as saved selections and spot channels, loading as transparency, and premultiplied TIFF alpha being taken for straight alpha; tiled TIFFs with extra channels now load;
- fixed tiled 8-bit greyscale and palette TIFFs with transparency loading blank, and 16-bit greyscale TIFFs with transparency (now RGBA16), half-float RGBA TIFFs (now RGBAF) and 16-bit CIELab TIFFs loading garbled;
- fixed 32-bit float greyscale TIFFs with transparency loading without their alpha channel: they load as RGBAF;
- fixed 8-bit TIFFs with transparency and RGBAF TIFFs being saved without the ExtraSamples tag that marks their alpha: other readers took it for an unspecified channel, and Pillow could not open the 8-bit ones;

### PSD

- fixed CMYK PSD files with transparency loading opaque, with FIF_LOAD_DISPLAY_ICC too: they load as RGBA;
- fixed saved selections and spot channels of RGB and Lab PSD files loading as transparency;
- fixed greyscale PSD files with transparency loading without their alpha channel: they load as RGBA, RGBA16 or RGBAF;
- fixed PSD files with several alpha or spot channels failing to load, or losing their color profile;
- fixed image resources stored after a thumbnail being misread, and CMYK and multichannel PSDs loaded as RGB losing their thumbnail;
- fixed 8-bit palette, 1-bit and float PSD saves: indexed files could not be read back, 1-bit saves overran a buffer, and float images are now saved as 32-bit greyscale;

### JPEG and JPEG 2000

- fixed JPEG files loaded with JPEG_EXIFROTATE losing their ICC profile and Exif thumbnail when rotated by 90°;
- fixed swapped width and height when loading JPEG and RAW files with FIF_LOAD_NOPIXELS;
- fixed JPEG 2000 images of 1 to 7 and 9 to 15 bits loading unscaled, a 12-bit one up to 4095 of 65535;
  - the FIMD_CUSTOM tag "SignificantBits" (FIDT_BYTE) keeps the file's precision;
  - added the J2K_UNSCALED and JP2_UNSCALED load flags, which keep the file's original values instead, tagged "UnscaledBits";

### JPEG XR

- the bundled jxrlib decodes 416 of the 463 ITU-T T.834 conformance files bit-exact with the T.835 reference decoder;
- added loading of n-channel images: 3 channels load as RGB or RGBA, 4 to 8 as CMYK;
- CMYK images are saved as CMYK, no longer as BGRA with K in the alpha channel;
- the T.835 reference decoder reads every file FreeImage saves, metadata included;
- JXR_PROGRESSIVE writes a progressive codestream, and images under 16x16 pixels save;
- fixed CMYK files, with or without alpha, and premultiplied, RGBA-ordered, RGBE and 128-bit float RGB files loading with wrong colors or not at all;
- fixed images stored with a quarter turn, cropped with planar alpha, or hard-tiled on non-square grids failing to load or loading with wrong pixels;
- fixed half-float, fixed-point, odd-width 10-bit RGB and black-is-one bilevel images, and interleaved alpha planes, loading with wrong pixels;
- many other fixes related to JPEG XR;

### Other formats

- fixed monochrome RAW files, such as those of the Leica M Monochrom and the Pentax K-3 Mark III Monochrome;
- fixed damaged PCX, TGA, PSD, PNM, RAS, IFF, WBMP, DDS and XBM files allocating gigabytes or showing uninitialized memory, and uncompressed and odd-width 16-color PCX files decoding wrongly;
- fixed DXT-compressed DDS images whose width or height is not a multiple of 4 loading cropped and garbled, or not at all;
- fixed raw PBM, PGM and PPM files being read and written one sample per callback; reading and writing them is now much faster;
- fixed PFM images being saved and loaded upside down; PFM files saved by older FreeImage versions now load flipped;
- fixed the G3 plugin hanging on a damaged fax file;

### Miscellaneous

- FreeImage_FillBackground(), FreeImage_AllocateEx() and FreeImage_EnlargeCanvas() accept the option FI_COLOR_SET_ALPHA (0x08): nothing is blended and a 32-bit image gets the color's alpha;
- FreeImage_OutputMessageProc() mirrors every message to the debugger output (Sysinternals DebugView, the Visual Studio output window) as "qpv: fim: [FORMAT] message";
- almost all of the FreeImage files are now UTF-8 encoded, no longer Latin-1 or CP1252;
- fixed Makefile.mingw not building with MinGW-w64 on Windows;

...and many other fixes.

## Bundled libraries

| Formats | Library | Bundled version | Upgraded from (r1910) |
|---|---|---|---|
| PNG | [libpng](http://www.libpng.org/pub/png/libpng.html) + zlib (v1.3.2) | 1.6.58 | 1.6.39 (zlib 1.2.13) |
| JPEG | [libjpeg (IJG)](http://ijg.org/) | 10 (January 2026) | 9d |
| TIFF | [libtiff](http://www.libtiff.org/) | 4.7.2 | 4.6.0 |
| JPEG 2000 (J2K/JP2) | [OpenJPEG](https://github.com/uclouvain/openjpeg) | 2.5.4 | 2.0.0 |
| OpenEXR (HDR) | [OpenEXR](https://openexr.com/) + [libdeflate](https://github.com/ebiggers/libdeflate) (v1.18) | 3.3.14 | 2.2.1 |
| WebP | [libwebp](https://developers.google.com/speed/webp) | 1.6.0 | 1.2.1 |
| Camera RAW | [LibRaw](https://www.libraw.org/) | 0.22.2 | 0.21.1 |
| JPEG-XR | [jxrlib](https://github.com/4creators/jxrlib) | 1.2 (FreeImage fork) | 1.2 |
| AVIF | [libavif](https://github.com/AOMediaCodec/libavif) + libdav1d (v1.5.4) | 1.4.2 | not included |
| HEIF | [libheif](https://github.com/strukturag/libheif) + libde265 (v1.1.3) | 1.23.5 | not included |
| JPEG XL | [libjxl](https://github.com/libjxl/libjxl) + highway (v1.2.0), brotli (v1.2.0) | 0.12.0 | not included |
| ICC profiles | [LittleCMS](https://github.com/mm2/Little-CMS) | 2.19.1 | not included |

## Color management API

### Functions

- FreeImage_ConvertToICCProfile() converts an image from its embedded ICC profile, or from sRGB when it has none, to any RGB, grey or CMYK profile;
- FreeImage_ApplyICCProfile() does the same in place, for showing images;
- FreeImage_ConvertToCMYK() and FreeImage_ConvertCMYKToRGB() convert between RGB and CMYK with a profile, or without one as FreeImage's loaders do;
- FreeImage_SoftProof() shows how a printer would reproduce an image;
- FreeImage_GetBuiltInICCProfile() gives sRGB, linear sRGB, grey, linear grey, Adobe RGB (1998) compatible, Display P3 and ProPhoto RGB profiles;
- FreeImage_GetICCProfileDescription() and FreeImage_GetICCProfileColorSpace() read profile details;
- FreeImage_SetDisplayICCProfile() sets the profile and rendering intent to use when an image is loaded with the FIF_LOAD_DISPLAY_ICC flag; use FreeImage_GetDisplayICCProfile() to retrieve it;
- FreeImage_ApplyDisplayICCProfile() does what FIF_LOAD_DISPLAY_ICC does, to an image loaded without it;

### Flags

- FICMS_INTENT_PERCEPTUAL (0), FICMS_INTENT_RELATIVE_COLORIMETRIC, FICMS_INTENT_SATURATION or FICMS_INTENT_ABSOLUTE_COLORIMETRIC, combined with FICMS_BLACKPOINT_COMPENSATION;
- FreeImage_SoftProof() also takes FICMS_GAMUT_CHECK and FICMS_SIMULATE_PAPER;

### How conversions work

- the conversion starts from the image's embedded profile when its colour space matches the pixels, else from the default of the image's type;
- a CMYK image without a profile is taken as the device CMYK of FreeImage's loaders;
- FreeImage_ConvertToCMYK() without a profile returns an image that is already CMYK as a copy, its profile kept;
- results keep the precision of the source (8-bit, 16-bit or float) and its alpha channel, except in CMYK, which has none;
- palettes and 16-bit 555/565 images become 24- or 32-bit, or 8-bit grey;
- FreeImage_ApplyICCProfile() keeps the pixel format: a palette changes its colours, and a CMYK image becomes RGBA with an opaque alpha; it returns FALSE for conversions that change the pixel size;

### Displaying images

- load the image with FIF_LOAD_DISPLAY_ICC, or load CMYK JPEG, TIFF, PSD, JPEG XL and JPEG XR files with JPEG_CMYK, TIFF_CMYK, PSD_CMYK, JXL_CMYK or JXR_CMYK, then call FreeImage_ApplyICCProfile() with the monitor's profile, or with NULL for sRGB;
- this must be done before FreeImage_ConvertTo*() and any call that changes the bit depth, which drop the profile;
- the FIF_LOAD_DISPLAY_ICC flag converts every loaded image to the screen's ICC profile (the primary monitor's on Windows, the X11 screen's on Linux), or to sRGB when there is none or Windows' HDR or Auto Color Management is on;
  - CMYK JPEG, TIFF, PSD, JPEG XL and JPEG XR files come back as 24- or 48-bit RGB, or 32- or 64-bit RGBA with their alpha channel;
  - grey images stay grey;
  - float images are not changed, and neither are PQ images (a CICP tag, or an ICC profile's cicp tag, saying PQ), which keep their light for the tone mappers and the CICP tag FreeImage_MustTonemap() reads;
  - a profile that is sRGB, or grey with the sRGB curve, in all but name counts as sRGB: on an sRGB screen those images are not converted;
  - the image then carries the profile it was converted to;
  - the flag is ignored with FIF_LOAD_NOPIXELS;

## Bugs or limitations identified

- AVIF, HEIF, PNG and JPEG XR files without an ICC profile whose code points say PQ, HLG, SMPTE 428 or a logarithmic curve get no profile: color management takes them as sRGB; the code points are in the CICP tag;
- saving WebP files is extremely slow at 16000 x 16000 px;
- images over 5000 mgpx saved as JXR might be malformed; only FreeImage opens them correctly; Windows Photo opens them [on Win10], but without an alpha channel; Affinity Photo 2.0 and paint.net v5.0 crash on open;
