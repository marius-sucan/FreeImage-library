# Audit: Source/FreeImage/ — bug report

Branch `qpv` @ fadba4c. Started 2026-09-16.
Scope: the 75 files in `Source/FreeImage/` (46,002 lines). Bundled third-party
libraries (`Source/Lib*`, `Source/OpenEXR`, `Source/ZLib`) are out of scope.

Confidence key:
- **CONFIRMED** — reproduced here (ASan/UBSan output or a repro program).
- **PLAUSIBLE** — read from the code, with the exact reasoning given, not run.


## Summary

48 findings in 20 of the 75 files. **25 are confirmed** — reproduced here against an
ASan+UBSan build, with the sanitizer output quoted in each entry; the rest are read from the
code with the reasoning given.

> **All 25 confirmed findings are fixed**, in 25 commits — one per finding, except that
> findings 43 and 44 share a commit (they are one defect in `PluginPICT.cpp` seen from two
> sides, and fixing either alone leaves the other broken) and findings 18, 21 and 39 each
> take two, because the allocator change they share is its own commit. Each entry below
> carries the commit that fixed it. Every reproducer that crashed or hung was re-run
> afterwards, and every valid file in the corpus was checked to decode to the same bytes as
> before — see "Verifying the fixes" at the end.
>
> The 23 *plausible* findings are **not** fixed.

Severity, for the confirmed ones:

| | Finding |
|---|---|
| **Out-of-bounds write, attacker-controlled content** | 42 XPM `sprintf` stack smash · 18 DDS whole-file-into-one-scanline · 21 ICO 4 GiB read request · 1 CUT wild write · 43/44 PICT unpackers · 39 HDR negative width · 33 TARGA RLE guard · 13 IFF PBM RLE · 5 RAS row loop · 46 XBM off-by-one |
| **Out-of-bounds read** | 35 PSD `UnpackRLE` · 29 XPM pixel row · 34 TARGA `IOCache` |
| **Infinite loop (denial of service)** | 25 PICT (472 zero bytes) · 27 WBMP (**2 bytes**) · 47 XBM · 14 IFF chunk walk · 5 RAS uncompressed |
| **Null dereference / crash** | 30 XPM unchecked allocation |
| **Resource exhaustion** | 26 DDS: 131 bytes → 268 MB and 33 M I/O calls |
| **Silently wrong output** | 7 RAS rejects every conforming 256-colour file · 40 HDR transposes `+X/+Y` dimensions |

The three I would fix first:

1. **42 (PluginXPM.cpp:281)** — `sprintf` of a file-supplied string into `char msg[256]`.
   The attacker picks the length and every byte; the trigger is an unrecognised colour name,
   which is the ordinary error path. One line.
2. **BitmapAccess.cpp:300** — `width = abs(width)`. One decision behind findings 18, 21 and
   39, each of which is an unbounded heap write of file content. Returning NULL for a
   negative dimension fixes all three at the source.
3. **43/44 (PluginPICT.cpp)** — the unpackers write into the bitmap with no end-of-row test
   at all, and `expandBuf8` disagrees with its own caller about whether its `width` argument
   counts bytes or pixels.

Note 7 and 40 are not security issues but they are *correctness* ones that would be visible
to any user: no standard 256-colour Sun raster loads at all, and any Radiance file using the
`+X … +Y …` resolution ordering comes out with its width and height swapped.

---

## Findings

### 1. PluginCUT.cpp:169 — RLE row pointer walks off the front of the bitmap (OOB write)

`Load()` sets `bits = FreeImage_GetScanLine(dib, header.height - 1)` (the last row in
memory) and steps it *down* one pitch per end-of-row marker:

```c
while (i < size) {
    read count;
    if (count == 0) {          // end of row
        k = 0;
        bits -= pitch;         // <-- never bounded by the row count
        io->read_proc(&count, 1, 1, handle);   // "paint shop pro adds two useless bytes"
        io->read_proc(&count, 1, 1, handle);
        continue;              // <-- i is NOT advanced
    }
    ...
    if (k + count <= header.width) { memset(bits + k, run, count); }   // column checked
    else throw;                                                        // row is not
    k += count; i += count;
}
```

Nothing counts rows. The only loop exits are `i >= size` and an I/O failure, and the
`count == 0` branch `continue`s **without touching `i`** — so a file that is just a long
run of `0x00` bytes decrements `bits` indefinitely while `i` stays 0. After `height`
markers `bits` is below the start of the allocation; every later record then writes there.

Both write paths are affected, and one of them writes *file-controlled bytes*:
`memset(bits + k, run, count)` (:187) and `io->read_proc(&bits[k], count, 1, handle)` (:193).
The `k + count <= header.width` guard only bounds the column offset within a row.

Trigger: `width=4096, height=1`, 8 × `00 00 00`, then `02 41 42`.

**CONFIRMED**, ASan:
```
ERROR: AddressSanitizer: SEGV on unknown address 0x774bc29d86a0
The signal is caused by a WRITE memory access.
    #5 fread /usr/include/x86_64-linux-gnu/bits/stdio2.h:331
    #6 _ReadProc Source/FreeImage/FreeImageIO.cpp:40
    #7 Load Source/FreeImage/PluginCUT.cpp:193
```
A wild write of *file bytes* ~32 KB below the allocation — not merely a redzone hit.
(A small `pitch` hides it: the first few decrements land inside the DIB's own header and
palette, which share the allocation, so the pitch has to be large enough to clear them.)

Note this also goes (harmlessly) out of bounds on *well-formed* input: a file that
terminates its last row with `0x00` leaves `bits == buffer_start - pitch`, which is
already undefined pointer arithmetic even though nothing is written through it.

Confidence: CONFIRMED — **FIXED**, commit 5cd7e6f

### 2. PluginCUT.cpp:159 — `width * height` overflows `int` before it is stored

`unsigned size = header.width * header.height;` — both are `WORD`, so both promote to
`int` and the product is computed in `int`. `65535 * 65535` = 4,294,836,225 > `INT_MAX`:
signed overflow, i.e. UB (UBSan traps it). Two's-complement wrap happens to give the
right value after the conversion to `unsigned`, so this is a latent/UB issue rather than
a wrong result today. Minor.

**CONFIRMED** by the fuzzer, UBSan:
```
Source/FreeImage/PluginCUT.cpp:159:32: runtime error: signed integer overflow:
65520 * 65535 cannot be represented in type 'int'
```

Confidence: CONFIRMED (UB; the wrapped value happens to be correct on this target) — **FIXED**, commit f052cff

### 3. PluginPFM.cpp:63 — a full-length header line is returned unterminated, then `sscanf`s

`pfm_get_line()` zero-fills the buffer, then reads up to `length` bytes, breaking on `\n`:

```c
memset(buffer, 0, length);
for (i = 0; i < length; i++) {
    if (!io->read_proc(&buffer[i], 1, 1, handle)) return FALSE;
    if (buffer[i] == 0x0A) break;
}
return (i < length) ? TRUE : FALSE;
```

When the `\n` lands on the **last** slot (`i == length - 1`) the function writes all
`length` bytes, wiping the last byte the `memset` had zeroed, and still returns TRUE —
the buffer is not NUL-terminated. The caller then runs

```c
char line_buffer[PFM_MAXLINE];                                  // 256 bytes, on the stack
BOOL bResult = pfm_get_line(io, handle, line_buffer, PFM_MAXLINE);
if (bResult) bResult = (sscanf(line_buffer, "%f", &scalefactor) == 1);   // :254
```

`%f` skips leading whitespace, so a scale line of 255 spaces followed by `\n` leaves
`sscanf` scanning past the end of the array for a digit: **stack buffer over-read** of
unbounded length. Any 256-byte line whose content does not terminate the conversion has
the same effect.

Trigger: `Pf\n1\n1\n` + 255 × `0x20` + `\n` + 4 bytes of pixel data. That file is rejected
with "invalid PFM header" — i.e. `sscanf` ran off the end and found no number there.
ASan cannot flag it, because the over-read happens inside glibc's uninstrumented `sscanf`,
not in FreeImage's own code; the same reason a checked build shows nothing.

Confidence: PLAUSIBLE (the missing terminator is plain from the code; the read past the
array is inside libc and so not observable with ASan/UBSan here)

### 4. PluginPFM.cpp:123 — unbounded digit accumulation in `pfm_get_int`

`i = (i * 10) + (c - '0');` runs for as many digits as the file supplies, with no overflow
check. A long digit string is signed-`int` overflow (UB; UBSan traps). The `width <= 0 ||
height <= 0` check at :246 catches the values that happen to wrap negative, but not those
that wrap to a positive number, so the reported dimensions can silently disagree with the
file. Practically bounded afterwards by `FreeImage_AllocateHeaderT` failing, so: low
severity, but it is genuine UB on a hostile file. `3 * width` at :274 is the same class.

Confidence: PLAUSIBLE

### 5. PluginRAS.cpp:384,403,411,443,451 — `WORD` loop counters against `DWORD` file fields (infinite loop → heap overflow)

`Load()` declares `WORD x, y;` (:204) but `header.width` / `header.height` are `DWORD`
straight out of the file, and they are what is passed to `FreeImage_AllocateHeader`.
In `y < header.height` the `unsigned short` promotes to `int` and then converts to
`unsigned`, so for `height > 65535` the condition is **always true** and `y++` wraps at
65536: the row loop never ends.

```c
bits = FreeImage_GetBits(dib) + (header.height - 1) * pitch;
for (y = 0; y < header.height; y++) {   // y is WORD
    ReadData(io, handle, bits, linelength, rle);
    bits -= pitch;                      // walks off the front of the allocation
    ...
}
```

`height = 70000, width = 1, depth = 8, type = RT_BYTE_ENCODED` allocates ~280 KB and then
writes one byte every `pitch` bytes *downward* forever: heap buffer underflow, unbounded.

**CONFIRMED**, ASan:
```
ERROR: AddressSanitizer: heap-buffer-overflow ... WRITE of size 1
    #0 ReadData Source/FreeImage/PluginRAS.cpp:117
    #1 Load    Source/FreeImage/PluginRAS.cpp:385
```
With `type = RT_STANDARD` the same file instead **hangs forever** (the uncompressed
`ReadData` writes nothing once `read_proc` starts failing, so the loop just spins) — a
plain denial of service.

The same defect is in the column loops (`for (x = 0; x < header.width; x++)`, :411/:419/
:451/:461) for `width > 65535`: `bits += 3`/`bits += 4` and `bp += 3`/`bp += 4` run
forever — heap overflow on both the destination scanline and the `buf` source. `width =
100000, height = 1, depth = 24` is enough (300 KB row).

