/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef _WIN32
#include "ui/gl-dispatch.h"
#include "gloffscreen.h"
extern void libretro_gl_set_standalone_mode(void);
#else
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <GL/glext.h>
#endif
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "swizzle.h"
#define ROUND_UP(n,a) (((n)+(a)-1)&~((a)-1))
#define g_realloc realloc
typedef struct {uint8_t *data;size_t size;} Scratch;
typedef struct {GLuint gl_fb_util,fb_bound,ul_zgrow_fbo,ul_zgrow_tex,ul_grow_fbo,ul_grow_tex;unsigned ul_zgrow_w,ul_zgrow_h,ul_grow_w,ul_grow_h;GLint ul_zgrow_fmt,ul_grow_fmt;Scratch surf_scratch,scale_scratch;} PGRAPHGLState;
typedef struct {unsigned surface_scale_factor;PGRAPHGLState *gl_renderer_state;} PGRAPHState;
typedef struct {PGRAPHState pgraph;uint8_t *vram_ptr;} NV2AState;
typedef struct {unsigned width,height,pitch,vram_addr,size,tex_w,tex_h;GLint tex_fmt;GLuint gl_buffer;bool color,swizzle;struct {unsigned bytes_per_pixel;GLenum gl_format,gl_type,gl_internal_format,gl_attachment;} fmt;} SurfaceBinding;
static unsigned restores,invalidations,reservations;
static uint8_t *scratch_reserve(Scratch*s,size_t bytes){reservations++;if(bytes>s->size){s->data=realloc(s->data,bytes);s->size=bytes;}assert(s->data);return s->data;}
static void pgraph_apply_scaling_factor(PGRAPHState*p,unsigned*w,unsigned*h){*w*=p->surface_scale_factor;*h*=p->surface_scale_factor;}
static void pgraph_gl_draw_state_invalidate(PGRAPHGLState*r){(void)r;invalidations++;}
static void bind_current_surface(NV2AState*d){(void)d;glBindFramebuffer(GL_FRAMEBUFFER,0);restores++;}
/* PRODUCTION */
/* The previous compact upload + native nearest blit, independently staged.
 * Depth blits may quantize a low bit: compare to this GPU reference, not an
 * assumed raw-byte-preserving depth conversion. */
