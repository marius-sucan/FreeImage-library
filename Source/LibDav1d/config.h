/* dav1d config for FreeImage (meson generates it upstream): no asm, no exports */

#ifndef DAV1D_FREEIMAGE_CONFIG_H
#define DAV1D_FREEIMAGE_CONFIG_H

/* feature-test macros; every dav1d file includes config.h first */
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

/* target arch: sets stack alignment even without asm */
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

/* build options: meson defaults, minus asm */
#define CONFIG_8BPC 1
#define CONFIG_16BPC 1
#define CONFIG_LOG 1
#define HAVE_ASM 0
#define TRIM_DSP_FUNCTIONS 0

/* only the asm paths use these */
#define HAVE_GETAUXVAL 0
#define HAVE_ELF_AUX_INFO 0

/* headers and functions */
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

/* aligned allocation: _aligned_malloc on Windows, else posix_memalign */
#if defined(_WIN32)
#  define HAVE_POSIX_MEMALIGN 0
#  define HAVE_MEMALIGN 0
#  define HAVE_ALIGNED_ALLOC 0
#else
#  define HAVE_POSIX_MEMALIGN 1
#  define HAVE_MEMALIGN 0
#  define HAVE_ALIGNED_ALLOC 0
#endif

/* threads; Windows uses src/win32/thread.c */
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

/* no exported dav1d symbols */
#define DAV1D_API

#endif /* DAV1D_FREEIMAGE_CONFIG_H */
