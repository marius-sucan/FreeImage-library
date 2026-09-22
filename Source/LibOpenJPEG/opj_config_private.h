/* FreeImage: stands in for CMake's version; MUTEX_pthread/win32 are command-line defines */
#ifndef OPJ_CONFIG_PRIVATE_H
#define OPJ_CONFIG_PRIVATE_H

#define OPJ_PACKAGE_VERSION "2.5.4"

#if defined(_WIN32)

/* MSVC and MinGW */
#define OPJ_HAVE_MALLOC_H
#define OPJ_HAVE__ALIGNED_MALLOC

#else

/* POSIX: posix_memalign, fseeko/ftello */
#define OPJ_HAVE_POSIX_MEMALIGN
#define OPJ_HAVE_FSEEKO 1

#if !defined(_POSIX_C_SOURCE)
/* glibc hides fseeko/posix_memalign under -std=c99 */
#define _POSIX_C_SOURCE 200112L
#endif

#endif /* _WIN32 */

/* byte order, used by cio.h */
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
