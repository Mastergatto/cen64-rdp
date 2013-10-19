/* ============================================================================
 *  Dither.h: Dithering Functions.
 *
 *  Original author: `MooglyGuy`. Many thanks to: Ville Linde, `angrylion`,
 *  Shoutouts to: `olivieryuyu`, `marshallh`, `LaC`, `oman`, `pinchy`, `ziggy`,
 *  `FatCat` and other folks I forgot.
 *
 *  Vectorization, Cleanup by Tyler J. Stachecki.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'MAMELICENSE', which is part of this source code package.
 * ========================================================================= */
#ifndef __DITHER_H__
#define __DITHER_H__
#include "Common.h"

typedef void (*DitherFunc)(int32_t *, int32_t *, int32_t *, int32_t);
extern const DitherFunc DitherFuncLUT[2];

#endif

