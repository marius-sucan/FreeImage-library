/* FreeImage: CMake's generate_export_header() output for a static build, written by hand */

#ifndef JXL_CMS_EXPORT_H
#define JXL_CMS_EXPORT_H

#define JXL_CMS_EXPORT
#define JXL_CMS_NO_EXPORT

#ifndef JXL_CMS_DEPRECATED
#  if defined(_MSC_VER)
#    define JXL_CMS_DEPRECATED __declspec(deprecated)
#  else
#    define JXL_CMS_DEPRECATED __attribute__ ((__deprecated__))
#  endif
#endif

#ifndef JXL_CMS_DEPRECATED_EXPORT
#  define JXL_CMS_DEPRECATED_EXPORT JXL_CMS_EXPORT JXL_CMS_DEPRECATED
#endif

#ifndef JXL_CMS_DEPRECATED_NO_EXPORT
#  define JXL_CMS_DEPRECATED_NO_EXPORT JXL_CMS_NO_EXPORT JXL_CMS_DEPRECATED
#endif

#endif /* JXL_CMS_EXPORT_H */
