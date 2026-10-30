/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Included by test-glib.c so both the shim and system GLib run this oracle. */
#ifdef _WIN32
#include <windows.h>
#include <process.h>
typedef HANDLE GlibTestThread;
#define GLIB_TEST_FN unsigned __stdcall
#define GLIB_TEST_YIELD() Sleep(0)
#else
#include <pthread.h>
#include <sched.h>
typedef pthread_t GlibTestThread;
#define GLIB_TEST_FN void *
#define GLIB_TEST_YIELD() sched_yield()
#endif

#define GLIB_TEST_THREADS 16
static unsigned threading_ready, threading_start, threading_failures;
static GQuark threaded_quarks[128];
static GPtrArray *threaded_array;
static GHashTable *threaded_hash;
static GTree *threaded_tree;
static GDateTime *threaded_date;
static void *threaded_default;

static GLIB_TEST_FN glib_test_thread(void *opaque)
{
    __atomic_add_fetch(&threading_ready, 1, __ATOMIC_RELEASE);
    while (!__atomic_load_n(&threading_start, __ATOMIC_ACQUIRE)) {
        GLIB_TEST_YIELD();
    }
    void *context = g_main_context_default(), *expected_context = NULL;
    __atomic_compare_exchange_n(&threaded_default, &expected_context, context,
                                FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    if (context != __atomic_load_n(&threaded_default, __ATOMIC_ACQUIRE)) {
        __atomic_add_fetch(&threading_failures, 1, __ATOMIC_RELAXED);
    }
    for (unsigned i = 0; i < 128; i++) {
        gchar name[40];
        GQuark expected = 0, q;
        snprintf(name, sizeof(name), "concurrent-quark-%u", i);
        q = g_quark_from_string(name);
        __atomic_compare_exchange_n(&threaded_quarks[i], &expected, q,
                                    FALSE, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        if (q != __atomic_load_n(&threaded_quarks[i], __ATOMIC_ACQUIRE) ||
            g_quark_try_string(name) != q ||
            strcmp(g_quark_to_string(q), name)) {
            __atomic_add_fetch(&threading_failures, 1, __ATOMIC_RELAXED);
        }
    }
    for (unsigned i = 0; i < 50000; i++) {
        g_date_time_ref(threaded_date);
        g_ptr_array_ref(threaded_array);
        g_hash_table_ref(threaded_hash);
        g_tree_ref(threaded_tree);
        g_tree_unref(threaded_tree);
        g_hash_table_unref(threaded_hash);
        g_ptr_array_unref(threaded_array);
        g_date_time_unref(threaded_date);
        (void)g_random_int();
    }
    return 0;
}

static void test_threading(void)
{
    GlibTestThread threads[GLIB_TEST_THREADS];
    threaded_array = g_ptr_array_new_with_free_func(count_destroy);
    g_ptr_array_add(threaded_array, g_strdup("array"));
    threaded_hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          count_destroy, NULL);
    g_hash_table_insert(threaded_hash, g_strdup("hash"), NULL);
    threaded_tree = g_tree_new_full(cmp_int_data, NULL, NULL, count_destroy);
    g_tree_insert(threaded_tree, GINT_TO_POINTER(1), g_strdup("tree"));
    threaded_date = g_date_time_new_from_unix_utc(86400);
    destroyed = 0;
    for (unsigned i = 0; i < GLIB_TEST_THREADS; i++) {
#ifdef _WIN32
        threads[i] = (HANDLE)_beginthreadex(NULL, 0, glib_test_thread,
                                          NULL, 0, NULL);
        CHECK(threads[i] != NULL);
#else
        CHECK(!pthread_create(&threads[i], NULL, glib_test_thread, NULL));
#endif
    }
    while (__atomic_load_n(&threading_ready, __ATOMIC_ACQUIRE) != GLIB_TEST_THREADS) {
        GLIB_TEST_YIELD();
    }
    __atomic_store_n(&threading_start, 1, __ATOMIC_RELEASE);
    for (unsigned i = 0; i < GLIB_TEST_THREADS; i++) {
#ifdef _WIN32
        CHECK(WaitForSingleObject(threads[i], 30000) == WAIT_OBJECT_0);
        CloseHandle(threads[i]);
#else
        CHECK(!pthread_join(threads[i], NULL));
#endif
    }
    CHECK(threading_failures == 0 && destroyed == 0);
    g_ptr_array_unref(threaded_array);
    g_hash_table_unref(threaded_hash);
    g_tree_unref(threaded_tree);
    CHECK(destroyed == 3);
    CHECK(g_date_time_to_unix(threaded_date) == 86400);
    g_date_time_unref(threaded_date);
}
