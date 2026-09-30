# Plan: FIF_LOAD_DISPLAY_ICC, color management on load

Status: plan only, nothing is edited. Written 2026-09-30 against `qpv` at f9a75b1.

## 1. Goal

A new load flag. With it, every loader returns the image already converted to the screen's ICC profile, or to sRGB
when no screen profile can be found. CMYK JPEG, TIFF and PSD files are read as CMYK first (JPEG_CMYK, TIFF_CMYK,
PSD_CMYK are implied), so their embedded CMYK profile is used instead of FreeImage's naive CMYK→RGB formula.
The flag must work with every loading entry point: file (ANSI and wide), handle, memory, and the multi-page functions.

This automates the display recipe in README.md:111 ("load CMYK ... with JPEG_CMYK ..., then call
FreeImage_ApplyICCProfile() with the monitor's profile"). It also removes that recipe's main trap: color
management has to run before FreeImage_ConvertTo*() and FreeImage_Rescale(), which drop the profile. With the flag
it always has.

## 2. What the code does today (facts the plan builds on)

### 2.1 The color management toolkit (Source/FreeImageToolkit/ColorManagement.cpp)

- `ColorConverter` does all the work. `SetSource()` (:743) takes the embedded profile when its color space matches
  the pixels (a grey palette may take an RGB profile). Otherwise it takes the type's default: sRGB, grey sRGB,
  linear sRGB/grey for float. An untagged CMYK image is "device CMYK", bit-exact with the loaders' formula.
- `Convert()` (:1165) makes a new image in the destination model (`OutputLayout`, :208). CMYK → 24-bit RGB or RGB16,
  palettes → 24/32-bit, grey → RGB when the destination is RGB.
- `Apply()` (:1198) works in place (`InPlaceLayout`, :228): a palette is recoloured, CMYK → RGBA with opaque alpha,
  and GRAY8 + RGB destination → 8-bit palette (no longer a grey ramp). It returns FALSE for GRAY16 + RGB destination
  and for 555/565.
- `IsIdentity()` (:822) skips the pixels when the source and destination profile bytes are equal.
- `Tag()` (:880) attaches an explicitly passed destination profile, or removes the profile for a NULL (default)
  destination. It also sets or clears `FIICC_COLOR_IS_CMYK`.
- `SetSource()` refuses bitmaps without pixels (`!FreeImage_HasPixels`), so header-only loads need their own path.
- lcms transforms may be shared across threads. 16-bit formats force `cmsFLAGS_NOOPTIMIZE`, which is 5-7x slower
  per thread (from memory: icc-color-management-lcms2).

### 2.2 Load entry points and where the flags go

| Entry point | Path to the plugin | What the flag needs there |
|---|---|---|
| `FreeImage_Load`, `LoadU`, `LoadFromMemory`, `LoadFromHandle` | all end in `FreeImage_LoadFromHandle` → `load_proc` (Plugin.cpp:405) | map the flags before the call; convert after `FreeImage_Close` |
| `OpenMultiBitmap`, `OpenMultiBitmapU`, `OpenMultiBitmapFromHandle`, `LoadMultiBitmapFromMemory` → `FreeImage_LockPage` | `load_proc` called directly with `header->load_flags` (MultiPage.cpp:1179) | map the flags; convert before `locked_pages[dib]` is recorded (MultiPage.cpp:1185) |
| `LockPage` on a page the app changed or inserted | `FreeImage_LoadFromMemory(cache_fif, hmem, 0)` (MultiPage.cpp:369) | nothing: these are the app's own pixels |
| `SaveMultiBitmapTo*` / `CloseMultiBitmap`, reloading unchanged pages | `load_proc` directly, `load_flags & ~FIF_LOAD_NOPIXELS` (MultiPage.cpp:689) | map the flags (the bit stripped), **no** conversion |

### 2.3 Nested loads that forward the caller's flags

- MNGHelper.cpp:584 and PluginMNG.cpp:1308 load embedded PNG/JNG frames.
- PluginAPNG.cpp:554 loads each frame through FIF_PNG.
- PluginRAW.cpp:356 loads the embedded preview, with `flags | JPEG_EXIFROTATE`.

If the bit reached these plugins, the inner load would convert a frame and the outer load would convert it again.
Other nested loads pass constants and are unaffected: PluginICO.cpp:498, PSDParser.cpp:797, PluginPICT.cpp:1287,
Exif.cpp:855, PluginJPEG.cpp:683.

### 2.4 Load-flag bits in use

- Plugin-specific load bits: 0x0001 to 0x0010. PCD uses `flags & 0x03`.
- FIF_LOAD_NOPIXELS is 0x8000.
- **PluginJPEG.cpp:1206 reads `flags >> 16` as the requested decode size**, so every bit from 0x10000 up is taken for
  JPEG loads.
- Save flags (0x0080 to 0x40000) are never tested on a load path: every `flags &` in the plugins was checked.
- So 0x4000 is free. It sits next to FIF_LOAD_NOPIXELS, as the second generic load bit.

### 2.5 Which loaders attach an ICC profile

| Attach one | Attach none (color management assumes sRGB) |
|---|---|
| JPEG (APP2) | GIF, BMP (V4/V5 color spaces ignored), TGA, PCX, PNM, DDS, ICO (except PNG icons) |
| TIFF, PSD (with the CMYK flag when asked) | JPEG 2000 (J2K/JP2) |
| PNG (iCCP) | AVIF/HEIF that only use CICP (nclx) |
| WebP (playback frames too) | APNG and MNG **playback canvases**: fresh bitmaps, PluginAPNG.cpp:652, PluginMNG.cpp:1440 |
| AVIF, HEIF (stills and sequences) | EXR/HDR/PFM (float; the flag leaves floats alone anyway) |
| JXR | |
| RAW (see §10: the wrong profile) | |

- JPEG drops the CMYK profile when it converts CMYK to RGB without JPEG_CMYK (PluginJPEG.cpp:1287).
- A PNG with gAMA and no iCCP/sRGB chunk is gamma-corrected to 2.2 by the loader (PluginPNG.cpp:563). Treating the
  result as sRGB afterwards is close enough.

### 2.6 The header-only contract

"A header-only load describes the image the full load with the same flags returns" (c546eb9, 3fcee46). The rig in
`.claude/scratch/headeronly` checks it.

`RemoveAlphaChannel()` (Conversion.cpp:341) already builds header-only 24-bit and RGB16 headers from 32-bit and
RGBA16 ones, with `CloneMetadata`, which copies the DPI. It copies no thumbnail and no profile.

## 3. Proposed API

```c
#define FIF_LOAD_DISPLAY_ICC 0x4000	//! loading: convert to the display's ICC profile (sRGB when unknown); CMYK JPEG, TIFF and PSD are read as CMYK first

// the profile FIF_LOAD_DISPLAY_ICC converts to: NULL detects the screen's (the default), else a copy of this RGB profile; flags as FICMS_* (intent | BPC)
DLL_API BOOL DLL_CALLCONV FreeImage_SetDisplayICCProfile(const void *profile FI_DEFAULT(NULL), DWORD size FI_DEFAULT(0), int flags FI_DEFAULT(FICMS_INTENT_RELATIVE_COLORIMETRIC | FICMS_BLACKPOINT_COMPENSATION));
// the size of that profile; copied into buffer when it is large enough
DLL_API DWORD DLL_CALLCONV FreeImage_GetDisplayICCProfile(void *buffer FI_DEFAULT(NULL), DWORD size FI_DEFAULT(0));
```

**The setter:**
- It exists because the library has no window: it can only pick the primary monitor (§5.1). It is also the escape
  hatch for HDR, Wayland and multi-monitor setups.
- It returns FALSE, and keeps the previous setting, for a profile that does not validate (§5.4).
- `flags` accepts only the intent and FICMS_BLACKPOINT_COMPENSATION.

**The getter:**
- It copies into the caller's buffer. Returning a pointer would dangle when detection swaps the profile on another
  thread.
- It also makes detection directly testable.

Both are new exports, so no existing prototype changes (see fillbackground-set-alpha-option). Win32 decorations:
`_FreeImage_SetDisplayICCProfile@12`, `_FreeImage_GetDisplayICCProfile@8`.

**Default flags 0x101** (relative colorimetric + BPC), what Examples/AHK/ColorManagement.ahk:22 uses. For
matrix/TRC profiles, lcms has no perceptual table and uses the colorimetric one, so the intent only matters for LUT
(CMYK press) profiles. There, relative colorimetric + BPC is the usual display choice.

## 4. Behaviour

### 4.1 What the plugin gets

The internal helper `DisplayICCLoadFlags(fif, flags)` is applied at all three call sites of §2.2 and does nothing
when the bit is absent:

- it strips FIF_LOAD_DISPLAY_ICC, so no plugin and no nested load (§2.3) ever sees it;
- FIF_JPEG gets `| JPEG_CMYK`, FIF_TIFF `| TIFF_CMYK`, FIF_PSD `| PSD_CMYK`. These are per-format bits (TIFF_CMYK and
  PSD_CMYK are bit 0, which means something else to other formats), so they can only be added once the format is
  known, never folded into the define;
- FIF_PSD also has PSD_LAB cleared: the flag asks for display RGB, and raw Lab samples would be fed to an RGB
  transform.

After the load, conversion is skipped for `FIF_RAW` with RAW_UNPROCESSED (Bayer data, not an image).

### 4.2 Invariant

**The result's pixel format (type, bpp) depends only on the file and the flags: never on the detected profile, and
never on whether the transform turned out to be an identity.** Header-only parity depends on this. So does
consistent behaviour across machines.

### 4.3 Per layout (the `AnalysePixels` layouts, :94)

| Source | Result | Converted to | How |
|---|---|---|---|
| 1/4/8-bit colour palette, transparency included | same; palette recoloured | display RGB | `Apply` (table) |
| 8-bit grey ramp (MINISBLACK/MINISWHITE), grey palettes | same; **stays grey** | display grey (§5.5) | `Apply`, grey → grey |
| UINT16 grey | same | display grey | `Apply` (rows, in place) |
| 16-bit 555/565 | 24-bit | display RGB | `Convert` (TARGA_LOAD_RGB888 already widens 16-bit on load) |
| 24/32-bit, RGB16, RGBA16 | same; alpha kept | display RGB | `Apply` |
| 32-bit CMYK, RGBA16 CMYK | 24-bit RGB / RGB16 | display RGB | `Convert` (device CMYK when untagged) |
| FLOAT, RGBF, RGBAF | untouched, profile kept | - | scene-referred linear light: a display TRC baked in here would be applied again by the tone mappers |
| other types (INT16, UINT32, DOUBLE, COMPLEX...) | untouched | - | not colour |

**CMYK → 24-bit (RGB16 for 16-bit):**
- This is the format JPEG (PluginJPEG.cpp:1246), TIFF (`RemoveAlphaChannel`) and PSD (`FinishImage`) return without
  the CMYK flags, and what QPV shows today. Turning the flag on changes the colours of CMYK files, not their format.
- It deliberately differs from README.md:110-111, where `Apply` turns CMYK into opaque RGBA in place.
- Cost: one extra allocation, so the peak is about 1.75× the size of the CMYK image.
- The alternative is RGBA in place; see §12.
- Exception: a PSD with CMYK + alpha returns 32-bit opaque today (PSDParser.cpp:1739). With the flag it would return
  24-bit. The alpha is lost either way (PSDParser.cpp:1463 keeps 4 channels).

**Grey stays grey.** The destination for grey layouts is a grey profile derived from the display (§5.5), not the RGB
profile:
- GRAY8 keeps its ramp (FIC_MINISBLACK survives).
- UINT16 is managed in place, where `Apply` with an RGB destination refuses it.
- The attached grey profile is valid when saved. An RGB profile on a grey image is dropped by the PNG writer
  (README.md:55) and ignored, with a message, by the explicit API.
- A grey source with an embedded RGB profile (JPEG_GREYSCALE of a tagged RGB JPEG) also goes to display grey. The
  existing table path already handles RGB → grey.

### 4.4 Source profile

The existing `SetSource` rules apply unchanged. There is one addition: if lcms opens the embedded profile but cannot
link it, retry once with the embedded profile ignored, which means the defaults or device CMYK. This is an internal
`ColorConverter` option; the bitmap is not modified to do it.

### 4.5 Destination and tag

**Every image the flag converts carries the destination profile afterwards** (display RGB or display grey), even
when the transform was skipped as an identity. `Tag()` does this already for an explicit destination. It also
clears FIICC_COLOR_IS_CMYK, and CreateICCProfile removes the Exif InterColorProfile copy.

Why tag every time:
- A header-only load can attach the same profile without parsing the source profile.
- A later `FreeImage_ConvertToICCProfile()` to another monitor's profile starts from the right space, so moving to
  another monitor needs no reload.
- A save stays truthful: the pixels are display-referred, and the file says so.
- Applying the same profile again is a byte-equal identity (`IsIdentity`), so it costs nothing.

Types left untouched keep whatever the loader attached. The price: the file's own profile is replaced. An app that
shows "embedded profile: X" must read it from a load without the flag (§11).

### 4.6 Identity skip

When the source bytes equal the destination bytes, the pixels are skipped (`IsIdentity`). After the sRGB
normalisation (§5.4), untagged images on an sRGB or undetectable display take this path and only get tagged. That is
the common case, so it costs nothing.

### 4.7 Thumbnails

Converted with the same rules: their own profile, else the defaults. They must be moved to the new bitmap when
`Convert` replaces the image, since conversions never copy thumbnails.

Header-only JPEG loads keep the Exif thumbnail. A viewer that shows it as a preview would otherwise see a different
colour from the full image.

### 4.8 Header-only (FIF_LOAD_NOPIXELS with the flag)

- The plugin gets `NOPIXELS | the CMYK flag`.
- The format follows §4.3:
  - CMYK → `RemoveAlphaChannel()`'s header;
  - 555/565 → a 24-bit `FreeImage_AllocateHeader` + `CloneMetadata`;
  - otherwise the same header.
- The destination profile is attached; the thumbnail is converted and moved.
- Detection runs (cached, §5.6). The source profile is never parsed.
- Known and accepted: a full load whose conversion fails keeps its own profile (§4.10), while its header says
  "display profile". A loader that leaves the palette empty in header-only mode could also pick grey versus colour
  differently.

### 4.9 Multi-page

- `LockPage` converts after `load_proc`; `locked_pages` records the converted pointer.
- Pages the app changed or inserted come back from the cache as stored (§2.2).
- A save reloads unchanged pages with the mapped flags and no conversion. A re-saved document therefore keeps its
  unchanged pages as the file has them: CMYK stays CMYK with its profile. Changed pages are saved display-referred,
  with the display profile. Without the strip, APNG and MNG re-saves would bake the display transform into
  "unchanged" frames through §2.3.
- Playback frames (GIF/APNG/MNG/WebP/AVIF/HEIF) are clones owned by the caller, so converting them never touches the
  plugins' cached canvases.

### 4.10 Failures

| Failure | Result |
|---|---|
| Out of memory | The load returns NULL with FI_MSG_ERROR_MEMORY, as a plugin running out of memory does |
| `AnalysePixels` refuses the image | The image is returned as loaded; the message goes out as today |
| Link failure after the §4.4 retry | Cannot happen for CMYK (device CMYK → sRGB → a display already validated), so only an in-place layout can get here; it is returned as loaded, with its own profile |

## 5. Display profile detection

Nothing runs until the flag or the getter is used: no DLL is loaded and no display is connected before that.

### 5.1 Windows

- Uses the method already proven by Examples/AHK/ColorManagement.ahk:174-188, with
  `MonitorFromPoint({0,0}, MONITOR_DEFAULTTOPRIMARY)` instead of MonitorFromWindow: `GetMonitorInfoW`
  (MONITORINFOEXW) → `CreateICW(szDevice)` → `GetICMProfileW` (grow the buffer on ERROR_INSUFFICIENT_BUFFER) →
  `DeleteDC`.
- user32/gdi32 entry points are resolved with `LoadLibraryW` + `GetProcAddress`, so no build file changes: the
  vcxproj has `<AdditionalDependencies />`, Makefile.mingw:57 links only ws2_32, and there are the zig builds.
- The profile file is read with `_wfopen`, with a size cap (16 MB). Its identity for re-queries is path + size +
  last write time (`GetFileAttributesExW`).
- **HDR / Auto Color Management.** In these modes DWM converts sRGB app output to the display itself. That is what
  the example's `UseMonitorProfile` comment (line 21) warns about: converting to the monitor profile as well would
  manage colours twice. What `GetICMProfileW` returns in these modes is unknown here.
  - Verify first on Marius's Windows 11: HDR on, then ACM on, and read the getter's output.
  - If it returns the monitor profile, add this check: `QueryDisplayConfig` → the path whose
    `DISPLAYCONFIG_SOURCE_DEVICE_NAME` matches the primary's `szDevice` → `DisplayConfigGetDeviceInfo`
    (GET_ADVANCED_COLOR_INFO_2, else GET_ADVANCED_COLOR_INFO). If HDR/WCG is active, use sRGB.
  - The structs are declared locally, since older SDK and MinGW headers lack the `_2` variant.
- Under headless Wine, `GetICMProfileW` returns FALSE, so only the sRGB fallback is testable there. The detection
  path itself needs real Windows.

### 5.2 Linux

- **Source.** Reads `_ICC_PROFILE` on the root window of `$DISPLAY`'s screen: the X11 convention of the "ICC Profiles
  in X" spec, which Firefox also reads. It is set by gsd-color, colord-kde, xiccd, ArgyllCMS `dispwin -I` and
  DisplayCAL's loader.
- **Wayland.** Only covered if the compositor publishes the atom on Xwayland's root; not verified here. Otherwise
  sRGB. colord over D-Bus is a follow-up (§13).
- **libxcb.so.1 through `dlopen`, not libX11.** Xlib's default error and IO-error handlers call `exit()`, and
  replacing them is process-wide. xcb returns errors in its replies and is thread-safe.
- **No headers needed.** The dozen prototypes and the three POD reply structs are declared locally: they are X11
  wire format, stable. This machine has no X11/xcb -dev headers, and build machines won't need them.
- **Calls:**
  - skip everything when `DISPLAY` is unset;
  - `xcb_connect(NULL, &screen)` → `xcb_connection_has_error`;
  - find the screen's root through the setup's roots iterator;
  - `xcb_intern_atom(only_if_exists)`;
  - `xcb_get_property(ANY, 0, cap/4)`, and accept only format 8 with `bytes_after == 0`;
  - copy the bytes, free the replies, `xcb_disconnect`.
- Only screen 0's `_ICC_PROFILE`. The per-monitor `_ICC_PROFILE_n` atoms need a window to choose from; the setter
  covers that.
- **Link.** `-ldl` in `LIBRARIES` of Makefile.gnu:21 and Makefile.fip:21, and in TestAPI/ICC's `LIB`. glibc 2.34
  and later (2.43 here) have `dlopen` in libc; older ones need `-ldl`.

### 5.3 Other platforms

macOS, Cygwin and the BSDs: compiled out, sRGB. The code is guarded by `_WIN32` and `__linux__`.

### 5.4 Validation and sRGB normalisation

- **Accepted** if lcms opens it, it is RGB, `ModelOf` accepts its device class, and it links as an output profile
  (a test transform from built-in sRGB). Otherwise built-in sRGB is used.
- **sRGB-equivalent profiles become built-in sRGB.** The test: built-in sRGB → the profile, 16-bit, a 17³ grid,
  maximum error ≤ 1/255. Such a profile is replaced by the built-in sRGB bytes. The common case is Windows' default
  "sRGB Color Space Profile.icm" on uncalibrated machines. Untagged images then hit the byte-equal identity (§4.6)
  instead of a near-identity transform over every pixel.
- **Calibrating the tolerance:** colord's /usr/share/color/icc/colord/sRGB.icc and the Windows file must pass.
  Rec709.icc (sRGB primaries, BT.709 curve), AdobeRGB1998.icc and Display P3 must fail.

### 5.5 The grey companion profile

- When the display is sRGB, it is the built-in grey (FICMS_PROFILE_GRAY).
- Otherwise it is synthesised once per display profile:
  1. transform display `(v,v,v)` → XYZ, relative colorimetric, at 4096 levels;
  2. take Y and make it monotonic;
  3. build a tabulated TRC and pass it to `cmsCreateGrayProfileTHR` with D50;
  4. save it with SaveBuiltInProfile's fixed date, so the bytes are identical on every run.
- Grey → this profile maps each grey level to the display grey `(v,v,v)` with the same luminance. That is exactly
  what a viewer shows for a grey image.

### 5.6 Cache, re-query, threads

- **Shared state.** One immutable state (RGB bytes, grey bytes, flags, identity of the OS source) behind a
  `std::shared_ptr`, swapped under a `std::mutex`. This is safe here: the vcxproj uses `/MT`, and libheif already
  uses std::mutex in every build. A conversion takes its shared_ptr copy and works outside the lock.
- **Re-query cadence.** While in auto mode, the OS is queried at most once per second. An unchanged identity keeps
  the state; the expensive work (validation, equivalence, grey synthesis) only runs when the profile changes. A
  profile set by the app is never re-queried.
- **No callbacks under the lock.** `FreeImage_OutputMessageProc` never runs while the lock is held: lcms stays muted
  during detection and messages are sent afterwards. A message callback that loads an image would otherwise deadlock.

### 5.7 Set / Get

- `Set(NULL, 0, flags)`: auto-detect, with these conversion flags.
- `Set(p, n, flags)`: pin a copy, validated and normalised as in §5.4.
- `Get`: returns the RGB profile loads convert to now: the detected one, the pinned one, or built-in sRGB. Its
  description tells which (the fallback reads "sRGB (FreeImage)").

## 6. Implementation steps

**Commit 1: the feature, README lines riding in it.** Marius wants single, easily undone commits.

1. **FreeImage.h:** the define next to FIF_LOAD_NOPIXELS (:677); both exports next to the other color management
   routines (:1207).
2. **ColorManagement.cpp:** all new code goes here; no new file, so Makefile.srcs, fipMakefile.srcs and the vcxproj
   need none of the edits a new source file requires.
   - detection: Windows, xcb, stub for other platforms;
   - the state/cache;
   - validation and normalisation, and the grey synthesis;
   - `DisplayICCLoadFlags()` and `ConvertToDisplayICC(fif, flags, dib)`: the policy table, the header-only path,
     thumbnails, the retry;
   - the `ColorConverter` "ignore the embedded profile" option;
   - the two exports.
3. **Utilities.h:** the two internal declarations, one-line comments.
4. **Plugin.cpp:**
   - `FreeImage_LoadFromHandle`: `load_proc(..., DisplayICCLoadFlags(fif, flags), ...)`;
   - after `FreeImage_Close`, `if (bitmap && (flags & FIF_LOAD_DISPLAY_ICC)) bitmap = ConvertToDisplayICC(...)`.
   The close comes first, so the plugin has released its decoder before the conversion allocates.
5. **MultiPage.cpp:**
   - LockPage: mapped flags, then convert before `locked_pages[dib] = page`;
   - SavePages: `DisplayICCLoadFlags(header->fif, header->load_flags) & ~FIF_LOAD_NOPIXELS`, no conversion.
6. **Makefile.gnu, Makefile.fip:** `-ldl`.
7. **README.md:**
   - one changelog line: "added FIF_LOAD_DISPLAY_ICC: images load in the screen's colours (its ICC profile, or
     sRGB), CMYK JPEG, TIFF and PSD included;";
   - under "Color management", one line for the flag + Set/Get;
   - line 111's recipe then points to the flag.
