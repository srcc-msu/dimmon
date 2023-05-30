// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>

#include "dmm_timer.h"

#include "dmm_message.h"
#include "timespec.h"

static LIST_HEAD(, dmm_timer) timerlist = LIST_HEAD_INITIALIZER(dmm_timer);
/*
 * Timer trigger list. Contains registered timer
 * in trigger order (earliest timer first).
 * Timer after trigger should be re-registered for new trigger time
 *
 * TODO: replace list with more efficient priority queue implementation
 */
static LIST_HEAD(, dmm_timer) timer_trigger_list = LIST_HEAD_INITIALIZER(dmm_timer);

/*
 * If a timer should trigger a little bit (not more than coalesce_interval)
 * in the future, its trigger will be coalesced in one wave with previous timers.
 *
 * XXX This should be configurable on startup
 */
static const struct timespec coalesce_interval = {0, 1000000L};

static void dmm_timer_deregister(dmm_timer_p timer);

static dmm_timer_p dmm_timer_alloc(void) {
    return (dmm_timer_p)DMM_MALLOC(sizeof(struct dmm_timer));
}

static void timer_destructor(dmm_event_p event);

int dmm_timer_create(dmm_timer_p *timerp)
{
    *timerp = NULL;

    if ((*timerp = dmm_timer_alloc()) == NULL) {
        dmm_log(DMM_LOG_CRIT, "Cannot allocate memory for timer");
        return ENOMEM;
    }

    dmm_event_init(&(*timerp)->tm_event);

    (*timerp)->tm_flags = DMM_TIMER_INVALID;
    DMM_TIMER_EVENT(*timerp)->ev_destructor = timer_destructor;

    (*timerp)->tm_next = (struct timespec){0, 0};
    (*timerp)->tm_interval = (struct timespec){0, 0};

    (*timerp)->tm_flags &= ~DMM_TIMER_INVALID;
    LIST_INSERT_HEAD(&timerlist, *timerp, tm_all);

    dmm_debug("Timer #%" PRIuid " created", DMM_TIMER_ID(*timerp));
    return 0;
}

void dmm_timer_rm(dmm_timer_p timer)
{
    timer->tm_flags |= DMM_TIMER_INVALID;
    if (DMM_TIMER_ISREGISTERED(timer))
        dmm_timer_deregister(timer);

    // Unsubscribe all subscribed nodes
    dmm_event_unsubscribeall(DMM_TIMER_EVENT(timer));

    LIST_REMOVE(timer, tm_all);
    dmm_debug("Timer #%" PRIuid " removed", DMM_TIMER_ID(timer));
    // Release last (must be) reference to launch garbage collection
    DMM_TIMER_UNREF(timer);
}

static void timer_destructor(dmm_event_p event)
{
    dmm_timer_p timer = (dmm_timer_p)((char *)event - offsetof(struct dmm_timer, tm_event));
    DMM_FREE(timer);
}

dmm_timer_p dmm_timer_id2ref(dmm_id_t id)
{
    dmm_timer_p timer;

    LIST_FOREACH(timer, &timerlist, tm_all) {
        if (DMM_TIMER_ID(timer) == id)
            break;
    }
    if (timer != NULL && DMM_TIMER_ISVALID(timer))
        DMM_TIMER_REF(timer);

    return timer;
}

/*
 * Insert timer into timer_trigger_list, thus registering timer to trigger
 * Time to trigger is set in timer->tm_next. Registering timer
 * with timer->tm_next == {0, 0} is an error
 */
static void dmm_timer_register(dmm_timer_p timer)
{
    dmm_timer_p t;

    if (!DMM_TIMER_ISVALID(timer))
        return;

    assert(!TIMESPEC_ISZERO(timer->tm_next));
    assert(!DMM_TIMER_ISREGISTERED(timer));

    if (LIST_EMPTY(&timer_trigger_list)) {
        LIST_INSERT_HEAD(&timer_trigger_list, timer, tm_trigger);
    } else if (TIMESPEC_GT(LIST_FIRST(&timer_trigger_list)->tm_next, timer->tm_next)) {
        LIST_INSERT_HEAD(&timer_trigger_list, timer, tm_trigger);
    } else {
        LIST_FOREACH(t, &timer_trigger_list, tm_trigger) {
            if ((LIST_NEXT(t, tm_trigger) == NULL) || TIMESPEC_GT(LIST_NEXT(t, tm_trigger)->tm_next, timer->tm_next))
                break;
        }
        LIST_INSERT_AFTER(t, timer, tm_trigger);
    }
    timer->tm_flags |= DMM_TIMER_REGISTERED;
    DMM_TIMER_REF(timer);
}

/*
 * Remove timer from timer_trigger list. This will make timer not to trigger
 */
static void dmm_timer_deregister(dmm_timer_p timer)
{
    /* Just skip if for some reason timer is not registered */
    if (!DMM_TIMER_ISREGISTERED(timer))
        return;

    LIST_REMOVE(timer, tm_trigger);
    timer->tm_flags &= ~DMM_TIMER_REGISTERED;
    DMM_TIMER_UNREF(timer);
}

