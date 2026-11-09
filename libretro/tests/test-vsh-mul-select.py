# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare production NV2A MUL/MAD with legacy GPU behavior and independent MUL goldens."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('repo', type=Path)
parser.add_argument('--cc', default='gcc')
parser.add_argument('--validator',default=shutil.which('spirv-val'),help='Validate each correct and mutated module')
parser.add_argument('--export-only',type=Path,help='Write bounded private benchmark inputs without running GPU tests')
args = parser.parse_args()
repo = args.repo.resolve()
import ast,json,re

def strings_between(path,start,end):
    text=path.read_text();text=text[text.index(start):];text=text[:text.index(end)]
    return ''.join(ast.literal_eval(m.group(1)) for m in re.finditer(r'^\s*("(?:[^"\\]|\\.)*")',text,re.M))

current=strings_between(repo/'hw/xbox/nv2a/pgraph/glsl/vsh-prog.c','    "vec4 _MUL(', '    "#define ADD(').replace('_MUL(', '_currentMUL(')
nan=strings_between(repo/'hw/xbox/nv2a/pgraph/glsl/vsh.c','        "vec4 NaNToOne(', '        "vec4 NaNToValue(')
current_mad=strings_between(repo/'hw/xbox/nv2a/pgraph/glsl/vsh-prog.c','    "vec4 _MAD(', '    "#define DP3(').replace('_MAD(', '_currentMAD(').replace('_MUL(', '_currentMUL(')
reference="""vec4 _referenceMUL(vec4 src0,vec4 src1){
 vec4 zero_components=sign(NaNToOne(src0))*sign(NaNToOne(src1));
 vec4 ret=src0*src1;
 if(zero_components.x==0.0)ret.x=0.0;
 if(zero_components.y==0.0)ret.y=0.0;
 if(zero_components.z==0.0)ret.z=0.0;
 if(zero_components.w==0.0)ret.w=0.0;
 return ret;}
"""
shader="#version 450\nlayout(local_size_x=1) in;\nlayout(std430,set=0,binding=0) buffer Data {uint value[65536];} result;\n"+nan+reference+current+current_mad+"vec4 _referenceMAD(vec4 a,vec4 b,vec4 c){return _referenceMUL(a,b)+c;}\n"+"""
void main(){uint i=gl_GlobalInvocationID.x;uint base=i*16u;
 vec4 a=uintBitsToFloat(uvec4(result.value[base],result.value[base+1u],result.value[base+2u],result.value[base+3u]));
 vec4 b=uintBitsToFloat(uvec4(result.value[base+4u],result.value[base+5u],result.value[base+6u],result.value[base+7u]));
 vec4 old=_referenceMUL(a,b);vec4 now=_currentMUL(a,b);
 for(int lane=0;lane<4;lane++){if((i&(1u<<uint(lane)))!=0u){result.value[base+8u+uint(lane)]=floatBitsToUint(old[lane]);result.value[base+12u+uint(lane)]=floatBitsToUint(now[lane]);}}}
"""
if args.export_only:
    args.export_only.mkdir(parents=True,exist_ok=True)
    for variant,helper in (('baseline',reference.replace('_referenceMUL','_benchMUL')),('candidate',current.replace('_currentMUL','_benchMUL'))):
        for count in (1,16,64):
            body='vec4 old=_benchMUL(a,b); vec4 now=old;\n'
            for step in range(1,count):
                body+='old=_benchMUL(old,b);now=old;\n'
            single=shader[:shader.index('vec4 _referenceMUL(')]+helper+shader[shader.index('\nvoid main()'):]
            single=single.replace('vec4 old=_referenceMUL(a,b);vec4 now=_currentMUL(a,b);',body)
            (args.export_only/(variant+'-'+str(count)+'.comp')).write_text(single)
    raise SystemExit(0)

helpers=(repo/'libretro/tests/spv-clear-vulkan.h').read_text().split('static VkPipeline pipeline(')[0]
source=Path(__file__).with_suffix('.c').read_text().replace('/* GPU HELPERS */',helpers).replace('/* SHADER SOURCE */',json.dumps(shader))

env = os.environ.copy()
env['PATH'] = str(Path(args.cc).resolve().parent) + os.pathsep + env.get('PATH', '')
with tempfile.TemporaryDirectory(prefix='nv2a-mul-') as directory:
    directory = Path(directory)
    test = directory / 'test.c'; test.write_text(source)
    for mode,mutant in (("MUL",False),("MUL",True),("MAD",False),("MAD",True)):
        compiler=repo/'hw/xbox/nv2a/pgraph/spv/glslc.c'
        variant=shader
        if mode=="MAD":
            variant=variant.replace('vec4 old=_referenceMUL(a,b);vec4 now=_currentMUL(a,b);','vec4 old=_referenceMAD(a,b,a.wzyx);vec4 now=_currentMAD(a,b,a.wzyx);')
        if mutant:
            # Keep the emitted multiply but remove its exceptional-value correction.
            begin=variant.index('vec4 _currentMUL(');end=variant.index('vec4 _currentMAD(',begin)
            variant=variant[:begin]+'vec4 _currentMUL(vec4 a,vec4 b){return a*b;}\n'+variant[end:]
        test.write_text(Path(__file__).with_suffix('.c').read_text().replace('/* GPU HELPERS */',helpers).replace('/* SHADER SOURCE */',json.dumps(variant)).replace('/* PRODUCT ORACLE */','true' if mode=='MUL' else 'false').replace('/* OP NAME */',mode))
        exe = directory / ('test.exe' if os.name == 'nt' else 'test')
        command = [args.cc, '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-Wno-missing-field-initializers', '-Wno-unused-variable',
            '-fsanitize=undefined', '-fsanitize-undefined-trap-on-error',
            '-I' + str(repo), '-I' + str(repo / 'deps/volk'),
            '-I' + str(repo / 'deps/vulkan-headers'), '-I' + str(repo / 'deps/spirv-headers/include'),
            '-I' + str(repo / 'hw/xbox/nv2a/pgraph/spv'), str(test), str(compiler),
            str(repo / 'hw/xbox/nv2a/pgraph/spv/glslc_lex.c'), str(repo / 'deps/volk/volk.c'),
            '-lm', '-o', str(exe)]
        if os.name != 'nt': command += ['-ldl']
        subprocess.run(command, check=True, env=env)
        module=directory/'module.spv'
        result = subprocess.run([str(exe),str(module)], env=env, timeout=60)
        if args.validator:
            subprocess.run([args.validator,str(module)],check=True,env=env)
        else:
            print('External SPIR-V validator unavailable; GPU checks do not establish structural validation')
        if mutant:
            assert result.returncode == 1, 'missing zero correction must fail GPU goldens'
            print('Required '+mode+' negative control rejected: missing zero/NaN correction')
        else:
            result.check_returncode()
