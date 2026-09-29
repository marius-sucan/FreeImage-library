# Files over 2 GB: the `worktree-io64` work reviewed again, and its head against qpv

**Date**: 2026-09-29
**Reviewed**: 7de5c34..b6e3af3, the eight commits that made FreeImage's I/O 64-bit (7de5c34, 05357f7, cdaa57f, fb327b7, 608cb13, fc9ee6d, 6b7462e, b6e3af3): 85 files, +1768 -734
**Compared**: `worktree-io64` at c41b50f against `qpv` at c0ed58c
**Targets run**: Linux x86_64 (gcc), Windows x64 (zig, static library) and Windows x86 (zig, `__stdcall` FreeImage.dll), both under Wine

This report adds to `IO64_ANALYSIS_REPORT.md` and replaces none of it. That report checked the new 64-bit positions. This one asks whether FreeImage can now load and save files over 2 GB and over 4 GB, format by format. It fixes nothing.

## Verdict

The 64-bit positions hold. Every format tried reads and writes past 4 GB where its own format allows it, and both Windows targets pass the range's tests at c41b50f. The head adds no regression in the differential test, including 748 cut files. The one regression the range brought is behind embedded MNG and JNG images of 4 GB or more.

A 4 GB file is still out of reach in several places, and some of those failures report success:

- `FreeImage_Save` of a TIFF over 4 GB returns TRUE and leaves a file that cannot be opened. A multi-page TIFF crossing 4 GB silently loses the page that crosses it.
- When a write fails, as on a full disk, PNG, TIFF, TGA, GIF, ICO, PFM, PNM, WBMP, JNG, MNG and APNG saves still return TRUE, and `FreeImage_Save` ignores `fclose()`. J2K and JP2 saves never return.
- Saving a float image of 2^31 samples or more as half-float EXR, the default, aborts the process or corrupts the heap.
- SGI rows past 2 GB, raw/DNG files over 2 GB, multi-page pages over 2 GiB and one PSB size fail on every platform.

All of these except the MNG and JNG cases behave the same on qpv: they predate the range and fall outside what it changed.

## Findings

R = regression or incomplete fix from the range. O = omission, still 32-bit or limited after it. P = older bug found on the way, hit hardest by large files. "Both heads" means reproduced on qpv as well.

