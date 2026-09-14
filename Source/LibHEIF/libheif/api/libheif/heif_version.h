/*
 * HEIF codec.
 * Copyright (c) 2017 Dirk Farin <dirk.farin@gmail.com>
 *
 * This file is part of libheif.
 *
 * libheif is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libheif is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libheif.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * FreeImage: this is heif_version.h.in as libheif's own build system (CMake)
 * would generate it for release 1.23.4. FreeImage compiles libheif from a flat
 * list of sources with no configure step, so the values are spelled out here.
 * Update them when the bundled libheif is updated. The plugin directory is
 * empty on purpose: the libde265 decoder is compiled in and dynamic plugin
 * loading (ENABLE_PLUGIN_LOADING) is not built.
 */

#ifndef LIBHEIF_HEIF_VERSION_H
#define LIBHEIF_HEIF_VERSION_H

/* Numeric representation of the version */
#define LIBHEIF_NUMERIC_VERSION ((1<<24) | (23<<16) | (4<<8) | 0)

/* Version string */
#define LIBHEIF_VERSION "1.23.4"

#define LIBHEIF_PLUGIN_DIRECTORY ""

#endif  // LIBHEIF_HEIF_VERSION_H
