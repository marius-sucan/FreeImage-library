# multipage agent: findings (MultiPage.cpp, CacheFile, Plugin.cpp/.h, FreeImage.cpp), received 2026-10-19
Harness: $J/agents/multipage/t (common.h, build.sh, one .cpp per lead), inputs data/, logs run/. progress.md = checkpoint.

## MP1 FreeImage_OutputMessageProc crashes on a file-controlled format string even with no handler installed
- Class: regression-vs-main (reachability) + new-bug. Severity: Critical. Confidence: high.
- Where: Source/FreeImage/FreeImage.cpp:140 (`if (fmt != NULL)`, was `if (fmt && handler set)`); crash in FreeImage_AppendMessage FreeImage.cpp:130 from the `%s` case ~:221; introduced by 93fbece (debugger mirroring). File text used as the format: PluginXPM.cpp:298/385 ("Unknown color name '%s'" built from file text), PluginEXR.cpp:329/336/375/389/396 (channel names).
- What: main ran the hand-rolled %-formatter only when a handler was installed; qpv always formats (to mirror to OutputDebugString). Plugins pass file text as the format with no varargs, so `%s%s...` in an XPM colour name or EXR channel name reads garbage varargs as char*. On main unreachable without a handler; on qpv every caller.
- Evidence: data/pct_color.xpm, data/pct_1ch.exr, data/pct_4ch.exr; `t/fmtcrash_qpv <f> nohandler` -> SEGV (139) for all three; `t/fmtcrash_main <f> nohandler` -> clean; with a handler main also crashes (pre-existing plugin bug). ASan qpv: SEGV FreeImage_AppendMessage FreeImage.cpp:130 <- FreeImage_OutputMessageProc:221 <- Load PluginEXR.cpp:375.
- Fix sketch: restore the handler guard; gate only the OutputDebugString block; fix plugins to pass "%s", text.
- **Fixed** in bf281d8, as sketched for the plugins, at every call that passed runtime text as the format, not only XPM and EXR: 76 calls (thrown texts, libpng, libjpeg and OpenEXR messages, text from the file), found with FreeImage_OutputMessageProc redeclared printf-like under -Wformat-security. They pass "%s", text; only the FI_MSG_* constants stay formats. Not as sketched for the handler guard: on Windows the mirror needs the formatted text, handler or not, so the guard would not have protected QPV, and on Linux it would hide these crashes from tests run without a handler. The three files and an EXR whose mixed channel types make OpenEXR throw a message naming the channel: exit 139 on ce8e623 with and without a handler; loaded or refused after, the handler getting the text verbatim. testMessageFileText (TestAPI) covers the XPM and both EXR paths.

