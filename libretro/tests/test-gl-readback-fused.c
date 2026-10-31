/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swizzle.h"
typedef void *GLsync;typedef uint64_t GLuint64;
#define GL_PIXEL_PACK_BUFFER 1
#define GL_MAP_READ_BIT 2
#define GL_SYNC_FLUSH_COMMANDS_BIT 4
#define GL_FRAMEBUFFER 8
#define GL_TEXTURE_2D 16
#define GL_FRAMEBUFFER_COMPLETE 32
#define trace_nv2a_pgraph_surface_download(...) ((void)0)
#define g_realloc realloc
typedef struct {void *data;} Scratch;
typedef struct {Scratch surf_scratch;unsigned gl_fb_util,fb_bound;} PGRAPHGLState;
typedef struct {unsigned surface_scale_factor;uint8_t *scale_buf;PGRAPHGLState *gl_renderer_state;} PGRAPHState;
typedef struct {PGRAPHState pgraph;uint8_t *vram_ptr;} NV2AState;
typedef struct {unsigned width,height,pitch,size,vram_addr,gl_buffer,dl_pbo;size_t dl_pbo_size;GLsync dl_fence;unsigned dl_draw_time,draw_time;bool color,swizzle,dl_swz,dl_native;struct {unsigned bytes_per_pixel,gl_attachment,gl_format,gl_type;} fmt;} SurfaceBinding;
static uint8_t *raw;static size_t raw_size;static unsigned scratches,unmaps,deletes,restores;
static void *scratch_reserve(Scratch *s,size_t size){scratches++;s->data=realloc(s->data,size);assert(s->data);return s->data;}
static bool surface_swizzle_on_gpu(NV2AState*d,SurfaceBinding*s){(void)d;(void)s;return false;}
static bool surface_shrink_on_gpu(PGRAPHState*p,SurfaceBinding*s){(void)p;(void)s;return false;}
static void fb_bind(PGRAPHGLState*r,unsigned fbo){r->fb_bound=fbo;}
static void glBindFramebuffer(unsigned target,unsigned fbo){(void)target;(void)fbo;}
static void glFramebufferTexture2D(unsigned a,unsigned b,unsigned c,unsigned d,unsigned e){(void)a;(void)b;(void)c;(void)d;(void)e;}
static unsigned glCheckFramebufferStatus(unsigned target){(void)target;return GL_FRAMEBUFFER_COMPLETE;}
static void bind_current_surface(NV2AState*d){(void)d;restores++;}
static void glBindBuffer(unsigned target,unsigned buffer){(void)target;(void)buffer;}
static void *glMapBufferRange(unsigned target,size_t offset,size_t size,unsigned flags){(void)target;(void)flags;assert(!offset&&size==raw_size);return raw;}
static void glUnmapBuffer(unsigned target){(void)target;unmaps++;}
static void glDeleteSync(GLsync s){assert(s);deletes++;}
static void glClientWaitSync(GLsync s,unsigned flags,GLuint64 time){(void)flags;(void)time;assert(s);}
static void glo_readpixels(unsigned format,unsigned type,unsigned bpp,unsigned pitch,unsigned w,unsigned h,bool flip,void *dst){
 (void)format;(void)type;assert((size_t)pitch*h==raw_size);
 for(unsigned y=0;y<h;y++)memcpy((uint8_t*)dst+y*pitch,raw+(flip?h-1-y:y)*pitch,w*bpp);
}
/* PRODUCTION */
/* Independent Morton oracle: deposit the coordinate bits, then choose the
 * original top-left sample. No intermediate native image is constructed. */
static size_t morton(unsigned x,unsigned y,unsigned w,unsigned h){size_t offset=0;unsigned lane=0;for(unsigned bit=1;bit<w||bit<h;bit<<=1){if(bit<w){if(x&bit)offset|=(size_t)1<<lane;lane++;}if(bit<h){if(y&bit)offset|=(size_t)1<<lane;lane++;}}return offset;}
static void reference(uint8_t *out,const uint8_t *src,unsigned w,unsigned h,unsigned pitch,unsigned bpp,unsigned factor,bool swizzle,bool flip){
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  size_t sy=flip?(size_t)h*factor-1-(size_t)y*factor:(size_t)y*factor;
  size_t offset=swizzle?morton(x,y,w,h)*bpp:(size_t)y*pitch+x*bpp;
  memcpy(out+offset,src+sy*pitch*factor+(size_t)x*factor*bpp,bpp);
 }
}
int main(void){unsigned cases=0;size_t pixels=0;unsigned sizes[]={1,2,4,8,32,64},formats[]={1,2,3,4,8},factors[]={1,2,3,4,10};
 for(unsigned iw=0;iw<6;iw++)for(unsigned ih=0;ih<6;ih++)for(unsigned ib=0;ib<5;ib++)for(unsigned is=0;is<5;is++){
  unsigned w=sizes[iw],h=sizes[ih],bpp=formats[ib],sf=factors[is],pitch=w*bpp+8;
  raw_size=(size_t)pitch*sf*sf*h;raw=malloc(raw_size);assert(raw);
  for(size_t i=0;i<raw_size;i++)raw[i]=(uint8_t)(i*37+(i>>9)*13);
  size_t bytes=(size_t)pitch*h;
  uint8_t *actual=malloc(bytes+32),*expected=malloc(bytes+32);assert(actual&&expected);
  NV2AState d={0};PGRAPHGLState renderer={.gl_fb_util=99};d.pgraph.surface_scale_factor=sf;d.pgraph.gl_renderer_state=&renderer;d.vram_ptr=actual;
  SurfaceBinding surface={.width=w,.height=h,.pitch=pitch,.size=bytes,.gl_buffer=1,.dl_pbo=2,.dl_pbo_size=raw_size,.dl_draw_time=7,.draw_time=7,.swizzle=true,.fmt.bytes_per_pixel=bpp};
  for(unsigned flip=0;flip<2;flip++){
   memset(actual,0xAB,bytes+32);memset(expected,0xAB,bytes+32);scratches=restores=0;
   reference(expected,raw,w,h,pitch,bpp,sf,true,flip);
   surface_download_to_buffer(&d,&surface,true,flip,true,actual);
   assert(!memcmp(actual,expected,bytes+32));assert(restores==1);assert(scratches==(sf==1?1u:0u));
  }
  memset(actual,0xAB,bytes+32);memset(expected,0xAB,bytes+32);scratches=unmaps=deletes=0;
  reference(expected,raw,w,h,pitch,bpp,sf,true,false);surface.dl_fence=(void*)1;
  assert(surface_download_harvest(&d,&surface));assert(!memcmp(actual,expected,bytes+32));assert(!scratches&&unmaps==1&&deletes==1&&!surface.dl_fence);
  surface.dl_fence=(void*)1;surface.dl_draw_time=6;assert(!surface_download_harvest(&d,&surface));assert(unmaps==1&&deletes==2);
  free(renderer.surf_scratch.data);free(d.pgraph.scale_buf);free(raw);free(actual);free(expected);cases++;pixels+=(size_t)w*h;
 }
 printf("%u synchronous/flip/pending/stale cases, %zu pixels: byte-exact Morton samples, scratch allocations eliminated for scaled swizzled fallback, guards/lifecycle PASS\n",cases,pixels);return 0;
}
