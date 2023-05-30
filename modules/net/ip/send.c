// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

/*
 * Send data over IP module
 * XXX Works only with dgram sockets
 */
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"
#include "send.h"
#include "common_impl.h"

struct pvt_data {
    int         fd;
    uint32_t    flags;
};

static int connect_socket(int fd, const char *addr)
{
    struct sockaddr *sa;
    socklen_t len;
    int res = 0;

    if ((res = parse_addr(addr, &sa, &len)) != 0)
        return res;

    if (connect(fd, sa, len) < 0)
        res = errno;

    DMM_FREE(sa);
    return res;
}

static int process_createsock_msg(dmm_node_p node, dmm_msg_p msg)
{
    struct dmm_msg_netip_createsock *nc;
    struct pvt_data *pvt;

    assert (msg->cm_type == DMM_MSGTYPE_NETIPSEND && msg->cm_cmd == DMM_MSG_NETIPSEND_CREATESOCK);
    assert (msg->cm_len == sizeof(*nc));

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    nc = DMM_MSG_DATA(msg, struct dmm_msg_netip_createsock);

    if (pvt->fd >= 0)
        return EEXIST;

#if 0
    return create_socket(&pvt->fd, nc->domain, nc->type, nc->protocol);
#else
    /*
     * XXX - Temporarily make socket blocking until proper queueing of data
     * messages is implemented
     */
    int err;

    if ((err = create_socket(&pvt->fd, nc->domain, nc->type, nc->protocol)) != 0) {
        return err;
    } else {
        int flags;
        if ((flags = fcntl(pvt->fd, F_GETFL)) < 0) {
            char errbuf[128], *errmsg;
            err = errno;
            errmsg = strerror_r(err, errbuf, sizeof(errbuf));
            dmm_log(DMM_LOG_ERR, "Can't fcntl(F_GETFL): %s", errmsg);
            return err;
        }
        if (fcntl(pvt->fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
            char errbuf[128], *errmsg;
            err = errno;
            errmsg = strerror_r(err, errbuf, sizeof(errbuf));
            dmm_log(DMM_LOG_ERR, "Can't fcntl(F_SETFL) to clear O_NONBLOCK flag: %s", errmsg);
            return err;
        }

    }
    return 0;
#endif
}

static int send_ctor(dmm_node_p node)
{
    struct pvt_data *pvt;

    dmm_debug("Constructor called for " DMM_PRINODE, DMM_NODEINFO(node));
    if ((pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt))) == NULL) {
        dmm_log(DMM_LOG_ERR, "Cannot allocate memory for private info");
        return ENOMEM;
    }

    pvt->fd = -1;
    pvt->flags = 0;
    DMM_NODE_SETPRIVATE(node, pvt);
    return 0;
}

static void send_dtor(dmm_node_p node)
{
    DMM_FREE(DMM_NODE_PRIVATE(node));
}

static int send_newhook(dmm_hook_p hook)
{
    if (DMM_HOOK_ISOUT(hook))
        return EINVAL;

    return 0;
}

static int send_rcvdata(dmm_hook_p hook, dmm_data_p data)
{
    struct pvt_data *pvt;
    int err;
    char errbuf[128], *errmsg;
    dmm_datanode_p dn;
    size_t len;

    err = 0;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    if (!(pvt->flags & DMM_NETIPSEND_CONNECTED)) {
        err = ENOTCONN;
        goto error;
    }
    for (dn = DMM_DATA_NODES(data); !DMM_DN_ISEND(dn); dn = DMM_DN_NEXT(dn))
        ;
    len = (char *)dn - (char *)DMM_DATA_NODES(data) + sizeof(struct dmm_datanode);
    if (len <= sizeof(struct dmm_datanode)) {
        dmm_log(DMM_LOG_ERR, "Sending empty messages is not allowed");
        err = EBADMSG;
        goto error;
    }

    if (send(pvt->fd, data->da_nodes, len, 0) <= 0) {
        err = errno;
        errmsg = strerror_r(err, errbuf, sizeof(errbuf));
        dmm_log(DMM_LOG_ERR, "Cannot write data: %s", errmsg);
    }

error:
    DMM_DATA_UNREF(data);
    return err;
}

static int send_rcvmsg(dmm_node_p node, dmm_msg_p msg)
{
    struct pvt_data *pvt;
    dmm_msg_p resp;
    int err = 0;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    switch (msg->cm_type) {
    case DMM_MSGTYPE_NETIPSEND:
        switch (msg->cm_cmd) {
        case DMM_MSG_NETIPSEND_CREATESOCK:
            err = process_createsock_msg(node, msg);
            if (err == 0)
                pvt->flags |= DMM_NETIPSEND_HASSOCK;
            resp = DMM_MSG_CREATE_RESP(DMM_NODE_ID(node), msg, 0);
            if (resp != NULL) {
                if (err != 0)
                    msg->cm_flags |= DMM_MSG_ERR;

                DMM_MSG_SEND_ID(msg->cm_src, resp);
            } else
                err = (err != 0) ? err : ENOMEM;
            break;

        case DMM_MSG_NETIPSEND_CONNECT: {
            struct dmm_msg_netipsend_connect *nc;
            nc = DMM_MSG_DATA(msg, struct dmm_msg_netipsend_connect);
            err = connect_socket(pvt->fd, nc->addr);
            if (err == 0)
                pvt->flags |= DMM_NETIPSEND_CONNECTED;
            resp = DMM_MSG_CREATE_RESP(DMM_NODE_ID(node), msg, 0);
            if (resp != NULL) {
                if (err != 0)
                    msg->cm_flags |= DMM_MSG_ERR;

                DMM_MSG_SEND_ID(msg->cm_src, resp);
            } else
                err = (err != 0) ? err : ENOMEM;
            break;
        }

        default:
            err = ENOTSUP;
            break;
        }
        break;

    default:
        err = ENOTSUP;
        break;
    }

    DMM_MSG_FREE(msg);
    return err;
}

struct dmm_type send_type = {
    "net/ip/send",
    send_ctor,
    send_dtor,
    send_rcvdata,
    send_rcvmsg,
    send_newhook,
    NULL,
    {},
};
