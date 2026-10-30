/*
 * The in-tree GLib (libretro/glib/glib.h): arrays, lists, queues, hash
 * tables, balanced trees, sequences and keyed data.
 *
 * GLib's own layouts where callers read the fields (GArray, GPtrArray,
 * GList, GSList, GQueue); the rest is private. libretro-common's rhmap
 * keys on 32-bit hashes alone and cannot take a caller's equality, so the
 * hash table is its own: open addressing, linear probing.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "glib.h"
#include <retro_atomic.h>

/* ---- arrays --------------------------------------------------------------- */

typedef struct {
    GArray a;                   /* what callers see */
    guint alloc;                /* elements allocated */
    guint elt_size;
    gboolean zero_terminated;
    gboolean clear;
    retro_atomic_int_t ref_count;
    GDestroyNotify clear_func;
} GRealArray;

static void g_array_grow(GRealArray *r, guint extra)
{
    guint want = r->a.len + extra + (r->zero_terminated ? 1 : 0);

    if (want > r->alloc) {
        guint n = r->alloc ? r->alloc : 16;
        while (n < want) {
            n *= 2;
        }
        r->a.data = g_realloc(r->a.data, (gsize)n * r->elt_size);
        r->alloc = n;
    }
}

static void g_array_terminate(GRealArray *r)
{
    if (r->zero_terminated) {
        memset(r->a.data + (gsize)r->a.len * r->elt_size, 0, r->elt_size);
    }
}

GArray *g_array_sized_new(gboolean zero_terminated, gboolean clear,
                          guint element_size, guint reserved_size)
{
    GRealArray *r = g_new0(GRealArray, 1);

    r->elt_size = element_size;
    r->zero_terminated = zero_terminated;
    r->clear = clear;
    retro_atomic_int_init(&r->ref_count, 1);
    g_array_grow(r, MAX(reserved_size, 1));
    g_array_terminate(r);
    return &r->a;
}

GArray *g_array_new(gboolean zero_terminated, gboolean clear,
                    guint element_size)
{
    return g_array_sized_new(zero_terminated, clear, element_size, 0);
}

guint g_array_get_element_size(GArray *array)
{
    return ((GRealArray *)array)->elt_size;
}

void g_array_set_clear_func(GArray *array, GDestroyNotify clear_func)
{
    ((GRealArray *)array)->clear_func = clear_func;
}

static void g_array_clear_range(GRealArray *r, guint index, guint length)
{
    if (r->clear_func) {
        for (guint i = 0; i < length; i++) {
            r->clear_func(r->a.data + (gsize)(index + i) * r->elt_size);
        }
    }
}

gchar *g_array_free(GArray *array, gboolean free_segment)
{
    GRealArray *r = (GRealArray *)array;
    gchar *seg = r->a.data;

    if (free_segment) {
        g_array_clear_range(r, 0, r->a.len);
        g_free(seg);
        seg = NULL;
    }
    if (retro_atomic_fetch_sub_int(&r->ref_count, 1) == 1) {
        g_free(r);
    } else {
        r->a.data = NULL;
        r->a.len = r->alloc = 0;
    }
    return seg;
}

/* Immutable arrays can cross worker queues; mutations still need an owner. */
GArray *g_array_ref(GArray *array)
{
    retro_atomic_fetch_add_int(&((GRealArray *)array)->ref_count, 1);
    return array;
}

void g_array_unref(GArray *array)
{
    if (retro_atomic_fetch_sub_int(&((GRealArray *)array)->ref_count, 1) == 1) {
        GRealArray *r = (GRealArray *)array;
        g_array_clear_range(r, 0, r->a.len);
        g_free(r->a.data);
        g_free(r);
    }
}

GArray *g_array_insert_vals(GArray *array, guint index, gconstpointer data,
                            guint len)
{
    GRealArray *r = (GRealArray *)array;
    gsize es = r->elt_size;

    if (!len) {
        return array;
    }
    if (index > r->a.len) {
        g_array_set_size(array, index);
    }
    g_array_grow(r, len);
    memmove(r->a.data + (index + len) * es, r->a.data + index * es,
            (r->a.len - index) * es);
    if (data) {
        memcpy(r->a.data + index * es, data, len * es);
    } else {
        memset(r->a.data + index * es, 0, len * es);
    }
    r->a.len += len;
    g_array_terminate(r);
    return array;
}

GArray *g_array_append_vals(GArray *array, gconstpointer data, guint len)
{
    return g_array_insert_vals(array, array->len, data, len);
}

GArray *g_array_prepend_vals(GArray *array, gconstpointer data, guint len)
{
    return g_array_insert_vals(array, 0, data, len);
}

GArray *g_array_set_size(GArray *array, guint length)
{
    GRealArray *r = (GRealArray *)array;

    if (length > r->a.len) {
        g_array_grow(r, length - r->a.len);
        if (r->clear) {
            memset(r->a.data + (gsize)r->a.len * r->elt_size, 0,
                   (gsize)(length - r->a.len) * r->elt_size);
        }
    } else {
        g_array_clear_range(r, length, r->a.len - length);
    }
    r->a.len = length;
    g_array_terminate(r);
    return array;
}

GArray *g_array_remove_range(GArray *array, guint index, guint length)
{
    GRealArray *r = (GRealArray *)array;
    gsize es = r->elt_size;

    g_array_clear_range(r, index, length);
    memmove(r->a.data + index * es, r->a.data + (index + length) * es,
            (r->a.len - index - length) * es);
    r->a.len -= length;
    g_array_terminate(r);
    return array;
}

GArray *g_array_remove_index(GArray *array, guint index)
{
    return g_array_remove_range(array, index, 1);
}

GArray *g_array_remove_index_fast(GArray *array, guint index)
{
    GRealArray *r = (GRealArray *)array;
    gsize es = r->elt_size;

    g_array_clear_range(r, index, 1);
    if (index != r->a.len - 1) {
        memcpy(r->a.data + index * es, r->a.data + (r->a.len - 1) * es, es);
    }
    r->a.len--;
    g_array_terminate(r);
    return array;
}

void g_array_sort(GArray *array, GCompareFunc compare_func)
{
    if (array->len) {
        qsort(array->data, array->len, ((GRealArray *)array)->elt_size,
              compare_func);
    }
}

/* a GByteArray is a GArray of bytes */
GByteArray *g_byte_array_new(void)
{
    return (GByteArray *)g_array_new(FALSE, FALSE, 1);
}

GByteArray *g_byte_array_sized_new(guint reserved_size)
{
    return (GByteArray *)g_array_sized_new(FALSE, FALSE, 1, reserved_size);
}

GByteArray *g_byte_array_new_take(guint8 *data, gsize len)
{
    GRealArray *r = g_new0(GRealArray, 1);

    r->elt_size = 1;
    retro_atomic_int_init(&r->ref_count, 1);
    r->a.data = (gchar *)data;
    r->a.len = (guint)len;
    r->alloc = (guint)len;
    return (GByteArray *)&r->a;
}

