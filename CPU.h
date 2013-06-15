/* ============================================================================
 *  CPU.h: Reality Display Processor (RDP).
 *
 *  RDPSIM: Reality Signal Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'LICENSE', which is part of this source code package.
 * ========================================================================= */
#ifndef __RDP__CPU_H__
#define __RDP__CPU_H__
#include "Externs.h"
#include "Registers.h"

struct RDP {
  struct BusController *bus;
  uint32_t regs[NUM_DP_REGISTERS];
};

struct RDP *CreateRDP(void);
void DestroyRDP(struct RDP *rdp);

#endif

