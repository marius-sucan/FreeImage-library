# Multi-page API audit

`FreeImage_OpenMultiBitmap`, `InsertPage`, `AppendPage`, `DeletePage`, `MovePage`,
`LockPage`, `UnlockPage`, `CloseMultiBitmap` — and what they actually write for each
supported format.

Audited 2026-09-17 on branch `worktree-multipage-audit` (from `qpv` @ `b8cc59d`).
Rig: `.claude/audit/multipage/mp.c`, built against `Dist/libfreeimage.a` from a stock
`make`. Every finding marked **REPRODUCED** has a command under it that shows it on
this build.

## Provenance up front

| File | State vs. FreeImage 3.18.0 |
|---|---|
| `Source/FreeImage/MultiPage.cpp` | one local commit (`f3ed0f7`, decoder reuse) + `auto_ptr`→`unique_ptr`; **everything else verbatim upstream** |
| `Source/FreeImage/CacheFile.cpp` | one local hunk (an `fread` return check); **the rest verbatim upstream** |
| `Source/CacheFile.h` | verbatim upstream |

29 findings below: **14 reproduced** (M1–M12, C1, C2) and **15 by inspection**
(N1–N16, less N9, which reproduced and was promoted to M11).
**27 of the 29 are upstream 3.18.0 defects**, not regressions of this fork. The two
local ones are M10 and N5, both called out as such.

> **Status: every reproduced finding is fixed.**
> M1–M9, M11 in `d79f90e`; M12, N6 in `28178b1`; C1, C2, N1, N2, N11, N14 in `e669ea6`;
> N3, N7, N8, N10, N15, N16 in `6267d80`; **M10 in `a48037f` and `de6aa23`**.
> N4 and N13 turned out to have been fixed already, and **N5 and N12 are not defects** —
> see §5.3. See §5.

---

# 1. Format matrix

## 1.1 All registered formats (from the built library, `mp matrix`)

40 formats, `FIF_BMP`=0 … `FIF_APNG`=39. Every one of them can be read.

**Load + save (25):** BMP, ICO, JPEG, JNG, PBM, PBMRAW, PGM, PGMRAW, PPM, PPMRAW, PNG,
TARGA, TIFF, WBMP, PSD, XPM, GIF, HDR, EXR, J2K, JP2, PFM, WebP, JPEG-XR, APNG.

**Read-only — no `save_proc` at all (15):** KOALA, IFF/LBM, MNG, PCD, PCX, RAS, CUT,
XBM, DDS, G3 (FAXG3), SGI, PICT, RAW, **AVIF**, **HEIF**.

## 1.2 Which formats the multi-page API actually understands

A plugin joins the multi-page API by setting `pagecount_proc`. Exactly **7** do:

| Format | FIF | Multipage read | Multipage save | Animation | Notes |
|---|---|---|---|---|---|
| **TIFF** | 18 | yes | **yes** | no | the reference implementation; pages are IFDs |
| **ICO** | 1 | yes | **yes** | no | pages are icon directory entries |
| **GIF** | 25 | yes | **yes** | **read + write** | `GIF_PLAYBACK` composites frames |
| **APNG** | 39 | yes | **yes** | **read + write** | `APNG_PLAYBACK`; local plugin |
| **WebP** | 35 | yes | **yes** (`a48037f`) | **read + write** (`WEBP_PLAYBACK`) | one ANMF frame per page |
| **AVIF** | 37 | yes | n/a (read-only fif) | **read only** | counts sequence frames |
| **HEIF** | 38 | yes | n/a (read-only fif) | no | counts *top-level images*, not frames |

Verified round-trips (3 pages in → 3 pages back, values intact):
TIFF, GIF, ICO, APNG, WebP. WebP is exact under `WEBP_LOSSLESS`; at the default
quality the pixels move, as they would for any lossy save.

## 1.3 Animation

- **Read + write:** GIF, APNG, WebP. All three carry `FIMD_ANIMATION` metadata
  (`FrameTime`, `Loop`, disposal/blend) and all three have a `_PLAYBACK` load flag.
  WebP's writer was added in `a48037f`; before that it could only read animations.
- **Read only:** AVIF (sets `FrameTime`/`Loop`; the format has no writer here at all).
- **Not animation:** HEIF — its `PageCount` returns the number of top-level images, and
  the plugin never emits `FIMD_ANIMATION`. A multi-image HEIC is a burst/collection,
  not a sequence. TIFF and ICO are multi-image but have no time dimension.
- **`FIMD_ANIMATION` is emitted by exactly 4 plugins:** GIF, APNG, WebP, AVIF.
- **Where the canvas lives:** GIF and APNG attach `LogicalWidth`/`LogicalHeight`/`Loop`
  to page 0 alone, so deleting the first page of an animation discards the only record
  of its canvas. WebP attaches them to every frame (`de6aa23`) precisely so that
  editing a document does not shrink it. GIF and APNG still have this limitation.

## 1.4 The trap: MNG is an animation format this API cannot reach

**MNG** is an animation container, and it sets `open_proc`/`close_proc`, but it leaves
`pagecount_proc = NULL`. It is therefore *not* a multi-page format as far as this API
is concerned: `OpenMultiBitmap(FIF_MNG, …)` reports 1 page. MNG is additionally
read-only. Anyone expecting to walk MNG frames through `LockPage` gets a single image
and no error.

