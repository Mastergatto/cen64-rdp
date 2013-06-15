/* ============================================================================
 *  Interface.c: Reality Display Processor (RDP) Interface.
 *
 *  RDPSIM: Reality Signal Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'LICENSE', which is part of this source code package.
 * ========================================================================= */
#include "Address.h"
#include "Common.h"
#include "Core.h"
#include "CPU.h"
#include "Definitions.h"
#include "Interface.h"
#include "Registers.h"

/* ============================================================================
 *  DPRegRead: Read from DP registers.
 * ========================================================================= */
int
DPRegRead(void *_rdp, uint32_t address, void *_data) {
	struct RDP *rdp = (struct RDP*) _rdp;
	uint32_t *data = (uint32_t*) _data;

  address -= DP_REGS_BASE_ADDRESS;
  enum DPRegister reg = (enum DPRegister) (address / 4);

  debugarg("DPRegRead: Reading from register [%s].", DPRegisterMnemonics[reg]);
  *data = rdp->regs[reg];

  return 0;
}

/* ============================================================================
 *  DPRegWrite: Write to DP registers.
 * ========================================================================= */
int
DPRegWrite(void *_rdp, uint32_t address, void *_data) {
	struct RDP *rdp = (struct RDP*) _rdp;
	uint32_t *data = (uint32_t*) _data;

  address -= DP_REGS_BASE_ADDRESS;
  enum DPRegister reg = (enum DPRegister) (address / 4);

  debugarg("DPRegWrite: Writing to register [%s].", DPRegisterMnemonics[reg]);

  switch(reg) {
    case DPC_START_REG:
      rdp->regs[DPC_CURRENT_REG] = *data;
      rdp->regs[DPC_START_REG] = *data;
      break;

    case DPC_END_REG:
      rdp->regs[DPC_END_REG] = *data;
      RDPProcessList(rdp);
      break;

    case DPC_STATUS_REG:
      if (*data & DP_CLEAR_XBUS_DMEM_DMA)
        rdp->regs[DPC_STATUS_REG] &= ~DP_XBUS_DMEM_DMA;
      else if (*data & DP_SET_XBUS_DMEM_DMA)
        rdp->regs[DPC_STATUS_REG] |= DP_XBUS_DMEM_DMA;

      if (*data & DP_CLEAR_FREEZE)
        rdp->regs[DPC_STATUS_REG] &= ~DP_FREEZE;
      else if (*data & DP_SET_FREEZE)
        rdp->regs[DPC_STATUS_REG] |= DP_FREEZE;

      if (*data & DP_CLEAR_FLUSH)
        rdp->regs[DPC_STATUS_REG] &= ~DP_FLUSH;
      else if (*data & DP_SET_FLUSH)
        rdp->regs[DPC_STATUS_REG] |= DP_FLUSH;

      break;

    default:
      rdp->regs[reg] = *data;
  }

  return 0;
}

