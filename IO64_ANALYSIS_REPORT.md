# Complete Analysis Report: FreeImage 64-Bit I/O (`worktree-io64`)

**Date**: 2026-09-25  
**Branch**: `worktree-io64`  
**Base Commit**: `c0ed58c` (`origin/qpv`)  
**Head Commit**: `05357f7` ("I/O: GIF and TARGA positions are 64-bit on Win32 too")  
**Target Platforms**: Linux (32-bit, 64-bit), Windows (32-bit, 64-bit)

---

## Executive Summary

The `worktree-io64` branch updates FreeImage's I/O layer so that stream positions are represented as 64-bit signed integers (`INT64`) instead of `long`. On Windows platforms (32-bit and 64-bit LLP64), `long` is 32 bits, historically preventing FreeImage from reading or writing beyond 2 GB.

The core conversion is well-designed:
1. `FreeImageIO` callbacks (`FI_SeekProc`, `FI_TellProc`) take and return `INT64`.
2. Standard I/O uses `_fseeki64` / `_ftelli64` on Windows and `fseeko` / `ftello` on POSIX.
3. Memory streams dynamically grow up to `FI_MEMORY_MAX` (`size_t_max >> 1`), and wrapped buffers can address large ranges.
4. Chunked helpers `FreeImage_ReadBytes()` and `FreeImage_WriteBytes()` eliminate 4 GB truncation in plugin wrappers.
5. Large sparse BigTIFF tests (2.5 GB, 4.5 GB) and memory tests run and pass.

However, a meticulous code audit across all modified and related files revealed **critical bugs, regressions, omissions, and platform-specific hazards** (particularly on 32-bit Linux and for offset/container streams) that require correction.

---

## Summary of Findings

| Severity | ID | Category | Location | Description |
|---|---|---|---|---|
| **Critical** | BUG-01 | Regression / Truncation | `Source/FreeImage/MultiPage.cpp:1335` | `FreeImage_OpenMultiBitmapFromMemory` calls `FreeImage_TellMemory`, resetting stream position to 0 past 2 GB |
| **Critical** | BUG-02 | Offset Stream Corruption | `Source/FreeImage/PluginICO.cpp:819, 843` | `Save` writes absolute file offset instead of relative offset, corrupting reloaded ICOs in offset streams and truncating past 4 GB |
| **High** | BUG-03 | 32-bit Linux Truncation | `Source/FreeImageToolkit/JPEGTransform.cpp:406` | Missing `_FILE_OFFSET_BITS 64` causes `(off_t)end` to truncate 64-bit offset to 32 bits in `ftruncate` |
| **High** | BUG-04 | 32-bit Linux Truncation | `TestAPI/testMPageStream.cpp`, `Wrapper/FreeImagePlus/test/fipTestMPageStream.cpp` | Missing `_FILE_OFFSET_BITS 64` narrows `(off_t)offset` in `mySeekProc` to 32 bits |
| **Medium** | BUG-05 | Error Handling Bug | `Source/FreeImage/PluginTIFF.cpp:161` | `_tiffSeekProc` returns `(toff_t)(-1 - start)` instead of `(toff_t)-1` when `tell_proc` fails in offset streams |
| **Medium** | BUG-06 | Missing Clamping | `Source/FreeImage/PluginRAW.cpp:52, 97` | `_start` not clamped to 0 on `tell_proc` failure; subsequent `tell()` calls are off by 1 |
| **Medium** | OMISSION-01 | API Limitation | `Source/FreeImage.h:877-878`, `MemoryIO.cpp` | Public exports `FreeImage_SeekMemory` and `FreeImage_TellMemory` remain 32-bit `long` on Windows and 32-bit Linux |
| **Medium** | OMISSION-02 | API Limitation | `Source/FreeImage.h:873, 879`, `MemoryIO.cpp` | `FreeImage_OpenMemory` and `FreeImage_AcquireMemory` remain bound to 32-bit `DWORD` |
| **Low** | OMISSION-03 | Example Header Defect | `Examples/Generic/FIIO_Mem.h:21` | `fiio_mem_handle` struct retains 32-bit `long curpos, filelen, datalen` while `fiio_mem_SeekProc` was changed to `INT64` |
| **Low** | OMISSION-04 | Wrapper Omission | `Wrapper/VB6/src/MFreeImage.bas:1002` | VB6 wrapper was omitted from the wrapper updates; stdcall stack layout changes need documentation |
| **Low** | PRE-01 | Pre-existing Bug | `Source/FreeImage/PluginGIF.cpp:920, 1293` | Hardcoded `io->seek_proc(handle, 6, SEEK_SET)` breaks reading GIFs embedded in offset streams |
| **Low** | PRE-02 | Overflow Vulnerability | `Source/FreeImage/PluginJXR.cpp:74` | `_jxr_io_SetPos` lacks upper bounds check against `INT64_MAX - fio->start` |