(**JNG** also has `pagecount_proc = NULL`, but that is correct rather than a gap — JNG
is "JPEG Network Graphics", a *single* JPEG-compressed image with PNG-style alpha in a
PNG-style wrapper, not a sequence. The plugin has no notion of frames at all.)

---

# 2. Confirmed findings (reproduced on this build)

Ordered by severity. All repro commands run from `.claude/audit/multipage/`.

## M1 — `LockPage` indexes the *file*, not the page list — **REPRODUCED**

`MultiPage.cpp:736` passes the caller's `page` straight to `load_proc`. It never
consults `header->m_blocks`, which is the only thing that knows about pages you
deleted, inserted or moved in this session.

After deleting page 0 of a 3-page TIFF (values 20/40/60), the live document is
[40, 60] — but `LockPage(0)` hands back the **deleted** page:

```
$ ./mp lockdel 18 t1.tif 0
  after DeletePage(0): GetPageCount=2
    LockPage(0) -> value 20        <-- the deleted page
    LockPage(1) -> value 40
  on disk : pages=2  values=[40 60]   <-- what was really saved
```

A `BLOCK_REFERENCE` page (anything appended or edited this session) can never be
locked at all — there is no file offset for it. Upstream.

## M2 — `UnlockPage(changed=TRUE)` writes the edit to the wrong page — **REPRODUCED**

The other half of M1, and the damaging half. `UnlockPage` maps the locked page back
through `FreeImage_FindBlock(bitmap, header->locked_pages[page])` (`:768`) — a *file*
index used as a *logical* index. So the edit is committed to a different page than the
one the caller was handed.

Delete page 0, lock page 0 (which M1 gives you as the old page 0), paint it 200:

```
$ ./mp unlockedit 18 t2.tif 0 0
  DeletePage(0); LockPage(0) -> value 20; overwrite with 200
  on disk : pages=2  values=[200 60]
```

The surviving page 40 has been **silently destroyed** and replaced. No error is
reported anywhere. Upstream.

## M3 — `MovePage(target, source)` moves the wrong page — **REPRODUCED**

`MultiPage.cpp:813-817` does `block_source = FindBlock(target)`,
`block_target = FindBlock(source)`, then inserts a copy of *block_source* before
*block_target* and erases the original. The net effect is **"move the page at index
`target` to immediately before index `source`"** — the two parameters are used the
opposite way round from their names.

FreeImage.h carries no doc comment for these functions, but the official .NET wrapper
in this repo states the contract exactly
(`Wrapper/FreeImage.NET/cs/UnitTest/FreeImage.cs:4879-4887`):

```
/// Moves the source page to the position of the target page.
/// <param name="target">New position of the page.</param>
/// <param name="source">Old position of the page.</param>
```

So `source` is where the page is now and `target` is where it should end up. The code
does the opposite.

```
$ ./mp move 18 t3.tif 0 2
  before  : pages=4  values=[20 40 60 80]
  MovePage(target=0, source=2) -> 1
  after   : pages=4  values=[40 20 60 80]
```

Documented behaviour would put page 2 (value 60) at position 0 → `[60 20 40 80]`.
Actual behaviour moved page 0. It returns `TRUE`, so callers cannot detect it.
Upstream.

## M4 — Saving to a read-only format calls a NULL pointer — **REPRODUCED (SIGSEGV)**

`FreeImage_SaveMultiBitmapToHandle` (`:428` and `:456`) calls
`node->m_plugin->save_proc(...)` without ever checking it is non-NULL. For any of the
15 read-only formats that is a call through a null function pointer.

```
$ ./mp savefif 37
  FIFSupportsWriting(AVIF) = 0
  SaveMultiBitmapToMemory(AVIF, 3-page TIFF) ...
  Segmentation fault
```

```
#0  0x0000000000000000 in ?? ()
#1  FreeImage_SaveMultiBitmapToHandle ()
#2  FreeImage_SaveMultiBitmapToMemory ()
```

Reproduced for AVIF (37), HEIF (38) and RAW (34); the same applies to the other 12.
Note `FreeImage_FIFSupportsWriting` returns the correct answer (0) two lines away —
the information needed to refuse is already there and simply is not consulted.
Upstream.

## M5 — `DeletePage` past the end aborts the process — **REPRODUCED (SIGABRT)**

`FreeImage_FindBlock` ends in `assert(false)` (`MultiPage.cpp:227`) when the position
is not found. Of the seven GNU-toolchain makefiles only `Makefile.mingw` defines
`NDEBUG` — `Makefile.gnu` (which builds the Linux `.so`/`.a` used here), `.osx`,
`.solaris`, `.cygwin`, `.iphone` and `.fip` do not. So on those platforms **asserts
are live in the release build** and an out-of-range page number takes down the host
process rather than returning an error. (MinGW, and MSVC Release configurations by
convention, get a silent no-op instead — which is its own problem.)

```
$ ./mp negpage 18 n2.tif 99
  pages=3; calling DeletePage(99)
mp: Source/FreeImage/MultiPage.cpp:227: ... Assertion `false' failed.
Aborted (core dumped)
```

`DeletePage` bounds-checks nothing; it only tests `GetPageCount(bitmap) > 1`. Upstream.

## M6 — `DeletePage` with a negative index corrupts the block list — **REPRODUCED**

With `page = -1`, `FindBlock` computes `item = start + (-1 - prev_count)` and splits the
run into blocks with negative extents. `PageBlock::getPageCount()` then returns a
negative page count for one of them, so the totals stop matching the file.

```
$ ./mp negpage 18 n1.tif -1
  pages=3; calling DeletePage(-1)
  survived; GetPageCount=2
  on disk : pages=3  values=[20 40 60]
