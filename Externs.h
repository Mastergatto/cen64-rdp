/* ============================================================================
 *  Externs.h: External definitions for the RDP plugin.
 *
 *  RDPSIM: Reality Display Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'LICENSE', which is part of this source code package.
 * ========================================================================= */
#ifndef __RDP__EXTERNS_H__
#define __RDP__EXTERNS_H__
#include "Common.h"

struct BusController;
struct RDP;

uint32_t BusReadWord(struct BusController *, uint32_t);
void BusWriteWord(const struct BusController *, uint32_t, uint32_t);

void BusClearRCPInterrupt(struct BusController *, unsigned);
void BusRaiseRCPInterrupt(struct BusController *, unsigned);
void ConnectRDPToBus(struct RDP *rdp, struct BusController *bus);

void DMAFromDRAM(struct BusController *, void *, uint32_t, uint32_t);
void DMAToDRAM(struct BusController *, uint32_t, const void *, size_t);

#endif

