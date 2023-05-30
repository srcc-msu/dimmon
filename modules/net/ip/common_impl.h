// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef NET_IP_COMMON_IMPL_H_
#define NET_IP_COMMON_IMPL_H_

#include <sys/socket.h>

int create_socket(int *out_fd, int domain, int type, int protocol);
int parse_addr(const char *addr, struct sockaddr **sa, socklen_t *len);

#endif  /* NET_IP_COMMON_IMPL_H_ */