/**
 * Set time for timer to trigger
 *   @param next - time when the timer will trigger RELATIVE to current time
 *   @param interval - interval for timer repetitions
 *   @param flags - if DMM_TIMERSET_ABS is set then next is treated as absolute time,
 *              setting next before current time is then an error and EINVAL is returned
 *
 *   if next == {0, 0} then the first timer trigger will occur after interval time
 *   if interval == {0, 0} then timer is one-shot and will be removed after trigger.
 *   Passing both next and interval value of {0, 0} is an error (EINVAL returned, nothing changes)
 */
int dmm_timer_set(dmm_timer_p timer, const struct timespec *next, const struct timespec *interval, uint32_t flags)
{
    struct timespec now;

    if (TIMESPEC_ISZERO(*next)) {
        if (TIMESPEC_ISZERO(*interval))
            return EINVAL;
        if (!(flags & DMM_TIMERSET_CHANGEINTERVALONLY)) {
            if (clock_gettime(CLOCK_REALTIME, &now))
                return errno;

            timer->tm_next = now;
            TIMESPEC_INC(&(timer->tm_next), interval);
        }
        timer->tm_interval = *interval;
    } else {
        timer->tm_next = *next;
        if (!(flags & DMM_TIMERSET_ABS)) {
            /* next is relative to current time */
            if (clock_gettime(CLOCK_REALTIME, &now))
                return errno;

            TIMESPEC_INC(&(timer->tm_next), &now);
        }
        timer->tm_interval = *interval;
    }

    /* Re-register timer so it'll be correctly placed in trigger list */
    if (DMM_TIMER_ISREGISTERED(timer))
        dmm_timer_deregister(timer);

    dmm_timer_register(timer);

    return 0;
}

/*
 * Make timer stop triggering
 */
void dmm_timer_unset(dmm_timer_p timer)
{
    if (DMM_TIMER_ISREGISTERED(timer))
        dmm_timer_deregister(timer);
}

/*
 * Subscribe node to timer, so node will begin to receive control
 * messages every time timer triggers
 */
int dmm_timer_subscribe(dmm_timer_p timer, dmm_node_p node)
{
    if (!DMM_NODE_ISVALID(node))
        return EINVAL;
    if (!DMM_TIMER_ISVALID(timer))
        return EINVAL;

    return dmm_event_checkedsubscribe(DMM_TIMER_EVENT(timer), node);
}

/*
 * Unsubscribe node from timer
 */
int dmm_timer_unsubscribe(dmm_timer_p timer, dmm_node_p node)
{
    return dmm_event_unsubscribe(DMM_TIMER_EVENT(timer), node);
}

static int dmm_timer_trigger(dmm_timer_p timer)
{
    dmm_msg_p msg;

    if (!DMM_TIMER_ISVALID(timer)) {
        DMM_TIMER_UNREF(timer);
        return EINVAL;
    }

    msg = dmm_msg_create(0, DMM_MSG_TIMERTRIGGER, DMM_MSGTYPE_GENERIC, 0, 0, sizeof(struct dmm_msg_timertrigger));
    if (msg == NULL) {
        dmm_log(DMM_LOG_CRIT, "Cannot allocate memory for message");
        return ENOMEM;
    }
    DMM_MSG_DATA(msg, struct dmm_msg_timertrigger)->id = DMM_TIMER_ID(timer);

    dmm_event_sendsubscribed(DMM_TIMER_EVENT(timer), msg);
    return 0;
}

/**
 * Trigger all timers for which it is time to trigger
 * @param force_trigger trigger at least one timer even if its time seems not to come
 */
int dmm_timers_trigger(bool force_trigger)
{
    struct timespec now;

    if(clock_gettime(CLOCK_REALTIME, &now))
        return errno;

    /* Move now to the future to honor coalesce_interval */
    TIMESPEC_INC(&now,  &coalesce_interval);

    dmm_timer_p tm, tmptm;

    LIST_FOREACH_SAFE(tm, &timer_trigger_list, tm_trigger, tmptm) {
        /*
         * Timers which are to be triggered only needed
         * but note force_trigger
         */
        if (!force_trigger && TIMESPEC_GT(tm->tm_next, now))
            break;

        /* We now have a copy of timer from list, REF it so it stays alive till end of function */
        DMM_TIMER_REF(tm);

        dmm_timer_trigger(tm);

        /*
         * Timer could become invalid e.g. because of timer remove request
         * while processing timer trigger.
         */
        if (DMM_TIMER_ISVALID(tm)) {
            dmm_timer_deregister(tm);

            if (!TIMESPEC_ISZERO(tm->tm_interval)) {
                /* Timer is not one-shot, so re-register it */
                TIMESPEC_INC(&(tm->tm_next), &(tm->tm_interval));
                dmm_timer_register(tm);
            }
        }

        DMM_TIMER_UNREF(tm);
        force_trigger = false;
    }

    return 0;
}

/*
 * Return time when first timer in queue is to trigger
 */
int dmm_timers_next(struct timespec *next)
{
    if (LIST_EMPTY(&timer_trigger_list)) {
        return ENOENT;
    }
    *next = LIST_FIRST(&timer_trigger_list)->tm_next;
    return 0;
}
