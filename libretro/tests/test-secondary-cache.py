# SPDX-License-Identifier: GPL-2.0-or-later
"""Production TB lookup must recover class entries despite primary collisions."""
from pathlib import Path
import os,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'accel/tcg/cpu-exec.c').read_text();a=s.index('static inline TranslationBlock *tb_lookup(');b=s.index('static void log_cpu_exec(',a)
h=(r/'accel/tcg/tb-hash.h').read_text();i=h.index('static inline void tb_jmp_cache_store(');j=h.index('static inline\nuint32_t tb_hash_func',i)
f=(r/'accel/tcg/translate-all.c').read_text();k=f.index('void tcg_flush_jmp_cache(');e=f.index('{',k)+1;depth=1
while depth:
 depth+=(f[e]=='{')-(f[e]=='}');e+=1
code=Path(__file__).with_suffix('.c').read_text().replace('/* FLUSH */',f[k:e]).replace('/* STORE */',h[i:j]).replace('/* PRODUCTION */',s[a:b])
env=os.environ.copy();env['PATH']=str(Path(cc).resolve().parent)+os.pathsep+env.get('PATH','')
with tempfile.TemporaryDirectory(prefix='secondary-cache-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code);exe=p/('test.exe' if os.name=='nt' else 'test')
 subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],check=True,env=env)
 subprocess.run([str(exe)],check=True,env=env,timeout=30)
