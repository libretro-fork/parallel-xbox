/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef XBOX_XBE_RUNTIME_H
#define XBOX_XBE_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef struct XbeRuntimePlan {
    uint32_t entry;
    uint32_t certificate;
    uint32_t flags;
} XbeRuntimePlan;

static inline uint32_t xbe_runtime_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* Shared by the disc probe and the execution-time check. All arithmetic is
 * bounded before a guest address or file offset is used. */
static inline bool xbe_runtime_plan(const uint8_t *h, size_t hsize,
                                    const uint8_t *cert, size_t csize,
                                    bool loaded, XbeRuntimePlan *plan)
{
    if (hsize < 0x178 || csize < 0x1dc ||
        xbe_runtime_u32(h) != 0x48454258 ||
        xbe_runtime_u32(h + 0x104) != 0x10000) {
        return false;
    }
    uint32_t headers = xbe_runtime_u32(h + 0x108);
    uint32_t image = xbe_runtime_u32(h + 0x10c);
    uint32_t certificate = xbe_runtime_u32(h + 0x118);
    uint32_t cert_size = xbe_runtime_u32(cert);
    if (headers < 0x178 || headers > 32768 || image < headers ||
        image > 64 * 1024 * 1024 || certificate < 0x10178 ||
        certificate > 0x10000 + headers || cert_size < 0x1dc ||
        cert_size > 0x10000 + headers - certificate ||
        xbe_runtime_u32(cert + 8) != 0x46530002) {
        return false;
    }
    uint32_t encoded = xbe_runtime_u32(h + 0x128);
    /* The kernel can replace the encoded field with its decoded address. */
    static const uint32_t keys[] = { 0, 0xa8fc57ab, 0x94859d4b };
    for (unsigned i = loaded ? 0 : 1; i < sizeof(keys) / sizeof(keys[0]); i++) {
        uint32_t entry = encoded ^ keys[i];
        if (entry >= 0x10000 + headers && entry < 0x10000 + image) {
            plan->entry = entry;
            plan->certificate = certificate;
            plan->flags = certificate + 0x1d8;
            return true;
        }
    }
    return false;
}

typedef struct CPUState CPUState;
typedef struct BlockBackend BlockBackend;
extern uint32_t xemu_xbe_runtime_entry;
extern bool xemu_xbe_runtime_enabled;
void xemu_xbe_runtime_disc(BlockBackend *blk);
void xemu_xbe_runtime_apply(CPUState *cpu, uint64_t pc);

#endif
