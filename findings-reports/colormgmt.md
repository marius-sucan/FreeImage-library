# colormgmt: ICC colour management review (qpv f1a2a67 vs main a976f39)

Scope:
- Source/FreeImageToolkit/ColorManagement.cpp and every export it implements.
- The FIF_LOAD_DISPLAY_ICC load path: Plugin.cpp, MultiPage.cpp, DisplayICCLoadFlags and ConvertToDisplayICC.
- CICP profiles.
- Source/LibLCMS2 2.19.1 and its `// FreeImage:` patches.

Main has no colour management, so every finding here is new-bug, not a regression.
Workspace: /home/marius/.claude/jobs/d912fdd7/tmp/agents/colormgmt (src/ test sources, bin/ binaries, out/ logs, icc/ profiles).

### CM-1 FIF_LOAD_DISPLAY_ICC's shared transform cache frees memory through dead Little CMS contexts: data race / use-after-free across threads
- Class: new-bug (thread-safety, memory safety)
- Severity: High
- Where: Source/FreeImageToolkit/ColorManagement.cpp
  - :668 TransformCache::Get builds the cached transform in the cache's own context, from profiles opened in the
    converter's per-call Context (:574).
  - :690 LRU eviction deletes transforms.
  - :643-647 ~TransformCache runs when the DisplayState is replaced.
  - :565-567 ~Context deletes the per-call context after every conversion.
  - :2186 ToDisplay uses the cache.
  - Introduced by f2fa4c2 "Color management: FIF_LOAD_DISPLAY_ICC reuses its transforms".
- What:
  - Little CMS keeps the source object's context in everything it duplicates into a pipeline: cmsDupToneCurve and
    cmsStageDup copy `ContextID` (cmsgamma.c:978, cmslut.c:1268). A cached transform's stages and curves therefore belong
    to the ColorConverter's context, which is deleted as soon as that conversion returns, while the transform stays in
    the cache (8 per display state, shared by all threads).
  - The transform is deleted later, either by eviction after 8 profile/format pairs or when the display state is
    replaced by FreeImage_SetDisplayICCProfile or a system profile change. At that point cmsFreeToneCurve and
    cmsStageFree call _cmsFree(deadContextID).
  - Little CMS looks the dead pointer up in its context pool. FreeImage creates and deletes contexts on every
    conversion, so the lookup typically finds another thread's live context at the recycled address. It then reads that
    context's memory-plugin pointer and FreePtr, after releasing the pool lock, while the owner may be deleting it.
  - On a single thread the stale ID either matches nothing (only a pointer compare, global context) or a live context
    with the same default allocator, so the misrouted free is harmless. That is why ASan never fires; the exposure is
    multithreaded hosts. main supports those: FreeImage is usable from several threads on independent bitmaps.
  - Windows, QPV's target, uses the same pool logic under a CRITICAL_SECTION.
- Trigger: two or more threads loading with FIF_LOAD_DISPLAY_ICC, or calling FreeImage_ApplyDisplayICCProfile, on
  images with more than 8 different ICC profiles, or while another thread calls FreeImage_SetDisplayICCProfile.
  16-bit and grey images are converted with NOOPTIMIZE, which keeps the profiles' own curves in the transform.
- Evidence:
  - ThreadSanitizer build: ColorManagement.o and LibLCMS2 compiled with -fsanitize=thread and swapped into a copy of
    qpv-rel/libfreeimage.a (tsan/libfreeimage_tsan.a).
  - `bin/t_evict_tsan 400 4 <16 colord RGB profiles>`, eviction only, fixed Display P3 display: "ThreadSanitizer:
    reported 9 warnings" (out/evict_tsan2.txt). Decisive report: "Write of size 8 by thread T3: free <-
    _cmsFreeDefaultFn <- _cmsFree <- cmsDeleteContext <- ~Context <- ~ColorConverter <- ToDisplay". Against it: "Previous
    read of size 8 by thread T4 (mutexes: write M0): _cmsFree cmserr.c:340 <- cmsFreeToneCurve <- ... <-
    cmsDeleteTransform <- TransformCache::Get ColorManagement.cpp:690". T4 reads the FreePtr it is about to call from
    a context block that T3 is freeing.
  - `bin/t_threads_tsan 60 4 ...`, with the display profile switching: races in _cmsContextGetClientChunk <-
    cmsDeleteTransform <- ~TransformCache <- ~DisplayState, on a 192-byte context block "allocated by thread T3 in
    cmsCreateContext <- IsPQEncoded" that was created long after the transform (out/threads_tsan.txt).
  - Deterministic single-thread proof, `bin/t_stalectx`:
    - one FreeImage_ApplyDisplayICCProfile of an RGB16 Adobe RGB image;
    - 64 contexts created with a counting memory plugin;
    - FreeImage_SetDisplayICCProfile(NULL), which deletes the cached transform.
    - Result: "frees routed through the new contexts while FreeImage deleted its cached transform: 42 (they allocated
      192 blocks)". The control run, without the conversion, gives 0.
  - Pixels stay right under the same stress in the rel build: bin/t_threads, 1200 loads, 711 display switches,
    0 mismatches. The bug is the race, not wrong colours.
  - ASan with quarantine_size_mb=0 did not hit the interleaving in 3600 loads.
  - main: no such path.
