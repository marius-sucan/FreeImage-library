What is FreeImage ?
-----------------------------------------------------------------------------
FreeImage is an Open Source library project for developers who would like to support popular graphics image formats like PNG, BMP, JPEG, TIFF and others as needed by today's multimedia applications.

FreeImage is easy to use, fast, multithreading safe, and cross-platform (works with Windows, Linux and Mac OS X).

Thanks to it's ANSI C interface, FreeImage is usable in many languages including C, C++, VB, C#, Delphi, Java and also in common scripting languages such as Perl, Python, PHP, TCL, Lua or Ruby.

The library comes in two versions: a binary DLL distribution that can be linked against any WIN32/WIN64 C/C++ compiler and a source distribution.
Workspace files for Microsoft Visual Studio provided, as well as makefiles for Linux, Mac OS X and other systems.

--------
This was initially a clone of https://sourceforge.net/p/freeimage/svn/ .

FreeImage is currently sporadically maintained by Hervé Drolon on SourceForge. It is licensed under the GNU General Public License, version 2.0 (GPLv2) or version 3.0 (GPLv3), and the FreeImage Public License (FIPL). More details on the project homepage: https://freeimage.sourceforge.io/ .

--------

This branch of the repository is used to compile the FreeImage.DLL used in Quick Picto Viewer. It brings the following changes:

Fixes:

- applied fixes found in the Fedora F39 repository for: CVE-2020-24292, CVE-2020-24293, CVE-2020-24295, CVE-2021-33367, CVE-2021-40263, CVE-2021-40266, CVE-2023-47995 and CVE-2023-47997 found at: https://src.fedoraproject.org/rpms/freeimage/tree/f39 ;
- fixed buffer overflows in PluginICO.cpp, PSDParser.cpp, PluginTIFF.cpp (with the aforementioned patches)
- fixed JXR encoder to be able to handle images over 1300 mgpx;
- fixed BMP decoder/encoder to be able to handle images over 1300 mgpx;
- fixed behavior with extreme values of the tone-mapping algorithms; 
- fixed out of bounds accesses in PluginBMP, PluginPSD, PluginMNG and PluginPICT;
- fixed integer wrap around and segmentation fault in Exif.cpp;
- fixed FreeImage_Copy() to not crash with very large images [over 5000 mgpx];
- fixed FreeImage_Rescale() to work with very large images [over 5000 mgpx]; it no longer screws up the colors;
- fixed FreeImage_RescaleRawBits() reading the wrong rows of FIT_UINT16, FIT_RGB16, FIT_RGBA16, FIT_FLOAT, FIT_RGBF and FIT_RGBAF sources whose pitch is not a multiple of the sample size;
- fixed TIFF files saved to memory carrying a byte of uninitialised heap memory: a write past the end of a memory stream leaves zeros, as in a file; the stream's growth is computed in 64 bits;
- fixed FreeImage_SaveMultiBitmapToMemory() writing into, and crashing on, a memory stream that wraps the caller's buffer; it returns FALSE, as FreeImage_SaveToMemory() does;
- fixed FreeImage_Rotate() to work with very large images [over 5000 mgpx];
- fixed multi-page GIF saving with variable frame dimensions; the logical screen is enlarged to fit every frame, and fixed a stack buffer overflow in the LZW encoder;
- fixed the G3 plugin hanging on a damaged fax file;
- fixed FreeImage_LockPage() getting slower the further into an animated GIF it went; reading an n-frame file from beginning to end cost O(n^2);
- fixed every multi-page document being parsed twice over, once to count its pages and again to read them;
- fixed CMYK pages added to or changed in multi-page files being stored as RGB;
- fixed Makefile.mingw not building with MinGW-w64 on Windows;
- fixed loading from a stream that does not start at byte zero in the RAW, SGI, TGA, PICT, TIFF, EXR, JXR, MNG, JNG and GIF plugins and in multi-page documents opened from a handle or memory; TIFF, EXR, JXR, TGA and ICO also save correctly there;
- fixed PCX, TGA and PSD files that are damaged allocating gigabytes or showing uninitialised memory; uncompressed and odd-width 16-colour PCX decoding wrongly;
- fixed BMPs with a V4, V5 (such as 32-bit BMPs with alpha), V2, V3 or OS/2 2.x header not loading;
- fixed 8-bit TIFFs with transparency and RGBAF TIFFs being saved without the ExtraSamples tag that marks their alpha: other readers took it for an unspecified channel, and Pillow could not open the 8-bit ones;
- fixed bugs related to CMYK support in TIFF and JPG files;
- fixed JPEG 2000 images of 1 to 7 and 9 to 15 bits loading unscaled, a 12-bit one up to 4095 of 65535; the FIMD_CUSTOM tag "SignificantBits" (FIDT_BYTE) keeps the file's precision. Added flags: J2K_UNSCALED and JP2_UNSCALED to keep the file's original values instead, tagged "UnscaledBits";
- fixed JPEG 2000 files loaded from a handle that cannot seek to its end aborting the program;
- fixed TIFF files with both an ICC profile and Exif data crashing on load, or loading with a damaged profile;
- fixed monochrome RAW files, such as those of the Leica M Monochrom and the Pentax K-3 Mark III Monochrome;
- fixed Leaf and Mamiya MOS RAW files misreading their white balance and colour matrix;
- fixed swapped width and height dimensions when loading JPEG and RAW files using FIF_LOAD_NOPIXELS;
- fixed JPEG files loaded with JPEG_EXIFROTATE losing their ICC profile and Exif thumbnail when rotated by 90°;
- fixed JPEG XR files that are damaged or hold a tag of an unexpected type crashing, aborting or leaking memory while loading;
- fixed JPEG XR images stored rotated by 90 degrees failing to load;
- fixed the FreeImage I/O layer's 2 GB file limit on Windows;
- fixed memory streams being limited to 2 GB buffers;
- fixed file saves returning TRUE when writting failed;
- fixed TIFF saves past 4 GB returning TRUE with a file that cannot be opened, and multi-page TIFFs crossing 4 GB losing the page that crossed it;
- fixed multi-page bitmaps refusing a page of more than 2 GiB once encoded;
- fixed PSD saves whose pixels pass 2 GB being written as version 1 PSD, which Photoshop reads up to 2 GB: they are written as PSB, as PSD_PSB asks;
- fixed raw PBM, PGM and PPM files being read and written one sample per callback; reading and writing such files is now much faster;
- fixed camera RAW files being read one byte per callback; they now load faster;
- and many other fixes

