/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef NV2A_SUN_SHADOW_PROFILE_H
#define NV2A_SUN_SHADOW_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <lrc_hash.h>

/* Called only by an opt-in enhancement. Readers copy into caller storage;
 * neither profiles nor validation retain a pointer into guest RAM. The
 * caller must hold guest memory stable for the entire validation. */
typedef bool (*PgraphSunRead)(void *opaque, uint32_t address, void *dst,
                              size_t size);

typedef struct PgraphSunProfile {
    uint32_t title_id, timestamp, version;
    uint32_t text_address, text_size;
    uint8_t text_sha256[32];
    uint32_t transform_address, transform_size;
    uint8_t transform_sha256[32];
    uint32_t sun_address, device_address;
} PgraphSunProfile;

/* USA DOAXVB, source XBE SHA-256:
 * 053d44e885fa33c1d15d909a533f39dfbd976e97eeaf67e4fdef8438ea7e5c54
 * Hash the loaded immutable main text, not the source-file hash or writable
 * data. Addresses below are admitted only after this validation succeeds.
 * Source: NoRain211/doaxvb-re 0f0deeccba18, d3d_draw_adapter.c. */
static const PgraphSunProfile pgraph_sun_doaxvb_us = {
    .title_id = 0x54430007,
    .timestamp = 0x3e1f168f,
    .version = 0x00011106,
    .text_address = 0x00011000,
    .text_size = 0x001d2620,
    .text_sha256 = {
        0xb2, 0x27, 0x12, 0xcf, 0x38, 0x71, 0x19, 0xb7,
        0x82, 0xc0, 0x71, 0xb2, 0x4a, 0x12, 0xbf, 0xe7,
        0x5c, 0x02, 0x32, 0xf7, 0xd3, 0x4b, 0xf7, 0x86,
        0xd4, 0x1f, 0xf2, 0x25, 0x7b, 0xe2, 0xee, 0x9a,
    },
    .sun_address = 0x00333870,
    .device_address = 0x001f2978,
    /* SetTransform lies in a writable executable section. Authenticate
     * this immutable code window without hashing live D3D data/imports. */
    .transform_address = 0x001e36d0,
    .transform_size = 256,
    .transform_sha256 = {
        0x02, 0x00, 0x8a, 0x6b, 0x06, 0xd9, 0xd2, 0xd8,
        0x1e, 0x06, 0x77, 0x2c, 0xa3, 0x46, 0x41, 0x07,
        0x3d, 0xec, 0x19, 0x3d, 0x61, 0x34, 0xf7, 0xa6,
        0x76, 0x73, 0x92, 0xff, 0xf4, 0xc6, 0xa7, 0x12,
    },
};

static inline uint32_t pgraph_sun_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static inline bool pgraph_sun_hash_region(PgraphSunRead read, void *opaque,
                                           uint32_t address, uint32_t length,
                                           const uint8_t expected[32])
{
    uint8_t bytes[4096], digest[32];
    struct sha256_state hash;
    if (!length || address > UINT32_MAX - length) return false;
    sha256_stream_init(&hash, 0);
    for (uint32_t offset = 0; offset < length;) {
        size_t size = length - offset;
        if (size > sizeof(bytes)) size = sizeof(bytes);
        if (!read(opaque, address + offset, bytes, size)) return false;
        sha256_stream_update(&hash, bytes, size);
        offset += size;
    }
    sha256_stream_final(&hash, digest);
    return memcmp(digest, expected, sizeof(digest)) == 0;
}

/* The two draw bodies live in a writable executable section, outside main
 * text authentication. These hashes cover the post-RequireSpace seams before
 * their BEGIN stores. No retail instruction bytes are embedded here. */
static inline bool pgraph_sun_validate_draw_seams(PgraphSunRead read, void *opaque)
{
    static const uint8_t up[32] = {
        0x80,0x59,0x2a,0x1f,0xc0,0xad,0xb5,0x0d,0x90,0x95,0xdf,0x3f,0xb0,0x30,0xfa,0xde,
        0xce,0xc4,0x96,0xd7,0xf6,0x86,0x59,0xba,0x65,0xa6,0xf8,0x46,0xaf,0xfc,0xd8,0xae};
    static const uint8_t indexed[32] = {
        0xca,0xcb,0x2d,0x4a,0x2e,0x00,0xa1,0x87,0xdb,0x55,0x97,0xb2,0xb2,0x06,0xc7,0x77,
        0x91,0x32,0x23,0x11,0x70,0x72,0x6c,0x0c,0x3e,0x4b,0x31,0x5e,0x47,0xc1,0xcf,0x00};
    return read && pgraph_sun_hash_region(read, opaque, 0x1e7750, 256, up) &&
        pgraph_sun_hash_region(read, opaque, 0x1e78b0, 256, indexed);
}

