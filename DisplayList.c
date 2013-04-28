/* ============================================================================
 *  DisplayList.c: Reality Display Processor (RDP) Display List Handler.
 *
 *  RDPSIM: Reality Signal Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'LICENSE', which is part of this source code package.
 * ========================================================================= */
#include "CPU.h"
#include "Definitions.h"
#include "DisplayList.h"
#include "Registers.h"

/* ============================================================================
 *  RDPProcessList: Processes a DisplayList.
 * ========================================================================= */
void RDPProcessList(struct RDP *rdp) {
  unsigned length = rdp->regs[DPC_END_REG] - rdp->regs[DPC_START_REG];
  debugarg("Processing display list at: [0x%.8X].", rdp->regs[DPC_START_REG]);
  debugarg("Display list has length: [%u].", length);

  /* For now, just cheat and draw in one pass. */
  rdp->regs[DPC_CURRENT_REG] = rdp->regs[DPC_END_REG];
}

