// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>

#include "common_impl.h"
#include "common.h"
#include "dmm_log.h"
#include "dmm_memman.h"

int create_socket(int *out_fd, int domain, int type, int protocol)
{
    int fd;
    int err = 0;
    char errbuf[128], *errmsg;
    int optval;

    if ((fd = socket(domain,
                     type | SOCK_NONBLOCK | SOCK_CLOEXEC,
                     protocol
                    )) < 0) {
        err = errno;
        errmsg = strerror_r(err, errbuf, sizeof(errbuf));
        dmm_log(DMM_LOG_ERR, "Cannot create socket: %s", errmsg);
        goto errexit;
    }
    optval = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        err = errno;
        errmsg = strerror_r(err, errbuf, sizeof(errbuf));
        dmm_log(DMM_LOG_ERR, "Cannot set socket options: %s", errmsg);
        goto errexit;
    }

    *out_fd = fd;
    return 0;

errexit:
    if (fd >= 0) {
        close(fd);
    }
    return err;
}

/**
 * Parse 'host:port' string into sockaddr struct
 * port is a numberic port number
 * host is an IPv4 address, '[IPv4]' or '[IPv6]' (v6 address should be in brackets,
 * for v4 address brackets are optional
 * @param addr - host:port string
 * @param sa - *sa is set to malloc'ed (DMM_MALLOC'ed) struct sockaddr
 * @param len - *len is set to **sa length
 * @return 0 if success, getaddrinfo(3) return code or errno code
 */
int parse_addr(const char *addr, struct sockaddr **sa, socklen_t *len)
{
    char node[DMM_NETIP_MAXADDRLEN], port[DMM_NETIP_MAXADDRLEN];
    const char *p, *host_start;
    size_t host_len;
    struct addrinfo hints;
    struct addrinfo *result;
    int res;

    p = strrchr(addr, ':');
    if (p == NULL)
        return EINVAL;
    // port is to the right of the rightest colon
    strcpy(port, p + 1);
    if (*(p - 1) == ']') {
        // Host is in brackets
        if (*addr == '[') {
            host_start = addr + 1;
            host_len = p - 1 - host_start;
        } else {
            return EINVAL;
        }
    } else {
        host_start = addr;
        host_len = p - host_start;
    }
    strncpy(node, host_start, host_len);
    node[host_len] = '\0';

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = 0;
    hints.ai_flags = AI_NUMERICHOST | AI_NUMERICSERV;
    hints.ai_protocol = 0;

    if ((res = getaddrinfo(node, port, &hints, &result)) != 0) {
        dmm_debug("Cannot parse address string %s: %s", addr, gai_strerror(res));
        return res;
    }
    *len = result->ai_addrlen;
    *sa = (struct sockaddr *)DMM_MALLOC(*len);
    if (*sa == NULL)
        return ENOMEM;
    // XXX Can getaddrinfo return multiple results with AI_NUMERICHOST | AI_NUMERICSERV?
    memcpy(*sa, result->ai_addr, *len);
    freeaddrinfo(result);
    return 0;
}
