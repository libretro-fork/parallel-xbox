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

helper_start=ff.index('        "float spotlightFactor(')
helper_end=ff.index('        "}\\n");',helper_start)+len('        "}\\n");')
spotlight_helper=strings(ff[helper_start:helper_end])
header=header.replace('void main(){',spotlight_helper+'void main(){')
base_start=ff.index('        "vec3 lightingBaseColor(')
base_end=ff.index('        "}\\n");',base_start)+len('        "}\\n");')
base_helper=strings(ff[base_start:base_end])
header=header.replace('void main(){',base_helper+'void main(){')
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
    # Numeric samples from the opaque diffuse-only halves of the hardware
    # Emissive_me{0,0_15,0_5,1_0} captures at x=220/352/484,y=110/210/310.
    # https://github.com/abaire/nxdk_pgraph_tests_golden_results/tree/main/results/Material_color_source
    pairs=[(1,1),(2,1),(1,2),(2,2),(0,2),(2,0),(0,1),(1,0),(0,0)] # emission,ambient
    hardware=[[(28,214,28),(105,138,28),(36,207,28),(112,130,28),(61,156,54),(76,178,76),(54,163,54),(76,178,76),(76,178,76)],
        [(32,229,32),(108,153,32),(51,210,32),(127,134,32),(76,159,57),(92,182,80),(57,178,57),(80,194,80),(76,178,76)],
        [(41,255,41),(117,189,41),(87,219,41),(163,143,41),(112,168,66),(127,191,89),(66,214,66),(89,229,89),(76,178,76)],
        [(54,255,54),(130,240,54),(138,232,54),(214,156,54),(163,181,79),(178,204,102),(79,255,79),(102,255,102),(76,178,76)]]
    colors={1:(.1,.4,.1),2:(.4,.1,.1)}
    globals_dynamic=strings(between(ff,'            "  uint dsrc =','            "  oD1 ='))
    ambient_dynamic=strings(between(ff,'            "  uint ambientSource =','    } else if (state->fixed_function.ambient_src'))
    static_start=ff.index('            "oD0 = vec4(lightingBaseColor(')
    globals_static=strings(ff[static_start:ff.index('            state->fixed_function.ambient_src',static_start)])
    dynamic_cases=[]
    for index,(emission,ambient) in enumerate(pairs):
        cases=[]
        for ki,k in enumerate((0,.15,.5,1)):
            # Independently validate the floating oracle against hardware bytes.
            if ambient==0:
                base=[.2+(colors[emission][c]*k if emission else 0) for c in range(3)]
            else:
                base=[(.2 if emission==0 else colors[emission][c])+colors[ambient][c]*k for c in range(3)]
            expected=[base[c]+.1*(1 if ambient==0 else colors[ambient][c])+(0,.4,0)[c] for c in range(3)]
            assert all(abs(min(255,int(v*255))-hw)<=1 for v,hw in zip(expected,hardware[ki][index]))
            values=[.2,.2,.2,.75,.1,.4,.1,.25,.4,.1,.1,.25,k,ambient,emission,0]
            cases.append((values,expected+[.75]))
        prefix=('uint csv0cCtl=(uint(probe[3].y)<<6u)|(uint(probe[3].z)<<4u);'
                'vec3 sceneAmbientColor=probe[0].xyz;vec4 materialEmissionColor=vec4(probe[3].x);'
                'vec4 diffuse=probe[1],specular=probe[2],oD0;float material_alpha=probe[0].a;\n')
        light='vec3 lightAmbient=vec3(.1);\n'
        static_global=globals_static.replace('%uu',str(ambient)+'u',1).replace('%uu',str(emission)+'u',1).replace('%s','material_alpha')
        if ambient:
            name='diffuse' if ambient==1 else 'specular'
            ambient_static=strings(next(line for line in ff.splitlines() if 'mstring_append(body, "  lightAmbient *= '+name+'.xyz;' in line))
        else:ambient_static=''
        add('material-static-'+str(index),prefix+static_global+light+ambient_static+'oD0.rgb+=lightAmbient+vec3(0,.4,0);',cases,'oD0')
        dynamic_cases.extend(cases)
    add('material-dynamic',prefix+globals_dynamic+light+ambient_dynamic+'oD0.rgb+=lightAmbient+vec3(0,.4,0);',dynamic_cases,'oD0')
    # Coefficient sets measured by nxdk_pgraph_tests' FoFixed hardware suite.
    # Include explicit zero/negative denominators and cone endpoints, rather
    # than comparing the rational hardware curve to a D3D pow approximation.
    coefficients=[(0,-0.494592,1.494592),(0,1,0),(-0.000244,0.500122,0.499634),
        (-0.170208,-0.855843,1.685635),(-0.706496,-2.507095,2.800600),
        (-0.932112,-3.097628,3.165516),(-0.986137,-3.165117,3.178980),
        (-0.993286,-2.953324,2.960038),(-0.996561,-3.084043,3.087482),
        (0,0,0),(0,0,-1),(0,-2,1),(-0.5,0,1)]
    calls=re.findall(r'"( +attenuation = spotlightAttenuation.*?;\\n)"',ff)
    assert len(calls)==2
    for path,call in zip(('dynamic','static'),calls):
        body=('#define lightSpotFalloff(i) probe[2].xyz\n'
              'vec4 spotDir=probe[0];vec3 VP=probe[1].xyz;int i=0;'
              'float attenuation=probe[3].x;\n')+ast.literal_eval('"'+call+'"').replace('%d','0')
        cases=[]
        for k in coefficients:
            for ramp in (-1,0,0.0001,0.125,0.25,0.5,0.75,0.999,1,2):
                # Match the uploaded float32 values before the double oracle.
                ramp,*rounded_k=struct.unpack('4f',struct.pack('4f',ramp,*k))
                if ramp<=0:factor=0
                elif ramp>=1:factor=1
                elif ramp+rounded_k[0]<=0:factor=0
                else:
                    denominator=rounded_k[1]*ramp+rounded_k[2]
                    factor=1 if denominator==0 else 0 if denominator<0 else min(1,max(0,(ramp+rounded_k[0])/denominator))
                for attenuation in (0.25,1,2):
                    values=[0,0,ramp,0,0,0,1,0,*rounded_k,0,attenuation,0,0,0]
                    cases.append((values,[attenuation*factor]*4))
        # Hard cones have huge cooked vectors. Squaring/renormalizing them
        # would overflow float32; direct dot+bias retains the cone cutoffs.
        for z,bias,factor in ((2e19,-1e19,1),(2e19,-2e19,0),(1e19,-2e19,0)):
            for attenuation in (0.25,1,2):
                values=[0,0,z,bias,0,0,1,0,0,-0.494592,1.494592,0,attenuation,0,0,0]
                cases.append((values,[attenuation*factor]*4))
        # Hardware cutoff masks force zero, even if reciprocal distance
        # attenuation is non-finite. Multiplying infinity by zero is NaN.
        for ramp,k in ((-1,(0,-0.494592,1.494592)),(0,(0,-0.494592,1.494592)),
                       (0.125,(-0.170208,-0.855843,1.685635))):
            for attenuation in (float('inf'),float('-inf'),float('nan')):
                values=[0,0,ramp,0,0,0,1,0,*k,0,attenuation,0,0,0]
                cases.append((values,[0]*4))
        add('spotlight-'+path,body,cases,'vec4(attenuation)')
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
    # Back-face alpha and shininess are independently programmed. Extract
    # both lighting generators' expressions and vary only those live values.
    alpha_lines=re.findall(r'"( *oB0 = vec4\(sceneBackAmbientColor, .*?;\\n)"',ff)
    assert len(alpha_lines)==2
    back_blocks=list(re.finditer('        " +float bDotVP =',ff))
    assert len(back_blocks)==2
    for path,alpha_line,match in zip(('dynamic','static'),alpha_lines,back_blocks):
        end=ff.index('oB0.xyz += lightBackAmbientColor',match.start())
        back=strings(ff[match.start():end])
        alpha=ast.literal_eval('"'+alpha_line+'"')
        body=('float nDotVPraw=probe[0].x,nDotHVraw=probe[0].y;'
              'float specularPower=probe[2].x,specularPowerBack=probe[2].z;'
              'float back_material_alpha=probe[2].y;'
              'vec3 sceneBackAmbientColor=vec3(0.0);vec4 oD0=vec4(probe[1].w);'
              'vec4 oB0;\n')+alpha+back
        cases=[]
        for vp,hv in ((-1,-0.5),(-0.5,-0.8),(-1,-1),(1,-0.5),(-1,0.5),(0,-0.5)):
            for front_power in (1,8):
                for back_power in (2,16,2):
                    for front_alpha,back_alpha in ((1,0.25),(0.2,0.75),(0.8,0)):
                        pf=0 if vp>=0 or hv>=0 else (-hv)**back_power
                        values=[vp,hv,0,0,0,0,0,front_alpha,front_power,back_alpha,back_power,0,0,0,0,0]
                        cases.append((values,[back_alpha,pf,abs(hv)**front_power,1]))
        add('lighting-back-'+path,body,cases,
            'vec4(oB0.a,bpf,pow(abs(nDotHVraw),specularPower),1)')
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
    fragment='#version 330\nin vec4 vtxT0,vtxT1,vtxT2,vtxT3;out vec4 color;void main(){'+setup+'color=vec4(pT3.xy,pT0.xy);}'
    passthrough=strings(between(ps,'    } else {\n        mstring_append(vars, "vec4 pT3 = vtxT3;',
                               '    if (ui_clamp)'))
    pass_fragment=fragment.replace(setup,setup[:setup.index('vec4 pT3')]+passthrough)
    begin=ps.index('static bool psh_point_sprite_enabled(')
    selection=ps[begin:ps.index('\n}',begin)+2]
    regs=(r/'hw/xbox/nv2a/pgraph/vsh_regs.h').read_text()
    enums='\n'.join(re.search(r'enum '+name+r' \{.*?\};',regs,re.S)[0]
                    for name in ('ShaderPrimitiveMode','ShaderPolygonMode'))
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
    nonpoints=r'''{
    const char *vertex="#version 330\nout vec4 vtxT0,vtxT1,vtxT2,vtxT3;void main(){vec2 pos[3]=vec2[3](vec2(-.7,-.3),vec2(.7,-.3),vec2(0,.7));gl_Position=vec4(pos[gl_VertexID],0,1);gl_PointSize=8;vtxT0=vec4(.125,.75,0,1);vtxT1=vec4(.25);vtxT2=vec4(.5);vtxT3=vec4(.625,.375,0,1);}";
    const char *fragments[2]={/* PASS FRAGMENT */,/* FRAGMENT */};
    GLuint texture,fbo;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,48,48,0,GL_RGBA,GL_FLOAT,NULL);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    unsigned draws=0;
    for(unsigned enabled=0;enabled<2;enabled++)for(unsigned polygon=0;polygon<2;polygon++)for(unsigned lines=0;lines<2;lines++){
      unsigned primitive=lines?PRIM_TYPE_LINES:PRIM_TYPE_TRIANGLES;
      uint32_t raster=polygon|(polygon<<2)|(enabled?NV_PGRAPH_SETUPRASTER_POINTSMOOTHENABLE:0);
      bool sprite=psh_point_sprite_enabled(raster,primitive);
      assert(sprite==(!lines&&polygon&&enabled));
      GLuint v=compile(GL_VERTEX_SHADER,vertex),f=compile(GL_FRAGMENT_SHADER,fragments[sprite]),p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);GLint linked;glGetProgramiv(p,GL_LINK_STATUS,&linked);assert(linked);glUseProgram(p);
      glPolygonMode(GL_FRONT_AND_BACK,polygon?GL_POINT:GL_FILL);glClearColor(-1,-1,-1,-1);glClear(GL_COLOR_BUFFER_BIT);glDrawArrays(lines?GL_LINES:GL_TRIANGLES,0,lines?2:3);
      float pixels[48*48*4];glReadPixels(0,0,48,48,GL_RGBA,GL_FLOAT,pixels);assert(glGetError()==GL_NO_ERROR);unsigned covered=0;
      for(unsigned i=0;i<48*48;i++){float *c=&pixels[i*4];if(c[0]<0)continue;covered++;
        if(!sprite){assert(c[0]==.625f&&c[1]==.375f);}else{assert(c[0]>=0&&c[0]<=1&&c[1]>=0&&c[1]<=1);}
        assert(c[2]==.125f&&c[3]==.75f);
      }
      assert(covered);draws++;glDeleteProgram(p);glDeleteShader(v);glDeleteShader(f);
    }
    glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);glDeleteFramebuffers(1,&fbo);glDeleteTextures(1,&texture);
    printf("PASS: %u GPU primitive/polygon/sprite transitions preserve stage-3 coordinates\n",draws);
    }'''
    fixture=out/'fixture.c'
    source=(r/'libretro/tests/test-point-fog-gl.c').read_text()
    prefix='#include "hw/xbox/nv2a/nv2a_regs.h"\n#define ctz32(v) __builtin_ctz(v)\n'+enums+'\n'+selection+'\n'
    source=source.replace('int main(',prefix+'int main(')
    source=source.replace('/* SPRITE */',sprite+'\n'+nonpoints)
    source=source.replace('/* PASS FRAGMENT */',json.dumps(pass_fragment)).replace('/* FRAGMENT */',json.dumps(fragment))
    fixture.write_text(source)
    command=[a.cc,'-O2' ,'-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r),str(fixture),'-o',str(exe)]
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
    for path in ('static','dynamic'):
        name='lighting-back-'+path
        for field,before,after in (('alpha','sceneBackAmbientColor, back_material_alpha','sceneBackAmbientColor, oD0.a'),
                                   ('power','pow(bDotHV, specularPowerBack)','pow(bDotHV, specularPower)')):
            negative=out/(name+'-front-'+field+'.vert')
            source=(out/(name+'.vert')).read_text()
            assert before in source
            negative.write_text(source.replace(before,after))
            result=subprocess.run([str(exe),str(negative),str(out/(name+'.bin'))],
                env=dict(env,EGL_PLATFORM='surfaceless'),capture_output=True,text=True)
            assert result.returncode==1 and ' != ' in result.stderr,result.stderr
    print('PASS: front-only back alpha/specular power fails both lighting path oracles')
    for path in ('static','dynamic'):
        name='spotlight-'+path
        negative=out/(name+'-linear.vert')
        source=(out/(name+'.vert')).read_text()
        source=re.sub(r'attenuation = spotlightAttenuation\([^;]+;',
            'attenuation *= clamp(dot(spotDir.xyz, VP) + spotDir.w, 0.0, 1.0);',source)
        negative.write_text(source)
        result=subprocess.run([str(exe),str(negative),str(out/(name+'.bin'))],
            env=dict(env,EGL_PLATFORM='surfaceless'),capture_output=True,text=True)
        assert result.returncode==1 and ' != ' in result.stderr,result.stderr
    print('PASS: linear-only spotlights fail both lighting path oracles')
    for path in ('static','dynamic'):
        name='spotlight-'+path
        negative=out/(name+'-unmasked.vert')
        source=(out/(name+'.vert')).read_text()
        before='factor == 0.0 ? 0.0 : attenuation * factor'
        assert before in source
        negative.write_text(source.replace(before,'attenuation * factor'))
        result=subprocess.run([str(exe),str(negative),str(out/(name+'.bin'))],
            env=dict(env,EGL_PLATFORM='surfaceless'),capture_output=True,text=True)
        assert result.returncode==1 and ' != ' in result.stderr,result.stderr
    print('PASS: unmasked non-finite spotlight attenuation fails both path oracles')
    for name in ('material-static-8','material-dynamic'):
        negative=out/(name+'-double-scene.vert')
        source=(out/(name+'.vert')).read_text()
        before='return scene + (emissionSource == 0u ? vec3(0.0) : emission * multiplier);'
        assert before in source
        negative.write_text(source.replace(before,'return scene * multiplier + (emissionSource == 0u ? scene : emission);'))
        result=subprocess.run([str(exe),str(negative),str(out/(name+'.bin'))],env=dict(env,EGL_PLATFORM='surfaceless'),capture_output=True,text=True)
        assert result.returncode==1 and ' != ' in result.stderr,result.stderr
    for name in ('material-static-0','material-dynamic'):
        negative=out/(name+'-unmodulated-ambient.vert')
        source=(out/(name+'.vert')).read_text()
        before=ambient_dynamic if name.endswith('dynamic') else '  lightAmbient *= diffuse.xyz;\n'
        assert before in source
        negative.write_text(source.replace(before,''))
        result=subprocess.run([str(exe),str(negative),str(out/(name+'.bin'))],env=dict(env,EGL_PLATFORM='surfaceless'),capture_output=True,text=True)
        assert result.returncode==1 and ' != ' in result.stderr,result.stderr
    print('PASS: double-scene and unmodulated ambient controls fail both material paths')
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
