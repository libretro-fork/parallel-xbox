/*
 * The core's GLib (libretro/glib) against what GLib itself does: the
 * containers, strings, paths, files, checksums and the main context, as
 * QEMU and libslirp use them. Built and run by run.sh.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib.h>
#include <glib/gstdio.h>

static int failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__,      \
                    #cond);                                                \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static int destroyed;

static void count_destroy(gpointer p)
{
    destroyed++;
    g_free(p);
}

static gint cmp_int(gconstpointer a, gconstpointer b)
{
    return GPOINTER_TO_INT(a) - GPOINTER_TO_INT(b);
}

static gint cmp_int_data(gconstpointer a, gconstpointer b, gpointer d)
{
    return GPOINTER_TO_INT(a) - GPOINTER_TO_INT(b);
}

static gint cmp_ptr_elems(gconstpointer a, gconstpointer b)
{
    return GPOINTER_TO_INT(*(gpointer *)a) - GPOINTER_TO_INT(*(gpointer *)b);
}

static gboolean remove_odd(gpointer k, gpointer v, gpointer d)
{
    return GPOINTER_TO_INT(v) & 1;
}

static void test_hash(void)
{
    GHashTable *h = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          count_destroy, NULL);
    GHashTableIter it;
    gpointer k, v;
    char key[32];
    int n = 0;

    destroyed = 0;
    for (int i = 0; i < 5000; i++) {
        snprintf(key, sizeof(key), "key%d", i);
        CHECK(g_hash_table_insert(h, g_strdup(key), GINT_TO_POINTER(i)));
    }
    CHECK(g_hash_table_size(h) == 5000);
    /* an existing key: the new key is freed, the value replaced */
    CHECK(!g_hash_table_insert(h, g_strdup("key7"), GINT_TO_POINTER(-7)));
    CHECK(destroyed == 1);
    CHECK(GPOINTER_TO_INT(g_hash_table_lookup(h, "key7")) == -7);
    CHECK(g_hash_table_contains(h, "key4999"));
    CHECK(!g_hash_table_contains(h, "key5000"));
    CHECK(g_hash_table_remove(h, "key10"));
    CHECK(!g_hash_table_remove(h, "key10"));
    CHECK(destroyed == 2);
    CHECK(g_hash_table_size(h) == 4999);

    g_hash_table_iter_init(&it, h);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        n++;
        if (GPOINTER_TO_INT(v) % 3 == 0) {
            g_hash_table_iter_remove(&it);
        }
    }
    CHECK(n == 4999);
    CHECK(g_hash_table_size(h) == 4999 - 1667);
    CHECK(!g_hash_table_contains(h, "key3"));
    CHECK(g_hash_table_contains(h, "key4"));

    n = g_hash_table_foreach_remove(h, remove_odd, NULL);
    CHECK(g_hash_table_size(h) == 4999 - 1667 - (guint)n);
    g_hash_table_iter_init(&it, h);
    while (g_hash_table_iter_next(&it, &k, &v)) {
        CHECK(!(GPOINTER_TO_INT(v) & 1) && GPOINTER_TO_INT(v) % 3);
    }
    g_hash_table_remove_all(h);
    CHECK(g_hash_table_size(h) == 0);
    CHECK(destroyed == 5001);
    g_hash_table_unref(h);

    /* direct keys, NULL among them */
    h = g_hash_table_new(NULL, NULL);
    g_hash_table_insert(h, NULL, GINT_TO_POINTER(1));
    g_hash_table_add(h, GINT_TO_POINTER(5));
    CHECK(g_hash_table_lookup_extended(h, NULL, &k, &v) && !k &&
          GPOINTER_TO_INT(v) == 1);
    CHECK(g_hash_table_lookup(h, GINT_TO_POINTER(5)) == GINT_TO_POINTER(5));
    g_hash_table_destroy(h);
}