## MP2 PNG/APNG saves fail for a write callback that returns a byte count (or anything but the item count)
- Class: regression-vs-main. Severity: High. Confidence: high (PNG/APNG reproduced).
- Where: core f84f1da WriteCheckIO (Plugin.cpp:483-505, FreeImageIO.cpp:267-273 `written < count` -> failed); ea1daef makes PluginPNG.cpp:98-103 _WriteProc call write_proc(data,size,1,handle) and png_error when return != 1. main's PNG _WriteProc ignored the return.
- What: write_proc is documented to return the item count, but FreeImage never checked it, so byte-returning callbacks (common; Delphi's TargaImage.pas FI_WriteProc returns bytes) worked everywhere. PNG/APNG now fail the save.
- Evidence: `t/wstyle_{qpv,main}` -> run/wstyle_results.txt: PNG main bytes=1, qpv bytes=0 (fail); APNG qpv bytes=0 (single and multi-page). Items style OK everywhere. PFM/HDR/TARGA/WBMP/GIF newly reject "returns 1"/"swapped" styles (plugin scope).
- Fix sketch: treat a write as complete when return >= count OR == size*count, in WriteCheckIO and PNG/APNG _WriteProc; or document and accept.
- **Fixed** in e67ef7a, in WriteCheckIO alone: a write is complete when write_proc returns count or size * count, and the plugin is then told count, so the PNG check (`!= 1`) passes, and so do the BMP, PSD and XPM ones, which refused byte counts on main too. Any other return fails the save and reads as 0 to the plugin: `>= count` would have let a -1 error code through, which the TGA, ICO, WBMP, GIF, HDR, PFM and ASCII PNM writers took for success on ce8e623, as they did a byte count short on the last write (TGA, ICO, WBMP). Every writable format, single and multi-page: byte counts save everywhere, files identical to the item-count saves; "returns 1" as before; "swapped" now passes in BMP, PNG, PSD, XPM and APNG too, whose writes are one item each; both, and "returns 0", fail elsewhere, since with count > 1 they look like short writes. Still undetectable: a byte-count write cut to exactly count bytes (HDR's last write, 1 byte of a 2-byte item). writefail (TestAPI/IO) has the byte-count and -1 cases.

## MP3 Re-locking a page, or the first LockPage after a save, returns NULL for single-image sequential readers (J2K, JP2, JXR)
- Class: regression-vs-main. Severity: Medium. Confidence: high.
- Where: MultiPage.cpp FreeImage_GetReadData (487-493) opens the decoder once and seeks to header->start only then; LockPage (1270) never re-seeks; introduced by 79b6281. main's LockPage did seek(0)+Open+Close every lock (main MultiPage.cpp:401).
- Evidence: `t/relock2_{qpv,main} pil.j2k pil.jp2 blue_flower.jxr` -> qpv OK NULL NULL, main OK OK OK; `t/relock3` (SaveMultiBitmapToMemory then first lock) -> qpv NULL, main OK for all three. Multi-page formats fine. qpv-asan clean (fails safe).
- Fix sketch: seek the shared handle to header->start before every load_proc in LockPage.
- **Fixed** in 8168afa, not as sketched: a seek is not enough, the J2K, JP2 and JXR decoders are spent after one load (tried: JP2 and JXR still give NULL, J2K aborts in an OpenJPEG assert). For a plugin with no pagecount_proc the page count opens no decoder, and LockPage seeks to the image start and reads it with FreeImage_LoadFromHandle(), a decoder per lock, as main did. Locks 0, 1, 0, last, 0, last, a save to memory, then 0, last, 0, each against a fresh document's page, by name and from memory at offset 777: on ce8e623 the 7 J2K, JP2 and JXR files fail; after, they pass, as do PNG, JPEG, BMP, TGA, JNG, PSD, AVIF and HEIF stills and GIF, TIFF, ICO, APNG, WebP, MNG, JXL and AVIF documents, with and without playback flags; ASan clean. testSingleImageRelock (TestAPI).

## MP4 A document with an unchanged CMYK+alpha page opened with a CMYK/display flag can no longer be saved
- Class: compat-break (intentional, a9262be). Severity: Medium. Confidence: high (mechanism), medium (real callers).
- Where: MultiPage.cpp:732-758 (SavePages reloads unchanged pages with CMYK flag + FIF_LOAD_DISPLAY_ICC, TakeCMYKAlpha, fails save if alpha returns).
- Evidence: data/cmyka.tif (5-sample CMYK+alpha): `t/cmyka_qpv` TIFF_CMYK -> page0 32bpp FIC_CMYK, refused, close -> 0, file still 1 page; flags 0 -> close 1, 2 pages. main's TIFF reader can't open this fixture, so comparison by code path.

## MP5 Cache/spool file names ~28 chars longer than main's: long filenames fail to open (keep_cache_in_memory FALSE) or to save at close (TRUE)
- Class: regression-vs-main. Severity: Low on Linux; closer to Medium on Windows (MAX_PATH 260). Confidence: high.
- Where: MultiPage.cpp:161-172 MakeCompanionName "<fullname>.<pid>.<ptr-hex>.ficache/.fispool"; introduced by e669ea6. main's ReplaceExtension kept the length.
- Evidence: `run/edit_{qpv,main} 18 <235 'n'>.tif 24 {0,1}`: keep=0 qpv "create: NULL", main normal; keep=1 qpv "...fispool, File name too long", close -> 0 (edits lost), main normal.
- Fix sketch: replace the extension and add a short uniquifier, or cap the base.

## MP6 FreeImage_MovePage(target, source) semantics changed
- Class: compat-break (intentional, d79f90e, documented BEHAVIOUR CHANGE). Severity: Low.
- Where: MultiPage.cpp:1420-1475. main moved the page at target to before source; qpv moves source to target.
- Evidence: `t/move_{qpv,main}` on [0,1,2,3,4]: MovePage(0,3) qpv [3,0,1,2,4], main [1,2,0,3,4]; MovePage(0,1) qpv [1,0,2,3,4], main no-op. Both TRUE.

## MP7 Animated PNGs now report FIF_APNG from GetFileType; ValidateFIF(FIF_PNG) rejects them
- Class: compat-break (intentional, 0f9863f). Severity: Low.
- Where: PluginPNG.cpp:348-354 (Validate FALSE for acTL stream) + FIF_APNG=39.
- Evidence: `t/apngdetect_{qpv,main}` Pillow APNGs: qpv GetFileType=39, Validate(PNG)=0; main 13/1. anim_default.png (default image not in animation): Load(GetFileType) -> qpv id 0 (first frame) vs main id 9 (default); Load(FIF_PNG) id 9 on both. GetFIFFromFilename("x.png") = FIF_PNG on both (filename vs content detection disagree for APNGs).

## Checked, no issue found (condensed)
- Plugin struct growth: AddNode memsets before init_proc; old-struct local plugin load/save/multi-page all work, ASan clean (t/localplug).
- Load<->save flag sharing: SavePages strips save-only bits; cache uses fixed flags (GetCacheFlags/GetCacheLoadFlags 366-444); TIFF_LZW as load flag = documented display conversion (t/lzwflag).
- Core WriteCheckIO: fails only when fewer items than count; BigTIFF retry (856-869) correct.
- Format detection: ISOBMFF brands route sensibly across 58 samples (t/ftype).
- CacheFile: 40 pages x interleaved delete/insert/lock-edit, keep TRUE/FALSE: model-exact, ASan/UBSan/LSan clean; full cache disk -> close FALSE cleanly (t/cachestress).
- Edit workflows and BOOL returns (TIFF/GIF/ICO, read-only, memory load/save, FromHandle INT64 IO, close with locked page, NOPIXELS): correct; main crashes on the same TIFF and ICO edit sequence (pre-existing) (t/sessions, t/edit).
- Persistent decoder + playback: composited pixels independent of lock order for GIF/APNG/WebP (t/playorder).
- OutputMessageProc buffers: bounded, NUL-terminated; only defect is MP1. Not thread-safe (nor was main).
- OpenMultiBitmapU / FIFileName (Windows): reviewed by reading, correct; %llx in cache uniquifier could misformat under old msvcrt (filename only).
- Save/SaveU partial-file cleanup (Plugin.cpp:517-565): sound.
## Outside scope (noted by agent)
- main's FIF_JP2 save with a write callback returning 0 loops forever (filled /tmp with 6.4 GB); qpv stops after 77 bytes: pre-existing main bug, fixed in qpv.
- The plugins passing file text as format (XPM, EXR, maybe others) need fixing regardless (codec scope).
- Per-plugin write-return checks rejecting non-count callbacks: PFM/HDR/TARGA/WBMP/GIF in run/wstyle_results.txt.

## Main-session verification
- MP1 VERIFIED 2026-10-19: code diff shows the handler guard removed (`if (fmt != NULL)`); PluginXPM.cpp:298/385 and PluginEXR.cpp:329/336/375/389/396 pass file-derived text as the format. Re-ran agents/multipage/t/fmtcrash_{qpv,main} with no handler: qpv exit 139 (SIGSEGV) on pct_color.xpm, pct_1ch.exr, pct_4ch.exr; main exit 0 on all three.
- QPV relevance: the QPV repo never calls FreeImage_SetOutputMessage(StdCall) (grep finds only the wrapper definitions in lib/freeimage-wrapper.ahk:200-207), so on main QPV never reached the formatter; on qpv a crafted XPM/EXR crashes QPV.