- Confidence: high on the mechanism and the race. An actual crash needs the freed context block reused by another
  allocation before the read, so it is rare.
- Fix sketch: make every object a cached transform owns live in a context that outlives it. Either re-open the two
  profiles from their bytes with cmsOpenProfileFromMemTHR(m_context, ...) inside TransformCache::Get before
  cmsCreateTransformTHR, or give the ColorConverter used for cached conversions the cache's context. Delete that
  context only after its last transform.
- **Fixed** in 5c6d01e, by the first sketch: TransformCache::Get opens both profiles again from their bytes in the
  cache's own context, which the cache deletes after its transforms (a converter using one holds the display state).
  - The trigger was wider than written: Little CMS's 8-bit matrix-shaper optimisation keeps cmsStageDup copies of the
    profiles' curves too, so every display conversion but 8-bit CMYK was exposed. After one conversion, 64 contexts
    made with a counting memory plugin took 42 frees (24/32-bit RGB, RGB16, RGBA16, palettes, untagged), 21 (grey)
    or 58 (CMYK16) meant for the deleted context; none now, in all 11 layouts.
  - ThreadSanitizer, eviction stress (4 threads x 400 loads of 16-bit PNGs in 16 colord profiles): 5 to 18 races per
    run in 4 runs before, none in 6 runs now. Display switching, 4 threads x 20 loads (60 did not finish under TSan in
    25 minutes): 1 race before, none now, every load matching the single-threaded one. That run needs
    OMP_NUM_THREADS=1, or libgomp, which is not instrumented, floods it with false positives.
  - TestAPI/ICC display checks those frees for 24-bit, RGB16 and grey images (it fails before; under asan-run it
    cannot fail, since ASan's quarantine keeps the blocks from being reused).
  - The cache's context reports Little CMS's messages as the converters' do: the tag errors read while linking stay
    reported, and a display conversion that cannot be linked also gives "Couldn't link the profiles", as
    FreeImage_ConvertToICCProfile does.

### CM-2 The near-sRGB shortcut takes profiles 9-10 8-bit steps away from sRGB as sRGB, and ignores the display's intent and black-point compensation
- Class: new-bug
- Severity: Low (narrow trigger)
- Where: ColorManagement.cpp:1769-1797 (Equivalent: a 17-step grid, INTENT_RELATIVE_COLORIMETRIC, no BPC). Used for:
  - the display profile (BuildDisplayState :2049);
  - embedded profiles (ToDisplay :2181, TwinCache);
  - multi-frame saves (ConvertToFileProfile :2514).
  - Introduced by d63a45b (display profile) and a8a3be1 (embedded profiles; its message says "within two 8-bit steps").
- What:
  - The grid samples device values 0, 16, 32 ... 255 only, so nothing between 0 and 16/255 is checked, which is where
    profiles with a raised black or a different toe differ most.
  - The check always uses relative colorimetric without BPC, while the conversion it replaces uses the display flags:
    RELATIVE | BPC by default, and FreeImage_SetDisplayICCProfile accepts any intent.
  - A passing profile is replaced by FreeImage's sRGB, so FIF_LOAD_DISPLAY_ICC skips a conversion that differs by far
    more than the two steps intended.
- Trigger: a display or embedded profile with sRGB primaries, gamma 2.2 and black at 0.28-0.34 % of white (a ~300:1
  display profiled without BPC).
- Evidence: src/mkprof.py makes the profiles; bin/t_display compares FreeImage_ApplyDisplayICCProfile (the load path's
  conversion) with FreeImage_ConvertToICCProfile to the same profile and flags.
  - Display g22_k0.003, REL|BPC: "SetDisplay=1 display=BUILTIN-sRGB"; "max diff 9.00 (8-bit steps) ... in 5.0 display
    5.0 reference 14.0; samples >2 steps 11087 of 49920". The grey ramp 1..16 is left as is; the reference gives
    4,7,11,...,23.
  - Same display, REL without BPC: max diff 9. The reference clips 1..9 to 0; the shortcut keeps them.
  - The same profile embedded, on a Display P3 display: max diff 9 (REL|BPC) and 10 (REL).
  - 16-bit RGB on the g22_k0.003 display, REL|BPC: max diff 8.55 steps.
  - Black offsets 0.002 and 0.004 are not taken as sRGB, so the window is narrow.
  - colord's sRGB/Rec709 profiles, the machine's EDID profile and ghostscript's srgb behave as intended. colord's
    Gamma5000K/5500K/6500K and Bluish are taken as sRGB; that is right under relative colorimetric, but an
    absolute-colorimetric display setting is then silently not applied.
- Confidence: high
- Fix sketch: run Equivalent with the display's intent and BPC flag, and sample the shadows densely (a fine ramp
  0..32/255 per channel and on the grey axis, plus the 17^3 grid).

