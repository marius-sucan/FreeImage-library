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
- fixed TIFF files saved to memory carrying a byte of uninitialised heap memory: a write past the end of a memory stream leaves zeros, as in a file; the stream's growth is computed in 64 bits;
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
- fixed Makefile.mingw not building with MinGW-w64 on Windows: LibJXR's guiddef.h and _byteswap_ulong clashed with MinGW-w64's headers, the object list is longer than a Windows command line, and cmd.exe has no uname; see README.minGW;
- fixed loading from a stream that does not start at byte zero in the SGI, TGA, PICT, TIFF, EXR, JXR, MNG, JNG and GIF plugins and in multi-page documents opened from a handle or memory; TIFF, EXR, JXR, TGA and ICO also save correctly there;
- fixed TIFF IPTC metadata being cut to a quarter on save and read past its buffer on load; big-endian TIFFs with IPTC crashed;
- fixed CMYK PSD files loading and saving with the black channel reversed, and a bad PSD thumbnail failing or crashing the whole load;
- fixed PCX, TGA and PSD files that are short or damaged allocating gigabytes or showing uninitialised memory; uncompressed and odd-width 16-colour PCX decoding wrongly;
- fixed BMPs with a V4, V5 (such as 32-bit BMPs with alpha), V2, V3 or OS/2 2.x header not loading;
- fixed 8-bit TIFFs with transparency and RGBAF TIFFs being saved without the ExtraSamples tag that marks their alpha: other readers took it for an unspecified channel, and Pillow could not open the 8-bit ones;
- fixed CMYK JPEGs loaded as RGB keeping their CMYK ICC profile: saving them as PNG, APNG or MNG failed, and TIFF, JPEG, WebP and JPEG XR files carried a profile for the wrong colour space;
- fixed tiled CMYK TIFF files: loaded with TIFF_CMYK their cyan and yellow came back swapped, and loaded without it their CMYK samples came back as a 32-bit RGBA image carrying the CMYK profile; they now load as striped files do;
- fixed PNG files with both an ICC profile and a gAMA chunk being gamma-corrected on load, which left them with a profile that no longer described their pixels; the profile wins over gAMA, as the PNG specification asks;
- fixed header-only loads (FIF_LOAD_NOPIXELS) of CMYK TIFF files without TIFF_CMYK describing the CMYK data: they reported 32 or 64 bits and the CMYK profile of the file, while a load returns 24- or 48-bit RGB without it, and a float CMYK file loaded header-only although it cannot be loaded;
- fixed header-only loads of PSD files: CMYK and multichannel files described the CMYK data instead of the RGB image a load without PSD_CMYK returns, a multichannel file loaded with PSD_CMYK missed its fourth channel and CMYK flag, and indexed and bitmap files got a grey palette; CMYK and multichannel files loaded as RGB also lost their thumbnail;
- fixed header-only loads of RAW files describing the sensor instead of the image a load returns: a photo taken with the camera turned reported its width and height swapped, RAW_HALFSIZE the full size, and RAW_DISPLAY, RAW_PREVIEW and RAW_UNPROCESSED a 48-bit image of the processed size without their metadata; each flag now reports the size, type and metadata of its load;
- fixed RAW previews stored as bitmaps, such as the uncompressed ones of Leica M8, M9 and M Monochrom DNGs, coming out unturned: RAW_PREVIEW turned a JPEG preview with the image but returned a bitmap preview of a turned camera sideways; a greyscale bitmap preview was read as RGB, past the end of its buffer, and now loads as 8-bit greyscale;
- fixed header-only loads of JPEG files with JPEG_EXIFROTATE reporting the size before the rotation, so Exif Orientation 5 to 8 swapped the width and height against the load; an Orientation tag shorter than two bytes, which the rotation read past its end, is ignored;
- fixed JPEG files loaded with JPEG_EXIFROTATE losing their ICC profile and Exif thumbnail when turned (Orientation 3 and 5 to 8), and a CMYK file so loaded with JPEG_CMYK its CMYK flag, which made it read as RGBA; the thumbnail is kept as the file stores it;
- fixed heap overflows loading TIFF files FreeImage stores in pixels of another size than the file's (2-sample 8-bit, float grey with alpha, 16-bit colour with extra samples): tiled ones wrote past the image and are now refused; striped ones read past their strip or, when planar, wrote past the image, and now keep the samples that fit;
- fixed a heap over-read loading TGA files whose thumbnail claims more pixels than the file holds, and TGA files whose pixels end past 4 GB being saved with their thumbnail and footer written over the pixels: the footer's offsets are 32-bit, so the thumbnail is left out;
- fixed the I/O layer's 2 GB file limit on Windows: FreeImageIO's seek_proc and tell_proc carry INT64 positions (long is 32-bit there), FreeImage_Load(), FreeImage_Save() and the multi-page cache seek in 64 bits, every plugin and the TIFF, OpenEXR, JPEG XR, JPEG 2000, LibRaw, HEIF, AVIF and WebP glue keep 64-bit positions, and reads or writes of more than 4 GB go through the callbacks in pieces; the G3 plugin also reads a stream that does not start at byte zero;
- fixed memory streams: FreeImage_OpenMemory() with a buffer of 2 GB or more read nothing, and a stream FreeImage grows stopped at 2 GB; both hold what memory allows, FreeImage_AcquireMemory() returns FALSE past 4 GB (FreeImage_ReadMemory() still reads it) and FreeImage_TellMemory() returns -1 past what a long holds, where their 64-bit variants go on;
- fixed sizes that 32-bit builds cut short: a WebP in a stream of 4 GB - 1 bytes overflowed its buffer, an RLE TGA with over 4 GB after its pixels decoded garbage (black on 64-bit builds), G3 and MNG streams past 4 GB were read in part, and a wrapped memory buffer over 2 GB could not seek past 2 GB; FreeImage_LoadMultiBitmapFromMemory() on Windows took a stream position past 2 GB for 0; on 32-bit Linux, FreeImage_Load(), FreeImage_Save(), the multi-page functions and the JPEG transforms open files over 2 GB;
- and many other fixes