```

`GetPageCount` reports **2**; the file that `CloseMultiBitmap` then writes has **3**
pages. The in-memory model and the output permanently disagree. Upstream.

## M7 — `LockPage(bitmap, -1)` returns page 0 instead of NULL — **REPRODUCED**

`-1` is the plugins' internal "not a page, save/load as a single image" sentinel.
`LockPage` forwards it unchecked, so TIFF skips its `TIFFSetDirectory` and serves
directory 0:

```
$ ./mp locknegpage 18 n3.tif -1
  LockPage(-1)... -> non-NULL (value 20)
```

Other negative values are safe — APNG, AVIF, GIF, HEIF and WebP all test `page < 0` in
`Load`, and `-5` returns NULL everywhere (checked on ICO/TIFF/GIF/APNG). Only the
exact value `-1` leaks through. Upstream.

## M8 — Single-image formats are accepted and silently concatenated — **REPRODUCED**

`OpenMultiBitmap` never asks whether the format supports multiple pages (there is no
`FreeImage_FIFSupportsMultiPage` in the public API at all). With `create_new=TRUE` any
format is accepted, `AppendPage` works, and `CloseMultiBitmap` calls the single-image
`save_proc` once per page against one handle:

```
$ ./mp nonmp 0 nm.bmp 3
  nonmp BMP: accepted, GetPageCount=3, Close=1
    output 4002 bytes; re-read: pages seen = 1
```

Same for JPEG, PNG, TARGA and PSD. The output is N complete files glued together;
every reader sees only the first. **`CloseMultiBitmap` returns success.** Upstream.

## M9 — `AppendPage` fails silently and `Close` still reports success — **REPRODUCED**

`AppendPage`/`InsertPage`/`DeletePage` return `void`. When the cache round-trip cannot
encode the page, the append is dropped on the floor:

```
$ ./mp nonmp 29 nm.exr 3
  [FI] EXR: Cannot save: invalid data type. ...   (x3)
  nonmp EXR: accepted, GetPageCount=0, Close=1
```

Three appends, zero pages, **no output file at all**, and `CloseMultiBitmap` returns
`TRUE`. Same with HDR. `changed` is never set, so `Close` has nothing to write and
calls that a success. Upstream.

## M10 — WebP multi-page save produces concatenated files — **REPRODUCED — LOCAL**

`PluginWebP.cpp`'s `Save` (`:845`) never reads its `page` argument. It calls
`WebPMuxSetImage` (which *replaces* the mux's single image), `WebPMuxAssemble`, and
writes a **complete WebP file** to the stream — once per page.

```
$ FI_BPP=24 ./mp mk 35 w3.webp 3
  mk: WebP pages=3 -> w3.webp
  readback: pages=1  values=[20]

$ grep -aob RIFF w3.webp
0:RIFF
44:RIFF
88:RIFF
```

Three RIFF containers in one file; readers see page 1 and the rest is trailing garbage.

**This one is local.** Upstream 3.18.0 has `plugin->pagecount_proc = NULL` for WebP, so
the format was never offered to this API. The animated-WebP reading work (`ed09abe`)
set `pagecount_proc = PageCount` to expose frames for *reading* and thereby also
advertised a multi-page *write* path that does not exist.

## M11 — `InsertPage` accepts a negative index and no-ops at the end — **REPRODUCED**

Placement itself is correct for valid indices (0, 1, 2 on a 3-page document all land
where they should). The two edges are not:

```
$ ./mp insert 18 i.tif -1
  before : pages=3  values=[20 40 60]
  InsertPage(page=-1, value 199); count before=3
  count after=4
  after  : pages=4  values=[199 20 40 60]     <-- silently inserted at the front

$ ./mp insert 18 i.tif 3
  InsertPage(page=3, value 199); count before=3
  count after=3
  after  : pages=3  values=[20 40 60]          <-- silently discarded
```

`MultiPage.cpp:654` rejects `page >= GetPageCount` but not negatives, and `page < 0`
then falls through `if (page > 0)` into `push_front`. Inserting at the end is a no-op
by design (that is what `AppendPage` is for) — but because the function returns `void`,
a caller that passes a stale count loses the page with no indication at all. Upstream.

## M12 — `OpenMultiBitmap` accepts a format it can never write — **REPRODUCED**

`OpenMultiBitmap` checks only that a plugin node exists for the `fif`. It never asks
whether that plugin can read, or — for `create_new` — whether it can write anything at
all. `FreeImage_LoadFromHandle` requires `load_proc` and `FreeImage_SaveToHandle`
requires `save_proc`; this entry point asks neither.

So a brand new multi-bitmap can be opened in any of the 15 read-only formats. It
returns an ordinary-looking handle, and nothing can ever come of it:

```
$ ./mp openmodes
  FIF      mode                     OpenMultiBitmap
  AVIF     create_new               ACCEPTED       <-- AVIF has no writer at all
  HEIF     create_new               ACCEPTED
  RAW      create_new               ACCEPTED
  DDS      create_new               ACCEPTED
