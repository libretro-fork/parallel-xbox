/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define R_CS 0
#define R_EBP 5
#define R_EBX 3
#define HF_CPL_MASK 3
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
typedef struct { uint64_t base; } Segment;
typedef struct { uint32_t eip, hflags, regs[8]; Segment segs[1]; } CPUX86State;
typedef struct { CPUX86State env; } CPUState;
static CPUX86State *cpu_env(CPUState *cpu) { return &cpu->env; }
static uint8_t code[40];
static uint32_t code_base=0x800100fcU;
static uint32_t list, pending;
static bool code_fault, list_fault, pending_fault;
static unsigned reads;
typedef uint64_t hwaddr;
#define TARGET_PAGE_SIZE 4096U
#define TARGET_PAGE_MASK (~4095U)
typedef struct { unsigned asidx; } MemTxAttrs;
typedef struct { uint8_t *ptr; bool ram, device; } MemoryRegion;
static unsigned rcu_depth, probes, copied_bytes;
static unsigned default_fallback;
static bool force_fallback, ram_device, short_region, mapping_fault;
static void rcu_end(unsigned *guard) { (void)guard; assert(rcu_depth); rcu_depth--; }
#define RCU_READ_LOCK_GUARD() unsigned guard __attribute__((cleanup(rcu_end))) = ++rcu_depth
static void cpu_synchronize_state(CPUState *cpu) { (void)cpu; assert(rcu_depth); }
static hwaddr cpu_get_phys_page_attrs_debug(CPUState *cpu, uint32_t page, MemTxAttrs *attrs) {
    (void)cpu; assert(rcu_depth); probes++; attrs->asidx=1;
    if(mapping_fault || ((page== (code_base & TARGET_PAGE_MASK) || page== ((code_base+sizeof(code)-1) & TARGET_PAGE_MASK)) && code_fault) ||
       (page==0x80030000U && list_fault) || (page==0x80040000U && pending_fault)) return (hwaddr)-1;
    return page;
}
static int cpu_asidx_from_attrs(CPUState *cpu, MemTxAttrs attrs) { (void)cpu; return attrs.asidx; }
static void *cpu_get_address_space(CPUState *cpu, int asidx) { (void)cpu;assert(asidx==1);return (void *)1; }
static MemoryRegion *address_space_translate(void *as, hwaddr addr, hwaddr *offset, hwaddr *len, bool write, MemTxAttrs attrs) {
    static MemoryRegion region;
    assert(rcu_depth && as==(void *)1 && attrs.asidx==1 && !write);
    region=(MemoryRegion){.ram=!force_fallback,.device=ram_device}; *offset=0;
    if(addr>=code_base && addr<(uint64_t)code_base+sizeof(code)) {
        region.ptr=code;*offset=addr-code_base;
        if(*len>sizeof(code)-*offset) *len=sizeof(code)-*offset;
    } else if(addr==0x80030000U) region.ptr=(uint8_t *)&list;
    else if(addr==0x8004002cU) region.ptr=(uint8_t *)&pending;
    else region.ram=false;
    if(short_region && *len) (*len)--;
    return &region;
}
static bool memory_region_is_ram(MemoryRegion *mr) { return mr->ram; }
static bool memory_region_is_ram_device(MemoryRegion *mr) { return mr->device; }
static void *memory_region_get_ram_ptr(MemoryRegion *mr) { assert(rcu_depth && mr->ram && !mr->device);return mr->ptr; }
static uint32_t ldl_le_p(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put_word(uint8_t *p, uint32_t value) {
    for (unsigned i=0;i<4;i++) p[i] = value >> (8*i);
}
static int cpu_memory_rw_debug(CPUState *cpu, uint32_t addr, uint8_t *p, size_t n, bool write) {
    (void)cpu; assert(rcu_depth && !write); reads++;copied_bytes+=n;
    if (addr >= code_base && (uint64_t)addr+n <= (uint64_t)code_base+sizeof(code) && !code_fault) {
        memcpy(p, code + addr-code_base, n); return 0;
    }
    if (addr==0x80030000U && n==4 && !list_fault) { put_word(p,list); return 0; }
    if (addr==0x8004002cU && n==4 && !pending_fault) { put_word(p,pending); return 0; }
    return -1;
}
/* PRODUCTION */
static void setup(CPUState *cpu, bool dispatch) {
    static const uint8_t short_loop[] = {0xfb,0x90,0x90,0xfa,0x3b,0x6d,0,0x74,0xf7};
    static const uint8_t dispatch_loop[] = {0xfb,0x90,0x90,0xfa,0x3b,0x6d,0,0x74,0x0c,0xb1,2,0xe8,1,2,3,4,0xe8,5,6,7,8,0x83,0x7b,0x2c,0,0x74,0xe5};
    memset(cpu,0,sizeof(*cpu)); memset(code,0xcc,sizeof(code));
    memcpy(code+4,dispatch?dispatch_loop:short_loop,dispatch?sizeof(dispatch_loop):sizeof(short_loop));
    code_base=0x800100fcU;cpu->env.eip=code_base+4; cpu->env.regs[R_EBP]=0x80030000;cpu->env.regs[R_EBX]=0x80040000;
    list=0x80030000;pending=0;code_fault=list_fault=pending_fault=false;reads=probes=copied_bytes=0;force_fallback=default_fallback;ram_device=short_region=mapping_fault=false;
}
int main(void) {
    CPUState cpu;
    for (unsigned mode=0;mode<2;mode++) for (unsigned dispatch=0;dispatch<2;dispatch++) {
        default_fallback=mode;unsigned positions[]={0,1,4};
        for(unsigned j=0;j<3;j++) { setup(&cpu,dispatch);cpu.env.eip+=positions[j];assert(xemu_libretro_kernel_idle(&cpu)); }
        setup(&cpu,dispatch);list+=4;assert(!xemu_libretro_kernel_idle(&cpu));
        setup(&cpu,dispatch);list_fault=true;assert(!xemu_libretro_kernel_idle(&cpu));
        setup(&cpu,dispatch);code_fault=true;assert(!xemu_libretro_kernel_idle(&cpu));
        setup(&cpu,dispatch);cpu.env.hflags=3;assert(!xemu_libretro_kernel_idle(&cpu));assert(!reads);
        setup(&cpu,dispatch);cpu.env.segs[R_CS].base=1;assert(!xemu_libretro_kernel_idle(&cpu));assert(!reads);
        setup(&cpu,dispatch);cpu.env.eip=0x10000;assert(!xemu_libretro_kernel_idle(&cpu));assert(!reads);
        setup(&cpu,dispatch);cpu.env.eip=0x87fffff0;assert(!xemu_libretro_kernel_idle(&cpu));assert(!reads);
        setup(&cpu,dispatch);cpu.env.regs[R_EBP]=0x10000;assert(!xemu_libretro_kernel_idle(&cpu));
        setup(&cpu,dispatch);assert(xemu_libretro_kernel_idle(&cpu));code[4]=0x90;assert(!xemu_libretro_kernel_idle(&cpu));
        unsigned n=dispatch?27:9;
        for (unsigned i=0;i<n;i++) for(unsigned bit=0;bit<8;bit++) {
            setup(&cpu,dispatch);code[4+i]^=1U<<bit;
            bool call_offset=dispatch&&((i>=12&&i<16)||(i>=17&&i<21));
            assert(xemu_libretro_kernel_idle(&cpu)==call_offset);
        }
    }
    setup(&cpu,true);pending=1;assert(!xemu_libretro_kernel_idle(&cpu));
    setup(&cpu,true);pending_fault=true;assert(!xemu_libretro_kernel_idle(&cpu));
    setup(&cpu,true);cpu.env.regs[R_EBX]=0xfffffff0U;assert(!xemu_libretro_kernel_idle(&cpu));
    setup(&cpu,true);cpu.env.regs[R_EBX]=0x10000;assert(!xemu_libretro_kernel_idle(&cpu));
    default_fallback=0;
    setup(&cpu,true);assert(xemu_libretro_kernel_idle(&cpu));assert(!reads && !copied_bytes && probes==3 && !rcu_depth);
    setup(&cpu,true);cpu.env.eip+=4;assert(xemu_libretro_kernel_idle(&cpu));assert(!reads && probes==3 && !rcu_depth);
    setup(&cpu,false);cpu.env.eip+=2;assert(!xemu_libretro_kernel_idle(&cpu));assert(!rcu_depth);
    for(unsigned kind=0;kind<4;kind++) {
        setup(&cpu,true);
        if(kind==0) force_fallback=true;
        if(kind==1) ram_device=true;
        if(kind==2) short_region=true;
        if(kind==3) mapping_fault=true;
        assert(xemu_libretro_kernel_idle(&cpu));assert(reads && !rcu_depth);
    }
    /* A page-crossing borrow must fail before translating anything. */
    assert(!lr_idle_ram_ptr(&cpu,0x80010fffU,2));assert(!rcu_depth);
    assert(!lr_idle_ram_ptr(&cpu,0x80010100U,0));assert(!rcu_depth);
    /* The wider code window crosses a page, but each original candidate
     * read may still succeed. Retain that legacy fallback decision. */
    for(unsigned dispatch=0;dispatch<2;dispatch++) {
        for(unsigned pos=0;pos<=4;pos++) {
            setup(&cpu,dispatch);code_base=0x8000fffcU;cpu.env.eip=code_base+4+pos;
            force_fallback=true;bool expected=xemu_libretro_kernel_idle(&cpu);
            force_fallback=false;reads=0;assert(xemu_libretro_kernel_idle(&cpu)==expected);
            assert(!rcu_depth);
            if(pos==0) assert(reads==1);
        }
    }
    /* Compare exact decisions after live byte and work-list changes. */
    uint32_t seed=0x9136fcaU;
    for(unsigned n=0;n<20000;n++) {
        setup(&cpu,n&1);
        seed=seed*1664525U+1013904223U;
        cpu.env.eip+=(seed>>28)%5;
        if(n%3) code[(seed>>8)%sizeof(code)]^=1U<<(seed&7);
        if(n%7==0) list+=4;
        if(n%11==0) pending=1;
        if(n%13==0) code_fault=true;
        if(n%17==0) list_fault=true;
        if(n%19==0) pending_fault=true;
        force_fallback=true;bool expected=xemu_libretro_kernel_idle(&cpu);assert(!rcu_depth);
        force_fallback=false;assert(xemu_libretro_kernel_idle(&cpu)==expected);assert(!rcu_depth);
    }
    puts("kernel idle recognition: both loops/exit positions, pending work, privilege/memory guards, live code mutation and all opcode-bit mutations passed");
    return 0;
}