static gboolean tree_sum(gpointer k, gpointer v, gpointer d)
{
    int *sum = d;

    /* in order: each key above the last */
    CHECK(GPOINTER_TO_INT(k) > sum[1]);
    sum[1] = GPOINTER_TO_INT(k);
    sum[0] += GPOINTER_TO_INT(v);
    return FALSE;
}

static void test_tree(void)
{
    GTree *t = g_tree_new(cmp_int);
    int sum[2] = { 0, -1 }, expect = 0;
    GTreeNode *n;

    for (int i = 0; i < 2000; i++) {
        int k = (i * 7919) % 2000;
        g_tree_insert(t, GINT_TO_POINTER(k), GINT_TO_POINTER(k * 2));
    }
    CHECK(g_tree_nnodes(t) == 2000);
    CHECK(g_tree_height(t) < 40);
    for (int i = 0; i < 2000; i += 2) {
        CHECK(g_tree_remove(t, GINT_TO_POINTER(i)));
    }
    CHECK(!g_tree_remove(t, GINT_TO_POINTER(0)));
    CHECK(g_tree_nnodes(t) == 1000);
    CHECK(g_tree_lookup(t, GINT_TO_POINTER(1001)) == GINT_TO_POINTER(2002));
    CHECK(!g_tree_lookup(t, GINT_TO_POINTER(1000)));
    g_tree_foreach(t, tree_sum, sum);
    for (int i = 1; i < 2000; i += 2) {
        expect += i * 2;
    }
    CHECK(sum[0] == expect);
    n = g_tree_lower_bound(t, GINT_TO_POINTER(1000));
    CHECK(n && g_tree_node_key(n) == GINT_TO_POINTER(1001));
    n = g_tree_upper_bound(t, GINT_TO_POINTER(1001));
    CHECK(n && g_tree_node_key(n) == GINT_TO_POINTER(1003));
    CHECK(g_tree_node_previous(n) &&
          g_tree_node_key(g_tree_node_previous(n)) == GINT_TO_POINTER(1001));
    CHECK(g_tree_node_key(g_tree_node_first(t)) == GINT_TO_POINTER(1));
    CHECK(g_tree_node_key(g_tree_node_last(t)) == GINT_TO_POINTER(1999));
    g_tree_destroy(t);
}

static void test_lists(void)
{
    GList *l = NULL;
    GSList *s = NULL;
    GQueue q = G_QUEUE_INIT;
    GSequence *seq = g_sequence_new(NULL);
    int prev = -1, n = 0;

    for (int i = 0; i < 100; i++) {
        l = g_list_prepend(l, GINT_TO_POINTER((i * 37) % 100));
        s = g_slist_prepend(s, GINT_TO_POINTER((i * 37) % 100));
    }
    l = g_list_sort(l, cmp_int);
    s = g_slist_sort(s, cmp_int);
    for (GList *e = l; e; e = e->next, n++) {
        CHECK(GPOINTER_TO_INT(e->data) == n);
        CHECK(!e->next || e->next->prev == e);
    }
    CHECK(n == 100 && !l->prev);
    CHECK(GPOINTER_TO_INT(g_slist_nth_data(s, 42)) == 42);
    l = g_list_remove(l, GINT_TO_POINTER(0));
    CHECK(GPOINTER_TO_INT(l->data) == 1 && !l->prev);
    l = g_list_reverse(l);
    CHECK(GPOINTER_TO_INT(l->data) == 99 && g_list_length(l) == 99);
    CHECK(GPOINTER_TO_INT(g_list_last(l)->data) == 1);
    l = g_list_insert_sorted(g_list_reverse(l), GINT_TO_POINTER(50),
                             cmp_int);
    CHECK(g_list_length(l) == 100);
    g_list_free(l);
    s = g_slist_insert_sorted(s, GINT_TO_POINTER(-1), cmp_int);
    CHECK(GPOINTER_TO_INT(s->data) == -1);
    s = g_slist_remove(s, GINT_TO_POINTER(-1));
    CHECK(g_slist_length(s) == 100);
    g_slist_free(s);

    for (int i = 0; i < 10; i++) {
        g_queue_push_tail(&q, GINT_TO_POINTER(i));
    }
    g_queue_push_head(&q, GINT_TO_POINTER(-1));
    CHECK(GPOINTER_TO_INT(g_queue_pop_head(&q)) == -1);
    CHECK(GPOINTER_TO_INT(g_queue_pop_tail(&q)) == 9);
    CHECK(g_queue_remove(&q, GINT_TO_POINTER(5)));
    CHECK(g_queue_get_length(&q) == 8);
    CHECK(GPOINTER_TO_INT(g_queue_peek_tail(&q)) == 8);
    g_queue_clear(&q);
    CHECK(g_queue_is_empty(&q));

    for (int i = 0; i < 20; i++) {
        g_sequence_insert_sorted(seq, GINT_TO_POINTER((i * 7) % 20),
                                 cmp_int_data, NULL);
    }
    CHECK(g_sequence_get_length(seq) == 20);
    g_sequence_remove(g_sequence_lookup(seq, GINT_TO_POINTER(3),
                                        cmp_int_data, NULL));
    CHECK(!g_sequence_lookup(seq, GINT_TO_POINTER(3), cmp_int_data, NULL));
    CHECK(g_sequence_get(g_sequence_lookup(seq, GINT_TO_POINTER(4),
                                           cmp_int_data, NULL)) ==
          GINT_TO_POINTER(4));
    g_sequence_free(seq);
    (void)prev;
}

