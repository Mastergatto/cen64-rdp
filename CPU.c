/* ============================================================================
 *  CPU.c: Reality Display Processor (RDP).
 *
 *  RDPSIM: Reality Signal Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'LICENSE', which is part of this source code package.
 * ========================================================================= */
#include "CPU.h"
#include "Externs.h"

#ifdef __cplusplus
#include <cstdlib>
#include <cstring>
#else
#include <stdlib.h>
#include <string.h>
#endif

static void InitRDP(struct RDP *rdp);

/* ============================================================================
 *  ConnectRDPToBus: Connects a RDP instance to a Bus instance.
 * ========================================================================= */
void
ConnectRDPToBus(struct RDP *rdp, struct BusController *bus) {
  rdp->bus = bus;
}

/* ============================================================================
 *  CreateRDP: Creates and initializes an RDP instance.
 * ========================================================================= */
struct RDP *
CreateRDP(void) {
  struct RDP *rdp;

  if ((rdp = (struct RDP*) malloc(sizeof(struct RDP))) == NULL) {
    debug("Failed to allocate memory.");
    return NULL;
  }

  InitRDP(rdp);
  return rdp;
}

/* ============================================================================
 *  DestroyRDP: Releases any resources allocated for a RDP instance.
 * ========================================================================= */
void
DestroyRDP(struct RDP *rdp) {
  free(rdp);
}

/* ============================================================================
 *  InitRDP: Initializes the RDP.
 * ========================================================================= */
static void
InitRDP(struct RDP *rdp) {
  debug("Initializing CPU.");
  memset(rdp, 0, sizeof(*rdp));

  rdp->regs[DPC_STATUS_REG] |= 0x008; /* GCLK is alive. */
  rdp->regs[DPC_STATUS_REG] |= 0x020; /* RDP PIPELINE is busy. */
  rdp->regs[DPC_STATUS_REG] |= 0x080; /* RDP COMMAND buffer is ready. */
}

