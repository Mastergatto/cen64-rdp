/* ============================================================================
 *  Registers.h: Reality Display Processor (RDP) Registers.
 *
 *  RDPSIM: Reality Signal Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'LICENSE', which is part of this source code package.
 * ========================================================================= */
#ifndef __RDP__REGISTERS_H__
#define __RDP__REGISTERS_H__
#include "Common.h"

enum DPRegister {
#define X(reg) reg,
#include "Registers.md"
#undef X
  NUM_DP_REGISTERS
};

#ifndef NDEBUG
extern const char *DPRegisterMnemonics[NUM_DP_REGISTERS];
#endif

#endif

