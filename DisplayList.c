/* ============================================================================
 *  DisplayList.c: Reality Display Processor (RDP) Display List Handler.
 *
 *  RDPSIM: Reality Signal Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'LICENSE', which is part of this source code package.
 *
 *  Most of this file is a direct rip of MAME/MESS's N64 software renderer.
 *  When the project stablizes more, this project will eventually be replaced
 *  with cycle-accurate components. Until then, hats off to the MAME/MESS
 *  team. Also note that those portions of the code are covered under the MAME
 *  license, NOT the BSD license.
 * ========================================================================= */
#include "CPU.h"
#include "Definitions.h"
#include "DisplayList.h"
#include "Registers.h"

#ifdef __cplusplus
#include <cassert>
#include <cstring>
#else
#include <assert.h>
#include <string.h>
#endif

/* ============================================================================
 *  List of display commands, and their lengths.
 * ========================================================================= */
static const unsigned CommandLengthLUT[64] = {
  8,          /* 0x00, No Op */
  8,          /* 0x01, ??? */
  8,          /* 0x02, ??? */
  8,          /* 0x03, ??? */
  8,          /* 0x04, ??? */
  8,          /* 0x05, ??? */
  8,          /* 0x06, ??? */
  8,          /* 0x07, ??? */
  32,         /* 0x08, Non-Shaded Triangle */
  32+16,      /* 0x09, Non-Shaded, Z-Buffered Triangle */
  32+64,      /* 0x0a, Textured Triangle */
  32+64+16,   /* 0x0b, Textured, Z-Buffered Triangle */
  32+64,      /* 0x0c, Shaded Triangle */
  32+64+16,   /* 0x0d, Shaded, Z-Buffered Triangle */
  32+64+64,   /* 0x0e, Shaded+Textured Triangle */
  32+64+64+16,/* 0x0f, Shaded+Textured, Z-Buffered Triangle */
  8,          /* 0x10, ??? */
  8,          /* 0x11, ??? */
  8,          /* 0x12, ??? */
  8,          /* 0x13, ??? */
  8,          /* 0x14, ??? */
  8,          /* 0x15, ??? */
  8,          /* 0x16, ??? */
  8,          /* 0x17, ??? */
  8,          /* 0x18, ??? */
  8,          /* 0x19, ??? */
  8,          /* 0x1a, ??? */
  8,          /* 0x1b, ??? */
  8,          /* 0x1c, ??? */
  8,          /* 0x1d, ??? */
  8,          /* 0x1e, ??? */
  8,          /* 0x1f, ??? */
  8,          /* 0x20, ??? */
  8,          /* 0x21, ??? */
  8,          /* 0x22, ??? */
  8,          /* 0x23, ??? */
  16,         /* 0x24, Texture_Rectangle */
  16,         /* 0x25, Texture_Rectangle_Flip */
  8,          /* 0x26, Sync_Load */
  8,          /* 0x27, Sync_Pipe */
  8,          /* 0x28, Sync_Tile */
  8,          /* 0x29, Sync_Full */
  8,          /* 0x2a, Set_Key_GB */
  8,          /* 0x2b, Set_Key_R */
  8,          /* 0x2c, Set_Convert */
  8,          /* 0x2d, Set_Scissor */
  8,          /* 0x2e, Set_Prim_Depth */
  8,          /* 0x2f, Set_Other_Modes */
  8,          /* 0x30, Load_TLUT */
  8,          /* 0x31, ??? */
  8,          /* 0x32, Set_Tile_Size */
  8,          /* 0x33, Load_Block */
  8,          /* 0x34, Load_Tile */
  8,          /* 0x35, Set_Tile */
  8,          /* 0x36, Fill_Rectangle */
  8,          /* 0x37, Set_Fill_Color */
  8,          /* 0x38, Set_Fog_Color */
  8,          /* 0x39, Set_Blend_Color */
  8,          /* 0x3a, Set_Prim_Color */
  8,          /* 0x3b, Set_Env_Color */
  8,          /* 0x3c, Set_Combine */
  8,          /* 0x3d, Set_Texture_Image */
  8,          /* 0x3e, Set_Mask_Image */
  8           /* 0x3f, Set_Color_Image */
};

