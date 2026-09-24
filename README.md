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
- fixed jxr encoder to be able to handle images over 1300 mgpx;
- fixed bmp decoder/encoder to be able to handle images over 1300 mgpx;
- fixed behavior with extreme values of the tone-mapping algorithms; 
- fixed out of bounds accesses in PluginBMP, PluginPSD, PluginMNG and PluginPICT;
- fixed integer wrap around and segmentation fault in Exif.cpp;
- fixed FreeImage_Copy() to not crash with very large images [over 5000 mgpx];
- fixed FreeImage_Rescale() to work with very large images [over 5000 mgpx]; it no longer screws up the colors;
- fixed FreeImage_RescaleRawBits() reading the wrong rows of FIT_UINT16, FIT_RGB16, FIT_RGBA16, FIT_FLOAT, FIT_RGBF and FIT_RGBAF sources whose pitch is not a multiple of the sample size;
- fixed TIFF files saved to memory carrying a byte of uninitialised heap memory: a write past the end of a memory stream leaves zeros, as in a file; a write near a memory stream's 2 GB limit no longer overflows on Windows;
- fixed FreeImage_SaveMultiBitmapToMemory() writing into, and crashing on, a memory stream that wraps the caller's buffer; it returns FALSE, as FreeImage_SaveToMemory() does;
- fixed FreeImage_Rotate() to work with very large images [over 5000 mgpx];
- fixed a data race in the 1-bit 90/180/270 rotation;
- fixed the bundled ZLib, OpenEXR, LibJXR failing to compile on GCC 14+;
- fixed the GIF plugin: multi-page saves now enlarge the logical screen to fit every frame, and fixed a stack buffer overflow in the LZW encoder and a heap buffer overflow when loading such files with GIF_PLAYBACK;
- fixed the RAW plugin not reading a stream that does not start at byte zero;
- fixed the RAW plugin's datastream returning the wrong byte on a big-endian machine. LibRaw_freeimage_datastream::get_char() read one byte into an int and returned the int;
- fixed the G3 plugin hanging on a damaged fax file;
- fixed FreeImage_LockPage() getting slower the further into an animated GIF it went; reading an n-frame file from beginning to end cost O(n^2);
- fixed every multi-page document being parsed twice over, once to count its pages and again to read them;
- fixed Makefile.srcs / fipMakefile.srcs omitting tif_hash_set.c, which left libfreeimage.so with undefined TIFFHashSet* symbols;
- fixed loading from a stream that does not start at byte zero in the SGI, TGA, PICT, TIFF, EXR, JXR, MNG and JNG plugins and in multi-page documents opened from a handle or memory; TIFF, EXR, JXR and TGA also save correctly there;
- fixed TIFF IPTC metadata being cut to a quarter on save and read past its buffer on load; big-endian TIFFs with IPTC crashed;
- fixed CMYK PSD files loading and saving with the black channel reversed, and a bad PSD thumbnail failing or crashing the whole load;
- fixed PCX, TGA and PSD files that are short or damaged allocating gigabytes or showing uninitialised memory; uncompressed and odd-width 16-colour PCX decoding wrongly;
- fixed BMPs with a V4, V5 (such as 32-bit BMPs with alpha), V2, V3 or OS/2 2.x header not loading;
- fixed 8-bit TIFFs with transparency and RGBAF TIFFs being saved without the ExtraSamples tag that marks their alpha: other readers took it for an unspecified channel, and Pillow could not open the 8-bit ones;
- fixed CMYK JPEGs loaded as RGB keeping their CMYK ICC profile: saving them as PNG, APNG or MNG failed, and TIFF, JPEG, WebP and JPEG XR files carried a profile for the wrong colour space;
- and many other fixes

