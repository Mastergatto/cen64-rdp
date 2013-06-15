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
  rdp->regs[reg] = *data;

  if (reg == DPC_START_REG)
    rdp->regs[DPC_CURRENT_REG] = *data;

  else if (reg == DPC_END_REG)
    RDPProcessList(rdp);

  return 0;
}

