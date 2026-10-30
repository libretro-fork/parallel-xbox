/*
 * Helpers for getting linearized buffers from iov / filling buffers into iovs
 *
 * Copyright IBM, Corp. 2007, 2008
 * Copyright (C) 2010 Red Hat, Inc.
 * Copyright (c) 2024 Seagate Technology LLC and/or its Affiliates
 *
 * Author(s):
 *  Anthony Liguori <aliguori@us.ibm.com>
 *  Amit Shah <amit.shah@redhat.com>
 *  Michael Tokarev <mjt@tls.msk.ru>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */

#include "qemu/osdep.h"
#include "qemu/iov.h"
#include "qemu/sockets.h"
#include "qemu/cutils.h"

size_t iov_from_buf_full(const struct iovec *iov, unsigned int iov_cnt,
                         size_t offset, const void *buf, size_t bytes)
{
    size_t done;
    unsigned int i;
    for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
        if (offset < iov[i].iov_len) {
            size_t len = MIN(iov[i].iov_len - offset, bytes - done);
            memcpy(iov[i].iov_base + offset, buf + done, len);
            done += len;
            offset = 0;
        } else {
            offset -= iov[i].iov_len;
        }
    }
    return done;
}

size_t iov_to_buf_full(const struct iovec *iov, const unsigned int iov_cnt,
                       size_t offset, void *buf, size_t bytes)
{
    size_t done;
    unsigned int i;
    for (i = 0, done = 0; (offset || done < bytes) && i < iov_cnt; i++) {
        if (offset < iov[i].iov_len) {
            size_t len = MIN(iov[i].iov_len - offset, bytes - done);
            memcpy(buf + done, iov[i].iov_base + offset, len);
            done += len;
            offset = 0;
        } else {
            offset -= iov[i].iov_len;
        }
    }
    return done;
}

size_t iov_size(const struct iovec *iov, const unsigned int iov_cnt)
{
    size_t len;
    unsigned int i;

    len = 0;
    for (i = 0; i < iov_cnt; i++) {
        len += iov[i].iov_len;
    }
    return len;
}

/* helper function for iov_send_recv() */
static ssize_t
do_send_recv(int sockfd, int flags, struct iovec *iov, unsigned iov_cnt,
             bool do_send)
{
#ifdef CONFIG_POSIX
    ssize_t ret;
    struct msghdr msg;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = iov;
    msg.msg_iovlen = iov_cnt;
    do {
        ret = do_send
            ? sendmsg(sockfd, &msg, flags)
            : recvmsg(sockfd, &msg, flags);
    } while (ret < 0 && errno == EINTR);
    return ret;
#else
    /* else send piece-by-piece */
    /*XXX Note: windows has WSASend() and WSARecv() */
    unsigned i = 0;
    ssize_t ret = 0;
    ssize_t off = 0;
    while (i < iov_cnt) {
        ssize_t r = do_send
            ? send(sockfd, iov[i].iov_base + off, iov[i].iov_len - off, flags)
            : recv(sockfd, iov[i].iov_base + off, iov[i].iov_len - off, flags);
        if (r > 0) {
            ret += r;
            off += r;
            if (off < iov[i].iov_len) {
                continue;
            }
        } else if (!r) {
            break;
        } else if (errno == EINTR) {
            continue;
        } else {
            /* else it is some "other" error,
             * only return if there was no data processed. */
            if (ret == 0) {
                ret = -1;
            }
            break;
        }
        off = 0;
        i++;
    }
    return ret;
#endif
}

ssize_t iov_send_recv(int sockfd, const struct iovec *_iov, unsigned iov_cnt,
                      size_t offset, size_t bytes,
                      bool do_send)
{
    return iov_send_recv_with_flags(sockfd, 0, _iov, iov_cnt, offset, bytes,
                                    do_send);
}

ssize_t iov_send_recv_with_flags(int sockfd, int sockflags,
                                 const struct iovec *_iov,
                                 unsigned iov_cnt, size_t offset,
                                 size_t bytes, bool do_send)
{
    ssize_t total = 0;
    ssize_t ret;
    size_t orig_len, tail;
    unsigned niov;
    struct iovec *local_iov, *iov;

    if (bytes <= 0) {
        return 0;
    }

    local_iov = g_new0(struct iovec, iov_cnt);
    iov_copy(local_iov, iov_cnt, _iov, iov_cnt, offset, bytes);
    offset = 0;
    iov = local_iov;

    while (bytes > 0) {
        /* Find the start position, skipping `offset' bytes:
         * first, skip all full-sized vector elements, */
        for (niov = 0; niov < iov_cnt && offset >= iov[niov].iov_len; ++niov) {
            offset -= iov[niov].iov_len;
        }

        /* niov == iov_cnt would only be valid if bytes == 0, which
         * we already ruled out in the loop condition.  */
        assert(niov < iov_cnt);
        iov += niov;
        iov_cnt -= niov;

        if (offset) {
            /* second, skip `offset' bytes from the (now) first element,
             * undo it on exit */
            iov[0].iov_base += offset;
            iov[0].iov_len -= offset;
        }
        /* Find the end position skipping `bytes' bytes: */
        /* first, skip all full-sized elements */
        tail = bytes;
        for (niov = 0; niov < iov_cnt && iov[niov].iov_len <= tail; ++niov) {
            tail -= iov[niov].iov_len;
        }
        if (tail) {
            /* second, fixup the last element, and remember the original
             * length */
            assert(niov < iov_cnt);
            assert(iov[niov].iov_len > tail);
            orig_len = iov[niov].iov_len;
            iov[niov++].iov_len = tail;
            ret = do_send_recv(sockfd, sockflags, iov, niov, do_send);
            /* Undo the changes above before checking for errors */
            iov[niov-1].iov_len = orig_len;
        } else {
            ret = do_send_recv(sockfd, sockflags, iov, niov, do_send);
        }
        if (offset) {
            iov[0].iov_base -= offset;
            iov[0].iov_len += offset;
        }

        if (ret < 0) {
            assert(errno != EINTR);
            g_free(local_iov);
            if (errno == EAGAIN && total > 0) {
                return total;
            }
            return -1;
        }

        if (ret == 0 && !do_send) {
            /* recv returns 0 when the peer has performed an orderly
             * shutdown. */
            break;
        }

        /* Prepare for the next iteration */
        offset += ret;
        total += ret;
        bytes -= ret;
    }

    g_free(local_iov);
    return total;
}