| # | Class | Severity | Where | What happens | Where seen |
|---|---|---|---|---|---|
| 1 | O | high | `PluginTIFF.cpp:1107` (`TIFFFdOpen(.., "w")`), `:2670-2765`, `:1121` | The writer only writes classic TIFF, whose offsets are 32-bit, and ignores every libtiff result: `TIFFWriteScanline`, `TIFFWriteDirectory`, `TIFFClose`. libtiff refuses data past 4 GiB and nothing is reported. A 65536 x 65600 8-bit `TIFF_NONE` save returned TRUE and left a 4,294,901,768-byte file that fails to reload ("Error while opening TIFF: data is invalid"). Seven 25000 x 25000 pages appended to a new multi-page TIFF: `CloseMultiBitmap` returned TRUE, the file is 4,294,952,056 bytes, and **6 pages** come back. A 2.2 GB TIFF round-trips correctly. | Linux, both heads (reproduced); the code is the same on every platform |
| 2 | P | high | `PluginPNG.cpp:100`, `PluginPFM.cpp:415,420`, `PluginPNM.cpp` (18 writes), `PluginTARGA.cpp` (10), `PluginGIF.cpp` (45), `PluginICO.cpp:535,550,612,718,776`, `PluginWBMP.cpp:124-341`, `MNGHelper.cpp:1340`, TIFF (above); `Plugin.cpp:484,509` | A `write_proc` that fails is ignored. A `FreeImageIO` whose writes fail after 100 bytes, or half way, gets `Save` = TRUE from PNG, TIFF (LZW and none), TGA, GIF, ICO, PFM, PBM/PGM/PPM (ASCII and raw), WBMP, JNG, MNG, APNG, and from JXR when the failure comes half way. BMP, JPEG, PSD, XPM, HDR, EXR and WebP return FALSE. `FreeImage_Save`/`FreeImage_SaveU` ignore `fclose()`, whose flush writes the last buffered bytes: `FreeImage_Save(FIF_BMP / FIF_PNG / FIF_TIFF, .., "/dev/full")` returns TRUE with nothing written. A disk that fills during a multi-GB save therefore yields a truncated file and TRUE, and `FreeImage_Save`'s clean-up (`remove()` on FALSE) never runs. | Linux, Windows x64, Windows x86, both heads: the same table on all four builds |
| 3 | P | high | `J2KHelper.cpp:50-52`; OpenJPEG `cio.c:441-455` | A J2K or JP2 save whose write fails **never returns**. `_WriteProc` returns 0 for a failed write, while OpenJPEG treats only `(OPJ_SIZE_T)-1` as an error, so `opj_stream_flush` loops on `m_bytes_in_buffer` at 100% CPU. Returning -1 when nothing was written fixes it (`_ReadProc` already does this). | Linux (killed after 30 s), Windows x64 and x86 (killed after 60 s), both heads |
| 4 | O | high | `PluginEXR.cpp:812`, `:819`, `:874`; `:653` | The half-float path (the default) sizes its buffer as `new(std::nothrow) half[width * height * components]` and indexes it by `y * width * components`, both `int`. From 2^31 samples, `new` gets a negative count: GCC throws `std::bad_array_new_length`, which `Save` does not catch (it catches `Iex::BaseExc`), so the process aborts (65536 x 32768 FIT_FLOAT: SIGABRT). From 2^32 samples the count can wrap to a small positive number and the fill loop writes past it (65536 x 65537: SIGSEGV). That is a half EXR of 4 GB uncompressed, or an RGBAF image of 536 Mpx at any compression. `SaveAsEXR_LC` uses `Imf::Array2D<Imf::Rgba>`, whose `long` sizes are 32-bit on Windows. A compiler whose nothrow `new[]` returns NULL for an impossible count would give Save = FALSE and an out-of-memory message for the first case; MSVC was not tried. | Linux, both heads (reproduced) |
| 5 | O | medium | `PluginSGI.cpp:220`, `:364` | RLE row offsets are read as `LONG` (signed 32-bit) and added to the start, so a row stored at 2 GB or later gets a negative position. The failed seek is not checked and the row is decoded from whatever the stream holds. Test: a 16 x 2 image with row 1 at 2 GB + 4 KB, and decoy runs after row 0. Row 1 decodes the decoy on every build; at 2 GB - 4 KB it is right. The format's offsets are unsigned and reach 4 GB. | Linux, Windows x64, Windows x86, both heads |
| 6 | O | medium | `MultiPage.cpp:902-909`, `:1171`; `CacheFile.h:103-104` | A page appended, inserted or changed in a multi-page bitmap is cached as the document's format in memory, then copied into `CacheFile`, whose sizes are `int`. A page over 2 GiB once encoded cannot be stored. A 46341 x 46341 8-bit page (2.15 GB, 2,939,030,238 bytes as the cache's LZW TIFF): io64's `AppendPageEx` returns FALSE with "the page cache cannot hold more than 2 GiB". qpv's memory stream stopped at 2 GB and TIFF ignored the failed writes: qpv cached a truncated page, returned TRUE, and failed only at close. io64 is better but keeps the limit. | Linux, both heads (behaviour differs as described) |
| 7 | O | medium | LibRaw `libraw_const.h:31-44`, `identify.cpp:722,732`; `PluginRAW.cpp` sets no limit | LibRaw refuses non-DNG raws over 2 GB - 1, and DNGs too unless it is built with `USE_DNGSDK`, and `max_raw_memory_mb` stays at 2048. A tracked DNG padded with zeros to 2,147,487,744 bytes loads as NULL, header-only too, with "failed to open input stream (unknown format)". Padded to 2,147,479,552 bytes it loads. The range's 64-bit LibRaw stream cannot load any raw over 2 GB while these defaults stand: they are compile-time macros plus a runtime parameter. | Linux, both heads |
| 8 | R | low/medium | `PluginMNG.cpp:294`, `:308` | 7de5c34 fixed `ScanEmbeddedStream`'s comparison with the file length but kept `*out_length = (DWORD)(end - start)`. An embedded image of 4 GB + W bytes is recorded as W bytes, and parsing resumes W bytes into its data. Test: a sparse MNG holding one 1x1 PNG of 2^32 + 1,000,000 bytes, with a 2x2 PNG and MEND planted in its first IDAT. io64 reports 2 pages, and page 1 is the planted image. qpv's `(DWORD)file_length` wrapped instead: 1 page, treated as cut. Both are wrong. io64's version shows a frame other readers do not, from a crafted file. `MNGFrame::length` is a DWORD, so frames of 4 GB and more need refusing, as `mng_CountPNGChunks` now does. | Linux (reproduced); by code, every platform |
| 9 | R | low | `MNGHelper.cpp:1281`, `:1319`, `:1339`; `:375,438,483,1055` | The JNG writer copies in-memory JPEG and PNG streams with the 32-bit `FreeImage_AcquireMemory` and ignores its result. Memory streams now pass 4 GB, and past it that call returns FALSE with NULL and 0. A JPEG part of 4 GB or more is written as no JDAT at all, and a whole JNG of 4 GB or more as zero bytes, with Save = TRUE. Reading, an alpha layer of 4 GB or more is dropped. On qpv the in-memory JPEG could not pass 2 GB, and the save returned FALSE. The wall moved from 2 to 4 GB, and beyond it the failure is now silent. | by inspection (a test needs about 8 GB of RAM) |
| 10 | O | low | `PSDParser.cpp:1502`, `:1521` | `const unsigned dst_buffer_size = dstLineSize * nHeight` wraps once the bitmap passes 4 GiB, and the CVE-2020-24295 check compares against the wrapped value. A valid raw 8-bit 65536 x 65536 PSB is refused as "Invalid PSD image"; 65536 x 65537 loads. RLE files do not use the check. | Linux, both heads |
| 11 | O | low | `PluginJ2K.cpp`/`PluginJP2.cpp` (no tiling); OpenJPEG `j2k.c:5466-5470`, `:13238`; `jp2.c:1823-1825` | FreeImage encodes a JPEG 2000 image as one tile. OpenJPEG caps a tile's encoded buffer at `UINT_MAX` and writes `Psot` and the `jp2c` box length in 32 bits (no XLBox), so a J2K or JP2 file cannot pass 4 GB. The encode should fail rather than wrap, since the buffer runs out first, but a JP2 near 4 GB can still get a wrapped box length. Tiling would lift the J2K limit; JP2 needs OpenJPEG to write an XLBox. | by inspection (OpenJPEG needs 4 bytes per sample in RAM) |
| 12 | R (by design) | info | `FreeImage.h:597-598`; `README.md:71`; `Wrapper/AHK/freeimage-wrapper.ahk:292` | The callback ABI changed, and the README's "must be recompiled on Windows" understates it. A `FreeImageIO` still written with `long` callbacks, cast into the structure or in a binary built against 3.18/3.19, **crashes the 32-bit DLL**: Wine reports a page fault on execute inside `FreeImage_GetFileTypeFromHandle`, because the caller pushes 16 bytes and the callee pops 12. In C, the old signatures compile with a pointer-type warning, not an error. On Windows x64, an old `tell_proc` returning -1 reads as 4,294,967,295. An external plugin DLL built against the old header calls FreeImage's own seek procedure with the old stack layout, and fails the same way on x86. AutoHotkey U32's `RegisterCallback` returns 32 bits, so an AHK script cannot implement a correct `tell_proc` on 32-bit AHK (from the ABI, not run); memory streams are the way there. | Windows x86 (reproduced); the rest by inspection |
| 13 | O / info | low | `PluginBMP.cpp:1507`; `PSDParser.cpp:2187`; `PluginAPNG.cpp:494`; `PluginPNM.cpp` | Format limits and cost, no corruption. A BMP over 4 GB gets a wrapped `bfSize`: FreeImage's reader ignores the field (a 4.3 GB BMP round-trips), and a reader that checks it may not. PSD version 1 is chosen for any image up to 30000 x 30000, even past 2 GB, while Photoshop uses PSB there. An APNG frame with more than 2 GiB of compressed data is refused, because it is rebuilt as one IDAT. PNM reads and writes one sample per callback: 4.3 GB took 80 s each way through `FILE*`, and through a script-level `FreeImageIO` it would be impractical. | Linux (BMP, PNM); by inspection |
| 14 | report | info | `IO64_ANALYSIS_REPORT.md`, "Narrowing" row | That row claims a `-Wshorten-64-to-32` run for x86_64- and x86-windows-gnu. The script it used hardcodes `-target x86_64-windows-gnu` and ignores the `TARGET` its caller sets, so x86 never ran. Run now, qpv against c41b50f over the 44 changed C/C++ sources: no new narrowing on x86 (156 warnings on qpv, 153 on io64) or on x64 (140 and 137). | x86 and x64 |