Changes:
- added FreeImage_OpenMemory64(), FreeImage_AcquireMemory64(), FreeImage_SeekMemory64() and FreeImage_TellMemory64(): memory streams with UINT64 sizes and INT64 positions, where FreeImage_OpenMemory() and FreeImage_AcquireMemory() stop at 4 GB and, on Windows, FreeImage_SeekMemory() and FreeImage_TellMemory() at 2 GB; the old exports are unchanged;
- FreeImageIO's seek_proc takes an INT64 offset and tell_proc returns INT64; a FreeImageIO built with long callbacks must be recompiled on Windows, where long is 32-bit (the structure's layout and every export are unchanged);
- FreeImage_FillBackground(), FreeImage_AllocateEx() and FreeImage_EnlargeCanvas() accept the option FI_COLOR_SET_ALPHA (0x08): nothing is blended and a 32-bit image gets the colour's alpha;
- the TGA, XPM, PNG, ICO, J2K, JP2, BMP, PSD and TIFF writers return FALSE for image types and bit depths they do not declare instead of writing garbage; a FIT_INT16 image must now be converted before it is saved as PNG;
- FreeImage_Rescale(), FreeImage_RescaleRect(), FreeImage_RescaleRawBits() and FreeImage_MakeThumbnail() resample FIT_INT16, FIT_UINT32, FIT_INT32, FIT_DOUBLE and FIT_COMPLEX images with every filter, instead of a blank image, TRUE with nothing written or no thumbnail; integer samples round half away from zero and saturate at their type's limits, complex images are filtered per real and imaginary part, and a thumbnail made with convert set is an 8-bit image, as for FIT_FLOAT;
- a cut or damaged APNG, PNG, BMP, CUT, DDS, EXR, GIF, HDR, ICO, IFF, J2K, JP2, JPEG, JNG, JXR, Koala, MNG, PCD, PCX, PFM, PNM, PSD, RAS, SGI, TGA, TIFF, WBMP, WebP, XBM or XPM file loads the rows, blocks or frames decoded before the damage, the rest remains blank; a warning message, mirrored to DebugView, says what was kept;
- FreeImage_Rescale(), FreeImage_RescaleRect(), FreeImage_RescaleRawBits() and FreeImage_MakeThumbnail() are faster, with the same output: the vertical pass reads rows instead of columns for 8-bit greyscale, 24-bit and 32-bit images and every other image type, and a resize in both directions keeps a band of about 4 MB between its two passes instead of a temporary image of the full size, which lowers the peak memory by that image's size;
- FreeImage_Rescale(), FreeImage_RescaleRect(), FreeImage_RescaleRawBits() and FreeImage_MakeThumbnail() still filter horizontally first unless the width grows, but take the other order when its taps are clearly cheaper, by 10%, or 2x for FIT_RGBAF and FIT_COMPLEX images: enlargements beyond about 1.35x, and of palette, 1-, 4- and 16-bit images from 1.1x, now filter horizontally first, and reductions below about 0.75x of other images vertically first, about 1.2x faster on average and up to 2x. Where the order changes, the image kept between the passes rounds differently, so 13-31% of the samples differ by one step, two at most, floating-point ones in their last bit, with the same error against an exact resize; FreeImage_TmoFattal02() changes with it, as it resizes internally;
- FreeImage_Rescale(), FreeImage_RescaleRect(), FreeImage_RescaleRawBits() and FreeImage_MakeThumbnail() filter 8-bit greyscale, 24-bit and 32-bit images horizontally with SSE2 on x86 and x64, 16, 4 or 4 rows at a time, converting every source pixel once, for filter windows of any width (up to a 4 MB buffer per thread), with the same output;
- FreeImage_Rescale(), FreeImage_RescaleRect(), FreeImage_RescaleRawBits() and FreeImage_MakeThumbnail() filter 8-bit greyscale, 24-bit and 32-bit images vertically with SSE2 on x86 and x64, with any compiler, 16 samples in registers at a time, converting each source row once when it feeds several destination rows, with the same output;
- 8-bit greyscale, 24-bit and 32-bit images take instead the pass order these SSE2 kernels make cheaper, counting a horizontal tap as 0.8 of a vertical one and each pixel the horizontal pass writes as 4 taps more, with no margin: fitted on GCC and MSVC builds, it is 0.4% from the faster order on average, against 7% for the rule above, and strong reductions with wide filters, or of very wide images, filter horizontally first again. With the two kernels above these resizes take 0.35-0.83x the time of the previous code with MSVC and 0.54-0.97x with GCC; a 129300 x 86102 to 1820 x 1059 b-spline reduction about 0.4x with MSVC and 0.65x with GCC. Where the order changes, the image kept between the passes rounds differently, as above;
- on x64 processors with AVX2 these kernels run with sums of four doubles, chosen at run time, and filter two destination pixels or rows at a time from the same converted source pixels, with the same output and the same pass order as without AVX2: 0.85-0.87x the time on average, 0.73x on reductions of 129300-pixel-wide images to 1820 pixels;
- multi-threaded image resizer and rotation using OpenMP pragma; the makefiles now enable OpenMP too. Build it with `make OPENMP=0` for a single-threaded library; see README.linux;
- the Visual Studio project compiles its Release configurations with /QIntel-jcc-erratum: on Intel Core 6th to 10th generation processors, the same code no longer runs up to a third slower depending on where the linker places a loop;
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
- added color management with the bundled Little CMS 2.19.1: FreeImage_ConvertToICCProfile() converts an image from its embedded ICC profile, or from sRGB when it has none, to any RGB, grey or CMYK profile; FreeImage_ApplyICCProfile() does it in place, for showing images; FreeImage_ConvertToCMYK() and FreeImage_ConvertCMYKToRGB() convert between RGB and CMYK with a profile, or without one as FreeImage's loaders do; FreeImage_SoftProof() shows how a printer would reproduce an image; FreeImage_GetBuiltInICCProfile() gives sRGB, linear sRGB, grey, linear grey, Adobe RGB (1998) compatible, Display P3 and ProPhoto RGB profiles; FreeImage_GetICCProfileDescription() and FreeImage_GetICCProfileColorSpace() read a profile; see Color management below;
- updated LibRaw library to version 0.22.2, from 0.21.1;
- updated LibJPEG library to version 10, from the 9d of January 2020;
- updated LibTIFF library to version 4.7.2, from 4.6.0 release of September 2023;
- updated ZLib library to version 1.3.2, from the 1.2.13 of October 2022;
- updated OpenEXR library to version 3.3.14, from version 2.2.0. OpenEXR no longer uses ZLib for EXR data since 3.2. ZIP and DWA compression modes use libdeflate;
- updated OpenJPEG library to version 2.5.4, from a March 2014 trunk snapshot labelled 2.0.0;
- almost all of the FreeImage files are now UTF-8 encoded, no longer Latin-1 or CP1252;