### CM-3 Out of memory leaks Little CMS profiles, transforms and buffers in the display paths
- Class: new-bug
- Severity: Medium (leaks; 066f7f8 and f5081b1 set out to make colour management memory-safe when memory runs out)
- Where: ColorManagement.cpp
  - :2032-2064 BuildDisplayState: raw `srgb`/`profile` handles, while Equivalent, `state->rgb = bytes` and
    DisplayGrey can throw.
  - :1773-1789 Equivalent: the transform is live while two std::vectors are allocated.
  - :1981-2020 DisplayGrey: the transform, and `data`/`profile` around grey.assign.
  - :1839-1858 TwinCache::Check.
  - :1916-1947 MakeCICPProfile: std::string and assign while `curve`/`profile` are live.
  - :1356-1369 ColorConverter::Apply: the profile `copy` is malloc'ed before Run(), which can throw from
    TransformCache::Get's vector and shared_ptr allocations.
  - Introduced by d63a45b, a8a3be1, fdecce1 and f2fa4c2.
- What: these functions mix raw Little CMS handles and malloc'ed buffers with std::vector/std::string allocations.
  When one of those throws std::bad_alloc, the public function catches it and returns FALSE or NULL, which is right,
  but the handles and buffers in flight are never freed. ~Context also deletes contexts that still have objects
  allocated in them.
- Trigger: allocation failure during FreeImage_SetDisplayICCProfile, FreeImage_ApplyDisplayICCProfile or a
  FIF_LOAD_DISPLAY_ICC load.
- Evidence: bin/t_oom_asan fails the n-th malloc/calloc/realloc/operator new inside one API call, for n = 1, 2, ...,
  under ASan + LSan (logs out/oom_*.txt).
  - op 6, SetDisplayICCProfile: "509312 byte(s) leaked in 369 allocation(s)". Among them cmsOpenProfileFromMemTHR <-
    Open <- BuildDisplayState:2041/2042; AllocEmptyTransform <- Equivalent:1773 and <- DisplayGrey:1982; 8556 bytes at
    DisplayGrey:2017.
  - op 7, ApplyDisplayICCProfile, and op 8, PNG load with FIF_LOAD_DISPLAY_ICC: "2368 byte(s) leaked in 4
    allocation(s)", malloc <- ToDisplay:2196, i.e. Apply's copy of the 592-byte display profile.
  - main: no such code.
- Confidence: high
- Fix sketch: hold Little CMS handles and malloc'ed buffers in RAII guards (unique_ptr with cmsCloseProfile,
  cmsDeleteTransform or free as deleter) in these functions. In Apply, allocate `copy` after the transform exists, or
  guard it.
