# SPDX-License-Identifier: LGPL-2.1-or-later
"""Execute production point/fog GLSL snippets with a transform-feedback oracle.

The surrounding shader supplies controlled inputs; expressions come from the
runtime generators. No core instrumentation or runtime test branch is used.
"""
import argparse, ast, json, math, os, re, struct, subprocess, tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('checkout',type=Path)
p.add_argument('--cc',default='cc')
p.add_argument('--epoxy-root',type=Path)
p.add_argument('--output',type=Path)
p.add_argument('--vulkan',action='store_true',help='also execute the numerical expressions through the production SPIR-V compiler')
a=p.parse_args();r=a.checkout.resolve()
vs=(r/'hw/xbox/nv2a/pgraph/glsl/vsh.c').read_text()
ff=(r/'hw/xbox/nv2a/pgraph/glsl/vsh-ff.c').read_text()
def strings(text):
    return ''.join(ast.literal_eval(s) for s in re.findall(r'"(?:[^"\\]|\\.)*"',text))
def between(text,start,end):
    return text[text.index(start):text.index(end,text.index(start))]

point=between(vs,'        if (!state->point_params_enable) {','    if (state->fog_dynamic)')
register=strings(point[:point.index('} else {')])
program=strings(point[point.index('} else {'):])
fixed=strings(ff[ff.index('    if (state->point_params_enable) {'):ff.index('    } else {',ff.index('    if (state->point_params_enable) {'))])
dynamic=strings(between(vs,'    if (state->fog_dynamic) {','    } else if (!state->fog_enable)'))
linear=strings(between(vs,'mstring_append(body, "  float fogFactor = fogParam.x + fogDistance','            break;'))
exp=strings(between(vs,'mstring_append(body, "  float fogFactor = fogParam.x + exp2(fogDistance','            break;'))
exp2=strings(between(vs,'mstring_append(body, "  float fogFactor = fogParam.x + exp2(-fogDistance','            break;'))
# The first is the dynamic string block, so select the formatted static call.
static=vs[vs.index('        mstring_append_fmt(\n            body,\n            "  if (isinf(fogDistance))'):]
tail=strings(static[:static.index('            infinite_fogdistance_result, nan_fogfactor_result')])