**CONFIRMED**, UBSan:
```
Source/FreeImage/PluginRAS.cpp:420:26: runtime error: load of address 0x7549c53aabe2
with insufficient space for an object of type 'BYTE'
```

Confidence: CONFIRMED — **FIXED**, commit ffa77c9

### 6. PluginRAS.cpp:97 — the RLE decoder keeps its state in `static` locals

```c
static void
ReadData(FreeImageIO *io, fi_handle handle, BYTE *buf, DWORD length, BOOL rle) {
    static BYTE repchar, remaining = 0;
```

Two consequences:

* **State leaks between images.** `remaining` is never reset on entry. A decode that
  stops with an unfinished run (a truncated or malformed file) leaves `remaining > 0`,
  and the *next* RAS image in the same process starts by emitting `repchar` — a byte from
  the previous file — `remaining` times. Output depends on decode history.
* **Not thread-safe.** Two threads decoding RAS concurrently share `repchar`/`remaining`
  and corrupt each other's output. Every other plugin here keeps its decoder state on the
  stack.

`repchar` also has no initialiser, so the very first run of a file that begins mid-state
reads an indeterminate value.

Confidence: PLAUSIBLE

### 7. PluginRAS.cpp:315 — a RAS file with a *complete* colormap is rejected

```c
int numcolors = 1 << header.depth;
if ((DWORD)(3 * numcolors) > header.maplength) {
    numcolors = header.maplength / 3;    // fewer colors than the full palette: OK
} else {
    throw "Invalid palette";             // <-- full palette lands here
}
```

The branches are the wrong way round. For the ordinary 8-bit case `numcolors = 256` and a
conforming file has `maplength = 768`; `768 > 768` is false, so the `else` fires and the
load fails with "Invalid palette". Only files whose colormap is *shorter* than the pixel
depth implies can be read at all. (Introduced by 793e195, "Add palette pointer check".)

**CONFIRMED.** Two files, identical but for the colormap length:
```
maplength = 768 (complete, conforming)  ->  [FI] RAS: Invalid palette / load failed
maplength = 765 (one entry short)       ->  loaded 2x2 bpp=8
```

`1 << header.depth` is also UB for the `depth == 32` case that reaches this switch
(shift count equal to the width of `int`).

Confidence: CONFIRMED — **FIXED**, commit c1f2f66

### 8. PluginRAS.cpp:327,349,401,441 — four unchecked `malloc`s

`r = malloc(3 * numcolors)`, `colormap = malloc(header.maplength)`, `buf =
malloc(header.width * 3)` and `buf = malloc(header.width * 4)` are all used without a NULL
check. `header.maplength` is an unvalidated `DWORD`, so `malloc(0xFFFFFFFF)` is directly
reachable and the following `io->read_proc(colormap, header.maplength, 1, handle)`
dereferences NULL. `header.width * 3` is computed in 32-bit unsigned and wraps for
`width >= 0x55555556`, giving a buffer far smaller than the loop writes.

Note: a `maplength = 0xFFFFFFFF` file did **not** crash on this machine — Linux
overcommit let the 4 GiB `malloc` succeed and the short read was ignored. The missing
check is still real; it is the allocator's generosity that hides it here.

Confidence: PLAUSIBLE

### 9. PluginRAS.cpp:372 — 32-bit rows get a padding byte they do not have

`fill = (linelength % 2) ? 1 : 0;` with `linelength = (WORD)header.width`. Sun raster pads
each row to 16 bits, so a 32-bit image (`width * 4` bytes per row) is *always* even and
needs no padding — but for an odd `width` this computes `fill = 1` and consumes one extra
byte per row at :472, desynchronising the rest of the stream. (The 24-bit case happens to
be correct, since `width * 3` and `width` have the same parity.)

`linelength` is also a `WORD` holding a truncated `DWORD` width; for `width > 65535`
(8-bit) the rows are read short rather than overlong, so that one under-reads rather than
overflows.

Confidence: PLAUSIBLE

### 10. PluginSGI.cpp:364 — the EOF check in the uncompressed path can never fire

```c
ch = io->read_proc(&packed, sizeof(BYTE), 1, handle);   // returns a COUNT: 0 or 1
...
if (ch == EOF) { throw SGI_EOF_IN_IMAGE_DATA; }          // EOF is -1
```

`read_proc` returns the number of items read, never `EOF`. So for an uncompressed SGI the
end-of-file test is dead: a truncated file keeps "reading" zero bytes and the remaining
pixels are filled with the `packed = 0` initialiser — it decodes as a silently black image
instead of reporting the error. The RLE path is fine, because `get_rlechar` converts the
short read into `EOF` itself.

`if (cnt == EOF)` at :142 is dead for the same reason: `cnt` is assigned from a `BYTE`, so
it is 1..255 there and never -1.

Confidence: PLAUSIBLE

### 11. PluginSGI.cpp:145 — an `0x80` opcode leaves the RLE counter negative

```c
pstatus->cnt = cnt & 0x7F;      // 0x80 -> 0
if (cnt & 0x80) pstatus->val = -1;
...
pstatus->cnt--;                 // -> -1
```

A count byte of `0x80` gives a run length of 0, and the unconditional `cnt--` takes the
counter to -1. `if (!pstatus->cnt)` on the next call is false for -1, so the decoder never
reloads an opcode again: with `val == -1` still set it consumes the whole rest of the
stream as literal bytes. The row loop bounds the writes, so this is a decode-correctness
bug, not a memory-safety one — but a single `0x80` byte silently garbles everything after
it.

Confidence: PLAUSIBLE

### 12. PluginSGI.cpp:266 — the RLE index is sized from an unvalidated channel count

`int index_len = height * zsize;` (and the `malloc` on the next line) run **before** the
`switch (zsize)` at :291 that rejects anything outside 1..4. `zsize` is a raw `WORD`, so
`height * zsize` can be up to `65535 * 65535`, which overflows `int` (UB) before it is
handed to `malloc`. Moving the channel-count validation above the allocation is the fix;
today it survives only because the oversized `malloc` fails and the file is rejected for
the wrong reason.

`io->seek_proc(handle, *pri, SEEK_SET)` at :354 also ignores its return value and never
validates the file-supplied row offset; a failed seek decodes whatever the stream is
pointing at.

Confidence: PLAUSIBLE

### 13. PluginIFF.cpp:306 — the PBM RLE loop checks its bound only between packets (heap overflow)

```c
unsigned line = FreeImage_GetLine(dib) + 1 & ~1;
for (unsigned i = 0; i < FreeImage_GetHeight(dib); i++) {
    BYTE *bits = FreeImage_GetScanLine(dib, FreeImage_GetHeight(dib) - i - 1);
    ...
    while (number_of_bytes_written < line) {
        io->read_proc(&rle_count, 1, 1, handle);
        if (rle_count < 128) {
            for (int k = 0; k < rle_count + 1; k++) {
                io->read_proc(&byte, 1, 1, handle);
                bits[number_of_bytes_written++] += byte;      // unbounded inside the packet
            }
        } else if (rle_count > 128) {
            io->read_proc(&byte, 1, 1, handle);
            for (int k = 0; k < 257 - rle_count; k++) {
                bits[number_of_bytes_written++] += byte;      // unbounded inside the packet
            }
        }
    }
}
```

The `while` guard is evaluated only between packets, and a single packet writes up to 128
bytes, so a row can be overrun by up to 127 bytes. The loop starts at `i = 0`, i.e. at
scanline `height - 1` — **the last row in memory** — so the very first packet runs past the
end of the allocation rather than into a neighbouring row.

`rle_count == 128` matches neither branch: nothing is read, nothing is written,
`number_of_bytes_written` does not move, and at EOF `read_proc` leaves `rle_count`
untouched — an **infinite loop**.

**CONFIRMED.** A 16×2 8-bpp PBM whose BODY is one literal packet (`7f` + 128 bytes):
```
ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 1
    #0 Load Source/FreeImage/PluginIFF.cpp:313
```
(reported as a READ because `bits[n++] += byte` loads before it stores). The same file with
a BODY of `80 80 80 80` hangs until killed.

Confidence: CONFIRMED — **FIXED**, commit 8b585c7

### 14. PluginIFF.cpp:230 — the chunk walk has no EOF test and `size` wraps

```c
size -= 4;                          // FORM size, straight from the file
while (size) {
    DWORD ch_type, ch_size;         // uninitialised
    io->read_proc(&ch_type, 4, 1, handle);   // return ignored
    io->read_proc(&ch_size, 4, 1, handle);   // return ignored
    ...
    io->seek_proc(handle, ch_end - io->tell_proc(handle), SEEK_CUR);
    size -= ch_size + 8;
}
```

Nothing stops the walk at end of file: past EOF both reads fail silently and `ch_type` /
`ch_size` keep the previous iteration's values. `size` is unsigned and `size -= ch_size + 8`
is not checked for underflow, so any file whose chunk lengths do not land exactly on zero
wraps it to ~4×10⁹ and the loop keeps seeking. `size -= 4` at :226 already underflows for a
FORM size below 4.

**CONFIRMED.** `FORM` with size `0xFFFFFFF0`, a valid BMHD and no BODY hangs until killed.

Confidence: CONFIRMED — **FIXED**, commit 5f2b599

### 15. PluginIFF.cpp:249 — a short BMHD chunk is used uninitialised

```c
BMHD bmhd;                                        // no initialiser
io->read_proc(&bmhd, sizeof(bmhd), 1, handle);    // return ignored
...
width = bmhd.w; height = bmhd.h; planes = bmhd.nPlanes; comp = bmhd.compression;
```

A `BMHD` chunk truncated by the file (or one at EOF) leaves the struct with whatever was on
the stack, and those values go straight into `FreeImage_Allocate`. The read also ignores
`ch_size` entirely — it always consumes 20 bytes, so a BMHD that declares a shorter length
eats the following chunk header and the `ch_end` seek then jumps *backwards*.

`FreeImage_Allocate` at :270/:272 is likewise unchecked, as is `malloc(src_size)` at :338
— the following `io->read_proc(src, src_size, 1, handle)` would dereference NULL.

Confidence: PLAUSIBLE

### 16. PluginPCX.cpp:454,490 — the palette seek assumes the PCX starts at byte 0 of the file

