# SPDX-License-Identifier: LGPL-2.1-or-later
"""Execute the production geometry UI classifier and packed bounds on GPUs."""
import argparse,ast,json,math,os,re,struct,subprocess,tempfile,shutil
from pathlib import Path

vk_loader_root = Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser(description=__doc__);p.add_argument('checkout',type=Path);p.add_argument('--cc',default='cc');p.add_argument('--epoxy-root',type=Path);p.add_argument('--output',type=Path);p.add_argument('--vulkan',action='store_true');a=p.parse_args();r=a.checkout.resolve()
s=(r/'hw/xbox/nv2a/pgraph/glsl/geom.c').read_text();start=s.index('static void append_ui_prepare(');end=s.index('\nMString *pgraph_glsl_gen_geom(',start);production=s[start:end]
start=s.index('        ui_emit = ');end=s.index(';\n',start);pack=ast.literal_eval(s[start+len('        ui_emit = '):end])
env=dict(os.environ)
if Path(a.cc).is_absolute():env['PATH']=str(Path(a.cc).parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='ui-bounds-') as temp:
 out=a.output.resolve() if a.output else Path(temp);out.mkdir(parents=True,exist_ok=True)
 generator=out/'generate.c';generator.write_text('''#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef struct {char text[65536];size_t length;}MString;
static void mstring_append(MString *s,const char *text){size_t n=strlen(text);assert(s->length+n<sizeof(s->text));memcpy(s->text+s->length,text,n+1);s->length+=n;}
static void mstring_append_fmt(MString *s,const char *format,...){va_list args;va_start(args,format);int n=vsnprintf(s->text+s->length,sizeof(s->text)-s->length,format,args);va_end(args);assert(n>=0&&(size_t)n<sizeof(s->text)-s->length);s->length+=n;}
'''+production+'''
int main(int argc,char **argv){assert(argc==2);for(unsigned n=3;n<=4;n++)for(unsigned stage=0;stage<4;stage++){MString s={0};append_ui_prepare(&s,stage,n);char path[4096];snprintf(path,sizeof(path),"%s/helper-%u-%u.glsl",argv[1],n,stage);FILE*f=fopen(path,"wb");assert(f);fwrite(s.text,1,s.length,f);fclose(f);}return 0;}
''')
 exe=out/('generate.exe' if os.name=='nt' else 'generate');subprocess.run([a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',str(generator),'-o',str(exe)],check=True,env=env);subprocess.run([str(exe),str(out)],check=True,env=env)
 args=[];vkargs=[];total=0
 for n in (3,4):
  for stage in range(4):
   cases=[]
   for w,h in ((1,1),(16,8),(13,7)):
    for orientation in range(3):
     for q in (1,2):
      points=[[0,0,1234,1],[w,0,1234,1],[w,h,1234,1],[0,h,1234,1]]
      uv=[]
      for x,y,z,pw in points:
       x,y=x/w,y/h
       if orientation==1:x=1-x
       if orientation==2:x,y=y,x
       uv.append([(0.125+.75*x)*q,(.25+.5*y)*q,0,q])
      dims=(h,w) if orientation==2 else (w,h)
      expected=[(.125+.75*.5/dims[0])*q,(.25+.5*.5/dims[1])*q,(.875-.75*.5/dims[0])*q,(.75-.5*.5/dims[1])*q,1]
      # Permute the triangle's provoking order, and the quad strip's order.
      orders=((0,1,2),(2,0,1),(1,2,0)) if n==3 else ((0,1,2,3),(0,3,1,2))
      for order in orders:
       pp=[points[i] for i in order];tt=[uv[i] for i in order]
       if n==3:pp.append([0]*4);tt.append([0]*4)
       cases.append((sum(pp,[])+sum(tt,[]),expected))
       for ox,oy in ((.5,.5),(.25,-.25),(-.5,.5)):
        shifted=[v[:] for v in pp]
        for point in shifted[:n]:point[0]+=ox;point[1]+=oy
        ax=(math.ceil(ox-.5)+.5-ox)/w;ay=(math.ceil(oy-.5)+.5-oy)/h
        bx=(math.ceil(ox+w-.5)-.5-ox)/w;by=(math.ceil(oy+h-.5)-.5-oy)/h
        if orientation==1:ax,bx=1-ax,1-bx
        if orientation==2:ax,ay,bx,by=ay,ax,by,bx
        ua,va=(.125+.75*ax)*q,(.25+.5*ay)*q;ub,vb=(.125+.75*bx)*q,(.25+.5*by)*q
        cases.append((sum(shifted,[])+sum(tt,[]),[min(ua,ub),min(va,vb),max(ua,ub),max(va,vb),1]))
   for kind in range(12):
    points=[[0,0,1234,1],[16,0,1234,1],[16,8,1234,1],[0,8,1234,1]];uv=[[0,0,0,1],[1,0,0,1],[1,1,0,1],[0,1,0,1]]
    if kind==0:points[1][1]=.25
    elif kind==1:points[1][2]=1235
    elif kind==2:points[1][3]=2
    elif kind==3:uv[1][3]=2
    elif kind==4:points[0][0]=float('inf')
    elif kind==5:uv[0][0]=float('nan')
    elif kind==6:
     for t in uv:t[1]+=.1*t[0]
    elif kind==7:
     for p0 in points:p0[0]*=.01
    elif kind==8:points[1]=points[0][:];points[2]=points[0][:]
    elif kind==9:
     for t in uv:t[3]=0
    elif kind==10:
     for i,p0 in enumerate(points):p0[0]=-3e38 if i in (0,3) else 3e38
    elif kind==11:
     for i,t in enumerate(uv):t[0]=-3e38 if i in (0,3) else 3e38
    cases.append((sum(points,[])+sum(uv,[]),[0,0,0,0,0]))
   if n==4:
    points=[[0,0,1234,1],[16,0,1234,1],[16,8,1234,1],[0,8,1234,1]];uv=[[0,0,0,1],[1,0,0,1],[1.1,1,0,1],[0,1,0,1]];cases.append((sum(points,[])+sum(uv,[]),[0,0,0,0,0]))
   helper=(out/f'helper-{n}-{stage}.glsl').read_text().replace('gl_in.length()',str(n))
   macros=f'#define v_vtxPos positions\n#define v_vtxT{stage} tex\n'
   uniforms='uniform vec4 positions[4],tex[4];\n'
   body=helper+'\nvoid main(){vec4 vtxPos1=vec4(0),vtxPos2=vec4(0);float triMZ=0.0;ui_prepare();\n'+pack+'\nbounds=vec4(vtxPos1.xy,vtxPos2.xy);valid=floatBitsToUint(triMZ)==0x80000000u?1.0:0.0;'
   vertex=out/f'ui-{n}-{stage}.vert';vertex.write_text('#version 450\n'+macros+uniforms+'out vec4 bounds;out float valid;\n'+body+'gl_Position=vec4(0,0,0,1);}\n')
   data=vertex.with_suffix('.bin')
   with data.open('wb') as f:
    for inputs,expected in cases:f.write(struct.pack('37f',*inputs,*expected))
   total+=len(cases);args.extend([str(vertex),str(data)])
   comp=vertex.with_suffix('.comp');comp.write_text('#version 450\n'+macros+'layout(set=0,binding=0,std140) uniform Inputs{vec4 positions[4];vec4 tex[4];};\nlayout(set=0,binding=1,std430) buffer Outputs{vec4 bounds;float valid;};\nlayout(local_size_x=1) in;\n'+body+'}\n');vkargs.extend([str(comp),str(data)])
 prefix=(vk_loader_root/'libretro/tests/test-point-fog-gl.c').read_text();prefix=prefix[:prefix.index('    printf("Point/fog GL GPU:')]
 driver=r'''printf("UI geometry GPU: %s\n",glGetString(GL_RENDERER));GLuint vao,b;glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&b);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,b);glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,20,NULL,GL_STREAM_READ);glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,b);glEnable(GL_RASTERIZER_DISCARD);unsigned count=0;
 for(int file=1;file<argc;file+=2){FILE*f=fopen(argv[file],"rb");assert(f);fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char*s=calloc(n+1,1);assert(s&&fread(s,1,n,f)==(size_t)n);fclose(f);GLuint v=compile(GL_VERTEX_SHADER,s),p=glCreateProgram();free(s);glAttachShader(p,v);const char*names[]={"bounds","valid"};glTransformFeedbackVaryings(p,2,names,GL_INTERLEAVED_ATTRIBS);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);assert(ok);glUseProgram(p);GLint pos=glGetUniformLocation(p,"positions"),tex=glGetUniformLocation(p,"tex");assert(pos>=0&&tex>=0);f=fopen(argv[file+1],"rb");assert(f);float input[37];while(fread(input,sizeof(input),1,f)==1){glUniform4fv(pos,4,input);glUniform4fv(tex,4,input+16);glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,1);glEndTransformFeedback();float actual[5];glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,20,actual);assert(glGetError()==GL_NO_ERROR);if(actual[4]!=input[36]){fprintf(stderr,"%s case %u valid %.9g != %.9g\n",argv[file],count,actual[4],input[36]);return 1;}if(actual[4])for(unsigned k=0;k<4;k++)if(fabsf(actual[k]-input[32+k])>5e-5f*fmaxf(1,fabsf(input[32+k]))){fprintf(stderr,"%s case %u bounds %u %.9g != %.9g\n",argv[file],count,k,actual[k],input[32+k]);return 1;}count++;}fclose(f);glDeleteProgram(p);glDeleteShader(v);}
 glDeleteBuffers(1,&b);glDeleteVertexArrays(1,&vao);printf("PASS: %u production geometry classifications, packed bounds and marker probes\n",count);return 0;}
'''
 test=out/'gl.c';test.write_text(prefix+driver);exe=out/('ui-gl.exe' if os.name=='nt' else 'ui-gl');cmd=[a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',str(test),'-o',str(exe)]
 if os.name=='nt':
  epoxy=a.epoxy_root.resolve();cmd+=['-I'+str(epoxy/'libepoxy/include'),str(epoxy/'libepoxy.a'),'-lopengl32','-lgdi32','-static']
 else:cmd+=['-lEGL','-lGL','-lm']
 subprocess.run(cmd,check=True,env=env);subprocess.run([str(exe),*args],check=True,env=dict(env,EGL_PLATFORM='surfaceless'))
 negative=out/'unmarked.vert';negative.write_text(Path(args[0]).read_text().replace('triMZ = uintBitsToFloat(0x80000000u)','triMZ = 0.0'));result=subprocess.run([str(exe),str(negative),args[1]],capture_output=True,text=True,env=dict(env,EGL_PLATFORM='surfaceless'));assert result.returncode==1 and 'valid' in result.stderr;print('PASS: missing metadata marker fails the independent oracle')
 if a.vulkan:
  helper=(vk_loader_root/'libretro/tests/spv-clear-vulkan.h').read_text().split('static VkPipeline pipeline(')[0]
  driver=(vk_loader_root/'libretro/tests/test-point-fog-vk.c').read_text().replace('/* GPU HELPERS */',helper)
  driver=driver.replace('make_buffer(64,','make_buffer(128,').replace('make_buffer(16,','make_buffer(32,').replace('input.buffer,0,64','input.buffer,0,128').replace('output.buffer,0,16','output.buffer,0,32').replace('float values[20]','float values[37]').replace('input.mapped,values,64','input.mapped,values,128')
  driver=driver.replace('production point/fog expressions via Vulkan SPIR-V','production UI geometry metadata via Vulkan SPIR-V')
  driver=driver.replace('for(unsigned k=0;k<4;k++)if(', 'if(actual[4]!=values[36]){fprintf(stderr,"valid mismatch %.9g != %.9g\\n",actual[4],values[36]);return 1;}\n            if(actual[4])for(unsigned k=0;k<4;k++)if(').replace('values[16+k]','values[32+k]')
  test=out/'vk.c';test.write_text(driver);exe=out/('ui-vk.exe' if os.name=='nt' else 'ui-vk');cmd=[a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-Wno-sign-compare','-Wno-missing-field-initializers','-I'+str(r),'-I'+str(vk_loader_root/'libretro'),'-I'+str(r/'deps/vulkan-headers'),'-I'+str(r/'deps/spirv-headers/include'),str(test),str(r/'hw/xbox/nv2a/pgraph/spv/glslc.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'),str(vk_loader_root/'libretro/vk_dispatch.c'),str(vk_loader_root/'libretro/tests/vk-test-loader.c'),'-lm','-o',str(exe)]
  if os.name!='nt':cmd+=['-ldl']
  subprocess.run(cmd,check=True,env=env);subprocess.run([str(exe),*vkargs],check=True,env=env)
  validator=shutil.which('spirv-val',path=env['PATH'])
  if validator:
   for f in out.glob('*.spv'):subprocess.run([validator,str(f)],check=True,env=env)
