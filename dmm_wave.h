// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#ifndef DMM_WAVE_H_
#define DMM_WAVE_H_

#include "dmm_event.h"
#include "dmm_types.h"
#include "queue.h"

#define DMM_CURRENT_WAVE()  dmm_current_wave()

int dmm_wave_start();
int dmm_wave_finish();

dmm_id_t dmm_current_wave();

struct dmm_wavefinish {
    struct dmm_event wf_event;

    dmm_id_t wf_waveid;

    /* List of all socket events */
    LIST_ENTRY(dmm_wavefinish) wf_all;
};

typedef struct dmm_wavefinish *dmm_wavefinish_p;

/* Wavefinish public interface */
#define DMM_WAVEFINISH_REF(wf) DMM_EVENT_REF(DMM_WAVEFINISH_EVENT(wf))
#define DMM_WAVEFINISH_UNREF(wf) DMM_EVENT_UNREF(DMM_WAVEFINISH_EVENT(wf))

#define DMM_WAVEFINISH_EVENT(wf) (&((wf)->wf_event))
#define DMM_WAVEFINISH_ID(wf) (DMM_EVENT_ID(DMM_WAVEFINISH_EVENT(wf)))

/*
 * Wavefinish events are created on subscription,
 * no separate creation is needed
 */
int dmm_wavefinish_subscribe(dmm_node_p node);
int dmm_wavefinish_unsubscribe(dmm_wavefinish_p wf, dmm_node_p node);

#endif /* DMM_WAVE_H_ */