```c
long start_pos = io->tell_proc(handle);     // :363 — the plugin KNOWS the stream may be offset
BOOL bValidated = pcx_validate(io, handle);
io->seek_proc(handle, start_pos, SEEK_SET);
...
io->seek_proc(handle, -769L, SEEK_END);     // :454 — relative to the whole file
...
io->seek_proc(handle, (long)sizeof(PCXHEADER), SEEK_SET);   // :490 — ABSOLUTE 128
```

The 8-bpp palette lookup seeks to `SEEK_END - 769` and then restores the position with an
*absolute* seek to 128, so a PCX that does not begin at offset 0 — `FreeImage_LoadFromHandle`
on a stream positioned mid-file, or a PCX embedded in a container — reads a palette from the
wrong place and then resumes decoding from the wrong place. The validation code three dozen
lines earlier goes to the trouble of saving and restoring `start_pos`, so the intent is clear;
these two seeks just do not honour it. Same class as the OpenJPEG stream-relative-seek fix.

Confidence: PLAUSIBLE

### 17. PluginPCX.cpp:153 — an `0xC0` packet becomes a 256-byte run

```c
if ((value & 0xC0) == 0xC0) {
    count = value & 0x3F;        // 0xC0 -> 0
    value = *(ReadBuf + (*ReadPos)++);
} else {
    count = 1;
}
count--;                         // 0 -> 255 (count is a BYTE)
*(buffer + written++) = value;
```

A run-length of zero underflows the `BYTE` counter, so the packet emits 256 bytes instead of
none. `length` bounds the total, so this corrupts the decode rather than the heap — the rest
of the row is shifted and filled with the wrong value.

Both `io->read_proc` calls that refill `ReadBuf` (:141, :144) ignore their result, so on a
truncated file the decoder keeps consuming whatever the buffer last held; on the *first*
refill there is nothing in it at all, and `ReadBuf` comes from `malloc` — uninitialised heap
is then used as pixel data.

Confidence: PLAUSIBLE

Not a bug, worth recording: the `line` buffer is sized `MAX(lineLength, width * header.bpp)`
(:514), and that `MAX` is load-bearing. I checked the two indexing loops that a corrupt
`bytes_per_line` could push out of bounds — the 4-plane one (`x/8 + plane * bytes_per_line`,
:571) and the 3-plane one (`pLine[x]` after two `+= bytes_per_line`, :610-621) — and neither
can exceed that allocation for any `width`/`bytes_per_line` pair. The dib-side writes are
bounded by the pitch in both. PCX is in better shape than its neighbours.

### 18. PluginDDS.cpp:599,641 + BitmapAccess.cpp:300 — a negative width writes the whole file into one scanline

The most serious one found so far: a fully file-controlled heap write of unbounded length.

`LoadRGB` casts the header's `DWORD` dimensions to `int` and keeps the signed values:

```c
const int width  = (int)desc->dwWidth;        // 0xFFFFFFF0 -> -16
const int height = (int)desc->dwHeight;
dib = FreeImage_Allocate(width, height, bpp, ...);
...
const int line = CalculateLine(width, FreeImage_GetBPP(dib));
for (int y = 0; y < height; y++) {
    BYTE *pixels = FreeImage_GetScanLine(dib, height - y - 1);
    io->read_proc(pixels, 1, line, handle);            // :641
    io->seek_proc(handle, delta, SEEK_CUR);
}
```

The core allocator *rescues* the negative value instead of rejecting it:

```c
width = abs(width);           // BitmapAccess.cpp:300 — -16 becomes 16
height = abs(height);
if (!((width > 0) && (height > 0))) return NULL;
```

so `FreeImage_Allocate` hands back a **16 × 4** bitmap while `LoadRGB` still believes the
width is -16. `CalculateLine` takes `unsigned` parameters, so `-16` arrives as
`0xFFFFFFF0`, the 64-bit product is truncated back to 32 bits, and `line` comes out as
`-64`. `read_proc`'s count parameter is `unsigned`, so `-64` becomes **4294967232** and
each row reads *the entire remaining file* into a 64-byte scanline.

Trigger: `dwWidth = 0xFFFFFFF0`, `dwHeight = 4`, `ddspf.dwFlags = DDPF_RGB`,
`dwRGBBitCount = 32`, followed by any payload. Every byte after the 128-byte header
lands on the heap past the bitmap.

**CONFIRMED**, ASan:
```
ERROR: AddressSanitizer: heap-buffer-overflow ... WRITE of size 4096
    #2 _ReadProc Source/FreeImage/FreeImageIO.cpp:40
    #3 LoadRGB   Source/FreeImage/PluginDDS.cpp:641
  allocated by FreeImage_AllocateBitmap BitmapAccess.cpp:390
```

Two separate defects, and both want fixing:

* **PluginDDS.cpp** must reject dimensions that do not fit a positive `int` (and validate
  `dwRGBBitCount`, which is passed to `FreeImage_Allocate` unchecked at :603).
* **BitmapAccess.cpp:300** — `abs(width)` is the wrong policy for a library whose callers
  are parsers. A negative dimension always means the caller mis-parsed something, and
  silently substituting a *different, positive* size guarantees the caller's own loop
  bounds no longer match the bitmap it was given. It should return NULL. `abs(INT_MIN)`
  is also UB in its own right.

Confidence: CONFIRMED — **FIXED**, commit 03fcfc5 + b926485

### 19. PluginDDS.cpp:849 — `Load` never checks the magic number that `Validate` checks

```c
static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
    DDSHEADER header;
    memset(&header, 0, sizeof(header));
    io->read_proc(&header, 1, sizeof(header), handle);     // result ignored
    const DWORD dwFlags = header.surfaceDesc.ddspf.dwFlags;   // straight to use
```

`Validate` rejects a bad `dwMagic` and a `dwSize` that is not 124/32, but `Load` repeats
none of it. `Validate` only runs from `FreeImage_GetFileType`; an application that names
the format itself — `FreeImage_Load(FIF_DDS, ...)`, which is what happens when the format
is picked from the file extension — goes straight into `LoadRGB`/`LoadDXT` with whatever
the first 128 bytes contained. That is the path finding 18 above is reached through.

Confidence: CONFIRMED (it is what the finding-18 repro exercises; that file is only — **FIXED**, commit 45afab6
accepted because `Load` does not re-validate)

### 20. PluginDDS.cpp:648 — division by zero on big-endian / RGB-order builds

```c
#if FREEIMAGE_COLORORDER == FREEIMAGE_COLORORDER_RGB
    const int bytespp = FreeImage_GetLine(dib) / width;
```

`width` here is the *unvalidated* `(int)desc->dwWidth`, so `dwWidth == 0` divides by zero.
`FreeImage_Allocate` would have returned NULL for a zero width — but the `width` in this
expression is the plugin's own copy, not the bitmap's, and for a negative `dwWidth` the two
disagree (see 18), which also makes `bytespp` negative and the following `pixels += bytespp`
walk backwards.

Not reachable on this build: little-endian x86 selects `FREEIMAGE_COLORORDER_BGR`
(FreeImage.h:95-101), so the block is compiled out. It is live on big-endian targets and on
any build that defines `FREEIMAGE_COLORORDER=1`. Unrunnable here; reported from the code.

Confidence: PLAUSIBLE (platform-conditional)

### 21. PluginICO.cpp:302,337 — a negative `biHeight` turns the pixel read into a 4 GiB request

```c
int width  = bmih.biWidth;              // LONG, straight from the file
int height = bmih.biHeight / 2;         // height == xor + and mask
...
unsigned line  = CalculateLine(width, bit_count);
unsigned pitch = CalculatePitch(line);
dib = FreeImage_AllocateHeader(header_only, width, height, bit_count);   // abs()es both
...
io->read_proc(FreeImage_GetBits(dib), height * pitch, 1, handle);        // :337
```

Same shape as finding 18: `FreeImage_AllocateBitmap` takes `abs(height)` so the bitmap is
built, while `LoadStandardIcon` keeps the negative value. `height * pitch` is `int *
unsigned`, so the product is evaluated *unsigned*: `biHeight = -4` gives `height = -2` and
a request of **4294967264 bytes** into a 32-byte bitmap. Nothing validates `biWidth` or
`biHeight` anywhere on the path — `Validate` only looks at `idReserved`/`idType`/`idCount`,
and `Open` only at the first two.

**CONFIRMED — but note ASan does not report it.** The call passes the huge value as
`size` with `count = 1`, so `fread` returns 0 (incomplete item) and ASan's interceptor,
which marks `res * size` bytes written, marks nothing. The library reports `loaded 4x2
bpp=32` and exits cleanly while the heap behind the bitmap has been overwritten. Measured
directly with a canary arena, replaying the same call:

```
fread returned 0; bytes written past the 32-byte bitmap: 4064
```

i.e. the whole remainder of the file. A 100 MB .ico writes 100 MB.

**This matters beyond ICO.** Every `read_proc(ptr, <computed>, 1, handle)` in the tree is
invisible to ASan in exactly this way, whereas `read_proc(ptr, 1, <computed>, handle)`
(the DDS form in 18) is caught. Fuzzing this library with ASan will silently miss the
first shape — the audit has to read for it.

Confidence: CONFIRMED (by direct measurement, not by the sanitizer) — **FIXED**, commit 03fcfc5 + 508a405

### 22. PluginICO.cpp:305 — `biBitCount == 2` is accepted, then silently turned into 8 bpp

The CVE-2020-24292 fix added a bit-depth whitelist, but it admits `2`:

```c
if (bit_count != 1 && bit_count != 2 && bit_count != 4 && bit_count != 8 &&
    bit_count != 16 && bit_count != 24 && bit_count != 32) return NULL;
```

`FreeImage_AllocateBitmap` has no case for 2 and falls into `default: bpp = 8` — so the
bitmap is 8 bpp while `line` and `pitch` (:310-311) were computed for 2 bpp. The pixel read
at :337 is then a quarter of the row length the bitmap actually has, and the image decodes
as garbage. Under-reads rather than overflows, so this is a correctness bug, but the
whitelist and the allocator disagree about what is supported.

Confidence: PLAUSIBLE

### 23. PluginICO.cpp:424 — the icon directory is used whether or not it was read

```c
ICONDIRENTRY *icon_list = (ICONDIRENTRY*)malloc(icon_header->idCount * sizeof(ICONDIRENTRY));
io->seek_proc(handle, sizeof(ICONHEADER), SEEK_SET);
io->read_proc(icon_list, icon_header->idCount * sizeof(ICONDIRENTRY), 1, handle);  // ignored
...
io->seek_proc(handle, icon_list[page].dwImageOffset, SEEK_SET);
```

