/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define JVS_SYNC 0xe0
#define JVS_HOST_ADDR 0
static int production(const uint8_t *input, int ppos, uint8_t *out, int out_max)
{
    uint8_t payload[256];
    memcpy(payload, input, ppos);
/* PRODUCTION */

static int reference(const uint8_t *payload, int n, uint8_t *out, int capacity)
{
    uint8_t frame[260];
    frame[0] = JVS_SYNC;
    frame[1] = JVS_HOST_ADDR;
    frame[2] = n + 1;
    memcpy(frame + 3, payload, n);
    uint8_t sum = frame[1] + frame[2];
    for (int i = 0; i < n; i++) sum += payload[i];
    frame[n + 3] = sum;
    int length = MIN(n + 4, capacity);
    memcpy(out, frame, length);
    return length;
}
int main(void)
{
    unsigned cases = 0;
    uint8_t payload[256], actual[280], expected[280];
    for (unsigned pattern = 0; pattern < 4; pattern++) {
        for (unsigned i = 0; i < sizeof(payload); i++) payload[i] = pattern == 0 ? 0 : pattern == 1 ? 255 : pattern == 2 ? i * 37 : (i & 1 ? 0xd0 : 0xe0);
        for (int n = 0; n <= 256; n++)
        for (int capacity = 0; capacity <= 264; capacity++)
        for (unsigned alias = 0; alias < 2; alias++) {
            memset(actual, 0x5a, sizeof(actual));
            memset(expected, 0x5a, sizeof(expected));
            if (alias) {
                memcpy(actual + 8, payload, n);
                memcpy(expected + 8, payload, n);
            }
            int want = reference(payload, n, expected + 8, capacity);
            int got = production(alias ? actual + 8 : payload, n, actual + 8, capacity);
            assert(got == want && !memcmp(actual, expected, sizeof(actual)));
            cases++;
        }
    }
    assert(production(payload, 256, NULL, 0) == 0);
    printf("JVS frame: %u payload/truncation/checksum/alias/canary cases PASS\n", cases);
}
