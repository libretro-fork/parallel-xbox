/* SPDX-License-Identifier: GPL-2.0-or-later */
#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <GL/gl.h>
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hw/xbox/nv2a/pgraph/gl/texture-recycle.h"
/* BINDING */
typedef struct { bool cubemap; unsigned dimensionality,color_format,levels,width,height,depth; bool border; unsigned min_mipmap_level,max_mipmap_level,pitch; } TextureShape;
typedef struct { unsigned bytes_per_pixel; bool linear; GLint gl_internal_format; GLenum gl_format,gl_type; GLint gl_swizzle_mask[4]; } ColorFormatInfo;
#define NV097_SET_TEXTURE_FORMAT_COLOR_LU_IMAGE_A8R8G8B8 0
static const ColorFormatInfo kelvin_color_format_gl_map[]={ {4,true,GL_RGBA8,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,{0}}, {4,false,GL_RGBA8,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,{0}}, {2,true,GL_RGB565,GL_RGB,GL_UNSIGNED_SHORT_5_6_5,{0}}, {4,true,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT,GL_FLOAT,{0}} };
typedef struct { SampledTexturePool sampled_texture_pool; int texture_pool; GLuint fresh_tex[128];unsigned fresh_tex_r,fresh_tex_w; } PGRAPHGLState;
typedef struct { PGRAPHGLState *gl_renderer_state; unsigned surface_scale_factor; } PGRAPHState;
typedef struct { PGRAPHState pgraph; } NV2AState;
static void pgraph_apply_scaling_factor(PGRAPHState *pg,unsigned *w,unsigned *h) { *w*=pg->surface_scale_factor;*h*=pg->surface_scale_factor; }
static NV2AState device,*g_nv2a=&device;
#define NV2A_GL_DLABEL(...)
#define NV2A_GL_DPRINTF(...)
#define NV2A_PROF_TEX_UPLOAD 0
static void nv2a_profile_inc_counter(int n) { (void)n; }
static TextureBinding *obj_pool_take(void *p) { (void)p; return calloc(1,sizeof(TextureBinding)); }
static size_t pgraph_get_cubemap_face_length(void *p,TextureShape s) { assert(0);return 0; }
static unsigned full_uploads;
static void upload_gl_texture(GLenum target,TextureShape s,const uint8_t *data,const uint8_t *palette) {
 assert(target==GL_TEXTURE_2D);full_uploads++;
 glPixelStorei(GL_UNPACK_ROW_LENGTH,s.pitch/4);
 glTexImage2D(target,0,GL_RGBA8,s.width,s.height,0,GL_BGRA,GL_UNSIGNED_INT_8_8_8_8_REV,data);
 glPixelStorei(GL_UNPACK_ROW_LENGTH,0);
}
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
static unsigned touches;
static void test_touch(GLenum mode,GLint first,GLsizei count) { assert(mode==GL_POINTS&&first==0&&count==1);touches++; }
typedef void *gpointer;
static void obj_pool_give(void *p,void *o) { free(o); }
static unsigned snapshot_allocations;
static void observed_snapshot_alloc(GLenum target,GLint level,GLint fmt,GLsizei w,GLsizei h,GLint border,GLenum format,GLenum type,const void *pixels) { snapshot_allocations++;glTexImage2D(target,level,fmt,w,h,border,format,type,pixels); }
/* PRODUCTION */
static GLuint shader(GLenum type,const char *text) {
 GLuint s=glCreateShader(type);glShaderSource(s,1,&text,NULL);glCompileShader(s);
 GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);assert(ok);return s;
}
static GLuint program(void) {
 const char *v="#version 330\nout vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0,1);}";
 const char *f="#version 330\nin vec2 uv;uniform sampler2D image;out vec4 color;void main(){color=texture(image,uv);}";
 GLuint vs=shader(GL_VERTEX_SHADER,v),fs=shader(GL_FRAGMENT_SHADER,f),p=glCreateProgram();
 glAttachShader(p,vs);glAttachShader(p,fs);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);assert(ok);glDeleteShader(vs);glDeleteShader(fs);return p;
}
static void apply(void) {
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
 glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
 glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_LOD_BIAS,0);
}
static void pixels(GLuint texture,unsigned w,unsigned h,uint8_t *out) {
 GLuint target,fbo;glGenTextures(1,&target);glBindTexture(GL_TEXTURE_2D,target);
 glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
 glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
 glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target,0);
 assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
 glBindTexture(GL_TEXTURE_2D,texture);apply();glViewport(0,0,w,h);glDrawArrays(GL_TRIANGLES,0,3);
 glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,out);
 glBindFramebuffer(GL_FRAMEBUFFER,0);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&target);
}
int main(void) {
 EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);assert(eglInitialize(display,NULL,NULL));assert(eglBindAPI(EGL_OPENGL_API));
 EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_NONE};EGLConfig cfg;EGLint count;
 assert(eglChooseConfig(display,attrs,&cfg,1,&count)&&count);EGLContext ctx=eglCreateContext(display,cfg,EGL_NO_CONTEXT,NULL);assert(ctx!=EGL_NO_CONTEXT);assert(eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx));
 printf("GL recycle oracle: %s\n",glGetString(GL_RENDERER));
 GLuint p=program(),vao;glUseProgram(p);glGenVertexArrays(1,&vao);glBindVertexArray(vao);
 PGRAPHGLState r={0};device.pgraph.gl_renderer_state=&r;device.pgraph.surface_scale_factor=1;
 uint8_t data[4096],actual[4096],reference[4096];unsigned reused=0;
 for(unsigned pass=0;pass<256;pass++) {
  unsigned w=1+pass%15,h=1+(pass*7)%13,pitch=(w+3)*4;
  TextureShape s={.dimensionality=2,.color_format=0,.levels=1,.width=w,.height=h,.depth=1,.pitch=pitch};
  for(unsigned i=0;i<sizeof(data);i++)data[i]=(i*19+pass*37)&255;
  TextureBinding *old=generate_texture(s,data,NULL,false,false,false);GLuint name=old->gl_texture;assert(sampled_texture_key_load(old->recycle_key));
  uint32_t key=sampled_texture_key_load(old->recycle_key);old->border_color_set=true;assert(sampled_texture_key_load(old->recycle_key)==key);
  sampled_texture_key_store(old->recycle_key,key);assert(old->border_color_set);
  const GLint swizzle[]={GL_ALPHA,GL_BLUE,GL_GREEN,GL_RED};glTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_RGBA,swizzle);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,3);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,3);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MIN_LOD,8);glTexParameterf(GL_TEXTURE_2D,GL_TEXTURE_MAX_LOD,8);
  old->refcnt++;
  texture_binding_destroy(old);assert(!r.sampled_texture_pool.count);
  texture_binding_destroy(old);assert(r.sampled_texture_pool.count==1);
  assert(sampled_texture_contains(&r.sampled_texture_pool,name));
  /* Even an otherwise matching name cannot be overwritten while pending. */
  assert(!sampled_texture_take(&r.sampled_texture_pool,key));
  uint64_t serial=sampled_texture_seal(&r.sampled_texture_pool);
  assert(!sampled_texture_take(&r.sampled_texture_pool,key));
  /* The test driver completes earlier work; production reuses an existing
   * flip readiness observation instead of adding this test-only glFinish. */
  glFinish();sampled_texture_complete(&r.sampled_texture_pool,serial);
  for(unsigned i=0;i<sizeof(data);i++)data[i]^=0xa7;
  unsigned before=full_uploads;TextureBinding *next=generate_texture(s,data,NULL,false,false,false);
  assert(next->gl_texture==name&&full_uploads==before);reused++;
  assert(!sampled_texture_contains(&r.sampled_texture_pool,name));
  GLint value;glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,&value);assert(value==0);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,&value);assert(value==1000);
  GLfloat lod;glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MIN_LOD,&lod);assert(lod==-1000);glGetTexParameterfv(GL_TEXTURE_2D,GL_TEXTURE_MAX_LOD,&lod);assert(lod==1000);
  pixels(name,w,h,actual);
  TextureBinding *fresh=generate_texture(s,data,NULL,false,false,false);pixels(fresh->gl_texture,w,h,reference);
  assert(!memcmp(actual,reference,w*h*4));
  for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<4;c++)assert(actual[(y*w+x)*4+c]==data[y*pitch+x*4+(c==0?2:c==2?0:c)]);
  glDeleteTextures(1,&next->gl_texture);glDeleteTextures(1,&fresh->gl_texture);free(next);free(fresh);
  assert(!r.sampled_texture_pool.count&&!r.sampled_texture_pool.bytes);assert(glGetError()==GL_NO_ERROR);
 }
 /* Surface snapshots allocate only when their converter runs. A poison
  * input must never reach the guest upload helper, including cube faces. */
 for(unsigned cube=0;cube<2;cube++) {
  TextureShape s={.cubemap=cube,.color_format=cube,.dimensionality=2,.levels=1,.width=4,.height=4,.depth=1,.pitch=16};
  unsigned before=full_uploads;
  TextureBinding *snapshot=generate_texture(s,NULL,NULL,false,false,true);
  assert(full_uploads==before);assert((bool)sampled_texture_key_load(snapshot->recycle_key)==!cube);
  assert(snapshot->draw_time==-1&&!snapshot->byte_reinterpret);
  GLenum face=cube?GL_TEXTURE_CUBE_MAP_POSITIVE_X:GL_TEXTURE_2D;
  GLint width=-1;glGetTexLevelParameteriv(face,0,GL_TEXTURE_WIDTH,&width);assert(width==0);
  /* A complete converter overwrite, followed by actual sampling. */
  for(unsigned f=0;f<(cube?6u:1u);f++)
   glTexImage2D(face+f,0,GL_RGBA8,4,4,0,GL_RGBA,GL_UNSIGNED_BYTE,data);
  if(!cube) { pixels(snapshot->gl_texture,4,4,actual);assert(!memcmp(actual,data,64)); }
  assert(glGetError()==GL_NO_ERROR);texture_binding_destroy(snapshot);
 }
 sampled_texture_finalize(&r.sampled_texture_pool);
 /* Reuse a complete GPU-written snapshot only after its existing fence.
  * Exchange it with a RAM texture too: both directions share exact storage. */
 for(unsigned pass=0;pass<64;pass++) {
  unsigned scale=1u<<(pass%4),w=4*scale,h=4*scale;
  device.pgraph.surface_scale_factor=scale;
  TextureShape s={.dimensionality=2,.levels=1,.width=4,.height=4,.depth=1,.pitch=16};
  unsigned before=full_uploads,alloc=snapshot_allocations;
  TextureBinding *first=generate_texture(s,NULL,NULL,false,false,true);
  assert(full_uploads==before&&first->draw_time==-1);
  snapshot_allocate(first,&s);assert(snapshot_allocations==alloc+1);
  /* Model a cache node transitioning from native RAM storage. */
  sampled_texture_key_store(first->recycle_key,sampled_texture_key(s.width,s.height));
  update_snapshot_key(first,s,false);assert(sampled_texture_key_load(first->recycle_key)==sampled_texture_key(w,h));
  GLuint source,fbo;glGenTextures(1,&source);glBindTexture(GL_TEXTURE_2D,source);
  for(unsigned i=0;i<sizeof(data);i++)data[i]=(i*73+pass*19)&255;
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,data);apply();
  glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,first->gl_texture,0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,w,h);glDrawArrays(GL_TRIANGLES,0,3);glBindFramebuffer(GL_FRAMEBUFFER,0);
  GLuint name=first->gl_texture;texture_binding_destroy(first);
  assert(!sampled_texture_take(&r.sampled_texture_pool,sampled_texture_key(w,h)));
  uint64_t serial=sampled_texture_seal(&r.sampled_texture_pool);
  assert(!sampled_texture_take(&r.sampled_texture_pool,sampled_texture_key(w,h)));
  glFinish();sampled_texture_complete(&r.sampled_texture_pool,serial);
  TextureBinding *next=generate_texture(s,NULL,NULL,false,false,true);
  assert(next->gl_texture==name&&full_uploads==before);snapshot_allocate(next,&s);assert(snapshot_allocations==alloc+1);
  /* Before overwriting, verify the retired producer's pixels survived. */
  pixels(name,w,h,actual);assert(!memcmp(actual,data,w*h*4));
  glBindTexture(GL_TEXTURE_2D,name);const GLint swizzle[]={GL_ALPHA,GL_BLUE,GL_GREEN,GL_RED};glTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_SWIZZLE_RGBA,swizzle);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_BASE_LEVEL,3);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,3);
  texture_binding_destroy(next);serial=sampled_texture_seal(&r.sampled_texture_pool);glFinish();sampled_texture_complete(&r.sampled_texture_pool,serial);
  TextureShape ram=s;ram.width=w;ram.height=h;ram.pitch=w*4;
  TextureBinding *ordinary=generate_texture(ram,data,NULL,false,false,false);assert(ordinary->gl_texture==name&&full_uploads==before);pixels(name,w,h,actual);
  for(unsigned i=0;i<w*h*4;i++)assert(actual[i]==data[(i&~3u)+((i%4)==0?2:(i%4)==2?0:i%4)]);
  texture_binding_destroy(ordinary);serial=sampled_texture_seal(&r.sampled_texture_pool);glFinish();sampled_texture_complete(&r.sampled_texture_pool,serial);
  TextureBinding *back=generate_texture(s,NULL,NULL,false,false,true);assert(back->gl_texture==name);snapshot_allocate(back,&s);assert(snapshot_allocations==alloc+1);
  update_snapshot_key(back,s,true);assert(!sampled_texture_key_load(back->recycle_key));
  texture_binding_destroy(back);glDeleteTextures(1,&source);glDeleteFramebuffers(1,&fbo);assert(!r.sampled_texture_pool.count&&glGetError()==GL_NO_ERROR);
 }
 device.pgraph.surface_scale_factor=1;
 /* Snapshot admission must not consume even a completed matching object. */
 {
  TextureShape shape={.dimensionality=2,.levels=1,.width=4,.height=4,.depth=1,.pitch=16};
  TextureBinding *ram=generate_texture(shape,data,NULL,false,false,false);GLuint kept=ram->gl_texture;texture_binding_destroy(ram);
  uint64_t serial=sampled_texture_seal(&r.sampled_texture_pool);glFinish();sampled_texture_complete(&r.sampled_texture_pool,serial);
  for(unsigned mode=0;mode<8;mode++) {
   TextureShape s=shape;
   if(mode==0)s.border=true;
   if(mode==1)s.levels=2;
   if(mode==2){s.cubemap=true;s.color_format=1;}
   if(mode==3)s.width=1025;
   if(mode==4){s.dimensionality=1;s.color_format=1;}
   if(mode==5)s.color_format=2;
   if(mode==6)s.color_format=3;
   if(mode==7)device.pgraph.surface_scale_factor=512;
   TextureBinding *excluded=generate_texture(s,NULL,NULL,false,false,true);
   assert(!sampled_texture_key_load(excluded->recycle_key)&&excluded->gl_texture!=kept);
   assert(r.sampled_texture_pool.count==1);texture_binding_destroy(excluded);
   device.pgraph.surface_scale_factor=1;
  }
  sampled_texture_finalize(&r.sampled_texture_pool);
 }
 /* Ineligible storage cannot consume a matching pooled object. */
 TextureShape shape={.dimensionality=2,.levels=1,.width=4,.height=4,.depth=1,.pitch=16};
 TextureBinding *eligible=generate_texture(shape,data,NULL,false,false,false);
 GLuint kept=eligible->gl_texture;texture_binding_destroy(eligible);
 for(unsigned mode=0;mode<3;mode++) {
  TextureShape s=shape;s.levels=mode==0?2:1;s.border=mode==1;
  TextureBinding *other=generate_texture(s,data,NULL,mode==2,false,false);
  assert(!sampled_texture_key_load(other->recycle_key)&&other->gl_texture!=kept);
  texture_binding_destroy(other);assert(r.sampled_texture_pool.count==1);
 }
 sampled_texture_finalize(&r.sampled_texture_pool);assert(!glIsTexture(kept));
 /* A stale warmup ticket must not extend a parked image's GPU lifetime. */
 eligible=generate_texture(shape,data,NULL,false,false,false);kept=eligible->gl_texture;
 texture_binding_destroy(eligible);
 TextureBinding *active=generate_texture(shape,data,NULL,false,false,false);
 r.fresh_tex[0]=kept;r.fresh_tex[1]=active->gl_texture;r.fresh_tex[2]=0;r.fresh_tex_w=3;
 touch_tickets(&r,1);assert(!touches&&r.fresh_tex_r==1);
 touch_tickets(&r,2);assert(touches==1&&r.fresh_tex_r==3);
 glDeleteTextures(1,&active->gl_texture);free(active);sampled_texture_finalize(&r.sampled_texture_pool);
 /* Independent count and byte pressure, exact-key misses and release-once. */
 assert(!sampled_texture_key(0,1)&&!sampled_texture_key(1025,1));
 for(unsigned w=1;w<=1024;w++)for(unsigned h=1;h<=1024;h++) {
  uint8_t encoded[3];uint32_t key=sampled_texture_key(w,h);
  sampled_texture_key_store(encoded,key);assert(sampled_texture_key_load(encoded)==key);
  assert(sampled_texture_bytes(key)==w*h*4);
 }
 for(unsigned cycle=0;cycle<32;cycle++) {
  for(unsigned i=0;i<17;i++){GLuint t;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);sampled_texture_put(&r.sampled_texture_pool,sampled_texture_key(1,1),t);assert((bool)glIsTexture(t)==(i<16));}
  assert(r.sampled_texture_pool.count==16&&r.sampled_texture_pool.bytes==64);
  assert(!sampled_texture_take(&r.sampled_texture_pool,sampled_texture_key(2,1)));sampled_texture_finalize(&r.sampled_texture_pool);
  for(unsigned i=0;i<2;i++){GLuint t;glGenTextures(1,&t);glBindTexture(GL_TEXTURE_2D,t);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,1024,1024,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);sampled_texture_put(&r.sampled_texture_pool,sampled_texture_key(1024,1024),t);assert((bool)glIsTexture(t)==(i==0));}
  assert(r.sampled_texture_pool.bytes==SAMPLED_TEXTURE_POOL_BYTES&&r.sampled_texture_pool.count==1);sampled_texture_finalize(&r.sampled_texture_pool);
  assert(!r.sampled_texture_pool.bytes&&!r.sampled_texture_pool.count);
 }
 /* Two outstanding fence labels: completing the older one must not make
  * a release made after it reusable. */
 GLuint pending;glGenTextures(1,&pending);glBindTexture(GL_TEXTURE_2D,pending);
 uint32_t key=sampled_texture_key(1,1);
 uint64_t older=sampled_texture_seal(&r.sampled_texture_pool);
 sampled_texture_put(&r.sampled_texture_pool,key,pending);
 uint64_t newer=sampled_texture_seal(&r.sampled_texture_pool);
 sampled_texture_complete(&r.sampled_texture_pool,older);
 assert(!sampled_texture_take(&r.sampled_texture_pool,key));
 sampled_texture_complete(&r.sampled_texture_pool,newer);
 assert(sampled_texture_take(&r.sampled_texture_pool,key)==pending);glDeleteTextures(1,&pending);
 r.sampled_texture_pool.submitted=UINT64_MAX;
 glGenTextures(1,&pending);glBindTexture(GL_TEXTURE_2D,pending);
 sampled_texture_put(&r.sampled_texture_pool,key,pending);assert(!glIsTexture(pending)&&!r.sampled_texture_pool.count);
 assert(sampled_texture_rollover(&r.sampled_texture_pool));
 assert(sampled_texture_seal(&r.sampled_texture_pool)==1&&!r.sampled_texture_pool.completed);
 sampled_texture_finalize(&r.sampled_texture_pool);
 assert(glGetError()==GL_NO_ERROR);printf("%u pitched fresh/reused pixel comparisons, state reset, count/byte limits and 32 teardown cycles PASS\n",reused);
 glDeleteProgram(p);glDeleteVertexArrays(1,&vao);eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);eglDestroyContext(display,ctx);eglTerminate(display);return 0;
}
