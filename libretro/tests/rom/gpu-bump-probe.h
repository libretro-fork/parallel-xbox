/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Opt-in observation fixture, included by gpu.c. No hardware oracle yet.
 * Not a flash image for installation on a physical console. */
#define BUMP_COUNT 0x00300100u
#define BUMP_ROWS  0x00300200u
#define BUMP_DATA  0x03500000u
#define BUMP_RAMP  0x03501000u

#if defined(BUMP_FILTER_MIXED_PROBE) || defined(BUMP_FILTER_MIP_PROBE)
static void bump_mixed_triangle(u32 layout, u32 footprint)
{
    static const u32 pos[3][4] = {
        {0x43200000u, 0x42c80000u, 0, 0x3f800000u},
        {0x43f00000u, 0x42c80000u, 0, 0x3f800000u},
        {0x43a00000u, 0x43c80000u, 0, 0x3f800000u},
    };
    /* At the sampled pixel center x=320.5: u=1.25 texels.
     * Slopes: 1/8, 1/2 and 4 texels/pixel.
     * Both magnification cases lie away from the LOD transition. */
    static const u32 tex_u[2][3][3] = {
#ifdef BUMP_FILTER_MIP_PROBE
        /* 4x4 base: u=1.25 texels, or one wrap period below it, at
         * x=320.5. Exact footprints 1/4, 1 and 2; bias tests mip blending. */
        {{0xc11b8000u, 0x41248000u, 0x3e900000u},
         {0xc21f4000u, 0x4220c000u, 0x3e400000u},
         {0xc29fe000u, 0x42a02000u, 0x3d800000u}},
        {{0xc12b8000u, 0x41148000u, 0xbf380000u},
         {0xc2234000u, 0x421cc000u, 0xbf500000u},
         {0xc2a1e000u, 0x429e2000u, 0xbf700000u}},
#else
        {{0xc1968000u, 0x41a98000u, 0x3f980000u},
         {0xc29e0000u, 0x42a20000u, 0x3f800000u},
         {0xc4203000u, 0x441fd000u, 0xbf400000u}},
        {{0xc1168000u, 0x41298000u, 0x3f180000u},
         {0xc21e0000u, 0x42220000u, 0x3f000000u},
         {0xc3a03000u, 0x439fd000u, 0xbec00000u}},
#endif
    };
    method(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 9 * 4, 2u | (2u << 4));
    method(NV097_SET_BEGIN_END, 5);
    push(0x40000000u | (18u << 18) | NV097_INLINE_ARRAY);
    for (u32 vertex = 0; vertex < 3; vertex++) {
        for (u32 component = 0; component < 4; component++) push(pos[vertex][component]);
        push(tex_u[layout][footprint][vertex]);
        push(layout ? 0x3e800000u : 0x3f000000u);
    }
    method(NV097_SET_BEGIN_END, 0);
    method(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 9 * 4, 2);
}
#endif

