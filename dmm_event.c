// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>
#include <stdbool.h>

#include "dmm_base.h"
#include "dmm_event.h"
#include "dmm_log.h"
#include "dmm_message.h"

/* For list of nodes subscribed to the event */
struct dmm_eventnode {
    dmm_node_p en_node;
    TAILQ_ENTRY(dmm_eventnode) en_nodelist;
};

/* For list of events to which the node is subscribed */
struct dmm_nodeevent {
    dmm_event_p ne_event;
    LIST_ENTRY(dmm_nodeevent) ne_eventlist;
};

static dmm_id_t lasteventid = 0;

static inline void dmm_event_refinit(dmm_event_p event)
{
    dmm_refinit(&(event->ev_refs));
}

int dmm_event_init(dmm_event_p event)
{
    /* Initialize reference count */
    dmm_event_refinit(event);
    DMM_EVENT_REF(event);

    event->ev_id = ++lasteventid;

    TAILQ_INIT(&event->ev_nodes);
    event->ev_destructor = NULL;

    return 0;
}

void dmm_node_unsubscribeallevents(dmm_node_p node)
{
    struct dmm_nodeevent *tm, *tmptm;
    LIST_FOREACH_SAFE(tm, &node->nd_events, ne_eventlist, tmptm)
        dmm_event_unsubscribe(tm->ne_event, node);
}

void dmm_event_unsubscribeall(dmm_event_p event)
{
    struct dmm_eventnode *nd, *tempnd;
    TAILQ_FOREACH_SAFE(nd, &event->ev_nodes, en_nodelist, tempnd)
        dmm_event_unsubscribe(event, nd->en_node);
}

static bool dmm_event_issubscribed(dmm_event_p event, dmm_node_p node)
{
    struct dmm_nodeevent *ne;
    LIST_FOREACH(ne, &node->nd_events, ne_eventlist) {
        if (ne->ne_event == event)
            return true;
    }
    return false;
}

/*
 * Subscribe node to event
 * If node is already subscribed to event, do nothing
 */
int dmm_event_checkedsubscribe(dmm_event_p event, dmm_node_p node)
{
    if (dmm_event_issubscribed(event, node)) {
        return 0;
    }
    return dmm_event_subscribe(event, node);
}

/*
 * Subscribe node to event
 * It is error to call dmm_vent_subscribe if node is already
 * subscribed to event, use dmm_event_checkedsubscribe
 */
int dmm_event_subscribe(dmm_event_p event, dmm_node_p node)
{
    struct dmm_eventnode *en = (struct dmm_eventnode *)DMM_MALLOC(sizeof(*en));
    if (en == NULL) {
        dmm_log(DMM_LOG_CRIT, "Cannot allocate memory for eventnode");
        return ENOMEM;
    }
    struct dmm_nodeevent *ne = (struct dmm_nodeevent *)DMM_MALLOC(sizeof(*ne));
    if (ne == NULL) {
        dmm_log(DMM_LOG_CRIT, "Cannot allocate memory for nodeevent");
        DMM_FREE(en);
        return ENOMEM;
    }

    en->en_node = node;
    TAILQ_INSERT_TAIL(&event->ev_nodes, en, en_nodelist);
    DMM_NODE_REF(node);
    ne->ne_event = event;
    LIST_INSERT_HEAD(&node->nd_events, ne, ne_eventlist);
    DMM_EVENT_REF(event);
    return 0;
}

int dmm_event_unsubscribe(dmm_event_p event, dmm_node_p node)
{
    struct dmm_eventnode *en;
    TAILQ_FOREACH(en, &event->ev_nodes, en_nodelist) {
        if (en->en_node == node)
            break;
    }
    if (en == NULL) {
        dmm_debug("Cannot unsubscribe " DMM_PRINODE " from " DMM_PRIEVENT, DMM_NODEINFO(node), DMM_EVENTINFO(event));
        return ENOENT;
    }
    struct dmm_nodeevent *ne;
    LIST_FOREACH(ne, &node->nd_events, ne_eventlist) {
        if (ne->ne_event == event)
            break;
    }
    /*
     * As for now node is definitely one of subscribed to the event
     *  (before we really unsubscribe it)
     * the event should be one of eventss the node is subscribed to.
     * If not - something is broken, so assertion fails.
     */
    assert(ne != NULL);

    TAILQ_REMOVE(&event->ev_nodes, en, en_nodelist);
    DMM_FREE(en);
    LIST_REMOVE(ne, ne_eventlist);
    DMM_FREE(ne);
    DMM_EVENT_UNREF(event);
    DMM_NODE_UNREF(node);
    return 0;
}

/*
 * Send a copy of message msg to all nodes subscribed to event
 */
void dmm_event_sendsubscribed(dmm_event_p event, dmm_msg_p msg)
{
    struct dmm_eventnode *en, *tmp_en;
    dmm_node_p node;
    dmm_msg_p msg_copy;

    TAILQ_FOREACH_SAFE(en, &event->ev_nodes, en_nodelist, tmp_en) {
        node = en->en_node;
        if (!DMM_NODE_ISVALID(node))
            continue;

        if ((msg_copy = DMM_MSG_COPY(msg)) != NULL) {
            DMM_NODE_REF(node);
            dmm_msg_send_ref(node, msg_copy);
        } else {
            dmm_log(DMM_LOG_CRIT, "Cannot create message copy");
        }
    }

    DMM_MSG_FREE(msg);
}
