/* FreeImage: CMake's generate_export_header() output for a static build, written by hand */

#ifndef JXL_THREADS_EXPORT_H
#define JXL_THREADS_EXPORT_H

#define JXL_THREADS_EXPORT
#define JXL_THREADS_NO_EXPORT

#ifndef JXL_THREADS_DEPRECATED
#  if defined(_MSC_VER)
#    define JXL_THREADS_DEPRECATED __declspec(deprecated)
#  else
#    define JXL_THREADS_DEPRECATED __attribute__ ((__deprecated__))
#  endif
#endif

#ifndef JXL_THREADS_DEPRECATED_EXPORT
#  define JXL_THREADS_DEPRECATED_EXPORT JXL_THREADS_EXPORT JXL_THREADS_DEPRECATED
#endif

#ifndef JXL_THREADS_DEPRECATED_NO_EXPORT
#  define JXL_THREADS_DEPRECATED_NO_EXPORT JXL_THREADS_NO_EXPORT JXL_THREADS_DEPRECATED
#endif

#endif /* JXL_THREADS_EXPORT_H */
