/* ============================================================================
 *  FBAccess.c: Framebuffer access functions.
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
#include "FBAccess.h"

#ifdef __cplusplus
#include <cassert>
#include <cstring>
#else
#include <assert.h>
#include <string.h>
#endif

#define GET_LOW(x) (((x) & 0x3E) << 2)
#define GET_MED(x) (((x) & 0x7C0) >> 3)
#define GET_HI(x)  (((x) >> 8) & 0xF8)

/* Global data. */
COLOR memory_color;
COLOR pre_memory_color;
uint32_t fb_address;

int32_t fb_format = FORMAT_RGBA;
uint8_t hidden_bits[0x400000];

static const COLOR ResetColor = {0x00, 0x00, 0x00, 0xE0};

/* FBRead functions. */
static void FBRead_4(uint32_t, uint32_t *);
static void FBRead_8(uint32_t, uint32_t *);
static void FBRead_16(uint32_t, uint32_t *);
static void FBRead_32(uint32_t, uint32_t *);
static void FBRead2_16(uint32_t, uint32_t *);
static void FBRead2_32(uint32_t, uint32_t *);

const FBReadFunc FBReadFuncLUT[4] = {
  FBRead_4, FBRead_8, FBRead_16, FBRead_32
};

const FBReadFunc FBReadFunc2LUT[4] = {
  FBRead_4, FBRead_8, FBRead2_16, FBRead2_32
};

/* FBWrite functions. */
static void FBWrite4(uint32_t, uint32_t,
  uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static void FBWrite8(uint32_t, uint32_t,
  uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static void FBWrite16(uint32_t, uint32_t,
  uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static void FBWrite32(uint32_t, uint32_t,
  uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);

const FBWriteFunc FBWriteFuncLUT[4] = {
  FBWrite4, FBWrite8, FBWrite16, FBWrite32
};

/* ============================================================================
 *  Memory access functions.
 * ========================================================================= */
static uint32_t bswap32(uint32_t x) { return __builtin_bswap32(x); }
static uint16_t bswap16(uint16_t x) { return ((x << 8) & 0xFF00) |
  ((x >> 8) & 0x00FF); }

static uint8_t
RDRAMRead8(uint32_t address) {
  assert(address < 0x7FFFFF);
  return rdram_8[address];
}

static void
RDRAMRead16H(uint32_t address, uint16_t *hword, uint8_t *hbyte) {
  uint16_t data;

  assert(address < 0x7FFFFF);
  memcpy(&data, rdram_8 + address, sizeof(data));
  *hword = ByteOrderSwap16(data);
  *hbyte = hidden_bits[address >> 1];
}

static void
RDRAMRead32(uint32_t address, uint8_t *buffer) {
  uint32_t data;

  assert(address <= 0x7FFFFF);
  memcpy(buffer, rdram_8 + address, sizeof(data));
}

static void
RDRAMWrite8(uint32_t address, uint8_t data) {
  assert(address <= 0x7FFFFF);
  rdram_8[address] = data;
}

#define PAIRWRITE16(in,rval,hval) {assert(in <= 0x7FFFFE); \
  rdram_16[in]=bswap16(rval); hidden_bits[in]=(hval);}

#define PAIRWRITE32(in,rval,hval0,hval1) {assert(in <= 0x7FFFFC); \
  rdram[in]=bswap32(rval); hidden_bits[(in)<<1]=(hval0); \
  hidden_bits[((in)<<1)+1]=(hval1); }

#define PAIRWRITE8(in,rval,hval) {assert(in <= 0x7FFFFF); \
  rdram_8[in]=(rval); if ((in) & 1) hidden_bits[(in)>>1]=(hval);}

/* ============================================================================
 *  Assistance functions.
 * ========================================================================= */
static int finalize_spanalpha(uint32_t blend_en,
  uint32_t curpixel_cvg, uint32_t curpixel_memcvg) {
  int finalcvg;
  
  switch(other_modes.cvg_dest) {
  case CVG_CLAMP: 
    if (!blend_en)
      finalcvg = curpixel_cvg - 1;
    else
      finalcvg = curpixel_cvg + curpixel_memcvg;

    if (!(finalcvg & 8))
      finalcvg &= 7;
    else
      finalcvg = 7;

    break;

  case CVG_WRAP:
    finalcvg = (curpixel_cvg + curpixel_memcvg) & 7;
    break;

  case CVG_ZAP: 
    finalcvg = 7;
    break;

  case CVG_SAVE: 
    finalcvg = curpixel_memcvg;
    break;
  }

  return finalcvg;
}

/* ============================================================================
 *  Framebuffer read functions.
 * ========================================================================= */
static void
FBRead_4(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  memory_color = ResetColor;
  *curpixel_memcvg = 7;
}

static void
FBRead_8(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  uint32_t address = fb_address + curpixel;
  uint32_t component = RDRAMRead8(address);

  memory_color.r = memory_color.g = memory_color.b = component;
  memory_color.a = 0xE0;
  *curpixel_memcvg = 7;
}

