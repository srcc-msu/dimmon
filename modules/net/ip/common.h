// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef NET_IP_COMMON_H_
#define NET_IP_COMMON_H_

struct dmm_msg_netip_createsock {
    int domain;
    int type;
    int protocol;
};

/*
 * Maximum length of a string in host:port notation
 * host is an IPv4 or IPv6 address (NOT a hostname),
 * optionally in brackets ([host]:port)
 * port is a numeric decimal port
 * So the length is
 * IPv6 digits (4 * 8 = 32) + colons in IPv6 (7) +
 * brackets (2) + colon between host and port (1) +
 * port number digits (5) + terminating \0 (1)
 */
enum {DMM_NETIP_MAXADDRLEN = 32 + 7 + 2 + 1 + 5 + 1};

#endif /* NET_IP_COMMON_H_ */
