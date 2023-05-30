// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

/*
 * Receive data over IP module
 * XXX Works only with dgram sockets
 */

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"
#include "dmm_sockevent.h"

#include "recv.h"
#include "common_impl.h"

/* Default receive buffer length */
#define DMM_NETIPRECV_DEFAULTBUFLEN 65507

struct pvt_data {
    int         fd;
    dmm_hook_p  outhook;
    void       *buf;
    size_t      buflen;
    uint32_t    flags;
};

static uint32_t last_token = 0;

#define GET_TOKEN() (++last_token)

static bool check_data_valid(void *data, size_t len)
{
    dmm_datanode_p dn;
    ssize_t slen;

    if (len < sizeof(struct dmm_datanode)) {
        dmm_log(DMM_LOG_WARN, "Received short message");
        return false;
    }
    for (dn = (dmm_datanode_p)data, slen = len; !DMM_DN_ISEND(dn) && len > 0; DMM_DN_ADVANCE(dn))
        slen -= sizeof(struct dmm_datanode) + dn->dn_len;

     if (slen <= 0) {
         dmm_log(DMM_LOG_WARN, "Received message: bad data structure");
         return false;
     }
     return true;
}

static int process_createsock_msg(dmm_node_p node, dmm_msg_p msg)
{
    struct dmm_msg_netip_createsock *nc;
    struct pvt_data *pvt;

    assert (msg->cm_type == DMM_MSGTYPE_NETIPRECV && msg->cm_cmd == DMM_MSG_NETIPRECV_CREATESOCK);
    assert (msg->cm_len == sizeof(*nc));

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    nc = DMM_MSG_DATA(msg, struct dmm_msg_netip_createsock);

    if (pvt->fd >= 0)
        return EEXIST;

    return create_socket(&pvt->fd, nc->domain, nc->type, nc->protocol);
}

static int bind_socket(int fd, const char *addr)
{
    struct sockaddr *sa;
    socklen_t len;
    int res = 0;

    if ((res = parse_addr(addr, &sa, &len)) != 0)
        return res;

    if (bind(fd, sa, len) < 0)
        res = errno;

    DMM_FREE(sa);
    return res;
}

static int process_bind_msg(dmm_node_p node, dmm_msg_p msg)
{
    struct dmm_msg_netiprecv_bind *nb;
    struct pvt_data *pvt;
    dmm_msg_p ses; // ses is socket event subscribe message
    int err;

    assert (msg->cm_type == DMM_MSGTYPE_NETIPRECV && msg->cm_cmd == DMM_MSG_NETIPRECV_BIND);

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    nb = DMM_MSG_DATA(msg, struct dmm_msg_netiprecv_bind);
    err = bind_socket(pvt->fd, nb->addr);
    if (err != 0)
        goto finish;
    ses = DMM_MSG_CREATE(DMM_NODE_ID(node),
                         DMM_MSG_SOCKEVENTSUBSCRIBE,
                         DMM_MSGTYPE_GENERIC,
                         GET_TOKEN(),
                         0,
                         sizeof(struct dmm_msg_sockeventsubscribe)
                        );
    if (ses == NULL) {
        err = ENOMEM;
        goto finish;
    }
    DMM_MSG_DATA(ses, struct dmm_msg_sockeventsubscribe)->fd = pvt->fd;
    DMM_MSG_DATA(ses, struct dmm_msg_sockeventsubscribe)->events = DMM_SOCKEVENT_IN;
    err = DMM_MSG_SEND_ID(DMM_NODE_ID(node), ses);

finish:
    return err;
}