8. **TestAPI/ICC:** a `display` test and an `x11` stub target (§7).

**Commit 2: AHK wrapper** (as 3ab2593 was separate):
- the flag line in **both** full lists, Load (:282) and OpenMultiBitmap (:1281):
  `; FIF_LOAD_DISPLAY_ICC = 0x4000; convert to the display's ICC profile (sRGB when unknown); CMYK JPEG, TIFF and PSD read as CMYK first`;
- the `FreeImage_SetDisplayICCProfile()` and `FreeImage_GetDisplayICCProfile()` functions;
- `getFIMfunc` lists: SetDisplayICCProfile in fList12, GetDisplayICCProfile in fList8;
- a changelog entry, with the version number left to Marius;
- then run check_wrapper.py and an AHK parse under Wine (`runahk.sh 64|32`).

**Commit 3, only if §8 shows it is needed: a transform cache.** A small LRU keyed by (source bytes, destination
bytes, formats, lcms flags, intent), holding transforms created in one process-lifetime lcms context. The context
must outlive them, because cmsDoTransform and cmsDeleteTransform use it.

**Follow-ups:** separate, optional commits (§10).

## 7. Tests

**New `TestAPI/ICC/display.c`.** Deterministic, independent of the OS: the "display" is pinned with the setter to
built-in sRGB, Adobe RGB, Display P3, ProPhoto, colord's profiles, and a LUT-based RGB profile made as profiles.c
makes its test profiles.