static void test_arrays(void)
{
    GArray *a = g_array_new(TRUE, TRUE, sizeof(int));
    GPtrArray *p = g_ptr_array_new_with_free_func(count_destroy);
    GByteArray *b = g_byte_array_new();
    int v;

    for (v = 0; v < 1000; v++) {
        g_array_append_val(a, v);
    }
    v = -1;
    g_array_prepend_val(a, v);
    CHECK(a->len == 1001 && g_array_index(a, int, 0) == -1 &&
          g_array_index(a, int, 1000) == 999);
    CHECK(g_array_index(a, int, 1001) == 0);    /* zero-terminated */
    g_array_remove_range(a, 0, 501);
    CHECK(a->len == 500 && g_array_index(a, int, 0) == 500);
    g_array_set_size(a, 600);
    CHECK(g_array_index(a, int, 599) == 0);     /* cleared */
    g_array_remove_index_fast(a, 0);
    CHECK(g_array_index(a, int, 0) == 0 && a->len == 599);
    g_array_free(a, TRUE);

    destroyed = 0;
    for (v = 0; v < 100; v++) {
        g_ptr_array_add(p, g_strdup_printf("%d", 99 - v));
    }
    g_ptr_array_remove_index(p, 0);
    CHECK(destroyed == 1 && p->len == 99 && !strcmp(p->pdata[0], "98"));
    g_free(g_ptr_array_steal_index(p, 0));
    CHECK(destroyed == 1 && !strcmp(p->pdata[0], "97"));
    g_ptr_array_unref(p);
    CHECK(destroyed == 99);

    p = g_ptr_array_new();
    for (v = 10; v > 0; v--) {
        g_ptr_array_add(p, GINT_TO_POINTER(v));
    }
    g_ptr_array_sort(p, cmp_ptr_elems);
    CHECK(p->pdata[0] == GINT_TO_POINTER(1) &&
          p->pdata[9] == GINT_TO_POINTER(10));
    g_ptr_array_free(p, TRUE);

    g_byte_array_append(b, (const guint8 *)"hello", 5);
    g_byte_array_remove_range(b, 0, 1);
    CHECK(b->len == 4 && !memcmp(b->data, "ello", 4));
    g_byte_array_unref(b);
}