```

An invalid or unregistered `fif` (including `FIF_UNKNOWN`) already returns NULL — the
`m_plugin_map` lookup fails — so that half was never broken. Upstream.

## C1 — CacheFile: block 0 is both a valid block and the end-of-chain marker — **REPRODUCED**

`Block::next == 0` means "end of chain" (`CacheFile.cpp:221`, `:278`), but
`allocateBlock` hands out `m_page_count++` starting at **0**, so 0 is also a perfectly
ordinary block number. While block 0 is the *head* of a chain this is harmless. Once
it is freed and re-allocated as a *continuation*, `readFile` stops at it and the tail
of the page is never copied back.

Three-way controlled experiment — the only variable is the order of two deletes, which
decides whether block 0 is re-used as a head or as a continuation:

```
$ FI_NODEL=1 ./mp blockzero 200        # A: no deletes, free list empty
  pages=4, close=1
    page 3: ok  200x200 checksum=3800015765  expected=3800015765  MATCH

$ FI_DEL01=1 ./mp blockzero 200        # B: free list [0,1] -> 0 becomes the HEAD
  pages=2, close=1
    page 1: ok  200x200 checksum=3800015765  expected=3800015765  MATCH

$ ./mp blockzero 200                   # TEST: free list [1,0] -> 0 is a CONTINUATION
  [FI] TIFF: Error while opening TIFF: data is invalid
  pages=2, close=0 *** SAVE FAILED ***
  bz.tif NEVER CREATED - the whole multibitmap was lost
```

The truncated blob fails `FreeImage_LoadFromMemory`, `save_proc` gets a NULL `dib`, and
`CloseMultiBitmap` returns FALSE. Because `Close` does `remove(spool_name)` on failure,
the damage is never confined to the one bad page: with `create_new=TRUE` (above) **the
output file is never created and every page is lost**; in an edit session
(`create_new=FALSE`) the original file is left untouched, so **every change made in the
session is discarded**. Either way the caller loses the whole document, and the only
signal is the `FALSE` return.

Trigger recipe: any session that deletes pages and then appends a page large enough to
need more than one 64 KB cache block, with the freed block 0 not at the head of the
free list. Upstream.

## C2 — Cache and spool filenames collide on the stem — **REPRODUCED**

`ReplaceExtension` builds the cache name by swapping the extension, so the cache for
`x.tif` and for `x.tiff` is the *same* `x.ficache`, opened `"w+b"` (truncating) by
both. The spool file `x.fispool` collides the same way.

```
$ ./mp cachename 40
    cache file: coll.ficache          <-- one file, two open multibitmaps
  opened coll.tif and coll.tiff, both rw, cache on DISK
  [FI] TIFF: Error while opening TIFF: data is invalid
  appended 40 pages to each; close tif=0 close tiff=1
  coll.tif   pages=-1 (expect 40) unreadable=0 corrupt=0
  coll.tiff  pages=40 (expect 40) unreadable=0 corrupt=0
