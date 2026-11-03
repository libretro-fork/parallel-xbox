# SPDX-License-Identifier: GPL-2.0-or-later
"""Character output retries yield without timed sleeps or changing write results."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'chardev/char.c').read_text()
def function(name):
 a=s.index('static '+name);b=s.index('{',a)+1;d=1
 while d:d+=(s[b]=='{')-(s[b]=='}');b+=1
 return s[a:b]
code=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
typedef struct Chardev Chardev;
typedef struct {int (*chr_write)(Chardev*,const uint8_t*,int);} ChardevClass;
struct Chardev {int logfd,chr_write_lock;};
static ChardevClass cls;
#define CHARDEV_GET_CLASS(s) (&cls)
static unsigned yields,calls,bytes,failures;
static bool held,fatal;
static void slock_unused(void){}
static void qemu_mutex_lock(int*p){(void)p;assert(!held);held=true;}
static void qemu_mutex_unlock(int*p){(void)p;assert(held);held=false;}
static void sthread_yield(void){yields++;}
static int backend(Chardev*s,const uint8_t*b,int n){(void)s;(void)b;assert(held);calls++;if(failures){failures--;errno=EAGAIN;return -1;}if(fatal){errno=EIO;return -1;}int k=n>2?2:n;bytes+=k;return k;}
static ssize_t write(int fd,const void*b,size_t n){(void)b;assert(fd==3);calls++;if(failures){failures--;errno=EAGAIN;return -1;}if(fatal){errno=EIO;return -1;}size_t k=n>2?2:n;bytes+=k;return (ssize_t)k;}
/* PRODUCTION */
int main(void){Chardev c={3,0};const uint8_t b[7]={0};unsigned cases=0;
 for(unsigned retry=0;retry<64;retry++){
  calls=bytes=yields=0;failures=retry;fatal=false;
  qemu_chr_write_log(&c,b,7);assert(bytes==7&&yields==retry&&calls==retry+4);cases++;
  c.logfd=-1;cls.chr_write=backend;calls=bytes=yields=0;failures=retry;int off=-1;
  assert(qemu_chr_write_buffer(&c,b,7,&off,true)==1&&off==7&&!held&&bytes==7&&yields==retry);cases++;
  calls=bytes=yields=0;failures=retry;
  int res=qemu_chr_write_buffer(&c,b,7,&off,false);
  assert(!held&&!yields&&calls==1);assert(retry?(res==-1&&off==0):(res==2&&off==2));cases++;
  c.logfd=3;
 }
 calls=bytes=yields=failures=0;fatal=true;qemu_chr_write_log(&c,b,7);assert(calls==1&&!yields&&!bytes);
 c.logfd=-1;cls.chr_write=backend;int off;assert(qemu_chr_write_buffer(&c,b,7,&off,true)==-1&&off==0&&!held&&!yields);
 cls.chr_write=null_chr_write;calls=0;assert(qemu_chr_write_buffer(&c,b,7,&off,true)==7&&off==7&&!calls&&!held);
 printf("Character writes: %u retry/partial/nonblocking cases plus fatal errors and null sink passed\n",cases);
}
'''
code=code.replace('static void slock_unused(void){}','').replace('/* PRODUCTION */','\n'.join(function(n) for n in ['void qemu_chr_write_log(','int null_chr_write(','int qemu_chr_write_buffer(']))
with tempfile.TemporaryDirectory(prefix='char-retry-') as t:
 p=Path(t);(p/'test.c').write_text(code);exe=p/'test'
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=undefined','-fsanitize-undefined-trap-on-error',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=30)
