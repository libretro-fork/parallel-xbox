/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef HW_XBOX_NV2A_RAMIN_H
#define HW_XBOX_NV2A_RAMIN_H
#define NV2A_PRAMIN_BYTES 0x100000u
#define NV2A_PRAMIN_BLOCK 16u

/* Instance addresses reverse whole 16-byte blocks, not the dwords in a block.
 * The cached top pointer also selects legacy storage for pre-alias states and
 * unverified RAM configurations. Callers borrow only one complete block. */
static inline uint8_t *nv2a_ramin_span(NV2AState *d, hwaddr addr, unsigned size)
{
    assert(size && addr < NV2A_PRAMIN_BYTES &&
           size <= NV2A_PRAMIN_BYTES - addr &&
           size <= NV2A_PRAMIN_BLOCK - (addr & (NV2A_PRAMIN_BLOCK - 1)));
    if (d->pramin_top) {
        return d->pramin_top - (addr & ~(hwaddr)(NV2A_PRAMIN_BLOCK - 1)) +
               (addr & (NV2A_PRAMIN_BLOCK - 1));
    }
    return d->ramin_ptr + addr;
}
#endif
