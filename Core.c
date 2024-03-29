/* ============================================================================
 *  Core.c: Core RDP logic.
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
#include "Common.h"
#include "Core.h"
#include "CPU.h"
#include "Definitions.h"
#include "Dither.h"
#include "Externs.h"
#include "FBAccess.h"
#include "Helpers.h"
#include "Random.h"
#include "Registers.h"
#include "Tables.h"
#include "TCLod.h"

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef USE_SSE
#include <tmmintrin.h>
#endif

#define SIGN16(x) ((int16_t)(x))
#define SIGN8(x)  ((int8_t)(x))

#define SIGN(x, numb) (((x) & ((1 << numb) - 1)) | -((x) & (1 << (numb - 1))))
#define SIGNF(x, numb)  ((x) | -((x) & (1 << (numb - 1))))

#define GET_LOW(x)  (((x) & 0x3e) << 2)
#define GET_MED(x)  (((x) & 0x7c0) >> 3)
#define GET_HI(x) (((x) >> 8) & 0xf8)

#define GET_LOW_RGBA16_TMEM(x)  (replicated_rgba[((x) >> 1) & 0x1f])
#define GET_MED_RGBA16_TMEM(x)  (replicated_rgba[((x) >> 6) & 0x1f])
#define GET_HI_RGBA16_TMEM(x) (replicated_rgba[(x) >> 11])

static void fatalerror(const char * err, ...)
{
  char VsprintfBuffer[200];
  va_list arg;
  va_start(arg, err);
  vsprintf(VsprintfBuffer, err, arg);
#ifdef WIN32
  MessageBoxA(0,VsprintfBuffer,"RDP: fatal error",MB_OK);
#endif
#ifndef WIN32
  printf(VsprintfBuffer);
#endif
  va_end(arg);
  exit(0);
}

uint32_t rdp_cmd_data[0x10000];
uint32_t rdp_cmd_ptr = 0;
uint32_t rdp_cmd_cur = 0;

int blshifta = 0, blshiftb = 0, pastblshifta = 0, pastblshiftb = 0;
uint8_t* rdram_8;
uint16_t* rdram_16;

SPAN span[1024];
uint32_t cvgbuf[1024];

enum SpanType {
  SPAN_DR,
  SPAN_DG,
  SPAN_DB,
  SPAN_DA,
  SPAN_DS,
  SPAN_DT,
  SPAN_DW,
  SPAN_DZ,
};

static int32_t spans[8] align(16);
static int spans_dzpix;
static int32_t ewdata[44] align(16);

int spans_drdy, spans_dgdy, spans_dbdy, spans_dady, spans_dzdy;
int spans_cdr, spans_cdg, spans_cdb, spans_cda, spans_cdz;

static int spans_dsdy, spans_dtdy, spans_dwdy;

typedef struct {
  uint8_t r, g, b;
} FBCOLOR;

typedef struct
{
  uint8_t r, g, b, cvg;
} CCVG;

typedef struct
{
  uint16_t xl, yl, xh, yh;    
} RECTANGLE;

typedef struct
{
  int tilenum;
  uint16_t xl, yl, xh, yh;    
  int16_t s, t;         
  int16_t dsdx, dtdy;     
  uint32_t flip;  
} TEX_RECTANGLE;

typedef struct
{
  int clampdiffs, clampdifft;
  int clampens, clampent;
  int masksclamped, masktclamped;
  int notlutswitch, tlutswitch;
} FAKETILE;

typedef struct
{
  int format;
  int size;
  int line;
  int tmem;
  int palette;
  int ct, mt, cs, ms;
  int mask_t, shift_t, mask_s, shift_s;
  
  uint16_t sl, tl, sh, th;    
  
  FAKETILE f;
} TILE;

typedef struct
{
  int sub_a_rgb0;
  int sub_b_rgb0;
  int mul_rgb0;
  int add_rgb0;
  int sub_a_a0;
  int sub_b_a0;
  int mul_a0;
  int add_a0;

  int sub_a_rgb1;
  int sub_b_rgb1;
  int mul_rgb1;
  int add_rgb1;
  int sub_a_a1;
  int sub_b_a1;
  int mul_a1;
  int add_a1;
} COMBINE_MODES;

#define PIXEL_SIZE_4BIT     0
#define PIXEL_SIZE_8BIT     1
#define PIXEL_SIZE_16BIT    2
#define PIXEL_SIZE_32BIT    3

#define CYCLE_TYPE_1      0
#define CYCLE_TYPE_2      1
#define CYCLE_TYPE_COPY     2
#define CYCLE_TYPE_FILL     3

#define TEXEL_RGBA4       0
#define TEXEL_RGBA8       1
#define TEXEL_RGBA16      2
#define TEXEL_RGBA32      3
#define TEXEL_YUV4        4
#define TEXEL_YUV8        5
#define TEXEL_YUV16       6
#define TEXEL_YUV32       7
#define TEXEL_CI4       8
#define TEXEL_CI8       9
#define TEXEL_CI16        0xa
#define TEXEL_CI32        0xb
#define TEXEL_IA4       0xc
#define TEXEL_IA8       0xd
#define TEXEL_IA16        0xe
#define TEXEL_IA32        0xf
#define TEXEL_I4        0x10
#define TEXEL_I8        0x11
#define TEXEL_I16       0x12
#define TEXEL_I32       0x13

#define ZMODE_OPAQUE      0
#define ZMODE_INTERPENETRATING  1
#define ZMODE_TRANSPARENT   2
#define ZMODE_DECAL       3

COMBINE_MODES combine;
OTHER_MODES other_modes;

COLOR blend_color;
COLOR prim_color;
COLOR env_color;
COLOR fog_color;
COLOR combined_color;
COLOR texel0_color;
COLOR texel1_color;
COLOR nexttexel_color;
COLOR shade_color;
COLOR key_scale;
COLOR key_center;
COLOR key_width;
static int32_t primitive_lod_frac = 0;
static int32_t one_color = 0x100;
static int32_t zero_color = 0x00;

int32_t keyalpha;

static int32_t blenderone = 0xff;

static int32_t *combiner_rgbsub_a_r[2];
static int32_t *combiner_rgbsub_a_g[2];
static int32_t *combiner_rgbsub_a_b[2];
static int32_t *combiner_rgbsub_b_r[2];
static int32_t *combiner_rgbsub_b_g[2];
static int32_t *combiner_rgbsub_b_b[2];
static int32_t *combiner_rgbmul_r[2];
static int32_t *combiner_rgbmul_g[2];
static int32_t *combiner_rgbmul_b[2];
static int32_t *combiner_rgbadd_r[2];
static int32_t *combiner_rgbadd_g[2];
static int32_t *combiner_rgbadd_b[2];

static int32_t *combiner_alphasub_a[2];
static int32_t *combiner_alphasub_b[2];
static int32_t *combiner_alphamul[2];
static int32_t *combiner_alphaadd[2];

static int32_t *blender1a_r[2];
static int32_t *blender1a_g[2];
static int32_t *blender1a_b[2];
static int32_t *blender1b_a[2];
static int32_t *blender2a_r[2];
static int32_t *blender2a_g[2];
static int32_t *blender2a_b[2];
static int32_t *blender2b_a[2];

COLOR pixel_color;
COLOR inv_pixel_color;
COLOR blended_pixel_color;

uint32_t fill_color;    

uint32_t primitive_z;
uint16_t primitive_delta_z;

static int fb_size = PIXEL_SIZE_4BIT;
static int fb_width = 0;

static int ti_format = FORMAT_RGBA;
static int ti_size = PIXEL_SIZE_4BIT;
static int ti_width = 0;
static uint32_t ti_address = 0;

static uint32_t zb_address = 0;

static TILE tile[8];

static RECTANGLE clip = {0,0,0x2000,0x2000};
static int scfield = 0;
static int sckeepodd = 0;
int oldscyl = 0;

uint8_t TMEM[0x1000]; 

#define tlut ((uint16_t*)(&TMEM[0x800]))

#define PIXELS_TO_BYTES(pix, siz) (((pix) << (siz)) >> 1)

static void rdp_set_other_modes(uint32_t w1, uint32_t w2);
static void fetch_texel(COLOR *color, int s, int t, uint32_t tilenum);
static void fetch_texel_entlut(COLOR *color, int s, int t, uint32_t tilenum);
static void fetch_texel_quadro(COLOR *color0, COLOR *color1, COLOR *color2, COLOR *color3, int s0, int s1, int t0, int t1, uint32_t tilenum);
static void fetch_texel_entlut_quadro(COLOR *color0, COLOR *color1, COLOR *color2, COLOR *color3, int s0, int s1, int t0, int t1, uint32_t tilenum);
void tile_tlut_common_cs_decoder(uint32_t w1, uint32_t w2);
void loading_pipeline(int start, int end, int tilenum, int coord_quad, int ltlut);
void get_tmem_idx(int s, int t, uint32_t tilenum, uint32_t* idx0, uint32_t* idx1, uint32_t* idx2, uint32_t* idx3, uint32_t* bit3flipped, uint32_t* hibit);
void sort_tmem_idx(uint32_t *idx, uint32_t idxa, uint32_t idxb, uint32_t idxc, uint32_t idxd, uint32_t bankno);
void sort_tmem_shorts_lowhalf(uint32_t* bindshort, uint32_t short0, uint32_t short1, uint32_t short2, uint32_t short3, uint32_t bankno);
void compute_color_index(uint32_t* cidx, uint32_t readshort, uint32_t nybbleoffset, uint32_t tilenum);
void read_tmem_copy(int s, int s1, int s2, int s3, int t, uint32_t tilenum, uint32_t* sortshort, int* hibits, int* lowbits);
void replicate_for_copy(uint32_t* outbyte, uint32_t inshort, uint32_t nybbleoffset, uint32_t tilenum, uint32_t tformat, uint32_t tsize);
void fetch_qword_copy(uint32_t* hidword, uint32_t* lowdword, int32_t ssss, int32_t ssst, uint32_t tilenum);
void render_spans_1cycle_complete(int start, int end, int tilenum, int flip);
void render_spans_1cycle_notexel1(int start, int end, int tilenum, int flip);
void render_spans_1cycle_notex(int start, int end, int tilenum, int flip);
void render_spans_2cycle_complete(int start, int end, int tilenum, int flip);
void render_spans_2cycle_notexelnext(int start, int end, int tilenum, int flip);
void render_spans_2cycle_notexel1(int start, int end, int tilenum, int flip);
void render_spans_2cycle_notex(int start, int end, int tilenum, int flip);
void render_spans_fill(int start, int end, int flip);
void render_spans_copy(int start, int end, int tilenum, int flip);
static void combiner_1cycle(int adseed, uint32_t* curpixel_cvg);
static void combiner_2cycle(int adseed, uint32_t* curpixel_cvg);
static int blender_1cycle(uint32_t* fr, uint32_t* fg, uint32_t* fb, int dith, uint32_t blend_en, uint32_t prewrap, uint32_t curpixel_cvg, uint32_t curpixel_cvbit);
static int blender_2cycle(uint32_t* fr, uint32_t* fg, uint32_t* fb, int dith, uint32_t blend_en, uint32_t prewrap, uint32_t curpixel_cvg, uint32_t curpixel_cvbit);
static void texture_pipeline_cycle(COLOR* TEX, COLOR* prev, int32_t SSS, int32_t SST, uint32_t tilenum, uint32_t cycle);
static void tc_pipeline_copy(int32_t* sss0, int32_t* sss1, int32_t* sss2, int32_t* sss3, int32_t* sst, int tilenum);
static void tc_pipeline_load(int32_t* sss, int32_t* sst, int tilenum, int coord_quad);
static void tcclamp_cycle(int32_t* S, int32_t* T, int32_t* SFRAC, int32_t* TFRAC, int32_t maxs, int32_t maxt, int32_t num);
static void tcclamp_cycle_light(int32_t* S, int32_t* T, int32_t maxs, int32_t maxt, int32_t num);
static void tcshift_cycle(int32_t* S, int32_t* T, int32_t* maxs, int32_t* maxt, uint32_t num);
static void tcshift_copy(int32_t* S, int32_t* T, uint32_t num);
static int alpha_compare(int32_t comb_alpha);
static int32_t color_combiner_equation(int32_t a, int32_t b, int32_t c, int32_t d);
static int32_t alpha_combiner_equation(int32_t a, int32_t b, int32_t c, int32_t d);
static void blender_equation_cycle0(int* r, int* g, int* b);
static void blender_equation_cycle0_2(int* r, int* g, int* b);
static void blender_equation_cycle1(int* r, int* g, int* b);
static uint32_t rightcvghex(uint32_t x, uint32_t fmask); 
static uint32_t leftcvghex(uint32_t x, uint32_t fmask);
static void compute_cvg_noflip(int32_t scanline);
static void compute_cvg_flip(int32_t scanline);
static void fbfill_4(uint32_t curpixel);
static void fbfill_8(uint32_t curpixel);
static void fbfill_16(uint32_t curpixel);
static void fbfill_32(uint32_t curpixel);
static uint32_t z_decompress(uint32_t rawz);
static uint32_t dz_decompress(uint32_t compresseddz);
static uint32_t dz_compress(uint32_t value);
static void lookup_cvmask_derivatives(uint32_t mask, uint8_t* offx, uint8_t* offy, uint32_t* curpixel_cvg, uint32_t* curpixel_cvbit);
static void z_store(uint32_t zcurpixel, uint32_t z, int dzpixenc);
static uint32_t z_compare(uint32_t zcurpixel, uint32_t sz, uint16_t dzpix, int dzpixenc, uint32_t* blend_en, uint32_t* prewrap, uint32_t* curpixel_cvg, uint32_t curpixel_memcvg);
static int32_t normalize_dzpix(int32_t sum);
static int32_t CLIP(int32_t value,int32_t min,int32_t max);
static void tcdiv_persp(int32_t ss, int32_t st, int32_t sw, int32_t* sss, int32_t* sst);
static void tcdiv_nopersp(int32_t ss, int32_t st, int32_t sw, int32_t* sss, int32_t* sst);
static void tclod_1cycle_next(int32_t* sss, int32_t* sst, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t scanline, int32_t prim_tile, int32_t* t1, SPANSIGS* sigs, int32_t* prelodfrac);
static void tclod_2cycle_current(int32_t* sss, int32_t* sst, int32_t nexts, int32_t nextt, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t prim_tile, int32_t* t1, int32_t* t2);
static void tclod_2cycle_current_simple(int32_t* sss, int32_t* sst, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t prim_tile, int32_t* t1, int32_t* t2);
static void tclod_2cycle_current_notexel1(int32_t* sss, int32_t* sst, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t prim_tile, int32_t* t1);
static void tclod_2cycle_next(int32_t* sss, int32_t* sst, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t prim_tile, int32_t* t1, int32_t* t2, int32_t* prelodfrac);
static void tclod_copy(int32_t* sss, int32_t* sst, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t prim_tile, int32_t* t1);
void tclod_4x17_to_15(int32_t scurr, int32_t snext, int32_t tcurr, int32_t tnext, int32_t previous, int32_t* lod);
static void tclod_1cycle_current(int32_t* sss, int32_t* sst, int32_t nexts, int32_t nextt, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t scanline, int32_t prim_tile, int32_t* t1, SPANSIGS* sigs);
static void get_texel1_1cycle(int32_t* s1, int32_t* t1, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t scanline, SPANSIGS* sigs);
static void get_nexttexel0_2cycle(int32_t* s1, int32_t* t1, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc);
static void calculate_clamp_diffs(uint32_t tile);
static void calculate_tile_derivs(uint32_t tile);
static void rgbaz_correct_clip(int offx, int offy, int r, int g, int b, int a, int* z, uint32_t curpixel_cvg);
void deduce_derivatives(void);

static int32_t k0 = 0, k1 = 0, k2 = 0, k3 = 0, k4 = 0, k5 = 0;
static int32_t lod_frac = 0;
uint32_t DebugMode = 0, DebugMode2 = 0;
int debugcolor = 0;

static void (*fbfill_func[4])(uint32_t) =
{
  fbfill_4, fbfill_8, fbfill_16, fbfill_32
};

static void (*tcdiv_func[2])(int32_t, int32_t, int32_t, int32_t*, int32_t*) =
{
  tcdiv_nopersp, tcdiv_persp
};

static void (*render_spans_1cycle_func[3])(int, int, int, int) =
{
  render_spans_1cycle_notex, render_spans_1cycle_notexel1, render_spans_1cycle_complete
};

static void (*render_spans_2cycle_func[4])(int, int, int, int) =
{
  render_spans_2cycle_notex, render_spans_2cycle_notexel1, render_spans_2cycle_notexelnext, render_spans_2cycle_complete
};

void (*fbread1_ptr)(uint32_t, uint32_t*);
void (*fbread2_ptr)(uint32_t, uint32_t*);
void (*fbwrite_ptr)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
void (*fbfill_ptr)(uint32_t) = fbfill_4;
void (*get_dither_noise_ptr)(int, int, int*, int*);
static DitherFunc rgb_dither_ptr;
void (*tcdiv_ptr)(int32_t, int32_t, int32_t, int32_t*, int32_t*) = tcdiv_nopersp;
void (*render_spans_1cycle_ptr)(int, int, int, int) = render_spans_1cycle_complete;
void (*render_spans_2cycle_ptr)(int, int, int, int) = render_spans_2cycle_notexel1;

uint32_t *rdram;
static uint32_t* rsp_dmem;

/* TYLER: Temporary hack. */
static struct RDP *my_rdp;
static uint32_t *vi_width;

void RDPSetVIWidthPointer(uint32_t *vi_width_ptr) {
  vi_width = vi_width_ptr;
}

void RDPSetRDRAMPointer(uint8_t *rdram_ptr) {
  rdram = (uint32_t *) rdram_ptr;
  rdram_8 = (uint8_t*)rdram;
  rdram_16 = (uint16_t*)rdram;
}

void RDPSetRSPDMEMPointer(uint8_t *rsp_dmem_ptr) {
  rsp_dmem = (uint32_t *) rsp_dmem_ptr;
}

static uint16_t bswap16(uint16_t x) { return ((x << 8) & 0xFF00) | ((x >> 8) & 0x00FF); }
static uint32_t bswap32(uint32_t x) { return __builtin_bswap32(x); }

static uint32_t RREADIDX32(unsigned in) {
  assert(in <= 0x1FFFFF);
  return bswap32(rdram[in]);
}

#define PAIRREAD16(rdst,hdst,in) {assert(in <= 0x7FFFFE); \
  (rdst)=bswap16(rdram_16[in]); (hdst) = hidden_bits[in];}

#define PAIRWRITE16(in,rval,hval) {assert(in <= 0x7FFFFE); \
  rdram_16[in]=bswap16(rval); hidden_bits[in]=(hval);}

#define PAIRWRITE32(in,rval,hval0,hval1) {assert(in <= 0x7FFFFC); \
  rdram[in]=bswap32(rval); hidden_bits[(in)<<1]=(hval0); \
  hidden_bits[((in)<<1)+1]=(hval1); }

#define PAIRWRITE8(in,rval,hval) {assert(in <= 0x7FFFFF); \
  rdram_8[in]=(rval); if ((in) & 1) hidden_bits[(in)>>1]=(hval);}

struct onetime
{
  int ntscnolerp, copymstrangecrashes, fillmcrashes, fillmbitcrashes, syncfullcrash;
} onetimewarnings;

extern int32_t pitchindwords;

uint32_t z64gl_command = 0;
uint32_t command_counter = 0;
int SaveLoaded = 0;
uint32_t max_level = 0;
int32_t min_level = 0;
int32_t* PreScale;
uint32_t tvfadeoutstate[625];

static void tcmask(int32_t* S, int32_t* T, int32_t num);
static void tcmask(int32_t* S, int32_t* T, int32_t num)
{
  int32_t wrap;
  
  

  if (tile[num].mask_s)
  {
    if (tile[num].ms)
    {
      wrap = *S >> tile[num].f.masksclamped;
      wrap &= 1;
      *S ^= (-wrap);
    }
    *S &= maskbits_table[tile[num].mask_s];
  }

  if (tile[num].mask_t)
  {
    if (tile[num].mt)
    {
      wrap = *T >> tile[num].f.masktclamped;
      wrap &= 1;
      *T ^= (-wrap);
    }
    
    *T &= maskbits_table[tile[num].mask_t];
  }
}

static void tcmask_coupled(int32_t* S, int32_t* S1, int32_t* T, int32_t* T1, int32_t num);
static void tcmask_coupled(int32_t* S, int32_t* S1, int32_t* T, int32_t* T1, int32_t num)
{
  int32_t wrap;
  int32_t maskbits; 
  int32_t wrapthreshold; 

  if (tile[num].mask_s)
  {
    if (tile[num].ms)
    {
      wrapthreshold = tile[num].f.masksclamped;

      wrap = (*S >> wrapthreshold) & 1;
      *S ^= (-wrap);

      wrap = (*S1 >> wrapthreshold) & 1;
      *S1 ^= (-wrap);
    }

    maskbits = maskbits_table[tile[num].mask_s];
    *S &= maskbits;
    *S1 &= maskbits;
  }

  if (tile[num].mask_t)
  {
    if (tile[num].mt)
    {
      wrapthreshold = tile[num].f.masktclamped;

      wrap = (*T >> wrapthreshold) & 1;
      *T ^= (-wrap);

      wrap = (*T1 >> wrapthreshold) & 1;
      *T1 ^= (-wrap);
    }
    maskbits = maskbits_table[tile[num].mask_t];
    *T &= maskbits;
    *T1 &= maskbits;
  }
}

static void tcmask_copy(int32_t* S, int32_t* S1, int32_t* S2, int32_t* S3, int32_t* T, int32_t num);
static void tcmask_copy(int32_t* S, int32_t* S1, int32_t* S2, int32_t* S3, int32_t* T, int32_t num)
{
  int32_t wrap;
  int32_t maskbits_s; 
  int32_t swrapthreshold; 

  if (tile[num].mask_s)
  {
    if (tile[num].ms)
    {
      swrapthreshold = tile[num].f.masksclamped;

      wrap = (*S >> swrapthreshold) & 1;
      *S ^= (-wrap);

      wrap = (*S1 >> swrapthreshold) & 1;
      *S1 ^= (-wrap);

      wrap = (*S2 >> swrapthreshold) & 1;
      *S2 ^= (-wrap);

      wrap = (*S3 >> swrapthreshold) & 1;
      *S3 ^= (-wrap);
    }

    maskbits_s = maskbits_table[tile[num].mask_s];
    *S &= maskbits_s;
    *S1 &= maskbits_s;
    *S2 &= maskbits_s;
    *S3 &= maskbits_s;
  }

  if (tile[num].mask_t)
  {
    if (tile[num].mt)
    {
      wrap = *T >> tile[num].f.masktclamped; 
      wrap &= 1;
      *T ^= (-wrap);
    }

    *T &= maskbits_table[tile[num].mask_t];
  }
}

static void tcshift_cycle(int32_t* S, int32_t* T, int32_t* maxs, int32_t* maxt, uint32_t num)
{

  int32_t coord = *S;
  int32_t shifter = tile[num].shift_s;

  if (shifter < 11)
  {
    coord = SIGN16(coord);
    coord >>= shifter;
  }
  else
  {
    coord <<= (16 - shifter);
    coord = SIGN16(coord);
  }
  *S = coord; 

  

  
  *maxs = ((coord >> 3) >= tile[num].sh);
  
  

  coord = *T;
  shifter = tile[num].shift_t;

  if (shifter < 11)
  {
    coord = SIGN16(coord);
    coord >>= shifter;
  }
  else
  {
    coord <<= (16 - shifter);
    coord = SIGN16(coord);
  }
  *T = coord; 
  *maxt = ((coord >> 3) >= tile[num].th);
} 

static void tcshift_copy(int32_t* S, int32_t* T, uint32_t num)
{
  int32_t coord = *S;
  int32_t shifter = tile[num].shift_s;

  if (shifter < 11)
  {
    coord = SIGN16(coord);
    coord >>= shifter;
  }
  else
  {
    coord <<= (16 - shifter);
    coord = SIGN16(coord);
  }
  *S = coord; 

  coord = *T;
  shifter = tile[num].shift_t;

  if (shifter < 11)
  {
    coord = SIGN16(coord);
    coord >>= shifter;
  }
  else
  {
    coord <<= (16 - shifter);
    coord = SIGN16(coord);
  }
  *T = coord; 
  
}

static void tcclamp_cycle(int32_t* S, int32_t* T, int32_t* SFRAC, int32_t* TFRAC, int32_t maxs, int32_t maxt, int32_t num)
{

  int32_t locs = *S, loct = *T;
  if (tile[num].f.clampens)
  {
    if (!(locs & 0x10000))
    {
      if (!maxs)
        *S = (locs >> 5);
      else
      {
        *S = tile[num].f.clampdiffs;
        *SFRAC = 0;
      }
    }
    else
    {
      *S = 0;
      *SFRAC = 0;
    }
  }
  else
    *S = (locs >> 5);

  if (tile[num].f.clampent)
  {
    if (!(loct & 0x10000))
    {
      if (!maxt)
        *T = (loct >> 5);
      else
      {
        *T = tile[num].f.clampdifft;
        *TFRAC = 0;
      }
    }
    else
    {
      *T = 0;
      *TFRAC = 0;
    }
  }
  else
    *T = (loct >> 5);
}

static void tcclamp_cycle_light(int32_t* S, int32_t* T, int32_t maxs, int32_t maxt, int32_t num)
{
  int32_t locs = *S, loct = *T;
  if (tile[num].f.clampens)
  {
    if (!(locs & 0x10000))
    {
      if (!maxs)
        *S = (locs >> 5);
      else
        *S = tile[num].f.clampdiffs;
    }
    else
      *S = 0;
  }
  else
    *S = (locs >> 5);

  if (tile[num].f.clampent)
  {
    if (!(loct & 0x10000))
    {
      if (!maxt)
        *T = (loct >> 5);
      else
        *T = tile[num].f.clampdifft;
    }
    else
      *T = 0;
  }
  else
    *T = (loct >> 5);
}

int rdp_init()
{
  rgb_dither_ptr = DitherFuncLUT[0];
  get_dither_noise_ptr = DitherNoiseFuncLUT[0];
  fbread1_ptr = FBReadFuncLUT[0];
  fbread2_ptr = FBReadFunc2LUT[0];
  fbwrite_ptr = FBWriteFuncLUT[0];

  combiner_rgbsub_a_r[0] = combiner_rgbsub_a_r[1] = &one_color;
  combiner_rgbsub_a_g[0] = combiner_rgbsub_a_g[1] = &one_color;
  combiner_rgbsub_a_b[0] = combiner_rgbsub_a_b[1] = &one_color;
  combiner_rgbsub_b_r[0] = combiner_rgbsub_b_r[1] = &one_color;
  combiner_rgbsub_b_g[0] = combiner_rgbsub_b_g[1] = &one_color;
  combiner_rgbsub_b_b[0] = combiner_rgbsub_b_b[1] = &one_color;
  combiner_rgbmul_r[0] = combiner_rgbmul_r[1] = &one_color;
  combiner_rgbmul_g[0] = combiner_rgbmul_g[1] = &one_color;
  combiner_rgbmul_b[0] = combiner_rgbmul_b[1] = &one_color;
  combiner_rgbadd_r[0] = combiner_rgbadd_r[1] = &one_color;
  combiner_rgbadd_g[0] = combiner_rgbadd_g[1] = &one_color;
  combiner_rgbadd_b[0] = combiner_rgbadd_b[1] = &one_color;

  combiner_alphasub_a[0] = combiner_alphasub_a[1] = &one_color;
  combiner_alphasub_b[0] = combiner_alphasub_b[1] = &one_color;
  combiner_alphamul[0] = combiner_alphamul[1] = &one_color;
  combiner_alphaadd[0] = combiner_alphaadd[1] = &one_color;

  rdp_set_other_modes(0, 0);
  other_modes.f.stalederivs = 1;
  
  memset(TMEM, 0, 0x1000);

  memset(hidden_bits, 3, sizeof(hidden_bits));
  
  

  memset(tile, 0, sizeof(tile));
  
  for (int i = 0; i < 8; i++)
  {
    calculate_tile_derivs(i);
    calculate_clamp_diffs(i);
  }

  memset(&combined_color, 0, sizeof(COLOR));
  memset(&prim_color, 0, sizeof(COLOR));
  memset(&env_color, 0, sizeof(COLOR));
  memset(&key_scale, 0, sizeof(COLOR));
  memset(&key_center, 0, sizeof(COLOR));

  memset(&onetimewarnings, 0, sizeof(onetimewarnings));

  rdram_8 = (uint8_t*)rdram;
  rdram_16 = (uint16_t*)rdram;
  return 0;
}

static void SET_SUBA_RGB_INPUT(int32_t **input_r,
  int32_t **input_g, int32_t **input_b, int code) {

  static int32_t *red_inputs[16] align(64) = {
    &combined_color.r, &texel0_color.r, &texel1_color.r, &prim_color.r,
    &shade_color.r,    &env_color.r,    &one_color,      &noise,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
  };

  static int32_t *green_inputs[16] align(64) = {
    &combined_color.g, &texel0_color.g, &texel1_color.g, &prim_color.g,
    &shade_color.g,    &env_color.g,    &one_color,      &noise,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
  };

  static int32_t *blue_inputs[16] align(64) = {
    &combined_color.b, &texel0_color.b, &texel1_color.b, &prim_color.b,
    &shade_color.b,    &env_color.b,    &one_color,      &noise,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
  };

  code &= 0x0F;
  *input_r = red_inputs[code];
  *input_g = green_inputs[code];
  *input_b = blue_inputs[code];
}

static void SET_SUBB_RGB_INPUT(int32_t **input_r,
  int32_t **input_g, int32_t **input_b, int code) {

  static int32_t *red_inputs[16] align(64) = {
    &combined_color.r, &texel0_color.r, &texel1_color.r, &prim_color.r,
    &shade_color.r,    &env_color.r,    &key_center.r,   &k4,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
  };

  static int32_t *green_inputs[16] align(64) = {
    &combined_color.g, &texel0_color.g, &texel1_color.g, &prim_color.g,
    &shade_color.g,    &env_color.g,    &key_center.g,   &k4,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
  };

  static int32_t *blue_inputs[16] align(64) = {
    &combined_color.b, &texel0_color.b, &texel1_color.b, &prim_color.b,
    &shade_color.b,    &env_color.b,    &key_center.b,   &k4,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
    &zero_color,       &zero_color,     &zero_color,     &zero_color,
  };

  code &= 0x0F;
  *input_r = red_inputs[code];
  *input_g = green_inputs[code];
  *input_b = blue_inputs[code];
}

static void SET_MUL_RGB_INPUT(int32_t **input_r,
  int32_t **input_g, int32_t **input_b, int code) {

  static int32_t *red_inputs[32] align(64) = {
    &combined_color.r, &texel0_color.r, &texel1_color.r,     &prim_color.r,
    &shade_color.r,    &env_color.r,    &key_center.r,       &combined_color.a,
    &texel0_color.a,   &texel1_color.a, &prim_color.a,       &shade_color.a,
    &env_color.a,      &lod_frac,       &primitive_lod_frac, &k5,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
  };

  static int32_t *green_inputs[32] align(64) = {
    &combined_color.g, &texel0_color.g, &texel1_color.g,     &prim_color.g,
    &shade_color.g,    &env_color.g,    &key_center.g,       &combined_color.a,
    &texel0_color.a,   &texel1_color.a, &prim_color.a,       &shade_color.a,
    &env_color.a,      &lod_frac,       &primitive_lod_frac, &k5,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
  };

  static int32_t *blue_inputs[32] align(64) = {
    &combined_color.b, &texel0_color.b, &texel1_color.b,     &prim_color.b,
    &shade_color.b,    &env_color.b,    &key_center.b,       &combined_color.a,
    &texel0_color.a,   &texel1_color.a, &prim_color.a,       &shade_color.a,
    &env_color.a,      &lod_frac,       &primitive_lod_frac, &k5,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
    &zero_color,       &zero_color,     &zero_color,         &zero_color,
  };

  code &= 0x1F;
  *input_r = red_inputs[code];
  *input_g = green_inputs[code];
  *input_b = blue_inputs[code];
}
  