Color management:
- a profile is a block of ICC bytes and its size. NULL stands for FreeImage's default space: sRGB for 8- and 16-bit colour images, sRGB primaries with a linear tone curve for float ones, grey with the sRGB tone curve for greyscale ones. A result in the default space has no profile attached, any other result has its profile, and CMYK results the FIICC_COLOR_IS_CMYK flag;
- the conversion starts from the image's embedded profile when its colour space matches the pixels, else from the default of the image's type; a CMYK image without a profile is taken as the device CMYK of FreeImage's loaders. FreeImage_ConvertToCMYK() without a profile returns an image that is already CMYK as a copy, its profile kept;
- flags: FICMS_INTENT_PERCEPTUAL (0), FICMS_INTENT_RELATIVE_COLORIMETRIC, FICMS_INTENT_SATURATION or FICMS_INTENT_ABSOLUTE_COLORIMETRIC, combined with FICMS_BLACKPOINT_COMPENSATION; FreeImage_SoftProof() also takes FICMS_GAMUT_CHECK and FICMS_SIMULATE_PAPER;
- results keep the precision of the source (8-bit, 16-bit or float) and its alpha channel, except in CMYK, which has none; palettes and 16-bit 555/565 images become 24- or 32-bit, or 8-bit grey. FreeImage_ApplyICCProfile() keeps the pixel format: a palette changes its colours, and a CMYK image becomes RGBA with an opaque alpha; it returns FALSE for conversions that change the pixel size;
- to show an image: load CMYK JPEG, TIFF and PSD files with JPEG_CMYK, TIFF_CMYK or PSD_CMYK, then call FreeImage_ApplyICCProfile() with the monitor's profile, or with NULL for sRGB. Do it before FreeImage_ConvertTo*() and FreeImage_Rescale(), which drop the profile;
- 8-bit conversions use Little CMS's precalculated tables; 16-bit ones are computed exactly, as those tables are coarser than the 8-bit path, and take about 5 to 7 times longer per thread. Rows are converted on several threads. A soft proof with FICMS_GAMUT_CHECK first builds a gamut table, 0.1 to 0.3 seconds;

Bugs or limitations identified:
- AVIF and HEIF images that describe their colours with CICP (nclx) instead of an ICC profile, and JPEG 2000 images, get no profile attached: color management takes them as sRGB;
- saving WEBP files is extremely slow at 16000 x 16000 px;
- images over 5000 mgpx saved as JXR might be malformed; only Freeimage opens them correctly; Windows Photo opens them [on Win10], but without an alpha channel; Affinity Photo 2.0 and paint.net v5.0 crash on open;
