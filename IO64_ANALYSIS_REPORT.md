# 64-bit I/O review (`worktree-io64`)

**Date**: 2026-09-25
**Reviewed**: 7de5c34 "I/O: FreeImageIO seeks and tells with 64-bit positions" and 05357f7 "I/O: GIF and TARGA positions are 64-bit on Win32 too", on qpv c0ed58c
**Fixes**: fb327b7 (HEIF), 608cb13 (ICO, GIF, TGA), and the commit that carries this report (32-bit builds, overflow guards, examples, wrappers)
**Targets**: Linux x86_64, Linux x86 (compile and symbol check), Windows x86 (a `__stdcall` FreeImage.dll), Windows x64

This replaces the first version of the report, which listed twelve findings without verifying or fixing them. Its claims are settled in the second table.

## Verdict

The change holds. `FI_SeekProc`/`FI_TellProc` carry `INT64` in every configuration of `FreeImage.h` (`int64_t`, `__int64` or `basetsd.h`'s, one type on each platform), the default I/O uses `_fseeki64`/`fseeko`, memory streams are 64-bit, bulk reads and writes go through the callbacks in 1 GB pieces, and every `tell_proc` result in the library now lands in an `INT64`. No `FreeImageIO` implementation in the repository kept the old types. The exports are unchanged: `libfreeimage.so` exports the same 551 symbols, and neither `FreeImage_fseek64` nor `FreeImage_ReadBytes` is among them.

The review found one regression the change brought to every platform (HEIF over a stream of unknown length), two it made reachable on 32-bit Windows (WebP, TGA's RLE cache), one gap on 32-bit POSIX (files over 2 GB were not opened for large-file access), smaller omissions, and older position bugs in the same code. All are fixed and tested.

## Findings

| # | Where | What | Since | Fixed in |
|---|---|---|---|---|
| 1 | `PluginHEIF.cpp` | libheif needs the exact stream length. 7de5c34 removed `HEIF_MeasureStream()` with the capped tells it served, so a `FreeImageIO` whose `SEEK_END` fails loaded no HEIF: 43 of 43 test files failed, qpv loads them all. The probe is back: doubling, then bisection, single 64-bit seeks. | 7de5c34, all platforms | fb327b7 |
| 2 | `PluginWebP.cpp` | The whole stream is read into `malloc(length + 1)` with `length = (size_t)(end - start)`. On Win32 a stream of 4 GB - 1 bytes made that `malloc(0)` followed by a 4 GB read into it. A length a `size_t` cannot hold with its spare byte is refused. | Win32 since 7de5c34 (Windows had it for any stream past 2 GB before) | this commit |
| 3 | `PluginTARGA.cpp` `loadRLE` | The RLE cache is sized remaining bytes / rows, raised to at least a pixel, then cast to `size_t`. On Win32 a remainder past 4 GB could leave it under a pixel, and `getBytes()` handed out bytes past the allocation: garbage pixels under Wine. On 64-bit, `refill()` read `(unsigned)` of the size and zeroed the rest: black pixels, reproduced on Linux with qpv. The cache is capped at 16 MB. | Win32 since 7de5c34; 64-bit before it | this commit |
| 4 | `Plugin.cpp`, `GetType.cpp`, `CacheFile.cpp`, `JPEGTransform.cpp` | On 32-bit POSIX, `fopen()` without large-file support fails on files over 2 GB (`EOVERFLOW`) and `ftruncate()` took a 32-bit `off_t`: `FreeImage_Load()`, `FreeImage_Save()`, the multi-page functions and the JPEG transforms stopped at 2 GB although `FreeImageIO.cpp` seeks in 64 bits. These files define `_FILE_OFFSET_BITS 64` as `FreeImageIO.cpp` does. Not in the makefiles: under it, zlib's `zconf.h` can rename `gzopen`, `gzseek` and others to their `*64` variants in every file that includes `zlib.h`, and the flag would reach every vendored library. | omission of 7de5c34 | this commit |
| 5 | `MultiPage.cpp` | `FreeImage_LoadMultiBitmapFromMemory()` took the stream's start through `FreeImage_TellMemory()`, a `long`: on Windows, a stream positioned past 2 GB started at byte 0. | omission of 7de5c34 | this commit |
| 6 | `FreeImageIO.cpp` | On 32-bit builds the 2 GB - 1 cap on memory streams also capped seeks in a wrapped buffer over 2 GB: `SEEK_END` failed. A wrapped buffer is now addressable whole; writes stay capped. Checked by inspection: the 32-bit Wine process here cannot reserve over 2 GB to run it. | omission of 7de5c34 | this commit |
| 7 | `PluginG3.cpp`, `MNGHelper.cpp` | Explicit casts cut a G3 stream or an embedded PNG past 4 GB to 32 bits, so part of it was read; MNG's chunk skip added the CRC in 32 bits. Both are refused or computed in 64 bits. | 7de5c34 (explicit casts, invisible to `-Wshorten-64-to-32`) | this commit |
| 8 | `PluginTIFF.cpp`, `PluginEXR.cpp`, `PluginRAW.cpp`, `PluginJXR.cpp` | Each adds a 64-bit offset from the file (BigTIFF IFD offsets, EXR chunk offsets, LibRaw's) to the stream's start: past `INT64_MAX` that is signed overflow. They refuse it; EXR throws `Iex::InputExc`, which OpenEXR's stream adapter catches, as its own streams do. | before 7de5c34 on LP64 | this commit |
| 9 | `PluginICO.cpp` | The writer stored the stream's positions as the directory's offsets, while `Load` counts them from where the ICO starts: an icon saved to a stream not at byte 0 read back wrong, and a two-page save failed, since `Save` reads page 0 back. Past 4 GB the `DWORD` offsets wrapped. | before 7de5c34 (the loader became relative on qpv) | 608cb13 |
| 10 | `PluginGIF.cpp` | `Load` read the logical screen at byte 6 of the stream: a GIF behind other data got a screen made of those bytes (a 12586 x 16184 canvas in the test with `GIF_PLAYBACK`). | upstream | 608cb13 |
| 11 | `PluginTARGA.cpp` thumbnail | A thumbnail claiming more pixels than the bytes before the footer was copied out of a buffer of that size: AddressSanitizer reports a heap-buffer-overflow in `TargaThumbnail::toFIBITMAP()`. The data is checked against the claim, and no more than 255 x 255 x 4 bytes are read. | upstream | 608cb13 |
| 12 | `PluginTARGA.cpp` `Save` | With the pixels ending past 4 GB, the extension area's offset was cut to 32 bits, the writer seeked back to it and wrote the thumbnail and the footer over the pixels, and `Save` returned `TRUE`. The extension area is left out, with a message. | before 7de5c34 on LP64; Windows since | 608cb13 |
| 13 | `Examples/Generic/FIIO_Mem` | The seek stored the `INT64` offset in a `long` unchecked (on Windows a 3 GB offset became negative, and the next read came from before the buffer). It refuses what the `long` cannot hold. Its callbacks lacked `DLL_CALLCONV`: against a 32-bit FreeImage.dll it did not compile (5 errors). | narrowing since 7de5c34; calling convention upstream | this commit |
| 14 | `TestAPI/IO`, `Examples/Generic/LoadFromHandle.cpp`, `Wrapper/VB6` | The tests failed on 32-bit Windows (`memstream` could not reserve 2.5 GB) and lacked large-file support on 32-bit POSIX; the example that shows `fseeko` did too. The VB6 module now says how VB6 passes the 64-bit offset (as `Currency`). | 7de5c34 | this commit |

## The first report's claims

| Claim | Verdict |
|---|---|
| BUG-01, `MultiPage.cpp` start through `FreeImage_TellMemory()` | Real: finding 5. The function is `FreeImage_LoadMultiBitmapFromMemory()`. |
| BUG-02, ICO offsets | Real, older than the change: finding 9. |
| BUG-03, `JPEGTransform.cpp` on 32-bit Linux | Real: finding 4, fixed per file rather than in `Makefile.gnu`, `Makefile.fip` and `Makefile.cygwin` as proposed (Cygwin's `off_t` is 64-bit anyway). |
| BUG-04, tests without `_FILE_OFFSET_BITS` | The IO tests, which make files over 2 GB, and the `LoadFromHandle` example have it now. `testMPageStream`, `fipTestMPageStream` and the J2K and narrowio tests use small files: nothing to fix. |
| BUG-05, `_tiffSeekProc` returning `-1 - start` | Not a defect: libtiff's `SeekOK()` compares the returned position with the one it asked for, so a failed tell cannot pass for a seek. The offset overflow next to it was real: finding 8. |
| BUG-06, LibRaw `_start` not clamped | Not a defect: a stream whose tell fails when it is opened also fails the `SEEK_END` size probe, and LibRaw refuses a 0-byte stream. |
| OMISSION-01/02, 64-bit memory exports | Not a bug: the exports keep their types by design, as the README says. See below. |
| OMISSION-03, `FIIO_Mem.h` fields | Real: finding 13, fixed by bounding the position rather than widening the example's structure. |
| OMISSION-04, VB6 | The module implements no callbacks; it now documents them: finding 14. |
| PRE-01, GIF byte 6 | Real: finding 10. |
| PRE-02, JXR `SetPos` overflow | Not reachable (jxrlib's positions come from 32-bit container fields); guarded anyway: finding 8. |
| Platform matrix | The first report did not build 32-bit Linux or 32-bit Windows; see below. |

## Verification

| Target | Build | Result |
|---|---|---|
| Linux x86_64, gcc | `make -f Makefile.gnu` | testAPI and the IO, JXR, HEIF, AVIF, J2K, EXR, MNG, APNG, ICC, WebP, RAW and JPEG suites pass; IO and HEIF under AddressSanitizer; `bigsave` (2.2 GB) and `memstream --grow`. `g++ -Wall -Wextra` shows no new warning in the 41 changed sources. |
| Linux x86, 32-bit | zig for x86-linux-gnu, the five files that open files | They reference `fopen64`, `fseeko64`, `ftello64` and `ftruncate64`; qpv's reference `fopen`, `fseek`, `ftell` and `ftruncate`. Not run: no 32-bit glibc here. |
| Windows x86 | the whole library as FreeImage.dll with `__stdcall` exports (zig, MinGW-w64 headers); tests linked against its import library, run under Wine | `bigfile` (pages at 2.5 GB and 4.5 GB through `FreeImage_Load`, `FreeImage_LoadFromHandle`, `FreeImage_OpenMultiBitmap` and `FreeImage_OpenMultiBitmapFromHandle`, the TGA and WebP cases), `streams`, an in-place `FreeImage_JPEGCrop()` (the file cut with `_chsize_s`) and a disk-cached multi-page TIFF pass. `memstream` skips. Against the unfixed branch: `bigfile` 2 failures, `streams` 12. |
| Windows x64 | static library, same programs | All pass, and `bigsave`: 2,209,282,234 bytes, IFD past 2 GB. |
| Narrowing | `-Wshorten-64-to-32`, 41 changed sources, qpv vs the branch, x86_64- and x86-windows-gnu | No new narrowing. |

A differential test read 186 files (every tracked sample, and a generated file per writable format, multi-page and thumbnail variants included) through `FreeImage_Load`, a `FreeImageIO` at byte 0, behind 777 bytes of junk, with `SEEK_END` refused, header-only at byte 0 and behind junk, from memory, and page by page through both multi-page openers. qpv and the fixed branch give identical results on Linux, and their saved files are byte-identical. Windows x86 and x64 give Linux's results, except for three J2K files read with `SEEK_END` refused (the OpenJPEG assert below; Windows builds define `NDEBUG`) and, on x86 only, the two lossy DWAA/DWAB EXR files, whose floating-point decode differs under x87 on the unfixed branch too.

## Seen, not fixed

- OpenJPEG asserts `m_user_data_length >= m_byte_offset` when a J2K or JP2 stream's length is unknown (its `SEEK_END` fails), although 0 means unknown there. `Makefile.gnu` builds without `NDEBUG`, so the Linux library aborts (qpv too); builds with `NDEBUG` fail the load cleanly.
- `Makefile.mingw` lacks `-DAVIF_ENABLE_EXPERIMENTAL_MINI=1`, which every other build defines: a MinGW build does not read mini AVIF files, which fall to the HEIF plugin and fail.
- The memory stream exports keep 32-bit types: `FreeImage_OpenMemory()` and `FreeImage_AcquireMemory()` take a `DWORD`, `FreeImage_SeekMemory()` and `FreeImage_TellMemory()` a `long`. A stream past 4 GB cannot be wrapped or acquired, and on Windows a position past 2 GB cannot be told or seeked to through them. Widening them means new exports and wrapper updates: a decision for the maintainer.
- On macOS `int64_t` is `long long`, so, as on Windows, a C++ `FreeImageIO` written with `long` callbacks no longer compiles (the binaries stay compatible).
- The Managed C++ wrapper's `SeekProc` (`FreeImageIO.Net.cpp`) returns `Stream::Seek()`'s new position where FreeImage expects 0; the file uses Managed Extensions syntax (`__nogc`), which current compilers no longer build.
- `mng_CopyRemoveChunks()` allocates the stream's size plus the chunk's for a removal and writes it all back, leaving uninitialised bytes after IEND in the in-memory PNG; the decoder stops at IEND.
- `libfreeimage.so` exports one weak `std::vector` instantiation for `long` instead of `unsigned long`, from GIF's offset vectors: harmless.
