// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"
#include "sensors.h"

#define DATAFILE "/proc/stat"

static int num_cores = 0;
/*
 * Number of system states provided by Linux (depends on Linux version)
 * See /proc/file description in man 5 proc
 */
static int num_states = 0;

struct pvt_data {
    FILE *f;
    dmm_hook_p hook;
    uint64_t *prev_val;
    bool prev_val_filled;
};

/*
 * Count number of cores and number of states
 * provided by kernel in /proc/stat file
 * Is called on first attempt to get values
 * or when number of cores changes.
 */
static void count_cores_states(dmm_node_p node)
{
    struct pvt_data *pvt;
    FILE *f;
    /* buf size is 3 (cpu) + (up to 10 states * 2^64 max value (20 chars)) */
    char buf[256];
    char *s, *s1;
    int tmp;
    int cores, states;
    const char cpu_prefix[] = "cpu";

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    f = pvt->f;
    /* Do not check IO errors. Assume /proc/stat is always readable (except eof) */
    fflush(f);
    rewind(f);
    if (fgets(buf, sizeof (buf), f) != buf)
        goto err;
    /*
     * Check for
     * cpu %d ..(4 to 10 times).. %d\n
     */
    s = buf;
    if (strncmp(s, cpu_prefix, strlen(cpu_prefix)) != 0)
        goto err;
    s += strlen(cpu_prefix);
    if (!isspace(*s))
        goto err;
    for (states = 0; *s != '\n'; states++) {
        while (*s != '\n' && isspace(*s) && s < buf + sizeof buf)
            s++;
        errno = 0;
        strtoul(s, &s1, 10);
        if (s1 == s || errno != 0)
            goto err;
        s = s1;
    }
    num_states = states;
    /*
     * OK, states are counted, now count cores.
     * Each core should have a line beginning with
     * cpu<core num> (no space between "cpu" and number
     */
    for (cores = 0;; cores++) {
        if (fgets(buf, sizeof (buf), f) != buf)
            goto err;
        s = buf;
        if (strncmp(s, cpu_prefix, strlen(cpu_prefix)) != 0)
            break;
        s += strlen(cpu_prefix);
        if (!isdigit(*s))
            goto err;
        if (sscanf(s , "%d", &tmp) < 1)
            goto err;
/* Seems that this check may cause problems */
# if 0
        /* Check that cores are numbered sequentially */
        if (tmp != cores)
            goto err;
#endif
        while (*s != '\0' && *s != '\n' && s < buf + sizeof buf)
            s++;
        if (*s != '\n') {
            /* Line in files is longer than buf, skip the remainder */
            while (!feof(f) && fgetc(f) != '\n')
                ;
        }
    }
    num_cores = cores;
    return;

err:
    dmm_log(DMM_LOG_ERR, "Can't parse %s", DATAFILE);
    num_states = 0;
    num_cores = 0;
}

static int fill_prev(dmm_node_p node, bool numcore_changed)
{
    struct pvt_data *pvt;
    FILE *f;
    int core, state;
    char buf[32];
    int tmp;

    assert(num_states > 0);
    assert(num_cores > 0);

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    f = pvt->f;
    pvt->prev_val_filled = false;
    do {
        if (pvt->prev_val != NULL && numcore_changed) {
            DMM_FREE(pvt->prev_val);
            pvt->prev_val = NULL;
        }
        if (pvt->prev_val == NULL) {
            pvt->prev_val = (uint64_t *)DMM_MALLOC(num_cores * num_states * sizeof (*pvt->prev_val));
        }
        if (pvt->prev_val == NULL) {
            dmm_log(DMM_LOG_ERR, "Can't allocate memory for previous values");
            return ENOMEM;
        }
        numcore_changed = false;
        fflush(f);
        rewind(f);
        /* Skip first line (aggregate stats) */
        assert(fscanf(f, "cpu%*[^\n]%*c") == 0);
        for (core = 0; core < num_cores; core++) {
            /* 31 = sizeof(buf) - 1 */
            assert(fscanf(f, "%31s", buf) == 1);
            if (strncmp(buf, "cpu", 3) != 0) {
                /* OOPS, number of cores is now less than it was */
                count_cores_states(node);
                numcore_changed = true;
                continue;
            }
            if (sscanf(buf + 3, "%d", &tmp) < 1/* || tmp != core*/) {
                /* OOPS, it's not "cpu<number> line or <number> != core */
                count_cores_states(node);
                numcore_changed = true;
                continue;
            }
            for (state = 0; state < num_states; state++)
                assert(fscanf(f, "%" SCNu64, &pvt->prev_val[core * num_states + state]) == 1);
            assert(fscanf(f, "%*1[\n]") == 0);
        }
        /* 31 = sizeof(buf) - 1 */
        assert(fscanf(f, "%31s", buf) == 1);
        if (strncmp(buf, "cpu", 3) == 0) {
            /* OOPS, more cores is here now */
            count_cores_states(node);
            numcore_changed = true;
            continue;
        }
        pvt->prev_val_filled = true;
    } while (!pvt->prev_val_filled);
    return 0;
}

