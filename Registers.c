/* ============================================================================
 *  Registers.c: Reality Display Processor (RDP) Registers.
 *
 *  RDPSIM: Reality Signal Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'LICENSE', which is part of this source code package.
 * ========================================================================= */
#include "Registers.h"

/* ============================================================================
 *  Mnemonics table.
 * ========================================================================= */
#ifndef NDEBUG
const char *DPRegisterMnemonics[NUM_DP_REGISTERS] = {
#define X(reg) #reg,
#include "Registers.md"
#undef X
};
#endif

