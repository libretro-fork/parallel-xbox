/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_SUN_SHADOW_RUNTIME_H
#define NV2A_SUN_SHADOW_RUNTIME_H

#include <stdbool.h>
#include <stdint.h>

enum PgraphSunBuild {
    PGRAPH_SUN_UNSUPPORTED,
    PGRAPH_SUN_DOAXVB_US_11106,
};

/* Owned by the emulation thread under the BQL. No guest pointer, renderer
 * object or callback is retained. Reset/load/unload invalidate the result;
 * the next title poll verifies a fresh lifetime. Not serialized. */
typedef struct PgraphSunIdentity {
    uint32_t title;
    enum PgraphSunBuild build;
    bool checked;
    uint64_t generation; /* host lifetime only; never serialized */
} PgraphSunIdentity;

static inline void pgraph_sun_identity_reset(PgraphSunIdentity *identity)
{
    uint64_t next = identity->generation == UINT64_MAX ? UINT64_MAX : identity->generation + 1;
    *identity = (PgraphSunIdentity){.generation = next};
}

/* Called by existing title polling, never by draw/texture/shader paths.
 * The verifier runs once on entering the candidate title, whether enabled
 * or disabled. A failure is final until the next title/reset/load lifetime.
 * A true result only identifies a build; it does not enable rendering. */
static inline bool pgraph_sun_identity_update(PgraphSunIdentity *identity,
    uint32_t title, bool (*verify)(void *opaque), void *opaque)
{
    if (identity->checked && identity->title == title) return false;
    pgraph_sun_identity_reset(identity);
    identity->title = title;
    identity->checked = true;
    identity->build = PGRAPH_SUN_UNSUPPORTED;
    if (title == 0x54430007 && verify && verify(opaque)) {
        identity->build = PGRAPH_SUN_DOAXVB_US_11106;
    }
    return true;
}

#endif
