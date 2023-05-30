// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef NET_IP_SEND_H_
#define NET_IP_SEND_H_

#include <sys/types.h>
#include <sys/socket.h>

#include "common.h"

enum {DMM_MSGTYPE_NETIPSEND = 0x8dddef66};

enum {
    DMM_MSG_NETIPSEND_CREATESOCK = 1,
    DMM_MSG_NETIPSEND_CONNECT,
    DMM_MSG_NETIPSEND_SETFLAGS,   // XXX - to implement
    DMM_MSG_NETIPSEND_GETFLAGS,   // XXX - to implement
};

enum {
    DMM_SENDTIMESTAMP = 102,
};

struct dmm_msg_netipsend_connect {
    char addr[DMM_NETIP_MAXADDRLEN];
};

struct dmm_msg_netipsend_setflags {
    uint32_t flags;
};

enum {
    DMM_NETIPSEND_PREPENDTIMESTAMP = 0x00000001,
    DMM_NETIPSEND_HASSOCK          = 0x80000000,
    DMM_NETIPSEND_CONNECTED        = 0x40000000,
};

/* Mask for flag that may be set via SETFLAGS */
enum {
    DMM_NETIPSEND_SETTABLEFLAGS = (DMM_NETIPSEND_PREPENDTIMESTAMP
                                  )
};

#endif /* NET_IP_SEND_H_ */
