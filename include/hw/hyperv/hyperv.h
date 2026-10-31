/*
 * Hyper-V guest/hypervisor interaction
 *
 * Copyright (c) 2015-2018 Virtuozzo International GmbH.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HYPERV_HYPERV_H
#define HW_HYPERV_HYPERV_H

#include "exec/hwaddr.h"
#include "hw/core/cpu.h"
#include "hw/hyperv/hyperv-proto.h"

typedef struct HvSintRoute HvSintRoute;

/*
 * Callback executed in a bottom-half when the status of posting the message
 * becomes known, before unblocking the connection for further messages
 */
typedef void (*HvSintMsgCb)(void *data, int status);


/*
 * Submit a message to be posted in vcpu context.  If the submission succeeds,
 * the status of posting the message is reported via the callback associated
 * with the @sint_route; until then no more messages are accepted.
 */
/*
 * Set event flag @eventno, and signal the SINT if the flag has changed.
 */

/*
 * Handler for messages arriving from the guest via HV_POST_MESSAGE hypercall.
 * Executed in vcpu context.
 */
typedef uint16_t (*HvMsgHandler)(const struct hyperv_post_message_input *msg,
                                 void *data);
/*
 * Associate @handler with the message connection @conn_id, such that @handler
 * is called with @data when the guest executes HV_POST_MESSAGE hypercall on
 * @conn_id.  If @handler is NULL clear the association.
 */
/*
 * Associate @notifier with the event connection @conn_id, such that @notifier
 * is signaled when the guest executes HV_SIGNAL_EVENT hypercall on @conn_id.
 * If @notifier is NULL clear the association.
 */

/*
 * Process HV_POST_MESSAGE hypercall: parse the data in the guest memory as
 * specified in @param, and call the HvMsgHandler associated with the
 * connection on the message contained therein.
 */
/*
 * Process HV_SIGNAL_EVENT hypercall: signal the EventNotifier associated with
 * the connection as specified in @param.
 */

static inline uint32_t hyperv_vp_index(CPUState *cs)
{
    return cs->cpu_index;
}


/*
 * Process HVCALL_RESET_DEBUG_SESSION hypercall.
 */
/*
 * Process HVCALL_RETREIVE_DEBUG_DATA hypercall.
 */
/*
 * Process HVCALL_POST_DEBUG_DATA hypercall.
 */


typedef enum HvSynthDbgMsgType {
    HV_SYNDBG_MSG_CONNECTION_INFO,
    HV_SYNDBG_MSG_SEND,
    HV_SYNDBG_MSG_RECV,
    HV_SYNDBG_MSG_SET_PENDING_PAGE,
    HV_SYNDBG_MSG_QUERY_OPTIONS
} HvDbgSynthMsgType;

typedef struct HvSynDbgMsg {
    HvDbgSynthMsgType type;
    union {
        struct {
            uint32_t host_ip;
            uint16_t host_port;
        } connection_info;
        struct {
            uint64_t buf_gpa;
            uint32_t count;
            uint32_t pending_count;
            bool is_raw;
        } send;
        struct {
            uint64_t buf_gpa;
            uint32_t count;
            uint32_t options;
            uint64_t timeout;
            uint32_t retrieved_count;
            bool is_raw;
        } recv;
        struct {
            uint64_t buf_gpa;
        } pending_page;
        struct {
            uint64_t options;
        } query_options;
    } u;
} HvSynDbgMsg;
typedef uint16_t (*HvSynDbgHandler)(void *context, HvSynDbgMsg *msg);


#endif
