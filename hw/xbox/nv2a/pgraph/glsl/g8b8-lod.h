/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_G8B8_LOD_H
#define NV2A_G8B8_LOD_H

/* Legacy keys never set both per-axis edge bits: edge/edge uses the shared
 * flag instead. Use that combination as an extended-key marker. The other
 * seven bits encode 12 minima (4..15) times nine wrap/mirror/edge pairs.
 * Keeping the original bytes avoids invalidating unrelated shader seeds. */
static inline void psh_g8b8_set_extended_lod(PshState *s, unsigned stage,
                                           unsigned minimum, unsigned u,
                                           unsigned v)
{
    unsigned code = (minimum - 4) * 9 + (u - 1) * 3 + v - 1;
    unsigned bit = 1u << stage, high = bit << 4, mask = bit | high;
    s->g8b8_mip_flags = (s->g8b8_mip_flags & ~mask) |
                       ((code & 1) ? bit : 0) | ((code & 2) ? high : 0);
    s->g8b8_mip_clamp_u = (s->g8b8_mip_clamp_u & ~mask) |
                         bit | ((code & 4) ? high : 0);
    s->g8b8_mip_clamp_v = (s->g8b8_mip_clamp_v & ~mask) |
                         bit | ((code & 8) ? high : 0);
    s->g8b8_mip_mirror = (s->g8b8_mip_mirror & ~mask) |
                        ((code & 16) ? bit : 0) | ((code & 32) ? high : 0);
    s->g8b8_signed[stage] = (s->g8b8_signed[stage] & ~32u) |
                          ((code & 64) ? 32u : 0);
}

static inline bool psh_g8b8_get_extended_lod(const PshState *s, unsigned stage,
                                           unsigned *minimum, unsigned *u,
                                           unsigned *v)
{
    unsigned bit = 1u << stage, high = bit << 4;
    if (!(s->g8b8_mip_clamp_u & s->g8b8_mip_clamp_v & bit)) {
        return false;
    }
    unsigned code = ((s->g8b8_mip_flags & bit) ? 1 : 0) |
                    ((s->g8b8_mip_flags & high) ? 2 : 0) |
                    ((s->g8b8_mip_clamp_u & high) ? 4 : 0) |
                    ((s->g8b8_mip_clamp_v & high) ? 8 : 0) |
                    ((s->g8b8_mip_mirror & bit) ? 16 : 0) |
                    ((s->g8b8_mip_mirror & high) ? 32 : 0) |
                    ((s->g8b8_signed[stage] & 32) ? 64 : 0);
    *minimum = 4 + code / 9;
    *u = 1 + (code % 9) / 3;
    *v = 1 + code % 3;
    return true;
}

#endif