1. **Oracle.** For every fixture, a load with the CMYK flag + explicit `ApplyICCProfile`/`ConvertToICCProfile`
   (already checked against Pillow) must equal the flag load pixel for pixel, and the ICC bytes must match. Grey
   fixtures are compared against the synthesised grey profile. Fixtures: CMYK JPEG/TIFF/PSD with and without a
   profile, tagged and untagged RGB 8/16-bit, colour and grey palettes, transparent palette, GRAY8, UINT16, 555/565,
   float, and Marius's cs-2025-cmyk.tiff.
2. **Every entry point gives identical results:** Load, LoadFromHandle (custom IO), LoadFromMemory,
   OpenMultiBitmap + LockPage, OpenMultiBitmapFromHandle, LoadMultiBitmapFromMemory. LoadU is Windows-only (§7.9).
3. **No double conversion:**
   - APNG with iCCP, frames and playback;
   - MNG;
   - RAW with a JPEG preview (RAW_PREVIEW);
   - an ICO holding a tagged PNG.
   Each is compared with its oracle.
4. **Header-only parity.** flag|NOPIXELS against the full load: type, bpp, ICC bytes, thumbnail. Normalise the
   FIC_RGBALPHA/IsTransparent trap for 32-bit FIT_BITMAP only (freeimage-header-only-loads).