```

`coll.tif` loses **all 40 pages** and is never created. `.tif`/`.tiff` in one directory
is an ordinary situation. It only bites once the 32-block memory cache overflows and
blocks are really written to disk, which is why small documents appear to work. The
same collision applies across processes and to any two formats sharing a stem
(`a.tif`/`a.gif`). Upstream.

---

# 3. By inspection (not reproduced — no repro built, or not reachable from the public API)

| # | Where | Issue |
|---|---|---|
| N1 | `CacheFile.cpp:212-214` | `readFile` does not check `lockBlock` for NULL before `block->next`. `lockBlock` returns NULL when the nr is absent from `m_page_map` or the `fread` fails — both mean a null deref. C1 is the reachable instance of a broken chain. |
| N2 | `CacheFile.cpp:97,149` | `old_block->nr * BLOCK_SIZE` is `unsigned * int` → **32-bit** arithmetic. It wraps at 2³²/65528 ≈ 65545 blocks ≈ **4.29 GB** of cache, after which reads and writes land at the wrong offset. |
| N3 | `CacheFile.cpp:230` | `writeFile(BYTE*, int size)` is fed a `DWORD compressed_size`. A cached page above 2 GiB becomes negative, `size > 0` fails, and the function returns 0 — which is also a valid block number. |
| N4 | `CacheFile.cpp:260` | `writeFile` returns `0` for failure, and `0` is the legitimate nr of the first block ever allocated. `SavePageToBlock` cannot tell the two apart (and does not check). Same root cause as C1: **0 is overloaded**. |
| N5 | `MultiPage.cpp:497` | *(local)* `read_data` now lives for the life of the multibitmap, while the public `SaveMultiBitmapToHandle`/`ToMemory` open a *second* decoder on the same handle. `CloseMultiBitmap` sequences this correctly; the public entry points do not. **I tried to break this on TIFF and GIF and could not** (`./mp savelock 18` / `25` both return correct data), so it is a latent ordering hazard, not a demonstrated bug. |
| N6 | `MultiPage.cpp:776-790` | `UnlockPage` ignores the return of `FreeImage_OpenMemory`, `FreeImage_SaveToMemory` and `FreeImage_AcquireMemory`. On failure it writes `PageBlock(BLOCK_REFERENCE, 0, 0)` — reference to block 0, size 0. `FreeImage_SavePageToBlock` checks all three; the two paths are inconsistent. |
| N7 | `MultiPage.cpp:536-537` | `remove(m_filename)` then `rename(spool, m_filename)`. If the rename fails the original is already gone. POSIX `rename` replaces atomically and needs no `remove`. |
| N8 | `MultiPage.cpp:634,650,675` | `AppendPage`, `InsertPage` and `DeletePage` return `void`, so "the page was locked", "the cache is full" and "this format cannot encode that bitmap" are all indistinguishable from success (M9). |
| N10 | `MultiPage.cpp:257` | `OpenMultiBitmap` never calls `FreeImage_ValidateFIF`. Opening a GIF as `FIF_TIFF` succeeds, returns a non-NULL handle with `GetPageCount()==0`, and with `read_only=FALSE` will happily overwrite the file on close. (Confirmed no crash: `./mp wrongfif 18 real.gif`.) |
| N11 | `CacheFile.cpp:183-201` | `deleteBlock` erases the `m_page_map` entry but leaves the `Block` in `m_page_cache_mem`. A later `cleanupMemCache` can flush that stale block to disk at an offset now owned by a *different* block. Ordering (LRU flushes the stale one first) makes this hard to hit — **my 60-page disk-cache stress with deletes and re-appends found no corruption**, so this is a latent hazard only. |
| N12 | `MultiPage.cpp:613,778` | The cache round-trips every page through `SaveToMemory(cache_fif, …, 0)` — the file's own format at default flags. For lossy formats (WebP, JXR, JPEG) that is a full generation of loss *before* the final save; for all formats it silently drops whatever the single-page writer cannot carry (e.g. `FIMD_ANIMATION`). |
| N13 | `MultiPage.cpp:425` | `SaveMultiBitmapToHandle` calls `load_proc` without the NULL check `LockPage` applies, and passes the resulting `dib` to `save_proc` without checking it either. |
| N14 | `CacheFile.cpp:232` | `nr_blocks_required = 1 + (size / BLOCK_SIZE)` allocates one block too many when `size` is an exact multiple of `BLOCK_SIZE`. Wasteful, not incorrect. |
| N15 | `PluginICO.cpp:295` | ICO's `PageCount` returns **1** for `data == NULL`; the other six return 0. After a failed `open_proc` an ICO multibitmap claims one page that cannot be loaded. |
| N16 | `Makefile.gnu:39,69` | Only `Makefile.mingw` defines `NDEBUG`; `Makefile.gnu`, `.osx`, `.solaris`, `.cygwin`, `.iphone` and `.fip` do not, so every `assert` in the library is live in those release builds. This is what turns M5 from a bad return value into a process abort. |

---

# 4. Ranked fixes (as assessed before any were made)

1. **M4** — one NULL check on `save_proc`; turns a SIGSEGV into `FALSE`. Trivial.
2. **M5/M6/M7** — bounds-check `page` in `DeletePage`, `LockPage`, `InsertPage` and
   return failure instead of asserting. Also define `NDEBUG` (N16).
3. **C1/N4** — make `allocateBlock` start at 1 (or use `-1`/`~0u` as the chain
   terminator) so 0 stops meaning two things. Small, and it removes a whole-document
   loss.
4. **C2** — derive the cache/spool name from the *full* filename plus the pid, or use
   `mkstemp`.
5. **M1/M2** — make `LockPage`/`UnlockPage` go through `m_blocks` like every other
   entry point. This is the deepest change and the one that fixes silent data loss.
6. **M8/M10** — refuse `OpenMultiBitmap` when `pagecount_proc == NULL`, and either
   teach `PluginWebP`'s `Save` to build an animation across `page` calls or set
   `pagecount_proc = NULL` again for writing. Add a public
   `FreeImage_FIFSupportsMultiPage`.
7. **M3** — cannot be fixed without breaking callers who compensated for it; document
   the real behaviour, or add a correctly-named replacement.
8. **M9/N8** — give the three `void` mutators a `BOOL` return (or an
   `..._Ex` variant).

---

# 5. Fixes applied

`d79f90e` — *MultiPage: fix the ten reproduced defects in the page API*
(`Source/FreeImage/MultiPage.cpp` only; no other file changed).

| # | Status | What changed |
|---|---|---|
| M1 | **fixed** | `LockPage` resolves the page number through the block list via a new non-mutating `FreeImage_FindPage`, and reads `BLOCK_REFERENCE` pages out of the cache. A page can now also be locked on a `create_new` bitmap. |
| M2 | **fixed** | Falls out of M1: `UnlockPage` already resolved the *logical* index, so once `LockPage` hands back the logical page the edit lands where the caller expects. |
| M3 | **fixed** (behaviour change) | `MovePage` now moves the page at `source` to position `target`, taking it out of the list before locating the destination. |
| M4 | **fixed** | `SaveMultiBitmapToHandle` refuses a destination with no `save_proc`, and one with no `pagecount_proc` when there is more than one page. `load_proc`, the loaded dib and the cache read are checked as well. |
| M5 | **fixed** | `DeletePage` bounds-checks; the `assert(false)` in `FindBlock` is gone, so a bad page number can no longer abort the host process. |
| M6 | **fixed** | Same bounds check — negative indices can no longer build blocks with negative extents. |
| M7 | **fixed** | `LockPage` bounds-checks, so `-1` no longer reaches the plugins as their single-image sentinel. |
| M8 | **fixed** | `AppendPage`/`InsertPage` refuse a second page on a format whose plugin has no `pagecount_proc`; the file written is a valid one-page file. |
| M9 | **fixed** (behaviour change) | Dropped operations are reported through `FreeImage_OutputMessageProc` and recorded, and `CloseMultiBitmap` returns `FALSE` instead of `TRUE`. |
| M10 | **fixed** (`a48037f`, `de6aa23`) | `Save` collects a frame per page and `Close` assembles the animation. The cache stores WebP losslessly, and the canvas now travels on every frame. See §5.4. |
| M11 | **fixed** | `InsertPage` rejects a negative position instead of silently inserting at the front, and says so when asked to insert at or past the end. |
| M12 | **fixed** (`28178b1`) | `OpenMultiBitmap` requires a `load_proc` to open an existing file and a `save_proc` for `create_new`, so a new multi-bitmap can no longer be opened in a format that has no writer. `OpenMultiBitmapFromHandle`/`LoadMultiBitmapFromMemory` require the loader only. |
| N6 | **fixed** (`28178b1`) | `UnlockPage` now checks that the page really was encoded into the cache before replacing the block, instead of writing a reference to block 0 of length 0. |
| C1 | **fixed** (`e669ea6`) | Block numbers start at 1, so `Block::next == 0` can only ever mean end-of-chain. `writeFile`'s `0` is now an unambiguous failure return, and both callers check it. |
| C2 | **fixed** (`e669ea6`) | The cache and spool are named after the whole filename plus the process id and the header's address, instead of the stem alone. |
| N1 | **fixed** (`e669ea6`) | `readFile` checks `lockBlock` for NULL, bounds the copy to the caller's buffer, and returns FALSE for a chain that ends early. |
| N2 | **fixed** (`e669ea6`) | `(long)nr * BLOCK_SIZE` — the product used to be computed in 32 bits. |
| N11 | **fixed** (`e669ea6`) | `deleteBlock` takes the block out of the list holding it and frees it, instead of leaving it to be flushed to a reused offset. |
| N14 | **fixed** (`e669ea6`) | `writeFile` rounds the block count up instead of always adding one. |
| N3 | **fixed** (`6267d80`) | A page over 2 GiB once encoded is refused with a message. `writeFile` takes an `int`, and the `DWORD` used to arrive there negative. |
| N4 | **was already fixed** | Closed by `e669ea6` (block numbers start at 1). The open-list above was stale. |
| N5 | **not a defect** | Could not be reproduced — see §5.3. |
| N7 | **fixed** (`6267d80`) | `Close` no longer removes the original before renaming the spool over it (POSIX replaces atomically); on a failed rename the original is intact and the spool is cleaned up. The `remove` is kept under `#ifdef _WIN32`, where `rename` will not replace. |
| N8 | **fixed** (`6267d80`) | New `FreeImage_AppendPageEx`, `FreeImage_InsertPageEx` and `FreeImage_DeletePageEx` return `BOOL`. The `void` originals now delegate to them, so nothing existing changes. |
| N10 | **fixed** (`6267d80`) | An existing file that yields no page is refused at open instead of returning a handle whose every `LockPage` is NULL. |
| N12 | **not a defect** | The claim was wrong — see §5.3. |
| N13 | **was already fixed** | Closed by `d79f90e` (`load_proc` and the loaded dib are both checked). The open-list above was stale. |
| N15 | **fixed** (`6267d80`) | ICO's `PageCount` returns 0 when its `Open` failed, like the other six. |
| N16 | **fixed** (`6267d80`) for the reachable aborts | Three asserts a malformed file could reach are gone; `NDEBUG` is still not defined, by decision — see §5.3. |