/* ============================================================================
 *  Command: FullSync.
 * ========================================================================= */
static void RDPFullSync(struct RDP *rdp,
  uint32_t unused(arg1), uint32_t unused(arg2)) {
  BusRaiseRCPInterrupt(rdp->bus, MI_INTR_DP);
}

/* ============================================================================
 *  Command: SetCombine.
 * ========================================================================= */
static void RDPSetCombine(struct RDP *rdp, uint32_t arg1, uint32_t arg2) {
  struct RDPCombine *combine = &rdp->combine;

  combine->subArgb0 = arg1 >> 20 & 0xF;
  combine->mulRgb0 = arg1 >> 15 & 0x1F;
  combine->subAa0 = arg1 >> 12 & 0x7;
  combine->mulA0 = arg1 >> 9 & 0x7;
  combine->subArgb1 = arg1 >> 5 & 0xF;
  combine->mulRgb1 = arg1 >> 0 & 0x1F;
  combine->subBrgb0 = arg2 >> 28 & 0xF;
  combine->subBrgb1 = arg2 >> 24 & 0xF;
  combine->subAa1 = arg2 >> 21 & 0x7;
  combine->mulA1 = arg2 >> 18 & 0x7;
  combine->addRgb0 = arg2 >> 15 & 0x7;
  combine->subBa0 = arg2 >> 12 & 0x7;
  combine->addA0 = arg2 >> 9 & 0x7;
  combine->addRgb1 = arg2 >> 6 & 0x7;
  combine->subBa1 = arg2 >> 3 & 0x7;
  combine->addA1 = arg2 >> 0 & 0x7;
}

/* ============================================================================
 *  Command: SetScissor.
 * ========================================================================= */
static void RDPSetScissor(struct RDP *rdp, uint32_t arg1, uint32_t arg2) {
  struct RDPScissor *scissor = &rdp->scissor;

  scissor->xh = ((arg1 >> 12) & 0xFFF) >> 2;
  scissor->yh = ((arg1 >>  0) & 0xFFF) >> 2;
  scissor->xl = ((arg2 >> 12) & 0xFFF) >> 2;
  scissor->xl = ((arg2 >>  0) & 0xFFF) >> 2;
}

/* ============================================================================
 *  Command: SetOtherModes.
 * ========================================================================= */
static void RDPSetOtherModes(struct RDP *rdp, uint32_t arg1, uint32_t arg2) {
  struct RDPOtherModes *otherModes = &rdp->otherModes;

  otherModes->cycleType = arg1 >> 20 & 0x3;
  otherModes->perspTexEn = arg1 >> 19 & 0x1;
  otherModes->detailTexEn = arg1 >> 18 & 0x1;
  otherModes->sharpenTexEn = arg1 >> 17 & 0x1;
  otherModes->texLodEn = arg1 >> 16 & 0x1;
  otherModes->enTlut = arg1 >> 15 & 0x1;
  otherModes->tlutType = arg1 >> 14 & 0x1;
  otherModes->sampleType = arg1 >> 13 & 0x1;
  otherModes->midTexel = arg1 >> 12 & 0x1;
  otherModes->biLerp0 = arg1 >> 11 & 0x1;
  otherModes->biLerp1 = arg1 >> 10 & 0x1;
  otherModes->convertOne = arg1 >> 9 & 0x1;
  otherModes->keyEn = arg1 >> 8 & 0x1;
  otherModes->rgbDitherSel = arg1 >> 6 & 0x3;
  otherModes->alphaDitherSel = arg1 >> 4 & 0x3;
  otherModes->blendm1a0 = arg2 >> 30 & 0x3;
  otherModes->blendm1a1 = arg2 >> 28 & 0x3;
  otherModes->blendm1b0 = arg2 >> 26 & 0x3;
  otherModes->blendm1b1 = arg2 >> 24 & 0x3;
  otherModes->blendm2a0 = arg2 >> 22 & 0x3;
  otherModes->blendm2a1 = arg2 >> 20 & 0x3;
  otherModes->blendm2b0 = arg2 >> 18 & 0x3;
  otherModes->blendm2b1 = arg2 >> 16 & 0x3;
  otherModes->forceBlend = arg2 >> 14 & 0x1;
  otherModes->alphaCvgSelect = arg2 >> 13 & 0x1;
  otherModes->cvgTimesAlpha = arg2 >> 12 & 0x1;
  otherModes->zMode = arg2 >> 10 & 0x3;
  otherModes->cvgDest = arg2 >> 8 & 0x3;
  otherModes->colorOnCvg = arg2 >> 7 & 0x1;
  otherModes->imageReadEn = arg2 >> 6 & 0x1;
  otherModes->zUpdateEn = arg2 >> 5 & 0x1;
  otherModes->zCompareEn = arg2 >> 4 & 0x1;
  otherModes->antialiasEn = arg2 >> 3 & 0x1;
  otherModes->zSourceSel = arg2 >> 2 & 0x1;
  otherModes->ditherAlphaEn = arg2 >> 1 & 0x1;
  otherModes->alphaCompareEn = arg2 >> 0 & 0x1;
}

