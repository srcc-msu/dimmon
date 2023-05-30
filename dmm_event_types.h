// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_EVENT_TYPES_H_
#define DMM_EVENT_TYPES_H_

#include "dmm_types.h"
#include "queue.h"

struct dmm_eventnode;

/*
 * Used as a 'base' (in OO-sense) for events.
 */
struct dmm_event {
    dmm_id_t ev_id;

    /* Nodes subscribed to the event */
    TAILQ_HEAD(, dmm_eventnode) ev_nodes;
    dmm_refnum_t ev_refs;

    /* A function to call when the last reference to the event is released */
    void (*ev_destructor)(struct dmm_event *);
};

typedef struct dmm_event *dmm_event_p;

/* fprintf helpers */
#define DMM_PRIEVENT "<event #%" PRIuid ">"
#define DMM_EVENTINFO(event) DMM_EVENT_ID(event)

#endif /* DMM_EVENT_TYPES_H_ */
