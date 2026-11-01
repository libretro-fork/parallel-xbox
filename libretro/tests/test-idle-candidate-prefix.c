/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif
#undef assert
#define assert(c) do { if (!(c)) { fprintf(stderr,"idle candidate: %u: %s\n",__LINE__,#c); _Exit(1); } } while(0)
#define XBOX 1
#define TARGET_PAGE_SIZE 4096u
#define TARGET_PAGE_MASK (~(uint64_t)(TARGET_PAGE_SIZE-1))
typedef struct { const uint8_t *host_addr[2]; uint64_t pc_first; } DisasContextBase;
/* PRODUCTION */
int main(void)
{
 unsigned cases=0;
 uint8_t code[3]={0};DisasContextBase db={.host_addr={code,NULL},.pc_first=0x80001000};
 for(unsigned a=0;a<256;a++) for(unsigned b=0;b<256;b++) {
  code[0]=a;code[1]=b;
  bool possible=a==0xFB || a==0x90 || (a==0x3B && b==0x6D) || (a==0x8B && b==0x0A);
  assert(lr_quiet_candidate_prefix(&db)==possible);cases++;
 }
 for(unsigned v=0;v<256;v++) {
  code[0]=0x3B;code[1]=0x6D;code[2]=v;
  assert(lr_quiet_candidate_prefix(&db)==(v==0));cases++;
 }
 code[2]=0;
 static const unsigned positions[]={/* POSITIONS */};
 static const uint8_t kernel[]=/* KERNEL */;
 static const uint8_t poll[]=/* POLL */;
 for(unsigned i=0;i<sizeof(positions)/sizeof(*positions);i++) {
  db.host_addr[0]=kernel+positions[i];assert(lr_quiet_candidate_prefix(&db));cases++;
 }
 db.host_addr[0]=poll;assert(lr_quiet_candidate_prefix(&db));cases++;
 for(unsigned offset=0;offset<TARGET_PAGE_SIZE;offset++) {
  db.pc_first=0x80001000+offset;db.host_addr[0]=NULL;
  assert(lr_quiet_candidate_prefix(&db));cases++;
  db.host_addr[0]=code;code[0]=0x8B;code[1]=0x0B;
  assert(lr_quiet_candidate_prefix(&db)==(offset==TARGET_PAGE_SIZE-1));cases++;
 }
 /* The feasibility probe must not fault merely to classify code at the
  * end of a mapped page. The actual translator owns instruction fetching. */
#ifdef _WIN32
 SYSTEM_INFO info;GetSystemInfo(&info);size_t host_page_size=info.dwPageSize;
 uint8_t *pages=VirtualAlloc(NULL,2*host_page_size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
 assert(pages);DWORD previous;assert(VirtualProtect(pages+host_page_size,host_page_size,PAGE_NOACCESS,&previous));
#else
 size_t host_page_size=(size_t)sysconf(_SC_PAGESIZE);
 uint8_t *pages=mmap(NULL,2*host_page_size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
 assert(pages!=MAP_FAILED);assert(mprotect(pages+host_page_size,host_page_size,PROT_NONE)==0);
#endif
 db.host_addr[0]=pages+host_page_size-1;db.pc_first=0x80001FFF;
 pages[host_page_size-1]=0x8B;assert(lr_quiet_candidate_prefix(&db));cases++;
 pages[host_page_size-1]=0x3B;assert(lr_quiet_candidate_prefix(&db));cases++;
 db.host_addr[0]=pages+host_page_size-2;db.pc_first=0x80001FFE;
 pages[host_page_size-2]=0x3B;assert(lr_quiet_candidate_prefix(&db));cases++;
 db.host_addr[0]=pages+host_page_size-1;db.pc_first=0x80001FFF;
 pages[host_page_size-1]=0x31;assert(!lr_quiet_candidate_prefix(&db));cases++;
#ifdef _WIN32
 assert(VirtualFree(pages,0,MEM_RELEASE));
#else
 assert(munmap(pages,2*host_page_size)==0);
#endif
 printf("idle candidate prefix: %u byte/proof-entry/page-boundary/non-RAM cases passed\n",cases);
}
