#ifndef EXEC_TB_CODE_HASH_H
#define EXEC_TB_CODE_HASH_H

#include "exec/cpu-common.h"
#include "exec/target_page.h"
#include "accel/tcg/internal-common.h"
#include "accel/tcg/cpu-ldst-common.h"
#include "accel/tcg/cpu-mmu-index.h"
#include "qemu/fast-hash.h"
#include <encodings/xxh3.h>

static inline uint32_t cpu_ldub_code(CPUArchState *env, vaddr addr)
{
    CPUState *cs = env_cpu(env);
    MemOpIdx oi = make_memop_idx(MO_UB, cpu_mmu_index(cs, true));
    return cpu_ldb_code_mmu(env, addr, oi, 0);
}

static inline void cpu_ld_code(CPUArchState *env, vaddr addr, size_t len, uint8_t *out)
{
    for (size_t i = 0; i < len; i++) {
        out[i] = cpu_ldub_code(env, addr+i);
    }
}

/* Keep the streaming state off the common single-page path's stack.
 * Resolve and consume pages in fetch order: a device read may affect the
 * mapping of the following page, and that page may fault. */
static uint64_t __attribute__((noinline))
tb_code_hash_spans(CPUArchState *env, vaddr pc, size_t size,
                   const void *first_host, bool first_resolved)
{
    xxh3_state_t hash;
    xxh3_reset(&hash, 0);
    while (size) {
        size_t len = MIN(size, TARGET_PAGE_SIZE - (pc & ~TARGET_PAGE_MASK));
        const void *host = first_host;
        first_host = NULL;
        if (!first_resolved) {
            void *resolved = NULL;
            if (get_page_addr_code_hostp(env, pc, &resolved) != -1) {
                host = resolved;
            }
        }
        first_resolved = false;
        if (host) {
            xxh3_update(&hash, host, len);
        } else {
            /* Device/sub-page reads retain their order and side effects.
             * Only this exceptional span needs a small gathering buffer. */
            uint8_t bytes[64];
            for (size_t off = 0; off < len;) {
                size_t n = MIN(sizeof(bytes), len - off);
                cpu_ld_code(env, pc + off, n, bytes);
                xxh3_update(&hash, bytes, n);
                off += n;
            }
        }
        pc += len;
        size -= len;
    }
    return xxh3_digest(&hash);
}

static inline uint64_t tb_code_hash_func(CPUArchState *env, vaddr pc, size_t size)
{
    assert(size < 4096);
    if (!size) {
        return fast_hash(NULL, 0);
    }
    if (size > TARGET_PAGE_SIZE - (pc & ~TARGET_PAGE_MASK)) {
        return tb_code_hash_spans(env, pc, size, NULL, false);
    }
    /* The instruction-fetch probe resolves executable RAM without reading
     * a device. This range lies in one guest page. Device/sub-page mappings
     * keep the byte-fetch path, including its exception and I/O behavior. */
    void *host = NULL;
    if (get_page_addr_code_hostp(env, pc, &host) != -1) {
        return fast_hash(host, size);
    }
    /* The first probe already rejected direct RAM; do not repeat it. */
    return tb_code_hash_spans(env, pc, size, NULL, true);
}

/* The caller already resolved this executable RAM for instruction fetch.
 * A second page need not be adjacent, and an uncacheable mapping must still
 * use the byte-fetch path. Never retain the pointer beyond this lookup. */
static inline uint64_t tb_code_hash_func_host(CPUArchState *env, vaddr pc,
                                              size_t size, const void *host)
{
    assert(size < 4096);
    if (host && size) {
        if (size <= TARGET_PAGE_SIZE - (pc & ~TARGET_PAGE_MASK)) {
            return fast_hash(host, size);
        }
        return tb_code_hash_spans(env, pc, size, host, true);
    }
    return tb_code_hash_func(env, pc, size);
}

#endif