Changes:
- added FreeImage_OpenMemory64(), FreeImage_AcquireMemory64(), FreeImage_SeekMemory64() and FreeImage_TellMemory64(): memory streams with UINT64 sizes and INT64 positions; the old exports are unchanged;
- the TIFF writer takes the flag TIFF_BIGTIFF_FORMAT (0x20000) to write BigTIFF files, over 4 GB in size;
- FreeImage_FillBackground(), FreeImage_AllocateEx() and FreeImage_EnlargeCanvas() accept the option FI_COLOR_SET_ALPHA (0x08): nothing is blended and a 32-bit image gets the colour's alpha;
- the TGA, XPM, PNG, ICO, J2K, JP2, BMP, PSD and TIFF writers return FALSE for image types and bit depths they do not declare instead of writing garbage; a FIT_INT16 image must now be converted before it is saved as PNG;
- FreeImage_Rescale() can now resample FIT_INT16, FIT_UINT32, FIT_INT32, FIT_DOUBLE and FIT_COMPLEX images with every filter;
- FreeImage_Rescale() is now faster for 8-bit greyscale, 24-bit and 32-bit images and has a lower peak memory usage, the kernels utilize SSE2 and AVX2 instructions when available;
- multi-threaded image resizer and rotation using OpenMP pragma; the makefiles now enable OpenMP too. Build it with `make OPENMP=0` for a single-threaded library; see README.linux;
- added FILTER_NEAREST (-1) to FreeImage_Rescale(): nearest-neighbour resampling, the fastest filter;
- a cut or damaged APNG, PNG, BMP, CUT, DDS, EXR, GIF, HDR, ICO, IFF, J2K, JP2, JPEG, JNG, JXR, Koala, MNG, PCD, PCX, PFM, PNM, PSD, RAS, SGI, TGA, TIFF, WBMP, WebP, XBM or XPM file loads the rows, blocks or frames decoded before the damage, the rest remains blank; a warning message, mirrored to DebugView, says what was kept;
- FreeImage_AppendPage(), FreeImage_InsertPage(), FreeImage_DeletePage() and FreeImage_UnlockPage() now return TRUE or FALSE; FreeImage_CloseMultiBitmap() returns FALSE only when the file could not be written;
- FreeImage_CloseMultiBitmap() returns FALSE when a document opened with read_only=0 was changed in a format that has no writer, such as AVIF or HEIF, and leaves the file as it was; an unchanged document closes with TRUE;
- added FreeImage_OpenMultiBitmapU(), which takes a wchar_t for the file name and path;
- FreeImage_OutputMessageProc() mirrors every message to the debugger output (Sysinternals DebugView, the Visual Studio output window) as "qpv: fim: [FORMAT] message";
- added FreeImage_RescaleRawBits();
- added full support for animated WebP files and example file; save WebP animations implemented as well;
- added AVIF loading (FIF_AVIF=37) with the bundled libavif 1.4.2 and dav1d 1.5.4;
- added HEIC/HEIF loading (FIF_HEIF=38, extensions heic/heif/hif) with the bundled libheif 1.23.5 and libde265 1.1.3; 
- added animated HEIC/HEIF reading (HEIF image sequences);
- added APNG reading and writing (FIF_APNG=39, extensions apng/png) on top of LibPNG; save APNG animations implemented as well;
- added full support for MNG animations (FIF_MNG=6), reader and write;
- added color management by bundling Little CMS 2.19.1: several new exported functions are available;
- added FIF_LOAD_DISPLAY_ICC load flag: images load in the screen's colors, CMYK images included;
- added APNG_LINEAR_BLEND load flag: with APNG_PLAYBACK, translucent APNG frames blend in linear light;
- the PNG, APNG, AVIF and HEIF loaders attach the color description of the file, its ITU-T H.273 code points (color primaries, transfer characteristics, matrix coefficients, full range flag), as the FIMD_CUSTOM tag "CICP" of 4 FIDT_BYTE values; the RAW loader describes its 16-bit output the same way: sRGB primaries, linear transfer; a color conversion that changes the pixels removes the tag;
- added FreeImage_MustTonemap(): whether an image needs tone mapping to be displayed, FITM_NONE (0), FITM_OPTIONAL (1), FITM_REQUIRED (2), FITM_PQ (3), FITM_UNSCALED (4), or FITM_ERROR (-1) for a NULL or unknown bitmap. Bitmaps tagged "UnscaledBits" return FITM_UNSCALED; PQ gives FITM_PQ, whose curve FreeImage_ConvertToLinear() undoes before the tone mapping; 16-bit images are taken as encoded for display unless they are linear (camera RAW at 16 bits, a linear profile) or steeper than a display curve (FITM_OPTIONAL), or use no more than 12 of their 16 bits (FITM_OPTIONAL); Floating-point RGB bitmaps always return FITM_REQUIRED;
- added FreeImage_ConvertToLinear();
- almost all of the FreeImage files are now UTF-8 encoded, no longer Latin-1 or CP1252;

