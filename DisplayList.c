/* ============================================================================
 *  DisplayList.c: Reality Display Processor (RDP) Display List Handler.
 *
 *  RDPSIM: Reality Signal Processor SIMulator.
 *  Copyright (C) 2013, Tyler J. Stachecki.
 *  All rights reserved.
 *
 *  Most of this file is a direct rip of MAME/MESS's N64 software renderer.
 *  When the project stablizes more, this project will eventually be replaced
 *  with cycle-accurate components. Until then, hats off to the MAME/MESS
 *  team.
 *
 *  Also note that those portions of the code are covered under the MAME
 *  license, NOT the BSD license. You may find the MAME license at:
 *  http://www.mamedev.org/legal.html.
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

#define SPAN_R (0)
#define SPAN_G (1)
#define SPAN_B (2)
#define SPAN_A (3)
#define SPAN_S (4)
#define SPAN_T (5)
#define SPAN_W (6)
#define SPAN_Z (7)

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
 *  Clip.
 * ========================================================================= */
static int32_t
Clip(int32_t value, int32_t min, int32_t max) {
  if (value < min)
    return min;

  if (value > max)
    return max;

  return value;
}

/* ============================================================================
 *  LeftCvgHex.
 * ========================================================================= */
static uint32_t
LeftCvgHex(uint32_t x, uint32_t fMask) {
  uint32_t stickyBit = ((x >> 1) & 0x1FFF) > 0;
  uint32_t covered = ((x >> 14) & 3) + stickyBit;
  covered = (0xF0 >> covered) & 0xF;
  return covered & fMask;
}

/* ============================================================================
 *  RightCvgHex.
 * ========================================================================= */
static uint32_t
RightCvgHex(uint32_t x, uint32_t fMask) {
  uint32_t stickyBit = ((x >> 1) & 0x1FFF) > 0;
  uint32_t covered = ((x >> 14) & 3) + stickyBit;
  covered = (0xF >> covered);
  return covered & fMask;
}

/* ============================================================================
 *  ComputeCvgFlip.
 * ========================================================================= */
static void
ComputeCvgFlip(struct RDPExtent *spans, int32_t *majorx, int32_t *minorx,
  int32_t *majorxint, int32_t *minorxint, int32_t scanline,
  int32_t yh, int32_t yl, int32_t base) {

  bool writeable = !(scanline & ~0x3FF);
  int32_t scanlineSpx = scanline << 2;
  int32_t purgeStart = 0xFFF;
  int32_t purgeEnd = 0;

  struct RDPSpanAux *userdata;
  int length, i;

  if (!writeable)
    return;

  for (i = 0; i < 4; i++) {
    if (majorxint[i] < purgeStart)
      purgeStart = majorxint[i];
    if (minorxint[i] > purgeEnd)
      purgeEnd = minorxint[i];
  }

  purgeStart = Clip(purgeStart, 0, 1023);
  purgeEnd = Clip(purgeEnd, 0, 1023);

  if ((length = purgeEnd - purgeStart) < 0)
    return;

  userdata = (struct RDPSpanAux*) spans[scanline - base].userdata;
  memset(&userdata->cvg[purgeStart], 0, (length + 1) << 1);

  for (i = 0; i < 4; i++) {
    int32_t minorCur = minorx[i];
    int32_t majorCur = majorx[i];
    int32_t majorCurInt = majorxint[i];
    int32_t minorCurInt = minorxint[i];
    length = minorCurInt - majorCurInt;

    int32_t fMask = (i & 1) ? 5 : 0xA;
    int32_t maskShift = (i ^ 3) << 2;
    int32_t fMaskShifted = fMask << maskShift;
    int32_t fLeft = Clip(majorCurInt + 1, 0, 647);
    int32_t fRight = Clip(majorCurInt - 1, 0, 647);

    bool validY = ((scanlineSpx + i) >= yh &&
      (scanlineSpx + i) < yl);

    if (validY && length >= 0) {
      if (minorCurInt != majorCurInt) {
        if (!(minorCurInt & ~0x3FF)) {
          uint32_t result = RightCvgHex(minorCur, fMask) << maskShift;
          userdata->cvg[minorCurInt] |= result;
        }

        if (!(majorCurInt & ~0x3FF)) {
          uint32_t result = LeftCvgHex(majorCur, fMask) << maskShift;
          userdata->cvg[majorCurInt] |= result;
        }
      }

      else {
        if (!(majorCurInt & ~0x3FF)) {
          uint32_t rightCvg = RightCvgHex(minorCur, fMask);
          uint32_t leftCvg = LeftCvgHex(majorCur, fMask);
          int32_t sameCvg = leftCvg & rightCvg;

          userdata->cvg[majorCurInt] |= (sameCvg << maskShift);
        }
      }

      for (; fLeft <= fRight; fLeft++)
        userdata->cvg[fLeft] |= fMaskShifted;
    }
  }
}

/* ============================================================================
 *  NormalizeDzPix.
 * ========================================================================= */