static void FBRead_16(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  uint32_t address = fb_address + (curpixel << 1);
  uint8_t hbyte, lowbits;
  uint16_t fword;

  RDRAMRead16H(address, &fword, &hbyte);

  if (fb_format == FORMAT_RGBA) {
    memory_color.r = GET_HI(fword);
    memory_color.g = GET_MED(fword);
    memory_color.b = GET_LOW(fword);
    lowbits = ((fword & 1) << 2) | hbyte;
  }

  else {
    uint32_t component = fword >> 8;

    memory_color.r = component;
    memory_color.g = component;
    memory_color.b = component;
    lowbits = (fword >> 5) & 7;
  }

  if (other_modes.image_read_en) {
    *curpixel_memcvg = lowbits;
    memory_color.a = lowbits << 5;
  }

  else {
    *curpixel_memcvg = 7;
    memory_color.a = 0xE0;
  }
}

static void
FBRead2_16(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  uint32_t address = fb_address + (curpixel << 1);
  uint8_t hbyte, lowbits;
  uint16_t fword;

  RDRAMRead16H(address, &fword, &hbyte);

  if (fb_format == FORMAT_RGBA) {
    pre_memory_color.r = GET_HI(fword);
    pre_memory_color.g = GET_MED(fword);
    pre_memory_color.b = GET_LOW(fword);
    lowbits = ((fword & 1) << 2) | hbyte;
  }

  else {
    uint32_t component = fword >> 8;

    pre_memory_color.r = component;
    pre_memory_color.g = component;
    pre_memory_color.b = component;
    lowbits = (fword >> 5) & 7;
  }

  if (other_modes.image_read_en) {
    *curpixel_memcvg = lowbits;
    pre_memory_color.a = lowbits << 5;
  }

  else {
    *curpixel_memcvg = 7;
    pre_memory_color.a = 0xE0;
  }
}

static void
FBRead_32(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  uint8_t buffer[4];

  RDRAMRead32(fb_address + curpixel, buffer);
  memory_color.r = buffer[0];
  memory_color.g = buffer[1];
  memory_color.b = buffer[2];
  memory_color.a = buffer[3] & 0xE0;

  *curpixel_memcvg = other_modes.image_read_en
    ? buffer[3] >> 5
    : 7;
}

static void
FBRead2_32(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  uint8_t buffer[4];

  RDRAMRead32(fb_address + curpixel, buffer);
  pre_memory_color.r = buffer[0];
  pre_memory_color.g = buffer[1];
  pre_memory_color.b = buffer[2];
  pre_memory_color.a = buffer[3] & 0xE0;

  *curpixel_memcvg = other_modes.image_read_en
    ? buffer[3] >> 5
    : 7;
}

/* ============================================================================
 *  Framebuffer write functions.
 * ========================================================================= */
static void FBWrite4(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b,
  uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg) {
  uint32_t address = fb_address + curpixel;
  RDRAMWrite8(address, 0);
}

static void FBWrite8(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b,
  uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg) {
  uint32_t fb = fb_address + curpixel;
  PAIRWRITE8(fb, r & 0xff, (r & 1) ? 3 : 0);
}

static void FBWrite16(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b,
  uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg) {
#undef CVG_DRAW
#ifdef CVG_DRAW
  int covdraw = (curpixel_cvg - 1) << 5;
  r=covdraw; g=covdraw; b=covdraw;
#endif

  uint32_t fb;
  uint16_t rval;
  uint8_t hval;
  fb = (fb_address >> 1) + curpixel;  

  int32_t finalcvg = finalize_spanalpha(blend_en, curpixel_cvg, curpixel_memcvg);
  int16_t finalcolor; 

  if (fb_format == FORMAT_RGBA)
  {
    finalcolor = ((r & ~7) << 8) | ((g & ~7) << 3) | ((b & ~7) >> 2);
  }
  else
  {
    finalcolor = (r << 8) | (finalcvg << 5);
    finalcvg = 0;
  }

  
  rval = finalcolor|(finalcvg >> 2);
  hval = finalcvg & 3;
  PAIRWRITE16(fb, rval, hval);
}

static void FBWrite32(uint32_t curpixel, uint32_t r, uint32_t g, uint32_t b,
  uint32_t blend_en, uint32_t curpixel_cvg, uint32_t curpixel_memcvg) {
  uint32_t fb = (fb_address >> 2) + curpixel;

  int32_t finalcolor;
  int32_t finalcvg = finalize_spanalpha(blend_en, curpixel_cvg, curpixel_memcvg);
    
  finalcolor = (r << 24) | (g << 16) | (b << 8);
  finalcolor |= (finalcvg << 5);

  PAIRWRITE32(fb, finalcolor, (g & 1) ? 3 : 0, 0);
}