static void reference_gpu(const SurfaceBinding *s,const uint8_t *linear,unsigned sf,uint8_t *out)
{
 unsigned w=s->width,h=s->height,bpp=s->fmt.bytes_per_pixel;
 uint8_t *compact=malloc((size_t)w*h*bpp);assert(compact);
 for(unsigned y=0;y<h;y++)memcpy(compact+(size_t)y*w*bpp,linear+(size_t)y*s->pitch,w*bpp);
 GLuint textures[2],fbos[2];glGenTextures(2,textures);glGenFramebuffers(2,fbos);
 glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
 glBindTexture(GL_TEXTURE_2D,textures[0]);glTexImage2D(GL_TEXTURE_2D,0,s->fmt.gl_internal_format,w,h,0,s->fmt.gl_format,s->fmt.gl_type,compact);
 glBindTexture(GL_TEXTURE_2D,textures[1]);glTexImage2D(GL_TEXTURE_2D,0,s->fmt.gl_internal_format,w*sf,h*sf,0,s->fmt.gl_format,s->fmt.gl_type,NULL);
 glBindFramebuffer(GL_READ_FRAMEBUFFER,fbos[0]);glFramebufferTexture2D(GL_READ_FRAMEBUFFER,s->fmt.gl_attachment,GL_TEXTURE_2D,textures[0],0);
 glBindFramebuffer(GL_DRAW_FRAMEBUFFER,fbos[1]);glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,s->fmt.gl_attachment,GL_TEXTURE_2D,textures[1],0);
 GLbitfield mask=s->color?GL_COLOR_BUFFER_BIT:GL_DEPTH_BUFFER_BIT;
 if(s->fmt.gl_attachment==GL_DEPTH_STENCIL_ATTACHMENT)mask|=GL_STENCIL_BUFFER_BIT;
 glDisable(GL_SCISSOR_TEST);glBlitFramebuffer(0,0,w,h,0,0,w*sf,h*sf,mask,GL_NEAREST);
 glBindTexture(GL_TEXTURE_2D,textures[1]);glGetTexImage(GL_TEXTURE_2D,0,s->fmt.gl_format,s->fmt.gl_type,out);
 glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(2,fbos);glDeleteTextures(2,textures);free(compact);
}
static unsigned cases,direct,gpu,packed,expanded;
static void run_case(NV2AState*d,unsigned w,unsigned h,unsigned bpp,unsigned pad,unsigned sf,GLenum internal,GLenum format,GLenum type,GLenum attachment,bool allow_gpu,bool swizzle)
{
 size_t pitch=w*bpp+pad,span=pitch*h,bytes=(size_t)w*h*sf*sf*bpp;
 uint8_t *source=malloc(span+32),*linear=malloc(span),*actual=malloc(bytes),*expected=malloc(bytes);
 assert(source&&linear&&actual&&expected);memset(source,0xAB,span+32);
 for(size_t i=0;i<span;i++)linear[i]=(uint8_t)(i*31+(i>>7)*17+cases);
 if(swizzle)swizzle_rect(linear,w,h,source,pitch,bpp);else memcpy(source,linear,span);
 for(unsigned y=0;y<h*sf;y++)for(unsigned x=0;x<w*sf;x++)memcpy(expected+((size_t)y*w*sf+x)*bpp,linear+(y/sf)*pitch+(x/sf)*bpp,bpp);
 d->vram_ptr=source;d->pgraph.surface_scale_factor=sf;
 SurfaceBinding surface={.width=w,.height=h,.pitch=pitch,.size=span,.color=attachment==GL_COLOR_ATTACHMENT0,.swizzle=swizzle,.tex_w=allow_gpu?w*sf:0,.tex_h=h*sf,.tex_fmt=internal,.fmt={bpp,format,type,internal,attachment}};
 glPixelStorei(GL_UNPACK_ALIGNMENT,1);glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
 GLuint target,sentinel;glGenTextures(1,&target);glGenTextures(1,&sentinel);surface.gl_buffer=target;
 glBindTexture(GL_TEXTURE_2D,target);glTexImage2D(GL_TEXTURE_2D,0,internal,w*sf,h*sf,0,format,type,NULL);
 glBindTexture(GL_TEXTURE_2D,sentinel);glPixelStorei(GL_UNPACK_ALIGNMENT,8);glPixelStorei(GL_UNPACK_ROW_LENGTH,37);
 unsigned previous_gpu=invalidations;restores=reservations=0;
 upload_pixels(d,&surface);
 GLint value;glGetIntegerv(GL_TEXTURE_BINDING_2D,&value);assert(value==(GLint)sentinel);
 glGetIntegerv(GL_UNPACK_ALIGNMENT,&value);assert(value==8);glGetIntegerv(GL_UNPACK_ROW_LENGTH,&value);assert(value==37);assert(restores==1);
 GLint row,align;bool representable=surface_unpack_layout(w,bpp,pitch,&row,&align);
 if(invalidations!=previous_gpu){assert(sf>1&&allow_gpu);gpu++;}
 else if(sf>1)expanded++;else if(!representable)packed++;else direct++;
 assert(reservations==(swizzle?1u:0u)+(!representable||(sf>1&&invalidations==previous_gpu)?1u:0u));
 glBindTexture(GL_TEXTURE_2D,target);glGetTexImage(GL_TEXTURE_2D,0,format,type,actual);
 if(invalidations!=previous_gpu)reference_gpu(&surface,linear,sf,expected);
 if(memcmp(actual,expected,bytes)){for(size_t i=0;i<bytes;i++)if(actual[i]!=expected[i]){fprintf(stderr,"w=%u bpp=%u pad=%u sf=%u GPU=%u format=%x byte=%zu old=%u new=%u\n",w,bpp,pad,sf,allow_gpu,internal,i,expected[i],actual[i]);break;}abort();}
 for(size_t i=span;i<span+32;i++)assert(source[i]==0xAB);
 assert(glGetError()==GL_NO_ERROR);glDeleteTextures(1,&target);glDeleteTextures(1,&sentinel);
 free(source);free(linear);free(actual);free(expected);cases++;
}
int main(void)
{
#ifdef _WIN32
 libretro_gl_set_standalone_mode();GloContext *context=glo_context_create();assert(context);glo_set_current(context);
#else
 EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE},n;EGLConfig config;assert(eglChooseConfig(display,attrs,&config,1,&n)&&n);
 EGLContext context=eglCreateContext(display,config,EGL_NO_CONTEXT,NULL);assert(context!=EGL_NO_CONTEXT);assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,context));