5. **Multi-page save.** Open a 2-page CMYK TIFF with the flag, lock and change page 1, save. Page 0 must equal the
   TIFF_CMYK load of the source; page 1 must be the display-referred page. Same for an APNG.
6. **Mapping rules.** PSD_LAB + flag gives RGB; RAW_UNPROCESSED + flag gives the UINT16 Bayer data unchanged; float
   images are untouched.
7. **Setter/getter:**
   - invalid profiles return FALSE and keep the previous setting (CMYK, grey, garbage, truncated);
   - normalisation: colord sRGB.icc → built-in sRGB bytes; Rec709/AdobeRGB are kept;
   - the grey synthesis for built-in sRGB (not normalised) is within 1/255 of FICMS_PROFILE_GRAY.
8. **X11 stub (Linux target `x11`).** A stub `libxcb.so.1` built by the Makefile and found through
   `LD_LIBRARY_PATH`, returning canned replies:
   - a valid RGB profile;
   - a CMYK profile → sRGB;
   - garbage → sRGB;
   - format 32 → sRGB;
   - `bytes_after > 0` → sRGB;
   - oversize → sRGB;
   - atom missing → sRGB;
   - connection error → sRGB;
   - `DISPLAY` unset → no dlopen at all.
   Optional end-to-end: Xvfb extracted from the .deb (dpkg-x style, like the Wine rig), with `_ICC_PROFILE` set by
   a 20-line helper.
