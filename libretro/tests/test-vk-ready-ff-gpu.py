#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Vulkan numerical proof of the finite prepared fixed-function families.

Keep the original 33,232 FF recipes, input values, exact comparisons and
four-worker/shared-300-second batch limits. Supplementary projections apply
to both reference and candidate. This is a source/arithmetic proof, not a
production constant-suffix, retirement or cold-play performance test.
"""
from pathlib import Path
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile


DOMAINS = ('original', 'unlit', 'idle', 'literal', 'csv-texgen')
BATCHES = [(3136,4160),(4160,5120),(5120,6416),(6416,9488),
           (9488,11536),(11536,12560),(12560,14096),(14096,15120),
           (15120,19216),(19216,19984),(19984,24080),(24080,28176),
           (28176,32272),(32272,36368)]


def once(text, before, after):
    assert text.count(before) == 1, (before, text.count(before))
    return text.replace(before, after)


# This oracle does not call the production classifier. Every recipe, including
# every excluded recipe, is checked before either path can skip a case.
ORACLE = r'''
static bool independent_eligible(const VshState *s) {
 if(!s->is_fixed_function || !s->fog_dynamic || s->compressed_attrs ||
    s->uniform_attrs || s->swizzle_attrs || s->const_attrs_dynamic ||
    (unsigned)s->fixed_function.skinning>=7)return false;
 const FixedFunctionVshState *f=&s->fixed_function;
 if(!f->texgen_dynamic)for(unsigned t=0;t<4;t++)for(unsigned a=0;a<4;a++)
  if((unsigned)f->texgen[t][a]>=6)return false;
 bool materials=f->ambient_src==MATERIAL_COLOR_SRC_MATERIAL &&
  f->emission_src==MATERIAL_COLOR_SRC_MATERIAL &&
  f->diffuse_src==MATERIAL_COLOR_SRC_MATERIAL &&
  f->specular_src==MATERIAL_COLOR_SRC_MATERIAL;
 bool idle=materials && !f->light_dynamic;
 for(unsigned l=0;l<8;l++)if(f->light[l]!=LIGHT_OFF)idle=false;
 bool literal=materials && !f->normalization && !f->local_eye && !f->two_sided &&
  s->specular_enable && s->separate_specular && s->ignore_specular_alpha;
 if(!f->light_dynamic)for(unsigned l=0;l<8;l++)if((unsigned)f->light[l]>=4)literal=false;
 return f->csv0c_dynamic || !f->lighting || idle || literal;
}
static bool domain_accepts(const VshState *s) {
 const FixedFunctionVshState *f=&s->fixed_function;
 switch(VK_READY_FF_DOMAIN){
 case 0:return true;
 case 1:return !f->csv0c_dynamic && !f->lighting;
 case 2:case 3:return !f->csv0c_dynamic && f->lighting;
 case 4:return f->csv0c_dynamic && !f->texgen_dynamic;
 default:abort();
 }
}
static void project_domain(VshState *s) {
 FixedFunctionVshState *f=&s->fixed_function;
 if(VK_READY_FF_DOMAIN==2 && f->lighting && !f->csv0c_dynamic){
  s->fog_dynamic=true;f->light_dynamic=false;memset(f->light,0,sizeof(f->light));
  f->emission_src=f->ambient_src=f->diffuse_src=f->specular_src=MATERIAL_COLOR_SRC_MATERIAL;
 }
 if(VK_READY_FF_DOMAIN==3 && f->lighting && !f->csv0c_dynamic){
  s->fog_dynamic=true;f->normalization=f->local_eye=f->two_sided=false;
  s->specular_enable=s->separate_specular=s->ignore_specular_alpha=true;
  f->emission_src=f->ambient_src=f->diffuse_src=f->specular_src=MATERIAL_COLOR_SRC_MATERIAL;
 }
 if(VK_READY_FF_DOMAIN==4 && f->csv0c_dynamic)s->fog_dynamic=true;
}
static void pack_controls(const VshState *s,VshUniformValues *v) {
 const FixedFunctionVshState *f=&s->fixed_function;
 if(!f->csv0c_dynamic)v->csv0cCtl[0]=(f->lighting?1u:0u)|(f->local_eye?2u:0u)|
  (f->normalization?4u:0u)|((unsigned)f->emission_src<<4)|
  ((unsigned)f->ambient_src<<6)|((unsigned)f->diffuse_src<<8)|
  ((unsigned)f->specular_src<<10)|(s->specular_enable?4096u:0u)|
  (s->separate_specular?8192u:0u)|(s->ignore_specular_alpha?16384u:0u)|
  (f->two_sided?32768u:0u);
 if(!f->light_dynamic){
  uint32_t modes=0,count=0;
  for(unsigned l=0;l<8;l++){modes|=(unsigned)f->light[l]<<(l*2);if(f->light[l]!=LIGHT_OFF)count=l+1;}
  v->lightMode[0]=modes|(count<<16);
 }
 if(!f->texgen_dynamic){
  v->texMatEnable[0]=0;
  for(unsigned t=0;t<4;t++){
   uint32_t mode=v->texgenMode[t]&4096u;
   for(unsigned a=0;a<4;a++)mode|=(unsigned)f->texgen[t][a]<<(a*3);
   v->texgenMode[t]=mode;if(f->texture_matrix_enable[t])v->texMatEnable[0]|=1u<<t;
  }
 }
}
'''


def prepare(root, folder, domain, mutation='', source_reference=False):
    script = (root/'libretro/tests/test-spv-vsh-gpu.py').read_text()
    script = once(script, 'DIRECT_VERTEX_FIXTURE = True', 'DIRECT_VERTEX_FIXTURE = False')
    line = next(line for line in script.splitlines() if line.startswith(' batches='))
    script = once(script, line, ' batches='+repr(BATCHES))
    needle = ' # Keep the full growing corpus, with a separate watchdog per bounded batch.'
    script = once(script, needle, ''' if os.environ.get('VK_READY_FF_BUILD_ONLY'):
  subprocess.run([str(exe),'--cpu-ledger'],check=True)
  sys.exit(0)
'''+needle)
    (folder/'driver.py').write_text(script)
    c = (root/'libretro/tests/test-spv-vsh-gpu.c').read_text()
    c = '#define VK_READY_FF_DOMAIN '+str(DOMAINS.index(domain))+'\n'+c
    c = once(c, '#define CHECK(x)', '#include "hw/xbox/nv2a/pgraph/gl/ready-ff-state.h"\n#define CHECK(x)')
    c = once(c, 'static VkDevice device;', '''static VkDevice device;
static struct {GlslcResult module;VkPipeline pipeline;} ready[GL_READY_FF_STAGE_COUNT];
static unsigned ready_builds,ready_reuses;
''')
    recipe_start = c.index('  VshState state={0};')
    recipe_end = c.index('  for(unsigned a=0;a<16;a++)attrs[a].format=', recipe_start)
    recipe = c[recipe_start:recipe_end]
    ledger = '''
static int cpu_ledger(void){
 VkPhysicalDeviceProperties props={0};props.limits.maxPushConstantsSize=256;
 unsigned eligible=0,outside=0;
 for(unsigned test=3136;test<36368;test++){
'''+recipe+'''
  (void)opts;
  project_domain(&state);bool expected=independent_eligible(&state);
  for(unsigned prefix=0;prefix<2;prefix++)assert((ready_ff_state_index(&state,prefix)>=0)==expected);
  if(expected && domain_accepts(&state))eligible++;else outside++;
 }
 printf("Vulkan FF CPU ledger: domain=%u eligible=%u outside=%u total=%u\\n",VK_READY_FF_DOMAIN,eligible,outside,eligible+outside);
 return 0;
}
'''
    main = c.index('int main(')
    c = c[:main]+ORACLE+ledger+c[main:]
    main_brace = c.index('{', c.index('int main('))+1
    c = c[:main_brace]+'''
 if(argc==2 && !strcmp(argv[1],"--cpu-ledger"))return cpu_ledger();
'''+c[main_brace:]
    c = once(c, 'unsigned compared=0,failures=0;', 'unsigned compared=0,failures=0,eligible=0,outside=0;')
    needle = '  for(unsigned a=0;a<16;a++)attrs[a].format='
    c = once(c, needle, '''  project_domain(&state);
  bool expected=independent_eligible(&state),prefix=!!((test/8)&1);
  int ready_index=ready_ff_state_index(&state,prefix);
  assert((ready_index>=0)==expected);
  if(!expected || !domain_accepts(&state)){outside++;continue;}
  eligible++;
'''+needle)
    first = c.index('  VshState excluded=state;')
    last = c.index('  VshUniformValues values={0};', first)
    source = '''  uint64_t prepared_hash=fixture_key_hash(&state,&opts);
  VshState canonical;ready_ff_state_init(&canonical,ready_index);
  /* MUTATE_SOURCE */
  GenVshGlslOptions shader_opts={.vulkan=true,.locations=true,.prefix_outputs=prefix,.ubo_binding=0};
  GlslcOptions glopts={.emit_layout=true,.preserve_float32=true};GlslcResult modules[2];clock_t before=clock();
  MString *text=NULL;
  /* REFERENCE_MODULE */
  reference_ticks+=clock()-before;reference_words+=modules[0].num_words;
  if(!ready[ready_index].module.words){
   before=clock();text=pgraph_glsl_gen_vsh(&canonical,shader_opts);
   if(!glslc_compile(GLSLC_STAGE_VERTEX,mstring_get_str(text),&glopts,&ready[ready_index].module)){fprintf(stderr,"READY: %s\\n",ready[ready_index].module.error);abort();}
   mstring_unref(text);direct_ticks+=clock()-before;direct_words+=ready[ready_index].module.num_words;ready_builds++;
  }
  modules[1]=ready[ready_index].module; /* synchronous immutable word/layout lend */
'''
    reference = '''  assert(pgraph_spv_vsh_supported(&state,opts));
  assert(pgraph_spv_vsh(&state,opts,&modules[0]));'''
    if source_reference:
        reference = '''  text=pgraph_glsl_gen_vsh(&state,shader_opts);
  if(!glslc_compile(GLSLC_STAGE_VERTEX,mstring_get_str(text),&glopts,&modules[0])){fprintf(stderr,"REFERENCE: %s\\n",modules[0].error);abort();}
  mstring_unref(text);'''
    source = once(source, '  /* REFERENCE_MODULE */', reference)
    c = c[:first]+source+c[last:]
    needle = '  for(unsigned path=0;path<2;path++) {'
    c = once(c, needle, '  pack_controls(&state,&values);\n'+needle)
    first = c.index('   size_t words;GlslcResult reference_module=modules[path];')
    last = c.index('   VkCommandBufferBeginInfo bi=', first)
    native = c[first:last].replace('VkShaderModule shader;', '').replace('VkPipeline pipeline;', '')
    # Both actual emitters now carry their own final FP32 modes. Do not patch
    # either assembled SPIR-V payload in this production-target comparison.
    native = once(native, 'if(!path && (state.point_params_enable || state.is_fixed_function))',
                  'if(false && (state.point_params_enable || state.is_fixed_function))')
    c = c[:first]+'''   VkPipeline pipeline=VK_NULL_HANDLE;VkShaderModule shader=VK_NULL_HANDLE;
   if(path && ready[ready_index].pipeline){pipeline=ready[ready_index].pipeline;ready_reuses++;}
   else{
'''+native+'''    if(path)ready[ready_index].pipeline=pipeline;
   }
'''+c[last:]
    c = once(c, 'vkDestroyPipeline(device,pipeline,NULL);vkDestroyShaderModule(device,shader,NULL);',
             'if(!path)vkDestroyPipeline(device,pipeline,NULL);if(shader)vkDestroyShaderModule(device,shader,NULL);')
    c = once(c, 'release(&modules[0]);release(&modules[1]);', 'release(&modules[0]); /* ready[] owns modules[1] */')
    c = once(c, ' drop(input);drop(output);drop(uniform);', '''
 for(unsigned i=0;i<GL_READY_FF_STAGE_COUNT;i++){
  if(ready[i].pipeline)vkDestroyPipeline(device,ready[i].pipeline,NULL);
  if(ready[i].module.words)release(&ready[i].module);
 }
 printf("Vulkan FF domain: %u eligible, %u outside; exact checks=%u; native owners=%u reuses=%u\\n",eligible,outside,compared,ready_builds,ready_reuses);
'''+ ' drop(input);drop(output);drop(uniform);')
    if mutation:
        mutations = {
            'point': 'canonical.point_params_enable=!canonical.point_params_enable;',
            'skin': 'canonical.fixed_function.skinning=(canonical.fixed_function.skinning+1)%7;',
            'material': 'canonical.fixed_function.emission_src=MATERIAL_COLOR_SRC_DIFFUSE;',
            'normal': 'canonical.fixed_function.normalization=true;',
            'eye': 'canonical.fixed_function.local_eye=true;',
            'back': 'canonical.fixed_function.two_sided=true;',
            'separate': 'canonical.separate_specular=false;',
        }
        if mutation in mutations:
            c = once(c, '  /* MUTATE_SOURCE */', mutations[mutation])
        else:
            field, mask = {'csv':('csv0cCtl',1),'light':('lightMode',1),
                           'texgen':('texgenMode',1),'matrix':('texMatEnable',1)}[mutation]
            needle = '   pack(uniform.mapped,&modules[path].uniforms,&values);'
            change = 'values.'+field+'[0]^='+str(mask)+'u;'
            if mutation=='light' and domain=='idle':change='values.lightMode[0]=1u|(1u<<16);'
            if mutation=='light' and domain=='literal':change='values.lightMode[0]=0u;'
            c = once(c, needle, '   if(path){'+change+'}\n'+needle)
    (folder/'driver.c').write_text(c)


def run(root, cc, folder, domain, mutation='', case_range=None, build_only=False, source_reference=False):
    folder.mkdir(parents=True, exist_ok=False)
    prepare(root, folder, domain, mutation, source_reference)
    env = dict(os.environ)
    env['PATH'] = str(Path(shutil.which(cc) or cc).resolve().parent)+os.pathsep+env.get('PATH','')
    for name in ('SPV_VSH_GPU_RANGE','SPV_VSH_FIXTURE_ROUND','SPV_VSH_FIXTURE_ROUNDS'):
        env.pop(name, None)
    if case_range:env['SPV_VSH_GPU_RANGE']=case_range
    if build_only:env['VK_READY_FF_BUILD_ONLY']='1'
    log = folder/'run.log'
    with log.open('w') as stream:
        result = subprocess.run([sys.executable,str(folder/'driver.py'),str(root),cc],
                                stdout=stream,stderr=subprocess.STDOUT,env=env)
    text = log.read_text(errors='replace')
    if mutation:
        assert result.returncode != 0 and re.search(r'[1-9][0-9]* differences, 0 validation errors;',text), (mutation,result.returncode,text[-5000:])
        print('Required Vulkan FF numerical fault rejected:',domain,mutation,flush=True)
    else:
        assert result.returncode == 0, (result.returncode,text[-5000:])
        if not build_only:
            rows = re.findall(r'Vulkan FF domain: (\d+) eligible, (\d+) outside; exact checks=(\d+);',text)
            assert rows and sum(int(a)+int(b) for a,b,c in rows)==33232,rows
            count = sum(int(a) for a,b,c in rows)
            checks = sum(int(c) for a,b,c in rows)
            assert checks==count*1632 and count>0,(count,checks)
            expected={'original':816,'unlit':816,'idle':2464,'literal':2464,'csv-texgen':400}
            if domain in expected:assert count==expected[domain],(domain,count)
        print(text,end='',flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('repo',type=Path);p.add_argument('cc',nargs='?',default='cc')
    p.add_argument('--domain',choices=DOMAINS,action='append')
    p.add_argument('--build-only',action='store_true')
    p.add_argument('--source-reference',action='store_true',help='supplementary ordinary-GLSL source comparison; default uses the actual ordinary direct SPIR-V emitter')
    p.add_argument('--keep',type=Path)
    a=p.parse_args();root=a.repo.resolve()
    def execute(parent):
        for domain in a.domain or DOMAINS:
            print('Vulkan FF mode:',domain,'supplementary ordinary GLSL reference' if a.source_reference else 'actual direct SPIR-V reference; both production emitters own final FP32 modes',flush=True)
            run(root,a.cc,parent/(domain+'-positive'),domain,build_only=a.build_only,source_reference=a.source_reference)
            if a.build_only:continue
            controls={'original':[('point','15144:15145'),('skin','15136:15137')],
                      'unlit':[('point','15144:15145'),('skin','15136:15137'),('csv','15136:15137'),('matrix','15136:15137')],
                      'idle':[('csv','20016:20032'),('light','20016:20032')],
                      'literal':[(x,'20016:20032' if x=='eye' else '20096:20112') for x in ('material','normal','eye','back','separate','skin','light','matrix')],
                      'csv-texgen':[(x,'32272:36368') for x in ('point','skin','csv','light','texgen','matrix')]}
            for mutation,case_range in controls[domain]:
                run(root,a.cc,parent/(domain+'-'+mutation),domain,mutation,case_range,source_reference=a.source_reference)
    if a.keep:
        a.keep.mkdir(parents=True,exist_ok=False);execute(a.keep.resolve())
    else:
        with tempfile.TemporaryDirectory(prefix='vk-ready-ff-') as tmp:execute(Path(tmp))


if __name__=='__main__':main()