---

## Detailed Analysis & Remediation

### 1. BUG-01: Regression in `FreeImage_OpenMultiBitmapFromMemory` (`MultiPage.cpp`)

#### Problem
In `Source/FreeImage/MultiPage.cpp`:
In `FreeImage_OpenMultiBitmapFromHandle` (line 602), the starting stream offset was updated to 64-bit:
```cpp
header->start = MAX(io->tell_proc(handle), (INT64)0);
```
However, in `FreeImage_OpenMultiBitmapFromMemory` (line 1335), the code still invokes `FreeImage_TellMemory`:
```cpp
SetMemoryIO(&header->io);
header->handle = (fi_handle)stream;						
header->start = MAX(FreeImage_TellMemory(stream), 0L);
```
Because `FreeImage_TellMemory` returns `long`, on Windows and 32-bit Linux any stream position exceeding `LONG_MAX` (2 GB) returns `-1L`. `MAX(-1L, 0L)` evaluates to `0L`, resetting `header->start` to 0. When pages are later locked or unlocked, seeks are performed relative to 0 rather than the multi-page document's actual position in the memory stream.

#### Remediation
Use `header->io.tell_proc` directly:
```cpp
SetMemoryIO(&header->io);
header->handle = (fi_handle)stream;						
header->start = MAX(header->io.tell_proc(header->handle), (INT64)0);
```

---

### 2. BUG-02: ICO Saved at Stream Offset or Past 4 GB Is Corrupted (`PluginICO.cpp`)

#### Problem
In `Source/FreeImage/PluginICO.cpp`:
In `Load` (line 493), `icon_list[page].dwImageOffset` is treated as an offset relative to `state->start_pos`:
```cpp
io->seek_proc(handle, state->start_pos + (INT64)icon_list[page].dwImageOffset, SEEK_SET);
```
However, in `Save` (line 819):
```cpp
DWORD dwImageOffset = (DWORD)io->tell_proc(handle);
...
DWORD dwBytesInRes = (DWORD)io->tell_proc(handle) - dwImageOffset;
icon_list[k].dwImageOffset = dwImageOffset;
icon_list[k].dwBytesInRes  = dwBytesInRes;
dwImageOffset += dwBytesInRes;
```
1. `io->tell_proc(handle)` is the **absolute** stream position, not the relative offset within the ICO data (`io->tell_proc(handle) - state->start_pos`). If an ICO is saved into a stream where `state->start_pos > 0`, `icon_list[k].dwImageOffset` stores `start_pos + local_offset`. When loaded back, `Load` seeks to `start_pos + (start_pos + local_offset)`, double-counting `start_pos`.
2. If `io->tell_proc(handle)` exceeds 4 GB, the cast `(DWORD)io->tell_proc(handle)` truncates the upper 32 bits, producing invalid offsets.

#### Remediation
Calculate `dwImageOffset` and `dwBytesInRes` relative to `state->start_pos`:
```cpp
const INT64 start_pos = state->start_pos;
INT64 current_pos = io->tell_proc(handle);
if ((current_pos - start_pos) > (INT64)0xFFFFFFFFu) {
    free(icon_list);
    throw "ICO image offset exceeds 4 GB limit";
}
DWORD dwImageOffset = (DWORD)(current_pos - start_pos);

for(k = 0; k < icon_header->idCount; k++) {
    ...
    INT64 after_image_pos = io->tell_proc(handle);
    DWORD dwBytesInRes = (DWORD)(after_image_pos - start_pos) - dwImageOffset;
    icon_list[k].dwImageOffset = dwImageOffset;
    icon_list[k].dwBytesInRes  = dwBytesInRes;
    dwImageOffset += dwBytesInRes;
}
```

---

### 3. BUG-03: `JPEGTransform.cpp` Truncates 64-bit Offset on 32-bit Linux

#### Problem
In `Source/FreeImageToolkit/JPEGTransform.cpp`:
```cpp
const INT64 end = FreeImage_ftell64(f);
if(end < 0) {
    return;
}
#ifdef _WIN32
if(_chsize_s(_fileno(f), end) != 0) {
#else
if(ftruncate(fileno(f), (off_t)end) != 0) {
#endif
```
`JPEGTransform.cpp` does not define `_FILE_OFFSET_BITS 64`, nor is it passed in `Makefile.gnu` or `Makefile.fip`. On 32-bit Linux (glibc), `off_t` is a signed 32-bit integer (`long`). The cast `(off_t)end` truncates the 64-bit position to 32 bits, causing `ftruncate` to truncate the file to the wrong size when operating on files larger than 2 GB.