9. **Windows:**
   - zig cross-compile of the whole library, x86 + x64 (rig in `.claude/scratch/io64/`);
   - `display` built as a Windows exe and run under Wine: the headless fallback path, plus every setter-driven
     test and LoadU;
   - an MSVC 14.29 compile check with the vcxproj flags (`.claude/scratch/msvc`);
   - Marius's manual check:
     - give the monitor a non-sRGB profile in Colour Management and read the getter;
     - HDR on, and ACM on (§5.1);
     - QPV loading a CMYK JPEG with the flag.
10. **Threads.** 8 threads loading with the flag while another thread alternates Set(profile)/Set(NULL), under ASan
    and TSan.

**Gates.** Run in the TestAPI/ICC suite:
- `make run threads asan-run pillow`, with `display` added to TESTS;
- TestAPI: the 174 output lines identical;
- **proof that loads without the flag are unchanged:** the headeronly `sig`/`sweep`/`compare` rig over its 2,254
  TIFF/PSD files plus the regress corpus, with flags 0, NOPIXELS, the CMYK flags and -1, pixel hashes before and
  after (the stash-rebuild-diff recipe). The only code on that path is `DisplayICCLoadFlags`, a no-op without the
  bit.

## 8. Performance: measure, do not guess

**How:** time at 6 threads (the 8-thread timings here are unreliable; check `ps` for tin.py first), each case with
and without the flag.

