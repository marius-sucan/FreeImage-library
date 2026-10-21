# io-abi-wrappers agent: findings (I/O layer, public API/ABI, wrappers), received 2026-10-19
Workspace W=$J/agents/io-abi-wrappers (progress.md has a summary; scripts/check_ahk.py, check_consts.py; thr/, items/, tag/, abi/, wrap/, mem/).

| ID | Class | Severity | Title |
|---|---|---|---|
| IO-1 | regression-vs-main | Medium | JPEG 2000 calls the message callback from OpenJPEG worker threads |
| NET-1 | regression-vs-main | Medium (perf) | TIFF/JXR/G3 I/O changed from (n,1) to (1,n): the bundled .NET stream adapter makes one call per byte |
| W-1 | new-bug | Medium | AHK FreeImage_SetTagValue passes a 4-byte temporary ("uint*") |
| IO-2 | compat-break | High (Win32 crash) | FI_SeekProc/FI_TellProc are INT64 (intentional, documented) |
| IO-3 | compat-break | Low | SaveMultiBitmapToMemory refuses a stream that wraps the caller's buffer |
| IO-4 | new-bug | Low | AcquireMemory's new failure path leaves outputs unset; TellMemory returns -1 past 2 GB on Windows |
| API-1 | compat-break | Low | MSVC: typedef long BOOL became typedef int BOOL |
| W-2 | new-bug | Low | AHK wrapper gives 16-bit bitmaps 8-bit masks; BMP saves of them are unreadable |

