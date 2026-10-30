#ifndef QEMU_RCU_QUEUE_H
#define QEMU_RCU_QUEUE_H

/*
 * rcu_queue.h
 *
 * RCU-friendly versions of the queue.h primitives.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <https://www.gnu.org/licenses/>.
 *
 * Copyright (c) 2013 Mike D. Day, IBM Corporation.
 *
 * IBM's contributions to this file may be relicensed under LGPLv2 or later.
 */

#include "qemu/queue.h"
#include <retro_atomic.h>

/*
 * Lists that RCU readers walk while a writer changes them. The links a
 * reader follows (the head's first pointer and each element's next one)
 * are retro_atomic_ptr_t; a reader loads them relaxed, as QEMU's
 * qatomic_rcu_read did (the address dependency orders what it reads
 * through them), and a writer publishes a new element with a release
 * store once its own links are set. The back links only writers use
 * stay plain. Every access to such a list, the writer's too, goes
 * through these macros; an empty zero-length member of the head gives
 * the element type back to FIRST.
 */

#define QRCU_LOAD(type, p)   ((type)retro_atomic_load_relaxed_ptr(p))

/*
 * List
 */
#define QLIST_RCU_HEAD(name, type)                                      \
struct name {                                                           \
        retro_atomic_ptr_t lh_first;    /* struct type * */             \
        struct type *lh_type[0];                                        \
}

#define QLIST_RCU_HEAD_INITIALIZER(head) { NULL }

#define QLIST_RCU_ENTRY(type)                                           \
struct {                                                                \
        retro_atomic_ptr_t le_next;     /* struct type * */             \
        retro_atomic_ptr_t *le_prev;    /* the link to this element */  \
}

#define QLIST_INIT_RCU(head)                                            \
        retro_atomic_store_relaxed_ptr(&(head)->lh_first, NULL)
#define QLIST_EMPTY_RCU(head)                                           \
        (retro_atomic_load_relaxed_ptr(&(head)->lh_first) == NULL)
#define QLIST_FIRST_RCU(head)                                           \
        QRCU_LOAD(__typeof__((head)->lh_type[0]), &(head)->lh_first)
#define QLIST_NEXT_RCU(elm, field)                                      \
        QRCU_LOAD(__typeof__(elm), &(elm)->field.le_next)
#define QLIST_IS_INSERTED_RCU(elm, field) ((elm)->field.le_prev != NULL)

#define QLIST_INSERT_AFTER_RCU(listelm, elm, field) do {                \
    __typeof__(elm) next_ = QLIST_NEXT_RCU((listelm), field);           \
    retro_atomic_store_relaxed_ptr(&(elm)->field.le_next, next_);       \
    (elm)->field.le_prev = &(listelm)->field.le_next;                   \
    retro_atomic_store_release_ptr(&(listelm)->field.le_next, (elm));   \
    if (next_ != NULL) {                                                \
        next_->field.le_prev = &(elm)->field.le_next;                   \
    }                                                                   \
} while (/*CONSTCOND*/0)

#define QLIST_INSERT_BEFORE_RCU(listelm, elm, field) do {               \
    (elm)->field.le_prev = (listelm)->field.le_prev;                    \
    retro_atomic_store_relaxed_ptr(&(elm)->field.le_next, (listelm));   \
    retro_atomic_store_release_ptr((listelm)->field.le_prev, (elm));    \
    (listelm)->field.le_prev = &(elm)->field.le_next;                   \
} while (/*CONSTCOND*/0)

#define QLIST_INSERT_HEAD_RCU(head, elm, field) do {                    \
    __typeof__(elm) next_ = QLIST_FIRST_RCU(head);                      \
    (elm)->field.le_prev = &(head)->lh_first;                           \
    retro_atomic_store_relaxed_ptr(&(elm)->field.le_next, next_);       \
    retro_atomic_store_release_ptr(&(head)->lh_first, (elm));           \
    if (next_ != NULL) {                                                \
        next_->field.le_prev = &(elm)->field.le_next;                   \
    }                                                                   \
} while (/*CONSTCOND*/0)