static int32_t
NormalizeDzPix(int32_t sum) {
  int count;

  if (sum & 0xC000)
    return 0x8000;

  if (!(sum & 0xFFFF))
    return 1;

  for (count = 0x2000; count > 0; count >>= 1)
    if (sum & count)
      return count << 1;

  return 0;
}

/* ============================================================================
 *  SetBlenderInput.
 * ========================================================================= */
static void
SetBlenderInput(struct RDP *rdp, int cycle, int which,
  uint8_t **inputR, uint8_t **inputG, uint8_t **inputB,  uint8_t **inputA,
  int a, int b, struct RDPSpanAux *userdata) {
  switch (a & 0x3) {
    case 0:
      if (cycle == 0) {
        *inputR = &userdata->pixelColor.data.i.r;
        *inputG = &userdata->pixelColor.data.i.g;
        *inputB = &userdata->pixelColor.data.i.a;
      }

      else {
        *inputR = &userdata->blendedPixelColor.data.i.r;
        *inputG = &userdata->blendedPixelColor.data.i.g;
        *inputB = &userdata->blendedPixelColor.data.i.a;
      }

      break;

    case 1:
      *inputR = &userdata->memoryColor.data.i.r;
      *inputG = &userdata->memoryColor.data.i.g;
      *inputB = &userdata->memoryColor.data.i.b;
      break;

    case 2:
      *inputR = &userdata->blendColor.data.i.r;
      *inputG = &userdata->blendColor.data.i.g;
      *inputB = &userdata->blendColor.data.i.b;
    break;

    case 3:
      *inputR = &userdata->fogColor.data.i.r;
      *inputG = &userdata->fogColor.data.i.g;
      *inputB = &userdata->fogColor.data.i.b;
    break;
  }

  if (which == 0) {
    switch (b & 0x3) {
      case 0: *inputA = &userdata->pixelColor.data.i.a; break;
      case 1: *inputA = &userdata->fogColor.data.i.a; break;
      case 2: *inputA = &userdata->shadeColor.data.i.a; break;
      case 3: *inputA = &rdp->zeroColor.data.i.a; break;
    }
  }

  else {
    switch (b & 0x3) {
      case 0: *inputA = &userdata->invPixelColor.data.i.a; break;
      case 1: *inputA = &userdata->memoryColor.data.i.a; break;
      case 2: *inputA = &rdp->oneColor.data.i.a; break;
      case 3: *inputA = &rdp->zeroColor.data.i.a; break;
    }
  }
}

/* ============================================================================
 *  DrawTriangle.
 * ========================================================================= */