static void test_strings(void)
{
    GString *s = g_string_new("abc");
    gchar **v, *t;
    guchar *d;
    gsize n;

    g_string_append_printf(s, "-%d-", 42);
    g_string_prepend(s, ">");
    g_string_insert_c(s, 1, '[');
    CHECK(!strcmp(s->str, ">[abc-42-") && s->len == 9);
    g_string_erase(s, 0, 2);
    g_string_append_len(s, s->str, 3);  /* a piece of itself */
    CHECK(!strcmp(s->str, "abc-42-abc"));
    g_string_truncate(s, 3);
    t = g_string_free(s, FALSE);
    CHECK(!strcmp(t, "abc"));
    g_free(t);

    v = g_strsplit("a,b,,c", ",", -1);
    CHECK(g_strv_length(v) == 4 && !strcmp(v[2], "") && !strcmp(v[3], "c"));
    t = g_strjoinv("+", v);
    CHECK(!strcmp(t, "a+b++c"));
    g_free(t);
    g_strfreev(v);
    v = g_strsplit("a,b,c", ",", 2);
    CHECK(g_strv_length(v) == 2 && !strcmp(v[1], "b,c"));
    g_strfreev(v);
    v = g_strsplit("", ",", -1);
    CHECK(g_strv_length(v) == 0);
    g_strfreev(v);

    t = g_strconcat("a", "b", "c", NULL);
    CHECK(!strcmp(t, "abc"));
    g_free(t);
    t = g_strdup("  pad  ");
    CHECK(!strcmp(g_strstrip(t), "pad"));
    g_free(t);
    t = g_strndup("abcdef", 3);
    CHECK(!strcmp(t, "abc"));
    g_free(t);
    CHECK(g_str_has_prefix("prefix", "pre") && !g_str_has_prefix("p", "pre"));
    CHECK(g_str_has_suffix("file.img", ".img"));
    CHECK(g_ascii_strcasecmp("HeLLo", "hello") == 0);
    CHECK(g_strcmp0(NULL, "a") < 0 && g_strcmp0(NULL, NULL) == 0);
    CHECK(g_pattern_match_simple("*.qc?w2", "xbox_hdd.qcow2"));
    CHECK(!g_pattern_match_simple("*.img", "xbox_hdd.qcow2"));
    CHECK(g_utf8_validate("h\xc3\xa9llo", -1, NULL));
    CHECK(!g_utf8_validate("h\xc3llo", -1, NULL));

    t = g_base64_encode((const guchar *)"libretro", 8);
    CHECK(!strcmp(t, "bGlicmV0cm8="));
    d = g_base64_decode(t, &n);
    CHECK(n == 8 && !memcmp(d, "libretro", 8));
    g_free(d);
    g_free(t);
    t = g_compute_checksum_for_data(G_CHECKSUM_SHA1, (const guchar *)"abc",
                                    3);
    CHECK(!strcmp(t, "a9993e364706816aba3e25717850c26c9cd0d89d"));
    g_free(t);
    t = g_compute_checksum_for_string(G_CHECKSUM_SHA1, "abc", -1);
    CHECK(!strcmp(t, "a9993e364706816aba3e25717850c26c9cd0d89d"));
    g_free(t);

    {
        GDebugKey keys[] = { { "tcp", 1 }, { "udp", 2 }, { "misc", 4 } };
        CHECK(g_parse_debug_string("tcp,misc", keys, 3) == 5);
        CHECK(g_parse_debug_string("all", keys, 3) == 7);
    }
    {
        GError *err = NULL;
        g_set_error(&err, G_FILE_ERROR, G_FILE_ERROR_NOENT, "x %d", 1);
        CHECK(err && err->code == G_FILE_ERROR_NOENT &&
              !strcmp(err->message, "x 1") && err->domain == G_FILE_ERROR);
        g_clear_error(&err);
        CHECK(!err);
    }
}

