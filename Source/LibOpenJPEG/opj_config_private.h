/* FreeImage: hand-written stand-in for the opj_config_private.h that
 * OpenJPEG's CMake build generates from opj_config_private.h.cmake.in.
 * FreeImage builds the library with its own makefiles and MSVC project, so
 * the platform probes CMake would run are answered here by hand, for the
 * two families of platforms FreeImage targets.
 *
 * opj_includes.h includes this header before any system header, so the
 * feature-test macro defined below reaches <stdlib.h> and <stdio.h> in every
 * translation unit of the library.
 *
 * Deliberately NOT configured here: MUTEX_pthread / MUTEX_win32, which pick
 * the thread pool's mutex implementation. thread.c tests them before it
 * includes opj_includes.h, so they only work as command-line definitions;
 * the makefiles and LibOpenJPEG.2017.vcxproj define them next to OPJ_STATIC.
 * Without either, thread.c builds its single-threaded stub and
 * opj_codec_set_threads() becomes a no-op. */
#ifndef OPJ_CONFIG_PRIVATE_H
#define OPJ_CONFIG_PRIVATE_H

#define OPJ_PACKAGE_VERSION "2.5.4"

#if defined(_WIN32)

/* MSVC and MinGW: <stdlib.h> declares _aligned_malloc / _aligned_realloc /
 * _aligned_free (opj_malloc.c). File seeks use _fseeki64 (opj_includes.h). */
#define OPJ_HAVE_MALLOC_H
#define OPJ_HAVE__ALIGNED_MALLOC

#else

/* Linux, macOS, the BSDs, Solaris, Cygwin: posix_memalign() and the 64-bit
 * fseeko()/ftello(). */
#define OPJ_HAVE_POSIX_MEMALIGN
#define OPJ_HAVE_FSEEKO 1

#if !defined(_POSIX_C_SOURCE)
/* Get declarations of fseeko, ftello, posix_memalign - glibc hides them
 * under -std=c99 unless a POSIX level is requested. Same value as the CMake
 * template uses. */
#define _POSIX_C_SOURCE 200112L
#endif

#endif /* _WIN32 */

/* Byte order. cio.h picks the big-endian or little-endian marker readers
 * and writers from this, so it has to be right on every target.
 * All compilers that support Mac OS X define either __BIG_ENDIAN__ or
 * __LITTLE_ENDIAN__ to match the endianness of the architecture being
 * compiled for (which is not necessarily that of the build machine, hence
 * Universal Binaries prefer those). Elsewhere GCC and Clang provide
 * __BYTE_ORDER__; the remaining tests cover older compilers on the
 * big-endian architectures that are still around. */
#if defined(__APPLE__)
#  if defined(__BIG_ENDIAN__)
#    define OPJ_BIG_ENDIAN
#  endif
#elif defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__)
#  if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#    define OPJ_BIG_ENDIAN
#  endif
#elif defined(__BIG_ENDIAN__) || defined(_BIG_ENDIAN) || defined(__ARMEB__) || \
      defined(__MIPSEB__) || defined(__sparc) || defined(__sparc__) || \
      defined(__hppa__) || defined(__s390__) || defined(__s390x__) || \
      (defined(BYTE_ORDER) && defined(BIG_ENDIAN) && BYTE_ORDER == BIG_ENDIAN) || \
      (defined(__BYTE_ORDER) && defined(__BIG_ENDIAN) && __BYTE_ORDER == __BIG_ENDIAN)
#  define OPJ_BIG_ENDIAN
#endif

#endif /* OPJ_CONFIG_PRIVATE_H */
