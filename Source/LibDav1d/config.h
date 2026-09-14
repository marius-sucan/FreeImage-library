/*
 * config.h for the copy of dav1d bundled with FreeImage.
 *
 * dav1d's own build system (meson) probes the toolchain and generates this
 * file. FreeImage compiles every bundled library from one flat list of
 * sources with one set of flags (Makefile.srcs and the .vcxproj files), so
 * the probe results are spelled out here from compiler and platform macros
 * instead. It covers the toolchains FreeImage is built with: GCC and clang
 * on Linux, macOS, MinGW, Cygwin and the BSDs, and MSVC.
 *
 * Where this differs from a default meson build of dav1d:
 *
 *  - HAVE_ASM is 0. dav1d's SIMD is hand-written NASM (x86) and GAS (ARM)
 *    assembly, for which the FreeImage makefiles and project files have no
 *    build rule. Decoding therefore runs on dav1d's portable C paths; it is
 *    still spread over every core through dav1d's task threading.
 *
 *  - DAV1D_API is empty, so no dav1d symbol is exported from FreeImage:
 *    -fvisibility=hidden hides them in the ELF/Mach-O shared library, and a
 *    Windows DLL exports nothing without __declspec(dllexport). A program
 *    that also links a system libdav1d cannot collide with this copy.
 *
 *  - TRIM_DSP_FUNCTIONS is 0: it drops C functions that the assembly always
 *    replaces, and with no assembly every one of them is needed.
 *
 * The 8- and 16-bit instances of the src/..._tmpl.c files are produced by the
 * wrappers in bitdepth/, which is what meson's -DBITDEPTH=8/16 does upstream.
 */

#ifndef DAV1D_FREEIMAGE_CONFIG_H
#define DAV1D_FREEIMAGE_CONFIG_H

/* ------------------------------------------------------------------------
 * Feature-test macros. They must come before the first system header, which
 * holds because every dav1d translation unit includes config.h first.
 * ---------------------------------------------------------------------- */
#if defined(__linux__) || defined(__gnu_hurd__) || defined(__EMSCRIPTEN__)
#  ifndef _GNU_SOURCE
#    define _GNU_SOURCE 1
#  endif
#endif
#if !defined(_WIN32)
#  ifndef _FILE_OFFSET_BITS
#    define _FILE_OFFSET_BITS 64
#  endif
#endif

/* Windows, as set by meson.build for host_machine.system() == 'windows'. */
#if defined(_WIN32)
#  ifndef _WIN32_WINNT
#    define _WIN32_WINNT 0x0601
#  endif
#  ifndef UNICODE
#    define UNICODE 1
#  endif
#  ifndef _UNICODE
#    define _UNICODE 1
#  endif
#  if defined(__MINGW32__) && !defined(__USE_MINGW_ANSI_STDIO)
#    define __USE_MINGW_ANSI_STDIO 1
#  endif
#  if defined(_MSC_VER) && !defined(_CRT_DECLARE_NONSTDC_NAMES)
#    define _CRT_DECLARE_NONSTDC_NAMES 1
#  endif
#endif

/* ------------------------------------------------------------------------
 * Target architecture. Used by include/common/attributes.h to pick the
 * stack alignment of local buffers and by src/cpu.h to pick the per-arch
 * cpu.h header, so it must be right even though no assembly is built.
 * ---------------------------------------------------------------------- */