#### Remediation
Define `_FILE_OFFSET_BITS 64` before any includes in `JPEGTransform.cpp` (identical to `FreeImageIO.cpp`):
```cpp
#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
#define _FILE_OFFSET_BITS 64
#endif
```
Additionally, add `-D_FILE_OFFSET_BITS=64` to `COMPILERFLAGS` in `Makefile.gnu`, `Makefile.fip`, and `Makefile.cygwin`.

---

### 4. BUG-04: Test Scripts and FreeImagePlus Narrow `(off_t)offset` on 32-bit Linux

#### Problem
In:
- `Wrapper/FreeImagePlus/test/fipTestMPageStream.cpp` (lines 40-54)
- `TestAPI/testMPageStream.cpp` (lines 47-61)
- `TestAPI/J2K/corpus.c` (lines 75-76)
- `TestAPI/JXR/narrowio.c` (lines 19-22)

The callback implementations use:
```cpp
static int DLL_CALLCONV
mySeekProc(fi_handle handle, INT64 offset, int origin) {
#ifdef _WIN32
    return _fseeki64((FILE *)handle, offset, origin);
#else
    return fseeko((FILE *)handle, (off_t)offset, origin);
#endif
}
```
None of these files define `_FILE_OFFSET_BITS 64`, nor do their respective test Makefiles specify `-D_FILE_OFFSET_BITS=64`. On 32-bit Linux, `(off_t)offset` truncates the 64-bit parameter back to 32 bits, re-introducing the 2 GB barrier.

#### Remediation
Add `#define _FILE_OFFSET_BITS 64` before includes or add `-D_FILE_OFFSET_BITS=64` to test Makefiles.

---

### 5. BUG-05: `_tiffSeekProc` Returns Corrupted Error Offset in Offset Streams (`PluginTIFF.cpp`)

#### Problem
In `Source/FreeImage/PluginTIFF.cpp`:
```cpp
static toff_t
_tiffSeekProc(thandle_t handle, toff_t off, int whence) {
    fi_TIFFIO *fio = (fi_TIFFIO*)handle;
    if(whence == SEEK_SET) {
        fio->io->seek_proc(fio->handle, fio->start + (INT64)off, SEEK_SET);
    } else {
        fio->io->seek_proc(fio->handle, (INT64)off, whence);
    }
    return (toff_t)(fio->io->tell_proc(fio->handle) - fio->start);
}
```
1. LibTIFF's `toff_t` is `uint64_t`, and its error return sentinel is `(toff_t)-1` (`~0ULL`).
2. If `tell_proc` returns `-1` (failure), `_tiffSeekProc` evaluates `(toff_t)(-1 - fio->start)`.
   - If `fio->start == 0`, `(toff_t)(-1)` is returned, which matches `(toff_t)-1`.
   - If `fio->start > 0` (e.g. `fio->start = 512`), `(toff_t)(-513)` is returned. LibTIFF does not recognize this as an error and assumes the seek succeeded at file position `0xFFFFFFFFFFFFFE00`.
3. In addition, the return code of `seek_proc` is not inspected.

#### Remediation
```cpp
static toff_t
_tiffSeekProc(thandle_t handle, toff_t off, int whence) {
    fi_TIFFIO *fio = (fi_TIFFIO*)handle;
    const INT64 target = (whence == SEEK_SET) ? (fio->start + (INT64)off) : (INT64)off;
    if(fio->io->seek_proc(fio->handle, target, whence) != 0) {
        return (toff_t)-1;
    }
    const INT64 pos = fio->io->tell_proc(fio->handle);
    if(pos < fio->start) {
        return (toff_t)-1;
    }
    return (toff_t)(pos - fio->start);
}
```

---

### 6. BUG-06: `PluginRAW.cpp` Missing `_start` Clamping

#### Problem
In `Source/FreeImage/PluginRAW.cpp`:
```cpp
_start = io->tell_proc(handle);
...
INT64 tell() {
    return _io->tell_proc(_handle) - _start;
}
```
In `PluginTIFF.cpp`, `PluginEXR.cpp`, `PluginAVIF.cpp`, and `PluginHEIF.cpp`, `start` is clamped:
```cpp
const INT64 start = io->tell_proc(handle);
fio->start = (start > 0) ? start : 0;
```
If `tell_proc` returns `-1` (e.g., non-seekable stream), `PluginRAW.cpp` stores `_start = -1`. Subsequent `tell()` calls return `pos - (-1)` = `pos + 1`, shifting all stream positions by 1 byte.

