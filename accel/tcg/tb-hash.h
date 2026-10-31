/*
 * internal execution defines for qemu
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef EXEC_TB_HASH_H
#define EXEC_TB_HASH_H

#include "exec/vaddr.h"
#include "exec/target_page.h"
#include "exec/translation-block.h"
#include "qemu/xxhash.h"
#include "tb-jmp-cache.h"

#ifdef CONFIG_SOFTMMU

/* Only the bottom TB_JMP_PAGE_BITS of the jump cache hash bits vary for
   addresses on the same page.  The top bits are the same.  This allows
   TLB invalidation to quickly clear a subset of the hash table.  */
#define TB_JMP_PAGE_BITS (TB_JMP_CACHE_BITS / 2)
#define TB_JMP_PAGE_SIZE (1 << TB_JMP_PAGE_BITS)
#define TB_JMP_ADDR_MASK (TB_JMP_PAGE_SIZE - 1)
#define TB_JMP_PAGE_MASK (TB_JMP_CACHE_SIZE - TB_JMP_PAGE_SIZE)

static inline unsigned int tb_jmp_cache_hash_page(vaddr pc)
{
    vaddr tmp;
    tmp = pc ^ (pc >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS));
    return (tmp >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS)) & TB_JMP_PAGE_MASK;
}

static inline unsigned int tb_jmp_cache_hash_func(vaddr pc)
{
    vaddr tmp;
    tmp = pc ^ (pc >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS));
    return (((tmp >> (TARGET_PAGE_BITS - TB_JMP_PAGE_BITS)) & TB_JMP_PAGE_MASK)
           | (tmp & TB_JMP_ADDR_MASK));
}

#else

/* In user-mode we can get better hashing because we do not have a TLB */
static inline unsigned int tb_jmp_cache_hash_func(vaddr pc)
{
    return (pc ^ (pc >> TB_JMP_CACHE_BITS)) & (TB_JMP_CACHE_SIZE - 1);
}

#endif /* CONFIG_SOFTMMU */

/* Salt only inside a page bucket, retaining page-flush bounds. */
static inline unsigned int tb_jmp_cache_class_hash(vaddr pc, uint32_t flags)
{
    unsigned h = tb_jmp_cache_hash_func(pc);
#ifdef CONFIG_SOFTMMU
    unsigned cls = (flags >> 30) | (flags & 4);
    h ^= (cls << 3) & TB_JMP_ADDR_MASK;
#endif
    return h;
}

static inline void tb_jmp_cache_store(CPUJumpCache *jc, vaddr pc,
                                      uint32_t flags, TranslationBlock *tb)
{
    unsigned h = tb_jmp_cache_hash_func(pc);
    if (xemu_cpu_boost &&
        (tb->flags & XEMU_TB_CF_ANY) != XEMU_TB_CF_ANY &&
        jc->array[h].pc == pc) {
        TranslationBlock *old = retro_atomic_load_relaxed_ptr(&jc->array[h].tb);
        if (old && old->cs_base == tb->cs_base &&
            tb_cflags(old) == tb_cflags(tb) &&
            (old->flags & XEMU_TB_CF_ANY) != XEMU_TB_CF_ANY &&
            (old->flags ^ tb->flags) != 0 &&
            ((old->flags ^ tb->flags) & ~XEMU_TB_CF_ANY) == 0) {
            /* Retain both variants only after observing a class conflict. */
            unsigned secondary = tb_jmp_cache_class_hash(pc, old->flags);
            jc->classes[secondary].pc = pc;
            retro_atomic_store_relaxed_ptr(&jc->classes[secondary].tb, old);
            secondary = tb_jmp_cache_class_hash(pc, flags);
            jc->classes[secondary].pc = pc;
            retro_atomic_store_relaxed_ptr(&jc->classes[secondary].tb, tb);
        }
    }
    jc->array[h].pc = pc;
    retro_atomic_store_relaxed_ptr(&jc->array[h].tb, tb);
}

static inline
uint32_t tb_hash_func(tb_page_addr_t phys_pc, vaddr pc,
                      uint32_t flags, uint64_t flags2, uint32_t cf_mask)
{
    return qemu_xxhash8(phys_pc, pc, flags2, flags, cf_mask);
}

#endif