## What the range gets right, verified

**Linux x86_64, io64 head, sizes past the walls.** The images are "virtual" dibs: a normal pitch over an address range that tiles one 64 MB memfd pattern, so a 4 GB image costs 64 MB of RAM. Every row is compared on reload.

| Format | Image | File (bytes) | Save / load | Result |
|---|---|---|---|---|
| BMP | 8-bit 65536 x 65600 | 4,299,162,678 | 3 s / 7 s | all rows match |
| PNG, no zlib compression | 8-bit 65536 x 65600 | 4,306,017,715 | 59 s / 16 s | all rows match |
| PGM raw | 8-bit 65536 x 65600 | 4,299,161,619 | 79 s / 80 s | all rows match (finding 13) |
| PFM | float 33000 x 33000 | 4,356,000,025 | 3 s / 9 s | all rows match |
| TGA | 24-bit 38000 x 38000 | 4,332,000,044 | 6 s / 7 s | all rows match |
| PSB raw | 8-bit 65536 x 65600 | 4,299,162,492 | 4 s / 5 s | all rows match |
| PSB RLE | 8-bit 65536 x 65600 | 4,333,268,402 | 7 s / 5 s | all rows match |
| EXR float, no compression | float 33000 x 33000 | 4,356,528,277 | 9 s / 7 s | reloads at its size (the writer flips the dib in place, which the shared pattern cannot survive) |
| TIFF, no compression | 8-bit 65536 x 34000 | 2,228,496,234 | 2 s / 2 s | all rows match |
| GIF | 8-bit 46000 x 46000 | 2,909,474,427 | 117 s / 73 s | all rows match |
| JPEG, quality 100 | 8-bit 50000 x 50000 | 3,946,348,081 | 71 s / 47 s | mean error 11 per sample (quantisation of noise; garbage is about 85) |
| JXR lossless | 8-bit 46000 x 46000 | 2,293,752,648 | 61 s / 56 s | reloads at its size (flip, as EXR); on Windows x64 with a real image, all rows match |

