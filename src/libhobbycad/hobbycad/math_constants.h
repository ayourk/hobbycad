// =====================================================================
//  hobbycad/math_constants.h — shared math constants (repository: hobbycad)
//  SPDX-License-Identifier: GPL-3.0-only
// =====================================================================
//
//  A single home for the M_PI portability fallback. M_PI is a POSIX
//  extension, not standard C++, so a strict <cmath> may not define it; this
//  header supplies it once instead of every translation unit repeating the
//  same #ifndef block. Qt-free by design.
#ifndef HOBBYCAD_MATH_CONSTANTS_H
#define HOBBYCAD_MATH_CONSTANTS_H

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif // HOBBYCAD_MATH_CONSTANTS_H
