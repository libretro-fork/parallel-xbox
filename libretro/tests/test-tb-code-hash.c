/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <features/features_cpu.h>
#define TARGET_PAGE_SIZE 4096u
#define TARGET_PAGE_MASK (~(uint64_t)(TARGET_PAGE_SIZE-1))
#define MO_UB 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#include <encodings/xxh3.h>
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
typedef uint64_t vaddr;
typedef int64_t tb_page_addr_t;
typedef int MemOpIdx;
typedef struct CPUArchState {
 uint8_t bytes[4][TARGET_PAGE_SIZE];
 uint8_t *mapped[4];
 bool ram[4], executable[4], subpage[4];
 bool io_enables_next;
 unsigned reads, io_reads, probes;
} CPUArchState;
typedef CPUArchState CPUState;
static CPUArchState env;
static jmp_buf fault;
static vaddr fault_pc;
static CPUState *env_cpu(CPUArchState *e){return e;}
static int cpu_mmu_index(CPUState *e,bool fetch){assert(e==&env&&fetch);return 0;}
static MemOpIdx make_memop_idx(int op,int mmu){assert(op==0&&mmu==0);return 0;}
static void executable(CPUArchState *e,vaddr pc){if(pc/4096>=4||!e->executable[pc/4096]){fault_pc=pc;longjmp(fault,1);}}
static uint32_t cpu_ldb_code_mmu(CPUArchState *e,vaddr pc,MemOpIdx op,uintptr_t ret)
{assert(op==0&&ret==0);executable(e,pc);e->reads++;if(!e->ram[pc/4096]){if(e->io_enables_next)e->executable[pc/4096+1]=true;return (pc+e->io_reads++)&255;}return e->mapped[pc/4096][pc%4096];}
static tb_page_addr_t get_page_addr_code_hostp(CPUArchState *e,vaddr pc,void **host)
{e->probes++;executable(e,pc);if(!e->ram[pc/4096]||e->subpage[pc/4096])return -1;*host=e->mapped[pc/4096]+pc%4096;return pc;}
static uint64_t test_simd;
uint64_t cpu_features_get(void){return test_simd;}
uint64_t fast_hash(const uint8_t *,size_t);
/* PRODUCTION */
static uint64_t reference(vaddr pc,size_t size)
{uint8_t code[4096];for(size_t i=0;i<size;i++)code[i]=cpu_ldb_code_mmu(&env,pc+i,0,0);return fast_hash(code,size);}
static void reset(void){env.reads=env.io_reads=env.probes=0;}
static void check(vaddr pc,size_t size)
{
 reset();uint64_t want=reference(pc,size);unsigned io=env.io_reads;
 reset();assert(tb_code_hash_func(&env,pc,size)==want);assert(env.io_reads==io);
 unsigned reads=0,probes=0;
 for(size_t off=0;off<size;){size_t n=MIN(size-off,4096-(pc+off)%4096);unsigned page=(pc+off)/4096;probes++;if(!env.ram[page]||env.subpage[page])reads+=n;off+=n;}
 assert(env.reads==reads);assert(env.probes==probes);
 /* The caller's first-page pointer must not bypass a boundary or device. */
 void *host=NULL;get_page_addr_code_hostp(&env,pc,&host);reset();
 assert(tb_code_hash_func_host(&env,pc,size,host)==want);assert(env.io_reads==io);
 assert(env.reads==reads);
 bool direct=host&&size<=4096-pc%4096;
 assert(env.probes==(direct?0:probes-(host?1:0)));
}
static uint32_t seed=0x516723ab;
static uint32_t rnd(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(int argc,char **argv)
{
#if (defined(__i386__) || defined(__x86_64__)) && defined(__GNUC__)
 __builtin_cpu_init();
 if(argc>1){
  if(!strcmp(argv[1],"sse2")&&__builtin_cpu_supports("sse2"))test_simd=RETRO_SIMD_SSE2;
  if(!strcmp(argv[1],"avx2")&&__builtin_cpu_supports("avx2"))test_simd=RETRO_SIMD_AVX2;
  if(!strcmp(argv[1],"avx512")&&__builtin_cpu_supports("avx512f")&&__builtin_cpu_supports("avx512bw"))test_simd=RETRO_SIMD_AVX512;
  if(!test_simd){printf("SKIP: %s unavailable on this host\n",argv[1]);return 0;}
 }
#else
 (void)argv;if(argc>1){puts("SKIP: forced x86 SIMD unavailable");return 0;}
#endif
 /* Deliberately nonadjacent and aliased physical pages. */
 unsigned order[]={2,0,3,2};
 for(unsigned p=0;p<4;p++){env.mapped[p]=env.bytes[order[p]];env.ram[p]=env.executable[p]=true;for(unsigned i=0;i<4096;i++)env.bytes[p][i]=rnd();}
 check(0,0);check(4096,0);
 size_t lens[]={1,2,3,7,8,15,16,17,31,32,63,64,65,127,128,129,239,240,241,255,256,511,512,1023,2048,4095};
 for(unsigned l=0;l<ARRAY_SIZE(lens);l++)for(unsigned p=0;p<3;p++){
  check(p*4096,lens[l]);check(p*4096+4096-lens[l],lens[l]);
  if(lens[l]>1)check(p*4096+4097-lens[l],lens[l]);
 }
 for(unsigned n=0;n<100000;n++){
  vaddr pc=rnd()%(3*4096);size_t size=1+rnd()%4095;unsigned page=rnd()%4;
  env.ram[page]=(rnd()&3)!=0;env.subpage[page]=(rnd()&7)==0;
  check(pc,size);env.mapped[pc/4096][pc%4096]^=0x5a;check(pc,size);
 }
 for(unsigned p=0;p<4;p++){env.ram[p]=true;env.subpage[p]=false;}
 /* A single-page probe faults at the same first instruction address. */
 env.executable[1]=false;
 if(!setjmp(fault)){(void)reference(4096+17,31);assert(false);}assert(fault_pc==4113);
 if(!setjmp(fault)){(void)tb_code_hash_func(&env,4113,31);assert(false);}assert(fault_pc==4113);
 /* Crossing into an inaccessible page keeps byte-fetch order and fault. */
 reset();if(!setjmp(fault)){(void)tb_code_hash_func(&env,4090,20);assert(false);}
 assert(fault_pc==4096&&env.reads==0&&env.probes==2);
 /* Side effects on the first page precede the next page fault. */
 env.ram[0]=false;reset();if(!setjmp(fault)){(void)tb_code_hash_func(&env,4090,20);assert(false);}
 assert(fault_pc==4096&&env.reads==6&&env.io_reads==6&&env.probes==2);
 /* Do not resolve the second page before device reads enable its mapping. */
 env.io_enables_next=true;env.executable[1]=false;reset();uint64_t want=reference(4090,20);
 env.executable[1]=false;reset();assert(tb_code_hash_func(&env,4090,20)==want);
 assert(env.io_reads==6&&env.reads==6&&env.probes==2);
 printf("path=%s ",argc>1?argv[1]:"scalar");
 puts("PASS: direct RAM hashes, page ends/crossings, aliases, MMIO read order, sub-pages, mutations and faults; 200000 randomized oracle comparisons");
 return 0;
}