static int process_socket_event(dmm_node_p node, uint32_t events)
{
    struct pvt_data *pvt;
    struct msghdr   r_msg;
    struct iovec    iov;
    ssize_t         bytes_recvd;
    dmm_size_t      datalen;
    size_t          numnodes;
    char            errbuf[128], *errmsg;
    dmm_data_p      data;
    dmm_datanode_p  dn;
    struct timespec now;
    struct sockaddr_storage ss;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);

    if ((events & ~DMM_SOCKEVENT_IN) != 0) {
        /* Not a read event on read socket */
        dmm_log(DMM_LOG_WARN,
                "Node " DMM_PRINODE ": Received socket event is not DMM_SOCKEVENT_IN for fd %d",
                DMM_NODEINFO(node), pvt->fd
               );
        return EINVAL;
    }
    memset(&r_msg, 0, sizeof(r_msg));
    r_msg.msg_name = &ss;
    r_msg.msg_namelen = sizeof(ss);
    iov.iov_base = pvt->buf;
    iov.iov_len = pvt->buflen;
    r_msg.msg_iov = &iov;
    r_msg.msg_iovlen = 1;

    if ((bytes_recvd = recvmsg(pvt->fd, &r_msg, 0)) < 0) {
        errmsg = strerror_r(errno, errbuf, sizeof(errbuf));
        dmm_log(DMM_LOG_WARN,
                "Node " DMM_PRINODE ": Can't read from socket %d: %s",
                DMM_NODEINFO(node), pvt->fd, errmsg
               );
        return 0;
    }
    if ((pvt->flags & DMM_NETIPRECV_NOCHECKDATA) ||
        !check_data_valid(pvt->buf, bytes_recvd)
       ) {
        dmm_log(DMM_LOG_WARN,
                "Node " DMM_PRINODE ": received invalid data",
                DMM_NODEINFO(node)
               );
        return 0;
    }

    if (pvt->outhook != NULL) {
        /* Allocate memory and copy data only
         * if we have hook to send data via
         * Otherwise just discard data (message is read anyway)
         */
        datalen = bytes_recvd;
        numnodes = 0;
        if (pvt->flags & DMM_NETIPRECV_PREPENDADDR) {
            datalen += r_msg.msg_namelen;
            numnodes++;
        }
        if (pvt->flags & DMM_NETIPRECV_PREPENDTIMESTAMP) {
            datalen += sizeof(struct timespec);
            numnodes++;
        }
        if ((data = DMM_DATA_CREATE_RAW(numnodes, datalen)) == NULL) {
            dmm_log(DMM_LOG_ERR,
                    "Node " DMM_PRINODE ": can't allocate memory for data",
                    DMM_NODEINFO(node)
                   );
            return ENOMEM;
        }

        dn = DMM_DATA_NODES(data);
        if (pvt->flags & DMM_NETIPRECV_PREPENDADDR)
            DMM_DN_FILL_ADVANCE(dn, DMM_SRCHOST, r_msg.msg_namelen, &ss);
        if (pvt->flags & DMM_NETIPRECV_PREPENDTIMESTAMP) {
            clock_gettime(CLOCK_REALTIME, &now);
            DMM_DN_FILL_ADVANCE(dn, DMM_RCVDTIMESTAMP, sizeof(now), &now);
        }
        memcpy(dn, pvt->buf, bytes_recvd);
        DMM_DATA_SEND(data, pvt->outhook);
        DMM_DATA_UNREF(data);
    }
    return 0;
}

static int recv_ctor(dmm_node_p node)
{
    struct pvt_data *pvt;

    dmm_debug("Constructor called for " DMM_PRINODE, DMM_NODEINFO(node));
    if ((pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt))) == NULL) {
        dmm_log(DMM_LOG_ERR, "Cannot allocate memory for private info");
        return ENOMEM;
    }
    pvt->buflen = DMM_NETIPRECV_DEFAULTBUFLEN;
    if ((pvt->buf = DMM_MALLOC(pvt->buflen)) == NULL) {
        dmm_log(DMM_LOG_ERR, "Cannot allocate memory for receive buffer");
        DMM_FREE(pvt);
        return ENOMEM;
    }

    pvt->fd = -1;
    pvt->outhook = NULL;
    pvt->flags = 0;
    DMM_NODE_SETPRIVATE(node, pvt);
    return 0;
}

static void recv_dtor(dmm_node_p node)
{
    DMM_FREE(DMM_NODE_PRIVATE(node));
}

static int recv_newhook(dmm_hook_p hook)
{
    struct pvt_data *pvt;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));

    if (DMM_HOOK_ISIN(hook))
        return EINVAL;
    if (pvt->outhook != NULL)
        return EEXIST;

    pvt->outhook = hook;

    return 0;
}

