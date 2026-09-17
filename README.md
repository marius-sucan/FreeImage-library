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
- fixed FreeImage_Rotate() to work with very large images [over 5000 mgpx];
- fixed a data race in the 1-bit 90/180/270 rotation;
- fixed the bundled ZLib, OpenEXR, LibJXR failing to compile on GCC 14+;
- fixed the GIF plugin: multi-page saves now enlarge the logical screen to fit every frame, and fixed a stack buffer overflow in the LZW encoder and a heap buffer overflow when loading such files with GIF_PLAYBACK;
- fixed the RAW plugin not reading a stream that does not start at byte zero;
- fixed the RAW plugin's datastream returning the wrong byte on a big-endian machine. LibRaw_freeimage_datastream::get_char() read one byte into an int and returned the int;
- fixed the G3 plugin hanging on a damaged fax file;
- fixed FreeImage_LockPage() getting slower the further into an animated GIF it went. Reading an n-frame file from beginning to end cost O(n^2);
- fixed Makefile.srcs / fipMakefile.srcs omitting tif_hash_set.c, which left libfreeimage.so with undefined TIFFHashSet* symbols;
- and many other fixes

Changes:
- FillBackgroundBitmap() has a new optional parameter;
- multi-threaded image resizer and rotation using OpenMP pragma; the makefiles now enable OpenMP too. Build it with `make OPENMP=0` for a single-threaded library; see README.linux;
- FreeImage_OutputMessageProc() mirrors every message to the debugger output (Sysinternals DebugView, the Visual Studio output window) as "qpv: fim: [FORMAT] message";
- added FreeImage_RescaleRawBits();
- added full support for animated WebP files and example file; save WebP animations implemented as well;
- added AVIF loading (FIF_AVIF=37) with the bundled libavif 1.4.2 and dav1d 1.5.4;
- added HEIC/HEIF loading (FIF_HEIF=38, extensions heic/heif/hif) with the bundled libheif 1.23.4 and libde265 1.1.3; 
- added APNG reading and writing (FIF_APNG=39, extensions apng/png) on top of LibPNG; save APNG animations implemented as well;
- updated LibRaw library to version 0.22.2, from 0.21.1;
- updated LibJPEG library to version 10, from the 9d of January 2020;
- updated LibTIFF library to version 4.7.2, from 4.6.0 release of September 2023;
- updated ZLib library to version 1.3.2, from the 1.2.13 of October 2022;
- updated OpenEXR library to version 3.3.14, from version 2.2.0. OpenEXR no longer uses ZLib for EXR data since 3.2. ZIP and DWA compression modes use libdeflate, bundled as Source/LibDeflate 1.18 with its symbols hidden;
- updated OpenJPEG library to version 2.5.4, from a March 2014 trunk snapshot labelled 2.0.0;

Bugs or limitations identified:
- saving WEBP files is extremely slow at 16000 x 16000 px;
- creating multi-paged TIFFs causes crashes under certain circumstances;
- images over 5000 mgpx saved as JXR might be malformed; only Freeimage opens them correctly; Windows Photo opens them [on Win10], but without an alpha channel; Affinity Photo 2.0 and paint.net v5.0 crash on open;
