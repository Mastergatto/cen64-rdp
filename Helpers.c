/* ============================================================================
 *  Helpers.c: Reality Display Processor (RDP) Interface.
 *
 *  RDPSIM: Reality Signal Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'LICENSE', which is part of this source code package.
 * ========================================================================= */
#include "Common.h"

#ifdef __cplusplus
#include <cassert>
#else
#include <assert.h>
#endif

#ifdef USE_SSE
#include <tmmintrin.h>
#endif

/* ============================================================================
 *  Global look-up tables.
 * ========================================================================= */
const int32_t FlipLUT[2] = {-1, 1};

/* ============================================================================
 *  FlipSigns: Conditionally flips the sign of 8 int32_ts according to flip.
 * ========================================================================= */
void
FlipSigns(int32_t *dest, const int32_t *src, unsigned flip) {
  assert((flip & 1) == flip);

  static int32_t FlipVector[2][4] = {
    {-1, -1, -1, -1},
    { 1,  1,  1,  1},
  };

#ifdef USE_SSE
  __m128i sign   = _mm_load_si128((__m128i*) (FlipVector[flip]));
  __m128i spans1 = _mm_load_si128((__m128i*) (src + 0));
  __m128i spans2 = _mm_load_si128((__m128i*) (src + 4));

  spans1 = _mm_sign_epi32(spans1, sign);
  spans2 = _mm_sign_epi32(spans2, sign);

  _mm_store_si128((__m128i*) (dest + 0), spans1);
  _mm_store_si128((__m128i*) (dest + 4), spans2);
#else
  if (!flip) {
    dest[0] = -src[0];
    dest[1] = -src[1];
    dest[2] = -src[2];
    dest[3] = -src[3];
    dest[4] = -src[4];
    dest[5] = -src[5];
    dest[6] = -src[6];
    dest[7] = -src[7];
  }
#endif
}

