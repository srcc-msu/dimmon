// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_SOCKEVENT_H_
#define DMM_SOCKEVENT_H_

#include "dmm_event.h"
#include "dmm_sockevent_types.h"
#include "queue.h"

/* Sockevent public interface */
#define DMM_SOCKEVENT_REF(se) DMM_EVENT_REF(DMM_SOCKEVENT_EVENT(se))
#define DMM_SOCKEVENT_UNREF(se) DMM_EVENT_UNREF(DMM_SOCKEVENT_EVENT(se))

#define DMM_SOCKEVENT_EVENT(se) (&((se)->se_event))
#define DMM_SOCKEVENT_ID(se) (DMM_EVENT_ID(DMM_SOCKEVENT_EVENT(se)))

/*
 * Socket events are created on subscription,
 * no separate creation is needed
 */
int dmm_sockevent_subscribe(int fd, uint32_t events, dmm_node_p node);
int dmm_sockevent_unsubscribe(int fd, dmm_node_p node);

/* XXX - maybe we should remove it later or #include <sys/epoll.h> fully */
struct epoll_event;

int dmm_sockevent_process(struct epoll_event *);

#endif /* DMM_SOCKEVENT_H_ */
