/*
 * Multifd common code
 *
 * Copyright (c) 2019-2020 Red Hat Inc
 *
 * Authors:
 *  Juan Quintela <quintela@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "qemu/iov.h"
#include "qemu/rcu.h"
#include "exec/target_page.h"
#include "system/system.h"
#include "system/ramblock.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "migration/misc.h"
#include "migration.h"
#include "migration-stats.h"
#include "savevm.h"
#include "qemu-file.h"
#include "trace.h"
#include "multifd.h"
#include "options.h"
#include "qemu/yank.h"
#include "io/channel-file.h"
#include "io/channel-socket.h"
#include "yank_functions.h"

typedef struct {
    uint32_t magic;
    uint32_t version;
    unsigned char uuid[16]; /* QemuUUID */
    uint8_t id;
    uint8_t unused1[7];     /* Reserved for future use */
    uint64_t unused2[4];    /* Reserved for future use */
} __attribute__((packed)) MultiFDInit_t;

struct {
    MultiFDSendParams *params;

    /* multifd_send() body is not thread safe, needs serialization */
    QemuMutex multifd_send_mutex;

    /*
     * Global number of generated multifd packets.
     *
     * The core runs on 64-bit hosts only, so a size_t counter is 64 bits.
     */
    retro_atomic_size_t packet_num;
    /*
     * Synchronization point past which no more channels will be
     * created.
     */
    QemuSemaphore channels_created;
    /* send channels ready */
    QemuSemaphore channels_ready;
    /*
     * Have we already run terminate threads.  There is a race when it
     * happens that we got one error while we are exiting.
     * We will use atomic operations.  Only valid values are 0 and 1.
     */
    int exiting;
    /* multifd ops */
    const MultiFDMethods *ops;
} *multifd_send_state;

struct {
    MultiFDRecvParams *params;
    MultiFDRecvData *data;
    /* number of created threads */
    int count;
    /*
     * This is always posted by the recv threads, the migration thread
     * uses it to wait for recv threads to finish assigned tasks.
     */
    QemuSemaphore sem_sync;
    /* global number of generated multifd packets */
    uint64_t packet_num;
    int exiting;
    /* multifd ops */
    const MultiFDMethods *ops;
} *multifd_recv_state;

void multifd_send_data_clear(MultiFDSendData *data)
{
    if (multifd_payload_empty(data)) {
        return;
    }

    switch (data->type) {
    case MULTIFD_PAYLOAD_DEVICE_STATE:
        multifd_send_data_clear_device_state(&data->u.device_state);
        break;
    default:
        /* Nothing to do */
        break;
    }

    data->type = MULTIFD_PAYLOAD_NONE;
}

void multifd_send_data_free(MultiFDSendData *data)
{
    if (!data) {
        return;
    }

    /* This also free's device state payload */
    multifd_send_data_clear(data);

    multifd_ram_payload_free(&data->u.ram);

    g_free(data);
}

static const MultiFDMethods *multifd_ops[MULTIFD_COMPRESSION__MAX] = {};

void multifd_register_ops(int method, const MultiFDMethods *ops)
{
    assert(0 <= method && method < MULTIFD_COMPRESSION__MAX);
    assert(!multifd_ops[method]);
    multifd_ops[method] = ops;
}

/* Fills a RAM multifd packet */
void multifd_send_fill_packet(MultiFDSendParams *p)
{
    MultiFDPacket_t *packet = p->packet;
    uint64_t packet_num;
    bool sync_packet = p->flags & MULTIFD_FLAG_SYNC;

    memset(packet, 0, p->packet_len);

    packet->hdr.magic = cpu_to_be32(MULTIFD_MAGIC);
    packet->hdr.version = cpu_to_be32(MULTIFD_VERSION);

    packet->hdr.flags = cpu_to_be32(p->flags);
    packet->next_packet_size = cpu_to_be32(p->next_packet_size);

    packet_num = retro_atomic_fetch_add_size(&multifd_send_state->packet_num, 1);
    packet->packet_num = cpu_to_be64(packet_num);

    p->packets_sent++;

    if (!sync_packet) {
        multifd_ram_fill_packet(p);
    }

    trace_multifd_send_fill(p->id, packet_num,
                            p->flags, p->next_packet_size);
}