static void SET_ADD_RGB_INPUT(int32_t **input_r,
  int32_t **input_g, int32_t **input_b, int code) {

  static int32_t *red_inputs[8] align(64) = {
    &combined_color.r, &texel0_color.r, &texel1_color.r, &prim_color.r,
    &shade_color.r,    &env_color.r,    &one_color,      &zero_color,
  };

  static int32_t *green_inputs[8] align(64) = {
    &combined_color.g, &texel0_color.g, &texel1_color.g, &prim_color.g,
    &shade_color.g,    &env_color.g,    &one_color,      &zero_color,
  };

  static int32_t *blue_inputs[8] align(64) = {
    &combined_color.b, &texel0_color.b, &texel1_color.b, &prim_color.b,
    &shade_color.b,    &env_color.b,    &one_color,      &zero_color,
  };

  code &= 0x07;
  *input_r = red_inputs[code];
  *input_g = green_inputs[code];
  *input_b = blue_inputs[code];
}

static void SET_SUB_ALPHA_INPUT(int32_t **input, int code) {

  static int32_t *alpha_inputs[8] align(64) = {
    &combined_color.a,
    &texel0_color.a,
    &texel1_color.a,
    &prim_color.a,
    &shade_color.a,
    &env_color.a,
    &one_color,
    &zero_color,
  };

  code &= 0x07;
  *input = alpha_inputs[code];
}

static void SET_MUL_ALPHA_INPUT(int32_t **input, int code) {

  static int32_t *alpha_inputs[8] align(64) = {
    &lod_frac,
    &texel0_color.a,
    &texel1_color.a,
    &prim_color.a,
    &shade_color.a,
    &env_color.a,
    &primitive_lod_frac,
    &zero_color,
  };

  code &= 0x07;
  *input = alpha_inputs[code];
}

static void combiner_1cycle(int adseed, uint32_t* curpixel_cvg)
{

  int32_t redkey, greenkey, bluekey, temp;

  

  
  combined_color.r = color_combiner_equation(*combiner_rgbsub_a_r[1],*combiner_rgbsub_b_r[1],*combiner_rgbmul_r[1],*combiner_rgbadd_r[1]);
  combined_color.g = color_combiner_equation(*combiner_rgbsub_a_g[1],*combiner_rgbsub_b_g[1],*combiner_rgbmul_g[1],*combiner_rgbadd_g[1]);
  combined_color.b = color_combiner_equation(*combiner_rgbsub_a_b[1],*combiner_rgbsub_b_b[1],*combiner_rgbmul_b[1],*combiner_rgbadd_b[1]);
  combined_color.a = alpha_combiner_equation(*combiner_alphasub_a[1],*combiner_alphasub_b[1],*combiner_alphamul[1],*combiner_alphaadd[1]);

  pixel_color.a = special_9bit_clamptable[combined_color.a];
  if (pixel_color.a == 0xff)
    pixel_color.a = 0x100;

  if (!other_modes.key_en)
  {
    
    combined_color.r >>= 8;
    combined_color.g >>= 8;
    combined_color.b >>= 8;
    pixel_color.r = special_9bit_clamptable[combined_color.r];
    pixel_color.g = special_9bit_clamptable[combined_color.g];
    pixel_color.b = special_9bit_clamptable[combined_color.b];
  }
  else
  {
    redkey = SIGN(combined_color.r, 17);
    if (redkey >= 0)
      redkey = (key_width.r << 4) - redkey;
    else
      redkey = (key_width.r << 4) + redkey;
    greenkey = SIGN(combined_color.g, 17);
    if (greenkey >= 0)
      greenkey = (key_width.g << 4) - greenkey;
    else
      greenkey = (key_width.g << 4) + greenkey;
    bluekey = SIGN(combined_color.b, 17);
    if (bluekey >= 0)
      bluekey = (key_width.b << 4) - bluekey;
    else
      bluekey = (key_width.b << 4) + bluekey;
    keyalpha = (redkey < greenkey) ? redkey : greenkey;
    keyalpha = (bluekey < keyalpha) ? bluekey : keyalpha;
    keyalpha = CLIP(keyalpha, 0, 0xff);

    
    pixel_color.r = special_9bit_clamptable[*combiner_rgbsub_a_r[1]];
    pixel_color.g = special_9bit_clamptable[*combiner_rgbsub_a_g[1]];
    pixel_color.b = special_9bit_clamptable[*combiner_rgbsub_a_b[1]];

    
    combined_color.r >>= 8;
    combined_color.g >>= 8;
    combined_color.b >>= 8;
  }
  
  
  if (other_modes.cvg_times_alpha)
  {
    temp = (pixel_color.a * (*curpixel_cvg) + 4) >> 3;
    *curpixel_cvg = (temp >> 5) & 0xf;
  }

  if (!other_modes.alpha_cvg_select)
  { 
    if (!other_modes.key_en)
    {
      pixel_color.a += adseed;
      if (pixel_color.a & 0x100)
        pixel_color.a = 0xff;
    }
    else
      pixel_color.a = keyalpha;
  }
  else
  {
    if (other_modes.cvg_times_alpha)
      pixel_color.a = temp;
    else
      pixel_color.a = (*curpixel_cvg) << 5;
    if (pixel_color.a > 0xff)
      pixel_color.a = 0xff;
  }
  

  shade_color.a += adseed;
  if (shade_color.a & 0x100)
    shade_color.a = 0xff;
}

static void combiner_2cycle(int adseed, uint32_t* curpixel_cvg)
{
  int32_t redkey, greenkey, bluekey, temp;

  combined_color.r = color_combiner_equation(*combiner_rgbsub_a_r[0],*combiner_rgbsub_b_r[0],*combiner_rgbmul_r[0],*combiner_rgbadd_r[0]);
  combined_color.g = color_combiner_equation(*combiner_rgbsub_a_g[0],*combiner_rgbsub_b_g[0],*combiner_rgbmul_g[0],*combiner_rgbadd_g[0]);
  combined_color.b = color_combiner_equation(*combiner_rgbsub_a_b[0],*combiner_rgbsub_b_b[0],*combiner_rgbmul_b[0],*combiner_rgbadd_b[0]);
  combined_color.a = alpha_combiner_equation(*combiner_alphasub_a[0],*combiner_alphasub_b[0],*combiner_alphamul[0],*combiner_alphaadd[0]);

  
  

  
  combined_color.r >>= 8;
  combined_color.g >>= 8;
  combined_color.b >>= 8;

  
  texel0_color = texel1_color;
  texel1_color = nexttexel_color;

  
  
  
  
  
  
  

  combined_color.r = color_combiner_equation(*combiner_rgbsub_a_r[1],*combiner_rgbsub_b_r[1],*combiner_rgbmul_r[1],*combiner_rgbadd_r[1]);
  combined_color.g = color_combiner_equation(*combiner_rgbsub_a_g[1],*combiner_rgbsub_b_g[1],*combiner_rgbmul_g[1],*combiner_rgbadd_g[1]);
  combined_color.b = color_combiner_equation(*combiner_rgbsub_a_b[1],*combiner_rgbsub_b_b[1],*combiner_rgbmul_b[1],*combiner_rgbadd_b[1]);
  combined_color.a = alpha_combiner_equation(*combiner_alphasub_a[1],*combiner_alphasub_b[1],*combiner_alphamul[1],*combiner_alphaadd[1]);

  if (!other_modes.key_en)
  {
    
    combined_color.r >>= 8;
    combined_color.g >>= 8;
    combined_color.b >>= 8;

    pixel_color.r = special_9bit_clamptable[combined_color.r];
    pixel_color.g = special_9bit_clamptable[combined_color.g];
    pixel_color.b = special_9bit_clamptable[combined_color.b];
  }
  else
  {
    redkey = SIGN(combined_color.r, 17);
    if (redkey >= 0)
      redkey = (key_width.r << 4) - redkey;
    else
      redkey = (key_width.r << 4) + redkey;
    greenkey = SIGN(combined_color.g, 17);
    if (greenkey >= 0)
      greenkey = (key_width.g << 4) - greenkey;
    else
      greenkey = (key_width.g << 4) + greenkey;
    bluekey = SIGN(combined_color.b, 17);
    if (bluekey >= 0)
      bluekey = (key_width.b << 4) - bluekey;
    else
      bluekey = (key_width.b << 4) + bluekey;
    keyalpha = (redkey < greenkey) ? redkey : greenkey;
    keyalpha = (bluekey < keyalpha) ? bluekey : keyalpha;
    keyalpha = CLIP(keyalpha, 0, 0xff);

    
    pixel_color.r = special_9bit_clamptable[*combiner_rgbsub_a_r[1]];
    pixel_color.g = special_9bit_clamptable[*combiner_rgbsub_a_g[1]];
    pixel_color.b = special_9bit_clamptable[*combiner_rgbsub_a_b[1]];

    
    combined_color.r >>= 8;
    combined_color.g >>= 8;
    combined_color.b >>= 8;
  }
  
  pixel_color.a = special_9bit_clamptable[combined_color.a];
  if (pixel_color.a == 0xff)
    pixel_color.a = 0x100;

  
  if (other_modes.cvg_times_alpha)
  {
    temp = (pixel_color.a * (*curpixel_cvg) + 4) >> 3;
    *curpixel_cvg = (temp >> 5) & 0xf;
  }

  if (!other_modes.alpha_cvg_select)
  {
    if (!other_modes.key_en)
    {
      pixel_color.a += adseed;
      if (pixel_color.a & 0x100)
        pixel_color.a = 0xff;
    }
    else
      pixel_color.a = keyalpha;
  }
  else
  {
    if (other_modes.cvg_times_alpha)
      pixel_color.a = temp;
    else
      pixel_color.a = (*curpixel_cvg) << 5;
    if (pixel_color.a > 0xff)
      pixel_color.a = 0xff;
  }
  

  shade_color.a += adseed;
  if (shade_color.a & 0x100)
    shade_color.a = 0xff;
}

static void SET_BLENDER_INPUT(int cycle, int which, int32_t **input_r, int32_t **input_g, int32_t **input_b, int32_t **input_a, int a, int b)
{

  switch (a & 0x3)
  {
    case 0:
    {
      if (cycle == 0)
      {
        *input_r = &pixel_color.r;
        *input_g = &pixel_color.g;
        *input_b = &pixel_color.b;
      }
      else
      {
        *input_r = &blended_pixel_color.r;
        *input_g = &blended_pixel_color.g;
        *input_b = &blended_pixel_color.b;
      }
      break;
    }

    case 1:
    {
      *input_r = &memory_color.r;
      *input_g = &memory_color.g;
      *input_b = &memory_color.b;
      break;
    }

    case 2:
    {
      *input_r = &blend_color.r;    *input_g = &blend_color.g;    *input_b = &blend_color.b;
      break;
    }

    case 3:
    {
      *input_r = &fog_color.r;    *input_g = &fog_color.g;    *input_b = &fog_color.b;
      break;
    }
  }

  if (which == 0)
  {
    switch (b & 0x3)
    {
      case 0:   *input_a = &pixel_color.a; break;
      case 1:   *input_a = &fog_color.a; break;
      case 2:   *input_a = &shade_color.a; break;
      case 3:   *input_a = &zero_color; break;
    }
  }
  else
  {
    switch (b & 0x3)
    {
      case 0:   *input_a = &inv_pixel_color.a; break;
      case 1:   *input_a = &memory_color.a; break;
      case 2:   *input_a = &blenderone; break;
      case 3:   *input_a = &zero_color; break;
    }
  }
}

static int blender_1cycle(uint32_t* fr, uint32_t* fg, uint32_t* fb, int dith, uint32_t blend_en, uint32_t prewrap, uint32_t curpixel_cvg, uint32_t curpixel_cvbit)
{
  int r, g, b, dontblend;
  
  
  if (alpha_compare(pixel_color.a))
  {

    

    
    
    
    if (other_modes.antialias_en ? (curpixel_cvg) : (curpixel_cvbit))
    {

      if (!other_modes.color_on_cvg || prewrap)
      {
        dontblend = (other_modes.f.partialreject_1cycle && pixel_color.a >= 0xff);
        if (!blend_en || dontblend)
        {
          r = *blender1a_r[0];
          g = *blender1a_g[0];
          b = *blender1a_b[0];
        }
        else
        {
          inv_pixel_color.a =  (~(*blender1b_a[0])) & 0xff;
          
          
          
          

          blender_equation_cycle0(&r, &g, &b);
        }
      }
      else
      {
        r = *blender2a_r[0];
        g = *blender2a_g[0];
        b = *blender2a_b[0];
      }

      rgb_dither_ptr(&r, &g, &b, dith);
      *fr = r;
      *fg = g;
      *fb = b;
      return 1;
    }
    else 
      return 0;
    }
  else 
    return 0;
}

static int blender_2cycle(uint32_t* fr, uint32_t* fg, uint32_t* fb, int dith, uint32_t blend_en, uint32_t prewrap, uint32_t curpixel_cvg, uint32_t curpixel_cvbit)
{
  int r, g, b, dontblend;

  
  if (alpha_compare(pixel_color.a))
  {
    if (other_modes.antialias_en ? (curpixel_cvg) : (curpixel_cvbit))
    {
      
      inv_pixel_color.a =  (~(*blender1b_a[0])) & 0xff;

      blender_equation_cycle0_2(&r, &g, &b);

      
      memory_color = pre_memory_color;

      blended_pixel_color.r = r;
      blended_pixel_color.g = g;
      blended_pixel_color.b = b;
      blended_pixel_color.a = pixel_color.a;

      if (!other_modes.color_on_cvg || prewrap)
      {
        dontblend = (other_modes.f.partialreject_2cycle && pixel_color.a >= 0xff);
        if (!blend_en || dontblend)
        {
          r = *blender1a_r[1];
          g = *blender1a_g[1];
          b = *blender1a_b[1];
        }
        else
        {
          inv_pixel_color.a =  (~(*blender1b_a[1])) & 0xff;
          blender_equation_cycle1(&r, &g, &b);
        }
      }
      else
      {
        r = *blender2a_r[1];
        g = *blender2a_g[1];
        b = *blender2a_b[1];
      }

      
      rgb_dither_ptr(&r, &g, &b, dith);
      *fr = r;
      *fg = g;
      *fb = b;
      return 1;
    }
    else 
      return 0;
  }
  else 
    return 0;
}

static void fetch_texel(COLOR *color, int s, int t, uint32_t tilenum)
{
  uint32_t tbase = tile[tilenum].line * t + tile[tilenum].tmem;
  

  uint32_t tpal = tile[tilenum].palette;

  
  
  
  
  
  
  
  uint16_t *tc16 = (uint16_t*)TMEM;
  uint32_t taddr = 0;

  

  

  switch (tile[tilenum].f.notlutswitch)
  {
  case TEXEL_RGBA4:
    {
      taddr = ((tbase << 4) + s) >> 1;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      uint8_t byteval, c; 

      byteval = TMEM[taddr & 0xfff];
      c = ((s & 1)) ? (byteval & 0xf) : (byteval >> 4);
      c |= (c << 4);
      color->r = c;
      color->g = c;
      color->b = c;
      color->a = c;
    }
    break;
  case TEXEL_RGBA8:
    {
      taddr = (tbase << 3) + s;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      
      uint8_t p;

      p = TMEM[taddr & 0xfff];
      color->r = p;
      color->g = p;
      color->b = p;
      color->a = p;
    }
    break;
  case TEXEL_RGBA16:
    {
      taddr = (tbase << 2) + s;
      taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
      
                
      uint16_t c;
          
      c = tc16[taddr & 0x7ff];
      color->r = GET_HI_RGBA16_TMEM(c);
      color->g = GET_MED_RGBA16_TMEM(c);
      color->b = GET_LOW_RGBA16_TMEM(c);
      color->a = (c & 1) ? 0xff : 0;
    }
    break;
  case TEXEL_RGBA32:
    {
      
      
      
      taddr = (tbase << 2) + s;
      taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
          
      uint16_t c;
          
      
      taddr &= 0x3ff;
      c = tc16[taddr];
      color->r = c >> 8;
      color->g = c & 0xff;
      c = tc16[taddr | 0x400];
      color->b = c >> 8;
      color->a = c & 0xff;
    }
    break;
  case TEXEL_YUV4:
  case TEXEL_YUV8:
    {
      taddr = (tbase << 3) + s;
      int taddrlow = taddr >> 1;

      taddrlow ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);

      taddrlow &= 0x3ff;
          
      uint16_t c = tc16[taddrlow];
          
      int32_t u, save;
      
      save = u = c >> 8;
      
      u ^= 0x80;
      if (u & 0x80)
        u |= 0x100;

      color->r = u;
      color->g = u;
      color->b = save;
      color->a = save;
    }
    break;
  case TEXEL_YUV16:
  case TEXEL_YUV32:
    {
      taddr = (tbase << 3) + s;
      int taddrlow = taddr >> 1;

      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      taddrlow ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
          
      taddr &= 0x7ff;
      taddrlow &= 0x3ff;
          
      uint16_t c = tc16[taddrlow];
          
      int32_t y, u, v;
      y = TMEM[taddr | 0x800];
      u = c >> 8;
      v = c & 0xff;

      v ^= 0x80; u ^= 0x80;
      if (v & 0x80)
        v |= 0x100;
      if (u & 0x80)
        u |= 0x100;
      
      

      color->r = u;
      color->g = v;
      color->b = y;
      color->a = y;
    }
    break;
  case TEXEL_CI4:
    {
      taddr = ((tbase << 4) + s) >> 1;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      
      uint8_t p;

      
      
      p = TMEM[taddr & 0xfff];
      p = (s & 1) ? (p & 0xf) : (p >> 4);
      p = (tpal << 4) | p;
      color->r = color->g = color->b = color->a = p;
    }
    break;
  case TEXEL_CI8:
    {
      taddr = (tbase << 3) + s;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      
      uint8_t p;

      
      p = TMEM[taddr & 0xfff];
      color->r = p;
      color->g = p;
      color->b = p;
      color->a = p;
    }
    break;
  case TEXEL_CI16:
    {
      taddr = (tbase << 2) + s;
      taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
                
      uint16_t c;
          
      c = tc16[taddr & 0x7ff];
      color->r = c >> 8;
      color->g = c & 0xff;
      color->b = color->r;
      color->a = (c & 1) ? 0xff : 0;
    }
    break;
  case TEXEL_CI32:
    {
      taddr = (tbase << 2) + s;
      taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
          
      uint16_t c;
          
      c = tc16[taddr & 0x7ff];
      color->r = c >> 8;
      color->g = c & 0xff;
      color->b = color->r;
      color->a = (c & 1) ? 0xff : 0;
      
    }
        break;
  case TEXEL_IA4:
    {
      taddr = ((tbase << 4) + s) >> 1;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      
      uint8_t p, i; 
          
      
      p = TMEM[taddr & 0xfff];
      p = (s & 1) ? (p & 0xf) : (p >> 4);
      i = p & 0xe;
      i = (i << 4) | (i << 1) | (i >> 2);
      color->r = i;
      color->g = i;
      color->b = i;
      color->a = (p & 0x1) ? 0xff : 0;
    }
    break;
  case TEXEL_IA8:
    {
      taddr = (tbase << 3) + s;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      
      uint8_t p, i;

      
      p = TMEM[taddr & 0xfff];
      i = p & 0xf0;
      i |= (i >> 4);
      color->r = i;
      color->g = i;
      color->b = i;
      color->a = ((p & 0xf) << 4) | (p & 0xf);
    }
    break;
  case TEXEL_IA16:
    {
    
    
      taddr = (tbase << 2) + s;
      taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
      
      uint16_t c; 
                    
      c = tc16[taddr & 0x7ff];
      color->r = color->g = color->b = (c >> 8);
      color->a = c & 0xff;
    }
    break;
  case TEXEL_IA32:
    {
      taddr = (tbase << 2) + s;
      taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
          
      uint16_t c;
          
      c = tc16[taddr & 0x7ff];
      color->r = c >> 8;
      color->g = c & 0xff;
      color->b = color->r;
      color->a = (c & 1) ? 0xff : 0;
    }
    break;
  case TEXEL_I4:
    {
      taddr = ((tbase << 4) + s) >> 1;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      
      uint8_t byteval, c; 
                          
      byteval = TMEM[taddr & 0xfff];
      c = (s & 1) ? (byteval & 0xf) : (byteval >> 4);
      c |= (c << 4);
      color->r = c;
      color->g = c;
      color->b = c;
      color->a = c;
    }
    break;
  case TEXEL_I8:
    {
      taddr = (tbase << 3) + s;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      
      uint8_t c; 

      c = TMEM[taddr & 0xfff];
      color->r = c;
      color->g = c;
      color->b = c;
      color->a = c;
    }
    break;
  case TEXEL_I16:
    {
      taddr = (tbase << 2) + s;
      taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
                
      uint16_t c;
          
      c = tc16[taddr & 0x7ff];
      color->r = c >> 8;
      color->g = c & 0xff;
      color->b = color->r;
      color->a = (c & 1) ? 0xff : 0;
    }
    break;
  case TEXEL_I32:
    {
      taddr = (tbase << 2) + s;
      taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
          
      uint16_t c;
          
      c = tc16[taddr & 0x7ff];
      color->r = c >> 8;
      color->g = c & 0xff;
      color->b = color->r;
      color->a = (c & 1) ? 0xff : 0;
    }
    break;
  default:
    fatalerror("fetch_texel: unknown texture format %d, size %d, tilenum %d\n", tile[tilenum].format, tile[tilenum].size, tilenum);
    break;
  }
}

