/*
 * H.265 video codec.
 * Copyright (c) 2013-2014 struktur AG, Dirk Farin <farin@struktur.de>
 *
 * This file is part of libde265.
 *
 * libde265 is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * libde265 is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with libde265.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * FreeImage: this is de265-version.h.in as libde265's own build system
 * (CMake) would generate it for release 1.1.3. The numeric version is
 * binary-coded decimal, 0xMMmmppXX for major.minor.patch, which is what
 * de265_get_version_number() returns. Update both values when the bundled
 * libde265 is updated.
 */

#ifndef LIBDE265_VERSION_H
#define LIBDE265_VERSION_H

/* Numeric representation of the version */
#define LIBDE265_NUMERIC_VERSION 0x01010300

/* Version string */
#define LIBDE265_VERSION "1.1.3"

#endif
