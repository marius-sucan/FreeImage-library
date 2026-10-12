/* FreeImage: CMake's generate_export_header() output for a static build, written by hand */

#ifndef JXL_EXPORT_H
#define JXL_EXPORT_H

#define JXL_EXPORT
#define JXL_NO_EXPORT

#ifndef JXL_DEPRECATED
#  if defined(_MSC_VER)
#    define JXL_DEPRECATED __declspec(deprecated)
#  else
#    define JXL_DEPRECATED __attribute__ ((__deprecated__))
#  endif
#endif

#ifndef JXL_DEPRECATED_EXPORT
#  define JXL_DEPRECATED_EXPORT JXL_EXPORT JXL_DEPRECATED
#endif

#ifndef JXL_DEPRECATED_NO_EXPORT
#  define JXL_DEPRECATED_NO_EXPORT JXL_NO_EXPORT JXL_DEPRECATED
#endif

#endif /* JXL_EXPORT_H */
