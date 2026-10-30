/* Exact wide-integer oracle for the VP state-variable filter.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include "hw/xbox/mcpx/apu/vp/svf.h"

typedef struct { int32_t h, b, l, n, p; } Reference;
static uint64_t random_state = 0x4718ac63f0b259deULL;
static unsigned checks;

static uint32_t random32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 7;
    random_state ^= random_state << 17;
    return random_state >> 32;
}

/* Divide with mathematical floor, rather than shifting a negative value. */
static int32_t round_reference(__int128 value, unsigned shift)
{
    __int128 denominator = (__int128)1 << (shift + 4);
    __int128 numerator = value + denominator / 2;
    __int128 quotient = numerator / denominator;
    if (numerator < 0 && numerator % denominator) {
        quotient--;
    }
    __int128 result = quotient * 16;
    if (result < -SVF_LIMIT) return -SVF_LIMIT;
    if (result > SVF_LIMIT - 16) return SVF_LIMIT - 16;
    return result;
}

static void check(sv_filter *filter, Reference *reference, int type,
                  int32_t input, int64_t cutoff)
{
    reference->h = round_reference(
        ((__int128)input - reference->l) * 32768 -
        (__int128)filter->q * reference->b, 15);
    reference->b = round_reference((__int128)reference->b * 2097152 +
                                   (__int128)cutoff * reference->h, 21);
    reference->l = round_reference((__int128)reference->l * 2097152 +
                                   (__int128)cutoff * reference->b, 21);
    reference->n = (int64_t)reference->l + reference->h;
    reference->p = (int64_t)reference->l - reference->h;
    int32_t expected = type == F_LP ? reference->l :
                       type == F_HP ? reference->h :
                       type == F_BP ? reference->b :
                       type == F_BR ? reference->n : reference->p;
    assert(run_svf(filter, input, cutoff) == expected);
    assert(filter->h == reference->h && filter->b == reference->b &&
           filter->l == reference->l && filter->n == reference->n &&
           filter->p == reference->p);
    checks++;
}

int main(void)
{
    const int32_t states[] = { -SVF_LIMIT, -16, 0, 16, SVF_LIMIT - 16 };
    const int32_t coefficients[] = { 0, 1, 16384, 32768, 65535 };
    const int32_t inputs[] = { INT32_MIN, -8388608, -1, 0, 1,
                              8388607, INT32_MAX };
    for (unsigned b = 0; b < 5; b++)
    for (unsigned l = 0; l < 5; l++)
    for (unsigned f = 0; f < 5; f++)
    for (unsigned q = 0; q < 5; q++)
    for (int type = F_LP; type <= F_AP; type++)
    for (unsigned i = 0; i < 7; i++) {
        sv_filter filter = { .b = states[b], .l = states[l] };
        Reference reference = { .b = states[b], .l = states[l] };
        setup_svf(&filter, coefficients[f], coefficients[q], type);
        check(&filter, &reference, type, inputs[i],
              (int64_t)coefficients[f] * 32);
    }
    for (unsigned sequence = 0; sequence < 1000; sequence++)
    for (int type = F_LP; type <= F_AP; type++) {
        int32_t b = ((int64_t)(random32() % 134217728) - 67108864) * 16;
        int32_t l = ((int64_t)(random32() % 134217728) - 67108864) * 16;
        sv_filter filter = { .b = b, .l = l };
        Reference reference = { .b = b, .l = l };
        setup_svf(&filter, random32() % 65536, random32() % 65536, type);
        for (unsigned sample = 0; sample < 128; sample++) {
            int32_t input = (int64_t)random32() - 2147483648LL;
            check(&filter, &reference, type, input,
                  random32() % (65535 * 32 + 1));
        }
    }
    printf("SVF: %u boundary/history samples match 128-bit oracle, all five outputs\n",
           checks);
    return 0;
}