guint8 *g_byte_array_free(GByteArray *array, gboolean free_segment)
{
    return (guint8 *)g_array_free((GArray *)array, free_segment);
}

GByteArray *g_byte_array_ref(GByteArray *array)
{
    return (GByteArray *)g_array_ref((GArray *)array);
}

void g_byte_array_unref(GByteArray *array)
{
    g_array_unref((GArray *)array);
}

GByteArray *g_byte_array_append(GByteArray *array, const guint8 *data,
                                guint len)
{
    return (GByteArray *)g_array_append_vals((GArray *)array, data, len);
}

GByteArray *g_byte_array_set_size(GByteArray *array, guint length)
{
    return (GByteArray *)g_array_set_size((GArray *)array, length);
}

GByteArray *g_byte_array_remove_range(GByteArray *array, guint index,
                                      guint length)
{
    return (GByteArray *)g_array_remove_range((GArray *)array, index, length);
}

/* ---- pointer arrays ------------------------------------------------------- */

typedef struct {
    GPtrArray a;
    guint alloc;
    retro_atomic_int_t ref_count;
    GDestroyNotify free_func;
} GRealPtrArray;

static void g_ptr_array_grow(GRealPtrArray *r, guint extra)
{
    guint want = r->a.len + extra;

    if (want > r->alloc) {
        guint n = r->alloc ? r->alloc : 16;
        while (n < want) {
            n *= 2;
        }
        r->a.pdata = g_renew(gpointer, r->a.pdata, n);
        r->alloc = n;
    }
}

GPtrArray *g_ptr_array_new_full(guint reserved_size,
                                GDestroyNotify element_free_func)
{
    GRealPtrArray *r = g_new0(GRealPtrArray, 1);

    retro_atomic_int_init(&r->ref_count, 1);
    r->free_func = element_free_func;
    if (reserved_size) {
        g_ptr_array_grow(r, reserved_size);
    }
    return &r->a;
}

GPtrArray *g_ptr_array_new(void)
{
    return g_ptr_array_new_full(0, NULL);
}

GPtrArray *g_ptr_array_sized_new(guint reserved_size)
{
    return g_ptr_array_new_full(reserved_size, NULL);
}

GPtrArray *g_ptr_array_new_with_free_func(GDestroyNotify element_free_func)
{
    return g_ptr_array_new_full(0, element_free_func);
}

void g_ptr_array_set_free_func(GPtrArray *array,
                               GDestroyNotify element_free_func)
{
    ((GRealPtrArray *)array)->free_func = element_free_func;
}

gpointer *g_ptr_array_free(GPtrArray *array, gboolean free_segment)
{
    GRealPtrArray *r = (GRealPtrArray *)array;
    gpointer *seg = r->a.pdata;

    if (free_segment) {
        if (r->free_func) {
            for (guint i = 0; i < r->a.len; i++) {
                r->free_func(seg[i]);
            }
        }
        g_free(seg);
        seg = NULL;
    }
    if (retro_atomic_fetch_sub_int(&r->ref_count, 1) == 1) {
        g_free(r);
    } else {
        r->a.pdata = NULL;
        r->a.len = r->alloc = 0;
    }
    return seg;
}

GPtrArray *g_ptr_array_ref(GPtrArray *array)
{
    retro_atomic_fetch_add_int(&((GRealPtrArray *)array)->ref_count, 1);
    return array;
}

void g_ptr_array_unref(GPtrArray *array)
{
    if (retro_atomic_fetch_sub_int(&((GRealPtrArray *)array)->ref_count, 1) == 1) {
        GRealPtrArray *r = (GRealPtrArray *)array;
        if (r->free_func) {
            for (guint i = 0; i < r->a.len; i++) {
                r->free_func(r->a.pdata[i]);
            }
        }
        g_free(r->a.pdata);
        g_free(r);
    }
}

void g_ptr_array_set_size(GPtrArray *array, gint length)
{
    GRealPtrArray *r = (GRealPtrArray *)array;
    guint l = (guint)length;

    if (l > r->a.len) {
        g_ptr_array_grow(r, l - r->a.len);
        memset(r->a.pdata + r->a.len, 0, (l - r->a.len) * sizeof(gpointer));
    } else if (r->free_func) {
        for (guint i = l; i < r->a.len; i++) {
            r->free_func(r->a.pdata[i]);
        }
    }
    r->a.len = l;
}

void g_ptr_array_insert(GPtrArray *array, gint index, gpointer data)
{
    GRealPtrArray *r = (GRealPtrArray *)array;
    guint i = index < 0 ? r->a.len : (guint)index;

    g_ptr_array_grow(r, 1);
    memmove(r->a.pdata + i + 1, r->a.pdata + i,
            (r->a.len - i) * sizeof(gpointer));
    r->a.pdata[i] = data;
    r->a.len++;
}

void g_ptr_array_add(GPtrArray *array, gpointer data)
{
    g_ptr_array_insert(array, -1, data);
}

gpointer g_ptr_array_steal_index(GPtrArray *array, guint index)
{
    gpointer p = array->pdata[index];

    memmove(array->pdata + index, array->pdata + index + 1,
            (array->len - index - 1) * sizeof(gpointer));
    array->len--;
    return p;
}

gpointer g_ptr_array_remove_index(GPtrArray *array, guint index)
{
    GRealPtrArray *r = (GRealPtrArray *)array;
    gpointer p = g_ptr_array_steal_index(array, index);

    if (r->free_func) {
        r->free_func(p);
    }
    return p;
}

gpointer g_ptr_array_remove_index_fast(GPtrArray *array, guint index)
{
    GRealPtrArray *r = (GRealPtrArray *)array;
    gpointer p = array->pdata[index];

    array->pdata[index] = array->pdata[array->len - 1];
    array->len--;
    if (r->free_func) {
        r->free_func(p);
    }
    return p;
}

gboolean g_ptr_array_find_with_equal_func(GPtrArray *haystack,
                                          gconstpointer needle,
                                          GEqualFunc equal_func,
                                          guint *index_)
{
    for (guint i = 0; i < haystack->len; i++) {
        if (equal_func ? equal_func(haystack->pdata[i], needle)
                       : haystack->pdata[i] == needle) {
            if (index_) {
                *index_ = i;
            }
            return TRUE;
        }
    }
    return FALSE;
}

gboolean g_ptr_array_find(GPtrArray *haystack, gconstpointer needle,
                          guint *index_)
{
    return g_ptr_array_find_with_equal_func(haystack, needle, NULL, index_);
}

gboolean g_ptr_array_remove(GPtrArray *array, gpointer data)
{
    guint i;

    if (!g_ptr_array_find(array, data, &i)) {
        return FALSE;
    }
    g_ptr_array_remove_index(array, i);
    return TRUE;
}