static void recv_rmhook(dmm_hook_p hook)
{
    struct pvt_data *pvt;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    assert(hook == pvt->outhook);
    pvt->outhook = NULL;
}

static int recv_rcvmsg(dmm_node_p node, dmm_msg_p msg)
{
    struct pvt_data *pvt;
    dmm_msg_p resp;
    int err = 0;

    if (msg->cm_flags & DMM_MSG_RESP)
        goto finish;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    switch (msg->cm_type) {
    case DMM_MSGTYPE_GENERIC:
        switch (msg->cm_cmd) {
        case DMM_MSG_SOCKEVENTTRIGGER: {
            struct dmm_msg_sockeventtrigger *se;
            se = DMM_MSG_DATA(msg, struct dmm_msg_sockeventtrigger);
            if (pvt->fd != se->fd) {
                dmm_log(DMM_LOG_WARN,
                        "Node " DMM_PRINODE ": Received socket event for fd %d, "
                        "our fd is %d",
                        DMM_NODEINFO(node), se->fd, pvt->fd
                       );
                err = EINVAL;
                break;
            }
            err = process_socket_event(node, se->events);
            break;
        }

        default:
            err = ENOTSUP;
            break;
        }
        break;

    case DMM_MSGTYPE_NETIPRECV:

#define CREATE_SEND_EMPTY_RESP()                                    \
        do {                                                        \
            resp = DMM_MSG_CREATE_RESP(DMM_NODE_ID(node), msg, 0);  \
            if (resp != NULL) {                                     \
                if (err != 0)                                       \
                    msg->cm_flags |= DMM_MSG_ERR;                   \
                                                                    \
                DMM_MSG_SEND_ID(msg->cm_src, resp);                 \
            } else                                                  \
                err = (err != 0) ? err : ENOMEM;                    \
        } while (0)

        switch (msg->cm_cmd) {
        case DMM_MSG_NETIPRECV_CREATESOCK:
            err = process_createsock_msg(node, msg);
            if (err == 0)
                pvt->flags |= DMM_NETIPRECV_HASSOCK;
            CREATE_SEND_EMPTY_RESP();
            break;

        case DMM_MSG_NETIPRECV_BIND:
            err = process_bind_msg(node, msg);
            if (err == 0)
                pvt->flags |= DMM_NETIPRECV_BOUND;
            CREATE_SEND_EMPTY_RESP();
            break;

        case DMM_MSG_NETIPRECV_GETFLAGS:
            assert(msg->cm_len == 0);

            resp = DMM_MSG_CREATE_RESP(DMM_NODE_ID(node), msg, sizeof(struct dmm_msg_netiprecv_getflags_resp));
            if (resp != NULL) {
                DMM_MSG_DATA(resp, struct dmm_msg_netiprecv_getflags_resp)->flags = pvt->flags;
                DMM_MSG_SEND_ID(msg->cm_src, resp);
            } else
                err = ENOMEM;
            break;

        case DMM_MSG_NETIPRECV_SETFLAGS: {
            int flags;
            assert(msg->cm_len == sizeof(struct dmm_msg_netiprecv_setflags));

            flags = DMM_MSG_DATA(msg, struct dmm_msg_netiprecv_setflags)->flags;
            if ((flags & ~DMM_NETIPRECV_SETTABLEFLAGS) != 0)
                err = EINVAL;
            else {
                /* Auxiliary flags should be kept */
                pvt->flags = (   pvt->flags
                              & ~DMM_NETIPRECV_SETTABLEFLAGS
                             )
                             | DMM_MSG_DATA(msg, struct dmm_msg_netiprecv_setflags)->flags;
            }
            CREATE_SEND_EMPTY_RESP();
            break;
        }

        default:
            err = ENOTSUP;
            break;
        }
#undef CREATE_SEND_EMPTY_RESP

        break;

    default:
        err = ENOTSUP;
        break;
    }

finish:
    DMM_MSG_FREE(msg);
    return err;

}

struct dmm_type recv_type = {
    "net/ip/recv",
    recv_ctor,
    recv_dtor,
    NULL,
    recv_rcvmsg,
    recv_newhook,
    recv_rmhook,
    {},
};