#if defined(__aarch64__) || defined(_M_ARM64)
#  define ARCH_AARCH64 1
#else
#  define ARCH_AARCH64 0
#endif
#if (defined(__arm__) || defined(_M_ARM)) && !ARCH_AARCH64
#  define ARCH_ARM 1
#else
#  define ARCH_ARM 0
#endif
#if defined(__x86_64__) || defined(_M_X64)
#  define ARCH_X86_64 1
#else
#  define ARCH_X86_64 0
#endif
#if (defined(__i386__) || defined(_M_IX86)) && !ARCH_X86_64
#  define ARCH_X86_32 1
#else
#  define ARCH_X86_32 0
#endif
#define ARCH_X86 (ARCH_X86_32 || ARCH_X86_64)
#if defined(__powerpc64__) && defined(__LITTLE_ENDIAN__)
#  define ARCH_PPC64LE 1
#else
#  define ARCH_PPC64LE 0
#endif
#if defined(__riscv)
#  define ARCH_RISCV 1
#  if defined(__riscv_xlen) && (__riscv_xlen == 64)
#    define ARCH_RV64 1
#    define ARCH_RV32 0
#  else
#    define ARCH_RV64 0
#    define ARCH_RV32 1
#  endif
#else
#  define ARCH_RISCV 0
#  define ARCH_RV32 0
#  define ARCH_RV64 0
#endif
#if defined(__loongarch__)
#  define ARCH_LOONGARCH 1
#  if defined(__loongarch64) || (defined(__loongarch_grlen) && (__loongarch_grlen == 64))
#    define ARCH_LOONGARCH64 1
#    define ARCH_LOONGARCH32 0
#  else
#    define ARCH_LOONGARCH64 0
#    define ARCH_LOONGARCH32 1
#  endif
#else
#  define ARCH_LOONGARCH 0
#  define ARCH_LOONGARCH32 0
#  define ARCH_LOONGARCH64 0
#endif

#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#  define ENDIANNESS_BIG 1
#else
#  define ENDIANNESS_BIG 0
#endif

/* ------------------------------------------------------------------------
 * Build options (meson_options.txt defaults, except for the assembly).
 * ---------------------------------------------------------------------- */
#define CONFIG_8BPC 1
#define CONFIG_16BPC 1
#define CONFIG_LOG 1
#define HAVE_ASM 0
#define TRIM_DSP_FUNCTIONS 0

/* CPU feature detection helpers; only the assembly paths consult them. */
#define HAVE_GETAUXVAL 0
#define HAVE_ELF_AUX_INFO 0

/* ------------------------------------------------------------------------
 * Headers and functions.
 * ---------------------------------------------------------------------- */
#if defined(_WIN32)
#  define HAVE_IO_H 1
#else
#  define HAVE_IO_H 0
#endif
#if defined(_MSC_VER)
#  define HAVE_UNISTD_H 0
#else
#  define HAVE_UNISTD_H 1
#endif
#define HAVE_SYS_TYPES_H 1

#if defined(_WIN32)
#  define HAVE_CLOCK_GETTIME 0
#  define HAVE_SIGACTION 0
#else
#  define HAVE_CLOCK_GETTIME 1
#  define HAVE_SIGACTION 1
#endif

/* Aligned allocation (src/mem.h): Windows uses _aligned_malloc, everything
 * else posix_memalign, which every POSIX libc FreeImage targets provides. */
#if defined(_WIN32)
#  define HAVE_POSIX_MEMALIGN 0
#  define HAVE_MEMALIGN 0
#  define HAVE_ALIGNED_ALLOC 0
#else
#  define HAVE_POSIX_MEMALIGN 1
#  define HAVE_MEMALIGN 0
#  define HAVE_ALIGNED_ALLOC 0
#endif

/* Threads (src/thread.h, src/cpu.c). Windows goes through src/win32/thread.c
 * and needs none of these. Linux names its threads with prctl(), so the
 * pthread_setname_np() variants only matter elsewhere. */
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
#  define HAVE_PTHREAD_NP_H 1
#else
#  define HAVE_PTHREAD_NP_H 0
#endif
#if (defined(__linux__) && !defined(__ANDROID__)) || defined(__FreeBSD__) || defined(__DragonFly__)
#  define HAVE_PTHREAD_GETAFFINITY_NP 1
#  define HAVE_PTHREAD_SETAFFINITY_NP 1
#else
#  define HAVE_PTHREAD_GETAFFINITY_NP 0
#  define HAVE_PTHREAD_SETAFFINITY_NP 0
#endif
#if defined(__APPLE__) || defined(__NetBSD__) || defined(__sun) || (defined(__linux__) && !defined(__ANDROID__))
#  define HAVE_PTHREAD_SETNAME_NP 1
#else
#  define HAVE_PTHREAD_SETNAME_NP 0
#endif
#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__OpenBSD__)
#  define HAVE_PTHREAD_SET_NAME_NP 1
#else
#  define HAVE_PTHREAD_SET_NAME_NP 0
#endif

/* Keep dav1d's symbols internal to FreeImage (see the header comment). */
#define DAV1D_API

#endif /* DAV1D_FREEIMAGE_CONFIG_H */