static void test_paths(const char *dir)
{
    gchar *t, *f, *c;
    gsize n;
    GDir *d;
    const gchar *name;
    int seen = 0;

    t = g_build_filename("a/", "/b", "c", NULL);
    CHECK(!strcmp(t, "a" G_DIR_SEPARATOR_S "b" G_DIR_SEPARATOR_S "c") ||
          !strcmp(t, "a/b/c"));
    g_free(t);
    t = g_path_get_basename("/x/y/z.bin");
    CHECK(!strcmp(t, "z.bin"));
    g_free(t);
    t = g_path_get_basename("/x/y/");
    CHECK(!strcmp(t, "y"));
    g_free(t);
    t = g_path_get_dirname("/x/y/z.bin");
    CHECK(!strcmp(t, "/x/y"));
    g_free(t);
    t = g_path_get_dirname("z.bin");
    CHECK(!strcmp(t, "."));
    g_free(t);
    CHECK(g_path_is_absolute("/x") && !g_path_is_absolute("x"));

    t = g_build_filename(dir, "glib", NULL);
    CHECK(g_mkdir_with_parents(t, 0700) == 0);
    CHECK(g_file_test(t, G_FILE_TEST_IS_DIR));
    f = g_build_filename(t, "file.txt", NULL);
    CHECK(g_file_set_contents(f, "contents", -1, NULL));
    CHECK(g_file_test(f, G_FILE_TEST_IS_REGULAR | G_FILE_TEST_EXISTS));
    CHECK(g_file_get_contents(f, &c, &n, NULL));
    CHECK(n == 8 && !strcmp(c, "contents"));
    g_free(c);
    CHECK(!g_file_get_contents("/no/such/file", &c, &n, NULL));

    d = g_dir_open(t, 0, NULL);
    CHECK(d);
    while (d && (name = g_dir_read_name(d))) {
        CHECK(strcmp(name, ".") && strcmp(name, ".."));
        seen += !strcmp(name, "file.txt");
    }
    g_dir_close(d);
    CHECK(seen == 1);

    {
        GMappedFile *m = g_mapped_file_new(f, FALSE, NULL);
        CHECK(m && g_mapped_file_get_length(m) == 8 &&
              !memcmp(g_mapped_file_get_contents(m), "contents", 8));
        g_mapped_file_unref(m);
    }
    {
        gchar *tmp = NULL;
        gint fd = g_file_open_tmp("glibtest-XXXXXX", &tmp, NULL);
        CHECK(fd >= 0 && tmp && g_file_test(tmp, G_FILE_TEST_IS_REGULAR));
        if (fd >= 0) {
            CHECK(g_close(fd, NULL));
        }
        CHECK(tmp && g_unlink(tmp) == 0);
        g_free(tmp);
    }
    CHECK(g_get_system_data_dirs()[0] != NULL);
    CHECK(g_unlink(f) == 0);
    CHECK(!g_file_test(f, G_FILE_TEST_EXISTS));
    CHECK(g_rmdir(t) == 0);
    g_free(f);
    g_free(t);
}

/* ---- the main context, driven as util/main-loop.c drives it -------------- */

static int idle_runs, timeout_runs, notified;

static gboolean idle_cb(gpointer d)
{
    return ++idle_runs < 3;
}

static gboolean timeout_cb(gpointer d)
{
    timeout_runs++;
    return G_SOURCE_REMOVE;
}

static void notify_cb(gpointer d)
{
    notified++;
}

typedef struct {
    GSource source;
    int prepared, dispatched;
    gboolean ready;
} TestSource;

static gboolean ts_prepare(GSource *s, gint *timeout)
{
    TestSource *t = (TestSource *)s;

    t->prepared++;
    *timeout = -1;
    return t->ready;
}

static gboolean ts_check(GSource *s)
{
    return ((TestSource *)s)->ready;
}

static gboolean ts_dispatch(GSource *s, GSourceFunc cb, gpointer d)
{
    TestSource *t = (TestSource *)s;

    t->dispatched++;
    t->ready = FALSE;
    return G_SOURCE_CONTINUE;
}

static GSourceFuncs ts_funcs = {
    .prepare = ts_prepare,
    .check = ts_check,
    .dispatch = ts_dispatch,
};

