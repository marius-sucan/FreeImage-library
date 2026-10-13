# BMP in FreeImage, checked against the bitmap specifications

2026-10-13 · branch `qpv` at `00abb5e` · `Source/FreeImage/PluginBMP.cpp` (loader and writer) and the core code it relies on
(`BitmapAccess.cpp`, `Conversion*.cpp`). Report only: nothing in `Source/` was changed.

## 1. Verdict

FreeImage **reads** every header Microsoft defines — `BITMAPCOREHEADER` (OS/2 1.x), `BITMAPINFOHEADER`,
`BITMAPV4HEADER` and `BITMAPV5HEADER`, the newest (Windows 98/2000; there is no V6) — plus the undocumented 52- and
56-byte variants and the full 64-byte OS/2 2.x header. It **writes** only the 40-byte `BITMAPINFOHEADER`.

Reading a V4 or V5 file is structural only. FreeImage takes the first 40 bytes and, for 16-bit images, the colour
masks; everything V4 and V5 add — 32-bit masks, the alpha mask, the colour space, endpoints and gamma, the rendering
intent, the ICC profile — is ignored. Since the fix of L1, L2, L3 and L7 (2026-10-13) the colour and alpha masks
count at every depth; the colour space, intent and profile are still ignored.

The classic content decodes exactly: 1-, 4- and 8-bit palettes, RLE4 and RLE8 with deltas, 24-bit, 16-bit 5-5-5 and
5-6-5, top-down rows, gaps before the pixels, oversized and undersized palettes. The defects sit in 16- and 32-bit
direct colour, in colour management, and in the writer's 32-bit layout:

| # | Finding | Severity | Evidence | Status |
|---|---|---|---|---|
| L1 | 32-bit: the byte the spec calls "not used" becomes alpha; files that hold 0 there, like the suite's baseline 32-bit file, load **fully transparent** | high | 2 of the suite's 27 "good" files; every other decoder surveyed (WIC, Chromium, Firefox, GIMP, ImageMagick, stb_image, Pillow) shows them opaque | **fixed** |
| L2 | 32-bit `BI_BITFIELDS` masks are ignored: only the B,G,R,A byte order decodes right | high | 1 "good" + 9 "questionable" suite files | **fixed** |
| L3 | 16-bit masks other than 5-5-5 / 5-6-5 decode as 5-5-5; 16-bit alpha masks are dropped | medium | 5 "questionable" + 1 "bad" suite files | **fixed** |
| L4 | V5 embedded ICC profiles are not attached; `LCS_CALIBRATED_RGB` is ignored | medium | `q/rgb24prof2.bmp` shows red and green swapped | open |
| W1 | The writer stores 32-bit alpha in `BI_RGB`'s unused byte with a 40-byte header; Windows (WIC) and Pillow read such files as opaque | medium | Pillow reloads them as RGB | open |
| L5 | OS/2 1.x 16/24/32-bit files ignore `bfOffBits` | medium-low | crafted file: every pixel wrong | open |
| W2 | RLE8 saves write `biSizeImage` = 0 and `bfSize` = the uncompressed size | low | header bytes | open |
| W6 | A saved `FreeImage_CreateView()` view writes its parent's pixels into the row padding, which must be zeros | low | padding bytes | open |

Everything else is either a valid but rare feature that is refused (2 bpp, `BI_ALPHABITFIELDS` until its fix with
L1–L3, `BI_JPEG`/`BI_PNG`, 64 bpp, OS/2 short headers, RLE24, Huffman 1D, OS/2 bitmap arrays) or a detail.

**BMP Suite 2.8 scorecard** at `00abb5e` (the standard test set; its own reference images are the oracle, appendix A):

| Directory | Meaning (suite README) | Files | Acceptable | Wrong, no warning | Refused |
|---|---|---|---|---|---|
| `g/` | "Your program should definitely support them" | 27 | 24 (4 of them ±1 in 16-bit rounding) | 3 | 0 |
| `q/` | "questionable": unusual, obsolete or arguably invalid | 43 | 19 (two with a caveat: `rgb24prof` loses its profile, `rgb32fakealpha` matches the suite's alternative rendering) | 15 | 9 |
| `b/` | "bad": must not crash | 20 | no crash; 6 refused, 14 loaded | 1 (`rgb16-880`) | — |
| `x/` | related formats | 1 | 0 | 0 | 1 (identified as BMP, then refused) |

For contrast, Pillow 12.1.1 refuses 19 of the `q/` files instead of decoding them: **FreeImage's main risk is silent
wrong output** (18 of the `g/` and `q/` files), not refusal.

Since the fix of L1, L2, L3 and L7: `g/` 27 of 27 (the same 4 within ±1); `q/` 34 acceptable (4 more within ±1),
1 wrong (`rgb24prof2`, L4), 8 refused; `b/rgb16-880.bmp` matches its reference.

## 2. How this was checked

- **Specifications** (verbatim text, saved under `.claude/scratch/bmp-spec/spec/`):
  - [MS-LEARN] Win32 GDI reference on Microsoft Learn — `BITMAPFILEHEADER`, `BITMAPINFOHEADER`, `BITMAPV4HEADER`,
    `BITMAPV5HEADER`, `BITMAPCOREHEADER`, `BITMAPINFO`, `RGBQUAD`, "Bitmap Header Types", "Bitmap Compression", the
    WCS 1.0 "Using Structures" page, plus the archived 2005 MSDN pages for the Windows 95/NT qualifiers. The live
    `BITMAPINFOHEADER` page currently shows the DirectShow text, without the 16/32-bpp table; its GDI sentences are
    quoted from <https://learn.microsoft.com/en-us/previous-versions/dd183376(v=vs.85)>, and the V4 and V5 pages
    carry the same ones.
  - [MS-WMF] Windows Metafile Format, v20240423 (rev. 18.0): §2.2.2.2–2.2.2.5 header objects, §2.2.2.9 DIB object,
    §2.1.1.3 BitCount, §2.1.1.7 Compression, §2.2.2.20 RGBQuad. The only normative ("MUST") text Microsoft publishes
    for these structures.
  - [WINCE] Windows CE 5.0/6.0 `BITMAPINFOHEADER` (2 bpp, `BI_ALPHABITFIELDS`).
  - [WIC] Windows Imaging Component BMP codec: "BMP Format Overview" and "Native pixel formats".
  - [OS2] IBM: the OS/2 Toolkit 4.5 header `pmbitmap.h` (github.com/bitwiseworks/os2tk45, the only source of the
    numeric values), its GPI/PM reference books, and the 1992 *Presentation Manager Programming Reference* vol. III,
    appendix D (`BITMAPINFOHEADER2`, `BITMAPFILEHEADER2`, `BITMAPARRAYFILEHEADER2`).
- **BMP Suite 2.8** (Jason Summers, 2023-11-28, the current release): all 91 files through `FreeImage_Load`, header-only
  too, converted to 32-bit and compared pixel by pixel with the suite's reference PNGs (where the suite accepts several
  renderings, the closest one counts).
- **36 crafted files**, plus one saved by Pillow, for cases the suite lacks (appendix B).
- **The writer**: 17 saves of every exportable layout, inspected byte by byte, decoded by a spec-literal decoder and by
  Pillow (section 5).
- **Pillow 12.1.1** as a second decoder only: it is wrong on RLE4 literal runs and on RLE deltas, and refuses many layouts.
- **Other decoders' source** (Chromium, Firefox, GIMP, ImageMagick 7, stb_image) read for the ambiguous points
  (section 6).
- **Sanitizers**: `PluginBMP.cpp` built with `-fsanitize=address,undefined` and linked ahead of the library; the 91
  suite files, the 36 crafted files and the 17 saves run clean, and the saves are byte-identical to the normal build's.

## 3. The format as specified, and what FreeImage does with it

### 3.1 Header versions

The header is told apart by its first DWORD, its size. [MS-LEARN "Bitmap Header Types"]: "The BITMAPCOREHEADER and
BITMAPV4HEADER formats have been superseded by BITMAPINFOHEADER and BITMAPV5HEADER formats, respectively." WIC
describes its codec as "Specification Support: BMP Specification v5".

