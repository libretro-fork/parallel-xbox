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
static uint32_t list, pending;
static bool code_fault, list_fault, pending_fault;
static unsigned reads;
static uint32_t ldl_le_p(const uint8_t *p) {
    return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static void put_word(uint8_t *p, uint32_t value) {
    for (unsigned i=0;i<4;i++) p[i] = value >> (8*i);
}
static int cpu_memory_rw_debug(CPUState *cpu, uint32_t addr, uint8_t *p, size_t n, bool write) {
    (void)cpu; assert(!write); reads++;
    if (addr >= 0x8000fffcU && (uint64_t)addr+n <= 0x80010024ULL && !code_fault) {
        memcpy(p, code + addr-0x8000fffcU, n); return 0;
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
    cpu->env.eip=0x80010000; cpu->env.regs[R_EBP]=0x80030000;cpu->env.regs[R_EBX]=0x80040000;
    list=0x80030000;pending=0;code_fault=list_fault=pending_fault=false;reads=0;
}
int main(void) {
    CPUState cpu;
    for (unsigned dispatch=0;dispatch<2;dispatch++) {
        unsigned positions[]={0,1,4};
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
    puts("kernel idle recognition: both loops/exit positions, pending work, privilege/memory guards, live code mutation and all opcode-bit mutations passed");
    return 0;
}