gboolean g_ptr_array_remove_fast(GPtrArray *array, gpointer data)
{
    guint i;

    if (!g_ptr_array_find(array, data, &i)) {
        return FALSE;
    }
    g_ptr_array_remove_index_fast(array, i);
    return TRUE;
}

void g_ptr_array_foreach(GPtrArray *array, GFunc func, gpointer user_data)
{
    for (guint i = 0; i < array->len; i++) {
        func(array->pdata[i], user_data);
    }
}

/* the compare function gets pointers to the elements, as in GLib */
void g_ptr_array_sort(GPtrArray *array, GCompareFunc compare_func)
{
    if (array->len) {
        qsort(array->pdata, array->len, sizeof(gpointer), compare_func);
    }
}

/* ---- doubly linked lists -------------------------------------------------- */

GList *g_list_alloc(void)
{
    return g_new0(GList, 1);
}

void g_list_free_1(GList *list)
{
    g_free(list);
}

void g_list_free(GList *list)
{
    while (list) {
        GList *n = list->next;
        g_free(list);
        list = n;
    }
}

void g_list_free_full(GList *list, GDestroyNotify free_func)
{
    for (GList *l = list; l; l = l->next) {
        free_func(l->data);
    }
    g_list_free(list);
}

GList *g_list_last(GList *list)
{
    if (list) {
        while (list->next) {
            list = list->next;
        }
    }
    return list;
}

GList *g_list_first(GList *list)
{
    if (list) {
        while (list->prev) {
            list = list->prev;
        }
    }
    return list;
}

GList *g_list_append(GList *list, gpointer data)
{
    GList *n = g_list_alloc(), *last = g_list_last(list);

    n->data = data;
    if (!last) {
        return n;
    }
    last->next = n;
    n->prev = last;
    return list;
}

GList *g_list_prepend(GList *list, gpointer data)
{
    GList *n = g_list_alloc();

    n->data = data;
    n->next = list;
    if (list) {
        n->prev = list->prev;
        if (list->prev) {
            list->prev->next = n;
        }
        list->prev = n;
    }
    return n;
}

GList *g_list_insert_before(GList *list, GList *sibling, gpointer data)
{
    GList *n;

    if (!sibling) {
        return g_list_append(list, data);
    }
    n = g_list_alloc();
    n->data = data;
    n->prev = sibling->prev;
    n->next = sibling;
    sibling->prev = n;
    if (n->prev) {
        n->prev->next = n;
        return list;
    }
    return n;
}

GList *g_list_insert(GList *list, gpointer data, gint position)
{
    if (position < 0) {
        return g_list_append(list, data);
    }
    if (position == 0) {
        return g_list_prepend(list, data);
    }
    return g_list_insert_before(list, g_list_nth(list, (guint)position),
                                data);
}

GList *g_list_insert_sorted(GList *list, gpointer data, GCompareFunc func)
{
    GList *l = list;

    while (l && func(data, l->data) > 0) {
        l = l->next;
    }
    return g_list_insert_before(list, l, data);
}

GList *g_list_remove_link(GList *list, GList *llink)
{
    if (!llink) {
        return list;
    }
    if (llink->prev) {
        llink->prev->next = llink->next;
    }
    if (llink->next) {
        llink->next->prev = llink->prev;
    }
    if (llink == list) {
        list = list->next;
    }
    llink->next = llink->prev = NULL;
    return list;
}

GList *g_list_delete_link(GList *list, GList *link_)
{
    list = g_list_remove_link(list, link_);
    g_free(link_);
    return list;
}

GList *g_list_find(GList *list, gconstpointer data)
{
    while (list && list->data != data) {
        list = list->next;
    }
    return list;
}

GList *g_list_find_custom(GList *list, gconstpointer data, GCompareFunc func)
{
    while (list && func(list->data, data)) {
        list = list->next;
    }
    return list;
}

GList *g_list_remove(GList *list, gconstpointer data)
{
    GList *l = g_list_find(list, data);

    return l ? g_list_delete_link(list, l) : list;
}

GList *g_list_reverse(GList *list)
{
    GList *last = NULL;

    while (list) {
        last = list;
        list = last->next;
        last->next = last->prev;
        last->prev = list;
    }
    return last;
}

GList *g_list_copy(GList *list)
{
    GList *out = NULL;

    for (; list; list = list->next) {
        out = g_list_prepend(out, list->data);
    }
    return g_list_reverse(out);
}

GList *g_list_concat(GList *list1, GList *list2)
{
    GList *last;

    if (!list2) {
        return list1;
    }
    last = g_list_last(list1);
    if (!last) {
        return list2;
    }
    last->next = list2;
    list2->prev = last;
    return list1;
}

GList *g_list_nth(GList *list, guint n)
{
    while (n-- && list) {
        list = list->next;
    }
    return list;
}

gpointer g_list_nth_data(GList *list, guint n)
{
    list = g_list_nth(list, n);
    return list ? list->data : NULL;
}

guint g_list_length(GList *list)
{
    guint n = 0;

    for (; list; list = list->next) {
        n++;
    }
    return n;
}

void g_list_foreach(GList *list, GFunc func, gpointer user_data)
{
    while (list) {
        GList *next = list->next;   /* func may free the link */
        func(list->data, user_data);
        list = next;
    }
}

/* stable merge sort over the next pointers; prev is fixed up after */
static GSList *g_slist_merge_sort(GSList *list, GCompareDataFunc cmp,
                                  gpointer user_data)
{
    GSList *a, *b, *slow, *fast, head, *t = &head;

    if (!list || !list->next) {
        return list;
    }
    slow = list;
    fast = list->next;
    while (fast && fast->next) {
        slow = slow->next;
        fast = fast->next->next;
    }
    b = slow->next;
    slow->next = NULL;
    a = g_slist_merge_sort(list, cmp, user_data);
    b = g_slist_merge_sort(b, cmp, user_data);
    while (a && b) {
        if (cmp(a->data, b->data, user_data) <= 0) {
            t->next = a;
            a = a->next;
        } else {
            t->next = b;
            b = b->next;
        }
        t = t->next;
    }
    t->next = a ? a : b;
    return head.next;
}

static gint g_cmp_nodata(gconstpointer a, gconstpointer b, gpointer f)
{
    return ((GCompareFunc)f)(a, b);
}

/* a GList link starts with data, next, as a GSList link does */
G_STATIC_ASSERT(offsetof(GList, data) == offsetof(GSList, data));
G_STATIC_ASSERT(offsetof(GList, next) == offsetof(GSList, next));

GList *g_list_sort_with_data(GList *list, GCompareDataFunc compare_func,
                             gpointer user_data)
{
    GList *prev = NULL;

    list = (GList *)g_slist_merge_sort((GSList *)list, compare_func,
                                       user_data);
    for (GList *l = list; l; l = l->next) {
        l->prev = prev;
        prev = l;
    }
    return list;
}