static void iterate(GMainContext *c)
{
    gint prio, timeout, n;
    GPollFD fds[16];

    g_main_context_prepare(c, &prio);
    n = g_main_context_query(c, prio, &timeout, fds, 16);
    CHECK(n <= 16);
    if (g_main_context_check(c, prio, fds, n)) {
        g_main_context_dispatch(c);
    }
}

static void test_main(void)
{
    GMainContext *c = g_main_context_new();
    TestSource *ts;
    guint id;
    gint64 start;

    g_main_context_push_thread_default(c);
    CHECK(g_main_context_get_thread_default() == c);

    ts = (TestSource *)g_source_new(&ts_funcs, sizeof(TestSource));
    g_source_set_name(&ts->source, "test");
    id = g_source_attach(&ts->source, c);
    CHECK(id && g_main_context_find_source_by_id(c, id) == &ts->source);
    iterate(c);
    CHECK(ts->prepared == 1 && ts->dispatched == 0);
    ts->ready = TRUE;
    iterate(c);
    CHECK(ts->dispatched == 1);

    {
        GSource *idle = g_idle_source_new();
        g_source_set_callback(idle, idle_cb, NULL, notify_cb);
        g_source_attach(idle, c);
        g_source_unref(idle);
    }
    for (int i = 0; i < 5; i++) {
        iterate(c);
    }
    CHECK(idle_runs == 3 && notified == 1);

    {
        GSource *to = g_timeout_source_new(20);
        g_source_set_callback(to, timeout_cb, NULL, NULL);
        g_source_attach(to, c);
        g_source_unref(to);
    }
    start = g_get_monotonic_time();
    while (!timeout_runs && g_get_monotonic_time() - start < 1000000) {
        g_main_context_iteration(c, TRUE);
    }
    CHECK(timeout_runs == 1);
    CHECK(g_get_monotonic_time() - start >= 19000);

    g_source_destroy(&ts->source);
    CHECK(g_source_is_destroyed(&ts->source));
    CHECK(!g_main_context_find_source_by_id(c, id));
    g_source_unref(&ts->source);

    g_main_context_pop_thread_default(c);
    g_main_context_unref(c);
}

static void test_misc(void)
{
    GRand *r = g_rand_new_with_seed(42), *r2 = g_rand_new_with_seed(42);
    GOnce once = G_ONCE_INIT;
    static gsize init;

    for (int i = 0; i < 100; i++) {
        gint32 v = g_rand_int_range(r, 10, 20);
        CHECK(v >= 10 && v < 20);
        CHECK(v == g_rand_int_range(r2, 10, 20));
    }
    g_rand_free(r);
    g_rand_free(r2);

    CHECK(g_once(&once, (GThreadFunc)g_strdup, "x") ==
          g_once(&once, (GThreadFunc)g_strdup, "y"));
    g_free(once.retval);
    /* 1 is a result like any other (QEMU's TCG helpers leave 1) */
    if (g_once_init_enter(&init)) {
        g_once_init_leave(&init, 1);
    }
    CHECK(init == 1 && !g_once_init_enter(&init));


    {
        GQuark q = g_quark_from_string("quark-test");
        CHECK(q && q == g_quark_try_string("quark-test") &&
              !strcmp(g_quark_to_string(q), "quark-test"));
    }
    {
        GDateTime *dt = g_date_time_new_from_unix_utc(86400);
        gchar *t = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S.%f");
        CHECK(!strcmp(t, "1970-01-02 00:00:00.000000"));
        g_free(t);
        g_date_time_unref(dt);
    }
    CHECK(g_get_num_processors() >= 1);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : g_get_tmp_dir();

    test_hash();
    test_tree();
    test_lists();
    test_arrays();
    test_strings();
    test_paths(dir);
    test_main();
    test_misc();
    if (failures) {
        fprintf(stderr, "glib: %d checks failed\n", failures);
        return 1;
    }
    printf("glib: every check passed\n");
    return 0;
}
