/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Opt-in observation fixture, included by gpu.c. No hardware oracle yet.
 * Not a flash image for installation on a physical console. */
#define BUMP_COUNT 0x00300100u
#define BUMP_ROWS  0x00300200u
#define BUMP_DATA  0x03500000u
#define BUMP_RAMP  0x03501000u
#ifdef BUMP_FILTER_WIDE_MIP_PROBE
#undef BUMP_RAMP
#define BUMP_RAMP  0x03540000u /* Beyond all 192 independent 1024-byte slots. */
#if BUMP_MIP_BASE_SIZE == 32
#undef BUMP_RAMP
#define BUMP_RAMP  0x03600000u /* Beyond 192 independent 4096-byte slots. */
#endif
#if BUMP_MIP_BASE_SIZE >= 64
#undef BUMP_DATA
#undef BUMP_RAMP
/* Up to 192 independent 128x128 mip chains fit below 48 MiB, away from
 * this fixture's SURF_D target and its scanout buffers. */
#define BUMP_DATA  0x02400000u
#define BUMP_RAMP  0x03400000u
#endif
_Static_assert(BUMP_DATA + 192u * BUMP_MIP_BASE_SIZE * BUMP_MIP_BASE_SIZE * 4u <= BUMP_RAMP,
               "independent mip slots must not overlap the ramp");
_Static_assert(BUMP_RAMP + 32u * 16u * 4u <= 0x04000000u,
               "the fixture must fit in 64 MiB");
#endif

#ifdef BUMP_NATIVE_COLOR
#undef BUMP_RAMP
#define BUMP_RAMP 0x03502000u /* Beyond 192 independent 32-byte native slots. */
#endif

#ifdef BUMP_NATIVE_CUBE_FACE
#undef BUMP_RAMP
#define BUMP_RAMP 0x03540000u
_Static_assert(BUMP_DATA + 192u * 1024u <= BUMP_RAMP, "cube slots overlap ramp");
#endif

#if defined(BUMP_NATIVE_VOLUME) && defined(BUMP_NATIVE_CHAIN)
#undef BUMP_RAMP
#define BUMP_RAMP 0x03600000u
_Static_assert(BUMP_DATA + 192u * 4096u <= BUMP_RAMP, "volume slots overlap ramp");
#endif