## The two behaviour changes, in full

Both can break a caller that was written against the old behaviour, so they are worth
stating plainly:

1. **`FreeImage_MovePage(bitmap, target, source)`** used to move the page at `target`
   to just before `source`. It now moves the page at `source` to position `target`.
   An adjacent swap — `MovePage(m, 1, 0)`, which is what the .NET sample does — gives
   the same result either way; every other move does not.
2. **`FreeImage_CloseMultiBitmap`** now returns `FALSE` if any page operation was
   dropped (read-only bitmap, a locked page, an image `cache_fif` cannot encode, a
   non-multipage format asked for a second page, an out-of-range index), where it
   used to return `TRUE`. This reaches the .NET wrapper: `FreeImageBitmap`'s
   `SaveAdd` does `AppendPage` then throws on a `FALSE` close, so adding a page to a
   single-image file now raises instead of quietly appending a second file to it.

## Verification

- **`testAPI` output is byte-identical** before and after the change — including
  `testMultiPage`, `testStreamMultiPage` and `testMultiPageMemory`. The
  `testThumbnail.cpp:132` abort it ends on is pre-existing: it reproduces on a
  library built from the unmodified `MultiPage.cpp`.
- **`TestAPI/APNG`**: `regress` 27/27, `robust` 19/19 (3165 damaged inputs).
- **`mp scenario`** applies delete → append → insert → move to a 5-page document and
  checks that an independent model, every `LockPage` and the file `Close` writes all
  agree. Passes on TIFF, GIF, ICO and APNG — the two pages it locks after the append
  and insert are cache-backed, which was not possible at all before.
- Round-trips (3 pages in, 3 pages back) still correct for TIFF, GIF, ICO, APNG.

## What the cache stress test found (`cachefuzz`)

