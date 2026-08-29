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
- multi-threaded image resizer and rotation using OpenMP pragma; the makefiles now enable OpenMP too (they never passed -fopenmp, so on Linux/macOS/MinGW the pragmas were dead and the code ran single-threaded - only the MSVC Release builds were actually parallel). Build with `make OPENMP=0` for a single-threaded library; see README.linux;
- fixed a data race in the 1-bit 90/180/270 rotation: eight consecutive source rows pack their bits into the same destination byte, so the parallel `|=` dropped updates and corrupted the output. This was live in the MSVC Release builds;
- fixed the bundled ZLib failing to compile on GCC 14+ / Clang 16+: gzguts.h pulled in <io.h> on Windows but nothing on POSIX, so gzlib.c/gzread.c/gzwrite.c called lseek/read/write/close with no prototype (also a silent 64-bit offset truncation on older compilers that merely warned). Cygwin was affected too;
- fixed the bundled OpenEXR not compiling on GCC 13+: 11 headers name uint64_t/int64_t in their own declarations without including <cstdint>, which stopped working when libstdc++ dropped transitive includes (4 of 107 translation units failed; the rest only survived on include order);
- updated the OpenEXR library to version 3.1.3, from version 2.2.0;
- added FreeImage_RescaleRawBits()
- fixed the GIF plugin: multi-page saves now enlarge the logical screen to fit every frame (frames wider than the first one produced invalid files), fixed a stack buffer overflow in the LZW encoder (StringTable::CompressEnd) and a heap buffer overflow when loading such files with GIF_PLAYBACK; these bugs crashed QPV when creating animated GIFs

Bugs or limitations identified:
- saving WEBP files is extremely slow at 16000 x 16000 px
- creating multi-paged TIFFs causes crashes under certain circumstances (easy to reproduce)
- images over 5000 mgpx saved as JXR might be malformed; only Freeimage opens them correctly; Windows Photo opens them [on Win10], but without an alpha channel; Affinity Photo 2.0 and paint.net v5.0 crash on open;