GList *g_list_sort(GList *list, GCompareFunc compare_func)
{
    return g_list_sort_with_data(list, g_cmp_nodata, (gpointer)compare_func);
}

/* ---- singly linked lists -------------------------------------------------- */

GSList *g_slist_alloc(void)
{
    return g_new0(GSList, 1);
}

void g_slist_free_1(GSList *list)
{
    g_free(list);
}

void g_slist_free(GSList *list)
{
    while (list) {
        GSList *n = list->next;
        g_free(list);
        list = n;
    }
}

void g_slist_free_full(GSList *list, GDestroyNotify free_func)
{
    for (GSList *l = list; l; l = l->next) {
        free_func(l->data);
    }
    g_slist_free(list);
}

GSList *g_slist_last(GSList *list)
{
    if (list) {
        while (list->next) {
            list = list->next;
        }
    }
    return list;
}

GSList *g_slist_append(GSList *list, gpointer data)
{
    GSList *n = g_slist_alloc(), *last = g_slist_last(list);

    n->data = data;
    if (!last) {
        return n;
    }
    last->next = n;
    return list;
}

GSList *g_slist_prepend(GSList *list, gpointer data)
{
    GSList *n = g_slist_alloc();

    n->data = data;
    n->next = list;
    return n;
}

GSList *g_slist_insert_sorted(GSList *list, gpointer data, GCompareFunc func)
{
    GSList **p = &list, *n;

    while (*p && func(data, (*p)->data) > 0) {
        p = &(*p)->next;
    }
    n = g_slist_alloc();
    n->data = data;
    n->next = *p;
    *p = n;
    return list;
}

GSList *g_slist_remove_link(GSList *list, GSList *link_)
{
    GSList **p = &list;

    while (*p && *p != link_) {
        p = &(*p)->next;
    }
    if (*p) {
        *p = link_->next;
        link_->next = NULL;
    }
    return list;
}

GSList *g_slist_delete_link(GSList *list, GSList *link_)
{
    list = g_slist_remove_link(list, link_);
    g_free(link_);
    return list;
}

GSList *g_slist_find(GSList *list, gconstpointer data)
{
    while (list && list->data != data) {
        list = list->next;
    }
    return list;
}

GSList *g_slist_find_custom(GSList *list, gconstpointer data,
                            GCompareFunc func)
{
    while (list && func(list->data, data)) {
        list = list->next;
    }
    return list;
}

GSList *g_slist_remove(GSList *list, gconstpointer data)
{
    GSList *l = g_slist_find(list, data);

    return l ? g_slist_delete_link(list, l) : list;
}

GSList *g_slist_reverse(GSList *list)
{
    GSList *prev = NULL;

    while (list) {
        GSList *n = list->next;
        list->next = prev;
        prev = list;
        list = n;
    }
    return prev;
}

GSList *g_slist_copy(GSList *list)
{
    GSList *out = NULL;

    for (; list; list = list->next) {
        out = g_slist_prepend(out, list->data);
    }
    return g_slist_reverse(out);
}

GSList *g_slist_concat(GSList *list1, GSList *list2)
{
    GSList *last = g_slist_last(list1);

    if (!last) {
        return list2;
    }
    last->next = list2;
    return list1;
}

GSList *g_slist_nth(GSList *list, guint n)
{
    while (n-- && list) {
        list = list->next;
    }
    return list;
}

gpointer g_slist_nth_data(GSList *list, guint n)
{
    list = g_slist_nth(list, n);
    return list ? list->data : NULL;
}

guint g_slist_length(GSList *list)
{
    guint n = 0;

    for (; list; list = list->next) {
        n++;
    }
    return n;
}

void g_slist_foreach(GSList *list, GFunc func, gpointer user_data)
{
    while (list) {
        GSList *next = list->next;
        func(list->data, user_data);
        list = next;
    }
}

GSList *g_slist_sort_with_data(GSList *list, GCompareDataFunc compare_func,
                               gpointer user_data)
{
    return g_slist_merge_sort(list, compare_func, user_data);
}

GSList *g_slist_sort(GSList *list, GCompareFunc compare_func)
{
    return g_slist_merge_sort(list, g_cmp_nodata, (gpointer)compare_func);
}

/* ---- queues --------------------------------------------------------------- */

GQueue *g_queue_new(void)
{
    return g_new0(GQueue, 1);
}

void g_queue_init(GQueue *queue)
{
    queue->head = queue->tail = NULL;
    queue->length = 0;
}

void g_queue_clear(GQueue *queue)
{
    g_list_free(queue->head);
    g_queue_init(queue);
}

void g_queue_free(GQueue *queue)
{
    g_list_free(queue->head);
    g_free(queue);
}

void g_queue_free_full(GQueue *queue, GDestroyNotify free_func)
{
    g_list_free_full(queue->head, free_func);
    g_free(queue);
}

gboolean g_queue_is_empty(GQueue *queue)
{
    return queue->head == NULL;
}

guint g_queue_get_length(GQueue *queue)
{
    return queue->length;
}

void g_queue_push_head(GQueue *queue, gpointer data)
{
    queue->head = g_list_prepend(queue->head, data);
    if (!queue->tail) {
        queue->tail = queue->head;
    }
    queue->length++;
}

void g_queue_push_tail(GQueue *queue, gpointer data)
{
    GList *n = g_list_alloc();

    n->data = data;
    n->prev = queue->tail;
    if (queue->tail) {
        queue->tail->next = n;
    } else {
        queue->head = n;
    }
    queue->tail = n;
    queue->length++;
}

static void g_queue_unlink(GQueue *queue, GList *l)
{
    if (l == queue->tail) {
        queue->tail = l->prev;
    }
    queue->head = g_list_delete_link(queue->head, l);
    queue->length--;
}

gpointer g_queue_pop_head(GQueue *queue)
{
    gpointer d;

    if (!queue->head) {
        return NULL;
    }
    d = queue->head->data;
    g_queue_unlink(queue, queue->head);
    return d;
}

gpointer g_queue_pop_tail(GQueue *queue)
{
    gpointer d;

    if (!queue->tail) {
        return NULL;
    }
    d = queue->tail->data;
    g_queue_unlink(queue, queue->tail);
    return d;
}

gpointer g_queue_peek_head(GQueue *queue)
{
    return queue->head ? queue->head->data : NULL;
}

gpointer g_queue_peek_tail(GQueue *queue)
{
    return queue->tail ? queue->tail->data : NULL;
}

gboolean g_queue_remove(GQueue *queue, gconstpointer data)
{
    GList *l = g_list_find(queue->head, data);

    if (!l) {
        return FALSE;
    }
    g_queue_unlink(queue, l);
    return TRUE;
}

