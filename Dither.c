/* ============================================================================
 *  Dither.c: Dithering Functions.
 *
 *  Original author: `MooglyGuy`. Many thanks to: Ville Linde, `angrylion`,
 *  Shoutouts to: `olivieryuyu`, `marshallh`, `LaC`, `oman`, `pinchy`, `ziggy`,
 *  `FatCat` and other folks I forgot.
 *
 *  Vectorization, Cleanup by Tyler J. Stachecki.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'MAMELICENSE', which is part of this source code package.
 * ========================================================================= */
#include "Common.h"
#include "Core.h"
#include "Dither.h"
#include "Random.h"

/* Global data. */
int32_t noise;

/* DitherFuncs. */
static void DitherComplete(int32_t *r, int32_t *g, int32_t *b, int32_t dither);
static void DitherNothing(int32_t *r, int32_t *g, int32_t *b, int32_t dither) {}

const DitherFunc DitherFuncLUT[2] = {
  DitherComplete,
  DitherNothing
};

/* DitherNoiseFuncs. */
static void DoDitherNoise(int32_t x, int32_t y, int32_t *cdith, int32_t *adith);
static void DoDitherOnly(int32_t x, int32_t y, int32_t *cdith, int32_t *adith);
static void DoDitherNothing(int32_t x, int32_t y, int32_t* cdith,
  int32_t* adith) {}

const DitherNoiseFunc DitherNoiseFuncLUT[3] = {
  DoDitherNoise,
  DoDitherOnly,
  DoDitherNothing
};

/* Magical LUTs. */
static const uint8_t BayerMatrix[16] align(16) = {
  0, 4, 1, 5,
  4, 0, 5, 1,
  3, 7, 2, 6,
  7, 3, 6, 2
};

static const uint8_t MagicMatrix[16] align(16) = {
  0, 6, 1, 7,
  4, 2, 5, 3,
  3, 5, 2, 4,
  7, 1, 6, 0
};

/* value = (value > 247) ? 0xFF : (value & 0xF8) + 8; */
static const uint8_t DitherLUT[256] align(64) = {
  0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 
  0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 
  0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 0x18, 
  0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 
  0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 0x28, 
  0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 
  0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 0x38, 
  0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 
  0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 0x48, 
  0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50, 
  0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 0x58, 
  0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 0x60, 
  0x68, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68, 0x68, 
  0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 0x70, 
  0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 0x78, 
  0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 
  0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 
  0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 
  0x98, 0x98, 0x98, 0x98, 0x98, 0x98, 0x98, 0x98, 
  0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 0xA0, 
  0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 0xA8, 
  0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 
  0xB8, 0xB8, 0xB8, 0xB8, 0xB8, 0xB8, 0xB8, 0xB8, 
  0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 
  0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 0xC8, 
  0xD0, 0xD0, 0xD0, 0xD0, 0xD0, 0xD0, 0xD0, 0xD0, 
  0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 0xD8, 
  0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 0xE0, 
  0xE8, 0xE8, 0xE8, 0xE8, 0xE8, 0xE8, 0xE8, 0xE8, 
  0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 0xF0, 
  0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 0xF8, 
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

static void
DitherComplete(int32_t *r, int32_t *g, int32_t *b, int32_t dither) {
  if ((*r & 7) > dither)
    *r = DitherLUT[*r];

  if (other_modes.rgb_dither_sel != 2) {
    if ((*g & 7) > dither)
      *g = DitherLUT[*g];

    if ((*b & 7) > dither)
      *b = DitherLUT[*b];
  }

  else {
    if ((*g & 7) > ((dither + 3) & 7))
      *g = DitherLUT[*g];

    if ((*b & 7) > ((dither + 5) & 7))
      *b = DitherLUT[*b];
  }
}

static void
DoDitherNoise(int32_t x, int32_t y, int32_t* cdith, int32_t* adith) {
  int dithindex; 

  noise = ((irand() & 7) << 6) | 0x20;
  switch(other_modes.f.rgb_alpha_dither) {
  case 0:
    dithindex = ((y & 3) << 2) | (x & 3);
    *adith = *cdith = MagicMatrix[dithindex];
    break;

  case 1:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = MagicMatrix[dithindex];
    *adith = (~(*cdith)) & 7;
    break;

  case 2:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = MagicMatrix[dithindex];
    *adith = (noise >> 6) & 7;
    break;

  case 3:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = MagicMatrix[dithindex];
    *adith = 0;
    break;

  case 4:
    dithindex = ((y & 3) << 2) | (x & 3);
    *adith = *cdith = BayerMatrix[dithindex];
    break;

  case 5:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = BayerMatrix[dithindex];
    *adith = (~(*cdith)) & 7;
    break;

  case 6:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = BayerMatrix[dithindex];
    *adith = (noise >> 6) & 7;
    break;

  case 7:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = BayerMatrix[dithindex];
    *adith = 0;
    break;

  case 8:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = irand() & 7;
    *adith = MagicMatrix[dithindex];
    break;

  case 9:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = irand() & 7;
    *adith = (~MagicMatrix[dithindex]) & 7;
    break;

  case 10:
    *cdith = irand() & 7;
    *adith = (noise >> 6) & 7;
    break;

  case 11:
    *cdith = irand() & 7;
    *adith = 0;
    break;

  case 12:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = 7;
    *adith = BayerMatrix[dithindex];
    break;

  case 13:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = 7;
    *adith = (~BayerMatrix[dithindex]) & 7;
    break;

  case 14:
    *cdith = 7;
    *adith = (noise >> 6) & 7;
    break;

  case 15:
    *cdith = 7;
    *adith = 0;
    break;
  }
}

static void
DoDitherOnly(int32_t x, int32_t y, int32_t* cdith, int32_t* adith) {
  int dithindex; 

  switch(other_modes.f.rgb_alpha_dither) {
  case 0:
    dithindex = ((y & 3) << 2) | (x & 3);
    *adith = *cdith = MagicMatrix[dithindex];
    break;

  case 1:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = MagicMatrix[dithindex];
    *adith = (~(*cdith)) & 7;
    break;

  case 2:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = MagicMatrix[dithindex];
    *adith = (noise >> 6) & 7;
    break;

  case 3:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = MagicMatrix[dithindex];
    *adith = 0;
    break;

  case 4:
    dithindex = ((y & 3) << 2) | (x & 3);
    *adith = *cdith = BayerMatrix[dithindex];
    break;

  case 5:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = BayerMatrix[dithindex];
    *adith = (~(*cdith)) & 7;
    break;

  case 6:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = BayerMatrix[dithindex];
    *adith = (noise >> 6) & 7;
    break;

  case 7:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = BayerMatrix[dithindex];
    *adith = 0;
    break;

  case 8:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = irand() & 7;
    *adith = MagicMatrix[dithindex];
    break;

  case 9:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = irand() & 7;
    *adith = (~MagicMatrix[dithindex]) & 7;
    break;

  case 10:
    *cdith = irand() & 7;
    *adith = (noise >> 6) & 7;
    break;

  case 11:
    *cdith = irand() & 7;
    *adith = 0;
    break;

  case 12:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = 7;
    *adith = BayerMatrix[dithindex];
    break;

  case 13:
    dithindex = ((y & 3) << 2) | (x & 3);
    *cdith = 7;
    *adith = (~BayerMatrix[dithindex]) & 7;
    break;

  case 14:
    *cdith = 7;
    *adith = (noise >> 6) & 7;
    break;

  case 15:
    *cdith = 7;
    *adith = 0;
    break;
  }
}