#### Remediation
Clamp `_start` in the constructor:
```cpp
const INT64 start = io->tell_proc(handle);
_start = (start > 0) ? start : 0;
```

---

### 7. OMISSION-01: Public Memory API Exports Retain 32-Bit `long` Signatures

#### Problem
In `Source/FreeImage.h` and `Source/FreeImage/MemoryIO.cpp`:
```c
DLL_API long DLL_CALLCONV FreeImage_TellMemory(FIMEMORY *stream);
DLL_API BOOL DLL_CALLCONV FreeImage_SeekMemory(FIMEMORY *stream, long offset, int origin);
```
While `FreeImageIO` callbacks (`FI_SeekProc`, `FI_TellProc`) were expanded to `INT64`, `FreeImage_SeekMemory` and `FreeImage_TellMemory` were kept as `long`.
- On Windows (both 32-bit and 64-bit) and 32-bit Linux, `long` is 32-bit signed (`LONG_MAX = 2147483647`).
- External callers cannot seek to an offset > 2 GB via `FreeImage_SeekMemory(stream, 2500000000LL, SEEK_SET)`.
- If a memory stream has grown past 2 GB, `FreeImage_TellMemory` returns `-1L`.
- `FreeImagePlus` (`fipMemoryIO::tell()`, `fipMemoryIO::seek()`) and `FreeImage.NET` (`FreeImage.TellMemory()`, `FreeImage.SeekMemory()`) inherit this 2 GB restriction.

#### Recommendation
To preserve existing binary compatibility while allowing 64-bit memory stream operations, provide 64-bit export variants:
```c
DLL_API INT64 DLL_CALLCONV FreeImage_TellMemory64(FIMEMORY *stream);
DLL_API BOOL  DLL_CALLCONV FreeImage_SeekMemory64(FIMEMORY *stream, INT64 offset, int origin);
```

---

### 8. OMISSION-02: User-Buffer Memory Streams Limited to 32-Bit `DWORD`

#### Problem
In `Source/FreeImage.h`:
```c
DLL_API FIMEMORY *DLL_CALLCONV FreeImage_OpenMemory(BYTE *data FI_DEFAULT(0), DWORD size_in_bytes FI_DEFAULT(0));
DLL_API BOOL DLL_CALLCONV FreeImage_AcquireMemory(FIMEMORY *stream, BYTE **data, DWORD *size_in_bytes);
```
- `FreeImage_OpenMemory` takes `DWORD size_in_bytes` (32 bits). Callers cannot wrap a memory buffer larger than 4 GB on 64-bit platforms.
- `FreeImage_AcquireMemory` returns `FALSE` if the memory stream exceeds 4 GB.

#### Recommendation
Provide 64-bit export variants taking `size_t` or `UINT64`:
```c
DLL_API FIMEMORY *DLL_CALLCONV FreeImage_OpenMemory64(BYTE *data, UINT64 size_in_bytes);
DLL_API BOOL DLL_CALLCONV FreeImage_AcquireMemory64(FIMEMORY *stream, BYTE **data, UINT64 *size_in_bytes);
```

---

### 9. OMISSION-03: `Examples/Generic/FIIO_Mem.h` Retains 32-bit Struct Fields

#### Problem
In `Examples/Generic/FIIO_Mem.h`:
```c
typedef struct fiio_mem_handle_s {
    long filelen, datalen, curpos;
    void *data;
} fiio_mem_handle;
```
In `Examples/Generic/FIIO_Mem.cpp`:
```cpp
int fiio_mem_SeekProc(fi_handle handle, INT64 offset, int origin) {
    ...
    case SEEK_SET:
        if( offset >= 0 ) {
            FIIOMEM(curpos) = offset; // <--- assigns INT64 to long
            return 0;
        }
```
`fiio_mem_SeekProc` was updated to `INT64 offset`, but `fiio_mem_handle` kept `long curpos`. On Windows and 32-bit Linux, assigning `INT64 offset` to `long curpos` narrows and overflows.

#### Remediation
Update `fiio_mem_handle`:
```c
typedef struct fiio_mem_handle_s {
    INT64 filelen, datalen, curpos;
    void *data;
} fiio_mem_handle;
```

---