/* ============================================================================
 *  RDPProcessList: Processes a DisplayList.
 * ========================================================================= */
void RDPProcessList(struct RDP *rdp) {
  int start = rdp->regs[DPC_CURRENT_REG];
  int end = rdp->regs[DPC_END_REG];
  int length = end - start;
  int i;

  unsigned cmd, cmdLength;

  debugarg("Processing display list at: [0x%.8X].",
    rdp->regs[DPC_CURRENT_REG]);

  /* Look into this ? */
  if (unlikely(length < 0)) {
    rdp->regs[DPC_CURRENT_REG] = end;
    return;
  }

  /* For now, just cheat and draw in one pass. */
  debug("[HACK]: Reading display list from RDRAM.");

  for (i = 0; i < length; i += 4) {
    uint32_t word = BusReadWord(rdp->bus, rdp->regs[DPC_CURRENT_REG] + i);
    memcpy(rdp->cmdBuffer + rdp->cmdPtr, &word, sizeof(word));
    rdp->cmdPtr++;
  }

  rdp->regs[DPC_CURRENT_REG] = end;
  cmd = (rdp->cmdBuffer[0] >> 24) & 0x3F;
  cmdLength = (rdp->cmdPtr + 1) * 4;

  /* Do we need more data? */
  if (cmdLength < CommandLengthLUT[cmd])
    return;

  /* Process as many commands as possible. */
  while (rdp->cmdCur < rdp->cmdPtr) {
    uint32_t arg1, arg2;

    cmd = (rdp->cmdBuffer[rdp->cmdCur] >> 24) & 0x3F;

    /* Do we need more data? */
    if (((rdp->cmdPtr - rdp->cmdCur) * 4) < CommandLengthLUT[cmd])
      return;

    arg1 = rdp->cmdBuffer[rdp->cmdCur];
    arg2 = rdp->cmdBuffer[rdp->cmdCur+1];

    /* ========================= */
    /*  Execute command here...  */
    /* ========================= */
    switch(cmd) {
    case 0x29: RDPFullSync(rdp, arg1, arg2); break;
    case 0x2D: RDPSetScissor(rdp, arg1, arg2); break;
    case 0x2F: RDPSetOtherModes(rdp, arg1, arg2); break;
    case 0x3C: RDPSetCombine(rdp, arg1, arg2); break;

    default:
      debugarg("Unimplemented command: 0x%.2X.", cmd);
      assert(0);
      break;
    }

    rdp->cmdCur += CommandLengthLUT[cmd] / 4;
  }

  rdp->cmdPtr = 0;
  rdp->cmdCur = 0;

  rdp->regs[DPC_CURRENT_REG] = end;
  rdp->regs[DPC_START_REG] = end;
}