**GIF frames past 4 GB.** Two 16 x 16 frames after a 5,033,164,800-byte application extension: `FreeImage_Load` of frame 0 and `LockPage` of frame 1 give the right pixels on Linux (both heads), Windows x64 and Windows x86. This is what 05357f7 fixed for 32-bit Windows.

**Windows at c41b50f.** Under Wine, x64 passes `TestAPI/IO`'s `bigfile` (TIFF pages at 2.5 and 4.5 GB through all four openers, the TGA case), `streams` and `memstream` (a 4.5 GB wrapped buffer, the 64-bit memory exports). A lossless 2.29 GB JXR of a real 46000 x 46000 image round-trips row for row, the 2 GB wall 7de5c34 removed. x86 passes `bigfile`, including the WebP stream of 4 GB - 1 bytes refused before it is read, and `streams`. The differential test reads every sample and generated file on Windows x64 as on Linux, except the three J2K/JP2 files whose `SEEK_END`-refused mode hits OpenJPEG's assert in the Linux build (Windows builds define `NDEBUG`).

## worktree-io64 against qpv

`worktree-io64` is qpv plus 30 commits: qpv has nothing the branch lacks. Eight are the range. The other 22 are Resize work, `Makefile.mingw`, reports and DLL drops (1eb0175..748fc04, 00f20cc with its revert b8f7b93), and the RAW/JPEG header-only and preview fixes (eddcb51, 1bdb923, c978dd6, 0ca5064, c41b50f). After b6e3af3 the sources changed are `Makefile.mingw`, comments in `FreeImage.h`, `PluginJPEG.cpp`, `PluginRAW.cpp`, `Resize.cpp/.h` and `Exif.cpp`. None of them touches `read_proc`, `write_proc`, `seek_proc`, `tell_proc` or the memory streams.

