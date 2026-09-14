/*
 * FreeImage build helper for dav1d.
 *
 * dav1d compiles src/cdef_apply_tmpl.c once per bit depth, with BITDEPTH set on the
 * compiler command line by its meson build. FreeImage compiles every source
 * file exactly once with one set of flags, so this wrapper supplies the
 * 16-bit instance and cdef_apply_8bpc.c the 8-bit one.
 */
#define BITDEPTH 16
#include "src/cdef_apply_tmpl.c"