| Formats | Library | Bundled version | Upgraded from (r1910) |
|---|---|---|---|
| PNG | [libpng](http://www.libpng.org/pub/png/libpng.html) + zlib (v1.3.2) | 1.6.58 (zlib 1.3.2) | 1.6.39 (zlib 1.2.13) |
| JPEG | [libjpeg (IJG)](http://ijg.org/) | 10 (January 2026) | 9d |
| TIFF | [libtiff](http://www.libtiff.org/) | 4.7.2 | 4.6.0 |
| JPEG 2000 (J2K/JP2) | [OpenJPEG](https://github.com/uclouvain/openjpeg) | 2.5.4 | 2.0.0 |
| OpenEXR (HDR) | [OpenEXR](https://openexr.com/) + [libdeflate](https://github.com/ebiggers/libdeflate) (v1.18) | 3.3.14 | 2.2.1 |
| WebP | [libwebp](https://developers.google.com/speed/webp) | 1.6.0 | 1.2.1 |
| Camera RAW | [LibRaw](https://www.libraw.org/) | 0.22.2 | 0.21.1 |
| JPEG-XR | [jxrlib](https://github.com/4creators/jxrlib) | 1.2 (FreeImage fork) | 1.2 |
| AVIF | [libavif](https://github.com/AOMediaCodec/libavif) + libdav1d (v1.5.4) | 1.4.2 | not included |
| HEIF | [libheif](https://github.com/strukturag/libheif) + libde265 (v1.1.3) | 1.23.5 | not included |
| ICC profiles | [LittleCMS](https://github.com/mm2/Little-CMS) | 2.19.1 | not included |

Color management:
- FreeImage_ConvertToICCProfile() converts an image from its embedded ICC profile, or from sRGB when it has none, to any RGB, grey or CMYK profile; FreeImage_ApplyICCProfile() does it in place, for showing images;
- FreeImage_ConvertToCMYK() and FreeImage_ConvertCMYKToRGB() convert between RGB and CMYK with a profile, or without one as FreeImage's loaders do;
- FreeImage_SoftProof() shows how a printer would reproduce an image;
- FreeImage_GetBuiltInICCProfile() gives sRGB, linear sRGB, grey, linear grey, Adobe RGB (1998) compatible, Display P3 and ProPhoto RGB profiles;
- FreeImage_GetICCProfileDescription() and FreeImage_GetICCProfileColorSpace() read profile details;
- the conversion starts from the image's embedded profile when its colour space matches the pixels, else from the default of the image's type; a CMYK image without a profile is taken as the device CMYK of FreeImage's loaders. FreeImage_ConvertToCMYK() without a profile returns an image that is already CMYK as a copy, its profile kept;
- flags: FICMS_INTENT_PERCEPTUAL (0), FICMS_INTENT_RELATIVE_COLORIMETRIC, FICMS_INTENT_SATURATION or FICMS_INTENT_ABSOLUTE_COLORIMETRIC, combined with FICMS_BLACKPOINT_COMPENSATION; FreeImage_SoftProof() also takes FICMS_GAMUT_CHECK and FICMS_SIMULATE_PAPER;
- results keep the precision of the source (8-bit, 16-bit or float) and its alpha channel, except in CMYK, which has none; palettes and 16-bit 555/565 images become 24- or 32-bit, or 8-bit grey. FreeImage_ApplyICCProfile() keeps the pixel format: a palette changes its colours, and a CMYK image becomes RGBA with an opaque alpha; it returns FALSE for conversions that change the pixel size;
- to display an image: load it with FIF_LOAD_DISPLAY_ICC, or load CMYK JPEG, TIFF and PSD files with JPEG_CMYK, TIFF_CMYK or PSD_CMYK, then call FreeImage_ApplyICCProfile() with the monitor's profile, or with NULL for sRGB. This must be done before FreeImage_ConvertTo*() and FreeImage_Rescale(), which drop the profile;
- the FIF_LOAD_DISPLAY_ICC flag converts every loaded image to the screen's ICC profile (the primary monitor's on Windows, the X11 screen's on Linux), or to sRGB when there is none or Windows' HDR or Auto Color Management is on. CMYK JPEG, TIFF and PSD files come back as 24- or 48-bit RGB; grey images stay grey; float images are not changed. A profile that is sRGB, or grey with the sRGB curve, in all but name counts as sRGB: on an sRGB screen those images are not converted. The image then carries the profile it was converted to. The flag is ignored with FIF_LOAD_NOPIXELS;
- FreeImage_SetDisplayICCProfile() sets the profile and rendering intent to use the image is loaded with the FIF_LOAD_DISPLAY_ICC flag; use FreeImage_GetDisplayICCProfile() to retrieve it;

Bugs or limitations identified:
- AVIF and HEIF images that describe their colours with CICP (nclx) instead of an ICC profile get no profile attached: color management takes them as sRGB; the code points are in the CICP tag;
- saving WEBP files is extremely slow at 16000 x 16000 px;
- images over 5000 mgpx saved as JXR might be malformed; only Freeimage opens them correctly; Windows Photo opens them [on Win10], but without an alpha channel; Affinity Photo 2.0 and paint.net v5.0 crash on open;
