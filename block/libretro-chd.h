/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef BLOCK_LIBRETRO_CHD_H
#define BLOCK_LIBRETRO_CHD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct LRCHD LRCHD;
typedef struct LRCHDRequest LRCHDRequest;
#define LR_CHD_SLOTS 8
#define LR_CHD_MAX_READ (1024 * 1024)

/* Source ownership transfers only on successful open. read is a whole,
 * positional read returning 0 or -errno. A view, if offered, stays stable
 * through source close. After open, only the decoder worker accesses it.
 * close runs after that worker joins; decoded views do not borrow the file. */
typedef struct LRCHDSource {
    void *opaque;
    uint64_t size;
    int (*read)(void *, uint64_t, void *, size_t);
    const uint8_t *(*view)(void *, uint64_t, size_t);
    void (*close)(void *);
} LRCHDSource;

/* Lifecycle only: open parses metadata synchronously, then starts one owner.
 * Initially accepts self-contained cooked DVD images (unit 2048, no tracks or
 * external parent). No partially decoded/unsupported layout is mounted. */
LRCHD *lr_chd_open(const LRCHDSource *source, int *error);
uint64_t lr_chd_size(const LRCHD *chd);
/* Lifecycle only: stops/joins the worker. Accepted requests remain pollable
 * and releasable after close, including decoded spans already handed out. */
void lr_chd_close(LRCHD *chd);

/* One serialized producer/consumer owner, plus the private worker. Submit,
 * poll and release never wait or decode, allocate payloads, copy payloads,
 * call file/VFS functions or invoke completion callbacks. Exhaustion returns
 * -EAGAIN immediately. Queue capacity and payload storage are fixed at open.
 * Request lengths must be 1..LR_CHD_MAX_READ; larger I/O must be chunked. */
int lr_chd_submit(LRCHD *chd, uint64_t offset, size_t len, LRCHDRequest **out);
/* 0 pending; 1 ready success; negative errno on completed error. Successful
 * bytes are 64-byte-aligned and immutable until release. The caller dispatches guest completion. */
int lr_chd_poll(LRCHDRequest *request, const void **data, size_t *len);
/* Exactly once. Pending release abandons its result without waiting; its
 * slot is reusable only after the worker has finished touching the payload.
 * No request pointer may be used again after release. */
void lr_chd_release(LRCHDRequest *request);
#endif