static void fetch_texel_entlut(COLOR *color, int s, int t, uint32_t tilenum)
{
  uint32_t tbase = tile[tilenum].line * t + tile[tilenum].tmem;
  uint32_t tpal = tile[tilenum].palette << 4;
  uint16_t *tc16 = (uint16_t*)TMEM;
  uint32_t taddr = 0;
  uint32_t c;

  
  
  switch(tile[tilenum].f.tlutswitch)
  {
  case 0:
  case 1:
  case 2:
    {
      taddr = ((tbase << 4) + s) >> 1;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      c = TMEM[taddr & 0x7ff];
      c = (s & 1) ? (c & 0xf) : (c >> 4);
      c = tlut[((tpal | c) << 2) ^ WORD_ADDR_XOR];
    }
    break;
  case 3:
    {
      taddr = (tbase << 3) + s;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      c = TMEM[taddr & 0x7ff];
      c = (s & 1) ? (c & 0xf) : (c >> 4);
      c = tlut[((tpal | c) << 2) ^ WORD_ADDR_XOR];
    }
    break;
  case 4:
  case 5:
  case 6:
  case 7:
    {
      taddr = (tbase << 3) + s;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      c = TMEM[taddr & 0x7ff];
      c = tlut[(c << 2) ^ WORD_ADDR_XOR];
    }
    break;
  case 8:
  case 9:
  case 10:
    {
      taddr = (tbase << 2) + s;
      taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
      c = tc16[taddr & 0x3ff];
      c = tlut[((c >> 6) & ~3) ^ WORD_ADDR_XOR];
      
    }
    break;
  case 11:
    {
      taddr = (tbase << 3) + s;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      c = TMEM[taddr & 0x7ff];
      c = tlut[(c << 2) ^ WORD_ADDR_XOR];
    }
    break;
  case 12:
  case 13:
  case 14:
    {
      taddr = (tbase << 2) + s;
      taddr ^= ((t & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR);
      c = tc16[taddr & 0x3ff];
      c = tlut[((c >> 6) & ~3) ^ WORD_ADDR_XOR];
    }
    break;
  case 15:
    {
      taddr = (tbase << 3) + s;
      taddr ^= ((t & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR);
      c = TMEM[taddr & 0x7ff];
      c = tlut[(c << 2) ^ WORD_ADDR_XOR];
    }
    break;
  default:
    fatalerror("fetch_texel_entlut: unknown texture format %d, size %d, tilenum %d\n", tile[tilenum].format, tile[tilenum].size, tilenum);
    break;
  }

  if (!other_modes.tlut_type)
  {
    color->r = GET_HI_RGBA16_TMEM(c);
    color->g = GET_MED_RGBA16_TMEM(c);
    color->b = GET_LOW_RGBA16_TMEM(c);
    color->a = (c & 1) ? 0xff : 0;
  }
  else
  {
    color->r = color->g = color->b = c >> 8;
    color->a = c & 0xff;
  }

}

static void fetch_texel_quadro(COLOR *color0, COLOR *color1, COLOR *color2, COLOR *color3, int s0, int s1, int t0, int t1, uint32_t tilenum)
{

  uint32_t tbase0 = tile[tilenum].line * t0 + tile[tilenum].tmem;
  uint32_t tbase2 = tile[tilenum].line * t1 + tile[tilenum].tmem;
  uint32_t tpal = tile[tilenum].palette;
  uint32_t xort = 0, ands = 0;

  
  

  uint16_t *tc16 = (uint16_t*)TMEM;
  uint32_t taddr0 = 0, taddr1 = 0, taddr2 = 0, taddr3 = 0;
  uint32_t taddrlow0 = 0, taddrlow1 = 0, taddrlow2 = 0, taddrlow3 = 0;

  switch (tile[tilenum].f.notlutswitch)
  {
  case TEXEL_RGBA4:
    {
      taddr0 = ((tbase0 << 4) + s0) >> 1;
      taddr1 = ((tbase0 << 4) + s1) >> 1;
      taddr2 = ((tbase2 << 4) + s0) >> 1;
      taddr3 = ((tbase2 << 4) + s1) >> 1;
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      
      uint32_t byteval, c; 
                          
      taddr0 &= 0xfff;
      taddr1 &= 0xfff;
      taddr2 &= 0xfff;
      taddr3 &= 0xfff;
      ands = s0 & 1;
      byteval = TMEM[taddr0];
      c = (ands) ? (byteval & 0xf) : (byteval >> 4);
      c |= (c << 4);
      color0->r = c;
      color0->g = c;
      color0->b = c;
      color0->a = c;
      byteval = TMEM[taddr2];
      c = (ands) ? (byteval & 0xf) : (byteval >> 4);
      c |= (c << 4);
      color2->r = c;
      color2->g = c;
      color2->b = c;
      color2->a = c;

      ands = s1 & 1;
      byteval = TMEM[taddr1];
      c = (ands) ? (byteval & 0xf) : (byteval >> 4);
      c |= (c << 4);
      color1->r = c;
      color1->g = c;
      color1->b = c;
      color1->a = c;
      byteval = TMEM[taddr3];
      c = (ands) ? (byteval & 0xf) : (byteval >> 4);
      c |= (c << 4);
      color3->r = c;
      color3->g = c;
      color3->b = c;
      color3->a = c;
    }
    break;
  case TEXEL_RGBA8:
    {
      taddr0 = ((tbase0 << 3) + s0);
      taddr1 = ((tbase0 << 3) + s1);
      taddr2 = ((tbase2 << 3) + s0);
      taddr3 = ((tbase2 << 3) + s1);
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      
      uint32_t p; 
      
      taddr0 &= 0xfff;
      taddr1 &= 0xfff;
      taddr2 &= 0xfff;
      taddr3 &= 0xfff;
      p = TMEM[taddr0];
      color0->r = p;
      color0->g = p;
      color0->b = p;
      color0->a = p;
      p = TMEM[taddr2];
      color2->r = p;
      color2->g = p;
      color2->b = p;
      color2->a = p;
      p = TMEM[taddr1];
      color1->r = p;
      color1->g = p;
      color1->b = p;
      color1->a = p;
      p = TMEM[taddr3];
      color3->r = p;
      color3->g = p;
      color3->b = p;
      color3->a = p;
    }
    break;
  case TEXEL_RGBA16:
    {
      taddr0 = ((tbase0 << 2) + s0);
      taddr1 = ((tbase0 << 2) + s1);
      taddr2 = ((tbase2 << 2) + s0);
      taddr3 = ((tbase2 << 2) + s1);
      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
          
      uint32_t c0, c1, c2, c3;
          
      taddr0 &= 0x7ff;
      taddr1 &= 0x7ff;
      taddr2 &= 0x7ff;
      taddr3 &= 0x7ff;
      c0 = tc16[taddr0];
      c1 = tc16[taddr1];
      c2 = tc16[taddr2];
      c3 = tc16[taddr3];
      color0->r = GET_HI_RGBA16_TMEM(c0);
      color0->g = GET_MED_RGBA16_TMEM(c0);
      color0->b = GET_LOW_RGBA16_TMEM(c0);
      color0->a = (c0 & 1) ? 0xff : 0;
      color1->r = GET_HI_RGBA16_TMEM(c1);
      color1->g = GET_MED_RGBA16_TMEM(c1);
      color1->b = GET_LOW_RGBA16_TMEM(c1);
      color1->a = (c1 & 1) ? 0xff : 0;
      color2->r = GET_HI_RGBA16_TMEM(c2);
      color2->g = GET_MED_RGBA16_TMEM(c2);
      color2->b = GET_LOW_RGBA16_TMEM(c2);
      color2->a = (c2 & 1) ? 0xff : 0;
      color3->r = GET_HI_RGBA16_TMEM(c3);
      color3->g = GET_MED_RGBA16_TMEM(c3);
      color3->b = GET_LOW_RGBA16_TMEM(c3);
      color3->a = (c3 & 1) ? 0xff : 0;
    }
    break;
  case TEXEL_RGBA32:
    {
      taddr0 = ((tbase0 << 2) + s0);
      taddr1 = ((tbase0 << 2) + s1);
      taddr2 = ((tbase2 << 2) + s0);
      taddr3 = ((tbase2 << 2) + s1);
      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
          
      uint16_t c0, c1, c2, c3;

      taddr0 &= 0x3ff;
      taddr1 &= 0x3ff;
      taddr2 &= 0x3ff;
      taddr3 &= 0x3ff;
      c0 = tc16[taddr0];
      color0->r = c0 >> 8;
      color0->g = c0 & 0xff;
      c0 = tc16[taddr0 | 0x400];
      color0->b = c0 >>  8;
      color0->a = c0 & 0xff;
      c1 = tc16[taddr1];
      color1->r = c1 >> 8;
      color1->g = c1 & 0xff;
      c1 = tc16[taddr1 | 0x400];
      color1->b = c1 >>  8;
      color1->a = c1 & 0xff;
      c2 = tc16[taddr2];
      color2->r = c2 >> 8;
      color2->g = c2 & 0xff;
      c2 = tc16[taddr2 | 0x400];
      color2->b = c2 >>  8;
      color2->a = c2 & 0xff;
      c3 = tc16[taddr3];
      color3->r = c3 >> 8;
      color3->g = c3 & 0xff;
      c3 = tc16[taddr3 | 0x400];
      color3->b = c3 >>  8;
      color3->a = c3 & 0xff;
    }
    break;
  case TEXEL_YUV4:
  case TEXEL_YUV8:
    {
      taddr0 = ((tbase0 << 3) + s0);
      taddr1 = ((tbase0 << 3) + s1);
      taddr2 = ((tbase2 << 3) + s0);
      taddr3 = ((tbase2 << 3) + s1);
      taddrlow0 = taddr0 >> 1;
      taddrlow1 = taddr1 >> 1;
      taddrlow2 = taddr2 >> 1;
      taddrlow3 = taddr3 >> 1;

      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddrlow0 ^= xort;
      taddrlow1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddrlow2 ^= xort;
      taddrlow3 ^= xort;

      taddrlow0 &= 0x3ff;
      taddrlow1 &= 0x3ff;
      taddrlow2 &= 0x3ff;
      taddrlow3 &= 0x3ff;

      uint16_t c0, c1, c2, c3;
      int32_t u0, u1, u2, u3, save0, save1, save2, save3;

      c0 = tc16[taddrlow0];
      c1 = tc16[taddrlow1];
      c2 = tc16[taddrlow2];
      c3 = tc16[taddrlow3];

      save0 = u0 = c0 >> 8;
      u0 ^= 0x80;
      if (u0 & 0x80)
        u0 |= 0x100;
      save1 = u1 = c1 >> 8;
      u1 ^= 0x80;
      if (u1 & 0x80)
        u1 |= 0x100;
      save2 = u2 = c2 >> 8;
      u2 ^= 0x80;
      if (u2 & 0x80)
        u2 |= 0x100;
      save3 = u3 = c3 >> 8;
      u3 ^= 0x80;
      if (u3 & 0x80)
        u3 |= 0x100;

      color0->r = u0;
      color0->g = u0;
      color0->b = save0;
      color0->a = save0;
      color1->r = u1;
      color1->g = u1;
      color1->b = save1;
      color1->a = save1;
      color2->r = u2;
      color2->g = u2;
      color2->b = save2;
      color2->a = save2;
      color3->r = u3;
      color3->g = u3;
      color3->b = save3;
      color3->a = save3;
    }
    break;
  case TEXEL_YUV16:
  case TEXEL_YUV32:
    {
      taddr0 = ((tbase0 << 3) + s0);
      taddr1 = ((tbase0 << 3) + s1);
      taddr2 = ((tbase2 << 3) + s0);
      taddr3 = ((tbase2 << 3) + s1);
      taddrlow0 = taddr0 >> 1;
      taddrlow1 = taddr1 >> 1;
      taddrlow2 = taddr2 >> 1;
      taddrlow3 = taddr3 >> 1;

      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddrlow0 ^= xort;
      taddrlow1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddrlow2 ^= xort;
      taddrlow3 ^= xort;

      taddr0 &= 0x7ff;
      taddr1 &= 0x7ff;
      taddr2 &= 0x7ff;
      taddr3 &= 0x7ff;
      taddrlow0 &= 0x3ff;
      taddrlow1 &= 0x3ff;
      taddrlow2 &= 0x3ff;
      taddrlow3 &= 0x3ff;

      uint16_t c0, c1, c2, c3;
      int32_t y0, y1, y2, y3, u0, u1, u2, u3, v0, v1, v2, v3;

      c0 = tc16[taddrlow0];
      c1 = tc16[taddrlow1];
      c2 = tc16[taddrlow2];
      c3 = tc16[taddrlow3];         
      
      y0 = TMEM[taddr0 | 0x800];
      u0 = c0 >> 8;
      v0 = c0 & 0xff;
      y1 = TMEM[taddr1 | 0x800];
      u1 = c1 >> 8;
      v1 = c1 & 0xff;
      y2 = TMEM[taddr2 | 0x800];
      u2 = c2 >> 8;
      v2 = c2 & 0xff;
      y3 = TMEM[taddr3 | 0x800];
      u3 = c3 >> 8;
      v3 = c3 & 0xff;

      v0 ^= 0x80; u0 ^= 0x80;
      if (v0 & 0x80)
        v0 |= 0x100;
      if (u0 & 0x80)
        u0 |= 0x100;
      v1 ^= 0x80; u1 ^= 0x80;
      if (v1 & 0x80)
        v1 |= 0x100;
      if (u1 & 0x80)
        u1 |= 0x100;
      v2 ^= 0x80; u2 ^= 0x80;
      if (v2 & 0x80)
        v2 |= 0x100;
      if (u2 & 0x80)
        u2 |= 0x100;
      v3 ^= 0x80; u3 ^= 0x80;
      if (v3 & 0x80)
        v3 |= 0x100;
      if (u3 & 0x80)
        u3 |= 0x100;

      color0->r = u0;
      color0->g = v0;
      color0->b = y0;
      color0->a = y0;
      color1->r = u1;
      color1->g = v1;
      color1->b = y1;
      color1->a = y1;
      color2->r = u2;
      color2->g = v2;
      color2->b = y2;
      color2->a = y2;
      color3->r = u3;
      color3->g = v3;
      color3->b = y3;
      color3->a = y3;
    }
    break;
  case TEXEL_CI4:
    {
      taddr0 = ((tbase0 << 4) + s0) >> 1;
      taddr1 = ((tbase0 << 4) + s1) >> 1;
      taddr2 = ((tbase2 << 4) + s0) >> 1;
      taddr3 = ((tbase2 << 4) + s1) >> 1;
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      
      uint32_t p;
                              
      taddr0 &= 0xfff;
      taddr1 &= 0xfff;
      taddr2 &= 0xfff;
      taddr3 &= 0xfff;
      ands = s0 & 1;
      p = TMEM[taddr0];
      p = (ands) ? (p & 0xf) : (p >> 4);
      p = (tpal << 4) | p;
      color0->r = color0->g = color0->b = color0->a = p;
      p = TMEM[taddr2];
      p = (ands) ? (p & 0xf) : (p >> 4);
      p = (tpal << 4) | p;
      color2->r = color2->g = color2->b = color2->a = p;

      ands = s1 & 1;
      p = TMEM[taddr1];
      p = (ands) ? (p & 0xf) : (p >> 4);
      p = (tpal << 4) | p;
      color1->r = color1->g = color1->b = color1->a = p;
      p = TMEM[taddr3];
      p = (ands) ? (p & 0xf) : (p >> 4);
      p = (tpal << 4) | p;
      color3->r = color3->g = color3->b = color3->a = p;
    }
    break;
  case TEXEL_CI8:
    {
      taddr0 = ((tbase0 << 3) + s0);
      taddr1 = ((tbase0 << 3) + s1);
      taddr2 = ((tbase2 << 3) + s0);
      taddr3 = ((tbase2 << 3) + s1);
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      
      uint32_t p;

      taddr0 &= 0xfff;
      taddr1 &= 0xfff;
      taddr2 &= 0xfff;
      taddr3 &= 0xfff;
      p = TMEM[taddr0];
      color0->r = p;
      color0->g = p;
      color0->b = p;
      color0->a = p;
      p = TMEM[taddr2];
      color2->r = p;
      color2->g = p;
      color2->b = p;
      color2->a = p;
      p = TMEM[taddr1];
      color1->r = p;
      color1->g = p;
      color1->b = p;
      color1->a = p;
      p = TMEM[taddr3];
      color3->r = p;
      color3->g = p;
      color3->b = p;
      color3->a = p;
    }
    break;
  case TEXEL_CI16:
    {
      taddr0 = ((tbase0 << 2) + s0);
      taddr1 = ((tbase0 << 2) + s1);
      taddr2 = ((tbase2 << 2) + s0);
      taddr3 = ((tbase2 << 2) + s1);
      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
                
      uint16_t c0, c1, c2, c3;
          
      taddr0 &= 0x7ff;
      taddr1 &= 0x7ff;
      taddr2 &= 0x7ff;
      taddr3 &= 0x7ff;
      c0 = tc16[taddr0];
      color0->r = c0 >> 8;
      color0->g = c0 & 0xff;
      color0->b = c0 >> 8;
      color0->a = (c0 & 1) ? 0xff : 0;
      c1 = tc16[taddr1];
      color1->r = c1 >> 8;
      color1->g = c1 & 0xff;
      color1->b = c1 >> 8;
      color1->a = (c1 & 1) ? 0xff : 0;
      c2 = tc16[taddr2];
      color2->r = c2 >> 8;
      color2->g = c2 & 0xff;
      color2->b = c2 >> 8;
      color2->a = (c2 & 1) ? 0xff : 0;
      c3 = tc16[taddr3];
      color3->r = c3 >> 8;
      color3->g = c3 & 0xff;
      color3->b = c3 >> 8;
      color3->a = (c3 & 1) ? 0xff : 0;
      
    }
    break;
  case TEXEL_CI32:
    {
      taddr0 = ((tbase0 << 2) + s0);
      taddr1 = ((tbase0 << 2) + s1);
      taddr2 = ((tbase2 << 2) + s0);
      taddr3 = ((tbase2 << 2) + s1);
      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
                
      uint16_t c0, c1, c2, c3;
          
      taddr0 &= 0x7ff;
      taddr1 &= 0x7ff;
      taddr2 &= 0x7ff;
      taddr3 &= 0x7ff;
      c0 = tc16[taddr0];
      color0->r = c0 >> 8;
      color0->g = c0 & 0xff;
      color0->b = c0 >> 8;
      color0->a = (c0 & 1) ? 0xff : 0;
      c1 = tc16[taddr1];
      color1->r = c1 >> 8;
      color1->g = c1 & 0xff;
      color1->b = c1 >> 8;
      color1->a = (c1 & 1) ? 0xff : 0;
      c2 = tc16[taddr2];
      color2->r = c2 >> 8;
      color2->g = c2 & 0xff;
      color2->b = c2 >> 8;
      color2->a = (c2 & 1) ? 0xff : 0;
      c3 = tc16[taddr3];
      color3->r = c3 >> 8;
      color3->g = c3 & 0xff;
      color3->b = c3 >> 8;
      color3->a = (c3 & 1) ? 0xff : 0;
      
    }
        break;
  case TEXEL_IA4:
    {
      taddr0 = ((tbase0 << 4) + s0) >> 1;
      taddr1 = ((tbase0 << 4) + s1) >> 1;
      taddr2 = ((tbase2 << 4) + s0) >> 1;
      taddr3 = ((tbase2 << 4) + s1) >> 1;
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      
      uint32_t p, i; 
          
      taddr0 &= 0xfff;
      taddr1 &= 0xfff;
      taddr2 &= 0xfff;
      taddr3 &= 0xfff;
      ands = s0 & 1;
      p = TMEM[taddr0];
      p = ands ? (p & 0xf) : (p >> 4);
      i = p & 0xe;
      i = (i << 4) | (i << 1) | (i >> 2);
      color0->r = i;
      color0->g = i;
      color0->b = i;
      color0->a = (p & 0x1) ? 0xff : 0;
      p = TMEM[taddr2];
      p = ands ? (p & 0xf) : (p >> 4);
      i = p & 0xe;
      i = (i << 4) | (i << 1) | (i >> 2);
      color2->r = i;
      color2->g = i;
      color2->b = i;
      color2->a = (p & 0x1) ? 0xff : 0;

      ands = s1 & 1;
      p = TMEM[taddr1];
      p = ands ? (p & 0xf) : (p >> 4);
      i = p & 0xe;
      i = (i << 4) | (i << 1) | (i >> 2);
      color1->r = i;
      color1->g = i;
      color1->b = i;
      color1->a = (p & 0x1) ? 0xff : 0;
      p = TMEM[taddr3];
      p = ands ? (p & 0xf) : (p >> 4);
      i = p & 0xe;
      i = (i << 4) | (i << 1) | (i >> 2);
      color3->r = i;
      color3->g = i;
      color3->b = i;
      color3->a = (p & 0x1) ? 0xff : 0;
      
    }
    break;
  case TEXEL_IA8:
    {
      taddr0 = ((tbase0 << 3) + s0);
      taddr1 = ((tbase0 << 3) + s1);
      taddr2 = ((tbase2 << 3) + s0);
      taddr3 = ((tbase2 << 3) + s1);
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      
      uint32_t p, i;

      taddr0 &= 0xfff;
      taddr1 &= 0xfff;
      taddr2 &= 0xfff;
      taddr3 &= 0xfff;
      p = TMEM[taddr0];
      i = p & 0xf0;
      i |= (i >> 4);
      color0->r = i;
      color0->g = i;
      color0->b = i;
      color0->a = ((p & 0xf) << 4) | (p & 0xf);
      p = TMEM[taddr1];
      i = p & 0xf0;
      i |= (i >> 4);
      color1->r = i;
      color1->g = i;
      color1->b = i;
      color1->a = ((p & 0xf) << 4) | (p & 0xf);
      p = TMEM[taddr2];
      i = p & 0xf0;
      i |= (i >> 4);
      color2->r = i;
      color2->g = i;
      color2->b = i;
      color2->a = ((p & 0xf) << 4) | (p & 0xf);
      p = TMEM[taddr3];
      i = p & 0xf0;
      i |= (i >> 4);
      color3->r = i;
      color3->g = i;
      color3->b = i;
      color3->a = ((p & 0xf) << 4) | (p & 0xf);
      
      
    }
    break;
  case TEXEL_IA16:
    {
      taddr0 = ((tbase0 << 2) + s0);
      taddr1 = ((tbase0 << 2) + s1);
      taddr2 = ((tbase2 << 2) + s0);
      taddr3 = ((tbase2 << 2) + s1);
      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
                
      uint16_t c0, c1, c2, c3;
                    
      taddr0 &= 0x7ff;
      taddr1 &= 0x7ff;
      taddr2 &= 0x7ff;
      taddr3 &= 0x7ff;
      c0 = tc16[taddr0];
      color0->r = color0->g = color0->b = c0 >> 8;
      color0->a = c0 & 0xff;
      c1 = tc16[taddr1];
      color1->r = color1->g = color1->b = c1 >> 8;
      color1->a = c1 & 0xff;
      c2 = tc16[taddr2];
      color2->r = color2->g = color2->b = c2 >> 8;
      color2->a = c2 & 0xff;
      c3 = tc16[taddr3];
      color3->r = color3->g = color3->b = c3 >> 8;
      color3->a = c3 & 0xff;
        
    }
    break;
  case TEXEL_IA32:
    {
      taddr0 = ((tbase0 << 2) + s0);
      taddr1 = ((tbase0 << 2) + s1);
      taddr2 = ((tbase2 << 2) + s0);
      taddr3 = ((tbase2 << 2) + s1);
      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
                
      uint16_t c0, c1, c2, c3;
          
      taddr0 &= 0x7ff;
      taddr1 &= 0x7ff;
      taddr2 &= 0x7ff;
      taddr3 &= 0x7ff;
      c0 = tc16[taddr0];
      color0->r = c0 >> 8;
      color0->g = c0 & 0xff;
      color0->b = c0 >> 8;
      color0->a = (c0 & 1) ? 0xff : 0;
      c1 = tc16[taddr1];
      color1->r = c1 >> 8;
      color1->g = c1 & 0xff;
      color1->b = c1 >> 8;
      color1->a = (c1 & 1) ? 0xff : 0;
      c2 = tc16[taddr2];
      color2->r = c2 >> 8;
      color2->g = c2 & 0xff;
      color2->b = c2 >> 8;
      color2->a = (c2 & 1) ? 0xff : 0;
      c3 = tc16[taddr3];
      color3->r = c3 >> 8;
      color3->g = c3 & 0xff;
      color3->b = c3 >> 8;
      color3->a = (c3 & 1) ? 0xff : 0;
            
    }
    break;
  case TEXEL_I4:
    {
      taddr0 = ((tbase0 << 4) + s0) >> 1;
      taddr1 = ((tbase0 << 4) + s1) >> 1;
      taddr2 = ((tbase2 << 4) + s0) >> 1;
      taddr3 = ((tbase2 << 4) + s1) >> 1;
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      
      uint32_t p, c0, c1, c2, c3; 
      
      taddr0 &= 0xfff;
      taddr1 &= 0xfff;
      taddr2 &= 0xfff;
      taddr3 &= 0xfff;
      ands = s0 & 1;
      p = TMEM[taddr0];
      c0 = ands ? (p & 0xf) : (p >> 4);
      c0 |= (c0 << 4);
      color0->r = color0->g = color0->b = color0->a = c0;
      p = TMEM[taddr2];
      c2 = ands ? (p & 0xf) : (p >> 4);
      c2 |= (c2 << 4);
      color2->r = color2->g = color2->b = color2->a = c2;

      ands = s1 & 1;
      p = TMEM[taddr1];
      c1 = ands ? (p & 0xf) : (p >> 4);
      c1 |= (c1 << 4);
      color1->r = color1->g = color1->b = color1->a = c1;
      p = TMEM[taddr3];
      c3 = ands ? (p & 0xf) : (p >> 4);
      c3 |= (c3 << 4);
      color3->r = color3->g = color3->b = color3->a = c3;
        
    }
    break;
  case TEXEL_I8:
    {
      taddr0 = ((tbase0 << 3) + s0);
      taddr1 = ((tbase0 << 3) + s1);
      taddr2 = ((tbase2 << 3) + s0);
      taddr3 = ((tbase2 << 3) + s1);
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      
      uint32_t p;

      taddr0 &= 0xfff;
      taddr1 &= 0xfff;
      taddr2 &= 0xfff;
      taddr3 &= 0xfff;
      p = TMEM[taddr0];
      color0->r = p;
      color0->g = p;
      color0->b = p;
      color0->a = p;
      p = TMEM[taddr1];
      color1->r = p;
      color1->g = p;
      color1->b = p;
      color1->a = p;
      p = TMEM[taddr2];
      color2->r = p;
      color2->g = p;
      color2->b = p;
      color2->a = p;
      p = TMEM[taddr3];
      color3->r = p;
      color3->g = p;
      color3->b = p;
      color3->a = p;
    }
    break;
  case TEXEL_I16:
    {
      taddr0 = ((tbase0 << 2) + s0);
      taddr1 = ((tbase0 << 2) + s1);
      taddr2 = ((tbase2 << 2) + s0);
      taddr3 = ((tbase2 << 2) + s1);
      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
                
      uint16_t c0, c1, c2, c3;
          
      taddr0 &= 0x7ff;
      taddr1 &= 0x7ff;
      taddr2 &= 0x7ff;
      taddr3 &= 0x7ff;
      c0 = tc16[taddr0];
      color0->r = c0 >> 8;
      color0->g = c0 & 0xff;
      color0->b = c0 >> 8;
      color0->a = (c0 & 1) ? 0xff : 0;
      c1 = tc16[taddr1];
      color1->r = c1 >> 8;
      color1->g = c1 & 0xff;
      color1->b = c1 >> 8;
      color1->a = (c1 & 1) ? 0xff : 0;
      c2 = tc16[taddr2];
      color2->r = c2 >> 8;
      color2->g = c2 & 0xff;
      color2->b = c2 >> 8;
      color2->a = (c2 & 1) ? 0xff : 0;
      c3 = tc16[taddr3];
      color3->r = c3 >> 8;
      color3->g = c3 & 0xff;
      color3->b = c3 >> 8;
      color3->a = (c3 & 1) ? 0xff : 0;
    }
    break;
  case TEXEL_I32:
    {
      taddr0 = ((tbase0 << 2) + s0);
      taddr1 = ((tbase0 << 2) + s1);
      taddr2 = ((tbase2 << 2) + s0);
      taddr3 = ((tbase2 << 2) + s1);
      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
                
      uint16_t c0, c1, c2, c3;
          
      taddr0 &= 0x7ff;
      taddr1 &= 0x7ff;
      taddr2 &= 0x7ff;
      taddr3 &= 0x7ff;
      c0 = tc16[taddr0];
      color0->r = c0 >> 8;
      color0->g = c0 & 0xff;
      color0->b = c0 >> 8;
      color0->a = (c0 & 1) ? 0xff : 0;
      c1 = tc16[taddr1];
      color1->r = c1 >> 8;
      color1->g = c1 & 0xff;
      color1->b = c1 >> 8;
      color1->a = (c1 & 1) ? 0xff : 0;
      c2 = tc16[taddr2];
      color2->r = c2 >> 8;
      color2->g = c2 & 0xff;
      color2->b = c2 >> 8;
      color2->a = (c2 & 1) ? 0xff : 0;
      c3 = tc16[taddr3];
      color3->r = c3 >> 8;
      color3->g = c3 & 0xff;
      color3->b = c3 >> 8;
      color3->a = (c3 & 1) ? 0xff : 0;
    }
    break;
  default:
    fatalerror("fetch_texel_quadro: unknown texture format %d, size %d, tilenum %d\n", tile[tilenum].format, tile[tilenum].size, tilenum);
    break;
  }
}

static void fetch_texel_entlut_quadro(COLOR *color0, COLOR *color1, COLOR *color2, COLOR *color3, int s0, int s1, int t0, int t1, uint32_t tilenum)
{
  uint32_t tbase0 = tile[tilenum].line * t0 + tile[tilenum].tmem;
  uint32_t tbase2 = tile[tilenum].line * t1 + tile[tilenum].tmem;
  uint32_t tpal = tile[tilenum].palette << 4;
  uint32_t xort = 0, ands = 0;

  uint16_t *tc16 = (uint16_t*)TMEM;
  uint32_t taddr0 = 0, taddr1 = 0, taddr2 = 0, taddr3 = 0;
  uint16_t c0, c1, c2, c3;

  
  
  switch(tile[tilenum].f.tlutswitch)
  {
  case 0:
  case 1:
  case 2:
    {
      taddr0 = ((tbase0 << 4) + s0) >> 1;
      taddr1 = ((tbase0 << 4) + s1) >> 1;
      taddr2 = ((tbase2 << 4) + s0) >> 1;
      taddr3 = ((tbase2 << 4) + s1) >> 1;
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
                              
      ands = s0 & 1;
      c0 = TMEM[taddr0 & 0x7ff];
      c0 = (ands) ? (c0 & 0xf) : (c0 >> 4);
      c0 = tlut[((tpal | c0) << 2) ^ WORD_ADDR_XOR];
      c2 = TMEM[taddr2 & 0x7ff];
      c2 = (ands) ? (c2 & 0xf) : (c2 >> 4);
      c2 = tlut[((tpal | c2) << 2) ^ WORD_ADDR_XOR];

      ands = s1 & 1;
      c1 = TMEM[taddr1 & 0x7ff];
      c1 = (ands) ? (c1 & 0xf) : (c1 >> 4);
      c1 = tlut[((tpal | c1) << 2) ^ WORD_ADDR_XOR];
      c3 = TMEM[taddr3 & 0x7ff];
      c3 = (ands) ? (c3 & 0xf) : (c3 >> 4);
      c3 = tlut[((tpal | c3) << 2) ^ WORD_ADDR_XOR];
    }
    break;
  case 3:
    {
      taddr0 = ((tbase0 << 3) + s0);
      taddr1 = ((tbase0 << 3) + s1);
      taddr2 = ((tbase2 << 3) + s0);
      taddr3 = ((tbase2 << 3) + s1);
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
                              
      ands = s0 & 1;
      c0 = TMEM[taddr0 & 0x7ff];
      c0 = (ands) ? (c0 & 0xf) : (c0 >> 4);
      c0 = tlut[((tpal | c0) << 2) ^ WORD_ADDR_XOR];
      c2 = TMEM[taddr2 & 0x7ff];
      c2 = (ands) ? (c2 & 0xf) : (c2 >> 4);
      c2 = tlut[((tpal | c2) << 2) ^ WORD_ADDR_XOR];

      ands = s1 & 1;
      c1 = TMEM[taddr1 & 0x7ff];
      c1 = (ands) ? (c1 & 0xf) : (c1 >> 4);
      c1 = tlut[((tpal | c1) << 2) ^ WORD_ADDR_XOR];
      c3 = TMEM[taddr3 & 0x7ff];
      c3 = (ands) ? (c3 & 0xf) : (c3 >> 4);
      c3 = tlut[((tpal | c3) << 2) ^ WORD_ADDR_XOR];
    }
    break;
  case 4:
  case 5:
  case 6:
  case 7:
    {
      taddr0 = ((tbase0 << 3) + s0);
      taddr1 = ((tbase0 << 3) + s1);
      taddr2 = ((tbase2 << 3) + s0);
      taddr3 = ((tbase2 << 3) + s1);
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      
      c0 = TMEM[taddr0 & 0x7ff];
      c0 = tlut[(c0 << 2) ^ WORD_ADDR_XOR];
      c2 = TMEM[taddr2 & 0x7ff];
      c2 = tlut[(c2 << 2) ^ WORD_ADDR_XOR];
      c1 = TMEM[taddr1 & 0x7ff];
      c1 = tlut[(c1 << 2) ^ WORD_ADDR_XOR];
      c3 = TMEM[taddr3 & 0x7ff];
      c3 = tlut[(c3 << 2) ^ WORD_ADDR_XOR];
    }
    break;
  case 8:
  case 9:
  case 10:
    {
      taddr0 = ((tbase0 << 2) + s0);
      taddr1 = ((tbase0 << 2) + s1);
      taddr2 = ((tbase2 << 2) + s0);
      taddr3 = ((tbase2 << 2) + s1);
      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
          
      c0 = tc16[taddr0 & 0x3ff];
      c0 = tlut[((c0 >> 6) & ~3) ^ WORD_ADDR_XOR];
      c1 = tc16[taddr1 & 0x3ff];
      c1 = tlut[((c1 >> 6) & ~3) ^ WORD_ADDR_XOR];
      c2 = tc16[taddr2 & 0x3ff];
      c2 = tlut[((c2 >> 6) & ~3) ^ WORD_ADDR_XOR];
      c3 = tc16[taddr3 & 0x3ff];
      c3 = tlut[((c3 >> 6) & ~3) ^ WORD_ADDR_XOR];
    }
    break;
  case 11:
    {
      taddr0 = ((tbase0 << 3) + s0);
      taddr1 = ((tbase0 << 3) + s1);
      taddr2 = ((tbase2 << 3) + s0);
      taddr3 = ((tbase2 << 3) + s1);
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      
      c0 = TMEM[taddr0 & 0x7ff];
      c0 = tlut[(c0 << 2) ^ WORD_ADDR_XOR];
      c2 = TMEM[taddr2 & 0x7ff];
      c2 = tlut[(c2 << 2) ^ WORD_ADDR_XOR];
      c1 = TMEM[taddr1 & 0x7ff];
      c1 = tlut[(c1 << 2) ^ WORD_ADDR_XOR];
      c3 = TMEM[taddr3 & 0x7ff];
      c3 = tlut[(c3 << 2) ^ WORD_ADDR_XOR];
    }
    break;
  case 12:
  case 13:
  case 14:
    {
      taddr0 = ((tbase0 << 2) + s0);
      taddr1 = ((tbase0 << 2) + s1);
      taddr2 = ((tbase2 << 2) + s0);
      taddr3 = ((tbase2 << 2) + s1);
      xort = (t0 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? WORD_XOR_DWORD_SWAP : WORD_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
                
      c0 = tc16[taddr0 & 0x3ff];
      c0 = tlut[((c0 >> 6) & ~3) ^ WORD_ADDR_XOR];
      c1 = tc16[taddr1 & 0x3ff];
      c1 = tlut[((c1 >> 6) & ~3) ^ WORD_ADDR_XOR];
      c2 = tc16[taddr2 & 0x3ff];
      c2 = tlut[((c2 >> 6) & ~3) ^ WORD_ADDR_XOR];
      c3 = tc16[taddr3 & 0x3ff];
      c3 = tlut[((c3 >> 6) & ~3) ^ WORD_ADDR_XOR];
    }
    break;
  case 15:
    {
      taddr0 = ((tbase0 << 3) + s0);
      taddr1 = ((tbase0 << 3) + s1);
      taddr2 = ((tbase2 << 3) + s0);
      taddr3 = ((tbase2 << 3) + s1);
      xort = (t0 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr0 ^= xort;
      taddr1 ^= xort;
      xort = (t1 & 1) ? BYTE_XOR_DWORD_SWAP : BYTE_ADDR_XOR;
      taddr2 ^= xort;
      taddr3 ^= xort;
      
      c0 = TMEM[taddr0 & 0x7ff];
      c0 = tlut[(c0 << 2) ^ WORD_ADDR_XOR];
      c2 = TMEM[taddr2 & 0x7ff];
      c2 = tlut[(c2 << 2) ^ WORD_ADDR_XOR];
      c1 = TMEM[taddr1 & 0x7ff];
      c1 = tlut[(c1 << 2) ^ WORD_ADDR_XOR];
      c3 = TMEM[taddr3 & 0x7ff];
      c3 = tlut[(c3 << 2) ^ WORD_ADDR_XOR];
    }
    break;
  default:
    fatalerror("fetch_texel_entlut_quadro: unknown texture format %d, size %d, tilenum %d\n", tile[tilenum].format, tile[tilenum].size, tilenum);
    break;
  }

  if (!other_modes.tlut_type)
  {
    color0->r = GET_HI_RGBA16_TMEM(c0);
    color0->g = GET_MED_RGBA16_TMEM(c0);
    color0->b = GET_LOW_RGBA16_TMEM(c0);
    color0->a = (c0 & 1) ? 0xff : 0;
    color1->r = GET_HI_RGBA16_TMEM(c1);
    color1->g = GET_MED_RGBA16_TMEM(c1);
    color1->b = GET_LOW_RGBA16_TMEM(c1);
    color1->a = (c1 & 1) ? 0xff : 0;
    color2->r = GET_HI_RGBA16_TMEM(c2);
    color2->g = GET_MED_RGBA16_TMEM(c2);
    color2->b = GET_LOW_RGBA16_TMEM(c2);
    color2->a = (c2 & 1) ? 0xff : 0;
    color3->r = GET_HI_RGBA16_TMEM(c3);
    color3->g = GET_MED_RGBA16_TMEM(c3);
    color3->b = GET_LOW_RGBA16_TMEM(c3);
    color3->a = (c3 & 1) ? 0xff : 0;
  }
  else
  {
    color0->r = color0->g = color0->b = c0 >> 8;
    color0->a = c0 & 0xff;
    color1->r = color1->g = color1->b = c1 >> 8;
    color1->a = c1 & 0xff;
    color2->r = color2->g = color2->b = c2 >> 8;
    color2->a = c2 & 0xff;
    color3->r = color3->g = color3->b = c3 >> 8;
    color3->a = c3 & 0xff;
  }
}

void get_tmem_idx(int s, int t, uint32_t tilenum, uint32_t* idx0, uint32_t* idx1, uint32_t* idx2, uint32_t* idx3, uint32_t* bit3flipped, uint32_t* hibit)
{
  uint32_t tbase = (tile[tilenum].line * t) & 0x1ff;
  tbase += tile[tilenum].tmem;
  uint32_t tsize = tile[tilenum].size;
  uint32_t tformat = tile[tilenum].format;
  uint32_t sshorts = 0;

  
  if (tsize == PIXEL_SIZE_8BIT || tformat == FORMAT_YUV)
    sshorts = s >> 1;
  else if (tsize >= PIXEL_SIZE_16BIT)
    sshorts = s;
  else
    sshorts = s >> 2;
  sshorts &= 0x7ff;

  *bit3flipped = ((sshorts & 2) ? 1 : 0) ^ (t & 1);
    
  int tidx_a = ((tbase << 2) + sshorts) & 0x7fd;
  int tidx_b = (tidx_a + 1) & 0x7ff;
  int tidx_c = (tidx_a + 2) & 0x7ff;
  int tidx_d = (tidx_a + 3) & 0x7ff;

  *hibit = (tidx_a & 0x400) ? 1 : 0;

  if (t & 1)
  {
    tidx_a ^= 2;
    tidx_b ^= 2;
    tidx_c ^= 2;
    tidx_d ^= 2;
  }

  
  sort_tmem_idx(idx0, tidx_a, tidx_b, tidx_c, tidx_d, 0);
  sort_tmem_idx(idx1, tidx_a, tidx_b, tidx_c, tidx_d, 1);
  sort_tmem_idx(idx2, tidx_a, tidx_b, tidx_c, tidx_d, 2);
  sort_tmem_idx(idx3, tidx_a, tidx_b, tidx_c, tidx_d, 3);
}

void read_tmem_copy(int s, int s1, int s2, int s3, int t, uint32_t tilenum, uint32_t* sortshort, int* hibits, int* lowbits)
{
  uint32_t tbase = (tile[tilenum].line * t) & 0x1ff;
  tbase += tile[tilenum].tmem;
  uint32_t tsize = tile[tilenum].size;
  uint32_t tformat = tile[tilenum].format;
  uint32_t shbytes = 0, shbytes1 = 0, shbytes2 = 0, shbytes3 = 0;
  int32_t delta = 0;
  uint32_t sortidx[8];

  
  if (tsize == PIXEL_SIZE_8BIT || tformat == FORMAT_YUV)
  {
    shbytes = s << 1;
    shbytes1 = s1 << 1;
    shbytes2 = s2 << 1;
    shbytes3 = s3 << 1;
  }
  else if (tsize >= PIXEL_SIZE_16BIT)
  {
    shbytes = s << 2;
    shbytes1 = s1 << 2;
    shbytes2 = s2 << 2;
    shbytes3 = s3 << 2;
  }
  else
  {
    shbytes = s;
    shbytes1 = s1;
    shbytes2 = s2;
    shbytes3 = s3;
  }

  shbytes &= 0x1fff;
  shbytes1 &= 0x1fff;
  shbytes2 &= 0x1fff;
  shbytes3 &= 0x1fff;

  int tidx_a, tidx_blow, tidx_bhi, tidx_c, tidx_dlow, tidx_dhi;

  tbase <<= 4;
  tidx_a = (tbase + shbytes) & 0x1fff;
  tidx_bhi = (tbase + shbytes1) & 0x1fff;
  tidx_c = (tbase + shbytes2) & 0x1fff;
  tidx_dhi = (tbase + shbytes3) & 0x1fff;

  if (tformat == FORMAT_YUV)
  {
    delta = shbytes1 - shbytes;
    tidx_blow = (tidx_a + (delta << 1)) & 0x1fff;
    tidx_dlow = (tidx_blow + shbytes3 - shbytes) & 0x1fff;
  }
  else
  {
    tidx_blow = tidx_bhi;
    tidx_dlow = tidx_dhi;
  }

  if (t & 1)
  {
    tidx_a ^= 8;
    tidx_blow ^= 8;
    tidx_bhi ^= 8;
    tidx_c ^= 8;
    tidx_dlow ^= 8;
    tidx_dhi ^= 8;
  }

  hibits[0] = (tidx_a & 0x1000) ? 1 : 0;
  hibits[1] = (tidx_blow & 0x1000) ? 1 : 0; 
  hibits[2] = (tidx_bhi & 0x1000) ? 1 : 0;
  hibits[3] = (tidx_c & 0x1000) ? 1 : 0;
  hibits[4] = (tidx_dlow & 0x1000) ? 1 : 0;
  hibits[5] = (tidx_dhi & 0x1000) ? 1 : 0;
  lowbits[0] = tidx_a & 0xf;
  lowbits[1] = tidx_blow & 0xf;
  lowbits[2] = tidx_bhi & 0xf;
  lowbits[3] = tidx_c & 0xf;
  lowbits[4] = tidx_dlow & 0xf;
  lowbits[5] = tidx_dhi & 0xf;

  uint16_t* tmem16 = (uint16_t*)TMEM;
  uint32_t short0, short1, short2, short3;

  
  tidx_a >>= 2;
  tidx_blow >>= 2;
  tidx_bhi >>= 2;
  tidx_c >>= 2;
  tidx_dlow >>= 2;
  tidx_dhi >>= 2;

  
  sort_tmem_idx(&sortidx[0], tidx_a, tidx_blow, tidx_c, tidx_dlow, 0);
  sort_tmem_idx(&sortidx[1], tidx_a, tidx_blow, tidx_c, tidx_dlow, 1);
  sort_tmem_idx(&sortidx[2], tidx_a, tidx_blow, tidx_c, tidx_dlow, 2);
  sort_tmem_idx(&sortidx[3], tidx_a, tidx_blow, tidx_c, tidx_dlow, 3);

  short0 = tmem16[sortidx[0] ^ WORD_ADDR_XOR];
  short1 = tmem16[sortidx[1] ^ WORD_ADDR_XOR];
  short2 = tmem16[sortidx[2] ^ WORD_ADDR_XOR];
  short3 = tmem16[sortidx[3] ^ WORD_ADDR_XOR];

  
  sort_tmem_shorts_lowhalf(&sortshort[0], short0, short1, short2, short3, lowbits[0] >> 2);
  sort_tmem_shorts_lowhalf(&sortshort[1], short0, short1, short2, short3, lowbits[1] >> 2);
  sort_tmem_shorts_lowhalf(&sortshort[2], short0, short1, short2, short3, lowbits[3] >> 2);
  sort_tmem_shorts_lowhalf(&sortshort[3], short0, short1, short2, short3, lowbits[4] >> 2);

  if (other_modes.en_tlut)
  {
    
    compute_color_index(&short0, sortshort[0], lowbits[0] & 3, tilenum);
    compute_color_index(&short1, sortshort[1], lowbits[1] & 3, tilenum);
    compute_color_index(&short2, sortshort[2], lowbits[3] & 3, tilenum);
    compute_color_index(&short3, sortshort[3], lowbits[4] & 3, tilenum);

    
    sortidx[4] = (short0 << 2);
    sortidx[5] = (short1 << 2) | 1;
    sortidx[6] = (short2 << 2) | 2;
    sortidx[7] = (short3 << 2) | 3;
  }
  else
  {
    sort_tmem_idx(&sortidx[4], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 0);
    sort_tmem_idx(&sortidx[5], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 1);
    sort_tmem_idx(&sortidx[6], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 2);
    sort_tmem_idx(&sortidx[7], tidx_a, tidx_bhi, tidx_c, tidx_dhi, 3);
  }

  short0 = tmem16[(sortidx[4] | 0x400) ^ WORD_ADDR_XOR];
  short1 = tmem16[(sortidx[5] | 0x400) ^ WORD_ADDR_XOR];
  short2 = tmem16[(sortidx[6] | 0x400) ^ WORD_ADDR_XOR];
  short3 = tmem16[(sortidx[7] | 0x400) ^ WORD_ADDR_XOR];

  
  
  if (other_modes.en_tlut)
  {
    sort_tmem_shorts_lowhalf(&sortshort[4], short0, short1, short2, short3, 0);
    sort_tmem_shorts_lowhalf(&sortshort[5], short0, short1, short2, short3, 1);
    sort_tmem_shorts_lowhalf(&sortshort[6], short0, short1, short2, short3, 2);
    sort_tmem_shorts_lowhalf(&sortshort[7], short0, short1, short2, short3, 3);
  }
  else
  {
    sort_tmem_shorts_lowhalf(&sortshort[4], short0, short1, short2, short3, lowbits[0] >> 2);
    sort_tmem_shorts_lowhalf(&sortshort[5], short0, short1, short2, short3, lowbits[2] >> 2);
    sort_tmem_shorts_lowhalf(&sortshort[6], short0, short1, short2, short3, lowbits[3] >> 2);
    sort_tmem_shorts_lowhalf(&sortshort[7], short0, short1, short2, short3, lowbits[5] >> 2);
  }
}

void sort_tmem_idx(uint32_t *idx, uint32_t idxa, uint32_t idxb, uint32_t idxc, uint32_t idxd, uint32_t bankno)
{
  if ((idxa & 3) == bankno)
    *idx = idxa & 0x3ff;
  else if ((idxb & 3) == bankno)
    *idx = idxb & 0x3ff;
  else if ((idxc & 3) == bankno)
    *idx = idxc & 0x3ff;
  else if ((idxd & 3) == bankno)
    *idx = idxd & 0x3ff;
  else
    *idx = 0;
}

void sort_tmem_shorts_lowhalf(uint32_t* bindshort, uint32_t short0, uint32_t short1, uint32_t short2, uint32_t short3, uint32_t bankno)
{
  switch(bankno)
  {
  case 0:
    *bindshort = short0;
    break;
  case 1:
    *bindshort = short1;
    break;
  case 2:
    *bindshort = short2;
    break;
  case 3:
    *bindshort = short3;
    break;
  }
}

void compute_color_index(uint32_t* cidx, uint32_t readshort, uint32_t nybbleoffset, uint32_t tilenum)
{
  uint32_t lownib, hinib;
  if (tile[tilenum].size == PIXEL_SIZE_4BIT)
  {
    lownib = (nybbleoffset ^ 3) << 2;
    hinib = tile[tilenum].palette;
  }
  else
  {
    lownib = ((nybbleoffset & 2) ^ 2) << 2;
    hinib = lownib ? ((readshort >> 12) & 0xf) : ((readshort >> 4) & 0xf);
  }
  lownib = (readshort >> lownib) & 0xf;
  *cidx = (hinib << 4) | lownib;
}

void replicate_for_copy(uint32_t* outbyte, uint32_t inshort, uint32_t nybbleoffset, uint32_t tilenum, uint32_t tformat, uint32_t tsize)
{
  uint32_t lownib, hinib;
  switch(tsize)
  {
  case PIXEL_SIZE_4BIT:
    lownib = (nybbleoffset ^ 3) << 2;
    lownib = hinib = (inshort >> lownib) & 0xf;
    if (tformat == FORMAT_CI)
    {
      *outbyte = (tile[tilenum].palette << 4) | lownib;
    }
    else if (tformat == FORMAT_IA)
    {
      lownib = (lownib << 4) | lownib;
      *outbyte = (lownib & 0xe0) | ((lownib & 0xe0) >> 3) | ((lownib & 0xc0) >> 6);
    }
    else
      *outbyte = (lownib << 4) | lownib;
    break;
  case PIXEL_SIZE_8BIT:
    hinib = ((nybbleoffset ^ 3) | 1) << 2;
    if (tformat == FORMAT_IA)
    {
      lownib = (inshort >> hinib) & 0xf;
      *outbyte = (lownib << 4) | lownib;
    }
    else
    {
      lownib = (inshort >> (hinib & ~4)) & 0xf;
      hinib = (inshort >> hinib) & 0xf;
      *outbyte = (hinib << 4) | lownib;
    }
    break;
  default:
    *outbyte = (inshort >> 8) & 0xff;
    break;
  }
}

void fetch_qword_copy(uint32_t* hidword, uint32_t* lowdword, int32_t ssss, int32_t ssst, uint32_t tilenum)
{
  uint32_t shorta, shortb, shortc, shortd;
  uint32_t sortshort[8];
  int hibits[6];
  int lowbits[6];
  int32_t sss = ssss, sst = ssst, sss1 = 0, sss2 = 0, sss3 = 0;
  int largetex = 0;

  uint32_t tformat, tsize;
  if (other_modes.en_tlut)
  {
    tsize = PIXEL_SIZE_16BIT;
    tformat = other_modes.tlut_type ? FORMAT_IA : FORMAT_RGBA;
  }
  else
  {
    tsize = tile[tilenum].size;
    tformat = tile[tilenum].format;
  }

  tc_pipeline_copy(&sss, &sss1, &sss2, &sss3, &sst, tilenum);
  read_tmem_copy(sss, sss1, sss2, sss3, sst, tilenum, sortshort, hibits, lowbits);
  largetex = (tformat == FORMAT_YUV || (tformat == FORMAT_RGBA && tsize == PIXEL_SIZE_32BIT));

  
  if (other_modes.en_tlut)
  {
    shorta = sortshort[4];
    shortb = sortshort[5];
    shortc = sortshort[6];
    shortd = sortshort[7];
  }
  else if (largetex)
  {
    shorta = sortshort[0];
    shortb = sortshort[1];
    shortc = sortshort[2];
    shortd = sortshort[3];
  }
  else
  {
    shorta = hibits[0] ? sortshort[4] : sortshort[0];
    shortb = hibits[1] ? sortshort[5] : sortshort[1];
    shortc = hibits[3] ? sortshort[6] : sortshort[2];
    shortd = hibits[4] ? sortshort[7] : sortshort[3];
  }

  *lowdword = (shortc << 16) | shortd;

  if (tsize == PIXEL_SIZE_16BIT)
    *hidword = (shorta << 16) | shortb;
  else
  {
    replicate_for_copy(&shorta, shorta, lowbits[0] & 3, tilenum, tformat, tsize);
    replicate_for_copy(&shortb, shortb, lowbits[1] & 3, tilenum, tformat, tsize);
    replicate_for_copy(&shortc, shortc, lowbits[3] & 3, tilenum, tformat, tsize);
    replicate_for_copy(&shortd, shortd, lowbits[4] & 3, tilenum, tformat, tsize);
    *hidword = (shorta << 24) | (shortb << 16) | (shortc << 8) | shortd;
  }
}

static void texture_pipeline_cycle(COLOR* TEX, COLOR* prev, int32_t SSS, int32_t SST, uint32_t tilenum, uint32_t cycle)                     
{
#define TRELATIVE(x, y)   ((x) - ((y) << 3));
#define UPPER ((sfrac + tfrac) & 0x20)
  int32_t maxs, maxt, invt0r, invt0g, invt0b, invt0a;
  int32_t sfrac, tfrac, invsf, invtf;
  int bilerp = cycle ? other_modes.bi_lerp1 : other_modes.bi_lerp0;
  int convert = other_modes.convert_one && cycle;
  COLOR t0, t1, t2, t3;
  int sss1, sst1, sss2, sst2;
  int32_t newk0, newk1, newk2, newk3, invk0, invk1, invk2, invk3;

  sss1 = SSS;
  sst1 = SST;

  tcshift_cycle(&sss1, &sst1, &maxs, &maxt, tilenum);

  sss1 = TRELATIVE(sss1, tile[tilenum].sl);
  sst1 = TRELATIVE(sst1, tile[tilenum].tl);

  if (other_modes.sample_type)
  { 
    sfrac = sss1 & 0x1f;
    tfrac = sst1 & 0x1f;

    tcclamp_cycle(&sss1, &sst1, &sfrac, &tfrac, maxs, maxt, tilenum);
    
  
    if (tile[tilenum].format != FORMAT_YUV)
      sss2 = sss1 + 1;
    else
      sss2 = sss1 + 2;
    
    
    

    sst2 = sst1 + 1;
    

    
    tcmask_coupled(&sss1, &sss2, &sst1, &sst2, tilenum);
    
    

    
    
    
    

    
    if (bilerp)
    {
      
      if (!other_modes.en_tlut)
        fetch_texel_quadro(&t0, &t1, &t2, &t3, sss1, sss2, sst1, sst2, tilenum);
      else
        fetch_texel_entlut_quadro(&t0, &t1, &t2, &t3, sss1, sss2, sst1, sst2, tilenum);

      if (!other_modes.mid_texel || sfrac != 0x10 || tfrac != 0x10)
      {
        if (!convert)
        {
          if (UPPER)
          {
            
            invsf = 0x20 - sfrac;
            invtf = 0x20 - tfrac;
            TEX->r = t3.r + ((((invsf * (t2.r - t3.r)) + (invtf * (t1.r - t3.r))) + 0x10) >> 5);  
            TEX->g = t3.g + ((((invsf * (t2.g - t3.g)) + (invtf * (t1.g - t3.g))) + 0x10) >> 5);                                    
            TEX->b = t3.b + ((((invsf * (t2.b - t3.b)) + (invtf * (t1.b - t3.b))) + 0x10) >> 5);                                
            TEX->a = t3.a + ((((invsf * (t2.a - t3.a)) + (invtf * (t1.a - t3.a))) + 0x10) >> 5);
          }
          else
          {
            TEX->r = t0.r + ((((sfrac * (t1.r - t0.r)) + (tfrac * (t2.r - t0.r))) + 0x10) >> 5);                      
            TEX->g = t0.g + ((((sfrac * (t1.g - t0.g)) + (tfrac * (t2.g - t0.g))) + 0x10) >> 5);                      
            TEX->b = t0.b + ((((sfrac * (t1.b - t0.b)) + (tfrac * (t2.b - t0.b))) + 0x10) >> 5);                  
            TEX->a = t0.a + ((((sfrac * (t1.a - t0.a)) + (tfrac * (t2.a - t0.a))) + 0x10) >> 5);
          }
        }
        else
        {
          if (UPPER)
          {
            TEX->r = prev->b + ((((prev->r * (t2.r - t3.r)) + (prev->g * (t1.r - t3.r))) + 0x80) >> 8); 
            TEX->g = prev->b + ((((prev->r * (t2.g - t3.g)) + (prev->g * (t1.g - t3.g))) + 0x80) >> 8);                                   
            TEX->b = prev->b + ((((prev->r * (t2.b - t3.b)) + (prev->g * (t1.b - t3.b))) + 0x80) >> 8);                               
            TEX->a = prev->b + ((((prev->r * (t2.a - t3.a)) + (prev->g * (t1.a - t3.a))) + 0x80) >> 8);
          }
          else
          {
            TEX->r = prev->b + ((((prev->r * (t1.r - t0.r)) + (prev->g * (t2.r - t0.r))) + 0x80) >> 8);                     
            TEX->g = prev->b + ((((prev->r * (t1.g - t0.g)) + (prev->g * (t2.g - t0.g))) + 0x80) >> 8);                     
            TEX->b = prev->b + ((((prev->r * (t1.b - t0.b)) + (prev->g * (t2.b - t0.b))) + 0x80) >> 8);                 
            TEX->a = prev->b + ((((prev->r * (t1.a - t0.a)) + (prev->g * (t2.a - t0.a))) + 0x80) >> 8);
          } 
        }
        
      }
      else
      {
        invt0r  = ~t0.r; invt0g = ~t0.g; invt0b = ~t0.b; invt0a = ~t0.a;
        if (!convert)
        {
          sfrac <<= 2;
          tfrac <<= 2;
          TEX->r = t0.r + ((((sfrac * (t1.r - t0.r)) + (tfrac * (t2.r - t0.r))) + ((invt0r + t3.r) << 6) + 0xc0) >> 8);                     
          TEX->g = t0.g + ((((sfrac * (t1.g - t0.g)) + (tfrac * (t2.g - t0.g))) + ((invt0g + t3.g) << 6) + 0xc0) >> 8);                     
          TEX->b = t0.b + ((((sfrac * (t1.b - t0.b)) + (tfrac * (t2.b - t0.b))) + ((invt0b + t3.b) << 6) + 0xc0) >> 8);                 
          TEX->a = t0.a + ((((sfrac * (t1.a - t0.a)) + (tfrac * (t2.a - t0.a))) + ((invt0a + t3.a) << 6) + 0xc0) >> 8);
        }
        else
        {
          TEX->r = prev->b + ((((prev->r * (t1.r - t0.r)) + (prev->g * (t2.r - t0.r))) + ((invt0r + t3.r) << 6) + 0xc0) >> 8);                      
          TEX->g = prev->b + ((((prev->r * (t1.g - t0.g)) + (prev->g * (t2.g - t0.g))) + ((invt0g + t3.g) << 6) + 0xc0) >> 8);                      
          TEX->b = prev->b + ((((prev->r * (t1.b - t0.b)) + (prev->g * (t2.b - t0.b))) + ((invt0b + t3.b) << 6) + 0xc0) >> 8);                  
          TEX->a = prev->b + ((((prev->r * (t1.a - t0.a)) + (prev->g * (t2.a - t0.a))) + ((invt0a + t3.a) << 6) + 0xc0) >> 8);
        }
      }
      
    }
    else
    {
      newk0 = SIGN(k0, 9); 
      newk1 = SIGN(k1, 9); 
      newk2 = SIGN(k2, 9); 
      newk3 = SIGN(k3, 9);
      invk0 = ~newk0; 
      invk1 = ~newk1; 
      invk2 = ~newk2; 
      invk3 = ~newk3;
      if (!other_modes.en_tlut)
        fetch_texel(&t0, sss1, sst1, tilenum);
      else
        fetch_texel_entlut(&t0, sss1, sst1, tilenum);
      if (convert)
        t0 = *prev;
      t0.r = SIGN(t0.r, 9);
      t0.g = SIGN(t0.g, 9); 
      t0.b = SIGN(t0.b, 9);
      TEX->r = t0.b + ((((newk0 - invk0) * t0.g) + 0x80) >> 8);
      TEX->g = t0.b + ((((newk1 - invk1) * t0.r + (newk2 - invk2) * t0.g) + 0x80) >> 8);
      TEX->b = t0.b + ((((newk3 - invk3) * t0.r) + 0x80) >> 8);
      TEX->a = t0.b;
    }
    
    TEX->r &= 0x1ff;
    TEX->g &= 0x1ff;
    TEX->b &= 0x1ff;
    TEX->a &= 0x1ff;
    
    
  }
  else                                                
  {                                                   
    
    
    

    tcclamp_cycle_light(&sss1, &sst1, maxs, maxt, tilenum);
    
        tcmask(&sss1, &sst1, tilenum);  
                                                    
      
    if (!other_modes.en_tlut)
      fetch_texel(&t0, sss1, sst1, tilenum);
    else
      fetch_texel_entlut(&t0, sss1, sst1, tilenum);
    
    if (bilerp)
    {
      if (!convert)
        *TEX = t0;
      else
        TEX->r = TEX->g = TEX->b = TEX->a = prev->b;
    }
    else
    {
      newk0 = SIGN(k0, 9); 
      newk1 = SIGN(k1, 9); 
      newk2 = SIGN(k2, 9); 
      newk3 = SIGN(k3, 9);
      invk0 = ~newk0; 
      invk1 = ~newk1; 
      invk2 = ~newk2; 
      invk3 = ~newk3;
      if (convert)
        t0 = *prev;
      t0.r = SIGN(t0.r, 9);
      t0.g = SIGN(t0.g, 9); 
      t0.b = SIGN(t0.b, 9);
      TEX->r = t0.b + ((((newk0 - invk0) * t0.g) + 0x80) >> 8);
      TEX->g = t0.b + ((((newk1 - invk1) * t0.r + (newk2 - invk2) * t0.g) + 0x80) >> 8);
      TEX->b = t0.b + ((((newk3 - invk3) * t0.r) + 0x80) >> 8);
      TEX->a = t0.b;
      TEX->r &= 0x1ff;
      TEX->g &= 0x1ff;
      TEX->b &= 0x1ff;
      TEX->a &= 0x1ff;
    }
  }
                                                  
}

static void tc_pipeline_copy(int32_t* sss0, int32_t* sss1, int32_t* sss2, int32_t* sss3, int32_t* sst, int tilenum)                     
{
  int ss0 = *sss0, ss1 = 0, ss2 = 0, ss3 = 0, st = *sst;

  tcshift_copy(&ss0, &st, tilenum);
  
  

  ss0 = TRELATIVE(ss0, tile[tilenum].sl);
  st = TRELATIVE(st, tile[tilenum].tl);
  ss0 = (ss0 >> 5);
  st = (st >> 5);

  ss1 = ss0 + 1;
  ss2 = ss0 + 2;
  ss3 = ss0 + 3;

  tcmask_copy(&ss0, &ss1, &ss2, &ss3, &st, tilenum);  

  *sss0 = ss0;
  *sss1 = ss1;
  *sss2 = ss2;
  *sss3 = ss3;
  *sst = st;
}

static void tc_pipeline_load(int32_t* sss, int32_t* sst, int tilenum, int coord_quad)
{
  int sss1 = *sss, sst1 = *sst;
  sss1 = SIGN16(sss1);
  sst1 = SIGN16(sst1);

  
  sss1 = TRELATIVE(sss1, tile[tilenum].sl);
  sst1 = TRELATIVE(sst1, tile[tilenum].tl);
  

  
  if (!coord_quad)
  {
    sss1 = (sss1 >> 5);
    sst1 = (sst1 >> 5);
  }
  else
  {
    sss1 = (sss1 >> 3);
    sst1 = (sst1 >> 3);
  }
  
  *sss = sss1;
  *sst = sst1;
}

void render_spans_1cycle_complete(int start, int end, int tilenum, int flip)
{
  int zb = zb_address >> 1;
  int zbcur;
  uint8_t offx, offy;
  SPANSIGS sigs;
  uint32_t blend_en;
  uint32_t prewrap;
  uint32_t curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

  int prim_tile = tilenum;
  int tile1 = tilenum;
  int newtile = tilenum; 
  int news, newt;

  int i, j;
  
  int32_t dincs[8];
  int32_t xinc;

  FlipSigns(dincs, spans, flip);
  assert((flip & 1) == flip);
  xinc = FlipLUT[flip];

  int dzpix;
  if (!other_modes.z_source_sel)
    dzpix = spans_dzpix;
  else
  {
    dzpix = primitive_delta_z;
    dincs[SPAN_DZ] = spans_cdz = spans_dzdy = 0;
  }
  int dzpixenc = dz_compress(dzpix);

  int cdith = 7, adith = 0;
  int r, g, b, a, z, s, t, w;
  int sr, sg, sb, sa, sz, ss, st, sw;
  int xstart, xend, xendsc;
  int sss = 0, sst = 0;
  int32_t prelodfrac;
  int curpixel = 0;
  int x, length, scdiff;
  uint32_t fir, fig, fib;
          
  for (i = start; i <= end; i++)
  {
    if (span[i].validline)
    {

    xstart = span[i].lx;
    xend = span[i].unscrx;
    xendsc = span[i].rx;
    r = span[i].r;
    g = span[i].g;
    b = span[i].b;
    a = span[i].a;
    z = other_modes.z_source_sel ? primitive_z : span[i].z;
    s = span[i].s;
    t = span[i].t;
    w = span[i].w;

    x = xendsc;
    curpixel = fb_width * i + x;
    zbcur = zb + curpixel;

    if (!flip)
    {
      length = xendsc - xstart;
      scdiff = xend - xendsc;
      compute_cvg_noflip(i);
    }
    else
    {
      length = xstart - xendsc;
      scdiff = xendsc - xend;
      compute_cvg_flip(i);
    }
    
    sigs.longspan = (length > 7);
    sigs.midspan = (length == 7);

    
    
    if (scdiff)
    {
      r += (dincs[SPAN_DR] * scdiff);
      g += (dincs[SPAN_DG] * scdiff);
      b += (dincs[SPAN_DB] * scdiff);
      a += (dincs[SPAN_DA] * scdiff);
      z += (dincs[SPAN_DZ] * scdiff);
      s += (dincs[SPAN_DS] * scdiff);
      t += (dincs[SPAN_DT] * scdiff);
      w += (dincs[SPAN_DW] * scdiff);
    }
    sigs.startspan = 1;

    for (j = 0; j <= length; j++)
    {
      sr = r >> 14;
      sg = g >> 14;
      sb = b >> 14;
      sa = a >> 14;
      ss = s >> 16;
      st = t >> 16;
      sw = w >> 16;
      sz = (z >> 10) & 0x3fffff;
      

      sigs.endspan = (j == length);
      sigs.preendspan = (j == (length - 1));

      lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);
      

      get_texel1_1cycle(&news, &newt, s, t, w, dincs[SPAN_DS], dincs[SPAN_DT], dincs[SPAN_DW], i, &sigs);

      
      
      if (!sigs.startspan)
      {
        texel0_color = texel1_color;
        lod_frac = prelodfrac;
      }
      else
      {
        tcdiv_ptr(ss, st, sw, &sss, &sst);

        
        tclod_1cycle_current(&sss, &sst, news, newt, s, t, w, dincs[SPAN_DS], dincs[SPAN_DT], dincs[SPAN_DW], i, prim_tile, &tile1, &sigs);
        
        
        
        
        texture_pipeline_cycle(&texel0_color, &texel0_color, sss, sst, tile1, 0);

        
        sigs.startspan = 0;
      }
      
      sigs.nextspan = sigs.endspan;
      sigs.endspan = sigs.preendspan;
      sigs.preendspan = (j == (length - 2));

      s += dincs[SPAN_DS];
      t += dincs[SPAN_DT];
      w += dincs[SPAN_DW];
      
      tclod_1cycle_next(&news, &newt, s, t, w, dincs[SPAN_DS], dincs[SPAN_DT], dincs[SPAN_DW], i, prim_tile, &newtile, &sigs, &prelodfrac);      
      
      texture_pipeline_cycle(&texel1_color, &texel1_color, news, newt, newtile, 0);

      rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);

      get_dither_noise_ptr(x, i, &cdith, &adith);
      combiner_1cycle(adith, &curpixel_cvg);
        
      fbread1_ptr(curpixel, &curpixel_memcvg);
      if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
      {
        if (blender_1cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
        {
          fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
          if (other_modes.z_update_en)
            z_store(zbcur, sz, dzpixenc);
        }
      }

      
      
      
      r += dincs[SPAN_DR];
      g += dincs[SPAN_DG];
      b += dincs[SPAN_DB];
      a += dincs[SPAN_DA];
      z += dincs[SPAN_DZ];
      
      x += xinc;
      curpixel += xinc;
      zbcur += xinc;
    }
    }
  }
}

void render_spans_1cycle_notexel1(int start, int end, int tilenum, int flip)
{
  int zb = zb_address >> 1;
  int zbcur;
  uint8_t offx, offy;
  SPANSIGS sigs;
  uint32_t blend_en;
  uint32_t prewrap;
  uint32_t curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

  int prim_tile = tilenum;
  int tile1 = tilenum;

  int i, j;

  static int32_t accum[8] align(16);
  static int32_t localspan[8] align(16);
  static int32_t slocalspan[8] align(16);
  static int32_t dincs[8] align(16);
  int32_t xinc;

  FlipSigns(dincs, spans, flip);
  assert((flip & 1) == flip);
  xinc = FlipLUT[flip];

  int dzpix;
  if (!other_modes.z_source_sel)
    dzpix = spans_dzpix;
  else
  {
    dzpix = primitive_delta_z;
    dincs[SPAN_DZ] = spans_cdz = spans_dzdy = 0;
  }
  int dzpixenc = dz_compress(dzpix);

  int cdith = 7, adith = 0;
  int xstart, xend, xendsc;
  int sss = 0, sst = 0;
  int curpixel = 0;
  int x, length, scdiff;
  uint32_t fir, fig, fib;
          
  for (i = start; i <= end; i++) {
    if (!span[i].validline)
      continue;

  xstart = span[i].lx;
  xend = span[i].unscrx;
  xendsc = span[i].rx;

  memcpy(localspan, &span[i].r, sizeof(localspan));
  localspan[SPAN_DZ] = other_modes.z_source_sel ? primitive_z : span[i].z;

  x = xendsc;
  curpixel = fb_width * i + x;
  zbcur = zb + curpixel;

  if (!flip) {
    length = xendsc - xstart;
    scdiff = xend - xendsc;
    compute_cvg_noflip(i);
  }

  else {
    length = xstart - xendsc;
    scdiff = xendsc - xend;
    compute_cvg_flip(i);
  }

  sigs.longspan = (length > 7);
  sigs.midspan = (length == 7);

  MulConstant(accum, dincs, scdiff);
  AddVectors(localspan, localspan, accum);

    for (j = 0; j <= length; j++) {
#ifdef USE_SSE
      __m128i data1 = _mm_load_si128((__m128i*) (localspan + 0));
      __m128i data2 = _mm_load_si128((__m128i*) (localspan + 4));
      data1 = _mm_srai_epi32(data1, 14);
      data2 = _mm_srai_epi32(data2, 16);
      _mm_store_si128((__m128i*) (slocalspan + 0), data1);
      _mm_store_si128((__m128i*) (slocalspan + 4), data2);
      slocalspan[SPAN_DZ] = (localspan[SPAN_DZ] >> 10) & 0x3FFFFF;
#else
      slocalspan[SPAN_DR] = localspan[SPAN_DR] >> 14;
      slocalspan[SPAN_DG] = localspan[SPAN_DG] >> 14;
      slocalspan[SPAN_DB] = localspan[SPAN_DB] >> 14;
      slocalspan[SPAN_DA] = localspan[SPAN_DA] >> 14;
      slocalspan[SPAN_DS] = localspan[SPAN_DS] >> 16;
      slocalspan[SPAN_DT] = localspan[SPAN_DT] >> 16;
      slocalspan[SPAN_DW] = localspan[SPAN_DW] >> 16;
      slocalspan[SPAN_DZ] = (localspan[SPAN_DZ] >> 10) & 0x3fffff;
#endif

      sigs.endspan = (j == length);
      sigs.preendspan = (j == (length - 1));

      lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);

      tcdiv_ptr(slocalspan[SPAN_DS], slocalspan[SPAN_DT], slocalspan[SPAN_DW], &sss, &sst);

      tclod_1cycle_current_simple(&sss, &sst, localspan + SPAN_DS, dincs + SPAN_DS, i, prim_tile, &tile1, &sigs);

      texture_pipeline_cycle(&texel0_color, &texel0_color, sss, sst, tile1, 0);

      rgbaz_correct_clip(offx, offy, slocalspan[SPAN_DR], slocalspan[SPAN_DG], slocalspan[SPAN_DB], slocalspan[SPAN_DA], &slocalspan[SPAN_DZ], curpixel_cvg);

      get_dither_noise_ptr(x, i, &cdith, &adith);
      combiner_1cycle(adith, &curpixel_cvg);
        
      fbread1_ptr(curpixel, &curpixel_memcvg);
      if (z_compare(zbcur, slocalspan[SPAN_DZ], dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
      {
        if (blender_1cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
        {
          fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
          if (other_modes.z_update_en)
            z_store(zbcur, slocalspan[SPAN_DZ], dzpixenc);
        }
      }

      AddVectors(localspan, localspan, dincs);
      
      x += xinc;
      curpixel += xinc;
      zbcur += xinc;
    }
  }
}

void render_spans_1cycle_notex(int start, int end, int tilenum, int flip)
{
  int zb = zb_address >> 1;
  int zbcur;
  uint8_t offx, offy;
  uint32_t blend_en;
  uint32_t prewrap;
  uint32_t curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

  int i, j;

  int32_t dincs[8];
  int32_t xinc;

  FlipSigns(dincs, spans, flip);
  assert((flip & 1) == flip);
  xinc = FlipLUT[flip];
  
  int dzpix;
  if (!other_modes.z_source_sel)
    dzpix = spans_dzpix;
  else
  {
    dzpix = primitive_delta_z;
    dincs[SPAN_DZ] = spans_cdz = spans_dzdy = 0;
  }
  int dzpixenc = dz_compress(dzpix);

  int cdith = 7, adith = 0;
  int r, g, b, a, z;
  int sr, sg, sb, sa, sz;
  int xstart, xend, xendsc;
  int curpixel = 0;
  int x, length, scdiff;
  uint32_t fir, fig, fib;
          
  for (i = start; i <= end; i++)
  {
    if (span[i].validline)
    {

    xstart = span[i].lx;
    xend = span[i].unscrx;
    xendsc = span[i].rx;
    r = span[i].r;
    g = span[i].g;
    b = span[i].b;
    a = span[i].a;
    z = other_modes.z_source_sel ? primitive_z : span[i].z;

    x = xendsc;
    curpixel = fb_width * i + x;
    zbcur = zb + curpixel;

    if (!flip)
    {
      length = xendsc - xstart;
      scdiff = xend - xendsc;
      compute_cvg_noflip(i);
    }
    else
    {
      length = xstart - xendsc;
      scdiff = xendsc - xend;
      compute_cvg_flip(i);
    }

    if (scdiff)
    {
      r += (dincs[SPAN_DR] * scdiff);
      g += (dincs[SPAN_DG] * scdiff);
      b += (dincs[SPAN_DB] * scdiff);
      a += (dincs[SPAN_DA] * scdiff);
      z += (dincs[SPAN_DZ] * scdiff);
    }

    for (j = 0; j <= length; j++)
    {
      sr = r >> 14;
      sg = g >> 14;
      sb = b >> 14;
      sa = a >> 14;
      sz = (z >> 10) & 0x3fffff;

      lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);

      rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);

      get_dither_noise_ptr(x, i, &cdith, &adith);
      combiner_1cycle(adith, &curpixel_cvg);
        
      fbread1_ptr(curpixel, &curpixel_memcvg);
      if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
      {
        if (blender_1cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
        {
          fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
          if (other_modes.z_update_en)
            z_store(zbcur, sz, dzpixenc);
        }
      }
      r += dincs[SPAN_DR];
      g += dincs[SPAN_DG];
      b += dincs[SPAN_DB];
      a += dincs[SPAN_DA];
      z += dincs[SPAN_DZ];
      
      x += xinc;
      curpixel += xinc;
      zbcur += xinc;
    }
    }
  }
}

void render_spans_2cycle_complete(int start, int end, int tilenum, int flip)
{
  int zb = zb_address >> 1;
  int zbcur;
  uint8_t offx, offy;
  SPANSIGS sigs;
  int32_t prelodfrac;
  COLOR nexttexel1_color;
  uint32_t blend_en;
  uint32_t prewrap;
  uint32_t curpixel_cvg, curpixel_cvbit, curpixel_memcvg;
  
  int tile2 = (tilenum + 1) & 7;
  int tile1 = tilenum;
  int prim_tile = tilenum;

  int newtile1 = tile1;
  int newtile2 = tile2;
  int news, newt;

  int i, j;

  int32_t dincs[8];
  int32_t xinc;

  FlipSigns(dincs, spans, flip);
  assert((flip & 1) == flip);
  xinc = FlipLUT[flip];

  int dzpix;
  if (!other_modes.z_source_sel)
    dzpix = spans_dzpix;
  else
  {
    dzpix = primitive_delta_z;
    dincs[SPAN_DZ] = spans_cdz = spans_dzdy = 0;
  }
  int dzpixenc = dz_compress(dzpix);

  int cdith = 7, adith = 0;
  int r, g, b, a, z, s, t, w;
  int sr, sg, sb, sa, sz, ss, st, sw;
  int xstart, xend, xendsc;
  int sss = 0, sst = 0;
  int curpixel = 0;
  
  int x, length, scdiff;
  uint32_t fir, fig, fib;
        
  for (i = start; i <= end; i++)
  {
    if (span[i].validline)
    {

    xstart = span[i].lx;
    xend = span[i].unscrx;
    xendsc = span[i].rx;
    r = span[i].r;
    g = span[i].g;
    b = span[i].b;
    a = span[i].a;
    z = other_modes.z_source_sel ? primitive_z : span[i].z;
    s = span[i].s;
    t = span[i].t;
    w = span[i].w;

    x = xendsc;
    curpixel = fb_width * i + x;
    zbcur = zb + curpixel;

    if (!flip)
    {
      length = xendsc - xstart;
      scdiff = xend - xendsc;
      compute_cvg_noflip(i);
    }
    else
    {
      length = xstart - xendsc;
      scdiff = xendsc - xend;
      compute_cvg_flip(i);
    }

    
    

    
    

    

    if (scdiff)
    {
      r += (dincs[SPAN_DR] * scdiff);
      g += (dincs[SPAN_DG] * scdiff);
      b += (dincs[SPAN_DB] * scdiff);
      a += (dincs[SPAN_DA] * scdiff);
      z += (dincs[SPAN_DZ] * scdiff);
      s += (dincs[SPAN_DS] * scdiff);
      t += (dincs[SPAN_DT] * scdiff);
      w += (dincs[SPAN_DW] * scdiff);
    }
    sigs.startspan = 1;

    for (j = 0; j <= length; j++)
    {
      sr = r >> 14;
      sg = g >> 14;
      sb = b >> 14;
      sa = a >> 14;
      ss = s >> 16;
      st = t >> 16;
      sw = w >> 16;
      sz = (z >> 10) & 0x3fffff;
      

      lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);

      get_nexttexel0_2cycle(&news, &newt, s, t, w, dincs[SPAN_DS], dincs[SPAN_DT], dincs[SPAN_DW]);
      
      if (!sigs.startspan)
      {
        lod_frac = prelodfrac;
        texel0_color = nexttexel_color;
        texel1_color = nexttexel1_color;
      }
      else
      {
        tcdiv_ptr(ss, st, sw, &sss, &sst);

        tclod_2cycle_current(&sss, &sst, news, newt, s, t, w, dincs[SPAN_DS], dincs[SPAN_DT], dincs[SPAN_DW], prim_tile, &tile1, &tile2);
        

        
        texture_pipeline_cycle(&texel0_color, &texel0_color, sss, sst, tile1, 0);
        texture_pipeline_cycle(&texel1_color, &texel0_color, sss, sst, tile2, 1);

        sigs.startspan = 0;
      }

      s += dincs[SPAN_DS];
      t += dincs[SPAN_DT];
      w += dincs[SPAN_DW];

      tclod_2cycle_next(&news, &newt, s, t, w, dincs[SPAN_DS], dincs[SPAN_DT], dincs[SPAN_DW], prim_tile, &newtile1, &newtile2, &prelodfrac);

      texture_pipeline_cycle(&nexttexel_color, &nexttexel_color, news, newt, newtile1, 0);
      texture_pipeline_cycle(&nexttexel1_color, &nexttexel_color, news, newt, newtile2, 1);

      rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);
          
      get_dither_noise_ptr(x, i, &cdith, &adith);
      combiner_2cycle(adith, &curpixel_cvg);
        
      fbread2_ptr(curpixel, &curpixel_memcvg);
      
      
      
      
      
      
      if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
      {
        if (blender_2cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
        {
          fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
          if (other_modes.z_update_en)
            z_store(zbcur, sz, dzpixenc);
          
        }
      }
      
      
      
      
      
      
      
      
      
      

      memory_color = pre_memory_color;
      pastblshifta = blshifta;
      pastblshiftb = blshiftb;
      r += dincs[SPAN_DR];
      g += dincs[SPAN_DG];
      b += dincs[SPAN_DB];
      a += dincs[SPAN_DA];
      z += dincs[SPAN_DZ];
      
      x += xinc;
      curpixel += xinc;
      zbcur += xinc;
    }
    }
  }
}

void render_spans_2cycle_notexelnext(int start, int end, int tilenum, int flip)
{
  int zb = zb_address >> 1;
  int zbcur;
  uint8_t offx, offy;
  uint32_t blend_en;
  uint32_t prewrap;
  uint32_t curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

  int tile2 = (tilenum + 1) & 7;
  int tile1 = tilenum;
  int prim_tile = tilenum;

  int i, j;

  int32_t dincs[8];
  int32_t xinc;

  FlipSigns(dincs, spans, flip);
  assert((flip & 1) == flip);
  xinc = FlipLUT[flip];

  int dzpix;
  if (!other_modes.z_source_sel)
    dzpix = spans_dzpix;
  else
  {
    dzpix = primitive_delta_z;
    dincs[SPAN_DZ] = spans_cdz = spans_dzdy = 0;
  }
  int dzpixenc = dz_compress(dzpix);

  int cdith = 7, adith = 0;
  int r, g, b, a, z, s, t, w;
  int sr, sg, sb, sa, sz, ss, st, sw;
  int xstart, xend, xendsc;
  int sss = 0, sst = 0;
  int curpixel = 0;
  
  int x, length, scdiff;
  uint32_t fir, fig, fib;
        
  for (i = start; i <= end; i++)
  {
    if (span[i].validline)
    {

    xstart = span[i].lx;
    xend = span[i].unscrx;
    xendsc = span[i].rx;
    r = span[i].r;
    g = span[i].g;
    b = span[i].b;
    a = span[i].a;
    z = other_modes.z_source_sel ? primitive_z : span[i].z;
    s = span[i].s;
    t = span[i].t;
    w = span[i].w;

    x = xendsc;
    curpixel = fb_width * i + x;
    zbcur = zb + curpixel;

    if (!flip)
    {
      length = xendsc - xstart;
      scdiff = xend - xendsc;
      compute_cvg_noflip(i);
    }
    else
    {
      length = xstart - xendsc;
      scdiff = xendsc - xend;
      compute_cvg_flip(i);
    }

    if (scdiff)
    {
      r += (dincs[SPAN_DR] * scdiff);
      g += (dincs[SPAN_DG] * scdiff);
      b += (dincs[SPAN_DB] * scdiff);
      a += (dincs[SPAN_DA] * scdiff);
      z += (dincs[SPAN_DZ] * scdiff);
      s += (dincs[SPAN_DS] * scdiff);
      t += (dincs[SPAN_DT] * scdiff);
      w += (dincs[SPAN_DW] * scdiff);
    }

    for (j = 0; j <= length; j++)
    {
      sr = r >> 14;
      sg = g >> 14;
      sb = b >> 14;
      sa = a >> 14;
      ss = s >> 16;
      st = t >> 16;
      sw = w >> 16;
      sz = (z >> 10) & 0x3fffff;

      lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);
      
      tcdiv_ptr(ss, st, sw, &sss, &sst);

      tclod_2cycle_current_simple(&sss, &sst, s, t, w, dincs[SPAN_DS], dincs[SPAN_DT], dincs[SPAN_DW], prim_tile, &tile1, &tile2);
        
      texture_pipeline_cycle(&texel0_color, &texel0_color, sss, sst, tile1, 0);
      texture_pipeline_cycle(&texel1_color, &texel0_color, sss, sst, tile2, 1);

      rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);
          
      get_dither_noise_ptr(x, i, &cdith, &adith);
      combiner_2cycle(adith, &curpixel_cvg);
        
      fbread2_ptr(curpixel, &curpixel_memcvg);

      if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
      {
        if (blender_2cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
        {
          fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
          if (other_modes.z_update_en)
            z_store(zbcur, sz, dzpixenc);
        }
      }

      memory_color = pre_memory_color;
      pastblshifta = blshifta;
      pastblshiftb = blshiftb;
      s += dincs[SPAN_DS];
      t += dincs[SPAN_DT];
      w += dincs[SPAN_DW];
      r += dincs[SPAN_DR];
      g += dincs[SPAN_DG];
      b += dincs[SPAN_DB];
      a += dincs[SPAN_DA];
      z += dincs[SPAN_DZ];
      
      x += xinc;
      curpixel += xinc;
      zbcur += xinc;
    }
    }
  }
}

void render_spans_2cycle_notexel1(int start, int end, int tilenum, int flip)
{
  int zb = zb_address >> 1;
  int zbcur;
  uint8_t offx, offy;
  uint32_t blend_en;
  uint32_t prewrap;
  uint32_t curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

  int tile1 = tilenum;
  int prim_tile = tilenum;

  int i, j;

  int32_t dincs[8];
  int xinc;

  FlipSigns(dincs, spans, flip);
  assert((flip & 1) == flip);
  xinc = FlipLUT[flip];

  int dzpix;
  if (!other_modes.z_source_sel)
    dzpix = spans_dzpix;
  else
  {
    dzpix = primitive_delta_z;
    dincs[SPAN_DZ] = spans_cdz = spans_dzdy = 0;
  }
  int dzpixenc = dz_compress(dzpix);

  int cdith = 7, adith = 0;
  int r, g, b, a, z, s, t, w;
  int sr, sg, sb, sa, sz, ss, st, sw;
  int xstart, xend, xendsc;
  int sss = 0, sst = 0;
  int curpixel = 0;
  
  int x, length, scdiff;
  uint32_t fir, fig, fib;
        
  for (i = start; i <= end; i++)
  {
    if (span[i].validline)
    {

    xstart = span[i].lx;
    xend = span[i].unscrx;
    xendsc = span[i].rx;
    r = span[i].r;
    g = span[i].g;
    b = span[i].b;
    a = span[i].a;
    z = other_modes.z_source_sel ? primitive_z : span[i].z;
    s = span[i].s;
    t = span[i].t;
    w = span[i].w;

    x = xendsc;
    curpixel = fb_width * i + x;
    zbcur = zb + curpixel;

    if (!flip)
    {
      length = xendsc - xstart;
      scdiff = xend - xendsc;
      compute_cvg_noflip(i);
    }
    else
    {
      length = xstart - xendsc;
      scdiff = xendsc - xend;
      compute_cvg_flip(i);
    }

    if (scdiff)
    {
      r += (dincs[SPAN_DR] * scdiff);
      g += (dincs[SPAN_DG] * scdiff);
      b += (dincs[SPAN_DB] * scdiff);
      a += (dincs[SPAN_DA] * scdiff);
      z += (dincs[SPAN_DZ] * scdiff);
      s += (dincs[SPAN_DS] * scdiff);
      t += (dincs[SPAN_DT] * scdiff);
      w += (dincs[SPAN_DW] * scdiff);
    }

    for (j = 0; j <= length; j++)
    {
      sr = r >> 14;
      sg = g >> 14;
      sb = b >> 14;
      sa = a >> 14;
      ss = s >> 16;
      st = t >> 16;
      sw = w >> 16;
      sz = (z >> 10) & 0x3fffff;

      lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);
      
      tcdiv_ptr(ss, st, sw, &sss, &sst);

      tclod_2cycle_current_notexel1(&sss, &sst, s, t, w, dincs[SPAN_DS], dincs[SPAN_DT], dincs[SPAN_DW], prim_tile, &tile1);
      
      
      texture_pipeline_cycle(&texel0_color, &texel0_color, sss, sst, tile1, 0);

      rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);
          
      get_dither_noise_ptr(x, i, &cdith, &adith);
      combiner_2cycle(adith, &curpixel_cvg);
        
      fbread2_ptr(curpixel, &curpixel_memcvg);

      if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
      {
        if (blender_2cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
        {
          fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
          if (other_modes.z_update_en)
            z_store(zbcur, sz, dzpixenc);
        }
      }

      memory_color = pre_memory_color;
      pastblshifta = blshifta;
      pastblshiftb = blshiftb;
      s += dincs[SPAN_DS];
      t += dincs[SPAN_DT];
      w += dincs[SPAN_DW];
      r += dincs[SPAN_DR];
      g += dincs[SPAN_DG];
      b += dincs[SPAN_DB];
      a += dincs[SPAN_DA];
      z += dincs[SPAN_DZ];
      
      x += xinc;
      curpixel += xinc;
      zbcur += xinc;
    }
    }
  }
}

void render_spans_2cycle_notex(int start, int end, int tilenum, int flip)
{
  int zb = zb_address >> 1;
  int zbcur;
  uint8_t offx, offy;
  int i, j;
  uint32_t blend_en;
  uint32_t prewrap;
  uint32_t curpixel_cvg, curpixel_cvbit, curpixel_memcvg;

  int32_t dincs[8];
  int32_t xinc;

  FlipSigns(dincs, spans, flip);
  assert((flip & 1) == flip);
  xinc = FlipLUT[flip];

  int dzpix;
  if (!other_modes.z_source_sel)
    dzpix = spans_dzpix;
  else
  {
    dzpix = primitive_delta_z;
    dincs[SPAN_DZ] = spans_cdz = spans_dzdy = 0;
  }
  int dzpixenc = dz_compress(dzpix);

  int cdith = 7, adith = 0;
  int r, g, b, a, z;
  int sr, sg, sb, sa, sz;
  int xstart, xend, xendsc;
  int curpixel = 0;
  
  int x, length, scdiff;
  uint32_t fir, fig, fib;
        
  for (i = start; i <= end; i++)
  {
    if (span[i].validline)
    {

    xstart = span[i].lx;
    xend = span[i].unscrx;
    xendsc = span[i].rx;
    r = span[i].r;
    g = span[i].g;
    b = span[i].b;
    a = span[i].a;
    z = other_modes.z_source_sel ? primitive_z : span[i].z;

    x = xendsc;
    curpixel = fb_width * i + x;
    zbcur = zb + curpixel;

    if (!flip)
    {
      length = xendsc - xstart;
      scdiff = xend - xendsc;
      compute_cvg_noflip(i);
    }
    else
    {
      length = xstart - xendsc;
      scdiff = xendsc - xend;
      compute_cvg_flip(i);
    }

    if (scdiff)
    {
      r += (dincs[SPAN_DR] * scdiff);
      g += (dincs[SPAN_DG] * scdiff);
      b += (dincs[SPAN_DB] * scdiff);
      a += (dincs[SPAN_DA] * scdiff);
      z += (dincs[SPAN_DZ] * scdiff);
    }

    for (j = 0; j <= length; j++)
    {
      sr = r >> 14;
      sg = g >> 14;
      sb = b >> 14;
      sa = a >> 14;
      sz = (z >> 10) & 0x3fffff;

      lookup_cvmask_derivatives(cvgbuf[x], &offx, &offy, &curpixel_cvg, &curpixel_cvbit);

      rgbaz_correct_clip(offx, offy, sr, sg, sb, sa, &sz, curpixel_cvg);
          
      get_dither_noise_ptr(x, i, &cdith, &adith);
      combiner_2cycle(adith, &curpixel_cvg);
        
      fbread2_ptr(curpixel, &curpixel_memcvg);

      if (z_compare(zbcur, sz, dzpix, dzpixenc, &blend_en, &prewrap, &curpixel_cvg, curpixel_memcvg))
      {
        if (blender_2cycle(&fir, &fig, &fib, cdith, blend_en, prewrap, curpixel_cvg, curpixel_cvbit))
        {
          fbwrite_ptr(curpixel, fir, fig, fib, blend_en, curpixel_cvg, curpixel_memcvg);
          if (other_modes.z_update_en)
            z_store(zbcur, sz, dzpixenc);
        }
      }

      memory_color = pre_memory_color;
      pastblshifta = blshifta;
      pastblshiftb = blshiftb;
      r += dincs[SPAN_DR];
      g += dincs[SPAN_DG];
      b += dincs[SPAN_DB];
      a += dincs[SPAN_DA];
      z += dincs[SPAN_DZ];
      
      x += xinc;
      curpixel += xinc;
      zbcur += xinc;
    }
    }
  }
}

void render_spans_fill(int start, int end, int flip)
{
  if (fb_size == PIXEL_SIZE_4BIT) {
#ifndef NDEBUG
    debug("Pipeline crashed.");
#endif
    return;
  }

  int i, j;
  int xinc = flip ? 1 : -1;

  int xstart = 0, xendsc;
  int curpixel = 0;
  int x, length;
        
  for (i = start; i <= end; i++) {
    xstart = span[i].lx;
    xendsc = span[i].rx;

    x = xendsc;
    curpixel = fb_width * i + x;
    length = flip ? (xstart - xendsc) : (xendsc - xstart);

    if (span[i].validline) {
#ifndef NDEBUG
      int fastkillbits = other_modes.image_read_en ||
        other_modes.z_compare_en;

      if (fastkillbits && length >= 0) {
        debug("render_spans_fill: Pipeline crashed.");
        return;
      }
#endif
      
      for (j = 0; j <= length; j++) {
        fbfill_ptr(curpixel);
        x += xinc;
        curpixel += xinc;
      }

#ifndef NDEBUG
      int slowkillbits = other_modes.z_update_en &&
        !other_modes.z_source_sel && !fastkillbits;

      if (slowkillbits && length >= 0) {
        debug("render_spans_fill: Pipeline crashed.");
        return;
      }
#endif
    }
  }
}

void render_spans_copy(int start, int end, int tilenum, int flip)
{
  int i, j, k;

#ifndef NDEBUG
  if (fb_size == PIXEL_SIZE_32BIT) {
    debug("render_spans_copy: Pipeline crashed.");
    return;
  }
#endif
  
  int tile1 = tilenum;
  int prim_tile = tilenum;

  int32_t dincs[8];
  int32_t xinc;

  FlipSigns(dincs, spans, flip);
  assert((flip & 1) == flip);
  xinc = FlipLUT[flip];

  int xstart = 0, xendsc;
  int s = 0, t = 0, w = 0, ss = 0, st = 0, sw = 0, sss = 0, sst = 0;
  int fb_index, length;

  uint32_t hidword = 0, lowdword = 0;
  int fbadvance = (fb_size == PIXEL_SIZE_4BIT) ? 8 : 16 >> fb_size;
  uint32_t fbptr = 0;
  int fbptr_advance = flip ? 8 : -8;
  uint64_t copyqword = 0;
  uint32_t tempdword = 0, tempbyte = 0;
  int copywmask = 0, alphamask = 0;
  int bytesperpixel = (fb_size == PIXEL_SIZE_4BIT) ? 1 : (1 << (fb_size - 1));
  uint32_t fbendptr = 0;
  int32_t threshold, currthreshold;

#define PIXELS_TO_BYTES_SPECIAL4(pix, siz) ((siz) ? PIXELS_TO_BYTES(pix, siz) : (pix))
        
  for (i = start; i <= end; i++)
  {
    if (span[i].validline)
    {

    s = span[i].s;
    t = span[i].t;
    w = span[i].w;
    
    xstart = span[i].lx;
    xendsc = span[i].rx;

    fb_index = fb_width * i + xendsc;
    fbptr = fb_address + PIXELS_TO_BYTES_SPECIAL4(fb_index, fb_size);
    fbendptr = fb_address + PIXELS_TO_BYTES_SPECIAL4((fb_width * i + xstart), fb_size);
    length = flip ? (xstart - xendsc) : (xendsc - xstart);

    
    

    for (j = 0; j <= length; j += fbadvance)
    {
      ss = s >> 16;
      st = t >> 16;
      sw = w >> 16;

      tcdiv_ptr(ss, st, sw, &sss, &sst);
      
      tclod_copy(&sss, &sst, s, t, w, dincs[SPAN_DS], dincs[SPAN_DT], dincs[SPAN_DW], prim_tile, &tile1);
      
      
      
      fetch_qword_copy(&hidword, &lowdword, sss, sst, tile1);

      
      
      if (fb_size == PIXEL_SIZE_16BIT || fb_size == PIXEL_SIZE_8BIT)
        copyqword = ((uint64_t)hidword << 32) | ((uint64_t)lowdword);
      else
        copyqword = 0;
      
      
      if (!other_modes.alpha_compare_en)
        alphamask = 0xff;
      else if (fb_size == PIXEL_SIZE_16BIT)
      {
        alphamask = 0;
        alphamask |= (((copyqword >> 48) & 1) ? 0xC0 : 0);
        alphamask |= (((copyqword >> 32) & 1) ? 0x30 : 0);
        alphamask |= (((copyqword >> 16) & 1) ? 0xC : 0);
        alphamask |= ((copyqword & 1) ? 0x3 : 0);
      }
      else if (fb_size == PIXEL_SIZE_8BIT)
      {
        alphamask = 0;
        threshold = (other_modes.dither_alpha_en) ? (irand() & 0xff) : blend_color.a;
        if (other_modes.dither_alpha_en)
        {
          currthreshold = threshold;
          alphamask |= (((copyqword >> 24) & 0xff) >= currthreshold ? 0xC0 : 0);
          currthreshold = ((threshold & 3) << 6) | (threshold >> 2);
          alphamask |= (((copyqword >> 16) & 0xff) >= currthreshold ? 0x30 : 0);
          currthreshold = ((threshold & 0xf) << 4) | (threshold >> 4);
          alphamask |= (((copyqword >> 8) & 0xff) >= currthreshold ? 0xC : 0);
          currthreshold = ((threshold & 0x3f) << 2) | (threshold >> 6);
          alphamask |= ((copyqword & 0xff) >= currthreshold ? 0x3 : 0); 
        }
        else
        {
          alphamask |= (((copyqword >> 24) & 0xff) >= threshold ? 0xC0 : 0);
          alphamask |= (((copyqword >> 16) & 0xff) >= threshold ? 0x30 : 0);
          alphamask |= (((copyqword >> 8) & 0xff) >= threshold ? 0xC : 0);
          alphamask |= ((copyqword & 0xff) >= threshold ? 0x3 : 0);
        }
      }
      else
        alphamask = 0;

      copywmask = (flip) ? (fbendptr - fbptr + bytesperpixel) : (fbptr - fbendptr + bytesperpixel);
      
      if (copywmask > 8) 
        copywmask = 8;
      tempdword = fbptr;
      k = 7;
      while(copywmask > 0)
      {
        tempbyte = (uint32_t)((copyqword >> (k << 3)) & 0xff);
        if (alphamask & (1 << k))
        {
          PAIRWRITE8(tempdword, tempbyte, (tempbyte & 1) ? 3 : 0);
        }
        k--;
        tempdword += xinc;
        copywmask--;
      }
      
      s += dincs[SPAN_DS];
      t += dincs[SPAN_DT];
      w += dincs[SPAN_DW];
      fbptr += fbptr_advance;
    }
    }
  }
}

void loading_pipeline(int start, int end, int tilenum, int coord_quad, int ltlut)
{
  int i, j;

  int dsinc, dtinc;
  dsinc = spans[SPAN_DS];
  dtinc = spans[SPAN_DT];

  int s, t;
  int ss, st;
  int xstart, xend;
  int sss = 0, sst = 0;
  int ti_index, length;

  uint32_t tmemidx0 = 0, tmemidx1 = 0, tmemidx2 = 0, tmemidx3 = 0;
  int dswap = 0;
  uint16_t* tmem16 = (uint16_t*)TMEM;
  uint32_t readval0, readval1, readval2, readval3;
  uint32_t readidx32;
  uint64_t loadqword;
  uint16_t tempshort;
  int tmem_formatting = 0;
  uint32_t bit3fl = 0, hibit = 0;

#ifndef NDEBUG
  if (end > start && ltlut) {
    debug("loading_pipeline: Pipeline crashed.");
    return;
  }
#endif

  if (tile[tilenum].format == FORMAT_YUV)
    tmem_formatting = 0;
  else if (tile[tilenum].format == FORMAT_RGBA && tile[tilenum].size == PIXEL_SIZE_32BIT)
    tmem_formatting = 1;
  else
    tmem_formatting = 2;

  int tiadvance = 0, spanadvance = 0;
  int tiptr = 0;
  switch (ti_size)
  {
  case PIXEL_SIZE_4BIT:
#ifndef NDEBUG
    debug("loading_pipeline: Pipeline crashed.");
    return;
#endif
    break;
  case PIXEL_SIZE_8BIT:
    tiadvance = 8;
    spanadvance = 8;
    break;
  case PIXEL_SIZE_16BIT:
    if (!ltlut)
    {
      tiadvance = 8;
      spanadvance = 4;
    }
    else
    {
      tiadvance = 2;
      spanadvance = 1;
    }
    break;
  case PIXEL_SIZE_32BIT:
    tiadvance = 8;
    spanadvance = 2;
    break;
  }

  for (i = start; i <= end; i++)
  {
    xstart = span[i].lx;
    xend = span[i].unscrx;
    s = span[i].s;
    t = span[i].t;

    ti_index = ti_width * i + xend;
    tiptr = ti_address + PIXELS_TO_BYTES(ti_index, ti_size);

    length = (xstart - xend + 1) & 0xfff;

    
    for (j = 0; j < length; j+= spanadvance)
    {
      ss = s >> 16;
      st = t >> 16;

      
      
      
      
      
      
      sss = ss & 0xffff;
      sst = st & 0xffff;

      tc_pipeline_load(&sss, &sst, tilenum, coord_quad);

      dswap = sst & 1;

      
      get_tmem_idx(sss, sst, tilenum, &tmemidx0, &tmemidx1, &tmemidx2, &tmemidx3, &bit3fl, &hibit);

      readidx32 = (tiptr >> 2) & ~1;
      readval0 = RREADIDX32(readidx32);
      readval1 = RREADIDX32(readidx32 + 1);
      readval2 = RREADIDX32(readidx32 + 2);
      readval3 = RREADIDX32(readidx32 + 3);

      
      switch(tiptr & 7)
      {
      case 0:
        if (!ltlut)
          loadqword = ((uint64_t)readval0 << 32) | readval1;
        else
        {
          tempshort = readval0 >> 16;
          loadqword = ((uint64_t)tempshort << 48) | ((uint64_t) tempshort << 32) | ((uint64_t) tempshort << 16) | tempshort;
        }
        break;
      case 1:
        loadqword = ((uint64_t)readval0 << 40) | ((uint64_t)readval1 << 8) | (readval2 >> 24);
        break;
      case 2:
        if (!ltlut)
          loadqword = ((uint64_t)readval0 << 48) | ((uint64_t)readval1 << 16) | (readval2 >> 16);
        else
        {
          tempshort = readval0 & 0xffff;
          loadqword = ((uint64_t)tempshort << 48) | ((uint64_t) tempshort << 32) | ((uint64_t) tempshort << 16) | tempshort;
        }
        break;
      case 3:
        loadqword = ((uint64_t)readval0 << 56) | ((uint64_t)readval1 << 24) | (readval2 >> 8);
        break;
      case 4:
        if (!ltlut)
          loadqword = ((uint64_t)readval1 << 32) | readval2;
        else
        {
          tempshort = readval1 >> 16;
          loadqword = ((uint64_t)tempshort << 48) | ((uint64_t) tempshort << 32) | ((uint64_t) tempshort << 16) | tempshort;
        }
        break;
      case 5:
        loadqword = ((uint64_t)readval1 << 40) | ((uint64_t)readval2 << 8) | (readval3 >> 24);
        break;
      case 6:
        if (!ltlut)
          loadqword = ((uint64_t)readval1 << 48) | ((uint64_t)readval2 << 16) | (readval3 >> 16);
        else
        {
          tempshort = readval1 & 0xffff;
          loadqword = ((uint64_t)tempshort << 48) | ((uint64_t) tempshort << 32) | ((uint64_t) tempshort << 16) | tempshort;
        }
        break;
      case 7:
        loadqword = ((uint64_t)readval1 << 56) | ((uint64_t)readval2 << 24) | (readval3 >> 8);
        break;
      }

      
      switch(tmem_formatting)
      {
      case 0:
        readval0 = (uint32_t)((((loadqword >> 56) & 0xff) << 24) | (((loadqword >> 40) & 0xff) << 16) | (((loadqword >> 24) & 0xff) << 8) | (((loadqword >> 8) & 0xff) << 0));
        readval1 = (uint32_t)((((loadqword >> 48) & 0xff) << 24) | (((loadqword >> 32) & 0xff) << 16) | (((loadqword >> 16) & 0xff) << 8) | (((loadqword >> 0) & 0xff) << 0));
        if (bit3fl)
        {
          tmem16[tmemidx2 ^ WORD_ADDR_XOR] = (uint16_t)(readval0 >> 16);
          tmem16[tmemidx3 ^ WORD_ADDR_XOR] = (uint16_t)(readval0 & 0xffff);
          tmem16[(tmemidx2 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(readval1 >> 16);
          tmem16[(tmemidx3 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(readval1 & 0xffff);
        }
        else
        {
          tmem16[tmemidx0 ^ WORD_ADDR_XOR] = (uint16_t)(readval0 >> 16);
          tmem16[tmemidx1 ^ WORD_ADDR_XOR] = (uint16_t)(readval0 & 0xffff);
          tmem16[(tmemidx0 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(readval1 >> 16);
          tmem16[(tmemidx1 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(readval1 & 0xffff);
        }
        break;
      case 1:
        readval0 = (uint32_t)(((loadqword >> 48) << 16) | ((loadqword >> 16) & 0xffff));
        readval1 = (uint32_t)((((loadqword >> 32) & 0xffff) << 16) | (loadqword & 0xffff));

        if (bit3fl)
        {
          tmem16[tmemidx2 ^ WORD_ADDR_XOR] = (uint16_t)(readval0 >> 16);
          tmem16[tmemidx3 ^ WORD_ADDR_XOR] = (uint16_t)(readval0 & 0xffff);
          tmem16[(tmemidx2 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(readval1 >> 16);
          tmem16[(tmemidx3 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(readval1 & 0xffff);
        }
        else
        {
          tmem16[tmemidx0 ^ WORD_ADDR_XOR] = (uint16_t)(readval0 >> 16);
          tmem16[tmemidx1 ^ WORD_ADDR_XOR] = (uint16_t)(readval0 & 0xffff);
          tmem16[(tmemidx0 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(readval1 >> 16);
          tmem16[(tmemidx1 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(readval1 & 0xffff);
        }
        break;
      case 2:
        if (!dswap)
        {
          if (!hibit)
          {
            tmem16[tmemidx0 ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 48);
            tmem16[tmemidx1 ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 32);
            tmem16[tmemidx2 ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 16);
            tmem16[tmemidx3 ^ WORD_ADDR_XOR] = (uint16_t)(loadqword & 0xffff);
          }
          else
          {
            tmem16[(tmemidx0 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 48);
            tmem16[(tmemidx1 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 32);
            tmem16[(tmemidx2 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 16);
            tmem16[(tmemidx3 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(loadqword & 0xffff);
          }
        }
        else
        {
          if (!hibit)
          {
            tmem16[tmemidx0 ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 16);
            tmem16[tmemidx1 ^ WORD_ADDR_XOR] = (uint16_t)(loadqword & 0xffff);
            tmem16[tmemidx2 ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 48);
            tmem16[tmemidx3 ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 32);
          }
          else
          {
            tmem16[(tmemidx0 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 16);
            tmem16[(tmemidx1 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(loadqword & 0xffff);
            tmem16[(tmemidx2 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 48);
            tmem16[(tmemidx3 | 0x400) ^ WORD_ADDR_XOR] = (uint16_t)(loadqword >> 32);
          }
        }
      break;
      }

      s = (s + dsinc) & ~0x1f;
      t = (t + dtinc) & ~0x1f;
      tiptr += tiadvance;
    }
  }
}

enum EdgeWalkerType {
  EW_R,
  EW_G,
  EW_B,
  EW_A,
  EW_S,
  EW_T,
  EW_W,
  EW_Z,
};

enum EdgeWalkerDeType {
  EWDE_DRDE,
  EWDE_DGDE,
  EWDE_DBDE,
  EWDE_DADE,
  EWDE_DSDE,
  EWDE_DTDE,
  EWDE_DWDE,
  EWDE_DZDE,
};

enum EdgeWalkerDehType {
  EWDEH_DRDEH,
  EWDEH_DGDEH,
  EWDEH_DBDEH,
  EWDEH_DADEH,
  EWDEH_DSDEH,
  EWDEH_DTDEH,
  EWDEH_DWDEH,
  EWDEH_DZDEH,
};

enum EdgeWalkerDiffType {
  EWDIFF_DRDIFF,
  EWDIFF_DGDIFF,
  EWDIFF_DBDIFF,
  EWDIFF_DADIFF,
  EWDIFF_DSDIFF,
  EWDIFF_DTDIFF,
  EWDIFF_DWDIFF,
  EWDIFF_DZDIFF,
};

enum EdgeWalkerDxType {
  EWDX_DRDX,
  EWDX_DGDX,
  EWDX_DBDX,
  EWDX_DADX,
  EWDX_DSDX,
  EWDX_DTDX,
  EWDX_DWDX,
  EWDX_DZDX,
};

enum EdgeWalkerDxhType {
  EWDXH_DRDXH,
  EWDXH_DGDXH,
  EWDXH_DBDXH,
  EWDXH_DADXH,
  EWDXH_DSDXH,
  EWDXH_DTDXH,
  EWDXH_DWDXH,
  EWDXH_DZDXH,
};

enum EdgeWalkerDyType {
  EWDY_DRDY,
  EWDY_DGDY,
  EWDY_DBDY,
  EWDY_DADY,
  EWDY_DSDY,
  EWDY_DTDY,
  EWDY_DWDY,
  EWDY_DZDY,
};

enum EdgeWalkerDyhType {
  EWDYH_DRDYH,
  EWDYH_DGDYH,
  EWDYH_DBDYH,
  EWDYH_DADYH,
  EWDYH_DSDYH,
  EWDYH_DTDYH,
  EWDYH_DWDYH,
  EWDYH_DZDYH,
};

static void edgewalker_for_prims(const int32_t* ewdata)
{
  int j = 0;
  int xleft = 0, xright = 0, xleft_inc = 0, xright_inc = 0;
  int tilenum = 0, flip = 0;
  int32_t yl = 0, ym = 0, yh = 0;
  int32_t xl = 0, xm = 0, xh = 0;
  int32_t dxldy = 0, dxhdy = 0, dxmdy = 0;

  static int32_t ewvars[8] align(16);
  static int32_t ewdxvars[8] align(16);
  static int32_t ewdyvars[8] align(16);
  static int32_t ewdevars[8] align(16);

  if (other_modes.f.stalederivs)
  {
    deduce_derivatives();
    other_modes.f.stalederivs = 0;
  }

  flip = (ewdata[0] & 0x800000) ? 1 : 0;
  max_level = (ewdata[0] >> 19) & 7;
  tilenum = (ewdata[0] >> 16) & 7;
  
  yl = SIGN(ewdata[0], 14); 
  ym = ewdata[1] >> 16;
  ym = SIGN(ym, 14);
  yh = SIGN(ewdata[1], 14); 
  
  xl = SIGN(ewdata[2], 30);
  xh = SIGN(ewdata[4], 30);
  xm = SIGN(ewdata[6], 30);
  
  dxldy = (int32_t)ewdata[3];
  dxhdy = (int32_t)ewdata[5];
  dxmdy = (int32_t)ewdata[7];

  LoadEWPrimData(ewvars, ewdxvars, ewdata + 8);
  LoadEWPrimData(ewdevars, ewdyvars, ewdata + 16);

  ewvars[EW_Z] = ewdata[40];
  ewdxvars[EWDX_DZDX] = ewdata[41];
  ewdevars[EWDE_DZDE] = ewdata[42];
  ewdyvars[EWDY_DZDY] = ewdata[43];

  int32_t dzdx = ewdxvars[EWDX_DZDX];
  ClearLow5(spans, ewdxvars);
  spans[SPAN_DZ] = dzdx;

  spans_drdy = ewdyvars[EWDY_DRDY] >> 14;
  spans_dgdy = ewdyvars[EWDY_DGDY] >> 14;
  spans_dbdy = ewdyvars[EWDY_DBDY] >> 14;
  spans_dady = ewdyvars[EWDY_DADY] >> 14;
  spans_dzdy = ewdyvars[EWDY_DZDY] >> 10;
  spans_drdy = SIGN(spans_drdy, 13);
  spans_dgdy = SIGN(spans_dgdy, 13);
  spans_dbdy = SIGN(spans_dbdy, 13);
  spans_dady = SIGN(spans_dady, 13);
  spans_dzdy = SIGN(spans_dzdy, 22);
  spans_cdr = spans[SPAN_DR] >> 14;
  spans_cdr = SIGN(spans_cdr, 13);
  spans_cdg = spans[SPAN_DG] >> 14;
  spans_cdg = SIGN(spans_cdg, 13);
  spans_cdb = spans[SPAN_DB] >> 14;
  spans_cdb = SIGN(spans_cdb, 13);
  spans_cda = spans[SPAN_DA] >> 14;
  spans_cda = SIGN(spans_cda, 13);
  spans_cdz = spans[SPAN_DZ] >> 10;
  spans_cdz = SIGN(spans_cdz, 22);
  
  spans_dsdy = ewdyvars[EWDY_DSDY] & ~0x7fff;
  spans_dtdy = ewdyvars[EWDY_DTDY] & ~0x7fff;
  spans_dwdy = ewdyvars[EWDY_DWDY] & ~0x7fff;
  
  int dzdy_dz = (ewdyvars[EWDY_DZDY] >> 16) & 0xffff;
  int dzdx_dz = (ewdxvars[EWDX_DZDX] >> 16) & 0xffff;
  
  spans_dzpix = ((dzdy_dz & 0x8000) ? ((~dzdy_dz) & 0x7fff) : dzdy_dz) + ((dzdx_dz & 0x8000) ? ((~dzdx_dz) & 0x7fff) : dzdx_dz);
  spans_dzpix = normalize_dzpix(spans_dzpix & 0xffff) & 0xffff;
  
  xleft_inc = (dxmdy >> 2) & ~0x1;
  xright_inc = (dxhdy >> 2) & ~0x1;
  
  xright = xh & ~0x1;
  xleft = xm & ~0x1;
    
  int k = 0;

  int32_t diffvars[8];
  int sign_dxhdy = (dxhdy & 0x80000000) ? 1 : 0;
  
  int do_offset = !(sign_dxhdy ^ flip);

  if (do_offset) {
    int32_t ewdehvars[8];
    int32_t ewdyhvars[8];

    ClearLow9(ewdehvars, ewdevars);
    ClearLow9(ewdyhvars, ewdyvars);
    DiffASR2(diffvars, ewdehvars, ewdyhvars);
  }

  else
    memset(diffvars, 0, sizeof(diffvars));

  int xfrac = 0;
  int32_t ewdxhvars[8];

  if (other_modes.cycle_type != CYCLE_TYPE_COPY)
    ASR8ClearLow(ewdxhvars, ewdxvars);
  else
    memset(ewdxhvars, 0, sizeof(ewdxhvars));

#define ADJUST_ATTR_PRIM()    \
{             \
  span[j].s = ((ewvars[EW_S] & ~0x1ff) + diffvars[EWDIFF_DSDIFF] - (xfrac * ewdxhvars[EWDXH_DSDXH])) & ~0x3ff;       \
  span[j].t = ((ewvars[EW_T] & ~0x1ff) + diffvars[EWDIFF_DTDIFF] - (xfrac * ewdxhvars[EWDXH_DTDXH])) & ~0x3ff;       \
  span[j].w = ((ewvars[EW_W] & ~0x1ff) + diffvars[EWDIFF_DWDIFF] - (xfrac * ewdxhvars[EWDXH_DWDXH])) & ~0x3ff;       \
  span[j].r = ((ewvars[EW_R] & ~0x1ff) + diffvars[EWDIFF_DRDIFF] - (xfrac * ewdxhvars[EWDXH_DRDXH])) & ~0x3ff;       \
  span[j].g = ((ewvars[EW_G] & ~0x1ff) + diffvars[EWDIFF_DGDIFF] - (xfrac * ewdxhvars[EWDXH_DGDXH])) & ~0x3ff;       \
  span[j].b = ((ewvars[EW_B] & ~0x1ff) + diffvars[EWDIFF_DBDIFF] - (xfrac * ewdxhvars[EWDXH_DBDXH])) & ~0x3ff;       \
  span[j].a = ((ewvars[EW_A] & ~0x1ff) + diffvars[EWDIFF_DADIFF] - (xfrac * ewdxhvars[EWDXH_DADXH])) & ~0x3ff;       \
  span[j].z = ((ewvars[EW_Z] & ~0x1ff) + diffvars[EWDIFF_DZDIFF] - (xfrac * ewdxhvars[EWDXH_DZDXH])) & ~0x3ff;       \
}

  int32_t maxxmx, minxmx, maxxhx, minxhx;

  int spix = 0;
  int ycur =  yh & ~3;
  int ldflag = (sign_dxhdy ^ flip) ? 0 : 3;
  int invaly = 1;
  int32_t xrsc = 0, xlsc = 0, stickybit = 0;
  int32_t yllimit = 0, yhlimit = 0;
  if (yl & 0x2000)
    yllimit = 1;
  else if (yl & 0x1000)
    yllimit = 0;
  else
    yllimit = (yl & 0xfff) < clip.yl;
  yllimit = yllimit ? yl : clip.yl;

  int ylfar = yllimit | 3;
  if ((yl >> 2) > (ylfar >> 2))
    ylfar += 4;
  else if ((yllimit >> 2) >= 0 && (yllimit >> 2) < 1023)
    span[(yllimit >> 2) + 1].validline = 0;
  
  
  if (yh & 0x2000)
    yhlimit = 0;
  else if (yh & 0x1000)
    yhlimit = 1;
  else
    yhlimit = (yh >= clip.yh);
  yhlimit = yhlimit ? yh : clip.yh;

  int yhclose = yhlimit & ~3;

  int32_t clipxlshift = clip.xl << 1;
  int32_t clipxhshift = clip.xh << 1;
  int allover = 1, allunder = 1, curover = 0, curunder = 0;
  int allinval = 1;
  int32_t curcross = 0;

  xfrac = ((xright >> 8) & 0xff);

  
  if (flip)
  {
  for (k = ycur; k <= ylfar; k++)
  {
    if (k == ym)
    {
    
      xleft = xl & ~1;
      xleft_inc = (dxldy >> 2) & ~1;
    }
    
    spix = k & 3;
            
    if (k >= yhclose)
    {
      invaly = k < yhlimit || k >= yllimit;
      
      j = k >> 2;

      if (spix == 0)
      {
        maxxmx = 0;
        minxhx = 0xfff;
        allover = allunder = 1;
        allinval = 1;
      }

      stickybit = ((xright >> 1) & 0x1fff) > 0;
      xrsc = ((xright >> 13) & 0x1ffe) | stickybit;
      curunder = ((xright & 0x8000000) || xrsc < clipxhshift); 
      xrsc = curunder ? clipxhshift : (((xright >> 13) & 0x3ffe) | stickybit);
      curover = ((xrsc & 0x2000) || (xrsc & 0x1fff) >= clipxlshift);
      xrsc = curover ? clipxlshift : xrsc;
      span[j].majorx[spix] = xrsc & 0x1fff;
      allover &= curover;
      allunder &= curunder; 

      stickybit = ((xleft >> 1) & 0x1fff) > 0;
      xlsc = ((xleft >> 13) & 0x1ffe) | stickybit;
      curunder = ((xleft & 0x8000000) || xlsc < clipxhshift);
      xlsc = curunder ? clipxhshift : (((xleft >> 13) & 0x3ffe) | stickybit);
      curover = ((xlsc & 0x2000) || (xlsc & 0x1fff) >= clipxlshift);
      xlsc = curover ? clipxlshift : xlsc;
      span[j].minorx[spix] = xlsc & 0x1fff;
      allover &= curover;
      allunder &= curunder; 
      
      
      
      curcross = ((xleft ^ (1 << 27)) & (0x3fff << 14)) < ((xright ^ (1 << 27)) & (0x3fff << 14));
      

      invaly |= curcross;
      span[j].invalyscan[spix] = invaly;
      allinval &= invaly;

      if (!invaly)
      {
        maxxmx = (((xlsc >> 3) & 0xfff) > maxxmx) ? (xlsc >> 3) & 0xfff : maxxmx;
        minxhx = (((xrsc >> 3) & 0xfff) < minxhx) ? (xrsc >> 3) & 0xfff : minxhx;
      }
      
      if (spix == ldflag)
      {
        span[j].unscrx = xright >> 16;
        xfrac = (xright >> 8) & 0xff;
        ADJUST_ATTR_PRIM();
      }

      if (spix == 3)
      {
        span[j].lx = maxxmx;
        span[j].rx = minxhx;
        span[j].validline  = !allinval && !allover && !allunder && (!scfield || (scfield && !(sckeepodd ^ (j & 1))));
        
      }
      
                       
    }

    if (spix == 3)
      AddVectors(ewvars, ewvars, ewdevars);

    xleft += xleft_inc;
    xright += xright_inc;

  }
  }
  else
  {
  for (k = ycur; k <= ylfar; k++)
  {
    if (k == ym)
    {
      xleft = xl & ~1;
      xleft_inc = (dxldy >> 2) & ~1;
    }
    
    spix = k & 3;
            
    if (k >= yhclose)
    {
      invaly = k < yhlimit || k >= yllimit;
      j = k >> 2;

      if (spix == 0)
      {
        maxxhx = 0;
        minxmx = 0xfff;
        allover = allunder = 1;
        allinval = 1;
      }

      stickybit = ((xright >> 1) & 0x1fff) > 0;
      xrsc = ((xright >> 13) & 0x1ffe) | stickybit;
      curunder = ((xright & 0x8000000) || xrsc < clipxhshift); 
      xrsc = curunder ? clipxhshift : (((xright >> 13) & 0x3ffe) | stickybit);
      curover = ((xrsc & 0x2000) || (xrsc & 0x1fff) >= clipxlshift);
      xrsc = curover ? clipxlshift : xrsc;
      span[j].majorx[spix] = xrsc & 0x1fff;
      allover &= curover;
      allunder &= curunder; 

      stickybit = ((xleft >> 1) & 0x1fff) > 0;
      xlsc = ((xleft >> 13) & 0x1ffe) | stickybit;
      curunder = ((xleft & 0x8000000) || xlsc < clipxhshift);
      xlsc = curunder ? clipxhshift : (((xleft >> 13) & 0x3ffe) | stickybit);
      curover = ((xlsc & 0x2000) || (xlsc & 0x1fff) >= clipxlshift);
      xlsc = curover ? clipxlshift : xlsc;
      span[j].minorx[spix] = xlsc & 0x1fff;
      allover &= curover;
      allunder &= curunder; 

      curcross = ((xright ^ (1 << 27)) & (0x3fff << 14)) < ((xleft ^ (1 << 27)) & (0x3fff << 14));
            
      invaly |= curcross;
      span[j].invalyscan[spix] = invaly;
      allinval &= invaly;

      if (!invaly)
      {
        minxmx = (((xlsc >> 3) & 0xfff) < minxmx) ? (xlsc >> 3) & 0xfff : minxmx;
        maxxhx = (((xrsc >> 3) & 0xfff) > maxxhx) ? (xrsc >> 3) & 0xfff : maxxhx;
      }

      if (spix == ldflag)
      {
        span[j].unscrx  = xright >> 16;
        xfrac = (xright >> 8) & 0xff;
        ADJUST_ATTR_PRIM();
      }

      if (spix == 3)
      {
        span[j].lx = minxmx;
        span[j].rx = maxxhx;
        span[j].validline  = !allinval && !allover && !allunder && (!scfield || (scfield && !(sckeepodd ^ (j & 1))));
      }
      
    }

    if (spix == 3)
      AddVectors(ewvars, ewvars, ewdevars);

    xleft += xleft_inc;
    xright += xright_inc;

  }
  }

  
  

  switch(other_modes.cycle_type)
  {
    case CYCLE_TYPE_1: render_spans_1cycle_ptr(yhlimit >> 2, yllimit >> 2, tilenum, flip); break;
    case CYCLE_TYPE_2: render_spans_2cycle_ptr(yhlimit >> 2, yllimit >> 2, tilenum, flip); break;
    case CYCLE_TYPE_COPY: render_spans_copy(yhlimit >> 2, yllimit >> 2, tilenum, flip); break;
    case CYCLE_TYPE_FILL: render_spans_fill(yhlimit >> 2, yllimit >> 2, flip); break;
    default: fatalerror("cycle_type %d", other_modes.cycle_type); break;
  }
  
  
}

static void edgewalker_for_loads(int32_t* lewdata)
{
  int j = 0;
  int xleft = 0, xright = 0;
  int xend = 0;
  int s = 0, t = 0;
  int dsdx = 0, dtdx = 0;
  int dtde = 0;
  int tilenum = 0;
  int32_t yl = 0, ym = 0, yh = 0;
  int32_t xl = 0, xm = 0, xh = 0;

  int commandcode = (lewdata[0] >> 24) & 0x3f;
  int ltlut = (commandcode == 0x30);
  int coord_quad = ltlut || (commandcode == 0x33);
  max_level = 0;
  tilenum = (lewdata[0] >> 16) & 7;

  
  yl = SIGN(lewdata[0], 14); 
  ym = lewdata[1] >> 16;
  ym = SIGN(ym, 14);
  yh = SIGN(lewdata[1], 14); 
  
  xl = SIGN(lewdata[2], 30);
  xh = SIGN(lewdata[3], 30);
  xm = SIGN(lewdata[4], 30);
  
  s    = lewdata[5] & 0xffff0000;
  t    = (lewdata[5] & 0xffff) << 16;
  dsdx = (lewdata[7] & 0xffff0000) | ((lewdata[6] >> 16) & 0xffff);
  dtdx = ((lewdata[7] << 16) & 0xffff0000)  | (lewdata[6] & 0xffff);
  dtde = (lewdata[9] & 0xffff) << 16;

  spans[SPAN_DS] = dsdx & ~0x1f;
  spans[SPAN_DT] = dtdx & ~0x1f;
  spans[SPAN_DW] = 0;
  
  xright = xh & ~0x1;
  xleft = xm & ~0x1;
    
  int k = 0;

#define ADJUST_ATTR_LOAD()                    \
{                               \
  span[j].s = s & ~0x3ff;                   \
  span[j].t = t & ~0x3ff;                   \
}

#define ADDVALUES_LOAD() {  \
      t += dtde;    \
}

  int32_t maxxmx, minxhx;

  int spix = 0;
  int ycur =  yh & ~3;
  int ylfar = yl | 3;
  
  int valid_y = 1;
  int32_t xrsc = 0, xlsc = 0;
  int32_t yllimit = yl;
  int32_t yhlimit = yh;

  xend = xright >> 16;

  
  for (k = ycur; k <= ylfar; k++)
  {
    if (k == ym)
      xleft = xl & ~1;
    
    spix = k & 3;
            
    if (!(k & ~0xfff))
    {
      j = k >> 2;
      valid_y = !(k < yhlimit || k >= yllimit);

      if (spix == 0)
      {
        maxxmx = 0;
        minxhx = 0xfff;
      }

      xrsc = (xright >> 13) & 0x7ffe;
      
      

      xlsc = (xleft >> 13) & 0x7ffe;

      if (valid_y)
      {
        maxxmx = (((xlsc >> 3) & 0xfff) > maxxmx) ? (xlsc >> 3) & 0xfff : maxxmx;
        minxhx = (((xrsc >> 3) & 0xfff) < minxhx) ? (xrsc >> 3) & 0xfff : minxhx;
      }

      if (spix == 0)
      {
        span[j].unscrx = xend;
        ADJUST_ATTR_LOAD();
      }

      if (spix == 3)
      {
        span[j].lx = maxxmx;
        span[j].rx = minxhx;
        
        
      }
      
                       
    }

    if (spix == 3)
    {
      ADDVALUES_LOAD();
    }

    

  }

  loading_pipeline(yhlimit >> 2, yllimit >> 2, tilenum, coord_quad, ltlut);
}

static const uint32_t rdp_command_length[64] = {
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  32,     
  32+16,    
  32+64,    
  32+64+16, 
  32+64,    
  32+64+16, 
  32+64+64, 
  32+64+64+16,
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  16,     
  16,     
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8,      
  8     
};

static void rdp_invalid(uint32_t w1, uint32_t w2)
{
}

static void rdp_noop(uint32_t w1, uint32_t w2)
{
}

static void rdp_tri_noshade(uint32_t w1, uint32_t w2)
{
  memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 8 * sizeof(int32_t));
  memset(&ewdata[8], 0, 36 * sizeof(int32_t));
  edgewalker_for_prims(ewdata);
}

static void rdp_tri_noshade_z(uint32_t w1, uint32_t w2)
{
  memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 8 * sizeof(int32_t));
  memset(&ewdata[8], 0, 32 * sizeof(int32_t));
  memcpy(&ewdata[40], &rdp_cmd_data[rdp_cmd_cur + 8], 4 * sizeof(int32_t));
  edgewalker_for_prims(ewdata);
}

static void rdp_tri_tex(uint32_t w1, uint32_t w2)
{
  memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 8 * sizeof(int32_t));
  memset(&ewdata[8], 0, 16 * sizeof(int32_t));
  memcpy(&ewdata[24], &rdp_cmd_data[rdp_cmd_cur + 8], 16 * sizeof(int32_t));
  memset(&ewdata[40], 0, 4 * sizeof(int32_t));
  edgewalker_for_prims(ewdata);
}

static void rdp_tri_tex_z(uint32_t w1, uint32_t w2)
{
  memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 8 * sizeof(int32_t));
  memset(&ewdata[8], 0, 16 * sizeof(int32_t));
  memcpy(&ewdata[24], &rdp_cmd_data[rdp_cmd_cur + 8], 16 * sizeof(int32_t));
  memcpy(&ewdata[40], &rdp_cmd_data[rdp_cmd_cur + 24], 4 * sizeof(int32_t));
  edgewalker_for_prims(ewdata);
}

static void rdp_tri_shade(uint32_t w1, uint32_t w2)
{
  memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 24 * sizeof(int32_t));
  memset(&ewdata[24], 0, 20 * sizeof(int32_t));
  edgewalker_for_prims(ewdata);
}

static void rdp_tri_shade_z(uint32_t w1, uint32_t w2)
{
  memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 24 * sizeof(int32_t));
  memset(&ewdata[24], 0, 16 * sizeof(int32_t));
  memcpy(&ewdata[40], &rdp_cmd_data[rdp_cmd_cur + 24], 4 * sizeof(int32_t));
  edgewalker_for_prims(ewdata);
}

static void rdp_tri_texshade(uint32_t w1, uint32_t w2)
{
  memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 40 * sizeof(int32_t));
  memset(&ewdata[40], 0, 4 * sizeof(int32_t));
  edgewalker_for_prims(ewdata);
}

static void rdp_tri_texshade_z(uint32_t w1, uint32_t w2)
{
  memcpy(&ewdata[0], &rdp_cmd_data[rdp_cmd_cur], 44 * sizeof(int32_t));
  edgewalker_for_prims(ewdata);
}

static void rdp_tex_rect(uint32_t w1, uint32_t w2)
{
  uint32_t w3 = rdp_cmd_data[rdp_cmd_cur + 2];
  uint32_t w4 = rdp_cmd_data[rdp_cmd_cur + 3];

  
  uint32_t tilenum  = (w2 >> 24) & 0x7;
  uint32_t xl = (w1 >> 12) & 0xfff;
  uint32_t yl = (w1 >>  0) & 0xfff;
  uint32_t xh = (w2 >> 12) & 0xfff;
  uint32_t yh = (w2 >>  0) & 0xfff;
  
  int32_t s = (w3 >> 16) & 0xffff;
  int32_t t = (w3 >>  0) & 0xffff;
  int32_t dsdx = (w4 >> 16) & 0xffff;
  int32_t dtdy = (w4 >>  0) & 0xffff;
  
  dsdx = SIGN16(dsdx);
  dtdy = SIGN16(dtdy);
  
  if (other_modes.cycle_type == CYCLE_TYPE_FILL || other_modes.cycle_type == CYCLE_TYPE_COPY)
    yl |= 3;

  uint32_t xlint = (xl >> 2) & 0x3ff;
  uint32_t xhint = (xh >> 2) & 0x3ff;

  ewdata[0] = (0x24 << 24) | ((0x80 | tilenum) << 16) | yl;
  ewdata[1] = (yl << 16) | yh;
  ewdata[2] = (xlint << 16) | ((xl & 3) << 14);
  ewdata[3] = 0;
  ewdata[4] = (xhint << 16) | ((xh & 3) << 14);
  ewdata[5] = 0;
  ewdata[6] = (xlint << 16) | ((xl & 3) << 14);
  ewdata[7] = 0;
  memset(&ewdata[8], 0, 16 * sizeof(uint32_t));
  ewdata[24] = (s << 16) | t;
  ewdata[25] = 0;
  ewdata[26] = ((dsdx >> 5) << 16);
  ewdata[27] = 0;
  ewdata[28] = 0;
  ewdata[29] = 0;
  ewdata[30] = ((dsdx & 0x1f) << 11) << 16;
  ewdata[31] = 0;
  ewdata[32] = (dtdy >> 5) & 0xffff;
  ewdata[33] = 0;
  ewdata[34] = (dtdy >> 5) & 0xffff;
  ewdata[35] = 0;
  ewdata[36] = (dtdy & 0x1f) << 11;
  ewdata[37] = 0;
  ewdata[38] = (dtdy & 0x1f) << 11;
  ewdata[39] = 0;
  memset(&ewdata[40], 0, 4 * sizeof(int32_t));

  

  edgewalker_for_prims(ewdata);

}

static void rdp_tex_rect_flip(uint32_t w1, uint32_t w2)
{
  uint32_t w3 = rdp_cmd_data[rdp_cmd_cur+2];
  uint32_t w4 = rdp_cmd_data[rdp_cmd_cur+3];
  
  
  uint32_t tilenum  = (w2 >> 24) & 0x7;
  uint32_t xl = (w1 >> 12) & 0xfff;
  uint32_t yl = (w1 >>  0) & 0xfff;
  uint32_t xh = (w2 >> 12) & 0xfff;
  uint32_t yh = (w2 >>  0) & 0xfff;
  
  int32_t s = (w3 >> 16) & 0xffff;
  int32_t t = (w3 >>  0) & 0xffff;
  int32_t dsdx = (w4 >> 16) & 0xffff;
  int32_t dtdy = (w4 >>  0) & 0xffff;
  
  dsdx = SIGN16(dsdx);
  dtdy = SIGN16(dtdy);

  if (other_modes.cycle_type == CYCLE_TYPE_FILL || other_modes.cycle_type == CYCLE_TYPE_COPY)
    yl |= 3;

  uint32_t xlint = (xl >> 2) & 0x3ff;
  uint32_t xhint = (xh >> 2) & 0x3ff;

  ewdata[0] = (0x25 << 24) | ((0x80 | tilenum) << 16) | yl;
  ewdata[1] = (yl << 16) | yh;
  ewdata[2] = (xlint << 16) | ((xl & 3) << 14);
  ewdata[3] = 0;
  ewdata[4] = (xhint << 16) | ((xh & 3) << 14);
  ewdata[5] = 0;
  ewdata[6] = (xlint << 16) | ((xl & 3) << 14);
  ewdata[7] = 0;
  memset(&ewdata[8], 0, 16 * sizeof(int32_t));
  ewdata[24] = (s << 16) | t;
  ewdata[25] = 0;
  
  ewdata[26] = (dtdy >> 5) & 0xffff;
  ewdata[27] = 0;
  ewdata[28] = 0;
  ewdata[29] = 0;
  ewdata[30] = ((dtdy & 0x1f) << 11);
  ewdata[31] = 0;
  ewdata[32] = (dsdx >> 5) << 16;
  ewdata[33] = 0;
  ewdata[34] = (dsdx >> 5) << 16;
  ewdata[35] = 0;
  ewdata[36] = (dsdx & 0x1f) << 27;
  ewdata[37] = 0;
  ewdata[38] = (dsdx & 0x1f) << 27;
  ewdata[39] = 0;
  memset(&ewdata[40], 0, 4 * sizeof(int32_t));

  edgewalker_for_prims(ewdata);
}

static void rdp_sync_load(uint32_t w1, uint32_t w2)
{
}

static void rdp_sync_pipe(uint32_t w1, uint32_t w2)
{
}

static void rdp_sync_tile(uint32_t w1, uint32_t w2)
{
}

static void rdp_sync_full(uint32_t w1, uint32_t w2)
{
  z64gl_command = 0;
  BusRaiseRCPInterrupt(my_rdp->bus, MI_INTR_DP);
}

static void rdp_set_key_gb(uint32_t w1, uint32_t w2)
{
  key_width.g = (w1 >> 12) & 0xfff;
  key_width.b = w1 & 0xfff;
  key_center.g = (w2 >> 24) & 0xff;
  key_scale.g = (w2 >> 16) & 0xff;
  key_center.b = (w2 >> 8) & 0xff;
  key_scale.b = w2 & 0xff;
}

static void rdp_set_key_r(uint32_t w1, uint32_t w2)
{
  key_width.r = (w2 >> 16) & 0xfff;
  key_center.r = (w2 >> 8) & 0xff;
  key_scale.r = w2 & 0xff;
}

static void rdp_set_convert(uint32_t w1, uint32_t w2)
{
  k0 = (w1 >> 13) & 0x1ff;
  k1 = (w1 >> 4) & 0x1ff;
  k2 = ((w1 & 0xf) << 5) | ((w2 >> 27) & 0x1f);
  k3 = (w2 >> 18) & 0x1ff;
  k4 = (w2 >> 9) & 0x1ff;
  k5 = w2 & 0x1ff;
}

static void rdp_set_scissor(uint32_t w1, uint32_t w2)
{
  clip.xh = (w1 >> 12) & 0xfff;
  clip.yh = (w1 >>  0) & 0xfff;
  clip.xl = (w2 >> 12) & 0xfff;
  clip.yl = (w2 >>  0) & 0xfff;
  
  scfield = (w2 >> 25) & 1;
  sckeepodd = (w2 >> 24) & 1;
}

static void rdp_set_prim_depth(uint32_t w1, uint32_t w2)
{
  primitive_z = w2 & (0x7fff << 16);
  

  primitive_delta_z = (uint16_t)(w2);
}

static void rdp_set_other_modes(uint32_t w1, uint32_t w2)
{
  other_modes.cycle_type      = (w1 >> 20) & 0x3;
  other_modes.persp_tex_en    = (w1 & 0x80000) ? 1 : 0;
  other_modes.detail_tex_en   = (w1 & 0x40000) ? 1 : 0;
  other_modes.sharpen_tex_en    = (w1 & 0x20000) ? 1 : 0;
  other_modes.tex_lod_en      = (w1 & 0x10000) ? 1 : 0;
  other_modes.en_tlut       = (w1 & 0x08000) ? 1 : 0;
  other_modes.tlut_type     = (w1 & 0x04000) ? 1 : 0;
  other_modes.sample_type     = (w1 & 0x02000) ? 1 : 0;
  other_modes.mid_texel     = (w1 & 0x01000) ? 1 : 0;
  other_modes.bi_lerp0      = (w1 & 0x00800) ? 1 : 0;
  other_modes.bi_lerp1      = (w1 & 0x00400) ? 1 : 0;
  other_modes.convert_one     = (w1 & 0x00200) ? 1 : 0;
  other_modes.key_en        = (w1 & 0x00100) ? 1 : 0;
  other_modes.rgb_dither_sel    = (w1 >> 6) & 0x3;
  other_modes.alpha_dither_sel  = (w1 >> 4) & 0x3;
  other_modes.blend_m1a_0     = (w2 >> 30) & 0x3;
  other_modes.blend_m1a_1     = (w2 >> 28) & 0x3;
  other_modes.blend_m1b_0     = (w2 >> 26) & 0x3;
  other_modes.blend_m1b_1     = (w2 >> 24) & 0x3;
  other_modes.blend_m2a_0     = (w2 >> 22) & 0x3;
  other_modes.blend_m2a_1     = (w2 >> 20) & 0x3;
  other_modes.blend_m2b_0     = (w2 >> 18) & 0x3;
  other_modes.blend_m2b_1     = (w2 >> 16) & 0x3;
  other_modes.force_blend     = (w2 >> 14) & 1;
  other_modes.alpha_cvg_select  = (w2 >> 13) & 1;
  other_modes.cvg_times_alpha   = (w2 >> 12) & 1;
  other_modes.z_mode        = (w2 >> 10) & 0x3;
  other_modes.cvg_dest      = (w2 >> 8) & 0x3;
  other_modes.color_on_cvg    = (w2 >> 7) & 1;
  other_modes.image_read_en   = (w2 >> 6) & 1;
  other_modes.z_update_en     = (w2 >> 5) & 1;
  other_modes.z_compare_en    = (w2 >> 4) & 1;
  other_modes.antialias_en    = (w2 >> 3) & 1;
  other_modes.z_source_sel    = (w2 >> 2) & 1;
  other_modes.dither_alpha_en   = (w2 >> 1) & 1;
  other_modes.alpha_compare_en  = (w2) & 1;

  SET_BLENDER_INPUT(0, 0, &blender1a_r[0], &blender1a_g[0], &blender1a_b[0], &blender1b_a[0],
            other_modes.blend_m1a_0, other_modes.blend_m1b_0);
  SET_BLENDER_INPUT(0, 1, &blender2a_r[0], &blender2a_g[0], &blender2a_b[0], &blender2b_a[0],
            other_modes.blend_m2a_0, other_modes.blend_m2b_0);
  SET_BLENDER_INPUT(1, 0, &blender1a_r[1], &blender1a_g[1], &blender1a_b[1], &blender1b_a[1],
            other_modes.blend_m1a_1, other_modes.blend_m1b_1);
  SET_BLENDER_INPUT(1, 1, &blender2a_r[1], &blender2a_g[1], &blender2a_b[1], &blender2b_a[1],
            other_modes.blend_m2a_1, other_modes.blend_m2b_1);

  other_modes.f.stalederivs = 1;
}

void deduce_derivatives()
{
  
  other_modes.f.partialreject_1cycle = (blender2b_a[0] == &inv_pixel_color.a && blender1b_a[0] == &pixel_color.a);
  other_modes.f.partialreject_2cycle = (blender2b_a[1] == &inv_pixel_color.a && blender1b_a[1] == &pixel_color.a);

  other_modes.f.special_bsel0 = (blender2b_a[0] == &memory_color.a);
  other_modes.f.special_bsel1 = (blender2b_a[1] == &memory_color.a);

  other_modes.f.rgb_alpha_dither = (other_modes.rgb_dither_sel << 2) | other_modes.alpha_dither_sel;

  if (other_modes.rgb_dither_sel == 3)
    rgb_dither_ptr = DitherFuncLUT[1];
  else
    rgb_dither_ptr = DitherFuncLUT[0];

  tcdiv_ptr = tcdiv_func[other_modes.persp_tex_en];

  
  int texel1_used_in_cc1 = 0, texel0_used_in_cc1 = 0, texel0_used_in_cc0 = 0, texel1_used_in_cc0 = 0;
  int lod_frac_used_in_cc1 = 0, lod_frac_used_in_cc0 = 0;

  if ((combiner_rgbmul_r[1] == &lod_frac) || (combiner_alphamul[1] == &lod_frac))
    lod_frac_used_in_cc1 = 1;
  if ((combiner_rgbmul_r[0] == &lod_frac) || (combiner_alphamul[0] == &lod_frac))
    lod_frac_used_in_cc0 = 1;

  if (combiner_rgbmul_r[1] == &texel1_color.r || combiner_rgbsub_a_r[1] == &texel1_color.r || combiner_rgbsub_b_r[1] == &texel1_color.r || combiner_rgbadd_r[1] == &texel1_color.r || \
    combiner_alphamul[1] == &texel1_color.a || combiner_alphasub_a[1] == &texel1_color.a || combiner_alphasub_b[1] == &texel1_color.a || combiner_alphaadd[1] == &texel1_color.a || \
    combiner_rgbmul_r[1] == &texel1_color.a)
    texel1_used_in_cc1 = 1;
  if (combiner_rgbmul_r[1] == &texel0_color.r || combiner_rgbsub_a_r[1] == &texel0_color.r || combiner_rgbsub_b_r[1] == &texel0_color.r || combiner_rgbadd_r[1] == &texel0_color.r || \
    combiner_alphamul[1] == &texel0_color.a || combiner_alphasub_a[1] == &texel0_color.a || combiner_alphasub_b[1] == &texel0_color.a || combiner_alphaadd[1] == &texel0_color.a || \
    combiner_rgbmul_r[1] == &texel0_color.a)
    texel0_used_in_cc1 = 1;
  if (combiner_rgbmul_r[0] == &texel1_color.r || combiner_rgbsub_a_r[0] == &texel1_color.r || combiner_rgbsub_b_r[0] == &texel1_color.r || combiner_rgbadd_r[0] == &texel1_color.r || \
    combiner_alphamul[0] == &texel1_color.a || combiner_alphasub_a[0] == &texel1_color.a || combiner_alphasub_b[0] == &texel1_color.a || combiner_alphaadd[0] == &texel1_color.a || \
    combiner_rgbmul_r[0] == &texel1_color.a)
    texel1_used_in_cc0 = 1;
  if (combiner_rgbmul_r[0] == &texel0_color.r || combiner_rgbsub_a_r[0] == &texel0_color.r || combiner_rgbsub_b_r[0] == &texel0_color.r || combiner_rgbadd_r[0] == &texel0_color.r || \
    combiner_alphamul[0] == &texel0_color.a || combiner_alphasub_a[0] == &texel0_color.a || combiner_alphasub_b[0] == &texel0_color.a || combiner_alphaadd[0] == &texel0_color.a || \
    combiner_rgbmul_r[0] == &texel0_color.a)
    texel0_used_in_cc0 = 1;
  
  if (texel1_used_in_cc1)
    render_spans_1cycle_ptr = render_spans_1cycle_func[2];
  else if (texel0_used_in_cc1 || lod_frac_used_in_cc1)
    render_spans_1cycle_ptr = render_spans_1cycle_func[1];
  else
    render_spans_1cycle_ptr = render_spans_1cycle_func[0];

  if (texel1_used_in_cc1)
    render_spans_2cycle_ptr = render_spans_2cycle_func[3];
  else if (texel1_used_in_cc0 || texel0_used_in_cc1)
    render_spans_2cycle_ptr = render_spans_2cycle_func[2];
  else if (texel0_used_in_cc0 || lod_frac_used_in_cc0 || lod_frac_used_in_cc1)
    render_spans_2cycle_ptr = render_spans_2cycle_func[1];
  else
    render_spans_2cycle_ptr = render_spans_2cycle_func[0];

  
  int lodfracused = 0;

  if ((other_modes.cycle_type == CYCLE_TYPE_2 && (lod_frac_used_in_cc0 || lod_frac_used_in_cc1)) || \
    (other_modes.cycle_type == CYCLE_TYPE_1 && lod_frac_used_in_cc1))
    lodfracused = 1;

  if ((other_modes.cycle_type == CYCLE_TYPE_1 && combiner_rgbsub_a_r[1] == &noise) || \
    (other_modes.cycle_type == CYCLE_TYPE_2 && (combiner_rgbsub_a_r[0] == &noise || combiner_rgbsub_a_r[1] == &noise)) || \
    other_modes.alpha_dither_sel == 2)
    get_dither_noise_ptr = DitherNoiseFuncLUT[0];
  else if (other_modes.f.rgb_alpha_dither != 0xf)
    get_dither_noise_ptr = DitherNoiseFuncLUT[1];
  else
    get_dither_noise_ptr = DitherNoiseFuncLUT[2];

  other_modes.f.dolod = other_modes.tex_lod_en || lodfracused;
}

static void rdp_set_tile_size(uint32_t w1, uint32_t w2)
{
  int tilenum = (w2 >> 24) & 0x7;
  tile[tilenum].sl = (w1 >> 12) & 0xfff;
  tile[tilenum].tl = (w1 >>  0) & 0xfff;
  tile[tilenum].sh = (w2 >> 12) & 0xfff;
  tile[tilenum].th = (w2 >>  0) & 0xfff;

  calculate_clamp_diffs(tilenum);
}
  
static void rdp_load_block(uint32_t w1, uint32_t w2)
{
  int tilenum = (w2 >> 24) & 0x7;
  int sl, sh, tl, dxt;
            
  
  tile[tilenum].sl = sl = ((w1 >> 12) & 0xfff);
  tile[tilenum].tl = tl = ((w1 >>  0) & 0xfff);
  tile[tilenum].sh = sh = ((w2 >> 12) & 0xfff);
  tile[tilenum].th = dxt  = ((w2 >>  0) & 0xfff);

  calculate_clamp_diffs(tilenum);

  int tlclamped = tl & 0x3ff;

  int32_t lewdata[10];
  
  lewdata[0] = (w1 & 0xff000000) | (0x10 << 19) | (tilenum << 16) | ((tlclamped << 2) | 3);
  lewdata[1] = (((tlclamped << 2) | 3) << 16) | (tlclamped << 2);
  lewdata[2] = sh << 16;
  lewdata[3] = sl << 16;
  lewdata[4] = sh << 16;
  lewdata[5] = ((sl << 3) << 16) | (tl << 3);
  lewdata[6] = (dxt & 0xff) << 8;
  lewdata[7] = ((0x80 >> ti_size) << 16) | (dxt >> 8);
  lewdata[8] = 0x20;
  lewdata[9] = 0x20;

  edgewalker_for_loads(lewdata);

}

static void rdp_load_tlut(uint32_t w1, uint32_t w2)
{
  

  tile_tlut_common_cs_decoder(w1, w2);
}

static void rdp_load_tile(uint32_t w1, uint32_t w2)
{
  tile_tlut_common_cs_decoder(w1, w2);
}

void tile_tlut_common_cs_decoder(uint32_t w1, uint32_t w2)
{
  int tilenum = (w2 >> 24) & 0x7;
  int sl, tl, sh, th;

  
  tile[tilenum].sl = sl = ((w1 >> 12) & 0xfff);
  tile[tilenum].tl = tl = ((w1 >>  0) & 0xfff);
  tile[tilenum].sh = sh = ((w2 >> 12) & 0xfff);
  tile[tilenum].th = th = ((w2 >>  0) & 0xfff);

  calculate_clamp_diffs(tilenum);

  
  int32_t lewdata[10];

  lewdata[0] = (w1 & 0xff000000) | (0x10 << 19) | (tilenum << 16) | (th | 3);
  lewdata[1] = ((th | 3) << 16) | (tl);
  lewdata[2] = ((sh >> 2) << 16) | ((sh & 3) << 14);
  lewdata[3] = ((sl >> 2) << 16) | ((sl & 3) << 14);
  lewdata[4] = ((sh >> 2) << 16) | ((sh & 3) << 14);
  lewdata[5] = ((sl << 3) << 16) | (tl << 3);
  lewdata[6] = 0;
  lewdata[7] = (0x200 >> ti_size) << 16;
  lewdata[8] = 0x20;
  lewdata[9] = 0x20;

  edgewalker_for_loads(lewdata);
}

static void rdp_set_tile(uint32_t w1, uint32_t w2)
{
  int tilenum = (w2 >> 24) & 0x7;
  
  tile[tilenum].format  = (w1 >> 21) & 0x7;
  tile[tilenum].size    = (w1 >> 19) & 0x3;
  tile[tilenum].line    = (w1 >>  9) & 0x1ff;
  tile[tilenum].tmem    = (w1 >>  0) & 0x1ff;
  tile[tilenum].palette = (w2 >> 20) & 0xf;
  tile[tilenum].ct    = (w2 >> 19) & 0x1;
  tile[tilenum].mt    = (w2 >> 18) & 0x1;
  tile[tilenum].mask_t  = (w2 >> 14) & 0xf;
  tile[tilenum].shift_t = (w2 >> 10) & 0xf;
  tile[tilenum].cs    = (w2 >>  9) & 0x1;
  tile[tilenum].ms    = (w2 >>  8) & 0x1;
  tile[tilenum].mask_s  = (w2 >>  4) & 0xf;
  tile[tilenum].shift_s = (w2 >>  0) & 0xf;

  calculate_tile_derivs(tilenum);
}

static void rdp_fill_rect(uint32_t w1, uint32_t w2)
{
  uint32_t xl = (w1 >> 12) & 0xfff;
  uint32_t yl = (w1 >>  0) & 0xfff;
  uint32_t xh = (w2 >> 12) & 0xfff;
  uint32_t yh = (w2 >>  0) & 0xfff;

  if (other_modes.cycle_type == CYCLE_TYPE_FILL || other_modes.cycle_type == CYCLE_TYPE_COPY)
    yl |= 3;

  uint32_t xlint = (xl >> 2) & 0x3ff;
  uint32_t xhint = (xh >> 2) & 0x3ff;

  ewdata[0] = (0x3680 << 16) | yl;
  ewdata[1] = (yl << 16) | yh;
  ewdata[2] = (xlint << 16) | ((xl & 3) << 14);
  ewdata[3] = 0;
  ewdata[4] = (xhint << 16) | ((xh & 3) << 14);
  ewdata[5] = 0;
  ewdata[6] = (xlint << 16) | ((xl & 3) << 14);
  ewdata[7] = 0;
  memset(&ewdata[8], 0, 36 * sizeof(int32_t));

  edgewalker_for_prims(ewdata);
}

static void rdp_set_fill_color(uint32_t w1, uint32_t w2)
{
  fill_color = w2;
}

static void rdp_set_fog_color(uint32_t w1, uint32_t w2)
{
  fog_color.r = (w2 >> 24) & 0xff;
  fog_color.g = (w2 >> 16) & 0xff;
  fog_color.b = (w2 >>  8) & 0xff;
  fog_color.a = (w2 >>  0) & 0xff;
}

static void rdp_set_blend_color(uint32_t w1, uint32_t w2)
{
  blend_color.r = (w2 >> 24) & 0xff;
  blend_color.g = (w2 >> 16) & 0xff;
  blend_color.b = (w2 >>  8) & 0xff;
  blend_color.a = (w2 >>  0) & 0xff;
}

static void rdp_set_prim_color(uint32_t w1, uint32_t w2)
{
  min_level = (w1 >> 8) & 0x1f;
  primitive_lod_frac = w1 & 0xff;
  prim_color.r = (w2 >> 24) & 0xff;
  prim_color.g = (w2 >> 16) & 0xff;
  prim_color.b = (w2 >>  8) & 0xff;
  prim_color.a = (w2 >>  0) & 0xff;
}

static void rdp_set_env_color(uint32_t w1, uint32_t w2)
{
  env_color.r = (w2 >> 24) & 0xff;
  env_color.g = (w2 >> 16) & 0xff;
  env_color.b = (w2 >>  8) & 0xff;
  env_color.a = (w2 >>  0) & 0xff;
}

static void rdp_set_combine(uint32_t w1, uint32_t w2)
{
  combine.sub_a_rgb0  = (w1 >> 20) & 0xf;
  combine.mul_rgb0  = (w1 >> 15) & 0x1f;
  combine.sub_a_a0  = (w1 >> 12) & 0x7;
  combine.mul_a0    = (w1 >>  9) & 0x7;
  combine.sub_a_rgb1  = (w1 >>  5) & 0xf;
  combine.mul_rgb1  = (w1 >>  0) & 0x1f;

  combine.sub_b_rgb0  = (w2 >> 28) & 0xf;
  combine.sub_b_rgb1  = (w2 >> 24) & 0xf;
  combine.sub_a_a1  = (w2 >> 21) & 0x7;
  combine.mul_a1    = (w2 >> 18) & 0x7;
  combine.add_rgb0  = (w2 >> 15) & 0x7;
  combine.sub_b_a0  = (w2 >> 12) & 0x7;
  combine.add_a0    = (w2 >>  9) & 0x7;
  combine.add_rgb1  = (w2 >>  6) & 0x7;
  combine.sub_b_a1  = (w2 >>  3) & 0x7;
  combine.add_a1    = (w2 >>  0) & 0x7;

  
  SET_SUBA_RGB_INPUT(&combiner_rgbsub_a_r[0], &combiner_rgbsub_a_g[0], &combiner_rgbsub_a_b[0], combine.sub_a_rgb0);
  SET_SUBB_RGB_INPUT(&combiner_rgbsub_b_r[0], &combiner_rgbsub_b_g[0], &combiner_rgbsub_b_b[0], combine.sub_b_rgb0);
  SET_MUL_RGB_INPUT(&combiner_rgbmul_r[0], &combiner_rgbmul_g[0], &combiner_rgbmul_b[0], combine.mul_rgb0);
  SET_ADD_RGB_INPUT(&combiner_rgbadd_r[0], &combiner_rgbadd_g[0], &combiner_rgbadd_b[0], combine.add_rgb0);
  SET_SUB_ALPHA_INPUT(&combiner_alphasub_a[0], combine.sub_a_a0);
  SET_SUB_ALPHA_INPUT(&combiner_alphasub_b[0], combine.sub_b_a0);
  SET_MUL_ALPHA_INPUT(&combiner_alphamul[0], combine.mul_a0);
  SET_SUB_ALPHA_INPUT(&combiner_alphaadd[0], combine.add_a0);

  SET_SUBA_RGB_INPUT(&combiner_rgbsub_a_r[1], &combiner_rgbsub_a_g[1], &combiner_rgbsub_a_b[1], combine.sub_a_rgb1);
  SET_SUBB_RGB_INPUT(&combiner_rgbsub_b_r[1], &combiner_rgbsub_b_g[1], &combiner_rgbsub_b_b[1], combine.sub_b_rgb1);
  SET_MUL_RGB_INPUT(&combiner_rgbmul_r[1], &combiner_rgbmul_g[1], &combiner_rgbmul_b[1], combine.mul_rgb1);
  SET_ADD_RGB_INPUT(&combiner_rgbadd_r[1], &combiner_rgbadd_g[1], &combiner_rgbadd_b[1], combine.add_rgb1);
  SET_SUB_ALPHA_INPUT(&combiner_alphasub_a[1], combine.sub_a_a1);
  SET_SUB_ALPHA_INPUT(&combiner_alphasub_b[1], combine.sub_b_a1);
  SET_MUL_ALPHA_INPUT(&combiner_alphamul[1], combine.mul_a1);
  SET_SUB_ALPHA_INPUT(&combiner_alphaadd[1], combine.add_a1);

  other_modes.f.stalederivs = 1;
}

static void rdp_set_texture_image(uint32_t w1, uint32_t w2)
{
  ti_format = (w1 >> 21) & 0x7;
  ti_size   = (w1 >> 19) & 0x3;
  ti_width  = (w1 & 0x3ff) + 1;
  ti_address  = w2 & 0x0ffffff;
  
  
  
}

static void rdp_set_mask_image(uint32_t w1, uint32_t w2)
{
  zb_address  = w2 & 0x0ffffff;
}

static void rdp_set_color_image(uint32_t w1, uint32_t w2)
{
  fb_format   = (w1 >> 21) & 0x7;
  fb_size   = (w1 >> 19) & 0x3;
  fb_width  = (w1 & 0x3ff) + 1;
  fb_address  = w2 & 0x0ffffff;

  
  fbread1_ptr = FBReadFuncLUT[fb_size];
  fbread2_ptr = FBReadFunc2LUT[fb_size];
  fbwrite_ptr = FBWriteFuncLUT[fb_size];
  fbfill_ptr = fbfill_func[fb_size];
}

static void (*const rdp_command_table[64])(uint32_t w1, uint32_t w2) = {
  rdp_noop,     rdp_invalid,      rdp_invalid,      rdp_invalid,
  rdp_invalid,    rdp_invalid,      rdp_invalid,      rdp_invalid,
  rdp_tri_noshade,  rdp_tri_noshade_z,    rdp_tri_tex,      rdp_tri_tex_z,
  rdp_tri_shade,    rdp_tri_shade_z,    rdp_tri_texshade,   rdp_tri_texshade_z,
  
  rdp_invalid,    rdp_invalid,      rdp_invalid,      rdp_invalid,
  rdp_invalid,    rdp_invalid,      rdp_invalid,      rdp_invalid,
  rdp_invalid,    rdp_invalid,      rdp_invalid,      rdp_invalid,
  rdp_invalid,    rdp_invalid,      rdp_invalid,      rdp_invalid,
  
  rdp_invalid,    rdp_invalid,      rdp_invalid,      rdp_invalid,
  rdp_tex_rect,   rdp_tex_rect_flip,    rdp_sync_load,      rdp_sync_pipe,
  rdp_sync_tile,    rdp_sync_full,      rdp_set_key_gb,     rdp_set_key_r,
  rdp_set_convert,  rdp_set_scissor,    rdp_set_prim_depth,   rdp_set_other_modes,
  
  rdp_load_tlut,    rdp_invalid,      rdp_set_tile_size,    rdp_load_block,
  rdp_load_tile,    rdp_set_tile,     rdp_fill_rect,      rdp_set_fill_color,
  rdp_set_fog_color,  rdp_set_blend_color,  rdp_set_prim_color,   rdp_set_env_color,
  rdp_set_combine,  rdp_set_texture_image,  rdp_set_mask_image,   rdp_set_color_image
};

void RDPProcessList(struct RDP *rdp)
{
  my_rdp = rdp;
  int i, length;
  uint32_t cmd, cmd_length;
  uint32_t dp_current_al = my_rdp->regs[DPC_CURRENT_REG] & ~7, dp_end_al = my_rdp->regs[DPC_END_REG] & ~7; 

  my_rdp->regs[DPC_STATUS_REG] &= ~DP_STATUS_FREEZE;

  if (dp_end_al <= dp_current_al)
    return;

  length = (dp_end_al - dp_current_al) >> 2;

  if ((rdp_cmd_ptr + length) & ~0xffff)
    fatalerror("rdp_process_list: rdp_cmd_ptr overflow: length 0x%x rdp_cmd_ptr 0x%x", length, rdp_cmd_ptr);
  
  dp_current_al >>= 2;

  if (my_rdp->regs[DPC_STATUS_REG] & DP_STATUS_XBUS_DMA) {
    for (i = 0; i < length; i++) {
      rdp_cmd_data[rdp_cmd_ptr] = __builtin_bswap32(rsp_dmem[dp_current_al & 0x3ff]);
      rdp_cmd_ptr++;
      dp_current_al++;
    }
  }

  else {
    for (i = 0; i < length; i++) {
      rdp_cmd_data[rdp_cmd_ptr] = RREADIDX32(dp_current_al & 0x3fffff);
      rdp_cmd_ptr++;
      dp_current_al++;
    }
  }

  while (rdp_cmd_cur < rdp_cmd_ptr)
  {
    cmd = (rdp_cmd_data[rdp_cmd_cur] >> 24) & 0x3f;
    cmd_length = rdp_command_length[cmd] >> 2;
    
    if ((rdp_cmd_ptr - rdp_cmd_cur) < cmd_length) {
      my_rdp->regs[DPC_START_REG] = my_rdp->regs[DPC_CURRENT_REG] = my_rdp->regs[DPC_END_REG];
      return;
    }
    
    rdp_command_table[cmd](rdp_cmd_data[rdp_cmd_cur+0], rdp_cmd_data[rdp_cmd_cur + 1]);
    rdp_cmd_cur += cmd_length;
  };

  rdp_cmd_ptr = 0;
  rdp_cmd_cur = 0;
  my_rdp->regs[DPC_START_REG] = my_rdp->regs[DPC_CURRENT_REG] = my_rdp->regs[DPC_END_REG];
}

static int alpha_compare(int32_t comb_alpha)
{
  int32_t threshold;
  if (!other_modes.alpha_compare_en)
    return 1;
  else
  {
    if (!other_modes.dither_alpha_en)
      threshold = blend_color.a;
    else
      threshold = irand() & 0xff;
    if (comb_alpha >= threshold)
      return 1;
    else
      return 0;
  }
}

static int32_t color_combiner_equation(int32_t a, int32_t b, int32_t c, int32_t d)
{

  a = special_9bit_exttable[a];
  b = special_9bit_exttable[b];
  c = SIGNF(c, 9);
  d = special_9bit_exttable[d];
  a = ((a - b) * c) + (d << 8) + 0x80;
  return (a & 0x1ffff);
}

static int32_t alpha_combiner_equation(int32_t a, int32_t b, int32_t c, int32_t d)
{
  a = special_9bit_exttable[a];
  b = special_9bit_exttable[b];
  c = SIGNF(c, 9);
  d = special_9bit_exttable[d];
  a = (((a - b) * c) + (d << 8) + 0x80) >> 8;
  return (a & 0x1ff);
}

static void blender_equation_cycle0(int* r, int* g, int* b)
{
  int blend1a, blend2a;
  int blr, blg, blb, sum;
  blend1a = *blender1b_a[0] >> 3;
  blend2a = *blender2b_a[0] >> 3;

  int mulb;
    
  
  
  if (other_modes.f.special_bsel0)
  {
    blend1a = (blend1a >> blshifta) & 0x3C;
    blend2a = (blend2a >> blshiftb) | 3;
  }
  
  mulb = blend2a + 1;

  
  blr = (*blender1a_r[0]) * blend1a + (*blender2a_r[0]) * mulb;
  blg = (*blender1a_g[0]) * blend1a + (*blender2a_g[0]) * mulb;
  blb = (*blender1a_b[0]) * blend1a + (*blender2a_b[0]) * mulb;
  
  

  if (!other_modes.force_blend)
  {
    
    
    
    
    
    sum = ((blend1a & ~3) + (blend2a & ~3) + 4) << 9;
    *r = bldiv_hwaccurate_table[sum | ((blr >> 2) & 0x7ff)];
    *g = bldiv_hwaccurate_table[sum | ((blg >> 2) & 0x7ff)];
    *b = bldiv_hwaccurate_table[sum | ((blb >> 2) & 0x7ff)];
  }
  else
  {
    *r = (blr >> 5) & 0xff; 
    *g = (blg >> 5) & 0xff; 
    *b = (blb >> 5) & 0xff;
  } 
}

static void blender_equation_cycle0_2(int* r, int* g, int* b)
{
  int blend1a, blend2a;
  blend1a = *blender1b_a[0] >> 3;
  blend2a = *blender2b_a[0] >> 3;

  if (other_modes.f.special_bsel0)
  {
    blend1a = (blend1a >> pastblshifta) & 0x3C;
    blend2a = (blend2a >> pastblshiftb) | 3;
  }
  
  blend2a += 1;
  *r = (((*blender1a_r[0]) * blend1a + (*blender2a_r[0]) * blend2a) >> 5) & 0xff;
  *g = (((*blender1a_g[0]) * blend1a + (*blender2a_g[0]) * blend2a) >> 5) & 0xff;
  *b = (((*blender1a_b[0]) * blend1a + (*blender2a_b[0]) * blend2a) >> 5) & 0xff;
}

static void blender_equation_cycle1(int* r, int* g, int* b)
{
  int blend1a, blend2a;
  int blr, blg, blb, sum;
  blend1a = *blender1b_a[1] >> 3;
  blend2a = *blender2b_a[1] >> 3;

  int mulb;
  if (other_modes.f.special_bsel1)
  {
    blend1a = (blend1a >> blshifta) & 0x3C;
    blend2a = (blend2a >> blshiftb) | 3;
  }
  
  mulb = blend2a + 1;
  blr = (*blender1a_r[1]) * blend1a + (*blender2a_r[1]) * mulb;
  blg = (*blender1a_g[1]) * blend1a + (*blender2a_g[1]) * mulb;
  blb = (*blender1a_b[1]) * blend1a + (*blender2a_b[1]) * mulb;

  if (!other_modes.force_blend)
  {
    sum = ((blend1a & ~3) + (blend2a & ~3) + 4) << 9;
    *r = bldiv_hwaccurate_table[sum | ((blr >> 2) & 0x7ff)];
    *g = bldiv_hwaccurate_table[sum | ((blg >> 2) & 0x7ff)];
    *b = bldiv_hwaccurate_table[sum | ((blb >> 2) & 0x7ff)];
  }
  else
  {
    *r = (blr >> 5) & 0xff; 
    *g = (blg >> 5) & 0xff; 
    *b = (blb >> 5) & 0xff;
  }
}

static uint32_t rightcvghex(uint32_t x, uint32_t fmask)
{
  uint32_t covered = ((x & 7) + 1) >> 1;
  
  covered = 0xf0 >> covered;
  return (covered & fmask);
}

static uint32_t leftcvghex(uint32_t x, uint32_t fmask) 
{
  uint32_t covered = ((x & 7) + 1) >> 1;
  covered = 0xf >> covered;
  return (covered & fmask);
}

static void compute_cvg_flip(int32_t scanline)
{
  int32_t purgestart, purgeend;
  int i, length, fmask, maskshift, fmaskshifted;
  int32_t fleft, minorcur, majorcur, minorcurint, majorcurint, samecvg;
  
  purgestart = span[scanline].rx;
  purgeend = span[scanline].lx;
  length = purgeend - purgestart;
  if (length >= 0)
  {
    memset(&cvgbuf[purgestart], 0, (length + 1) << 2);
    for(i = 0; i < 4; i++)
    {
      if (!span[scanline].invalyscan[i])
      {
        minorcur = span[scanline].minorx[i];
        majorcur = span[scanline].majorx[i];
        minorcurint = minorcur >> 3;
        majorcurint = majorcur >> 3;
        fmask = 0xa >> (i & 1);
        
        
        
        
        maskshift = (i - 2) & 4;

        fmaskshifted = fmask << maskshift;
        fleft = majorcurint + 1;

        if (minorcurint != majorcurint)
        {
          cvgbuf[minorcurint] |= (rightcvghex(minorcur, fmask) << maskshift);
          cvgbuf[majorcurint] |= (leftcvghex(majorcur, fmask) << maskshift);
        }
        else
        {
          samecvg = rightcvghex(minorcur, fmask) & leftcvghex(majorcur, fmask);
          cvgbuf[majorcurint] |= (samecvg << maskshift);
        }
        for (; fleft < minorcurint; fleft++)
          cvgbuf[fleft] |= fmaskshifted;
      }
    }
  }
}

static void compute_cvg_noflip(int32_t scanline)
{
  int32_t purgestart, purgeend;
  int i, length, fmask, maskshift, fmaskshifted;
  int32_t fleft, minorcur, majorcur, minorcurint, majorcurint, samecvg;
  
  purgestart = span[scanline].lx;
  purgeend = span[scanline].rx;
  length = purgeend - purgestart;

  if (length >= 0)
  {
    memset(&cvgbuf[purgestart], 0, (length + 1) << 2);

    for(i = 0; i < 4; i++)
    {
      if (!span[scanline].invalyscan[i])
      {
        minorcur = span[scanline].minorx[i];
        majorcur = span[scanline].majorx[i];
        minorcurint = minorcur >> 3;
        majorcurint = majorcur >> 3;
        fmask = 0xa >> (i & 1);
        maskshift = (i - 2) & 4;
        fmaskshifted = fmask << maskshift;
        fleft = minorcurint + 1;

        if (minorcurint != majorcurint)
        {
          cvgbuf[minorcurint] |= (leftcvghex(minorcur, fmask) << maskshift);
          cvgbuf[majorcurint] |= (rightcvghex(majorcur, fmask) << maskshift);
        }
        else
        {
          samecvg = leftcvghex(minorcur, fmask) & rightcvghex(majorcur, fmask);
          cvgbuf[majorcurint] |= (samecvg << maskshift);
        }
        for (; fleft < majorcurint; fleft++)
          cvgbuf[fleft] |= fmaskshifted;
      }
    }
  }
}

int rdp_close()
{
  return 0;
}

static void fbfill_4(uint32_t curpixel) {
  debug("fbfill_4: Pipeline crashed.");
}

static void fbfill_8(uint32_t curpixel)
{
  uint32_t fb = fb_address + curpixel;
  uint32_t val = (fill_color >> (((fb & 3) ^ 3) << 3)) & 0xff;
  uint8_t hval = ((val & 1) << 1) | (val & 1);
  PAIRWRITE8(fb, val, hval);
}

static void fbfill_16(uint32_t curpixel)
{
  uint16_t val;
  uint8_t hval;
  uint32_t fb = (fb_address >> 1) + curpixel;
  if (fb & 1)
    val = fill_color & 0xffff;
  else
    val = (fill_color >> 16) & 0xffff;
  hval = ((val & 1) << 1) | (val & 1);
  PAIRWRITE16(fb, val, hval);
}

static void fbfill_32(uint32_t curpixel)
{
  uint32_t fb = (fb_address >> 2) + curpixel;
  PAIRWRITE32(fb, fill_color, (fill_color & 0x10000) ? 3 : 0, (fill_color & 0x1) ? 3 : 0);
}

static uint32_t z_decompress(uint32_t zb)
{
  return z_complete_dec_table[(zb >> 2) & 0x3fff];
}

static void lookup_cvmask_derivatives(uint32_t mask, uint8_t* offx, uint8_t* offy, uint32_t* curpixel_cvg, uint32_t* curpixel_cvbit)
{
  CVtcmaskDERIVATIVE temp = cvarray[mask];
  *curpixel_cvg = temp.cvg;
  *curpixel_cvbit = temp.cvbit;
  *offx = temp.xoff;
  *offy = temp.yoff;
}

static void z_store(uint32_t zcurpixel, uint32_t z, int dzpixenc)
{
  uint16_t zval = z_com_table[z & 0x3ffff]|(dzpixenc >> 2);
  uint8_t hval = dzpixenc & 3;
  PAIRWRITE16(zcurpixel, zval, hval);
}

static uint32_t dz_decompress(uint32_t dz_compressed)
{
  return (1 << dz_compressed);
}

static uint32_t dz_compress(uint32_t value) {
  static const uint8_t lut[4][16] align(16) = {
    {
      0x00, 0x0C, 0x0D, 0x0D,
      0x0E, 0x0E, 0x0F, 0x0F,
      0x0F, 0x0F, 0x0F, 0x0F,
      0x0F, 0x0F, 0x0F, 0x0F
    },
    {
      0x00, 0x08, 0x09, 0x09,
      0x0A, 0x0A, 0x0B, 0x0B,
      0x0B, 0x0B, 0x0B, 0x0B,
      0x0B, 0x0B, 0x0B, 0x0B
    },
    {
      0x00, 0x04, 0x05, 0x05,
      0x06, 0x06, 0x07, 0x07,
      0x07, 0x07, 0x07, 0x07,
      0x07, 0x07, 0x07, 0x0
    },
    {
      0x00, 0x00, 0x01, 0x01,
      0x02, 0x02, 0x03, 0x03,
      0x03, 0x03, 0x03, 0x03,
      0x03, 0x03, 0x03, 0x03
    }
  };

  return lut[0][(value >> 12)] | lut[1][(value >> 8 & 0xF)] |
    lut[2][(value >> 4 & 0xF)] | lut[3][(value & 0xF)];
}

static uint32_t z_compare(uint32_t zcurpixel, uint32_t sz, uint16_t dzpix, int dzpixenc, uint32_t* blend_en, uint32_t* prewrap, uint32_t* curpixel_cvg, uint32_t curpixel_memcvg)
{

  int force_coplanar = 0;
  sz &= 0x3ffff;

  uint32_t oz, dzmem, zval, hval;
  int32_t rawdzmem;

  if (other_modes.z_compare_en)
  {
    PAIRREAD16(zval, hval, zcurpixel);
    oz = z_decompress(zval);    
    rawdzmem = ((zval & 3) << 2) | hval;
    dzmem = dz_decompress(rawdzmem);

    
    blshifta = CLIP(dzpixenc - rawdzmem, 0, 4);
    blshiftb = CLIP(rawdzmem - dzpixenc, 0, 4);

    int precision_factor = (zval >> 13) & 0xf;

    
    uint32_t dzmemmodifier; 
    if (precision_factor < 3)
    {
      if (dzmem != 0x8000)
      {
        dzmemmodifier = 16 >> precision_factor;
        dzmem <<= 1;
        if (dzmem <= dzmemmodifier)
          dzmem = dzmemmodifier;
      }
      else
      {
        force_coplanar = 1;
        dzmem <<= 1;
      }
      
    }
    if (dzmem > 0x8000)
      dzmem = 0xffff;

    uint32_t dznew = (dzmem > dzpix) ? dzmem : (uint32_t)dzpix;
    uint32_t dznotshift = dznew;
    dznew <<= 3;
    

    uint32_t farther = force_coplanar || ((sz + dznew) >= oz);
    
    int overflow = (curpixel_memcvg + *curpixel_cvg) & 8;
    *blend_en = other_modes.force_blend || (!overflow && other_modes.antialias_en && farther);
    
    *prewrap = overflow;

    
    
    int cvgcoeff = 0;
    uint32_t dzenc = 0;
  
    int32_t diff;
    uint32_t nearer, max, infront;

    switch(other_modes.z_mode)
    {
    case ZMODE_OPAQUE: 
      infront = sz < oz;
      diff = (int32_t)sz - (int32_t)dznew;
      nearer = force_coplanar || (diff <= (int32_t)oz);
      max = (oz == 0x3ffff);
      return (max || (overflow ? infront : nearer));
      break;
    case ZMODE_INTERPENETRATING: 
      infront = sz < oz;
      if (!infront || !farther || !overflow)
      {
        diff = (int32_t)sz - (int32_t)dznew;
        nearer = force_coplanar || (diff <= (int32_t)oz);
        max = (oz == 0x3ffff);
        return (max || (overflow ? infront : nearer)); 
      }
      else
      {
        dzenc = dz_compress(dznotshift & 0xffff);
        cvgcoeff = ((oz >> dzenc) - (sz >> dzenc)) & 0xf;
        *curpixel_cvg = ((cvgcoeff * (*curpixel_cvg)) >> 3) & 0xf;
        return 1;
      }
      break;
    case ZMODE_TRANSPARENT: 
      infront = sz < oz;
      max = (oz == 0x3ffff);
      return (infront || max); 
      break;
    case ZMODE_DECAL: 
      diff = (int32_t)sz - (int32_t)dznew;
      nearer = force_coplanar || (diff <= (int32_t)oz);
      max = (oz == 0x3ffff);
      return (farther && nearer && !max); 
      break;
    }
    return 0;
  }
  else
  {
    

    blshifta = CLIP(dzpixenc - 0xf, 0, 4);
    blshiftb = CLIP(0xf - dzpixenc, 0, 4);

    int overflow = (curpixel_memcvg + *curpixel_cvg) & 8;
    *blend_en = other_modes.force_blend || (!overflow && other_modes.antialias_en);
    *prewrap = overflow;

    return 1;
  }
}

static int32_t normalize_dzpix(int32_t sum)
{
  if (sum & 0xc000)
    return 0x8000;
  if (!(sum & 0xffff))
    return 1;
  if (sum == 1)
    return 3;
  for(int count = 0x2000; count > 0; count >>= 1)
    {
      if (sum & count)
        return(count << 1);
    }
  fatalerror("normalize_dzpix: invalid codepath taken");
  return 0;
}

static int32_t CLIP(int32_t value,int32_t min,int32_t max)
{
  if (value < min)
    return min;
  else if (value > max)
    return max;
  else
    return value;
}

static void calculate_clamp_diffs(uint32_t i)
{
  tile[i].f.clampdiffs = ((tile[i].sh >> 2) - (tile[i].sl >> 2)) & 0x3ff;
  tile[i].f.clampdifft = ((tile[i].th >> 2) - (tile[i].tl >> 2)) & 0x3ff;
}

static void calculate_tile_derivs(uint32_t i)
{
  tile[i].f.clampens = tile[i].cs || !tile[i].mask_s;
  tile[i].f.clampent = tile[i].ct || !tile[i].mask_t;
  tile[i].f.masksclamped = tile[i].mask_s <= 10 ? tile[i].mask_s : 10;
  tile[i].f.masktclamped = tile[i].mask_t <= 10 ? tile[i].mask_t : 10;
  tile[i].f.notlutswitch = (tile[i].format << 2) | tile[i].size;
  tile[i].f.tlutswitch = (tile[i].size << 2) | ((tile[i].format + 2) & 3);
}

static void rgbaz_correct_clip(int offx, int offy, int r, int g, int b, int a, int* z, uint32_t curpixel_cvg)
{
  int summand_r, summand_b, summand_g, summand_a;
  int summand_z;
  int sz = *z;
  int zanded;

  if (curpixel_cvg == 8)
  {
    r >>= 2;
    g >>= 2;
    b >>= 2;
    a >>= 2;
    sz = sz >> 3;
  }
  else
  {
    summand_r = offx * spans_cdr + offy * spans_drdy;
    summand_g = offx * spans_cdg + offy * spans_dgdy;
    summand_b = offx * spans_cdb + offy * spans_dbdy;
    summand_a = offx * spans_cda + offy * spans_dady;
    summand_z = offx * spans_cdz + offy * spans_dzdy;

    r = ((r << 2) + summand_r) >> 4;
    g = ((g << 2) + summand_g) >> 4;
    b = ((b << 2) + summand_b) >> 4;
    a = ((a << 2) + summand_a) >> 4;
    sz = ((sz << 2) + summand_z) >> 5;
  }
  
  shade_color.r = special_9bit_clamptable[r & 0x1ff];
  shade_color.g = special_9bit_clamptable[g & 0x1ff];
  shade_color.b = special_9bit_clamptable[b & 0x1ff];
  shade_color.a = special_9bit_clamptable[a & 0x1ff];

  static const uint32_t zandtable[4] = {0x3FFFF, 0x3FFFF, 0,       0};
  static const uint32_t zortable[4] =  {0,       0,       0x3FFFF, 0};

  zanded = (sz & 0x00060000) >> 17;
  *z = (sz & zandtable[zanded]) | zortable[zanded];
}

static void tcdiv_nopersp(int32_t ss, int32_t st, int32_t sw, int32_t* sss, int32_t* sst)
{

  *sss = (SIGN16(ss)) & 0x1ffff;
  *sst = (SIGN16(st)) & 0x1ffff;
}

static void tcdiv_persp(int32_t ss, int32_t st, int32_t sw, int32_t* sss, int32_t* sst)
{

  int w_carry = 0;
  int shift; 
  int tlu_rcp;
    int sprod, tprod;
  int outofbounds_s, outofbounds_t;
  int tempmask;
  int shift_value;
  int32_t temps, tempt;

  
  
  int overunder_s = 0, overunder_t = 0;
  
  
  if (SIGN16(sw) <= 0)
    w_carry = 1;

  sw &= 0x7fff;

  
  
  shift = tcdiv_table[sw];
  tlu_rcp = shift >> 4;
  shift &= 0xf;

  sprod = SIGN16(ss) * tlu_rcp;
  tprod = SIGN16(st) * tlu_rcp;

  
  
  
  tempmask = ((1 << 30) - 1) & -((1 << 29) >> shift);
  
  outofbounds_s = sprod & tempmask;
  outofbounds_t = tprod & tempmask;
  
  if (shift != 0xe)
  {
    shift_value = 13 - shift;
    temps = sprod = (sprod >> shift_value);
    tempt = tprod = (tprod >> shift_value);
  }
  else
  {
    temps = sprod << 1;
    tempt = tprod << 1;
  }
  
  if (outofbounds_s != tempmask && outofbounds_s != 0)
  {
    if (!(sprod & (1 << 29)))
      overunder_s = 2 << 17;
    else
      overunder_s = 1 << 17;
  }

  if (outofbounds_t != tempmask && outofbounds_t != 0)
  {
    if (!(tprod & (1 << 29)))
      overunder_t = 2 << 17;
    else
      overunder_t = 1 << 17;
  }

  if (w_carry)
  {
    overunder_s |= (2 << 17);
    overunder_t |= (2 << 17);
  }

  *sss = (temps & 0x1ffff) | overunder_s;
  *sst = (tempt & 0x1ffff) | overunder_t;
}

static void tclod_2cycle_current(int32_t* sss, int32_t* sst, int32_t nexts, int32_t nextt, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t prim_tile, int32_t* t1, int32_t* t2)
{

  int nextys, nextyt, nextysw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile;
  uint32_t magnify = 0;
  uint32_t distant = 0;
  int inits = *sss, initt = *sst;

  *sss = tclod_tcclamp(*sss);
  *sst = tclod_tcclamp(*sst);

  if (other_modes.f.dolod)
  {
    
    
    
    
    
    
    nextys = (s + spans_dsdy) >> 16;
    nextyt = (t + spans_dtdy) >> 16;
    nextysw = (w + spans_dwdy) >> 16;

    tcdiv_ptr(nextys, nextyt, nextysw, &nextys, &nextyt);

    lodclamp = (initt & 0x60000) || (nextt & 0x60000) || (inits & 0x60000) || (nexts & 0x60000) || (nextys & 0x60000) || (nextyt & 0x60000);
    
    

    
    tclod_4x17_to_15(inits, nexts, initt, nextt, 0, &lod);
    tclod_4x17_to_15(inits, nextys, initt, nextyt, lod, &lod);

    lodfrac_lodtile_signals(lodclamp, lod, &l_tile, &magnify, &distant);

    
    if (other_modes.tex_lod_en)
    {
      if (distant)
        l_tile = max_level;
      if (!other_modes.detail_tex_en)
      {
        *t1 = (prim_tile + l_tile) & 7;
        if (!(distant || (!other_modes.sharpen_tex_en && magnify)))
          *t2 = (*t1 + 1) & 7;
        else
          *t2 = *t1;
      }
      else 
      {
        if (!magnify)
          *t1 = (prim_tile + l_tile + 1);
        else
          *t1 = (prim_tile + l_tile);
        *t1 &= 7;
        if (!distant && !magnify)
          *t2 = (prim_tile + l_tile + 2) & 7;
        else
          *t2 = (prim_tile + l_tile + 1) & 7;
      }
    }
  }
}

static void tclod_2cycle_current_simple(int32_t* sss, int32_t* sst, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t prim_tile, int32_t* t1, int32_t* t2)
{
  int nextys, nextyt, nextysw, nexts, nextt, nextsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile;
  uint32_t magnify = 0;
  uint32_t distant = 0;
  int inits = *sss, initt = *sst;

  *sss = tclod_tcclamp(*sss);
  *sst = tclod_tcclamp(*sst);

  if (other_modes.f.dolod)
  {
    nextsw = (w + dwinc) >> 16;
    nexts = (s + dsinc) >> 16;
    nextt = (t + dtinc) >> 16;
    nextys = (s + spans_dsdy) >> 16;
    nextyt = (t + spans_dtdy) >> 16;
    nextysw = (w + spans_dwdy) >> 16;

    tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
    tcdiv_ptr(nextys, nextyt, nextysw, &nextys, &nextyt);

    lodclamp = (initt & 0x60000) || (nextt & 0x60000) || (inits & 0x60000) || (nexts & 0x60000) || (nextys & 0x60000) || (nextyt & 0x60000);

    tclod_4x17_to_15(inits, nexts, initt, nextt, 0, &lod);
    tclod_4x17_to_15(inits, nextys, initt, nextyt, lod, &lod);

    lodfrac_lodtile_signals(lodclamp, lod, &l_tile, &magnify, &distant);
  
    if (other_modes.tex_lod_en)
    {
      if (distant)
        l_tile = max_level;
      if (!other_modes.detail_tex_en)
      {
        *t1 = (prim_tile + l_tile) & 7;
        if (!(distant || (!other_modes.sharpen_tex_en && magnify)))
          *t2 = (*t1 + 1) & 7;
        else
          *t2 = *t1;
      }
      else 
      {
        if (!magnify)
          *t1 = (prim_tile + l_tile + 1);
        else
          *t1 = (prim_tile + l_tile);
        *t1 &= 7;
        if (!distant && !magnify)
          *t2 = (prim_tile + l_tile + 2) & 7;
        else
          *t2 = (prim_tile + l_tile + 1) & 7;
      }
    }
  }
}

static void tclod_2cycle_current_notexel1(int32_t* sss, int32_t* sst, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t prim_tile, int32_t* t1)
{
  int nextys, nextyt, nextysw, nexts, nextt, nextsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile;
  uint32_t magnify = 0;
  uint32_t distant = 0;
  int inits = *sss, initt = *sst;

  *sss = tclod_tcclamp(*sss);
  *sst = tclod_tcclamp(*sst);

  if (other_modes.f.dolod)
  {
    nextsw = (w + dwinc) >> 16;
    nexts = (s + dsinc) >> 16;
    nextt = (t + dtinc) >> 16;
    nextys = (s + spans_dsdy) >> 16;
    nextyt = (t + spans_dtdy) >> 16;
    nextysw = (w + spans_dwdy) >> 16;

    tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
    tcdiv_ptr(nextys, nextyt, nextysw, &nextys, &nextyt);

    lodclamp = (initt & 0x60000) || (nextt & 0x60000) || (inits & 0x60000) || (nexts & 0x60000) || (nextys & 0x60000) || (nextyt & 0x60000);

    tclod_4x17_to_15(inits, nexts, initt, nextt, 0, &lod);
    tclod_4x17_to_15(inits, nextys, initt, nextyt, lod, &lod);

    lodfrac_lodtile_signals(lodclamp, lod, &l_tile, &magnify, &distant);
  
    if (other_modes.tex_lod_en)
    {
      if (distant)
        l_tile = max_level;
      if (!other_modes.detail_tex_en || magnify)
        *t1 = (prim_tile + l_tile) & 7;
      else
        *t1 = (prim_tile + l_tile + 1) & 7;
    }
    
  }
}

static void tclod_2cycle_next(int32_t* sss, int32_t* sst, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t prim_tile, int32_t* t1, int32_t* t2, int32_t* prelodfrac)
{
  int nexts, nextt, nextsw, nextys, nextyt, nextysw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile;
  uint32_t magnify = 0;
  uint32_t distant = 0;
  int inits = *sss, initt = *sst;

  *sss = tclod_tcclamp(*sss);
  *sst = tclod_tcclamp(*sst);

  if (other_modes.f.dolod)
  {
    nextsw = (w + dwinc) >> 16;
    nexts = (s + dsinc) >> 16;
    nextt = (t + dtinc) >> 16;
    nextys = (s + spans_dsdy) >> 16;
    nextyt = (t + spans_dtdy) >> 16;
    nextysw = (w + spans_dwdy) >> 16;

    tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
    tcdiv_ptr(nextys, nextyt, nextysw, &nextys, &nextyt);
  
    lodclamp = (initt & 0x60000) || (nextt & 0x60000) || (inits & 0x60000) || (nexts & 0x60000) || (nextys & 0x60000) || (nextyt & 0x60000);

    tclod_4x17_to_15(inits, nexts, initt, nextt, 0, &lod);
    tclod_4x17_to_15(inits, nextys, initt, nextyt, lod, &lod);

    
    if ((lod & 0x4000) || lodclamp)
      lod = 0x7fff;
    else if (lod < min_level)
      lod = min_level;
            
    magnify = (lod < 32) ? 1: 0;
    l_tile =  log2table[(lod >> 5) & 0xff];
    distant = ((lod & 0x6000) || (l_tile >= max_level)) ? 1 : 0;

    *prelodfrac = ((lod << 3) >> l_tile) & 0xff;

    
    if(!other_modes.sharpen_tex_en && !other_modes.detail_tex_en)
    {
      if (distant)
        *prelodfrac = 0xff;
      else if (magnify)
        *prelodfrac = 0;
    }

    
    

    if(other_modes.sharpen_tex_en && magnify)
      *prelodfrac |= 0x100;

    if (other_modes.tex_lod_en)
    {
      if (distant)
        l_tile = max_level;
      if (!other_modes.detail_tex_en)
      {
        *t1 = (prim_tile + l_tile) & 7;
        if (!(distant || (!other_modes.sharpen_tex_en && magnify)))
          *t2 = (*t1 + 1) & 7;
        else
          *t2 = *t1;
      }
      else 
      {
        if (!magnify)
          *t1 = (prim_tile + l_tile + 1);
        else
          *t1 = (prim_tile + l_tile);
        *t1 &= 7;
        if (!distant && !magnify)
          *t2 = (prim_tile + l_tile + 2) & 7;
        else
          *t2 = (prim_tile + l_tile + 1) & 7;
      }
    }
  }
}

static void tclod_1cycle_current(int32_t* sss, int32_t* sst, int32_t nexts, int32_t nextt, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t scanline, int32_t prim_tile, int32_t* t1, SPANSIGS* sigs)
{
  int fars, fart, farsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile = 0, magnify = 0, distant = 0;
  
  *sss = tclod_tcclamp(*sss);
  *sst = tclod_tcclamp(*sst);

  if (other_modes.f.dolod)
  {
    int nextscan = scanline + 1;

    
    if (span[nextscan].validline)
    {
      if (!sigs->endspan || !sigs->longspan)
      {
        if (!(sigs->preendspan && sigs->longspan) && !(sigs->endspan && sigs->midspan))
        {
          farsw = (w + (dwinc << 1)) >> 16;
          fars = (s + (dsinc << 1)) >> 16;
          fart = (t + (dtinc << 1)) >> 16;
        }
        else
        {
          farsw = (w - dwinc) >> 16;
          fars = (s - dsinc) >> 16;
          fart = (t - dtinc) >> 16;
        }
      }
      else
      {
        fart = (span[nextscan].t + dtinc) >> 16; 
        fars = (span[nextscan].s + dsinc) >> 16; 
        farsw = (span[nextscan].w + dwinc) >> 16;
      }
    }
    else
    {
      farsw = (w + (dwinc << 1)) >> 16;
      fars = (s + (dsinc << 1)) >> 16;
      fart = (t + (dtinc << 1)) >> 16;
    }

    tcdiv_ptr(fars, fart, farsw, &fars, &fart);

    lodclamp = (fart & 0x60000) || (nextt & 0x60000) || (fars & 0x60000) || (nexts & 0x60000);
    
    

    
    tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);

    lodfrac_lodtile_signals(lodclamp, lod, &l_tile, &magnify, &distant);
  
    if (other_modes.tex_lod_en)
    {
      if (distant)
        l_tile = max_level;

      
      
      if (!other_modes.detail_tex_en || magnify)
        *t1 = (prim_tile + l_tile) & 7;
      else
        *t1 = (prim_tile + l_tile + 1) & 7;
    }
  }
}

static void tclod_1cycle_next(int32_t* sss, int32_t* sst, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t scanline, int32_t prim_tile, int32_t* t1, SPANSIGS* sigs, int32_t* prelodfrac)
{
  int nexts, nextt, nextsw, fars, fart, farsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile = 0, magnify = 0, distant = 0;
  
  *sss = tclod_tcclamp(*sss);
  *sst = tclod_tcclamp(*sst);

  if (other_modes.f.dolod)
  {
    
    int nextscan = scanline + 1;
    
    if (span[nextscan].validline)
    {
      if (!sigs->nextspan)
      {
        if (!sigs->endspan || !sigs->longspan)
        {
          nextsw = (w + dwinc) >> 16;
          nexts = (s + dsinc) >> 16;
          nextt = (t + dtinc) >> 16;
          
          if (!(sigs->preendspan && sigs->longspan) && !(sigs->endspan && sigs->midspan))
          {
            farsw = (w + (dwinc << 1)) >> 16;
            fars = (s + (dsinc << 1)) >> 16;
            fart = (t + (dtinc << 1)) >> 16;
          }
          else
          {
            farsw = (w - dwinc) >> 16;
            fars = (s - dsinc) >> 16;
            fart = (t - dtinc) >> 16;
          }
        }
        else
        {
          nextt = span[nextscan].t;
          nexts = span[nextscan].s;
          nextsw = span[nextscan].w;
          fart = (nextt + dtinc) >> 16; 
          fars = (nexts + dsinc) >> 16; 
          farsw = (nextsw + dwinc) >> 16;
          nextt >>= 16;
          nexts >>= 16;
          nextsw >>= 16;
        }
      }
      else
      {
        if (sigs->longspan || sigs->midspan)
        {
          nextt = span[nextscan].t + dtinc;
          nexts = span[nextscan].s + dsinc;
          nextsw = span[nextscan].w + dwinc;
          fart = (nextt + dtinc) >> 16; 
          fars = (nexts + dsinc) >> 16; 
          farsw = (nextsw + dwinc) >> 16;
          nextt >>= 16;
          nexts >>= 16;
          nextsw >>= 16;
        }
        else
        {
          nextsw = (w + dwinc) >> 16;
          nexts = (s + dsinc) >> 16;
          nextt = (t + dtinc) >> 16;
          farsw = (w - dwinc) >> 16;
          fars = (s - dsinc) >> 16;
          fart = (t - dtinc) >> 16;
        }
      }
    }
    else
    {
      nextsw = (w + dwinc) >> 16;
      nexts = (s + dsinc) >> 16;
      nextt = (t + dtinc) >> 16;
      farsw = (w + (dwinc << 1)) >> 16;
      fars = (s + (dsinc << 1)) >> 16;
      fart = (t + (dtinc << 1)) >> 16;
    }

    tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
    tcdiv_ptr(fars, fart, farsw, &fars, &fart);

    lodclamp = (fart & 0x60000) || (nextt & 0x60000) || (fars & 0x60000) || (nexts & 0x60000);
    
    
    tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);

    
    if ((lod & 0x4000) || lodclamp)
      lod = 0x7fff;
    else if (lod < min_level)
      lod = min_level;
          
    magnify = (lod < 32) ? 1: 0;
    l_tile =  log2table[(lod >> 5) & 0xff];
    distant = ((lod & 0x6000) || (l_tile >= max_level)) ? 1 : 0;

    *prelodfrac = ((lod << 3) >> l_tile) & 0xff;

    
    if(!other_modes.sharpen_tex_en && !other_modes.detail_tex_en)
    {
      if (distant)
        *prelodfrac = 0xff;
      else if (magnify)
        *prelodfrac = 0;
    }

    if(other_modes.sharpen_tex_en && magnify)
      *prelodfrac |= 0x100;

    if (other_modes.tex_lod_en)
    {
      if (distant)
        l_tile = max_level;
      if (!other_modes.detail_tex_en || magnify)
        *t1 = (prim_tile + l_tile) & 7;
      else
        *t1 = (prim_tile + l_tile + 1) & 7;
    }
  }
}

static void tclod_copy(int32_t* sss, int32_t* sst, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t prim_tile, int32_t* t1)
{

  int nexts, nextt, nextsw, fars, fart, farsw;
  int lodclamp = 0;
  int32_t lod = 0;
  uint32_t l_tile = 0, magnify = 0, distant = 0;

  *sss = tclod_tcclamp(*sss);
  *sst = tclod_tcclamp(*sst);

  if (other_modes.tex_lod_en)
  {
    
    
    
    nextsw = (w + dwinc) >> 16;
    nexts = (s + dsinc) >> 16;
    nextt = (t + dtinc) >> 16;
    farsw = (w + (dwinc << 1)) >> 16;
    fars = (s + (dsinc << 1)) >> 16;
    fart = (t + (dtinc << 1)) >> 16;
  
    tcdiv_ptr(nexts, nextt, nextsw, &nexts, &nextt);
    tcdiv_ptr(fars, fart, farsw, &fars, &fart);

    lodclamp = (fart & 0x60000) || (nextt & 0x60000) || (fars & 0x60000) || (nexts & 0x60000);

    tclod_4x17_to_15(nexts, fars, nextt, fart, 0, &lod);

    if ((lod & 0x4000) || lodclamp)
      lod = 0x7fff;
    else if (lod < min_level)
      lod = min_level;
            
    magnify = (lod < 32) ? 1: 0;
    l_tile =  log2table[(lod >> 5) & 0xff];
    distant = ((lod & 0x6000) || (l_tile >= max_level)) ? 1 : 0;

    if (distant)
      l_tile = max_level;
  
    if (!other_modes.detail_tex_en || magnify)
      *t1 = (prim_tile + l_tile) & 7;
    else
      *t1 = (prim_tile + l_tile + 1) & 7;
  }

}

static void get_texel1_1cycle(int32_t* s1, int32_t* t1, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc, int32_t scanline, SPANSIGS* sigs)
{
  int32_t nexts, nextt, nextsw;
  
  if (!sigs->endspan || !sigs->longspan || !span[scanline + 1].validline)
  {
  
  
    nextsw = (w + dwinc) >> 16;
    nexts = (s + dsinc) >> 16;
    nextt = (t + dtinc) >> 16;
  }
  else
  {
    int32_t nextscan = scanline + 1;
    nextt = span[nextscan].t >> 16;
    nexts = span[nextscan].s >> 16;
    nextsw = span[nextscan].w >> 16;
  }

  tcdiv_ptr(nexts, nextt, nextsw, s1, t1);
}

static void get_nexttexel0_2cycle(int32_t* s1, int32_t* t1, int32_t s, int32_t t, int32_t w, int32_t dsinc, int32_t dtinc, int32_t dwinc)
{
  int32_t nexts, nextt, nextsw;
  nextsw = (w + dwinc) >> 16;
  nexts = (s + dsinc) >> 16;
  nextt = (t + dtinc) >> 16;

  tcdiv_ptr(nexts, nextt, nextsw, s1, t1);
}

void tclod_4x17_to_15(int32_t scurr, int32_t snext, int32_t tcurr, int32_t tnext, int32_t previous, int32_t* lod)
{

  int dels = SIGN(snext, 17) - SIGN(scurr, 17);
  if (dels & 0x20000)
    dels = ~dels & 0x1ffff;
  int delt = SIGN(tnext, 17) - SIGN(tcurr, 17);
  if(delt & 0x20000)
    delt = ~delt & 0x1ffff;
  

  dels = (dels > delt) ? dels : delt;
  dels = (previous > dels) ? previous : dels;
  *lod = dels & 0x7fff;
  if (dels & 0x1c000)
    *lod |= 0x4000;
}

void lodfrac_lodtile_signals(int lodclamp, int32_t lod, uint32_t* l_tile, uint32_t* magnify, uint32_t* distant)
{
  uint32_t ltil, dis, mag;
  int32_t lf;

  
  if ((lod & 0x4000) || lodclamp)
    lod = 0x7fff;
  else if (lod < min_level)
    lod = min_level;
            
  mag = (lod < 32) ? 1: 0;
  ltil=  log2table[(lod >> 5) & 0xff];
  dis = ((lod & 0x6000) || (ltil >= max_level)) ? 1 : 0;
            
  lf = ((lod << 3) >> ltil) & 0xff;

  
  if(!other_modes.sharpen_tex_en && !other_modes.detail_tex_en)
  {
    if (dis)
      lf = 0xff;
    else if (mag)
      lf = 0;
  }

  
  

  if(other_modes.sharpen_tex_en && mag)
    lf |= 0x100;

  *distant = dis;
  *l_tile = ltil;
  *magnify = mag;
  lod_frac = lf;
}

