/*
 * Adapted from SWH LADSPA Plugins package, modified for xemu
 *
 * Source:  https://github.com/swh/ladspa/blob/master/svf_1214.xml
 * Author:  Steve Harris, andy@vellocet
 * License: GPLv2
 *
 */

#ifndef SVF_H
#define SVF_H

#include <stdint.h>

/* Hardware keeps filter values to 1/16 of a 24-bit sample, rounding halves
 * up (measured): here a value of 16 sample steps. svf_round takes a value
 * scaled up by 2^shift and gives it on that grid, in sample steps,
 * saturated at 128 times full scale, so an unstable filter (a resonance
 * the coefficients drive past unity) clips instead of wrapping. */
#define SVF_LIMIT (1 << 30)
static inline int32_t svf_round(int64_t x, int shift)
{
    int64_t y = ((x + ((int64_t)1 << (shift + 3))) >> (shift + 4)) * 16;

    return (int32_t)(y > SVF_LIMIT - 16 ? SVF_LIMIT - 16 :
                     y < -SVF_LIMIT ? -SVF_LIMIT : y);
}

// Constants to match filter types
#define F_LP 1
#define F_HP 2
#define F_BP 3
#define F_BR 4
#define F_AP 5

/* Structure to hold parameters for SV filter: the cutoff coefficient in
 * 1/65536 (the measured grid, 0..65535), the damping in 1/32768, the
 * outputs in sample steps. The cutoff moves from frame to frame in a ramp
 * the caller runs, at 1/2^21: run_svf takes the sample's own. */
typedef struct {
    int32_t f;   // cutoff coefficient, 1/65536
    int32_t q;   // damping coefficient, 1/32768
    int32_t h;   // high pass output
    int32_t b;   // band pass output
    int32_t l;   // low pass output
    int32_t p;   // peaking output (allpass with resonance)
    int32_t n;   // notch output
    int32_t *op; // pointer to output value
} sv_filter;

static inline void setup_svf(sv_filter *sv, int32_t fc, int32_t q, int t) {
    sv->f = fc;
    sv->q = q;
    switch(t) {
    case F_LP:
        sv->op = &(sv->l);
        break;
    case F_HP:
        sv->op = &(sv->h);
        break;
    case F_BP:
        sv->op = &(sv->b);
        break;
    case F_BR:
        sv->op = &(sv->n);
        break;
    default:
        sv->op = &(sv->p);
    }
}

/* Run one sample through the SV filter (by andy@vellocet), with the cutoff
 * f21 in 1/2^21. No input scaling or waveshaping: on hardware the filter is
 * linear with unity passband gain at any resonance. Each step is exact
 * before its rounding to the grid. */
static inline int32_t run_svf(sv_filter *sv, int32_t in, int64_t f21) {
    sv->h = svf_round(((int64_t)in - sv->l) * 32768 - (int64_t)sv->q * sv->b,
                      15);
    /* Feedback can be negative: multiply instead of signed left shift.
     * Saturated filter states scaled by 2^21 fit exactly in int64_t. */
    const int64_t scale = (int64_t)1 << 21;
    sv->b = svf_round((int64_t)sv->b * scale + f21 * sv->h, 21);
    sv->l = svf_round((int64_t)sv->l * scale + f21 * sv->b, 21);
    sv->n = sv->l + sv->h;
    sv->p = sv->l - sv->h;
    return *(sv->op);
}

#endif
