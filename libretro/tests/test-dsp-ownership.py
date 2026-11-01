# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production EP ownership, DMA, dispatch, GPU reservations and TLB guards.
Usage: python3 test-dsp-ownership.py CHECKOUT [CC]
"""
from pathlib import Path
import os, subprocess, sys, tempfile
repo=Path(sys.argv[1]).resolve(); cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
dsp=(repo/'hw/xbox/mcpx/apu/dsp/gp_ep.c').read_text()
tlb=(repo/'accel/tcg/cputlb.c').read_text(); scalar=(repo/'system/memory_ldst.c.inc').read_text()
def between(s,a,b):
 start=s.index(a); return s[start:s.index(b,start)]
header=(repo/'libretro/dsp-ownership.h').read_text()
blocks={
 '/* PRODUCTION TRANSACTIONS */':between(dsp,'int lr_dsp_external_begin(','static void ownership_ram_access('),
 '/* PRODUCTION CLEANUP */':between(header,'static inline void lr_dsp_external_cleanup(','bool lr_dsp_tlb_watched('),
 '/* PRODUCTION WAIT */':between(dsp,'void lr_dsp_ownership_quiesce(','static void *ownership_worker('),
 '/* PRODUCTION WORKER */':between(dsp,'static void *ownership_worker(','static void ownership_start('),
 '/* PRODUCTION ACCESS */':between(dsp,'static void ownership_ram_access(','/* The option values'),
 '/* PRODUCTION WALK */':between(dsp,'static void scatter_gather_walk(','static void gp_scratch_walk('),
 '/* PRODUCTION POST */':between(dsp,'void mcpx_apu_dsp_post(','void mcpx_apu_dsp_post_silence('),
 '/* PRODUCTION GPU */':between(dsp,'static retro_atomic_int_t gpu_page_refs','static void ownership_mark('),
 '/* PRODUCTION FOOTPRINT */':between(dsp,'static void ownership_mark(','static void scatter_gather_walk(')+between(dsp,'static bool ownership_candidate(','void mcpx_apu_dsp_post('),
 '/* PRODUCTION SCALAR */':between(scalar,'static inline uint32_t glue(address_space_ldl_internal','uint32_t glue(address_space_ldl,')+between(scalar,'static inline void glue(address_space_stl_internal','void glue(address_space_stl,'),
 '/* PRODUCTION TLB */':between(tlb,'static inline void tlb_set_compare(','/* Sparse reverse index')+between(tlb,'#define DSP_TLB_SLOTS','/*\n * Add a new TLB entry.')+between(tlb,'static bool victim_tlb_hit(','/*\n * CPU boost:')
}
env=os.environ.copy(); env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
common=repo/'libretro-common'; thread=[str(common/'rthreads/rthreads.c'),str(common/'rthreads/retro_eventcount.c')]
flags=['-std=gnu11','-O2','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-Wno-unused-function','-Wno-unused-variable','-I'+str(repo),'-I'+str(repo/'include'),'-I'+str(common/'include')]
if os.environ.get('DSP_OWNERSHIP_SANITIZE'):
 flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
if os.name=='nt': flags+=['-static']
else: flags+=['-pthread']
rendered={}
with tempfile.TemporaryDirectory(prefix='dsp-ownership-') as directory:
 tmp=Path(directory)
 for case in ['access','dma','dispatch','scalar','footprint','tlb','epochs','transactions']:
  code=(repo/('libretro/tests/test-dsp-ownership-'+case+'.c')).read_text()
  for marker,body in blocks.items(): code=code.replace(marker,body)
  rendered[case]=code
  source=tmp/(case+'.c');binary=tmp/(case+('.exe' if os.name=='nt' else ''))
  source.write_text(code)
  subprocess.run([cc,*flags,str(source),*thread,'-o',str(binary)],env=env,check=True)
  subprocess.run([str(binary)],env=env,check=True,timeout=60)
 # Race complete borrowed-pointer lifetimes against DSP admission.
 binary=tmp/('lease.exe' if os.name=='nt' else 'lease')
 subprocess.run([cc,*flags,str(repo/'libretro/tests/test-dsp-bus-lease.c'),*thread,'-o',str(binary)],env=env,check=True)
 subprocess.run([str(binary)],env=env,check=True,timeout=60)

 # Negative controls must fail at the guarded pointer or producer handoff.
 if os.name!='nt':
  import resource
  resource.setrlimit(resource.RLIMIT_CORE,(0,0))
  mutations=[('dma','if (ownership_gp_dma_guard && !ownership_is_worker)','if (false)'),('dispatch','    lr_dsp_ownership_fence(LR_DSP_FENCE_TICK);','    /* Deliberately omit handoff. */')]
  for case,before,after in mutations:
   assert before in rendered[case]
   source=tmp/(case+'-negative.c');binary=tmp/(case+'-negative')
   source.write_text(rendered[case].replace(before,after,1))
   subprocess.run([cc,*flags,str(source),*thread,'-o',str(binary)],env=env,check=True)
   result=subprocess.run([str(binary)],env=env,capture_output=True,timeout=60)
   assert result.returncode!=0 and b'Assertion' in result.stderr,(case,result.returncode,result.stderr)
   print('Negative control rejected:',case,flush=True)
