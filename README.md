What is FreeImage ?
-----------------------------------------------------------------------------
FreeImage is an Open Source library project for developers who would like to support popular graphics image formats like PNG, BMP, JPEG, TIFF and others as needed by today's multimedia applications.

FreeImage is easy to use, fast, multithreading safe, and cross-platform (works with Windows, Linux and Mac OS X).

Thanks to it's ANSI C interface, FreeImage is usable in many languages including C, C++, VB, C#, Delphi, Java and also in common scripting languages such as Perl, Python, PHP, TCL, Lua or Ruby.

The library comes in two versions: a binary DLL distribution that can be linked against any WIN32/WIN64 C/C++ compiler and a source distribution.
Workspace files for Microsoft Visual Studio provided, as well as makefiles for Linux, Mac OS X and other systems.

--------
This is a clone of https://sourceforge.net/p/freeimage/svn/ .

FreeImage is currently sporadically maintained by Hervé Drolon on SourceForge. It is licensed under the GNU General Public License, version 2.0 (GPLv2) or version 3.0 (GPLv3), and the FreeImage Public License (FIPL). More details on the project homepage: https://freeimage.sourceforge.io/ .

--------

This branch is used to compile the FreeImage.DLL used in Quick Picto Viewer. It brings the following changes:
- FillBackgroundBitmap() has a new optional parameter;
- applied patches/fixes found in the Fedora F39 repository for: CVE-2020-24292, CVE-2020-24293, CVE-2020-24295, CVE-2021-33367, CVE-2021-40263, CVE-2021-40266, CVE-2023-47995, CVE-2023-47997;
-- Patches found at: https://src.fedoraproject.org/rpms/freeimage/tree/f39
-- CVE-2021-40266 - NULL pointer dereference in ReadPalette() in PluginTIFF.cpp [fedora-all]
-- CVE-2023-47995 - prevents memory allocation with dimensions that exceed the JPEG format limits
-- CVE-2023-47997 - prevent an infinite loop in PluginTIFF.cpp::Load. 
- fixed buffer overflows in PluginICO.cpp, PSDParser.cpp, PluginTIFF.cpp (with the aforementioned patches)
- fixed jxr encoder to be able to handle images over 1300 mgpx;
- fixed bmp decoder/encoder to be able to handle images over 1300 mgpx;
- fixed behavior with extreme values of the tone-mapping algorithms; 
- fixed out of bounds accesses in PluginBMP, PluginPSD, PluginMNG and PluginPICT;
- fixed integer wrap around and segmentation fault in Exif.cpp;
- fixed FreeImage_Copy() to not crash with very large images [over 5000 mgpx];
- fixed FreeImage_Rescale() to work with very large images [over 5000 mgpx]; it no longer screws up the colors;
- fixed FreeImage_Rotate() to work with very large images [over 5000 mgpx];
- fixed a data race in the 1-bit 90/180/270 rotation;
- fixed the bundled ZLib, OpenEXR, LibJXR failing to compile on GCC 14+;
- fixed the GIF plugin: multi-page saves now enlarge the logical screen to fit every frame, and fixed a stack buffer overflow in the LZW encoder and a heap buffer overflow when loading such files with GIF_PLAYBACK;
- multi-threaded image resizer and rotation using OpenMP pragma; the makefiles now enable OpenMP too. Build it with `make OPENMP=0` for a single-threaded library; see README.linux;
- updated the OpenEXR library to version 3.3.14, from version 2.2.0, and Imath to 3.1.12. Since 3.3 the C++ API reads and writes through OpenEXRCore, OpenEXR's C library, which is bundled too (Source/OpenEXR/OpenEXRCore): fewer allocations per scanline read, one implementation of each compressor instead of two, and more uniform thread dispatch. OpenEXR has not used ZLib for EXR data since 3.2 - ZIP and DWA compress with libdeflate, bundled as Source/LibDeflate 1.18 with its symbols hidden, both as OpenEXR's own build does it. The release notes between 3.1.3 and 3.3.14 name more than forty CVEs. Decoding did not change: the 97 files of the openexr-images test set and every EXR save flag decode bit-for-bit as they did before, through a file and through a memory stream; only DWAA/DWAB move, by one or two half-float ULP in a handful of pixels, because 3.3 reimplemented that codec in C. ZIP and PXR24 files come out a different size, libdeflate not being zlib. OpenEXR's own sources now need C++14 at the least and are compiled as C++17 (Makefile.openexr); the rest of FreeImage is untouched, and the three makefiles that asked for a strict -std=c99 ask for gnu99 instead, since OpenEXRCore opens files with O_CLOEXEC and reads them with pread(). Regression tests live in TestAPI/EXR;
- two patches to the bundled OpenEXR and libdeflate, each marked "FreeImage:" in the source: OpenEXRCore's default error handler printed every failure to stderr (internal_structs.c), which is new in 3.3 and which no other bundled codec does - the printing is dropped, while the error code still reaches the C++ layer, which raises the exception PluginEXR turns into a message for the callback installed with FreeImage_SetOutputMessage. Those messages are less specific than 3.1.3's, though, and that part is upstream's doing rather than the patch's: the detail now belongs to the core, which reports it out of band, so a truncated file that used to say "Data decompression (zlib) failed" now says "Unable to run decoder". And libdeflate's API is tagged visibility("hidden") instead of "default" (lib_common.h), so its two dozen unversioned libdeflate_* symbols stay out of libfreeimage's dynamic symbol table and cannot interpose on another libdeflate in the same process - the same rewrite OpenEXR's own CMake performs on the copy it fetches;
- added FreeImage_RescaleRawBits()
- updated the bundled OpenJPEG library to version 2.5.4, from a March 2014 trunk snapshot labelled 2.0.0: HTJ2K (JPEG 2000 Part 15) files decode instead of turning into noise, two conformance files that were refused or decoded wrong now match the reference decoder, a save rate of 1 is lossless, and eleven years of upstream security fixes come along. The J2K/JP2 plugins now decode and encode on every core (3-5x faster on 8 cores), save images smaller than 32 pixels (the old library refused anything under 64), and no longer corrupt a JP2 saved through a handle that does not start at offset 0. Regression tests live in TestAPI/J2K;
- added AVIF loading (FIF_AVIF) with the bundled libavif 1.4.2 and dav1d 1.5.4: 8/10/12-bit, alpha, image sequences as multi-page bitmaps, ICC/Exif/XMP, the clap/irot/imir transforms applied. Files branded 'mif3', which replace the meta box with a MinimizedImageBox (the compact header of ISO/IEC 23008-12 Amd. 2, written for small images), are read as well: libavif gates that reader behind AVIF_ENABLE_EXPERIMENTAL_MINI, which every makefile and the MSVC project now define. "Experimental" there covers the *writer*, which produces files older decoders cannot read; FreeImage writes no AVIF, so enabling it only adds files the library can open. A 'mif3' file names no codec in its brands, so the plugin claims one only when the FileTypeBox minor_version is 'avif', the value libavif itself writes and requires: an HEVC-coded 'mif3' file goes to the HEIF plugin, which reads it. Read-only: no AV1 encoder is bundled. HEIC is a different codec (HEVC) and has its own plugin, see below;
- three small patches to the bundled libavif, each marked "FreeImage:" in the source: its decoder refused any image size limit above 16384 x 16384 pixels as "not implemented" (read.c), which would have capped AVIF at 268 mgpx; the two lone static_asserts (io.c, and read.c's MinimizedImageBox parser) are spelled _Static_assert for the -std=c99 makefiles; and a file whose ipma boxes share one pair of version and flags (one ipma per item, as link-u's cavif writes and as the widely copied avif-sample-images set is built) is no longer refused with "Multiple Box[ipma] ..." but read as one association list, since an item listed twice is still rejected (read.c);
- the LibAVIF MSVC project gives the bundled libyuv objects a libyuv_ prefix: its scale.c and libavif's src/scale.c both compiled to scale.obj, one overwrote the other, and FreeImage.dll failed to link with avifImageScale / avifImageScaleWithLimit unresolved;
- fixed the LibAVIF and LibDav1d MSVC projects, which had lost the separator in front of their extra preprocessor defines (WIN32_LEAN_AND_MEANAVIF_CODEC_DAV1D=1, _LIBAVIF_CODEC_DAV1D=1): libavif was compiled with no decoder at all and every AVIF failed with "No codec available";
- FreeImage_OutputMessageProc() mirrors every message to the debugger output (Sysinternals DebugView, the Visual Studio output window) as "qpv: fim: [FORMAT] message", whether or not a handler was registered with FreeImage_SetOutputMessage(); the message formatter is now bounds-checked (no append can overrun its 512-byte buffer, a NULL %s prints as "(null)") and understands %u;
- fixed Makefile.srcs / fipMakefile.srcs omitting tif_hash_set.c, which left libfreeimage.so with undefined TIFFHashSet* symbols;
- added HEIC/HEIF loading (FIF_HEIF, extensions heic/heif/hif) with the bundled libheif 1.23.4 and libde265 1.1.3: the HEVC-coded HEIF files of iPhones, Samsung, Canon and Sony (.hif), 8/10/12-bit, monochrome, alpha, grid (tiled) images, the clap/irot/imir transforms applied by libheif, ICC/Exif/XMP, the file's 'thmb' thumbnail as FreeImage_GetThumbnail(), files with several top-level images as multi-page bitmaps (the primary image first). A JPEG or ISO/IEC 23001-17 uncompressed payload is decoded too, through the LibJPEG and ZLib that FreeImage already bundles (this also decodes the JPEG thumbnails Sony .hif files carry). Grid tiles are decoded in parallel and single pictures use libde265's worker threads. Read-only: no HEVC encoder is bundled. A JPEG 2000 payload is decoded too, through the bundled OpenJPEG 2.5.4. A HEIF file carrying an AVC or VVC payload is recognised but refused with a message naming the codec: no H.264 or H.266 decoder is bundled; AVIF files stay with the AVIF plugin;
- the two HEIF libraries are C++17/C++20 with *.cc sources: every makefile gained a .cc rule and includes the new Makefile.heif for their per-library flags; the LibHEIF and LibDe265 MSVC projects (v142, /std:c++20 and /std:c++17) are in the solution and referenced by FreeImage and FreeImageLib;

Bugs or limitations identified:
- saving WEBP files is extremely slow at 16000 x 16000 px;
- creating multi-paged TIFFs causes crashes under certain circumstances;
- images over 5000 mgpx saved as JXR might be malformed; only Freeimage opens them correctly; Windows Photo opens them [on Win10], but without an alpha channel; Affinity Photo 2.0 and paint.net v5.0 crash on open;
