/*
 * The in-tree GLib (libretro/glib/glib.h): main contexts, sources and
 * g_poll.
 *
 * QEMU's own loop (util/main-loop.c) drives a context with prepare, query,
 * its own poll, check and dispatch, as with GLib. Contexts track their
 * sources and callback snapshots (no wakeup pipe, no locks). QEMU attaches,
 * destroys and dispatches sources with the BQL held, or on a context only
 * one thread uses, which is what keeps this single-threaded code safe.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "glib.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <poll.h>
#endif

#include <retro_timers.h>

/* source->flags */
#define G_SRC_ATTACHED 1
#define G_SRC_DESTROYED 2
#define G_SRC_READY 4

struct _GMainContext {
    gint ref_count;
    GSource *sources;           /* attached, in attach order */
    gint64 time;                /* of the last prepare */
    gint timeout;               /* found by the last prepare, in ms */
    GPtrArray *pending;         /* sources to dispatch */
    GPtrArray *dispatching;     /* the ones being dispatched */
    GPtrArray *walking;         /* reusable source snapshot for prepare/check */
    guint walking_depth;
    gint depth;                 /* dispatches in progress */
    volatile gint wakeup;
};

struct _GMainLoop {
    GMainContext *context;
    volatile gint is_running;
    gint ref_count;
};

static guint g_next_source_id = 1;

/* ---- contexts ------------------------------------------------------------- */

GMainContext *g_main_context_new(void)
{
    GMainContext *c = g_new0(GMainContext, 1);

    c->ref_count = 1;
    c->pending = g_ptr_array_new();
    c->dispatching = g_ptr_array_new();
    c->walking = g_ptr_array_new();
    return c;
}

GMainContext *g_main_context_ref(GMainContext *context)
{
    __atomic_add_fetch(&context->ref_count, 1, __ATOMIC_RELAXED);
    return context;
}

/* Remove ownership before unref: a finalizer may re-enter the context. */
static void g_main_context_clear_sources(GPtrArray *sources)
{
    while (sources->len) {
        GSource *source = sources->pdata[--sources->len];
        g_source_unref(source);
    }
}

/* Source callbacks may detach themselves or other sources. Keep the complete
 * walk alive, and keep its storage across calls. A nested walk gets its own
 * array so it cannot overwrite the outer callback's snapshot.
 */
static GPtrArray *g_main_context_ref_sources(GMainContext *context)
{
    GPtrArray *sources = context->walking_depth++ ? g_ptr_array_new()
                                                : context->walking;
    for (GSource *s = context->sources; s; s = s->next) {
        g_ptr_array_add(sources, g_source_ref(s));
    }
    return sources;
}

static void g_main_context_unref_sources(GMainContext *context, GPtrArray *sources)
{
    g_main_context_clear_sources(sources);
    context->walking_depth--;
    if (sources != context->walking) {
        g_ptr_array_free(sources, TRUE);
    }
}

void g_main_context_unref(GMainContext *context)
{
    if (__atomic_sub_fetch(&context->ref_count, 1, __ATOMIC_ACQ_REL)) {
        return;
    }
    while (context->sources) {
        g_source_destroy(context->sources);
    }
    g_main_context_clear_sources(context->pending);
    g_main_context_clear_sources(context->dispatching);
    g_ptr_array_free(context->pending, TRUE);
    g_ptr_array_free(context->dispatching, TRUE);
    g_main_context_clear_sources(context->walking);
    g_ptr_array_free(context->walking, TRUE);
    g_free(context);
}

static GMainContext *g_default_context;

GMainContext *g_main_context_default(void)
{
    static gsize initialized;
    if (g_once_init_enter(&initialized)) {
        g_default_context = g_main_context_new();
        g_once_init_leave(&initialized, 1);
    }
    return g_default_context;
}

/* each thread's stack of pushed contexts */
#define G_THREAD_DEFAULT_MAX 8
static __thread GMainContext *g_thread_default[G_THREAD_DEFAULT_MAX];
static __thread guint g_n_thread_default;

GMainContext *g_main_context_get_thread_default(void)
{
    return g_n_thread_default ? g_thread_default[g_n_thread_default - 1]
                              : NULL;
}

GMainContext *g_main_context_ref_thread_default(void)
{
    GMainContext *c = g_main_context_get_thread_default();

    return g_main_context_ref(c ? c : g_main_context_default());
}

