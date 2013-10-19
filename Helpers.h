/* ============================================================================
 *  Helpers.h: Reality Display Processor (RDP) Interface.
 *
 *  RDPSIM: Reality Signal Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'LICENSE', which is part of this source code package.
 * ========================================================================= */
#ifndef __RDP__HELPERS_H__
#define __RDP__HELPERS_H__
#include "Common.h"

#ifdef USE_SSE
#ifdef SSSE3_ONLY
#include <tmmintrin.h>
#else
#include <smmintrin.h>
#endif
#endif

extern const int32_t FlipLUT[2];

void AddVectors(int32_t *dest, const int32_t *srca, const int32_t *srcb);
void ASR8ClearLow(int32_t *dest, const int32_t *src);
__m128i BroadcastInt(int32_t constant);
void ClearLow5(int32_t *dest, const int32_t *src);
void ClearLow9(int32_t *dest, const int32_t *src);
void DiffASR2(int32_t *dest, const int32_t *srca, const int32_t *srcb);
void FlipSigns(int32_t *dest, const int32_t *src, unsigned flip);
void LoadEWPrimData(int32_t *dest1, int32_t *dest2, const int32_t *src);
void MulConstant(int32_t *dest, int32_t *src, int32_t constant);

#endif