static int process_timer_msg(dmm_node_p node)
{
    struct pvt_data *pvt;
    FILE *f;
    uint64_t cur_val[num_cores * num_states];
    uint64_t total_jiffies_delta;
    char buf[32];
    int core, state;
    int tmp;
    dmm_datanode_p dn;
    dmm_data_p data;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    data = NULL;
    f = pvt->f;
    fflush(f);
    rewind(f);
    /* Skip first line (aggregate stats) */
    assert(fscanf(f, "cpu%*[^\n]%*c") == 0);
    if (pvt->prev_val_filled && pvt->hook != NULL) {
        /* We have previous data, so ready to send */
        data = DMM_DATA_CREATE(num_states, num_cores * sizeof(float));
        if (data == NULL) {
            return ENOMEM;
        }
        dn = DMM_DATA_NODES(data);
        for (state = 0; state < num_states; state++)
            DMM_DN_CREATE_ADVANCE(dn, CPU_USER + state, num_cores * sizeof(float));
        DMM_DN_MKEND(dn);
        /* XXX - if num_cores is changed in another thread, this may be a problem */
        for (core = 0; core < num_cores; core++) {
            total_jiffies_delta = 0;
            /* 31 == sizeof(buf) - 1 */
            assert(fscanf(f, "%31s", buf) == 1);
            if (strncmp(buf, "cpu", 3) != 0) {
                /* OOPS, number of cores is now less than it was */
                goto numcore_changed;
            }
            if (sscanf(buf + 3, "%d", &tmp) < 1) {
                /* OOPS, it's not "cpu<number> line */
                goto numcore_changed;
            }

            for (state = 0; state < num_states; state++) {
                int64_t cur_val_delta;

                assert(fscanf(f, "%" SCNu64, &cur_val[core * num_states + state]) == 1);
                cur_val_delta = cur_val[core * num_states + state] - pvt->prev_val[core * num_states + state];

                /* proc(5) says that counter for iowait state can decrease, check for it */
                if (cur_val_delta < 0) {
                    cur_val_delta = 0;
                }

                total_jiffies_delta += cur_val_delta;
            }
            if (total_jiffies_delta == 0) {
                /* This should not happen, but it does, so check for it */
                dmm_log(DMM_LOG_ERR, "total_jiffies == 0 for core %d, return 0 for all states (instead of NaN)", core);
                for (state = 0; state < num_states; state++) {
                    dmm_log(DMM_LOG_ERR, "state: %d, prev_val: %" PRIu64 ", cur_val: %" PRIu64,
                            state,
                            pvt->prev_val[core * num_states + state],
                            cur_val[core * num_states + state]
                           );
                }
                /* As total_jiffies_delta == 0, all counters are equal to their
                 * previous state, so all returned values will be 0 for
                 * any total_jiffies_delta value except 0
                 */
                total_jiffies_delta = 1;
            }
            dn = DMM_DATA_NODES(data);
            for (state = 0; state < num_states; state++) {
                int64_t cur_val_delta;

                cur_val_delta = cur_val[core * num_states + state] - pvt->prev_val[core * num_states + state];
                /* proc(5) says that counter for iowait state can decrease, check for it */
                if (cur_val_delta < 0) {
                    cur_val_delta = 0;
                }

                DMM_DN_DATA(dn, float)[core] =
                    (float)(cur_val_delta) / total_jiffies_delta;
                DMM_DN_ADVANCE(dn);
                pvt->prev_val[core * num_states + state] = cur_val[core * num_states + state];
            }
            assert(fscanf(f, "%*1[\n]") == 0);
        }
        /* 31 = sizeof(buf) - 1 */
        assert(fscanf(f, "%31s", buf) == 1);
        if (strncmp(buf, "cpu", 3) == 0) {
            /* OOPS, more cores is here now */
            goto numcore_changed;
        }
        DMM_DATA_SEND(data, pvt->hook);
        DMM_DATA_UNREF(data);
    } else {
        /* No previous data, or no hook to send. Just fill prev_val */
        return fill_prev(node, false);
    }
    return 0;

numcore_changed:
    DMM_DATA_UNREF(data);
    count_cores_states(node);
    return fill_prev(node, true);
}

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;
    int err;

    pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt));
    if (pvt == NULL)
        return ENOMEM;
    DMM_NODE_SETPRIVATE(node, pvt);

    pvt->f = fopen(DATAFILE, "re");
    if (pvt->f == NULL) {
        char errbuf[128], *errmsg;
        err = errno;
        errmsg = strerror_r(err, errbuf, sizeof(errbuf));
        dmm_log(DMM_LOG_ERR, "Cannot open %s for reading: %s", DATAFILE, errmsg);
        DMM_FREE(pvt);
        return err;
    }
    // Disable buffering to have fresh data on successive reads
    setlinebuf(pvt->f);
    if (num_states == 0)
        count_cores_states(node);
    if (num_states == 0) {
        fclose(pvt->f);
        DMM_FREE(pvt);
        return EINVAL;
    }
    pvt->hook = NULL;
    pvt->prev_val_filled = false;
    pvt->prev_val = NULL;
    return 0;
}

static void dtor(dmm_node_p node)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    fclose(pvt->f);
    DMM_FREE(pvt);
}

static int newhook(dmm_hook_p hook)
{
    struct pvt_data *pvt;

    if (DMM_HOOK_ISIN(hook))
        return EINVAL;
    if (strcmp("out", DMM_HOOK_NAME(hook)) != 0)
        return EINVAL;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    pvt->hook = hook;

    return 0;
}

static void rmhook(dmm_hook_p hook)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    pvt->hook = NULL;
}

static int rcvmsg(dmm_node_p node, dmm_msg_p msg)
{
    int err = 0;

    /* Accept only generic messages */
    if (msg->cm_type != DMM_MSGTYPE_GENERIC) {
        err = ENOTSUP;
        goto err;
    }
    /* Accept only TIMERTRIGGER messages */
    if (msg->cm_cmd != DMM_MSG_TIMERTRIGGER) {
        err = ENOTSUP;
        goto err;
    }
    err = process_timer_msg(node);

err:
    DMM_MSG_FREE(msg);
    return err;

}
static struct dmm_type type = {
    "cpuload",
    ctor,
    dtor,
    NULL,
    rcvmsg,
    newhook,
    rmhook,
    {},
};

DMM_MODULE_DECLARE(&type);