static void bump_filter_probe(void)
{
    /* Linear A8R8G8B8 ramp: R reports U, G reports V independently. */
    for (u32 y = 0; y < 16; y++) {
        for (u32 x = 0; x < 32; x++) {
            ram32(BUMP_RAMP + y * 128 + x * 4,
                  0xff000000u | ((x * 8) << 16) | ((y * 16) << 8));
        }
    }
    ram32(BUMP_COUNT, 0);
    for (u32 id = 0; id < 192; id++) {
        /* Fastest to slowest: coordinate, filter, flags, axis, mode, layout. */
        u32 coord = id % 3;
        u32 linear = (id / 3) % 2;
        u32 flags = (id / 6) % 4; /* bit 0 B, bit 1 G */
        u32 axis = (id / 24) % 2;
        u32 lum = (id / 48) % 2;
        u32 swizzled_layout = id / 96;
#ifdef BUMP_FILTER_COLOR_PROBE
        flags = (id / 6) % 16;
        axis = lum = 0;
#endif
        u32 format = swizzled_layout ? 0x28 : 0x17;
        u32 bump_address = BUMP_DATA + id * 16;
#ifdef BUMP_FILTER_MIP_PROBE
        swizzled_layout = 1;
        format = 0x28;
        bump_address = BUMP_DATA + id * 64;
#endif
        static const u32 rect_u[] = {0x3f000000, 0x3f800000, 0x3fc00000};
        static const u32 norm_u[] = {0x3e800000, 0x3f000000, 0x3f400000};
        wait_vblank();
        /* Unique addresses keep texture rewrite/invalidation behavior out
         * of the observation. 2x2 Morton order equals row-major here. */
        u32 pair = axis ? 0x0100ff00u : 0x000100ffu;
#ifdef BUMP_FILTER_COLOR_PROBE
        pair = 0xc00140ffu; /* B: -1/+1, G: +64/-64 when signed. */
#endif
#ifdef BUMP_FILTER_MIXED_PROBE
        pair = 0xc04040c0u; /* B: -64/+64; G: +64/-64. */
#endif
        ram32(bump_address, pair);
        ram32(bump_address + 4, pair);
#ifdef BUMP_FILTER_MIP_PROBE
        for (u32 word = 0; word < 8; word++) ram32(bump_address + word * 4, 0xc04040c0u);
        ram32(bump_address + 32, 0xa02060e0u);
        ram32(bump_address + 36, 0xa02060e0u);
        ram32(bump_address + 40, 0x7010u);
#endif
        /* Reuse the established surface/transform setup, but defer its draw. */
        texture_alias_target(0xc0000000u | format, 1,
                             swizzled_layout ? norm_u[coord] : rect_u[coord],
                             0, bump_address, SURF_D);
        method(NV097_SET_TEXTURE_FORMAT,
               1u | (2u << 4) | (1u << 3) | (format << 8) |
               (1u << 16) | (1u << 20) | (1u << 24));
        method(NV097_SET_TEXTURE_CONTROL1, 4u << 16);
        method(NV097_SET_TEXTURE_ADDRESS, 0x00010101u); /* wrap: AF-eligible */
        method(NV097_SET_TEXTURE_IMAGE_RECT, (2u << 16) | 2u);
#ifdef BUMP_FILTER_MIP_PROBE
        method(NV097_SET_TEXTURE_FORMAT,
               1u | (2u << 4) | (1u << 3) | (format << 8) |
               (3u << 16) | (2u << 20) | (2u << 24));
        method(NV097_SET_TEXTURE_CONTROL0, (1u << 30) | ((linear ? 1u : 2u) << 6));
#endif
        method(NV097_SET_TEXTURE_FILTER,
#ifdef BUMP_FILTER_MIP_PROBE
               (BUMP_MIP_FILTER << 16) | (BUMP_MIP_MAG << 24) | BUMP_MIXED_BIAS |
#elif defined(BUMP_FILTER_MIXED_PROBE)
               (linear ? 0x01020000u : 0x02010000u) | BUMP_MIXED_BIAS |
#else
               (linear ? 0x02020000u : 0x01010000u) |
#endif
               ((flags & 1) ? 0x80000000u : 0) |
               ((flags & 2) ? 0x40000000u : 0) |
               ((flags & 4) ? 0x20000000u : 0) |
               ((flags & 8) ? 0x10000000u : 0));
        /* Stage 1 samples the ramp with the displacement supplied by T0. */
        method(NV097_SET_TEXTURE_OFFSET + 64, BUMP_RAMP);
        method(NV097_SET_TEXTURE_FORMAT + 64,
               1u | (2u << 4) | (1u << 3) | (0x12u << 8) |
               (1u << 16) | (5u << 20) | (4u << 24));
        method(NV097_SET_TEXTURE_ADDRESS + 64, 0x00030303u);
        method(NV097_SET_TEXTURE_CONTROL0 + 64, 1u << 30);
        method(NV097_SET_TEXTURE_CONTROL1 + 64, 128u << 16);
        method(NV097_SET_TEXTURE_IMAGE_RECT + 64, (32u << 16) | 16u);
        method(NV097_SET_TEXTURE_FILTER + 64, 0x02020000u);
        method(NV097_SET_VERTEX_DATA2F_M + 10 * 8, 0x41800000u); /* 16 */
        method(NV097_SET_VERTEX_DATA2F_M + 10 * 8 + 4, 0x41000000u); /* 8 */
        method(0x1b28u + 64, 0x41000000u); /* bump matrix: diagonal 8 */
        method(0x1b2cu + 64, 0);
        /* NV097 order is 00, 01, 11, 10 (not row-major). */
        method(0x1b30u + 64, 0x41000000u);
        method(0x1b34u + 64, 0);
        method(0x1b38u + 64, 0);           /* luminance scale 0 */
        method(0x1b3cu + 64, 0x3f800000u); /* luminance offset 1 */
        method(0x1e78u, 0); /* stage 1 always consumes stage 0 */
        method(NV097_SET_SHADER_STAGE_PROGRAM, 1u | ((lum ? 7u : 6u) << 5));
        method(NV097_SET_COMBINER_SPECULAR_FOG_CW0, 9); /* final RGB T1 */
        method(NV097_SET_COMBINER_SPECULAR_FOG_CW1, 0x00001900u);
#ifdef BUMP_FILTER_COLOR_PROBE
        method(NV097_SET_SHADER_STAGE_PROGRAM, 1);
        method(NV097_SET_COMBINER_SPECULAR_FOG_CW0, 8);
        method(NV097_SET_COMBINER_SPECULAR_FOG_CW1, 0x00001800u);
#endif
#ifdef BUMP_FILTER_MIP_PROBE
        bump_mixed_triangle(id / 96, coord);
#elif defined(BUMP_FILTER_MIXED_PROBE)
        bump_mixed_triangle(swizzled_layout, coord);
#else
        triangle();
#endif
        texture_cpu_barrier();
        ram32(BUMP_ROWS + id * 8, id);
        ram32(BUMP_ROWS + id * 8 + 4,
              rd32(SURF_D + (HEIGHT / 2) * PITCH + (WIDTH / 2) * 4));
        ram32(BUMP_COUNT, id + 1);
        disable_texture();
        method(NV097_SET_TEXTURE_CONTROL0 + 64, 0);
        method(NV097_FLIP_INCREMENT_WRITE, 0);
        kick();
    }
    /* Keep the frontend alive after collection; do not spin on CPU time. */
    for (;;) {
        wait_vblank();
        method(NV097_FLIP_INCREMENT_WRITE, 0);
        kick();
    }
}
