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

struct RDPCombine {
  uint8_t subArgb0;
  uint8_t mulRgb0;
  uint8_t subAa0;
  uint8_t mulA0;
  uint8_t subArgb1;
  uint8_t mulRgb1;
  uint8_t subBrgb0;
  uint8_t subBrgb1;
  uint8_t subAa1;
  uint8_t mulA1;
  uint8_t addRgb0;
  uint8_t subBa0;
  uint8_t addA0;
  uint8_t addRgb1;
  uint8_t subBa1;
  uint8_t addA1;
};

struct RDPOtherModes {
  uint8_t cycleType;
  uint8_t perspTexEn;
  uint8_t detailTexEn;
  uint8_t sharpenTexEn;
  uint8_t texLodEn;
  uint8_t enTlut;
  uint8_t tlutType;
  uint8_t sampleType;
  uint8_t midTexel;
  uint8_t biLerp0;
  uint8_t biLerp1;
  uint8_t convertOne;
  uint8_t keyEn;
  uint8_t rgbDitherSel;
  uint8_t alphaDitherSel;
  uint8_t blendm1a0;
  uint8_t blendm1a1;
  uint8_t blendm1b0;
  uint8_t blendm1b1;
  uint8_t blendm2a0;
  uint8_t blendm2a1;
  uint8_t blendm2b0;
  uint8_t blendm2b1;
  uint8_t forceBlend;
  uint8_t alphaCvgSelect;
  uint8_t cvgTimesAlpha;
  uint8_t zMode;
  uint8_t cvgDest;
  uint8_t colorOnCvg;
  uint8_t imageReadEn;
  uint8_t zUpdateEn;
  uint8_t zCompareEn;
  uint8_t antialiasEn;
  uint8_t zSourceSel;
  uint8_t ditherAlphaEn;
  uint8_t alphaCompareEn;
};

struct RDPScissor {
  uint32_t xh;
  uint32_t yh;
  uint32_t xl;
  uint32_t yl;
};

struct RDP {
  struct BusController *bus;
  uint32_t regs[NUM_DP_REGISTERS];

  uint32_t cmdBuffer[0x1000];
  unsigned cmdPtr, cmdCur;

  struct RDPScissor scissor;
  struct RDPOtherModes otherModes;
  struct RDPCombine combine;
};

struct RDP *CreateRDP(void);
void DestroyRDP(struct RDP *rdp);

#endif