`idCount` comes from the file and the read result is discarded, so a file that declares
100 icons but contains one leaves `icon_list` as uninitialised heap; `dwImageOffset` is
then whatever `malloc` handed back and the loader seeks there. (Same `size,1` shape as
above, so again nothing traps.) The `seek_proc` to `sizeof(ICONHEADER)` is also absolute,
so an ICO that does not start at offset 0 of the stream reads its directory from the wrong
place — cf. finding 16 in PCX.

Confidence: PLAUSIBLE

### 24. PluginPCD.cpp:173-175 — the three scratch buffers are shadowed, so the handler frees nothing

```c
BYTE *y1 = NULL, *y2 = NULL, *cbcr = NULL;        // :124 — what the catch block frees
...
try {
    BYTE *y1   = (BYTE*)malloc(width * sizeof(BYTE));   // :173 — shadows it
    BYTE *y2   = (BYTE*)malloc(width * sizeof(BYTE));
    BYTE *cbcr = (BYTE*)malloc(width * sizeof(BYTE));
    if (!y1 || !y2 || !cbcr) throw FI_MSG_ERROR_MEMORY;
...
} catch (const char *text) {
    if (cbcr) free(cbcr);        // the OUTER cbcr — still NULL
    if (y2)   free(y2);
    if (y1)   free(y1);
```

The inner `BYTE *` declarations shadow the outer ones, so the `throw` at :176 leaks
whichever of the three allocations succeeded. The outer declarations have no other purpose
than to be freed by the handler, which makes the shadowing plainly unintentional. Narrow
(it needs a `malloc` failure), but the same defect class as the two shadowed-pointer leaks
already fixed in the tone mappers.

`VerticalOrientation` (:55) also reads 128 bytes into a stack buffer without checking the
result and then tests `buffer[72]`: on a file shorter than 128 bytes the orientation comes
from uninitialised stack. The three `read_proc(y1, width, 1, handle)` calls at :188-190
likewise ignore their result, so a truncated PCD converts uninitialised heap to pixels.
None of this is exploitable — PCD dimensions are hard-coded constants, so there is no
size arithmetic to corrupt.

Confidence: PLAUSIBLE

### 25. PluginPICT.cpp:975 — `Read8` cannot signal EOF, so the version scan spins forever

```c
static BYTE
Read8(FreeImageIO *io, fi_handle handle) {
    BYTE i = 0;
    io->read_proc(&i, 1, 1, handle);    // result discarded
    return i;
}
...
BYTE b = 0;
while ((b = Read8(io, handle)) == 0);   // :975 — skip to the version opcode
```

Every read primitive in this plugin (`Read8`, and `Read16`/`Read32` built on it) returns 0
at end of file, indistinguishable from a genuine zero byte. The loop at :975 spins until it
sees a non-zero byte, so **a file that is all zeros from that point on never terminates**.

The opcode loop further down does guard against this — `if (currentPos ==
io->tell_proc(handle))` at :1218 breaks when an iteration consumed nothing — but the header
scan that runs before it has no such check.

**CONFIRMED.** The minimal reproducer is 472 zero bytes:
```
python3 -c "open('x.pct','wb').write(bytes(472))"
-> killed at 12 s, and again at 30 s
```
(The `io->seek_proc(handle, 512, SEEK_CUR)` at :965 succeeds even past the end of a short
file, so the file does not even have to be 512 bytes long.) Found by the fuzzer, then
reduced.

Related, at :965: `if ( !io->seek_proc(handle, 512, SEEK_CUR) == 0 )` parses as
`(!seek) == 0`, i.e. "seek returned non-zero". That is the intended test by luck — the
obvious reading of the line, `!(seek == 0)`, means the same thing here — but it is one
edit away from being wrong and should be `if (io->seek_proc(...) != 0)`.

Confidence: CONFIRMED — **FIXED**, commit beec28f

### 26. PluginDDS.cpp — a 131-byte file asks for 268 MB and 33 million I/O calls

Also from the fuzzer. `dwHeight = 0x00FFFFFF`, `dwWidth = 4`, 32 bpp, `DDPF_RGB`, total file
length 131 bytes. `LoadRGB` allocates `4 × 16777215 × 4` = 268 MB and then runs its row loop
16.7 million times, each iteration a `read_proc` plus a `seek_proc` on a stream that ended
long ago. The process was still going when the 10-second fuzzing timeout killed it.