void g_main_context_push_thread_default(GMainContext *context)
{
    g_assert(g_n_thread_default < G_THREAD_DEFAULT_MAX);
    if (context) {
        g_main_context_ref(context);
    }
    g_thread_default[g_n_thread_default++] = context;
}

void g_main_context_pop_thread_default(GMainContext *context)
{
    g_assert(g_n_thread_default &&
             g_thread_default[g_n_thread_default - 1] == context);
    g_n_thread_default--;
    if (context) {
        g_main_context_unref(context);
    }
}

static GMainContext *g_ctx(GMainContext *context)
{
    return context ? context : g_main_context_default();
}

/* one thread drives a context at a time; QEMU's lock makes that so */
gboolean g_main_context_acquire(GMainContext *context)
{
    return TRUE;
}

void g_main_context_release(GMainContext *context)
{
}

gboolean g_main_context_is_owner(GMainContext *context)
{
    return TRUE;
}

GSource *g_main_context_find_source_by_id(GMainContext *context,
                                          guint source_id)
{
    for (GSource *s = g_ctx(context)->sources; s; s = s->next) {
        if (s->source_id == source_id) {
            return s;
        }
    }
    return NULL;
}

/* ---- one iteration -------------------------------------------------------- */

static void g_source_mark_ready(GSource *s)
{
    for (; s; s = s->parent) {
        s->flags |= G_SRC_READY;
    }
}

static gboolean g_source_blocked(GSource *s)
{
    return s->in_dispatch && !s->can_recurse;
}

gboolean g_main_context_prepare(GMainContext *context, gint *priority)
{
    GMainContext *c = g_ctx(context);
    gint best = G_MAXINT;
    GPtrArray *sources = g_main_context_ref_sources(c);

    c->time = g_get_monotonic_time();
    c->timeout = -1;
    for (guint i = 0; i < sources->len; i++) {
        GSource *s = sources->pdata[i];
        gint t = -1;

        if ((s->flags & G_SRC_DESTROYED) || g_source_blocked(s)) {
            continue;
        }
        if (!(s->flags & G_SRC_READY)) {
            gboolean ready = FALSE;
            if (s->source_funcs->prepare) {
                ready = s->source_funcs->prepare(s, &t);
                if (s->flags & G_SRC_DESTROYED) {
                    continue;
                }
            }
            if (!ready && s->ready_time >= 0) {
                if (s->ready_time <= c->time) {
                    ready = TRUE;
                } else {
                    gint64 ms = (s->ready_time - c->time + 999) / 1000;
                    gint rt = ms > G_MAXINT ? G_MAXINT : (gint)ms;
                    t = t < 0 ? rt : MIN(t, rt);
                }
            }
            if (ready) {
                g_source_mark_ready(s);
            }
        }
        if (s->flags & G_SRC_READY) {
            t = 0;
            best = MIN(best, s->priority);
        }
        if (t >= 0) {
            c->timeout = c->timeout < 0 ? t : MIN(c->timeout, t);
        }
    }
    g_main_context_unref_sources(c, sources);
    if (priority) {
        *priority = best;
    }
    return best != G_MAXINT;
}

gint g_main_context_query(GMainContext *context, gint max_priority,
                          gint *timeout_, GPollFD *fds, gint n_fds)
{
    GMainContext *c = g_ctx(context);
    gint n = 0;

    for (GSource *s = c->sources; s; s = s->next) {
        if (g_source_blocked(s) || s->priority > max_priority) {
            continue;
        }
        for (GSList *l = s->poll_fds; l; l = l->next) {
            GPollFD *p = l->data;
            if (n < n_fds) {
                fds[n].fd = p->fd;
                fds[n].events = p->events;
                fds[n].revents = 0;
            }
            n++;
        }
        for (GSList *l = s->unix_fds; l; l = l->next) {
            GPollFD *p = l->data;
            if (n < n_fds) {
                fds[n].fd = p->fd;
                fds[n].events = p->events;
                fds[n].revents = 0;
            }
            n++;
        }
    }
    if (timeout_) {
        *timeout_ = c->timeout;
    }
    return n;
}

