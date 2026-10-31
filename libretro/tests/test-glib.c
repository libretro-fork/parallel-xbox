/*
 * The core's GLib (libretro/glib) against what GLib itself does: the
 * containers, strings, paths, files, checksums and the main context, as
 * QEMU and libslirp use them. Built and run by run.sh.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include <glib.h>
#include <glib/gstdio.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

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

static GHashTable *reentrant_hash;
static gboolean clear_from_key_destroy;
static int reentrant_destroyed;

static void reentrant_key_destroy(gpointer key)
{
    reentrant_destroyed++;
    if (clear_from_key_destroy) {
        clear_from_key_destroy = FALSE;
        CHECK(GPOINTER_TO_INT(g_hash_table_lookup(reentrant_hash, "reentrant-99")) == 999);
        g_hash_table_remove_all(reentrant_hash);
    }
    g_free(key);
}

static void test_hash_reentrant_replace(void)
{
    reentrant_hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          reentrant_key_destroy, NULL);
    for (int i = 0; i < 100; i++) {
        g_hash_table_insert(reentrant_hash, g_strdup_printf("reentrant-%d", i),
                            GINT_TO_POINTER(i));
    }
    clear_from_key_destroy = TRUE;
    g_hash_table_replace(reentrant_hash, g_strdup("reentrant-99"),
                         GINT_TO_POINTER(999));
    CHECK(g_hash_table_size(reentrant_hash) == 0);
    CHECK(reentrant_destroyed == 101);
    g_hash_table_unref(reentrant_hash);
}

static gboolean insert_from_destroy;
static void reentrant_insert_destroy(gpointer key)
{
    reentrant_destroyed++;
    if (insert_from_destroy) {
        insert_from_destroy = FALSE;
        CHECK(g_hash_table_size(reentrant_hash) == 0);
        g_hash_table_insert(reentrant_hash, g_strdup("survivor"),
                            GINT_TO_POINTER(123));
    }
    g_free(key);
}

static void test_hash_reentrant_clear(void)
{
    reentrant_destroyed = 0;
    reentrant_hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                          reentrant_insert_destroy, NULL);
    for (int i = 0; i < 100; i++) {
        g_hash_table_insert(reentrant_hash, g_strdup_printf("clear-%d", i), NULL);
    }
    insert_from_destroy = TRUE;
    g_hash_table_remove_all(reentrant_hash);
    CHECK(g_hash_table_size(reentrant_hash) == 1);
    CHECK(GPOINTER_TO_INT(g_hash_table_lookup(reentrant_hash, "survivor")) == 123);
    CHECK(reentrant_destroyed == 100);
    g_hash_table_unref(reentrant_hash);
    CHECK(reentrant_destroyed == 101);
}

static int replacement_unref_count;
static void replacement_unref(gpointer pointer)
{
    replacement_unref_count++;
}

static void test_hash_same_reference(void)
{
    int object;
    GHashTable *h = g_hash_table_new_full(g_direct_hash, g_direct_equal,
                                         replacement_unref, replacement_unref);
    replacement_unref_count = 0;
    g_hash_table_insert(h, &object, &object);
    g_hash_table_replace(h, &object, &object);
    CHECK(replacement_unref_count == 2);
    g_hash_table_insert(h, &object, &object);
    CHECK(replacement_unref_count == 4);
    g_hash_table_unref(h);
    CHECK(replacement_unref_count == 6);
}

static GData *reentrant_data;
static gboolean clear_from_data_destroy;
static int data_destroyed;
static void reentrant_data_destroy(gpointer data)
{
    data_destroyed++;
    if (clear_from_data_destroy) {
        clear_from_data_destroy = FALSE;
        CHECK(GPOINTER_TO_INT(g_datalist_get_data(&reentrant_data, "slot")) == 2);
        g_datalist_clear(&reentrant_data);
    }
}

static void test_datalist_reentrant_replace(void)
{
    g_datalist_init(&reentrant_data);
    g_datalist_set_data_full(&reentrant_data, "slot", GINT_TO_POINTER(1),
                             reentrant_data_destroy);
    clear_from_data_destroy = TRUE;
    g_datalist_set_data_full(&reentrant_data, "slot", GINT_TO_POINTER(2),
                             reentrant_data_destroy);
    CHECK(g_datalist_get_data(&reentrant_data, "slot") == NULL);
    CHECK(data_destroyed == 2);
    g_datalist_clear(&reentrant_data);
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
    guint8 *taken = g_memdup2("hello", 5);
    b = g_byte_array_new_take(taken, 5);
    CHECK(b->data == taken && b->len == 5);
    g_byte_array_append(b, (const guint8 *)" world", 6);
    CHECK(b->len == 11 && !memcmp(b->data, "hello world", 11));
    /* free() releases one reference and empties any remaining wrapper. */
    GArray *shared = g_array_new(FALSE, FALSE, sizeof(int));
    g_array_append_val(shared, v);
    g_array_ref(shared);
    g_array_free(shared, TRUE);
    CHECK(shared->len == 0);
    g_array_append_val(shared, v);
    CHECK(shared->len == 1);
    g_array_unref(shared);
    GPtrArray *shared_ptrs = g_ptr_array_new();
    g_ptr_array_add(shared_ptrs, GINT_TO_POINTER(1));
    g_ptr_array_ref(shared_ptrs);
    gpointer *segment = g_ptr_array_free(shared_ptrs, FALSE);
    CHECK(shared_ptrs->len == 0 && segment[0] == GINT_TO_POINTER(1));
    g_free(segment);
    g_ptr_array_add(shared_ptrs, GINT_TO_POINTER(2));
    CHECK(shared_ptrs->len == 1);
    g_ptr_array_unref(shared_ptrs);
    GByteArray *held = g_byte_array_ref(b);
    g_byte_array_unref(b);
    CHECK(held->len == 11 && !memcmp(held->data, "hello world", 11));
    g_byte_array_unref(held);
    b = g_byte_array_new_take(NULL, 0);
    g_byte_array_append(b, (const guint8 *)"abc", 3);
    CHECK(b->len == 3 && !memcmp(b->data, "abc", 3));
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

