/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "notdirty host: line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
#define MAKE_64BIT_MASK(shift, length) (((~0ULL) >> (64 - (length))) << (shift))
#define DIRTY_MEMORY_CODE 1
#define DIRTY_CLIENTS_NOCODE 6
#define TLB_WATCHPOINT 1
#define TLB_NOTDIRTY 2
#define BP_MEM_WRITE 2
#define BP_MEM_READ 1
typedef uint64_t ram_addr_t;
typedef uint64_t vaddr;
typedef int MMUAccessType;
enum { MMU_DATA_STORE, MMU_DATA_LOAD };
typedef struct { int unused; } CPUState;
typedef struct { ram_addr_t xlat_section; int attrs; } CPUTLBEntryFull;
typedef struct { CPUTLBEntryFull *full; void *haddr; vaddr addr; int flags, size; } MMULookupPageData;
static bool xemu_cpu_boost, code_dirty, clean;
static unsigned invalidations, dirties, tlb_dirties, callbacks, watches;
static vaddr expected_va;
static ram_addr_t expected_ram;
static unsigned expected_size;
static uintptr_t expected_ra;
static void trace_memory_notdirty_write_access(vaddr va, ram_addr_t ram, unsigned size) { CHECK(va == expected_va && ram == expected_ram && size == expected_size); }
static bool physical_memory_get_dirty_flag(ram_addr_t ram, unsigned client) { CHECK(ram == expected_ram && client == DIRTY_MEMORY_CODE); return code_dirty; }
static void physical_memory_set_dirty_range(ram_addr_t ram, unsigned size, unsigned clients) { CHECK(ram == expected_ram && size == expected_size && clients == DIRTY_CLIENTS_NOCODE); dirties++; }
static void tb_invalidate_phys_range_fast(CPUState *cpu, ram_addr_t ram, unsigned size, uintptr_t ra) { CHECK(cpu && ram == expected_ram && size == expected_size && ra == expected_ra); invalidations++; }
static bool physical_memory_is_clean(ram_addr_t ram) { CHECK(ram == expected_ram); return clean; }
static void trace_memory_notdirty_set_dirty(vaddr va) { CHECK(va == expected_va); }
static void tlb_set_dirty(CPUState *cpu, vaddr va) { CHECK(cpu && va == expected_va); tlb_dirties++; }
static void mem_check_access_callback_vaddr(CPUState *cpu, vaddr va, int size, int wp, CPUTLBEntryFull *full) { CHECK(cpu && va == expected_va && size == (int)expected_size && wp == BP_MEM_WRITE && full); callbacks++; }
static void cpu_check_watchpoint(CPUState *cpu, vaddr va, int size, int attrs, int wp, uintptr_t ra) { CHECK(cpu && va == expected_va && size == (int)expected_size && attrs == 7 && wp == BP_MEM_WRITE && ra == expected_ra && callbacks == 1); watches++; }
/* PRODUCTION */
int main(void)
{
    CPUState cpu = {0};
    CPUTLBEntryFull full = { .xlat_section = 0x70000000, .attrs = 7 };
    uint8_t memory[32];
    unsigned cases = 0;
    expected_va = 0x123001;
    expected_ram = expected_va + full.xlat_section;
    expected_ra = 0x12345678;
    for (unsigned size = 1; size <= 8; size *= 2)
    for (unsigned offset = 0; offset < 8; offset++)
    for (unsigned boost = 0; boost < 2; boost++)
    for (unsigned dirty = 0; dirty < 2; dirty++)
    for (unsigned is_clean = 0; is_clean < 2; is_clean++)
    for (unsigned known = 0; known < 2; known++)
    for (unsigned changed = 0; changed < 2; changed++)
    for (unsigned watch = 0; watch < 2; watch++) {
        memset(memory, 0, sizeof(memory));
        expected_size = size;
        xemu_cpu_boost = boost; code_dirty = dirty; clean = is_clean;
        invalidations = dirties = tlb_dirties = callbacks = watches = 0;
        MMULookupPageData data = { .full = &full, .haddr = memory + offset,
            .addr = expected_va, .flags = TLB_NOTDIRTY | (watch ? TLB_WATCHPOINT : 0), .size = size };
        mmu_watch_or_dirty(&cpu, &data, MMU_DATA_STORE, expected_ra, changed, known ? size : 0);
        bool unchanged = boost && !dirty && known && !changed;
        CHECK(invalidations == (!dirty && !unchanged));
        CHECK(dirties == 1 && tlb_dirties == (!unchanged && !is_clean));
        CHECK(callbacks == watch && watches == watch && !data.flags);
        CHECK(data.haddr == memory + offset);
        cases++;
    }
    /* Probe APIs pass unknown values: a host pointer need never be read. */
    expected_size = 4; xemu_cpu_boost = true; code_dirty = false; clean = true;
    invalidations = dirties = tlb_dirties = 0;
    notdirty_write(&cpu, expected_va, expected_size, &full, expected_ra, 0, 0, NULL);
    CHECK(invalidations == 1 && dirties == 1 && !tlb_dirties);
    printf("notdirty host: %u dirty/watch/alias/alignment cases and unknown probe PASS\n", cases);
}
