/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define likely(c) (c)
typedef uint32_t target_ulong;
typedef uint64_t vaddr;
typedef struct { int unused; } CPUX86State;
/* TYPES */
static uint8_t first[4096], second[4096];
static X86Access *current;
static unsigned slow_calls;
static uint8_t *logical_ptr(unsigned off)
{
    return off < current->size1 ? first + 4096 - current->size1 + off : second + off - current->size1;
}
static uint64_t load_host(const void *p, unsigned n)
{
    const uint8_t *bytes = p;
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++) v |= (uint64_t)bytes[i] << (i * 8);
    return v;
}
static void store_host(void *p, uint64_t v, unsigned n)
{
    uint8_t *bytes = p;
    for (unsigned i = 0; i < n; i++) bytes[i] = v >> (i * 8);
}
static uint64_t logical_load(unsigned off, unsigned n)
{
    uint64_t v = 0;
    for (unsigned i = 0; i < n; i++) v |= (uint64_t)*logical_ptr(off + i) << (i * 8);
    return v;
}
static uint64_t slow_load(CPUX86State *env, vaddr addr, int mmu, uintptr_t ra, unsigned n)
{
    assert(env == current->env && mmu == current->mmu_idx && ra == current->ra);
    slow_calls++;
    return logical_load(addr - current->vaddr, n);
}
static void slow_store(CPUX86State *env, vaddr addr, uint64_t value, int mmu, uintptr_t ra, unsigned n)
{
    assert(env == current->env && mmu == current->mmu_idx && ra == current->ra);
    slow_calls++;
    for (unsigned i = 0; i < n; i++) *logical_ptr(addr - current->vaddr + i) = value >> (i * 8);
}
#define ldub_p(p) load_host(p, 1)
#define lduw_le_p(p) load_host(p, 2)
#define ldl_le_p(p) load_host(p, 4)
#define ldq_le_p(p) load_host(p, 8)
#define stb_p(p, v) store_host(p, v, 1)
#define stw_le_p(p, v) store_host(p, v, 2)
#define stl_le_p(p, v) store_host(p, v, 4)
#define stq_le_p(p, v) store_host(p, v, 8)
#define cpu_ldub_mmuidx_ra(e, a, m, r) slow_load(e, a, m, r, 1)
#define cpu_lduw_le_mmuidx_ra(e, a, m, r) slow_load(e, a, m, r, 2)
#define cpu_ldl_le_mmuidx_ra(e, a, m, r) slow_load(e, a, m, r, 4)
#define cpu_ldq_le_mmuidx_ra(e, a, m, r) slow_load(e, a, m, r, 8)
#define cpu_stb_mmuidx_ra(e, a, v, m, r) slow_store(e, a, v, m, r, 1)
#define cpu_stw_le_mmuidx_ra(e, a, v, m, r) slow_store(e, a, v, m, r, 2)
#define cpu_stl_le_mmuidx_ra(e, a, v, m, r) slow_store(e, a, v, m, r, 4)
#define cpu_stq_le_mmuidx_ra(e, a, v, m, r) slow_store(e, a, v, m, r, 8)
/* PRODUCTION */
int main(void)
{
    CPUX86State env = {0};
    for (unsigned i = 0; i < 4096; i++) { first[i] = i * 37; second[i] = i * 11; }
    unsigned cases = 0, crossing = 0, missing = 0;
    for (unsigned split = 1; split <= 4096; split++)
    for (unsigned mappings = 0; mappings < 4; mappings++) {
        unsigned size = split + (4096 - split < 16 ? 4096 - split : 16);
        X86Access ac = { .vaddr = 0x100000, .haddr1 = mappings & 1 ? first + 4096 - split : NULL,
            .haddr2 = mappings & 2 ? second : NULL, .size = size, .size1 = split,
            .env = &env, .mmu_idx = 5, .ra = 0x12345678 };
        current = &ac;
        for (unsigned len = 1; len <= 8; len *= 2) {
            if (len > size) continue;
            unsigned offsets[] = {0, split - 1, split, split + 1, size - len};
            for (unsigned k = 0; k < sizeof(offsets) / sizeof(offsets[0]); k++) {
                unsigned offset = offsets[k];
                if (offset > size - len) continue;
                void *expected = NULL;
                if (ac.haddr1 && offset + len <= split) expected = (uint8_t *)ac.haddr1 + offset;
                else if (ac.haddr1 && ac.haddr2 && offset >= split) expected = (uint8_t *)ac.haddr2 + offset - split;
                void *got = access_ptr(&ac, ac.vaddr + offset, len);
                if (got != expected) {
                    if (offset < split) crossing++; else missing++;
                    if (crossing + missing <= 2) fprintf(stderr, "split=%u size=%u mappings=%u offset=%u len=%u: pointer classification mismatch\n", split, size, mappings, offset, len);
                    continue; /* Baseline reproduction must not dereference a bad pointer. */
                }
                uint64_t want = logical_load(offset, len), value;
                vaddr addr = ac.vaddr + offset;
                slow_calls = 0;
                switch (len) {
                case 1: value = access_ldb(&ac, addr); break;
                case 2: value = access_ldw(&ac, addr); break;
                case 4: value = access_ldl(&ac, addr); break;
                default: value = access_ldq(&ac, addr); break;
                }
                assert(value == want && slow_calls == (expected == NULL));
                uint64_t written = want ^ UINT64_MAX;
                switch (len) {
                case 1: access_stb(&ac, addr, written); break;
                case 2: access_stw(&ac, addr, written); break;
                case 4: access_stl(&ac, addr, written); break;
                default: access_stq(&ac, addr, written); break;
                }
                assert(slow_calls == 2 * (expected == NULL));
                if (len < 8) written &= (UINT64_C(1) << (len * 8)) - 1;
                assert(logical_load(offset, len) == written);
                for (unsigned i = 0; i < len; i++) *logical_ptr(offset + i) = want >> (i * 8);
                cases++;
            }
        }
    }
    if (crossing || missing) {
        fprintf(stderr, "FAIL: %u cross-page and %u second-page classifications\n", crossing, missing);
        return 1;
    }
    printf("x86 access pointer: %u split/width/offset/RAM-MMIO classifications and load/store routes PASS\n", cases);
}