static void test_bounded_utf_conversion(void)
{
#if defined(_WIN32) || !defined(LR_GLIB_H)
    const gunichar2 wide[] = { 'h', 'i', 'X', 'Y', 0 };
    const gunichar2 emoji[] = { 0xd83d, 0xde00, 'X', 0 };
    glong read = -1, written = -1;
    GError *error = NULL;
    gchar *text = g_utf16_to_utf8(wide, 2, &read, &written, &error);
    CHECK(text && !strcmp(text, "hi") && read == 2 && written == 2 && !error);
    g_free(text);
    text = g_utf16_to_utf8(emoji, 2, &read, &written, &error);
    CHECK(text && !strcmp(text, "\xf0\x9f\x98\x80") && read == 2 && written == 4);
    g_free(text);
    text = g_utf16_to_utf8(wide, 0, &read, &written, &error);
    CHECK(text && !*text && read == 0 && written == 0 && !error);
    g_free(text);
    gunichar2 *converted = g_utf8_to_utf16("hiXY", 2, &read, &written, &error);
    CHECK(converted && converted[0] == 'h' && converted[1] == 'i' &&
          !converted[2] && read == 2 && written == 2 && !error);
    g_free(converted);
    converted = g_utf8_to_utf16("\xf0\x9f\x98\x80X", 4, &read, &written, &error);
    CHECK(converted && converted[0] == 0xd83d && converted[1] == 0xde00 &&
          !converted[2] && read == 4 && written == 2 && !error);
    g_free(converted);
    converted = g_utf8_to_utf16("hiXY", 0, &read, &written, &error);
    CHECK(converted && !converted[0] && read == 0 && written == 0 && !error);
    g_free(converted);
    const gchar raw_bytes[2] = { 'h', 'i' };
    const gunichar2 raw_units[2] = { 'h', 'i' };
    converted = g_utf8_to_utf16(raw_bytes, 2, &read, &written, &error);
    CHECK(converted && converted[0] == 'h' && converted[1] == 'i' &&
          !converted[2] && read == 2 && written == 2 && !error);
    g_free(converted);
    text = g_utf16_to_utf8(raw_units, 2, &read, &written, &error);
    CHECK(text && !strcmp(text, "hi") && read == 2 && written == 2 && !error);
    g_free(text);
    text = g_utf16_to_utf8(emoji, -1, &read, &written, &error);
    CHECK(text && !strcmp(text, "\xf0\x9f\x98\x80X") && read == 3 && written == 5);
    g_free(text);
    converted = g_utf8_to_utf16("\xf0\x9f\x98\x80X", -1, &read, &written, &error);
    CHECK(converted && converted[0] == 0xd83d && converted[1] == 0xde00 &&
          converted[2] == 'X' && !converted[3] && read == 5 && written == 3);
    g_free(converted);
    g_clear_error(&error);
#endif
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
        gint fd = g_open(f, O_RDONLY, 0);
        CHECK(fd >= 0);
        if (fd >= 0) {
            CHECK(lseek(fd, 3, SEEK_SET) == 3);
            GMappedFile *m = g_mapped_file_new_from_fd(fd, FALSE, NULL);
            CHECK(m && g_mapped_file_get_length(m) == 8 &&
                  !memcmp(g_mapped_file_get_contents(m), "contents", 8));
            CHECK(lseek(fd, 0, SEEK_CUR) == 3);
            gchar next = 0;
            CHECK(read(fd, &next, 1) == 1 && next == 't');
            CHECK(g_close(fd, NULL));
            if (m) {
                CHECK(!memcmp(g_mapped_file_get_contents(m), "contents", 8));
                g_mapped_file_unref(m);
            }
        }
        fd = g_open(f, O_RDONLY, 0);
        CHECK(fd >= 0);
        if (fd >= 0) {
            GMappedFile *m = g_mapped_file_new_from_fd(fd, TRUE, NULL);
            CHECK(m != NULL);
            if (m) {
                g_mapped_file_get_contents(m)[0] = 'C';
                g_mapped_file_unref(m);
            }
            CHECK(g_close(fd, NULL));
            gchar *original = NULL;
            CHECK(g_file_get_contents(f, &original, NULL, NULL));
            CHECK(original && !strcmp(original, "contents"));
            g_free(original);
        }
        fd = g_open(f, O_WRONLY, 0);
        CHECK(fd >= 0);
        if (fd >= 0) {
            GError *error = NULL;
            CHECK(lseek(fd, 3, SEEK_SET) == 3);
            GMappedFile *m = g_mapped_file_new_from_fd(fd, FALSE, &error);
            CHECK(!m && error && lseek(fd, 0, SEEK_CUR) == 3);
            if (m) {
                g_mapped_file_unref(m);
            }
            g_clear_error(&error);
            CHECK(g_close(fd, NULL));
        }
        CHECK(g_file_set_contents(f, "", 0, NULL));
        fd = g_open(f, O_RDONLY, 0);
        CHECK(fd >= 0);
        if (fd >= 0) {
            CHECK(lseek(fd, 5, SEEK_SET) == 5);
            GMappedFile *m = g_mapped_file_new_from_fd(fd, FALSE, NULL);
            CHECK(m && g_mapped_file_get_length(m) == 0);
            CHECK(lseek(fd, 0, SEEK_CUR) == 5);
            if (m) {
                g_mapped_file_unref(m);
            }
            CHECK(g_close(fd, NULL));
        }
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

static int pending_source_finalized;
static void pending_source_finalize(GSource *source)
{
    pending_source_finalized++;
}
static GSourceFuncs pending_source_funcs = {
    .prepare = ts_prepare,
    .check = ts_check,
    .dispatch = ts_dispatch,
    .finalize = pending_source_finalize,
};

static void test_pending_source_lifetime(void)
{
    for (int checks = 1; checks <= 2; checks++) {
        GMainContext *c = g_main_context_new();
        TestSource *ts = (TestSource *)g_source_new(&pending_source_funcs,
                                                  sizeof(TestSource));
        GPollFD fds[16];
        gint priority, timeout, n;
        ts->ready = TRUE;
        g_source_attach(&ts->source, c);
        for (int i = 0; i < checks; i++) {
            g_main_context_prepare(c, &priority);
            n = g_main_context_query(c, priority, &timeout, fds, 16);
            CHECK(n <= 16);
            CHECK(g_main_context_check(c, priority, fds, n));
        }
        g_source_destroy(&ts->source);
        g_source_unref(&ts->source);
        g_main_context_unref(c);
        CHECK(pending_source_finalized == checks);
    }
}

static int self_destroy_phase, self_destroy_finalized;
static gboolean self_destroy_prepare(GSource *source, gint *timeout)
{
    *timeout = -1;
    if (!self_destroy_phase) {
        g_source_destroy(source);
    }
    return FALSE;
}
static gboolean self_destroy_check(GSource *source)
{
    g_source_destroy(source);
    return FALSE;
}
static void self_destroy_finalize(GSource *source)
{
    self_destroy_finalized++;
}
static GSourceFuncs self_destroy_funcs = {
    .prepare = self_destroy_prepare,
    .check = self_destroy_check,
    .finalize = self_destroy_finalize,
};
static void test_source_self_removal(void)
{
    for (self_destroy_phase = 0; self_destroy_phase < 2; self_destroy_phase++) {
        GMainContext *c = g_main_context_new();
        GSource *source = g_source_new(&self_destroy_funcs, sizeof(GSource));
        gint priority, timeout, n;
        GPollFD fds[16];
        g_source_attach(source, c);
        g_source_unref(source);
        g_main_context_prepare(c, &priority);
        n = g_main_context_query(c, priority, &timeout, fds, 16);
        CHECK(n <= 16);
        CHECK(!g_main_context_check(c, priority, fds, n));
        g_main_context_unref(c);
        CHECK(self_destroy_finalized == self_destroy_phase + 1);
    }
}

typedef struct {
    GSource source;
    int kind;
} RemovalSource;
static GSource *removal_victim;
static int removal_victim_called, removal_follower_called, removal_finalized;
static gboolean removal_prepare(GSource *source, gint *timeout)
{
    RemovalSource *s = (RemovalSource *)source;
    *timeout = -1;
    if (!s->kind && removal_victim) {
        g_source_destroy(removal_victim);
        removal_victim = NULL;
    } else if (s->kind == 1) {
        removal_victim_called++;
    } else if (s->kind == 2) {
        removal_follower_called++;
    }
    return FALSE;
}
static void removal_finalize(GSource *source)
{
    if (((RemovalSource *)source)->kind == 1) {
        removal_finalized++;
    }
}
static GSourceFuncs removal_funcs = {
    .prepare = removal_prepare,
    .finalize = removal_finalize,
};
static void test_source_removes_next(void)
{
    GMainContext *c = g_main_context_new();
    GSource *retained[2];
    gint priority;
    for (int i = 0; i < 3; i++) {
        RemovalSource *s = (RemovalSource *)g_source_new(&removal_funcs,
                                                        sizeof(RemovalSource));
        s->kind = i;
        g_source_attach(&s->source, c);
        if (i == 1) {
            removal_victim = &s->source;
            g_source_unref(&s->source);
        } else {
            retained[i == 2] = &s->source;
        }
    }
    CHECK(!g_main_context_prepare(c, &priority));
    CHECK(removal_victim_called == 0 && removal_follower_called == 1);
    CHECK(removal_finalized == 1);
    for (int i = 0; i < 2; i++) {
        g_source_destroy(retained[i]);
        g_source_unref(retained[i]);
    }
    g_main_context_unref(c);
}

static void test_child_source_parent_lifetime(void)
{
    GSource *parent = g_idle_source_new();
    GSource *child = g_idle_source_new();
    g_source_add_child_source(parent, child);
    g_source_unref(parent);
    GMainContext *c = g_main_context_new();
    gint priority;
    CHECK(!g_source_is_destroyed(child));
    CHECK(g_source_attach(child, c) != 0);
    CHECK(g_main_context_prepare(c, &priority));
    g_source_destroy(child);
    g_source_unref(child);
    g_main_context_unref(c);
}

static GSource *callback_lifetime_source;
static gboolean callback_active;
static int callback_depth;
static GMainContext *callback_lifetime_context;
static int callback_mode, callback_destroyed;
static gboolean replacement_callback(gpointer data)
{
    return G_SOURCE_REMOVE;
}
static void callback_payload_destroy(gpointer data)
{
    CHECK(!callback_active);
    callback_destroyed++;
    g_free(data);
}
static gboolean callback_lifetime_run(gpointer data)
{
    callback_depth++;
    callback_active = TRUE;
    if (callback_mode == 2 && callback_depth == 1) {
        CHECK(g_main_context_iteration(callback_lifetime_context, FALSE));
    } else if (callback_mode != 1) {
        g_source_set_callback(callback_lifetime_source, replacement_callback,
                              NULL, NULL);
    } else {
        g_source_destroy(callback_lifetime_source);
    }
    CHECK(*(int *)data == 123 && callback_destroyed == callback_mode);
    callback_depth--;
    callback_active = callback_depth != 0;
    return callback_mode == 1 ? G_SOURCE_REMOVE : G_SOURCE_CONTINUE;
}
static void test_dispatch_callback_lifetime(void)
{
    for (callback_mode = 0; callback_mode < 3; callback_mode++) {
        GMainContext *c = g_main_context_new();
        int *data = g_new(int, 1);
        *data = 123;
        callback_lifetime_context = c;
        callback_lifetime_source = g_idle_source_new();
        g_source_set_can_recurse(callback_lifetime_source, callback_mode == 2);
        g_source_set_callback(callback_lifetime_source, callback_lifetime_run,
                              data, callback_payload_destroy);
        g_source_attach(callback_lifetime_source, c);
        CHECK(g_main_context_iteration(c, FALSE));
        CHECK(callback_destroyed == callback_mode + 1);
        g_source_destroy(callback_lifetime_source);
        g_source_unref(callback_lifetime_source);
        g_main_context_unref(c);
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

#include "test-glib-threading.h"

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : g_get_tmp_dir();

    test_threading();
    test_hash();
    test_hash_reentrant_replace();
    test_hash_reentrant_clear();
    test_hash_same_reference();
    test_datalist_reentrant_replace();
    test_tree();
    test_lists();
    test_arrays();
    test_strings();
    test_bounded_utf_conversion();
    test_paths(dir);
    test_main();
    test_pending_source_lifetime();
    test_source_self_removal();
    test_source_removes_next();
    test_child_source_parent_lifetime();
    test_dispatch_callback_lifetime();
    test_misc();
    if (failures) {
        fprintf(stderr, "glib: %d checks failed\n", failures);
        return 1;
    }
    printf("glib: every check passed\n");
    return 0;
}
