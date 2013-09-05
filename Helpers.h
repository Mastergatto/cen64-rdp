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

extern int32_t FlipLUT[2];

void FlipSigns(int32_t *dest, const int32_t *src, unsigned flip);

#endif