gboolean g_main_context_check(GMainContext *context, gint max_priority,
                              GPollFD *fds, gint n_fds)
{
    GMainContext *c = g_ctx(context);
    gint i = 0;

    __atomic_store_n(&c->wakeup, 0, __ATOMIC_RELAXED);
    c->time = g_get_monotonic_time();
    /* the poll's results, back in the order query handed them out */
    for (GSource *s = c->sources; s; s = s->next) {
        if (g_source_blocked(s) || s->priority > max_priority) {
            continue;
        }
        for (GSList *l = s->poll_fds; l; l = l->next, i++) {
            ((GPollFD *)l->data)->revents = i < n_fds ? fds[i].revents : 0;
        }
        for (GSList *l = s->unix_fds; l; l = l->next, i++) {
            ((GPollFD *)l->data)->revents = i < n_fds ? fds[i].revents : 0;
        }
    }

    g_main_context_clear_sources(c->pending);
    GPtrArray *sources = g_main_context_ref_sources(c);
    for (guint j = 0; j < sources->len; j++) {
        GSource *s = sources->pdata[j];
        if ((s->flags & G_SRC_DESTROYED) || g_source_blocked(s)) {
            continue;
        }
        if (!(s->flags & G_SRC_READY)) {
            gboolean ready = FALSE;
            if (s->source_funcs->check) {
                ready = s->source_funcs->check(s);
                if (s->flags & G_SRC_DESTROYED) {
                    continue;
                }
            }
            for (GSList *l = s->unix_fds; !ready && l; l = l->next) {
                ready = ((GPollFD *)l->data)->revents != 0;
            }
            if (!ready && s->ready_time >= 0 && s->ready_time <= c->time) {
                ready = TRUE;
            }
            if (ready) {
                g_source_mark_ready(s);
            }
        }
    }
    g_main_context_unref_sources(c, sources);
    for (GSource *s = c->sources; s; s = s->next) {
        if ((s->flags & G_SRC_READY) && s->priority <= max_priority &&
            !g_source_blocked(s)) {
            g_ptr_array_add(c->pending, g_source_ref(s));
        }
    }
    return c->pending->len > 0;
}

void g_main_context_dispatch(GMainContext *context)
{
    GMainContext *c = g_ctx(context);
    GPtrArray *list;

    if (!c->pending->len) {
        return;
    }
    /*
     * Swapped out, so a check from inside a dispatch has its own list; only
     * a dispatch nested in a dispatch needs a fresh one.
     */
    if (c->depth) {
        list = g_ptr_array_new();
    } else {
        list = c->dispatching;
    }
    c->dispatching = c->pending;
    c->pending = list;
    list = c->dispatching;
    c->depth++;
    for (guint i = 0; i < list->len; i++) {
        GSource *s = list->pdata[i];

        s->flags &= ~G_SRC_READY;
        if (!(s->flags & G_SRC_DESTROYED)) {
            gboolean was_in = s->in_dispatch, keep;

            s->in_dispatch = TRUE;
            keep = s->source_funcs->dispatch(s, s->callback,
                                             s->callback_data);
            s->in_dispatch = was_in;
            if (!keep && !(s->flags & G_SRC_DESTROYED)) {
                g_source_destroy(s);
            }
        }
        g_source_unref(s);
    }
    g_ptr_array_set_size(list, 0);
    c->depth--;
    if (c->dispatching != list) {
        /* a nested dispatch left its own list as the spare */
        g_ptr_array_free(list, TRUE);
    }
}

gint g_poll(GPollFD *fds, guint nfds, gint timeout)
{
#ifdef _WIN32
    /* GLib's on Windows: the fds are event handles to wait on */
    HANDLE h[MAXIMUM_WAIT_OBJECTS];
    guint idx[MAXIMUM_WAIT_OBJECTS], n = 0;
    DWORD r;
    gint ready = 0;

    for (guint i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        if (fds[i].fd > 0 && fds[i].events && n < MAXIMUM_WAIT_OBJECTS) {
            h[n] = (HANDLE)(gintptr)fds[i].fd;
            idx[n++] = i;
        }
    }
    if (!n) {
        if (timeout > 0) {
            retro_sleep((unsigned)timeout);
        }
        return 0;
    }
    r = WaitForMultipleObjectsEx(n, h, FALSE,
                                 timeout < 0 ? INFINITE : (DWORD)timeout,
                                 TRUE);
    if (r == WAIT_FAILED) {
        return -1;
    }
    /* the one that woke the wait, then any others already signalled */
    for (guint k = 0; k < n; k++) {
        if ((r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + n &&
             k == r - WAIT_OBJECT_0) ||
            WaitForSingleObject(h[k], 0) == WAIT_OBJECT_0) {
            fds[idx[k]].revents = fds[idx[k]].events;
            ready++;
        }
    }
    return ready;
#else
    G_STATIC_ASSERT(sizeof(GPollFD) == sizeof(struct pollfd));
    G_STATIC_ASSERT(G_IO_IN == POLLIN && G_IO_OUT == POLLOUT &&
                    G_IO_PRI == POLLPRI && G_IO_ERR == POLLERR &&
                    G_IO_HUP == POLLHUP && G_IO_NVAL == POLLNVAL);
    return poll((struct pollfd *)fds, nfds, timeout);
#endif
}

