// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"

#define INHOOKNAME  "in"

// XXX - SHOULD BE REFACTORED TO GET RID OF HARDCODED CONSTANTS!!!
#define NUM_SENSORS 1
enum {
    DIFFIFBYTESIN = 200,
    DIFFIFPACKETSIN,
    DIFFIFBYTESOUT,
    DIFFIFPACKETSOUT,
    CPULOAD = 500,
    USER_CPU
};
#define DIFFSENSORBASE USER_CPU
#define MAX_INTERFACES 30

// Seconds between dumps
#define DUMPPERIOD 300

struct pvt_data {
    struct timespec lastdump, lastdump_real;
    int n[NUM_SENSORS][MAX_INTERFACES];
    double sum[NUM_SENSORS][MAX_INTERFACES];
    int all_n[NUM_SENSORS];
    double all_sum[NUM_SENSORS];
};

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;
    int s, i;

    dmm_debug("Constructor called for node id %d", DMM_NODE_ID(node));
    if ((pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt))) == NULL) {
        return ENOMEM;
    }
    pvt->lastdump = (struct timespec){0, 0};
    pvt->lastdump_real = (struct timespec){0, 0};
    for (s = 0; s < NUM_SENSORS; ++s)
        for (i = 0; i < MAX_INTERFACES; ++i) {
            pvt->n[s][i] = 0;
            pvt->sum[s][i] = 0.0;
            pvt->all_n[s] = 0;
            pvt->all_sum[s] = 0.0;
        }
    DMM_NODE_SETPRIVATE(node, pvt);
    return 0;
}

static void dtor(dmm_node_p node)
{
    DMM_FREE(DMM_NODE_PRIVATE(node));
}

static int newhook(dmm_hook_p hook)
{
    if (DMM_HOOK_ISIN(hook)) {
        if (strcmp(DMM_HOOK_NAME(hook), INHOOKNAME) != 0)
            return EINVAL;
    } else {
        return EINVAL;
    }
    return 0;
}

static void dump_avgs(struct pvt_data *);

static int rcvdata(dmm_hook_p hook, dmm_data_p data)
{
    (void)hook;
    dmm_datanode_p dn;
    struct timespec now, now_real;
    int s;
    unsigned i;
    struct pvt_data *pvt;

    clock_gettime(CLOCK_MONOTONIC, &now);
    clock_gettime(CLOCK_REALTIME, &now_real);
    pvt = (struct pvt_data *)DMM_HOOK_NODE_PRIVATE(hook);

    if (pvt->lastdump.tv_sec == 0) {
        pvt->lastdump = now;
        pvt->lastdump_real = now_real;
    }
    if (now.tv_sec / DUMPPERIOD != pvt->lastdump.tv_sec / DUMPPERIOD) {
        pvt->lastdump = now;
        pvt->lastdump_real = now_real;
        dump_avgs(pvt);
        for (s = 0; s < NUM_SENSORS; ++s)
            for (i = 0; i < MAX_INTERFACES; ++i) {
                pvt->n[s][i] = 0;
                pvt->sum[s][i] = 0.0;
                pvt->all_n[s] = 0;
                pvt->all_sum[s] = 0.0;
            }
    }
    for (dn = DMM_DATA_NODES(data); !DMM_DN_ISEND(dn); DMM_DN_ADVANCE(dn)) {
        s = dn->dn_sensor - DIFFSENSORBASE;
        for (i = 0; i < DMM_DN_VECSIZE(dn, double); ++i) {
            pvt->n[s][i]++;
            pvt->sum[s][i] += DMM_DN_VECTOR(dn, double)[i];
            pvt->all_n[s]++;
            pvt->all_sum[s] += DMM_DN_VECTOR(dn, double)[i];
        }
    }
    DMM_DATA_UNREF(data);
    return 0;
}

static void dump_avgs(struct pvt_data *pvt)
{
    int s;
    char timestr[128], *eol;

    ctime_r(&(pvt->lastdump_real.tv_sec), timestr);
    eol = strchr(timestr, '\n');
    if (eol != NULL)
        *eol = '\0';

#if 0 // Comment out
    for (s = 0; s < NUM_SENSORS; ++s)
        for (i = 0; i < MAX_INTERFACES; ++i) {
            if (pvt->n[s][i] != 0)
                printf("Timestamp %lld.%09ld, time %s, sensor %d, index %2d avg is %f\n",
                                  (long long)pvt->lastdump_real.tv_sec,
                                  pvt->lastdump_real.tv_nsec,
                                  timestr,
                                  s + DIFFSENSORBASE,
                                  i,
                                  pvt->sum[s][i] / pvt->n[s][i]
                      );
        }
#endif
    for (s = 0; s < NUM_SENSORS; ++s) {
        printf("Timestamp %lld.%09ld, time %s, sensor %d, avg is %f, num val is %d\n",
                (long long)pvt->lastdump_real.tv_sec,
                pvt->lastdump_real.tv_nsec,
                timestr,
                s + DIFFSENSORBASE,
                pvt->all_sum[s] / pvt->all_n[s],
                pvt->all_n[s]
        );
    }
}

static struct dmm_type type = {
    "avgprint",
    ctor,
    dtor,
    rcvdata,
    NULL,
    newhook,
    NULL,
    {},
};

DMM_MODULE_DECLARE(&type);