/* Unlink; a reader still on the element keeps its next link. */
#define QLIST_REMOVE_RCU(elm, field) do {                               \
    __typeof__(elm) next_ = QLIST_NEXT_RCU((elm), field);               \
    if (next_ != NULL) {                                                \
        next_->field.le_prev = (elm)->field.le_prev;                    \
    }                                                                   \
    retro_atomic_store_relaxed_ptr((elm)->field.le_prev, next_);        \
} while (/*CONSTCOND*/0)

/* Unlink an element no reader walks, if it is on the list. */
#define QLIST_SAFE_REMOVE_RCU(elm, field) do {                          \
    if (QLIST_IS_INSERTED_RCU((elm), field)) {                          \
        QLIST_REMOVE_RCU((elm), field);                                 \
        retro_atomic_store_relaxed_ptr(&(elm)->field.le_next, NULL);    \
        (elm)->field.le_prev = NULL;                                    \
    }                                                                   \
} while (/*CONSTCOND*/0)

#define QLIST_FOREACH_RCU(var, head, field)                             \
        for ((var) = QLIST_FIRST_RCU(head);                             \
             (var);                                                     \
             (var) = QLIST_NEXT_RCU((var), field))

#define QLIST_FOREACH_SAFE_RCU(var, head, field, next_var)              \
        for ((var) = QLIST_FIRST_RCU(head);                             \
             (var) && ((next_var) = QLIST_NEXT_RCU((var), field), 1);   \
             (var) = (next_var))

/*
 * Tail queue: the forward links as above; the circular back links, which
 * only writers follow, stay plain.
 */
typedef struct QTailQRcuLink {
    retro_atomic_ptr_t tql_next;
    struct QTailQRcuLink *tql_prev;
} QTailQRcuLink;

#define QTAILQ_RCU_HEAD(name, type)                                     \
union name {                                                            \
        retro_atomic_ptr_t tqh_first;   /* struct type * */             \
        QTailQRcuLink tqh_circ;                                         \
        struct type *tqh_type[0];                                       \
}

#define QTAILQ_RCU_HEAD_INITIALIZER(head)                               \
        { .tqh_circ = { NULL, &(head).tqh_circ } }

#define QTAILQ_RCU_ENTRY(type)                                          \
union {                                                                 \
        retro_atomic_ptr_t tqe_next;    /* struct type * */             \
        QTailQRcuLink tqe_circ;                                         \
}

#define QTAILQ_INIT_RCU(head) do {                                      \
    retro_atomic_store_relaxed_ptr(&(head)->tqh_first, NULL);           \
    (head)->tqh_circ.tql_prev = &(head)->tqh_circ;                      \
} while (/*CONSTCOND*/0)

#define QTAILQ_EMPTY_RCU(head)                                          \
        (retro_atomic_load_relaxed_ptr(&(head)->tqh_first) == NULL)
#define QTAILQ_FIRST_RCU(head)                                          \
        QRCU_LOAD(__typeof__((head)->tqh_type[0]), &(head)->tqh_first)
#define QTAILQ_NEXT_RCU(elm, field)                                     \
        QRCU_LOAD(__typeof__(elm), &(elm)->field.tqe_next)

#define QTAILQ_INSERT_HEAD_RCU(head, elm, field) do {                   \
    __typeof__(elm) next_ = QTAILQ_FIRST_RCU(head);                     \
    retro_atomic_store_relaxed_ptr(&(elm)->field.tqe_next, next_);      \
    if (next_ != NULL) {                                                \
        next_->field.tqe_circ.tql_prev = &(elm)->field.tqe_circ;        \
    } else {                                                            \
        (head)->tqh_circ.tql_prev = &(elm)->field.tqe_circ;             \
    }                                                                   \
    retro_atomic_store_release_ptr(&(head)->tqh_first, (elm));          \
    (elm)->field.tqe_circ.tql_prev = &(head)->tqh_circ;                 \
} while (/*CONSTCOND*/0)