| Size | Header | Defined by | Adds | Read | Written |
|---|---|---|---|---|---|
| 12 | `BITMAPCOREHEADER` | Windows 2.x / OS/2 1.x | 16-bit width and height; 1, 4, 8, 24 bpp; 3-byte palette entries | yes (L5) | no |
| 16–64 | OS/2 `BITMAPINFOHEADER2` | IBM OS/2 2.x | unsigned 32-bit sizes; Huffman 1D, RLE24; halftoning fields; truncated at any field (15 lengths) | 40, 52, 56, 64 only (L6) | no |
| 40 | `BITMAPINFOHEADER` | Windows 3.x / NT | compression; masks after the header for `BI_BITFIELDS` | yes | **always** |
| 52 | "`BITMAPV2INFOHEADER`" | undocumented | R, G, B masks in the header | yes | no |
| 56 | "`BITMAPV3INFOHEADER`" | undocumented | + alpha mask | yes (alpha mask ignored until the L1–L3 fix) | no |
| 108 | `BITMAPV4HEADER` | Windows 95 / NT 4 | masks, alpha mask, colour space, endpoints, gamma | yes, extras ignored | no |
| 124 | `BITMAPV5HEADER` | Windows 98 / 2000 | + rendering intent, embedded or linked ICC profile | yes, extras ignored | no |

`Load()` dispatches on exactly 12, 40, 52, 56, 64, 108 and 124 (`PluginBMP.cpp:1306-1324`); every other size fails
with "Unknown bmp subtype with id N". `CheckBitmapInfoHeader()`'s `biSize >= 40` test (`:148`) is not the gate.

### 3.2 Compression values

| Value | Windows meaning | OS/2 2.x meaning | Valid for | Read | Written |
|---|---|---|---|---|---|
| 0 `BI_RGB` | uncompressed | same | all | yes | yes |
| 1 `BI_RLE8` | RLE, 8 bpp | same | 8 bpp, bottom-up | yes | with `BMP_SAVE_RLE` |
| 2 `BI_RLE4` | RLE, 4 bpp | same | 4 bpp, bottom-up | yes | no |
| 3 `BI_BITFIELDS` | masks | Huffman 1D | 16 and 32 bpp / 1 bpp | 16-bit: 5-5-5 and 5-6-5 only (L3); 32-bit: masks ignored (L2); both **fixed**; Huffman refused (L10) | 16-bit |
| 4 `BI_JPEG` | JPEG stream | RLE24 | printers only / 24 bpp | refused (L9, L10) | no |
| 5 `BI_PNG` | PNG stream | — | printers only | refused (L9) | no |
| 6 `BI_ALPHABITFIELDS` | 4 masks | — | Windows CE .NET 4.0+, 16 and 32 bpp | refused (L7, **fixed**) | no |
| 11–13 `BI_CMYK`, `BI_CMYKRLE8`, `BI_CMYKRLE4` | CMYK | — | listed in [MS-WMF] §2.1.1.7 | refused | no |

[MS-LEARN] on `BI_JPEG`/`BI_PNG`: "These compression values are only valid for SetDIBitsToDevice and StretchDIBits when
the hdc parameter specifies a printer device." They are a printer pass-through, not a file format feature.

### 3.3 Bit depths

| bpp | Defined by | Read | Written |
|---|---|---|---|
| 0 | `BI_JPEG`/`BI_PNG` ("implied by the JPEG or PNG format") | refused | — |
| 1, 4, 8 | all headers | yes | yes |
| 2 | Windows CE: "The bitmap has four possible color values." | refused (L8) | — |
| 16 | 40-byte header and later | 5-5-5 and 5-6-5 right; other masks wrong (L3, **fixed**: 24-bit, or 32-bit with alpha) | 5-5-5 / 5-6-5 as `BI_BITFIELDS` |
| 24 | all headers | yes | yes |
| 32 | 40-byte header and later | B,G,R order only (L2); unused byte → alpha (L1); both **fixed** | `BI_RGB`, alpha in the unused byte (W1) |
| 64 | WIC decodes `64bppRGBAFixedPoint`; GDI+ lists 64 among BMP depths; **no layout is documented** | refused (L11) | — |

### 3.4 `BITMAPV5HEADER`, field by field

| Offset | Field | Specification ([MS-LEARN] unless noted) | FreeImage reads | FreeImage writes |
|---|---|---|---|---|
| 0 | `bV5Size` | 124 | dispatch | 40 |
| 4, 8 | `bV5Width`, `bV5Height` | negative height = top-down; "Top-down DIBs cannot be compressed." | yes; top-down RLE refused | positive height |
| 12 | `bV5Planes` | "This value must be set to 1." | checked | 1 |
| 14 | `bV5BitCount` | 0, 1, 4, 8, 16, 24, 32 | 1, 4, 8, 16, 24, 32 | same |
| 16 | `bV5Compression` | see 3.2 | 0–3; 6 since the L7 fix | 0, 1, 3 |
| 20 | `bV5SizeImage` | "This may be set to zero for BI_RGB bitmaps." | ignored (right) | always 0 (W2, W3) |
| 24, 28 | `bV5X/YPelsPerMeter` | resolution | yes | yes |
| 32 | `bV5ClrUsed` | palette size; for ≥16 bpp an optional palette | yes, clamped | yes |
| 36 | `bV5ClrImportant` | 0 = all | ignored | = `ClrUsed` |
| 40–48 | `bV5Red/Green/BlueMask` | "valid only if bV5Compression is set to BI_BITFIELDS" | 16-bit only (L2, L3); every depth since their fix | — |
| 52 | `bV5AlphaMask` | "Color mask that specifies the alpha component of each pixel." | ignored (L1, L3); used with `BI_BITFIELDS` since their fix | — |
| 56 | `bV5CSType` | `LCS_CALIBRATED_RGB`, `LCS_sRGB`, `LCS_WINDOWS_COLOR_SPACE`, `PROFILE_LINKED`, `PROFILE_EMBEDDED` | ignored (L4) | — |
| 60 | `bV5Endpoints` | XYZ of the primaries, 2.30 fixed point, for `LCS_CALIBRATED_RGB` | ignored (L4) | — |
| 96 | `bV5GammaRed/Green/Blue` | "unsigned fixed 16.16", for `LCS_CALIBRATED_RGB` | ignored (L4) | — |
| 108 | `bV5Intent` | `LCS_GM_ABS_COLORIMETRIC`, `_BUSINESS`, `_GRAPHICS`, `_IMAGES` | ignored (FreeImage has no intent) | — |
| 112 | `bV5ProfileData` | "The offset, in bytes, from the beginning of the BITMAPV5HEADER structure to the start of the profile data." | ignored (L4) | — |
| 116 | `bV5ProfileSize` | profile size | ignored (L4) | — |
| 120 | `bV5Reserved` | "should be set to zero" | ignored | — |

`plugin->supports_icc_profiles_proc = NULL; // not implemented yet;` (`PluginBMP.cpp:1722`): FreeImage reports that
BMP carries no ICC profile, in either direction.

## 4. Loader findings

### L1 — 32-bit: the unused byte becomes alpha (high)

**Spec.** [MS-LEARN] `BITMAPINFOHEADER`, 32 bpp, `BI_RGB`: "The value for blue is in the least significant 8 bits,
followed by 8 bits each for green and red. The high byte in each DWORD is not used." [MS-WMF] §2.1.1.3 says the same.
With `BI_BITFIELDS` and a 40-byte header there are three masks, no alpha mask; a V4/V5 header with `bV5AlphaMask` = 0
has no alpha either.

**FreeImage.** `LoadPixelData()` copies the four bytes as B, G, R, A, and `LoadWindowsBMP()` then calls
`FreeImage_SetTransparent(dib, FreeImage_GetColorType(dib) == FIC_RGBALPHA)` (`PluginBMP.cpp:803`, likewise `:1033`,
`:1186`). `FreeImage_GetColorType()` answers `FIC_RGBALPHA` as soon as one fourth byte is not 0xFF
(`BitmapAccess.cpp:812-833`). So:

