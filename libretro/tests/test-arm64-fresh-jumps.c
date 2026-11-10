/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#define CONFIG_DEBUG_TCG 1
#include "tcg/tcg.c"
#include "tcg/tcg-common.c"
unsigned cpuinfo;
void *g_malloc(size_t n) { abort(); }
G_NORETURN void g_assertion_message_expr(const char *domain,const char *file,int line,const char *func,const char *expr)
{ fprintf(stderr,"%s:%d %s: %s\n",file,line,func,expr); abort(); }
static unsigned flushes;
void flush_idcache_range(uintptr_t rx,uintptr_t rw,size_t len)
{ flushes++; __builtin___clear_cache((char *)rx,(char *)rx+len); }
#include "reset.h"
typedef struct {
    TranslationBlock tb;
    uint32_t code[1024] QEMU_ALIGNED(4096);
} Memory;
#define memory (*rw)
typedef unsigned (*Run)(void);
int main(void)
{
    unsigned cases=0;
    for(unsigned split=0;split<2;split++) {
    int fd=memfd_create("tcg-jump-test",0); assert(fd>=0);
    assert(!ftruncate(fd,sizeof(Memory)));
    Memory *rw=mmap(NULL,sizeof(Memory),PROT_READ|PROT_WRITE|(split?0:PROT_EXEC),MAP_SHARED,fd,0);
    assert(rw!=MAP_FAILED);
    Memory *rx=split?mmap(NULL,sizeof(Memory),PROT_READ|PROT_EXEC,MAP_SHARED,fd,0):rw;
    assert(rx!=MAP_FAILED); close(fd);
    tcg_splitwx_diff=(uintptr_t)rx-(uintptr_t)rw;
    for(unsigned bti=0;bti<2;bti++) for(unsigned slot=0;slot<2;slot++)
    for(unsigned pad=0;pad<64;pad++) for(unsigned recycled=0;recycled<2;recycled++) {
        TCGContext s={0}; TranslationBlock *tb=&memory.tb;
        memset(&memory,0,sizeof(memory)); cpuinfo=bti?CPUINFO_BTI:0;
        tb->tc.ptr=rx->code; s.gen_tb=tb;
        s.code_buf=(void *)memory.code; s.code_ptr=(void *)(memory.code+pad);
        tcg_out_goto_tb(&s,slot);
        unsigned reset=tb->jmp_reset_offset[slot]/4;
        /* Return 17 through the reset target and 29 through a linked target. */
        tcg_out32(&s,0x52800220); tcg_out32(&s,0xd65f03c0);
        unsigned linked=tcg_current_code_size(&s)/4;
        tcg_out32(&s,0x528003a0); tcg_out32(&s,0xd65f03c0);
        assert(tb->jmp_reset_offset[slot]==tb->jmp_insn_offset[slot]+8);
        assert(memory.code[pad]==0x14000002);
        flush_idcache_range((uintptr_t)rx->code,(uintptr_t)memory.code,sizeof(memory.code));
        Run run=(Run)(rx->code+pad);
        if(recycled) {
            tb_set_jmp_target(tb,slot,(uintptr_t)(rx->code+linked));
            assert(run()==29);
        }
        flushes=0;
        tb_init_jump(tb,slot,recycled);
        assert(flushes==recycled);
        assert(memory.code[pad]==0x14000002);
        assert(tb->jmp_target_addr[slot]==(uintptr_t)(rx->code+reset));
        assert(run()==17);
        /* The fresh path is byte-identical to the former reset patch. */
        uint32_t before=memory.code[pad];
        tb_reset_jump(tb,slot);
        assert(memory.code[pad]==before && run()==17);
        cases++;
    }
    if(split) assert(!munmap(rx,sizeof(Memory)));
    assert(!munmap(rw,sizeof(Memory)));
    }
    printf("PASS: %u actual ARM64 fresh/recycled jump executions; both slots, BTI, offsets, split W^X and byte-identical legacy resets\n",cases);
}