/* Full validation is deliberately separate from title polling. Do not call
 * it from normal draw/texture paths or while the enhancement is disabled.
 * Callers must drop any previous selection when validation fails. */
static inline bool pgraph_sun_validate(const PgraphSunProfile *profile,
                                        PgraphSunRead read, void *opaque)
{
    uint8_t header[0x178], cert[0xb0], section[56];
    if (!profile || !read ||
        !read(opaque, 0x10000, header, sizeof(header)) ||
        pgraph_sun_u32(header) != 0x48454258 ||
        pgraph_sun_u32(header + 0x104) != 0x10000 ||
        pgraph_sun_u32(header + 0x114) != profile->timestamp) {
        return false;
    }
    uint32_t headers = pgraph_sun_u32(header + 0x108);
    uint32_t image = pgraph_sun_u32(header + 0x10c);
    uint32_t cert_addr = pgraph_sun_u32(header + 0x118);
    uint32_t sections = pgraph_sun_u32(header + 0x11c);
    uint32_t table = pgraph_sun_u32(header + 0x120);
    if (headers < sizeof(header) || headers > 32768 || image < headers ||
        image > 64 * 1024 * 1024 - 0x10000 ||
        cert_addr < 0x10178 || cert_addr - 0x10000 > headers ||
        headers - (cert_addr - 0x10000) < sizeof(cert) ||
        !sections || sections > 64 || table < 0x10178 ||
        table - 0x10000 > headers ||
        sections > (headers - (table - 0x10000)) / sizeof(section) ||
        profile->text_address < 0x10000 ||
        profile->text_address - 0x10000 > image ||
        !profile->text_size ||
        profile->text_size > image - (profile->text_address - 0x10000) ||
        (profile->transform_size &&
         (profile->transform_address < 0x10000 ||
          profile->transform_address - 0x10000 > image ||
          profile->transform_size > image - (profile->transform_address - 0x10000))) ||
        !read(opaque, cert_addr, cert, sizeof(cert)) ||
        pgraph_sun_u32(cert) < 0x1d0 ||
        pgraph_sun_u32(cert) > headers - (cert_addr - 0x10000) ||
        pgraph_sun_u32(cert + 8) != profile->title_id ||
        pgraph_sun_u32(cert + 0xac) != profile->version) {
        return false;
    }
    bool found = false;
    for (uint32_t i = 0; i < sections; i++) {
        if (!read(opaque, table + i * sizeof(section), section, sizeof(section))) {
            return false;
        }
        if (pgraph_sun_u32(section + 4) != profile->text_address) {
            continue;
        }
        /* Executable, not writable; include precisely the authenticated
         * raw extent, with no uninitialized tail in this profile. */
        if (found || (pgraph_sun_u32(section) & 5) != 4 ||
            pgraph_sun_u32(section + 8) != profile->text_size ||
            pgraph_sun_u32(section + 16) != profile->text_size) {
            return false;
        }
        found = true;
    }
    if (!found) {
        return false;
    }
    return pgraph_sun_hash_region(read, opaque, profile->text_address,
                                  profile->text_size, profile->text_sha256) &&
           (!profile->transform_size ||
            pgraph_sun_hash_region(read, opaque, profile->transform_address,
                                  profile->transform_size, profile->transform_sha256));
}

/* Activation only. In particular an Off request and other titles perform
 * no guest reads, hashes or allocations. Renderer dispatch must bypass the
 * enhancement entirely for ordinary draws. */
static inline const PgraphSunProfile *pgraph_sun_select(
    bool requested, uint32_t title_id, PgraphSunRead read, void *opaque)
{
    if (!requested || title_id != pgraph_sun_doaxvb_us.title_id) {
        return NULL;
    }
    return pgraph_sun_validate(&pgraph_sun_doaxvb_us, read, opaque) &&
           pgraph_sun_validate_draw_seams(read, opaque) ?
           &pgraph_sun_doaxvb_us : NULL;
}

#endif