gboolean g_main_context_iteration(GMainContext *context, gboolean may_block)
{
    GMainContext *c = g_ctx(context);
    gint priority, timeout, n, alloc = 16;
    GPollFD *fds = g_new(GPollFD, alloc);
    gboolean done;

    g_main_context_prepare(c, &priority);
    while ((n = g_main_context_query(c, priority, &timeout, fds, alloc)) >
           alloc) {
        alloc = n;
        fds = g_renew(GPollFD, fds, alloc);
    }
    if (!may_block || __atomic_load_n(&c->wakeup, __ATOMIC_RELAXED)) {
        timeout = 0;
    } else if (timeout < 0 || timeout > 10) {
        /* no wakeup pipe: look again for g_main_context_wakeup soon */
        timeout = 10;
    }
    if (n || timeout) {
        g_poll(fds, (guint)n, timeout);
    }
    done = g_main_context_check(c, priority, fds, n);
    g_main_context_dispatch(c);
    g_free(fds);
    return done;
}

gboolean g_main_context_pending(GMainContext *context)
{
    GMainContext *c = g_ctx(context);
    gint priority, timeout, n, alloc = 16;
    GPollFD *fds = g_new(GPollFD, alloc);
    gboolean ready;

    g_main_context_prepare(c, &priority);
    while ((n = g_main_context_query(c, priority, &timeout, fds, alloc)) >
           alloc) {
        alloc = n;
        fds = g_renew(GPollFD, fds, alloc);
    }
    if (n) {
        g_poll(fds, (guint)n, 0);
    }
    ready = g_main_context_check(c, priority, fds, n);
    for (guint i = 0; i < c->pending->len; i++) {
        GSource *s = c->pending->pdata[i];
        s->flags |= G_SRC_READY;    /* still ready for the next dispatch */
        g_source_unref(s);
    }
    g_ptr_array_set_size(c->pending, 0);
    g_free(fds);
    return ready;
}

void g_main_context_wakeup(GMainContext *context)
{
    __atomic_store_n(&g_ctx(context)->wakeup, 1, __ATOMIC_RELAXED);
}

/* ---- main loops ----------------------------------------------------------- */

GMainLoop *g_main_loop_new(GMainContext *context, gboolean is_running)
{
    GMainLoop *l = g_new0(GMainLoop, 1);

    l->context = g_main_context_ref(g_ctx(context));
    l->is_running = is_running;
    l->ref_count = 1;
    return l;
}

GMainLoop *g_main_loop_ref(GMainLoop *loop)
{
    __atomic_add_fetch(&loop->ref_count, 1, __ATOMIC_RELAXED);
    return loop;
}

void g_main_loop_unref(GMainLoop *loop)
{
    if (!__atomic_sub_fetch(&loop->ref_count, 1, __ATOMIC_ACQ_REL)) {
        g_main_context_unref(loop->context);
        g_free(loop);
    }
}

void g_main_loop_run(GMainLoop *loop)
{
    __atomic_store_n(&loop->is_running, TRUE, __ATOMIC_RELEASE);
    while (__atomic_load_n(&loop->is_running, __ATOMIC_ACQUIRE)) {
        g_main_context_iteration(loop->context, TRUE);
    }
}

void g_main_loop_quit(GMainLoop *loop)
{
    __atomic_store_n(&loop->is_running, FALSE, __ATOMIC_RELEASE);
    g_main_context_wakeup(loop->context);
}

gboolean g_main_loop_is_running(GMainLoop *loop)
{
    return __atomic_load_n(&loop->is_running, __ATOMIC_ACQUIRE);
}

GMainContext *g_main_loop_get_context(GMainLoop *loop)
{
    return loop->context;
}

/* ---- sources -------------------------------------------------------------- */

GSource *g_source_new(GSourceFuncs *source_funcs, guint struct_size)
{
    GSource *s;

    g_assert(struct_size >= sizeof(GSource));
    s = g_malloc0(struct_size);
    s->source_funcs = source_funcs;
    s->ref_count = 1;
    s->priority = G_PRIORITY_DEFAULT;
    s->ready_time = -1;
    return s;
}

