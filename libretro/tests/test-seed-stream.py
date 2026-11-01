# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise production streamed seed rewriting against a byte-image oracle."""
from pathlib import Path
import os,re,shlex,subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/xbox/nv2a/pgraph/seed.c').read_text(encoding='utf-8')
parts=[re.search(r'typedef struct '+n+r' \{.*?\} '+n+r';',s,re.S).group() for n in ['SeedHeader','SeedSection']]
parts += [re.search(r'struct SeedRewrite \{.*?\};',s,re.S).group()]
for name in ['seed_walk','seed_stream_write','seed_rewrite_visit','pgraph_seed_write']:
 m=re.search(r'^(?:static )?[^\n]*\b'+name+r'\([^;{]*\)\s*\{',s,re.M);assert m,name
 end=s.index('{',m.start())+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 parts.append(s[m.start():end])
code=Path(__file__).with_suffix('.c').read_text().replace('/* PRODUCTION */','\n'.join(parts))
with tempfile.TemporaryDirectory(prefix='seed-stream-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(code)
 binary=os.environ.get('SEED_STREAM_BINARY',str(p/'test'))
 subprocess.run(shlex.split(cc)+['-O2','-g','-std=gnu11','-Wall','-Wextra','-Werror','-I'+str(r/'include')]+
                shlex.split(os.environ.get('SEED_STREAM_CFLAGS','-fsanitize=address,undefined'))+[str(p/'test.c'),'-o',binary],check=True)
 if not os.environ.get('SEED_STREAM_SKIP_RUN'):subprocess.run([binary],check=True,timeout=30)
