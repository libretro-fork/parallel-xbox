/* SPDX-License-Identifier: GPL-2.0-or-later */
int dspdiff_main(int argc, char **argv);
#define main dspdiff_main
#include "dspdiff.c"
#undef main
#include "hw/xbox/mcpx/apu/dsp/interp/dsp_cpu_regs.h"
static unsigned reads, writes;
static uint32_t last_addr, last_value;
static uint32_t y_read(dsp_core_t *c, uint32_t addr)
{ assert(c && addr>=DSP_PERIPH_BASE);reads++;last_addr=addr;return addr^0x123456; }
static void y_write(dsp_core_t *c,uint32_t addr,uint32_t value)
{ assert(c && addr>=DSP_PERIPH_BASE);writes++;last_addr=addr;last_value=value; }
static void dispose(Machine *m)
{ m->dsp->ops->finalize(m->dsp);free(m->dsp);free(m); }
int main(void)
{
    uint64_t cases=0;unsigned programs=0;
    for(unsigned jit=0;jit<2;jit++) {
        Machine *m=machine_new(jit);dsp_core_t *c=m->dsp->backend;
        dsp_core_t *before=malloc(sizeof(*before));assert(before);
        c->read_y_peripheral=y_read;c->write_y_peripheral=y_write;
        for(uint32_t addr=0;addr<DSP_YRAM_SIZE;addr++) {
            uint32_t value=(addr*0x12345)&0xffffff;
            dsp56k_write_memory(c,DSP_SPACE_Y,addr,value);
            assert(dsp56k_read_memory(c,DSP_SPACE_Y,addr)==value);cases++;
        }
        memcpy(before,c,sizeof(*c));reads=writes=0;
        for(uint32_t addr=DSP_YRAM_SIZE;addr<DSP_PERIPH_BASE;addr++) {
            dsp56k_write_memory(c,DSP_SPACE_Y,addr,(addr*7919)&0xffffff);
            assert(dsp56k_read_memory(c,DSP_SPACE_Y,addr)==0);cases++;
        }
        assert(!reads && !writes && !memcmp(before,c,sizeof(*c)));
        uint32_t words[4];
        dsp56k_read_memory_words(c,DSP_SPACE_Y,DSP_YRAM_SIZE-1,words,4);
        assert(words[0]==c->yram[DSP_YRAM_SIZE-1] && !words[1] && !words[2] && !words[3]);
        assert(!dsp56k_memory_ptr(c,DSP_SPACE_Y,DSP_YRAM_SIZE,1,false));
        assert(!dsp56k_memory_ptr(c,DSP_SPACE_Y,DSP_YRAM_SIZE-1,2,true));
        for(uint32_t addr=DSP_PERIPH_BASE;addr<0x1000000;addr++) {
            dsp56k_write_memory(c,DSP_SPACE_Y,addr,0xabcdef);
            assert(last_addr==addr && last_value==0xabcdef);
            assert(dsp56k_read_memory(c,DSP_SPACE_Y,addr)==(addr^0x123456));cases++;
        }
        assert(reads==128 && writes==128);
        c->read_y_peripheral=NULL;c->write_y_peripheral=NULL;
        for(uint32_t addr=DSP_PERIPH_BASE;addr<0x1000000;addr++) {
            dsp56k_write_memory(c,DSP_SPACE_Y,addr,0xffffff);
            assert(dsp56k_read_memory(c,DSP_SPACE_Y,addr)==0xffffff);cases++;
        }
        free(before);dispose(m);
        /* This core's indirect address registers are 16-bit. The API sweep
         * above separately covers all 24-bit addresses and Y peripherals. */
        const uint32_t addresses[]={0x7ff,0x800,0xbff,0xc00,0x1000,0xff7f,0xff80,0xffff};
        for(unsigned i=0;i<ARRAY_SIZE(addresses);i++) {
            m=machine_new(jit);c=m->dsp->backend;
            c->read_y_peripheral=y_read;c->write_y_peripheral=y_write;
            uint32_t program[]={0x64f400,addresses[i],0x44f400,0xabcdef,
                                0x4c6400,0x4fe400,0x08f484,1};
            memcpy(c->pram,program,sizeof(program));
            uint32_t saved[DSP_PRAM_SIZE];memcpy(saved,c->pram,sizeof(saved));
            reads=writes=0;c->pc=0;c->is_idle=false;
            dspjit_test_reset_entry_count();
            m->dsp->ops->run(m->dsp,10000);
            assert(c->is_idle && !memcmp(saved,c->pram,sizeof(saved)));
            uint32_t addr=addresses[i];
            uint32_t expected=addr<DSP_YRAM_SIZE?0xabcdef:addr<DSP_PERIPH_BASE?0:addr^0x123456;
            if(c->registers[DSP_REG_Y1]!=expected) {
                fprintf(stderr,"Y program jit=%u addr=%x actual=%x expected=%x r4=%x pc=%x\n",jit,addr,c->registers[DSP_REG_Y1],expected,c->registers[DSP_REG_R4],c->pc);
                abort();
            }
            assert(reads==(addr>=DSP_PERIPH_BASE) && writes==(addr>=DSP_PERIPH_BASE));
            if(addr<DSP_YRAM_SIZE)assert(c->yram[addr]==0xabcdef);
            if(jit)assert(dspjit_test_entry_count()>0);
            programs++;dispose(m);
        }
    }
    printf("DSP Y memory: %llu full-address read/write cases; %u interpreter/native-JIT programs; peripherals, whole-state preservation and bulk boundaries PASS\n",(unsigned long long)cases,programs);
    return 0;
}
