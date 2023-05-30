// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_SOCKEVENT_TYPES_H_
#define DMM_SOCKEVENT_TYPES_H_

#include "dmm_event_types.h"
#include "queue.h"

struct dmm_sockevent {
    struct dmm_event se_event;
    /* File descriptor to track */
    int se_fd;
    /* Which events are tracked */
    uint32_t se_sockevents;

    /* List of all socket events */
    LIST_ENTRY(dmm_sockevent) se_all;
};

typedef struct dmm_sockevent *dmm_sockevent_p;

/*
 * Socket event types
 * Created following epoll consts but with some simplifications
 */
enum { DMM_SOCKEVENT_IN  = 0x00000001 };
enum { DMM_SOCKEVENT_OUT = 0x00000002 };
enum { DMM_SOCKEVENT_ERR = 0x00000004 };

#endif /* DMM_SOCKEVENT_TYPES_H_ */