#endif
 printf("Pitched uploads: %s\n",glGetString(GL_RENDERER));
 glPixelStorei(GL_PACK_ALIGNMENT,1);
 PGRAPHGLState renderer={0};glGenFramebuffers(1,&renderer.gl_fb_util);NV2AState d={.pgraph.gl_renderer_state=&renderer};
 struct {unsigned bpp;GLenum internal,format,type,attachment;} formats[]={
 {1,GL_R8,GL_RED,GL_UNSIGNED_BYTE,GL_COLOR_ATTACHMENT0},
 {2,GL_RGB565,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,GL_COLOR_ATTACHMENT0},
 {3,GL_RGB8,GL_RGB,GL_UNSIGNED_BYTE,GL_COLOR_ATTACHMENT0},
 {4,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,GL_COLOR_ATTACHMENT0},
 {8,GL_RGBA16UI,GL_RGBA_INTEGER,GL_UNSIGNED_SHORT,GL_COLOR_ATTACHMENT0},
 {2,GL_DEPTH_COMPONENT16,GL_DEPTH_COMPONENT,GL_UNSIGNED_SHORT,GL_DEPTH_ATTACHMENT},
 {4,GL_DEPTH24_STENCIL8,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,GL_DEPTH_STENCIL_ATTACHMENT}};
 unsigned widths[]={1,3,13,64},pads[]={0,1,2,3,4,8,16,32};
 for(unsigned f=0;f<7;f++)for(unsigned w=0;w<4;w++)for(unsigned pad=0;pad<8;pad++)for(unsigned sf=1;sf<=3;sf++)for(unsigned mode=0;mode<2;mode++){
  run_case(&d,widths[w],5,formats[f].bpp,pads[pad],sf,formats[f].internal,formats[f].format,formats[f].type,formats[f].attachment,mode,false);
 }
 for(unsigned sf=1;sf<=3;sf++)for(unsigned mode=0;mode<2;mode++)run_case(&d,16,8,4,0,sf,GL_RGBA8,GL_RGBA,GL_UNSIGNED_BYTE,GL_COLOR_ATTACHMENT0,mode,true);
 printf("%u driver cases byte-exact; direct=%u GPU-grow=%u CPU-expand=%u rare-byte-pitch-pack=%u; unpack state and guards PASS\n",cases,direct,gpu,expanded,packed);
 free(renderer.scale_scratch.data);free(renderer.surf_scratch.data);
 GLuint textures[]={renderer.ul_grow_tex,renderer.ul_zgrow_tex};glDeleteTextures(2,textures);GLuint fbos[]={renderer.gl_fb_util,renderer.ul_grow_fbo,renderer.ul_zgrow_fbo};glDeleteFramebuffers(3,fbos);
#ifdef _WIN32
 glo_context_destroy(context);
#else
 eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,context);eglTerminate(display);
#endif
 return 0;
}