static void
DrawTriangle(struct RDP * rdp, bool shade,
  bool texture, bool zbuffer, bool rect) {
  struct RDPSpanBase *spanBase = &rdp->spanBase;
  uint32_t arg1, arg2, arg3, arg4;
  uint32_t arg5, arg6, arg7, arg8;
  struct RDPExtent spans[1024];

  uint32_t fifoIndex = rect ? 0 : rdp->cmdCur;
  uint32_t *cmdData = rect
    ? rdp->tempRectData
    : rdp->cmdBuffer;

  unsigned dsDiff, dsDeh;
  unsigned dsDxh, dsDyh;
  unsigned flip, tileNum;
  unsigned shadeBase;
  unsigned textureBase;
  unsigned zbufferBase;

  int r, g, b, a, s, t, w, z;
  int drdx, dgdx, dbdx, dadx;
  int drde, dgde, dbde, dade;
  int drdy, dgdy, dbdy, dady;
  int dsdx, dtdx, dwdx, dsde;
  int dtde, dwde, dsdy, dtdy;
  int dwdy, dzdx, dzde, dzdy;
  int dzdyDz, dzdxDz, tempDzPix;

  int yl, ym, yh, xl, xh, xm, dxldy, dxhdy, dxmdy;
  int maxxmx = 0, minxmx = 0, maxxhx = 0, minxhx = 0;
  int dsdiff = 0, dtdiff = 0, dwdiff = 0, drdiff = 0;
  int dgdiff = 0, dbdiff = 0, dadiff = 0, dzdiff = 0;
  int dsdeh = 0, dtdeh = 0, dwdeh = 0, drdeh = 0;
  int dgdeh = 0, dbdeh = 0, dadeh = 0, dzdeh = 0;
  int dsdxh = 0, dtdxh = 0, dwdxh = 0, drdxh = 0;
  int dgdxh = 0, dbdxh = 0, dadxh = 0, dzdxh = 0;
  int dsdyh = 0, dtdyh = 0, dwdyh = 0, drdyh = 0;
  int dgdyh = 0, dbdyh = 0, dadyh = 0, dzdyh = 0;

  arg1 = cmdData[fifoIndex];
  arg2 = cmdData[fifoIndex + 1];

  rdp->miscState.maxLevel = arg1 >> 19 & 0x7;
  flip = arg1 >> 23 & 0x1;
  tileNum = arg1 >> 16 & 0x7;
  shadeBase = fifoIndex + 8;
  textureBase = fifoIndex + 8;
  zbufferBase = fifoIndex + 8;

  if (shade) {
    textureBase += 16;
    zbufferBase += 16;
  }

  if (texture)
    zbufferBase += 16;

  arg3 = cmdData[fifoIndex + 2];
  arg4 = cmdData[fifoIndex + 3];
  arg5 = cmdData[fifoIndex + 4];
  arg6 = cmdData[fifoIndex + 5];
  arg7 = cmdData[fifoIndex + 6];
  arg8 = cmdData[fifoIndex + 7];

  yl = arg1 & 0x3FFF;
  ym = arg2 >> 16 & 0x3FFF;
  yh = arg2 >> 0 & 0x3FFF;
  xl = arg3 & 0x3FFFFFFF;
  xh = arg5 & 0x3FFFFFFF;
  xm = arg7 & 0x3FFFFFFF;
  dxldy = arg4;
  dxhdy = arg6;
  dxmdy = arg8;

  if (yl & 0x2000) yl |= 0xFFFFC000;
  if (ym & 0x2000) ym |= 0xFFFFC000;
  if (yh & 0x2000) yh |= 0xFFFFC000;
  if (xl & 0x20000000) xl |= 0xC0000000;
  if (xm & 0x20000000) xm |= 0xC0000000;
  if (xh & 0x20000000) xh |= 0xC0000000;

  r    = (rdp->cmdBuffer[shadeBase+0 ] & 0xffff0000) |
        ((rdp->cmdBuffer[shadeBase+4 ] >> 16) & 0x0000ffff);

  g    = ((rdp->cmdBuffer[shadeBase+0 ] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[shadeBase+4 ] & 0x0000ffff);

  b    = (rdp->cmdBuffer[shadeBase+1 ] & 0xffff0000) |
        ((rdp->cmdBuffer[shadeBase+5 ] >> 16) & 0x0000ffff);

  a    = ((rdp->cmdBuffer[shadeBase+1 ] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[shadeBase+5 ] & 0x0000ffff);

  drdx = (rdp->cmdBuffer[shadeBase+2 ] & 0xffff0000) |
        ((rdp->cmdBuffer[shadeBase+6 ] >> 16) & 0x0000ffff);

  dgdx = ((rdp->cmdBuffer[shadeBase+2 ] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[shadeBase+6 ] & 0x0000ffff);

  dbdx = (rdp->cmdBuffer[shadeBase+3 ] & 0xffff0000) |
        ((rdp->cmdBuffer[shadeBase+7 ] >> 16) & 0x0000ffff);

  dadx = ((rdp->cmdBuffer[shadeBase+3 ] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[shadeBase+7 ] & 0x0000ffff);

  drde = (rdp->cmdBuffer[shadeBase+8 ] & 0xffff0000) |
        ((rdp->cmdBuffer[shadeBase+12] >> 16) & 0x0000ffff);

  dgde = ((rdp->cmdBuffer[shadeBase+8 ] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[shadeBase+12] & 0x0000ffff);

  dbde = (rdp->cmdBuffer[shadeBase+9 ] & 0xffff0000) |
        ((rdp->cmdBuffer[shadeBase+13] >> 16) & 0x0000ffff);

  dade = ((rdp->cmdBuffer[shadeBase+9 ] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[shadeBase+13] & 0x0000ffff);

  drdy = (rdp->cmdBuffer[shadeBase+10] & 0xffff0000) |
        ((rdp->cmdBuffer[shadeBase+14] >> 16) & 0x0000ffff);

  dgdy = ((rdp->cmdBuffer[shadeBase+10] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[shadeBase+14] & 0x0000ffff);

  dbdy = (rdp->cmdBuffer[shadeBase+11] & 0xffff0000) |
        ((rdp->cmdBuffer[shadeBase+15] >> 16) & 0x0000ffff);

  dady = ((rdp->cmdBuffer[shadeBase+11] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[shadeBase+15] & 0x0000ffff);

  s    = (rdp->cmdBuffer[textureBase+0 ] & 0xffff0000) |
        ((rdp->cmdBuffer[textureBase+4 ] >> 16) & 0x0000ffff);

  t    = ((rdp->cmdBuffer[textureBase+0 ] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[textureBase+4 ] & 0x0000ffff);

  w    = (rdp->cmdBuffer[textureBase+1 ] & 0xffff0000) |
        ((rdp->cmdBuffer[textureBase+5 ] >> 16) & 0x0000ffff);

  dsdx = (rdp->cmdBuffer[textureBase+2 ] & 0xffff0000) |
        ((rdp->cmdBuffer[textureBase+6 ] >> 16) & 0x0000ffff);

  dtdx = ((rdp->cmdBuffer[textureBase+2 ] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[textureBase+6 ] & 0x0000ffff);

  dwdx = (rdp->cmdBuffer[textureBase+3 ] & 0xffff0000) |
        ((rdp->cmdBuffer[textureBase+7 ] >> 16) & 0x0000ffff);

  dsde = (rdp->cmdBuffer[textureBase+8 ] & 0xffff0000) |
        ((rdp->cmdBuffer[textureBase+12] >> 16) & 0x0000ffff);

  dtde = ((rdp->cmdBuffer[textureBase+8 ] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[textureBase+12] & 0x0000ffff);

  dwde = (rdp->cmdBuffer[textureBase+9 ] & 0xffff0000) |
        ((rdp->cmdBuffer[textureBase+13] >> 16) & 0x0000ffff);

  dsdy = (rdp->cmdBuffer[textureBase+10] & 0xffff0000) |
        ((rdp->cmdBuffer[textureBase+14] >> 16) & 0x0000ffff);

  dtdy = ((rdp->cmdBuffer[textureBase+10] << 16) & 0xffff0000) |
          (rdp->cmdBuffer[textureBase+14] & 0x0000ffff);

  dwdy = (rdp->cmdBuffer[textureBase+11] & 0xffff0000) |
        ((rdp->cmdBuffer[textureBase+15] >> 16) & 0x0000ffff);

  z    = rdp->cmdBuffer[zbufferBase+0];
  dzdx = rdp->cmdBuffer[zbufferBase+1];
  dzde = rdp->cmdBuffer[zbufferBase+2];
  dzdy = rdp->cmdBuffer[zbufferBase+3];

  dzdyDz = (dzdy >> 16) & 0xffff;
  dzdxDz = (dzdx >> 16) & 0xffff;

  spanBase->drdy = drdy;
  spanBase->dgdy = dgdy;
  spanBase->dbdy = dbdy;
  spanBase->dady = dady;
  spanBase->dzdy = rdp->otherModes.zSourceSel
    ? 0 : dzdy;

  spanBase->dr = drdx & ~0x1F;
  spanBase->dg = dgdx & ~0x1F;
  spanBase->db = dbdx & ~0x1F;
  spanBase->da = dadx & ~0x1F;
  spanBase->ds = dsdx;
  spanBase->dt = dtdx;
  spanBase->dw = dwdx;
  spanBase->dz = rdp->otherModes.zSourceSel
    ? 0 : dzdx;

  tempDzPix =
    ((dzdyDz & 0x8000)
      ? ((~dzdyDz) & 0x7FFF)
      : dzdyDz)

    + ((dzdxDz & 0x8000)
      ? ((~dzdxDz) & 0x7FFF)
      : dzdxDz);

  spanBase->dyMax = 0;
  spanBase->dzPix = NormalizeDzPix(tempDzPix & 0xFFFF) & 0xFFFF;

  int xleftInc = (dxmdy >> 2) & ~1;
  int xrightInc = (dxhdy >> 2) & ~1;
  int xleft = xm & ~1;
  int xright = xh & ~1;

  int signDxhdy = (dxhdy & 0x80000000) ? 1 : 0;
  int doOffset = !(signDxhdy ^ (flip));

  if (doOffset) {
    dsdeh = dsde >> 9;
    dsdyh = dsdy >> 9;
    dtdeh = dtde >> 9;
    dtdyh = dtdy >> 9;
    dwdeh = dwde >> 9;
    dwdyh = dwdy >> 9;
    drdeh = drde >> 9;
    drdyh = drdy >> 9;
    dgdeh = dgde >> 9;
    dgdyh = dgdy >> 9;
    dbdeh = dbde >> 9;
    dbdyh = dbdy >> 9;
    dadeh = dade >> 9;
    dadyh = dady >> 9;
    dzdeh = dzde >> 9;
    dzdyh = dzdy >> 9;

    dsdiff = (dsdeh << 8) + (dsdeh << 7) - (dsdyh << 8) - (dsdyh << 7);
    dtdiff = (dtdeh << 8) + (dtdeh << 7) - (dtdyh << 8) - (dtdyh << 7);
    dwdiff = (dwdeh << 8) + (dwdeh << 7) - (dwdyh << 8) - (dwdyh << 7);
    drdiff = (drdeh << 8) + (drdeh << 7) - (drdyh << 8) - (drdyh << 7);
    dgdiff = (dgdeh << 8) + (dgdeh << 7) - (dgdyh << 8) - (dgdyh << 7);
    dbdiff = (dbdeh << 8) + (dbdeh << 7) - (dbdyh << 8) - (dbdyh << 7);
    dadiff = (dadeh << 8) + (dadeh << 7) - (dadyh << 8) - (dadyh << 7);
    dzdiff = (dzdeh << 8) + (dzdeh << 7) - (dzdyh << 8) - (dzdyh << 7);
  }

  else {
    dsdiff = 0;
    dtdiff = 0;
    dwdiff = 0;
    drdiff = 0;
    dgdiff = 0;
    dbdiff = 0;
    dadiff = 0;
    dzdiff = 0;
  }

  dsdxh = dsdx >> 8;
  dtdxh = dtdx >> 8;
  dwdxh = dwdx >> 8;
  drdxh = drdx >> 8;
  dgdxh = dgdx >> 8;
  dbdxh = dbdx >> 8;
  dadxh = dadx >> 8;
  dzdxh = dzdx >> 8;

  int majorxint[4];
  int minorxint[4];
  int majorx[4];
  int minorx[4];

  int ycur = yh & ~3;
  int ylfar = yl | 3;
  int ldflag = (signDxhdy ^ flip) ? 0 : 3;
  int xfrac = ((xright >> 8) & 0xff);
  bool validY = true;

  int clipy1 = rdp->scissor.yh;
  int clipy2 = rdp->scissor.yl;

  /* Trivial reject. */
  if ((ycur >> 2) >= clipy2 && (ylfar >> 2) >= clipy2)
    return;

  if ((ycur >> 2) < clipy1 && (ylfar >> 2) < clipy1)
    return;

  struct RDPPolyState *object = NULL;
  bool newObject = true;

  if (flip) {
    int k;

    for (k = ycur; k <= ylfar; k++) {
      int xstart, xend, j, spix;

      if (k == ym) {
        xleft = xl & ~1;
        xleftInc = (dxldy >> 2) & ~1;
      }

      xstart = xleft >> 16;
      xend = xright >> 16;
      j = k >> 2;
      spix = k & 3;
      validY = !(k < yh || k >= yl);

      if (k >= 0 && k < 0x1000) {
        majorxint[spix] = xend;
        minorxint[spix] = xstart;
        majorx[spix] = xright;
        minorx[spix] = xleft;

        if (spix == 0) {
          minxhx = 0xFFF;
          maxxmx = 0;
        }

        if (validY) {
          maxxmx = (xstart > maxxmx) ? xstart : maxxmx;
          minxhx = (xend < minxhx) ? xend : minxhx;
        }

        if (spix == 0) {
          /* ... */
          newObject = false;
        }

        spans[j - (ycur >> 2)].userdata = rdp->auxBuf + rdp->auxBufPtr;
        rdp->auxBufPtr += sizeof(struct RDPSpanAux);

        if (rdp->auxBufPtr >= (EXTENT_AUX_COUNT * sizeof(struct RDPSpanAux)))
          assert(0 && "DrawTriangle: SpanAux Buffer Overflow.");

        struct RDPSpanAux *userdata = (struct RDPSpanAux*)
          spans[j - (ycur >> 2)].userdata;

        userdata->tmem = object->tmem;
        userdata->blendColor = rdp->blendColor;
        userdata->primColor = rdp->primColor;
        userdata->envColor = rdp->envColor;
        userdata->fogColor = rdp->fogColor;
        userdata->keyScale = rdp->keyScale;
        userdata->lodFraction = rdp->lodFraction;
        userdata->primLodFraction = rdp->primLodFraction;

        userdata->colorInputs.combinerRgbSubAr[0] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbSubAr[1] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbSubAg[0] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbSubAg[1] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbSubAb[0] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbSubAb[1] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbSubBr[0] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbSubBr[1] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbSubBg[0] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbSubBg[1] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbSubBb[0] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbSubBb[1] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbMulR[0] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbMulR[1] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbMulG[0] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbMulG[1] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbMulB[0] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbMulB[1] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbAddR[0] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbAddR[1] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbAddG[0] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbAddG[1] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbAddB[0] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbAddB[1] = &rdp->oneColor.data.i.b;

        userdata->colorInputs.blender1Ar[0] = &userdata->pixelColor.data.i.r;
        userdata->colorInputs.blender1Ar[1] = &userdata->pixelColor.data.i.r;
        userdata->colorInputs.blender1Ag[0] = &userdata->pixelColor.data.i.g;
        userdata->colorInputs.blender1Ag[1] = &userdata->pixelColor.data.i.g;
        userdata->colorInputs.blender1Ab[0] = &userdata->pixelColor.data.i.b;
        userdata->colorInputs.blender1Ab[1] = &userdata->pixelColor.data.i.b;
        userdata->colorInputs.blender1Ba[0] = &userdata->pixelColor.data.i.a;
        userdata->colorInputs.blender1Ba[1] = &userdata->pixelColor.data.i.a;
        userdata->colorInputs.blender2Ar[0] = &userdata->pixelColor.data.i.r;
        userdata->colorInputs.blender2Ar[1] = &userdata->pixelColor.data.i.r;
        userdata->colorInputs.blender2Ag[0] = &userdata->pixelColor.data.i.g;
        userdata->colorInputs.blender2Ab[1] = &userdata->pixelColor.data.i.b;
        userdata->colorInputs.blender2Ab[0] = &userdata->pixelColor.data.i.b;
        userdata->colorInputs.blender2Ba[0] = &userdata->pixelColor.data.i.a;
        userdata->colorInputs.blender2Ba[1] = &userdata->pixelColor.data.i.a;

        /* Setup blender data for this scanline. */
        SetBlenderInput(rdp, 0, 0,
          &userdata->colorInputs.blender1Ar[0],
          &userdata->colorInputs.blender1Ag[0],
          &userdata->colorInputs.blender1Ab[0],
          &userdata->colorInputs.blender1Ba[0],
          rdp->otherModes.blendm1a0,
          rdp->otherModes.blendm1b0,
          userdata);

        SetBlenderInput(rdp, 0, 1,
          &userdata->colorInputs.blender2Ar[0],
          &userdata->colorInputs.blender2Ag[0],
          &userdata->colorInputs.blender2Ab[0],
          &userdata->colorInputs.blender2Ba[0],
          rdp->otherModes.blendm2a0,
          rdp->otherModes.blendm2b0,
          userdata);

        SetBlenderInput(rdp, 1, 0,
          &userdata->colorInputs.blender1Ar[1],
          &userdata->colorInputs.blender1Ag[1],
          &userdata->colorInputs.blender1Ab[1],
          &userdata->colorInputs.blender1Ba[1],
          rdp->otherModes.blendm1a1,
          rdp->otherModes.blendm1b1,
          userdata);

        SetBlenderInput(rdp, 1, 1,
          &userdata->colorInputs.blender2Ar[1],
          &userdata->colorInputs.blender2Ag[1],
          &userdata->colorInputs.blender2Ab[1],
          &userdata->colorInputs.blender2Ba[1],
          rdp->otherModes.blendm2a1,
          rdp->otherModes.blendm2b1,
          userdata);

        /* Setup color combiner for this scanline. */

        if (spix == 3) {
          spans[j - (ycur >> 2)].startx = maxxmx;
          spans[j - (ycur >> 2)].stopx = minxhx;

          ComputeCvgFlip(spans,
            majorx, minorx,
            majorxint, minorxint,
            j, yh, yl, ycur >> 2);
        }

        if (spix == ldflag) {
          struct RDPSpanAux *span = spans[j - (ycur >> 2)].userdata;
          span->unscissoredRx = xend;

					xfrac = ((xright >> 8) & 0xff);

					spans[j - (ycur >> 2)].param[SPAN_R].start =
            ((r >> 9) << 9) + drdiff - (xfrac * drdxh);
					spans[j - (ycur >> 2)].param[SPAN_G].start =
            ((g >> 9) << 9) + dgdiff - (xfrac * dgdxh);
					spans[j - (ycur >> 2)].param[SPAN_B].start =
            ((b >> 9) << 9) + dbdiff - (xfrac * dbdxh);
					spans[j - (ycur >> 2)].param[SPAN_A].start =
            ((a >> 9) << 9) + dadiff - (xfrac * dadxh);
					spans[j - (ycur >> 2)].param[SPAN_S].start =
            (((s >> 9) << 9)  + dsdiff - (xfrac * dsdxh)) & ~0x1f;
					spans[j - (ycur >> 2)].param[SPAN_T].start =
            (((t >> 9) << 9)  + dtdiff - (xfrac * dtdxh)) & ~0x1f;
					spans[j - (ycur >> 2)].param[SPAN_W].start =
            (((w >> 9) << 9)  + dwdiff - (xfrac * dwdxh)) & ~0x1f;
					spans[j - (ycur >> 2)].param[SPAN_Z].start =
            ((z >> 9) << 9)  + dzdiff - (xfrac * dzdxh);
				}
			}

      if (spix == 3) {
        r += drde;
        g += dgde;
        b += dbde;
        a += dade;
        s += dsde;
        t += dtde;
        w += dwde;
        z += dzde;
      }

      xleft += xleftInc;
      xright += xrightInc;
    }
  }

  else {
    for (int k = ycur; k <= ylfar; k++) {
      int xstart, xend, j, spix;

      if (k == ym) {
        xleft = xl & ~1;
        xleftInc = (dxldy >> 2) & ~1;
      }

      xstart = xleft >> 16;
      xend = xright >> 16;
      j = k >> 2;
      spix = k & 3;
      validY = !(k < yh || k >= yl);

      if (k >= 0 && k < 0x1000) {
        majorxint[spix] = xend;
        minorxint[spix] = xstart;
        majorx[spix] = xright;
        minorx[spix] = xleft;

        if (spix == 0) {
          minxmx = 0xFFF;
          maxxhx = 0;
        }

        if (validY) {
          minxmx = (xstart < minxmx) ? xstart : minxmx;
          maxxhx = (xend > maxxhx) ? xend : maxxhx;
        }

        if (spix == 0) {
          /* ... */
          newObject = false;
        }

        spans[j - (ycur >> 2)].userdata = rdp->auxBuf + rdp->auxBufPtr;
        rdp->auxBufPtr += sizeof(struct RDPSpanAux);

        if (rdp->auxBufPtr >= (EXTENT_AUX_COUNT * sizeof(struct RDPSpanAux)))
          assert(0 && "DrawTriangle: SpanAux Buffer Overflow.");

        struct RDPSpanAux *userdata = (struct RDPSpanAux*)
          spans[j - (ycur >> 2)].userdata;

        userdata->tmem = object->tmem;
        userdata->blendColor = rdp->blendColor;
        userdata->primColor = rdp->primColor;
        userdata->envColor = rdp->envColor;
        userdata->fogColor = rdp->fogColor;
        userdata->keyScale = rdp->keyScale;
        userdata->lodFraction = rdp->lodFraction;
        userdata->primLodFraction = rdp->primLodFraction;

        userdata->colorInputs.combinerRgbSubAr[0] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbSubAr[1] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbSubAg[0] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbSubAg[1] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbSubAb[0] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbSubAb[1] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbSubBr[0] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbSubBr[1] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbSubBg[0] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbSubBg[1] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbSubBb[0] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbSubBb[1] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbMulR[0] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbMulR[1] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbMulG[0] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbMulG[1] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbMulB[0] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbMulB[1] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbAddR[0] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbAddR[1] = &rdp->oneColor.data.i.r;
        userdata->colorInputs.combinerRgbAddG[0] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbAddG[1] = &rdp->oneColor.data.i.g;
        userdata->colorInputs.combinerRgbAddB[0] = &rdp->oneColor.data.i.b;
        userdata->colorInputs.combinerRgbAddB[1] = &rdp->oneColor.data.i.b;

        userdata->colorInputs.blender1Ar[0] = &userdata->pixelColor.data.i.r;
        userdata->colorInputs.blender1Ar[1] = &userdata->pixelColor.data.i.r;
        userdata->colorInputs.blender1Ag[0] = &userdata->pixelColor.data.i.g;
        userdata->colorInputs.blender1Ag[1] = &userdata->pixelColor.data.i.g;
        userdata->colorInputs.blender1Ab[0] = &userdata->pixelColor.data.i.b;
        userdata->colorInputs.blender1Ab[1] = &userdata->pixelColor.data.i.b;
        userdata->colorInputs.blender1Ba[0] = &userdata->pixelColor.data.i.a;
        userdata->colorInputs.blender1Ba[1] = &userdata->pixelColor.data.i.a;
        userdata->colorInputs.blender2Ar[0] = &userdata->pixelColor.data.i.r;
        userdata->colorInputs.blender2Ar[1] = &userdata->pixelColor.data.i.r;
        userdata->colorInputs.blender2Ag[0] = &userdata->pixelColor.data.i.g;
        userdata->colorInputs.blender2Ab[1] = &userdata->pixelColor.data.i.b;
        userdata->colorInputs.blender2Ab[0] = &userdata->pixelColor.data.i.b;
        userdata->colorInputs.blender2Ba[0] = &userdata->pixelColor.data.i.a;
        userdata->colorInputs.blender2Ba[1] = &userdata->pixelColor.data.i.a;

        /* Setup blender data for this scanline. */
        SetBlenderInput(rdp, 0, 0,
          &userdata->colorInputs.blender1Ar[0],
          &userdata->colorInputs.blender1Ag[0],
          &userdata->colorInputs.blender1Ab[0],
          &userdata->colorInputs.blender1Ba[0],
          rdp->otherModes.blendm1a0,
          rdp->otherModes.blendm1b0,
          userdata);

        SetBlenderInput(rdp, 0, 1,
          &userdata->colorInputs.blender2Ar[0],
          &userdata->colorInputs.blender2Ag[0],
          &userdata->colorInputs.blender2Ab[0],
          &userdata->colorInputs.blender2Ba[0],
          rdp->otherModes.blendm2a0,
          rdp->otherModes.blendm2b0,
          userdata);

        SetBlenderInput(rdp, 1, 0,
          &userdata->colorInputs.blender1Ar[1],
          &userdata->colorInputs.blender1Ag[1],
          &userdata->colorInputs.blender1Ab[1],
          &userdata->colorInputs.blender1Ba[1],
          rdp->otherModes.blendm1a1,
          rdp->otherModes.blendm1b1,
          userdata);

        SetBlenderInput(rdp, 1, 1,
          &userdata->colorInputs.blender2Ar[1],
          &userdata->colorInputs.blender2Ag[1],
          &userdata->colorInputs.blender2Ab[1],
          &userdata->colorInputs.blender2Ba[1],
          rdp->otherModes.blendm2a1,
          rdp->otherModes.blendm2b1,
          userdata);

        /* Setup color combiner for this scanline. */

        if (spix == 3) {
          spans[j - (ycur >> 2)].startx = minxmx;
          spans[j - (ycur >> 2)].stopx = maxxhx;

          ComputeCvgFlip(spans,
            majorx, minorx,
            majorxint, minorxint,
            j, yh, yl, ycur >> 2);
        }

        if (spix == ldflag) {
          struct RDPSpanAux *span = spans[j - (ycur >> 2)].userdata;
          span->unscissoredRx = xend;

					xfrac = ((xright >> 8) & 0xff);

					spans[j - (ycur >> 2)].param[SPAN_R].start =
            ((r >> 9) << 9) + drdiff - (xfrac * drdxh);
					spans[j - (ycur >> 2)].param[SPAN_G].start =
            ((g >> 9) << 9) + dgdiff - (xfrac * dgdxh);
					spans[j - (ycur >> 2)].param[SPAN_B].start =
            ((b >> 9) << 9) + dbdiff - (xfrac * dbdxh);
					spans[j - (ycur >> 2)].param[SPAN_A].start =
            ((a >> 9) << 9) + dadiff - (xfrac * dadxh);
					spans[j - (ycur >> 2)].param[SPAN_S].start =
            (((s >> 9) << 9)  + dsdiff - (xfrac * dsdxh)) & ~0x1f;
					spans[j - (ycur >> 2)].param[SPAN_T].start =
            (((t >> 9) << 9)  + dtdiff - (xfrac * dtdxh)) & ~0x1f;
					spans[j - (ycur >> 2)].param[SPAN_W].start =
            (((w >> 9) << 9)  + dwdiff - (xfrac * dwdxh)) & ~0x1f;
					spans[j - (ycur >> 2)].param[SPAN_Z].start =
            ((z >> 9) << 9)  + dzdiff - (xfrac * dzdxh);
        }
      }

      if (spix == 3) {
        r += drde;
        g += dgde;
        b += dbde;
        a += dade;
        s += dsde;
        t += dtde;
        w += dwde;
        z += dzde;
      }

      xleft += xleftInc;
      xright += xrightInc;
    }
  }

  if (!newObject) {
    /* ... */
  }
}

/* ============================================================================
 *  Command: FillRect.
 * ========================================================================= */
static void
RDPFillRect(struct RDP *rdp, uint32_t arg1, uint32_t arg2) {
  uint32_t xl = arg1 >> 12 & 0xFFF;
  uint32_t yl = arg1 >>  0 & 0xFFF;
  uint32_t xh = arg2 >> 12 & 0xFFF;
  uint32_t yh = arg2 >>  0 & 0xFFF;

  uint32_t xlint = (xl >> 2) & 0x3FF;
  uint32_t xhint = (xh >> 2) & 0x3FF;
  uint32_t *ewData = rdp->tempRectData;

  if (rdp->otherModes.cycleType == CYCLE_TYPE_FILL ||
    rdp->otherModes.cycleType == CYCLE_TYPE_COPY)
    yl |= 3;

  ewData[0] = (0x3680 << 16) | yl;
  ewData[1] = (yl << 16) | yh;
  ewData[2] = (xlint << 16) | ((xl & 0x3) << 14);
  ewData[3] = 0;
  ewData[4] = (xhint << 16) | ((xh & 0x3) << 14);
  ewData[5] = 0;
  ewData[6] = (xlint << 16) | ((xl & 0x3) << 14);
  ewData[7] = 0;

  memset(ewData + 8, 0, 36 * sizeof(*ewData));
  DrawTriangle(rdp, false, false, false, true);
}

/* ============================================================================
 *  Command: FullSync.
 * ========================================================================= */
static void
  RDPFullSync(struct RDP *rdp,
  uint32_t unused(arg1), uint32_t unused(arg2)) {
  BusRaiseRCPInterrupt(rdp->bus, MI_INTR_DP);
}

/* ============================================================================
 *  Command: SetColorImage.
 * ========================================================================= */
static void
RDPSetColorImage(struct RDP *rdp, uint32_t arg1, uint32_t arg2) {
  struct RDPMiscState *miscState = &rdp->miscState;

  miscState->fbFormat = arg1 >> 21 & 0x7;
  miscState->fbSize = arg1 >> 19 & 0x3;
  miscState->fbWidth = (arg1 & 0x3FF) + 1;
  miscState->fbAddress = arg2 & 0x01FFFFFF;

  /* Jet Force Gemini attempts to set fbFormat to 4? */
  if (miscState->fbFormat < 2 || miscState->fbFormat > 32)
    miscState->fbFormat = 2;
}

/* ============================================================================
 *  Command: SetCombine.
 * ========================================================================= */
static void
RDPSetCombine(struct RDP *rdp, uint32_t arg1, uint32_t arg2) {
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
 *  Command: SetFillColor32.
 * ========================================================================= */
static void
RDPSetFillColor32(struct RDP *rdp,
  uint32_t unused(arg1), uint32_t arg2) {
  rdp->fillColor = arg2;
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
 *  Command: SyncPipe.
 * ========================================================================= */
static void RDPSyncPipe(struct RDP *unused(rdp),
  uint32_t unused(arg1), uint32_t unused(arg2)) {
  debug("Synchronize pipeline.");
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
    case 0x27: RDPSyncPipe(rdp, arg1, arg2); break;
    case 0x29: RDPFullSync(rdp, arg1, arg2); break;
    case 0x2D: RDPSetScissor(rdp, arg1, arg2); break;
    case 0x2F: RDPSetOtherModes(rdp, arg1, arg2); break;
    case 0x36: RDPFillRect(rdp, arg1, arg2); break;
    case 0x37: RDPSetFillColor32(rdp, arg1, arg2); break;
    case 0x3C: RDPSetCombine(rdp, arg1, arg2); break;
    case 0x3F: RDPSetColorImage(rdp, arg1, arg2); break;

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

