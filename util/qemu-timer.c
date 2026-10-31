/*
 * QEMU System Emulator
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2026 Réda Chérif-Touil
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu/main-loop.h"
#include "qemu/timer.h"
#include "qemu/cpu-boost.h"
#include "qemu/lockable.h"
#include "system/cpu-timers.h"
#include "exec/icount.h"
#include "system/cpus.h"

#ifdef CONFIG_POSIX
#include <pthread.h>
#endif

#ifdef CONFIG_PPOLL
#include <poll.h>
#endif

#ifdef CONFIG_PRCTL_PR_SET_TIMERSLACK
#include <sys/prctl.h>
#endif

/***********************************************************/
/* timers */

/* All core timers belong to one queue per clock. AIO contexts never
 * attach timers, so they do not need independent clock/list hierarchies. */
struct QEMUTimerList {
    QEMUClockType type;
    bool enabled;
    bool initialized;
    QemuMutex active_timers_lock;
    QEMUTimer *active_timers;
    QEMUTimerListNotifyCB *notify_cb;
    QemuEvent timers_done_ev;
};

static QEMUTimerList qemu_clocks[QEMU_CLOCK_MAX];

/*
 * Whether a timer list is empty is read without its lock, so every store
 * to a link (the head among them) is a single relaxed atomic one.
 */
#define TIMER_LINK(pt) ((retro_atomic_ptr_t *)(pt))
#define timer_link_set(pt, v) retro_atomic_store_relaxed_ptr(TIMER_LINK(pt), (v))
#define timer_list_head(tl) \
    ((QEMUTimer *)retro_atomic_load_relaxed_ptr(TIMER_LINK(&(tl)->active_timers)))

/**
 * qemu_clock_ptr:
 * @type: type of clock
 *
 * Translate a clock type into a pointer to its core timer queue.
 *
 * Returns: a pointer to the core timer queue
 */
static inline QEMUTimerList *qemu_clock_ptr(QEMUClockType type)
{
    return &qemu_clocks[type];
}

static bool timer_expired_ns(const QEMUTimer *timer_head, int64_t current_time)
{
    return timer_head && (timer_head->expire_time <= current_time);
}

static void qemu_clock_init(QEMUClockType type, QEMUTimerListNotifyCB *notify_cb)
{
    QEMUTimerList *clock = qemu_clock_ptr(type);
    assert(!clock->initialized);
    clock->type = type;
    clock->enabled = type != QEMU_CLOCK_VIRTUAL;
    clock->notify_cb = notify_cb;
    qemu_event_init(&clock->timers_done_ev, true);
    qemu_mutex_init(&clock->active_timers_lock);
    clock->initialized = true;
}

bool qemu_clock_use_for_deadline(QEMUClockType type)
{
    return !(icount_enabled() && (type == QEMU_CLOCK_VIRTUAL));
}

void qemu_clock_notify(QEMUClockType type)
{
    QEMUTimerList *clock = qemu_clock_ptr(type);
    if (clock->notify_cb) {
        clock->notify_cb(NULL, type);
    } else {
        qemu_notify_event();
    }
}

/* Disabling the clock waits for its callback dispatch to stop
 * executing qemu_run_timers.  Thus, this functions should not
 * be used from the callback of a timer that is based on @clock.
 * Doing so would cause a deadlock.
 *
 * Caller should hold BQL.
 */
void qemu_clock_enable(QEMUClockType type, bool enabled)
{
    QEMUTimerList *clock = qemu_clock_ptr(type);
    bool old = clock->enabled;
    clock->enabled = enabled;
    if (enabled && !old) {
        qemu_clock_notify(type);
    } else if (!enabled && old) {
        qemu_event_wait(&clock->timers_done_ev);
    }
}

bool qemu_clock_expired(QEMUClockType type)
{
    QEMUTimerList *timer_list = qemu_clock_ptr(type);
    int64_t expire_time = 0;

    if (!timer_list_head(timer_list)) {
        return false;
    }

    WITH_QEMU_LOCK_GUARD(&timer_list->active_timers_lock) {
        if (!timer_list->active_timers) {
            return false;
        }
        expire_time = timer_list->active_timers->expire_time;
    }

    return expire_time <= qemu_clock_get_ns(timer_list->type);
}