#if defined(BUMP_FILTER_MIXED_PROBE) || defined(BUMP_FILTER_MIP_PROBE)
static void bump_mixed_triangle(u32 layout, u32 footprint)
{
    static const u32 pos[3][4] = {
        {0x43200000u, 0x42c80000u, 0, 0x3f800000u},
        {0x43f00000u, 0x42c80000u, 0, 0x3f800000u},
        {0x43a00000u, 0x43c80000u, 0, 0x3f800000u},
    };
    /* Exact texture-coordinate footprints at the sampled pixel center
     * (320.5, 240.5) avoid approximate fractional-LOD derivatives. */
#ifdef BUMP_FILTER_CLAMP_PROBE
    /* Independent U/X and V/Y slopes preserve exact LOD footprints. Sample
     * centers cover inside, below, above and opposite U/V texture edges. */
#ifdef BUMP_FILTER_WIDE_MIP_PROBE
    /* Shift normalized U/V by 1/(4 * base size), keeping mip-zero point
     * and gather footprints away from exact boundaries after Force LOD0. */
#if BUMP_MIP_BASE_SIZE == 128
    static const u32 tex_u[4][3][3] = {
        {{0xc11b7800u, 0x41248800u, 0x3e910000u},
         {0xc21f3e00u, 0x4220c200u, 0x3e420000u},
         {0xc29fdf00u, 0x42a02100u, 0x3d840000u}},
        {{0xc1217800u, 0x411e8800u, 0xbdbc0000u},
         {0xc220be00u, 0x421f4200u, 0xbe3e0000u},
         {0xc2a09f00u, 0x429f6100u, 0xbe9f0000u}},
        {{0xc10f7800u, 0x41308800u, 0x3f844000u},
         {0xc21c3e00u, 0x4223c200u, 0x3f708000u},
         {0xc29e5f00u, 0x42a1a100u, 0x3f508000u}},
        {{0xc1217800u, 0x411e8800u, 0xbdbc0000u},
         {0xc220be00u, 0x421f4200u, 0xbe3e0000u},
         {0xc2a09f00u, 0x429f6100u, 0xbe9f0000u}},
    };
    static const u32 tex_v[4][3][3] = {
        {{0xc1077800u, 0xc1077800u, 0x41248800u},
         {0xc20b3e00u, 0xc20b3e00u, 0x4220c200u},
         {0xc28bdf00u, 0xc28bdf00u, 0x42a02100u}},
        {{0xc10d7800u, 0xc10d7800u, 0x411e8800u},
         {0xc20cbe00u, 0xc20cbe00u, 0x421f4200u},
         {0xc28c9f00u, 0xc28c9f00u, 0x429f6100u}},
        {{0xc0f6f000u, 0xc0f6f000u, 0x41308800u},
         {0xc2083e00u, 0xc2083e00u, 0x4223c200u},
         {0xc28a5f00u, 0xc28a5f00u, 0x42a1a100u}},
        {{0xc0f6f000u, 0xc0f6f000u, 0x41308800u},
         {0xc2083e00u, 0xc2083e00u, 0x4223c200u},
         {0xc28a5f00u, 0xc28a5f00u, 0x42a1a100u}},
    };
#elif BUMP_MIP_BASE_SIZE == 64
    static const u32 tex_u[4][3][3] = {
        {{0xc11b7000u, 0x41249000u, 0x3e920000u},
         {0xc21f3c00u, 0x4220c400u, 0x3e440000u},
         {0xc29fde00u, 0x42a02200u, 0x3d880000u}},
        {{0xc1217000u, 0x411e9000u, 0xbdb80000u},
         {0xc220bc00u, 0x421f4400u, 0xbe3c0000u},
         {0xc2a09e00u, 0x429f6200u, 0xbe9e0000u}},
        {{0xc10f7000u, 0x41309000u, 0x3f848000u},
         {0xc21c3c00u, 0x4223c400u, 0x3f710000u},
         {0xc29e5e00u, 0x42a1a200u, 0x3f510000u}},
        {{0xc1217000u, 0x411e9000u, 0xbdb80000u},
         {0xc220bc00u, 0x421f4400u, 0xbe3c0000u},
         {0xc2a09e00u, 0x429f6200u, 0xbe9e0000u}},
    };
    static const u32 tex_v[4][3][3] = {
        {{0xc1077000u, 0xc1077000u, 0x41249000u},
         {0xc20b3c00u, 0xc20b3c00u, 0x4220c400u},
         {0xc28bde00u, 0xc28bde00u, 0x42a02200u}},
        {{0xc10d7000u, 0xc10d7000u, 0x411e9000u},
         {0xc20cbc00u, 0xc20cbc00u, 0x421f4400u},
         {0xc28c9e00u, 0xc28c9e00u, 0x429f6200u}},
        {{0xc0f6e000u, 0xc0f6e000u, 0x41309000u},
         {0xc2083c00u, 0xc2083c00u, 0x4223c400u},
         {0xc28a5e00u, 0xc28a5e00u, 0x42a1a200u}},
        {{0xc0f6e000u, 0xc0f6e000u, 0x41309000u},
         {0xc2083c00u, 0xc2083c00u, 0x4223c400u},
         {0xc28a5e00u, 0xc28a5e00u, 0x42a1a200u}},
    };
#elif BUMP_MIP_BASE_SIZE == 32
    static const u32 tex_u[4][3][3] = {
        {{0xc11b6000u, 0x4124a000u, 0x3e940000u},
         {0xc21f3800u, 0x4220c800u, 0x3e480000u},
         {0xc29fdc00u, 0x42a02400u, 0x3d900000u}},
        {{0xc1216000u, 0x411ea000u, 0xbdb00000u},
         {0xc220b800u, 0x421f4800u, 0xbe380000u},
         {0xc2a09c00u, 0x429f6400u, 0xbe9c0000u}},
        {{0xc10f6000u, 0x4130a000u, 0x3f850000u},
         {0xc21c3800u, 0x4223c800u, 0x3f720000u},
         {0xc29e5c00u, 0x42a1a400u, 0x3f520000u}},
        {{0xc1216000u, 0x411ea000u, 0xbdb00000u},
         {0xc220b800u, 0x421f4800u, 0xbe380000u},
         {0xc2a09c00u, 0x429f6400u, 0xbe9c0000u}},
    };
    static const u32 tex_v[4][3][3] = {
        {{0xc1076000u, 0xc1076000u, 0x4124a000u},
         {0xc20b3800u, 0xc20b3800u, 0x4220c800u},
         {0xc28bdc00u, 0xc28bdc00u, 0x42a02400u}},
        {{0xc10d6000u, 0xc10d6000u, 0x411ea000u},
         {0xc20cb800u, 0xc20cb800u, 0x421f4800u},
         {0xc28c9c00u, 0xc28c9c00u, 0x429f6400u}},
        {{0xc0f6c000u, 0xc0f6c000u, 0x4130a000u},
         {0xc2083800u, 0xc2083800u, 0x4223c800u},
         {0xc28a5c00u, 0xc28a5c00u, 0x42a1a400u}},
        {{0xc0f6c000u, 0xc0f6c000u, 0x4130a000u},
         {0xc2083800u, 0xc2083800u, 0x4223c800u},
         {0xc28a5c00u, 0xc28a5c00u, 0x42a1a400u}},
    };
#else
    static const u32 tex_u[4][3][3] = {
        {{0xc11b4000u, 0x4124c000u, 0x3e980000u},
         {0xc21f3000u, 0x4220d000u, 0x3e500000u},
         {0xc29fd800u, 0x42a02800u, 0x3da00000u}},
        {{0xc1214000u, 0x411ec000u, 0xbda00000u},
         {0xc220b000u, 0x421f5000u, 0xbe300000u},
         {0xc2a09800u, 0x429f6800u, 0xbe980000u}},
        {{0xc10f4000u, 0x4130c000u, 0x3f860000u},
         {0xc21c3000u, 0x4223d000u, 0x3f740000u},
         {0xc29e5800u, 0x42a1a800u, 0x3f540000u}},
        {{0xc1214000u, 0x411ec000u, 0xbda00000u},
         {0xc220b000u, 0x421f5000u, 0xbe300000u},
         {0xc2a09800u, 0x429f6800u, 0xbe980000u}},
    };
    static const u32 tex_v[4][3][3] = {
        {{0xc1074000u, 0xc1074000u, 0x4124c000u},
         {0xc20b3000u, 0xc20b3000u, 0x4220d000u},
         {0xc28bd800u, 0xc28bd800u, 0x42a02800u}},
        {{0xc10d4000u, 0xc10d4000u, 0x411ec000u},
         {0xc20cb000u, 0xc20cb000u, 0x421f5000u},
         {0xc28c9800u, 0xc28c9800u, 0x429f6800u}},
        {{0xc0f68000u, 0xc0f68000u, 0x4130c000u},
         {0xc2083000u, 0xc2083000u, 0x4223d000u},
         {0xc28a5800u, 0xc28a5800u, 0x42a1a800u}},
        {{0xc0f68000u, 0xc0f68000u, 0x4130c000u},
         {0xc2083000u, 0xc2083000u, 0x4223d000u},
         {0xc28a5800u, 0xc28a5800u, 0x42a1a800u}},
    };
#endif
#else
    static const u32 tex_u[4][3][3] = {
        {{0xc11b8000u, 0x41248000u, 0x3e900000u},
         {0xc21f4000u, 0x4220c000u, 0x3e400000u},
         {0xc29fe000u, 0x42a02000u, 0x3d800000u}},
        {{0xc1218000u, 0x411e8000u, 0xbdc00000u},
         {0xc220c000u, 0x421f4000u, 0xbe400000u},
         {0xc2a0a000u, 0x429f6000u, 0xbea00000u}},
        {{0xc10f8000u, 0x41308000u, 0x3f840000u},
         {0xc21c4000u, 0x4223c000u, 0x3f700000u},
         {0xc29e6000u, 0x42a1a000u, 0x3f500000u}},
        {{0xc1218000u, 0x411e8000u, 0xbdc00000u},
         {0xc220c000u, 0x421f4000u, 0xbe400000u},
         {0xc2a0a000u, 0x429f6000u, 0xbea00000u}},
    };
    static const u32 tex_v[4][3][3] = {
        {{0xc1078000u, 0xc1078000u, 0x41248000u},
         {0xc20b4000u, 0xc20b4000u, 0x4220c000u},
         {0xc28be000u, 0xc28be000u, 0x42a02000u}},
        {{0xc10d8000u, 0xc10d8000u, 0x411e8000u},
         {0xc20cc000u, 0xc20cc000u, 0x421f4000u},
         {0xc28ca000u, 0xc28ca000u, 0x429f6000u}},
        {{0xc0f70000u, 0xc0f70000u, 0x41308000u},
         {0xc2084000u, 0xc2084000u, 0x4223c000u},
         {0xc28a6000u, 0xc28a6000u, 0x42a1a000u}},
        {{0xc0f70000u, 0xc0f70000u, 0x41308000u},
         {0xc2084000u, 0xc2084000u, 0x4223c000u},
         {0xc28a6000u, 0xc28a6000u, 0x42a1a000u}},
    };
#endif
#else
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
#endif
#if defined(BUMP_NATIVE_CUBE_FACE) || defined(BUMP_NATIVE_VOLUME)
    (void)layout; (void)tex_u;
    /* Three-component fixtures use texel footprints 1/8, 1/2 and 1/4. */
#ifdef BUMP_NATIVE_VOLUME
    /* Volume coordinates preserve the three controlled footprints. */
#ifdef BUMP_NATIVE_CHAIN
    static const u32 cube_s[3][3] = {
        {0xc0168000u, 0x40298000u, 0x3e180000u},
        {0xc11e0000u, 0x41220000u, 0x3e000000u},
        {0xc09b8000u, 0x40a48000u, 0x3e100000u},
    };
#else
    static const u32 cube_s[3][3] = {
        {0xc1168000u, 0x41298000u, 0x3f180000u},
        {0xc21e0000u, 0x42220000u, 0x3f000000u},
        {0xc19b8000u, 0x41a48000u, 0x3f100000u},
    };
#endif
#else
    /* Fixed major coordinate one keeps the sampled quad within one face. */
    static const u32 cube_s[3][3] = {
        {0xc19e8000u, 0x41a18000u, 0x3e400000u},
        {0xc2a00000u, 0x42a00000u, 0x00000000u},
        {0xc21f8000u, 0x42208000u, 0x3e000000u},
    };
#endif
    method(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 9 * 4, 2u | (3u << 4));
    method(NV097_SET_BEGIN_END, 5);
    push(0x40000000u | (21u << 18) | NV097_INLINE_ARRAY);
#elif defined(BUMP_NATIVE_DEPTH)
    method(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 9 * 4, 2u | (3u << 4));
    method(NV097_SET_BEGIN_END, 5);
    push(0x40000000u | (21u << 18) | NV097_INLINE_ARRAY);
#else
    method(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 9 * 4, 2u | (2u << 4));
    method(NV097_SET_BEGIN_END, 5);
    push(0x40000000u | (18u << 18) | NV097_INLINE_ARRAY);
#endif
    for (u32 vertex = 0; vertex < 3; vertex++) {
        for (u32 component = 0; component < 4; component++) push(pos[vertex][component]);
#if defined(BUMP_NATIVE_CUBE_FACE) || defined(BUMP_NATIVE_VOLUME)
        u32 c = cube_s[footprint][vertex], neg = c ^ 0x80000000u;
        (void)neg;
#ifdef BUMP_NATIVE_VOLUME
        push(c);
#ifdef BUMP_NATIVE_CHAIN
        push(0x3d800000u); /* V = 0.5 / 8 */
        push(BUMP_NATIVE_VOLUME_SLICE ? 0x3f700000u : 0x3d800000u); /* Z 7 or 0 */
#else
        push(0x3e800000u);
        push(BUMP_NATIVE_VOLUME_SLICE ? 0x3f400000u : 0x3e800000u);
#endif
#elif BUMP_NATIVE_CUBE_FACE == 0
        push(0x3f800000u); push(0x3f000000u); push(neg);
#elif BUMP_NATIVE_CUBE_FACE == 1
        push(0xbf800000u); push(0x3f000000u); push(c);
#elif BUMP_NATIVE_CUBE_FACE == 2
        push(c); push(0x3f800000u); push(0xbf000000u);
#elif BUMP_NATIVE_CUBE_FACE == 3
        push(c); push(0xbf800000u); push(0x3f000000u);
#elif BUMP_NATIVE_CUBE_FACE == 4
        push(c); push(0x3f000000u); push(0x3f800000u);
#else
        push(neg); push(0x3f000000u); push(0xbf800000u);
#endif
#else
#ifdef BUMP_GATHER_BOUNDARY_U
        (void)tex_u; (void)footprint;
        push(layout ? BUMP_GATHER_BOUNDARY_U : BUMP_GATHER_BOUNDARY_LINEAR_U);
#else
#ifdef BUMP_LINEAR_LEVEL
        /* Scale normalized 4x4 coordinates to texels exactly by increasing
         * the exponent by two; these fixture values are finite and nonzero. */
        push(tex_u[layout][footprint][vertex] + 0x01000000u);
#else
#ifdef BUMP_NATIVE_F16_SHIFT
        union { u32 bits; float value; } shifted;
        shifted.bits = tex_u[layout][footprint][vertex];
        shifted.value += BUMP_NATIVE_F16_SHIFT;
        push(shifted.bits);
#else
        push(tex_u[layout][footprint][vertex]);
#endif
#endif
#endif
#ifdef BUMP_FILTER_CLAMP_PROBE
#ifdef BUMP_LINEAR_LEVEL
        push(tex_v[layout][footprint][vertex] + 0x01000000u);
#else
        push(tex_v[layout][footprint][vertex]);
#endif
#else
#ifdef BUMP_NATIVE_F16_GRID
        push(0x3fa00000u); /* V 1.25, fractional texel weight 0.75. */
#else
        push(layout ? 0x3e800000u : 0x3f000000u);
#endif
#endif
#ifdef BUMP_NATIVE_DEPTH
        push(BUMP_NATIVE_DEPTH_REF_BITS);
#endif
#endif
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
#ifdef BUMP_NATIVE_F16
        swizzled_layout = 0;
#endif
#ifdef BUMP_FILTER_LUMINANCE_PROBE
        flags = (id / 6) % 16;
        axis = 0;
        lum = 1;
#endif
#ifdef BUMP_FILTER_COLOR_PROBE
        flags = (id / 6) % 16;
        axis = lum = 0;
#endif
#ifdef BUMP_FILTER_CLAMP_PROBE
        flags = (id / 3) % 16;
        linear = flags & 1; /* Upper mip clamp 1/2, independent of footprint. */
#endif
        u32 format = swizzled_layout ? 0x28 : 0x17;
        u32 bump_address = BUMP_DATA + id * 16;
#ifdef BUMP_NATIVE_COLOR
        bump_address = BUMP_DATA + id * 32;
#ifdef BUMP_NATIVE_F16_REUSE
        bump_address = BUMP_DATA;
#endif
#if defined(BUMP_NATIVE_VOLUME) && defined(BUMP_NATIVE_CHAIN)
        bump_address = BUMP_DATA + id * 4096;
#endif
#ifdef BUMP_NATIVE_CUBE_FACE
        bump_address = BUMP_DATA + id * 1024;
#endif
#endif
#ifdef BUMP_FILTER_MIP_PROBE
        swizzled_layout = 1;
        format = 0x28;
        bump_address = BUMP_DATA + id * 64;
#ifdef BUMP_FILTER_WIDE_MIP_PROBE
        bump_address = BUMP_DATA + id * BUMP_MIP_BASE_SIZE * BUMP_MIP_BASE_SIZE * 4;
#endif
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
#ifdef BUMP_FILTER_WIDE_MIP_PROBE
        /* Adjacent Morton X pairs; word bit zero gives Y parity at every
         * nontrivial level. The last two levels are 2x2 and 1x1. */
        static const u32 pairs[][2] = {
            {0x404040c0u, 0xc040c0c0u}, {0x602060e0u, 0xa020a0e0u},
            {0x70f07010u, 0x90f09010u}, {0x503050d0u, 0xb030b0d0u},
#if BUMP_MIP_BASE_SIZE >= 32
            {0x602060e0u, 0xa020a0e0u},
#endif
#if BUMP_MIP_BASE_SIZE >= 64
            {0x404040c0u, 0xc040c0c0u},
#endif
#if BUMP_MIP_BASE_SIZE >= 128
            {0x503050d0u, 0xb030b0d0u},
#endif
        };
        u32 offset = 0, size = BUMP_MIP_BASE_SIZE;
        for (u32 level = 0; size > 1; level++) {
            for (u32 word = 0; word < size * size / 2; word++) {
                ram32(bump_address + offset + word * 4, pairs[level][word & 1]);
            }
            offset += size * size * 2;
            size >>= 1;
        }
        ram32(bump_address + offset, 0x7010u);
#else
#ifdef BUMP_FILTER_CLAMP_PROBE
        /* Morton words pair adjacent X texels; bit zero selects Y parity. */
        for (u32 word = 0; word < 8; word++) {
            ram32(bump_address + word * 4, word & 1 ? 0xc040c0c0u : 0x404040c0u);
        }
        ram32(bump_address + 32, 0x602060e0u);
        ram32(bump_address + 36, 0xa020a0e0u);
#else
        for (u32 word = 0; word < 8; word++) ram32(bump_address + word * 4, 0xc04040c0u);
        ram32(bump_address + 32, 0xa02060e0u);
        ram32(bump_address + 36, 0xa02060e0u);
#endif
        ram32(bump_address + 40, 0x7010u);
#endif
#endif
#ifdef BUMP_LINEAR_LEVEL
        swizzled_layout = 0;
        format = 0x17;
        /* Four row-major 4-texel rows, preserving the swizzled oracle's
         * independent alternating X/R and Y/G byte patterns. */
        for (u32 word = 0; word < 8; word++) {
            ram32(bump_address + word * 4,
                  (word / 2) & 1 ? 0xc040c0c0u : 0x404040c0u);
        }
#endif
#ifdef BUMP_NATIVE_COLOR
        format = swizzled_layout ? 0x06 : 0x12; /* ARGB8 */
        u32 first = 0xff000000u | ((16u + flags * 11u) << 16) |
                    ((32u + flags * 7u) << 8) | (240u - flags * 9u);
        u32 second = 0xff000000u | ((224u - flags * 9u) << 16) |
                     ((208u - flags * 7u) << 8) | (16u + flags * 11u);
#ifdef BUMP_NATIVE_CUBE_FACE
        u32 cube_first = first, cube_second = second;
#endif
#ifdef BUMP_NATIVE_RGB565
        format = swizzled_layout ? 0x05 : 0x11;
        first = ((first >> 8) & 0xf800u) | ((first >> 5) & 0x07e0u) | ((first >> 3) & 0x001fu);
        second = ((second >> 8) & 0xf800u) | ((second >> 5) & 0x07e0u) | ((second >> 3) & 0x001fu);
        ram32(bump_address, first | (second << 16));
        ram32(bump_address + 4, first | (second << 16));
#else
        ram32(bump_address, first); ram32(bump_address + 4, second);
        ram32(bump_address + 8, first); ram32(bump_address + 12, second);
#endif
#ifdef BUMP_NATIVE_CHAIN
        /* A green lower level exposes accidental interpolation across mips. */
        ram32(bump_address +
#ifdef BUMP_NATIVE_RGB565
              8, 0x07e0u);
#else
              16, 0xff00ff00u);
#endif
#endif
#ifdef BUMP_NATIVE_CUBE_FACE
        swizzled_layout = 1;
#ifdef BUMP_NATIVE_RGB565
        format = 0x05;
#else
        format = 0x06;
#endif
        /* NV2A cube faces are individually aligned to 128 bytes. Distinct
         * face colors and a second palette band expose face/address errors. */
        for (u32 face = 0; face < 6; face++) {
            u32 address = bump_address + face * 128;
            u32 c0 = cube_first + ((face * 5 + (id / 96) * 16) << 8);
            u32 c1 = cube_second + ((face * 5 + (id / 96) * 16) << 8);
#ifdef BUMP_NATIVE_RGB565
            c0 = ((c0 >> 8) & 0xf800u) | ((c0 >> 5) & 0x07e0u) | ((c0 >> 3) & 0x001fu);
            c1 = ((c1 >> 8) & 0xf800u) | ((c1 >> 5) & 0x07e0u) | ((c1 >> 3) & 0x001fu);
            ram32(address, c0 | (c1 << 16));
            ram32(address + 4, c0 | (c1 << 16));
#ifdef BUMP_NATIVE_CHAIN
            ram32(address + 8, ((128u + face * 16u) >> 2) << 5);
#endif
#else
            ram32(address, c0); ram32(address + 4, c1);
            ram32(address + 8, c0); ram32(address + 12, c1);
#ifdef BUMP_NATIVE_CHAIN
            ram32(address + 16, 0xff000000u | ((128u + face * 16u) << 8));
#endif
#endif
        }
#endif
#ifdef BUMP_NATIVE_VOLUME
        swizzled_layout = 1; format = 0x06;
#ifdef BUMP_NATIVE_CHAIN
        /* 8x8x8 Morton layout. Every depth slice has a distinct green offset;
         * a contrasting 4x4x4 lower mip exposes any unintended mip mixing. */
        for (u32 z = 0; z < 8; z++)
        for (u32 y = 0; y < 8; y++)
        for (u32 x = 0; x < 8; x++) {
            u32 texel = 0;
            for (u32 bit = 0; bit < 3; bit++) {
                texel |= ((x >> bit) & 1) << (3 * bit);
                texel |= ((y >> bit) & 1) << (3 * bit + 1);
                texel |= ((z >> bit) & 1) << (3 * bit + 2);
            }
            ram32(bump_address + texel * 4, (x & 1 ? second : first) + (z * 2u << 8));
        }
        for (u32 texel = 0; texel < 64; texel++)
            ram32(bump_address + 2048 + texel * 4, 0xff00ff00u);
#else
        /* Alternating X colors in each 2x2 slice; slice one has distinct G. */
        for (u32 z = 0; z < 2; z++) {
            u32 offset = z * 16u;
            u32 c0 = first + (z * 16u << 8), c1 = second + (z * 16u << 8);
            ram32(bump_address + offset, c0); ram32(bump_address + offset + 4, c1);
            ram32(bump_address + offset + 8, c0); ram32(bump_address + offset + 12, c1);
        }
#endif
#endif
#ifdef BUMP_NATIVE_DEPTH
#ifdef BUMP_NATIVE_F16
        format = 0x31;
        static const u32 codes[16] = {0,1,0x0fff,0x1000,0x1001,0x3fff,0x4000,0x4001,
                                      0x7fff,0x8000,0x8001,0xbfff,0xc000,0xc001,0xfffe,0xffff};
        u32 f16_first = codes[(id / 6) % 16];
#ifdef BUMP_NATIVE_F16_CONSTANT
        u32 f16_second = f16_first;
#else
        u32 f16_second = codes[15 - (id / 6) % 16];
#endif
        ram32(bump_address, f16_first | (f16_second << 16));
#ifdef BUMP_NATIVE_F16_GRID
        ram32(bump_address + 4, f16_second | (f16_first << 16));
#else
        ram32(bump_address + 4, f16_first | (f16_second << 16));
#endif
#else
        format = swizzled_layout ? 0x2c : 0x30; /* Fixed D16 */
        ram32(bump_address, 0xe0002000u); ram32(bump_address + 4, 0xe0002000u);
#endif
#ifdef BUMP_NATIVE_CHAIN
        ram32(bump_address + 8, 0x1000u); /* Contrasting lower mip */
#endif
#endif
        flags = 0; /* Independent colors, without signed-channel conversion. */
#endif
#ifdef BUMP_NATIVE_F16_ALIAS
        if (((id / 6) % 16) & 1) {
            /* Native clears must differ from RAM so bypassing the GPU
             * alias cannot accidentally satisfy the scalar oracle. */
            ram32(bump_address, 0); ram32(bump_address + 4, 0);
            method(0x290u, 1u << 12); /* Floating Z16 */
            method(NV097_SET_SURFACE_CLIP_H, 2u << 16);
            method(NV097_SET_SURFACE_CLIP_V, 2u << 16);
            method(NV097_SET_SURFACE_FORMAT, 0x118u);
            method(NV097_SET_SURFACE_PITCH, 8u | (4u << 16));
            method(NV097_SET_SURFACE_COLOR_OFFSET, 0x03800000u);
            method(NV097_SET_SURFACE_ZETA_OFFSET, bump_address);
            method(NV097_SET_ZSTENCIL_CLEAR_VALUE, f16_first);
#if defined(BUMP_NATIVE_F16_CONSTANT) && !defined(BUMP_NATIVE_F16_GRID)
            method(NV097_SET_CLEAR_RECT_H, 1u << 16);
#else
            method(NV097_SET_CLEAR_RECT_H, 0);
#endif
#ifdef BUMP_NATIVE_F16_GRID
            method(NV097_SET_CLEAR_RECT_V, 0);
#else
            method(NV097_SET_CLEAR_RECT_V, 1u << 16);
#endif
            method(NV097_SET_DEPTH_MASK, 1);
            method(NV097_CLEAR_SURFACE, 1);
#ifdef BUMP_NATIVE_F16_GRID
            method(NV097_SET_ZSTENCIL_CLEAR_VALUE, f16_second);
            method(NV097_SET_CLEAR_RECT_H, 0x00010001u);
            method(NV097_CLEAR_SURFACE, 1);
            method(NV097_SET_CLEAR_RECT_H, 0);
            method(NV097_SET_CLEAR_RECT_V, 0x00010001u);
            method(NV097_CLEAR_SURFACE, 1);
            method(NV097_SET_ZSTENCIL_CLEAR_VALUE, f16_first);
            method(NV097_SET_CLEAR_RECT_H, 0x00010001u);
            method(NV097_CLEAR_SURFACE, 1);
#elif !defined(BUMP_NATIVE_F16_CONSTANT)
            method(NV097_SET_ZSTENCIL_CLEAR_VALUE, f16_second);
            method(NV097_SET_CLEAR_RECT_H, 0x00010001u);
            method(NV097_CLEAR_SURFACE, 1);
#endif
            method(NV097_SET_DEPTH_MASK, 0);
            method(0x290u, 0);
            method(NV097_SET_SURFACE_ZETA_OFFSET, ZETA_B);
        }
#endif
        /* Reuse the established surface/transform setup, but defer its draw. */
        texture_alias_target(0xc0000000u | format, 1,
                             swizzled_layout ? norm_u[coord] : rect_u[coord],
                             0, bump_address, SURF_D);
        method(NV097_SET_TEXTURE_FORMAT,
               1u | (2u << 4) | (1u << 3) | (format << 8) |
               (1u << 16) | (1u << 20) | (1u << 24));
#if defined(BUMP_NATIVE_COLOR) && !defined(BUMP_NATIVE_RGB565) && !defined(BUMP_NATIVE_DEPTH)
        method(NV097_SET_TEXTURE_CONTROL1, 8u << 16);
#else
        method(NV097_SET_TEXTURE_CONTROL1, 4u << 16);
#endif
#ifdef BUMP_PRIME_SINGLE_ADDRESS
        method(NV097_SET_TEXTURE_ADDRESS, 0x00050505u);
#elif defined(BUMP_FILTER_CLAMP_PROBE)
        method(NV097_SET_TEXTURE_ADDRESS, BUMP_CLAMP_ADDRESS);
#else
        method(NV097_SET_TEXTURE_ADDRESS, 0x00010101u); /* wrap: AF-eligible */
#endif
        method(NV097_SET_TEXTURE_IMAGE_RECT, (2u << 16) | 2u);
#ifdef BUMP_BORDER_COLOR
        method(0x1b24u, BUMP_BORDER_COLOR);
#endif
#ifdef BUMP_FILTER_MIP_PROBE
        method(NV097_SET_TEXTURE_FORMAT,
               1u | (2u << 4) | (1u << 3) | (format << 8) |
#ifdef BUMP_FILTER_WIDE_MIP_PROBE
               ((BUMP_MIP_LOG_SIZE + 1u) << 16) |
               (BUMP_MIP_LOG_SIZE << 20) | (BUMP_MIP_LOG_SIZE << 24));
        method(NV097_SET_TEXTURE_CONTROL0, (1u << 30) | (BUMP_MIN_LOD << 18) |
               ((BUMP_MIP_LOG_SIZE - (linear ? 1u : 0u)) << 6));
#else
               (3u << 16) | (2u << 20) | (2u << 24));
        method(NV097_SET_TEXTURE_CONTROL0, (1u << 30) | (BUMP_MIN_LOD << 18) |
               ((linear ? 1u : 2u) << 6));
