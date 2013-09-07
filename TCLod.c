/* ============================================================================
 *  TCLod.c: Texture Coordinate (TC) Level of Detail (LOD) Functions.
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
#include "TCLod.h"

#include <smmintrin.h>

int32_t tclod_tcclamp(int32_t x) {
  static const uint32_t LUT2[4] align(16) = {
    0x0000, 0x7FFF, 0x8000, 0x0000,
  };

  static const uint32_t LUT[16] align(64) = {
    0x1FFFF, 0x8000, 0x10000,0x4FFFF,
    0x8000, 0x8000, 0x8000, 0x8000,
    0x7FFF, 0x7FFF, 0x7FFF, 0x7FFF,
    0x7FFF, 0x7FFF, 0X7FFF, 0x7FFF,
  };

  unsigned idx = (x >> 15) & 0xF;
  uint32_t value = LUT[idx];
  uint32_t temp;

  if ((x >> 17) & 0x3)
    return value;

  else if ((temp = (x & value)) != value)
    return temp;

  return LUT2[idx];
}

void tclod_1cycle_current_simple(int32_t* sss, int32_t* sst,
  const int32_t *spanptr, const int32_t *dincs, int32_t scanline,
  int32_t prim_tile, int32_t* t1, const SPANSIGS* sigs) {
  int32_t nexts, nextt, nextsw;
  int32_t fars, fart, farsw;
  int32_t nextstwtemp[4];
  int32_t farstwtemp[4];

  uint32_t l_tile = 0, magnify = 0, distant = 0;
  int32_t nextscan = scanline + 1;
  int32_t lodclamp = 0;
  int32_t lod = 0;

  __m128i stw;
  __m128i dincstw;
  __m128i nextstw;
  __m128i farstw;

  stw = _mm_load_si128((__m128i*) spanptr);
  dincstw = _mm_load_si128((__m128i*) dincs);
  *sss = tclod_tcclamp(*sss);
  *sst = tclod_tcclamp(*sst);

  if (!other_modes.f.dolod)
    return;

  if (span[nextscan].validline) {
    if (!sigs->endspan || !sigs->longspan) {
      nextstw = _mm_add_epi32(stw, dincstw);
      nextstw = _mm_srai_epi32(nextstw, 16);

      if (!(sigs->preendspan && sigs->longspan)
        && !(sigs->endspan && sigs->midspan)) {
        farstw = _mm_slli_epi32(dincstw, 1);
        farstw = _mm_add_epi32(farstw, stw);
        farstw = _mm_srai_epi32(farstw, 16);
      }

      else {
        farstw = _mm_sub_epi32(stw, dincstw);
        farstw = _mm_srai_epi32(farstw, 16);
      }
    }

    else {
      __m128i temp = _mm_loadu_si128((__m128i*) &span[nextscan].s);

      nextstw = _mm_srai_epi32(temp, 16);
      farstw = _mm_add_epi32(temp, dincstw);
      farstw = _mm_srai_epi32(farstw, 16);
    }
  }

  else {
    nextstw = _mm_add_epi32(stw, dincstw);
    nextstw = _mm_srai_epi32(nextstw, 16);
    farstw = _mm_slli_epi32(dincstw, 1);
    farstw = _mm_add_epi32(farstw, stw);
    farstw = _mm_srai_epi32(farstw, 16);
  }

  /* TODO: Avoid pulling this out? Expensive. */
  _mm_store_si128((__m128i*) nextstwtemp, nextstw);
  _mm_store_si128((__m128i*) farstwtemp, farstw);

  fars = farstwtemp[0];
  fart = farstwtemp[1];
  farsw = farstwtemp[2];
  nexts = nextstwtemp[0];
  nextt = nextstwtemp[1];
  nextsw = nextstwtemp[2];

  tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
  tcdiv_ptr(fars, fart, farsw, &fars, &fart);

  int farcheck = fart | fars;
  int nextcheck = nextt | nexts;
  int check = nextcheck | farcheck;
  lodclamp = (check & 0x60000) != 0;

  tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);
  lodfrac_lodtile_signals(lodclamp, lod, &l_tile, &magnify, &distant);
  
  if (other_modes.tex_lod_en) {
    static const int32_t lut[2] = {1, 0};
    int addendIdx = !other_modes.detail_tex_en || magnify;

    if (distant)
      l_tile = max_level;

    *t1 = (prim_tile + l_tile + lut[addendIdx]) & 7;
  }
}