Changes:
- FreeImage_FillBackground(), FreeImage_AllocateEx() and FreeImage_EnlargeCanvas() accept the option FI_COLOR_SET_ALPHA (0x08): nothing is blended and a 32-bit image gets the colour's alpha;
- the TGA, XPM, PNG, ICO, J2K, JP2, BMP, PSD and TIFF writers return FALSE for image types and bit depths they do not declare instead of writing garbage; a FIT_INT16 image must now be converted before it is saved as PNG;
- FreeImage_Rescale(), FreeImage_RescaleRect(), FreeImage_RescaleRawBits() and FreeImage_MakeThumbnail() resample FIT_INT16, FIT_UINT32, FIT_INT32, FIT_DOUBLE and FIT_COMPLEX images with every filter, instead of a blank image, TRUE with nothing written or no thumbnail; integer samples round half away from zero and saturate at their type's limits, complex images are filtered per real and imaginary part, and a thumbnail made with convert set is an 8-bit image, as for FIT_FLOAT;
- a cut or damaged APNG, PNG, BMP, CUT, DDS, EXR, GIF, HDR, ICO, IFF, J2K, JP2, JPEG, JNG, JXR, Koala, MNG, PCD, PCX, PFM, PNM, PSD, RAS, SGI, TGA, TIFF, WBMP, WebP, XBM or XPM file loads the rows, blocks or frames decoded before the damage, the rest remains blank; a warning message, mirrored to DebugView, says what was kept;
- FreeImage_Rescale(), FreeImage_RescaleRect(), FreeImage_RescaleRawBits() and FreeImage_MakeThumbnail() are faster, with the same output: the vertical pass reads rows instead of columns for 8-bit greyscale, 24-bit and 32-bit images and every other image type, and a resize in both directions keeps a band of about 4 MB between its two passes instead of a temporary image of the full size, which lowers the peak memory by that image's size;
- FreeImage_Rescale(), FreeImage_RescaleRect(), FreeImage_RescaleRawBits() and FreeImage_MakeThumbnail() still filter horizontally first unless the width grows, but take the other order when its taps are clearly cheaper, by 10%, or 2x for FIT_RGBAF and FIT_COMPLEX images: enlargements beyond about 1.35x, and of palette, 1-, 4- and 16-bit images from 1.1x, now filter horizontally first, and reductions below about 0.75x of other images vertically first, about 1.2x faster on average and up to 2x. Where the order changes, the image kept between the passes rounds differently, so 13-31% of the samples differ by one step, two at most, floating-point ones in their last bit, with the same error against an exact resize; FreeImage_TmoFattal02() changes with it, as it resizes internally;
- multi-threaded image resizer and rotation using OpenMP pragma; the makefiles now enable OpenMP too. Build it with `make OPENMP=0` for a single-threaded library; see README.linux;
- FreeImage_CloseMultiBitmap() returns FALSE when a document opened with read_only=0 was changed in a format that has no writer, such as AVIF or HEIF, and leaves the file as it was; an unchanged document closes with TRUE;
- FreeImage_OutputMessageProc() mirrors every message to the debugger output (Sysinternals DebugView, the Visual Studio output window) as "qpv: fim: [FORMAT] message";
- added FreeImage_OpenMultiBitmapU(), which takes a wchar_t for the file name and path;
- added FreeImage_AppendPageEx(), FreeImage_InsertPageEx(), FreeImage_RemovePageEx(), which return TRUE or FALSE;
- added FreeImage_RescaleRawBits();
- added FILTER_NEAREST (-1) to FreeImage_Rescale(), FreeImage_RescaleRect() and FreeImage_RescaleRawBits(): nearest-neighbour resampling, the fastest filter; each pixel is copied from the source pixel under its centre and the image keeps its type, bit depth, palette and transparency, so FreeImage_RescaleRawBits() takes every bit depth with it; with FI_RESCALE_TRUE_COLOR an image of 8 bits or less comes out as 24-bit, or 32-bit when transparent;
- added full support for animated WebP files and example file; save WebP animations implemented as well;
- added AVIF loading (FIF_AVIF=37) with the bundled libavif 1.4.2 and dav1d 1.5.4;
- added HEIC/HEIF loading (FIF_HEIF=38, extensions heic/heif/hif) with the bundled libheif 1.23.4 and libde265 1.1.3; 
- added animated HEIC/HEIF reading (HEIF image sequences);
- added APNG reading and writing (FIF_APNG=39, extensions apng/png) on top of LibPNG; save APNG animations implemented as well;
- added full support for MNG animations (FIF_MNG=6), reader and write;
- updated LibRaw library to version 0.22.2, from 0.21.1;
- updated LibJPEG library to version 10, from the 9d of January 2020;
- updated LibTIFF library to version 4.7.2, from 4.6.0 release of September 2023;
- updated ZLib library to version 1.3.2, from the 1.2.13 of October 2022;
- updated OpenEXR library to version 3.3.14, from version 2.2.0. OpenEXR no longer uses ZLib for EXR data since 3.2. ZIP and DWA compression modes use libdeflate;
- updated OpenJPEG library to version 2.5.4, from a March 2014 trunk snapshot labelled 2.0.0;
- almost all of the FreeImage files are now UTF-8 encoded, no longer Latin-1 or CP1252;

Bugs or limitations identified:
- saving WEBP files is extremely slow at 16000 x 16000 px;
- images over 5000 mgpx saved as JXR might be malformed; only Freeimage opens them correctly; Windows Photo opens them [on Win10], but without an alpha channel; Affinity Photo 2.0 and paint.net v5.0 crash on open;