#define QTAILQ_INSERT_TAIL_RCU(head, elm, field) do {                   \
    retro_atomic_store_relaxed_ptr(&(elm)->field.tqe_next, NULL);       \
    (elm)->field.tqe_circ.tql_prev = (head)->tqh_circ.tql_prev;         \
    retro_atomic_store_release_ptr(&(head)->tqh_circ.tql_prev->tql_next,\
                                   (elm));                              \
    (head)->tqh_circ.tql_prev = &(elm)->field.tqe_circ;                 \
} while (/*CONSTCOND*/0)

/* Unlink; a reader still on the element keeps its next link. */
#define QTAILQ_REMOVE_RCU(head, elm, field) do {                        \
    __typeof__(elm) next_ = QTAILQ_NEXT_RCU((elm), field);              \
    if (next_ != NULL) {                                                \
        next_->field.tqe_circ.tql_prev = (elm)->field.tqe_circ.tql_prev;\
    } else {                                                            \
        (head)->tqh_circ.tql_prev = (elm)->field.tqe_circ.tql_prev;     \
    }                                                                   \
    retro_atomic_store_relaxed_ptr(                                     \
        &(elm)->field.tqe_circ.tql_prev->tql_next, next_);              \
    (elm)->field.tqe_circ.tql_prev = NULL;                              \
} while (/*CONSTCOND*/0)

#define QTAILQ_FOREACH_RCU(var, head, field)                            \
        for ((var) = QTAILQ_FIRST_RCU(head);                            \
             (var);                                                     \
             (var) = QTAILQ_NEXT_RCU((var), field))

#define QTAILQ_FOREACH_SAFE_RCU(var, head, field, next_var)             \
        for ((var) = QTAILQ_FIRST_RCU(head);                            \
             (var) && ((next_var) = QTAILQ_NEXT_RCU((var), field), 1);  \
             (var) = (next_var))

/*
 * Singly-linked list with lock-free insertion at the head and a lock-free
 * take of the whole list (the bottom halves).
 */
#define QSLIST_RCU_HEAD(name, type)                                     \
struct name {                                                           \
        retro_atomic_ptr_t slh_first;   /* struct type * */             \
        struct type *slh_type[0];                                       \
}

#define QSLIST_RCU_ENTRY(type)                                          \
struct {                                                                \
        retro_atomic_ptr_t sle_next;    /* struct type * */             \
}

#define QSLIST_INIT_RCU(head)                                           \
        retro_atomic_store_relaxed_ptr(&(head)->slh_first, NULL)
#define QSLIST_EMPTY_RCU(head)                                          \
        (retro_atomic_load_relaxed_ptr(&(head)->slh_first) == NULL)
#define QSLIST_FIRST_RCU(head)                                          \
        QRCU_LOAD(__typeof__((head)->slh_type[0]), &(head)->slh_first)
#define QSLIST_NEXT_RCU(elm, field)                                     \
        QRCU_LOAD(__typeof__(elm), &(elm)->field.sle_next)

/* Any thread; the release publishes the element's own fields. */
#define QSLIST_INSERT_HEAD_ATOMIC_RCU(head, elm, field) do {            \
    void *next_;                                                        \
    do {                                                                \
        next_ = retro_atomic_load_relaxed_ptr(&(head)->slh_first);      \
        retro_atomic_store_relaxed_ptr(&(elm)->field.sle_next, next_);  \
    } while (!retro_atomic_cas_ptr(&(head)->slh_first, next_, (elm)));  \
} while (/*CONSTCOND*/0)

/* Take the whole list from @src into @dest, a list of this thread's. */
#define QSLIST_MOVE_ATOMIC_RCU(dest, src)                               \
        retro_atomic_store_relaxed_ptr(&(dest)->slh_first,              \
            retro_atomic_exchange_ptr(&(src)->slh_first, NULL))

/* The owner of @head only. */
#define QSLIST_REMOVE_HEAD_RCU(head, field)                             \
        retro_atomic_store_relaxed_ptr(&(head)->slh_first,              \
            QSLIST_NEXT_RCU(QSLIST_FIRST_RCU(head), field))

#define QSLIST_FOREACH_RCU(var, head, field)                            \
        for ((var) = QSLIST_FIRST_RCU(head);                            \
             (var);                                                     \
             (var) = QSLIST_NEXT_RCU((var), field))

#endif /* QEMU_RCU_QUEUE_H */
