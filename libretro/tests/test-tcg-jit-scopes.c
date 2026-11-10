/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
#define CF_INVALID 4u
typedef uint64_t tb_page_addr_t;
typedef struct TranslationBlock {tb_page_addr_t page;uint32_t cflags,codeword;} TranslationBlock;
static TranslationBlock *allocation;
static bool writable,pages_locked,expected_remove,expected_reusable,no_op;
static bool *expected_owed;
static unsigned writes,executes,locks,unlocks,invalidations;
static void protection(bool write) {
#ifdef _WIN32
    DWORD previous;
    assert(VirtualProtect(allocation,4096,write?PAGE_READWRITE:PAGE_EXECUTE_READ,&previous));
#else
    assert(!mprotect(allocation,4096,PROT_READ|(write?PROT_WRITE:PROT_EXEC)));
#endif
    writable=write;
}
static void qemu_thread_jit_write(void) {writes++;if(!writable)protection(true);}
static void qemu_thread_jit_execute(void) {executes++;if(writable)protection(false);}
static tb_page_addr_t tb_page_addr0(TranslationBlock *tb) {return tb->page;}
static void tb_lock_pages(TranslationBlock *tb) {assert(!pages_locked);pages_locked=true;locks++;}
static void tb_unlock_pages(TranslationBlock *tb) {assert(pages_locked);pages_locked=false;unlocks++;}
static void do_tb_phys_invalidate(TranslationBlock *tb,bool remove,bool reusable,bool *owed) {
    assert(remove==expected_remove&&reusable==expected_reusable&&owed==expected_owed);
    invalidations++;
    if(no_op)return;
    /* These are real stores into an RX mapping when the scope is missing,
     * representing metadata updates and a native jump instruction patch. */
    tb->cflags|=CF_INVALID;tb->codeword^=0x40;
    assert(writable);
    if(owed)*owed=true;
}
/* PRODUCTION */
int main(void) {
#ifdef _WIN32
    allocation=VirtualAlloc(NULL,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(allocation);
#else
    allocation=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(allocation!=MAP_FAILED);
#endif
    unsigned cases=0;
    for(unsigned initial=0;initial<2;initial++)for(unsigned mapped=0;mapped<2;mapped++)
    for(unsigned explicit_page=0;explicit_page<2;explicit_page++)for(unsigned absent=0;absent<2;absent++)
    for(unsigned kind=0;kind<3;kind++) {
        protection(true);*allocation=(TranslationBlock){.page=mapped?0x1000:UINT64_MAX,.codeword=0x14000002};
        protection(initial);
        bool owed=false;expected_owed=kind==2?&owed:NULL;
        expected_reusable=kind!=1;
        expected_remove=kind==2||(mapped&&(kind==1||!explicit_page));
        no_op=absent;pages_locked=kind==2;
        writes=executes=locks=unlocks=invalidations=0;
        if(kind==0)tb_phys_invalidate(allocation,explicit_page?0x1000:UINT64_MAX);
        else if(kind==1)tb_phys_invalidate_recompile(allocation);
        else tb_phys_invalidate__locked(allocation,&owed);
        assert(!writable&&writes==1&&executes==1&&invalidations==1);
        assert(locks==(unsigned)(kind!=2&&expected_remove)&&locks==unlocks);
        assert(pages_locked==(kind==2));
        assert(allocation->cflags==(absent?0:CF_INVALID));
        assert(allocation->codeword==(absent?0x14000002u:0x14000042u));
        assert(owed==(bool)(kind==2&&!absent));cases++;
    }
#ifdef _WIN32
    assert(VirtualFree(allocation,0,MEM_RELEASE));
#else
    assert(!munmap(allocation,4096));
#endif
    printf("PASS: %u production watchpoint/I-O/range invalidation scope cases; protected metadata/code, no-op removals, mapped/MMIO pages and execute-mode restoration\n",cases);
}