/* Calculate the soonest matching deadline on this clock. For icount timeouts
 * we ignore whether the clock is used in host deadline
 * calculations.
 */
int64_t qemu_clock_deadline_ns_all(QEMUClockType type, int attr_mask)
{
    QEMUTimerList *clock = qemu_clock_ptr(type);
    QEMUTimer *ts;
    int64_t expire_time;

    if (!clock->enabled || !timer_list_head(clock)) {
        return -1;
    }
    WITH_QEMU_LOCK_GUARD(&clock->active_timers_lock) {
        ts = clock->active_timers;
        while (ts && (ts->attributes & ~attr_mask)) {
            ts = ts->next;
        }
        if (!ts) {
            return -1;
        }
        expire_time = ts->expire_time;
    }
    return MAX(0, expire_time - qemu_clock_get_ns(type));
}

/* Transition function to convert a nanosecond timeout to ms
 * This is used where a system does not support ppoll
 */
int qemu_timeout_ns_to_ms(int64_t ns)
{
    int64_t ms;
    if (ns < 0) {
        return -1;
    }

    if (!ns) {
        return 0;
    }

    /* Always round up, because it's better to wait too long than to wait too
     * little and effectively busy-wait
     */
    ms = DIV_ROUND_UP(ns, SCALE_MS);

    /* To avoid overflow problems, limit this to 2^31, i.e. approx 25 days */
    return MIN(ms, INT32_MAX);
}


/* qemu implementation of g_poll which uses a nanosecond timeout but is
 * otherwise identical to g_poll
 */
#if defined(XBOX) && defined(_WIN32) && !defined(CONFIG_PPOLL)
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif
#define XBOX_NO_HR_TIMER (-2)

/* Poll the fds and a high-resolution waitable timer set to the timeout
 * (Windows 10 1803+); XBOX_NO_HR_TIMER when this thread has none. */
static int xbox_win_poll_hr(GPollFD *fds, guint nfds, int64_t timeout)
{
    static __thread HANDLE hr_timer;
    static __thread int hr_state; /* 0 untried, 1 usable, -1 unavailable */

    if (hr_state == 0) {
        hr_timer = CreateWaitableTimerExW(
            NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
            TIMER_ALL_ACCESS);
        hr_state = hr_timer ? 1 : -1;
    }
    if (hr_state != 1) {
        return XBOX_NO_HR_TIMER;
    }
    LARGE_INTEGER due;
    due.QuadPart = -(timeout / 100); /* relative, 100 ns units */
    if (due.QuadPart == 0) {
        due.QuadPart = -1;
    }
    if (!SetWaitableTimer(hr_timer, &due, 0, NULL, NULL, FALSE)) {
        return XBOX_NO_HR_TIMER;
    }
    GPollFD *all = g_newa(GPollFD, nfds + 1);
    memcpy(all, fds, nfds * sizeof(GPollFD));
    all[nfds].fd = (gintptr)hr_timer;
    all[nfds].events = G_IO_IN;
    all[nfds].revents = 0;
    /* The millisecond timeout only backs the timer up. */
    int ret = g_poll(all, nfds + 1, qemu_timeout_ns_to_ms(timeout) + 1);
    memcpy(fds, all, nfds * sizeof(GPollFD));
    if (ret > 0 && all[nfds].revents) {
        ret--; /* the timer's own wake is not an fd event */
    }
    return ret;
}
#endif

