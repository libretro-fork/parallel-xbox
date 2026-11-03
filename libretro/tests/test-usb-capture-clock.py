# SPDX-License-Identifier: GPL-2.0-or-later
"""USB capture headers follow guest time, including pauses and state rewinds."""
from pathlib import Path
import subprocess,sys,tempfile
r=Path(sys.argv[1]).resolve();cc=sys.argv[2] if len(sys.argv)>2 else 'cc'
s=(r/'hw/usb/pcap.c').read_text()
a=s.index('static void do_usb_pcap_header(');b=s.index('\nstatic void do_usb_pcap_ctrl',a)
structs=s[s.index('struct pcap_hdr {'):s.index('/* ------------------------------------------------------------------------ */')]
code=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define NANOSECONDS_PER_SECOND 1000000000LL
#define QEMU_CLOCK_VIRTUAL 1
static int64_t guest;
static int64_t qemu_clock_get_ns(int domain){assert(domain==QEMU_CLOCK_VIRTUAL);return guest;}
/* STRUCTS */
/* PRODUCTION */
int main(void){
 const int64_t times[]={0,999,1000,999999999,1000000000,1668333333,1668333333,50050000000LL,1000000000,4102444800123456789LL};
 for(unsigned i=0;i<sizeof(times)/sizeof(*times);i++){
  guest=times[i];FILE*f=tmpfile();assert(f);struct usbmon_packet p={0},readback;
  p.len_cap=17;p.length=23;p.id=0x12345678;do_usb_pcap_header(f,&p);
  assert(ftell(f)==(long)(sizeof(struct pcaprec_hdr)+sizeof(p)));rewind(f);
  struct pcaprec_hdr h;assert(fread(&h,sizeof(h),1,f)==1);assert(fread(&readback,sizeof(readback),1,f)==1);fclose(f);
  assert(h.ts_sec==(uint32_t)(guest/1000000000));assert(h.ts_usec==(guest%1000000000)/1000);
  assert(readback.ts_sec==guest/1000000000&&readback.ts_usec==(int32_t)h.ts_usec);
  assert(h.incl_len==17&&h.orig_len==23+sizeof(p)&&!memcmp(&p,&readback,sizeof(p)));
 }
 puts("USB capture: guest timestamp boundaries, pause, rewind and packet layout passed");
}
'''
code=code.replace('/* STRUCTS */',structs).replace('/* PRODUCTION */',s[a:b])
with tempfile.TemporaryDirectory(prefix='usb-clock-') as t:
 p=Path(t);(p/'test.c').write_text(code);exe=p/'test'
 subprocess.run([cc,'-O2','-std=gnu11','-Wall','-Wextra','-Werror',str(p/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True,timeout=30)