void g_queue_foreach(GQueue *queue, GFunc func, gpointer user_data)
{
    g_list_foreach(queue->head, func, user_data);
}

/* ---- hash tables ---------------------------------------------------------- */

/* hash of a slot: 0 empty, 1 removed, else the key's hash (2 or more) */
#define G_HASH_EMPTY 0
#define G_HASH_TOMB 1

struct _GHashTable {
    guint size;                 /* slots, a power of two */
    guint nnodes;               /* keys */
    guint noccupied;            /* keys and tombstones */
    guint *hashes;
    gpointer *keys;
    gpointer *values;
    GHashFunc hash_func;
    GEqualFunc key_equal_func;
    GDestroyNotify key_destroy_func;
    GDestroyNotify value_destroy_func;
    retro_atomic_int_t ref_count;
    gint version;               /* bumped on every change, for iterators */
};

typedef struct {
    GHashTable *table;
    gpointer dummy2, dummy3;
    gint position;
    gboolean dummy5;
    gpointer dummy6;
} GRealIter;
G_STATIC_ASSERT(sizeof(GRealIter) <= sizeof(GHashTableIter));

static guint g_hash_of(GHashTable *t, gconstpointer key)
{
    guint h = t->hash_func(key);

    /* spread (direct and int hashes are weak), keep clear of 0 and 1 */
    h ^= h >> 16;
    h *= 0x45d9f3bu;
    h ^= h >> 16;
    return h < 2 ? h + 2 : h;
}

static void g_hash_alloc(GHashTable *t, guint size)
{
    t->size = size;
    t->hashes = g_new0(guint, size);
    t->keys = g_new0(gpointer, size);
    t->values = g_new0(gpointer, size);
    t->noccupied = 0;
}

static void g_hash_resize(GHashTable *t, guint size)
{
    guint old_size = t->size, *old_hashes = t->hashes;
    gpointer *old_keys = t->keys, *old_values = t->values;

    g_hash_alloc(t, size);
    for (guint i = 0; i < old_size; i++) {
        if (old_hashes[i] >= 2) {
            guint j = old_hashes[i] & (size - 1);
            while (t->hashes[j] != G_HASH_EMPTY) {
                j = (j + 1) & (size - 1);
            }
            t->hashes[j] = old_hashes[i];
            t->keys[j] = old_keys[i];
            t->values[j] = old_values[i];
            t->noccupied++;
        }
    }
    g_free(old_hashes);
    g_free(old_keys);
    g_free(old_values);
}

static void g_hash_maybe_resize(GHashTable *t)
{
    guint size = 8;

    if (t->noccupied * 4 < t->size * 3 &&
        (t->size <= 8 || t->nnodes * 8 >= t->size)) {
        return;
    }
    while (size < t->nnodes * 2) {
        size *= 2;
    }
    g_hash_resize(t, size);
}

/* the slot holding key, or ~0u */
static guint g_hash_find(GHashTable *t, gconstpointer key, guint h)
{
    guint i = h & (t->size - 1);

    while (t->hashes[i] != G_HASH_EMPTY) {
        if (t->hashes[i] == h &&
            (t->key_equal_func ? t->key_equal_func(t->keys[i], key)
                               : t->keys[i] == key)) {
            return i;
        }
        i = (i + 1) & (t->size - 1);
    }
    return ~0u;
}

GHashTable *g_hash_table_new_full(GHashFunc hash_func,
                                  GEqualFunc key_equal_func,
                                  GDestroyNotify key_destroy_func,
                                  GDestroyNotify value_destroy_func)
{
    GHashTable *t = g_new0(GHashTable, 1);

    t->hash_func = hash_func ? hash_func : g_direct_hash;
    t->key_equal_func = key_equal_func;
    t->key_destroy_func = key_destroy_func;
    t->value_destroy_func = value_destroy_func;
    retro_atomic_int_init(&t->ref_count, 1);
    g_hash_alloc(t, 8);
    return t;
}

GHashTable *g_hash_table_new(GHashFunc hash_func, GEqualFunc key_equal_func)
{
    return g_hash_table_new_full(hash_func, key_equal_func, NULL, NULL);
}

static void g_hash_clear_slot(GHashTable *t, guint i, gboolean notify)
{
    gpointer k = t->keys[i], v = t->values[i];

    t->hashes[i] = G_HASH_TOMB;
    t->keys[i] = t->values[i] = NULL;
    t->nnodes--;
    t->version++;
    if (notify) {
        if (t->key_destroy_func) {
            t->key_destroy_func(k);
        }
        if (t->value_destroy_func) {
            t->value_destroy_func(v);
        }
    }
}

void g_hash_table_remove_all(GHashTable *t)
{
    for (guint i = 0; i < t->size; i++) {
        if (t->hashes[i] >= 2) {
            g_hash_clear_slot(t, i, TRUE);
        }
    }
    g_hash_maybe_resize(t);
}

GHashTable *g_hash_table_ref(GHashTable *t)
{
    retro_atomic_fetch_add_int(&t->ref_count, 1);
    return t;
}

void g_hash_table_unref(GHashTable *t)
{
    if (retro_atomic_fetch_sub_int(&t->ref_count, 1) == 1) {
        g_hash_table_remove_all(t);
        g_free(t->hashes);
        g_free(t->keys);
        g_free(t->values);
        g_free(t);
    }
}

void g_hash_table_destroy(GHashTable *t)
{
    g_hash_table_remove_all(t);
    g_hash_table_unref(t);
}

static gboolean g_hash_insert(GHashTable *t, gpointer key, gpointer value,
                              gboolean keep_new_key)
{
    guint h = g_hash_of(t, key), i = g_hash_find(t, key, h);

    if (i != ~0u) {
        gpointer old_key = t->keys[i], old_value = t->values[i];
        if (keep_new_key) {
            t->keys[i] = key;
            if (t->key_destroy_func && old_key != key) {
                t->key_destroy_func(old_key);
            }
        } else if (t->key_destroy_func && old_key != key) {
            t->key_destroy_func(key);
        }
        t->values[i] = value;
        if (t->value_destroy_func && old_value != value) {
            t->value_destroy_func(old_value);
        }
        return FALSE;
    }
    i = h & (t->size - 1);
    while (t->hashes[i] >= 2) {
        i = (i + 1) & (t->size - 1);
    }
    if (t->hashes[i] == G_HASH_EMPTY) {
        t->noccupied++;
    }
    t->hashes[i] = h;
    t->keys[i] = key;
    t->values[i] = value;
    t->nnodes++;
    t->version++;
    g_hash_maybe_resize(t);
    return TRUE;
}

gboolean g_hash_table_insert(GHashTable *t, gpointer key, gpointer value)
{
    return g_hash_insert(t, key, value, FALSE);
}

gboolean g_hash_table_replace(GHashTable *t, gpointer key, gpointer value)
{
    return g_hash_insert(t, key, value, TRUE);
}