GSource *g_source_ref(GSource *source)
{
    __atomic_add_fetch(&source->ref_count, 1, __ATOMIC_RELAXED);
    return source;
}

void g_source_unref(GSource *source)
{
    if (!source ||
        __atomic_sub_fetch(&source->ref_count, 1, __ATOMIC_ACQ_REL)) {
        return;
    }
    if (source->callback_notify) {
        GDestroyNotify notify = source->callback_notify;
        source->callback_notify = NULL;
        notify(source->callback_data);
    }
    if (source->source_funcs->finalize) {
        source->source_funcs->finalize(source);
    }
    g_slist_free(source->poll_fds);
    g_slist_free_full(source->unix_fds, g_free);
    g_slist_free_full(source->child_sources,
                      (GDestroyNotify)g_source_unref);
    g_free(source->name);
    g_free(source);
}

guint g_source_attach(GSource *source, GMainContext *context)
{
    GMainContext *c = g_ctx(context);
    GSource **tail = &c->sources;

    g_assert(!source->context);
    source->context = c;
    source->source_id = g_next_source_id++;
    source->flags |= G_SRC_ATTACHED;
    source->next = NULL;
    while (*tail) {
        tail = &(*tail)->next;
    }
    *tail = g_source_ref(source);
    for (GSList *l = source->child_sources; l; l = l->next) {
        g_source_attach(l->data, c);
    }
    return source->source_id;
}

void g_source_destroy(GSource *source)
{
    GMainContext *c = source->context;
    GSource **p;

    if (source->flags & G_SRC_DESTROYED) {
        return;
    }
    source->flags |= G_SRC_DESTROYED;
    source->flags &= ~(G_SRC_ATTACHED | G_SRC_READY);
    if (source->callback_notify) {
        GDestroyNotify notify = source->callback_notify;
        source->callback_notify = NULL;
        notify(source->callback_data);
    }
    source->callback = NULL;
    for (GSList *l = source->child_sources; l; l = l->next) {
        g_source_destroy(l->data);
    }
    if (!c) {
        return;
    }
    for (p = &c->sources; *p; p = &(*p)->next) {
        if (*p == source) {
            *p = source->next;
            source->next = NULL;
            g_source_unref(source);     /* the context's */
            return;
        }
    }
}

gboolean g_source_is_destroyed(GSource *source)
{
    return (source->flags & G_SRC_DESTROYED) != 0;
}

gboolean g_source_remove(guint tag)
{
    GSource *s = g_main_context_find_source_by_id(NULL, tag);

    if (s) {
        g_source_destroy(s);
    }
    return s != NULL;
}

void g_source_set_callback(GSource *source, GSourceFunc func, gpointer data,
                           GDestroyNotify notify)
{
    GDestroyNotify old_notify = source->callback_notify;
    gpointer old_data = source->callback_data;

    source->callback = func;
    source->callback_data = data;
    source->callback_notify = notify;
    if (old_notify) {
        old_notify(old_data);
    }
}

void g_source_set_priority(GSource *source, gint priority)
{
    source->priority = priority;
    for (GSList *l = source->child_sources; l; l = l->next) {
        g_source_set_priority(l->data, priority);
    }
}

gint g_source_get_priority(GSource *source)
{
    return source->priority;
}

void g_source_set_can_recurse(GSource *source, gboolean can_recurse)
{
    source->can_recurse = can_recurse;
}

void g_source_set_name(GSource *source, const char *name)
{
    g_free(source->name);
    source->name = g_strdup(name);
}

const char *g_source_get_name(GSource *source)
{
    return source->name;
}

void g_source_set_ready_time(GSource *source, gint64 ready_time)
{
    source->ready_time = ready_time;
}

guint g_source_get_id(GSource *source)
{
    return source->source_id;
}

GMainContext *g_source_get_context(GSource *source)
{
    return source->context;
}

gint64 g_source_get_time(GSource *source)
{
    return source->context ? source->context->time : g_get_monotonic_time();
}

void g_source_add_poll(GSource *source, GPollFD *fd)
{
    source->poll_fds = g_slist_prepend(source->poll_fds, fd);
}

void g_source_remove_poll(GSource *source, GPollFD *fd)
{
    source->poll_fds = g_slist_remove(source->poll_fds, fd);
}

gpointer g_source_add_unix_fd(GSource *source, gint fd, GIOCondition events)
{
    GPollFD *p = g_new0(GPollFD, 1);

    p->fd = fd;
    p->events = (gushort)events;
    source->unix_fds = g_slist_prepend(source->unix_fds, p);
    return p;
}