int qemu_poll_ns(GPollFD *fds, guint nfds, int64_t timeout)
{
#ifdef CONFIG_PPOLL
    if (timeout < 0) {
        return ppoll((struct pollfd *)fds, nfds, NULL, NULL);
    } else {
        struct timespec ts;
        int64_t tvsec = timeout / 1000000000LL;
        /* Avoid possibly overflowing and specifying a negative number of
         * seconds, which would turn a very long timeout into a busy-wait.
         */
        if (tvsec > (int64_t)INT32_MAX) {
            tvsec = INT32_MAX;
        }
        ts.tv_sec = tvsec;
        ts.tv_nsec = timeout % 1000000000LL;
        return ppoll((struct pollfd *)fds, nfds, &ts, NULL);
    }
#else

    /* Timers are facilitated by this function. A poll honours only whole
     * milliseconds, so a nearer deadline is busy-waited, or, with CPU boost
     * on Windows 10 1803+, woken by a high-resolution waitable timer polled
     * with the handles. */
    if (timeout > 0) {
#ifdef _WIN32
        if (xemu_cpu_boost) {
            int ret = xbox_win_poll_hr(fds, nfds, timeout);
            if (ret != XBOX_NO_HR_TIMER) {
                return ret;
            }
        }
#endif
        #define XBOX_BUSYWAIT_THRESHOLD_NS 1250000
        if (timeout < XBOX_BUSYWAIT_THRESHOLD_NS) {
            int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            int64_t end = now + timeout;
            while (now < end) {
                now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            }
            timeout = 0;
        }
    }

    return g_poll(fds, nfds, qemu_timeout_ns_to_ms(timeout));
#endif
}


void timer_init_full(QEMUTimer *ts,
                     QEMUClockType type,
                     int scale, int attributes,
                     QEMUTimerCB *cb, void *opaque)
{
    ts->timer_list = qemu_clock_ptr(type);
    ts->cb = cb;
    ts->opaque = opaque;
    ts->scale = scale;
    ts->attributes = attributes;
    ts->expire_time = -1;
}

static void timer_del_locked(QEMUTimerList *timer_list, QEMUTimer *ts)
{
    QEMUTimer **pt, *t;

    ts->expire_time = -1;
    pt = &timer_list->active_timers;
    for(;;) {
        t = *pt;
        if (!t)
            break;
        if (t == ts) {
            timer_link_set(pt, t->next);
            break;
        }
        pt = &t->next;
    }
}

static bool timer_mod_ns_locked(QEMUTimerList *timer_list,
                                QEMUTimer *ts, int64_t expire_time)
{
    QEMUTimer **pt, *t;

    /* add the timer in the sorted list */
    pt = &timer_list->active_timers;
    for (;;) {
        t = *pt;
        if (!timer_expired_ns(t, expire_time)) {
            break;
        }
        pt = &t->next;
    }
    ts->expire_time = MAX(expire_time, 0);
    ts->next = *pt;
    timer_link_set(pt, ts);

    return pt == &timer_list->active_timers;
}

/* stop a timer, but do not dealloc it */
void timer_del(QEMUTimer *ts)
{
    QEMUTimerList *timer_list = ts->timer_list;

    if (timer_list) {
        qemu_mutex_lock(&timer_list->active_timers_lock);
        timer_del_locked(timer_list, ts);
        qemu_mutex_unlock(&timer_list->active_timers_lock);
    }
}

/* modify the current timer so that it will be fired when current_time
   >= expire_time. The corresponding callback will be called. */
void timer_mod_ns(QEMUTimer *ts, int64_t expire_time)
{
    QEMUTimerList *timer_list = ts->timer_list;
    bool rearm;

    qemu_mutex_lock(&timer_list->active_timers_lock);
    timer_del_locked(timer_list, ts);
    rearm = timer_mod_ns_locked(timer_list, ts, expire_time);
    qemu_mutex_unlock(&timer_list->active_timers_lock);

    if (rearm) {
        qemu_clock_notify(timer_list->type);
    }
}

/* modify the current timer so that it will be fired when current_time
   >= expire_time or the current deadline, whichever comes earlier.
   The corresponding callback will be called. */
