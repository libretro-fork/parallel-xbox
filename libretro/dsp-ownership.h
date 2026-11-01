/* EP job ownership shared by CPU, device and renderer access paths. */
#ifndef LR_DSP_OWNERSHIP_H
#define LR_DSP_OWNERSHIP_H
#include "qemu/typedefs.h"
#include "exec/hwaddr.h"
#define LR_DSP_RAM_PAGES 32768
bool lr_dsp_ownership_match(MemoryRegion *mr, hwaddr addr, hwaddr len);
void lr_dsp_ownership_callback(void *opaque, MemoryRegion *mr, hwaddr addr, hwaddr len, bool write);
void lr_dsp_bus_init(void);
void lr_dsp_bus_borrow(void);
void lr_dsp_bus_return(void);
void lr_gpu_ram_claim(uintptr_t cmd, uint64_t addr, uint64_t len);
void lr_gpu_ram_drop(uintptr_t cmd);
void lr_dsp_ownership_quiesce(void);
void lr_dsp_ownership_stop(void);
void lr_dsp_ownership_resume(void);
int lr_dsp_external_begin(void);
/* VP workers are joined before the CPU publishes another DSP epoch. */
void lr_dsp_vp_worker(bool active);
void lr_dsp_cpu_transaction_end(void);
void lr_dsp_ownership_ram(uint64_t addr, uint64_t len);
void lr_dsp_ownership_read(uint64_t addr, uint64_t len);
static inline void lr_dsp_external_cleanup(int *held) { if(*held==1) lr_dsp_bus_return(); else if(*held==2) lr_dsp_cpu_transaction_end(); }
#define LR_DSP_EXTERNAL_GUARD int lr_dsp_external_loan __attribute__((cleanup(lr_dsp_external_cleanup))) = lr_dsp_external_begin()
bool lr_dsp_tlb_watched(uint64_t ram_addr);
void lr_dsp_tlb_refresh(CPUState *cpu, bool arm);
enum { LR_DSP_FENCE_GENERAL, LR_DSP_FENCE_POLL, LR_DSP_FENCE_TIMER, LR_DSP_FENCE_MMIO, LR_DSP_FENCE_RAM, LR_DSP_FENCE_TICK, LR_DSP_FENCE_STATE, LR_DSP_FENCE_FRAME };
void lr_dsp_ownership_fence(unsigned reason);
bool lr_dsp_is_tick_callback(void (*cb)(void *));
int lr_dsp_timer_begin(void (*cb)(void *));
#define LR_DSP_TIMER_GUARD(cb) int lr_dsp_timer_loan __attribute__((cleanup(lr_dsp_external_cleanup))) = lr_dsp_timer_begin(cb)
void lr_dsp_register_guarded_timer(void (*cb)(void *));
bool lr_dsp_timer_guarded(void (*cb)(void *));
struct MemoryRegionOps;
void lr_dsp_register_guarded_mmio_ops(const struct MemoryRegionOps *ops);
bool lr_dsp_mmio_guarded(MemoryRegion *mr);
#endif
