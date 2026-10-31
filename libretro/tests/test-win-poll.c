/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#ifdef _WIN32
#include <windows.h>
#else
#define WIN_POLL_MOCK 1
#define _WIN32 1
#define MAXIMUM_WAIT_OBJECTS 64
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_IO_COMPLETION 192u
#define WAIT_FAILED 0xffffffffu
#define INFINITE 0xffffffffu
#define TRUE 1
#define FALSE 0
#define TIMER_ALL_ACCESS 0x1f0003
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 2
#define HANDLE uintptr_t
#define DWORD unsigned
#define LARGE_INTEGER MockInteger
typedef struct { int64_t QuadPart; } MockInteger;
#endif
#define XBOX 1
#define G_OS_WIN32 1
typedef int gint;
typedef unsigned guint;
typedef unsigned short gushort;
typedef intptr_t gintptr;
/* POLLTYPE */
#define G_IO_IN 1
#define MIN(a,b) ((a)<(b)?(a):(b))
#define DIV_ROUND_UP(n,d) (((n)+(d)-1)/(d))
#define SCALE_MS 1000000
static unsigned copies;
static __attribute__((unused)) void *counted_memcpy(void *dst, const void *src, size_t n)
{
    copies++;
    return memcpy(dst, src, n);
}
#define memcpy counted_memcpy
#define g_newa(t,n) ((t *)__builtin_alloca(sizeof(t)*(n)))
#ifdef WIN_POLL_MOCK
static bool signal_state[128], automatic[128];
static unsigned mode, create_calls, set_calls, slept, create_fail, set_fail;
static gint last_timeout;
static int64_t last_due;
static void retro_sleep(unsigned ms) { slept = ms; }
static DWORD WaitForSingleObject(HANDLE h, DWORD timeout)
{
    (void)timeout;
    assert(h && h < 128);
    if (!signal_state[h]) return WAIT_TIMEOUT;
    if (automatic[h]) signal_state[h] = false;
    return WAIT_OBJECT_0;
}
static DWORD WaitForMultipleObjectsEx(DWORD n, const HANDLE *h, int all,
                                      DWORD timeout, int alertable)
{
    assert(n && n <= 64 && !all && alertable);
    last_timeout = (gint)timeout;
    if (mode == 1) return WAIT_FAILED;
    if (mode == 2) return WAIT_IO_COMPLETION;
    for (unsigned i = 0; i < n; i++) {
        if (WaitForSingleObject(h[i], 0) == WAIT_OBJECT_0)
            return WAIT_OBJECT_0 + i;
    }
    return WAIT_TIMEOUT;
}
static HANDLE CreateWaitableTimerExW(void *attr, void *name, DWORD flags, DWORD access)
{
    (void)attr; (void)name;
    assert(flags == CREATE_WAITABLE_TIMER_HIGH_RESOLUTION && access == TIMER_ALL_ACCESS);
    create_calls++;
    automatic[127] = true;
    return create_fail ? 0 : 127;
}
static int SetWaitableTimer(HANDLE h, const LARGE_INTEGER *due, int period,
                           void *callback, void *opaque, int resume)
{
    (void)callback; (void)opaque;
    assert(h == 127 && !period && !resume && due->QuadPart < 0);
    set_calls++; last_due = due->QuadPart;
    signal_state[127] = true;
    return !set_fail;
}
#else
static void retro_sleep(unsigned ms) { Sleep(ms); }
#endif
/* BASELINE */
/* POLL */
/* TIMEOUT */
/* HR */
#undef memcpy
static __attribute__((unused)) int baseline_poll_with_handle(GPollFD *fds, unsigned n, int timeout, gintptr extra)
{
    GPollFD *all = calloc(n + 1, sizeof(*all));
    assert(all);
    memcpy(all, fds, n * sizeof(*fds));
    all[n].fd = extra; all[n].events = G_IO_IN;
    int result = baseline_g_poll(all, n + 1, timeout);
    memcpy(fds, all, n * sizeof(*fds));
    if (result > 0 && all[n].revents) result--;
    free(all);
    return result;
}
#ifdef WIN_POLL_MOCK
static uint32_t seed = 0x38973422;
static unsigned random32(void) { seed ^= seed << 13; seed ^= seed >> 17; return seed ^= seed << 5; }
static void compare(unsigned n, gintptr extra)
{
    GPollFD *a = calloc(n + 2, sizeof(*a)), *b = calloc(n + 2, sizeof(*b));
    bool saved[128];
    assert(a && b);
    a[0].fd = a[n+1].fd = 0x555555;
    for (unsigned i = 0; i < n; i++) {
        a[i+1].fd = (gintptr)(random32() % 93) - 2;
        a[i+1].events = random32() & 7;
        a[i+1].revents = 0xffff;
    }
    memcpy(b, a, (n+2)*sizeof(*a));
    for (unsigned i = 0; i < 128; i++) {
        signal_state[i] = (random32() & 3) == 0;
        automatic[i] = (random32() & 1) != 0;
    }
    memcpy(saved, signal_state, sizeof(saved));
    int expected = baseline_poll_with_handle(b+1, n, 17, extra);
    memcpy(signal_state, saved, sizeof(saved));
    unsigned before = copies;
    int actual = g_poll_with_handle(a+1, n, 17, extra);
    assert(copies == before && actual == expected);
    assert(memcmp(a, b, (n+2)*sizeof(*a)) == 0);
    assert(a[0].fd == 0x555555 && a[n+1].fd == 0x555555);
    free(a); free(b);
}
#endif
int main(int argc, char **argv)
{
#ifdef WIN_POLL_MOCK
    if (argc > 1) {
        create_fail = !strcmp(argv[1], "create-fail");
        set_fail = !strcmp(argv[1], "set-fail");
        GPollFD p = {1, G_IO_IN, 7};
        assert(xbox_win_poll_hr(&p, 1, 1000) == -2);
        assert(p.fd == 1 && p.events == G_IO_IN && p.revents == 7);
        assert(xbox_win_poll_hr(&p, 1, 1000) == -2);
        assert(create_calls == 1 && set_calls == (set_fail ? 2u : 0u));
        puts("Windows HR poll: unavailable/set-failure fallback PASS");
        return 0;
    }
    for (unsigned i = 0; i < 20000; i++) {
        mode = i % 3;
        compare(i % 81, (i & 1) ? 127 : 0);
    }
    mode = 0;
    memset(signal_state, 0, sizeof(signal_state));
    unsigned before = copies;
    assert(xbox_win_poll_hr(NULL, 0, 1) == 0);
    assert(last_due == -1 && last_timeout == 2);
    assert(xbox_win_poll_hr(NULL, 0, INT64_MAX) == 0);
    assert(last_timeout == INT32_MAX && copies == before);
    GPollFD guarded[3] = {{77, 3, 9}, {1, G_IO_IN, 0xffff}, {88, 4, 7}};
    signal_state[1] = true; automatic[1] = true;
    assert(xbox_win_poll_hr(&guarded[1], 1, 1000000) == 1);
    assert(guarded[1].revents == G_IO_IN);
    assert(guarded[0].fd == 77 && guarded[0].revents == 9);
    assert(guarded[2].fd == 88 && guarded[2].revents == 7);
    assert(copies == before && create_calls == 1);
    puts("Windows poll: 20000 baseline comparisons, canaries, 64-handle limit, errors/APCs, timer-only wake, saturation and zero descriptor copies PASS");
#else
    (void)argc; (void)argv;
    HANDLE event = CreateEvent(NULL, TRUE, TRUE, NULL);
    assert(event);
    GPollFD p[3] = {{77, 3, 9}, {(gintptr)event, G_IO_IN, 0xffff}, {88, 4, 7}};
    assert(g_poll(&p[1], 1, 0) == 1);
    unsigned before = copies;
    int result = xbox_win_poll_hr(&p[1], 1, 1000000);
    assert(result == 1 || result == -2); /* Optional HR timer on old Windows. */
    assert(copies == before && p[0].revents == 9 && p[2].revents == 7);
    assert(p[1].fd == (gintptr)event && p[1].events == G_IO_IN);
    assert(g_poll_with_handle(NULL, 0, 0, (gintptr)event) == 0);
    ResetEvent(event);
    assert(g_poll_with_handle(&p[1], 1, 0, 0) == 0 && !p[1].revents);
    assert(g_poll_with_handle(NULL, 0, 0, (gintptr)INVALID_HANDLE_VALUE) == 0);
    HANDLE handles[64]; GPollFD fds[64];
    for (unsigned i = 0; i < 64; i++) {
        handles[i] = CreateEvent(NULL, TRUE, i == 63, NULL); assert(handles[i]);
        fds[i] = (GPollFD){(gintptr)handles[i], G_IO_IN, 0xffff};
    }
    assert(g_poll_with_handle(fds, 64, 0, (gintptr)event) == 1);
    assert(fds[63].revents == G_IO_IN);
    for (unsigned i = 0; i < 64; i++) CloseHandle(handles[i]);
    CloseHandle(event);
    puts("Native Windows poll: event/timer wakes, exact-size arrays, canaries and all 64 descriptors PASS");
#endif
    return 0;
}