#endif
#endif
#ifdef BUMP_MAX_LOD_ZERO
        method(NV097_SET_TEXTURE_CONTROL0, (1u << 30) | (BUMP_MIN_LOD << 18));
#endif
#ifdef BUMP_SINGLE_MIP_LOG
        method(NV097_SET_TEXTURE_FORMAT,
               1u | (2u << 4) | (1u << 3) | (format << 8) |
               (1u << 16) | (BUMP_SINGLE_MIP_LOG << 20) | (BUMP_SINGLE_MIP_LOG << 24));
#endif
#ifdef BUMP_LINEAR_LEVEL
        method(NV097_SET_TEXTURE_FORMAT,
               1u | (2u << 4) | (1u << 3) | (format << 8) |
               (BUMP_LINEAR_MIP_COUNT << 16) | (2u << 20) | (2u << 24));
        method(NV097_SET_TEXTURE_CONTROL1, 8u << 16);
        method(NV097_SET_TEXTURE_IMAGE_RECT, (4u << 16) | 4u);
#endif
#ifdef BUMP_NATIVE_CHAIN
        method(NV097_SET_TEXTURE_FORMAT,
               1u | (2u << 4) | (1u << 3) | (format << 8) |
               (2u << 16) | (1u << 20) | (1u << 24));
        method(NV097_SET_TEXTURE_CONTROL0, (1u << 30) | (1u << 6));