`.claude/audit/multipage/cachefuzz.cpp` links `CacheFile.cpp` directly, with a stub
for `FreeImage_OutputMessageProc` and nothing else of the library, so it can drive the
block store far harder than a document can and run under ASan+UBSan. It does the exact
C1 shape, then ~5200 writes, ~3000 reads and ~2000 deletes across memory- and
disk-backed caches with single- and multi-block payloads.

Against the **fixed** code: clean, ASan and UBSan included.

Against the **unfixed** code it fails four different ways, which is what makes it
worth keeping — two of these were only ever "by inspection" before:

```
the C1 shape:
  first three block numbers: 0, 1, 2 *** 0 IS IN USE ***
  two-block payload came back CORRUPT
  memory cache, small          writeFile returned 0 (size 65528)
  disk cache, small            readFile returned WRONG DATA (ref 21, 196584 bytes)     [x22]
  disk cache, small            final data WRONG (ref 1)

AddressSanitizer: negative-size-param: (size=-65528)
  #2 CacheFile::readFile(unsigned char*, int, int) CacheFile.cpp:216
```

- The `negative-size-param` is **N1**, and it is a memory-safety bug rather than the
  robustness gap it was filed as: a chain longer than the page it holds drives
  `size - s` negative and the `memcpy` length to a huge `size_t`.
- The 22 `WRONG DATA` reads are **N11**, the stale block flushed to a reused offset.
  I could not reach it through the multi-page API in the original audit and recorded
  it as a latent hazard; driven directly it is plain, reproducible corruption.
- `writeFile returned 0` on a legitimate write is **N4**, block 0 being handed out and
  mistaken for the failure return.

---

## 5.3 The four that were not what the audit said

Six of the ten findings this pass examined were real and are fixed. The other four are
worth recording carefully, because two of them were simply wrong.

### N12 — withdrawn, the claim was mine and it was wrong

N12 said the cache "silently drops whatever the single-page writer cannot carry (e.g.
`FIMD_ANIMATION`)". It does not. Every page goes into the cache through
`SaveToMemory(cache_fif, dib, hmem, 0)` and comes back through `LoadFromMemory`, and
that trip preserves `FrameTime`, `FrameLeft`, `FrameTop` and `DisposalMethod` on both
GIF and APNG — tested end to end through `AppendPage`, and again on the round trip in
isolation (`ncheck n12`).

My first run did show them being lost, and that was a bug in the test, not the library:
each `FIMD_ANIMATION` tag has one type the plugins will accept, and
`FreeImage_GetMetadataEx` filters on it. `FrameLeft` written as `FIDT_LONG` is invisible
to a GIF writer that asks for `FIDT_SHORT`, so the writer never saw the tags at all.
Written as `FIDT_SHORT` (and `DisposalMethod` as `FIDT_BYTE`) everything survives.
`FrameTime` appeared to survive the first run only because it really is `FIDT_LONG`.

The other half of N12 — a generation of lossy loss before the final save — cannot happen
either: `cache_fif` is the file's own format, and every format this API can write
(TIFF, ICO, GIF, APNG) is lossless. The one lossy multi-page format, WebP, cannot be
written at all (M10).

### N5 — real hazard, no reproduction

`LockPage` keeps a decoder open for the life of the multi-bitmap while the public
`SaveMultiBitmapToHandle`/`ToMemory` open a second one on the same handle. Locking a
page, saving, and locking again returns correct data on TIFF, GIF, ICO and APNG, with
the saves succeeding (`ncheck n5`); every plugin seeks to the page it wants before
reading it, so the two decoders do not disturb each other.

Left alone. The fix would be to reuse `header->read_data` when it is already open, which
is tidier but changes a path that works today for no demonstrated gain.

### N16 — real, reproduced, and fixed where it bites

A crafted PSD carrying an EXIF *3* image resource and no EXIF 1 **aborts the process**:

