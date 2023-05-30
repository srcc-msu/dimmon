// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_TIMER_H_
#define DMM_TIMER_H_

#include <stdbool.h>
#include <time.h>

#include "dmm_base_decl.h"
#include "dmm_event.h"
#include "dmm_log.h"
#include "dmm_memman.h"
#include "dmm_types.h"
#include "queue.h"

struct dmm_timer {
    struct dmm_event tm_event;

    uint32_t tm_flags;
    /*
     * tm_next - next absolute time when the timer will trigger
     * tm_interval - period of timer recurrence
     *   if tm_interval is {0, 0} then the timer is one-shot
     *     and tm_next will be the last timer trigger, after which
     *     timer should be deleted
     */
    struct timespec tm_next;
    struct timespec tm_interval;

    /* Internal DMM structures */
    LIST_ENTRY(dmm_timer) tm_all; /* List of all timers, timerlist in dmm_timer.c is the head */
    /* Priority queue (list for now) of timers, timer_trigger_list in dmm_timer.c is the head */
    LIST_ENTRY(dmm_timer) tm_trigger;

};

typedef struct dmm_timer *dmm_timer_p;

/* Timer flags bits */
#define DMM_TIMER_INVALID    0x00000001
#define DMM_TIMER_REGISTERED 0x00000002

/* Flags for dmm_timer_set */
#define DMM_TIMERSET_ABS            0x00000001
#define DMM_TIMERSET_CHANGEINTERVALONLY 0x00000002

/* Timer public interface */
#define DMM_TIMER_REF(timer) DMM_EVENT_REF(DMM_TIMER_EVENT(timer))
#define DMM_TIMER_UNREF(timer) DMM_EVENT_UNREF(DMM_TIMER_EVENT(timer))

#define DMM_TIMER_EVENT(timer) (&((timer)->tm_event))
#define DMM_TIMER_ID(timer) (DMM_EVENT_ID(DMM_TIMER_EVENT(timer)))
#define DMM_TIMER_ISVALID(timer) (!((timer)->tm_flags & DMM_TIMER_INVALID))
#define DMM_TIMER_ISREGISTERED(timer)  ((timer)->tm_flags & DMM_TIMER_REGISTERED)

/* fprintf helpers */
#define DMM_PRITIMER "<timer #%" PRIuid ">"
#define DMM_TIMERINFO(timer) DMM_TIMER_ID(timer)

/* Implementation functions, not for direct use */

static inline void dmm_timer_free(dmm_timer_p timer)
{
    DMM_FREE(timer);
}

int dmm_timer_create(dmm_timer_p *timerp);
void dmm_timer_rm(dmm_timer_p timer);
int dmm_timer_set(dmm_timer_p timer, const struct timespec *next, const struct timespec *interval, uint32_t flags);
void dmm_timer_unset(dmm_timer_p timer);
int dmm_timer_subscribe(dmm_timer_p timer, dmm_node_p node);
int dmm_timer_unsubscribe(dmm_timer_p timer, dmm_node_p node);
dmm_timer_p dmm_timer_id2ref(dmm_id_t id);

int dmm_timers_trigger(bool force_trigger);
int dmm_timers_next(struct timespec *next);

#endif /* DMM_TIMER_H_ */
