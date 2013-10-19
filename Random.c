/* ============================================================================
 *  Random.c: RNG functions.
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
#include "Common.h"
#include "Random.h"

/* Global data. */
int32_t iseed = 1;

int32_t
irand() {
  iseed *= 0x343FD;
  iseed += 0x269EC3;

  return ((iseed >> 16) & 0x7FFF);
}

