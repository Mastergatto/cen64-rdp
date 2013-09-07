/* ============================================================================
 *  Tables.h: Precalculated RDP data tables.
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
#ifndef __RDP__TABLES__
#define __RDP__TABLES__
#include "Common.h"

typedef struct {
  uint8_t cvg;
  uint8_t cvbit;
  uint8_t xoff;
  uint8_t yoff;
} CVtcmaskDERIVATIVE;

extern const uint16_t z_com_table[0x40000];
extern const uint32_t z_complete_dec_table[0x4000];
extern const CVtcmaskDERIVATIVE cvarray[0x100];
extern const int32_t log2table[256];
extern const uint8_t replicated_rgba[32];
extern const int32_t maskbits_table[16];
extern const uint32_t special_9bit_clamptable[512];
extern const int32_t special_9bit_exttable[512];
extern const int32_t tcdiv_table[0x8000];
extern const uint8_t bldiv_hwaccurate_table[0x8000];

#endif

