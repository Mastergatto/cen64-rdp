/* ============================================================================
 *  Definitions.h: Reality Display Processor (RDP) Defines.
 *
 *  RSPSIM: Reality Signal Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'LICENSE', which is part of this source code package.
 * ========================================================================= */
#ifndef __RDP__DEFINITIONS_H__
#define __RDP__DEFINITIONS_H__
#include "Common.h"

/* MI_INTR_REG bits. */
#define MI_INTR_DP                0x20

/* DPC_STATUS_REG read bits. */
#define DP_XBUS_DMEM_DMA          0x00000001
#define DP_FREEZE                 0x00000002
#define DP_FLUSH                  0x00000004

/* DPC_STATUS_REG write bits. */
#define DP_CLEAR_XBUS_DMEM_DMA    0x00000001
#define DP_SET_XBUS_DMEM_DMA      0x00000002
#define DP_CLEAR_FREEZE           0x00000004
#define DP_SET_FREEZE             0x00000008
#define DP_CLEAR_FLUSH            0x00000010
#define DP_SET_FLUSH              0x00000020

#endif