```
$ python3 mkpsd_exif3.py base.psd exif3.psd
$ ./ncheck n16 exif3.psd 20
ncheck: PSDParser.cpp:2081: psdParser::Load(...): Assertion `false' failed.
Aborted (core dumped)
```

The `assert(false)` was not guarding anything — the two lines under it read the resource
correctly, and the comment above it says only that the author had not found such a file.
Two more asserts a file's own contents can reach went with it: `PluginJXR.cpp`'s
`default:` over Exif value types, and `PluginTIFF.cpp`'s `assert(Bpc <= 2)` on CMYK,
which is *not* a note — `Bpc` indexes the copy loops below it — so that one became a
real check that refuses the image.

`NDEBUG` is still not defined outside `Makefile.mingw`, deliberately. Defining it would
close the remaining asserts across the bundled decoders in one line, but it would also
have turned that TIFF assert into a silently wrong stride rather than an abort. Now that
the three reachable ones are real code, defining it is a safe follow-up rather than a
prerequisite — but it wants an audit of the remaining asserts first, and `PluginTARGA.cpp`
has nine.

### N3 — real, not reproducible here

`writeFile` takes an `int` and is handed a `DWORD`. A page whose encoded form exceeds
2 GiB arrives negative, and `PageBlock` could not describe it anyway. Since `e669ea6`
that already failed safely rather than corrupting anything, so what was left was the
absence of a diagnostic; there is now an explicit check and a message naming the limit.
Reproducing it needs a bitmap that encodes to more than 2 GiB, and this machine has 2 GB
of RAM free, so this one is settled by inspection.


## 5.4 WebP: what it took to make the page API work

`Save` ignored its `page` argument, assembled a complete WebP file and wrote it on
every call, so a three-page save produced three whole files concatenated. This was a
*local* regression: `ed09abe` gave the plugin a `pagecount_proc` so a caller could read
animation frames, and thereby advertised a multi-page writer that had never existed.

The writer now collects a frame per `Save(page >= 0)` and assembles the animation in
`Close()`, reading each frame's `FIMD_ANIMATION` back into a `WebPMuxFrameInfo` — the
exact inverse of the `SetFrameMetadata` the loader already had. The first frame is held
rather than pushed, because on its own it is still a lone image; the arrival of a second
is what makes the output an animation.

Three things were not obvious:

- **A still WebP cannot hold a frame's duration or position.** Every page travels
  through the multi-page cache as a single-image save, so a page went in with
  `FrameTime` and came back without it (`ncheck n12` showed `340 -> -1` before this
  work; GIF and APNG showed no such loss). **Behaviour change:**
  `FreeImage_Save(FIF_WEBP, dib)` now writes a *one-frame animation* rather than a still
  when the bitmap carries frame tags. `PluginAPNG.cpp` already did exactly this, for
  exactly this reason.
- **The cache was adding a generation of lossy compression.** `cache_fif` is the file's
  own format, so a page was encoded lossily into the cache and lossily again into the
  file. The cache now uses `WEBP_LOSSLESS`; WebP is the only multi-page format with
  anything lossy to turn off.
- **The canvas has to travel with every frame.** See §1.3.

Two format constraints are worth knowing rather than fixing: WebP stores frame offsets
in even pixels only (the mux snaps with `offset &= ~1`, so the writer rounds too), and
`WebPMuxAssemble` refuses an animation whose frames fall outside its canvas — which is
why the canvas is widened to cover the frames rather than enforced, `Close()` having no
way to report a refusal.

**Cross-checked against a different implementation.** `webpanim.py` drives Pillow, whose
WebP encoder and decoder are not FreeImage's. FreeImage reads a four-frame animation
Pillow wrote with the right durations, canvas and loop count; rewrites it through
`SaveMultiBitmapToMemory`; and Pillow reads the result back with the same frame count,
durations, canvas, loop count and exact pixels. The saved file also survives a
`WEBP_PLAYBACK` read, which goes through libwebp's demuxer rather than its mux.


# 6. Rig

`.claude/audit/multipage/mp.c` — single binary, one subcommand per finding.

```
gcc -g -O0 -o mp mp.c -I../../../Dist ../../../Dist/libfreeimage.a \
    -lstdc++ -lm -lpthread -fopenmp

# the cache stress test links CacheFile.cpp on its own, under the sanitizers
g++ -g -O1 -fsanitize=address,undefined -I../../../Source -I../../../Source/FreeImage \
    -D__ANSI__ cachefuzz.cpp ../../../Source/FreeImage/CacheFile.cpp -o cachefuzz

gcc -g -O0 -o ncheck ncheck.c -I../../../Dist ../../../Dist/libfreeimage.a \
    -lstdc++ -lm -lpthread -fopenmp
gcc -shared -fPIC -o renamefail.so renamefail.c -ldl
```

| Subcommand | Finding |
|---|---|
| `matrix` | §1.1 read/write capability straight from the library |
| `mk <fif> <file> <n>` | round-trip check (`FI_BPP=24` for WebP), M10 |
| `lockdel <fif> <file> <delpage>` | M1 |
| `unlockedit <fif> <file> <del> <lock>` | M2 |
| `move <fif> <file> <target> <source>` | M3 |
| `insert <fif> <file> <at>` | M11 |
| `savefif <dstfif>` | M4 |
| `negpage <fif> <file> <page>` | M5, M6 |
| `locknegpage <fif> <file> <page>` | M7 |
| `nonmp <fif> <file> <n>` | M8, M9 |
| `openmodes` | M12 — the (format, mode) accept/refuse matrix |
| `blockzero [dim]` | C1 (`FI_NODEL=1`, `FI_DEL01=1` are the controls) |
| `cachename [n]` | C2 |
| `cachefuzz` (separate binary) | C1, N1, N2, N4, N11, N14 — drives `CacheFile` directly under ASan |
| `ncheck n5 / n8 / n10 / n12 / n15` | N5, N8, N10, N12, N15 |
| `webpanim.py make`, then `ncheck anim 35 <in> [out]` | M10 — walks an animation, rewrites it, and reads it back composited; `webpanim.py show` checks the result with Pillow |
| `ncheck canvas 35 c.webp` | M10 — the canvas after the page that declared it is deleted |
| `ncheck n7prep` then `ncheck n7edit` under `renamefail.so` | N7 — makes `rename()` fail on demand |
| `mp mk 20 base.psd 1`, then `mkpsd_exif3.py base.psd exif3.psd`, then `ncheck n16 exif3.psd 20` | N16 — a PSD that aborts the unfixed library |
| `cachestress <n>` | C1/N11 through the public API (`FI_MEMCACHE=1` for the memory cache) |
| `savelock <fif> <file>` | N5 |
| `wrongfif <fif> <file>` | N10 |

Note when reading the harness: GCC evaluates function arguments right-to-left, so
`printf("%d %d", GetPageCount(m), CloseMultiBitmap(m))` closes the bitmap before
counting it. Two early "crashes" in this audit were that mistake in the rig, not
FreeImage — the calls are sequenced explicitly now.