#endif
#ifdef BUMP_NATIVE_MIP_FILTER
        method(NV097_SET_TEXTURE_CONTROL0, (1u << 30) | (BUMP_NATIVE_MIN_LOD << 18) |
#ifdef BUMP_NATIVE_CHAIN
               (1u << 6));
#else
               0);
#endif
#endif
#ifdef BUMP_NATIVE_BASE_ONLY
        method(NV097_SET_TEXTURE_CONTROL0, 1u << 30);
#endif
#ifdef BUMP_NATIVE_CUBE_FACE
        method(NV097_SET_TEXTURE_FORMAT,
               1u | (1u << 2) | (2u << 4) | (1u << 3) | (format << 8) |
#ifdef BUMP_NATIVE_CHAIN
               (2u << 16) |
#else
               (1u << 16) |
#endif
               (1u << 20) | (1u << 24));
#endif
#ifdef BUMP_NATIVE_VOLUME
        method(NV097_SET_TEXTURE_FORMAT,
               1u | (3u << 4) | (1u << 3) | (format << 8) |
#ifdef BUMP_NATIVE_CHAIN
               (2u << 16) | (3u << 20) | (3u << 24) | (3u << 28));
#else
               (1u << 16) | (1u << 20) | (1u << 24) | (1u << 28));
