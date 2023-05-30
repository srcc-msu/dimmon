// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef NET_IP_RECV_H_
#define NET_IP_RECV_H_

#include <sys/types.h>
#include <sys/socket.h>

#include "common.h"

enum {DMM_MSGTYPE_NETIPRECV  = 0x89c0202};

enum {
    DMM_MSG_NETIPRECV_CREATESOCK = 1,
    DMM_MSG_NETIPRECV_BIND,
    DMM_MSG_NETIPRECV_SETBUFLEN,  // XXX - to implement
    DMM_MSG_NETIPRECV_SETFLAGS,
    DMM_MSG_NETIPRECV_GETFLAGS,
};

enum {
    DMM_SRCHOST         = 100,
    DMM_RCVDTIMESTAMP,
};

struct dmm_msg_netiprecv_bind {
    char addr[DMM_NETIP_MAXADDRLEN];
};

struct dmm_msg_netiprecv_setflags {
    uint32_t flags;
};

struct dmm_msg_netiprecv_getflags_resp {
    uint32_t flags;
};

enum {
    DMM_NETIPRECV_NOCHECKDATA      = 0x00000001,
    DMM_NETIPRECV_PREPENDADDR      = 0x00000002,
    DMM_NETIPRECV_PREPENDTIMESTAMP = 0x00000002,
    /* Auxiliary flags set by module itself */
    DMM_NETIPRECV_HASSOCK          = 0x80000000,
    DMM_NETIPRECV_BOUND            = 0x40000000,
};

/* Mask for flag that may be set via SETFLAGS */
enum {
    DMM_NETIPRECV_SETTABLEFLAGS  = (DMM_NETIPRECV_NOCHECKDATA    |
                                    DMM_NETIPRECV_PREPENDADDR    |
                                    DMM_NETIPRECV_PREPENDTIMESTAMP
                                   )
};

#endif /* NET_IP_RECV_H_ */