- **Fixed** in 0ecfb4d, by the sketch: unique_ptr owners (deleter structs) hold every profile, transform, tone curve,
  malloc() block and bitmap of these functions, and each is freed before the Context it was made in. Apply's `copy`
  is guarded, still made before the pixels change.
  - Two more sites of the kind, both reproduced: ColorConverter::Convert left the whole new image of a CMYK or
    555/565 display load when the transform cache's bookkeeping threw, and the X11 query left libxcb's two replies
    and its connection when the copy of the profile threw.
  - The Windows query got the same owners for its information context (across two vector allocations) and the
    profile file (across its read buffer, up to 16 MB). Compiled with MSVC 14.29 x64 at /W4 without warnings, and
    with zig for x86 and x64 MinGW with HEAD's 4 warnings; not run.
  - Sweeps failing every allocation in turn, one child each, LeakSanitizer asked for unreachable blocks: setting
    the display (Adobe RGB, colord's sRGB twin), FreeImage_ApplyDisplayICCProfile (RGB16, 24-bit, grey, untagged),
    FIF_LOAD_DISPLAY_ICC loads of a PNG, a CMYK TIFF and a CMYK PSD, two CICP profiles and the X11 query (xcbstub):
    2 to 8 leaking allocation points in each of the 12 before, none now, no crash.
  - TestAPI/IO allocfail counts blocks for the display setter and the display loads now, a CMYK TIFF load added;
    all three sweeps fail before.
  - Left, out of memory but no leak, a failure that outlasts it: a CICP profile that cannot be made is cached empty,
    so its code points get no profile until the process ends; a display state whose TransformCache got no context
    converts nothing (RGB stays as loaded, CMYK and 555/565 loads give NULL), a pinned one until the next
    FreeImage_SetDisplayICCProfile; a system display profile that cannot be opened for want of memory gives an sRGB
    state, kept until the system reports another profile.

### CM-4 Converting an image with alpha to a grey profile silently drops the alpha
- Class: new-bug (documented behaviour not met)
- Severity: Low (explicit grey-destination conversions only; the display path keeps alpha)
- Where: ColorManagement.cpp:233-237 (OutputLayout, MODEL_GRAY: GRAY8 / FIT_UINT16 / FIT_FLOAT whatever px.alpha says);
  introduced by 3d8bdf5.
- What:
  - README promises "results keep the precision of the source (8-bit, 16-bit or float) and its alpha channel, except
    in CMYK, which has none".
  - FreeImage_ConvertToICCProfile of a 32-bit RGBA, RGBA16 or RGBAF image to a grey profile returns 8-bit grey,
    FIT_UINT16 or FIT_FLOAT, with no alpha and no message.
  - A palette with a transparency table keeps its transparency in the same conversion, so the behaviour is also
    inconsistent.
- Trigger: FreeImage_ConvertToICCProfile(rgba, sgray.icc, size, FICMS_INTENT_RELATIVE_COLORIMETRIC).
- Evidence:
  - bin/t_conv: "rgba8 -> sgray: RESULT type=1 bpp=8 ct=1", "rgba16 -> sgray: type=2 bpp=16", "rgbaf -> sgray:
    type=6 bpp=32".
  - bin/t_types: an 8-bit palette with tRNS converted to sgray stays "transparent=1"; the same image expanded to
    32 bits first loses its alpha (displayed alpha differs by 254).
- Confidence: high
- Fix sketch: for sources with alpha, write the grey into an RGBA result (R=G=B, alpha copied) or refuse with a
  message; at least document it.
- **Fixed** in ff4ddfd, by the sketch's first way: the result keeps the source's layout (32-bit RGBA, RGBA16 or
  RGBAF), red, green and blue the grey, the alpha the source's, as FreeImage's PNG, PSD and TIFF loaders store grey
  with alpha.
  - Little CMS writes the grey into a row buffer, which red, green and blue then take. FreeImage_ApplyICCProfile, which
    refused the conversion for changing the pixel format, now does it in place.
  - The greys are those of the old grey results, byte for byte: 8-bit, 16-bit and float, to sRGB-curve, gamma 1.8 and
    linear grey, on 1 and 8 threads.
  - TestAPI/ICC convert expects the RGBA layout, red, green and blue equal and the alpha kept, in place too, and an RGBA
    ramp within a step of the tone curves: 352 failures on the old library, none now. Four planted mutants (alpha
    dropped, red only, in place refused, the grey written over the row) each fail it. TestAPI/IO allocfail fails each of
    the 701 allocations of those conversions in turn: the RGBA result, NULL, or FALSE and the image as it was, and
    nothing left allocated.
  - Left, and listed in the README: the result carries the grey profile, as every explicit destination does, and
    FreeImage's own conversions take an RGBA image with a grey profile as sRGB, as they take a grey+alpha PNG's
    (differential.md D2, open). Writers treat it as they treat that PNG's: PNG, JP2 and BMP leave the profile out with a
    warning, JXL without one, TIFF, PSD, JXR and still WebP write it into an RGB file.
  - Seen on the way, not changed, on every conversion path at HEAD: an embedded profile Little CMS cannot open for want
    of memory is reported as invalid ("ICC: the embedded profile is invalid and is ignored") and the conversion runs
    from the type's default, returning a result tagged with the destination.

### CM-5 Header-only loads with FIF_LOAD_DISPLAY_ICC describe another image than the full load with the same flags
- Class: new-bug (FIF_LOAD_NOPIXELS disagrees with the full load)
- Severity: Low (FreeImage.h says the flag is "ignored with FIF_LOAD_NOPIXELS", so the header reports the file's own format)
- Where: ColorManagement.cpp:2431-2433 (DisplayICCLoadFlags drops the flag for NOPIXELS), :2454 (ConvertToDisplayICC
  skips header-only bitmaps), :2169 (555/565 are always replaced by 24-bit). Introduced by d63a45b.
- What:
  - A header-only load should describe what the full load returns.
  - With FIF_LOAD_DISPLAY_ICC, 16-bit 555/565 images (BMP, and any loader returning 555/565) report 16 bpp while the
    full load returns 24 bpp.
  - Every converted image reports the file's ICC profile, while the full load carries the display profile.
- Trigger: FreeImage_Load(FIF_BMP, "555.bmp", FIF_LOAD_DISPLAY_ICC | FIF_LOAD_NOPIXELS), compared with the same call
  without NOPIXELS.
- Evidence: bin/t_loadflag, with Display P3 as the display:
  - "555.bmp  H type=1 bpp=16 ... F type=1 bpp=24 ... !! header-only and full load disagree"; 565 the same.
  - "adobe.png H ... 'Compatible with Adobe RGB (1998)'  F ... 'Display P3 (FreeImage)'".
  - CMYK JPEG/TIFF/PSD, palettes, grey and 16-bit agree on type and bpp.
  - main: no such flag.
- Confidence: high
- Fix sketch: with NOPIXELS, give 555/565 images the 24-bit header the conversion would produce, or state in
  FreeImage.h/README that the header reports the file's format and profile.

**Outside my scope:**
- LibTIFF out of memory: TIFFGetField(TIFFTAG_INKSET) dereferences NULL in _TIFFVGetField (tif_dir.c:1524) after a
  failed directory allocation, reached from PluginTIFF.cpp:1905 (bin/t_oom_asan op 9: SEGV). For the TIFF agent.
- AttachCICPProfile now runs on every load (Plugin.cpp:415, MultiPage.cpp:1276 and :761), so CICP-only files get a
  synthesised ICC profile, which savers may then write. Plugin agents.
- Little CMS upstream, benign or fuzz-only:
  - UBSan signed overflow in interpolation (cmsintrp.c 606/607/831/845/958/1022, cmsopt.c:1012) on fuzzed LUT profiles;
  - data races on its own statics (cmsD50_xyY's static xyY; _cmsGetTime's gmtime, without HAVE_GMTIME_R on Linux).

**Checked, no issue found:**
- Built-in profiles (sRGB, linear sRGB, grey, linear grey, Adobe RGB 563/256, Display P3, ProPhoto with D50 and 1.8):
  - v4.4, D50 PCS, Bradford chad; colorants and TRCs match the standards; fixed date bytes.
  - FreeImage_GetBuiltInICCProfile(-1) and (7) return NULL with size 0; a NULL size pointer is fine.
- Conversions against independent oracles:
  - 8-bit RGB is identical to Pillow 12.1.1 ImageCms with default flags.
  - RGB16, RGBA16, RGBF and RGBAF to Adobe RGB, ProPhoto, WideGamut and Rec709, intents 0-3 with and without BPC: exact
    against system lcms 2.17 with NOOPTIMIZE, alpha copied.
  - Grey 8/16/float to and from grey and RGB profiles, RGB to grey, RGB16 to CMYK16, grey to CMYK, CMYK16 to grey: exact.
  - 8-bit CMYK and CMYK16 to RGB16: FreeImage equals the vendored lcms 2.19.1 bit for bit (bin/vlcms). The gap to lcms
    2.17 is a library-version difference, not integration.
- 16-bit min-is-white (Exif PhotometricInterpretation 0) to grey and to RGB: exact against TYPE_GRAY_16_REV.
- Palettes and 555/565 (bin/t_types):
  - 1/4/8-bit palettes (colour, grey, min-is-white, with transparency tables) and 16-bit 555/565 equal the conversion
    of the image expanded to 24/32 bits first (max diff 0-1).
  - Transparency tables are kept.
  - ApplyICCProfile refuses 555/565 and grey to RGB with a message.
  - In-place ApplyICCProfile keeps 8-bit, 16-bit and float alpha bytes unchanged and matches the oracle; CMYK becomes
    RGBA with opaque alpha.
- FreeImage_CreateView views: Convert and Apply equal the converted crop, and pixels outside the view are untouched.
  Header-only bitmaps are refused by every export.
- FIF_LOAD_DISPLAY_ICC on real files, sRGB and Display P3 displays:
  - Files: CMYK JPEG with and without profile; CMYK TIFF 8/16-bit; PSD CMYK 8/16 with and without alpha; RGB, RGBA and
    16-bit PNG/TIFF/JPEG; grey 8/16; palette with tRNS; PSD grey+alpha; float EXR (unchanged); PQ PNG (samples and CICP
    kept); HLG and P3 cICP PNGs.
  - Type, bpp and alpha are as documented.
  - The result equals FreeImage_ApplyDisplayICCProfile on the image loaded without the flag (max diff 0) for every
    image that is not CMYK or 555/565. Those two return FALSE, as the AHK wrapper documents.
- Robustness: 360 iterations of mutated profiles (truncation; damage to the header, the tag table and random bytes)
  through every export, as destination, embedded, proofing and display profile, under ASan + UBSan + LSan. No error in
  FreeImage code.
- Buffer sizes:
  - FreeImage_GetICCProfileDescription with buffer sizes 0..16 cuts on UTF-8 character boundaries, always
    NUL-terminates and returns the needed size; NULL or too-short profiles return 0.
  - FreeImage_GetDisplayICCProfile with a short buffer writes nothing and returns the size.
- Out of memory, with every allocation point failed once (13-471 points each): ConvertToICCProfile (8-bit RGB, 16-bit
  with profile, palette), ApplyICCProfile, ConvertToCMYK, ConvertCMYKToRGB and GetICCProfileDescription have no crash,
  no leak and no exception escaping. The SoftProof (gamut check) sweep was stopped as too slow under ASan, so it is not
  covered.
- Thread-stress correctness on the rel build: 4 threads x 300 loads with display switching. Every load matches the
  single-threaded result for one display, with pixels and profile from that same display.
- DisplayCache, TwinCache, CICPProfiles and BuiltInProfiles are mutex-guarded or magic statics. Sharing one transform
  across threads is safe in lcms 2.19, since cmsDoTransform copies its cache (cmsxform.c:524/579). Their only problem
  is CM-1's lifetime.
- Load flags:
  - FIF_LOAD_DISPLAY_ICC = 0x4000 collides with no load flag in qpv or main; only the save flags JPEG_SUBSAMPLING_420
    and TIFF_LZW share the bit, and TIFF_LZW is read only in WriteCompression.
  - TIFF, PSD, JXL and JXR, which receive the flag, give 0x4000 no other meaning.
  - Nested loads (MNG, APNG, RAW, ICO, PSD and JPEG thumbnails) get the flag stripped, so nothing is converted twice.
  - RAW_UNPROCESSED is skipped.
- MultiPage:
  - LockPage converts file pages and locks the replacement.
  - SavePages forces FIF_LOAD_DISPLAY_ICC on for TIFF/PSD/JXL/JXR only to keep a CMYK page's alpha. A CMYK+alpha TIFF
    page opened with TIFF_CMYK then makes the save fail with a message and leaves the file intact. main cannot load
    5-sample CMYK TIFFs at all, so this is not a regression.
- Little CMS 2.19.1 import (341ee6d) and its 4 `// FreeImage:` patches are correct: cmserr.c mutex NULL checks,
  cmsgamma.c OOM gotos, cmsio1.c Dup NULL check and InvShapes free, cmslut.c MatrixElemDup.
- Win32 code:
  - The DISPLAYCONFIG structure sizes (72/64/84/36) and ids (1, 9, 15, QDC_ONLY_ACTIVE_PATHS) match the SDK.
  - GetICMProfileW length handling and the 16 MB read cap are fine.
  - Rows are sized per row (width <= INT_MAX) and cmsDoTransform is called once per row, so images over 4G pixels and
    32-bit size_t are handled.
- CICP:
  - H.273 primaries 1, 4-9, 11, 12, 22 and transfers 1, 4-8, 11-15 are correct.
  - PQ, HLG and log curves get no profile, as documented.
  - A pixel-changing conversion removes the CICP tag; an identity conversion keeps it.