| File | Fourth byte | FreeImage | Spec / WIC / Pillow |
|---|---|---|---|
| `g/rgb32.bmp` (`BI_RGB`), `g/rgb32bfdef.bmp` (`BI_BITFIELDS`) | all 0 | **alpha 0 everywhere, transparent** | opaque |
| crafted `v3_rgb32_zero`, `v4_x8r8g8b8_zero`, `v5_x8r8g8b8_zero` (alpha mask 0) | all 0 | fully transparent | opaque |
| crafted `v3_bf32_junk`, `v4/v5_x8r8g8b8_junk` | non-zero | semi-transparent | opaque |
| crafted `v3_rgb32_onebyte` | 0 except one byte | fully transparent but one pixel | opaque |
| `q/rgb32fakealpha.bmp` | non-zero | alpha from the byte | the suite accepts both renderings |
| `q/rgba32-1.bmp`, `/usr/share/pixmaps/debian-logo.bmp` (V5, alpha mask `FF000000`) | real alpha | right | right |

Microsoft's own decoder settles the practice. [WIC] "BMP Format Overview": "for 16-bit and 32-bit Windows BMP files,
the BMP codec ignores any alpha channel, as many legacy image files contain invalid data in this extra channel.
Starting with Windows 8, 32-bit Windows BMP files written using the BITMAPV5HEADER with valid alpha channel content are
read as WICPixelFormat32bppBGRA". Pillow loads every row of the table above as opaque RGB except the two real-alpha files.

The GDI reference does contradict itself on the in-memory side: "Alpha values per pixel are only supported for 32-bpp
BI_RGB" (Alpha Blending), premultiplied ("the APIs use premultiplied alpha", `BLENDFUNCTION`). That describes
`AlphaBlend()` on DIB sections; Microsoft's file codec is the one quoted above, and it ignores the byte.

FreeImage cannot simply follow WIC: its own writer stores alpha in that byte (W1), and so does Pillow's (an RGBA image
saved by Pillow 12.1.1 is a 40-byte `BI_RGB` file with alpha in byte 3, which Pillow itself reloads as RGB). Files
written that way exist and FreeImage round-trips them today. The behaviour is inherited from upstream 3.18.

**Fix.** Treat the fourth byte as alpha only where a header says so (a V3/V4/V5 alpha mask with `BI_BITFIELDS`, or
`BI_ALPHABITFIELDS`), or, for `BI_RGB` and mask-only layouts, when it is not zero everywhere: a plane of zeros means
"unused", so set it to 0xFF and report the image opaque. That rule fixes the transparency of the three 32-bit `g/`
files (`g/rgb32bf.bmp` also needs L2) and of every crafted zero case, keeps FreeImage's and Pillow's RGBA files, and
leaves `q/rgb32fakealpha.bmp` on the suite's second rendering. ImageMagick and stb_image use this very rule
(section 6).

The rule has a cost: an image whose alpha is 0 everywhere stops round-tripping. A 32-bit dib whose alpha was never
written (`FreeImage_Allocate()` zero-fills it) or a deliberately invisible one, saved as today, reloads transparent now
and would reload opaque. With W1 in place, FreeImage's own new files carry an alpha mask and stay exact; the rule then
only decides for files without one. It belongs in all three loaders (`PluginBMP.cpp:803`, `:1033`, `:1186`), and a
header-only load cannot apply it: with no pixels, a 32-bit header still reports `FIC_RGBALPHA`, as for every format.
Section 6 lists what other decoders do.

**Fixed** 2026-10-13, as proposed: with `BI_RGB`, in all three loaders, the fourth byte is alpha unless it is 0
everywhere; with `BI_BITFIELDS` it is alpha only under an alpha mask (56-byte headers and up, an alpha mask overlapping
a colour counting as none) or with `BI_ALPHABITFIELDS`, and otherwise the image is opaque, the rows a cut file lacks
too. `g/rgb32.bmp`, `g/rgb32bfdef.bmp` and the crafted zero and junk files load opaque; FreeImage's and Pillow's RGBA
saves keep their alpha; `v3_rgb32_onebyte` stays transparent, the rule's known limit.

### L2 — 32-bit `BI_BITFIELDS` masks are ignored (high)

**Spec.** [MS-LEARN]: "If the biCompression member of the BITMAPINFOHEADER is BI_BITFIELDS, the bmiColors member
contains three DWORD color masks that specify the red, green, and blue components, respectively, of each pixel. [...]
bits set in each DWORD mask must be contiguous and should not overlap the bits of another mask. All the bits in the
pixel do not need to be used." [MS-WMF] §2.1.1.3 makes it "MUST be contiguous and MUST NOT overlap". Only Windows
95/98/Me were limited, per the archived 2005 page: "the system supports only the following 32-bpp color mask: The blue
mask is 0x000000FF, the green mask is 0x0000FF00, and the red mask is 0x00FF0000."

**FreeImage.** `LoadWindowsBMP()` reads the masks and hands them to `FreeImage_AllocateHeader()`
(`PluginBMP.cpp:765-768`), which keeps masks for 16-bit bitmaps only (`BitmapAccess.cpp:321-345`); for 24 and 32 bits
`FreeImage_GetRedMask()` and friends return the fixed BGRA masks (`:862-910`). The pixels are copied raw. Only the
Windows 95 layout decodes right.

Wrong in the suite: `g/rgb32bf.bmp` (masks `FF000000`, `00000FF0`, `00FF0000`), `q/rgb32h52.bmp`, `q/rgb32-xbgr.bmp`,
`q/rgba32-2.bmp`, `q/rgba32h56.bmp`, `q/rgb32-111110.bmp`, `q/rgb32-7187.bmp`, `q/rgba32-1010102.bmp`,
`q/rgba32-61754.bmp`, `q/rgba32-81284.bmp`. Crafted: `v4_a8b8g8r8` and `v5_a8b8g8r8` (R and B swapped; 679 of 703
pixels wrong). Pillow decodes the byte-aligned ones right and refuses the rest.

**Fix.** When the masks are not `00FF0000`/`0000FF00`/`000000FF`, extract every channel by mask and shift and scale it
to 8 bits (`v * 255 / max`, rounded), alpha by the alpha mask or 0xFF. Channels wider than 8 bits (10-10-10-2, 11-11-10)
lose precision in a 32-bit dib; `FIT_RGBA16` would keep it, if that is ever wanted.

**Fixed** 2026-10-13: B, G, R with or without alpha in the fourth byte still loads as it is; any other layout is decoded
channel by channel into a 32-bit dib, rounded to the nearest 8-bit value, from a table for channels up to 16 bits wide.
The ten suite files and the crafted A8B8G8R8 files match their references, four of them within one level. 24 MP takes
155–200 ms that way, against 65 ms for a raw copy.

### L3 — 16-bit: masks other than 5-5-5 and 5-6-5, and 16-bit alpha (medium)

**Spec.** The same `BI_BITFIELDS` rule as L2. The archived page limits only Windows 95/98/Me to "A 5-5-5 16-bit image
[...] and a 5-6-5 16-bit image"; NT accepts any contiguous masks. V3/V4/V5 headers add an alpha mask.

**FreeImage.** The 16-bit dib keeps the file's masks (`PluginBMP.cpp:722-725`), but every consumer in the library —
`FreeImage_ConvertTo24Bits`/`32Bits`, `FreeImage_GetPixelColor`, `IS_FORMAT_RGB565` (`Utilities.h:624`,
`Conversion32.cpp:266` and others) — reads a 16-bit dib as 5-6-5 when the masks match it exactly and as 5-5-5
otherwise. The alpha mask is never read for 16-bit.

Wrong: `q/rgb16-231.bmp`, `q/rgb16-3103.bmp`, `q/rgba16-4444.bmp`, `q/rgba16-1924.bmp` (colours), `q/rgba16-5551.bmp`
(colours right, alpha lost), `b/rgb16-880.bmp`. Pillow refuses all but `rgba16-5551` (where it also drops alpha).

**Fix.** Keep a 16-bit dib only for exact 5-5-5 and 5-6-5 without alpha; convert anything else to 24-bit, or 32-bit
when there is an alpha mask, with the extraction of L2.

**Fixed** 2026-10-13 that way; an alpha mask with bits beyond the 16 counts as none. The five `q/` files and
`b/rgb16-880.bmp` match their references.

**Rounding, for the record.** The four 16-bit `g/` files match their references within ±1: FreeImage scales 5-bit
values with truncation (`v * 255 / 31`, `Conversion32.cpp:86-101`), the references round (`v = 3` gives 24, not 25; 15
of the 32 values are one lower). That is the library's conversion, not the plugin's, and white stays 255.