Nothing in DDS relates the declared dimensions to the number of bytes actually available —
the same check the EXR plugin already grew (`dabc2fc`, "check the data window against the
file before allocating the bitmap"). `io->read_proc`'s result is ignored on every row, so
the loop cannot notice that it stopped making progress either.

Confidence: CONFIRMED (resource exhaustion, not memory corruption) — **FIXED**, commit 305c4c8

---

## Files checked and found sound

**PluginBMP.cpp** — no findings. It is the one legacy loader in this set that is properly
hardened, and it is worth saying why, because the same measures are what the others lack:

* `CheckBitmapInfoHeader` (:146) rejects `biWidth < 0` outright and only allows a negative
  `biHeight` for the compressions where a top-down DIB is legal. That single check is what
  stops the DDS/ICO `abs()` mismatch (findings 18, 21) from happening here.
* `LoadPixelData` (:221) does its scanline arithmetic in `INT64` and checks the result of
  every `read_proc`.
* `used_colors` is clamped to `CalculateUsedPaletteEntries(bit_count)` before the palette
  is read (:551, :789), so `biClrUsed` cannot overrun the palette.
* Both RLE decoders bound each run with `count = MIN(status_byte, width - bits)` and bail
  out on `count < 0`, and `scanline` is re-tested by the loop condition after `RLE_DELTA`.
* `LoadOS21XBMP` never calls `CheckBitmapInfoHeader`, but its header
  (`BITMAPINFOOS2_1X_HEADER`) stores the dimensions as `WORD`s, so they cannot go negative
  or exceed 65535 and the pitch cannot disagree with the allocated bitmap.

The only nit: `io->seek_proc(handle, bitmap_bits_offset, SEEK_SET)` (:596, :669, :725,
:843, :1030) is absolute, so like PCX (16) and ICO (23) a BMP that does not start at
offset 0 of the stream reads its pixels from the wrong place.

---

(continued)

### 27. PluginWBMP.cpp:255 — a two-byte file hangs the process

```c
if (header.FixHeaderField & 0x80) {
    header.ExtHeaderFields = 0x80;
    while (header.ExtHeaderFields & 0x80) {
        io->read_proc(&header.ExtHeaderFields, 1, 1, handle);   // result ignored
        readExtHeader(io, handle, header.ExtHeaderFields);
    }
}
```

Once the stream is exhausted `read_proc` writes nothing and leaves `ExtHeaderFields` at its
previous value. The loop primes it with `0x80`, so if the file ends right there the
condition stays true forever. `readExtHeader` does not break the cycle either: for
`0x80 & 0x60 == 0` it calls `multiByteRead`, whose own loop ends immediately at EOF.

**CONFIRMED.** The whole reproducer is two bytes:
```
printf '\x00\x80' > x.wbmp     ->  killed at 12 s
```

`readExtHeader` also `malloc`s `sizeParamIdent` and `sizeParamValue` bytes (:155-156)
without checking either result, and `sizeParamValue` can be 0.

Confidence: CONFIRMED — **FIXED**, commit 0691416

### 28. PluginKOALA.cpp:140 — 10001 bytes of uninitialised stack become the image

```c
koala_t image;                                  // 10001 bytes, no initialiser
unsigned char load_address[2];
io->read_proc(&load_address, 1, 2, handle);     // result ignored
if ((load_address[0] != 0x00) || (load_address[1] != 0x60)) {
    ((BYTE *)&image)[0] = load_address[0];
    ((BYTE *)&image)[1] = load_address[1];
    io->read_proc((BYTE *)&image + 2, 1, 10001 - 2, handle);   // result ignored
} else {
    io->read_proc(&image, 1, 10001, handle);                   // result ignored
}
```

No read result is checked and the struct is never zeroed, so a KOALA file shorter than
10001 bytes leaves the remainder of `image` holding whatever was on the stack — and the
decode loop turns every one of those bytes into pixels. That is a **stack memory
disclosure**: the caller gets an image whose content is the process's own stack. KOALA is
never auto-detected on content alone for such a file (`Validate` wants `00 60`), but
`FreeImage_Load(FIF_KOALA, ...)` — i.e. the `.koa` extension — reaches it directly.

The indexing itself is safe: `index` peaks at 7999 of 8000 and `colourindex` at 999 of 1000.

Separately at :193, `found_color = image.background;` takes the whole byte where every
other branch masks to a nibble (`>> 4`, `& 0xf`). The result is packed as
`(found_color << 4) | found_color` into one 4-bpp byte, so a background byte outside 0..15
produces *two different* palette indices for what should be one colour — e.g. `0x35`
becomes `0x75`. It should be `image.background & 0x0f`.

Confidence: PLAUSIBLE

### 29. PluginXPM.cpp:322 — the pixel row is indexed by the *declared* width, not its own length

```c
str = ReadString(io, handle);          // whatever the file put between the quotes
char *pixel_ptr = str;
for (int x = 0; x < width; x++) {
    std::string chrs(pixel_ptr, cpp);  // copies cpp bytes, NUL or not
    FILE_RGBA rgba = rawpal[chrs];
    ...
    pixel_ptr += cpp;
}
```

`width` and `cpp` come from the info string, `str` from the quoted row. Nothing requires
`strlen(str) >= width * cpp`, so a row shorter than it claims walks `pixel_ptr` off the end
of the `ReadString` allocation — `width * cpp` bytes past it in the worst case. The
`std::string` constructor takes an explicit length, so the terminator does not stop it.

Trigger: info string `"8 1 1 4"` (width 8, 1 colour, 4 chars per pixel) with a two-character
pixel row.

**CONFIRMED**, ASan:
```
ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 4
    #0 memcpy
    #1 std::char_traits<char>::copy(...)          <- std::string chrs(pixel_ptr, cpp)
  allocated by ReadString Source/FreeImage/PluginXPM.cpp:75
```

The colour-map loop above it does check (`strlen(str) < (size_t)cpp` at :201); the pixel
loop simply forgot to.

Confidence: CONFIRMED — **FIXED**, commit dabe04b

### 30. PluginXPM.cpp:190,192 — the allocation result is never checked

```c
if (colors > 256) dib = FreeImage_AllocateHeader(header_only, width, height, 24, ...);
else              dib = FreeImage_AllocateHeader(header_only, width, height, 8);

//build a map of color chars to rgb values           <- no NULL test in between
...
RGBQUAD *pal = FreeImage_GetPalette(dib);            // :297
pal[i].rgbBlue = rgba.b;                             // :298
```

`width` and `height` are only checked for `<= 0` (:185), never for plausibility, so an XPM
that declares a huge canvas gets NULL back and the palette loop writes through it. Every
other loader in this directory tests the result and throws `FI_MSG_ERROR_DIB_MEMORY`.

**CONFIRMED**, ASan (with `allocator_may_return_null=1`, so the allocator behaves as a
normal `malloc` would):
```
WARNING: AddressSanitizer failed to allocate 0x3782dace9d9005a0 bytes
ERROR: AddressSanitizer: SEGV on unknown address 0x000000000000
    #0 Load Source/FreeImage/PluginXPM.cpp:298
```
Trigger: info string `"2000000000 2000000000 1 1"`.

Confidence: CONFIRMED — **FIXED**, commit 6e7bc25

### 31. PluginXPM.cpp:54,69,75 — the read helpers ignore failures, and `ReadString` is unbounded

```c
static BOOL FindChar(FreeImageIO *io, fi_handle handle, BYTE look_for) {
    BYTE c;                                             // uninitialised
    io->read_proc(&c, sizeof(BYTE), 1, handle);         // result ignored
    while (c != look_for) { ... }
```

On an empty stream the first comparison is against an indeterminate value; if it happens to
match, `FindChar` reports success having read nothing. `ReadString` (:69) opens the same
way.

`ReadString` then accumulates into a `std::string` with no length cap (`s += c`), so a file
with an opening quote and no closing one grows the string until allocation fails — and the
resulting `std::bad_alloc` is **not** caught by `catch (const char *text)`, so it escapes
`Load`, passes through `FreeImage_LoadFromHandle`, and lands in the application. The
plugin-authoring notes in PluginPCX.cpp state the opposite contract: "Throwing exceptions
in plugin functions is allowed, as long as those exceptions are being caught inside the
same plugin."

`malloc(s.length()+1)` at :75 is unchecked and `strcpy` writes through it immediately.
There is also a leak at :201-202: the `strlen(str) < cpp` branch throws without freeing
`str`.

Finally, `Base92` (:81) returns a pointer into a function-local `static char b92[16]` —
not reentrant, and shared between threads saving XPMs.

Confidence: PLAUSIBLE

### 32. PluginPNM.cpp:81 — the same unbounded digit accumulator as PFM

`GetInt` is PFM's `pfm_get_int` with the error handling changed:

```c
int i = 0;
while (1) {
    i = (i * 10) + (c - '0');
    if (!io->read_proc(&c, 1, 1, handle)) throw FI_MSG_ERROR_PARSING;
    if (c < '0' || c > '9') break;
}
```

No digit cap, so a long run of digits is signed-`int` overflow (UB). `width < 0 || height < 0`
at :249 catches the values that wrap negative and `FreeImage_AllocateHeader` rejects the rest,
so PNM survives it — this is the second site of finding 4, recorded so a fix covers both.

At :377 and :388, `(255 * level) / maxval` multiplies an unbounded `GetInt` result by 255 in
`int`; `maxval` itself is properly validated to 1..65535 at :255, so there is no division by
zero.

Otherwise **PluginPNM.cpp is sound**: dimensions are range-checked, the allocation result is
tested, the 1-bit writes are bounded by `x >> 3 < CalculateLine(width, 1)`, and the
obscure-looking `bits[x>>3] &= (0xFF7F >> (x & 0x7))` at :345 does produce the correct
clear-mask for all eight bit positions (I checked each).

Confidence: PLAUSIBLE (UB, same reasoning as 4; not separately reproduced)

### 33. PluginTARGA.cpp:621 — the RLE overflow guard measures rows at `line_size`, but they are spaced at `pitch`

```c
const BYTE* dib_end = FreeImage_GetScanLine(dib, height);   // one-past-end row
const int line_size = CalculateLine(width, bpp);            // NOT the pitch
...
if ((line_bits + x) + packet_count * pixel_size > dib_end) {   // :621 — linear estimate
    FreeImage_OutputMessageProc(s_format_id, FI_MSG_ERROR_CORRUPTED);
    return;
}
...
for (int ix = 0; ix < packet_count; ix++) {
    _assignPixel<bPP>((line_bits + x), val, as24bit);
    x += pixel_size;
    if (x >= line_size) { x = 0; y++; line_bits = FreeImage_GetScanLine(dib, y); }  // jumps by PITCH
}
```

The guard assumes the packet's pixels occupy `packet_count * pixel_size` contiguous bytes.
They do not: `x` wraps at `line_size` while `line_bits` advances by the *pitch*, so every
row crossed costs an extra `pitch - line_size` bytes that the guard never counted. Whenever
`line_size` is not a multiple of 4 the guard therefore under-estimates and lets a packet run
past `dib_end`.

The worst case is a narrow 24-bpp image: `width = 1` gives `line_size = 3`, `pitch = 4`, so
every single pixel crosses a row and the guard is short by one byte per pixel.

Trigger: 1 × 100, 24 bpp, `image_type = 10` (RLE true-colour); 97 one-pixel RLE packets to
reach `y = 97`, then a packet of 4. The guard computes `bits+388 + 4*3 = bits+400` against
`dib_end = bits + 100*4 = bits+400` — not greater, so it passes — and the fourth pixel is
written at `dib_end`.

**CONFIRMED**, ASan:
```
ERROR: AddressSanitizer: heap-buffer-overflow ... WRITE of size 1
    #0 _assignPixel<24> Source/FreeImage/PluginTARGA.cpp:546
    #1 loadRLE<24>      Source/FreeImage/PluginTARGA.cpp:635
```

Confidence: CONFIRMED — **FIXED**, commit aafe9c5

### 34. PluginTARGA.cpp:246,599 — `IOCache::getBytes` has a contract the caller does not honour

```c
BYTE* getBytes(size_t count /*must be < _size!*/) {
    if (_ptr + count >= _end) { /* refill from _begin */ }
    BYTE *result = _ptr;
    _ptr += count;                 // may now be past _end
    return result;
}
```

The "must be < _size" is a comment, not a check. The cache size is chosen from the file:

```c
const long remaining_size = (eof - pixels_offset);
if (remaining_size < height) throw FI_MSG_ERROR_CORRUPTED;   // only rules out sz == 0
const long sz = (remaining_size / height);
IOCache cache(io, handle, sz);
...
BYTE *val = cache.getBytes(file_pixel_size);                 // file_pixel_size = bPP/8, up to 4
```

So any file whose pixel data averages fewer than `bPP/8` bytes per row gives `sz <
file_pixel_size`, and `getBytes` hands back a pointer with fewer than `count` valid bytes
behind it. `_assignPixel` then reads `count` bytes from it.

Trigger: 1 × 100, **32 bpp**, `image_type = 10`, 300 bytes of pixel data → `sz = 3`,
`file_pixel_size = 4`. (32 bpp keeps `line_size == pitch`, so this is finding 33's
neighbour, not finding 33 again.)

**CONFIRMED**, ASan:
```
ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 4
    #0 _assignPixel<32> Source/FreeImage/PluginTARGA.cpp:559
    #1 loadRLE<32>      Source/FreeImage/PluginTARGA.cpp:635
  allocated by IOCache::IOCache Source/FreeImage/PluginTARGA.cpp:215
```

`_ptr + count` in the refill test is itself out-of-bounds pointer arithmetic when `count >
_size`. Both refills (:235, :260) also ignore `read_proc`'s result — the `//### EOF - no
problem?` comment in the source is asking exactly the right question, and the answer is
that a truncated file silently decodes whatever the cache held last.

Confidence: CONFIRMED — **FIXED**, commit 2003de9

### 35. PSDParser.cpp:1330 — `UnpackRLE` clamps the destination but never the source

```c
void psdParser::UnpackRLE(BYTE* line, const BYTE* rle_line, BYTE* line_end, unsigned srcSize) {
    while (srcSize > 0 && line < line_end) {
        int len = *rle_line++;
        srcSize--;
        if (len < 128) {
            ++len;
            // assert we don't write beyound eol
            memcpy(line, rle_line, line + len > line_end ? line_end - line : len);
            line     += len;
            rle_line += len;     // <- source advances unchecked
            srcSize  -= len;     // <- unsigned; underflows when len > srcSize
        }
        ...
```

The ternary bounds the copy by the **destination** row only. `rle_line` points into
`rle_line_start`, an array of `largestRLELine` bytes of which only `rleLineSize` were read,
and nothing relates `len` to what is left there — so a literal packet that claims more bytes
than the compressed line contains reads straight off the end of that allocation.

`srcSize -= len` then underflows (it is `unsigned`), so `srcSize > 0` stays true and the loop
keeps going with `rle_line` already far past the buffer, until the destination row fills up.
The guard the loop was given is therefore defeated by the same packet that overruns it.

Trigger, and the whole file is **44 bytes**: an 8-bit greyscale PSD, `width = 4096`,
`height = 1`, one channel, `compression = 1`, `rleLineSizeList[0] = 2`, and the two RLE bytes
`7f 41` — a literal packet announcing 128 bytes with one byte behind it.

**CONFIRMED**, ASan:
```
ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 128
    #2 psdParser::UnpackRLE     Source/FreeImage/PSDParser.cpp:1330
    #3 psdParser::ReadImageData Source/FreeImage/PSDParser.cpp:1588
  allocated by ReadImageData    Source/FreeImage/PSDParser.cpp:1558
```

Confidence: CONFIRMED — **FIXED**, commit 8c8d529

### 36. PSDParser.cpp:1567-1591 — the RLE path skipped the CVE-2020-24295 hardening the raw path got

The two decompression branches of `ReadImageData` are no longer symmetric. Uncompressed
(:1482-1501):

```c
const unsigned limitLineSize = MIN(dstLineSize, lineSize);
BYTE* dst_line_start = dst_first_line + channelOffset;
if (channelOffset + lineSize > dst_buffer_size) {
    // Fix for CVE-2020-24295 ...
    throw "Invalid PSD image";
}
for (unsigned h = 0; h < nHeight; ++h, dst_line_start -= dstLineSize) {
    io->read_proc(line_start, lineSize, 1, handle);
    ReadImageLine(dst_line_start, line_start, limitLineSize, dstBpp, bytes);   // clamped
}
```

RLE (:1572-1590):

```c
const unsigned channelOffset = GetChannelOffset(bitmap, ch) * bytes;
BYTE* dst_line_start = dst_first_line + channelOffset;         // no dst_buffer_size test
for (unsigned h = 0; h < nHeight; ++h, dst_line_start -= dstLineSize) {
    ...
    ReadImageLine(dst_line_start, line_start, lineSize, dstBpp, bytes);        // NOT clamped
}
```

The RLE branch has neither the `channelOffset + lineSize > dst_buffer_size` throw nor the
`MIN(dstLineSize, lineSize)` clamp — it passes the raw `lineSize`. The CVE fix was applied to
one of the two paths that need it. Even where the destination stays in bounds for the channel
layouts this parser builds, the asymmetry is the bug: the invariant is asserted in one branch
and assumed in the other.

Confidence: PLAUSIBLE

### 37. PSDParser.cpp:1274,1290 — `ReadImageLine` decrements its counter past zero

```c
case 4: { ... while (lineSize > 0) { ...; d += dstBpp; lineSize -= 4; } }
case 2: { ... while (lineSize > 0) { ...; d += dstBpp; lineSize -= 2; } }
```

`lineSize` is `unsigned`. If it is ever not a multiple of the element size the subtraction
underflows and the loop runs ~2³⁰ times, writing the whole way. Today it is always a multiple
(`lineSize = nWidth * bytes` with `bytes` 2 or 4, and the raw path's `MIN` is taken against a
4-aligned pitch), so this is latent rather than live — but it is one `depth` value away from
being reachable, and `>= 4` / `>= 2` costs nothing.

Confidence: PLAUSIBLE (latent)

### 38. FreeImageIO.cpp:92,122 — the memory backend's size arithmetic, and what it accidentally protects

```c
// _MemoryReadProc
const int required_bytes = (int)(size) * count;          // :92
const int remaining_bytes = mem_header->file_length - mem_header->current_position;
if ((required_bytes > 0) && (remaining_bytes > 0)) { ... }
return 0;
```

`size * count` is evaluated in 32-bit `unsigned` and stored in an `int`, so **any request of
2 GiB or more lands on a negative `required_bytes` and the function returns 0 with the buffer
untouched.** Two consequences, one good and one not:

* *Good, by accident:* the overflowed read sizes in findings 18 and 21 are rejected here. I
  ran every repro through both backends:

  | repro | `FreeImage_Load` (stdio) | `FreeImage_LoadFromMemory` |
  |---|---|---|
  | `dds_negwidth.dds` (18) | heap-buffer-overflow | `loaded 16x4`, clean |
  | `ico_negheight.ico` (21) | silent 4064-byte OOB write | clean |
  | `tga_rle_pitch.tga` (33) | heap-buffer-overflow | heap-buffer-overflow |
  | `psd_unpackrle.psd` (35) | heap-buffer-overflow | heap-buffer-overflow |
  | `xpm_oob.xpm` (29) | heap-buffer-overflow | heap-buffer-overflow |

  So **18 and 21 are reachable only through the stdio backend** (`FreeImage_Load`, or any
  application `FreeImageIO` that passes the size straight to `fread`). The in-plugin buffer
  bugs are backend-independent. This is worth stating in any advisory, and it is also why a
  fuzzer driven through `LoadFromMemory` would never have found 18 or 21.

* *Not good:* it is a silent short read, not an error. A legitimate `FreeImage_ReadMemory`
  of ≥ 2 GiB returns 0 as though at end of file. That is the `unsigned size` limit in the
  `FI_ReadProc` signature showing through; see "Known, by design" below.

`_MemoryWriteProc` has the matching truncation without the protection:

```c
const long required_bytes = (long)(size * count);        // :122 — wraps in 32-bit FIRST
while ((mem_header->current_position + required_bytes) >= mem_header->data_length) { ...grow... }
memcpy((char *)mem_header->data + mem_header->current_position, buffer, required_bytes);
...
return count;                                            // reports full success
```

The cast to `long` happens *after* the 32-bit multiply, so `size = count = 0x10000` gives
`required_bytes == 0`: nothing is copied, and the function still returns `count`. Silent data
loss on save-to-memory rather than an error.

Minor, same file: `FreeImage_SaveToMemory` (:101), `FreeImage_AcquireMemory` (:121) and
`FreeImage_WriteMemory` (:210) all dereference `stream->data` after testing only `stream`.

Confidence: CONFIRMED (the backend comparison above was measured; the write truncation is — **FIXED**, commit ae73c7a
read from the code)

### 39. PluginHDR.cpp:261 — `%d` into `unsigned*` lets a negative width through to the pixel loop

```c
static BOOL
rgbe_ReadHeader(..., unsigned *width, unsigned *height, ...) {
    ...
    if (sscanf(buf, "-Y %d +X %d", height, width) < 2) {     // :261  %d into unsigned*
```

The resolution line is parsed with `%d` into `unsigned*` — so `-Y 1 +X -1` stores
`0xFFFFFFFF` in `width`, and nothing range-checks it afterwards. In `Load`:

```c
dib = FreeImage_AllocateHeaderT(header_only, FIT_RGBF, width, height);   // -> abs() -> 1 px wide
...
rgbe_ReadPixels_RLE(io, handle, scanline, width, 1);                     // int scanline_width = -1
```

and in `rgbe_ReadPixels_RLE`:

```c
if ((scanline_width < 8) || (scanline_width > 0x7fff)) {
    return rgbe_ReadPixels(io, handle, data, scanline_width * num_scanlines);   // (unsigned)(-1)
}
```

`-1` passes the `< 8` test, then becomes `numpixels = 4294967295`. `rgbe_ReadPixels` writes
one 12-byte `FIRGBF` per 4 bytes read, so it overruns the 12-byte scanline by three times the
remaining file length, stopping only at EOF.

Trigger: `#?RADIANCE` / `FORMAT=32-bit_rle_rgbe` / blank / `-Y 1 +X -1` / payload.

**CONFIRMED**, ASan:
```
ERROR: AddressSanitizer: heap-buffer-overflow ... WRITE of size 4
    #0 rgbe_RGBEToFloat     Source/FreeImage/PluginHDR.cpp:176
    #1 rgbe_ReadPixels      Source/FreeImage/PluginHDR.cpp:340
    #2 rgbe_ReadPixels_RLE  Source/FreeImage/PluginHDR.cpp:373
    #3 Load                 Source/FreeImage/PluginHDR.cpp:665
```

This is finding 18's pattern once more: `FreeImage_AllocateBitmap`'s `abs()` builds a bitmap
of a size the caller does not believe in. Here it is reached through a *signed format
specifier*, not a cast.

Confidence: CONFIRMED — **FIXED**, commit 03fcfc5 + 83b4785

### 40. PluginHDR.cpp:262 — the `+X … +Y …` resolution string is read transposed

```c
if (sscanf(buf, "-Y %d +X %d", height, width) < 2) {
    if (sscanf(buf, "+X %d +Y %d", height, width) < 2) {     // :262
```

The first line is right: `-Y <h> +X <w>` fills `height` then `width`. The fallback keeps the
same argument order while the *format* swaps the fields, so `+X <w> +Y <h>` puts the width
into `height` and the height into `width`.

**CONFIRMED.** A file whose resolution line is `+X 8 +Y 2` — 8 wide, 2 high — loads as:
```
loaded 2x8 bpp=96
```

Confidence: CONFIRMED — **FIXED**, commit 1666841

### 41. PluginHDR.cpp:126 — `rgbe_GetLine` is `pfm_get_line` with the same missing terminator

Byte for byte the same function as finding 3, including `return (i < length) ? TRUE : FALSE`,
so a header line that fills `buf[HDR_MAXLINE]` (256) exactly leaves it unterminated and the
callers run off the end of the stack array:

* `strcmp(buf, "FORMAT=32-bit_rle_rgbe\n")` (:234)
* `sscanf(buf, "GAMMA=%g", &tempf)` (:237) and `EXPOSURE` (:241)
* `sscanf(buf, "-Y %d +X %d", height, width)` (:261)

(`strncpy(header_info->comment, buf, HDR_MAXLINE - 1)` at :247 is bounded and fine.)

Also at :215, `isspace(buf[i + 2])` is passed a plain `char`: negative on a byte ≥ 0x80 where
`char` is signed, which is undefined for the `<ctype.h>` functions.

Confidence: PLAUSIBLE (the over-read is inside libc's `strcmp`/`sscanf`, so ASan cannot show
it — same reason as finding 3)

### 42. PluginXPM.cpp:281 — `sprintf` of a file-controlled string into a 256-byte stack buffer

The highest-severity finding here: a plain, directly controllable stack smash.

```c
static FIBITMAP * DLL_CALLCONV
Load(FreeImageIO *io, fi_handle handle, int page, int flags, void *data) {
    char msg[256];                                          // :158
    ...
        if (!FreeImage_LookupX11Color(clr, &rgba.r, &rgba.g, &rgba.b)) {
            sprintf(msg, "Unknown color name '%s'", str);   // :281
            free(str);
            throw msg;
        }
```

`str` is the whole colour-definition line as `ReadString` returned it — arbitrary length,
straight from the file — and it is formatted with `sprintf` into a fixed 256-byte automatic
array. Any XPM naming a colour that `FreeImage_LookupX11Color` does not recognise, with a
name longer than about 230 characters, writes past `msg` and over the stack frame.

The name is copied verbatim into the message, so the attacker chooses both the length and
every byte of what lands on the stack. Reaching it needs nothing unusual: an unknown colour
name is the ordinary error path.

Trigger:
```
/* XPM */
static char *x[] = {
"1 1 1 1",
"a c zzzz…z",        <- 300 z's
"a"
};
```

**CONFIRMED**, ASan:
```
ERROR: AddressSanitizer: stack-buffer-overflow ... WRITE of size 326
    #0 vsprintf
    #3 Load Source/FreeImage/PluginXPM.cpp:281
  Address is located in stack of thread T0 ... Load Source/FreeImage/PluginXPM.cpp:157
```

The two sibling sites are fine: `PluginPICT.cpp:733` and `:1214` format only an `int` and a
`WORD` into their 256-byte `outputMessage`, and the `sprintf`s in PluginPNM.cpp's `Save` all
use bounded field widths.

Confidence: CONFIRMED — **FIXED**, commit 215257a

### 43. PluginPICT.cpp:763-807 — `UnpackBits` never bounds the destination scanline

```c
BYTE* dst = (BYTE*)FreeImage_GetScanLine( dib, height - 1 - i );
...
for ( int j = 0; j < linelen; ) {             // linelen comes from the file
    FlagCounter = Read8( io, handle );
    if (FlagCounter & 0x80) {
        int len = ((FlagCounter ^ 255) & 255) + 2;        // 2..129
        expandBuf8( io, handle, 1, pixelSize, dst );
        for ( int k = 1; k < len; k++ )
            memcpy( dst+(k*PixelPerRLEUnit), dst, PixelPerRLEUnit );
        dst += len*PixelPerRLEUnit;                       // no end-of-row test
    } else {
        int len = (FlagCounter & 255) + 1;                // 1..128
        expandBuf8( io, handle, len, pixelSize, dst );     // writes len*PixelPerRLEUnit bytes
        dst += len*PixelPerRLEUnit;                       // no end-of-row test
    }
    j += ( len * pkpixsize ) + 1;
}
```

`dst` points straight into the bitmap, `linelen` is read from the file (a `Read8`, or a
`Read16` when `rowBytes > 250`), and neither branch compares `dst` against the end of the
scanline or of the bitmap. For a 1-bpp source `PixelPerRLEUnit` is 8, so a single literal
packet expands to **1024 bytes** — into a row that may be 8 bytes long.

The comment just above the allocation says "I allocate the temporary line buffer here. I
allocate too much memory to compensate for sloppy (& hence fast) decompression." There is no
temporary line buffer in this function any more: `dst` is `FreeImage_GetScanLine(dib, ...)`.
The slack the sloppy decompression was relying on does not exist.

Trigger (686 bytes): 512-byte preamble, frame rect, `11 02 ff`, opcode `0x0090` with
`rowBytes = 8` (clears the 0x8000 pixmap flag and is ≥ 8, so the RLE path is taken), bounds
`0,0,1,8` → an 8×1 8-bpp bitmap of 8 bytes, then `linelen = 0x40` and one `0x7f` literal
packet followed by 128 bytes.

**CONFIRMED**, ASan:
```
ERROR: AddressSanitizer: heap-buffer-overflow ... WRITE of size 1
    #0 expandBuf8   Source/FreeImage/PluginPICT.cpp:523
    #1 UnpackBits   Source/FreeImage/PluginPICT.cpp:803
    #2 DecodeBitmap Source/FreeImage/PluginPICT.cpp:849
    #3 Load         Source/FreeImage/PluginPICT.cpp:1307
```

`Unpack8Bits` (:656) and `Unpack32Bits` are separate routines with the same shape and should
be checked against the same rule when this is fixed.

Confidence: CONFIRMED — **FIXED**, commit 5942c98

### 44. PluginPICT.cpp:484 — `expandBuf8`'s `width` argument means two different things

```c
expandBuf8( FreeImageIO *io, fi_handle handle, int width, int bpp, BYTE* dst ) {
    switch (bpp) {
        case 8:  io->read_proc( dst, width, 1, handle ); break;      // width = bytes = pixels
        case 4:  for (int i = 0; i < width; i++) { read 1 byte; write 2 pixels; dst += 2; }
                 if (width & 1) { ... }                              // width = PIXELS here
        case 2:  for (int i = 0; i < width; i++) { read 1 byte; write 4 pixels; dst += 4; }
                 if (width & 3) { ... }
        case 1:  for (int i = 0; i < width; i++) { read 1 byte; write 8 pixels; dst += 8; }
                 if (width & 7) { ... }
```

The main loops treat `width` as a count of **source bytes** — one byte in, 8/4/2 pixels out.
The leftover blocks right below them (`if (width & 7)`, `& 3`, `& 1`) only make sense if
`width` is a count of **pixels**. The function cannot be right both ways, and the caller at
:744 passes pixels:

```c
if (rowBytes < 8) {
    // ah-ha!  The bits aren't actually packed.  This will be easy.
    for ( int i = 0; i < height; i++ ) {
        BYTE* dst = (BYTE*)FreeImage_GetScanLine( dib, height - 1 - i);
        ...
        expandBuf8( io, handle, width, pixelSize, dst );    // width = image width in pixels
    }
}
```

So for a 1-bpp source this writes **8 × width** bytes into a `width`-byte row (the bitmap is
allocated 8 bpp, one byte per pixel). No RLE is involved — this is the plain uncompressed
path, reached whenever `rowBytes < 8`, which includes the common `rowBytes == 0` case since
`rowBytes` is then set to `pixwidth`.

Trigger (a 588-byte file): opcode `0x0090`, `rowBytes = 0`, bounds `0,0,1,4` → a 4×1 8-bpp
bitmap of 4 bytes, then 64 bytes of data.

**CONFIRMED**, ASan:
```
ERROR: AddressSanitizer: heap-buffer-overflow ... WRITE of size 1
    #0 expandBuf8   Source/FreeImage/PluginPICT.cpp:527
    #1 UnpackBits   Source/FreeImage/PluginPICT.cpp:744
    #2 DecodeBitmap Source/FreeImage/PluginPICT.cpp:849
```

Two smaller bugs in the same leftover blocks, both operator-precedence slips that drop the
final pixels of a row:

* `:534` — `for (int i = 7; i > (8-width & 7); i--)` parses as `(8 - width) & 7`. For a
  width of 9 that is 7, so the loop runs zero times where it should run once. It should be
  `i > 7 - (width & 7)`.
* `:513` — `for (int i = 6; i > 8 - (width & 3) * 2; i -= 2)` is off by one in the same
  direction: `width & 3 == 1` gives `i > 6`, zero iterations instead of one. It should be
  `i > 7 - (width & 3) * 2`... though with `width` meaning source bytes in the loop above,
  fixing the unit confusion first is what actually settles these.

Confidence: CONFIRMED — **FIXED**, commit 5942c98

### 45. PluginPICT.cpp:547 — `UnpackPictRow` has the same missing destination bound

```c
static BYTE*
UnpackPictRow( FreeImageIO *io, fi_handle handle, BYTE* pLineBuf, int width, int rowBytes, int srcBytes ) {
    if (rowBytes < 8) { io->read_proc( pLineBuf, rowBytes, 1, handle ); }
    else {
        BYTE* pCurPixel = pLineBuf;
        for (int j = 0; j < srcBytes; ) {
            BYTE FlagCounter = Read8( io, handle );
            if (FlagCounter & 0x80) {
                ...
                int len = ((FlagCounter ^ 255) & 255) + 2;
                memset( pCurPixel, p, len ); pCurPixel += len; j += 2;
            } else {
                int len = (FlagCounter & 255) + 1;
                io->read_proc( pCurPixel, len, 1, handle ); pCurPixel += len; j += len + 1;
            }
        }
    }
```

`srcBytes` is the file-supplied `linelen`; nothing compares `pCurPixel` with the end of
`pLineBuf`. It is the same defect as 43, in the routine the *other* two decompressors use:

* `Unpack32Bits` (:592) passes a `malloc(rowBytes)` line buffer — heap overflow of that
  buffer.
* `Unpack8Bits` (:675) passes `FreeImage_GetScanLine(dib, …)` — the overflow lands directly
  in the bitmap.

I did not build a separate reproducer for these two; they are the same code path as the
confirmed 43, reached through `pixelSize == 32` and `pixelSize == 8` respectively.

Confidence: PLAUSIBLE (by inspection; 43 is the confirmed instance of the pattern)

### 46. PluginXBM.cpp:59-62 — `readLine` writes the terminator one byte past the buffer

```c
static char *
readLine(char *str, int n, FreeImageIO *io, fi_handle handle) {
    BYTE c = 0;
    int count, i = 0;
    do {
        count = io->read_proc(&c, 1, 1, handle);
        str[i++] = c;
    } while ((c != '\n') && (i < n));
    if (count <= 0) return NULL;
    str[i] = '\0';          // i == n when the loop ended on the length test
    return str;
}
```

The `i < n` test runs *after* `str[i++] = c`, so a line with no newline in its first `n`
bytes leaves the loop with `i == n`, and `str[n] = '\0'` writes one past the array. The
caller passes a 512-byte automatic buffer:

```c
char line[MAX_LINE], name_and_type[MAX_LINE];      // MAX_LINE = 512
...
if (readLine(line, MAX_LINE, io, handle) == NULL) { eof = TRUE; }
else { if (strlen(line) == MAX_LINE - 1) return ERR_XBM_LINE; ... }
```

The over-long-line check in the caller runs too late to help, and it tests the wrong value
anyway (`strlen` is `MAX_LINE` at that point, not `MAX_LINE - 1`).

**CONFIRMED**, ASan. The whole file is 512 `A` characters:
```
ERROR: AddressSanitizer: stack-buffer-overflow ... WRITE of size 1
    #0 readLine    Source/FreeImage/PluginXBM.cpp:62
    #1 readXBMFile Source/FreeImage/PluginXBM.cpp:112
    #2 Load        Source/FreeImage/PluginXBM.cpp:323
```

Confidence: CONFIRMED — **FIXED**, commit 0f9e945

### 47. PluginXBM.cpp:73 — `readChar` cannot return EOF, so three loops never end

```c
static int
readChar(FreeImageIO *io, fi_handle handle) {
    BYTE c;                                 // uninitialised
    io->read_proc(&c, 1, 1, handle);        // result discarded
    return c;                               // 0..255 — never EOF
}
```

Exactly PICT's `Read8` problem (finding 25), and this file has three loops that depend on
the EOF it cannot produce:

```c
while ((c1 = readChar(io, handle)) != 'x') {     // :193
    if (c1 == EOF) return ERR_XBM_EOFREAD;       // unreachable
}
...
for (;;) {                                        // :222
    c1 = readChar(io, handle);
    if (c1 == EOF) return ERR_XBM_EOFREAD;        // unreachable
    value1 = hex_table[c1];
    if (value1 != 256) break;
}
```

Past end of file `read_proc` writes nothing, so `readChar` keeps returning the last byte it
did read — and if that byte is neither `'x'` nor a hex digit, the loop spins forever. On the
very first call at EOF it returns an indeterminate value, since `c` has no initialiser.

**CONFIRMED.** A truncated XBM — header lines and an opening brace, nothing after it:
```
#define a_width 8
#define a_height 8
static char a_bits[] = {
```
hangs until killed. (The last byte read is `'\n'`, `hex_table['\n'] == 256`, so the :222 loop
never breaks.)

`*widthP` / `*heightP` come from `sscanf(line, "#define %s %d", ...)` and are only checked
against the `-1` sentinel, never for range, so `raster_length = bytes_per_line * *heightP`
(:159) is computed from unvalidated `int`s.

Confidence: CONFIRMED — **FIXED**, commit 17d467d

### 48. Conversion.cpp:379 — `FreeImage_ColorQuantizeEx` clamps the sizes but not the pointer

```c
FreeImage_ColorQuantizeEx(FIBITMAP *dib, FREE_IMAGE_QUANTIZE quantize, int PaletteSize, int ReserveSize, RGBQUAD *ReservePalette) {
    if (PaletteSize < 2) PaletteSize = 2;
    if (PaletteSize > 256) PaletteSize = 256;
    if (ReserveSize < 0) ReserveSize = 0;
    if (ReserveSize > PaletteSize) ReserveSize = PaletteSize;       // no ReservePalette test
```

The size clamping is thorough — and it is what keeps `network[netsize - ReserveSize + i]`
(NNQuantizer.cpp:468) from going negative — but `ReservePalette` itself is never checked, and
the three quantizers disagree about whether it may be NULL:

* `LFPQuantizer::Quantize` (:38): `if (ReserveSize > 0 && ReservePalette != NULL)` — guarded.
* `WuQuantizer::Hist3D` (:151): `if (ReserveSize > 0)` then `ReservePalette[i].rgbRed` — NULL deref.
* `NNQuantizer::Quantize` (:467): `for (i = 0; i < ReserveSize; i++) ... ReservePalette[i]` — NULL deref.

So `FreeImage_ColorQuantizeEx(dib, FIQ_WUQUANT, 256, 16, NULL)` crashes while the same call
with `FIQ_LFPQUANT` does not. This is caller-facing API behaviour, not file parsing.

Confidence: PLAUSIBLE

---

## Cross-cutting patterns

Most of the findings above are instances of five recurring mistakes. Fixing the pattern is
worth more than fixing the 48 sites.

**A. `FreeImage_AllocateBitmap` takes `abs()` of the dimensions (BitmapAccess.cpp:300).**
The bitmap it returns then has a different size from the one the caller parsed, and every
loop bound the caller computed is wrong. Findings 18 (DDS), 21 (ICO) and 39 (HDR) are all
this, reached by three different routes: a `(int)` cast of a `DWORD`, a `/ 2` of a signed
`LONG`, and `%d` into an `unsigned *`. A negative dimension always means the caller
mis-parsed; the allocator should return NULL. PluginBMP is the one loader that rejects
negative dimensions itself, and it is the one loader with no findings.

**B. A read primitive that cannot report EOF.** `Read8` (PICT, 25), `readChar` (XBM, 47),
`get_rlechar`'s caller (SGI, 10) and the `ExtHeaderFields` loop (WBMP, 27) all discard
`read_proc`'s result and return a byte value, so end of file is indistinguishable from data.
Four independent infinite loops; three reproduced with files under 500 bytes, one of them
2 bytes. The IFF chunk walk (14) and the RAS row loop (5) are the same defect reached
through loop counters instead.

**C. An RLE decoder that bounds one side of the copy.** PSD's `UnpackRLE` clamps the
destination and not the source (35); IFF's PBM decoder checks its bound only between
packets (13); PICT's `UnpackBits` and `UnpackPictRow` check neither (43, 45); TARGA's guard
measures the destination in the wrong units (33); CUT bounds the column but not the row (1).

**D. `read_proc(ptr, <computed>, 1, handle)` is invisible to ASan.** `fread` returns 0 for
an incomplete single item, and ASan's interceptor marks `res * size` bytes — so a partially
satisfied huge read is never reported even though glibc wrote every byte it managed to read.
Finding 21 is exactly this; I had to measure it with a canary arena. Any future fuzzing of
this tree has to account for it. The mirrored form, `read_proc(ptr, 1, <computed>, handle)`,
*is* caught (finding 18).

**E. Absolute seeks that assume the image starts at byte 0.** PCX (16), ICO (23) and BMP
(the note under "found sound") all use `SEEK_SET` with a file-absolute offset, or `SEEK_END`,
which is wrong for any image embedded in a container or loaded from a stream positioned
mid-file — a case `FreeImage_LoadFromHandle` explicitly supports, and which PCX's own
validation code takes care to handle three dozen lines earlier.

## Known, by design — not reported as bugs

* **`FI_ReadProc` / `FI_WriteProc` take `unsigned size, unsigned count`.** No single I/O
  call can move 4 GiB, and `_MemoryReadProc` effectively caps a request at 2 GiB
  (finding 38). This is the documented plugin ABI; changing it breaks every external
  plugin. Noted previously in this repo's own history.
* **`FreeImage_AllocateBitmap` coerces an unsupported `bpp` to 8** (BitmapAccess.cpp:330)
  rather than failing. Findings 22 (ICO's `biBitCount == 2`) and DDS's unvalidated
  `dwRGBBitCount` are consequences, but the coercion itself is long-standing behaviour that
  callers may rely on.
* **LibJPEG UBSan reports.** The fuzzer surfaced `jdmarker.c:506`, `jdhuff.c:529` and
  `jdhuff.c:1449` (out-of-range array *index arithmetic* and a signed shift of the bit
  buffer). These are in `Source/LibJPEG`, outside this audit's scope, and are upstream
  libjpeg behaviour, not anything PluginJPEG.cpp does.
* **Plugin.cpp:314** `strncat(buffer, find_data.name, nPathSize)` passes the buffer size as
  the append limit rather than the remaining space. `_finddata_t::name` is bounded by
  `MAX_PATH` and `buffer` is `8 * _MAX_PATH`, so it cannot actually overflow today; it is
  Windows-only plugin discovery and I could not build or run it here.

## Coverage

**Read closely** (findings above, or explicitly cleared): PluginCUT, PluginPFM, PluginRAS,
PluginSGI, PluginIFF, PluginPCX, PluginDDS, PluginICO, PluginPCD, PluginWBMP, PluginKOALA,
PluginXPM, PluginPNM, PluginTARGA, PluginHDR, PluginXBM, PluginBMP, PSDParser (image-data
path), PluginPICT (header scan + the three unpackers), MemoryIO, FreeImageIO,
BitmapAccess (allocation path), Conversion.cpp (quantize entry), the three quantizers'
reserve-palette handling.

**Swept for the patterns above but not read line by line**: PluginTIFF (81 KB),
PluginJPEG, PluginPNG, MNGHelper/PluginMNG/PluginJNG, the rest of PSDParser (resource and
layer sections), the rest of PluginPICT's opcode table, MultiPage, CacheFile, ColorLookup,
Halftoning, PixelAccess, GetType, ZLibInterface, the `Conversion*.cpp` family. The sweeps
were: unchecked allocations, `strcpy`/`strcat`/`sprintf` into fixed buffers, and
`read_proc(…, 1, handle)` with a computed size. Of these, only `Plugin.cpp:314` and
`PluginXPM.cpp:281` came back, and the XPM one is finding 42.

**Deliberately skipped**: PluginJXR, PluginEXR, PluginWebP, PluginGIF, PluginG3, PluginRAW,
PluginJ2K/PluginJP2/J2KHelper, PluginAVIF, PluginHEIF, the tone mappers and
FreeImageIO's default callbacks — all audited and fixed in this repo within the last month.

**Not attempted**: the `Save` paths, except where a helper is shared with `Load`. Every
finding above is in a load path, i.e. reachable from untrusted input.

## Reproducing

Everything is under `.claude/audit/` (untracked):

* `build-asan.sh` / `build-asan2.sh` — copy `Source/` to a scratch tree and build
  `libfreeimage.a` with ASan+UBSan. The second drops `-fno-sanitize-recover` so a benign
  UBSan report in a bundled library does not abort the run; use that one.
* `harness.c` / `harness_mem.c` — `FreeImage_Load` and `FreeImage_LoadFromMemory` drivers.
  Build: `gcc -fsanitize=address,undefined -I. -ISource -o harness harness.c libfreeimage.a
  -lstdc++ -lpthread -lm -fopenmp`.
* `poc/` — one file per finding, named after it.
* `fuzz.py`, `seeds/`, `minseeds/`, `crashes/` — the mutation fuzzer and its corpus. It
  found 25, 26 and the CUT overflow in 2; everything else came from reading.

Run with `ASAN_OPTIONS=detect_leaks=0:abort_on_error=0`, and add
`allocator_may_return_null=1` for finding 30, which needs `malloc` to fail rather than
ASan to abort.


---

## Verifying the fixes

The rig lives in `.claude/audit/` (untracked). Two builds of the library are kept side by
side: `asan2/` from the current sources and `asan2-ref/` from the sources as they were at
`b27e278`, both with identical flags, so any difference is the change and not the build.

* `rebuild.sh <file.cpp>` recompiles one file into `asan2/` and re-links the drivers. It
  verifies the archive member is byte-identical to the object just compiled — `ar r` keys on
  the basename, and a mismatched one silently *appends* a second member that the linker then
  ignores, which makes a sanitizer run report "clean" while testing the old code. That
  happened once during this work and cost a fix that looked applied and was not.
* `cmp-valid.sh` decodes every file in `minseeds/` and `seeds/` with both builds and compares
  an FNV-1a hash of the pixels and palette. The corpus was grown as the work went on, to
  cover each decoder path a fix touched: RLE and uncompressed twins of the same image for
  IFF, PSD and PICT; TARGA RLE at 8/16/24/32 bpp, widths 16 and 17, literal and run packets,
  including packets spanning every row boundary; a top-down BMP and its bottom-up twin;
  8/16/24/32-bit DDS; literal-RLE and run-RLE Radiance files; 8- and 32-bit icons.
* `dump.c` prints the decoded rows, for the cases where a checksum is not enough — the PICT
  fixes were checked pixel by pixel against the reference build.

Two results worth keeping:

* Every reproducer that crashed or hung on the reference build is clean on the fixed one,
  and no valid file in the corpus decodes differently — with one intended exception,
  `q_raw.pct`, an uncompressed 1-bpp PICT that the reference build *crashes* on and the
  fixed build decodes correctly.
* The 14 changed files were also compiled with the stock flags plus `-Wall -Wextra`, and
  produce no new warnings relative to the reference (PluginPICT produces one fewer).

### Still open after this pass

* The 23 plausible findings.
* **`PSDParser.cpp:125`** — UBSan reports `load of misaligned address … for type 'const
  DWORD'` on *every* PSD, valid ones included, on both builds. Pre-existing and not in the
  48; harmless on x86, a fault on architectures that require natural alignment.
* **DDS 16-bit rows without `DDSD_PITCH`** — `line` is computed from the *bitmap's* 24 bpp
  rather than the file's 16, so each row reads 3×width bytes where the row holds 2×width.
  With `DDSD_PITCH` set the negative `delta` seek happens to correct it. Also pre-existing
  and not in the 48; noticed while fixing finding 26.