| Check | qpv c0ed58c | io64 c41b50f |
|---|---|---|
| TestAPI: testAPI, APNG, AVIF, EXR, HEIF, ICC, J2K, JPEG, JXR, MNG, RAW, WebP | all pass | all pass |
| TestAPI/IO; IO and HEIF under AddressSanitizer | (absent) | pass |
| Differential test, 72 generated files: save results and bytes | identical | identical |
| Differential test, 190 file readings x 7 modes | same fields except: `fi_raw_mono*.dng` now load (c41b50f), and header-only sizes of turned raws (eddcb51); both intended | |
| Same on 748 cut files (every file at 25/50/75/99 %) | same fields except the same RAW header-only fields | |
| `-Wshorten-64-to-32`, 44 changed sources, x86 and x64 | | no new narrowing |
| Findings 1-7, 10, 11, 13 | present | present (6 fails cleanly now) |
| Findings 8-9 | different failure | the regression |
| Finding 12 (callback ABI) | `long` callbacks, 2 GB wall on Windows | `INT64` callbacks, by design |

The cut-file run matters for one change in the range: `_tiffReadProc` used to return all or nothing, `read_proc(buf, size, 1) * size`. It now returns the bytes it got, through `FreeImage_ReadBytes`, and damaged TIFFs are salvaged exactly as before.

## Not exercised

- HDR files over 2 GB: the float image needs three times the file in RAM.
- J2K/JP2 files over 2 GB: OpenJPEG needs 4 bytes per sample.
- TIFF, PSB and EXR past 4 GB on Windows: the code paths are the ones Linux ran.
- MSVC builds (the Windows runs are zig with MinGW-w64 headers), macOS, and a 32-bit Linux runtime (compile-only in the earlier review).
- EXR pixels written with `EXR_FLOAT` and JXR pixels at 2 GB on Linux were not compared, because those writers flip the dib in place (sizes only). JXR pixels were compared on Windows x64.

## Suggested order of fixes

1. TIFF: write BigTIFF (`"w8"`) when a single image's data, or a multi-page document's running size, can pass 4 GB (or behind a flag), and fail the save when `TIFFWriteScanline`, `TIFFWriteDirectory` or `TIFFClose` fails. `Close()` returns `void`, so the last check needs the directory written inside `Save`.
2. Check `write_proc` in the eleven writers of finding 2, and `fclose()` in `FreeImage_Save`/`FreeImage_SaveU`.
3. J2KHelper `_WriteProc`: return `(OPJ_SIZE_T)-1` when the write falls short.
4. EXR: compute the half buffer and its index in `size_t`, and catch `std::bad_alloc` around the save.
5. SGI: read the offset table as `DWORD`, check the seek.
6. `PluginMNG.cpp:294,308`: refuse an embedded image of 4 GB or more; JNG writer: `FreeImage_AcquireMemory64`, and check it.
7. The page cache (`CacheFile` sizes in `INT64`, `FreeImage_AcquireMemory64`), the PSB `dst_buffer_size` (`UINT64`), and the LibRaw limits (define `LIBRAW_MAX_NONDNG_RAW_FILE_SIZE`/`LIBRAW_MAX_DNG_RAW_FILE_SIZE`, set `max_raw_memory_mb`), as far as each is wanted.
8. README: describe the callback break as a source and binary break on Windows, and point AHK U32 users at memory streams.

## Reproducing

The rig is untracked, in the worktree: `.claude/scratch/io64-big/` (README there). `bigio.c` holds the Linux cases, one forked child each: `writefail`, `sgi`, `exrhalf`, `gifofs`, `psb`, `mpcache`, `tiffmp` and the round trips `tiff2g`..`jxr2g`. `wincases.c` holds the Windows ones. The scripts `mkmng.py`/`mng.sh`, `rawbig.sh` and `oldabi.c` cover findings 8, 7 and 12. `win/` holds the zig builds, the Wine runners and the narrowing sweep, `diff/` the differential and cut-file tests, and `logs/` every output quoted here.