gboolean g_hash_table_add(GHashTable *t, gpointer key)
{
    return g_hash_insert(t, key, key, TRUE);
}

gboolean g_hash_table_lookup_extended(GHashTable *t, gconstpointer lookup_key,
                                      gpointer *orig_key, gpointer *value)
{
    guint i = g_hash_find(t, lookup_key, g_hash_of(t, lookup_key));

    if (i == ~0u) {
        return FALSE;
    }
    if (orig_key) {
        *orig_key = t->keys[i];
    }
    if (value) {
        *value = t->values[i];
    }
    return TRUE;
}

gpointer g_hash_table_lookup(GHashTable *t, gconstpointer key)
{
    gpointer v = NULL;

    g_hash_table_lookup_extended(t, key, NULL, &v);
    return v;
}

gboolean g_hash_table_contains(GHashTable *t, gconstpointer key)
{
    return g_hash_find(t, key, g_hash_of(t, key)) != ~0u;
}

static gboolean g_hash_remove(GHashTable *t, gconstpointer key,
                              gboolean notify)
{
    guint i = g_hash_find(t, key, g_hash_of(t, key));

    if (i == ~0u) {
        return FALSE;
    }
    g_hash_clear_slot(t, i, notify);
    g_hash_maybe_resize(t);
    return TRUE;
}

gboolean g_hash_table_remove(GHashTable *t, gconstpointer key)
{
    return g_hash_remove(t, key, TRUE);
}

gboolean g_hash_table_steal(GHashTable *t, gconstpointer key)
{
    return g_hash_remove(t, key, FALSE);
}

gboolean g_hash_table_steal_extended(GHashTable *t, gconstpointer lookup_key,
                                     gpointer *stolen_key,
                                     gpointer *stolen_value)
{
    guint i = g_hash_find(t, lookup_key, g_hash_of(t, lookup_key));

    if (i == ~0u) {
        if (stolen_key) {
            *stolen_key = NULL;
        }
        if (stolen_value) {
            *stolen_value = NULL;
        }
        return FALSE;
    }
    if (stolen_key) {
        *stolen_key = t->keys[i];
    }
    if (stolen_value) {
        *stolen_value = t->values[i];
    }
    g_hash_clear_slot(t, i, FALSE);
    g_hash_maybe_resize(t);
    return TRUE;
}

void g_hash_table_foreach(GHashTable *t, GHFunc func, gpointer user_data)
{
    for (guint i = 0; i < t->size; i++) {
        if (t->hashes[i] >= 2) {
            func(t->keys[i], t->values[i], user_data);
        }
    }
}

gpointer g_hash_table_find(GHashTable *t, GHRFunc predicate,
                           gpointer user_data)
{
    for (guint i = 0; i < t->size; i++) {
        if (t->hashes[i] >= 2 &&
            predicate(t->keys[i], t->values[i], user_data)) {
            return t->values[i];
        }
    }
    return NULL;
}

static guint g_hash_foreach_remove(GHashTable *t, GHRFunc func,
                                   gpointer user_data, gboolean notify)
{
    guint n = 0;

    for (guint i = 0; i < t->size; i++) {
        if (t->hashes[i] >= 2 && func(t->keys[i], t->values[i], user_data)) {
            g_hash_clear_slot(t, i, notify);
            n++;
        }
    }
    g_hash_maybe_resize(t);
    return n;
}

guint g_hash_table_foreach_remove(GHashTable *t, GHRFunc func,
                                  gpointer user_data)
{
    return g_hash_foreach_remove(t, func, user_data, TRUE);
}

guint g_hash_table_foreach_steal(GHashTable *t, GHRFunc func,
                                 gpointer user_data)
{
    return g_hash_foreach_remove(t, func, user_data, FALSE);
}

guint g_hash_table_size(GHashTable *t)
{
    return t->nnodes;
}

GList *g_hash_table_get_keys(GHashTable *t)
{
    GList *l = NULL;

    for (guint i = 0; i < t->size; i++) {
        if (t->hashes[i] >= 2) {
            l = g_list_prepend(l, t->keys[i]);
        }
    }
    return l;
}

GList *g_hash_table_get_values(GHashTable *t)
{
    GList *l = NULL;

    for (guint i = 0; i < t->size; i++) {
        if (t->hashes[i] >= 2) {
            l = g_list_prepend(l, t->values[i]);
        }
    }
    return l;
}

void g_hash_table_iter_init(GHashTableIter *iter, GHashTable *hash_table)
{
    GRealIter *ri = (GRealIter *)iter;

    ri->table = hash_table;
    ri->position = -1;
}

gboolean g_hash_table_iter_next(GHashTableIter *iter, gpointer *key,
                                gpointer *value)
{
    GRealIter *ri = (GRealIter *)iter;
    GHashTable *t = ri->table;

    for (guint i = (guint)(ri->position + 1); i < t->size; i++) {
        if (t->hashes[i] >= 2) {
            ri->position = (gint)i;
            if (key) {
                *key = t->keys[i];
            }
            if (value) {
                *value = t->values[i];
            }
            return TRUE;
        }
    }
    ri->position = (gint)t->size;
    return FALSE;
}

/*
 * Removing through an iterator leaves a tombstone and does not resize, so
 * the positions still to come stay where they are.
 */
void g_hash_table_iter_remove(GHashTableIter *iter)
{
    GRealIter *ri = (GRealIter *)iter;

    g_hash_clear_slot(ri->table, (guint)ri->position, TRUE);
}

void g_hash_table_iter_steal(GHashTableIter *iter)
{
    GRealIter *ri = (GRealIter *)iter;

    g_hash_clear_slot(ri->table, (guint)ri->position, FALSE);
}

GHashTable *g_hash_table_iter_get_hash_table(GHashTableIter *iter)
{
    return ((GRealIter *)iter)->table;
}

guint g_str_hash(gconstpointer v)
{
    guint32 h = 5381;

    for (const signed char *p = v; *p; p++) {
        h = (h << 5) + h + (guint32)*p;
    }
    return h;
}

gboolean g_str_equal(gconstpointer v1, gconstpointer v2)
{
    return strcmp(v1, v2) == 0;
}

guint g_direct_hash(gconstpointer v)
{
    return (guint)GPOINTER_TO_SIZE(v);
}

gboolean g_direct_equal(gconstpointer v1, gconstpointer v2)
{
    return v1 == v2;
}

guint g_int_hash(gconstpointer v)
{
    return (guint)*(const gint *)v;
}

gboolean g_int_equal(gconstpointer v1, gconstpointer v2)
{
    return *(const gint *)v1 == *(const gint *)v2;
}

guint g_int64_hash(gconstpointer v)
{
    guint64 x = *(const guint64 *)v;

    return (guint)(x ^ (x >> 32));
}