void g_source_modify_unix_fd(GSource *source, gpointer tag,
                             GIOCondition new_events)
{
    ((GPollFD *)tag)->events = (gushort)new_events;
}

void g_source_remove_unix_fd(GSource *source, gpointer tag)
{
    source->unix_fds = g_slist_remove(source->unix_fds, tag);
    g_free(tag);
}

GIOCondition g_source_query_unix_fd(GSource *source, gpointer tag)
{
    return (GIOCondition)((GPollFD *)tag)->revents;
}

void g_source_add_child_source(GSource *source, GSource *child_source)
{
    child_source->parent = source;
    child_source->priority = source->priority;
    source->child_sources = g_slist_prepend(source->child_sources,
                                            g_source_ref(child_source));
    if (source->context) {
        g_source_attach(child_source, source->context);
    }
}

void g_source_remove_child_source(GSource *source, GSource *child_source)
{
    source->child_sources = g_slist_remove(source->child_sources,
                                           child_source);
    child_source->parent = NULL;
    g_source_destroy(child_source);
    g_source_unref(child_source);
}

/* ---- idle and timeout sources --------------------------------------------- */

typedef struct {
    GSource source;
    guint interval;             /* ms */
    gboolean seconds;
} GTimeoutSource;

static gboolean g_idle_prepare(GSource *source, gint *timeout_)
{
    *timeout_ = 0;
    return TRUE;
}

static gboolean g_idle_dispatch(GSource *source, GSourceFunc callback,
                                gpointer user_data)
{
    return callback ? callback(user_data) : G_SOURCE_REMOVE;
}

static GSourceFuncs g_idle_funcs = {
    .prepare = g_idle_prepare,
    .dispatch = g_idle_dispatch,
};

GSource *g_idle_source_new(void)
{
    GSource *s = g_source_new(&g_idle_funcs, sizeof(GSource));

    s->priority = G_PRIORITY_DEFAULT_IDLE;
    return s;
}

static void g_timeout_arm(GTimeoutSource *t)
{
    gint64 now = g_get_monotonic_time();

    t->source.ready_time = now + (gint64)t->interval * 1000;
}

static gboolean g_timeout_dispatch(GSource *source, GSourceFunc callback,
                                   gpointer user_data)
{
    gboolean again = callback ? callback(user_data) : G_SOURCE_REMOVE;

    if (again) {
        g_timeout_arm((GTimeoutSource *)source);
    }
    return again;
}

static GSourceFuncs g_timeout_funcs = {
    .dispatch = g_timeout_dispatch,
};

GSource *g_timeout_source_new(guint interval)
{
    GTimeoutSource *t = (GTimeoutSource *)g_source_new(&g_timeout_funcs,
                                                       sizeof(*t));

    t->interval = interval;
    g_timeout_arm(t);
    return &t->source;
}

GSource *g_timeout_source_new_seconds(guint interval)
{
    GTimeoutSource *t = (GTimeoutSource *)g_timeout_source_new(interval *
                                                               1000);

    t->seconds = TRUE;
    return &t->source;
}

static guint g_add_source(GSource *s, gint priority, GSourceFunc function,
                          gpointer data, GDestroyNotify notify)
{
    guint id;

    s->priority = priority;
    g_source_set_callback(s, function, data, notify);
    id = g_source_attach(s, NULL);
    g_source_unref(s);
    return id;
}

guint g_idle_add_full(gint priority, GSourceFunc function, gpointer data,
                      GDestroyNotify notify)
{
    return g_add_source(g_idle_source_new(), priority, function, data, notify);
}

guint g_idle_add(GSourceFunc function, gpointer data)
{
    return g_idle_add_full(G_PRIORITY_DEFAULT_IDLE, function, data, NULL);
}

guint g_timeout_add_full(gint priority, guint interval, GSourceFunc function,
                         gpointer data, GDestroyNotify notify)
{
    return g_add_source(g_timeout_source_new(interval), priority, function,
                        data, notify);
}

guint g_timeout_add(guint interval, GSourceFunc function, gpointer data)
{
    return g_timeout_add_full(G_PRIORITY_DEFAULT, interval, function, data,
                              NULL);
}

guint g_timeout_add_seconds(guint interval, GSourceFunc function,
                            gpointer data)
{
    return g_add_source(g_timeout_source_new_seconds(interval),
                        G_PRIORITY_DEFAULT, function, data, NULL);
}
