// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>

#include "dmm_base_internals.h"
#include "dmm_log.h"
#include "dmm_message.h"
#include "dmm_sockevent.h"

static LIST_HEAD(, dmm_sockevent) sockeventlist = LIST_HEAD_INITIALIZER(sockeventlist);

static dmm_sockevent_p dmm_sockevent_alloc(void)
{
    return (dmm_sockevent_p)DMM_MALLOC(sizeof(struct dmm_sockevent));
}

static dmm_sockevent_p dmm_sockevent_fd2ref(int fd)
{
    dmm_sockevent_p se;
    LIST_FOREACH(se, &sockeventlist, se_all)
        if (se->se_fd == fd) {
            DMM_SOCKEVENT_REF(se);
            break;
        }

    return se;
}

static void sockevent_destructor(dmm_event_p event);

/*
 * Translate events from dmm_msg_sockeventsubscribe to epoll events
 */
static int se_ev_2_epoll_ev(int sev)
{
    int epoll_events = 0;
    if (sev & DMM_SOCKEVENT_IN)
        epoll_events |= EPOLLIN;
    if (sev & DMM_SOCKEVENT_OUT)
        epoll_events |= EPOLLOUT;
    return epoll_events;
}

/*
 * Translate events from epoll_wait to events for dmm_msg_sockeventtrigger
 */
static int epoll_ev_2_se_ev(int epoll_events)
{
    int sev = 0;
    if (epoll_events & EPOLLIN)
        sev |= DMM_SOCKEVENT_IN;
    if (epoll_events & EPOLLOUT)
        sev |= DMM_SOCKEVENT_OUT;
    if (epoll_events & ~(EPOLLIN | EPOLLOUT))
        sev |= DMM_SOCKEVENT_ERR;
    return sev;
}

int dmm_sockevent_subscribe(int fd, uint32_t events, dmm_node_p node)
{
    struct epoll_event ev;
    dmm_sockevent_p se;
    int err = 0;
    char errbuf[128], *errmsg;

    if ((se = dmm_sockevent_alloc()) == NULL)
        return ENOMEM;

    ev.data.ptr = se;
    ev.events = se_ev_2_epoll_ev(events);

    if (epoll_ctl(dmm_epollfd, EPOLL_CTL_ADD, fd, &ev) == 0) {
        /* New fd came, register event for it */
        dmm_event_init(DMM_SOCKEVENT_EVENT(se));
        se->se_fd = fd;
        se->se_sockevents = events;
        DMM_SOCKEVENT_EVENT(se)->ev_destructor = sockevent_destructor;
        LIST_INSERT_HEAD(&sockeventlist, se, se_all);
        err = dmm_event_subscribe(DMM_SOCKEVENT_EVENT(se), node);
        /*
         * We UNREF se not to count sockeventlist membership as reference,
         * so last unsubscribe will launch sockevent destructor which will
         * remove it from sockeventlist and free memory.
         */
        DMM_SOCKEVENT_UNREF(se);
        dmm_debug("Create new sockevent for fd %d", fd);
    } else {
        err = errno;
        DMM_FREE(se);
        if (err != EEXIST) {
            errmsg = strerror_r(err, errbuf, sizeof(errbuf));
            dmm_log(DMM_LOG_ERR, "epoll_ctl ADD failed for fd %d: %s", fd, errmsg);
            return err;
        }
        /*
         * err == EEXIST, so
         * the fd has already been registered, find event and subscribe
         */
        se = dmm_sockevent_fd2ref(fd);
        assert(se != NULL);
        if (se->se_sockevents != events) {
            /* Events in new request differ from what was set before, modify events */
            /* Previous se was free'ed, so fill in the right one */
            ev.data.ptr = se;
            se->se_sockevents = events;
            if (epoll_ctl(dmm_epollfd, EPOLL_CTL_MOD, fd, &ev)) {
                err = errno;
                DMM_SOCKEVENT_UNREF(se);
                errmsg = strerror_r(err, errbuf, sizeof(errbuf));
                dmm_log(DMM_LOG_ERR, "epoll_ctl ADD failed for fd %d: %s", fd, errmsg);
                return err;
            }
            dmm_debug("Change sockevents on existing sockevent for fd %d", fd);
        }
        err = dmm_event_checkedsubscribe(DMM_SOCKEVENT_EVENT(se), node);
        DMM_SOCKEVENT_UNREF(se);
        dmm_debug("Subscribe to existing sockevent for fd %d", fd);
    }

    if (err) {
        errmsg = strerror_r(err, errbuf, sizeof(errbuf));
        dmm_debug("Can't subscribe to fd %d: %s", fd, errmsg);
    }

    return err;
}

int dmm_sockevent_unsubscribe(int fd, dmm_node_p node)
{
    dmm_sockevent_p se;
    int err = 0;
    se = dmm_sockevent_fd2ref(fd);
    if (se == NULL)
        return ENOENT;

    err = dmm_event_unsubscribe(DMM_SOCKEVENT_EVENT(se), node);
    DMM_SOCKEVENT_UNREF(se);
    return err;
}

static void sockevent_destructor(dmm_event_p event)
{
    dmm_sockevent_p se = (dmm_sockevent_p)((char *)event - offsetof(struct dmm_sockevent, se_event));
    LIST_REMOVE(se, se_all);
    if (epoll_ctl(dmm_epollfd, EPOLL_CTL_DEL, se->se_fd, NULL)) {
        assert(errno == ENOENT);
        dmm_debug("fd %d is gone from epoll before last unsubscribe", se->se_fd);
    }
    DMM_FREE(se);
}

/*
 * @param epoll_events what epoll_wait returned as events
 */
static int dmm_sockevent_trigger(dmm_sockevent_p se, uint32_t epoll_events)
{
    dmm_msg_p msg;
    struct dmm_msg_sockeventtrigger *set;

    dmm_debug("Socket event triggered for fd %d", se->se_fd);

    msg = DMM_MSG_CREATE(0, DMM_MSG_SOCKEVENTTRIGGER, DMM_MSGTYPE_GENERIC, 0, 0, sizeof(struct dmm_msg_sockeventtrigger));
    if (msg == NULL) {
        DMM_SOCKEVENT_UNREF(se);
        return ENOMEM;
    }
    set = DMM_MSG_DATA(msg, struct dmm_msg_sockeventtrigger);
    set->fd = se->se_fd;
    set->events = epoll_ev_2_se_ev(epoll_events);

    dmm_event_sendsubscribed(DMM_SOCKEVENT_EVENT(se), msg);

    return 0;
}

int dmm_sockevent_process(struct epoll_event *ev)
{
    dmm_sockevent_p se;
    se = (dmm_sockevent_p)(ev->data.ptr);
    return dmm_sockevent_trigger(se, ev->events);
}
