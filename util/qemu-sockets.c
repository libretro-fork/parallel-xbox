/*
 *  inet and unix socket functions for qemu
 *
 *  (c) 2008 Gerd Hoffmann <kraxel@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; under version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "qemu/osdep.h"

#ifdef CONFIG_AF_VSOCK
#include <linux/vm_sockets.h>
#endif /* CONFIG_AF_VSOCK */

#include "monitor/monitor.h"
#include "qapi/clone-visitor.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-sockets.h"
#include "qemu/sockets.h"
#include "qemu/main-loop.h"
#include "qapi/qobject-input-visitor.h"
#include "qapi/qobject-output-visitor.h"
#include "qemu/cutils.h"
#include "qemu/option.h"
#include "trace.h"

#ifndef AI_ADDRCONFIG
# define AI_ADDRCONFIG 0
#endif

#ifndef AI_V4MAPPED
# define AI_V4MAPPED 0
#endif

#ifndef AI_NUMERICSERV
# define AI_NUMERICSERV 0
#endif

/*
 * On macOS TCP_KEEPIDLE is available under a different name, TCP_KEEPALIVE.
 * https://github.com/apple/darwin-xnu/blob/xnu-4570.1.46/bsd/man/man4/tcp.4#L172
 */
#if defined(TCP_KEEPALIVE) && !defined(TCP_KEEPIDLE)
# define TCP_KEEPIDLE TCP_KEEPALIVE
#endif



#ifdef _WIN32
#define QEMU_SOCKET_RC_INPROGRESS(rc) \
    ((rc) == -EINPROGRESS || (rc) == -EWOULDBLOCK || (rc) == -WSAEALREADY)
#else
#define QEMU_SOCKET_RC_INPROGRESS(rc) \
    ((rc) == -EINPROGRESS)
#endif

static QemuOptsList inet_opts = {
    .name = "InetSocketAddress",
    .head = QTAILQ_HEAD_INITIALIZER(inet_opts.head),
    .implied_opt_name = "addr",
    .desc = {
        {
            .name = "addr",
            .type = QEMU_OPT_STRING,
        },
        {
            .name = "numeric",
            .type = QEMU_OPT_BOOL,
        },
        {
            .name = "to",
            .type = QEMU_OPT_NUMBER,
        },
        {
            .name = "ipv4",
            .type = QEMU_OPT_BOOL,
        },
        {
            .name = "ipv6",
            .type = QEMU_OPT_BOOL,
        },
        {
            .name = "keep-alive",
            .type = QEMU_OPT_BOOL,
        },
#ifdef HAVE_TCP_KEEPCNT
        {
            .name = "keep-alive-count",
            .type = QEMU_OPT_NUMBER,
        },
#endif
#ifdef HAVE_TCP_KEEPIDLE
        {
            .name = "keep-alive-idle",
            .type = QEMU_OPT_NUMBER,
        },
#endif
#ifdef HAVE_TCP_KEEPINTVL
        {
            .name = "keep-alive-interval",
            .type = QEMU_OPT_NUMBER,
        },
#endif
#ifdef HAVE_IPPROTO_MPTCP
        {
            .name = "mptcp",
            .type = QEMU_OPT_BOOL,
        },
#endif
        { /* end of list */ }
    },
};


#ifdef CONFIG_AF_VSOCK

#else
static void vsock_unsupported(Error **errp)
{
    error_setg(errp, "socket family AF_VSOCK unsupported");
}

bool fd_is_socket(int fd)
{
    int optval;
    socklen_t optlen = sizeof(optval);
    return !getsockopt(fd, SOL_SOCKET, SO_TYPE, &optval, &optlen);
}

static int vsock_connect_saddr(VsockSocketAddress *vaddr, Error **errp)
{
    vsock_unsupported(errp);
    return -1;
}

static int vsock_listen_saddr(VsockSocketAddress *vaddr,
                              int num,
                              Error **errp)
{
    vsock_unsupported(errp);
    return -1;
}
#endif /* CONFIG_AF_VSOCK */

