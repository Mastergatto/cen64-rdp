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
 *  AddVectors: Sums up all pairs of values in a vector.
 * ========================================================================= */
void
AddVectors(int32_t *dest, const int32_t *srca, const int32_t *srcb) {
#ifdef USE_SSE
  __m128i srca1 = _mm_load_si128((__m128i*) (srca + 0));
  __m128i srca2 = _mm_load_si128((__m128i*) (srca + 4));
  __m128i srcb1 = _mm_load_si128((__m128i*) (srcb + 0));
  __m128i srcb2 = _mm_load_si128((__m128i*) (srcb + 4));
  __m128i dest1, dest2;

  dest1 = _mm_add_epi32(srca1, srcb1);
  dest2 = _mm_add_epi32(srca2, srcb2);

  _mm_store_si128((__m128i*) (dest + 0), dest1);
  _mm_store_si128((__m128i*) (dest + 4), dest2);
#else
  dest[0] = srca[0] + srcb[0];
  dest[1] = srca[1] + srcb[1];
  dest[2] = srca[2] + srcb[2];
  dest[3] = srca[3] + srcb[3];
  dest[4] = srca[4] + srcb[4];
  dest[5] = srca[5] + srcb[5];
  dest[6] = srca[6] + srcb[6];
  dest[7] = srca[7] + srcb[7];
#endif
}

/* ============================================================================
 *  ASR8ClearLow: Performs an arithmetic right shift by 8, clears low bit.
 * ========================================================================= */
void
ASR8ClearLow(int32_t *dest, const int32_t *src) {
  static int32_t DataVector[4] align(16) = {
    ~1, ~1, ~1, ~1,
  };

#ifdef USE_SSE
  __m128i mask = _mm_load_si128((__m128i*) (DataVector));
  __m128i src1 = _mm_load_si128((__m128i*) (src + 0));
  __m128i src2 = _mm_load_si128((__m128i*) (src + 4));

  src1 = _mm_srai_epi32(src1, 8);
  src2 = _mm_srai_epi32(src2, 8);
  src1 = _mm_and_si128(src1, mask);
  src2 = _mm_and_si128(src2, mask);

  _mm_store_si128((__m128i*) (dest + 0), src1);
  _mm_store_si128((__m128i*) (dest + 4), src2);
#else
  dest[0] = (src[0] >> 8) & ~1;
  dest[1] = (src[1] >> 8) & ~1;
  dest[2] = (src[2] >> 8) & ~1;
  dest[3] = (src[3] >> 8) & ~1;
  dest[4] = (src[4] >> 8) & ~1;
  dest[5] = (src[5] >> 8) & ~1;
  dest[6] = (src[6] >> 8) & ~1;
  dest[7] = (src[7] >> 8) & ~1;
#endif
}

/* ============================================================================
 *  ClearLow5: Clears the least significant 5 bits within a vector.
 * ========================================================================= */
void
ClearLow5(int32_t *dest, const int32_t *src) {
  static int32_t DataVector[4] align(16) = {
    ~0x1F, ~0x1F, ~0x1F, ~0x1F,
  };

#ifdef USE_SSE
  __m128i mask = _mm_load_si128((__m128i*) (DataVector));
  __m128i src1 = _mm_load_si128((__m128i*) (src + 0));
  __m128i src2 = _mm_load_si128((__m128i*) (src + 4));

  src1 = _mm_and_si128(src1, mask);
  src2 = _mm_and_si128(src2, mask);

  _mm_store_si128((__m128i*) (dest + 0), src1);
  _mm_store_si128((__m128i*) (dest + 4), src2);
#else
  dest[0] = src[0] & ~0x1F;
  dest[1] = src[1] & ~0x1F;
  dest[2] = src[2] & ~0x1F;
  dest[3] = src[3] & ~0x1F;
  dest[4] = src[4] & ~0x1F;
  dest[5] = src[5] & ~0x1F;
  dest[6] = src[6] & ~0x1F;
  dest[7] = src[7] & ~0x1F;
#endif
}

/* ============================================================================
 *  ClearLow9: Clears the least significant 5 bits within a vector.
 * ========================================================================= */
void
ClearLow9(int32_t *dest, const int32_t *src) {
  static int32_t DataVector[4] align(16) = {
    ~0x1FF, ~0x1FF, ~0x1FF, ~0x1FF,
  };

#ifdef USE_SSE
  __m128i mask = _mm_load_si128((__m128i*) (DataVector));
  __m128i src1 = _mm_load_si128((__m128i*) (src + 0));
  __m128i src2 = _mm_load_si128((__m128i*) (src + 4));

  src1 = _mm_and_si128(src1, mask);
  src2 = _mm_and_si128(src2, mask);

  _mm_store_si128((__m128i*) (dest + 0), src1);
  _mm_store_si128((__m128i*) (dest + 4), src2);
#else
  dest[0] = src[0] & ~0x1FF;
  dest[1] = src[1] & ~0x1FF;
  dest[2] = src[2] & ~0x1FF;
  dest[3] = src[3] & ~0x1FF;
  dest[4] = src[4] & ~0x1FF;
  dest[5] = src[5] & ~0x1FF;
  dest[6] = src[6] & ~0x1FF;
  dest[7] = src[7] & ~0x1FF;
#endif
}

/* ============================================================================
 *  DiffASR2: Computes the difference, and subs (diff shift arith right by two).
 * ========================================================================= */
void
DiffASR2(int32_t *dest, const int32_t *srca, const int32_t *srcb) {
#ifdef USE_SSE
  __m128i srca1 = _mm_load_si128((__m128i*) (srca + 0));
  __m128i srca2 = _mm_load_si128((__m128i*) (srca + 4));
  __m128i srcb1 = _mm_load_si128((__m128i*) (srcb + 0));
  __m128i srcb2 = _mm_load_si128((__m128i*) (srcb + 4));
  __m128i dest1, dest2, temp1, temp2;

  dest1 = _mm_sub_epi32(srca1, srcb1);
  dest2 = _mm_sub_epi32(srca2, srcb2);
  temp1 = _mm_srai_epi32(dest1, 2);
  temp2 = _mm_srai_epi32(dest2, 2);
  dest1 = _mm_sub_epi32(dest1, temp1);
  dest2 = _mm_sub_epi32(dest2, temp2);

  _mm_store_si128((__m128i*) (dest + 0), dest1);
  _mm_store_si128((__m128i*) (dest + 4), dest2);
#else
  dest[0] = srca[0] - srcb[0];
  dest[0] -= (dsdiff >> 2);
  dest[1] = srca[1] - srcb[1];
  dest[1] -= (dsdiff >> 2);
  dest[2] = srca[2] - srcb[2];
  dest[2] -= (dsdiff >> 2);
  dest[3] = srca[3] - srcb[3];
  dest[3] -= (dsdiff >> 2);
  dest[4] = srca[4] - srcb[4];
  dest[4] -= (dsdiff >> 2);
  dest[5] = srca[5] - srcb[5];
  dest[5] -= (dsdiff >> 2);
  dest[6] = srca[6] - srcb[6];
  dest[6] -= (dsdiff >> 2);
  dest[7] = srca[7] - srcb[7];
  dest[7] -= (dsdiff >> 2);
#endif
}

/* ============================================================================
 *  FlipSigns: Conditionally flips the sign of 8 int32_ts according to flip.
 * ========================================================================= */
void
FlipSigns(int32_t *dest, const int32_t *src, unsigned flip) {
  assert((flip & 1) == flip);

  static int32_t FlipVector[2][4] align(16) = {
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

