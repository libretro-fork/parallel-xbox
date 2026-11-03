#ifndef QEMU_CPUS_H
#define QEMU_CPUS_H

#include "qemu/thread.h"
#include <retro_atomic.h>

/* register accel-specific operations */
void cpus_register_accel(const AccelOpsClass *i);

/* interface available for cpus accelerator threads */

/* For temporary buffers for forming a name */
#define VCPU_THREAD_NAME_SIZE 16

void cpus_kick_thread(CPUState *cpu);
bool cpu_work_list_empty(CPUState *cpu);
bool cpu_thread_is_idle(CPUState *cpu);
bool all_cpu_threads_idle(void);
bool cpu_can_run(CPUState *cpu);
/* Prove the Xbox kernel is in an empty idle loop before skipping time. */
bool xemu_libretro_kernel_idle(CPUState *cpu);
/* Prove a complete, interruptible ordinary-RAM polling loop is waiting. */
bool xemu_libretro_memory_poll(CPUState *cpu);
void cpu_thread_signal_created(CPUState *cpu);
void cpu_handle_guest_debug(CPUState *cpu);

/* end interface for cpus accelerator threads */

bool qemu_in_vcpu_thread(void);
void qemu_init_cpu_loop(void);
void resume_all_vcpus(void);
void pause_all_vcpus(void);
void cpu_stop_current(void);

bool cpus_are_resettable(void);

void cpu_synchronize_all_states(void);
void cpu_synchronize_all_post_reset(void);
void cpu_synchronize_all_post_init(void);
void cpu_synchronize_all_pre_loadvm(void);

#endif
