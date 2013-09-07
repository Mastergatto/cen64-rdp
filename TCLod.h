/* ============================================================================
 *  TCLod.h: Texture Coordinate (TC) Level of Detail (LOD) Functions.
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
#ifndef __RDP__TCLOD__
#define __RDP__TCLOD__
#include "Common.h"
#include "Core.h"

int32_t tclod_tcclamp(int32_t x);

void tclod_4x17_to_15(int32_t scurr, int32_t snext,
  int32_t tcurr, int32_t tnext, int32_t previous, int32_t* lod);

void lodfrac_lodtile_signals(int lodclamp, int32_t lod,
  uint32_t* l_tile, uint32_t* magnify, uint32_t* distant);

void tclod_1cycle_current_simple(int32_t* sss, int32_t* sst,
  const int32_t *spanptr, const int32_t *dincs, int32_t scanline,
  int32_t prim_tile, int32_t* t1, const SPANSIGS* sigs);

#endif