void timer_mod_anticipate_ns(QEMUTimer *ts, int64_t expire_time)
{
    QEMUTimerList *timer_list = ts->timer_list;
    bool rearm = false;

    WITH_QEMU_LOCK_GUARD(&timer_list->active_timers_lock) {
        if (ts->expire_time == -1 || ts->expire_time > expire_time) {
            if (ts->expire_time != -1) {
                timer_del_locked(timer_list, ts);
            }
            rearm = timer_mod_ns_locked(timer_list, ts, expire_time);
        } else {
            rearm = false;
        }
    }
    if (rearm) {
        qemu_clock_notify(timer_list->type);
    }
}

void timer_mod(QEMUTimer *ts, int64_t expire_time)
{
    timer_mod_ns(ts, expire_time * ts->scale);
}

void timer_mod_anticipate(QEMUTimer *ts, int64_t expire_time)
{
    timer_mod_anticipate_ns(ts, expire_time * ts->scale);
}

bool timer_pending(const QEMUTimer *ts)
{
    return ts->expire_time >= 0;
}

bool qemu_clock_run_timers(QEMUClockType type)
{
    QEMUTimerList *timer_list = qemu_clock_ptr(type);
    QEMUTimer *ts;
    int64_t current_time;
    bool progress = false;
    QEMUTimerCB *cb;
    void *opaque;

    if (!timer_list_head(timer_list)) {
        return false;
    }

    qemu_event_reset(&timer_list->timers_done_ev);
    if (!timer_list->enabled) {
        goto out;
    }

    /* Extract expired timers from active timers list and process them. */
    current_time = qemu_clock_get_ns(timer_list->type);
    qemu_mutex_lock(&timer_list->active_timers_lock);
    while ((ts = timer_list->active_timers)) {
        if (!timer_expired_ns(ts, current_time)) {
            /* No expired timers left. */
            break;
        }

        /* remove timer from the list before calling the callback */
        timer_link_set(&timer_list->active_timers, ts->next);
        ts->next = NULL;
        ts->expire_time = -1;
        cb = ts->cb;
        opaque = ts->opaque;

        /* run the callback (the timer list can be modified) */
        qemu_mutex_unlock(&timer_list->active_timers_lock);
        cb(opaque);
        qemu_mutex_lock(&timer_list->active_timers_lock);

        progress = true;
    }
    qemu_mutex_unlock(&timer_list->active_timers_lock);

out:
    qemu_event_set(&timer_list->timers_done_ev);
    return progress;
}

int64_t qemu_clock_deadline_ns(void)
{
    int64_t deadline = -1;
    for (QEMUClockType type = 0; type < QEMU_CLOCK_MAX; type++) {
        if (qemu_clock_use_for_deadline(type)) {
            deadline = qemu_soonest_timeout(deadline,
                qemu_clock_deadline_ns_all(type, QEMU_TIMER_ATTR_ALL));
        }
    }
    return deadline;
}

int64_t qemu_clock_get_ns(QEMUClockType type)
{
    switch (type) {
    case QEMU_CLOCK_REALTIME:
        return get_clock();
    default:
    case QEMU_CLOCK_VIRTUAL:
        return cpus_get_virtual_clock();
    case QEMU_CLOCK_HOST:
        return get_clock_realtime();
    case QEMU_CLOCK_VIRTUAL_RT:
        return cpu_get_clock();
    }
}

void qemu_init_clocks(QEMUTimerListNotifyCB *notify_cb)
{
    QEMUClockType type;
    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        qemu_clock_init(type, notify_cb);
    }

#ifdef CONFIG_PRCTL_PR_SET_TIMERSLACK
    prctl(PR_SET_TIMERSLACK, 1, 0, 0, 0);
#endif
}

uint64_t timer_expire_time_ns(const QEMUTimer *ts)
{
    return timer_pending(ts) ? ts->expire_time : -1;
}

bool qemu_clock_run_all_timers(void)
{
    bool progress = false;
    QEMUClockType type;

    for (type = 0; type < QEMU_CLOCK_MAX; type++) {
        if (qemu_clock_use_for_deadline(type)) {
            progress |= qemu_clock_run_timers(type);
        }
    }

    return progress;
}