### 10. OMISSION-04: Visual Basic 6 Wrapper (`Wrapper/VB6/src/MFreeImage.bas`)

#### Problem
`Wrapper/VB6/src/MFreeImage.bas` was untouched:
```vb
Public Type FreeImageIO
   read_proc As Long
   write_proc As Long
   seek_proc As Long
   tell_proc As Long
End Type
```
In 32-bit x86 stdcall:
- `FI_SeekProc` changed from 3 arguments totaling 12 bytes (`handle`, `long offset`, `origin`) to 16 bytes (`handle`, `INT64 offset` [8 bytes], `origin`).
- `FI_TellProc` changed from returning 32-bit in `EAX` to 64-bit in `EDX:EAX`.
If any VB6 application implements custom callbacks using standard VB6 types (`ByVal Offset As Long`), stdcall stack corruption will occur upon invocation.

#### Remediation
Update comments in `MFreeImage.bas` and `WhatsNew_VB.txt` to clearly explain that `FreeImageIO` callbacks now require 64-bit integer handling (`Currency` or low/high dwords on VB6).

---

### 11. PRE-01 & PRE-02: Plugin Edge Cases (`PluginGIF.cpp` & `PluginJXR.cpp`)

1. **`PluginGIF.cpp` Lines 920 & 1293**:
   `io->seek_proc(handle, 6, SEEK_SET);`
   Seeks to byte 6 of the stream unconditionally instead of `stream_start + 6`. When a GIF is opened from an offset stream (e.g. at an offset inside another container), playback and canvas dimensions fail to load.
   *Remediation*: Save `stream_start = io->tell_proc(handle);` in `Open()` and seek to `stream_start + 6`.

2. **`PluginJXR.cpp` Line 74**:
   `fio->io->seek_proc(fio->handle, fio->start + (INT64)offPos, SEEK_SET);`
   `offPos` is `size_t` (unsigned 64-bit on 64-bit platforms). If `offPos > INT64_MAX - fio->start`, `fio->start + (INT64)offPos` overflows into negative numbers.
   *Remediation*: Check `if (offPos > (size_t)(std::numeric_limits<INT64>::max() - fio->start)) return WMP_errFileIO;`.

---

## Platform Verification Matrix

| Target | Build Toolchain | Core Compiles | `-Wshorten-64-to-32` Clean | Test Suites Status |
|---|---|:---:|:---:|---|
| **Linux x86_64** | gcc / g++ 15.0 | Yes | Yes | All tests pass (`testAPI`, `IO`, `AVIF`, `HEIF`, `JXR`) |
| **Linux x86 (32-bit)** | gcc / g++ `-m32` | Yes (syntax) | Yes | **Requires `-D_FILE_OFFSET_BITS=64`** in Makefiles and headers |
| **Windows x86_64** | zig clang++ `x86_64-windows-gnu` | Yes | Yes | Zero narrowing warnings on changed files |
| **Windows x86 (32-bit)** | zig clang++ `x86-windows-gnu` | Yes | Yes | Zero narrowing warnings on changed files |

---

## Conclusion & Action Checklist

The `worktree-io64` branch achieves its primary goal of enabling 64-bit file stream positioning in FreeImage. To make it completely robust and production-ready across Linux and Windows (32-bit and 64-bit), the following corrections should be applied:

- [ ] **Fix `MultiPage.cpp:1335`**: Replace `FreeImage_TellMemory(stream)` with `header->io.tell_proc(header->handle)`.
- [ ] **Fix `PluginICO.cpp:819, 843`**: Make `dwImageOffset` relative to `state->start_pos` in `Save()`.
- [ ] **Fix `JPEGTransform.cpp`**: Add `#define _FILE_OFFSET_BITS 64` before includes.
- [ ] **Fix Makefiles**: Add `-D_FILE_OFFSET_BITS=64` to `Makefile.gnu`, `Makefile.fip`, `Makefile.cygwin`, and `TestAPI/Makefile`.
- [ ] **Fix `PluginTIFF.cpp:161`**: Return `(toff_t)-1` on seek failure instead of `(toff_t)(-1 - fio->start)`.
- [ ] **Fix `PluginRAW.cpp:52`**: Clamp `_start = (start > 0) ? start : 0;`.
- [ ] **Fix `FIIO_Mem.h`**: Change `curpos`, `filelen`, `datalen` to `INT64`.
- [ ] **Add 64-bit Memory Exports**: Provide `FreeImage_SeekMemory64`, `FreeImage_TellMemory64`, `FreeImage_OpenMemory64`, and `FreeImage_AcquireMemory64`.
