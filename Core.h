/* ============================================================================
 *  Core.h: Core RDP logic.
 *
 *  Original author: `MooglyGuy`. Many thanks to: Ville Linde, `angrylion`,
 *  Shoutouts to: `olivieryuyu`, `marshallh`, `LaC`, `oman`, `pinchy`, `ziggy`,
 *  `FatCat` and other folks I forgot.
 *
 *  Vectorization by Tyler J. Stachecki.
 *
 *  This file is subject to the terms and conditions defined in
 *  file 'MAMELICENSE', which is part of this source code package.
 * ========================================================================= */
#ifndef __RDP__CORE__
#define __RDP__CORE__
#include "Common.h"

#define SP_STATUS_HALT                  0x0001
#define SP_STATUS_BROKE                 0x0002
#define SP_STATUS_DMABUSY               0x0004
#define SP_STATUS_DMAFULL               0x0008
#define SP_STATUS_IOFULL                0x0010
#define SP_STATUS_SSTEP                 0x0020
#define SP_STATUS_INTR_BREAK            0x0040
#define SP_STATUS_SIGNAL0               0x0080
#define SP_STATUS_SIGNAL1               0x0100
#define SP_STATUS_SIGNAL2               0x0200
#define SP_STATUS_SIGNAL3               0x0400
#define SP_STATUS_SIGNAL4               0x0800
#define SP_STATUS_SIGNAL5               0x1000
#define SP_STATUS_SIGNAL6               0x2000
#define SP_STATUS_SIGNAL7               0x4000

#define DP_STATUS_XBUS_DMA              0x01
#define DP_STATUS_FREEZE                0x02
#define DP_STATUS_FLUSH                 0x04
#define DP_STATUS_START_GCLK            0x008
#define DP_STATUS_TMEM_BUSY             0x010
#define DP_STATUS_PIPE_BUSY             0x020
#define DP_STATUS_CMD_BUSY              0x040
#define DP_STATUS_CBUF_READY            0x080
#define DP_STATUS_DMA_BUSY              0x100
#define DP_STATUS_END_VALID             0x200
#define DP_STATUS_START_VALID           0x400

#define PRESCALE_WIDTH 640
#define PRESCALE_HEIGHT 625

#define LSB_FIRST 0
#ifdef LSB_FIRST
  #define BYTE_ADDR_XOR           3
  #define WORD_ADDR_XOR           1
  #define BYTE4_XOR_BE(a)         ((a) ^ 3)                               
#else
  #define BYTE_ADDR_XOR           0
  #define WORD_ADDR_XOR           0
  #define BYTE4_XOR_BE(a)         (a)
#endif

#ifdef LSB_FIRST
  #define BYTE_XOR_DWORD_SWAP 7
  #define WORD_XOR_DWORD_SWAP 3
#else
  #define BYTE_XOR_DWORD_SWAP 4
  #define WORD_XOR_DWORD_SWAP 2
#endif

#define DWORD_XOR_DWORD_SWAP 1

extern uint8_t *rdram_8;
extern uint16_t *rdram_16;
extern uint32_t *rdram;

struct RDP;
void RDPProcessList(struct RDP *);
int rdp_init();

typedef struct {
  int32_t r, g, b, a;
} COLOR;

typedef struct {
  int stalederivs;
  int dolod;
  int partialreject_1cycle; 
  int partialreject_2cycle;
  int special_bsel0; 
  int special_bsel1;
  int rgb_alpha_dither;
} MODEDERIVS;

typedef struct {
  int cycle_type;
  int persp_tex_en;
  int detail_tex_en;
  int sharpen_tex_en;
  int tex_lod_en;
  int en_tlut;
  int tlut_type;
  int sample_type;
  int mid_texel;
  int bi_lerp0;
  int bi_lerp1;
  int convert_one;
  int key_en;
  int rgb_dither_sel;
  int alpha_dither_sel;
  int blend_m1a_0;
  int blend_m1a_1;
  int blend_m1b_0;
  int blend_m1b_1;
  int blend_m2a_0;
  int blend_m2a_1;
  int blend_m2b_0;
  int blend_m2b_1;
  int force_blend;
  int alpha_cvg_select;
  int cvg_times_alpha;
  int z_mode;
  int cvg_dest;
  int color_on_cvg;
  int image_read_en;
  int z_update_en;
  int z_compare_en;
  int antialias_en;
  int z_source_sel;
  int dither_alpha_en;
  int alpha_compare_en;
  MODEDERIVS f;
} OTHER_MODES;

typedef struct {
  int lx, rx;
  int unscrx;
  int validline;
  int32_t r, g, b, a, s, t, w, z;
  int32_t majorx[4];
  int32_t minorx[4];
  int32_t invalyscan[4];
} SPAN;

typedef struct {
  int startspan;
  int endspan;
  int preendspan;
  int nextspan;
  int midspan;
  int longspan;
} SPANSIGS;

extern uint32_t max_level;
extern OTHER_MODES other_modes;
extern SPAN span[1024];

extern void (*tcdiv_ptr)(int32_t, int32_t, int32_t, int32_t*, int32_t*);

#endif

