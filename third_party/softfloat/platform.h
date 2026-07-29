/*
 * Build configuration for Berkeley SoftFloat Release 3e.
 *
 * The implementation is fetched from the pinned upstream revision declared
 * in the top-level CMakeLists.txt. This configuration selects the same GCC
 * intrinsics as the upstream Win64 and Linux 64-bit build templates.
 */

#ifndef RV_SOFTFLOAT_PLATFORM_H
#define RV_SOFTFLOAT_PLATFORM_H

#define LITTLEENDIAN 1

#define INLINE static inline

#define SOFTFLOAT_BUILTIN_CLZ 1

#if defined(__SIZEOF_INT128__)
#define SOFTFLOAT_INTRINSIC_INT128 1
#endif

#include "opts-GCC.h"

#endif