void socket_listen_cleanup(int fd, Error **errp)
{
    SocketAddress *addr;

    addr = socket_local_address(fd, errp);
    if (!addr) {
        return;
    }

    if (addr->type == SOCKET_ADDRESS_TYPE_UNIX
        && addr->u.q_unix.path) {
        if (unlink(addr->u.q_unix.path) < 0 && errno != ENOENT) {
            error_setg_errno(errp, errno,
                             "Failed to unlink socket %s",
                             addr->u.q_unix.path);
        }
    }

    qapi_free_SocketAddress(addr);
}


static SocketAddress *
socket_sockaddr_to_address_inet(struct sockaddr_storage *sa,
                                socklen_t salen,
                                Error **errp)
{
    char host[NI_MAXHOST];
    char serv[NI_MAXSERV];
    SocketAddress *addr;
    InetSocketAddress *inet;
    int ret;

    ret = getnameinfo((struct sockaddr *)sa, salen,
                      host, sizeof(host),
                      serv, sizeof(serv),
                      NI_NUMERICHOST | NI_NUMERICSERV);
    if (ret != 0) {
        error_setg(errp, "Cannot format numeric socket address: %s",
                   gai_strerror(ret));
        return NULL;
    }

    addr = g_new0(SocketAddress, 1);
    addr->type = SOCKET_ADDRESS_TYPE_INET;
    inet = &addr->u.inet;
    inet->host = g_strdup(host);
    inet->port = g_strdup(serv);
    if (sa->ss_family == AF_INET) {
        inet->has_ipv4 = inet->ipv4 = true;
    } else {
        inet->has_ipv6 = inet->ipv6 = true;
    }

    return addr;
}


static SocketAddress *
socket_sockaddr_to_address_unix(struct sockaddr_storage *sa,
                                socklen_t salen,
                                Error **errp)
{
    SocketAddress *addr;
    struct sockaddr_un *su = (struct sockaddr_un *)sa;

    addr = g_new0(SocketAddress, 1);
    addr->type = SOCKET_ADDRESS_TYPE_UNIX;
    salen -= offsetof(struct sockaddr_un, sun_path);
#ifdef CONFIG_LINUX
    if (salen > 0 && !su->sun_path[0]) {
        /* Linux abstract socket */
        addr->u.q_unix.path = g_strndup(su->sun_path + 1, salen - 1);
        addr->u.q_unix.has_abstract = true;
        addr->u.q_unix.abstract = true;
        addr->u.q_unix.has_tight = true;
        addr->u.q_unix.tight = salen < sizeof(su->sun_path);
        return addr;
    }
#endif

    addr->u.q_unix.path = g_strndup(su->sun_path, salen);
    return addr;
}

#ifdef CONFIG_AF_VSOCK
static SocketAddress *
socket_sockaddr_to_address_vsock(struct sockaddr_storage *sa,
                                 socklen_t salen,
                                 Error **errp)
{
    SocketAddress *addr;
    VsockSocketAddress *vaddr;
    struct sockaddr_vm *svm = (struct sockaddr_vm *)sa;

    addr = g_new0(SocketAddress, 1);
    addr->type = SOCKET_ADDRESS_TYPE_VSOCK;
    vaddr = &addr->u.vsock;
    vaddr->cid = g_strdup_printf("%u", svm->svm_cid);
    vaddr->port = g_strdup_printf("%u", svm->svm_port);

    return addr;
}
#endif /* CONFIG_AF_VSOCK */

SocketAddress *
socket_sockaddr_to_address(struct sockaddr_storage *sa,
                           socklen_t salen,
                           Error **errp)
{
    switch (sa->ss_family) {
    case AF_INET:
    case AF_INET6:
        return socket_sockaddr_to_address_inet(sa, salen, errp);

    case AF_UNIX:
        return socket_sockaddr_to_address_unix(sa, salen, errp);

#ifdef CONFIG_AF_VSOCK
    case AF_VSOCK:
        return socket_sockaddr_to_address_vsock(sa, salen, errp);
#endif

    default:
        error_setg(errp, "socket family %d unsupported",
                   sa->ss_family);
        return NULL;
    }
    return 0;
}


SocketAddress *socket_local_address(int fd, Error **errp)
{
    struct sockaddr_storage ss;
    socklen_t sslen = sizeof(ss);

    if (getsockname(fd, (struct sockaddr *)&ss, &sslen) < 0) {
        error_setg_errno(errp, errno, "%s",
                         "Unable to query local socket address");
        return NULL;
    }

    return socket_sockaddr_to_address(&ss, sslen, errp);
}