### L4 — Colour space and ICC profiles are ignored (medium)

**Spec.** `bV5CSType` `PROFILE_EMBEDDED`: "bV5ProfileData points to a memory buffer that contains the profile to be used
(gamma and endpoints values are ignored)"; `bV5ProfileData` is "The offset, in bytes, from the beginning of the
BITMAPV5HEADER structure to the start of the profile data"; "Bitmap Header Types": "The file format places the profile
data at the end of the file." `LCS_CALIBRATED_RGB` "implies that endpoints and gamma values are given in the
appropriate fields" (the WCS page adds "Bogus values cause trouble."). `LCS_sRGB` and `LCS_WINDOWS_COLOR_SPACE` mean sRGB.

**FreeImage.** None of `bV5CSType`, the endpoints, the gammas or the profile are read; no profile is attached, and
`supports_icc_profiles_proc` is NULL. `q/rgb24prof.bmp` loads its pixels right but loses its sRGB profile;
`q/rgb24prof2.bmp` (green and red swapped in the pixels, swapped back by its profile) shows the swap. With
`FIF_LOAD_DISPLAY_ICC` such files are treated as sRGB. DISPLAY-ICC-PLAN §10 item 7 already lists this
(`git show 0abf265^:DISPLAY-ICC-PLAN.md`).

