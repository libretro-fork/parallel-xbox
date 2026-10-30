/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "hw/xbox/nv2a/pgraph/vram-ranges.h"

/* Independent endpoint arithmetic, keeping the carry instead of wrapping. */
static bool endpoint_reference(uint64_t a, uint64_t n, uint64_t b, uint64_t m)
{
    uint64_t a_end = a + n, b_end = b + m;
    bool a_carry = a_end < a, b_carry = b_end < b;
    return n && m && (b_carry || a < b_end) && (a_carry || b < a_end);
}
static uint64_t random_state = UINT64_C(0x9837c0ab71542d6f);
static uint64_t random_value(void)
{
    random_state ^= random_state << 13; random_state ^= random_state >> 7;
    random_state ^= random_state << 17; return random_state;
}
int main(void)
{
    unsigned small = 0;
    for (unsigned a = 0; a < 32; a++)
    for (unsigned n = 0; n <= 8; n++)
    for (unsigned b = 0; b < 32; b++)
    for (unsigned m = 0; m <= 8; m++) {
        bool expected = false;
        for (unsigned i = 0; i < n; i++)
        for (unsigned j = 0; j < m; j++) expected |= a + i == b + j;
        assert(pgraph_vram_ranges_overlap(a, n, b, m) == expected);
        assert(endpoint_reference(a, n, b, m) == expected);
        small++;
    }
    for (unsigned i = 0; i < 500000; i++) {
        uint64_t a = random_value(), n = random_value();
        uint64_t b = random_value(), m = random_value();
        if (i % 5 == 0) n = 0;
        if (i % 7 == 0) m = 0;
        assert(pgraph_vram_ranges_overlap(a, n, b, m) == endpoint_reference(a, n, b, m));
        assert(pgraph_vram_ranges_overlap(a, n, b, m) == pgraph_vram_ranges_overlap(b, m, a, n));
    }
    assert(pgraph_vram_ranges_overlap(64, 2048, 64, 1));
    assert(pgraph_vram_ranges_overlap(64, 2048, 2111, 1));
    assert(!pgraph_vram_ranges_overlap(64, 2048, 2112, 1));
    assert(!pgraph_vram_ranges_overlap(64, 2048, 65, 0));
    assert(!pgraph_vram_ranges_overlap(65, 0, 64, 2048));
    assert(pgraph_vram_ranges_overlap(UINT64_MAX, 1, UINT64_MAX, 1));
    assert(!pgraph_vram_ranges_overlap(UINT64_MAX - 1, 1, UINT64_MAX, 1));
    assert(!pgraph_vram_ranges_overlap(UINT64_MAX - 2, 8, 0, 8));
    printf("VRAM ranges: %u byte-walk and 500000 wide endpoint cases passed\n", small);
    return 0;
}
