# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute production depth GLSL against double-precision and old-path oracles."""
from pathlib import Path
import ast
import os
import re
import shutil
import subprocess
import sys
import tempfile

repo=Path(sys.argv[1]).resolve()
cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
source=(repo/'hw/xbox/nv2a/pgraph/glsl/psh.c').read_text()


def strings(text):
    return ''.join(ast.literal_eval(token) for token in re.findall(r'"(?:[^"\\]|\\.)*"',text))


helpers=strings(source[source.index('        "float kahan_det('):source.index('        "const float[9] gaussian3x3')])
start=source.index('    /* Barycentric depth:')
divide=source.index('    if (ps->state->z_perspective)',start)
barycentric=strings(source[start:divide])
normalization=source.rfind('    mstring_append(',divide,source.index('"float inv_bcsum',divide))
depth=source.index('    if (ps->state->z_perspective)',normalization)
perspective_divide=strings(source[divide:normalization])
normalize=strings(source[normalization:depth])
linear=source.index('    } else {',depth)
end=source.index('    /* Depth clipping.',linear)
perspective_depth=strings(source[depth:linear])
linear_depth=strings(source[linear:end])
prefix='''#version 450
layout(local_size_x=64) in;
struct Case {vec4 p0;vec4 p1;vec4 p2;vec4 point;};
layout(std430,binding=0) readonly buffer Input {Case cases[];};
layout(std430,binding=1) writeonly buffer Output {vec4 results[];};
'''+helpers+'''
void main(){
  uint index=gl_GlobalInvocationID.x;
  if(index>=cases.length()) return;
  Case c=cases[index];vec4 vtxPos0=c.p0,vtxPos1=c.p1,vtxPos2=c.p2;
  float surfaceScale=c.point.z,depthOffset=0.0,depthFactor=0.0,triMZ=0.0;
'''
suffix='\nfloat z;\nif(c.point.w!=0.0){\n'+perspective_depth+'\nz=zvalue;\n}else{\n'+linear_depth+'\nz=zvalue;\n}\nresults[index]=vec4(bc1,bc2,z,(isnan(bc_probe)||isinf(bc_probe))?1.0:0.0);\n}\n'
old=barycentric[:barycentric.index('precise float bc_probe')]+'float bc_probe=bc0+bc1+bc2;\n'
with tempfile.TemporaryDirectory(prefix='barycentric-overflow-') as directory:
    folder=Path(directory)
    # Compile the changed fragment arithmetic through the core's compiler,
    # whose accepted GLSL vocabulary is narrower than glslang's.
    driver=folder/'compiler.c'
    driver.write_text('''#include <stdio.h>
#include <stdlib.h>
#include "hw/xbox/nv2a/pgraph/spv/glslc.h"
int main(int argc,char **argv){
  if(argc!=4)return 2;
  FILE *f=fopen(argv[1],"rb");if(!f)return 2;
  fseek(f,0,SEEK_END);long n=ftell(f);rewind(f);char *s=malloc(n+1);
  if(!s || fread(s,1,n,f)!=(size_t)n)return 2;fclose(f);s[n]=0;
  GlslcOptions o={.spirv_1_6=atoi(argv[3])!=0};GlslcResult r;
  if(!glslc_compile(GLSLC_STAGE_FRAGMENT,s,&o,&r)){
    fprintf(stderr,"line %d: %s\\n",r.error_line,r.error);return 1;}
  f=fopen(argv[2],"wb");if(!f)return 2;
  if(fwrite(r.words,4,r.num_words,f)!=r.num_words)return 2;
  fclose(f);free(s);free(r.words);return 0;
}
''')
    compiler=folder/'compiler'
    subprocess.run([cc,'-O2','-std=gnu11','-I'+str(repo),'-I'+str(repo/'deps/SPIRV-Reflect/include'),
                    str(driver),str(repo/'hw/xbox/nv2a/pgraph/spv/glslc.c'),
                    str(repo/'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'),'-lm','-o',str(compiler)],check=True)
    for name,body in (('current',barycentric),('original',old)):
        shader=prefix+body.replace('gl_FragCoord.xy','c.point.xy')
        shader+='\nif(c.point.w!=0.0){\n'+perspective_divide+'}\n'+normalize+suffix
        path=folder/(name+'.comp');path.write_text(shader)
        if shutil.which('glslc'):
            subprocess.run(['glslc',str(path),'-o',str(folder/(name+'.spv'))],check=True)
    # Check the shader language level of the GL backend too, without the
    # SSBO/compute requirements of the arithmetic execution harness.
    for mode in ('linear','perspective'):
        fragment='#version 400\nuniform vec4 vtxPos0,vtxPos1,vtxPos2;\nuniform float surfaceScale;\n'
        fragment+='layout(location=0) out vec4 result;\n'+helpers+'\nvoid main(){\nfloat depthOffset=0.0,depthFactor=0.0,triMZ=0.0;\n'
        fragment+=barycentric+(perspective_divide if mode=='perspective' else '')+normalize
        fragment+=(perspective_depth if mode=='perspective' else linear_depth)+'\nresult=vec4(zvalue);\n}\n'
        (folder/(mode+'.frag')).write_text(fragment)
        vk=fragment.replace('#version 400','#version 450').replace(
            'uniform vec4 vtxPos0,vtxPos1,vtxPos2;\nuniform float surfaceScale;',
            'layout(set=0,binding=0,std140) uniform U {vec4 p0,p1,p2;float scale;} u;\n'
            '#define vtxPos0 u.p0\n#define vtxPos1 u.p1\n#define vtxPos2 u.p2\n#define surfaceScale u.scale')
        path=folder/(mode+'-vk.frag');path.write_text(vk)
        for version in (0,1):
            output=folder/(mode+str(version)+'.spv')
            subprocess.run([str(compiler),str(path),str(output),str(version)],check=True)
            if shutil.which('spirv-val'):
                subprocess.run(['spirv-val','--target-env','vulkan1.3' if version else 'vulkan1.2',str(output)],check=True)
    exe=folder/'test'
    subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',
                    str(Path(__file__).with_suffix('.c')),'-lEGL','-lGL','-lm','-o',str(exe)],check=True)
    subprocess.run([str(exe),str(folder/'current.comp'),str(folder/'original.comp'),
                    str(folder/'linear.frag'),str(folder/'perspective.frag')],check=True,
                   timeout=30,env=dict(os.environ,EGL_PLATFORM='surfaceless'))