**Fix.** "Applications should access the profile data only when bV5Size equals the size of the BITMAPV5HEADER and
bV5CSType equals PROFILE_EMBEDDED or PROFILE_LINKED." For `PROFILE_EMBEDDED`, read `bV5ProfileSize` bytes at header
start + `bV5ProfileData` and attach them with `FreeImage_CreateICCProfile()`; set `supports_icc_profiles_proc`. Optionally build a profile from
`LCS_CALIBRATED_RGB` endpoints and gamma with lcms2 (`ColorManagement.cpp` links it). Keep ignoring `PROFILE_LINKED`:
the spec allows "any fully qualified name (including a network path)", which a library must not open
(`q/rgb24lprof.bmp`'s reference is the plain image, so ignoring it is right).

### L5 — OS/2 1.x 16/24/32-bit files ignore `bfOffBits` (medium-low)

**Spec.** [MS-LEARN] `bfOffBits`: "The offset, in bytes, from the beginning of the BITMAPFILEHEADER structure to the
bitmap bits."

**FreeImage.** `LoadOS21XBMP()` seeks to `bfOffBits` for 1-, 4- and 8-bit images (`PluginBMP.cpp:1123`) but not in the
16-bit (`:1133-1154`) or 24/32-bit (`:1156-1189`) cases: the pixels are read from right after the 12-byte header. A
24-bit `BITMAPCOREHEADER` file with 16 bytes between header and pixels (crafted `core24_gap16`) decodes with all 703
pixels wrong; the same file without the gap is right, and Pillow reads both. Inherited from upstream 3.18.

**Fix.** The same `io->seek_proc(handle, bitmap_bits_offset, SEEK_SET)` in both cases. (16 and 32 bpp are not legal in a
core header — "This value must be 1, 4, 8, or 24" — accepting them is harmless.)

### L6 — OS/2 2.x headers of 16–36, 42–48 and 60 bytes are refused (low)

**Spec.** [OS2] `BITMAPINFOHEADER2`: "The structure can be truncated after cBitCount or any subsequent field." and
"This length can range from 16 (BITMAPINFOHEADER through field cBitCount) up to sizeof(BITMAPINFOHEADER2) bytes."
With the field layout of the toolkit's `pmbitmap.h` that gives 15 legal lengths: 16, 20, 24, 28, 32, 36, 40, 42, 44,
46, 48, 52, 56, 60 and 64. IBM states no value for omitted fields, but every default it names is 0, and the
secondary sources say they are "assumed to be zero". The Archive Team wiki: "16, 24, 40, 48, and 64 may be most
common".

**FreeImage.** Only 40, 52, 56 and 64 reach a loader; the other eleven fail with "Unknown bmp subtype with id N"
(`q/pal8os2v2-16.bmp` and the crafted `os2v2_N` files; 42 and 46 by code reading). 52 and 56 are read as the
Windows V2/V3 headers, which gives the same result for palette and 24-bit images (OS/2 has no bit fields). Pillow
refuses the same sizes.

**Fix.** Route any other size from 16 to 64 to `LoadOS22XBMP()` with the missing fields zeroed.

### L7 — `BI_ALPHABITFIELDS` is refused; the code for it is dead (low)

**Spec.** [WINCE]: "Specifies that the bitmap is not compressed and that the color table consists of four DWORD color
masks that specify the red, green, blue, and alpha components of each pixel. This is valid when used with 16- and
32-bpp bitmaps. This value is valid in Windows CE .NET 4.0 and later." (No Microsoft page states its value, 6.)

**FreeImage.** `CheckBitmapInfoHeader()` accepts compression 0–3 only (`PluginBMP.cpp:183-193`), so the
`use_bitfields = 4` branches (`:718-720`, `:761-763`) never run. `q/rgba32abf.bmp` is refused with "Invalid file format".

**Fix.** Accept 6 for 16 and 32 bpp and let L2's extraction use the fourth mask.

**Fixed** 2026-10-13 that way, top-down allowed as for `BI_BITFIELDS`; 1-, 4-, 8- and 24-bit files with it are still
refused. `q/rgba32abf.bmp` matches its reference.

### L8 — 2 bits per pixel is refused (low)

**Spec.** [WINCE] `biBitCount` 2: "The bitmap has four possible color values." Not in [MS-LEARN] or [MS-WMF].
**FreeImage.** Refused by the bit-count check (`PluginBMP.cpp:166-182`): `q/pal2.bmp`, `q/pal2color.bmp`. Pillow refuses
them too. **Fix.** Expand to a 4-bit dib (FreeImage has no 2-bit type).

### L9 — `BI_JPEG` and `BI_PNG` are refused (low)

**Spec.** Printer pass-through only (3.2). **FreeImage.** `q/rgb24jpeg.bmp`, `q/rgb24png.bmp` refused ("Invalid file
format"); Pillow refuses them too. **Fix, if ever wanted.** Hand `biSizeImage` bytes at `bfOffBits` to the JPEG or PNG
plugin through a memory stream.

### L10 — OS/2 RLE24 and Huffman 1D are refused (low)

**Spec.** [OS2] `pmbitmap.h`: `BCA_HUFFMAN1D` 3, `BCA_RLE24` 4. IBM's whole text on Huffman: "The bit map is
compressed by a modified Huffman encoding. This is valid for a bi-level (one bit per pel) bit map." — no bit order,
end-of-line rule or colour sense. RLE24: "A string of bytes specifying the RGB color values of succeeding pels. If
COUNT is odd, it must be padded by a zero byte for an even length overall", next to "COUNT = a multiple of 3", which
leaves the unit open; the suite and the OS/2-era GBM read COUNT as pixels in B,G,R order. Both suite files are its
author's "attempt". **FreeImage.** Both share numbers with Windows codes: `q/rgb24rle24.bmp` (4) fails
`CheckBitmapInfoHeader()` as if it were `BI_JPEG`; `q/pal1huffmsb.bmp` (3) passes it as if it were `BI_BITFIELDS` and is
refused later with "Unsupported compression type". Pillow refuses both. **Fix, if wanted.** RLE24 is RLE8 with 3-byte
pixels; Huffman 1D is, in the suite's reading, ITU-T T.4 one-dimensional coding (libtiff's fax decoder, already linked
for G3, could serve).

### L11 — 64 bits per pixel is refused (low)

No Microsoft document defines the layout (3.3). The suite's `q/rgba64.bmp` is "A 64-bits/pixel BMP. This is a high
dynamic range format supported by some Microsoft software." It is refused, as by Pillow. A reader would decode 16-bit
s2.13 fixed point (WIC's "1.0 represented by the signed integer value 8192"), linear, into `FIT_RGBAF`.

### L12 — OS/2 bitmap arrays: identified as BMP, then refused (low)

**Spec.** [OS2] IBM's 1992 PM reference (appendix D): a bitmap array is a chain of `BITMAPARRAYFILEHEADER2` entries
("BA", cbSize, offNext, cxDisplay, cyDisplay), each followed by a whole `BITMAPFILEHEADER2`; offNext is the "Offset
of the next [...] structure from the start of the file"; cxDisplay/cyDisplay are the "Pel dimensions of the device
for which this version is intended"; "The device-independent version must be the first [...] defined." A viewer takes
the version matching the requested size, else the display size, else the device-independent one. The toolkit header:
"All offsets are absolute based on the start of the FILE".

**FreeImage.** `Validate()` accepts "BA" as well as "BM" (`PluginBMP.cpp:1233-1242`), so `FreeImage_GetFileType()`
returns `FIF_BMP` for `x/ba-bm.bmp`. `Load()` then reads the 14-byte array header as a `BITMAPFILEHEADER` and the
embedded "BM" header as the info-header size: "Unknown bmp subtype with id 1723714". In that file the embedded
header's `offBits` (808) counts from the start of the whole file.

**Fix.** Either load the first bitmap, the device-independent one (skip the 14-byte array header, read the embedded
file header, keep offsets relative to the array's start in the stream), or stop accepting "BA" in `Validate()`.

## 5. Writer findings

What the writer produces (`Save()`, `PluginBMP.cpp:1483-1698`), checked on 17 saves:

| Source | File written | Spec-literal decode | Pillow |
|---|---|---|---|
| 1, 4, 8-bit | 40-byte header, `BI_RGB`, full palette | exact | exact |
| 8-bit + `BMP_SAVE_RLE` | `BI_RLE8`, valid stream (EOL per row, EOB at the end, even absolute runs) | exact | exact |
| 1, 4-bit + `BMP_SAVE_RLE` | `BI_RGB` (no RLE4 writer) | exact | exact |
| 16-bit 5-5-5, 5-6-5 | `BI_BITFIELDS` + 3 masks | exact (±1 rounding) | exact |
| 24-bit, views of 24/32-bit | `BI_RGB` (a view's row padding is not zero: W6) | exact | exact |
| 32-bit with alpha | `BI_RGB`, alpha in the fourth byte | RGB exact, alpha "not used" | **mode RGB, alpha lost** |
| 8-bit with a transparency table | palette only | RGB exact | transparency lost, no message |

### W1 — 32-bit alpha is written where the spec says nothing is (medium)

The writer copies the dib header (`:1536`) and writes `BI_RGB` for 32-bit images, so alpha lands in the byte of which
the spec says "The high byte in each DWORD is not used." Windows' codec ignores it ([WIC], quoted in L1), as does
Pillow. The documented way
is a `BITMAPV5HEADER` with `BI_BITFIELDS`, masks `00FF0000`/`0000FF00`/`000000FF` and `bV5AlphaMask` = `FF000000`,
`bV5CSType` = `LCS_sRGB` — the layout of `/usr/share/pixmaps/debian-logo.bmp`, which FreeImage reads right today.
WIC writes alpha only that way: its `EnableV5Header32bppBGRA` option, off by default, "Specifies whether to allow
encoding data in the GUID_WICPixelFormat32bppBGRA pixel format. If this option is set to VARIANT_TRUE, the BMP will be
written out with a BITMAPV5HEADER header", and "If a file has a BITMAPV5HEADER, it decodes as
GUID_WICPixelFormat32bppBGRA".

Trade-off: readers that cannot parse a V5 header lose the whole file (upstream FreeImage refuses V4/V5 since r1836, so
`main` would). Writing the V5 header only when the image has alpha (`FIC_RGBALPHA`), and the 40-byte one otherwise,
keeps opaque saves as compatible as they are now.

### W2 — RLE8 saves: `bfSize` and `biSizeImage` (low)

`bfSize` is set before encoding to the uncompressed size (`:1507`; the comment at `:1520` says so) and `biSizeImage`
stays 0. [MS-LEARN] `bfSize`: "The size, in bytes, of the bitmap file." `biSizeImage`: "This may be set to zero for
BI_RGB bitmaps" — so not for `BI_RLE8`; [MS-WMF] §2.2.2.9: unless the compression is `BI_RGB`, `BI_BITFIELDS` or
`BI_CMYK`, "the size of aData MUST be the BitmapInfoHeader Object value ImageSize". Measured: an 8-bit 61×23 save
claims 2550 bytes and holds 1908; a greyscale ramp claims 2550 and holds 2598, because RLE8 can grow the data. Readers
mostly ignore both fields (the suite's `q/pal8os2-sz.bmp` relies on that), but a reader that takes the size of the
RLE data from `biSizeImage`, as [MS-WMF] prescribes, finds 0 bytes. **Fix.** Encode first (or seek back), then write
both sizes; the 4 GB guard (`:1521`) should test the encoded size too.

### W3 — `biSizeImage` = 0 for `BI_BITFIELDS` saves (low)

Same sentence as W2: zero is allowed for `BI_RGB` only. 16-bit saves are `BI_BITFIELDS`. **Fix.** Write
`pitch × height` for every uncompressed save.

### W4 — Palette `rgbReserved` is copied from the dib (low)

[MS-LEARN] `RGBQUAD`: "rgbReserved: This member is reserved and must be zero." [MS-WMF] §2.2.2.20: "MUST be 0x00".
`Save()` writes `pal[i].rgbReserved` (`:1590`), and the loader fills it from the file, so a non-zero byte survives a
round trip (a crafted palette with 255 non-zero bytes was written as is). **Fix.** Write 0.

### W5 — Not written: colour information, transparency tables, RLE4 (info)

No V4/V5 header means no ICC profile or colour space: a dib with a profile loses it, as `supports_icc_profiles_proc`
announces. An 8-bit transparency table is dropped without a message (BMP has no standard place for it). `BMP_SAVE_RLE`
is honoured for 8-bit only; RLE4 is optional and its absence is fine. Non-`FIT_BITMAP` types are refused.

### W6 — A saved view carries its parent's pixels in the row padding (low)

[MS-LEARN] `BITMAPINFO`: "each scan line must be padded with zeros to end on a LONG data-type boundary." For a view
made with `FreeImage_CreateView()`, `Save()` writes `dst_pitch` bytes from each row of the view (`:1684-1690`), so the
padding — and, at 1 and 4 bpp, the unused bits of the last byte — comes from the parent's pixels to the right of the
view. Measured with 33-pixel views of parents filled with 0xFF: 3 padding bytes of `FF` per row at 1, 4 and 8 bpp, 2 at
16 bpp, 1 at 24 bpp (plus 7 and 4 trailing bits at 1 and 4 bpp); a plain 33-pixel image writes zeros. Readers ignore
padding, but a crop saved this way leaks up to 3 bytes per row of the image it was cut from. **Fix.** Write
`FreeImage_GetLine()` bytes, mask the last byte's unused bits at 1 and 4 bpp, then zeros up to the pitch.

## 6. What other decoders do

Source-level survey of current code (read, not run; permalinks in `.claude/scratch/bmp-spec/spec/peers.md`), WIC's
documentation, and Pillow 12.1.1 run here. Chromium decodes standalone BMPs with image-rs through Skia since mid-2026;
its older C++ `BMPImageReader` now serves ICO only.

| Decoder | 32-bit `BI_RGB`, fourth byte | Alpha mask ≠ 0 | Other masks | RLE skipped pixels | Embedded ICC | Also reads |
|---|---|---|---|---|---|---|
| Chromium (image-rs) | ignored | used (`BI_BITFIELDS` V3+, `BI_ALPHABITFIELDS`, `BI_RGB` with V4/V5) | yes | black | applied | 2 bpp, `BI_ALPHABITFIELDS`, OS/2 16–64-byte headers, RLE24 |
| Chromium, older C++ reader | ignored | used with any compression; all zero = opaque | yes | transparent | applied, calibrated RGB too | 2 bpp, `BI_ALPHABITFIELDS`, OS/2 headers, RLE24 |
| Firefox | ignored | `BI_BITFIELDS` only | yes | transparent; black after an early end of data | applied, with the intent; calibrated RGB too | — |
| GIMP | ignored | `BI_BITFIELDS`, `BI_ALPHABITFIELDS` | yes; wider than 8 bits → 16/32-bit image | transparent | ignored | 2 bpp, 64 bpp (s2.13), Huffman 1D, RLE24, `BA` (first image) |
| ImageMagick 7 | **alpha unless all zero** (40-byte header) | `BI_BITFIELDS` | yes | index 0; an early end-of-bitmap fails the read (from the code, not run) | exposed, not applied | `BI_JPEG`/`BI_PNG`, 64 bpp, `BA` (all images) |
| stb_image 2.30 | **alpha unless all zero** | `BI_BITFIELDS` | channels up to 8 bits | not supported | ignored | — |
| WIC (documented) | ignored | V5, 32-bit, "valid alpha" (Windows 8+) | not documented | not documented | not documented | — |
| Pillow 12.1.1 (run) | ignored | used | byte-aligned layouts; others refused | wrong on deltas | ignored | — |
| **FreeImage** at `00abb5e` | **alpha; all zero = transparent** | not read (the fourth byte is alpha anyway) | 16-bit 5-5-5/5-6-5 and 32-bit B,G,R,A only | index 0 | ignored | OS/2 40/52/56/64-byte headers |

Every decoder surveyed shows a 32-bit file that has no alpha mask and a fourth byte of 0 everywhere as opaque; only
FreeImage made it transparent, until L1's fix, which follows ImageMagick and stb_image. ImageMagick and stb_image use exactly L1's rule, and say why. ImageMagick: "We should
ignore the alpha value in BMP3 files but there have been reports about 32 bit files with alpha. We do a quick check to
see if the alpha channel contains a value that is not zero (default value)." stb_image: "if alpha channel is all 0s,
replace with all 255s". Chromium's older reader applies the same test even to a declared alpha channel: "Some BMPs
specify an alpha channel but don't actually use it (it contains all 0s). To avoid displaying these images as
fully-transparent, decode as if images are fully opaque until we actually see a non-zero alpha value". Arbitrary masks
(L2, L3) are decoded by Chromium, Firefox, GIMP and ImageMagick, and embedded profiles (L4) are applied by Chromium and
Firefox. No decoder follows a linked profile.

## 7. Behaviour the specification leaves open

### S1 — RLE8: an absolute run past the end of the row breaks the stream

The spec does not say what a run longer than the rest of the row means. `DecodeRLE8()` clips the run to the row
(`count = MIN(status_byte, width - bits)`, `PluginBMP.cpp:486`), reads `count` bytes, then advances by the full run
(`:494`): the bytes it did not read are then decoded as commands. Crafted `rle8_literal_cross` (a 6-pixel run starting
3 pixels before the end of an 8-pixel row) is refused outright: "Error encountered while decoding RLE8 BMP data".
`LoadPixelDataRLE4()` reads the whole run into its linear buffer, and the RLE4 twin decodes. **Fix.** Skip the
`status_byte - count` bytes that were not stored.

### S2 — Runs past the end of the row: RLE8 clips, RLE4 wraps

An encoded run longer than the rest of the row is clipped by RLE8 and continues on the next row in RLE4 (its buffer is
one block). Both are defensible; the two decoders should agree. A row without an end-of-line (crafted `rle8_no_eol`)
leaves the next row empty in FreeImage; Pillow refuses the file.

### S3 — Header-only loads accept files the full load refuses

For 1-, 4- and 8-bit images the header-only path returns before the compression is checked, so `q/pal1huffmsb.bmp`
(and, by code reading, any 8-bit `BI_RLE4` or `BI_BITFIELDS` file) loads with `FIF_LOAD_NOPIXELS` and fails without
it. The contract is
that a header-only load describes what the full load returns.

### S4 — Messages

Valid but unsupported files say "Invalid file format" (2 bpp, 64 bpp, `BI_JPEG`, `BI_PNG`, `BI_ALPHABITFIELDS`,
RLE24); an OS/2 bitmap array says "Unknown bmp subtype with id 1723714"; zero width or height says "DIB allocation
failed"; an OS/2 1.x file with an unsupported bit count returns NULL with **no message** (`LoadOS21XBMP()`'s switch has
no default).

## 8. Robustness notes found on the way (not conformance)

- **R1.** A file cut before its `BI_BITFIELDS` masks loads with uninitialised masks: `DWORD bitfields[4]` is not
  initialised and the `read_proc` result is not checked (`PluginBMP.cpp:723-725`, `:766-768`, `:966-968`). Two runs of
  the crafted `cut_before_masks.bmp` after filling the stack differently gave masks `F80371F0 00007FFE 52899322` and
  `A48214C0 00007FFC 39699322`. **Fixed** for the Windows headers with L1–L3: masks the file cuts off give the default
  layout. The OS/2 2.x 64-byte path (`LoadOS22XBMP()`'s 16-bit `BI_BITFIELDS`) is unchanged.
- **R2.** The RLE8 writer does not check its `malloc` (`:1600`).
- **R3.** Big-endian builds (`FREEIMAGE_BIGENDIAN`) byte-swap the headers and 16-bit pixels but not the masks, on load
  (`:724`, `:767`, `:968`) or save (`:1562-1578`). Code reading only. The Windows headers' load side swaps since L1–L3.

## 9. Correct today — keep

- Top-down RLE is refused ("Run-length encoded (RLE) format bitmaps and BITMAPCOREHEADER bitmaps cannot be top-down
  bitmaps"): `b/rletopdown.bmp`.
- `biPlanes` ≠ 1 is refused ("This value MUST be 0x0001"): `b/badplanes.bmp`. Pillow is laxer.
- Pixels skipped by RLE deltas or early end-of-line/end-of-bitmap are palette index 0, one of the suite's accepted
  renderings (`q/pal4rletrns.bmp`, `q/pal8rlecut.bmp` and twins). Decoders split three ways here: index 0
  (ImageMagick), transparent (Chromium's older reader, GIMP, Firefox), black (Chromium today).
- `bfSize`, `biSizeImage`, `biClrImportant` are not trusted on load (`q/pal8os2-sz.bmp`, `b/badfilesize.bmp`,
  `b/badbitssize.bmp`). IBM even defines the OS/2 `cbSize` as the size of the headers, not of the file.
- `bfOffBits` is honoured, relative to the start of the file header in the stream (`q/pal8offs.bmp`).
- `biClrUsed` above 2^bpp is clamped ([MS-WMF]: "the maximum color table size SHOULD be assumed"),
  `q/pal8oversizepal.bmp`; palettes of 16/24/32-bit files are skipped (`g/rgb24pal.bmp`, `q/rgb24largepal.bmp`,
  `g/rgb16-565pal.bmp`); a one-entry palette works (`q/pal1p1.bmp`).
- Masks left in a V2–V5 header count only with `BI_BITFIELDS` ("valid only if bV5Compression is set to BI_BITFIELDS").
- V4/V5 masks are read from inside the header, not after it. ("Bitmap Header Types" says the masks "immediately
  follow" V4/V5 headers too, but those headers have mask members of their own, and the suite's V4/V5 files and the
  Debian logo put nothing after them.)
- `PROFILE_LINKED` is not followed.
- Odd absolute runs in RLE4 are decoded completely (Pillow drops the last pixel of each).
- No `b/` file crashes, and nothing trips ASan or UBSan; damaged and cut files keep their rows with a warning (the
  salvage contract).

## 10. Suggested order of work

1. **Done** 2026-10-13. **L1 + L2 + L3 + L7 in one change**: one mask-extraction path in `LoadWindowsBMP()` for 16- and 32-bit (alpha
   by mask, or by the non-zero rule for `BI_RGB`, which the OS/2 loaders' 32-bit cases need too), `BI_ALPHABITFIELDS`
   accepted. Fixes the 3 wrong `g/` files, 14 of the 15 wrong `q/` files (the 15th, `rgb24prof2`, is L4) and
   `b/rgb16-880.bmp`, and makes `q/rgba32abf.bmp` load.
2. **L5** (two seeks) and **S1** (skip the unread bytes): small and safe.
3. **W1**: V5 header for 32-bit images with alpha. A decision for Marius (compatibility with readers that cannot
   parse V5).
4. **L4**: embedded profiles on load, then on save (with W1's V5 header), and `supports_icc_profiles_proc`.
5. **W2, W3, W4, W6**: header fields, palette bytes, zero padding.
6. **L6, L12**: OS/2 short headers and bitmap arrays (or drop "BA" from `Validate()`).
7. **L8–L11** only if wanted: 2 bpp, JPEG/PNG pass-through, RLE24/Huffman 1D, 64 bpp.

## Appendix A — BMP Suite 2.8 results

FreeImage loads each file with flags 0, converts to 32-bit and compares with the suite's reference image ("Correct
display" column of `html/bmpsuite.html`). "±1": every pixel within one level. Measured at `00abb5e`: since the fix of
L1, L2, L3 and L7, every **WRONG** row, `q/rgba32abf.bmp` and `b/rgb16-880.bmp` match their references (`q/rgb24prof2.bmp`
is L4).

| File | Hdr | bpp | Compr. | FreeImage loads | Verdict | Why |
|---|---|---|---|---|---|---|
| g/pal1.bmp | 40 | 1 | RGB | 1bpp MINISBLACK | pass |  |
| g/pal1bg.bmp | 40 | 1 | RGB | 1bpp PALETTE | pass |  |
| g/pal1wb.bmp | 40 | 1 | RGB | 1bpp MINISWHITE | pass |  |
| g/pal4.bmp | 40 | 4 | RGB | 4bpp PALETTE | pass |  |
| g/pal4gs.bmp | 40 | 4 | RGB | 4bpp PALETTE | pass |  |
| g/pal4rle.bmp | 40 | 4 | RLE4 | 4bpp PALETTE | pass |  |
| g/pal8-0.bmp | 40 | 8 | RGB | 8bpp PALETTE | pass |  |
| g/pal8.bmp | 40 | 8 | RGB | 8bpp PALETTE | pass |  |
| g/pal8gs.bmp | 40 | 8 | RGB | 8bpp PALETTE | pass |  |
| g/pal8nonsquare.bmp | 40 | 8 | RGB | 8bpp PALETTE | pass |  |
| g/pal8os2.bmp | 12 | 8 | RGB | 8bpp PALETTE | pass |  |
| g/pal8rle.bmp | 40 | 8 | RLE8 | 8bpp PALETTE | pass |  |
| g/pal8topdown.bmp | 40 | 8 | RGB | 8bpp PALETTE | pass |  |
| g/pal8v4.bmp | 108 | 8 | RGB | 8bpp PALETTE | pass |  |
| g/pal8v5.bmp | 124 | 8 | RGB | 8bpp PALETTE | pass |  |
| g/pal8w124.bmp | 40 | 8 | RGB | 8bpp PALETTE | pass |  |
| g/pal8w125.bmp | 40 | 8 | RGB | 8bpp PALETTE | pass |  |
| g/pal8w126.bmp | 40 | 8 | RGB | 8bpp PALETTE | pass |  |
| g/rgb16-565.bmp | 40 | 16 | BITFIELDS | 16bpp RGB | pass ±1 | 5→8-bit scaling truncates, the reference rounds |
| g/rgb16-565pal.bmp | 40 | 16 | BITFIELDS | 16bpp RGB | pass ±1 | same |
| g/rgb16.bmp | 40 | 16 | RGB | 16bpp RGB | pass ±1 | same |
| g/rgb16bfdef.bmp | 40 | 16 | BITFIELDS | 16bpp RGB | pass ±1 | same |
| g/rgb24.bmp | 40 | 24 | RGB | 24bpp RGB | pass |  |
| g/rgb24pal.bmp | 40 | 24 | RGB | 24bpp RGB | pass |  |
| g/rgb32.bmp | 40 | 32 | RGB | 32bpp RGBA +transparent | **WRONG** | unused byte (0) taken as alpha: fully transparent |
| g/rgb32bf.bmp | 40 | 32 | BITFIELDS | 32bpp RGBA +transparent | **WRONG** | masks ignored, and fully transparent |
| g/rgb32bfdef.bmp | 40 | 32 | BITFIELDS | 32bpp RGBA +transparent | **WRONG** | same |
| q/pal1huffmsb.bmp | 64 | 1 | Huffman 1D (3) | refused | refused | OS/2 Huffman 1D not implemented |
| q/pal1p1.bmp | 40 | 1 | RGB | 1bpp PALETTE | pass |  |
| q/pal2.bmp | 40 | 2 | RGB | refused | refused | 2 bpp not implemented |
| q/pal2color.bmp | 40 | 2 | RGB | refused | refused | 2 bpp not implemented |
| q/pal4rlecut.bmp | 40 | 4 | RLE4 | 4bpp PALETTE | pass | skipped pixels = index 0 (an accepted rendering) |
| q/pal4rletrns.bmp | 40 | 4 | RLE4 | 4bpp PALETTE | pass | same |
| q/pal8offs.bmp | 40 | 8 | RGB | 8bpp PALETTE | pass |  |
| q/pal8os2-hs.bmp | 12 | 8 | RGB | 8bpp PALETTE | pass |  |
| q/pal8os2-sz.bmp | 12 | 8 | RGB | 8bpp PALETTE | pass |  |
| q/pal8os2sp.bmp | 12 | 8 | RGB | 8bpp PALETTE | pass |  |
| q/pal8os2v2-16.bmp | 16 | 8 | RGB | refused | refused | 16-byte OS/2 2.x header |
| q/pal8os2v2-40sz.bmp | 40 | 8 | RGB | 8bpp PALETTE | pass |  |
| q/pal8os2v2-sz.bmp | 64 | 8 | RGB | 8bpp PALETTE | pass |  |
| q/pal8os2v2.bmp | 64 | 8 | RGB | 8bpp PALETTE | pass |  |
| q/pal8oversizepal.bmp | 40 | 8 | RGB | 8bpp PALETTE | pass |  |
| q/pal8rlecut.bmp | 40 | 8 | RLE8 | 8bpp PALETTE | pass | same |
| q/pal8rletrns.bmp | 40 | 8 | RLE8 | 8bpp PALETTE | pass | same |
| q/rgb16-231.bmp | 40 | 16 | BITFIELDS | 16bpp RGB | **WRONG** | masks other than 555/565 read as 555 |
| q/rgb16-3103.bmp | 40 | 16 | BITFIELDS | 16bpp RGB | **WRONG** | same |
| q/rgb16faketrns.bmp | 40 | 16 | RGB | 16bpp RGB | pass ±1 | unused bit ignored |
| q/rgb24jpeg.bmp | 124 | 0 | JPEG | refused | refused | BI_JPEG |
| q/rgb24largepal.bmp | 40 | 24 | RGB | 24bpp RGB | pass |  |
| q/rgb24lprof.bmp | 124 | 24 | RGB | 24bpp RGB | pass | linked profile ignored |
| q/rgb24png.bmp | 124 | 0 | PNG | refused | refused | BI_PNG |
| q/rgb24prof.bmp | 124 | 24 | RGB | 24bpp RGB | pixels pass | embedded profile not attached |
| q/rgb24prof2.bmp | 124 | 24 | RGB | 24bpp RGB | **WRONG** | embedded profile ignored (R/G stay swapped) |
| q/rgb24rle24.bmp | 64 | 24 | RLE24 (4) | refused | refused | OS/2 RLE24 |
| q/rgb32-111110.bmp | 40 | 32 | BITFIELDS | 32bpp RGBA +transparent | **WRONG** | 32-bit masks ignored; transparent |
| q/rgb32-7187.bmp | 40 | 32 | BITFIELDS | 32bpp RGBA +transparent | **WRONG** | same |
| q/rgb32-xbgr.bmp | 124 | 32 | BITFIELDS | 32bpp RGBA +transparent | **WRONG** | same |
| q/rgb32fakealpha.bmp | 40 | 32 | RGB | 32bpp RGBA +transparent | alt. pass | matches the suite's 'heuristic' alternative |
| q/rgb32h52.bmp | 52 | 32 | BITFIELDS | 32bpp RGBA +transparent | **WRONG** | 32-bit masks ignored; transparent |
| q/rgba16-1924.bmp | 124 | 16 | BITFIELDS | 16bpp RGB | **WRONG** | 16-bit masks read as 555, alpha mask dropped |
| q/rgba16-4444.bmp | 124 | 16 | BITFIELDS | 16bpp RGB | **WRONG** | same |
| q/rgba16-5551.bmp | 124 | 16 | BITFIELDS | 16bpp RGB | **WRONG** | colours right, alpha mask dropped |
| q/rgba32-1.bmp | 124 | 32 | BITFIELDS | 32bpp RGBA +transparent | pass | standard BGRA order |
| q/rgba32-1010102.bmp | 124 | 32 | BITFIELDS | 32bpp RGBA +transparent | **WRONG** | 32-bit masks ignored |
| q/rgba32-2.bmp | 124 | 32 | BITFIELDS | 32bpp RGBA +transparent | **WRONG** | same |
| q/rgba32-61754.bmp | 124 | 32 | BITFIELDS | 32bpp RGBA +transparent | **WRONG** | same |
| q/rgba32-81284.bmp | 124 | 32 | BITFIELDS | 32bpp RGBA +transparent | **WRONG** | same |
| q/rgba32abf.bmp | 40 | 32 | ALPHABITFIELDS (6) | refused | refused | BI_ALPHABITFIELDS |
| q/rgba32h56.bmp | 56 | 32 | BITFIELDS | 32bpp RGBA +transparent | **WRONG** | 32-bit masks ignored |
| q/rgba64.bmp | 40 | 64 | RGB | refused | refused | 64 bpp |
| b/badbitcount.bmp | 40 | 30000 | RGB | refused | refused (ok) | Invalid file format |
| b/badbitssize.bmp | 40 | 1 | RGB | 1bpp MINISBLACK | no crash | no reference |
| b/baddens1.bmp | 40 | 1 | RGB | 1bpp MINISBLACK | no crash | no reference |
| b/baddens2.bmp | 40 | 1 | RGB | 1bpp MINISBLACK | no crash | no reference |
| b/badfilesize.bmp | 40 | 1 | RGB | 1bpp MINISBLACK | no crash | no reference |
| b/badheadersize.bmp | 66 | 1 | RGB | refused | refused (ok) | Unknown bmp subtype with id 66 |
| b/badpalettesize.bmp | 40 | 8 | RGB | 8bpp PALETTE | no crash | no reference |
| b/badplanes.bmp | 40 | 1 | RGB | refused | refused (ok) | Invalid file format |
| b/badrle.bmp | 40 | 8 | RLE8 | 8bpp PALETTE, warning | no crash | no reference |
| b/badrle4.bmp | 40 | 4 | RLE4 | 4bpp PALETTE | no crash | no reference |
| b/badrle4bis.bmp | 40 | 4 | RLE4 | 4bpp PALETTE | no crash | no reference |
| b/badrle4ter.bmp | 40 | 4 | RLE4 | 4bpp PALETTE | no crash | no reference |
| b/badrlebis.bmp | 40 | 8 | RLE8 | 8bpp PALETTE, warning | no crash | no reference |
| b/badrleter.bmp | 40 | 8 | RLE8 | 8bpp PALETTE, warning | no crash | no reference |
| b/badwidth.bmp | 40 | 1 | RGB | refused | refused (ok) | Invalid file format |
| b/pal8badindex.bmp | 40 | 8 | RGB | 8bpp PALETTE | no crash | no reference |
| b/reallybig.bmp | 40 | 24 | RGB | refused | refused (ok) | The file holds too little data for its size |
| b/rgb16-880.bmp | 40 | 16 | BITFIELDS | 16bpp RGB | wrong | read as 555 (the suite marks its reference '?') |
| b/rletopdown.bmp | 40 | 8 | RLE8 | refused | refused (ok) | Invalid file format |
| b/shortfile.bmp | 40 | 1 | RGB | 1bpp MINISBLACK, warning | no crash | no reference |
| x/ba-bm.bmp | BA + 12 | 8 | RGB | refused | refused | OS/2 bitmap array, though identified as BMP |

## Appendix B — crafted files

Made by `.claude/scratch/bmp-spec/rig/mkextra.py` (37 × 19 pixels unless noted), each with the rendering the spec
gives. "Wrong" counts pixels whose RGB or alpha differs from it.

| File | What it holds | FreeImage | Pillow | Finding |
|---|---|---|---|---|
| `core24_gap0` | 12-byte header, 24-bit, pixels right after it | right | right | — |
| `core24_gap16` | the same with 16 bytes before the pixels | **703 of 703 wrong** | right | L5 |
| `core8_gap16` | 12-byte header, 8-bit, 16-byte gap | right | right | — |
| `core2bpp` | 12-byte header, 2 bpp (not allowed there) | refused, **no message** | refused | S4 |
| `os2v2_16` … `os2v2_64` | OS/2 2.x header of 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64 bytes, 8-bit | 40, 52, 56, 64 right; the other nine refused | same | L6 |
| `v3_rgb32_zero` | 40-byte header, 32-bit `BI_RGB`, fourth byte 0 | **fully transparent** | opaque RGB | L1 |
| `v3_rgb32_onebyte` | the same, one fourth byte = 1 | **transparent but one pixel** | opaque RGB | L1 |
| `v3_bf32_junk` | 40-byte header, `BI_BITFIELDS` standard masks, fourth byte 0x80 | **alpha 128** | opaque RGB | L1 |
| `v4_x8r8g8b8_zero`, `v5_…` | V4/V5, `BI_BITFIELDS`, alpha mask 0, fourth byte 0 | **fully transparent** | opaque RGB | L1 |
| `v4_x8r8g8b8_junk`, `v5_…` | the same, fourth byte non-zero | **semi-transparent** | opaque RGB | L1 |
| `v4_a8r8g8b8`, `v5_…` | V4/V5, masks `00FF0000 0000FF00 000000FF FF000000` | right | right | — |
| `v4_a8b8g8r8`, `v5_…` | V4/V5, masks `000000FF 0000FF00 00FF0000 FF000000` | **679 of 703 wrong** (R/B swapped) | right | L2 |
| `rle8_literal_cross` | 8×2 RLE8, absolute run of 6 starting 3 pixels before the row end | **refused** | wraps 3 pixels into the next row, which loses its own | S1 |
| `rle4_literal_cross` | its RLE4 twin | right | the same as for RLE8 | S1 |
| `rle8_run_cross`, `rle4_run_cross` | encoded run of 11 in an 8-pixel row | right (RLE8 clips, RLE4 wraps then overwrites) | right | S2 |
| `rle8_no_eol` | two full rows, no end-of-line between | second row empty | refused | S2 |
| `cut_before_masks` | 40-byte header, 16-bit `BI_BITFIELDS`, file ends before the masks | loads with **uninitialised masks** | refused | R1 |
| `zero_w`, `zero_h` | width or height 0 | refused, "DIB allocation failed" | refused | S4 |
| `saved/pillow_RGBA.bmp` | an RGBA image saved by Pillow: 40-byte `BI_RGB`, alpha in the fourth byte | RGBA, alpha kept | RGB, alpha lost | L1, W1 |

Real-world files on this machine: `/usr/share/pixmaps/debian-logo.bmp` (V5, 32-bit, `BI_BITFIELDS`, alpha mask
`FF000000`, `LCS_sRGB`, intent `LCS_GM_IMAGES`) loads right, as do three 24- and 8-bit `BITMAPINFOHEADER` files.

## Appendix C — re-running

Everything is in `.claude/scratch/bmp-spec/` (see its `README.txt`): the suite zip (SHA-256 `f1b1f18c…`), the
extracted suite, the verbatim spec quotes (`spec/ms-learn.md`, `spec/wmf-wince-wic.md`, `spec/os2.md`,
`spec/peers.md`) and the tools in `rig/`:

```sh
cd .claude/scratch/bmp-spec/rig
R=../../../..
g++ -O1 -std=c++17 -I$R/Dist bmpdump.cpp $R/Dist/libfreeimage.a -fopenmp -lpthread -lm -ldl -o bmpdump   # likewise bmpsave, dirt
./bmpdump out ../suite/bmpsuite-2.8/{g,q,b,x}/*.bmp > suite.jsonl
python3 -I compare.py ../suite/bmpsuite-2.8 suite.jsonl $PWD > suite_cmp.tsv      # appendix A
python3 -I mkextra.py extra && ./bmpdump outx extra/*.bmp > extra.jsonl
python3 -I cmp_extra.py extra.jsonl extra                                         # appendix B
./bmpsave saved && python3 -I inspect_saved.py saved pal8_rleflag grey8_rle rgba32 pal8_palreserved   # section 5
```