gboolean g_int64_equal(gconstpointer v1, gconstpointer v2)
{
    return *(const gint64 *)v1 == *(const gint64 *)v2;
}

/* ---- balanced trees: a treap, with parent links for in-order steps -------- */

struct _GTreeNode {
    gpointer key;
    gpointer value;
    GTreeNode *left, *right, *parent;
    guint32 priority;
};

struct _GTree {
    GTreeNode *root;
    GCompareDataFunc key_compare;
    gpointer key_compare_data;
    GDestroyNotify key_destroy_func;
    GDestroyNotify value_destroy_func;
    gint nnodes;
    retro_atomic_int_t ref_count;
    guint32 seed;
};

GTree *g_tree_new_full(GCompareDataFunc key_compare_func,
                       gpointer key_compare_data,
                       GDestroyNotify key_destroy_func,
                       GDestroyNotify value_destroy_func)
{
    GTree *t = g_new0(GTree, 1);

    t->key_compare = key_compare_func;
    t->key_compare_data = key_compare_data;
    t->key_destroy_func = key_destroy_func;
    t->value_destroy_func = value_destroy_func;
    retro_atomic_int_init(&t->ref_count, 1);
    t->seed = 0x2545f491u;
    return t;
}

GTree *g_tree_new_with_data(GCompareDataFunc key_compare_func,
                            gpointer key_compare_data)
{
    return g_tree_new_full(key_compare_func, key_compare_data, NULL, NULL);
}

GTree *g_tree_new(GCompareFunc key_compare_func)
{
    return g_tree_new_full(g_cmp_nodata, (gpointer)key_compare_func, NULL,
                           NULL);
}

static gint g_tree_cmp(GTree *t, gconstpointer a, gconstpointer b)
{
    return t->key_compare(a, b, t->key_compare_data);
}

static void g_tree_free_node(GTree *t, GTreeNode *n, gboolean notify)
{
    if (notify) {
        if (t->key_destroy_func) {
            t->key_destroy_func(n->key);
        }
        if (t->value_destroy_func) {
            t->value_destroy_func(n->value);
        }
    }
    g_free(n);
}

static void g_tree_free_all(GTree *t, GTreeNode *n)
{
    while (n) {
        GTreeNode *r = n->right;
        g_tree_free_all(t, n->left);
        g_tree_free_node(t, n, TRUE);
        n = r;
    }
}

void g_tree_destroy(GTree *tree)
{
    g_tree_free_all(tree, tree->root);
    tree->root = NULL;
    tree->nnodes = 0;
    g_tree_unref(tree);
}

GTree *g_tree_ref(GTree *tree)
{
    retro_atomic_fetch_add_int(&tree->ref_count, 1);
    return tree;
}

void g_tree_unref(GTree *tree)
{
    if (retro_atomic_fetch_sub_int(&tree->ref_count, 1) == 1) {
        g_tree_free_all(tree, tree->root);
        g_free(tree);
    }
}

/* the link that points at n */
static GTreeNode **g_tree_link(GTree *t, GTreeNode *n)
{
    if (!n->parent) {
        return &t->root;
    }
    return n->parent->left == n ? &n->parent->left : &n->parent->right;
}

/* turn n's child c above n */
static void g_tree_rotate_up(GTree *t, GTreeNode *c)
{
    GTreeNode *n = c->parent, **link = g_tree_link(t, n);

    if (n->left == c) {
        n->left = c->right;
        if (c->right) {
            c->right->parent = n;
        }
        c->right = n;
    } else {
        n->right = c->left;
        if (c->left) {
            c->left->parent = n;
        }
        c->left = n;
    }
    c->parent = n->parent;
    n->parent = c;
    *link = c;
}

GTreeNode *g_tree_lookup_node(GTree *tree, gconstpointer key)
{
    GTreeNode *n = tree->root;

    while (n) {
        gint c = g_tree_cmp(tree, key, n->key);
        if (!c) {
            return n;
        }
        n = c < 0 ? n->left : n->right;
    }
    return NULL;
}

static GTreeNode *g_tree_put(GTree *t, gpointer key, gpointer value,
                             gboolean replace_key)
{
    GTreeNode **link = &t->root, *parent = NULL, *n;

    while (*link) {
        gint c = g_tree_cmp(t, key, (*link)->key);
        if (!c) {
            n = *link;
            if (t->value_destroy_func) {
                t->value_destroy_func(n->value);
            }
            n->value = value;
            if (replace_key) {
                if (t->key_destroy_func) {
                    t->key_destroy_func(n->key);
                }
                n->key = key;
            } else if (t->key_destroy_func) {
                t->key_destroy_func(key);
            }
            return n;
        }
        parent = *link;
        link = c < 0 ? &parent->left : &parent->right;
    }
    n = g_new0(GTreeNode, 1);
    n->key = key;
    n->value = value;
    n->parent = parent;
    t->seed ^= t->seed << 13;
    t->seed ^= t->seed >> 17;
    t->seed ^= t->seed << 5;
    n->priority = t->seed;
    *link = n;
    while (n->parent && n->parent->priority < n->priority) {
        g_tree_rotate_up(t, n);
    }
    t->nnodes++;
    return n;
}

GTreeNode *g_tree_insert_node(GTree *tree, gpointer key, gpointer value)
{
    return g_tree_put(tree, key, value, FALSE);
}

void g_tree_insert(GTree *tree, gpointer key, gpointer value)
{
    g_tree_put(tree, key, value, FALSE);
}

void g_tree_replace(GTree *tree, gpointer key, gpointer value)
{
    g_tree_put(tree, key, value, TRUE);
}

static gboolean g_tree_delete(GTree *t, gconstpointer key, gboolean notify)
{
    GTreeNode *n = g_tree_lookup_node(t, key);

    if (!n) {
        return FALSE;
    }
    /* turn it down to a leaf or a single child, then cut it out */
    while (n->left && n->right) {
        g_tree_rotate_up(t, n->left->priority > n->right->priority ? n->left
                                                                   : n->right);
    }
    {
        GTreeNode *c = n->left ? n->left : n->right;
        *g_tree_link(t, n) = c;
        if (c) {
            c->parent = n->parent;
        }
    }
    t->nnodes--;
    g_tree_free_node(t, n, notify);
    return TRUE;
}

gboolean g_tree_remove(GTree *tree, gconstpointer key)
{
    return g_tree_delete(tree, key, TRUE);
}

gboolean g_tree_steal(GTree *tree, gconstpointer key)
{
    return g_tree_delete(tree, key, FALSE);
}

gpointer g_tree_lookup(GTree *tree, gconstpointer key)
{
    GTreeNode *n = g_tree_lookup_node(tree, key);

    return n ? n->value : NULL;
}

