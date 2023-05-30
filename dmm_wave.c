// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>
#include <string.h>

#include "dmm_log.h"
#include "dmm_message.h"
#include "dmm_wave.h"

static dmm_id_t dmm_wave_id = 0;

static void dmm_wavefinish_rm(dmm_wavefinish_p wf);
static void wavefinish_destructor(dmm_event_p event);
static dmm_wavefinish_p dmm_wavefinish_id2ref(dmm_id_t id);

int dmm_wave_start()
{
    ++dmm_wave_id;
    dmm_debug("New wave #%" PRIdid " started", dmm_wave_id);
    return 0;
}

int dmm_wave_finish()
{
    dmm_wavefinish_p wf;
    dmm_msg_p msg;
    int err;

    err = 0;
    wf = dmm_wavefinish_id2ref(DMM_CURRENT_WAVE());
    if (wf != NULL) {
        msg = DMM_MSG_CREATE(0, DMM_MSG_WAVEFINISH, DMM_MSGTYPE_GENERIC, 0, 0, 0);
        if (msg != NULL)
            dmm_event_sendsubscribed(DMM_WAVEFINISH_EVENT(wf), msg);
        else
            err = ENOMEM;

        dmm_event_unsubscribeall(DMM_WAVEFINISH_EVENT(wf));
        dmm_wavefinish_rm(wf);
    }
    dmm_debug("Wave #%" PRIdid " finished", DMM_CURRENT_WAVE());
    return err;
}

dmm_id_t dmm_current_wave()
{
    return dmm_wave_id;
}

static LIST_HEAD(, dmm_wavefinish) wavefinishlist = LIST_HEAD_INITIALIZER(wavefinishlist);

static dmm_wavefinish_p dmm_wavefinish_alloc(void)
{
    return (dmm_wavefinish_p)DMM_MALLOC(sizeof(struct dmm_wavefinish));
}

static dmm_wavefinish_p dmm_wavefinish_id2ref(dmm_id_t id)
{
    dmm_wavefinish_p wf;
    LIST_FOREACH(wf, &wavefinishlist, wf_all)
        if (wf->wf_waveid == id) {
            DMM_WAVEFINISH_REF(wf);
            break;
        }

    return wf;
}

int dmm_wavefinish_subscribe(dmm_node_p node)
{
    dmm_wavefinish_p wf;
    dmm_id_t curwaveid;
    int err = 0;
    char errbuf[128], *errmsg;

    curwaveid = DMM_CURRENT_WAVE();

    if ((wf = dmm_wavefinish_id2ref(curwaveid)) != NULL) {
        err = dmm_event_subscribe(DMM_WAVEFINISH_EVENT(wf), node);
        DMM_WAVEFINISH_UNREF(wf);
        if (!err)
            dmm_debug("Subscribe to existing wavefinish event for wave #%" PRIdid, curwaveid);
    } else {
        if ((wf = dmm_wavefinish_alloc()) == NULL)
            return ENOMEM;

        dmm_event_init(DMM_WAVEFINISH_EVENT(wf));
        wf->wf_waveid = curwaveid;
        DMM_WAVEFINISH_EVENT(wf)->ev_destructor = wavefinish_destructor;
        LIST_INSERT_HEAD(&wavefinishlist,wf , wf_all);
        err = dmm_event_subscribe(DMM_WAVEFINISH_EVENT(wf), node);
        /*
         * We UNREF wf not to count wavefinishlist membership as reference,
         * so last unsubscribe will launch wavefinish destructor which will
         * remove it from wavefinishlist and free memory.
         */
        DMM_WAVEFINISH_UNREF(wf);
        dmm_debug("Create new wavefinish event for wave #%" PRIdid, curwaveid);
    }

    if (err) {
        errmsg = strerror_r(err, errbuf, sizeof(errbuf));
        dmm_debug("Can't subscribe to wave #%" PRIdid ": %s", curwaveid, errmsg);
    }

    return err;
}

int dmm_wavefinish_unsubscribe(dmm_wavefinish_p wf, dmm_node_p node)
{
    return dmm_event_unsubscribe(DMM_WAVEFINISH_EVENT(wf), node);
}

static void wavefinish_destructor(dmm_event_p event)
{
    dmm_wavefinish_p wf = (dmm_wavefinish_p)event;
    LIST_REMOVE(wf, wf_all);
    DMM_FREE(wf);
}

static void dmm_wavefinish_rm(dmm_wavefinish_p wf)
{
    dmm_event_unsubscribeall(DMM_WAVEFINISH_EVENT(wf));

    LIST_REMOVE(wf, wf_all);
    dmm_debug("Wavefinish event for wave #%" PRIuid " removed", wf->wf_waveid);
    // Release last (must be) reference to launch garbage collection
    DMM_WAVEFINISH_UNREF(wf);
}
