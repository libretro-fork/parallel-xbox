# SPDX-License-Identifier: LGPL-2.1-or-later
"""Probe fixture-only report-dispatch candidates; production remains unchanged.
Complete production dispatcher, frozen reference, modeled callbacks/queue.
Usage: python test-report-dispatch.py CHECKOUT [GCC]
"""
from pathlib import Path
import os,subprocess,sys,tempfile
repo=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'gcc'
s=(repo/'hw/xbox/nv2a/pgraph/vk/reports.c').read_text();a=s.index('void pgraph_vk_process_pending_reports(NV2AState *d)');a=s.index('{',a);b=a+1;depth=1
while depth:depth+=(s[b]=='{')-(s[b]=='}');b+=1
body=s[a:b]
queue_first=body.replace('    if (pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_GET) ==\n            pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_PUT) &&\n        r->in_command_buffer &&\n        !QSIMPLEQ_EMPTY(&r->report_queue)) {', '    QueryReport *head = QSIMPLEQ_FIRST(&r->report_queue);\n    if (!head) { return; }\n    if (pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_GET) ==\n            pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_PUT) &&\n        r->in_command_buffer) {')
queue_first=queue_first.replace('    QueryReport *head = QSIMPLEQ_FIRST(&r->report_queue);\n    if (head && (r->queries_read', '    if (head && (r->queries_read')
queue_open=queue_first.replace('    if (pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_GET) ==\n            pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_PUT) &&\n        r->in_command_buffer) {', '    if (r->in_command_buffer &&\n        pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_GET) ==\n            pfifo_reg_r(d, NV_PFIFO_CACHE1_DMA_PUT)) {')
assert queue_first!=body and queue_open!=queue_first
assert queue_first.count('QueryReport *head')==queue_open.count('QueryReport *head')==1
template=Path(__file__).with_suffix('.c').read_text().replace('/* REFERENCE */',Path(__file__).with_name('report-dispatch-reference.h').read_text())
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='report-dispatch-') as tmp:
 p=Path(tmp)
 for mode,(name,candidate) in enumerate([('production',body),('queue-first',queue_first),('queue-open-first',queue_open)]):
  print(name,flush=True)
  (p/'test.c').write_text(template.replace('/* CURRENT */',candidate))
  for checked in [True,False]:
   exe=p/('test.exe' if os.name=='nt' else 'test')
   flags=['-DEXPECT_MODE='+str(mode),'-DCHECKED','-fsanitize=undefined','-fsanitize-undefined-trap-on-error'] if checked else ['-DEXPECT_MODE='+str(mode)]
   subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',*flags,str(p/'test.c'),'-o',str(exe)],env=env,check=True)
   subprocess.run([str(exe)],env=env,check=True)
