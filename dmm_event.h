// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_EVENT_H_
#define DMM_EVENT_H_

#include "dmm_base_decl.h"
#include "dmm_event_types.h"
#include "dmm_memman.h"
#include "dmm_message_decl.h"
#include "queue.h"

#define DMM_EVENT_REF(event) dmm_event_ref(event)
#define DMM_EVENT_UNREF(event) dmm_event_unref(event)

#define DMM_EVENT_ID(event) ((event)->ev_id + 0)

int dmm_event_init(dmm_event_p event);

int dmm_event_checkedsubscribe(dmm_event_p event, dmm_node_p node);
int dmm_event_subscribe(dmm_event_p event, dmm_node_p node);
int dmm_event_unsubscribe(dmm_event_p event, dmm_node_p node);
void dmm_event_unsubscribeall(dmm_event_p event);
void dmm_node_unsubscribeallevents(dmm_node_p node);
void dmm_event_sendsubscribed(dmm_event_p event, dmm_msg_p msg);

static inline void dmm_event_ref(dmm_event_p event)
{
    dmm_refacquire(&(event->ev_refs));
}

/*
 * Decrements reference count
 * Call event destructor if last reference was released
 */
static inline void dmm_event_unref(dmm_event_p event)
{
    if (dmm_refrelease(&(event->ev_refs))) {
        // No references to timer exists, so no subscribed nodes
        assert(TAILQ_EMPTY(&(event->ev_nodes)));
        assert(event->ev_destructor != NULL);
        event->ev_destructor(event);
    }
}

#endif /* DMM_EVENT_H_ */