#endif
#endif
        method(NV097_SET_TEXTURE_FILTER,
#ifdef BUMP_NATIVE_MIP_FILTER
               (BUMP_NATIVE_MIP_FILTER << 16) | ((linear ? 2u : 1u) << 24) | BUMP_MIXED_BIAS |
#elif defined(BUMP_FILTER_MIP_PROBE)
               (BUMP_MIP_FILTER << 16) | (BUMP_MIP_MAG << 24) | BUMP_MIXED_BIAS |
#elif defined(BUMP_NATIVE_MATCHING)
               (linear ? 0x02020000u : 0x01010000u) | BUMP_MIXED_BIAS |
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
#ifdef BUMP_FILTER_LUMINANCE_PROBE
        method(0x1b38u + 64, BUMP_LUM_SCALE);
        method(0x1b3cu + 64, BUMP_LUM_OFFSET);
#else
        method(0x1b38u + 64, 0);           /* luminance scale 0 */
        method(0x1b3cu + 64, 0x3f800000u); /* luminance offset 1 */
#endif
        method(0x1e78u, 0); /* stage 1 always consumes stage 0 */
        method(NV097_SET_SHADER_STAGE_PROGRAM, 1u | ((lum ? 7u : 6u) << 5));
        method(NV097_SET_COMBINER_SPECULAR_FOG_CW0, 9); /* final RGB T1 */
        method(NV097_SET_COMBINER_SPECULAR_FOG_CW1, 0x00001900u);
#ifdef BUMP_FILTER_COLOR_PROBE
        method(NV097_SET_SHADER_STAGE_PROGRAM, 1);
        method(NV097_SET_COMBINER_SPECULAR_FOG_CW0, 8);
        method(NV097_SET_COMBINER_SPECULAR_FOG_CW1, 0x00001800u);
#endif
#ifdef BUMP_NATIVE_DEPTH
        method(NV097_SET_SHADER_STAGE_PROGRAM, 2);
        method(0x1e6cu, 4); /* Sampled depth > reference */
#endif
#ifdef BUMP_NATIVE_CUBE_FACE
        method(NV097_SET_SHADER_STAGE_PROGRAM, 3);
#endif
#ifdef BUMP_NATIVE_VOLUME
        method(NV097_SET_SHADER_STAGE_PROGRAM, 2);
#endif
#ifdef BUMP_FILTER_MIP_PROBE
#ifdef BUMP_PRIME_SINGLE_ADDRESS
        /* Bind the excluded legacy-clamp key without touching the sampled
         * pixel, then cross the address-only eligibility boundary. */
        method(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 9 * 4, 2u | (2u << 4));
        method(NV097_SET_BEGIN_END, 5);
        push(0x40000000u | (18u << 18) | NV097_INLINE_ARRAY);
        for (u32 vertex = 0; vertex < 3; vertex++) {
            push(0); push(0); push(0); push(0x3f800000u); push(0); push(0);
        }
        method(NV097_SET_BEGIN_END, 0);
        method(NV097_SET_TEXTURE_ADDRESS, BUMP_CLAMP_ADDRESS);
#endif
#ifdef BUMP_SAMPLER_TRANSITION
        u32 mip_sampler = (BUMP_MIP_FILTER << 16) | (BUMP_MIP_MAG << 24) |
            BUMP_MIXED_BIAS | ((flags & 1) ? 0x80000000u : 0) |
            ((flags & 2) ? 0x40000000u : 0) | ((flags & 4) ? 0x20000000u : 0) |
            ((flags & 8) ? 0x10000000u : 0);
        u32 lod0_sampler = (mip_sampler & ~0x000f0000u) |
            ((BUMP_MIP_FILTER & 1 ? 1u : 2u) << 16);
#ifdef BUMP_PRIME_LOD0
        method(NV097_SET_TEXTURE_FILTER, lod0_sampler);
#endif
        /* Bind the other view with an invisible degenerate primitive. The
         * final draw must select the correct cached view of the same image. */
        method(NV097_SET_VERTEX_DATA_ARRAY_FORMAT + 9 * 4, 2u | (2u << 4));
        method(NV097_SET_BEGIN_END, 5);
        push(0x40000000u | (18u << 18) | NV097_INLINE_ARRAY);
        for (u32 vertex = 0; vertex < 3; vertex++) {
            push(0); push(0); push(0); push(0x3f800000u); push(0); push(0);
        }
        method(NV097_SET_BEGIN_END, 0);
        method(NV097_SET_TEXTURE_FILTER,
#ifdef BUMP_PRIME_LOD0
               mip_sampler);
#else
               lod0_sampler);
#endif
#endif
        bump_mixed_triangle(id /
#ifdef BUMP_FILTER_CLAMP_PROBE
                            48,
#else
                            96,
#endif
                            coord);
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
