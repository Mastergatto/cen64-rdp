/* ============================================================================
 *  FBAccess.h: Framebuffer access functions.
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
#ifndef __FBACCESS_H__
#define __FBACCESS_H__
#include "Common.h"
#include "Core.h"

#define FORMAT_RGBA 0
#define FORMAT_YUV  1
#define FORMAT_CI   2
#define FORMAT_IA   3
#define FORMAT_I    4

extern COLOR memory_color;
extern COLOR pre_memory_color;
extern uint32_t fb_address;
extern int32_t fb_format;

extern uint8_t hidden_bits[0x400000];

typedef void (*FBReadFunc)(uint32_t, uint32_t *);

extern const FBReadFunc FBReadFuncLUT[4];
extern const FBReadFunc FBReadFunc2LUT[4];

#endif

