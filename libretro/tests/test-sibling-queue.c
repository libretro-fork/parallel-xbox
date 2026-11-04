/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>
#include <retro_spsc.h>
#include <retro_atomic.h>
#include "hw/xbox/nv2a/pgraph/glsl/shaders.h"
#define QEMU_ALWAYS_INLINE __attribute__((always_inline))
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#define VK_SPEC_RING 256
#define VK_SPEC_READY (3 * VK_SPEC_RING)
#define VK_SPEC_QUEUED 1
#define QEMU_THREAD_DETACHED 1
typedef struct ShaderSource ShaderSource;
typedef int retro_eventcount_t;
typedef int QemuThread;
/* TYPES */
static struct { uint64_t hash; int phase; } vk_spec_track[1024];
static retro_spsc_t vk_spec_in, vk_spec_out;
static retro_eventcount_t vk_spec_ec, vk_spec_idle_ec;
static retro_atomic_int_t vk_spec_inflight;
static unsigned vk_spec_undrained, vk_spec_ready_order[VK_SPEC_READY];
static bool vk_spec_started;
static QemuThread vk_spec_thread;
static unsigned attempts, fail_at, drains, notifications, threads;
static bool test_init(retro_spsc_t *q, size_t bytes)
{
    if (++attempts == fail_at) return false;
    return retro_spsc_init(q, bytes);
}
static bool retro_eventcount_init(retro_eventcount_t *ec)
{
    (void)ec;
    return ++attempts != fail_at;
}
static void retro_eventcount_notify(retro_eventcount_t *ec)
{
    assert(ec == &vk_spec_ec);
    notifications++;
}
static void *vk_spec_worker_fn(void *p) { return p; }
static void qemu_thread_create(QemuThread *t, const char *name,
                              void *(*fn)(void *), void *p, int flags)
{
    assert(t == &vk_spec_thread && !strcmp(name, "vk-firstmeet"));
    assert(fn == vk_spec_worker_fn && !p && flags == QEMU_THREAD_DETACHED);
    threads++;
}
static int vk_spec_find(uint64_t h)
{
    for (unsigned i = 0; i < ARRAY_SIZE(vk_spec_track); i++)
        if (vk_spec_track[i].phase && vk_spec_track[i].hash == h) return (int)i;
    return -1;
}
static void vk_spec_drain(void) { drains++; }
#define retro_spsc_init test_init
/* FUNCTIONS */
#undef retro_spsc_init