header='''#version 330
#define FLOAT_MAX uintBitsToFloat(0x7F7FFFFFu)
uniform vec4 probe[4];
out vec4 result;
vec4 NaNToValue(vec4 v,float replacement) {
 return vec4(isnan(v.x)?replacement:v.x,isnan(v.y)?replacement:v.y,
             isnan(v.z)?replacement:v.z,isnan(v.w)?replacement:v.w);
}
void main(){
 vec4 oPts=vec4(probe[0].x),oFog=vec4(probe[0].x);
 float pointScale=probe[0].y,pointSizeReg=probe[0].x;
 vec2 fogParam=probe[0].yz;
 uint fogCtl=uint(probe[0].w);
 vec4 position=probe[1];mat4 modelViewMat0=mat4(1);
 float pointParams[8]=float[8](probe[2].x,probe[2].y,probe[2].z,probe[2].w,
                             0,0,probe[3].x,probe[3].y);
'''
env=dict(os.environ)
if Path(a.cc).is_absolute():env['PATH']=str(Path(a.cc).parent)+os.pathsep+env['PATH']
with tempfile.TemporaryDirectory(prefix='point-fog-') as temp:
    out=a.output.resolve() if a.output else Path(temp);out.mkdir(parents=True,exist_ok=True)
    args=[]
    def add(name,body,cases,output):
        shader=out/(name+'.vert');data=out/(name+'.bin')
        shader.write_text(header+body+'\nresult='+output+';gl_Position=vec4(0,0,0,1);}\n')
        with data.open('wb') as f:
            for uniform,expected in cases:f.write(struct.pack('20f',*uniform,*expected))
        args.extend([str(shader),str(data)])
    # TEXGEN_REF is a live bit in the existing texgenMode uniform. Exercise
    # both generator paths with alternating modes in one shader binding.
    eye=strings(between(ff,'    mstring_append(body,\n        "vec3 texgenEye',
                        '    if (state->fixed_function.texgen_dynamic)'))
    dynamic_texgen=strings(between(ff,'            "/* Dynamic texgen */',
                                  '        for (i = 0; i < NV2A_MAX_TEXTURES; i++)'))
    texgen_inputs=('vec4 tPosition=probe[0];vec3 tNormal=probe[1].xyz;'
                   'uint texgenMode[4]=uint[4](uint(probe[3].x),0u,0u,0u);'
                   'vec4 oT0=vec4(0);\n')
    for sphere in (False,True):
        name='SPHERE' if sphere else 'REFLECTION'
        begin=ff.index('            case TEXGEN_'+name+'_MAP:')
        end=ff.index('                break;',begin)
        component=strings(ff[begin:end])
        static=''.join(component.replace('%d','0').replace('%c',axis)
                       for axis in ('xy' if sphere else 'xyz'))
        cases=[]
        for position in ((1,0,1),(-1,2,3),(3,-2,4),(0,0,2)):
            for normal in ((0,1,0),(1,0,0),(0.6,0,0.8),(0.3,0.4,0)):
                for infinite in (False,True,False):
                    length=math.sqrt(sum(v*v for v in position))
                    u=(0,0,1) if infinite else tuple(v/length for v in position)
                    dot=sum(x*y for x,y in zip(u,normal))
                    reflected=[u[i]-2*dot*normal[i] for i in range(3)]
                    if sphere:
                        denominator=2*math.sqrt(sum(v*v for v in reflected[:2])+(reflected[2]+1)**2)
                        if denominator<1e-6:continue
                        expected=[reflected[i]/denominator+0.5 for i in range(2)]+[0,1]
                    else:expected=reflected+[1]
                    values=[*position,1,*normal,0,0,0,0,0,4096 if infinite else 0,0,0,0]
                    cases.append((values,expected))
        for dyn in (False,True):
            body=texgen_inputs+eye+(dynamic_texgen if dyn else static)
            output=('vec4(tgSph.xy,0,1)' if sphere else 'vec4(tgR,1)') if dyn else 'vec4(oT0.xyz,1)'
            add('texgen-'+name.lower()+('-dynamic' if dyn else '-static'),body,cases,output)
    for kind,body in [('register',register),('program',program),('fixed',fixed)]:
        cases=[]
        for scale in (1,2,4,8):
            for size in (0,0.25,1,2.5,8,16,63.875):
                for distance in (0,1,4,16):
                    values=[size,scale,0,0,0,0,distance,1,1,0.5,0.25,12,0.25,0.5,0,0]
                    if kind=='program':expected=size*scale
                    elif kind=='register':expected=(1 if size<=0 else size)*scale
                    else:
                        d=math.sqrt(distance*distance+1)
                        expected=min(max((1/math.sqrt(1+0.5*d+0.25*d*d)+0.25)*12+0.5,0.5),12.5)*scale
                    cases.append((values,[expected]*4))
        add('point-'+kind,body,cases,'vec4(oPts.x)')
    spec={0:1,1:1,3:0,4:1,5:0,7:0}
    for mode in spec:
        for dyn in (False,True):
            body=dynamic if dyn else 'float fogDistance=oFog.x;\n'+{0:linear,1:exp,3:exp2}[mode&3]+('fogFactor=abs(fogFactor);\n' if mode>=4 else '')+tail.replace('%f',str(float(spec[mode])))
            cases=[]
            # (1,1) linear is the direct vertex factor; other pairs cover
            # linear distance, exp and exp2 fog without assuming API enums.
            for x,y in ((1,1),(2,-0.25),(1.5,-0.0625),(0.5,0.125)):
                for distance in (-8,-1,0,0.25,0.5,1,4,16,float('inf'),float('-inf'),float('nan')):
                    values=[distance,x,y,mode*2+1]+[0]*12
                    if math.isinf(distance):expected=spec[mode]
                    elif math.isnan(distance):expected=spec[mode]
                    else:
                        f=mode&3
                        expected=x+distance*y-1 if f==0 else x+2**(distance*y*16)-1.5 if f==1 else x+2**(-distance*distance*y*y*32)-1.5
                        if mode>=4:expected=abs(expected)
                    cases.append((values,[expected]*4))
            if dyn:
                for distance in (0,0.5,1,float('inf'),float('nan')):
                    cases.append(([distance,1,1,mode*2]+[0]*12,[1]*4))
            add('fog-'+str(mode)+('-dynamic' if dyn else '-static'),body,cases,'oFog')
    exe=out/('point-fog.exe' if os.name=='nt' else 'point-fog')
        # Rasterize actual stage-coordinate setup: only stage 3 is replaced.
    ps=(r/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text()
    setup=strings(between(ps,'    mstring_append(vars, "vec4 pT0 =','    } else {'))
    fragment='#version 330\nin vec4 vtxT0,vtxT1,vtxT2;out vec4 color;void main(){'+setup+'color=vec4(pT3.xy,pT0.xy);}'
    sprite=r'''glDisable(GL_RASTERIZER_DISCARD);glEnable(GL_PROGRAM_POINT_SIZE);glEnable(GL_POINT_SPRITE);
    const char *vertex="#version 330\nuniform float size;out vec4 vtxT0,vtxT1,vtxT2;void main(){gl_Position=vec4(0,0,0,1);gl_PointSize=size;vtxT0=vec4(.125,.75,0,1);vtxT1=vec4(.25);vtxT2=vec4(.5);}";
    GLuint v=compile(GL_VERTEX_SHADER,vertex),f=compile(GL_FRAGMENT_SHADER,/* FRAGMENT */),p=glCreateProgram();
    glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);GLint linked;glGetProgramiv(p,GL_LINK_STATUS,&linked);assert(linked);glUseProgram(p);
    GLuint texture,fbo;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,48,48,0,GL_RGBA,GL_FLOAT,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);glViewport(0,0,48,48);
    for(int size=8;size<=32;size*=2){
      glClearColor(-1,-1,-1,-1);glClear(GL_COLOR_BUFFER_BIT);glUniform1f(glGetUniformLocation(p,"size"),size);glDrawArrays(GL_POINTS,0,1);
      float pixels[48*48*4];glReadPixels(0,0,48,48,GL_RGBA,GL_FLOAT,pixels);assert(glGetError()==GL_NO_ERROR);
      int low=24-size/2,high=24+size/2;
      for(int y=0;y<48;y++)for(int x=0;x<48;x++){
        float *c=&pixels[(y*48+x)*4];bool inside=x>=low&&x<high&&y>=low&&y<high;
        if(!inside){assert(c[0]==-1);continue;}
        assert(fabsf(c[0]-(x-low+.5f)/size)<1e-6f);
        assert(fabsf(c[1]-(1-(y-low+.5f)/size))<1e-6f);
        assert(c[2]==.125f&&c[3]==.75f);
      }
    }
    glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    puts("PASS: point sprite sizes 8/16/32, stage-3 orientation and unchanged stage-0 coordinates");'''
    fixture=out/'fixture.c';fixture.write_text((r/'libretro/tests/test-point-fog-gl.c').read_text().replace('/* SPRITE */',sprite.replace('/* FRAGMENT */',json.dumps(fragment))))
    command=[a.cc,'-O2' ,'-std=gnu11','-Wall','-Wextra','-Werror',str(fixture),'-o',str(exe)]
    if os.name=='nt':
        epoxy=a.epoxy_root.resolve();command+=['-I'+str(epoxy/'libepoxy/include'),str(epoxy/'libepoxy.a'),'-lopengl32','-lgdi32','-static']
    else:command+=['-lEGL','-lGL','-lm']
    subprocess.run(command,check=True,env=env)
    subprocess.run([str(exe),*args],check=True,env=dict(env,EGL_PLATFORM='surfaceless'))
    negative=out/'point-program-unscaled.vert'
    negative.write_text((out/'point-program.vert').read_text().replace('oPts.x *= pointScale;','/* pre-fix behavior */'))
    result=subprocess.run([str(exe),str(negative),str(out/'point-program.bin')],env=dict(env,EGL_PLATFORM='surfaceless'),capture_output=True,text=True)
    assert result.returncode==1 and ' != ' in result.stderr,result.stderr
    print('PASS: pre-fix unscaled programmable point size fails the independent oracle')
    for kind in ('reflection','sphere'):
        for path in ('static','dynamic'):
            name='texgen-'+kind+'-'+path
            negative=out/(name+'-local-only.vert')
            negative.write_text((out/(name+'.vert')).read_text().replace(
                '(texgenMode[0] & 4096u) != 0u','false'))
            result=subprocess.run([str(exe),str(negative),str(out/(name+'.bin'))],
                env=dict(env,EGL_PLATFORM='surfaceless'),capture_output=True,text=True)
            assert result.returncode==1 and ' != ' in result.stderr,result.stderr
    print('PASS: local-only reflection/sphere texgen fails both path oracles')
    if a.vulkan:
        vkargs=[]
        for path,data in zip(args[::2],args[1::2]):
            source=Path(path).read_text().replace('#version 330','#version 450')
            source=source.replace('uniform vec4 probe[4];','layout(set=0,binding=0,std140) uniform Inputs {vec4 probe[4];};')
            source=source.replace('out vec4 result;','layout(set=0,binding=1,std430) buffer Outputs {vec4 result;};\nlayout(local_size_x=1) in;')
            source=source.replace('gl_Position=vec4(0,0,0,1);','')
            source=re.sub(r' float pointParams\[8\]=float\[8\]\(.*?\);','',source,flags=re.S)
            for i,expr in enumerate(('probe[2].x','probe[2].y','probe[2].z','probe[2].w','0.0','0.0','probe[3].x','probe[3].y')):
                source=source.replace('pointParams['+str(i)+']',expr)
            comp=Path(path).with_suffix('.comp');comp.write_text(source);vkargs.extend([str(comp),data])
        helper=(r/'libretro/tests/spv-clear-vulkan.h').read_text().split('static VkPipeline pipeline(')[0]
        test=out/'fixture-vk.c';test.write_text((r/'libretro/tests/test-point-fog-vk.c').read_text().replace('/* GPU HELPERS */',helper))
        exe=out/('point-fog-vk.exe' if os.name=='nt' else 'point-fog-vk')
        command=[a.cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',
                 '-Wno-sign-compare','-Wno-missing-field-initializers','-I'+str(r),'-I'+str(r/'deps/volk'),
                 '-I'+str(r/'deps/vulkan-headers'),'-I'+str(r/'deps/spirv-headers/include'),str(test),
                 str(r/'hw/xbox/nv2a/pgraph/spv/glslc.c'),str(r/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'),str(r/'deps/volk/volk.c'),'-lm','-o',str(exe)]
        if os.name!='nt':command+=['-ldl']
        subprocess.run(command,check=True,env=env)
        subprocess.run([str(exe),*vkargs],check=True,env=env)
