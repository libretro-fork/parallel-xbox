/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define assert(x) do {if(!(x)){fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);_Exit(1);}}while(0)
#define TARGET_PAGE_SIZE 4096u
#define TARGET_PAGE_MASK (~(TARGET_PAGE_SIZE-1u))
enum { R_EAX, R_ECX, R_EDX, R_EBX, R_ESP, R_EBP, R_ESI, R_EDI };
enum { R_CS, R_DS };
#define HF_CPL_MASK 3u
#define HF_INHIBIT_IRQ_MASK 8u
#define HF_CS32_MASK 16u
#define HF_CS64_MASK 32768u
#define IF_MASK 0x200u
#define TF_MASK 0x100u
#define VM_MASK 0x20000u
typedef uint64_t hwaddr;
typedef struct {unsigned asidx;} MemTxAttrs;
typedef struct {uint8_t *ptr;bool ram,device;} MemoryRegion;
typedef struct {uint32_t eip,hflags,eflags,regs[8];struct {uint64_t base;uint32_t limit;} segs[2];} CPUX86State;
typedef struct {CPUX86State env;unsigned singlestep_enabled;} CPUState;
static CPUX86State *cpu_env(CPUState *cpu){return &cpu->env;}
static unsigned rcu_depth,probes,translations;
static uint32_t code_va,data_va;
static uint8_t code[10],data[4];
static unsigned variant;
static bool code_fault,data_fault,code_device,data_device,short_code,short_data;
static void rcu_end(unsigned *p){(void)p;assert(rcu_depth);rcu_depth--;}
#define RCU_READ_LOCK_GUARD() unsigned guard __attribute__((cleanup(rcu_end)))=++rcu_depth
static void cpu_synchronize_state(CPUState *cpu){(void)cpu;assert(rcu_depth);}
static hwaddr cpu_get_phys_page_attrs_debug(CPUState *cpu,uint32_t page,MemTxAttrs *attrs){
    (void)cpu;assert(rcu_depth);probes++;attrs->asidx=1;
    if((page==(code_va&TARGET_PAGE_MASK)&&code_fault)||(page==(data_va&TARGET_PAGE_MASK)&&data_fault))return (hwaddr)-1;
    return page;
}
static int cpu_asidx_from_attrs(CPUState *cpu,MemTxAttrs attrs){(void)cpu;return attrs.asidx;}
static void *cpu_get_address_space(CPUState *cpu,int asidx){(void)cpu;assert(asidx==1);return (void*)1;}
static MemoryRegion *address_space_translate(void *as,hwaddr addr,hwaddr *offset,hwaddr *len,bool write,MemTxAttrs attrs){
    static MemoryRegion region;assert(rcu_depth&&as==(void*)1&&!write&&attrs.asidx==1);translations++;
    *offset=0;region=(MemoryRegion){.ram=true};
    if(addr==code_va){region.ptr=code;region.device=code_device;if(short_code)(*len)--;}
    else if(addr==data_va){region.ptr=data;region.device=data_device;if(short_data)(*len)--;}
    else region.ram=false;
    return &region;
}
static bool memory_region_is_ram(MemoryRegion *r){return r->ram;}
static bool memory_region_is_ram_device(MemoryRegion *r){return r->device;}
#define BP_MEM_READ 1
static uint64_t memory_region_get_ram_addr(MemoryRegion*r){assert(rcu_depth && r->ram && !r->device);return 0;}
static void mem_check_access_callback_ramaddr(CPUState*cpu,uint64_t addr,uint64_t len,int flags){(void)cpu;(void)addr;assert(rcu_depth && len && flags==BP_MEM_READ);}
static void *memory_region_get_ram_ptr(MemoryRegion *r){assert(rcu_depth&&r->ram&&!r->device);return r->ptr;}
static uint32_t ldl_le_p(const uint8_t *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(uint32_t value){for(unsigned i=0;i<4;i++)data[i]=value>>(i*8);}
#define CF_USE_ICOUNT 1u
#define CF_NOIRQ 2u
typedef struct {uint32_t cflags;} TestTB;
typedef struct {TestTB *tb;const uint8_t *host_addr[2];uint64_t pc_first;} DisasContextBase;
static uint32_t tb_cflags(TestTB *tb){return tb->cflags;}
/* PRODUCTION */
static bool nominated(void){TestTB tb={CF_USE_ICOUNT};DisasContextBase db={.tb=&tb,.host_addr={code,NULL},.pc_first=code_va};return xemu_ram_wait_candidate(&db);}
static void setup(CPUState *cpu){
    static const uint8_t loop[]={0x8b,0x0a,0x8b,0xfe,0x2b,0xf9,0x3b,0xc7,0x72,0xf6};
    memset(cpu,0,sizeof(*cpu));memcpy(code,loop,sizeof(code));
    if(variant){code[3]=0xf7;code[5]=0xf1;code[7]=0xc6;}
    code_va=0x003cab40;data_va=0x8196b000;
    cpu->env.segs[R_CS].limit=cpu->env.segs[R_DS].limit=UINT32_MAX;
    cpu->env.eip=code_va;cpu->env.hflags=HF_CS32_MASK;cpu->env.eflags=IF_MASK;
    cpu->env.regs[R_EDX]=data_va;cpu->env.regs[R_ESI]=variant?0xdeadbeef:40;cpu->env.regs[R_EDI]=variant?40:0xdeadbeef;cpu->env.regs[R_EAX]=36;put(0);
    probes=translations=0;code_fault=data_fault=code_device=data_device=short_code=short_data=false;
}
int main(void){
    CPUState cpu;unsigned cases=0;uint32_t seed=1;
    for(variant=0;variant<2;variant++){
    unsigned base_reg=variant?R_EDI:R_ESI;
    for(unsigned i=0;i<50000;i++){
        setup(&cpu);seed=seed*1664525u+1013904223u;uint32_t value=seed;
        cpu.env.regs[base_reg]=seed*97u;cpu.env.regs[variant?R_ESI:R_EDI]=~(seed*97u);cpu.env.regs[R_EAX]=seed>>2;put(value);
        /* The proof is independent of title addresses and accepts unaligned
         * ordinary RAM, with the same little-endian arithmetic as the CPU. */
        code_va=0x100000u+((seed>>16)&0x7ff)*16;cpu.env.eip=code_va;
        data_va=0x80030000u+(seed&15);cpu.env.regs[R_EDX]=data_va;
        CPUState before=cpu;uint8_t old_code[10],old_data[4];memcpy(old_code,code,10);memcpy(old_data,data,4);
        uint64_t difference=((uint64_t)cpu.env.regs[base_reg]+(1ull<<32)-value)&0xffffffffu;
        bool expected=(uint64_t)cpu.env.regs[R_EAX]<difference;
        assert(nominated());assert(xemu_libretro_memory_poll(&cpu)==expected);
        assert(!memcmp(&cpu,&before,sizeof(cpu))&&!memcmp(code,old_code,10)&&!memcmp(data,old_data,4));
        assert(!rcu_depth&&probes==2&&translations==2);cases++;
    }
    for(unsigned byte=0;byte<10;byte++)for(unsigned v=0;v<256;v++){
        setup(&cpu);uint8_t original=code[byte];code[byte]=v;
        assert(nominated()==(v==original));assert(xemu_libretro_memory_poll(&cpu)==(v==original));cases++;
    }
    setup(&cpu);assert(xemu_libretro_memory_poll(&cpu));put(4);assert(!xemu_libretro_memory_poll(&cpu));put(0);assert(xemu_libretro_memory_poll(&cpu));
    code[9]=0xf5;assert(!xemu_libretro_memory_poll(&cpu));code[9]=0xf6;assert(xemu_libretro_memory_poll(&cpu));
    for(unsigned kind=0;kind<12;kind++){
        setup(&cpu);
        switch(kind){
        case 0:cpu.env.eflags&=~IF_MASK;break;
        case 1:cpu.env.eflags|=TF_MASK;break;
        case 2:cpu.env.eflags|=VM_MASK;break;
        case 3:cpu.env.hflags|=HF_INHIBIT_IRQ_MASK;break;
        case 4:cpu.env.hflags&=~HF_CS32_MASK;break;
        case 5:cpu.env.hflags|=HF_CS64_MASK;break;
        case 6:cpu.env.hflags|=3;break;
        case 7:cpu.env.segs[R_CS].base=1;break;
        case 8:cpu.env.segs[R_DS].base=1;break;
        case 9:cpu.singlestep_enabled=1;break;
        case 10:cpu.env.segs[R_CS].limit=code_va+8;break;
        case 11:cpu.env.segs[R_DS].limit=data_va+2;break;
        }
        assert(!xemu_libretro_memory_poll(&cpu)&&!probes&&!translations);cases++;
    }
    for(unsigned kind=0;kind<8;kind++){
        setup(&cpu);
        switch(kind){
        case 0:code_fault=true;break;case 1:data_fault=true;break;
        case 2:code_device=true;break;case 3:data_device=true;break;
        case 4:short_code=true;break;case 5:short_data=true;break;
        case 6:code_va=0x003caff7;cpu.env.eip=code_va;break;
        case 7:data_va=0x80030ffd;cpu.env.regs[R_EDX]=data_va;break;
        }
        assert(!xemu_libretro_memory_poll(&cpu)&&!rcu_depth);cases++;
    }
    setup(&cpu);TestTB tb={0};DisasContextBase db={.tb=&tb,.host_addr={code,NULL},.pc_first=code_va};
    for(unsigned flags=0;flags<8;flags++){
        tb.cflags=flags;assert(xemu_ram_wait_candidate(&db)==((flags&(CF_USE_ICOUNT|CF_NOIRQ))==CF_USE_ICOUNT));cases++;
    }
    tb.cflags=CF_USE_ICOUNT;db.pc_first=0x1ff6;assert(xemu_ram_wait_candidate(&db));
    db.pc_first=0x1ff7;db.host_addr[0]=(const uint8_t*)1;assert(!xemu_ram_wait_candidate(&db));
    db.pc_first=code_va;db.host_addr[0]=NULL;assert(!xemu_ram_wait_candidate(&db));cases+=3;
    }
    printf("RAM poll regression: %u arithmetic/opcode/interrupt/mapping cases, counter release and code changes passed; no MMIO reads, state changes or byte snapshots\n",cases);
}