**Cases:**
- 24 MP JPEG: untagged, sRGB-tagged, Adobe RGB-tagged;
- CMYK JPEG and TIFF;
- a 16-bit TIFF;
- a GIF/WebP LockPage playback loop;
- a header-only probe.

**Displays:** built-in sRGB, colord sRGB.icc (normalised), Adobe RGB (matrix/TRC), a LUT-based RGB profile.

Expected, to confirm:
- untagged images on an sRGB display are free (identity + tag);
- a real 8-bit transform is small next to decoding;
- 16-bit costs 5-7x per thread (NOOPTIMIZE);
- **creating the lcms transform is paid on every load.** It averaged ~25 ms in the convert suite (mostly LUT and
  CMYK cases). Per LockPage frame with a LUT display profile, that decides commit 3.

## 9. Risks

| Risk | Mitigation |
|---|---|
| QPV's NOPIXELS probes pass flags `-1` (from memory), so they get this bit today and would start returning RGB headers for CMYK files, the display profile, and pay detection | Tell QPV to pass 0x8000 alone (§11); header-only never parses source profiles; detection is cached |
| Windows HDR/ACM double management | §5.1: verify first, then the DisplayConfig check; the setter as the escape hatch |
| Multi-monitor: the library picks the primary | QPV calls the setter with its window's monitor profile, as the example does on WM_MOVE/WM_DISPLAYCHANGE |
| Wayland without the atom → sRGB on wide-gamut screens | Documented; setter; colord later |
| An X server that hangs makes `xcb_connect` block | Only with a set but unreachable `DISPLAY`; at most once per second |
| A bad EDID-derived profile (GNOME makes them automatically, e.g. ~/.local/share/icc/edid-*.icc here) gives wrong colours | This is what the OS says the screen is; the setter overrides it |
| Untrusted profile bytes (any X client can set the atom) | Same lcms parser as for file profiles; size cap |
| A profile that opens but will not link | §4.4 retry; display validated when detected |
| The flag loses the file's own profile | Tag policy §4.5, documented; read it without the flag |
| A 16-bit source is slow | Same cost as the explicit API; measured in §8 |
| An OOM in the CMYK → RGB conversion fails a load that would succeed without the flag | Stated in §4.10; peak ~1.75× the CMYK image |

