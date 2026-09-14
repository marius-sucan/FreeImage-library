/*
 * FreeImage build helper for dav1d.
 *
 * dav1d compiles src/loopfilter_tmpl.c once per bit depth, with BITDEPTH set on the
 * compiler command line by its meson build. FreeImage compiles every source
 * file exactly once with one set of flags, so this wrapper supplies the
 * 8-bit instance and loopfilter_16bpc.c the 16-bit one.
 */
#define BITDEPTH 8
#include "src/loopfilter_tmpl.c"
