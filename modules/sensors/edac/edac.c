// SPDX-License-Identifier: BSD-2-Clause-Views
// Copyright (c) 2013-2023
//   Research Computing Center Lomonosov Moscow State University

/*
 * This is edac sensor which sends {memory controllers count, corrected
 * errors, uncorrected errors, pci parity errors} on each timer trigger 
 * message
 */

#include <errno.h>
#include <string.h>

#include <edac.h>

#include "dmm_base.h"
#include "dmm_log.h"
#include "dmm_message.h"
#include "sensors.h"

typedef uint64_t sensor_type;
const int num_sensors = 4;

struct pvt_data {
    dmm_hook_p hook;
    edac_handle *edac;
};

static int process_timer_msg(dmm_node_p node)
{
    struct pvt_data *pvt;
    dmm_hook_p hook;
    dmm_data_p data;
    dmm_datanode_p dn;
    struct edac_totals totals;
    edac_handle *edac;
    unsigned int mc_count = 0;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    hook = pvt->hook;
    edac = pvt->edac;
    if (hook != NULL) 
    {
        data = DMM_DATA_CREATE(num_sensors, sizeof (sensor_type));
        if (data == NULL)
    	    return ENOMEM;
	mc_count = edac_mc_count (edac);
	edac_error_totals (edac, &totals);
        dn = DMM_DATA_NODES(data);
        DMM_DN_CREATE (dn, EDAC_MC_COUNT, sizeof (sensor_type));
        *DMM_DN_DATA (dn, sensor_type) = mc_count;
        DMM_DN_ADVANCE (dn);
        DMM_DN_CREATE (dn, EDAC_CORRECTED, sizeof (sensor_type));
        *DMM_DN_DATA (dn, sensor_type) = totals.ce_total;
        DMM_DN_ADVANCE (dn);
        DMM_DN_CREATE (dn, EDAC_UNCORRECTED, sizeof (sensor_type));
        *DMM_DN_DATA (dn, sensor_type) = totals.ue_total;
        DMM_DN_ADVANCE (dn);
        DMM_DN_CREATE (dn, EDAC_PCI_PARITY, sizeof (sensor_type));
        *DMM_DN_DATA (dn, sensor_type) = totals.pci_parity_total;
        DMM_DN_ADVANCE (dn);
        DMM_DN_MKEND(dn);
        DMM_DATA_SEND(data, hook);
        DMM_DATA_UNREF(data);
    }

    return 0;
}

static int ctor(dmm_node_p node)
{
    struct pvt_data *pvt;
    edac_handle *edac;
    pvt = (struct pvt_data *)DMM_MALLOC(sizeof(*pvt));
    if (pvt == NULL)
        return ENOMEM;
    DMM_NODE_SETPRIVATE(node, pvt);

    if (!(edac = edac_handle_create ())) 
    {
	dmm_log (DMM_LOG_ERR, "edac_handle_create: Out of memory!");
	DMM_FREE (pvt);
    	return -1;
    }
    if (edac_handle_init (edac) < 0) 
    {
	dmm_log (DMM_LOG_ERR, "Unable to get EDAC data: %s", edac_strerror (edac));
	DMM_FREE (pvt);
    	return -1;
    }

    pvt->hook = NULL;
    pvt->edac = edac;
    return 0;
}

static void dtor(dmm_node_p node)
{
    struct pvt_data *pvt;
    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(node);
    edac_handle_destroy (pvt->edac);
    DMM_FREE(pvt);
}

static int newhook(dmm_hook_p hook)
{
    struct pvt_data *pvt;

    if (DMM_HOOK_ISIN(hook))
        return EINVAL;

    pvt = (struct pvt_data *)DMM_NODE_PRIVATE(DMM_HOOK_NODE(hook));
    if (pvt->hook != NULL)
        return EEXIST;

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
    "edac",
    ctor,
    dtor,
    NULL,
    rcvmsg,
    newhook,
    rmhook,
    {},
};

DMM_MODULE_DECLARE(&type);