typedef struct Snapshot {
    unsigned attempts, drains, notifications, threads, undrained;
    int inflight;
    bool started;
    size_t head, tail, bytes;
    uint8_t data[8192];
    uint8_t tracks[sizeof(vk_spec_track)];
    unsigned ready[VK_SPEC_READY];
} Snapshot;
static uint32_t rng = 0x89231abc;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static Snapshot run(unsigned version, unsigned mode, size_t start,
                    const ShaderState *input)
{
    const uint64_t hash = UINT64_C(0x8877665544332211);
    Snapshot result;
    memset(&result, 0, sizeof(result));
    memset(&vk_spec_in, 0, sizeof(vk_spec_in));
    memset(&vk_spec_out, 0, sizeof(vk_spec_out));
    memset(vk_spec_track, 0, sizeof(vk_spec_track));
    memset(vk_spec_ready_order, 0, sizeof(vk_spec_ready_order));
    attempts = drains = notifications = threads = vk_spec_undrained = 0;
    fail_at = mode >= 7 ? mode - 6 : 0;
    retro_atomic_int_init(&vk_spec_inflight, 0);
    vk_spec_started = mode < 6;
    if (vk_spec_started) {
        assert(retro_spsc_init(&vk_spec_in, 8192));
        retro_atomic_size_init(&vk_spec_in.head, start);
        retro_atomic_size_init(&vk_spec_in.tail, start);
        vk_spec_in.cached_head = vk_spec_in.cached_tail = start;
    }
    if (mode == 1) { vk_spec_track[0].hash = hash; vk_spec_track[0].phase = 1; }
    if (mode == 2) vk_spec_undrained = VK_SPEC_RING;
    if (mode == 3) for (unsigned i = 0; i < ARRAY_SIZE(vk_spec_track); i++) {
        vk_spec_track[i].hash = i + 1; vk_spec_track[i].phase = 1;
    }
    if (mode == 4 || mode == 5) {
        uint8_t fill[8192]; memset(fill, 0x68, sizeof(fill));
        size_t n = mode == 4 ? sizeof(fill) : sizeof(fill) - 1000;
        assert(retro_spsc_write(&vk_spec_in, fill, n) == n);
    }
    ShaderState base, oracle;
    memcpy(&base, input, sizeof(base));
    ShaderStateView view;
    view.head = &base;
    memcpy(&view.tail, (const uint8_t *)&base + VK_SHADER_HEAD_BYTES, sizeof(view.tail));
    /* The tail differs from the borrowed source, as canonicalization does. */
    ((uint8_t *)&view.tail)[17] ^= 0x80;
    memcpy(&oracle, &base, sizeof(oracle));
    memcpy((uint8_t *)&oracle + VK_SHADER_HEAD_BYTES, &view.tail, sizeof(view.tail));
    if (version == 0) reference_queue(hash, &oracle);
    else if (version == 1) vk_spec_queue(hash, &oracle);
    else vk_spec_queue_view(hash, &view);
    memset(&base, 0xdb, sizeof(base));
    memset(&view.tail, 0xdc, sizeof(view.tail));
    result.attempts = attempts; result.drains = drains;
    result.notifications = notifications; result.threads = threads;
    result.undrained = vk_spec_undrained;
    result.inflight = retro_atomic_load_relaxed_int(&vk_spec_inflight);
    result.started = vk_spec_started;
    memcpy(result.tracks, vk_spec_track, sizeof(vk_spec_track));
    memcpy(result.ready, vk_spec_ready_order, sizeof(vk_spec_ready_order));
    if (vk_spec_in.buffer) {
        result.head = retro_atomic_load_relaxed_size(&vk_spec_in.head);
        result.tail = retro_atomic_load_relaxed_size(&vk_spec_in.tail);
        result.bytes = retro_spsc_read_avail(&vk_spec_in);
        assert(result.bytes <= sizeof(result.data));
        assert(retro_spsc_read(&vk_spec_in, result.data, result.bytes) == result.bytes);
    }
    if (mode == 0 || mode == 6) {
        assert(result.notifications == 1 && result.inflight == 1 && result.undrained == 1);
        assert(result.bytes == sizeof(oracle) && !memcmp(result.data, &oracle, sizeof(oracle)));
    } else assert(!result.notifications && !result.inflight);
    if (mode == 5 && version == 2) assert(result.bytes == 7192); /* No partial record. */
    retro_spsc_free(&vk_spec_in);
    retro_spsc_free(&vk_spec_out);
    return result;
}
int main(void)
{
    ShaderState state;
    for (size_t i = 0; i < sizeof(state); i++) ((uint8_t *)&state)[i] = (uint8_t)next();
    for (unsigned trial = 0; trial < 10000; trial++) {
        unsigned mode = trial % 11;
        size_t start = (trial & 1) ? SIZE_MAX - next() % 16384 : next();
        Snapshot a = run(0, mode, start, &state);
        Snapshot b = run(1, mode, start, &state);
        assert(!memcmp(&a, &b, sizeof(a)));
        Snapshot c = run(2, mode, start, &state);
        if (mode != 5) assert(!memcmp(&a, &c, sizeof(a)));
        ((uint8_t *)&state)[next() % sizeof(state)] ^= 1;
    }
    puts("10000 queue schedules: actual startup/failure guards, tracking, notifications, owned bytes and rollover match; view writes reject partial records");
    return 0;
}