## IO-1 JPEG 2000 calls the output-message callback from OpenJPEG worker threads
- Where: PluginJ2K.cpp:40-49 (callbacks forward to FreeImage_OutputMessageProc), :160-161, :172 (opj_codec_set_threads on decode); PluginJP2.cpp:46-54, :199-200, :211; J2KHelper.cpp:138-168 (>= 2 threads once a tile holds >= 4 KiB); 03c89f4 "decode and encode on every core". Encode also sets threads (PluginJ2K.cpp:292, PluginJP2.cpp:346; not tested whether encode jobs emit messages).
- What: OpenJPEG emits code-block warnings/errors inside pool jobs (t1.c opj_t1_clbl_decode_processor / opj_t1_decode_cblk); the user's SetOutputMessage(StdCall) callback now runs on OpenJPEG threads (serialised by OpenJPEG's mutex, but not on the caller's thread). main never set threads. QPV receives messages via an AutoHotkey v1 RegisterCallback (wrapper L200-203); AutoHotkey runs script code on its main thread only (the wrapper itself warns "JPEG 2000 may call it from a worker thread").
- Evidence: W/thr/rgn31.j2k (512x512 Pillow J2K with an RGN marker SPrgn=31 before SOT): `W/thr/one_qpv rgn31.j2k` prints `opj_t1_decode_cblk(): unsupported bpno_plus_one = 40 >= 31` from 4 pool thread ids, then "Failed to decode..." 3x on the caller's thread; summary "callbacks on the caller's thread 3, on other threads 4". `one_main`: loaded yes, 0/0.
- Confidence: high (foreign-thread callbacks); medium on how often real files warn at code-block level.
- Fix sketch: queue messages under a mutex in the j2k/jp2 callbacks, emit them on the calling thread after opj_decode/opj_encode returns.
- **Fixed** in e171cc7, not as sketched: the handlers, now in J2KHelper for both plugins, pass on what OpenJPEG raises on the calling thread and only count what it raises on its own threads, whose text is dropped. When decoding ends, one message stands for those: "Errors have occurred decoding the image" if the decoding failed (rgn31.j2k: its bpno warnings fail it), "Warning: problems have occurred decoding the image" if it went through. rgn31.j2k: callbacks on the caller's thread 4, on other threads 0 (3 and 3 to 4 before, run to run); as JP2 5/0 (4/5); 300 corrupted copies: 0 on other threads (802 before), one summary each. Saves use the same handlers; OpenJPEG 2.5.4's encoding jobs raise no messages. The AHK wrapper's worker-thread note is gone.
- Note: rgn31.j2k loads in main but fails in qpv (OpenJPEG 2.5.4's bpno limit) - an upstream limit, flagged outside scope.

## NET-1 TIFF, JXR and G3 I/O became one item per byte; the .NET stream adapter now makes one managed call per byte
- Where: Utilities.h FreeImage_ReadBytes/WriteBytes call read_proc(buf, 1, n); call sites PluginTIFF.cpp:152-160 (main read_proc(buf, size, 1)), PluginJXR.cpp:51/62 (main (cb,1)), PluginG3.cpp:99 (main (n,1)); 7de5c34. Affected: Wrapper/FreeImage.NET/cs/Library/Classes/FreeImageStreamIO.cs:80-133 (loops per item).
- Evidence (W/items/items_{main,qpv}, sum of count = C# loop iterations): 8.9 MB LZW TIFF load 157 -> 8,930,502; save 1,509 -> 1,342,564; uncompressed TIFF load 6 -> 9,000,140; save 1,509 -> 9,009,250; G3 TIFF load 12 -> 1,070,259; JXR 9.7 MB load 3,793 -> 18,928,996; save 3,824 -> 14,916,128. J2K/MNG/WebP already (1,n) in main; PNM improves. Wall time not measured.
- Fix sketch: make C# streamRead/streamWrite move size*count bytes in one Stream call, or issue (chunk, 1) from ReadBytes/WriteBytes.

## W-1 AHK FreeImage_SetTagValue passes the value as "uint*": tags longer than 4 bytes get AutoHotkey's scratch memory
- Where: Wrapper/AHK/freeimage-wrapper.ahk:1724-1727 (unchanged since d989f51). FreeImage_SetTagValue copies tag->length bytes (FreeImageTag.cpp:264-303).
- What: DllCall "uint*" passes the address of AutoHotkey's per-argument scratch slot (8-byte union + internal fields). RATIONAL gets numerator=value, denominator=0; ASCII/multi-value/> 8-byte tags get internal bytes; strings cannot be passed (become 0).
- Evidence: W/tag/tag.c emulating the pointer: `RATIONAL XResolution, value 72: length 8, bytes: 48 00 00 00 00 00 00 00 -> "0"`; `ASCII Artist, 12 bytes: 00 x12 -> ""`.
- Fix sketch: pass "UPtr", pValue (buffer filled with VarSetCapacity/NumPut or StrPut).

## IO-2 FI_SeekProc/FI_TellProc take/return INT64
- Class: compat-break (intentional, 7de5c34; README 2196cb3). Severity: High for 32-bit Windows binaries.
- Where: FreeImage.h:598-599; FreeImageIO.cpp:66-73.
- What: Win32 stdcall: an old seek callback pops 12 of 16 bytes (stack corruption), reads origin from the offset's high dword (SEEK_END arrives as SEEK_SET); old external plugins calling FreeImage's seek unbalance the stack too. Win64: old callbacks fine below 2 GB, but an old tell's -1 is zero-extended to 4294967295, and an old external plugin's backward seek (-4, SEEK_CUR) reaches FreeImage as +4294967292 (not in README). 32-bit AutoHotkey tell callbacks cannot return 64 bits (wrapper L335). 32-bit POSIX: origin read from high dword, tell's high dword garbage; SONAME unchanged. LP64 Linux unaffected.
- Evidence: zig + wine: W/abi/abi64.exe "old tell returning -1 is read as 4294967295"; "old plugin seeking (-4, SEEK_CUR)... FreeImage sees 4294967292"; W/abi/abi32.exe "seeks (100, SEEK_END) via an old callback: it sees offset=100 origin=0", then "wine: Unhandled page fault on execute access".
- Fix sketch: keep; add the Win64 external-plugin case to README; consider a new SONAME for 32-bit POSIX.

## IO-3 SaveMultiBitmapToMemory refuses a stream that wraps the caller's buffer
- Class: compat-break (intentional, 8e64e77). Severity: Low.
- Where: MultiPage.cpp:1582-1598. main wrote into FreeImage_OpenMemory(buf, size) streams: GIF worked while output fit; TIFF realloc()ed the caller's memory (UB). qpv refuses all, matching SaveToMemory in main.
- Evidence: W/wrap/wrap.c vs ASan builds: main GIF -> 1 (TellMemory 1791, reloads 2 pages); main TIFF -> ASan "attempting free on address which was not malloc()-ed ... _MemoryWriteProc FreeImageIO.cpp:134 ... 'user_buf'"; qpv GIF and TIFF "Memory buffer is read only" -> 0.

## IO-4 AcquireMemory's new failure path leaves *data and *size unset; TellMemory returns -1 past 2 GB on Windows
- Class: new-bug. Severity: Low.
- Where: MemoryIO.cpp:130-145, :195-198; 6b7462e. On Win64 streams can now exceed 2/4 GB (main refused writes past 0x7FFFFFFF); FreeImage_TellMemory returns -1, FreeImage_AcquireMemory returns FALSE above 4 GB without writing outputs.
- Evidence: W/abi/memwin_x86_64.exe (real qpv FreeImageIO.cpp + MemoryIO.cpp cross-built, run under wine): "wrap a 5 GB buffer ... AcquireMemory -> 0, data=0000000000001234 size=3735928559 (outputs left as the caller had them)"; "SeekMemory64(3 GB) -> 1; TellMemory -> -1; TellMemory64 -> 3221225472".
- Fix sketch: set *data = NULL and *size = 0 before returning FALSE.

## API-1 MSVC: typedef long BOOL became typedef int BOOL
- Class: compat-break (71dbf21). Severity: Low.
- What: matters only for MSVC/clang-cl code including FreeImage.h before <windows.h>. Values unchanged, exports extern "C", no BOOL* in FreeImage.h. Third-party C++ functions with BOOL in their signatures change mangling (J -> H); int/BOOL overloads collide. Fixes the typedef-redefinition error with <minwindef.h> after FreeImage.h. FreeImagePlus includes windows.h first (unchanged).
- Evidence: W/abi/booltest.cpp with zig clang -cc1 -triple x86_64-pc-windows-msvc19.29.0: main ?takes_bool@@YAXJ@Z, qpv ?takes_bool@@YAXH@Z; with a later typedef int BOOL main errors, qpv compiles.

## W-2 AHK wrapper: 16-bit bitmaps get 8-bit-per-channel masks; BMP saves of them unreadable
- Class: new-bug (qpv-only wrapper). Severity: Low.
- Where: freeimage-wrapper.ahk:2268-2294 ConvertPBITMAPtoFIM(pBitmap, 2) passes 0x00FF0000/0x0000FF00/0x000000FF for PixelFormat16bppRGB555; :237, :243 FreeImage_Allocate/AllocateEx default masks 0xFF000000/0x00FF0000/0x0000FF00 (ignored at 24/32, stored at 16 bits; BitmapAccess.cpp:324-337).
- Evidence: W/tag/m16.c (identical on qpv and main): masks stored; FreeImage reads pixel 0 correctly (falls back to 555), but the BMP saver writes the BI_BITFIELDS masks: Pillow "Unsupported BMP bitfields layout".
- Fix sketch: pass 0x7C00/0x03E0/0x001F on the 555 path; default Allocate masks to 0.

## Flag-collision table (summary)
- global: FIF_LOAD_NOPIXELS 0x8000 = TIFF_JPEG/JPEG_SUBSAMPLING_422 (pre-existing); FIF_LOAD_DISPLAY_ICC 0x4000 = TIFF_LZW/JPEG_SUBSAMPLING_420 (only reachable by passing a save flag to a load); DisplayICCLoadFlags strips 0x4000 except TIFF/PSD/JXL/JXR and adds the format's CMYK flag.
- MNG flags reach the JPEG decoder for JNG frames (MNG_PLAYBACK 2 = JPEG_ACCURATE, PNG_IGNOREGAMMA 1 = JPEG_FAST): harmless (see mng-webp F6).
- WEBP_PLAYBACK passed to a save = quality 1; JXL_PLAYBACK/JXL_CMYK passed to a save = quality 2/4; J2K UNSCALED passed to a save = rate 1 (lossless); APNG PLAYBACK/LINEAR_BLEND passed to a save = zlib level 2/4.
- RAW flags + JPEG_EXIFROTATE go to the embedded preview's loader (as in main): HALFSIZE = JPEG_CMYK; a JXL/HEIF preview would read DISPLAY as PLAYBACK and HALFSIZE as JXL_CMYK.
- PNM: `flags == PNM_SAVE_RAW` exact comparison (pre-existing).
- Result: no documented flag combination behaves differently from main because of a collision.

## Checked, no issue found (condensed)
- Memory I/O: 64-bit arithmetic, growth to FI_MEMORY_MAX, seek limits, partial-item reads, item-count returns of default procs; seek gaps now zeroed (main left 4088 uninitialised bytes, W/mem/mem.c); seeks past 2 GB on LP64; *Memory64 exports; OpenMemory64 address-space check; Win32 wrapped buffers >= 2 GB (main's int file_length went negative).
- ReadBytes/WriteBytes chunking correct; INT64 positions consistent in Plugin.cpp, MultiPage.cpp, J2KHelper, plugins.
- ABI/exports: no FreeImage_* export removed, 19 added; 133 libwebp symbols no longer leak; SONAME unchanged; prototypes only AppendPage/InsertPage/DeletePage/UnlockPage void -> BOOL (ABI-compatible); no default-argument changes; Plugin struct zeroed before init_proc; FILTER_NEAREST = -1 fine (signed types in .NET/VB6/Delphi).
- FreeImagePlus compiles against both headers. .NET delegates/stream procs/LocalPlugin 64-bit consistent; C# streamWrite returns count. Delphi Int64 OK; VB6 no seek/tell callbacks.
- AHK wrapper: all 227 DllCalls match prototypes (arg count/types, Ptr/Int64/WStr/AStr, return types, Cdecl only for variadic OutputMessageProc); all 268 x86 @N decorations match (check_ahk.py caught 7/8 mutants); all 229 constants match (check_consts.py); struct offsets right (FIICCPROFILE size@4 data@8; RGBQUAD BGRA; MONITORINFOEXW); call patterns right (GetICCProfileDescription two-call sizing, GetDisplayICCProfile, GetLockedPageNumbers); docs for JXL_CMYK/JXR_CMYK alpha correct; UnlockPage FALSE paths documented. Doc nits: L1260 lists "CMYK JPEG, TIFF and PSD" (omits JXL/JXR); L1822 FrameTime list omits JXL; L331/332 repeat the pIO line.
- Examples/AHK/ColorManagement.ahk DllCalls/offsets right. OutputMessageProc debugger mirror bounded. Save/SaveU deleting a partial file after failure: not a regression.
## Outside scope (agent notes)
- W/thr/rgn31.j2k (ROI shift 31) loads in main, fails in qpv (OpenJPEG 2.5.4 limit "unsupported bpno_plus_one >= 31"); real 16-bit ROI files could hit it.
- Legacy Managed C++ .NET adapter (FreeImageIO.Net.cpp) transfers only `size` bytes per call -> 1 byte per call with (1,n) requests (pre-existing; its __gc syntax no longer compiles).
- RegisterExternalPlugin / .fip search look up "_Init@8" on x64 too (pre-existing).
