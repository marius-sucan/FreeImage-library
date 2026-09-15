/* FreeImage: hand-written stand-in for the opj_config.h that OpenJPEG's CMake
 * build generates from opj_config.h.cmake.in. FreeImage builds the library
 * with its own makefiles and MSVC project, so nothing generates it here.
 * Keep the version in step with NEWS.md when the library is updated. */
#ifndef OPJ_CONFIG_H_INCLUDED
#define OPJ_CONFIG_H_INCLUDED

/*--------------------------------------------------------------------------*/
/* OpenJPEG Versioning                                                      */

/* Version number. */
#define OPJ_VERSION_MAJOR 2
#define OPJ_VERSION_MINOR 5
#define OPJ_VERSION_BUILD 4

#endif
