/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
static __thread bool ownership_is_worker, ownership_is_vp_worker;
static __thread unsigned ownership_cpu_depth;
static bool self=true;static unsigned loans,owner_lookups,self_checks;
struct MemoryRegionOps {int tag;};
typedef struct MemoryRegion {const struct MemoryRegionOps *ops;} MemoryRegion;
static void apu_cb(void *unused){}
static bool lr_dsp_is_tick_callback(void (*cb)(void *)){return cb==apu_cb;}
static void classified_cb_0(void *unused){}
static void classified_cb_1(void *unused){}
static void classified_cb_2(void *unused){}
static void classified_cb_3(void *unused){}
static void classified_cb_4(void *unused){}
static void classified_cb_5(void *unused){}
static void classified_cb_6(void *unused){}
static void classified_cb_7(void *unused){}
static void classified_cb_8(void *unused){}
static void classified_cb_9(void *unused){}
static void classified_cb_10(void *unused){}
static void classified_cb_11(void *unused){}
static void classified_cb_12(void *unused){}
static void classified_cb_13(void *unused){}
static void classified_cb_14(void *unused){}
static void classified_cb_15(void *unused){}
static void classified_cb_16(void *unused){}

typedef int CPUState;static CPUState cpu;
static CPUState *qemu_get_cpu(unsigned i){assert(i==0);owner_lookups++;return &cpu;}
static bool qemu_cpu_is_self(CPUState*c){assert(c==&cpu);self_checks++;return self;}
static void lr_dsp_bus_borrow(void){loans++;}
static void lr_dsp_bus_return(void){assert(loans);loans--;}
/* PRODUCTION TRANSACTIONS */
/* PRODUCTION CLEANUP */
static bool can_post(void){return !ownership_cpu_depth && !loans;}
static void nested_timer(void){assert(!can_post());}
static void second_callback(void)
{
    LR_DSP_EXTERNAL_GUARD;
    assert(ownership_cpu_depth==2);nested_timer();
}
static void cpu_transaction(void)
{
    LR_DSP_EXTERNAL_GUARD;
    assert(ownership_cpu_depth==1 && !loans);second_callback();assert(ownership_cpu_depth==1);
}
static void external_transaction(void)
{
    LR_DSP_EXTERNAL_GUARD;
    assert(loans==1 && !ownership_cpu_depth);nested_timer();
}

int main(void)
{
    for(unsigned i=0;i<2000;i++){
        unsigned before=owner_lookups;
        self=true;assert(can_post());cpu_transaction();assert(can_post());
        assert(owner_lookups==before+1 && self_checks==owner_lookups);
        self=false;external_transaction();assert(can_post());
        assert(owner_lookups==before+2 && self_checks==owner_lookups);
    }
    assert(lr_dsp_timer_guarded(apu_cb));
    lr_dsp_register_guarded_timer(classified_cb_0);
    lr_dsp_register_guarded_timer(classified_cb_1);
    lr_dsp_register_guarded_timer(classified_cb_2);
    lr_dsp_register_guarded_timer(classified_cb_3);
    lr_dsp_register_guarded_timer(classified_cb_4);
    lr_dsp_register_guarded_timer(classified_cb_5);
    lr_dsp_register_guarded_timer(classified_cb_6);
    lr_dsp_register_guarded_timer(classified_cb_7);
    lr_dsp_register_guarded_timer(classified_cb_8);
    lr_dsp_register_guarded_timer(classified_cb_9);
    lr_dsp_register_guarded_timer(classified_cb_10);
    lr_dsp_register_guarded_timer(classified_cb_11);
    lr_dsp_register_guarded_timer(classified_cb_12);
    lr_dsp_register_guarded_timer(classified_cb_13);
    lr_dsp_register_guarded_timer(classified_cb_14);
    lr_dsp_register_guarded_timer(classified_cb_15);
    lr_dsp_register_guarded_timer(classified_cb_16);

    assert(lr_dsp_timer_guarded(classified_cb_0));assert(lr_dsp_timer_guarded(classified_cb_15));assert(!lr_dsp_timer_guarded(classified_cb_16));
    lr_dsp_register_guarded_timer(classified_cb_0);assert(ownership_guarded_timer_count==16);
    struct MemoryRegionOps ops[9];
    for(unsigned i=0;i<9;i++)lr_dsp_register_guarded_mmio_ops(&ops[i]);
    MemoryRegion known={&ops[0]},alias={&ops[0]},unknown={&ops[8]};
    assert(lr_dsp_mmio_guarded(&known) && lr_dsp_mmio_guarded(&alias) && !lr_dsp_mmio_guarded(&unknown));
    lr_dsp_register_guarded_mmio_ops(&ops[0]);assert(ownership_guarded_mmio_count==8);
    self=true;int token=lr_dsp_timer_begin(apu_cb);assert(token==0 && !ownership_cpu_depth);
    token=lr_dsp_timer_begin(classified_cb_0);assert(token==2 && ownership_cpu_depth==1);lr_dsp_external_cleanup(&token);assert(!ownership_cpu_depth);
    ownership_is_vp_worker=true;assert(lr_dsp_external_begin()==0 && !loans && !ownership_cpu_depth);ownership_is_vp_worker=false;
    ownership_is_worker=true;assert(lr_dsp_external_begin()==0 && !loans && !ownership_cpu_depth);
    puts("PASS: production transaction guard suppresses nested DSP posting through 2000 callback chains, retains external leases and releases both at scope exit; worker never self-borrows");
}
