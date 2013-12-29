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
#include "Helpers.h"

#ifdef __cplusplus
#include <cassert>
#include <cstring>
#else
#include <assert.h>
#include <string.h>
#endif

/* ============================================================================
 *  _mm_mullo_epi32: SSE2 lacks _mm_mullo_epi32, define it manually.
 *  TODO/WARNING/DISCLAIMER: Assumes one argument is positive.
 * ========================================================================= */
#ifdef SSSE3_ONLY
static __m128i
_mm_mullo_epi32(__m128i a, __m128i b) {
  __m128i a4 = _mm_srli_si128(a, 4);
  __m128i b4 = _mm_srli_si128(b, 4);
  __m128i ba = _mm_mul_epu32(b, a);
  __m128i b4a4 = _mm_mul_epu32(b4, a4);

  __m128i mask = _mm_setr_epi32(~0, 0, ~0, 0);
  __m128i baMask = _mm_and_si128(ba, mask);
  __m128i b4a4Mask = _mm_and_si128(b4a4, mask);
  __m128i b4a4MaskShift = _mm_slli_si128(b4a4Mask, 4);

  return _mm_or_si128(baMask, b4a4MaskShift);
}
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
  dest[0] -= (dest[0] >> 2);
  dest[1] = srca[1] - srcb[1];
  dest[1] -= (dest[1] >> 2);
  dest[2] = srca[2] - srcb[2];
  dest[2] -= (dest[2] >> 2);
  dest[3] = srca[3] - srcb[3];
  dest[3] -= (dest[3] >> 2);
  dest[4] = srca[4] - srcb[4];
  dest[4] -= (dest[4] >> 2);
  dest[5] = srca[5] - srcb[5];
  dest[5] -= (dest[5] >> 2);
  dest[6] = srca[6] - srcb[6];
  dest[6] -= (dest[6] >> 2);
  dest[7] = srca[7] - srcb[7];
  dest[7] -= (dest[7] >> 2);
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
  else
    memcpy(dest, src, sizeof(src) * 8);
#endif
}

/* ============================================================================
 *  LoadEWPrimData: Loads data for edgewalker_for_prims().
 * ========================================================================= */
void
LoadEWPrimData(int32_t *dest1, int32_t *dest2, const int32_t *src) {
#ifdef USE_SSE
  __m128i ewShuffleKey;
  __m128i ewData1, ewData2;
  __m128i ewDataLo, ewDataHi;

  static const uint8_t ewShuffleData[16] align(16) = {
    0xA,0xB,0x2,0x3,
    0x8,0x9,0x0,0x1,
    0xE,0xF,0x6,0x7,
    0xC,0xD,0x4,0x5,
  };

  ewShuffleKey = _mm_load_si128((__m128i*) (ewShuffleData));

  /* Build the ewvars and ewdxvars arrays. */
  ewData1 = _mm_load_si128((__m128i*) (src + 0));
  ewData2 = _mm_load_si128((__m128i*) (src + 4));
  ewDataLo = _mm_unpacklo_epi64(ewData1, ewData2);
  ewDataHi = _mm_unpackhi_epi64(ewData1, ewData2);
  ewDataLo = _mm_shuffle_epi8(ewDataLo, ewShuffleKey);
  ewDataHi = _mm_shuffle_epi8(ewDataHi, ewShuffleKey);
  _mm_store_si128((__m128i*) (dest1 + 0), ewDataLo);
  _mm_store_si128((__m128i*) (dest2 + 0), ewDataHi);

  ewData1 = _mm_load_si128((__m128i*) (src + 16));
  ewData2 = _mm_load_si128((__m128i*) (src + 20));
  ewDataLo = _mm_unpacklo_epi64(ewData1, ewData2);
  ewDataHi = _mm_unpackhi_epi64(ewData1, ewData2);
  ewDataLo = _mm_shuffle_epi8(ewDataLo, ewShuffleKey);
  ewDataHi = _mm_shuffle_epi8(ewDataHi, ewShuffleKey);
  _mm_store_si128((__m128i*) (dest1 + 4), ewDataLo);
  _mm_store_si128((__m128i*) (dest2 + 4), ewDataHi);
#else
  dest1[0] = (src[0] & 0xffff0000) | ((src[4] >> 16) & 0x0000ffff);
  dest1[1] = ((src[0] << 16) & 0xffff0000) | (src[4] & 0x0000ffff);
  dest1[2] = (src[1] & 0xffff0000) | ((src[5] >> 16) & 0x0000ffff);
  dest1[3] = ((src[1] << 16) & 0xffff0000) | (src[5] & 0x0000ffff);
  dest1[4] = (src[16] & 0xffff0000) | ((src[20] >> 16) & 0x0000ffff);
  dest1[5] = ((src[16] << 16) & 0xffff0000)  | (src[20] & 0x0000ffff);
  dest1[6] = (src[17] & 0xffff0000) | ((src[21] >> 16) & 0x0000ffff);

  dest2[0] = (src[2] & 0xffff0000) | ((src[6] >> 16) & 0x0000ffff);
  dest2[1] = ((src[2] << 16) & 0xffff0000) | (src[6] & 0x0000ffff);
  dest2[2] = (src[3] & 0xffff0000) | ((src[7] >> 16) & 0x0000ffff);
  dest2[3] = ((src[3] << 16) & 0xffff0000) | (src[7] & 0x0000ffff);
  dest2[4] = (src[18] & 0xffff0000) | ((src[22] >> 16) & 0x0000ffff);
  dest2[5] = ((src[18] << 16) & 0xffff0000)  | (src[22] & 0x0000ffff);
  dest2[6] = (src[19] & 0xffff0000) | ((src[23] >> 16) & 0x0000ffff);
#endif
}

/* ============================================================================
 *  MulConstant: Multiplies a vector by a constant value.
 * ========================================================================= */
void
MulConstant(int32_t *dest, int32_t *src, int32_t constant) {
#ifdef USE_SSE
  __m128i constantVector;
  __m128i src1, src2;

  constantVector = _mm_set1_epi32(constant);
  src1 = _mm_load_si128((__m128i*) (src + 0));
  src2 = _mm_load_si128((__m128i*) (src + 4));
  src1 = _mm_mullo_epi32(src1, constantVector);
  src2 = _mm_mullo_epi32(src2, constantVector);
  _mm_store_si128((__m128i*) (dest + 0), src1);
  _mm_store_si128((__m128i*) (dest + 4), src2);
#else
  dest[0] = src[0] * constant;
  dest[1] = src[1] * constant;
  dest[2] = src[2] * constant;
  dest[3] = src[3] * constant;
  dest[4] = src[4] * constant;
  dest[5] = src[5] * constant;
  dest[6] = src[6] * constant;
  dest[7] = src[7] * constant;
#endif
}

