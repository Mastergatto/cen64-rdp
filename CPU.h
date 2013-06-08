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

#define EXTENT_AUX_COUNT (480 * 192)
#define RDP_CVG_SPAN_MAX 1024

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

struct RDPExtent {
  uint16_t startx;
  uint16_t stopx;

  struct {
    uint32_t start;
    uint32_t dpdx;
  } param[8];

  void *userdata;  
};

struct RDPMiscState {
  uint32_t fbAddress;
  uint32_t fbWidth;
  uint8_t fbFormat;
  uint8_t fbSize;
  uint8_t maxLevel;
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

struct RDPRectangle {
  uint16_t xl;
  uint16_t yl;
  uint16_t xh;
  uint16_t yh;
};

struct RDPSpanBase {
  uint32_t drdy;
  uint32_t dgdy;
  uint32_t dbdy;
  uint32_t dady;
  uint32_t dzdy;
  uint32_t dr;
  uint32_t dg;
  uint32_t db;
  uint32_t da;
  uint32_t ds;
  uint32_t dt;
  uint32_t dw;
  uint32_t dz;
  uint32_t dyMax;
  uint32_t dzPix;
};

struct RDPTile {
  int format;
  int size;
  int line;
  int tmem;
  int palette;
  int ct, mt, cs, ms;
  int maskT, shiftT;
  int maskS, shiftS;
  uint16_t sl, tl, sh, th;
  int num;
};

struct RDPPolyState {
  struct RDP *rdp;

  struct RDPMiscState miscState;
  struct RDPOtherModes otherModes;
  struct RDPSpanBase spanBase;
  struct RDPRectangle scissor;
  uint32_t fillColor;
  struct RDPTile tiles[8];
  uint8_t tmem[0x1000];
  int tileNum;
  bool flip;
  bool rect;
};

struct RDPColor {
  union {
    uint32_t c;

    struct {
#ifdef LITTLE_ENDIAN
      uint8_t a, b, g, r;
#else
      uint8_t r, g, b, a;
#endif
    } i;
  } data;
};

struct RDPColorInputs {
  uint8_t *combinerRgbSubAr[2];
  uint8_t *combinerRgbSubAg[2];
  uint8_t *combinerRgbSubAb[2];
  uint8_t *combinerRgbSubBr[2];
  uint8_t *combinerRgbSubBg[2];
  uint8_t *combinerRgbSubBb[2];

  uint8_t *combinerRgbMulR[2];
  uint8_t *combinerRgbMulG[2];
  uint8_t *combinerRgbMulB[2];
  uint8_t *combinerRgbAddR[2];
  uint8_t *combinerRgbAddG[2];
  uint8_t *combinerRgbAddB[2];

  uint8_t *combinerAlphaSubA[2];
  uint8_t *combinerAlphaSubB[2];
  uint8_t *combinerAlphaMul[2];
  uint8_t *combinerAlphaAdd[2];

  uint8_t *blender1Ar[2];
  uint8_t *blender1Ag[2];
  uint8_t *blender1Ab[2];
  uint8_t *blender1Ba[2];
  uint8_t *blender2Ar[2];
  uint8_t *blender2Ag[2];
  uint8_t *blender2Ab[2];
  uint8_t *blender2Ba[2];
};

/* As MAME states in its source; */
/* "This is enormous and horrible." */
struct RDPSpanAux {
  uint32_t unscissoredRx;
  uint16_t cvg[RDP_CVG_SPAN_MAX];
  struct RDPColor memoryColor;
  struct RDPColor pixelColor;
  struct RDPColor invPixelColor;
  struct RDPColor blendedPixelColor;
  struct RDPColor combinedColor;
  struct RDPColor texel0Color;
  struct RDPColor texel1Color;
  struct RDPColor nextTexelColor;
  struct RDPColor blendColor;
  struct RDPColor primColor;
  struct RDPColor envColor;
  struct RDPColor fogColor;
  struct RDPColor shadeColor;
  struct RDPColor keyScale;
  struct RDPColor noiseColor;
  uint8_t lodFraction;
  uint8_t primLodFraction;
  struct RDPColorInputs colorInputs;
  uint32_t currentPixCvg;
  uint32_t currentMemCvg;
  uint32_t currentCvgBit;
  uint32_t currentPixOverlap;
  int32_t shiftA;
  int32_t shiftB;
  int32_t mPrecompS;
  int32_t mPrecompT;
  uint8_t blendEnable;
  uint8_t preWrap;
  int32_t mDzPixEnc;
  uint8_t *tmem;
  uint8_t startSpan;
};

enum RDPCycleType {
  CYCLE_TYPE_1,
  CYCLE_TYPE_2,
  CYCLE_TYPE_COPY,
  CYCLE_TYPE_FILL
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
  uint32_t tempRectData[0x1000];
  unsigned cmdPtr, cmdCur;

  uint8_t auxBuf[EXTENT_AUX_COUNT * sizeof(struct RDPSpanAux)];
  uint32_t auxBufPtr;
  uint32_t auxBufIndex;

  struct RDPSpanBase spanBase;
  struct RDPScissor scissor;
  struct RDPMiscState miscState;
  struct RDPOtherModes otherModes;
  struct RDPCombine combine;

  struct RDPColor blendColor;
  struct RDPColor primColor;
  struct RDPColor envColor;
  struct RDPColor fogColor;
  struct RDPColor keyScale;
  struct RDPColor oneColor;
  struct RDPColor zeroColor;
  uint32_t fillColor;
  uint8_t lodFraction;
  uint8_t primLodFraction;
};

struct RDP *CreateRDP(void);
void DestroyRDP(struct RDP *rdp);

#endif