gboolean g_tree_lookup_extended(GTree *tree, gconstpointer lookup_key,
                                gpointer *orig_key, gpointer *value)
{
    GTreeNode *n = g_tree_lookup_node(tree, lookup_key);

    if (!n) {
        return FALSE;
    }
    if (orig_key) {
        *orig_key = n->key;
    }
    if (value) {
        *value = n->value;
    }
    return TRUE;
}

GTreeNode *g_tree_node_first(GTree *tree)
{
    GTreeNode *n = tree->root;

    while (n && n->left) {
        n = n->left;
    }
    return n;
}

GTreeNode *g_tree_node_last(GTree *tree)
{
    GTreeNode *n = tree->root;

    while (n && n->right) {
        n = n->right;
    }
    return n;
}

GTreeNode *g_tree_node_next(GTreeNode *node)
{
    if (node->right) {
        node = node->right;
        while (node->left) {
            node = node->left;
        }
        return node;
    }
    while (node->parent && node->parent->right == node) {
        node = node->parent;
    }
    return node->parent;
}

GTreeNode *g_tree_node_previous(GTreeNode *node)
{
    if (node->left) {
        node = node->left;
        while (node->right) {
            node = node->right;
        }
        return node;
    }
    while (node->parent && node->parent->left == node) {
        node = node->parent;
    }
    return node->parent;
}

gpointer g_tree_node_key(GTreeNode *node)
{
    return node->key;
}

gpointer g_tree_node_value(GTreeNode *node)
{
    return node->value;
}

/* the first node whose key is >= key (upper: > key) */
static GTreeNode *g_tree_bound(GTree *t, gconstpointer key, gboolean upper)
{
    GTreeNode *n = t->root, *best = NULL;

    while (n) {
        gint c = g_tree_cmp(t, key, n->key);
        if (c < 0 || (c == 0 && !upper)) {
            best = n;
            n = n->left;
        } else {
            n = n->right;
        }
    }
    return best;
}

GTreeNode *g_tree_lower_bound(GTree *tree, gconstpointer key)
{
    return g_tree_bound(tree, key, FALSE);
}

GTreeNode *g_tree_upper_bound(GTree *tree, gconstpointer key)
{
    return g_tree_bound(tree, key, TRUE);
}

/* in order; the function must not change the tree, as in GLib */
void g_tree_foreach(GTree *tree, GTraverseFunc func, gpointer user_data)
{
    for (GTreeNode *n = g_tree_node_first(tree); n; n = g_tree_node_next(n)) {
        if (func(n->key, n->value, user_data)) {
            break;
        }
    }
}

gpointer g_tree_search(GTree *tree, GCompareFunc search_func,
                       gconstpointer user_data)
{
    GTreeNode *n = tree->root;

    while (n) {
        gint c = search_func(n->key, user_data);
        if (!c) {
            return n->value;
        }
        n = c < 0 ? n->right : n->left;
    }
    return NULL;
}

gint g_tree_nnodes(GTree *tree)
{
    return tree->nnodes;
}

static gint g_tree_node_height(GTreeNode *n)
{
    return n ? 1 + MAX(g_tree_node_height(n->left),
                       g_tree_node_height(n->right)) : 0;
}

gint g_tree_height(GTree *tree)
{
    return g_tree_node_height(tree->root);
}

/* ---- sequences: kept sorted in a list (PCI keeps a handful) --------------- */

struct _GSequenceNode {
    gpointer data;
    GSequence *seq;
    struct _GSequenceNode *prev, *next;
};

struct _GSequence {
    struct _GSequenceNode *head;
    GDestroyNotify data_destroy;
    gint length;
};

GSequence *g_sequence_new(GDestroyNotify data_destroy)
{
    GSequence *s = g_new0(GSequence, 1);

    s->data_destroy = data_destroy;
    return s;
}

void g_sequence_free(GSequence *seq)
{
    while (seq->head) {
        g_sequence_remove(seq->head);
    }
    g_free(seq);
}

GSequenceIter *g_sequence_insert_sorted(GSequence *seq, gpointer data,
                                        GCompareDataFunc cmp_func,
                                        gpointer cmp_data)
{
    struct _GSequenceNode *n = g_new0(struct _GSequenceNode, 1), *p = NULL;
    struct _GSequenceNode **link = &seq->head;

    /* after any equal ones, as GLib places it */
    while (*link && cmp_func((*link)->data, data, cmp_data) <= 0) {
        p = *link;
        link = &p->next;
    }
    n->data = data;
    n->seq = seq;
    n->prev = p;
    n->next = *link;
    if (n->next) {
        n->next->prev = n;
    }
    *link = n;
    seq->length++;
    return n;
}

GSequenceIter *g_sequence_lookup(GSequence *seq, gpointer data,
                                 GCompareDataFunc cmp_func, gpointer cmp_data)
{
    for (struct _GSequenceNode *n = seq->head; n; n = n->next) {
        gint c = cmp_func(n->data, data, cmp_data);
        if (!c) {
            return n;
        }
        if (c > 0) {
            break;
        }
    }
    return NULL;
}

void g_sequence_remove(GSequenceIter *iter)
{
    GSequence *s = iter->seq;

    if (iter->prev) {
        iter->prev->next = iter->next;
    } else {
        s->head = iter->next;
    }
    if (iter->next) {
        iter->next->prev = iter->prev;
    }
    s->length--;
    if (s->data_destroy) {
        s->data_destroy(iter->data);
    }
    g_free(iter);
}

gint g_sequence_get_length(GSequence *seq)
{
    return seq->length;
}

gpointer g_sequence_get(GSequenceIter *iter)
{
    return iter->data;
}

/* ---- keyed data ----------------------------------------------------------- */

struct _GData {
    GQuark key;
    gpointer data;
    GDestroyNotify destroy;
    GData *next;
};

gpointer g_datalist_get_data(GData **datalist, const gchar *key)
{
    GQuark q = g_quark_try_string(key);

    for (GData *d = *datalist; q && d; d = d->next) {
        if (d->key == q) {
            return d->data;
        }
    }
    return NULL;
}

void g_datalist_id_set_data_full(GData **datalist, GQuark key_id,
                                 gpointer data, GDestroyNotify destroy_func)
{
    GData **p = datalist, *d;

    if (!key_id) {
        return;
    }
    while (*p && (*p)->key != key_id) {
        p = &(*p)->next;
    }
    d = *p;
    if (d) {
        if (d->destroy) {
            d->destroy(d->data);
        }
        if (!data) {
            *p = d->next;
            g_free(d);
            return;
        }
    } else {
        if (!data) {
            return;
        }
        d = g_new0(GData, 1);
        d->key = key_id;
        *p = d;
    }
    d->data = data;
    d->destroy = destroy_func;
}

void g_datalist_clear(GData **datalist)
{
    GData *d = *datalist;

    *datalist = NULL;
    while (d) {
        GData *n = d->next;
        if (d->destroy) {
            d->destroy(d->data);
        }
        g_free(d);
        d = n;
    }
}
