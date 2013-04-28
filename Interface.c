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
#include "Common.h"
#include "Interface.h"

/* ============================================================================
 *  WriteDPRegister: Used to receive commands from the RSP.
 * ========================================================================= */
void WriteDPRegister(unsigned unused(reg), uint32_t unused(value)) {
  debug("RDP received a command from the RSP.");
}