## 10. Loader gaps

These make the flag, and the explicit API, treat images as sRGB when they are not. Each is a separate follow-up
commit, most valuable first:

1. **APNG/MNG playback canvases** (PluginAPNG.cpp:652, PluginMNG.cpp:1440) lack the stream's profile, although the
   raw frames have it. Attach the frame's or global profile to the returned canvas. Small.
2. **RAW:**
   - PluginRAW.cpp:839 attaches `imgdata.color.profile`, the raw file's embedded (input) profile, to processed output,
     which is sRGB primaries. That profile should not describe the output. The PluginRAW-only fix leaves
     LibRawLite untouched, as agreed.
   - The RAW_DEFAULT 48-bit output is linear (gamm 1,1 at PluginRAW.cpp:404) and is taken as sRGB. Tag it
     FICMS_PROFILE_LINEAR_SRGB.
   - Count the corpus raws with an embedded profile before changing anything.
3. **AVIF/HEIF using CICP only.** Synthesise a profile from the primaries and transfer function: sRGB (1/13),
   Display P3 (12/13), BT.709 (1/1), BT.2020 SDR. PQ/HLG stay unmanaged: HDR needs tone mapping. README.md:114
   already lists this.
4. **JPEG 2000:** OpenJPEG's `icc_profile_buf` (the colr box, method 2) is never attached.
5. **Camera JPEGs in Adobe RGB mode** often have no ICC profile, only Exif ColorSpace = uncalibrated plus
   InteropIndex "R03" (DCF). Map that to FICMS_PROFILE_ADOBE_RGB; many photo tools honour it, browsers don't.
