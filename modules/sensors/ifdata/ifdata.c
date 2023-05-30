// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"

#define DATAFILE "/proc/net/dev"
#define HOOKNAME "out"

static int num_interfaces = 0;
struct pvt_data {
    FILE *f;
    dmm_hook_p hook;
};
typedef uint64_t ifcounter_t;
#define SCNifcounter SCNu64

#define NUM_SENSORS 4

enum {
    IFBYTESIN = 100,
    IFPACKETSIN,
    IFBYTESOUT,
    IFPACKETSOUT
};

/*
 * Try to open data file to ensure it exists and readable
 * Count interfaces
 */
static int init()
{
    FILE *f;
    int err;
    char errbuf[128], *errmsg;
    int c, i;
    char tempc;

    f = fopen(DATAFILE, "re");
    if (f == NULL) {
        err = errno;
        errmsg = strerror_r(err, errbuf, sizeof(errbuf));
        dmm_log(DMM_LOG_ERR, "Cannot open %s for reading: %s", DATAFILE, errmsg);
        return err;
    }
    // Skip first two lines (header)
    for (i = 0; i < 2; ++i) {
        while ((c = fgetc(f)) != '\n' && c != EOF)
            ;
    }
    while (fscanf(f, "%*[^:]%c", &tempc) > 0) {
        num_interfaces++;
        while ((c = fgetc(f)) != '\n' && c != EOF)
            ;
    }
    dmm_debug("ifdatda: found %d interfaces", num_interfaces);

    fclose(f);
    return 0;
}

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;

    pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt));
    if (pvt == NULL)
        return ENOMEM;
    pvt->f = fopen(DATAFILE, "re");
    if (pvt->f == NULL) {
        int err;
        char errbuf[128], *errmsg;
        err = errno;
        errmsg = strerror_r(err, errbuf, sizeof(errbuf));
        dmm_log(DMM_LOG_ERR, "Cannot open %s for reading: %s", DATAFILE, errmsg);
        return err;
    }
    // Disable buffering to have fresh data on successive reads
    setlinebuf(pvt->f);
    pvt->hook = NULL;
    DMM_NODE_SETPRIVATE(node, pvt);
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
    if (strcmp(HOOKNAME, DMM_HOOK_NAME(hook)) != 0)
        return EINVAL;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    pvt->hook = hook;

    return 0;
}

static void rmhook(dmm_hook_p hook)
{
    (void)hook;
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    pvt->hook = hook;
}

static int rcvmsg(dmm_node_p node, dmm_msg_p msg)
{
    dmm_data_p data;
    dmm_datanode_p dn;
    struct pvt_data *pvt;
    int i, c;
    FILE *f;
    ifcounter_t bytes_in[num_interfaces],
                packets_in[num_interfaces],
                bytes_out[num_interfaces],
                packets_out[num_interfaces];

    // We believe all messages are timer messages, so ignore
    DMM_MSG_FREE(msg);

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    // Nowhere to send, so why bother?
    if (pvt->hook == NULL)
        return 0;

    f = pvt->f;
    if ((data = DMM_DATA_CREATE(NUM_SENSORS * num_interfaces, sizeof(ifcounter_t))) == NULL) {
        dmm_log(DMM_LOG_ERR, "Cannot allocate memory for data");
        return ENOMEM;
    }
    rewind(f);
    fflush(f);

    // Skip first two lines (header)
    for (i = 0; i < 2; ++i) {
        while ((c = fgetc(f)) != '\n')
            if (c == EOF)
                goto errexit;
    }

    dn = DMM_DATA_NODES(data);
    for (i = 0; i < num_interfaces; ++i) {
// Silence scanf format warnings
#pragma GCC diagnostic ignored "-Wformat"
        if (fscanf(f,
                "%*[^:]:"
                "%" SCNifcounter
                "%" SCNifcounter
                "%*" SCNifcounter
                "%*" SCNifcounter
                "%*" SCNifcounter
                "%*" SCNifcounter
                "%*" SCNifcounter
                "%*" SCNifcounter
                "%" SCNifcounter
                "%" SCNifcounter
                "%*" SCNifcounter
                "%*" SCNifcounter
                "%*" SCNifcounter
                "%*" SCNifcounter
                "%*" SCNifcounter
                "%*" SCNifcounter,
                bytes_in + i, packets_in + i, bytes_out + i, packets_out + i
            ) < 4
           ) {
            goto errexit;
        }
#pragma GCC diagnostic error "-Wformat"
    }
    DMM_DN_FILL_ADVANCE(dn, IFBYTESIN, sizeof(bytes_in), bytes_in);
    DMM_DN_FILL_ADVANCE(dn, IFPACKETSIN, sizeof(packets_in), packets_in);
    DMM_DN_FILL_ADVANCE(dn, IFBYTESOUT, sizeof(bytes_out), bytes_out);
    DMM_DN_FILL_ADVANCE(dn, IFPACKETSOUT, sizeof(packets_out), packets_out);
    DMM_DN_MKEND(dn);
    DMM_DATA_SEND(data, pvt->hook);
    DMM_DATA_UNREF(data);

    return 0;

errexit:
    DMM_FREE(data);
    return EINVAL;
}

static struct dmm_type type = {
    "ifdata",
    ctor,
    dtor,
    NULL,
    rcvmsg,
    newhook,
    rmhook,
    {},
};

DMM_MODULE_DECLARE(&type);
DMM_MODULEINIT_DECLARE(init);
