# SPDX-License-Identifier: LGPL-2.1-or-later
"""Run the production GL depth-copy recording branch and shader on a real GPU."""
import argparse
import ast
import os
from pathlib import Path
import re
import subprocess
import tempfile

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("checkout", type=Path)
p.add_argument("--cc", default="cc")
p.add_argument("--epoxy-root", type=Path)
a = p.parse_args()
repo = a.checkout.resolve()
source = (repo / "hw/xbox/nv2a/pgraph/gl/surface.c").read_text(encoding="utf-8")
start = source.index("    if (!surface->color && (f->gl_format == GL_DEPTH_STENCIL")
end = source.index("    unsigned int width = texture_shape->width", start)
branch = source[start:end]
shader = source[source.index("    const char *fs_depth ="):source.index("    /* The inverse of fs_pack")]
shader = "".join(ast.literal_eval(x) for x in re.findall(r'"(?:[^"\\]|\\.)*"', shader))
gl = repo / "hw/xbox/nv2a/pgraph/gl"
for file in ("draw.c", "texture.c", "surface.c"):
    assert "nv2a_get_high_resolution_depth_textures" not in (gl / file).read_text(encoding="utf-8")
renderer = (gl / "renderer.c").read_text(encoding="utf-8")
start = renderer.index("    pgraph_gl_renderer.ops.draw_begin =")
selection = renderer[start:renderer.index(";", start) + 1]
texture = (gl / "texture.c").read_text(encoding="utf-8")
start = texture.index("            binding->scale =", texture.index("/* Byte aliases always"))
scale_update = texture[start:texture.index(";", start) + 1]
fixture = (repo / "libretro/tests/test-point-fog-gl.c").read_text()
prefix = fixture[:fixture.index("int main(")]
context = fixture[fixture.index("#ifdef _WIN32", fixture.index("int main(")):fixture.index('    printf("Point/fog GL GPU:')]
code = prefix + r'''
typedef struct {GLuint gl_target,gl_texture;unsigned s2t_width,s2t_height,scale;GLint s2t_format;} TextureBinding;
typedef struct {unsigned width,height;} TextureShape;
typedef struct {bool color;GLuint gl_buffer;} SurfaceBinding;
typedef struct {struct {GLuint fbo,vao,depth_prog,depth_tex_loc,depth_scale_loc;} s2t_rndr;} PGRAPHGLState;
typedef struct {unsigned surface_scale_factor;PGRAPHGLState *gl_renderer_state;} PGRAPHState;
typedef struct {PGRAPHState pgraph;} NV2AState;
static bool option;static unsigned option_reads;
static bool nv2a_get_high_resolution_depth_textures(void){option_reads++;return option;}
static void pgraph_gl_draw_begin(NV2AState *d){(void)d;}
static void pgraph_gl_draw_begin_highres_depth(NV2AState *d){(void)d;}
static struct {struct {void (*draw_begin)(NV2AState *);} ops;} pgraph_gl_renderer;
static void select_policy(bool value){option=value;SELECTION
 assert(pgraph_gl_renderer.ops.draw_begin==(value?pgraph_gl_draw_begin_highres_depth:pgraph_gl_draw_begin));}
static void update_scale(PGRAPHState *pg,TextureBinding *binding,bool highres_depth,bool surf_to_tex_bytes){SCALE_UPDATE}
static bool pgraph_gl_own_draw_suspend(NV2AState *d){(void)d;return false;}
static void s2t_pass_end(NV2AState *d,bool b){(void)d;(void)b;}
static void fb_bind(PGRAPHGLState *r,GLuint fbo){(void)r;glBindFramebuffer(GL_FRAMEBUFFER,fbo);}
static void tex_sample_raw(SurfaceBinding *s){(void)s;glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);}
static void s2t_pass_state(PGRAPHGLState *r){(void)r;glDisable(GL_SCISSOR_TEST);glDisable(GL_BLEND);glDisable(GL_CULL_FACE);}
static void pgraph_apply_scaling_factor(PGRAPHState *pg,unsigned *w,unsigned *h){*w*=pg->surface_scale_factor;*h*=pg->surface_scale_factor;}
static unsigned allocations;
static void observed_tex_image(GLenum target,GLint level,GLint format,GLsizei w,GLsizei h,GLint border,GLenum layout,GLenum type,const void *data){allocations++;glTexImage2D(target,level,format,w,h,border,layout,type,data);}
#undef glTexImage2D
#define glTexImage2D observed_tex_image
static void copy(NV2AState *d,SurfaceBinding *surface,TextureBinding *texture,TextureShape *texture_shape,int texture_unit,bool highres_depth,GLenum format){
 PGRAPHState *pg=&d->pgraph;PGRAPHGLState *r=pg->gl_renderer_state;
 const struct {GLenum gl_format;} value={format},*f=&value;
 BRANCH
 assert(!"depth branch not selected");
}
int main(void){
 CONTEXT
 printf("GL depth GPU: %s\n",glGetString(GL_RENDERER));
 PGRAPHGLState r={0};NV2AState d={{1,&r}};
 select_policy(true);select_policy(false);select_policy(true);assert(option_reads==3);
 GLuint vs=compile(GL_VERTEX_SHADER,"#version 330\nvoid main(){vec2 p=vec2((gl_VertexID&1)*4-1,(gl_VertexID&2)*2-1);gl_Position=vec4(p,0,1);}");
 GLuint fs=compile(GL_FRAGMENT_SHADER,DEPTH_SHADER_SOURCE);
 r.s2t_rndr.depth_prog=glCreateProgram();glAttachShader(r.s2t_rndr.depth_prog,vs);glAttachShader(r.s2t_rndr.depth_prog,fs);glLinkProgram(r.s2t_rndr.depth_prog);
 GLint linked;glGetProgramiv(r.s2t_rndr.depth_prog,GL_LINK_STATUS,&linked);assert(linked);
 r.s2t_rndr.depth_tex_loc=glGetUniformLocation(r.s2t_rndr.depth_prog,"tex");r.s2t_rndr.depth_scale_loc=glGetUniformLocation(r.s2t_rndr.depth_prog,"uscale");
 glGenFramebuffers(1,&r.s2t_rndr.fbo);glGenVertexArrays(1,&r.s2t_rndr.vao);
 const unsigned extents[][2]={{1,1},{3,5},{7,9},{16,8},{31,17}},scales[]={1,2,3,4,8,10};
 const GLenum formats[]={GL_DEPTH_COMPONENT16,GL_DEPTH_COMPONENT24,GL_DEPTH_COMPONENT32F};unsigned cases=0;
 for(unsigned fmt=0;fmt<3;fmt++)for(unsigned size=0;size<5;size++)for(unsigned sc=0;sc<6;sc++){
  unsigned scale=scales[sc],w=extents[size][0],h=extents[size][1],sw=w*scale,sh=h*scale;d.pgraph.surface_scale_factor=scale;
  float *input=malloc((size_t)sw*sh*sizeof(float));for(unsigned y=0;y<sh;y++)for(unsigned x=0;x<sw;x++)input[y*sw+x]=(float)((x*17+y*31)%1000+1)/1024;
  SurfaceBinding surface={0};glActiveTexture(GL_TEXTURE0);glGenTextures(1,&surface.gl_buffer);glBindTexture(GL_TEXTURE_2D,surface.gl_buffer);glTexImage2D(GL_TEXTURE_2D,0,formats[fmt],sw,sh,0,GL_DEPTH_COMPONENT,GL_FLOAT,input);
  TextureBinding texture={.gl_target=GL_TEXTURE_2D};glGenTextures(1,&texture.gl_texture);TextureShape shape={w,h};
  for(unsigned mode=0;mode<2;mode++)for(unsigned repeat=0;repeat<2;repeat++){
   unsigned ow=mode?sw:w,oh=mode?sh:h;
   unsigned before=allocations;
   copy(&d,&surface,&texture,&shape,0,mode,fmt==1?GL_DEPTH_STENCIL:GL_DEPTH_COMPONENT);
   assert(allocations-before==(!repeat&&(!mode||scale!=1)?1u:0u));
   update_scale(&d.pgraph,&texture,mode,false);
   assert(texture.s2t_width==ow&&texture.s2t_height==oh&&texture.scale==(mode?scale:1));
   fb_bind(&r,r.s2t_rndr.fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,texture.gl_texture,0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
   float *output=malloc((size_t)ow*oh*sizeof(float));glReadPixels(0,0,ow,oh,GL_DEPTH_COMPONENT,GL_FLOAT,output);
   for(unsigned y=0;y<oh;y++)for(unsigned x=0;x<ow;x++){
    float expected=input[(mode?y:y*scale)*sw+(mode?x:x*scale)];float tolerance=fmt==0?2.0f/65535:2.0f/16777215;
    if(fabsf(output[y*ow+x]-expected)>tolerance){fprintf(stderr,"depth copy mismatch: mode=%u scale=%u xy=%u,%u %.9g != %.9g\n",mode,scale,x,y,output[y*ow+x],expected);return 1;}
   }
   free(output);cases++;glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_ATTACHMENT,GL_TEXTURE_2D,0,0);assert(glGetError()==GL_NO_ERROR);
  }
  glDeleteTextures(1,&texture.gl_texture);glDeleteTextures(1,&surface.gl_buffer);free(input);
 }
 assert(option_reads==3);
 for(unsigned sc=0;sc<6;sc++)for(unsigned mode=0;mode<2;mode++)for(unsigned bytes=0;bytes<2;bytes++)for(unsigned depth=0;depth<2;depth++){
  d.pgraph.surface_scale_factor=scales[sc];TextureBinding binding={.s2t_format=depth?GL_DEPTH_COMPONENT24:GL_RGBA8};
  update_scale(&d.pgraph,&binding,mode,bytes);assert(binding.scale==((bytes||(!mode&&depth))?1:scales[sc]));
 }
 printf("PASS: %u production depth copies, formats, scales, exact native sub-samples, full-resolution detail and allocation reuse; startup dispatch and 48 metadata cases\n",cases);return 0;
}
'''.replace("SELECTION", selection).replace("SCALE_UPDATE", scale_update).replace("BRANCH", branch).replace("CONTEXT", context).replace("DEPTH_SHADER_SOURCE", '"' + shader.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n") + '"')
env = dict(os.environ, EGL_PLATFORM="surfaceless")
with tempfile.TemporaryDirectory(prefix="gl-highres-depth-") as directory:
    folder = Path(directory)
    src, exe = folder / "test.c", folder / ("test.exe" if os.name == "nt" else "test")
    command = [a.cc, "-O2", "-std=gnu11", "-Wall", "-Wextra", "-Werror", str(src), "-o", str(exe)]
    if os.name == "nt":
        epoxy = a.epoxy_root.resolve()
        command += ["-I" + str(epoxy / "libepoxy/include"), str(epoxy / "libepoxy.a"), "-lopengl32", "-lgdi32", "-static"]
    else:
        command += ["-lEGL", "-lGL", "-lm"]
    src.write_text(code)
    subprocess.run(command, env=env, check=True)
    subprocess.run([str(exe)], env=env, check=True)
    negative = code.replace("highres_depth ? 1 : pg->surface_scale_factor", "pg->surface_scale_factor")
    assert negative != code
    src.write_text(negative)
    subprocess.run(command, env=env, check=True)
    result = subprocess.run([str(exe)], env=env, capture_output=True, text=True)
    assert result.returncode == 1 and "depth copy mismatch" in result.stderr, result.stderr
    print("PASS: retained shrinking coordinates fail the independent full-resolution oracle")