6. **PNG gAMA/cHRM without iCCP/sRGB:** gamma-corrected to 2.2 and cHRM ignored. With the flag, a profile could be
   synthesised instead (PNG_IGNOREGAMMA implied). Rare today.
7. **BMP V4/V5** calibrated RGB and embedded or linked profiles. Rare.

## 11. Notes for QPV

Marius applies these himself; this plan touches only the FreeImage repo.

1. **The probe.** Pass FIF_LOAD_NOPIXELS alone instead of `-1`. Otherwise every probe gets this flag (and
   JPEG_GREYSCALE, RAW_UNPROCESSED... as today).
2. **Multi-monitor.** Call `FreeImage_SetDisplayICCProfile()` with the window's monitor profile (the example's
   `MonitorProfileFile()`), or with sRGB when HDR/ACM is on. An already loaded image can be moved to a new monitor
   with `FreeImage_ConvertToICCProfile(dib, newProfile)`, since it carries the old display profile; no reload.
3. **Info panel.** It will show the display profile for flag loads. Read the file's own profile from the probe.
4. **No double conversion.** Drop any own conversion step after a flag load, or pass the same bytes, which is then a
   byte-equal identity.
5. **Unaffected:** `FreeImage_ConvertTo32Bits()` and friends after the load are fine now, and QPV's WIC path is not
   touched.
6. **DLL version.** Needs a DLL built from commit 1.

## 12. Decisions for Marius (my recommendation first)

1. **Name and bit:** `FIF_LOAD_DISPLAY_ICC = 0x4000`. The only free generic bit below JPEG's size field.
2. **CMYK result:** 24-bit RGB / RGB16, as the loaders return without the CMYK flags. The alternative, RGBA in place
   (the README recipe), costs one allocation less but changes the format QPV sees for CMYK files.
3. **Grey:** it stays grey, through a display-grey profile. The alternative is `Apply`'s recoloured palette, with
   UINT16 left unmanaged.
4. **Floats:** untouched.
5. **555/565:** always 24-bit. The alternative is untouched, and unmanaged.
6. **Tag:** the display profile on every converted image, identity cases included.
7. **Intent:** 0x101 by default, changeable through the setter.
8. **Thumbnails:** converted too.
9. **Set/Get exports:** yes. Without them there is no multi-monitor, HDR or Wayland override and no way to test
   detection.
10. **HDR/ACM:** sRGB, after the check on your machine (§5.1).
11. **Linux:** `_ICC_PROFILE` through libxcb only for now; colord later, if wanted.
12. **Loader gaps:** which of §10 to do, in that order? 1 and 2 are small.
13. **Delphi/VB6/.NET constants:** one line each, or leave them.

## 13. Not in scope

- Wayland's color-management protocol (it needs the app's surface) and colord over D-Bus (possible later).
- HDR (PQ/HLG) tone mapping, and soft-proofing on load.
- Tracking which monitor a window is on: the app's job, through the setter.
- Keeping the file's original profile next to the display one: a load without the flag gives it.
- Any change to the explicit color management API.
