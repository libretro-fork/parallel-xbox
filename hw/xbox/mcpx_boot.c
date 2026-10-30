/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* MCPX 1.0 HLE: initialize hardware via BIOS Xcodes, load 2BL and hand off.
 * This replaces behavior, not the copyrighted boot ROM's instruction bytes.
 */
#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "system/address-spaces.h"
#include "system/cpus.h"
#include "system/hw_accel.h"
#include "cpu.h"
#include "hw/i386/pc.h"
#include "hw/xbox/xbox.h"
#include "mcpx_boot.h"

static uint32_t boot_read(void *opaque, uint32_t addr, unsigned size, bool io)
{
    uint8_t bytes[4] = { 0 };
    address_space_read(io ? &address_space_io : &address_space_memory,
                       addr, MEMTXATTRS_UNSPECIFIED, bytes, size);
    return ldl_le_p(bytes);
}

static void boot_write(void *opaque, uint32_t addr, uint32_t value,
                       unsigned size, bool io)
{
    uint8_t bytes[4];
    stl_le_p(bytes, value);
    address_space_write(io ? &address_space_io : &address_space_memory,
                        addr, MEMTXATTRS_UNSPECIFIED, bytes, size);
}

void xbox_mcpx_hle_reset(MachineState *machine)
{
    X86CPU *cpu = X86_CPU(first_cpu);
    CPUX86State *env = &cpu->env;
    McpxXcodes xcodes = { .pc = 0xff000080 };
    const McpxBootBus bus = { .read = boot_read, .write = boot_write };
    /* Public interoperability key (data, not ROM instructions), also
     * published as arc4_key_0 in Fancy Mouse Boot ROM b4b62ed's mouse.S.
     */
    static const uint8_t key[16] = {
        0x27, 0x45, 0xa9, 0x10, 0x39, 0x7e, 0x6a, 0xa6,
        0x86, 0xfb, 0x4b, 0x1a, 0x4b, 0xa9, 0x0f, 0xd2,
    };
    uint8_t bootloader[0x6000], state[258];
    uint32_t entry;
    unsigned steps;

    cpu_synchronize_state(CPU(cpu));
    x86_cpu_set_a20(cpu, 1);
    /* Flat protected-mode segments. The data-only GDT is in the HLE
     * overlay, and remains available until 2BL hides the boot ROM.
     */
    env->gdt.base = 0xfffffe00;
    env->gdt.limit = 23;
    cpu_x86_update_cr0(env, (env->cr[0] | CR0_PE_MASK) &
                           ~(CR0_CD_MASK | CR0_NW_MASK));
    cpu_x86_load_seg_cache(env, R_CS, 8, 0, UINT32_MAX, 0x00cf9b00);
    for (int seg = R_ES; seg <= R_GS; seg++) {
        if (seg != R_CS) {
            cpu_x86_load_seg_cache(env, seg, 16, 0, UINT32_MAX, 0x00cf9300);
        }
    }

    /* Bound malformed BIOS loops; do not hang the frontend on reset. */
    for (steps = 0; steps < 1000000; steps++) {
        if (!mcpx_xcode_step(&xcodes, &bus)) {
            break;
        }
    }
    if (steps == 1000000) {
        error_report("MCPX HLE: BIOS Xcodes did not finish at %08x; "
                     "CPU halted (try a supplied MCPX ROM)", xcodes.pc);
        CPU(cpu)->halted = 1;
        return;
    }

    cpu_physical_memory_read(0xffff9e00, bootloader, sizeof(bootloader));
    mcpx_boot_rc4(state, key, bootloader, sizeof(bootloader));
    cpu_physical_memory_write(0x90000, bootloader, sizeof(bootloader));
    cpu_physical_memory_write(0x8f000, state, sizeof(state));
    entry = ldl_le_p(bootloader);
    if (ldl_le_p(bootloader + 0x5fe4) != 0x7854794a) {
        /* Match the failure-path wrap to address zero. Free BIOSes such
         * as Cromwell intentionally install a payload there via Xcodes.
         */
        boot_write(NULL, 0xcf8, 0x80000880, 4, true);
        boot_write(NULL, 0xcfc, 2, 1, true);
        entry = 0;
        warn_report("MCPX HLE: 2BL signature mismatch; using BIOS failure "
                    "path at address zero");
    }
    env->regs[R_ESI] = xcodes.pc;
    env->regs[R_EDI] = xcodes.result;
    env->regs[R_EBP] = xcodes.scratch;
    env->regs[R_EAX] = entry;
    env->eip = entry;
    CPU(cpu)->halted = 0;
}
