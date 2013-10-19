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
#else
#include <assert.h>
#endif

#define GET_LOW(x) (((x) & 0x3E) << 2)
#define GET_MED(x) (((x) & 0x7C0) >> 3)
#define GET_HI(x)  (((x) >> 8) & 0xF8)

#define PAIRREAD16(rdst,hdst,in) {assert(in <= 0x7FFFFE); \
  (rdst)=bswap16(rdram_16[in]); (hdst) = hidden_bits[in];}

/* Global data. */
COLOR memory_color;
COLOR pre_memory_color;
uint32_t fb_address;

int32_t fb_format = FORMAT_RGBA;
uint8_t hidden_bits[0x400000];

/* FBRead functions. */
static void FBRead_4(uint32_t, uint32_t *);
static void FBRead_8(uint32_t, uint32_t *);
static void FBRead_16(uint32_t, uint32_t *);
static void FBRead_32(uint32_t, uint32_t *);
static void FBRead2_4(uint32_t, uint32_t *);
static void FBRead2_8(uint32_t, uint32_t *);
static void FBRead2_16(uint32_t, uint32_t *);
static void FBRead2_32(uint32_t, uint32_t *);

const FBReadFunc FBReadFuncLUT[4] = {
  FBRead_4, FBRead_8, FBRead_16, FBRead_32
};

const FBReadFunc FBReadFunc2LUT[4] = {
  FBRead2_4, FBRead2_8, FBRead2_16, FBRead2_32
};

#define bswap16(x) (((x << 8) & 0xFF00) | ((x >> 8) & 0x00FF))
#define bswap32(x) __builtin_bswap32(x)

static uint8_t RREADADDR8(unsigned in) {
  assert(in <= 0x7FFFFF);
  return rdram_8[in];
}

static uint32_t RREADIDX32(unsigned in) {
  assert(in <= 0x1FFFFF);
  return bswap32(rdram[in]);
}

static void
FBRead_4(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  memory_color.r = memory_color.g = memory_color.b = 0;
  
  *curpixel_memcvg = 7;
  memory_color.a = 0xe0;
}

static void
FBRead2_4(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  pre_memory_color.r = pre_memory_color.g = pre_memory_color.b = 0;
  pre_memory_color.a = 0xe0;
  *curpixel_memcvg = 7;
}

static void
FBRead_8(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  uint8_t mem = RREADADDR8(fb_address + curpixel);
  memory_color.r = memory_color.g = memory_color.b = mem;
  *curpixel_memcvg = 7;
  memory_color.a = 0xe0;
}

static void
FBRead2_8(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  uint8_t mem = RREADADDR8(fb_address + curpixel);
  pre_memory_color.r = pre_memory_color.g = pre_memory_color.b = mem;
  pre_memory_color.a = 0xe0;
  *curpixel_memcvg = 7;
}

static void FBRead_16(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  uint16_t fword;
  uint8_t hbyte;
  uint32_t addr = (fb_address >> 1) + curpixel;
  PAIRREAD16(fword, hbyte, addr);
  uint8_t lowbits;

  if (fb_format == FORMAT_RGBA) {
    memory_color.r = GET_HI(fword);
    memory_color.g = GET_MED(fword);
    memory_color.b = GET_LOW(fword);
    lowbits = ((fword & 1) << 2) | hbyte;
  }

  else {
    memory_color.r = memory_color.g = memory_color.b = fword >> 8;
    lowbits = (fword >> 5) & 7;
  }

  if (other_modes.image_read_en) {
    *curpixel_memcvg = lowbits;
    memory_color.a = lowbits << 5;
  }

  else {
    *curpixel_memcvg = 7;
    memory_color.a = 0xe0;
  }
}

static void
FBRead2_16(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  uint16_t fword;
  uint8_t hbyte;
  uint32_t addr = (fb_address >> 1) + curpixel;
  PAIRREAD16(fword, hbyte, addr);
  uint8_t lowbits;

  if (fb_format == FORMAT_RGBA) {
    pre_memory_color.r = GET_HI(fword);
    pre_memory_color.g = GET_MED(fword);
    pre_memory_color.b = GET_LOW(fword);
    lowbits = ((fword & 1) << 2) | hbyte;
  }

  else {
    pre_memory_color.r = pre_memory_color.g = pre_memory_color.b = fword >> 8;
    lowbits = (fword >> 5) & 7;
  }

  if (other_modes.image_read_en) {
    *curpixel_memcvg = lowbits;
    pre_memory_color.a = lowbits << 5;
  }

  else {
    *curpixel_memcvg = 7;
    pre_memory_color.a = 0xe0;
  }
}

static void
FBRead_32(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  uint32_t mem = RREADIDX32((fb_address >> 2) + curpixel);
  memory_color.r = (mem >> 24) & 0xff;
  memory_color.g = (mem >> 16) & 0xff;
  memory_color.b = (mem >> 8) & 0xff;

  if (other_modes.image_read_en) {
    *curpixel_memcvg = (mem >> 5) & 7;
    memory_color.a = (mem) & 0xe0;
  }

  else {
    *curpixel_memcvg = 7;
    memory_color.a = 0xe0;
  }
}

static void
FBRead2_32(uint32_t curpixel, uint32_t* curpixel_memcvg) {
  uint32_t mem = RREADIDX32((fb_address >> 2) + curpixel);
  pre_memory_color.r = (mem >> 24) & 0xff;
  pre_memory_color.g = (mem >> 16) & 0xff;
  pre_memory_color.b = (mem >> 8) & 0xff;

  if (other_modes.image_read_en) {
    *curpixel_memcvg = (mem >> 5) & 7;
    pre_memory_color.a = (mem) & 0xe0;
  }

  else {
    *curpixel_memcvg = 7;
    pre_memory_color.a = 0xe0;
  }
}