unsigned iov_copy(struct iovec *dst_iov, unsigned int dst_iov_cnt,
                 const struct iovec *iov, unsigned int iov_cnt,
                 size_t offset, size_t bytes)
{
    size_t len;
    unsigned int i, j;
    for (i = 0, j = 0;
         i < iov_cnt && j < dst_iov_cnt && (offset || bytes); i++) {
        if (offset >= iov[i].iov_len) {
            offset -= iov[i].iov_len;
            continue;
        }
        len = MIN(bytes, iov[i].iov_len - offset);

        dst_iov[j].iov_base = iov[i].iov_base + offset;
        dst_iov[j].iov_len = len;
        j++;
        bytes -= len;
        offset = 0;
    }
    return j;
}

/* io vectors */

void qemu_iovec_init(QEMUIOVector *qiov, int alloc_hint)
{
    qiov->iov = g_new(struct iovec, alloc_hint);
    qiov->niov = 0;
    qiov->nalloc = alloc_hint;
    qiov->size = 0;
}

void qemu_iovec_add(QEMUIOVector *qiov, void *base, size_t len)
{
    assert(qiov->nalloc != -1);

    if (qiov->niov == qiov->nalloc) {
        qiov->nalloc = 2 * qiov->nalloc + 1;
        qiov->iov = g_renew(struct iovec, qiov->iov, qiov->nalloc);
    }
    qiov->iov[qiov->niov].iov_base = base;
    qiov->iov[qiov->niov].iov_len = len;
    qiov->size += len;
    ++qiov->niov;
}

void qemu_iovec_reset(QEMUIOVector *qiov)
{
    assert(qiov->nalloc != -1);

    qiov->niov = 0;
    qiov->size = 0;
}

size_t qemu_iovec_from_buf(QEMUIOVector *qiov, size_t offset,
                           const void *buf, size_t bytes)
{
    return iov_from_buf(qiov->iov, qiov->niov, offset, buf, bytes);
}

typedef struct {
    int src_index;
    struct iovec *src_iov;
    void *dest_base;
} IOVectorSortElem;

size_t iov_discard_front_undoable(struct iovec **iov,
                                  unsigned int *iov_cnt,
                                  size_t bytes,
                                  IOVDiscardUndo *undo)
{
    size_t total = 0;
    struct iovec *cur;

    if (undo) {
        undo->modified_iov = NULL;
    }

    for (cur = *iov; *iov_cnt > 0; cur++) {
        if (cur->iov_len > bytes) {
            if (undo) {
                undo->modified_iov = cur;
                undo->orig = *cur;
            }

            cur->iov_base += bytes;
            cur->iov_len -= bytes;
            total += bytes;
            break;
        }

        bytes -= cur->iov_len;
        total += cur->iov_len;
        *iov_cnt -= 1;
    }

    *iov = cur;
    return total;
}

size_t iov_discard_front(struct iovec **iov, unsigned int *iov_cnt,
                         size_t bytes)
{
    return iov_discard_front_undoable(iov, iov_cnt, bytes, NULL);
}

size_t iov_discard_back_undoable(struct iovec *iov,
                                 unsigned int *iov_cnt,
                                 size_t bytes,
                                 IOVDiscardUndo *undo)
{
    size_t total = 0;
    struct iovec *cur;

    if (undo) {
        undo->modified_iov = NULL;
    }

    if (*iov_cnt == 0) {
        return 0;
    }

    cur = iov + (*iov_cnt - 1);

    while (*iov_cnt > 0) {
        if (cur->iov_len > bytes) {
            if (undo) {
                undo->modified_iov = cur;
                undo->orig = *cur;
            }

            cur->iov_len -= bytes;
            total += bytes;
            break;
        }

        bytes -= cur->iov_len;
        total += cur->iov_len;
        cur--;
        *iov_cnt -= 1;
    }

    return total;
}

size_t iov_discard_back(struct iovec *iov, unsigned int *iov_cnt,
                        size_t bytes)
{
    return iov_discard_back_undoable(iov, iov_cnt, bytes, NULL);
}

void qemu_iovec_discard_back(QEMUIOVector *qiov, size_t bytes)
{
    size_t total;
    unsigned int niov = qiov->niov;

    assert(qiov->size >= bytes);
    total = iov_discard_back(qiov->iov, &niov, bytes);
    assert(total == bytes);

    qiov->niov = niov;
    qiov->size -= bytes;
}
